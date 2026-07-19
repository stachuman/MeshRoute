// MeshRoute — test_console_json.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_json.h"
#include <cstring>
#include <string>

using namespace meshroute;            // CmdResult, CmdCode, EventField, Push, NodeConfig
using namespace meshroute::console;   // JsonBuf, write_*

TEST_CASE("JsonBuf — primitives, escaping, overflow latch") {
    char b[64];
    {   JsonBuf j(b, sizeof b);
        j.lit("{"); j.key("n"); j.i64(-7); j.ch(',');
        j.key("s"); j.str("a\"b\n", 4); j.ch('}');
        size_t len = j.finish();
        CHECK(std::string(b, len) == "{\"n\":-7,\"s\":\"a\\\"b\\n\"}\n");
    }
    {   char tiny[8]; JsonBuf j(tiny, sizeof tiny);   // overflow → finish()==0
        j.lit("123456789");
        CHECK(j.finish() == 0);
        CHECK(j.overflow);
    }
}

// M9 (2026-07-04 wave-3): JsonBuf::str must validate UTF-8 — legit multi-byte sequences pass VERBATIM (valid
// inside a JSON string), invalid/truncated bytes are replaced with U+FFFD so the pushed line stays valid UTF-8
// (an attacker DM/channel body with a lone 0xC3 must NOT make the whole NDJSON line undecodable by iOS).
static bool is_valid_utf8(const char* p, size_t n) {   // independent re-check of the emitted line
    const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ) {
        unsigned char c = u[i];
        if (c < 0x80) { ++i; continue; }
        size_t need; unsigned char lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 2;
        else if (c >= 0xE0 && c <= 0xEF) { need = 3; if (c == 0xE0) lo = 0xA0; if (c == 0xED) hi = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { need = 4; if (c == 0xF0) lo = 0x90; if (c == 0xF4) hi = 0x8F; }
        else return false;
        if (i + need > n) return false;
        if (u[i+1] < lo || u[i+1] > hi) return false;
        for (size_t k = 2; k < need; ++k) if (u[i+k] < 0x80 || u[i+k] > 0xBF) return false;
        i += need;
    }
    return true;
}
TEST_CASE("M9 — JsonBuf::str UTF-8 validation (valid verbatim, invalid -> U+FFFD, escaping preserved)") {
    char b[64];
    // (1) valid multi-byte UTF-8 "café" (c3 a9) passes VERBATIM — NOT re-escaped as \u00xx.
    {   const char cafe[] = { 'c','a','f',(char)0xC3,(char)0xA9, 0 };   // "café"
        JsonBuf j(b, sizeof b); j.str(cafe, 5); size_t len = j.finish();
        CHECK(std::string(b, len) == std::string("\"caf\xC3\xA9\"\n"));   // bytes preserved
        CHECK(is_valid_utf8(b, len));
    }
    // (2) a valid 4-byte emoji U+1F600 (f0 9f 98 80) passes verbatim.
    {   const char emoji[] = { (char)0xF0,(char)0x9F,(char)0x98,(char)0x80, 0 };
        JsonBuf j(b, sizeof b); j.str(emoji, 4); size_t len = j.finish();
        CHECK(std::string(b, len) == std::string("\"\xF0\x9F\x98\x80\"\n"));
        CHECK(is_valid_utf8(b, len));
    }
    // (3) a lone 0xC3 (truncated 2-byte lead, no continuation) -> replaced with U+FFFD (ef bf bd); line stays valid.
    {   const char bad[] = { 'x',(char)0xC3, 0 };
        JsonBuf j(b, sizeof b); j.str(bad, 2); size_t len = j.finish();
        CHECK(std::string(b, len) == std::string("\"x\xEF\xBF\xBD\"\n"));
        CHECK(is_valid_utf8(b, len));
    }
    // (4) a lone 0xFF (never a valid lead) -> U+FFFD; valid.
    {   const char bad[] = { (char)0xFF, 0 };
        JsonBuf j(b, sizeof b); j.str(bad, 1); size_t len = j.finish();
        CHECK(std::string(b, len) == std::string("\"\xEF\xBF\xBD\"\n"));
        CHECK(is_valid_utf8(b, len));
    }
    // (5) an OVERLONG 2-byte encoding of '/' (c0 af) -> invalid lead 0xC0 -> two U+FFFD (each bad byte replaced).
    {   const char overlong[] = { (char)0xC0,(char)0xAF, 0 };
        JsonBuf j(b, sizeof b); j.str(overlong, 2); size_t len = j.finish();
        CHECK(std::string(b, len) == std::string("\"\xEF\xBF\xBD\xEF\xBF\xBD\"\n"));
        CHECK(is_valid_utf8(b, len));
    }
    // (6) a UTF-16 surrogate encoded as 3-byte (ed a0 80 = U+D800) -> invalid (0xED continuation > 0x9F) -> 3x U+FFFD.
    {   const char surr[] = { (char)0xED,(char)0xA0,(char)0x80, 0 };
        JsonBuf j(b, sizeof b); j.str(surr, 3); size_t len = j.finish();
        CHECK(is_valid_utf8(b, len));                       // whatever the replacement, output must be valid UTF-8
        CHECK(std::string(b, len).find("\xED\xA0\x80") == std::string::npos);   // the surrogate bytes are NOT passed through
    }
    // (7) the existing quote/backslash/newline/tab escaping still holds, interleaved with a valid multi-byte char.
    {   const char mix[] = { 'a','"','\\','\n','\t',(char)0xC3,(char)0xA9, 0 };
        JsonBuf j(b, sizeof b); j.str(mix, 7); size_t len = j.finish();
        CHECK(std::string(b, len) == std::string("\"a\\\"\\\\\\n\\t\xC3\xA9\"\n"));
        CHECK(is_valid_utf8(b, len));
    }
}
TEST_CASE("write_ack — CmdResult → ack JSON") {
    char b[96];
    // id-addressed send: dh/lp == 0
    size_t n = write_ack(b, sizeof b, CmdResult{CmdCode::queued, 7, 1});
    CHECK(std::string(b, n) == "{\"ack\":\"queued\",\"ctr\":7,\"qd\":1,\"dh\":0,\"lp\":0}\n");
    n = write_ack(b, sizeof b, CmdResult{CmdCode::err_unknown_dst, 0, 0});
    CHECK(std::string(b, n) == "{\"ack\":\"err_unknown_dst\",\"ctr\":0,\"qd\":0,\"dh\":0,\"lp\":0}\n");
    // hash/layer-addressed send: the handle (dh = key_hash32, lp = packed path [2,3] -> 0x0203 = 515)
    n = write_ack(b, sizeof b, CmdResult{CmdCode::queued, 9, 2, 0xDEADBEEFu, 0x0203u});
    CHECK(std::string(b, n) == "{\"ack\":\"queued\",\"ctr\":9,\"qd\":2,\"dh\":3735928559,\"lp\":515}\n");
}

