// MeshRoute — test_console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_parse.h"
#include "frame_codec.h"   // DATA_FLAG_E2E_ACK_REQ — assert the parser emits the bit the RX acts on
#include <cstring>
#include <string>

using namespace meshroute;            // Command, CmdKind, NodeConfig
using namespace meshroute::console;   // parse_command, parse_cfg, ParseErr, CfgErr

TEST_CASE("parse_command — send <dst> <body> (NO E2E ack)") {
    const char* line = "send 5 hello world";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.flags == 0x00);              // plain DM: no ack requested
    CHECK(c.body_len == 11);                     // "hello world"
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hello world");
}

TEST_CASE("parse_command — send_ack <dst> <body> (E2E ack-req)") {
    const char* line = "send_ack 5 hi there";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ);   // the bit the RX acts on (was 0x08, a dead bit -> acks never fired)
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi there");
}

TEST_CASE("parse_command — send_channel <ch> <body>") {
    const char* line = "send_channel 7 broadcast msg";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_channel);
    CHECK(c.u.channel.channel_id == 7);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "broadcast msg");
    // channel id range is 0..255 (a full byte), wider than the 0..254 dst-id range
    const char* hi = "send_channel 255 x";
    CHECK(parse_command(hi, std::strlen(hi), c) == ParseErr::ok);
    CHECK(c.u.channel.channel_id == 255);
}

TEST_CASE("parse_command — sendhash <hash> <body> (address by key_hash32, NO ack)") {
    const char* line = "sendhash a1b2c3d4 hello";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);                 // hash-addressed DM rides the same send path
    CHECK(c.u.send.dst_id == 0);                    // no short id — on_command routes by dst_hash
    CHECK(c.u.send.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.send.flags == 0x00);                  // plain DM: no ack requested
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hello");
}

TEST_CASE("parse_command — sendhash_ack <hash> <body> (E2E ack-req)") {
    const char* line = "sendhash_ack 0a0b0c0d ok";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 0);
    CHECK(c.u.send.dst_hash == 0x0a0b0c0du);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ);  // the bit the RX acts on (was 0x08, a dead bit -> acks never fired)
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "ok");
}

TEST_CASE("parse_command — sendhash bad hash -> bad_args") {
    Command c{};
    const char* nonhex = "sendhash xyz hi";
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);     // non-hex
    const char* toolong = "sendhash 123456789 hi";
    CHECK(parse_command(toolong, std::strlen(toolong), c) == ParseErr::bad_args);   // >8 hex digits
}

TEST_CASE("parse_command — send_layer <hash> <l1,l2,…> <body> (explicit-path cross-layer DM)") {
    Command c{};
    const char* line = "send_layer a1b2c3d4 2,3 hi there";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.u.layer.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.layer.hop_count == 2);
    CHECK(c.u.layer.hops[0] == 2);
    CHECK(c.u.layer.hops[1] == 3);
    CHECK(c.u.layer.flags == 0x00);                                  // plain send_layer: no E2E ack
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi there");

    // a single-hop path
    const char* one = "send_layer 0a0b0c0d 5 yo";
    CHECK(parse_command(one, std::strlen(one), c) == ParseErr::ok);
    CHECK(c.u.layer.hop_count == 1);
    CHECK(c.u.layer.hops[0] == 5);
}

TEST_CASE("parse_command — send_layer_ack sets the E2E ack-req flag") {
    Command c{};
    const char* line = "send_layer_ack a1b2c3d4 2 hi";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.u.layer.hop_count == 1);
    CHECK(c.u.layer.hops[0] == 2);
    CHECK(c.u.layer.flags == DATA_FLAG_E2E_ACK_REQ);                 // the wire bit the RX acts on (0x10)
}

