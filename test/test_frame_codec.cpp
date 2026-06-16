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
    CHECK_FALSE(parse_cts(shortbuf).has_value());        // len < 3
    std::array<uint8_t, 5> longbuf{0x27, 0x11, 0x2A, 0x10, 0x00};
    CHECK_FALSE(parse_cts(longbuf).has_value());         // len not in {3,4}
    // 4-byte CTS is valid: byte 3 = payload_len (NAV). Roundtrip pack(4)/parse.
    std::array<uint8_t, 4> p4{};
    CHECK(pack_cts({7, false, 1, 2, /*payload_len=*/30}, p4) == 4);
    auto rp = parse_cts(p4);
    CHECK(rp.has_value());
    if (rp) { CHECK(rp->payload_len == 30); CHECK(rp->chosen_data_sf == 7); CHECK(rp->tx_id == 1); CHECK(rp->rx_id == 2); }
    auto r3 = parse_cts(cts);                            // a 3-byte CTS still parses, payload_len defaults 0
    CHECK(r3.has_value());
    if (r3) CHECK(r3->payload_len == 0);

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
                for (uint8_t ttl : {0, 1, 16, 255})
                    for (bool hard : {false, true}) {
                        std::array<uint8_t, 8> buf{};
                        CHECK(pack_h({leaf, origin, kh, ttl, hard}, buf) == 8);
                        auto o = parse_h(buf);
                        CHECK(o.has_value());
                        if (o) {
                            CHECK(o->leaf_id == leaf);   CHECK(o->origin == origin);
                            CHECK(o->key_hash32 == kh);  CHECK(o->ttl == ttl);
                            CHECK(o->hard == hard);      // byte 7 H flags
                        }
                    }
    std::array<uint8_t, 8> buf{};
    CHECK(pack_h({3, 0x2A, 0xDEADBEEFu, 0x10, /*hard=*/true}, buf) == 8);
    const uint8_t ex[] = {0x73, 0x2A, 0xEF, 0xBE, 0xAD, 0xDE, 0x10, 0x01};   // key_hash32 LE; byte 7 = flags (HARD)
    for (int i = 0; i < 8; ++i) CHECK(buf[i] == ex[i]);
    std::array<uint8_t, 5> sh{0x73, 0x2A, 0xEF, 0xBE, 0xAD};
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
                        std::array<uint8_t, 7> buf{};
                        CHECK(pack_f({leaf, origin, rep, 0x2A, b4, hops, /*relay=*/0x55}, buf) == 7);
                        auto o = parse_f(buf);
                        CHECK(o.has_value());
                        if (o) {
                            CHECK(o->leaf_id == leaf); CHECK(o->origin == origin);
                            CHECK(o->is_reply == rep); CHECK(o->dst_id == 0x2A);
                            CHECK(o->ttl_or_next_hop == b4); CHECK(o->hops == hops);
                            CHECK(o->relay == 0x55);
                        }
                    }
    std::array<uint8_t, 7> rreq{}, rrep{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 0, /*relay=*/0x07}, rreq) == 7);   // RREQ: byte4 = ttl 8, byte6 = relay 7
    const uint8_t exq[] = {0x83, 0x11, 0x00, 0x2A, 0x08, 0x00, 0x07};
    for (int i = 0; i < 7; ++i) CHECK(rreq[i] == exq[i]);
    CHECK(pack_f({3, 0x11, true, 0x2A, 9, 4, /*relay=*/0x07}, rrep) == 7);    // RREP: byte4 = next_hop 9
    const uint8_t exr[] = {0x83, 0x11, 0x80, 0x2A, 0x09, 0x04, 0x07};
    for (int i = 0; i < 7; ++i) CHECK(rrep[i] == exr[i]);
    // is_reply isolation: same everything else → only byte2 bit 7 flips.
    std::array<uint8_t, 7> a{}, b{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 4, /*relay=*/0x07}, a) == 7);
    CHECK(pack_f({3, 0x11, true,  0x2A, 8, 4, /*relay=*/0x07}, b) == 7);
    CHECK((a[2] ^ b[2]) == 0x80);
    CHECK(a[0] == b[0]); CHECK(a[1] == b[1]); CHECK(a[3] == b[3]);
    CHECK(a[4] == b[4]); CHECK(a[5] == b[5]); CHECK(a[6] == b[6]);
    std::array<uint8_t, 6> sh{0x83, 0x11, 0x00, 0x2A, 0x08, 0x00};
    CHECK_FALSE(parse_f(sh).has_value());                 // len < 7
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
// DATA — cmd 0x3 (C6). 8-B §10 header + opaque inner + opaque 4-B MAC.
// Golden hex hand-derived from §10.3; field meaning matches the Lua data plane.
// -----------------------------------------------------------------------------
TEST_CASE("DATA — golden NORMAL DM (full flags byte; no APP/TYPE byte; no payload-flags byte)") {
    const uint8_t inner[] = {0x07, 0xAA, 0xBB};   // [origin=7][body=AA BB] — NO payload-flags byte
    const uint8_t mac[]   = {0, 0, 0, 0};
    data_in in{};
    in.addr_len = 0; in.flags = 0; in.next = 0x0B; in.dst = 0x0C;
    in.hops_remaining = 10; in.committed_hops = 2; in.prev_fwd_rt_hops = 3;
    in.ctr = 0x1234; in.inner = inner; in.mac = mac;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 15);   // 8 header + 3 inner + 4 MAC (no TYPE byte; APP=0)
    const uint8_t ex[] = {0x30, 0x00, 0x0B, 0x0C, 0x52, 0x03, 0x34, 0x12,
                          0x07, 0xAA, 0xBB, 0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 15; ++i) CHECK(buf[i] == ex[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_data(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->addr_len == 0);
        CHECK(o->flags == 0);
        CHECK_FALSE(o->app); CHECK(o->type == 0);
        CHECK(o->inner_off == 8);
        CHECK(o->next == 0x0B); CHECK(o->dst == 0x0C);
        CHECK(o->hops_remaining == 10); CHECK(o->committed_hops == 2);
        CHECK(o->prev_fwd_rt_hops == 3);
        CHECK(o->ctr == 0x1234); CHECK(o->ctr_lo4 == 0x4);
        CHECK(o->inner_len == 3);
        CHECK(o->frame_len == 15);
        auto inr = data_inner(fr, *o);
        CHECK(inr.size() == 3);
        auto mc = data_mac(fr, *o);
        CHECK(mc.size() == 4);
        if (mc.size() == 4) CHECK(mc[0] == 0x00);
        auto u = parse_unicast_inner(inr, o->flags);
        CHECK(u.has_value());
        if (u) { CHECK(u->origin == 7); CHECK(u->body.size() == 2);
                 if (u->body.size() == 2) { CHECK(u->body[0] == 0xAA); CHECK(u->body[1] == 0xBB); } }
    }
}