TEST_CASE("write_event — type + typed EventField k/v") {
    char b[128];
    EventField f[2] = {
        { "from", EventField::T::i64, 5, 0,    nullptr, false },
        { "snr",  EventField::T::f64, 0, 7.25, nullptr, false },
    };
    size_t n = write_event(b, sizeof b, "cts_rx", f, 2);
    CHECK(std::string(b, n) == "{\"ev\":\"cts_rx\",\"from\":5,\"snr\":7.25}\n");
}

TEST_CASE("write_push — msg_recv/channel_recv carry identity + seq (model B); seq OMITTED when 0 (inbox disabled)") {
    char b[300];
    Push m{}; m.kind = PushKind::msg_recv; m.origin = 3; m.layer_id = 5; m.ctr = 7; m.sender_hash = 3735928559u;  // 0xDEADBEEF
    const char* body = "hi\"x"; m.body_len = 4; std::memcpy(m.body, body, 4);
    size_t n = write_push(b, sizeof b, m);                                   // seq==0 -> omitted (best-effort live only)
    CHECK(std::string(b, n) ==
      "{\"ev\":\"msg_recv\",\"origin\":3,\"layer_id\":5,\"ctr\":7,\"sender_hash\":3735928559,\"body\":\"hi\\\"x\"}\n");
    m.seq = 42;                                                              // inbox enabled -> seq present
    n = write_push(b, sizeof b, m);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"msg_recv\",\"origin\":3,\"layer_id\":5,\"ctr\":7,\"sender_hash\":3735928559,\"seq\":42,\"body\":\"hi\\\"x\"}\n");

    Push ch{}; ch.kind = PushKind::channel_recv; ch.origin = 4; ch.layer_id = 9; ch.channel_id = 3; ch.channel_msg_id = 68298753u; ch.seq = 7;
    const char* cb = "yo"; ch.body_len = 2; std::memcpy(ch.body, cb, 2);
    n = write_push(b, sizeof b, ch);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"channel_recv\",\"origin\":4,\"layer_id\":9,\"channel_id\":3,\"channel_msg_id\":68298753,\"seq\":7,\"body\":\"yo\"}\n");

    Push a{}; a.kind = PushKind::send_acked; a.dst = 5; a.ctr = 7;
    n = write_push(b, sizeof b, a);
    CHECK(std::string(b, n) == "{\"ev\":\"send_acked\",\"dst\":5,\"ctr\":7}\n");

    // §8b: a SEALED msg_recv stamps "enc":true (after seq, before body); plaintext omits it (above).
    Push e{}; e.kind = PushKind::msg_recv; e.origin = 3; e.layer_id = 5; e.ctr = 7; e.sender_hash = 0xDEADBEEFu; e.seq = 42; e.enc = true;
    const char* eb = "x"; e.body_len = 1; std::memcpy(e.body, eb, 1);
    n = write_push(b, sizeof b, e);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"msg_recv\",\"origin\":3,\"layer_id\":5,\"ctr\":7,\"sender_hash\":3735928559,\"seq\":42,\"enc\":true,\"body\":\"x\"}\n");
}

