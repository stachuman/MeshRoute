// MeshRoute — test_console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_parse.h"
#include <cstring>
#include <string>

using namespace meshroute;            // Command, CmdKind, NodeConfig
using namespace meshroute::console;   // parse_command, parse_cfg, ParseErr, CfgErr

TEST_CASE("parse_command — send <dst> <body>") {
    const char* line = "send 5 hello world";
    Command c{};
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.flags == 0x08);              // E2E default
    CHECK(c.body_len == 11);                     // "hello world"
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hello world");
}

TEST_CASE("parse_command — errors") {
    Command c{};
    CHECK(parse_command("ping 5 x", 8, c) == ParseErr::unknown_verb);
    CHECK(parse_command("send x hi", 9, c) == ParseErr::bad_args);    // non-numeric dst
    CHECK(parse_command("send 999 hi", 11, c) == ParseErr::bad_args); // dst > 254
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
