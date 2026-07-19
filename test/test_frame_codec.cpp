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
#include "leaf_config.h"   // R6.1: leaf_config_hash golden + sensitivity
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

TEST_CASE("RTS — rejects addr_len > 1 and wrong cmd / short frame") {
    std::array<uint8_t, 7> f{0x12, 0x0A, 0x0B, uint8_t(0x50 | (2 << 1)), 0x0C, 0xC0, 0x14};
    CHECK_FALSE(parse_rts(f).has_value());                 // addr_len = 2 (0/1 valid §mobile Slice 1; 2..7 hierarchy-deferred)
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

TEST_CASE("§2 H — a WANT_PUBKEY query appends the requester's ed_pub[32] (mutual reqpubkey); non-pubkey H unchanged") {
    uint8_t reqpub[32]; for (int i = 0; i < 32; ++i) reqpub[i] = uint8_t(0xA0 + i);
    h_in in{}; in.leaf_id = 2; in.origin = 7; in.key_hash32 = 0x11223344u; in.ttl = 16; in.hard = true; in.want_pubkey = true;
    for (int i = 0; i < 32; ++i) in.requester_ed_pub[i] = reqpub[i];
    std::array<uint8_t, 40> buf{};
    CHECK(pack_h(in, buf) == 40);                              // 8 header + 32 appended requester pubkey
    auto o = parse_h(std::span<const uint8_t>(buf.data(), 40));
    CHECK(o.has_value());
    if (o) {
        CHECK(o->want_pubkey); CHECK(o->hard);
        CHECK(o->origin == 7); CHECK(o->key_hash32 == 0x11223344u);
        bool same = true; for (int i = 0; i < 32; ++i) if (o->requester_ed_pub[i] != reqpub[i]) same = false;
        CHECK(same);                                           // §2: the requester's pubkey round-trips
    }
    // a WANT_PUBKEY frame TRUNCATED to 8 bytes (flag set, pubkey missing) -> REJECT (fail loud, never cache a zero key)
    CHECK_FALSE(parse_h(std::span<const uint8_t>(buf.data(), 8)).has_value());
    // a non-WANT_PUBKEY H is still exactly 8 bytes (the appended field is conditional)
    std::array<uint8_t, 8> b8{};
    CHECK(pack_h({2, 7, 0x11223344u, 16, /*hard=*/true}, b8) == 8);
    auto o8 = parse_h(b8);
    CHECK(o8.has_value());
    if (o8) CHECK_FALSE(o8->want_pubkey);
}

TEST_CASE("§name — a WANT_PUBKEY H appends the requester's [name_len][name] WITH the pubkey (after team_id); nameless stays 40 B (backward-compatible); round-trips") {
    uint8_t reqpub[32]; for (int i = 0; i < 32; ++i) reqpub[i] = uint8_t(0x50 + i);
    h_in in{}; in.leaf_id = 1; in.origin = 9; in.key_hash32 = 0xCAFEBABEu; in.ttl = 12; in.want_pubkey = true;
    for (int i = 0; i < 32; ++i) in.requester_ed_pub[i] = reqpub[i];
    const char* nm = "Alice-Rover"; in.name_len = 11; for (int i = 0; i < 11; ++i) in.name[i] = uint8_t(nm[i]);
    std::array<uint8_t, 8 + 32 + 1 + 32> buf{};
    const size_t n = pack_h(in, buf);
    CHECK(n == 8 + 32 + 1 + 11);                                    // hdr + ed_pub + [name_len=11] + name
    auto o = parse_h(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) {
        CHECK(o->want_pubkey); CHECK(o->name_len == 11);
        bool nameok = true; for (int i = 0; i < 11; ++i) if (o->name[i] != uint8_t(nm[i])) nameok = false;
        CHECK(nameok);                                             // §name: the requester's name round-trips
        bool epub = true; for (int i = 0; i < 32; ++i) if (o->requester_ed_pub[i] != reqpub[i]) epub = false;
        CHECK(epub);                                               // ed_pub still intact before the name
    }
    // nameless WANT_PUBKEY H is UNCHANGED at 40 B (an old sender / no name -> no trailing block)
    h_in bare = in; bare.name_len = 0;
    std::array<uint8_t, 48> b2{};
    CHECK(pack_h(bare, b2) == 40);
    auto o2 = parse_h(std::span<const uint8_t>(b2.data(), 40));
    CHECK(o2.has_value()); if (o2) CHECK(o2->name_len == 0);       // old/nameless -> no trailing name, name_len 0
    // team_scoped + named: name is appended AFTER the team_id (offset unchanged)
    h_in tn = in; tn.team_scoped = true; tn.team_id = 0x99AABBCCu;
    std::array<uint8_t, 8 + 32 + 4 + 1 + 32> b3{};
    const size_t n3 = pack_h(tn, b3);
    CHECK(n3 == 8 + 32 + 4 + 1 + 11);
    auto o3 = parse_h(std::span<const uint8_t>(b3.data(), n3));
    CHECK(o3.has_value());
    if (o3) { CHECK(o3->team_scoped); CHECK(o3->team_id == 0x99AABBCCu); CHECK(o3->name_len == 11); }
    // a name_len byte present but the name truncated -> REJECT (fail loud)
    std::array<uint8_t, 8 + 32 + 2> trunc{}; for (size_t i = 0; i < 40; ++i) trunc[i] = buf[i]; trunc[40] = 11; trunc[41] = 'A';
    CHECK_FALSE(parse_h(std::span<const uint8_t>(trunc.data(), trunc.size())).has_value());   // says name_len=11 but only 1 name byte
}

TEST_CASE("F — RREQ/RREP round-trip + golden + is_reply isolation + reject") {
    for (uint8_t leaf : {0, 3, 15})
        for (uint8_t origin : {0, 0x11, 255})
            for (bool rep : {false, true})
                for (uint8_t b4 : {0, 9, 255})
                    for (uint8_t hops : {0, 4, 255}) {
                        std::array<uint8_t, 9> buf{};
                        CHECK(pack_f({leaf, origin, rep, 0x2A, b4, hops, /*relay=*/0x55, /*config_hash=*/0xABCD}, buf) == 9);
                        auto o = parse_f(buf);
                        CHECK(o.has_value());
                        if (o) {
                            CHECK(o->leaf_id == leaf); CHECK(o->origin == origin);
                            CHECK(o->is_reply == rep); CHECK(o->dst_id == 0x2A);
                            CHECK(o->ttl_or_next_hop == b4); CHECK(o->hops == hops);
                            CHECK(o->relay == 0x55); CHECK(o->config_hash == 0xABCD);   // R6.1 §6.4 fingerprint round-trips
                        }
                    }
    std::array<uint8_t, 9> rreq{}, rrep{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 0, /*relay=*/0x07, /*config_hash=*/0}, rreq) == 9);   // RREQ: byte4 = ttl 8, byte6 = relay 7
    const uint8_t exq[] = {0x83, 0x11, 0x00, 0x2A, 0x08, 0x00, 0x07, 0x00, 0x00};   // +config_hash LE (0)
    for (int i = 0; i < 9; ++i) CHECK(rreq[i] == exq[i]);
    CHECK(pack_f({3, 0x11, true, 0x2A, 9, 4, /*relay=*/0x07, /*config_hash=*/0}, rrep) == 9);    // RREP: byte4 = next_hop 9
    const uint8_t exr[] = {0x83, 0x11, 0x80, 0x2A, 0x09, 0x04, 0x07, 0x00, 0x00};
    for (int i = 0; i < 9; ++i) CHECK(rrep[i] == exr[i]);
    // is_reply isolation: same everything else → only byte2 bit 7 flips.
    std::array<uint8_t, 9> a{}, b{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 4, /*relay=*/0x07, 0}, a) == 9);
    CHECK(pack_f({3, 0x11, true,  0x2A, 8, 4, /*relay=*/0x07, 0}, b) == 9);
    CHECK((a[2] ^ b[2]) == 0x80);
    CHECK(a[0] == b[0]); CHECK(a[1] == b[1]); CHECK(a[3] == b[3]);
    CHECK(a[4] == b[4]); CHECK(a[5] == b[5]); CHECK(a[6] == b[6]); CHECK(a[7] == b[7]); CHECK(a[8] == b[8]);
    std::array<uint8_t, 8> sh{0x83, 0x11, 0x00, 0x2A, 0x08, 0x00, 0x07, 0x00};
    CHECK_FALSE(parse_f(sh).has_value());                 // len < 9 (R6.1: config_hash mandatory)
    // §team-multihop (spec §5): TEAM-plane F — byte-2 b6 = TEAM + team_id (4 B) appended at offset 9 (team F = 13 B).
    std::array<uint8_t, 13> tf{};
    CHECK(pack_f({3, 0x77, false, 0x2A, 8, 0, /*relay=*/0x07, /*config_hash=*/0xABCD, /*team_scoped=*/true, /*team_id=*/0x12345678u}, tf) == 13);
    CHECK((tf[2] & 0x40) != 0);                           // b6 = TEAM
    CHECK((tf[2] & 0x80) == 0);                           // b7 = is_reply stays 0
    const uint8_t ext[] = {0x78, 0x56, 0x34, 0x12};       // team_id LE at bytes 9..12
    for (int i = 0; i < 4; ++i) CHECK(tf[9 + i] == ext[i]);
    auto to = parse_f(tf);
    CHECK(to.has_value());
    if (to) { CHECK(to->team_scoped); CHECK(to->team_id == 0x12345678u);
              CHECK(to->origin == 0x77); CHECK(to->config_hash == 0xABCD); CHECK_FALSE(to->is_reply); }
    // static F (team_scoped defaults false) packs 9 B with b6=0 — byte-identical to the pre-team wire.
    std::array<uint8_t, 13> sf{};
    CHECK(pack_f({3, 0x11, false, 0x2A, 8, 0, /*relay=*/0x07, /*config_hash=*/0}, sf) == 9);
    CHECK((sf[2] & 0x40) == 0);
    auto so = parse_f(std::span<const uint8_t>(sf.data(), 9));
    CHECK(so.has_value());
    if (so) { CHECK_FALSE(so->team_scoped); CHECK(so->team_id == 0); }
    // a team-flagged F truncated to 9 B (b6 set but no appended team_id) -> reject.
    std::array<uint8_t, 9> tt{0x83, 0x11, 0x40, 0x2A, 0x08, 0x00, 0x07, 0x00, 0x00};
    CHECK_FALSE(parse_f(tt).has_value());
}