TEST_CASE("§GapA — msg_recv emits origin_layer on a cross-layer DM (after layer_id, before ctr); OMITTED when 0") {
    char b[300];
    Push x{}; x.kind = PushKind::msg_recv; x.origin = 101; x.layer_id = 7; x.origin_layer = 4; x.ctr = 9; x.sender_hash = 0x2716EFCDu;
    const char* body = "hi"; x.body_len = 2; std::memcpy(x.body, body, 2);
    size_t n = write_push(b, sizeof b, x);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"msg_recv\",\"origin\":101,\"layer_id\":7,\"origin_layer\":4,\"ctr\":9,\"sender_hash\":655814605,\"body\":\"hi\"}\n");
    x.origin_layer = 0;                                                      // same-layer / non-XL -> OMITTED (byte-identical to pre-GapA)
    n = write_push(b, sizeof b, x);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"msg_recv\",\"origin\":101,\"layer_id\":7,\"ctr\":9,\"sender_hash\":655814605,\"body\":\"hi\"}\n");
}

TEST_CASE("write_inbox_* — pull stream records + terminator + mark_read ack") {
    char b[400];
    size_t n = write_inbox_dm(b, sizeof b, 42, 2, /*layer_id*/ 23, 7, 3735928559u, 123456ull, "hi", 2);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_dm\",\"seq\":42,\"origin\":2,\"layer_id\":23,\"ctr\":7,\"sender_hash\":3735928559,\"rx_ms\":123456,\"body\":\"hi\"}\n");
    n = write_inbox_dm(b, sizeof b, 42, 2, /*layer_id*/ 23, 7, 3735928559u, 123456ull, "hi", 2, /*enc=*/true);  // §8b
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_dm\",\"seq\":42,\"origin\":2,\"layer_id\":23,\"ctr\":7,\"sender_hash\":3735928559,\"rx_ms\":123456,\"enc\":true,\"body\":\"hi\"}\n");
    // E2E-ack RECEIPT (type = DATA_TYPE_E2E_ACK = 3): "type":"e2e_ack" rides right after "ev"; origin = the acker, ctr = the
    // acked ctr, empty body. The default type=0 (the two calls above) OMITS the field -> the normal-DM wire is unchanged.
    n = write_inbox_dm(b, sizeof b, 9, 5, /*layer_id*/ 1, 55, 0xC0FFEEu, 222ull, "", 0, /*enc=*/false, /*type=*/3);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_dm\",\"type\":\"e2e_ack\",\"seq\":9,\"origin\":5,\"layer_id\":1,\"ctr\":55,\"sender_hash\":12648430,\"rx_ms\":222,\"body\":\"\"}\n");

    // §GapA-durable: origin_layer rides after rx_ms (before enc), OMITTED when 0.
    n = write_inbox_dm(b, sizeof b, 42, 2, /*layer_id*/ 23, 7, 3735928559u, 123456ull, "hi", 2, /*enc=*/false, /*type=*/0, /*origin_layer=*/4);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_dm\",\"seq\":42,\"origin\":2,\"layer_id\":23,\"ctr\":7,\"sender_hash\":3735928559,\"rx_ms\":123456,\"origin_layer\":4,\"body\":\"hi\"}\n");
    n = write_inbox_dm(b, sizeof b, 42, 2, /*layer_id*/ 23, 7, 3735928559u, 123456ull, "hi", 2, /*enc=*/true, /*type=*/0, /*origin_layer=*/4);
    CHECK(std::string(b, n) ==   // XL + sealed: origin_layer then enc
      "{\"ev\":\"inbox_dm\",\"seq\":42,\"origin\":2,\"layer_id\":23,\"ctr\":7,\"sender_hash\":3735928559,\"rx_ms\":123456,\"origin_layer\":4,\"enc\":true,\"body\":\"hi\"}\n");

    n = write_inbox_channel(b, sizeof b, 7, 4, /*layer_id*/ 7, 3, 68298753u, 123456ull, "yo", 2);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_channel\",\"seq\":7,\"origin\":4,\"layer_id\":7,\"channel_id\":3,\"channel_msg_id\":68298753,\"rx_ms\":123456,\"body\":\"yo\"}\n");

    n = write_inbox_end(b, sizeof b, 42, 7, 3, 15, 987654ull);
    CHECK(std::string(b, n) == "{\"ev\":\"inbox_end\",\"dm_seq\":42,\"chan_seq\":7,\"epoch\":3,\"count\":15,\"now_ms\":987654}\n");

    n = write_inbox_marked(b, sizeof b, "dm", 42);
    CHECK(std::string(b, n) == "{\"ack\":\"mark_read\",\"kind\":\"dm\",\"seq\":42}\n");
}

