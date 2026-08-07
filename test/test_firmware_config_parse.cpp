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
#include <cstdlib>   // strtoul — §team-target-range pins WHICH of the two range clauses catches WHICH token shape
#include <cerrno>    // errno/ERANGE — same (explicit, not leaned on via firmware_config_parse.h)
#include <climits>   // ULONG_MAX  — same: the ABI-split assertion compares it against UINT32_MAX
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
// §3-B.3: this now drives the PRODUCTION helper instead of a local copy of the formula — the console verbs it
// guards are device-only, so this is the ONLY executable check on that conversion.
TEST_CASE("W2b bw console unit: fractional kHz -> Hz, rounded (62.5 -> 62500)") {
    auto bw_hz = [](double khz) { return meshroute::protocol::khz_to_hz(khz); };
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

// §team-ch-key (T-K1): the `tkpub=`/`tkpriv=` hex64 parser. No scenario runs a console verb, so this suite is
// the ONLY detector for the grammar — and a lenient parse here would silently install a key the operator never
// typed (which then encrypts for a team that cannot read it). Every reject path is pinned, including the
// commit-only-after-validation contract.
TEST_CASE("parse_hex32 — EXACTLY 64 hex digits -> 32 bytes; case-insensitive; anything else REFUSES") {
    uint8_t out[32];

    // The RFC 7748 §6.1 Alice private key — the same string the team-key KAT feeds in.
    CHECK(mrfw::parse_hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", out));
    CHECK(out[0] == 0x77); CHECK(out[1] == 0x07); CHECK(out[30] == 0x2c); CHECK(out[31] == 0x2a);

    // UPPER and mixed case parse identically (operators paste from either convention).
    uint8_t up[32], mix[32];
    CHECK(mrfw::parse_hex32("77076D0A7318A57D3C16C17251B26645DF4C2F87EBC0992AB177FBA51DB92C2A", up));
    CHECK(mrfw::parse_hex32("77076d0A7318a57D3c16C17251b26645Df4c2F87eBc0992Ab177fBa51dB92c2A", mix));
    CHECK(std::memcmp(out, up, 32) == 0);
    CHECK(std::memcmp(out, mix, 32) == 0);

    // All-zero is SYNTACTICALLY fine here — the crypto-domain refusal lives in team_channel_key_derive.
    uint8_t z[32];
    CHECK(mrfw::parse_hex32("0000000000000000000000000000000000000000000000000000000000000000", z));

    // ---- refusals
    CHECK_FALSE(mrfw::parse_hex32(nullptr, out));
    CHECK_FALSE(mrfw::parse_hex32("", out));
    CHECK_FALSE(mrfw::parse_hex32("77076d0a", out));                                                       // far too short
    CHECK_FALSE(mrfw::parse_hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2", out)); // 63 — one short
    CHECK_FALSE(mrfw::parse_hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2aa", out));// 65 — one long
    CHECK_FALSE(mrfw::parse_hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2g", out)); // non-hex last digit
    CHECK_FALSE(mrfw::parse_hex32("g7076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", out)); // non-hex first digit
    CHECK_FALSE(mrfw::parse_hex32("0x076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", out)); // a `0x` prefix is a LENGTH error, not a number
    CHECK_FALSE(mrfw::parse_hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a ", out));// trailing space
}

TEST_CASE("parse_hex32 — a REFUSED token leaves the caller's buffer untouched (no half-written key)") {
    uint8_t out[32];
    for (int i = 0; i < 32; ++i) out[i] = 0xEE;
    CHECK_FALSE(mrfw::parse_hex32("aabbccddeeff00112233445566778899aabbccddeeff00112233445566778", out));  // 61 digits
    for (int i = 0; i < 32; ++i) CHECK(out[i] == 0xEE);      // the first 30 bytes DID validate — none may land
}

// §team-ch-key (T-K1): splitting `tkpub=`/`tkpriv=` out of a `team` tail. The firmware wrapper around this
// (firmware_config.cpp:parse_team_key_tail) only maps the returned enum to console strings, so THIS is where the
// grammar is actually gated — and the corpus cannot reach it (no scenario runs a console verb).
TEST_CASE("split_team_key_tail — extracts the pair and hands the PHY triplet through untouched") {
    char scratch[224], rest[96];
    uint8_t pub[32], priv[32];
    const char* bad = nullptr;
    const char* A = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
    const char* B = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";

    // No team keys at all -> `none`, and the tail is passed through so parse_phy_tail sees exactly what it did.
    CHECK(mrfw::split_team_key_tail("freq=869.0 sf=7 bw=125", scratch, sizeof scratch, rest, sizeof rest,
                                    pub, priv, bad) == mrfw::TeamKeyTail::none);
    CHECK(std::string(rest) == "freq=869.0 sf=7 bw=125");

    // Both keys + a PHY triplet, keys FIRST.
    std::string in = std::string("tkpub=") + A + " tkpriv=" + B + " freq=869.0 sf=7 bw=125";
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::ok);
    CHECK(std::string(rest) == "freq=869.0 sf=7 bw=125");
    CHECK(pub[0]  == 0x77); CHECK(pub[31]  == 0x2a);
    CHECK(priv[0] == 0x5d); CHECK(priv[31] == 0xeb);

    // Interleaved, and keys LAST — order must not matter, and `rest` must not gain/lose a separator.
    in = std::string("freq=869.0 tkpriv=") + B + " sf=7 tkpub=" + A + " bw=125";
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::ok);
    CHECK(std::string(rest) == "freq=869.0 sf=7 bw=125");

    // Keys ONLY (the QR-onboarding shape: `team new tkpub=… tkpriv=…`) -> `rest` is EMPTY, which parse_phy_tail
    // must then read as "no PHY given", not as an error.
    in = std::string("tkpub=") + A + " tkpriv=" + B;
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::ok);
    CHECK(std::string(rest).empty());

    // An unknown key survives into `rest` VERBATIM so parse_phy_tail's own error still names it (behaviour-preserving).
    CHECK(mrfw::split_team_key_tail("freq=869.0 wibble=3", scratch, sizeof scratch, rest, sizeof rest,
                                    pub, priv, bad) == mrfw::TeamKeyTail::none);
    CHECK(std::string(rest) == "freq=869.0 wibble=3");
    // ...including a VALUELESS token (kv_next yields val=nullptr), which must not be silently swallowed.
    CHECK(mrfw::split_team_key_tail("freq=869.0 wibble", scratch, sizeof scratch, rest, sizeof rest,
                                    pub, priv, bad) == mrfw::TeamKeyTail::none);
    CHECK(std::string(rest) == "freq=869.0 wibble");

    // Last-wins on a duplicate, matching the rest of this grammar (`freq=1 freq=2`).
    in = std::string("tkpub=") + A + " tkpub=" + B + " tkpriv=" + B;
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::ok);
    CHECK(pub[0] == 0x5d);
}

TEST_CASE("split_team_key_tail — C2 refusals: half a pair, bad hex, an over-long tail") {
    char scratch[224], rest[96];
    uint8_t pub[32], priv[32];
    const char* bad = nullptr;
    const char* A = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";

    // Only ONE half -> half_pair (either way round). A lone public key cannot decrypt; a lone private key with a
    // wrong/absent public half would seal to nothing readable.
    std::string in = std::string("tkpub=") + A;
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::half_pair);
    in = std::string("tkpriv=") + A;
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::half_pair);

    // Malformed hex -> bad_hex, and `bad_key` names WHICH key so the console can say so.
    CHECK(mrfw::split_team_key_tail("tkpub=deadbeef tkpriv=00", scratch, sizeof scratch, rest, sizeof rest,
                                    pub, priv, bad) == mrfw::TeamKeyTail::bad_hex);
    CHECK(bad != nullptr);
    CHECK(std::string(bad) == "tkpub");
    in = std::string("tkpub=") + A + " tkpriv=nothex";
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::bad_hex);
    CHECK(std::string(bad) == "tkpriv");
    // A valueless `tkpub` (no '=') is bad_hex too — NOT silently treated as absent.
    CHECK(mrfw::split_team_key_tail("tkpub", scratch, sizeof scratch, rest, sizeof rest,
                                    pub, priv, bad) == mrfw::TeamKeyTail::bad_hex);

    // Over-long input REFUSES rather than truncating (a truncated hex blob is the dangerous case).
    char tiny[16], tiny_rest[8];
    in = std::string("tkpub=") + A;
    CHECK(mrfw::split_team_key_tail(in.c_str(), tiny, sizeof tiny, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::too_long);
    CHECK(mrfw::split_team_key_tail("freq=869.4625 sf=7 bw=125", scratch, sizeof scratch, tiny_rest, sizeof tiny_rest,
                                    pub, priv, bad) == mrfw::TeamKeyTail::too_long);
    CHECK(mrfw::split_team_key_tail("freq=869.0", scratch, sizeof scratch, rest, 0,
                                    pub, priv, bad) == mrfw::TeamKeyTail::too_long);

    // The realistic worst-case tail (both keys + the full PHY triplet) MUST fit the 224-byte firmware scratch.
    in = std::string("tkpub=") + A + " tkpriv=" + A + " freq=869.4625 sf=12 bw=125.00";
    CHECK(in.size() < 224);
    CHECK(mrfw::split_team_key_tail(in.c_str(), scratch, sizeof scratch, rest, sizeof rest,
                                   pub, priv, bad) == mrfw::TeamKeyTail::ok);
}