TEST_CASE("parse_command — send_layer malformed paths -> bad_args (fail loud, no silent fix)") {
    Command c{};
    // gw_env_max_hops == 4, so the user may supply at most 3 destination layers (path[0] is our own, prepended).
    const char* toomany = "send_layer a1b2c3d4 2,3,4,5 hi";   // 4 hops -> overflow
    CHECK(parse_command(toomany, std::strlen(toomany), c) == ParseErr::bad_args);
    const char* nonnum  = "send_layer a1b2c3d4 2,x hi";       // non-numeric element
    CHECK(parse_command(nonnum, std::strlen(nonnum), c) == ParseErr::bad_args);
    const char* zero    = "send_layer a1b2c3d4 0 hi";         // layer id 0 (unset)
    CHECK(parse_command(zero, std::strlen(zero), c) == ParseErr::bad_args);
    const char* empties = "send_layer a1b2c3d4 2,,3 hi";      // empty element
    CHECK(parse_command(empties, std::strlen(empties), c) == ParseErr::bad_args);
    const char* nopath  = "send_layer a1b2c3d4";              // no path token at all
    CHECK(parse_command(nopath, std::strlen(nopath), c) == ParseErr::bad_args);
    const char* badhash = "send_layer zz 2 hi";               // non-hex hash
    CHECK(parse_command(badhash, std::strlen(badhash), c) == ParseErr::bad_args);
    const char* over255 = "send_layer a1b2c3d4 300 hi";       // layer id > 255
    CHECK(parse_command(over255, std::strlen(over255), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — resolve <hash> [hard] (network hash-locate, notify-only)") {
    Command c{};
    const char* soft = "resolve a1b2c3d4";
    CHECK(parse_command(soft, std::strlen(soft), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::resolve);
    CHECK(c.u.resolve.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.resolve.hard == false);
    const char* hard = "resolve 00ff00ff hard";
    CHECK(parse_command(hard, std::strlen(hard), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::resolve);
    CHECK(c.u.resolve.dst_hash == 0x00ff00ffu);
    CHECK(c.u.resolve.hard == true);
}

TEST_CASE("parse_command — resolve bad hash / bad 2nd arg -> bad_args") {
    Command c{};
    const char* nonhex = "resolve zz";
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);
    const char* badopt = "resolve a1 soft";       // only `hard` is a valid 2nd arg
    CHECK(parse_command(badopt, std::strlen(badopt), c) == ParseErr::bad_args);
}

// §6 (E2E peer-key provisioning): reqpubkey <key_hash32 hex8> — the user-triggered on-air pubkey request.
TEST_CASE("parse_command — reqpubkey <hash> (user-triggered WANT_PUBKEY request)") {
    Command c{};
    const char* line = "reqpubkey a1b2c3d4";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::reqpubkey);
    CHECK(c.u.resolve.dst_hash == 0xa1b2c3d4u);
    const char* bad = "reqpubkey zz";
    CHECK(parse_command(bad, std::strlen(bad), c) == ParseErr::bad_args);
}

// §3 (E2E peer-key provisioning): peerkey <ed_pub hex64> — install a scanned peer's full pubkey (QR import, PINNED).
TEST_CASE("parse_command — peerkey <ed_pub hex64> (QR import)") {
    Command c{};
    const char* line = "peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";  // 64 hex = 32 B
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::peerkey);
    bool ok = true; for (int i = 0; i < 32; ++i) ok = ok && (c.u.peerkey.ed_pub[i] == static_cast<uint8_t>(i + 1));
    CHECK(ok);
    const char* tooshort = "peerkey 0102";                                                            // 4 hex != 64
    CHECK(parse_command(tooshort, std::strlen(tooshort), c) == ParseErr::bad_args);
    const char* nonhex = "peerkey zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz";   // 64 non-hex
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — errors") {
    Command c{};
    CHECK(parse_command("ping 5 x", 8, c) == ParseErr::unknown_verb);
    CHECK(parse_command("send x hi", 9, c) == ParseErr::bad_args);    // non-numeric dst
    CHECK(parse_command("send 999 hi", 11, c) == ParseErr::bad_args); // dst > 254
    CHECK(parse_command("send_channel 999 x", 18, c) == ParseErr::bad_args); // channel > 255
    CHECK(parse_command("", 0, c) == ParseErr::empty);
}

TEST_CASE("parse_cfg — keys map to NodeConfig/id/key") {
    NodeConfig c{}; uint8_t id = 0; uint32_t key = 0;
    auto P = [&](const char* l) { return parse_cfg(l, std::strlen(l), c, id, key); };
    CHECK(P("cfg id 3") == CfgErr::ok);           CHECK(id == 3);
    CHECK(P("cfg routing_sf 9") == CfgErr::ok);   CHECK(c.routing_sf == 9);
    CHECK(P("cfg data_sf 12") == CfgErr::unknown_key);   // removed: sf_list is mandatory, no single data_sf fallback
    CHECK(P("cfg gateway 1") == CfgErr::unknown_key);   // removed: is_gateway is DERIVED=(n_layers==2), not configurable
    CHECK(P("cfg key a1b2c3d4") == CfgErr::ok);   CHECK(key == 0xa1b2c3d4u);
    CHECK(P("cfg routing_sf 99") == CfgErr::bad_value);  // SF out of 5..12
    CHECK(P("cfg nope 1") == CfgErr::unknown_key);
}