TEST_CASE("write_err / write_log / write_ready / write_status") {
    char b[512];   // ready-with-pubkey+duty is ~280B — must clear the largest emitter here (device streams it via the 1700B scratch)
    size_t n = write_err(b, sizeof b, "parse", "expected: send <dst> <body>");
    CHECK(std::string(b, n) == "{\"err\":\"parse\",\"msg\":\"expected: send <dst> <body>\"}\n");
    n = write_err(b, sizeof b, "not_started", nullptr);
    CHECK(std::string(b, n) == "{\"err\":\"not_started\"}\n");
    n = write_log(b, sizeof b, "hello");
    CHECK(std::string(b, n) == "{\"log\":\"hello\"}\n");

    NodeConfig c{}; c.routing_sf = 7; c.is_gateway = false; c.leaf_id = 0;
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 5, 123456789012ull);   // > u32: proves the 64-bit digits
    CHECK(std::string(b, n) ==
      "{\"ev\":\"ready\",\"id\":3,\"key\":\"a1b2c3d4\",\"leaf_id\":0,\"lineage\":0,\"epoch\":0,\"layer\":0,\"synced\":true,\"mode\":\"existing\",\"gateway\":false,\"routing_sf\":7,\"inbox_epoch\":5,\"now_ms\":123456789012,\"duty_pct\":0,\"duty_avail_ms\":0}\n");
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 5, 99ull, "Bench \"5\"", 9);  // /mrid name, escaped
    CHECK(std::string(b, n) ==
      "{\"ev\":\"ready\",\"id\":3,\"key\":\"a1b2c3d4\",\"name\":\"Bench \\\"5\\\"\",\"leaf_id\":0,\"lineage\":0,\"epoch\":0,\"layer\":0,\"synced\":true,\"mode\":\"existing\",\"gateway\":false,\"routing_sf\":7,\"inbox_epoch\":5,\"now_ms\":99,\"duty_pct\":0,\"duty_avail_ms\":0}\n");
    // ready carries the duty snapshot (app shows it on connect): duty_pct + duty_avail_ms ride after now_ms.
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 5, 99ull, nullptr, 0, nullptr, /*duty_pct=*/42, /*duty_avail_ms=*/73000);
    CHECK(std::string(b, n).find("\"duty_pct\":42,\"duty_avail_ms\":73000}") != std::string::npos);
    // §4: ready carries the full ed_pub (so MyCardView emits the QR `p` field). pubkey rides right after key; omitted when ed_pub==nullptr.
    uint8_t ep[32]; for (int i = 0; i < 32; ++i) ep[i] = static_cast<uint8_t>(i);
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 5, 99ull, nullptr, 0, ep);
    CHECK(std::string(b, n).find("\"key\":\"a1b2c3d4\",\"pubkey\":\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"") != std::string::npos);
    meshroute::console::StatusFields sf;
    sf.uptime_ms = 123456; sf.duty_ms = 42; sf.txq = 0; sf.txdrop = 0; sf.rx = 7; sf.tx = 3;
    sf.routes = 2; sf.pending = false; sf.lbt = true; sf.batt_mv = -1;   // no battery -> omitted
    n = write_status(b, sizeof b, 3, 0xa1b2c3d4u, c, "operating", sf);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"status\",\"id\":3,\"key\":\"a1b2c3d4\",\"state\":\"operating\",\"leaf_id\":0,\"gateway\":false,\"routing_sf\":7,"
      "\"uptime_ms\":123456,\"duty_ms\":42,\"txq\":0,\"txdrop\":0,\"rx\":7,\"tx\":3,\"routes\":2,\"pending\":false,\"lbt\":true}\n");
    sf.batt_mv = 4100;                                                   // battery present -> field appears
    n = write_status(b, sizeof b, 3, 0xa1b2c3d4u, c, "operating", sf);
    CHECK(std::string(b, n).find("\"batt_mv\":4100") != std::string::npos);
}

TEST_CASE("write_duty — pct/avail_ms/enabled query reply") {
    char b[64];
    size_t n = write_duty(b, sizeof b, 42, 0, true);                     // headroom
    CHECK(std::string(b, n) == "{\"ev\":\"duty\",\"pct\":42,\"avail_ms\":0,\"enabled\":true}\n");
    n = write_duty(b, sizeof b, 100, 73000, true);                       // silent, ~73 s to availability
    CHECK(std::string(b, n) == "{\"ev\":\"duty\",\"pct\":100,\"avail_ms\":73000,\"enabled\":true}\n");
    n = write_duty(b, sizeof b, 0, 0, false);                            // disabled (no limit)
    CHECK(std::string(b, n) == "{\"ev\":\"duty\",\"pct\":0,\"avail_ms\":0,\"enabled\":false}\n");
}