// ===== C4: J join family (§10 cmd-nibble 0x9; byte1 reading A) ===============

TEST_CASE("J DISCOVER — round-trip (key_hash32 LE) + golden") {
    for (uint8_t leaf : {0, 3, 15})
        for (bool gw : {false, true})
            for (bool mob : {false, true})
                for (uint32_t kh : {0u, 0x11223344u, 0xDEADBEEFu}) {
                    std::array<uint8_t, 9> buf{};
                    const size_t want = mob ? 9u : 6u;      // §S6: a mobile DISCOVER carries the +3-B last-home block
                    CHECK(pack_j_discover({leaf, gw, mob, kh}, buf) == want);
                    auto o = parse_j(std::span<const uint8_t>(buf.data(), want));
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->opcode == 0); CHECK(o->leaf_id == leaf);
                        CHECK(o->gateway_capable == gw); CHECK(o->is_mobile == mob);
                        CHECK(o->key_hash32 == kh);
                    }
                }
    std::array<uint8_t, 9> g{};                              // §S6: mobile DISCOVER = 9 B (last-home block = 0/0/0 fresh)
    CHECK(pack_j_discover({3, true, true, 0x11223344u}, g) == 9);
    const uint8_t ex[] = {0x93, 0xC1, 0x44, 0x33, 0x22, 0x11, 0x00, 0x00, 0x00};
    for (int i = 0; i < 9; ++i) CHECK(g[i] == ex[i]);
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
    const uint8_t ex[] = {0x95, 0xB1, 0x2A, 0xEF, 0xBE, 0xAD, 0xDE, 0x06};
    for (int i = 0; i < 8; ++i) CHECK(g[i] == ex[i]);
}

TEST_CASE("J CLAIM — round-trip (LE u16 lease) + golden") {
    for (uint8_t leaf : {0, 5, 15})
        for (bool mob : {false, true})
            for (uint16_t lease : {0u, 300u, 65535u})
                for (uint8_t epoch : {0, 7, 255}) {
                    std::array<uint8_t, 11> buf{};
                    // §mobile: byte-10 is the NONCE for a static CLAIM, the CHOSEN_HOST_ID for a mobile CLAIM (same slot)
                    j_claim_in in{leaf, false, mob, 0xDEADBEEFu, 0x2A, lease, epoch, /*nonce=*/0x99};
                    if (mob) in.chosen_host_id = 0x77;
                    CHECK(pack_j_claim(in, buf) == 11);
                    auto o = parse_j(buf);
                    CHECK(o.has_value());
                    if (o) {
                        CHECK(o->opcode == 1); CHECK(o->leaf_id == leaf); CHECK(o->is_mobile == mob);
                        CHECK(o->key_hash32 == 0xDEADBEEFu); CHECK(o->proposed_node_id == 0x2A);
                        CHECK(o->lease_age_seconds == lease);
                        CHECK(o->claim_epoch == epoch);
                        if (mob) CHECK(o->chosen_host_id == 0x77);   // §mobile: byte-10 carries the chosen host
                        else     CHECK(o->nonce == 0x99);            // static: byte-10 is the nonce (byte-identical)
                    }
                }
    std::array<uint8_t, 11> g{};   // golden: a mobile CLAIM with chosen_host_id=0x99 -> byte-10 == 0x99 (same bytes as the pre-fix static golden)
    CHECK(pack_j_claim({5, false, true, 0xDEADBEEFu, 0x2A, 300, 7, /*nonce=*/0x99, /*chosen_host=*/0x99}, g) == 11);
    const uint8_t ex[] = {0x95, 0x51, 0xEF, 0xBE, 0xAD, 0xDE, 0x2A, 0x2C, 0x01, 0x07, 0x99};
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
    const uint8_t ex[] = {0x95, 0xA1, 0x2A, 0x44, 0x33, 0x22, 0x11,
                          0xEF, 0xBE, 0xAD, 0xDE, 0xE8, 0x03, 0x03, 0x03};
    for (int i = 0; i < 15; ++i) CHECK(buf[i] == ex[i]);
}

