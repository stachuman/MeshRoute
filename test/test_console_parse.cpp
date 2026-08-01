// MeshRoute — test_console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_parse.h"
#include "frame_codec.h"   // DATA_FLAG_E2E_ACK_REQ — assert the parser emits the bit the RX acts on
#include <cstring>
#include <string>

using namespace meshroute;            // Command, CmdKind, NodeConfig
using namespace meshroute::console;   // parse_command, ParseErr (parse_cfg/CfgErr DELETED §3-A.7)

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

TEST_CASE("parse_command — Wave 2 `-t` sets the TEAM plane; plain send = GLOBAL; §S7 send_channel accepts -t/-g") {
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
    // §S7 T-B: send_channel now ACCEPTS -t (TEAM) / -g (explicit GLOBAL) / `-t -g` (BOTH); plain leaves both clear (=> GLOBAL).
    const char* scp = "send_channel 3 \"hi\"";           // plain
    CHECK(parse_command(scp, std::strlen(scp), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_channel); CHECK(!c.u.channel.team); CHECK(!c.u.channel.global);
    const char* sct = "send_channel 3 \"hi\" -t";        // TEAM
    CHECK(parse_command(sct, std::strlen(sct), c) == ParseErr::ok);
    CHECK(c.u.channel.team); CHECK(!c.u.channel.global);
    const char* scg = "send_channel 3 \"hi\" -g";        // explicit GLOBAL
    CHECK(parse_command(scg, std::strlen(scg), c) == ParseErr::ok);
    CHECK(!c.u.channel.team); CHECK(c.u.channel.global);
    const char* scb = "send_channel 3 \"hi\" -t -g";     // BOTH
    CHECK(parse_command(scb, std::strlen(scb), c) == ParseErr::ok);
    CHECK(c.u.channel.team); CHECK(c.u.channel.global);
    const char* scx = "send_channel 3 \"hi\" -e";        // §chan-crypt CL1: -e now PARSES (on_command refuses this form)
    CHECK(parse_command(scx, std::strlen(scx), c) == ParseErr::ok);
    CHECK(c.crypt == CryptIntent::on);
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

// ★★ §loc-per-send (2026-07-31, open-bug-register B0): `-l` = attach this node's position to THIS message. It replaces
// the removed `cfg set loc_dm` global toggle, which attached the position to every originated DM on a size check ALONE
// (no crypt gate) so a plaintext DM aired coordinates in the clear. The parser's whole job is to put
// DATA_FLAG_LOCATION into the EXISTING flags word — the refusals live in Node::enqueue_data / on_command.
TEST_CASE("parse_command — §loc-per-send `-l` sets DATA_FLAG_LOCATION on send (id AND hash), in any order") {
    Command c{};
    const char* id = "send 5 \"hi\" -l";
    CHECK(parse_command(id, std::strlen(id), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send);
    CHECK(c.u.send.dst_id == 5);
    CHECK(c.u.send.flags == DATA_FLAG_LOCATION);            // the ONLY bit set — `-l` does not imply -a
    CHECK(c.crypt == CryptIntent::def);                     // and it does not imply -e either (e2e_dm decides)
    // ★ `-l` is accepted on an ID target even though `-e` is NOT: a node with e2e_dm on seals by default, so this is the
    //   normal sealed case there; with e2e_dm off enqueue_data refuses it loudly, which IS the rule.
    const char* hash = "send 0xa1b2c3d4 -l -e -a \"x\"";    // hash target: -l alongside -e/-a, flags before the body
    CHECK(parse_command(hash, std::strlen(hash), c) == ParseErr::ok);
    CHECK(c.u.send.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.send.flags == (DATA_FLAG_E2E_ACK_REQ | DATA_FLAG_LOCATION));
    CHECK(c.crypt == CryptIntent::on);
    const char* after = "send 5 \"hi\" -a -l";              // order-free, same as every other flag
    CHECK(parse_command(after, std::strlen(after), c) == ParseErr::ok);
    CHECK(c.u.send.flags == (DATA_FLAG_E2E_ACK_REQ | DATA_FLAG_LOCATION));
    const char* none = "send 5 \"hi\"";                     // CONTROL: no -l => the bit stays clear (an ordinary DM is untouched)
    CHECK(parse_command(none, std::strlen(none), c) == ParseErr::ok);
    CHECK((c.u.send.flags & DATA_FLAG_LOCATION) == 0);
}

TEST_CASE("parse_command — §loc-per-send `-l` parses on send_layer AND (§chan-crypt CL2b) on send_channel; on_command owns the refusals") {
    Command c{};
    // send_layer ACCEPTS the letter so on_command can refuse it with an explanation (a cross-layer frame carries no
    // position) instead of the operator getting a bare bad_args. One letter, one meaning, across the verbs.
    const char* lay = "send_layer 0xa1b2c3d4 2,3 \"hi\" -l";
    CHECK(parse_command(lay, std::strlen(lay), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.u.layer.flags == DATA_FLAG_LOCATION);
    // ★ §chan-crypt CL2b: send_channel now ACCEPTS `-l` too — the owner's target is `send_channel <ch> "…" -t -l -e`.
    // Same accepted-not-honoured rule as `-e`: whether a position may ride depends on the EFFECTIVE-crypt decision
    // (key held + team_channel_crypt), which only on_command can compute — so all three producers get one rule.
    // The position lands in the SEALED inner's flags byte (bit1), never in a DATA frame flag, which is why this
    // verb carries a dedicated `u.channel.loc` rather than borrowing `SendCmd::flags`/DATA_FLAG_LOCATION.
    const char* ch = "send_channel 7 \"x\" -l";
    CHECK(parse_command(ch, std::strlen(ch), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_channel);
    CHECK(c.u.channel.loc);
    CHECK_FALSE(c.u.channel.team);                        // on_command refuses THIS one (no team ⇒ no content key)
    const char* cht = "send_channel 7 \"x\" -t -l -e";     // ★ the target form
    CHECK(parse_command(cht, std::strlen(cht), c) == ParseErr::ok);
    CHECK(c.u.channel.team); CHECK(c.u.channel.loc); CHECK(c.crypt == CryptIntent::on);
    const char* none = "send_channel 7 \"x\" -t -e";       // CONTROL: no -l => the field stays clear
    CHECK(parse_command(none, std::strlen(none), c) == ParseErr::ok);
    CHECK_FALSE(c.u.channel.loc);
    const char* glued = "send_channel 7 \"x\" -tl";        // still a LONE token, like every other flag
    CHECK(parse_command(glued, std::strlen(glued), c) == ParseErr::bad_args);
}

TEST_CASE("parse_command — send_channel <ch> \"text\" (no ack: O3)") {
    Command c{};
    const char* p = "send_channel 7 \"broadcast msg\"";
    CHECK(parse_command(p, std::strlen(p), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_channel);
    CHECK(c.u.channel.channel_id == 7);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "broadcast msg");
    CHECK(c.crypt == CryptIntent::def);               // ★ no -e => `def`, byte-identical to the pre-CL1 hardcoded value
    const char* hi = "send_channel 255 \"x\"";        // channel id 0..255 (wider than the 0..254 dst id)
    CHECK(parse_command(hi, std::strlen(hi), c) == ParseErr::ok);
    CHECK(c.u.channel.channel_id == 255);
    const char* aflag = "send_channel 7 \"x\" -a";    // -a on a channel -> error (O3: no single recipient to ack)
    CHECK(parse_command(aflag, std::strlen(aflag), c) == ParseErr::bad_args);
}

// ★★ §chan-crypt CL1 (spec 2026-07-30 §2.2) — the PARSE half of the four-case matrix. All four forms must reach
// on_command with the right (team, global, crypt) triple; on_command owns which of them are then REFUSED, and the
// companion's binary transport builds the same triple without ever coming through here.
TEST_CASE("parse_command — §chan-crypt send_channel -e: all four matrix cases parse to the right (team,global,crypt)") {
    Command c{};
    struct Case { const char* line; bool team; bool global; CryptIntent crypt; };
    const Case cases[] = {
        { "send_channel 4 \"hi\"",           false, false, CryptIntent::def },  // GLOBAL, plaintext  — unchanged
        { "send_channel 4 \"hi\" -t",        true,  false, CryptIntent::def },  // TEAM,   plaintext  — unchanged
        { "send_channel 4 \"hi\" -t -e",     true,  false, CryptIntent::on  },  // ★ the target capability (CL1 stubs it)
        { "send_channel 4 \"hi\" -e",        false, false, CryptIntent::on  },  // ❌ on_command REFUSES (no team => no key)
        { "send_channel 4 \"hi\" -t -g -e",  true,  true,  CryptIntent::on  },  // ❌ on_command REFUSES (clear global copy)
        { "send_channel 4 \"hi\" -e -t",     true,  false, CryptIntent::on  },  // flag ORDER is free (parse_send_tail)
    };
    for (const Case& k : cases) {
        CHECK(parse_command(k.line, std::strlen(k.line), c) == ParseErr::ok);
        CHECK(c.kind == CmdKind::send_channel);
        CHECK(c.u.channel.channel_id == 4);
        CHECK(c.u.channel.team   == k.team);
        CHECK(c.u.channel.global == k.global);
        CHECK(c.crypt            == k.crypt);
        CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi");
    }
    // `-e` is a LONE token like every other flag: a glued form is still an error, not a silent accept.
    const char* glued = "send_channel 4 \"hi\" -et";
    CHECK(parse_command(glued, std::strlen(glued), c) == ParseErr::bad_args);
    // and it is still not a licence for -a (O3) on this verb — a channel post has no single recipient to ack.
    const char* ae = "send_channel 4 \"hi\" -e -a";
    CHECK(parse_command(ae, std::strlen(ae), c) == ParseErr::bad_args);
    // ★ `-l` IS a licence now (§chan-crypt CL2b) — it parses here and is adjudicated in on_command by ruling O6.
    const char* el = "send_channel 4 \"hi\" -e -l";
    CHECK(parse_command(el, std::strlen(el), c) == ParseErr::ok);
    CHECK(c.u.channel.loc); CHECK(c.crypt == CryptIntent::on); CHECK_FALSE(c.u.channel.team);
}

TEST_CASE("parse_command — send_layer <hash> <l1,l2,…> \"text\" [-a] [-e]") {
    Command c{};
    const char* line = "send_layer 0xa1b2c3d4 2,3 \"hi there\" -a";
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.u.layer.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.layer.hop_count == 2);
    CHECK(c.u.layer.hops[0] == 2); CHECK(c.u.layer.hops[1] == 3);
    CHECK(c.u.layer.flags == DATA_FLAG_E2E_ACK_REQ);
    CHECK(c.crypt == CryptIntent::def);                    // no -e => the node's e2e_dm default
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi there");
    const char* one = "send_layer 0x0a0b0c0d 5 \"yo\"";
    CHECK(parse_command(one, std::strlen(one), c) == ParseErr::ok);
    CHECK(c.u.layer.hop_count == 1); CHECK(c.u.layer.hops[0] == 5);
    CHECK(c.u.layer.flags == 0x00);
    CHECK(c.crypt == CryptIntent::def);
}

// ★ §xl-crypt-intent (2026-07-29): `-e` on send_layer. This test REPLACES one that asserted `-e` was `bad_args` —
// deliberately deleted, because it pinned the very gap this slice closes (the sim could ask for a sealed cross-layer DM
// via `send_layerx`, the console could not). -e MUST reach the core as CryptIntent::on: on_command's send_layer then
// seals into a DATA_TYPE_SEALED_RELAY or fails loud. A silent no-op here would recreate the cleartext bug at a new door.
TEST_CASE("parse_command — send_layer -e => CryptIntent::on (never a silent no-op)") {
    Command c{};
    const char* e = "send_layer 0xa1b2c3d4 2 \"hi\" -e";
    CHECK(parse_command(e, std::strlen(e), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::send_layer);
    CHECK(c.crypt == CryptIntent::on);                     // ★ the intent SURVIVES the parse
    CHECK(c.u.layer.dst_hash == 0xa1b2c3d4u);
    CHECK(c.u.layer.hop_count == 1); CHECK(c.u.layer.hops[0] == 2);
    CHECK(c.u.layer.flags == 0x00);                        // -e is not -a
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "hi");
    // flags in ANY order, and -a/-e/-K compose (the parse_send_tail contract)
    const char* both = "send_layer 0xa1b2c3d4 2,3 -e -a -K \"x\"";
    CHECK(parse_command(both, std::strlen(both), c) == ParseErr::ok);
    CHECK(c.crypt == CryptIntent::on);
    CHECK(c.u.layer.flags == DATA_FLAG_E2E_ACK_REQ);
    CHECK(c.no_intro);
    // -t is still REFUSED on send_layer (the plane split is unchanged by this slice)
    const char* t = "send_layer 0xa1b2c3d4 2 \"x\" -t";
    CHECK(parse_command(t, std::strlen(t), c) == ParseErr::bad_args);
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
    // §AB2: the name is OPTIONAL and MUST stay so — a bare peerkey carries no body (this is the shipped shape).
    CHECK(parse_command(line, std::strlen(line), c) == ParseErr::ok);
    CHECK(c.body == nullptr);
    CHECK(c.body_len == 0);
}

// ★ §AB2 (spec 2026-07-29 §2.3): the OPTIONAL one-shot name on `peerkey`, for the QR-import flow where the key and the
// label arrive together. ⚠ GRAMMAR: a BARE QUOTED tail, not the spec's `name="…"` — a kv scanner (kv_next) exists only
// in src/firmware_config_parse.h, a device-layer header lib/console must not include, so honouring that spelling here
// would fork it. Bare-quoted matches send / send_channel / peername: one grammar per library.
TEST_CASE("§AB2 parse_command — peerkey <hex64> \"<name>\" (the optional one-shot label)") {
    Command c{};
    const char* named = "peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 \"Ola K\"";
    CHECK(parse_command(named, std::strlen(named), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::peerkey);
    CHECK(c.u.peerkey.ed_pub[0] == 0x01);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "Ola K");   // spaces survive the quotes
    // An over-cap name PARSES (on_command refuses it with err_too_large -> the app's "too_long"), so the operator gets
    // a reason with a remedy instead of a flat bad_args. 40 > protocol::peer_name_max.
    const std::string over = std::string("peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 \"")
                           + std::string(40, 'X') + "\"";
    CHECK(parse_command(over.c_str(), over.size(), c) == ParseErr::ok);
    CHECK(c.body_len == 40);
    // Refusals: an UNQUOTED label, an unterminated quote, an empty label, a stray flag.
    const char* unq = "peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 Ola";
    CHECK(parse_command(unq, std::strlen(unq), c) == ParseErr::bad_args);
    const char* unterm = "peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 \"Ola";
    CHECK(parse_command(unterm, std::strlen(unterm), c) == ParseErr::bad_args);
    const char* empty = "peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 \"\"";
    CHECK(parse_command(empty, std::strlen(empty), c) == ParseErr::bad_args);
    const char* flag = "peerkey 0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20 -e \"Ola\"";
    CHECK(parse_command(flag, std::strlen(flag), c) == ParseErr::bad_args);
}

// ★★ §AB2 (spec 2026-07-29 §2.3): `peername 0x<hash> "<text>"` — EVERY PARSE REFUSAL, which the spec's gate mandates be
// covered natively. No scenario runs a console verb, so this TEST_CASE is the whole detector for this grammar.
TEST_CASE("§AB2 parse_command — peername 0x<hash> \"<name>\" and every parse refusal") {
    Command c{};
    const char* ok = "peername 0x6c297145 \"Ola K\"";
    CHECK(parse_command(ok, std::strlen(ok), c) == ParseErr::ok);
    CHECK(c.kind == CmdKind::peername);
    CHECK(c.u.peername.key_hash32 == 0x6c297145u);
    CHECK(std::string(reinterpret_cast<const char*>(c.body), c.body_len) == "Ola K");
    // The hash accepts 0X and short forms, exactly like every other 0x target in this file (parse_hex32_0x).
    const char* upper = "peername 0X1F \"x\"";
    CHECK(parse_command(upper, std::strlen(upper), c) == ParseErr::ok);
    CHECK(c.u.peername.key_hash32 == 0x1Fu);
    // 1. A BARE DECIMAL is refused — there is no id form. Naming an id-only peer is out of scope (spec §4): an id is an
    //    address, the hash is the identity, so there would be nothing stable to attach the name to.
    const char* dec = "peername 228 \"Ola\"";
    CHECK(parse_command(dec, std::strlen(dec), c) == ParseErr::bad_args);
    // 2. A hash without the 0x prefix — the B1 family: `88A672BA` read as decimal 88 is how `team` silently joined the
    //    wrong team. Requiring the prefix is what kills the ambiguity.
    const char* noprefix = "peername 6c297145 \"Ola\"";
    CHECK(parse_command(noprefix, std::strlen(noprefix), c) == ParseErr::bad_args);
    // 3. hash 0 = "unset" everywhere in this codebase; never a target.
    const char* zero = "peername 0x0 \"Ola\"";
    CHECK(parse_command(zero, std::strlen(zero), c) == ParseErr::bad_args);
    const char* zero8 = "peername 0x00000000 \"Ola\"";
    CHECK(parse_command(zero8, std::strlen(zero8), c) == ParseErr::bad_args);
    // 4. Non-hex digits.
    const char* nonhex = "peername 0xzzzz \"Ola\"";
    CHECK(parse_command(nonhex, std::strlen(nonhex), c) == ParseErr::bad_args);
    // 5. No name at all.
    const char* noname = "peername 0x6c297145";
    CHECK(parse_command(noname, std::strlen(noname), c) == ParseErr::bad_args);
    // 6. An UNQUOTED name (the body must be quoted, as on every send verb).
    const char* unq = "peername 0x6c297145 Ola";
    CHECK(parse_command(unq, std::strlen(unq), c) == ParseErr::bad_args);
    // 7. An unterminated quote.
    const char* unterm = "peername 0x6c297145 \"Ola";
    CHECK(parse_command(unterm, std::strlen(unterm), c) == ParseErr::bad_args);
    // 8. ★ An EMPTY name is REFUSED, not silently treated as "clear the name". v1 offers no clear operation, and a UI
    //    that passes "" by accident must not wipe a label. If clearing is wanted it gets its own agreed spelling.
    const char* empty = "peername 0x6c297145 \"\"";
    CHECK(parse_command(empty, std::strlen(empty), c) == ParseErr::bad_args);
    // 9. No flags on this verb — every send-tail letter is rejected, so none can grow a second meaning here.
    for (const char* f : { "peername 0x6c297145 \"Ola\" -a", "peername 0x6c297145 \"Ola\" -e",
                           "peername 0x6c297145 \"Ola\" -t", "peername 0x6c297145 \"Ola\" -l" })
        CHECK(parse_command(f, std::strlen(f), c) == ParseErr::bad_args);
    // 10. Two bodies.
    const char* two = "peername 0x6c297145 \"Ola\" \"Bob\"";
    CHECK(parse_command(two, std::strlen(two), c) == ParseErr::bad_args);
    // 11. An over-cap name PARSES here and is refused by on_command with err_too_large ("too_long"), so the reason names
    //     its remedy (shorten) rather than collapsing into bad_args (whose remedy is different).
    const std::string over = std::string("peername 0x6c297145 \"") + std::string(33, 'X') + "\"";
    CHECK(parse_command(over.c_str(), over.size(), c) == ParseErr::ok);
    CHECK(c.body_len == 33);
    // 12. `peername` must not be swallowed by the `peerkey` arm (tok_eq is length-exact) nor fall to unknown_verb.
    const char* verbonly = "peername";
    CHECK(parse_command(verbonly, std::strlen(verbonly), c) == ParseErr::bad_args);   // known verb, missing args
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
// (parse_cfg — the lying dead twin of the live `cfg set` — was DELETED §3-A.7; this now drives the guard directly.)
namespace meshroute::console { struct Tok { const char* s; size_t n; };
                               bool parse_u32_tok(const Tok& t, uint32_t max, uint32_t& out); }
TEST_CASE("parse_u32_tok — over-u32 token rejected (no mod-2^32 wrap)") {
    auto P = [](const char* lit, uint32_t max, uint32_t& out) {
        meshroute::console::Tok t{ lit, std::strlen(lit) };
        return meshroute::console::parse_u32_tok(t, max, out);
    };
    uint32_t u = 0;
    CHECK(!P("4294967296", 0xFFFFFFFFu, u));    // 2^32: would wrap to 0 without the overflow guard
    CHECK(P("4294967295", 0xFFFFFFFFu, u));     // UINT32_MAX: the largest valid value
    CHECK(u == 0xFFFFFFFFu);
    CHECK(!P("99999999999", 0xFFFFFFFFu, u));   // way over -> reject (not a wrapped truncation)
    CHECK(P("0", 0xFFFFFFFFu, u));
    CHECK(u == 0u);
    CHECK(!P("255", 254u, u));                  // the plain > max reject still works
}