TEST_CASE("write_limits — the companion `limits` query shape/values") {
    char b[256];
    meshroute::console::LimitsFields L;
    L.win_ms = 300000; L.win_left_ms = 142000; L.n = 40; L.ch_sf = 7;
    L.ch_cap = 8; L.ch_used = 2; L.ch_min_ms = 10000; L.ch_next_ms = 0; L.ch_ceiling = 42;
    L.dm_min_ms = 3000; L.dm_next_ms = 1200; L.duty_ms = 3000; L.duty_used_ms = 640;
    size_t n = write_limits(b, sizeof b, L);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"limits\",\"win_ms\":300000,\"win_left_ms\":142000,\"n\":40,\"ch_sf\":7,"
      "\"ch_cap\":8,\"ch_used\":2,\"ch_min_ms\":10000,\"ch_next_ms\":0,\"ch_ceiling\":42,"
      "\"dm_min_ms\":3000,\"dm_next_ms\":1200,\"duty_ms\":3000,\"duty_used_ms\":640}\n");
    // duty-disabled node: duty_ms == 0 -> still a well-formed line (fields never omitted)
    L.duty_ms = 0; L.duty_used_ms = 0; L.ch_cap = 20; L.ch_ceiling = 0;
    n = write_limits(b, sizeof b, L);
    CHECK(std::string(b, n).find("\"duty_ms\":0,\"duty_used_ms\":0}") != std::string::npos);
}

TEST_CASE("write_route / write_routes_end / write_cfg — Node+Network screens") {
    char b[400];
    meshroute::console::RouteRow r;
    r.dest = 2; r.next = 4; r.hops = 2; r.score = -48; r.gw = true; r.leaf = 7; r.age_ms = 5000; r.cand = 1;
    size_t n = write_route(b, sizeof b, r);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"route\",\"dest\":2,\"next\":4,\"hops\":2,\"score\":-48,\"gw\":true,\"leaf\":7,\"age_ms\":5000,\"cand\":1}\n");
    n = write_routes_end(b, sizeof b, 3);
    CHECK(std::string(b, n) == "{\"ev\":\"routes_end\",\"count\":3}\n");

    NodeConfig cc{}; cc.routing_sf = 7; cc.allowed_sf_bitmap = (1u << 7) | (1u << 12); cc.radio_bw_hz = 125000;
    cc.radio_cr = 5; cc.duty_cycle = 0.1; cc.lbt_enabled = true; cc.beacon_period_ms = 900000;
    cc.dv_hop_cap = 16; cc.leaf_id = 0; cc.is_gateway = false; cc.is_mobile = false;
    meshroute::console::CfgExtras x;
    x.node_id = 5; x.freq_hz = 869462500u; x.tx_power = 22; x.duty_x1000 = 100;   // 0.1 → 100 (no float on wire)
    x.ble_mode = "on"; x.ble_period = 15; x.ble_pin = 123456;
    x.lat_e7 = 522297000; x.lon_e7 = -41000000;   // 52.2297, -4.1 (signed → i64)
    n = write_cfg(b, sizeof b, cc, x);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"cfg\",\"node_id\":5,\"freq_hz\":869462500,\"routing_sf\":7,\"sf_list\":\"7,12\",\"bw_hz\":125000,\"cr\":5,"
      "\"tx_power\":22,\"duty_x1000\":100,\"lbt\":true,\"beacon_ms\":900000,\"hop_cap\":16,\"leaf_id\":0,"
      "\"gateway\":false,\"mobile\":false,\"mobile_autoregister\":true,\"team_id\":\"00000000\",\"ble_mode\":\"on\",\"ble_period\":15,\"ble_pin\":123456,"
      "\"lat_e7\":522297000,\"lon_e7\":-41000000}\n");
    // §S1: cfg team_id round-trips as a hex string; mobile_autoregister always present.
    cc.is_mobile = true; cc.mobile_autoregister = true; cc.team_id = 0xcccc0001u;
    n = write_cfg(b, sizeof b, cc, x);
    CHECK(std::string(b, n).find("\"mobile\":true,\"mobile_autoregister\":true,\"team_id\":\"cccc0001\"") != std::string::npos);
}