TEST_CASE("J — header flag/opcode isolation + strict-length + wrong-cmd reject") {
    std::array<uint8_t, 6> a{}, b{}; std::array<uint8_t, 9> c{};   // §S6: mobile DISCOVER = 9 B
    CHECK(pack_j_discover({3, false, false, 0}, a) == 6);
    CHECK(pack_j_discover({3, true,  false, 0}, b) == 6);
    CHECK((a[1] ^ b[1]) == 0x80);                          // gateway_capable = bit 7
    CHECK(pack_j_discover({3, false, true,  0}, c) == 9);
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
    CHECK(n == 22);   // 8-B header + 6-B leaf header (R6.1) + 2*4 entries
    const uint8_t ex[] = {0x03, 0x11, 0x02, 0x01, 0xEF, 0xBE, 0xAD, 0xDE,
                          0,0, 0,0, 0,0,                                      // leaf header: lineage 0, epoch 0, config_hash 0 (u16x3)
                          0x05, 0x07, 0xC0, 0x02, 0x09, 0x07, 0xA1, 0x03};
    for (int i = 0; i < 22; ++i) CHECK(buf[i] == ex[i]);

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
        CHECK(o->frame_len == 22);
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
    CHECK(n == 14 + 1 + 4 + 32);   // 8-B header + 6-B leaf header + (1 lc + 4 sched) + 32 bitmap = 51
    const uint8_t head[] = {0x01, 0x05, 0xD0, 0x01, 0xEF, 0xBE, 0xAD, 0xDE,   // 8-B header
                            0,0, 0,0, 0,0,                                    // 6-B leaf header (zeros, u16x3)
                            0x01, 0x26, 0x1E, 0x0F, 0x3C};                    // schedule: lc + 4
    for (int i = 0; i < 19; ++i) CHECK(buf[i] == head[i]);
    CHECK(buf[19 + 0]  == 0x20);   // bitmap now at 14 + 1 + 4 = 19
    CHECK(buf[19 + 1]  == 0x02);
    CHECK(buf[19 + 16] == 0x04);

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
        CHECK(o->frame_len == 51);
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
    CHECK(n == 14 + 1 + 6);   // 8-B header + 6-B leaf header + ext_len + 6 = 21
    CHECK(buf[2] == 0x08);   // only has_ext, n_entries 0
    CHECK(buf[14] == 0x06);  // ext_len (after the 6-B leaf header)
    for (int i = 0; i < 6; ++i) CHECK(buf[15 + i] == ext_payload[i]);

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->has_ext);
        CHECK_FALSE(o->has_schedule);
        CHECK_FALSE(o->has_seen_bitmap);
        CHECK(o->n_entries == 0);
        CHECK(o->ext_len == 6);
        CHECK(o->frame_len == 21);
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
        std::array<uint8_t, 14 + 63 * 4> buf{};
        size_t n = pack_beacon(in, buf);
        CHECK(n == size_t(14 + count * 4));   // 8-B header + 10-B leaf header + entries
        CHECK(buf[2] == uint8_t(count & 0x07));                       // flags 0, n_lo
        CHECK(buf[3] == uint8_t(((((count >> 3) & 0x07) << 5)) | protocol::wire_version));   // n_hi | wire_version (§7c)

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
    CHECK(n == size_t(14 + 1 + 5 * 4));   // + 6-B leaf header
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
    CHECK(n == size_t(14 + (1 + 4) + (2 * 4) + 32 + (1 + 2)));   // + 6-B leaf header = 62
    CHECK(buf[2] == 0xFA);   // has_schedule|self_gw|is_mobile|seen_bm|ext | n_lo(2) = 0xF8|0x02
    CHECK(buf[3] == protocol::wire_version);   // n_entries_hi 0 | wire_version (§7c)
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
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 18);   // 8-B header + 6-B leaf header + 4 entry
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        auto e = parse_beacon_entry(fr, *o, 0);
        CHECK(e.has_value());
        if (e) { CHECK(e->dest == 0xFE); CHECK(e->next == 0xFD);
                 CHECK(e->score_bucket == 0xF); CHECK(e->is_gateway); CHECK(e->hops == 255); }
        // forge the entry's rsv bits (byte2 bits 1..3) -> parse must mask them off
        std::array<uint8_t, 32> forged{};
        for (size_t i = 0; i < n; ++i) forged[i] = buf[i];
        forged[14 + 2] |= 0x0E;   // entry byte2 is now at offset 14 (after the 6-B leaf header)
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

TEST_CASE("R6.1 BCN — leaf header (lineage/epoch/config_hash, u16x3) round-trips at bytes 8..13 LE") {
    beacon_in in{};
    in.leaf_id = 2; in.src = 7; in.key_hash32 = 0xCAFEBABE;
    in.lineage_id = 0x1122; in.config_epoch = 0x0507; in.config_hash = 0x99AA;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 14);   // 8-B header + 6-B leaf header, no body
    CHECK(buf[8]  == 0x22); CHECK(buf[9]  == 0x11);   // lineage_id LE
    CHECK(buf[10] == 0x07); CHECK(buf[11] == 0x05);   // config_epoch LE
    CHECK(buf[12] == 0xAA); CHECK(buf[13] == 0x99);   // config_hash LE
    auto o = parse_beacon(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->lineage_id == 0x1122); CHECK(o->config_epoch == 0x0507); CHECK(o->config_hash == 0x99AA); }
}

TEST_CASE("leaf_config_hash — deterministic, sensitive to every input, never 0 (the filter sentinel)") {
    using meshroute::leaf_config_hash;
    // bitmap {7,9} (wire u8 0x14), duty 1% (bp 100), frac_bp 1250, ch_interval 10000ms, dm_interval 3000ms, name "hub"
    const uint16_t h = leaf_config_hash(0x0280, 100, 1250, 10000, 3000, "hub", 3);
    CHECK(h == leaf_config_hash(0x0280, 100, 1250, 10000, 3000, "hub", 3));   // deterministic
    CHECK(h != 0);                                                            // BLAKE2b never 0 — the config_hash==0 sentinel depends on this
    CHECK(h != leaf_config_hash(0x0080, 100, 1250, 10000, 3000, "hub", 3));   // sf-set-sensitive (wire u8 differs)
    CHECK(h != leaf_config_hash(0x0280, 200, 1250, 10000, 3000, "hub", 3));   // duty-sensitive (duty_bp differs)
    CHECK(h != leaf_config_hash(0x0280, 100, 2500, 10000, 3000, "hub", 3));   // active_fraction-sensitive (anti-spam v2)
    CHECK(h != leaf_config_hash(0x0280, 100, 1250, 20000, 3000, "hub", 3));   // ch_interval-sensitive (anti-spam v2)
    CHECK(h != leaf_config_hash(0x0280, 100, 1250, 10000, 5000, "hub", 3));   // dm_interval-sensitive (anti-spam v2)
    CHECK(h != leaf_config_hash(0x0280, 100, 1250, 10000, 3000, "hu2", 3));   // name-sensitive
    CHECK(h != leaf_config_hash(0x0280, 100, 1250, 10000, 3000, "hub", 2));   // name-length-sensitive
    CHECK(h == 46126u);      // GOLDEN (C-frame wire form): BLAKE2b-512({14 64 00 E2 04 10 27 B8 0B 03 'h' 'u' 'b'})[:2] LE
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
    CHECK(n == size_t(14 + 5 + 4 + 32 + 3));   // + 6-B leaf header = 58

    std::array<uint8_t, 8> wrong{};
    for (int i = 0; i < 8; ++i) wrong[i] = buf[i];
    wrong[0] = wire::cmd_byte(wire::Cmd::D, 1);
    CHECK_FALSE(parse_beacon(wrong).has_value());                              // wrong cmd
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 13)).has_value());        // < 14-B header+leaf (R6.1 min)
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 14 + 1 + 2)).has_value()); // mid-schedule
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 14 + 5 + 2)).has_value()); // mid-entries
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), 14 + 5 + 4 + 16)).has_value()); // mid-bitmap
    CHECK_FALSE(parse_beacon(std::span<const uint8_t>(buf.data(), n - 1)).has_value());     // ext_len past end

    std::array<uint8_t, 32> tb{};
    const beacon_entry me[] = {{0x05, 0x07, 0xC, false, 2}};
    beacon_in min_in{};
    min_in.leaf_id = 3; min_in.src = 0x11; min_in.key_hash32 = 0xDEADBEEF;
    min_in.entries = me; min_in.seen_bitmap = {};
    size_t mn = pack_beacon(min_in, tb);
    CHECK(mn == 18);   // 8-B header + 6-B leaf header + 4 entry
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
    CHECK(pack_beacon(oe255, big) == size_t(14 + 1 + 255));                    // 255-B ext boundary packs (+6-B leaf header)
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
    al[0] = static_cast<uint8_t>(al[0] | 0x04);   // addr_len=2 in byte0 bits 3..1
    CHECK_FALSE(parse_data(al).has_value());                                   // addr_len > 1 (0/1 valid §mobile Slice 1; 2..7 deferred)
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

    data_in bad_al = in; bad_al.addr_len = 2;
    CHECK(pack_data(bad_al, buf) == 0);                                        // addr_len > 1 (0/1 valid §mobile Slice 1; 2..7 deferred)
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
    // §1c: a CRYPTED inner exposes NO cleartext origin — the parse stops at dst_hash and hands back ct||tag as body.
    { const uint8_t in[] = { 0x78, 0x56, 0x34, 0x12, 0xAB, 0xCD };   // [dst_hash 4 LE = 0x12345678][sealed ct/tag bytes]
      auto u = parse_unicast_inner(std::span<const uint8_t>(in, sizeof in), DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH);
      CHECK(u.has_value());
      if (u) { CHECK(u->has_dst_hash); CHECK(u->dst_key_hash32 == 0x12345678u);
               CHECK(u->origin == 0); CHECK_FALSE(u->has_source_hash);     // origin + source_hash are SEALED, not read raw
               CHECK(u->body.size() == 2); CHECK(u->body[0] == 0xAB); CHECK(u->body[1] == 0xCD); } }
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