TEST_CASE("DATA — golden APP frame (TYPE byte at offset 8; inner_off 9)") {
    // An APP frame: type != 0 -> pack sets the APP bit (0x80) in byte 1 + emits the TYPE byte at offset 8.
    // E2E_ACK example: inner = [origin=9][acked_ctr 0x0005 LE] (normal-unicast shape, no payload-flags byte).
    const uint8_t inner[] = {0x09, 0x05, 0x00};
    const uint8_t mac[]   = {0, 0, 0, 0};
    data_in in{};
    in.addr_len = 0; in.flags = 0; in.type = DATA_TYPE_E2E_ACK;   // 3
    in.next = 0x0B; in.dst = 0x0C;
    in.hops_remaining = 10; in.committed_hops = 2; in.prev_fwd_rt_hops = 3;
    in.ctr = 0x1234; in.inner = inner; in.mac = mac;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 16);   // 8 header + 1 TYPE + 3 inner + 4 MAC
    const uint8_t ex[] = {0x30, 0x80, 0x0B, 0x0C, 0x52, 0x03, 0x34, 0x12,
                          0x03, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00};
    for (int i = 0; i < 16; ++i) CHECK(buf[i] == ex[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_data(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->flags == 0x80);             // APP bit set (derived from type)
        CHECK(o->app); CHECK(o->type == DATA_TYPE_E2E_ACK); CHECK(o->e2e_is_ack);
        CHECK(o->inner_off == 9);
        CHECK(o->inner_len == 3);
        CHECK(o->frame_len == 16);
        auto inr = data_inner(fr, *o);
        CHECK(inr.size() == 3);
        if (inr.size() == 3) { CHECK(inr[0] == 0x09); CHECK(inr[1] == 0x05); CHECK(inr[2] == 0x00); }
    }
    // A too-short APP frame (no room for the TYPE byte: 8 header + 4 MAC = 12, APP set) is rejected.
    std::array<uint8_t, 12> shortapp{};
    for (int i = 0; i < 12; ++i) shortapp[i] = 0;
    shortapp[0] = 0x30; shortapp[1] = 0x80;   // cmd D, APP set, but only 12 B (no TYPE byte slot)
    CHECK_FALSE(parse_data(shortapp).has_value());
}

TEST_CASE("M — golden lean channel-message frame (cmd 0xA, leaf in byte-0, channel_msg_id BE)") {
    // 2026-06-09: channel messages moved off DATA onto their own cmd 0xA. leaf rides byte-0 low nibble.
    const uint8_t body[] = {0x99};
    m_in in{}; in.leaf_id = 5; in.channel_id = 0x02; in.flavor = 0x01; in.channel_msg_id = 0x07ABCDEFu; in.body = body;
    std::array<uint8_t, 16> buf{};
    size_t n = pack_m(in, buf);
    CHECK(n == 8);                                            // 7-B header + 1-B body
    const uint8_t ex[] = {0xA5, 0x02, 0x01, 0x07, 0xAB, 0xCD, 0xEF, 0x99};   // cmd 0xA|leaf 5; id BE
    for (int i = 0; i < 8; ++i) CHECK(buf[i] == ex[i]);
    auto o = parse_m(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->leaf_id == 5);                          // leaf survives byte-0 (the leak gate reads it)
             CHECK(o->channel_id == 0x02); CHECK(o->flavor == 0x01);
             CHECK(o->channel_msg_id == 0x07ABCDEFu);         // BIG-endian
             CHECK(o->body.size() == 1);
             if (o->body.size() == 1) CHECK(o->body[0] == 0x99); }
}

TEST_CASE("M — round-trip across leaf/channel/flavor/body; reject len<7 + wrong cmd; accept empty body") {
    for (uint8_t leaf : {uint8_t(0), uint8_t(7), uint8_t(15)})
        for (uint8_t ch : {uint8_t(0), uint8_t(0xFF)})
            for (uint32_t id : {uint32_t(0), uint32_t(0x01020304u), uint32_t(0xFFFFFFFFu)})
                for (size_t blen : {size_t(0), size_t(1), size_t(110)}) {
                    std::array<uint8_t, 120> body{};
                    for (size_t i = 0; i < blen; ++i) body[i] = uint8_t(0x20 + i);
                    m_in in{}; in.leaf_id = leaf; in.channel_id = ch; in.flavor = 3; in.channel_msg_id = id;
                    in.body = std::span<const uint8_t>(body.data(), blen);
                    std::array<uint8_t, 128> buf{};
                    size_t n = pack_m(in, buf);
                    CHECK(n == 7 + blen);
                    auto o = parse_m(std::span<const uint8_t>(buf.data(), n));
                    CHECK(o.has_value());
                    if (o) { CHECK(o->leaf_id == (leaf & 0x0F)); CHECK(o->channel_id == ch);
                             CHECK(o->flavor == 3); CHECK(o->channel_msg_id == id);
                             CHECK(o->body.size() == blen);
                             bool bok = true; for (size_t i = 0; i < blen; ++i) bok = bok && (o->body[i] == uint8_t(0x20 + i));
                             CHECK(bok); }
                }
    const uint8_t too_short[] = {0xA0, 0x01, 0x02, 0x03, 0x04, 0x05};               // 6 B header incomplete -> reject
    CHECK_FALSE(parse_m(std::span<const uint8_t>(too_short, 6)).has_value());
    const uint8_t wrong_cmd[] = {0x30, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};         // cmd 0x3 (DATA) -> reject
    CHECK_FALSE(parse_m(std::span<const uint8_t>(wrong_cmd, 7)).has_value());
    const uint8_t min_m[] = {0xA0, 0x05, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};             // 7-B header, empty body -> accept
    auto m0 = parse_m(std::span<const uint8_t>(min_m, 7));
    CHECK(m0.has_value());
    if (m0) { CHECK(m0->channel_id == 0x05); CHECK(m0->channel_msg_id == 0xDEADBEEFu); CHECK(m0->body.empty()); }
}

