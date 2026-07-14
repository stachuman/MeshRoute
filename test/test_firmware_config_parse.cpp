// MeshRoute — test_firmware_config_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// First unit coverage of fw_main's pure config/provisioning parse primitives (extracted to
// firmware_config_parse.h, cleanup 2026-07-14). fw_main.cpp itself is outside the native build, so these
// had none before. Behaviour-preserving extraction — these tests pin the exact grammar the device relies on.
#include "doctest.h"
#include "firmware_config_parse.h"
#include <cstring>
#include <string>
#include <vector>

using mrfw::parse_sf_list;
using mrfw::kv_next;
using mrfw::team_fnv1a32;

TEST_CASE("parse_sf_list — digits 5..12 set their bit; separators are any non-digit; out-of-range ignored") {
    CHECK(parse_sf_list("7") == static_cast<uint16_t>(1u << 7));
    CHECK(parse_sf_list("7,9,12") == static_cast<uint16_t>((1u << 7) | (1u << 9) | (1u << 12)));
    CHECK(parse_sf_list("7 9 12") == static_cast<uint16_t>((1u << 7) | (1u << 9) | (1u << 12)));   // space-separated too
    CHECK(parse_sf_list("12") == static_cast<uint16_t>(1u << 12));
    CHECK(parse_sf_list("") == 0);
    CHECK(parse_sf_list("4") == 0);                          // < 5 -> ignored
    CHECK(parse_sf_list("13") == 0);                         // > 12 -> ignored
    CHECK(parse_sf_list("4,5,13,12") == static_cast<uint16_t>((1u << 5) | (1u << 12)));   // only the in-range ones
    CHECK(parse_sf_list("9") == static_cast<uint16_t>(1u << 9));
}

// helper: collect every key=value token kv_next yields from a MUTABLE copy of `line`.
static std::vector<std::pair<std::string, std::string>> tokenize(const char* line) {
    std::vector<char> buf(line, line + std::strlen(line) + 1);
    char* p = buf.data();
    std::vector<std::pair<std::string, std::string>> out;
    char* k = nullptr; char* v = nullptr;
    while (kv_next(p, k, v)) out.emplace_back(k ? k : "", v ? v : "\x01<null>");   // sentinel marks a malformed (no '=') token
    return out;
}

TEST_CASE("kv_next — key=value grammar: bare, quoted (spans spaces), multiple tokens, malformed") {
    auto a = tokenize("l0=7 win0=500");
    CHECK(a.size() == 2);
    if (a.size() == 2) {
        CHECK(a[0] == std::make_pair(std::string("l0"), std::string("7")));
        CHECK(a[1] == std::make_pair(std::string("win0"), std::string("500")));
    }

    auto q = tokenize("name=\"Bob the Rover\" sf=12");           // quoted value spans spaces
    CHECK(q.size() == 2);
    if (q.size() == 2) {
        CHECK(q[0] == std::make_pair(std::string("name"), std::string("Bob the Rover")));
        CHECK(q[1] == std::make_pair(std::string("sf"), std::string("12")));
    }

    auto m = tokenize("bogus");                                  // no '=' -> malformed, val == nullptr
    CHECK(m.size() == 1);
    if (m.size() == 1) {
        CHECK(m[0].first == "bogus");
        CHECK(m[0].second == "\x01<null>");
    }

    CHECK(tokenize("").empty());                                 // end of string -> no tokens
    CHECK(tokenize("   ").empty());                              // only spaces -> no tokens

    auto lead = tokenize("   a=1    b=2  ");                     // leading/trailing/multiple spaces tolerated
    CHECK(lead.size() == 2);
    if (lead.size() == 2) {
        CHECK(lead[0] == std::make_pair(std::string("a"), std::string("1")));
        CHECK(lead[1] == std::make_pair(std::string("b"), std::string("2")));
    }
}

TEST_CASE("team_fnv1a32 — deterministic FNV-1a/32 over the 8 LE bytes of (a‖b); order-sensitive") {
    const uint32_t h = team_fnv1a32(0x11223344u, 0xAABBCCDDu);
    CHECK(h == team_fnv1a32(0x11223344u, 0xAABBCCDDu));         // deterministic
    CHECK(team_fnv1a32(0xAABBCCDDu, 0x11223344u) != h);        // (a,b) order matters
    CHECK(team_fnv1a32(0, 0) == 0x9be17165u);                   // golden: FNV-1a/32 of 8 zero bytes
}