TEST_CASE("§1c parse_unicast_inner — CRYPTED stops at dst_hash; origin is SEALED, the whole rest is opaque body") {
    uint8_t inner[4 + 11];
    inner[0] = 0x44; inner[1] = 0x33; inner[2] = 0x22; inner[3] = 0x11;     // dst_key_hash32 = 0x11223344 LE
    for (int i = 0; i < 11; ++i) inner[4 + i] = static_cast<uint8_t>(0xA0 + i);  // sealed ct||tag (opaque — origin is INSIDE)
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    auto u = parse_unicast_inner(std::span<const uint8_t>(inner, sizeof inner), flags);
    CHECK(u.has_value());
    if (u) {
        CHECK(u->has_dst_hash); CHECK(u->dst_key_hash32 == 0x11223344u);
        CHECK(u->origin == 0);                                             // §1c: NO cleartext origin (sealed in pt[0])
        CHECK_FALSE(u->has_source_hash);                                   // source_hash sealed too — NOT read raw
        CHECK_FALSE(u->has_location);
        CHECK(u->body.size() == 11);                                       // everything after dst_hash = the sealed region (ct||tag)
        CHECK(u->body[0] == 0xA0); CHECK(u->body[10] == 0xAA);
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
// the labelled byte ranges [aad 4 | ciphertext | tag 16] + the 8-B nonce-seed trailer. Build a real CRYPTED frame
// via pack_data (synthetic inner = aad(4)+ct(15)+tag(16)=35, 8-B seed mac), parse it, and check every region maps
// to the right bytes — so the device trace highlights exactly the encrypted span and nothing else. (§1c: origin is
// SEALED inside the ct, so the cleartext aad is just dst_hash 4.)
TEST_CASE("data_crypted_region — carves [aad | ciphertext | tag | seed] of a CRYPTED DATA frame") {
    std::array<uint8_t, 35> inner{};                                   // [dst_hash 4][ct 15][tag 16]
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
        // aad = the first 4 inner bytes (dst_hash 4), CLEARTEXT (§1c: origin moved into the ct)
        CHECK(r.aad_off == d->inner_off);            CHECK(r.aad_len == 4);
        // ciphertext = the 15 sealed bytes between aad and tag
        CHECK(r.ct_off  == d->inner_off + 4);        CHECK(r.ct_len  == 15);
        // tag = the last 16 inner bytes
        CHECK(r.tag_off == d->inner_off + 35 - 16);  CHECK(r.tag_len == 16);
        // seed = the 8-B nonce-seed MAC trailer, right after the inner
        CHECK(r.seed_off == d->mac_off);             CHECK(r.seed_len == 8);
        // the carved ciphertext bytes are exactly inner[4..19) (proves the offset maps the right span)
        bool ct_ok = true; for (size_t i = 0; i < r.ct_len; ++i) ct_ok = ct_ok && (buf[r.ct_off + i] == inner[4 + i]);
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

// §F2 / seen-bitmap cost-reduction: the TRUE byte-budget beacon-entry cap. Replaces the old fixed
// kMaxBeaconEntries=27 (which ignored the variable ext/schedule blocks → a full page + a populated
// ext TLV silently overflowed beacon_max_bytes → the whole beacon was DROPPED, node_beacon.cpp:316).
TEST_CASE("§F2 beacon_max_entries — true byte-budget cap (header + schedule + bitmap + ext)") {
    const size_t CAP = 151;   // protocol::beacon_max_bytes
    // (a) bare beacon (no schedule / no bitmap / no ext) -> 34 = (151-8-6)/4 (R6.1: +6-B leaf header in the overhead)
    CHECK(beacon_max_entries(CAP, /*sched*/0, /*bitmap*/0, /*ext_block*/0) == 34);
    // (b) bitmap present, nothing else -> 26 = (151-8-6-32)/4
    CHECK(beacon_max_entries(CAP, 0, 32, 0) == 26);
    // (c) F2 REGRESSION GUARD: a 2-leaf gateway schedule + bitmap + a 12-B ext TLV must NOT overflow —
    //     the cap shrinks so the whole frame still fits beacon_max_bytes.
    const size_t sched = 1 + 4 * 2;        // schedule block: 1-B nibble + 2×4-B records
    const size_t extbk = 1 + 12;           // ext block: 1-B ext_len + 12-B payload
    const uint8_t c = beacon_max_entries(CAP, sched, 32, extbk);
    CHECK(14 + sched + 32 + extbk + static_cast<size_t>(c) * 4 <= CAP);   // 8-B header + 6-B leaf header + blocks; total fits
    // clamp to the 6-bit n_entries field even with a huge frame
    CHECK(beacon_max_entries(1000, 0, 0, 0) == 63);
    // overhead exceeding the frame -> 0 entries (no underflow)
    CHECK(beacon_max_entries(CAP, 0, 32, 1 + 200) == 0);
}

TEST_CASE("R6.2 Q CONFIG_PULL — round-trips lineage + epoch (8 B); truncation rejected") {
    q_in in{}; in.leaf_id = 3; in.src = 7; in.dest = 9; in.opcode = q_opcode::config_pull;
    in.pull_lineage = 0xBEEF; in.pull_epoch = 0x0102;
    std::array<uint8_t, 16> buf{};
    size_t n = pack_q(in, buf);
    CHECK(n == 8);
    auto o = parse_q(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->opcode == static_cast<uint8_t>(q_opcode::config_pull));
             CHECK(o->pull_lineage == 0xBEEF); CHECK(o->pull_epoch == 0x0102); }
    CHECK_FALSE(parse_q(std::span<const uint8_t>(buf.data(), 4)).has_value());   // no lineage/epoch -> reject
}

TEST_CASE("C config frame body — round-trips sf_list(u8)/duty_bp/frac/chI/dmI/epoch/name (§2)") {
    meshroute::CConfig in{};
    in.allowed_sf_bitmap = (1u << 7) | (1u << 9);   // SF7 + SF9 -> wire bits 2 + 4 = 0b00010100 = 0x14
    in.duty_bp = 10;                                  // 0.1% (§7) -> 2-B wire field, NOT the old u32 duty_ppm
    in.active_fraction_bp = 2500;                     // anti-spam v2 knobs — non-default so a round-trip drop would show
    in.ch_interval_ms = 15000;
    in.dm_interval_ms = 4000;
    in.config_epoch = 7;
    in.leaf_name_len = 3; in.leaf_name[0]='h'; in.leaf_name[1]='u'; in.leaf_name[2]='b';
    uint8_t buf[32]; size_t n = meshroute::pack_c_config(in, buf, sizeof buf);
    CHECK(n == 12 + 3);                               // 1 sf + 2 duty + 2 frac + 2 chI + 2 dmI + 2 epoch + 1 nlen + 3 name
    CHECK(buf[0] == 0x14);                            // the u8 SF list (SF5..12 packed to bits 0..7)
    meshroute::CConfig out{};
    CHECK(meshroute::parse_c_config(buf, n, out));
    CHECK(out.allowed_sf_bitmap == ((1u << 7) | (1u << 9)));   // unpacks to the IDENTICAL internal bitmap
    CHECK(out.duty_bp == 10);
    CHECK(out.active_fraction_bp == 2500); CHECK(out.ch_interval_ms == 15000); CHECK(out.dm_interval_ms == 4000);
    CHECK(out.config_epoch == 7);
    CHECK(out.leaf_name_len == 3); CHECK(out.leaf_name[0]=='h'); CHECK(out.leaf_name[2]=='b');
    CHECK_FALSE(meshroute::parse_c_config(buf, 11, out));       // truncated (< 12-B prefix)
    CHECK_FALSE(meshroute::parse_c_config(buf, 4, out));        // grossly short
}

TEST_CASE("C config frame — name truncates at leaf_name_max(10), identically on both sides (§5)") {
    meshroute::CConfig in{};
    in.allowed_sf_bitmap = (1u << 8); in.duty_bp = 1000; in.config_epoch = 2;
    const char* long_name = "abcdefghijKLMNO";          // 15 chars -> must cut to 10
    in.leaf_name_len = 15; for (uint8_t i = 0; i < 15; ++i) in.leaf_name[i] = long_name[i % 10];
    uint8_t buf[32]; size_t n = meshroute::pack_c_config(in, buf, sizeof buf);
    CHECK(n == 12 + meshroute::protocol::leaf_name_max);       // packed length is capped at 10
    meshroute::CConfig out{};
    CHECK(meshroute::parse_c_config(buf, n, out));
    CHECK(out.leaf_name_len == meshroute::protocol::leaf_name_max);   // == 10
}

TEST_CASE("leaf_config_hash — equal across the sf-bitmap/duty_bp wire forms (§5 no-re-pull)") {
    // The mother hashes its INTERNAL bitmap + duty_bp; a joiner that unpacked the SAME wire forms must hash identically
    // (else config_hash diverges -> perpetual re-pull). duty entered finer than 0.01% must quantize to the same bp.
    const uint16_t bm = (1u << 6) | (1u << 7);                 // SF6 + SF7
    const uint16_t bp = meshroute::duty_to_bp(0.001);          // 0.1% -> 10
    CHECK(bp == 10);
    const uint16_t h1 = meshroute::leaf_config_hash(bm, bp, 1250, 10000, 3000, "hub", 3);
    // a joiner that received sf_list u8 + duty_bp on the wire, unpacked, then re-derived bp from the lossy double:
    const uint16_t bp2 = meshroute::duty_to_bp(meshroute::bp_to_duty(bp));   // round-trip stable
    CHECK(bp2 == bp);
    // frac/interval knobs also round-trip through their wire forms (frac_to_bp/bp_to_frac, ms_to_u16) unchanged
    const uint16_t fbp = meshroute::frac_to_bp(meshroute::bp_to_frac(1250));
    CHECK(fbp == 1250);
    const uint16_t h2 = meshroute::leaf_config_hash(meshroute::sf_wire_to_bitmap(meshroute::sf_bitmap_to_wire(bm)), bp2, fbp, 10000, 3000, "hub", 3);
    CHECK(h1 == h2);
    // a finer-than-0.01% duty quantizes to the SAME bp on both sides
    CHECK(meshroute::duty_to_bp(0.123456) == meshroute::duty_to_bp(0.1235));
}

TEST_CASE("BCN §7c — wire_version in byte-3 low nibble; round-trips + readable at a fixed offset cross-version") {
    beacon_entry e{0x05, 0x07, 0xC, false, 2};
    beacon_in in{}; in.leaf_id = 3; in.src = 0x11; in.key_hash32 = 0xDEADBEEF;
    in.entries = std::span<const beacon_entry>(&e, 1);
    std::array<uint8_t, 64> buf{}; size_t n = pack_beacon(in, buf);
    CHECK(n > 0);
    CHECK((buf[3] & 0x0F) == protocol::wire_version);                 // stamped in the low nibble (+0 B)
    auto o = parse_beacon(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) CHECK(o->wire_version == protocol::wire_version);
    buf[3] = static_cast<uint8_t>((buf[3] & 0xF0) | 0x02);            // a foreign version at the SAME fixed offset
    auto o2 = parse_beacon(std::span<const uint8_t>(buf.data(), n));
    if (o2) CHECK(o2->wire_version == 2);                             // readable even when the format would differ
}

// ── Asymmetric-link-aware routing, SLICE 1: the two inert wire bits ───────────
TEST_CASE("BCN — route-entry degraded bit (byte-2 b3) round-trips, default 0, leaves score/gw untouched") {
    beacon_entry ents[2] = {
        {0x05, 0x07, 0xC, false, 2},   // degraded defaults false in struct
        {0x09, 0x07, 0xA, true,  3},
    };
    ents[0].degraded = true;
    beacon_in in{};
    in.leaf_id = 3; in.src = 0x11; in.key_hash32 = 0xDEADBEEF;
    in.entries = std::span<const beacon_entry>(ents, 2);
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 22);                                          // 8 hdr + 6 leaf-cfg + 2*4 entries, no schedule/bitmap/ext
    CHECK(buf[16] == 0xC8);   // entry0 byte2 = score(0xC<<4)|degraded(0x08)|gw(0); entries at 14 -> byte2 at 14+2
    CHECK(buf[20] == 0xA1);   // entry1 byte2 = score(0xA<<4)|gw(0x01), NOT degraded
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        auto e0 = parse_beacon_entry(fr, *o, 0);
        CHECK(e0.has_value());
        if (e0) { CHECK(e0->degraded); CHECK(e0->score_bucket == 0xC); CHECK_FALSE(e0->is_gateway); }
        auto e1 = parse_beacon_entry(fr, *o, 1);
        CHECK(e1.has_value());
        if (e1) { CHECK_FALSE(e1->degraded); CHECK(e1->score_bucket == 0xA); CHECK(e1->is_gateway); }  // default-0 survives
    }
}