TEST_CASE("DATA — round-trip across fields (full flags byte, TYPE/APP, ctr, inner, hops)") {
    for (uint8_t flags : {uint8_t(0), uint8_t(DATA_FLAG_E2E_ACK_REQ),
                          uint8_t(DATA_FLAG_DST_HASH | DATA_FLAG_PRIORITY),
                          uint8_t(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH)})  // CRYPTED has its own 8-B-trailer round-trip (Phase-1 tests below)
        for (uint8_t type : {uint8_t(0), uint8_t(DATA_TYPE_H_ANSWER),
                             uint8_t(DATA_TYPE_AUTHORITATIVE_H_ANSWER), uint8_t(DATA_TYPE_E2E_ACK)})
            for (uint16_t ctr : {uint16_t(0), uint16_t(0x1234), uint16_t(0xFFFF)})
                for (uint8_t hr : {uint8_t(0), uint8_t(31)})
                    for (size_t inlen : {size_t(0), size_t(1), size_t(40)}) {
                        std::array<uint8_t, 40> inner{};
                        for (size_t i = 0; i < inlen; ++i) inner[i] = uint8_t(0x10 + i);
                        std::array<uint8_t, 4> mac{0xDE, 0xAD, 0xBE, 0xEF};
                        data_in in{};
                        in.flags = flags; in.type = type; in.next = 0x21; in.dst = 0x22;
                        in.hops_remaining = hr; in.committed_hops = 5; in.prev_fwd_rt_hops = 9;
                        in.ctr = ctr;
                        in.inner = std::span<const uint8_t>(inner.data(), inlen); in.mac = mac;
                        std::array<uint8_t, 64> buf{};
                        size_t n = pack_data(in, buf);
                        const size_t app_off = (type != 0) ? 1u : 0u;   // +1 TYPE byte when APP
                        CHECK(n == 12 + app_off + inlen);
                        std::span<const uint8_t> fr(buf.data(), n);
                        auto o = parse_data(fr);
                        CHECK(o.has_value());
                        if (o) {
                            // APP is derived from type: the written flags byte is the input flags + the APP bit (or not).
                            const uint8_t exp_flags = (type != 0) ? uint8_t(flags | DATA_FLAG_APP) : flags;
                            CHECK(o->flags == exp_flags);
                            CHECK(o->app            == (type != 0));
                            CHECK(o->type           == type);
                            CHECK(o->inner_off      == (8u + app_off));
                            CHECK(o->e2e_is_ack     == (type == DATA_TYPE_E2E_ACK));
                            CHECK(o->cross_layer    == ((flags & DATA_FLAG_CROSS_LAYER) != 0));
                            CHECK(o->crypted        == ((flags & DATA_FLAG_CRYPTED)     != 0));
                            CHECK(o->e2e_ack_req    == ((flags & DATA_FLAG_E2E_ACK_REQ) != 0));
                            CHECK(o->source_hash    == ((flags & DATA_FLAG_SOURCE_HASH) != 0));
                            CHECK(o->dst_hash       == ((flags & DATA_FLAG_DST_HASH)    != 0));
                            CHECK(o->priority       == ((flags & DATA_FLAG_PRIORITY)    != 0));
                            CHECK(o->ctr == ctr);
                            CHECK(o->ctr_lo4 == (ctr & 0x0F));
                            CHECK(o->hops_remaining == hr);
                            CHECK(o->committed_hops == 5);
                            CHECK(o->prev_fwd_rt_hops == 9);
                            CHECK(o->inner_len == inlen);
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
    in.inner = {}; in.mac = {};
    std::array<uint8_t, 24> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 12);
    CHECK(buf[4] == 0xFF);   // (31<<3)|7 = 0xF8|0x07 — saturated, not 40&0x1f / 9&0x07
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_data(fr);
    CHECK(o.has_value());
    if (o) { CHECK(o->hops_remaining == 31); CHECK(o->committed_hops == 7); }
}

TEST_CASE("DATA — byte1 flag bit isolation (full byte; each flag toggles exactly its bit)") {
    auto pack_flags = [](uint8_t flags, std::array<uint8_t, 24>& out) -> size_t {
        data_in in{};
        in.next = 1; in.dst = 2; in.flags = flags;   // type stays 0 (no APP byte) so byte 1 == the flags input
        in.inner = {}; in.mac = {};
        return pack_data(in, out);
    };
    std::array<uint8_t, 24> base{}, xl{}, cry{}, ack_req{}, srch{}, dsth{}, prio{};
    CHECK(pack_flags(0, base) == 12);
    CHECK(pack_flags(DATA_FLAG_CROSS_LAYER, xl)      == 12);
    CHECK(pack_flags(DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH, cry) == 16);  // CRYPTED requires DST_HASH + an 8-B nonce-seed trailer
    CHECK(pack_flags(DATA_FLAG_E2E_ACK_REQ, ack_req) == 12);
    CHECK(pack_flags(DATA_FLAG_SOURCE_HASH, srch)    == 12);
    CHECK(pack_flags(DATA_FLAG_DST_HASH,    dsth)    == 12);
    CHECK(pack_flags(DATA_FLAG_PRIORITY,    prio)    == 12);
    CHECK(base[1] == 0x00);
    CHECK((base[1] ^ xl[1])      == 0x40);   // CROSS_LAYER -> bit 6
    CHECK((dsth[1] ^ cry[1])     == 0x20);   // CRYPTED     -> bit 5 (isolated vs DST_HASH, which CRYPTED requires)
    CHECK((base[1] ^ ack_req[1]) == 0x10);   // E2E_ACK_REQ -> bit 4
    CHECK((base[1] ^ srch[1])    == 0x04);   // SOURCE_HASH -> bit 2
    CHECK((base[1] ^ dsth[1])    == 0x02);   // DST_HASH    -> bit 1
    CHECK((base[1] ^ prio[1])    == 0x01);   // PRIORITY    -> bit 0
    // APP (bit 7) is DERIVED from type, not set directly: a non-zero type sets it + emits the TYPE byte.
    std::array<uint8_t, 24> app{};
    data_in ai{}; ai.next = 1; ai.dst = 2; ai.flags = 0; ai.type = DATA_TYPE_H_ANSWER; ai.inner = {}; ai.mac = {};
    CHECK(pack_data(ai, app) == 13);          // +1 TYPE byte
    CHECK((base[1] ^ app[1]) == 0x80);        // APP -> bit 7
    CHECK(app[8] == DATA_TYPE_H_ANSWER);      // the TYPE byte
}

TEST_CASE("DATA — reject: wrong cmd / <12 / addr_len!=0 / bad span sizes; 12-B min accepts") {
    const uint8_t inner[] = {0x00, 0x07, 0xAA};
    const uint8_t mac[] = {0, 0, 0, 0};
    data_in in{};
    in.next = 0x0B; in.dst = 0x0C; in.hops_remaining = 5;
    in.ctr = 0x1234; in.inner = inner; in.mac = mac;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 15);   // 8 + 3 + 4

    std::array<uint8_t, 15> w2{};
    for (int i = 0; i < 15; ++i) w2[i] = buf[i];
    w2[0] = wire::cmd_byte(wire::Cmd::B, 0);
    CHECK_FALSE(parse_data(w2).has_value());                                   // wrong cmd
    CHECK_FALSE(parse_data(std::span<const uint8_t>(buf.data(), 11)).has_value()); // < 12

    std::array<uint8_t, 15> al{};
    for (int i = 0; i < 15; ++i) al[i] = buf[i];
    al[0] = static_cast<uint8_t>(al[0] | 0x02);   // addr_len=1 in byte0 bits 3..1
    CHECK_FALSE(parse_data(al).has_value());                                   // addr_len != 0
    std::array<uint8_t, 15> al7{};
    for (int i = 0; i < 15; ++i) al7[i] = buf[i];
    al7[0] = static_cast<uint8_t>(wire::cmd_byte(wire::Cmd::D, 0) | (0x07 << 1));  // addr_len=7
    CHECK_FALSE(parse_data(al7).has_value());                                  // 3-bit addr_len field
    std::array<uint8_t, 15> rsv{};
    for (int i = 0; i < 15; ++i) rsv[i] = buf[i];
    rsv[0] = static_cast<uint8_t>(rsv[0] | 0x01);   // byte0 bit 0 is rsv
    auto ro = parse_data(rsv);
    CHECK(ro.has_value());                                                     // rsv bit 0 ignored on parse
    if (ro) CHECK(ro->addr_len == 0);

    // 12-byte minimal (empty inner) parses — DATA has no inner length prefix
    data_in mn{}; mn.next = 1; mn.dst = 2; mn.inner = {}; mn.mac = {};
    std::array<uint8_t, 12> mbuf{};
    CHECK(pack_data(mn, mbuf) == 12);
    auto mo = parse_data(mbuf);
    CHECK(mo.has_value());
    if (mo) { CHECK(mo->inner_len == 0); CHECK(data_inner(mbuf, *mo).empty()); }

    data_in bad_al = in; bad_al.addr_len = 1;
    CHECK(pack_data(bad_al, buf) == 0);                                        // addr_len != 0
    std::array<uint8_t, 3> mac3{};
    data_in bad_mac = in; bad_mac.mac = mac3;
    CHECK(pack_data(bad_mac, buf) == 0);                                       // mac size 3 != 4
}

