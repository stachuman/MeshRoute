// MeshRoute — test_console_parse.cpp
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