TEST_CASE("BCN — heard_set_complete flag (byte-3 b4) round-trips and wire_version SURVIVES") {
    const beacon_entry ents[2] = {
        {0x05, 0x07, 0xC, false, 2},
        {0x09, 0x07, 0xA, true,  3},
    };
    beacon_in in{};
    in.leaf_id = 3; in.src = 0x11; in.key_hash32 = 0xDEADBEEF;
    in.entries = std::span<const beacon_entry>(ents, 2);
    in.heard_set_complete = true;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 22);
    CHECK(buf[3] == uint8_t(0x10 | (protocol::wire_version & 0x0F)));   // complete(b4) | wire_version(b3..0); n_entries_hi=0
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->heard_set_complete);
        CHECK(o->wire_version == protocol::wire_version);   // b3..0 UNTOUCHED by the complete flag (MF1)
        CHECK(o->n_entries == 2);                           // n_entries_hi (b7..5) untouched
    }
    // OLD-FORMAT frame: clear b4, keep wire_version -> parses as not-complete, version intact
    std::array<uint8_t, 32> old = buf;
    old[3] = static_cast<uint8_t>(old[3] & ~0x10);          // strip the complete bit (old emitter never set it)
    auto oo = parse_beacon(std::span<const uint8_t>(old.data(), n));
    CHECK(oo.has_value());
    if (oo) {
        CHECK_FALSE(oo->heard_set_complete);
        CHECK(oo->wire_version == protocol::wire_version);
        CHECK(oo->n_entries == 2);
    }
}