TEST_CASE("DATA — endianness guard (ctr LE)") {
    data_in in{};
    in.next = 1; in.dst = 2; in.ctr = 0xBEEF;
    in.inner = {}; in.mac = {};
    std::array<uint8_t, 24> buf{};
    size_t n = pack_data(in, buf);
    CHECK(n == 12);
    CHECK(buf[6] == 0xEF);   // ctr LE: low byte first
    CHECK(buf[7] == 0xBE);
    // (channel_msg_id BE is exercised by the M-frame golden/round-trip tests above — it's no longer a DATA inner.)
}

TEST_CASE("DATA — inner helpers: reject malformed + accept minimum (empty body)") {
    // No payload-flags byte: a plain inner (flags=0) is [origin][body], so an empty inner has no origin -> reject.
    const uint8_t empty_uni[] = {0x00};
    CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(empty_uni, 0), /*flags=*/0).has_value());

    // minimum-accept boundary: just an origin (flags=0), empty body
    const uint8_t min_uni[] = {0x07};               // origin=7, body empty
    auto u = parse_unicast_inner(std::span<const uint8_t>(min_uni, 1), /*flags=*/0);
    CHECK(u.has_value());
    if (u) { CHECK(u->origin == 7); CHECK(u->body.size() == 0); }
    // (channel-M inner retired — its parse boundaries are covered by the M-frame reject/accept test above.)
}

TEST_CASE("DATA — default hops_remaining is 31 (no TTL enforcement, Lua 'or 31')") {
    data_in in{};   // default-constructed: hops_remaining must default to 31, not 0
    in.next = 1; in.dst = 2; in.inner = {}; in.mac = {};
    std::array<uint8_t, 12> buf{};
    CHECK(pack_data(in, buf) == 12);
    CHECK(buf[4] == 0xF8);   // hops_remaining 31 (<<3) | committed 0 — NOT 0x00 (TTL exhausted)
    auto o = parse_data(buf);
    CHECK(o.has_value());
    if (o) { CHECK(o->hops_remaining == 31); CHECK(o->committed_hops == 0); }
}

TEST_CASE("DATA unicast inner — DST_HASH round-trip + plain + reject truncations (layout from flags)") {
    // plain inner (flags=0): [origin][body] — no payload-flags byte
    { const uint8_t in[] = { 7, 'h', 'i' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), /*flags=*/0);
      CHECK(u.has_value());
      if (u) { CHECK_FALSE(u->has_dst_hash); CHECK(u->origin == 7);
               CHECK(u->body.size() == 2); CHECK(u->body[0] == 'h'); CHECK(u->body[1] == 'i'); } }
    // DST_HASH inner (flags & DST_HASH): [dst_key_hash32 LE = 0x12345678][origin][body]
    { const uint8_t in[] = { 0x78, 0x56, 0x34, 0x12, 9, 'o', 'k' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_DST_HASH);
      CHECK(u.has_value());
      if (u) { CHECK(u->has_dst_hash); CHECK(u->dst_key_hash32 == 0x12345678u); CHECK(u->origin == 9);
               CHECK(u->body.size() == 2); CHECK(u->body[0] == 'o'); CHECK(u->body[1] == 'k'); } }
    // DST_HASH with an EMPTY body is valid (origin present, zero-length body)
    { const uint8_t in[] = { 1, 2, 3, 4, 9 };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_DST_HASH);
      CHECK(u.has_value());
      if (u) { CHECK(u->has_dst_hash); CHECK(u->origin == 9); CHECK(u->body.size() == 0); } }
    // SOURCE_HASH inner (flags & SOURCE_HASH): [origin][source_hash 4 B LE = 0xCAFEF00D][body] — the sender's
    // stable key_hash32, AFTER origin.
    { const uint8_t in[] = { 9, 0x0D, 0xF0, 0xFE, 0xCA, 'h', 'i' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_SOURCE_HASH);
      CHECK(u.has_value());
      if (u) { CHECK_FALSE(u->has_dst_hash); CHECK(u->origin == 9);
               CHECK(u->has_source_hash); CHECK(u->source_hash == 0xCAFEF00Du);
               CHECK(u->body.size() == 2); CHECK(u->body[0] == 'h'); CHECK(u->body[1] == 'i'); } }
    // DST_HASH + SOURCE_HASH together: [dst_hash 4][origin][source_hash 4][body] — both prefixes, in order.
    { const uint8_t in[] = { 0x78, 0x56, 0x34, 0x12, 9, 0x0D, 0xF0, 0xFE, 0xCA, 'y' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in),
                                   static_cast<uint8_t>(DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH));
      CHECK(u.has_value());
      if (u) { CHECK(u->has_dst_hash); CHECK(u->dst_key_hash32 == 0x12345678u); CHECK(u->origin == 9);
               CHECK(u->has_source_hash); CHECK(u->source_hash == 0xCAFEF00Du);
               CHECK(u->body.size() == 1); CHECK(u->body[0] == 'y'); } }
    // SOURCE_HASH set but the 4-B hash is truncated -> reject (no OOB read)
    { const uint8_t in[] = { 9, 0x0D, 0xF0 };   // origin + only 2 of 4 source_hash bytes
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_SOURCE_HASH).has_value()); }
    // A flag the unicast inner doesn't decode (CRYPTED) leaves the layout at [origin][body].
    { const uint8_t in[] = { 5, 'x' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CRYPTED);   // no DST_HASH/SOURCE_HASH bit
      CHECK(u.has_value());
      if (u) { CHECK_FALSE(u->has_dst_hash); CHECK_FALSE(u->has_source_hash); CHECK(u->origin == 5); CHECK(u->body.size() == 1); } }
    // reject: truncations
    { const uint8_t shorthash[] = { 1, 2, 3 };          // DST_HASH set but hash truncated (<4)
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(shorthash, sizeof shorthash), DATA_FLAG_DST_HASH).has_value()); }
    { const uint8_t noorigin[] = { 1, 2, 3, 4 };        // hash present but no origin byte
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(noorigin, sizeof noorigin), DATA_FLAG_DST_HASH).has_value()); }
    { const uint8_t* e = nullptr;
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(e, size_t(0)), /*flags=*/0).has_value()); }   // empty, no origin
}