// ================= §team-ch-key (T-K3) — `team grantkey` argument grammar =====================================
// The console verb has NO other detector: no scenario runs a console verb and src/firmware_config.cpp is outside the
// sim build, so this suite is the whole coverage of the target/name/flag grammar (the T-K1/T-K1b lesson).
TEST_CASE("§T-K3 parse_grant_args — the 0x-hex target, the bare team-id target, and the implied TEAM plane") {
    char scratch[128];
    mrfw::GrantArgsOut o; const char* bad = nullptr;

    CHECK(mrfw::parse_grant_args(" 0xdeadbeef", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.target_hash == 0xdeadbeefu);
    CHECK(o.target_id == 0);
    CHECK(o.name_len == 0);
    CHECK_FALSE(o.team_plane);                                   // a hash target defaults to AUTO, like `send`

    CHECK(mrfw::parse_grant_args("0XDEADBEEF", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.target_hash == 0xdeadbeefu);                         // case-insensitive, `0X` accepted
    CHECK(mrfw::parse_grant_args("0x1", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.target_hash == 1u);                                  // 1..8 digits, not exactly 8

    // A bare decimal 1..254 = a teammate's team_local_id, and it IMPLIES the team plane.
    // ⚠ V1 2026-08-01 (§id-hash S1): this used to be justified as *"reqpubkey's convention"* — that citation is now
    // STALE. `reqpubkey <bare id>` no longer forces TEAM (it resolves on both planes and refuses `err_ambiguous_plane`
    // on a collision). `grantkey` keeps the team-only reading on its own merits, not by borrowing that one: a grant
    // SHIPS A PRIVATE TEAM KEY, so a static-plane target is not a plane choice but a category error.
    CHECK(mrfw::parse_grant_args("213", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.target_id == 213);
    CHECK(o.target_hash == 0);
    CHECK(o.team_plane);
    CHECK(mrfw::parse_grant_args("254", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.target_id == 254);
}

TEST_CASE("§T-K3 parse_grant_args — name= (quoted, spaces, 32-cap) and the -t flag in either position") {
    char scratch[128];
    mrfw::GrantArgsOut o; const char* bad = nullptr;

    CHECK(mrfw::parse_grant_args("0xabcd name=Alpha", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(std::string(o.name) == "Alpha");
    CHECK(o.name_len == 5);

    CHECK(mrfw::parse_grant_args("0xabcd name=\"Alpha Team 7\"", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(std::string(o.name) == "Alpha Team 7");                // kv_next's quoting -> spaces survive
    CHECK(o.name_len == 12);

    CHECK(mrfw::parse_grant_args("0xabcd -t", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.team_plane);
    CHECK(mrfw::parse_grant_args("0xabcd -t name=X", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.team_plane); CHECK(std::string(o.name) == "X");      // order-free
    CHECK(mrfw::parse_grant_args("0xabcd name=X -t", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.team_plane); CHECK(std::string(o.name) == "X");

    // Exactly 32 is accepted; 33 REFUSES (C2 — an operator label must never be silently truncated).
    const std::string n32(32, 'z');
    CHECK(mrfw::parse_grant_args(("0xabcd name=" + n32).c_str(), scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.name_len == 32);
    const std::string n33(33, 'z');
    CHECK(mrfw::parse_grant_args(("0xabcd name=" + n33).c_str(), scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::too_long);
    // Duplicate name= is LAST-WINS, matching split_team_key_tail and the rest of this grammar.
    CHECK(mrfw::parse_grant_args("0xabcd name=one name=two", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(std::string(o.name) == "two");
}

TEST_CASE("§T-K3 parse_grant_args — C2 refusals: missing, bad target, unknown key, over-long tail") {
    char scratch[128];
    mrfw::GrantArgsOut o; const char* bad = nullptr;

    CHECK(mrfw::parse_grant_args("", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::missing);
    CHECK(mrfw::parse_grant_args("   ", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::missing);

    CHECK(mrfw::parse_grant_args("deadbeef", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);   // hash needs 0x
    CHECK(mrfw::parse_grant_args("0x", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);         // no digits
    CHECK(mrfw::parse_grant_args("0xdeadbeef1", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target); // 9 digits
    CHECK(mrfw::parse_grant_args("0xzz", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);
    CHECK(mrfw::parse_grant_args("0x00000000", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);  // hash 0 = unset
    CHECK(mrfw::parse_grant_args("0", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);           // team id 0 = unset
    CHECK(mrfw::parse_grant_args("255", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);         // 0xFF reserved
    CHECK(mrfw::parse_grant_args("300", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);
    CHECK(mrfw::parse_grant_args("12a", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_target);

    CHECK(mrfw::parse_grant_args("0xabcd freq=869.0", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_key);
    CHECK(bad != nullptr); if (bad) CHECK(std::string(bad) == "freq");
    CHECK(mrfw::parse_grant_args("0xabcd name", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_key);   // valueless name=
    CHECK(mrfw::parse_grant_args("0xabcd -q", scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::bad_key);
    CHECK(bad != nullptr); if (bad) CHECK(std::string(bad) == "-q");

    char tiny[8];
    CHECK(mrfw::parse_grant_args("0xabcd name=abcdefghijklmnop", tiny, sizeof tiny, o, bad) == mrfw::GrantArgs::too_long);

    // The realistic worst case (hash + a 32-char quoted name + -t) MUST fit the 128-byte firmware scratch.
    const std::string worst = std::string("0xdeadbeef name=\"") + std::string(32, 'x') + "\" -t";
    CHECK(worst.size() < 128);
    CHECK(mrfw::parse_grant_args(worst.c_str(), scratch, sizeof scratch, o, bad) == mrfw::GrantArgs::ok);
    CHECK(o.name_len == 32);
    CHECK(o.team_plane);
}

// ---- ★★ §team-target (BUG FIX 2026-07-30): `team <garbage>` USED TO LEAVE THE TEAM ---------------------------------
// handle_team read its first token with strtoul, which consumes ZERO characters from a non-numeric tail and returns 0 —
// and `team 0` MEANS LEAVE. So every near-spelling of a subcommand (`team exportky`, `team grantky`, `team nwe`) silently
// left the team. handle_team lives in src/, which is outside the native build, so the decision was extracted into
// parse_team_target and pinned here. ★ The load-bearing assertion is the SURVIVING id, not the return value: out_id is
// seeded with a LIVE team_id and must still hold it after every refusal, because that is what the caller persists.
TEST_CASE("★★ §team-target — a NON-NUMERIC tail is REFUSED and the caller's team_id SURVIVES (it used to become 0 = LEAVE)") {
    const uint32_t live = 0xDEADBEEFu;                       // the node's current team_id
    const char* tail = nullptr;
    // Every one of these used to parse as 0 -> `team 0` -> LEAVE. The exact spellings from the bench report.
    for (const char* garbage : { "exportky", "exportkeyy", "grantky", "grantkeyx", "nwe", "ne", "leave", "x",
                                 "Exportkey", "-1", "+5", " 7", "?",
                                 // ★ clause (2): these BEGIN with a digit yet strtoul reads only the leading "0",
                                 // so a leading-digit rule ALONE still left the team. Found by test, not by reading.
                                 "0x", "0X", "0xZZ", "08", "09", "0abc", "0x ",
                                 // ★★ §team-target-whole (B1): partially consumed with a NON-zero value — these did
                                 // not leave, they JOINED THE WRONG TEAM (88 / 12 / 1 / 7). Clause (2) is no longer
                                 // gated on `v == 0`, so they refuse too and the live team_id survives.
                                 "88A672BA", "12abc", "1x", "7abc", "9deadbeef", "0x88A672BAZ" }) {
        uint32_t id = live;                                  // seeded with the LIVE id, exactly as the caller holds it
        tail = reinterpret_cast<const char*>(0x1);           // a poison value: a refusal must not write the tail either
        CHECK_FALSE(mrfw::parse_team_target(garbage, id, tail));
        CHECK(id == live);                                   // ★★ THE ASSERTION THAT PROTECTS THE OPERATOR: still in the team
        CHECK(id != 0);                                      // ...and specifically NOT the leave value
        CHECK(tail == reinterpret_cast<const char*>(0x1));   // fail-closed: no half-write
    }
    uint32_t id = live;
    CHECK_FALSE(mrfw::parse_team_target(nullptr, id, tail));  // defensive: a null tail is a refusal, not a crash
    CHECK(id == live);
}

TEST_CASE("★ §team-target — `team 0` (LEAVE) and every legitimate id still parse EXACTLY as before") {
    uint32_t id = 0xFFFFFFFFu; const char* tail = nullptr;
    // ⚠ LEAVING IS LEGITIMATE and must keep working — this is the direction a leading-digit rule could have broken.
    CHECK(mrfw::parse_team_target("0", id, tail));
    CHECK(id == 0u);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail).empty());
    id = 0; CHECK(mrfw::parse_team_target("42", id, tail));            CHECK(id == 42u);
    id = 0; CHECK(mrfw::parse_team_target("0x2a", id, tail));          CHECK(id == 42u);          // base 0 -> hex
    id = 0; CHECK(mrfw::parse_team_target("0X2A", id, tail));          CHECK(id == 42u);
    id = 0; CHECK(mrfw::parse_team_target("010", id, tail));           CHECK(id == 8u);           // base 0 -> octal (pre-existing)
    id = 0; CHECK(mrfw::parse_team_target("4294967295", id, tail));    CHECK(id == 0xFFFFFFFFu);  // the full 32-bit id space
    id = 0; CHECK(mrfw::parse_team_target("0xDEADBEEF", id, tail));     CHECK(id == 0xDEADBEEFu);
    // The PHY/key tail is handed through verbatim (strtoul's endp), which is what parse_team_key_tail/parse_phy_tail read.
    id = 0; CHECK(mrfw::parse_team_target("7 freq=869.0 sf=7 bw=125", id, tail));
    CHECK(id == 7u);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail) == " freq=869.0 sf=7 bw=125");
    id = 0; CHECK(mrfw::parse_team_target("0 ", id, tail));            CHECK(id == 0u);           // `team 0` with a trailing space still leaves
    id = 9; CHECK(mrfw::parse_team_target("00", id, tail));            CHECK(id == 0u);           // an unambiguous zero spelling MAY leave
    id = 9; CHECK(mrfw::parse_team_target("0x0", id, tail));           CHECK(id == 0u);
    id = 9; CHECK(mrfw::parse_team_target("0 freq=869.0", id, tail));  CHECK(id == 0u);           // `team 0` + a tail: clause (2) measures the TOKEN, not the line
    id = 0; CHECK(mrfw::parse_team_target("12345", id, tail));         CHECK(id == 12345u);
}

// ---- ★★ §team-target-whole (BUG FIX 2026-07-31, register B1): `team 88A672BA` JOINED TEAM 88 ------------------------
// A hex team id typed WITHOUT `0x` is read by strtoul base 0 as DECIMAL: `88A672BA` -> 88, endp stops at the `A`, and 88
// is non-zero so the 07-30 zero-gate never looked. ⇒ a SILENT JOIN OF THE WRONG TEAM. Clause (2) is now unconditional.
// ★ The load-bearing assertion is again the SURVIVING team_id: the point of the fix is that a typo does not move state.
TEST_CASE("★★ §team-target-whole — a 0x-LESS hex id is REFUSED and the live team_id SURVIVES (it used to JOIN team 88)") {
    const uint32_t live = 0xDEADBEEFu;
    uint32_t id = live; const char* tail = reinterpret_cast<const char*>(0x1);
    CHECK_FALSE(mrfw::parse_team_target("88A672BA", id, tail));
    CHECK(id == live);                                   // ★★ NOT 88 — the operator is still in the team it was in
    CHECK(id != 88u);                                    // the exact wrong value the old rule installed
    CHECK(tail == reinterpret_cast<const char*>(0x1));   // fail-closed: no half-write
    // ...and the SAME id spelled correctly still joins, so the fix refuses the typo, not the feature.
    id = 0; tail = nullptr;
    CHECK(mrfw::parse_team_target("0x88A672BA", id, tail));
    CHECK(id == 0x88A672BAu);
}

// ★★★ THE REGRESSION THIS FIX COULD HAVE CAUSED, AND THE REASON IT DOES NOT: the target token is measured only up to
// the FIRST SPACE, so a fully-consumed id followed by the legitimate PHY/key tail is still accepted — the tail begins
// at the space, past the token. Pinned for BOTH the decimal and the hex spelling; without this the whole-token rule
// would have broken `team <id> freq=… sf=… bw=…`, which is the documented join form.
TEST_CASE("★★ §team-target-whole — the legitimate PHY / team-key TAIL still parses (the token stops at the space)") {
    uint32_t id = 0; const char* tail = nullptr;
    CHECK(mrfw::parse_team_target("0x88A672BA freq=868 sf=7", id, tail));
    CHECK(id == 0x88A672BAu);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail) == " freq=868 sf=7");
    id = 0; CHECK(mrfw::parse_team_target("7 freq=869.0 sf=7 bw=125", id, tail));
    CHECK(id == 7u);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail) == " freq=869.0 sf=7 bw=125");
    id = 0; CHECK(mrfw::parse_team_target("42 tkpub=aa tkpriv=bb", id, tail));
    CHECK(id == 42u);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail) == " tkpub=aa tkpriv=bb");
    // ✅ §team-target-range (B17) CLOSED below — the ✖ MISSING note that used to sit here (pinning the out-of-range
    // token as ACCEPTED, so the range slice had a before-arm) is now the TEST_CASE that follows.
}

// ---- ★★ §team-target-range (BUG FIX 2026-07-31, register B17): an OUT-OF-RANGE `team <id>` was DESTRUCTIVE ------------
// The 07-30 and 07-31 clauses are both SYNTAX rules, so a perfectly-spelled token that does not FIT IN 32 BITS passed
// both and landed as whatever this ABI's `unsigned long` produced — and ★ THE TWO ABIs FAIL DIFFERENTLY, which is why
// the fix needs TWO clauses:
//   • `unsigned long` = 8 B (this native build): `strtoul("4294967296")` succeeds with NO errno, and the narrowing cast
//     truncates to 0 — and `team 0` MEANS LEAVE. Caught by clause (3b), the UINT32_MAX width check.
//   • `unsigned long` = 4 B (EVERY board target — `static_assert(sizeof(unsigned long)==4)` holds on both
//     arm-none-eabi and xtensa-esp32s3): strtoul SATURATES to ULONG_MAX and sets ERANGE, so the same command JOINED
//     GARBAGE TEAM 0xFFFFFFFF. Caught by clause (3a). ⚠ This suite cannot REPRODUCE that path (see the test below for
//     what it can), but it does cover the guard that stops it.
// ★ AND THE HOST WAS NOT SAFE EITHER, which the deferral note used to imply: a token overflowing even 64 bits raises
// ERANGE here too and truncates ULONG_MAX to 0xFFFFFFFF ⇒ the garbage JOIN, on native. ⚠ But see the ABI-split note
// mid-test: that shape is refused by the WIDTH arm on a 64-bit host, so this suite exercises clause (3a) without being
// able to prove it NECESSARY. Do not read a green run as licence to delete it — it is the boards' only guard.
TEST_CASE("★★ §team-target-range — an OUT-OF-RANGE target is REFUSED and the live team_id SURVIVES (both ABIs' failures)") {
    const uint32_t live = 0xDEADBEEFu;
    // ★ THE ERANGE ARM (clause 3a) ON THE PLATFORM THAT RUNS THIS SUITE. These overflow a 64-bit `unsigned long`, so the
    // host's own strtoul saturates + sets ERANGE — the identical mechanism the 32-bit boards hit at 2^32. Pre-fix these
    // JOINED GARBAGE TEAM 0xFFFFFFFF *on native*, which is the boards' destructive outcome, reachable here.
    for (const char* over : { "99999999999999999999999", "18446744073709551616", "0x1FFFFFFFFFFFFFFFFF",
                              "99999999999999999999999 freq=868" }) {
        uint32_t id = live;
        const char* tail = reinterpret_cast<const char*>(0x1);
        CHECK_FALSE(mrfw::parse_team_target(over, id, tail));
        CHECK(id == live);                                   // ★★ still in the team it was in
        CHECK(id != 0xFFFFFFFFu);                            // ...and specifically NOT the saturated garbage team
        CHECK(tail == reinterpret_cast<const char*>(0x1));   // fail-closed: no half-write
    }
    // ★ THE WIDTH ARM (clause 3b): fully consumed, no ERANGE on a 64-bit host, but > UINT32_MAX. Pre-fix `4294967296`
    // truncated to 0 = SILENT LEAVE here, and saturated to 0xFFFFFFFF = SILENT JOIN on the boards. One command, two
    // different wrong outcomes, neither of them what was typed.
    for (const char* wide : { "4294967296", "4294967297", "0x100000000", "8589934592",
                              "4294967296 freq=869.0 sf=7" }) {
        uint32_t id = live;
        const char* tail = reinterpret_cast<const char*>(0x1);
        CHECK_FALSE(mrfw::parse_team_target(wide, id, tail));
        CHECK(id == live);                                   // ★★ NOT 0 (the 64-bit LEAVE) and NOT 0xFFFFFFFF (the 32-bit JOIN)
        CHECK(id != 0u);
        CHECK(id != 0xFFFFFFFFu);
        CHECK(tail == reinterpret_cast<const char*>(0x1));
    }
    // ★★ WHY BOTH CLAUSES EXIST, stated as the ABI algebra it actually is — ⚠ AND CORRECTING A CLAIM THIS TEST FIRST
    // MADE. I wrote "neither arm is redundant, and these assertions prove it"; the MUTANT DISPROVED IT. Deleting the
    // ERANGE arm leaves this whole suite GREEN, because on a 64-bit host ERANGE implies `ul == ULONG_MAX`, which is
    // itself > UINT32_MAX ⇒ the width arm SUBSUMES the ERANGE arm here. The truth is sharper and less comfortable:
    //   • 64-bit (this build): the WIDTH arm does all the work. Removing it => 19 assertions red (measured).
    //   • 32-bit (every board): `UINT32_MAX == ULONG_MAX`, so the width arm is a compile-time false and the ERANGE
    //     arm is THE ONLY GUARD — over the DESTRUCTIVE outcome (the garbage join), which is why it must stay.
    // ⇒ **no test on THIS ABI can prove the ERANGE arm necessary.** What it can prove is that the arm FUNCTIONS
    // (mutant B: width arm gone, the >2^64 cases above stay green because ERANGE refuses them) and that the two
    // token shapes really are distinguished by the two mechanisms. That is what the asserts below pin.
    {
        errno = 0; char* endp = nullptr;
        const unsigned long ul32 = strtoul("4294967296", &endp, 0);
        CHECK(errno != ERANGE);                                          // no ERANGE at 2^32 on a 64-bit host => (3a) alone would MISS it
        CHECK(*endp == '\0');                                            // ...and it is FULLY consumed, so clause (2) misses it too
        if (sizeof(unsigned long) >= 8) CHECK(ul32 > static_cast<unsigned long>(UINT32_MAX));   // only the width check sees it
        errno = 0; endp = nullptr;
        const unsigned long ulbig = strtoul("99999999999999999999999", &endp, 0);
        CHECK(errno == ERANGE);                                          // the >2^64 shape DOES raise ERANGE on the host
        CHECK(ulbig == ULONG_MAX);                                       // saturation, exactly as the boards do at 2^32
        CHECK(static_cast<uint32_t>(ulbig) == 0xFFFFFFFFu);              // ★ narrowed = the GARBAGE TEAM the boards used to join
        CHECK(*endp == '\0');                                            // fully consumed => clause (2) cannot catch it either
        // ★ THE ABI SPLIT, asserted so a future "simplification" trips over it instead of over the fleet: exactly one
        // of the two arms is live per ABI, and on a 32-bit target it is the ERANGE one.
        CHECK((static_cast<unsigned long>(UINT32_MAX) < ULONG_MAX) == (sizeof(unsigned long) > 4));
    }
    // ★★★ THE NEGATIVE CONTROLS — the range rule must refuse a RANGE ERROR, not a value. These are the directions an
    // over-eager guard would have broken, and every one of them is a documented, legitimate form.
    uint32_t id = 9; const char* tail = nullptr;
    CHECK(mrfw::parse_team_target("0", id, tail));                       // ⚠ `team 0` = LEAVE THE TEAM, still works
    CHECK(id == 0u);
    id = 0; CHECK(mrfw::parse_team_target("4294967295", id, tail));      // the largest in-range id, EXACTLY at the boundary
    CHECK(id == 0xFFFFFFFFu);
    id = 0; CHECK(mrfw::parse_team_target("0xFFFFFFFF", id, tail));      // ★ the SAME value spelled explicitly stays ACCEPTED —
    CHECK(id == 0xFFFFFFFFu);                                            //   the bug was a range error MAPPING onto it, not the value
    id = 0; CHECK(mrfw::parse_team_target("0xffffffff", id, tail));      // lower-case hex, same value
    CHECK(id == 0xFFFFFFFFu);
    id = 0; CHECK(mrfw::parse_team_target("037777777777", id, tail));    // and base 0 still means OCTAL on a leading 0 (= 0xFFFFFFFF)
    CHECK(id == 0xFFFFFFFFu);
    // B1's regression control, re-pinned here: a boundary/hex id followed by the legitimate PHY tail still parses.
    id = 0; tail = nullptr;
    CHECK(mrfw::parse_team_target("0x88A672BA freq=868 sf=7", id, tail));
    CHECK(id == 0x88A672BAu);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail) == " freq=868 sf=7");
    id = 0; CHECK(mrfw::parse_team_target("4294967295 freq=869.0 sf=7 bw=125", id, tail));
    CHECK(id == 0xFFFFFFFFu);
    CHECK(tail != nullptr); if (tail) CHECK(std::string(tail) == " freq=869.0 sf=7 bw=125");
    // ★ errno HYGIENE: a pre-set ERANGE from unrelated earlier code must not make a GOOD id refuse. The fix clears
    // errno before the call precisely so the flag means "this parse overflowed" and nothing else.
    errno = ERANGE;
    id = 0; CHECK(mrfw::parse_team_target("42", id, tail));
    CHECK(id == 42u);
}

// ============================================================================================================
// ★★ §B136 — `parse_seq_arg`: the STRICT target parser for the DESTRUCTIVE `del_msg <dm|chan> <seq>` verb.
// The bare `strtoul(args, nullptr, 10)` it replaces deleted sequence 1 for `1oops`, `1 extra` and `+1` alike.
// FAIL-CLOSED is the property that protects the operator, so every refusal case below seeds `out` with a
// sentinel and asserts the SENTINEL SURVIVED — an error return with a half-written target is the same class of
// defect as the one this fixes.
// ============================================================================================================
TEST_CASE("§B136 parse_seq_arg: only ONE unsigned decimal token is accepted for a DESTRUCTIVE target") {
    uint32_t v = 0;
    // ---- ACCEPTED: the forms the operator and the §3.5 modal actually produce ----
    v = 0; CHECK(mrfw::parse_seq_arg("1", v));            CHECK(v == 1u);
    v = 0; CHECK(mrfw::parse_seq_arg(" 7", v));           CHECK(v == 7u);       // the kind parser leaves the separating space
    v = 0; CHECK(mrfw::parse_seq_arg("  12  ", v));       CHECK(v == 12u);      // trailing whitespace only
    v = 0; CHECK(mrfw::parse_seq_arg("42\r\n", v));       CHECK(v == 42u);      // a CRLF console line
    v = 1; CHECK(mrfw::parse_seq_arg("0", v));            CHECK(v == 0u);       // 0 is SYNTACTICALLY fine — the inbox never issues it, so erase() answers not_found
    v = 0; CHECK(mrfw::parse_seq_arg("4294967295", v));   CHECK(v == 0xFFFFFFFFu);  // UINT32_MAX exactly, in range
    v = 0; CHECK(mrfw::parse_seq_arg("010", v));          CHECK(v == 10u);      // ★ base 10, NOT octal — `010` must not silently mean message 8

    // ---- REFUSED, and `out` must SURVIVE UNTOUCHED every time (fail-closed) ----
    const uint32_t kSentinel = 0xDEADBEEFu;
    const char* refuse[] = {
        "",  " ", "   ",                 // empty / whitespace-only: no target named at all
        "abc", "dm", "-", "+",           // junk
        "+1", "-1", "-0",                // ★ signs: strtoul ACCEPTS both and "-1" becomes 0xFFFFFFFF
        "1oops", "1 extra", "1,2",       // ★ junk suffix / a SECOND token / a list
        "1.5", "1e3", "0x1", "0X10",     // a non-integer or a hex spelling (base 10 stops at the 'x')
        " 1 2 ",                         // two tokens with the same shape as one
        "4294967296",                    // ★ (3b) does not fit 32 bits — truncates to 0 on the 64-bit host
        "99999999999999999999999",       // ★ (3a) ERANGE even on the host -> would saturate to 0xFFFFFFFF
        "12345678901234567890123456789012345678901234567890",
    };
    for (const char* s : refuse) {
        uint32_t out = kSentinel;
        const bool ok = mrfw::parse_seq_arg(s, out);
        CHECK_FALSE(ok);
        CHECK(out == kSentinel);                          // ★ nothing was half-written for the delete to act on
    }
    uint32_t nul = kSentinel; CHECK_FALSE(mrfw::parse_seq_arg(nullptr, nul)); CHECK(nul == kSentinel);

    // ★ errno HYGIENE (same rule as parse_team_target): a stale ERANGE from earlier code must not refuse a good seq.
    errno = ERANGE;
    v = 0; CHECK(mrfw::parse_seq_arg("5", v)); CHECK(v == 5u);
    // ★ THE ABI SPLIT, pinned rather than argued: exactly as at parse_team_target (3a)/(3b), only ONE of the two
    // range arms does the work on a given ABI, so neither can be shown necessary by testing this ABI alone.
    CHECK((static_cast<unsigned long>(UINT32_MAX) < ULONG_MAX) == (sizeof(unsigned long) > 4));
}