// §S1 — ready mobile/team snapshot: static/teamless node byte-identical (default mob); mobile + team add omit-gated fields.
TEST_CASE("write_ready — §S1 mobile/team fields (omit-when-inactive; static byte-identical)") {
    char b[512];
    NodeConfig c{}; c.routing_sf = 7; c.leaf_id = 0;
    // (a) default mob = a static, teamless node -> NO mobile_*/team fields at all.
    size_t n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 0, 0ull);
    std::string s(b, n);
    CHECK(s.find("mobile") == std::string::npos);
    CHECK(s.find("team") == std::string::npos);
    CHECK(s.find("hosting") == std::string::npos);
    // (b) a registered mobile in a team.
    meshroute::console::MobileReadyFields mob{};
    mob.is_mobile = true; mob.registered = true; mob.home = 222; mob.local = 17; mob.home_layer = 4;
    mob.team_id = 0xcccc0001u; mob.team_local = 9;
    n = write_ready(b, sizeof b, 17, 0xa1b2c3d4u, c, "existing", 0, 0ull, nullptr, 0, nullptr, 0, 0, mob);
    s.assign(b, n);
    CHECK(s.find("\"mobile\":true,\"mobile_registered\":true,\"mobile_home\":222,\"mobile_local\":17,\"mobile_home_layer\":4") != std::string::npos);
    CHECK(s.find("\"team\":\"cccc0001\",\"team_local\":9") != std::string::npos);
    // (c) unregistered mobile -> home/local 0, NO mobile_home_layer; a static host with hosting>0.
    meshroute::console::MobileReadyFields un{}; un.is_mobile = true; un.registered = false;
    n = write_ready(b, sizeof b, 0, 0u, c, "existing", 0, 0ull, nullptr, 0, nullptr, 0, 0, un);
    s.assign(b, n);
    CHECK(s.find("\"mobile\":true,\"mobile_registered\":false,\"mobile_home\":0,\"mobile_local\":0") != std::string::npos);
    CHECK(s.find("mobile_home_layer") == std::string::npos);
    meshroute::console::MobileReadyFields host{}; host.hosting = 2;
    n = write_ready(b, sizeof b, 5, 0u, c, "existing", 0, 0ull, nullptr, 0, nullptr, 0, 0, host);
    s.assign(b, n);
    CHECK(s.find("\"hosting\":2") != std::string::npos);
    CHECK(s.find("\"mobile\":") == std::string::npos);   // hosting is independent of is_mobile
}

// §S2 — mobile_reg / team_reg pushes; §S4 — channel_recv team_id; §S6 — peer_key_cached name.
TEST_CASE("write_push — §S2 mobile_reg/team_reg, §S4 channel_recv team_id, §S6 peer name") {
    char b[256];
    // mobile_reg registered
    Push r{}; r.kind = PushKind::mobile_reg; r.origin = 222; r.dst = 17; r.layer_id = 4; r.ctr = 6; r.relayed = true;
    size_t n = write_push(b, sizeof b, r);
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_reg\",\"home\":222,\"local\":17,\"home_layer\":4,\"epoch\":6,\"registered\":true}\n");
    // mobile_reg home-loss
    Push d{}; d.kind = PushKind::mobile_reg; d.relayed = false;
    n = write_push(b, sizeof b, d);
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_reg\",\"home\":0,\"local\":0,\"registered\":false}\n");
    // team_reg
    Push t{}; t.kind = PushKind::team_reg; t.team_id = 0xcccc0001u; t.dst = 9;
    n = write_push(b, sizeof b, t);
    CHECK(std::string(b, n) == "{\"ev\":\"team_reg\",\"team\":\"cccc0001\",\"local\":9}\n");
    // channel_recv WITH team_id (hex, omit-when-0 proven by the existing channel_recv test)
    Push ch{}; ch.kind = PushKind::channel_recv; ch.origin = 4; ch.layer_id = 4; ch.channel_id = 0;
    ch.channel_msg_id = 12345; ch.seq = 7; ch.team_id = 0xcccc0001u;
    const char* body = "hi"; ch.body_len = 2; ch.body[0] = 'h'; ch.body[1] = 'i'; (void)body;
    n = write_push(b, sizeof b, ch);
    CHECK(std::string(b, n).find("\"team_id\":\"cccc0001\",\"body\":\"hi\"") != std::string::npos);
    // peer_key_cached with a cached name (body carries the name)
    Push pk{}; pk.kind = PushKind::peer_key_cached; pk.sender_hash = 3735928559u;
    const char* nm = "Alice"; pk.body_len = 5; for (int i = 0; i < 5; ++i) pk.body[i] = (uint8_t)nm[i];
    n = write_push(b, sizeof b, pk);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_key_cached\",\"hash\":3735928559,\"pinned\":false,\"name\":\"Alice\"}\n");
    // peer_key_cached with NO name -> omitted (byte-identical to the pre-S6 shape)
    Push pk0{}; pk0.kind = PushKind::peer_key_cached; pk0.sender_hash = 3735928559u;
    n = write_push(b, sizeof b, pk0);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_key_cached\",\"hash\":3735928559,\"pinned\":false}\n");
    // join_adopted — a DAD/join adopt landed (dst=id, layer_id=leaf, ctr=epoch)
    Push ja{}; ja.kind = PushKind::join_adopted; ja.dst = 17; ja.layer_id = 4; ja.ctr = 3;
    n = write_push(b, sizeof b, ja);
    CHECK(std::string(b, n) == "{\"ev\":\"join_adopted\",\"id\":17,\"layer\":4,\"epoch\":3}\n");
}