TEST_CASE("DATA unicast inner — CROSS_LAYER layer-path parse + fail-loud (Slice 4b / spec §5)") {
    using meshroute::protocol::gw_env_max_hops;
    // CROSS_LAYER inner: [n_layers | cur | layer_ids...][origin][body], BETWEEN dst_hash and origin.
    // layer-path = n_layers 2, cur 1, ids [0x17, 0x27] (FULL 8-bit, NOT the 4-bit leaf nibble).
    { const uint8_t in[] = { 2, 1, 0x17, 0x27, 9, 'h', 'i' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CROSS_LAYER);
      CHECK(u.has_value());
      if (u) { CHECK(u->has_cross_layer); CHECK(u->n_layers == 2); CHECK(u->cur == 1);
               CHECK(u->layer_ids[0] == 0x17); CHECK(u->layer_ids[1] == 0x27);   // FULL 8-bit ids preserved
               CHECK(u->origin == 9); CHECK(u->body.size() == 2); CHECK(u->body[0] == 'h'); } }
    // DST_HASH + CROSS_LAYER + SOURCE_HASH together — the LOCKED order [dst_hash][layer-path][origin][source_hash][body].
    { const uint8_t in[] = { 0x78,0x56,0x34,0x12,  2,1,0x17,0x27,  9,  0x0D,0xF0,0xFE,0xCA,  'y' };
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in),
                                   static_cast<uint8_t>(DATA_FLAG_DST_HASH | DATA_FLAG_CROSS_LAYER | DATA_FLAG_SOURCE_HASH));
      CHECK(u.has_value());
      if (u) { CHECK(u->dst_key_hash32 == 0x12345678u); CHECK(u->has_cross_layer); CHECK(u->n_layers == 2);
               CHECK(u->cur == 1); CHECK(u->layer_ids[0] == 0x17); CHECK(u->origin == 9);
               CHECK(u->source_hash == 0xCAFEF00Du); CHECK(u->body.size() == 1); CHECK(u->body[0] == 'y'); } }
    // FAIL LOUD (nullopt), NO clamp / default:
    { const uint8_t in[] = { 0, 0, 9 };                                    // n_layers 0 -> refuse
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CROSS_LAYER).has_value()); }
    { const uint8_t in[] = { 5, 0, 1,2,3,4,5, 9 };                          // n_layers 5 > gw_env_max_hops(4) -> refuse
      CHECK(gw_env_max_hops == 4);
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CROSS_LAYER).has_value()); }
    { const uint8_t in[] = { 2, 2, 0x17,0x27, 9 };                         // cur 2 >= n_layers 2 -> refuse (cur indexes < n)
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CROSS_LAYER).has_value()); }
    { const uint8_t in[] = { 3, 0, 0x17, 0x27 };                           // n_layers 3 but only 2 ids present -> refuse
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CROSS_LAYER).has_value()); }
    { const uint8_t in[] = { 2 };                                          // n_layers byte but no cur -> refuse
      CHECK_FALSE(parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CROSS_LAYER).has_value()); }
}

TEST_CASE("DATA unicast inner — pack_unicast_inner round-trips with parse + overflow/invalid -> 0 (Slice 4b)") {
    uint8_t out[64];
    const uint8_t ids[2] = { 0x17, 0x27 };
    const uint8_t body[3] = { 'a', 'b', 'c' };
    // plain [origin][body] — and the SAME helper backs enqueue_data, so this is the no-op-preserving path.
    { const size_t n = pack_unicast_inner(std::span<uint8_t>(out, sizeof out), /*flags=*/0, 0, nullptr, 0, 0, 9, 0, body, 3, 0, 0);
      CHECK(n == 4);
      auto u = parse_unicast_inner(std::span<const uint8_t>(out, n), 0);
      CHECK(u.has_value()); if (u) { CHECK(u->origin == 9); CHECK_FALSE(u->has_cross_layer); CHECK(u->body.size() == 3); } }
    // full combo: pack -> parse -> identical (the locked order, the single source of truth).
    { const uint8_t f = static_cast<uint8_t>(DATA_FLAG_DST_HASH | DATA_FLAG_CROSS_LAYER | DATA_FLAG_SOURCE_HASH);
      const size_t n = pack_unicast_inner(std::span<uint8_t>(out, sizeof out), f, 0x12345678u, ids, 2, 1, 9, 0xCAFEF00Du, body, 3, 0, 0);
      CHECK(n == 4 /*dh*/ + 4 /*path:2+2*/ + 1 /*origin*/ + 4 /*srch*/ + 3 /*body*/);
      auto u = parse_unicast_inner(std::span<const uint8_t>(out, n), f);
      CHECK(u.has_value());
      if (u) { CHECK(u->dst_key_hash32 == 0x12345678u); CHECK(u->n_layers == 2); CHECK(u->cur == 1);
               CHECK(u->layer_ids[0] == 0x17); CHECK(u->layer_ids[1] == 0x27); CHECK(u->origin == 9);
               CHECK(u->source_hash == 0xCAFEF00Du); CHECK(u->body.size() == 3); CHECK(u->body[0] == 'a'); } }
    // overflow -> 0 (NO truncation): a 5-byte buffer can't hold dst_hash(4)+origin(1)+body(3).
    { CHECK(pack_unicast_inner(std::span<uint8_t>(out, 5), DATA_FLAG_DST_HASH, 1, nullptr, 0, 0, 9, 0, body, 3, 0, 0) == 0); }
    // invalid path (CROSS_LAYER but n_layers 0 / cur>=n) -> 0 (refuse, never emit a bad frame).
    { CHECK(pack_unicast_inner(std::span<uint8_t>(out, sizeof out), DATA_FLAG_CROSS_LAYER, 0, ids, 0, 0, 9, 0, body, 3, 0, 0) == 0);
      CHECK(pack_unicast_inner(std::span<uint8_t>(out, sizeof out), DATA_FLAG_CROSS_LAYER, 0, ids, 2, 2, 9, 0, body, 3, 0, 0) == 0); }
}

