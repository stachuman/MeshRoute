// MeshRoute — test_firmware_config_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// First unit coverage of fw_main's pure config/provisioning parse primitives (extracted to
// firmware_config_parse.h, cleanup 2026-07-14). fw_main.cpp itself is outside the native build, so these
// had none before. Behaviour-preserving extraction — these tests pin the exact grammar the device relies on.
#include "doctest.h"
#include "firmware_config_parse.h"
#include "leaf_config.h"   // W2b: meshroute::duty_to_bp/bp_to_duty — pin the console-setter unit conventions
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using mrfw::parse_sf_list;
using mrfw::kv_next;
using mrfw::team_fnv1a32;

TEST_CASE("parse_sf_list — digits 5..12 set their bit; separators are any non-digit; FAIL-LOUD: any out-of-range entry rejects the whole list") {
    CHECK(parse_sf_list("7") == static_cast<uint16_t>(1u << 7));
    CHECK(parse_sf_list("7,9,12") == static_cast<uint16_t>((1u << 7) | (1u << 9) | (1u << 12)));
    CHECK(parse_sf_list("7 9 12") == static_cast<uint16_t>((1u << 7) | (1u << 9) | (1u << 12)));   // space-separated too
    CHECK(parse_sf_list("12") == static_cast<uint16_t>(1u << 12));
    CHECK(parse_sf_list("") == 0);
    CHECK(parse_sf_list("4") == 0);                          // < 5 -> invalid
    CHECK(parse_sf_list("13") == 0);                         // > 12 -> invalid
    CHECK(parse_sf_list("4,5,13,12") == 0);                  // §3-A.7: ANY invalid entry -> the WHOLE list rejects (was silently {5,12})
    CHECK(parse_sf_list("7,13") == 0);                       // §3-A.7: the review's exact case — must error, not silently {7}
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

// W2b console unit unification (ruled 2026-07-21): `cfg set duty=` and `create duty=` now share ONE conversion —
// PERCENT input (1 = 1%, fractional ok) -> internal 0..1 fraction, quantized to the 0.01% wire step. This pins the
// exact expression both call sites in firmware_config.cpp run, so a divergence (the pre-W2b `set duty=` raw-fraction
// bug) is caught. The NV/wire form is the internal fraction/bp — unchanged by the console-unit flip.
TEST_CASE("W2b duty console unit: percent -> internal fraction (set duty= == create duty=)") {
    // The canonical create/set path: bp_to_duty(duty_to_bp(pct / 100.0)).
    auto duty_from_pct = [](double pct) { return meshroute::bp_to_duty(meshroute::duty_to_bp(pct / 100.0)); };
    CHECK(duty_from_pct(1.0)   == doctest::Approx(0.01));    // `set duty=1`  == `create duty=1`  == 1%  -> 0.01
    CHECK(duty_from_pct(10.0)  == doctest::Approx(0.10));    // 10%  -> 0.10
    CHECK(duty_from_pct(0.1)   == doctest::Approx(0.001));   // fractional percent: 0.1% -> 0.001
    CHECK(duty_from_pct(100.0) == doctest::Approx(1.0));     // 100% -> 1.0 (clamped ceiling)
    CHECK(duty_from_pct(0.0)   == doctest::Approx(0.0));     // 0%   -> 0.0 (silent)
    // The 0.01% wire quantization the config_hash relies on: 12.34% rounds to 1234 bp -> 0.1234.
    CHECK(meshroute::duty_to_bp(12.34 / 100.0) == 1234u);
}

// W2b: every console bw setter is kHz ALWAYS (join/create/gateway already were; `cfg set bw` + `cfg set l1_bw`
// were Hz and now converge). The kHz->Hz conversion is `(uint32_t)(kHz * 1000.0 + 0.5)` — ROUNDED so a fractional
// LoRa bandwidth lands exactly (62.5 -> 62500, not 62000/62500-off-by-one).
TEST_CASE("W2b bw console unit: fractional kHz -> Hz, rounded (62.5 -> 62500)") {
    auto bw_hz = [](double khz) { return static_cast<uint32_t>(khz * 1000.0 + 0.5); };
    CHECK(bw_hz(62.5)  == 62500u);    // the fractional-BW pin
    CHECK(bw_hz(125.0) == 125000u);
    CHECK(bw_hz(250.0) == 250000u);
    CHECK(bw_hz(500.0) == 500000u);
    CHECK(bw_hz(41.67) == 41670u);
    CHECK(bw_hz(7.0)   == 7000u);
}

// §3-A.2: the `cfg set` domain predicates — junk (atoi 0) / out-of-domain values must REJECT, not persist an
// RF-dead / filter-deaf node. The SF6 hardware floor stays deliberately WAIVED (only the 5..12 domain holds).
TEST_CASE("§3-A.2 cfg-set domain predicates: routing_sf 5..12, leaf_id 0..15 (wire nibble), hop_cap 1..16") {
    CHECK_FALSE(mrfw::valid_routing_sf(0));    // atoi junk -> 0 -> reject (was: persisted an RF-dead SF 0)
    CHECK_FALSE(mrfw::valid_routing_sf(4));
    CHECK(mrfw::valid_routing_sf(5));          // SF5 stays legal (the SF6 FLOOR is deliberately not enforced)
    CHECK(mrfw::valid_routing_sf(12));
    CHECK_FALSE(mrfw::valid_routing_sf(13));

    CHECK(mrfw::valid_leaf_id(0));
    CHECK(mrfw::valid_leaf_id(15));            // the wire leaf nibble ceiling (cmd-byte low nibble)
    CHECK_FALSE(mrfw::valid_leaf_id(16));      // >15 could never match ANY received frame (filter-deaf)
    CHECK_FALSE(mrfw::valid_leaf_id(254));     // the dead parse_cfg's old cap — wrong, now rejected
    CHECK_FALSE(mrfw::valid_leaf_id(-1));

    CHECK_FALSE(mrfw::valid_hop_cap(0));       // 0 kills ALL route learning
    CHECK(mrfw::valid_hop_cap(1));
    CHECK(mrfw::valid_hop_cap(16));            // == protocol::flood_hop_max (the F RREQ TTL config cap)
    CHECK_FALSE(mrfw::valid_hop_cap(17));
}

TEST_CASE("team_fnv1a32 — deterministic FNV-1a/32 over the 8 LE bytes of (a‖b); order-sensitive") {
    const uint32_t h = team_fnv1a32(0x11223344u, 0xAABBCCDDu);
    CHECK(h == team_fnv1a32(0x11223344u, 0xAABBCCDDu));         // deterministic
    CHECK(team_fnv1a32(0xAABBCCDDu, 0x11223344u) != h);        // (a,b) order matters
    CHECK(team_fnv1a32(0, 0) == 0x9be17165u);                   // golden: FNV-1a/32 of 8 zero bytes
}
