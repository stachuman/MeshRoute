// MeshRoute — test_console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_parse.h"
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
    CHECK(c.u.send.flags == 0x08);              // E2E ack-req
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
    CHECK(c.u.send.flags == 0x08);                  // E2E ack-req
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "ok");
}

TEST_CASE("parse_command — sendhash bad hash -> bad_args") {
    Command c{};
    const char* nonhex = "sendhash xyz hi";
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);     // non-hex
    const char* toolong = "sendhash 123456789 hi";
    CHECK(parse_command(toolong, std::strlen(toolong), c) == ParseErr::bad_args);   // >8 hex digits
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
    CHECK(P("cfg gateway 1") == CfgErr::ok);      CHECK(c.is_gateway == true);
    CHECK(P("cfg key a1b2c3d4") == CfgErr::ok);   CHECK(key == 0xa1b2c3d4u);
    CHECK(P("cfg routing_sf 99") == CfgErr::bad_value);  // SF out of 5..12
    CHECK(P("cfg nope 1") == CfgErr::unknown_key);
}