TEST_CASE("RTS — FLOOD RTS-M round-trip (43 B: channel_msg_id BE + 32-B bitmap) + rejects") {
    std::array<uint8_t, 32> bm{};
    for (int i = 0; i < 32; ++i) bm[i] = static_cast<uint8_t>(i * 7 + 1);
    rts_in in{};
    in.leaf_id = 3; in.src = 9; in.next = 0xFF; in.ctr_lo = 5;
    in.dst = 14 /*hop_left*/; in.sf_index = 2; in.payload_len = 42;
    in.rts_flags = RTS_FLAG_M_BROADCAST | RTS_FLAG_FLOOD;
    in.flood_channel_msg_id = 0x11223344u;
    in.flood_bitmap = std::span<const uint8_t>(bm.data(), 32);
    std::array<uint8_t, 64> buf{};
    const size_t n = pack_rts(in, std::span<uint8_t>(buf.data(), buf.size()));
    CHECK(n == 43);
    auto o = parse_rts(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) {
        CHECK(o->leaf_id == 3); CHECK(o->src == 9); CHECK(o->next == 0xFF);  // next slot = broadcast 0xFF
        CHECK(o->ctr_lo == 5); CHECK(o->dst == 14);                          // dst slot = hop_left
        CHECK(o->sf_index == 2); CHECK(o->m_broadcast); CHECK(o->flood);
        CHECK(o->flood_channel_msg_id == 0x11223344u);
        auto pbm = rts_flood_bitmap(std::span<const uint8_t>(buf.data(), n), *o);
        CHECK(pbm.size() == 32);
        bool same = true; for (int i = 0; i < 32; ++i) if (pbm[i] != bm[i]) same = false;
        CHECK(same);
    }
    // reject: FLOOD with a non-32-B bitmap -> pack 0 (no zero-fill fallback)
    { rts_in bad = in; const uint8_t three[3] = { 1, 2, 3 }; bad.flood_bitmap = std::span<const uint8_t>(three, 3);
      CHECK(pack_rts(bad, std::span<uint8_t>(buf.data(), buf.size())) == 0); }
    // reject: a FLOOD frame truncated below 43 B -> parse nullopt
    CHECK_FALSE(parse_rts(std::span<const uint8_t>(buf.data(), 20)).has_value());
    // a non-FLOOD M_BROADCAST still packs 9 B with the id_lo16 tail (unchanged)
    { rts_in mb{}; mb.leaf_id = 0; mb.src = 1; mb.next = 2; mb.ctr_lo = 3; mb.dst = 4; mb.sf_index = 1;
      mb.rts_flags = RTS_FLAG_M_BROADCAST; mb.payload_len = 10; mb.m_payload_id_lo16 = 0xBEEF;
      const size_t mn = pack_rts(mb, std::span<uint8_t>(buf.data(), buf.size()));
      CHECK(mn == 9);
      auto mo = parse_rts(std::span<const uint8_t>(buf.data(), mn));
      CHECK(mo.has_value());
      if (mo) { CHECK(mo->m_broadcast); CHECK_FALSE(mo->flood); CHECK(mo->m_payload_id_lo16 == 0xBEEF); } }
}

TEST_CASE("gateway-layer TLV (type 4) — pack/parse round-trip, incl ODD N nibble packing") {
    for (uint8_t N : { uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(9) }) {   // 3/5/9 = odd N
        GwLayerEntry in[9];
        for (uint8_t i = 0; i < N; ++i) in[i] = GwLayerEntry{ static_cast<uint8_t>(10 + i), static_cast<uint8_t>((i * 3 + 1) & 0x0F) };
        uint8_t buf[32];
        const size_t n = pack_gateway_layer_tlv(in, N, std::span<uint8_t>(buf, sizeof buf));
        CHECK(n > 0);
        CHECK(static_cast<uint8_t>(buf[0] >> 4) == protocol::bcn_ext_type_gateway_layer);        // header type nibble
        GwLayerEntry out[9];
        const uint8_t got = parse_gateway_layer_tlv(std::span<const uint8_t>(buf, n), out, 9);
        CHECK(got == N);
        for (uint8_t i = 0; i < N; ++i) { CHECK(out[i].gw_id == in[i].gw_id); CHECK(out[i].dest_leaf == (in[i].dest_leaf & 0x0F)); }
    }
    // empty -> 0 bytes (the codec-level s18 keystone)
    uint8_t b[4]; GwLayerEntry none[1] = {{0, 0}};
    CHECK(pack_gateway_layer_tlv(none, 0, std::span<uint8_t>(b, sizeof b)) == 0);
}

TEST_CASE("gateway-layer TLV — a DUPLICATE gw_id discards the whole TLV; an unrelated TLV is skipped") {
    GwLayerEntry dup[2] = { {7, 2}, {7, 3} };                  // same gw_id twice (pack does not dedup; the wire is malformed)
    uint8_t buf[16];
    const size_t n = pack_gateway_layer_tlv(dup, 2, std::span<uint8_t>(buf, sizeof buf));
    CHECK(n > 0);
    GwLayerEntry out[2];
    CHECK(parse_gateway_layer_tlv(std::span<const uint8_t>(buf, n), out, 2) == 0);               // duplicate -> reject whole TLV
    // a type-3 (channel-digest) TLV alone -> the type-4 parse finds nothing (skips other types)
    uint32_t ids[1] = { 0xDEADBEEF }; uint8_t c3[16];
    const size_t c3n = pack_channel_digest_tlv(ids, 1, std::span<uint8_t>(c3, sizeof c3));
    CHECK(parse_gateway_layer_tlv(std::span<const uint8_t>(c3, c3n), out, 2) == 0);
}

// -----------------------------------------------------------------------------
// loc6 — 6-byte location codec (21-bit lat + 22-bit lon, ~11 m). Location-propagation
// spec 2026-06-14. step = 1024 e7-units; +512 cell-centering bounds the per-axis
// decode error by 512 e7 (~5.7 m at the equator). The DECODE MUST use int64
// intermediates — u_lon<<10 reaches 3.6e9 > INT32_MAX.
// -----------------------------------------------------------------------------
static long loc6_abs(long v) { return v < 0 ? -v : v; }

TEST_CASE("loc6 — round-trip within the ~11 m quantization bound across the globe") {
    struct P { int32_t lat, lon; };
    const P pts[] = {
        {0, 0}, {523000000, 134050000},        // ~52.30, 13.405
        {-337680000, 1511000000},              // Sydney -33.768, 151.10
        {900000000, 1800000000},               // +90 / +180 (antimeridian -> the int64 decode path)
        {-900000000, -1800000000},             // -90 / -180
        {900000000, -1800000000}, {-1, 1},
    };
    for (const P& p : pts) {
        uint8_t b[6] = {};
        CHECK(pack_loc6(p.lat, p.lon, std::span<uint8_t>(b, sizeof b)) == 6);
        int32_t lat = 0, lon = 0;
        CHECK(unpack_loc6(std::span<const uint8_t>(b, 6), lat, lon));
        CHECK(loc6_abs(static_cast<long>(lat) - p.lat) <= 512);
        CHECK(loc6_abs(static_cast<long>(lon) - p.lon) <= 512);
    }
}

TEST_CASE("loc6 — 6 bytes MSB-first; low 5 bits reserved = 0") {
    uint8_t b[6] = {};
    CHECK(pack_loc6(0, 0, std::span<uint8_t>(b, sizeof b)) == 6);
    CHECK((b[5] & 0x1F) == 0);
}