TEST_CASE("write_join_started — join vs create verb-ack shape (integer freq/bw, create-only fields)") {
    char b[256];
    // plain join: no create/lineage/leaf_name
    JoinStartedFields jn{}; jn.layer = 4; jn.leaf = 4; jn.freq_khz = 869500; jn.sf = 9; jn.bw_hz = 125000;
    size_t n = write_join_started(b, sizeof b, jn);
    CHECK(std::string(b, n) == "{\"ev\":\"join_started\",\"layer\":4,\"leaf\":4,\"freq_khz\":869500,\"sf\":9,\"bw_hz\":125000}\n");
    // create: "create":true + lineage + leaf_name inserted additively
    JoinStartedFields cr{}; cr.create = true; cr.layer = 4; cr.leaf = 4; cr.lineage = 41153;
    const char* nm = "north field"; cr.leaf_name = nm; cr.leaf_name_len = 11;
    cr.freq_khz = 869500; cr.sf = 9; cr.bw_hz = 125000;
    n = write_join_started(b, sizeof b, cr);
    CHECK(std::string(b, n) == "{\"ev\":\"join_started\",\"create\":true,\"layer\":4,\"leaf\":4,\"lineage\":41153,"
                               "\"leaf_name\":\"north field\",\"freq_khz\":869500,\"sf\":9,\"bw_hz\":125000}\n");
}

// §S3 — mobile_status / mobile_gw stream / mobile_err; §S6 — peer_name; §S5 — inbox_channel team_id.
TEST_CASE("write_mobile_* / write_peer_name / inbox_channel team_id — §S3/S5/S6") {
    char b[256];
    meshroute::console::MobileStatusFields m{};
    m.registered = true; m.home = 222; m.local = 17; m.epoch = 6; m.home_layer = 4;
    m.autoregister = true; m.layer = 4; m.freq_khz = 869525; m.sf = 9; m.bw_hz = 125000; m.nets = 2;
    size_t n = write_mobile_status(b, sizeof b, m);
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_status\",\"mobile\":true,\"registered\":true,\"home\":222,\"local\":17,"
                               "\"epoch\":6,\"home_layer\":4,\"autoregister\":true,\"layer\":4,\"freq_khz\":869525,"
                               "\"sf\":9,\"bw_hz\":125000,\"nets\":2}\n");
    meshroute::console::MobileStatusFields un{}; un.autoregister = false; un.layer = 0; un.freq_khz = 868000; un.sf = 7; un.bw_hz = 125000;
    n = write_mobile_status(b, sizeof b, un);
    CHECK(std::string(b, n).find("\"registered\":false,\"home\":0,\"local\":0,\"epoch\":0,\"autoregister\":false") != std::string::npos);
    CHECK(std::string(b, n).find("home_layer") == std::string::npos);
    n = write_mobile_err(b, sizeof b, "not_mobile");
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_err\",\"reason\":\"not_mobile\"}\n");
    n = write_mobile_gw(b, sizeof b, 3, 4);
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_gw\",\"gw\":3,\"leaf\":4}\n");
    const char* net = "north field";
    n = write_mobile_net(b, sizeof b, 7, net, 11, 869525, 9, 125000);
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_net\",\"layer\":7,\"name\":\"north field\",\"freq_khz\":869525,\"sf\":9,\"bw_hz\":125000}\n");
    n = write_mobile_gw_end(b, sizeof b, 1, 2);
    CHECK(std::string(b, n) == "{\"ev\":\"mobile_gw_end\",\"gws\":1,\"nets\":2}\n");
    // §S6 peer_name
    n = write_peer_name(b, sizeof b, 3735928559u, "Alice", 5);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name\",\"hash\":3735928559,\"name\":\"Alice\"}\n");
    n = write_peer_name(b, sizeof b, 3735928559u, nullptr, 0);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name\",\"hash\":3735928559}\n");
    // §S5 inbox_channel team_id omit-when-0 vs present
    n = write_inbox_channel(b, sizeof b, 5, 4, 4, 0, 12345, 99ull, "hi", 2);
    CHECK(std::string(b, n).find("team_id") == std::string::npos);
    n = write_inbox_channel(b, sizeof b, 5, 4, 4, 0, 12345, 99ull, "hi", 2, 0xcccc0001u);
    CHECK(std::string(b, n).find("\"team_id\":\"cccc0001\",\"body\":\"hi\"") != std::string::npos);
}

// R6.3 leaf-config membership — the iOS companion contract additions (INBOX_SYNC_CONTRACT.md): send_failed{joining},
// the config_adopted push (membership from the config), and the managed-node ready snapshot.
TEST_CASE("write_push/write_ready — R6.3 leaf-config membership (iOS contract)") {
    char b[320];
    // (1) send_failed reason `joining` (transient — the participation gate, lifts on adopt)
    Push f{}; f.kind = PushKind::send_failed; f.dst = 2; f.ctr = 7; f.reason = SendFailReason::joining;
    size_t n = write_push(b, sizeof b, f);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":2,\"ctr\":7,\"reason\":\"joining\"}\n");
    // (2) config_adopted -> membership fields read from the live config
    NodeConfig c{}; c.routing_sf = 8; c.leaf_id = 2; c.lineage_id = 41153; c.config_epoch = 3;
    c.leaf_name_len = 3; c.leaf_name[0] = 'h'; c.leaf_name[1] = 'u'; c.leaf_name[2] = 'b';
    Push ca{}; ca.kind = PushKind::config_adopted;
    n = write_push(b, sizeof b, ca, &c);
    CHECK(std::string(b, n) == "{\"ev\":\"config_adopted\",\"lineage\":41153,\"epoch\":3,\"leaf\":\"hub\",\"layer\":2}\n");
    // (3) managed ready carries lineage/epoch/leaf/level/synced
    n = write_ready(b, sizeof b, 17, 0xa1b2c3d4u, c, "existing", 0, 0ull);
    CHECK(std::string(b, n).find("\"lineage\":41153,\"epoch\":3,\"leaf\":\"hub\",\"layer\":2,\"synced\":true") != std::string::npos);
}