TEST_CASE("BCN — both new bits coexist; old-format (all-zero new bits) parses clean") {
    beacon_entry ents[2] = {
        {0x12, 0x12, 0x7, false, 1},   // a hops==1 direct-neighbour-style entry, flagged degraded
        {0x40, 0x12, 0x3, false, 2},
    };
    ents[0].degraded = true;
    beacon_in in{};
    in.leaf_id = 1; in.src = 0x12; in.key_hash32 = 0x01020304;
    in.entries = std::span<const beacon_entry>(ents, 2);
    in.heard_set_complete = true;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 22);
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    CHECK(o.has_value());
    if (o) {
        CHECK(o->heard_set_complete);
        CHECK(o->wire_version == protocol::wire_version);
        auto e0 = parse_beacon_entry(fr, *o, 0); CHECK(e0.has_value()); if (e0) CHECK(e0->degraded);
        auto e1 = parse_beacon_entry(fr, *o, 1); CHECK(e1.has_value()); if (e1) CHECK_FALSE(e1->degraded);
    }
    // OLD emitter: byte-3 b4 clear AND every entry byte-2 b3 clear -> default-0 reads, payload untouched
    std::array<uint8_t, 32> old = buf;
    old[3]  = static_cast<uint8_t>(old[3]  & ~0x10);   // clear complete
    old[16] = static_cast<uint8_t>(old[16] & ~0x08);   // clear entry0 degraded (byte2 at 14+2)
    auto oo2 = parse_beacon(std::span<const uint8_t>(old.data(), n));
    CHECK(oo2.has_value());
    if (oo2) {
        CHECK_FALSE(oo2->heard_set_complete);
        CHECK(oo2->wire_version == protocol::wire_version);
        auto oe0 = parse_beacon_entry(std::span<const uint8_t>(old.data(), n), *oo2, 0);
        CHECK(oe0.has_value());
        if (oe0) { CHECK_FALSE(oe0->degraded); CHECK(oe0->score_bucket == 0x7); }   // payload bits untouched by masking
    }
}

TEST_CASE("mobile marks — RTS/DATA addr_len + RTS/ACK MOBILE round-trip (Slice 1)") {
    uint8_t buf[43];

    // RTS: addr_len=1 (mobile-next) + mobile_src round-trips
    rts_in ri{}; ri.leaf_id=4; ri.src=17; ri.next=42; ri.ctr_lo=3;
    ri.dst=42; ri.sf_index=0; ri.rts_flags=0; ri.payload_len=10;
    ri.addr_len=1; ri.mobile_src=true;
    size_t n = pack_rts(ri, buf); CHECK(n == 7);
    auto ro = parse_rts({buf, n});
    CHECK(ro.has_value());
    if (ro) { CHECK(ro->addr_len == 1); CHECK(ro->mobile_src == true);
              CHECK(ro->src == 17); CHECK(ro->next == 42); }

    // RTS marks default clear (backward-compat)
    rts_in ri0{}; ri0.leaf_id=4; ri0.src=17; ri0.next=42; ri0.ctr_lo=3; ri0.dst=42; ri0.payload_len=1;
    n = pack_rts(ri0, buf); CHECK(n == 7);
    auto ro0 = parse_rts({buf, n});
    CHECK(ro0.has_value());
    if (ro0) { CHECK(ro0->addr_len == 0); CHECK(ro0->mobile_src == false); }

    // addr_len=2 rejected: craft byte-3 with addr_len=2 in an otherwise-valid RTS -> parse nullopt
    buf[0] = wire::cmd_byte(wire::Cmd::R, 4);
    buf[1] = 17; buf[2] = 42;
    buf[3] = static_cast<uint8_t>((3u << 4) | (2u << 1));   // ctr_lo=3 | addr_len=2
    buf[4] = 42; buf[5] = 0; buf[6] = 1;
    CHECK_FALSE(parse_rts({buf, 7}).has_value());

    // ACK: mobile_to round-trips, and warn stays independent
    ack_in ai{}; ai.ctr_lo=3; ai.budget_hint=1; ai.snr_bucket=2; ai.to=42; ai.warn=true; ai.mobile_to=true;
    n = pack_ack(ai, buf); CHECK(n == 3);
    auto ao = parse_ack({buf, n});
    CHECK(ao.has_value());
    if (ao) { CHECK(ao->mobile_to == true); CHECK(ao->warn == true); CHECK(ao->to == 42); }
    // ACK mobile_to defaults clear
    ack_in ai0{}; ai0.ctr_lo=3; ai0.to=42;
    n = pack_ack(ai0, buf); CHECK(n == 3);
    auto ao0 = parse_ack({buf, n});
    CHECK(ao0.has_value());
    if (ao0) { CHECK(ao0->mobile_to == false); }

    // DATA: addr_len=1 (mobile-next) round-trips; addr_len=2 rejected at pack
    const uint8_t inner[] = {0x07, 0xAA, 0xBB};
    const uint8_t mac[]   = {0, 0, 0, 0};
    data_in di{};
    di.addr_len = 1; di.flags = 0; di.next = 0x0B; di.dst = 0x0C;
    di.hops_remaining = 10; di.committed_hops = 2; di.prev_fwd_rt_hops = 3;
    di.ctr = 0x1234; di.inner = inner; di.mac = mac;
    std::array<uint8_t, 32> dbuf{};
    size_t dn = pack_data(di, dbuf); CHECK(dn == 15);
    auto dobj = parse_data({dbuf.data(), dn});
    CHECK(dobj.has_value());
    if (dobj) CHECK(dobj->addr_len == 1);
    di.addr_len = 2;                                  // 2..7 hierarchy-deferred -> pack refuses
    CHECK(pack_data(di, dbuf) == 0);
}

TEST_CASE("J mobile OFFER — 13-B round-trip (Slice 2a + §S6 target hash); normal OFFER stays 8-B") {
    j_offer_in m{}; m.leaf_id=4; m.is_mobile=true; m.responder_node_id=7; m.responder_key_hash32=0xABCD1234;
    m.data_sf_bitmap=0x06; m.proposed_mobile_id=33; m.target_key_hash32=0xFEEDBEEFu;
    uint8_t buf[16]; size_t n = pack_j_offer(m, buf); CHECK(n == 13);
    auto o = parse_j({buf, n}); CHECK(o.has_value());
    if (o) { CHECK(o->opcode == (uint8_t)j_opcode::offer); CHECK(o->is_mobile); CHECK(o->proposed_mobile_id == 33); CHECK(o->target_key_hash32 == 0xFEEDBEEFu);
             CHECK(o->responder_node_id == 7); }
    j_offer_in s{}; s.leaf_id=4; s.is_mobile=false; s.responder_node_id=7; s.responder_key_hash32=0xABCD1234; s.data_sf_bitmap=0x06;
    n = pack_j_offer(s, buf); CHECK(n == 8);                 // static OFFER unchanged (8-B)
    auto o2 = parse_j({buf, n}); CHECK(o2.has_value());
    if (o2) { CHECK_FALSE(o2->is_mobile); }
    // a 9-B frame parsed as an 8-B (is_mobile=0) offer is length-rejected, and vice-versa (exact-length per opcode)
    m.is_mobile=true; n = pack_j_offer(m, buf); buf[1] = static_cast<uint8_t>(buf[1] & ~0x40);   // clear is_mobile in byte1
    CHECK_FALSE(parse_j({buf, n}).has_value());              // 9-B body but is_mobile=0 -> length mismatch
}

TEST_CASE("§mobile 4a — hash_bind_inner: mobile variant packs the epoch (7 B); normal is byte-identical (6 B)") {
    hash_bind_inner in{}; in.target_layer=4; in.node_id=19; in.key_hash32=0xDEADBEEFu; in.epoch=42;
    // normal (6 B, no epoch) — byte-identical to before
    std::array<uint8_t,7> nb{};
    CHECK(pack_hash_bind_inner(in, nb, /*mobile=*/false) == 6);
    auto no = parse_hash_bind_inner(std::span<const uint8_t>(nb.data(), 6));
    CHECK(no.has_value());
    if (no) { CHECK(no->node_id==19); CHECK(no->key_hash32==0xDEADBEEFu); CHECK(no->epoch==0); }   // the 6-B form has no epoch
    // mobile (7 B, +epoch)
    std::array<uint8_t,7> mb{};
    CHECK(pack_hash_bind_inner(in, mb, /*mobile=*/true) == 7);
    auto mo = parse_hash_bind_inner(std::span<const uint8_t>(mb.data(), 7));
    CHECK(mo.has_value());
    if (mo) { CHECK(mo->node_id==19); CHECK(mo->key_hash32==0xDEADBEEFu); CHECK(mo->epoch==42); }   // ★ epoch round-trips
    for (int i=0;i<6;i++) CHECK(nb[i]==mb[i]);               // ★ the 6-B prefix is identical (normal answer untouched)
}