TEST_CASE("loc6 — short buffer refuses (pack 0, unpack false)") {
    uint8_t five[5] = {};
    CHECK(pack_loc6(523000000, 134050000, std::span<uint8_t>(five, 5)) == 0);
    int32_t lat = 7, lon = 7;
    CHECK_FALSE(unpack_loc6(std::span<const uint8_t>(five, 5), lat, lon));
}

TEST_CASE("loc6 — over-range latitude clamps to the field ceiling (no overflow)") {
    uint8_t b[6] = {};
    CHECK(pack_loc6(2000000000, 0, std::span<uint8_t>(b, sizeof b)) == 6);   // lat >> +90, garbage in
    int32_t lat = 0, lon = 0;
    CHECK(unpack_loc6(std::span<const uint8_t>(b, 6), lat, lon));
    CHECK(lat > 900000000);                                                  // clamped near the ceiling, bounded
}

// -----------------------------------------------------------------------------
// DATA inner — LOCATION slot (DATA_FLAG_LOCATION 0x08), spec 2026-06-14 §3.
// The 6-B location sits AFTER source_hash, BEFORE body (inside the origin-onward
// sealed region). The KEYSTONE: absent the flag, the new coord params must not
// perturb a single byte (s18 byte-identity).
// -----------------------------------------------------------------------------
TEST_CASE("unicast inner — LOCATION round-trips after source_hash, before body") {
    const uint8_t body[] = {0xAA, 0xBB, 0xCC};
    uint8_t buf[64] = {};
    const uint8_t flags = DATA_FLAG_SOURCE_HASH | DATA_FLAG_LOCATION;
    const int32_t LAT = 523000000, LON = 134050000;
    const size_t n = pack_unicast_inner(std::span<uint8_t>(buf, sizeof buf), flags,
                                        /*dst_hash*/ 0, /*layer_ids*/ nullptr, 0, 0,
                                        /*origin*/ 42, /*source_hash*/ 0x11223344,
                                        body, sizeof body, LAT, LON);
    CHECK(n == 1 + 4 + 6 + 3);                                  // origin + source_hash + loc6 + body
    auto u = parse_unicast_inner(std::span<const uint8_t>(buf, n), flags);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->origin == 42);
        CHECK(u->has_source_hash); CHECK(u->source_hash == 0x11223344);
        CHECK(u->has_location);
        CHECK(loc6_abs(static_cast<long>(u->lat_e7) - LAT) <= 512);
        CHECK(loc6_abs(static_cast<long>(u->lon_e7) - LON) <= 512);
        CHECK(u->body.size() == 3);
        CHECK(u->body[0] == 0xAA); CHECK(u->body[2] == 0xCC);  // body intact AFTER the 6-B location
    }
}

TEST_CASE("unicast inner — KEYSTONE: coords inert when LOCATION absent (byte-identical)") {
    const uint8_t body[] = {1, 2, 3, 4};
    uint8_t with_coords[64] = {}, zero_coords[64] = {};
    const uint8_t flags = DATA_FLAG_SOURCE_HASH;               // NO LOCATION
    const size_t a = pack_unicast_inner(std::span<uint8_t>(with_coords, 64), flags, 0, nullptr, 0, 0,
                                        7, 0xDEADBEEF, body, sizeof body, 523000000, 134050000);
    const size_t b = pack_unicast_inner(std::span<uint8_t>(zero_coords, 64), flags, 0, nullptr, 0, 0,
                                        7, 0xDEADBEEF, body, sizeof body, 0, 0);
    CHECK(a == b);
    CHECK(a == 1 + 4 + 4);                                     // origin + source_hash + body — NO loc6
    bool same = true; for (size_t i = 0; i < a; ++i) if (with_coords[i] != zero_coords[i]) same = false;
    CHECK(same);                                               // coords MUST NOT perturb the flag-absent path
    auto u = parse_unicast_inner(std::span<const uint8_t>(with_coords, a), flags);
    CHECK(u.has_value()); if (u) CHECK_FALSE(u->has_location);
}

TEST_CASE("unicast inner — LOCATION grows the inner by 6; the body cap shrinks (overflow refuses)") {
    const uint8_t body[8] = {};
    uint8_t tight[13] = {};                                    // fits origin(1)+source_hash(4)+body(8)=13, not +6
    const uint8_t flags = DATA_FLAG_SOURCE_HASH | DATA_FLAG_LOCATION;
    CHECK(pack_unicast_inner(std::span<uint8_t>(tight, sizeof tight), flags, 0, nullptr, 0, 0,
                             7, 1, body, sizeof body, 1, 1) == 0);     // +6 doesn't fit -> refuse, no truncation
    uint8_t ok[19] = {};
    CHECK(pack_unicast_inner(std::span<uint8_t>(ok, sizeof ok), flags, 0, nullptr, 0, 0,
                             7, 1, body, sizeof body, 1, 1) == 19);    // 1+4+6+8
}

// =============================================================================
// Phase 1 — conditional DATA MAC trailer (4 normally, 8 under CRYPTED = the nonce-seed)
// + the CRYPTED inner split. Spec docs/superpowers/specs/2026-06-15-phase1-e2e-dm-crypto.md §3.
// =============================================================================
TEST_CASE("data_mac_len — 4 normally, 8 under CRYPTED") {
    CHECK(data_mac_len(0) == 4);
    CHECK(data_mac_len(DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH) == 4);
    CHECK(data_mac_len(DATA_FLAG_CRYPTED) == 8);
    CHECK(data_mac_len(DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH) == 8);
}

TEST_CASE("pack_data — KEYSTONE: a non-CRYPTED DATA keeps the 4-B trailer (byte-identical layout)") {
    const uint8_t inner[5] = { 9, 'h', 'e', 'l', 'l' };
    uint8_t out[64] = {};
    data_in din{}; din.flags = DATA_FLAG_SOURCE_HASH; din.next = 2; din.dst = 3; din.ctr = 0x0042;
    din.inner = std::span<const uint8_t>(inner, sizeof inner);          // mac empty -> 4 zero trailer
    const size_t n = pack_data(din, std::span<uint8_t>(out, sizeof out));
    CHECK(n == 8 + 5 + 4);
    auto d = parse_data(std::span<const uint8_t>(out, n));
    CHECK(d.has_value());
    if (d) { CHECK_FALSE(d->crypted); CHECK(d->inner_len == 5); CHECK(d->mac_off == n - 4); }
}

TEST_CASE("pack_data — CRYPTED carries an 8-B trailer (nonce-seed); parse exposes data_nonce_seed + data_mac(8)") {
    const uint8_t inner[6] = { 9, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };       // origin + opaque sealed blob
    const uint8_t seed[8]  = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t out[64] = {};
    data_in din{}; din.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH; din.next = 2; din.dst = 3; din.ctr = 7;
    din.inner = std::span<const uint8_t>(inner, sizeof inner);
    din.mac   = std::span<const uint8_t>(seed, 8);
    const size_t n = pack_data(din, std::span<uint8_t>(out, sizeof out));
    CHECK(n == 8 + 6 + 8);
    auto d = parse_data(std::span<const uint8_t>(out, n));
    CHECK(d.has_value());
    if (d) {
        CHECK(d->crypted);
        CHECK(d->inner_len == 6);
        CHECK(d->mac_off == n - 8);
        auto sd = data_nonce_seed(std::span<const uint8_t>(out, n), *d);
        CHECK(sd.size() == 8);
        bool seed_ok = true; for (int i = 0; i < 8; ++i) if (sd[i] != seed[i]) seed_ok = false;
        CHECK(seed_ok);
        CHECK(data_mac(std::span<const uint8_t>(out, n), *d).size() == 8);   // data_mac == the conditional trailer
    }
}

