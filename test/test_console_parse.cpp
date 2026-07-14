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

TEST_CASE("parse_command — send <id> \"text\" [-a] (id target; quoted body; flags)") {
    Command c{};
    const char* p = "send 5 \"hello world\"";
    CHECK(parse_command(p, std::strlen(p), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.dst_hash == 0u);
    CHECK(c.u.send.flags == 0x00);
    CHECK(c.crypt == CryptIntent::def);              // no -e -> the node's e2e_dm default
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hello world");
    const char* a = "send 5 \"hi\" -a";
    CHECK(parse_command(a, std::strlen(a), c) == ParseErr::ok);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi");
}

TEST_CASE("parse_command — Wave 2 `-t` sets the TEAM plane; plain send = GLOBAL; -t rejected on send_channel") {
    Command c{};
    const char* g = "send 5 \"hi\"";                     // plain send -> GLOBAL (2)
    CHECK(parse_command(g, std::strlen(g), c) == ParseErr::ok);
    CHECK(c.u.send.plane == 2);
    const char* t = "send 5 \"hi\" -t";                  // -t (tail flag, like -a/-e) on an id target -> TEAM (1)
    CHECK(parse_command(t, std::strlen(t), c) == ParseErr::ok);
    CHECK(c.u.send.plane == 1); CHECK(c.u.send.dst_id == 5);
    const char* th = "send 0x4be09089 \"hi\" -e -t";     // -t + -e together on a hash target
    CHECK(parse_command(th, std::strlen(th), c) == ParseErr::ok);
    CHECK(c.u.send.plane == 1); CHECK(c.u.send.dst_hash == 0x4be09089u); CHECK(c.crypt == CryptIntent::on);
    const char* sc = "send_channel 3 \"hi\" -t";         // -t is send-only -> rejected elsewhere
    CHECK(parse_command(sc, std::strlen(sc), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — reqpubkey -t = TEAM plane; plain = GLOBAL; a bare team-id is implicitly TEAM") {
    Command c{};
    const char* g = "reqpubkey 0xdeadbeef";                  // plain -> GLOBAL (2)
    CHECK(parse_command(g, std::strlen(g), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::reqpubkey); CHECK(c.u.resolve.plane == 2); CHECK(c.u.resolve.dst_hash == 0xdeadbeefu);
    const char* t = "reqpubkey 0xdeadbeef -t";               // -t -> TEAM (1)
    CHECK(parse_command(t, std::strlen(t), c) == ParseErr::ok);
    CHECK(c.u.resolve.plane == 1);
    const char* bid = "reqpubkey 93";                        // bare team-id -> implicitly TEAM
    CHECK(parse_command(bid, std::strlen(bid), c) == ParseErr::ok);
    CHECK(c.u.resolve.plane == 1); CHECK(c.u.resolve.dst_id == 93);
    const char* bad = "reqpubkey 0xdeadbeef -x";             // unknown trailing flag -> error
    CHECK(parse_command(bad, std::strlen(bad), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — send 0xhash + -e (CRYPTED, hash-only)") {
    Command c{};
    const char* h = "send 0x1a2b3c4d \"hi\" -e";
    CHECK(parse_command(h, std::strlen(h), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 0);
    CHECK(c.u.send.dst_hash == 0x1a2b3c4du);
    CHECK(c.crypt == CryptIntent::on);               // -e => CRYPTED
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi");
    const char* d = "send 0x12345678 \"x\"";           // 0x-prefixed all-digit token => HASH (unambiguous), not an id
    CHECK(parse_command(d, std::strlen(d), c) == ParseErr::ok);
    CHECK(c.u.send.dst_id == 0);
    CHECK(c.u.send.dst_hash == 0x12345678u);
    const char* idtok = "send 100 \"x\"";            // a bare decimal <=254 => id
    CHECK(parse_command(idtok, std::strlen(idtok), c) == ParseErr::ok);
    CHECK(c.u.send.dst_id == 100);
    CHECK(c.u.send.dst_hash == 0u);
}

TEST_CASE("parse_command — a BARE hex hash (no 0x) is NOT a hash (kills id-vs-hash ambiguity)") {
    Command c{};
    const char* s = "send 1a2b3c4d \"x\"";           // was auto-hash; now not-0x + not decimal <=254 -> bad_args
    CHECK(parse_command(s, std::strlen(s), c) == ParseErr::bad_args);
    const char* r = "reqpubkey 1a2b3c4d";
    CHECK(parse_command(r, std::strlen(r), c) == ParseErr::bad_args);
    const char* v = "resolve 1a2b3c4d";
    CHECK(parse_command(v, std::strlen(v), c) == ParseErr::bad_args);
    const char* ok = "send 0x1a2b3c4d \"x\"";         // the 0x form works
    CHECK(parse_command(ok, std::strlen(ok), c) == ParseErr::ok);
    CHECK(c.u.send.dst_hash == 0x1a2b3c4du);
}

TEST_CASE("parse_command — flags before OR after the quoted body both parse") {
    Command c{};
    const char* after  = "send 0x1a2b3c4d \"hi\" -a -e";
    CHECK(parse_command(after, std::strlen(after), c) == ParseErr::ok);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ); CHECK(c.crypt == CryptIntent::on);
    const char* before = "send 0x1a2b3c4d -e -a \"hi\"";
    CHECK(parse_command(before, std::strlen(before), c) == ParseErr::ok);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ); CHECK(c.crypt == CryptIntent::on);
    const char* mixed  = "send 0x1a2b3c4d -e \"hi\" -a";
    CHECK(parse_command(mixed, std::strlen(mixed), c) == ParseErr::ok);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ); CHECK(c.crypt == CryptIntent::on);
}

TEST_CASE("parse_command — send errors: -e on non-hash, unquoted body, no body, bad target/flag") {
    Command c{};
    const char* eonid  = "send 5 \"hi\" -e";          // -e on an id target -> error
    CHECK(parse_command(eonid, std::strlen(eonid), c) == ParseErr::bad_args);
    const char* unq    = "send 5 hello";              // unquoted body -> error
    CHECK(parse_command(unq, std::strlen(unq), c) == ParseErr::bad_args);
    const char* nobody = "send 5 -a";                 // no body -> error
    CHECK(parse_command(nobody, std::strlen(nobody), c) == ParseErr::bad_args);
    const char* big    = "send 255 \"x\"";            // id > 254 + not 8-hex -> error
    CHECK(parse_command(big, std::strlen(big), c) == ParseErr::bad_args);
    const char* nonhex = "send abcd \"x\"";           // 4 chars: not 8-hex, not decimal -> error
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);
    const char* badflag= "send 5 \"x\" -z";           // unknown flag -> error
    CHECK(parse_command(badflag, std::strlen(badflag), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — send_channel <ch> \"text\" (no ack/enc)") {
    Command c{};
    const char* p = "send_channel 7 \"broadcast msg\"";
    CHECK(parse_command(p, std::strlen(p), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_channel);
    CHECK(c.u.channel.channel_id == 7);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "broadcast msg");
    const char* hi = "send_channel 255 \"x\"";        // channel id 0..255 (wider than the 0..254 dst id)
    CHECK(parse_command(hi, std::strlen(hi), c) == ParseErr::ok);
    CHECK(c.u.channel.channel_id == 255);
    const char* aflag = "send_channel 7 \"x\" -a";    // -a/-e on a channel -> error
    CHECK(parse_command(aflag, std::strlen(aflag), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — send_layer <hash> <l1,l2,…> \"text\" [-a]") {
    Command c{};
    const char* line = "send_layer 0xa1b2c3d4 2,3 \"hi there\" -a";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.u.layer.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.layer.hop_count == 2);
    CHECK(c.u.layer.hops[0] == 2); CHECK(c.u.layer.hops[1] == 3);
    CHECK(c.u.layer.flags == DATA_FLAG_E2E_ACK_REQ);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi there");
    const char* one = "send_layer 0x0a0b0c0d 5 \"yo\"";
    CHECK(parse_command(one, std::strlen(one), c) == ParseErr::ok);
    CHECK(c.u.layer.hop_count == 1); CHECK(c.u.layer.hops[0] == 5);
    CHECK(c.u.layer.flags == 0x00);
    const char* e = "send_layer 0xa1b2c3d4 2 \"hi\" -e";   // -e invalid on a layer target
    CHECK(parse_command(e, std::strlen(e), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — send_layer malformed paths -> bad_args (fail loud)") {
    Command c{};
    const char* toomany = "send_layer 0xa1b2c3d4 2,3,4,5 \"hi\"";
    CHECK(parse_command(toomany, std::strlen(toomany), c) == ParseErr::bad_args);
    const char* nonnum  = "send_layer 0xa1b2c3d4 2,x \"hi\"";
    CHECK(parse_command(nonnum, std::strlen(nonnum), c) == ParseErr::bad_args);
    const char* zero    = "send_layer 0xa1b2c3d4 0 \"hi\"";
    CHECK(parse_command(zero, std::strlen(zero), c) == ParseErr::bad_args);
    const char* empties = "send_layer 0xa1b2c3d4 2,,3 \"hi\"";
    CHECK(parse_command(empties, std::strlen(empties), c) == ParseErr::bad_args);
    const char* nopath  = "send_layer 0xa1b2c3d4";
    CHECK(parse_command(nopath, std::strlen(nopath), c) == ParseErr::bad_args);
    const char* badhash = "send_layer zz 2 \"hi\"";
    CHECK(parse_command(badhash, std::strlen(badhash), c) == ParseErr::bad_args);
    const char* over255 = "send_layer 0xa1b2c3d4 300 \"hi\"";
    CHECK(parse_command(over255, std::strlen(over255), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — §2 HARD SWITCH: the removed send verbs are unknown_verb") {
    Command c{};
    const char* removed[] = { "send_ack 5 \"hi\"", "sendhash a1b2c3d4 \"hi\"", "sendhash_ack a1b2c3d4 \"hi\"",
                              "sendhashx a1b2c3d4 \"hi\"", "sendhashx_ack a1b2c3d4 \"hi\"", "send_layer_ack a1b2c3d4 2 \"hi\"" };
    for (const char* v : removed) CHECK(parse_command(v, std::strlen(v), c) == ParseErr::unknown_verb);
}

TEST_CASE("parse_command — resolve <hash> [hard] (network hash-locate, notify-only)") {
    Command c{};
    const char* soft = "resolve 0xa1b2c3d4";
    CHECK(parse_command(soft, std::strlen(soft), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::resolve);
    CHECK(c.u.resolve.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.resolve.hard == false);
    const char* hard = "resolve 0x00ff00ff hard";
    CHECK(parse_command(hard, std::strlen(hard), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::resolve);
    CHECK(c.u.resolve.dst_hash == 0x00ff00ffu);
    CHECK(c.u.resolve.hard == true);
}

TEST_CASE("parse_command — resolve bad hash / bad 2nd arg -> bad_args") {
    Command c{};
    const char* nonhex = "resolve zz";
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);
    const char* badopt = "resolve 0xa1 soft";       // only `hard` is a valid 2nd arg
    CHECK(parse_command(badopt, std::strlen(badopt), c) == ParseErr::bad_args);
}

// §6 (E2E peer-key provisioning): reqpubkey <key_hash32 hex8> — the user-triggered on-air pubkey request.
TEST_CASE("parse_command — reqpubkey <hash> (user-triggered WANT_PUBKEY request)") {
    Command c{};
    const char* line = "reqpubkey 0xa1b2c3d4";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::reqpubkey);
    CHECK(c.u.resolve.dst_hash == 0xa1b2c3d4u);
    const char* bad = "reqpubkey zz";
    CHECK(parse_command(bad, std::strlen(bad), c) == ParseErr::bad_args);
    CHECK(c.u.resolve.dst_id == 0);   // a hash-addressed request leaves dst_id 0
}

// §enc: reqpubkey <team-id> — a decimal <=254 is a teammate's team_local_id (the hash is resolved from the team key
// cache at execution). Mirrors the send verb's id-vs-hash auto-detect.
TEST_CASE("parse_command — reqpubkey <team-id> (decimal -> dst_id, resolved from the team cache at execution)") {
    Command c{};
    const char* line = "reqpubkey 25";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::reqpubkey);
    CHECK(c.u.resolve.dst_id == 25);       // ★ the team_local_id
    CHECK(c.u.resolve.dst_hash == 0);      // ★ resolved later, not a hash
    const char* h = "reqpubkey 0xa1b2c3d4";  // 8-hex still routes to the hash path
    CHECK(parse_command(h, std::strlen(h), c) == ParseErr::ok);
    CHECK(c.u.resolve.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.resolve.dst_id == 0);
    const char* z = "reqpubkey 0";         // id 0 is reserved -> bad_args
    CHECK(parse_command(z, std::strlen(z), c) == ParseErr::bad_args);
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

// §2 per-message crypt (HARD SWITCH): -e => CRYPTED; absent => the node's e2e_dm default. The old sendhash
// force-PLAIN / sendhashx force-CRYPT verbs are gone — `cfg set e2e_dm off` + no -e is the plain path.
TEST_CASE("parse_command — -e carries the per-message crypt intent; absent = e2e_dm default") {
    Command c{};
    const char* e = "send 0xa1b2c3d4 \"hi there\" -e";
    CHECK(parse_command(e, std::strlen(e), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send); CHECK(c.u.send.dst_hash == 0xa1b2c3d4u);
    CHECK(c.crypt == CryptIntent::on); CHECK(c.u.send.flags == 0x00);
    const char* ea = "send 0xa1b2c3d4 \"hi\" -a -e";
    CHECK(parse_command(ea, std::strlen(ea), c) == ParseErr::ok);
    CHECK(c.crypt == CryptIntent::on); CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ);   // crypted + E2E ack
    const char* plain = "send 0xa1b2c3d4 \"hi\"";              // no -e -> default (NOT force-plain)
    CHECK(parse_command(plain, std::strlen(plain), c) == ParseErr::ok);
    CHECK(c.crypt == CryptIntent::def);
    const char* sid = "send 2 \"hi\"";
    CHECK(parse_command(sid, std::strlen(sid), c) == ParseErr::ok);
    CHECK(c.crypt == CryptIntent::def);                       // id target -> default (follows e2e_dm)
}

TEST_CASE("parse_command — errors") {
    Command c{};
    CHECK(parse_command("ping 5 x", 8, c) == ParseErr::unknown_verb);
    CHECK(parse_command("send x hi", 9, c) == ParseErr::bad_args);    // non-numeric dst
    CHECK(parse_command("send 999 hi", 11, c) == ParseErr::bad_args); // dst > 254
    CHECK(parse_command("send_channel 999 x", 18, c) == ParseErr::bad_args); // channel > 255
    CHECK(parse_command("", 0, c) == ParseErr::empty);
}

// L3: `send 00000000` (an all-zero hash) must be rejected, not aliased to a unicast to reserved id 0.
// Mirrors send_layer's h==0 guard (which already rejects `send_layer 0x00000000 ...`).
TEST_CASE("parse_command — send 0x00000000 (all-zero hash) -> bad_args (mirror send_layer h==0)") {
    Command c{};
    const char* zero = "send 0x00000000 \"hi\"";
    CHECK(parse_command(zero, std::strlen(zero), c) == ParseErr::bad_args);
    const char* layerzero = "send_layer 0x00000000 2 \"hi\"";   // send_layer already guards this
    CHECK(parse_command(layerzero, std::strlen(layerzero), c) == ParseErr::bad_args);
    const char* ok = "send 0x00000001 \"hi\"";                  // a nonzero all-hex token still parses as a hash
    CHECK(parse_command(ok, std::strlen(ok), c) == ParseErr::ok);
    CHECK(c.u.send.dst_hash == 0x00000001u);
}

// L2: parse_u32_tok(max=0xFFFFFFFF) must REJECT an over-u32 token (accumulator wrap), not parse it as 0.
// Driven through `cfg beacon_period_ms` — the only call site that passes max == 0xFFFFFFFF.
TEST_CASE("parse_cfg — beacon_period_ms over-u32 token rejected (no mod-2^32 wrap)") {
    NodeConfig c{}; uint8_t id = 0; uint32_t key = 0;
    auto P = [&](const char* l) { return parse_cfg(l, std::strlen(l), c, id, key); };
    CHECK(P("cfg beacon_period_ms 4294967296") == CfgErr::bad_value);   // 2^32: would wrap to 0 without the overflow guard
    CHECK(P("cfg beacon_period_ms 4294967295") == CfgErr::ok);          // UINT32_MAX: the largest valid value
    CHECK(c.beacon_period_ms == 0xFFFFFFFFu);
    CHECK(P("cfg beacon_period_ms 99999999999") == CfgErr::bad_value);  // way over -> reject (not a wrapped truncation)
    CHECK(P("cfg beacon_period_ms 0") == CfgErr::ok);                   // 0 still parses here (the floor is enforced at the fw_main cfg-set layer)
    CHECK(c.beacon_period_ms == 0u);
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
