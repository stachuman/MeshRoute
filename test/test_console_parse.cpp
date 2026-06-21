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

TEST_CASE("parse_command — send <hash> auto-detect + -e (CRYPTED, hash-only)") {
    Command c{};
    const char* h = "send 1a2b3c4d \"hi\" -e";
    CHECK(parse_command(h, std::strlen(h), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 0);
    CHECK(c.u.send.dst_hash == 0x1a2b3c4du);
    CHECK(c.crypt == CryptIntent::on);               // -e => CRYPTED
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi");
    const char* d = "send 12345678 \"x\"";           // 8 all-digit chars => HASH (8-hex wins), not an id
    CHECK(parse_command(d, std::strlen(d), c) == ParseErr::ok);
    CHECK(c.u.send.dst_id == 0);
    CHECK(c.u.send.dst_hash == 0x12345678u);
    const char* idtok = "send 100 \"x\"";            // <=3 digits => id
    CHECK(parse_command(idtok, std::strlen(idtok), c) == ParseErr::ok);
    CHECK(c.u.send.dst_id == 100);
    CHECK(c.u.send.dst_hash == 0u);
}

TEST_CASE("parse_command — flags before OR after the quoted body both parse") {
    Command c{};
    const char* after  = "send 1a2b3c4d \"hi\" -a -e";
    CHECK(parse_command(after, std::strlen(after), c) == ParseErr::ok);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ); CHECK(c.crypt == CryptIntent::on);
    const char* before = "send 1a2b3c4d -e -a \"hi\"";
    CHECK(parse_command(before, std::strlen(before), c) == ParseErr::ok);
    CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ); CHECK(c.crypt == CryptIntent::on);
    const char* mixed  = "send 1a2b3c4d -e \"hi\" -a";
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
    const char* line = "send_layer a1b2c3d4 2,3 \"hi there\" -a";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.u.layer.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.layer.hop_count == 2);
    CHECK(c.u.layer.hops[0] == 2); CHECK(c.u.layer.hops[1] == 3);
    CHECK(c.u.layer.flags == DATA_FLAG_E2E_ACK_REQ);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi there");
    const char* one = "send_layer 0a0b0c0d 5 \"yo\"";
    CHECK(parse_command(one, std::strlen(one), c) == ParseErr::ok);
    CHECK(c.u.layer.hop_count == 1); CHECK(c.u.layer.hops[0] == 5);
    CHECK(c.u.layer.flags == 0x00);
    const char* e = "send_layer a1b2c3d4 2 \"hi\" -e";   // -e invalid on a layer target
    CHECK(parse_command(e, std::strlen(e), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — send_layer malformed paths -> bad_args (fail loud)") {
    Command c{};
    const char* toomany = "send_layer a1b2c3d4 2,3,4,5 \"hi\"";
    CHECK(parse_command(toomany, std::strlen(toomany), c) == ParseErr::bad_args);
    const char* nonnum  = "send_layer a1b2c3d4 2,x \"hi\"";
    CHECK(parse_command(nonnum, std::strlen(nonnum), c) == ParseErr::bad_args);
    const char* zero    = "send_layer a1b2c3d4 0 \"hi\"";
    CHECK(parse_command(zero, std::strlen(zero), c) == ParseErr::bad_args);
    const char* empties = "send_layer a1b2c3d4 2,,3 \"hi\"";
    CHECK(parse_command(empties, std::strlen(empties), c) == ParseErr::bad_args);
    const char* nopath  = "send_layer a1b2c3d4";
    CHECK(parse_command(nopath, std::strlen(nopath), c) == ParseErr::bad_args);
    const char* badhash = "send_layer zz 2 \"hi\"";
    CHECK(parse_command(badhash, std::strlen(badhash), c) == ParseErr::bad_args);
    const char* over255 = "send_layer a1b2c3d4 300 \"hi\"";
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

// §2 per-message crypt (HARD SWITCH): -e => CRYPTED; absent => the node's e2e_dm default. The old sendhash
// force-PLAIN / sendhashx force-CRYPT verbs are gone — `cfg set e2e_dm off` + no -e is the plain path.
TEST_CASE("parse_command — -e carries the per-message crypt intent; absent = e2e_dm default") {
    Command c{};
    const char* e = "send a1b2c3d4 \"hi there\" -e";
    CHECK(parse_command(e, std::strlen(e), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send); CHECK(c.u.send.dst_hash == 0xa1b2c3d4u);
    CHECK(c.crypt == CryptIntent::on); CHECK(c.u.send.flags == 0x00);
    const char* ea = "send a1b2c3d4 \"hi\" -a -e";
    CHECK(parse_command(ea, std::strlen(ea), c) == ParseErr::ok);
    CHECK(c.crypt == CryptIntent::on); CHECK(c.u.send.flags == DATA_FLAG_E2E_ACK_REQ);   // crypted + E2E ack
    const char* plain = "send a1b2c3d4 \"hi\"";              // no -e -> default (NOT force-plain)
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