TEST_CASE("pack_data — refuses CRYPTED without DST_HASH, and a wrong-size CRYPTED trailer") {
    const uint8_t inner[2] = { 9, 0xAA }; const uint8_t seed[8] = {};
    uint8_t out[64];
    data_in din{}; din.flags = DATA_FLAG_CRYPTED;                       // NO DST_HASH -> refuse
    din.inner = std::span<const uint8_t>(inner, sizeof inner); din.mac = std::span<const uint8_t>(seed, 8);
    CHECK(pack_data(din, std::span<uint8_t>(out, sizeof out)) == 0);
    din.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH; din.mac = std::span<const uint8_t>(seed, 4);  // 4-B trailer under CRYPTED -> refuse
    CHECK(pack_data(din, std::span<uint8_t>(out, sizeof out)) == 0);
}

TEST_CASE("parse_unicast_inner — CRYPTED stops at origin; the sealed region is handed back as body") {
    uint8_t inner[4 + 1 + 10];
    inner[0] = 0x44; inner[1] = 0x33; inner[2] = 0x22; inner[3] = 0x11;     // dst_key_hash32 = 0x11223344 LE
    inner[4] = 42;                                                          // origin (cleartext)
    for (int i = 0; i < 10; ++i) inner[5 + i] = static_cast<uint8_t>(0xA0 + i);  // sealed ct||tag (opaque)
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    auto u = parse_unicast_inner(std::span<const uint8_t>(inner, sizeof inner), flags);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->has_dst_hash); CHECK(u->dst_key_hash32 == 0x11223344u);
        CHECK(u->origin == 42);
        CHECK_FALSE(u->has_source_hash);                                   // NOT read raw (sealed)
        CHECK_FALSE(u->has_location);
        CHECK(u->body.size() == 10);                                       // the whole sealed region (ct||tag)
        CHECK(u->body[0] == 0xA0); CHECK(u->body[9] == 0xA9);
    }
}

TEST_CASE("hash-bind PUBKEY inner (DATA TYPE 5) — 34-B round-trip; <34 rejected") {
    hash_bind_pubkey_inner in{}; in.target_layer = 5; in.node_id = 42;
    for (int i = 0; i < 32; ++i) in.ed_pub[i] = static_cast<uint8_t>(0x10 + i);
    uint8_t buf[40] = {};
    CHECK(pack_hash_bind_pubkey_inner(in, std::span<uint8_t>(buf, sizeof buf)) == 34);
    auto o = parse_hash_bind_pubkey_inner(std::span<const uint8_t>(buf, 34));
    CHECK(o.has_value());
    if (o) { CHECK(o->target_layer == 5); CHECK(o->node_id == 42);
             bool same = true; for (int i = 0; i < 32; ++i) if (o->ed_pub[i] != in.ed_pub[i]) same = false; CHECK(same); }
    CHECK(pack_hash_bind_pubkey_inner(in, std::span<uint8_t>(buf, 33)) == 0);
    CHECK_FALSE(parse_hash_bind_pubkey_inner(std::span<const uint8_t>(buf, 33)).has_value());
}

// E2E observability (device console eye-confirm): data_crypted_region carves a parsed CRYPTED DATA frame into
// the labelled byte ranges [aad 5 | ciphertext | tag 16] + the 8-B nonce-seed trailer. Build a real CRYPTED frame
// via pack_data (synthetic inner = aad(5)+ct(14)+tag(16)=35, 8-B seed mac), parse it, and check every region maps
// to the right bytes — so the device trace highlights exactly the encrypted span and nothing else.
TEST_CASE("data_crypted_region — carves [aad | ciphertext | tag | seed] of a CRYPTED DATA frame") {
    std::array<uint8_t, 35> inner{};                                   // [dst_hash 4][origin 1][ct 14][tag 16]
    for (size_t i = 0; i < inner.size(); ++i) inner[i] = uint8_t(0x40 + i);
    std::array<uint8_t, 8> seed{};
    for (size_t i = 0; i < 8; ++i) seed[i] = uint8_t(0xA0 + i);
    data_in in{};
    in.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH;                 // CRYPTED requires DST_HASH; mac trailer = 8-B seed
    in.next = 0x21; in.dst = 0x22; in.ctr = 0x1234;
    in.inner = std::span<const uint8_t>(inner.data(), inner.size());
    in.mac   = std::span<const uint8_t>(seed.data(), seed.size());
    std::array<uint8_t, 80> buf{};
    const size_t n = pack_data(in, buf);
    CHECK(n > 0);
    auto d = parse_data(std::span<const uint8_t>(buf.data(), n));
    CHECK(d.has_value());
    if (d) {
        CHECK(d->crypted);
        auto r = data_crypted_region(*d);
        CHECK(r.valid);
        // aad = the first 5 inner bytes (dst_hash 4 + origin 1), CLEARTEXT
        CHECK(r.aad_off == d->inner_off);            CHECK(r.aad_len == 5);
        // ciphertext = the 14 sealed bytes between aad and tag
        CHECK(r.ct_off  == d->inner_off + 5);        CHECK(r.ct_len  == 14);
        // tag = the last 16 inner bytes
        CHECK(r.tag_off == d->inner_off + 35 - 16);  CHECK(r.tag_len == 16);
        // seed = the 8-B nonce-seed MAC trailer, right after the inner
        CHECK(r.seed_off == d->mac_off);             CHECK(r.seed_len == 8);
        // the carved ciphertext bytes are exactly inner[5..19) (proves the offset maps the right span)
        bool ct_ok = true; for (size_t i = 0; i < r.ct_len; ++i) ct_ok = ct_ok && (buf[r.ct_off + i] == inner[5 + i]);
        CHECK(ct_ok);
    }
    // a NON-crypted DATA frame -> invalid (no encrypted region to show)
    data_in plain{}; plain.flags = 0; plain.next = 1; plain.dst = 2;
    std::array<uint8_t, 4> pmac{1,2,3,4}; std::array<uint8_t, 4> pin{9,9,9,9};
    plain.inner = std::span<const uint8_t>(pin.data(), pin.size()); plain.mac = pmac;
    std::array<uint8_t, 40> pbuf{}; const size_t pn = pack_data(plain, pbuf);
    auto pd = parse_data(std::span<const uint8_t>(pbuf.data(), pn));
    CHECK(pd.has_value());
    if (pd) CHECK_FALSE(data_crypted_region(*pd).valid);
}