TEST_CASE("§mobile 5a — LayerRecord codec round-trips (with a name, and name-less)") {
    LayerRecord r{}; r.layer_id=7; r.freq_khz=868100; r.sf=9; r.bw_hz=125000; r.name_len=4;
    r.name[0]='t'; r.name[1]='e'; r.name[2]='s'; r.name[3]='t';
    std::array<uint8_t,32> buf{};
    const size_t n = pack_layer_record(r, buf);
    CHECK(n == 15);                                          // 11 + name(4)
    size_t consumed = 0;
    auto o = parse_layer_record(std::span<const uint8_t>(buf.data(), n), consumed);
    CHECK(o.has_value());
    CHECK(consumed == 15);
    if (o) { CHECK(o->layer_id==7); CHECK(o->freq_khz==868100u); CHECK(o->sf==9); CHECK(o->bw_hz==125000u);
             CHECK(o->name_len==4); CHECK(o->name[0]=='t'); CHECK(o->name[3]=='t'); }
    LayerRecord r2{}; r2.layer_id=3; r2.freq_khz=915000; r2.sf=7; r2.bw_hz=250000;   // name-less = 11 B
    std::array<uint8_t,16> b2{};
    CHECK(pack_layer_record(r2, b2) == 11);
    size_t c2=0; auto o2 = parse_layer_record(std::span<const uint8_t>(b2.data(), 11), c2);
    CHECK(o2.has_value()); if (o2) { CHECK(o2->layer_id==3); CHECK(o2->name_len==0); }
}