// R6.3 §7c: join_refused push -> reason-coded JSON (wire_version carries their/my version; leaf_full is bare).
TEST_CASE("write_push — R6.3 §7c join_refused (wire_version + leaf_full)") {
    char b[160];
    Push w{}; w.kind = PushKind::join_refused; w.join_reason = JoinRefuseReason::wire_version; w.origin = 2; w.dst = 1;
    size_t n = write_push(b, sizeof b, w);
    CHECK(std::string(b, n) == "{\"ev\":\"join_refused\",\"reason\":\"wire_version\",\"their_ver\":2,\"my_ver\":1}\n");
    Push f{}; f.kind = PushKind::join_refused; f.join_reason = JoinRefuseReason::leaf_full;
    n = write_push(b, sizeof b, f);
    CHECK(std::string(b, n) == "{\"ev\":\"join_refused\",\"reason\":\"leaf_full\"}\n");
}

// ── Companion-contract gap fixes: Gap 2 (reqpubkey_sent) + Gap 3 (e2e_acked) ──
TEST_CASE("write_reqpubkey_sent — §2 the on-air pubkey-request event (replaces the generic ack)") {
    char b[64];
    size_t n = write_reqpubkey_sent(b, sizeof b, 3735928559u);   // 0xDEADBEEF
    CHECK(std::string(b, n) == "{\"ev\":\"reqpubkey_sent\",\"hash\":3735928559}\n");
}
TEST_CASE("write_push — send_e2e_acked → live e2e_acked twin (origin/ctr/sender_hash; never ev:unknown)") {
    char b[128];
    Push p{}; p.kind = PushKind::send_e2e_acked; p.dst = 2; p.ctr = 7; p.sender_hash = 3735928559u;  // push stores the confirming node in .dst (node_mac_rx.cpp:610)
    size_t n = write_push(b, sizeof b, p);
    CHECK(std::string(b, n) == "{\"ev\":\"e2e_acked\",\"origin\":2,\"ctr\":7,\"sender_hash\":3735928559}\n");
}

// ── Slice 6 — anti-spam v2 send-outcome feedback events (send_blocked / send_failed / channel_sent) ──
TEST_CASE("write_push — send_blocked carries kind/reason/next_ms (Slice 6a)") {
    char b[160];
    Push c{}; c.kind = PushKind::send_blocked; c.blocked_channel = true;
    c.reason = SendFailReason::min_interval; c.next_ms = 7300;
    size_t n = write_push(b, sizeof b, c);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"send_blocked\",\"kind\":\"channel\",\"reason\":\"min_interval\",\"next_ms\":7300}\n");
    Push d{}; d.kind = PushKind::send_blocked; d.blocked_channel = false;   // DM
    d.reason = SendFailReason::cap; d.next_ms = 0;
    n = write_push(b, sizeof b, d);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"send_blocked\",\"kind\":\"dm\",\"reason\":\"cap\",\"next_ms\":0}\n");
}

TEST_CASE("write_push — send_failed carries no_cts / no_ack DM giveup reasons (Slice 6b)") {
    char b[128];
    Push c{}; c.kind = PushKind::send_failed; c.dst = 2; c.ctr = 7; c.reason = SendFailReason::no_cts;
    size_t n = write_push(b, sizeof b, c);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":2,\"ctr\":7,\"reason\":\"no_cts\"}\n");
    Push a{}; a.kind = PushKind::send_failed; a.dst = 4; a.ctr = 9; a.reason = SendFailReason::no_ack;
    n = write_push(b, sizeof b, a);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":4,\"ctr\":9,\"reason\":\"no_ack\"}\n");
}

TEST_CASE("write_push — channel_sent carries relayed bool + no_relay reason (Slice 6c)") {
    char b[128];
    Push t{}; t.kind = PushKind::channel_sent; t.relayed = true; t.ctr = 5;
    size_t n = write_push(b, sizeof b, t);
    CHECK(std::string(b, n) == "{\"ev\":\"channel_sent\",\"ctr\":5,\"relayed\":true}\n");
    Push f{}; f.kind = PushKind::channel_sent; f.relayed = false; f.ctr = 6;
    n = write_push(b, sizeof b, f);
    CHECK(std::string(b, n) == "{\"ev\":\"channel_sent\",\"ctr\":6,\"relayed\":false,\"reason\":\"no_relay\"}\n");
}