// ---------------------------------------------------------------------------
// §S6 presence plane — P-probe / P-roster (cmd 0xC) + j_discover +3 B last-home
// ---------------------------------------------------------------------------
TEST_CASE("P-probe rev2 — check (8 B) golden + round-trip; searching derived from selected==0") {
    p_probe_in in{}; in.selected_home_id = 103; in.selected_home_layer = 4; in.key_hash32 = 0x2716EFCD; in.reg_epoch = 5;
    std::array<uint8_t, 42> buf{};
    const size_t n = pack_p_probe(in, buf);
    CHECK(n == 8);
    CHECK(buf[0] == 0xC0);                                   // cmd P, dir=probe, no flags
    CHECK(buf[1] == 103); CHECK(buf[2] == 4);                // selected pair
    CHECK(buf[3] == 0xCD); CHECK(buf[4] == 0xEF); CHECK(buf[5] == 0x16); CHECK(buf[6] == 0x27);  // key LE
    CHECK(buf[7] == 5);
    auto o = parse_p_probe(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->key_hash32 == 0x2716EFCD); CHECK(o->reg_epoch == 5);
             CHECK(o->selected_home_id == 103); CHECK(o->selected_home_layer == 4);
             CHECK_FALSE(o->searching()); CHECK_FALSE(o->has_last_home); CHECK_FALSE(o->has_pubkey); }
    // searching (selected==0)
    p_probe_in s{}; s.key_hash32 = 1; std::array<uint8_t,42> sb{}; CHECK(pack_p_probe(s, sb) == 8);
    auto so = parse_p_probe(std::span<const uint8_t>(sb.data(), 8));
    CHECK(so.has_value()); if (so) CHECK(so->searching());
}
TEST_CASE("P-probe rev2 — searching + last_home (10 B) flag order") {
    p_probe_in in{}; in.has_last_home = true;   // selected 0/0 = searching
    in.key_hash32 = 0x11223344; in.reg_epoch = 9; in.last_home_id = 103; in.last_home_layer = 7;
    std::array<uint8_t, 42> buf{};
    const size_t n = pack_p_probe(in, buf);
    CHECK(n == 10);
    CHECK(buf[0] == (0xC0 | P_PROBE_HAS_LAST_HOME));
    CHECK(buf[8] == 103); CHECK(buf[9] == 7);
    auto o = parse_p_probe(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->searching()); CHECK(o->has_last_home); CHECK_FALSE(o->has_pubkey);
             CHECK(o->last_home_id == 103); CHECK(o->last_home_layer == 7); }
}
TEST_CASE("P-probe rev2 — registering: selected + last_home + pubkey (42 B) fields in flag-bit order") {
    p_probe_in in{}; in.selected_home_id = 40; in.selected_home_layer = 4; in.has_last_home = true; in.has_pubkey = true;
    in.key_hash32 = 0xAABBCCDD; in.reg_epoch = 1; in.last_home_id = 50; in.last_home_layer = 4;
    in.ed_pub[0] = 0xDD; in.ed_pub[1] = 0xCC; in.ed_pub[2] = 0xBB; in.ed_pub[3] = 0xAA;  // self-consistent w/ key LE
    for (int i = 4; i < 32; ++i) in.ed_pub[i] = static_cast<uint8_t>(0xDD + i);
    std::array<uint8_t, 64> buf{};
    const size_t n = pack_p_probe(in, buf);
    CHECK(n == 42);
    CHECK(buf[8] == 50); CHECK(buf[9] == 4);                 // last_home block first
    CHECK(buf[10] == 0xDD); CHECK(buf[13] == 0xAA);          // then ed_pub
    auto o = parse_p_probe(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->has_pubkey); CHECK(o->ed_pub[0] == 0xDD); CHECK(o->ed_pub[3] == 0xAA); }
}
TEST_CASE("P-probe/P-roster — reject wrong cmd / wrong dir / short-for-flag") {
    // a roster frame must not parse as a probe (dir bit)
    std::array<uint8_t, 40> rb{};
    PRosterEntry ents[1] = {{0x01020304, 20, 3, protocol::presence_q_ok, true}};
    p_roster_in ri{}; ri.home_id = 103; ri.home_layer = 4; ri.dir_epoch = 2; ri.entries = ents; ri.count = 1;
    const size_t rn = pack_p_roster(ri, rb);
    CHECK(parse_p_probe(std::span<const uint8_t>(rb.data(), rn)) == std::nullopt);
    // a probe must not parse as a roster
    p_probe_in pi{}; pi.key_hash32 = 1; std::array<uint8_t, 10> pb{};
    const size_t pn = pack_p_probe(pi, pb);
    CHECK(parse_p_roster(std::span<const uint8_t>(pb.data(), pn)) == std::nullopt);
    // HAS_PUBKEY set but frame too short -> fail loud (8-B frame, pubkey flag)
    std::array<uint8_t, 8> shortpk = { (0xC0 | P_PROBE_HAS_PUBKEY), 40, 4, 1, 2, 3, 4, 5 };
    CHECK(parse_p_probe(shortpk) == std::nullopt);
    // a <8-B P frame is rejected outright
    std::array<uint8_t, 6> tooshort = { 0xC0, 0, 0, 0, 0, 0 };
    CHECK(parse_p_probe(tooshort) == std::nullopt);
    // wrong cmd nibble
    std::array<uint8_t, 8> wrong = { 0x30, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(parse_p_probe(wrong) == std::nullopt);
    CHECK(parse_p_roster(wrong) == std::nullopt);
}
TEST_CASE("P-roster rev2 — ECHO block round-trip + size") {
    PRosterEntry ents[1] = {{ 0x2716EFCD, 20, 5, protocol::presence_q_strong, true, false }};
    p_roster_in in{}; in.home_id = 103; in.home_layer = 4; in.dir_epoch = 9; in.entries = ents; in.count = 1;
    in.has_echo = true; in.echo_hash32 = 0x44070031; in.echo_quality = protocol::presence_q_weak;
    std::array<uint8_t, 40> buf{};
    const size_t n = pack_p_roster(in, buf);
    CHECK(n == 6u + 6u + 1u + 1u + 5u);                     // §D16: hdr(6) + 1 entry + 1 qual + 1 haskey + 5 echo = 19 (no deleg failures -> §B2 bitmap OMITTED)
    auto r = parse_p_roster(std::span<const uint8_t>(buf.data(), n));
    CHECK(r.has_value());
    if (r) { CHECK(r->has_echo); CHECK(r->echo_hash32 == 0x44070031u); CHECK(r->echo_quality == protocol::presence_q_weak);
             auto e0 = parse_p_roster_entry(std::span<const uint8_t>(buf.data(), n), *r, 0);
             CHECK(e0.has_value()); if (e0) CHECK(e0->quality == protocol::presence_q_strong); }
    // a no-echo roster of the same entry is 5 B shorter and has_echo=false
    p_roster_in ne = in; ne.has_echo = false; std::array<uint8_t,40> nb{};
    const size_t nn = pack_p_roster(ne, nb); CHECK(nn == n - 5u);
    auto rr = parse_p_roster(std::span<const uint8_t>(nb.data(), nn)); CHECK(rr.has_value()); if (rr) CHECK_FALSE(rr->has_echo);
}
TEST_CASE("P-roster — 3 mobiles (27 B) golden size + bitmap round-trip") {
    PRosterEntry ents[3] = {
        { 0x2716EFCD, 20, 5, protocol::presence_q_strong, true,  false },
        { 0x3A3E77A3, 21, 6, protocol::presence_q_weak,   false, true  },   // §B2: this one carries the deleg_fail bit
        { 0xBCC13CC5, 22, 7, protocol::presence_q_critical, true, false },
    };
    p_roster_in in{}; in.home_id = 103; in.home_layer = 4; in.dir_epoch = 9; in.wire_version = 1; in.entries = ents; in.count = 3;
    std::array<uint8_t, 64> buf{};
    const size_t n = pack_p_roster(in, buf);
    CHECK(n == 27);                                          // §D16/B2: 6 hdr + 3*6 entries(18) + ceil(3/4)=1 quality + ceil(3/8)=1 has_key + ceil(3/8)=1 deleg
    auto r = parse_p_roster(std::span<const uint8_t>(buf.data(), n));
    CHECK(r.has_value());
    if (r) {
        CHECK(r->home_id == 103); CHECK(r->home_layer == 4); CHECK(r->dir_epoch == 9); CHECK(r->wire_version == 1); CHECK(r->count == 3);
        auto e0 = parse_p_roster_entry(std::span<const uint8_t>(buf.data(), n), *r, 0);
        auto e1 = parse_p_roster_entry(std::span<const uint8_t>(buf.data(), n), *r, 1);
        auto e2 = parse_p_roster_entry(std::span<const uint8_t>(buf.data(), n), *r, 2);
        CHECK(e0.has_value()); CHECK(e1.has_value()); CHECK(e2.has_value());
        if (e0) { CHECK(e0->key_hash32 == 0x2716EFCD); CHECK(e0->local_id == 20); CHECK(e0->reg_epoch == 5);
                  CHECK(e0->quality == protocol::presence_q_strong); CHECK(e0->has_key); }
        if (e0) CHECK_FALSE(e0->deleg_fail);
        if (e1) { CHECK(e1->quality == protocol::presence_q_weak); CHECK_FALSE(e1->has_key); CHECK(e1->deleg_fail); }   // §B2
        if (e2) { CHECK(e2->key_hash32 == 0xBCC13CC5); CHECK(e2->quality == protocol::presence_q_critical); CHECK(e2->has_key); CHECK_FALSE(e2->deleg_fail); }
        CHECK(parse_p_roster_entry(std::span<const uint8_t>(buf.data(), n), *r, 3) == std::nullopt);  // OOB
    }
}
TEST_CASE("P-roster — 16 mobiles = 108 B no-deleg / 110 B with-deleg (frame-size guide)") {
    PRosterEntry ents[16];
    for (uint8_t i = 0; i < 16; ++i) ents[i] = { 0x1000u + i, static_cast<uint8_t>(20 + i), i, static_cast<uint8_t>(i & 3), (i & 1) != 0, false };
    p_roster_in in{}; in.home_id = 5; in.home_layer = 20; in.dir_epoch = 0; in.entries = ents; in.count = 16;
    std::array<uint8_t, 128> buf{};
    size_t n = pack_p_roster(in, buf);
    CHECK(n == 6u + 16u * 6u + 4u + 2u);                     // §D16: 6 + 96 + ceil(16/4)=4 + ceil(16/8)=2 has_key = 108 (no deleg failures -> §B2 bitmap OMITTED)
    ents[3].deleg_fail = true;                               // §B2: one failure -> the deleg bitmap rides (+ceil(16/8)=2)
    n = pack_p_roster(in, buf);
    CHECK(n == 6u + 16u * 6u + 4u + 2u + 2u);                // = 110
    auto r = parse_p_roster(std::span<const uint8_t>(buf.data(), n));
    CHECK(r.has_value());
    if (r) { auto e15 = parse_p_roster_entry(std::span<const uint8_t>(buf.data(), n), *r, 15);
             CHECK(e15.has_value()); if (e15) { CHECK(e15->local_id == 35); CHECK(e15->quality == (15 & 3)); CHECK(e15->has_key); } }
}
TEST_CASE("j_discover — re-home +3 B last-home block +4 B old-home hash (§B4); fresh mobile 9 B; static 6 B") {
    // §B4: last_home_id != 0 (a re-home) -> the 4-B old-home hash rides -> 13 B
    j_discover_in m{}; m.leaf_id = 4; m.is_mobile = true; m.key_hash32 = 0x44070031;
    m.last_home_id = 103; m.last_home_layer = 4; m.last_reg_epoch = 7; m.last_home_key_hash32 = 0xDEADBEEF;
    std::array<uint8_t, 16> buf{};
    const size_t n = pack_j_discover(m, buf);
    CHECK(n == 13);
    auto o = parse_j(std::span<const uint8_t>(buf.data(), n));
    CHECK(o.has_value());
    if (o) { CHECK(o->is_mobile); CHECK(o->key_hash32 == 0x44070031u);
             CHECK(o->last_home_id == 103); CHECK(o->last_home_layer == 4); CHECK(o->last_reg_epoch == 7);
             CHECK(o->last_home_key_hash32 == 0xDEADBEEFu); }
    // FRESH mobile (last_home_id == 0): 9-B last-home block all-zero, NO hash (byte-identical to pre-B4)
    j_discover_in f{}; f.leaf_id = 4; f.is_mobile = true; f.key_hash32 = 0x1111;
    std::array<uint8_t, 16> fb{};
    const size_t fn = pack_j_discover(f, fb);
    CHECK(fn == 9);
    auto fo = parse_j(std::span<const uint8_t>(fb.data(), fn));
    CHECK(fo.has_value());
    if (fo) { CHECK(fo->last_home_id == 0); CHECK(fo->last_home_key_hash32 == 0u); }
    // legacy 6-B (fresh): static discover, or a 6-B mobile frame -> block parses as 0
    j_discover_in s{}; s.leaf_id = 4; s.is_mobile = false; s.key_hash32 = 0x1234;
    std::array<uint8_t, 16> sb{};
    const size_t sn = pack_j_discover(s, sb);
    CHECK(sn == 6);
    auto so = parse_j(std::span<const uint8_t>(sb.data(), sn));
    CHECK(so.has_value());
    if (so) { CHECK(so->last_home_id == 0); CHECK(so->last_home_layer == 0); CHECK(so->last_reg_epoch == 0); }
}
