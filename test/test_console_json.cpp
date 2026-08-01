// MeshRoute — test_console_json.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_json.h"
#include "firmware_config_parse.h"   // §team-ch-key T-K1b: mrfw::parse_hex32 — the IMPORT half, so the export->import round trip is pinned against the real parser, not a re-implementation
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
    char b[512];   // §chan-crypt CL2a: the cfg golden grew by `"team_channel_crypt":true,` and 400 now OVERFLOWS (write_cfg returns 0). The DEVICE buffer is fw_main.cpp's 1700-B s_inbox_jb, so this was a test-fixture bound only.
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
      "\"tx_power\":22,\"duty_x1000\":100,\"lbt\":true,\"beacon_ms\":900000,\"hop_cap\":16,\"team_hop_cap\":8,\"leaf_id\":0,"   // §team-parity T3: team_hop_cap defaults to protocol::team_hop_cap = 8, distinct from dv_hop_cap's 16 -> the golden pins WHICH field each key reads
      "\"gateway\":false,\"mobile\":false,\"mobile_autoregister\":true,\"team_id\":\"00000000\",\"team_channel_crypt\":true,\"team_ch_key\":false,"   // §team-ch-key T-K1b: the CONTENT-key lock state, ALWAYS present (cfg is the explicit dump) — default extras = no key. §chan-crypt CL2a: team_channel_crypt sits beside it and is likewise ALWAYS present; NodeConfig defaults it to true (T-K2 §2.5 default-ON)
      "\"ble_mode\":\"on\",\"ble_period\":15,\"ble_pin\":123456,"
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

// §team-ch-key (T-K1b) — `team exportkey`: the export event, its refusals, and the export->import round trip.
TEST_CASE("write_team_key_export / write_team_key_err — §team-ch-key T-K1b") {
    char b[256];
    // A pub/priv pair with EVERY nibble value present and the two halves distinguishable, so a swapped or
    // truncated field cannot pass: pub = 0x00,0x11,…,0xFF repeating; priv = its bitwise complement.
    uint8_t pub[32], priv[32];
    for (int i = 0; i < 32; ++i) { pub[i] = static_cast<uint8_t>((i % 16) * 0x11); priv[i] = static_cast<uint8_t>(~pub[i]); }
    size_t n = write_team_key_export(b, sizeof b, 858993459u, pub, priv);   // the contract's own example team_id (= 0x33333333)
    std::string s(b, n);
    // team_id is DECIMAL (contract), NOT the "33333333" hex-string form ready/cfg use for the same value.
    CHECK(s == "{\"ev\":\"team_key_export\",\"team_id\":858993459,"
               "\"tkpub\":\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\","
               "\"tkpriv\":\"ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100\"}\n");
    CHECK(s.find("\"33333333\"") == std::string::npos);   // guards against a key_hex32 regression on team_id
    // ★ THE ROUND TRIP IS TEXTUALLY EXACT: what we print, mrfw::parse_hex32 (the `tkpub=`/`tkpriv=` import half)
    // parses back to the identical bytes. This is what makes the T-K4 QR export->adopt path byte-exact.
    const size_t p0 = s.find("\"tkpub\":\"") + 9, p1 = s.find("\"tkpriv\":\"") + 10;
    const std::string hpub = s.substr(p0, 64), hpriv = s.substr(p1, 64);
    CHECK(hpub.size() == 64);
    CHECK(hpriv.size() == 64);
    CHECK(hpub.find_first_not_of("0123456789abcdef") == std::string::npos);   // LOWER-case, no 0x — parse_hex32's accepted form
    CHECK(hpriv.find_first_not_of("0123456789abcdef") == std::string::npos);
    uint8_t rpub[32] = {}, rpriv[32] = {};
    CHECK(mrfw::parse_hex32(hpub.c_str(),  rpub));
    CHECK(mrfw::parse_hex32(hpriv.c_str(), rpriv));
    CHECK(std::memcmp(rpub,  pub,  32) == 0);
    CHECK(std::memcmp(rpriv, priv, 32) == 0);
    // The refusals — a DISTINCT ev, never a team_key_export with null fields (see console_json.h's note).
    n = write_team_key_err(b, sizeof b, "no_key");
    CHECK(std::string(b, n) == "{\"ev\":\"team_key_err\",\"reason\":\"no_key\"}\n");
    n = write_team_key_err(b, sizeof b, "no_team");
    CHECK(std::string(b, n) == "{\"ev\":\"team_key_err\",\"reason\":\"no_team\"}\n");
    // ⚠ No JSON `null` anywhere on this surface — the whole reason the keyless answer is a refusal.
    CHECK(std::string(b, n).find("null") == std::string::npos);
}

// ================= §team-ch-key (T-K3) — the `team grantkey` answers + the granted-key push =====================
TEST_CASE("write_team_key_grant / write_team_key_err — §team-ch-key T-K3 (the console's grant answers)") {
    char b[256];
    // ACCEPTED, airborne: ctr != 0 -> parked:false.
    size_t n = write_team_key_grant(b, sizeof b, 0xdeadbeefu, /*ctr=*/1234);
    CHECK(std::string(b, n) == "{\"ev\":\"team_key_grant\",\"hash\":3735928559,\"ctr\":1234,\"parked\":false}\n");
    // ACCEPTED but PARKED behind a hash resolve: ctr == 0, and `parked` says so EXPLICITLY rather than leaving the
    // app to read intent out of a sentinel 0.
    n = write_team_key_grant(b, sizeof b, 0x1u, /*ctr=*/0);
    CHECK(std::string(b, n) == "{\"ev\":\"team_key_grant\",\"hash\":1,\"ctr\":0,\"parked\":true}\n");
    // ★ The success event carries NO key material and no name echo — the grant's confidentiality is the feature.
    CHECK(std::string(b, n).find("tkpriv") == std::string::npos);
    CHECK(std::string(b, n).find("tkpub")  == std::string::npos);
    // Every T-K3 refusal reuses T-K1b's ONE error event (U1) with a DISTINCT reason the app matches on.
    for (const char* r : { "no_team", "no_key", "no_identity", "no_pubkey", "self", "delegated", "too_large", "bad_target" }) {
        n = write_team_key_err(b, sizeof b, r);
        CHECK(std::string(b, n) == std::string("{\"ev\":\"team_key_err\",\"reason\":\"") + r + "\"}\n");
        CHECK(std::string(b, n).find("null") == std::string::npos);
    }
}

TEST_CASE("write_push team_key_received — §team-ch-key T-K3 (the granted-key notification the app renders)") {
    char b[256];
    Push p{}; p.kind = PushKind::team_key_received;
    p.team_id = 0xcccc0001u; p.sender_hash = 0xa1b2c3d4u; p.origin = 213;
    const char* nm = "Alpha Team";
    p.body_len = 10; std::memcpy(p.body, nm, 10);
    size_t n = write_push(b, sizeof b, p);
    std::string s(b, n);
    // `team` is the hex8 spelling team_reg already uses; `hash` matches peer_key_cached; `name` is omit-when-absent.
    CHECK(s == "{\"ev\":\"team_key_received\",\"team\":\"cccc0001\",\"hash\":2712847316,\"origin\":213,\"name\":\"Alpha Team\"}\n");
    // No name given -> the key is OMITTED entirely (this surface emits no JSON nulls).
    Push q{}; q.kind = PushKind::team_key_received; q.team_id = 0x11u; q.sender_hash = 7; q.origin = 1;
    n = write_push(b, sizeof b, q);
    CHECK(std::string(b, n) == "{\"ev\":\"team_key_received\",\"team\":\"00000011\",\"hash\":7,\"origin\":1}\n");
    CHECK(std::string(b, n).find("null") == std::string::npos);
    // ★★ THE DISCLOSURE FENCE, the same one `ready` carries: this push is UNSOLICITED, so it must never carry the
    // pair it is announcing. `team exportkey` stays the ONE disclosure verb.
    CHECK(std::string(b, n).find("tkpriv") == std::string::npos);
    CHECK(std::string(b, n).find("tkpub")  == std::string::npos);
}

// ★★ §team-ch-key (T-K1b) THE HARD CONSTRAINT: `ready` carries the LOCK-STATE BOOLEAN and NOTHING ELSE. ready is
// unsolicited and fires on every connect; the private key is disclosed ONLY by the explicit `team exportkey` verb.
TEST_CASE("write_ready — §team-ch-key T-K1b: team_ch_key boolean, and NEVER the key itself") {
    char b[768];
    NodeConfig c{}; c.routing_sf = 7;
    meshroute::console::MobileReadyFields mob{};
    mob.is_mobile = true; mob.registered = true; mob.home = 222; mob.local = 17; mob.home_layer = 4;
    mob.team_id = 0xcccc0001u; mob.team_local = 9; mob.team_ch_key = true;
    // A distinctive ed_pub so we can also prove the pubkey field still works after the hex32_digits extraction (U1).
    uint8_t ep[32]; for (int i = 0; i < 32; ++i) ep[i] = static_cast<uint8_t>(0xA0 + i);
    size_t n = write_ready(b, sizeof b, 17, 0xa1b2c3d4u, c, "existing", 0, 0ull, nullptr, 0, ep, 0, 0, mob);
    std::string s(b, n);
    CHECK(s.find("\"team\":\"cccc0001\",\"team_local\":9,\"team_ch_key\":true") != std::string::npos);
    // ★ the disclosure fence: no key field names, and no 64-hex private blob.
    CHECK(s.find("tkpriv") == std::string::npos);
    CHECK(s.find("tkpub")  == std::string::npos);
    CHECK(s.find("team_key_export") == std::string::npos);
    // hex32_digits still emits the PUBLIC pubkey exactly as before the U1 extraction.
    CHECK(s.find("\"pubkey\":\"a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf\"") != std::string::npos);
    // key present == false is EXPLICIT (never inferred from absence) for a node that IS in a team.
    mob.team_ch_key = false;
    n = write_ready(b, sizeof b, 17, 0xa1b2c3d4u, c, "existing", 0, 0ull, nullptr, 0, nullptr, 0, 0, mob);
    s.assign(b, n);
    CHECK(s.find("\"team_ch_key\":false") != std::string::npos);
    // ...and OMITTED entirely for a teamless node, so a static node's ready stays byte-identical (§S1's rule).
    meshroute::console::MobileReadyFields teamless{}; teamless.is_mobile = true; teamless.team_ch_key = true;   // flag set but no team -> still omitted
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 0, 0ull, nullptr, 0, nullptr, 0, 0, teamless);
    s.assign(b, n);
    CHECK(s.find("team_ch_key") == std::string::npos);
    CHECK(s.find("\"team\"") == std::string::npos);
}

// §team-ch-key (T-K1b) — cfg carries the lock state ALWAYS (explicit dump), like team_id.
TEST_CASE("write_cfg — §team-ch-key T-K1b: team_ch_key always present") {
    char b[768];
    NodeConfig c{}; c.routing_sf = 7; c.team_id = 0xcccc0001u;
    meshroute::console::CfgExtras x; x.node_id = 3;
    size_t n = write_cfg(b, sizeof b, c, x);                     // default extras -> no key
    CHECK(std::string(b, n).find("\"team_id\":\"cccc0001\",\"team_channel_crypt\":true,\"team_ch_key\":false") != std::string::npos);   // §chan-crypt CL2a: a DEFAULT-constructed NodeConfig has team_channel_crypt = true; key absent -> nothing is sealed anyway
    x.team_ch_key = true;
    n = write_cfg(b, sizeof b, c, x);
    CHECK(std::string(b, n).find("\"team_id\":\"cccc0001\",\"team_channel_crypt\":true,\"team_ch_key\":true") != std::string::npos);
    // §chan-crypt CL2a: the OPT-OUT round-trips — key held but the node will not seal by default (`cfg set team_channel_crypt 0`).
    { NodeConfig cx{}; cx.routing_sf = 7; cx.team_id = 0xcccc0001u; cx.team_channel_crypt = false;
      const size_t nn = write_cfg(b, sizeof b, cx, x);
      CHECK(std::string(b, nn).find("\"team_channel_crypt\":false,\"team_ch_key\":true") != std::string::npos); }
    // present even with NO team (team_id "00000000") — cfg is the explicit dump, unlike ready's omit.
    NodeConfig c0{}; c0.routing_sf = 7;
    n = write_cfg(b, sizeof b, c0, x);
    CHECK(std::string(b, n).find("\"team_id\":\"00000000\",\"team_channel_crypt\":true,\"team_ch_key\":true") != std::string::npos);
    // the pair itself is NEVER on the cfg surface either.
    CHECK(std::string(b, n).find("tkpriv") == std::string::npos);
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
    // peer_key_cached with a cached name (body carries the name). §AB2: `conf` now precedes `pinned`.
    Push pk{}; pk.kind = PushKind::peer_key_cached; pk.sender_hash = 3735928559u;
    const char* nm = "Alice"; pk.body_len = 5; for (int i = 0; i < 5; ++i) pk.body[i] = (uint8_t)nm[i];
    n = write_push(b, sizeof b, pk);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_key_cached\",\"hash\":3735928559,\"conf\":\"overheard\",\"pinned\":false,\"name\":\"Alice\"}\n");
    // peer_key_cached with NO name -> omitted (byte-identical to the pre-S6 shape)
    Push pk0{}; pk0.kind = PushKind::peer_key_cached; pk0.sender_hash = 3735928559u;
    n = write_push(b, sizeof b, pk0);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_key_cached\",\"hash\":3735928559,\"conf\":\"overheard\",\"pinned\":false}\n");
    // join_adopted — a DAD/join adopt landed (dst=id, layer_id=leaf, ctr=epoch)
    Push ja{}; ja.kind = PushKind::join_adopted; ja.dst = 17; ja.layer_id = 4; ja.ctr = 3;
    n = write_push(b, sizeof b, ja);
    CHECK(std::string(b, n) == "{\"ev\":\"join_adopted\",\"id\":17,\"layer\":4,\"epoch\":3}\n");
}

// ★★ §AB2 (address-book spec 2026-07-29 §0.1/§2.2) — THE JSON GOLDEN FOR `conf`, and the whole point of the field.
// Before AB2 `pinned` was a HARDCODED literal `false`, so all three rows below rendered IDENTICALLY: the app could not
// tell `overheard` (key present, e2e_seal_inner REFUSES) from `authoritative` (can seal) from `pinned`. It therefore
// offered "send encrypted", the user tried, and got FAILED (no recipient pubkey).
// ⚠ THE CORPUS CANNOT SEE ANY OF THIS. The sim's push bridge emits only ctr/dst/kind (measured on this tree: 14
// peer_key_cached push lines across s22/s27/s30, none carrying a field from write_push), so this golden is the ENTIRE
// detector for the encoder — byte-identity is structurally blind to it, it is not evidence of correctness.
TEST_CASE("§AB2 peer_key_cached — `conf` renders the real level and `pinned` is its DERIVED duplicate") {
    char b[256];
    auto emit = [&b](Node::PeerKeyConf c) {
        Push p{}; p.kind = PushKind::peer_key_cached; p.sender_hash = 0x6C297145u;
        p.peer_conf = static_cast<uint8_t>(c);
        return std::string(b, write_push(b, sizeof b, p));
    };
    // 1. overheard — cached on-air/on-pass. The app must NOT offer encryption: seal requires >= authoritative.
    CHECK(emit(Node::PeerKeyConf::overheard) ==
          "{\"ev\":\"peer_key_cached\",\"hash\":1814655301,\"conf\":\"overheard\",\"pinned\":false}\n");
    // 2. authoritative — the owner's own answer. THIS is the row that used to be indistinguishable from (1).
    CHECK(emit(Node::PeerKeyConf::authoritative) ==
          "{\"ev\":\"peer_key_cached\",\"hash\":1814655301,\"conf\":\"authoritative\",\"pinned\":false}\n");
    // 3. pinned — and note `pinned` is now TRUE here. The old literal claimed `false` for a QR-verified key.
    CHECK(emit(Node::PeerKeyConf::pinned) ==
          "{\"ev\":\"peer_key_cached\",\"hash\":1814655301,\"conf\":\"pinned\",\"pinned\":true}\n");
    // 4. `pinned` is EXACTLY `conf == "pinned"` (spec §2.2 keeps it as a derived duplicate, so the app's existing
    //    boolean reader cannot start disagreeing with the new level).
    for (unsigned v = 0; v < 3; ++v) {
        const std::string s = emit(static_cast<Node::PeerKeyConf>(v));
        const bool says_pinned_true  = s.find("\"pinned\":true")  != std::string::npos;
        const bool conf_says_pinned  = s.find("\"conf\":\"pinned\"") != std::string::npos;
        CHECK(says_pinned_true == conf_says_pinned);
    }
    // 5. An OUT-OF-RANGE byte (a future 4th level, or memory corruption) reads as the LEAST capable level — never
    //    over-claim a sealing capability. Same discipline as sendfailreason_name's fallback.
    Push bad{}; bad.kind = PushKind::peer_key_cached; bad.sender_hash = 1u; bad.peer_conf = 200;
    const size_t nb = write_push(b, sizeof b, bad);
    CHECK(std::string(b, nb) == "{\"ev\":\"peer_key_cached\",\"hash\":1,\"conf\":\"overheard\",\"pinned\":false}\n");
    // 6. Push carries `conf` for FREE: byte 11 was alignment padding between `relayed` (10) and `ctr` (12), so
    //    sizeof(Push) — and therefore Node::_push_ring[] and sizeof(Node) — must not have moved.
    CHECK(sizeof(Push) == 292);
    CHECK(offsetof(Push, peer_conf) == 11);
    CHECK(offsetof(Push, ctr) == 12);
}

// ★ §AB2: the `peername` SYNCHRONOUS ack + its three refusal reasons (spec §2.3, mechanism ruled §2.6(b) — an ack, not
// a push, so no PushKind was touched). Also console-only: no scenario runs a console verb.
TEST_CASE("§AB2 write_peer_name_set / write_peer_name_err — the peername ack shapes") {
    char b[256];
    size_t n = write_peer_name_set(b, sizeof b, 0x6C297145u, "Ola", 3);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name_set\",\"hash\":1814655301,\"name\":\"Ola\"}\n");
    // The echoed name is ESCAPED like every other body on this surface (a UI may send quotes/newlines).
    n = write_peer_name_set(b, sizeof b, 1u, "a\"b\nc", 5);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name_set\",\"hash\":1,\"name\":\"a\\\"b\\nc\"}\n");
    // A full-width 32-byte name (the cap) still fits and is echoed verbatim.
    const std::string wide(32, 'X');
    n = write_peer_name_set(b, sizeof b, 2u, wide.c_str(), wide.size());
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name_set\",\"hash\":2,\"name\":\"" + wide + "\"}\n");
    // The three reasons have three different remedies, which is why they are not one `bad_args` (C2).
    n = write_peer_name_err(b, sizeof b, "unknown_hash");
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name_err\",\"reason\":\"unknown_hash\"}\n");
    n = write_peer_name_err(b, sizeof b, "too_long");
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name_err\",\"reason\":\"too_long\"}\n");
    n = write_peer_name_err(b, sizeof b, "bad_args");
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name_err\",\"reason\":\"bad_args\"}\n");
    // Overflow is LATCHED, not truncated: a cap too small yields 0 bytes rather than a half line.
    char tiny[8];
    CHECK(write_peer_name_set(tiny, sizeof tiny, 0x6C297145u, "Ola", 3) == 0);
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

// ── ★ THE ENUM→STRING COVERAGE GUARD (2026-07-25) ────────────────────────────────────────────────────────
// console_json.cpp hand-maintains one switch per contract-visible enum, and a missing `case` is SILENT: the
// switch falls through to the trailing `return "none"` / `return "unknown"`, so the companion receives a value
// its documented mapping (ios-companion/INBOX_SYNC_CONTRACT.md) cannot handle. That has now happened three
// times (`mobile_no_home`, `e2e_ack_timeout`, `err_ack_ring_full`) — two hand-maintained lists that must agree
// is the root cause. This block makes the whole class fail LOUDLY, at both build and test time:
//
//   (1) COMPILE TIME — each `ord()` overload below is a switch with NO `default:` and `-Wswitch` promoted to a
//       hard ERROR, so appending an enumerator breaks THIS FILE's build until the value is listed here. That is
//       what keeps "walks EVERY enumerator" honest: a runtime loop cannot see an enumerator nobody wrote down.
//   (2) RUN TIME — the loop then asserts the REAL mapper returns neither the fallback nor an empty string for
//       every listed enumerator, and that the enumerators occupy exactly 0..N-1 (the app may PERSIST the
//       number, so a renumbering or a hole is a contract break, not a style choice).
//
// So both ways to get it wrong now fail:  enum grown + mapper case forgotten -> (2) fails;
//                                        enum grown + this list forgotten    -> (1) fails to build.
// The trailing fallbacks in console_json.cpp are deliberately KEPT and re-asserted below — they are the right
// guard for an OUT-OF-RANGE cast (a corrupt wire byte). The bug was never the fallback; it was the silent
// fall-through of a LIVE enumerator into it.
static constexpr unsigned kUnlisted = 0xFFFFu;   // sentinel: not an enumerator this test knows about

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"          // ★ a new enumerator must BREAK THIS BUILD, not merely warn
static unsigned ord(CmdCode c) {
    switch (c) {
        case CmdCode::queued:            case CmdCode::err_unknown_dst:  case CmdCode::err_too_large:
        case CmdCode::err_no_gateway:    case CmdCode::err_priority_capped: case CmdCode::err_no_binding:
        case CmdCode::err_unsupported:   case CmdCode::err_unprovisioned: case CmdCode::err_no_data_sf:
        case CmdCode::err_ack_ring_full:
            return static_cast<unsigned>(c);
    }
    return kUnlisted;
}
static unsigned ord(PushKind k) {
    switch (k) {
        case PushKind::msg_recv:       case PushKind::channel_recv:   case PushKind::send_acked:
        case PushKind::send_failed:    case PushKind::send_e2e_acked: case PushKind::hash_resolved:
        case PushKind::peer_key_cached:case PushKind::config_adopted: case PushKind::join_refused:
        case PushKind::send_blocked:   case PushKind::channel_sent:   case PushKind::mobile_reg:
        case PushKind::team_reg:       case PushKind::join_adopted:   case PushKind::team_key_received:
        case PushKind::team_channel_no_key:   // §chan-crypt CL2a
            return static_cast<unsigned>(k);
    }
    return kUnlisted;
}
static unsigned ord(SendFailReason r) {
    switch (r) {
        case SendFailReason::none:           case SendFailReason::no_pubkey:      case SendFailReason::no_identity:
        case SendFailReason::too_large:      case SendFailReason::bad_rng:        case SendFailReason::no_route:
        case SendFailReason::joining:        case SendFailReason::cap:            case SendFailReason::min_interval:
        case SendFailReason::no_cts:         case SendFailReason::no_ack:         case SendFailReason::mobile_no_home:
        case SendFailReason::gateway_unreachable: case SendFailReason::e2e_ack_timeout:
        case SendFailReason::queue_full:     case SendFailReason::reprovisioned:  case SendFailReason::unsealable:
        case SendFailReason::no_location:    // §loc-per-send: `-l` asked for a position and the node has no fix
            return static_cast<unsigned>(r);
    }
    return kUnlisted;
}
static unsigned ord(JoinRefuseReason r) {
    switch (r) {
        case JoinRefuseReason::wire_version: case JoinRefuseReason::leaf_full:
        case JoinRefuseReason::phy_mismatch: case JoinRefuseReason::sf_list_mismatch:
            return static_cast<unsigned>(r);
    }
    return kUnlisted;
}
// §AB2: the 4th mapped enum at the app boundary — peer_key_cached's `conf`. A 4th confidence level added without a
// mapper case must break THIS build, exactly as for the three above.
static unsigned ord(Node::PeerKeyConf c) {
    switch (c) {
        case Node::PeerKeyConf::overheard: case Node::PeerKeyConf::authoritative: case Node::PeerKeyConf::pinned:
            return static_cast<unsigned>(c);
    }
    return kUnlisted;
}
// §AB4: the 5th mapped enum — the retained position's TRUST ANCHOR. A third anchor added without a mapper case must
// break THIS build, because an unmapped anchor renders as the fallback and would silently mis-attribute a position.
static unsigned ord(Node::PeerLocSrc s) {
    switch (s) {
        case Node::PeerLocSrc::peer: case Node::PeerLocSrc::team:
            return static_cast<unsigned>(s);
    }
    return kUnlisted;
}
#pragma GCC diagnostic pop

// Walk every enumerator of E and assert its mapper never yields the SILENT fallback (nor an empty string).
// exempt_ord = the one enumerator for which the fallback string IS the correct answer (SendFailReason::none
// legitimately renders "none" = "this push carries no reason"); -1 = no exemption.
template <class E>
static void check_mapper_covers_every_enumerator(const char* enum_name, const char* (*name)(E),
                                                 const char* fallback, unsigned expect_count,
                                                 int exempt_ord = -1) {
    unsigned listed = 0, first_gap = 256u;
    for (unsigned v = 0; v < 256u; ++v) {
        const E e = static_cast<E>(v);                       // fixed uint8_t underlying type -> 0..255 well-defined
        if (ord(e) == kUnlisted) { if (first_gap == 256u) first_gap = v; continue; }
        ++listed;
        const char* s = name(e);
        CHECK(s != nullptr);                                 // NB (-fno-exceptions): doctest REQUIRE is unavailable
        if (!s) continue;                                    //     in this build, so guard by hand (see test_node_query.cpp:335)
        // NB doctest renders a bare `const char*` as a POINTER — wrap the variables in std::string so a failure
        // names the enum and the fallback in words (the string LITERALS below print correctly as-is).
        CHECK_MESSAGE(s[0] != '\0', std::string(enum_name), " enumerator ", v, " maps to an EMPTY string");
        if (static_cast<int>(v) != exempt_ord)
            CHECK_MESSAGE(std::strcmp(s, fallback) != 0,
                          std::string(enum_name), " enumerator ", v, " falls through to the SILENT fallback \"",
                          std::string(fallback), "\" — add its case to the mapper in lib/console/console_json.cpp");
    }
    CHECK(listed == expect_count);       // this test's list agrees with the enum's cardinality
    CHECK(first_gap == expect_count);    // ...and the enumerators are contiguous 0..N-1 (no renumbering/holes)
}

TEST_CASE("★ enum->string mappers cover EVERY enumerator — no silent fallback at the app boundary") {
    check_mapper_covers_every_enumerator<CmdCode>("CmdCode", cmdcode_name, "err_unknown", 10);
    check_mapper_covers_every_enumerator<PushKind>("PushKind", pushkind_name, "unknown", 16);   // 14 -> 15: §team-ch-key T-K3 `team_key_received`; 15 -> 16: §chan-crypt CL2a `team_channel_no_key`
    check_mapper_covers_every_enumerator<SendFailReason>("SendFailReason", sendfailreason_name, "none", 18,
                                                         /*exempt_ord=*/0);   // SendFailReason::none == "none"  (15 -> 16: §clean-join-carriers `reprovisioned`; 16 -> 17: §team-ch-key T-K3 `unsealable`; 17 -> 18: §loc-per-send `no_location`)
    check_mapper_covers_every_enumerator<JoinRefuseReason>("JoinRefuseReason", joinrefusereason_name, "none", 4);
    // §AB2: peer_key_cached's `conf`. exempt_ord = 0 because `overheard` IS the fallback string — deliberately, since an
    // out-of-range byte must read as the least capable level rather than claim a sealing capability.
    check_mapper_covers_every_enumerator<Node::PeerKeyConf>("PeerKeyConf", peerkeyconf_name, "overheard", 3,
                                                            /*exempt_ord=*/0);
    // §AB4: the retained position's trust anchor. exempt_ord = 1 because `team` IS the fallback string — deliberately
    // the mirror of PeerKeyConf's policy in this field's own honest direction: an out-of-range byte must read as the
    // WEAKER (group) anchor, never claim the stronger pairwise attribution.
    check_mapper_covers_every_enumerator<Node::PeerLocSrc>("PeerLocSrc", peerlocsrc_name, "team", 2,
                                                            /*exempt_ord=*/1);
    // The one exemption is EXACT, not a licence for a hole: `none` must render precisely "none".
    CHECK(std::strcmp(sendfailreason_name(SendFailReason::none), "none") == 0);
    // The fallbacks STAY: an out-of-range cast (a corrupt byte, never a live enumerator) must still land there.
    CHECK(std::strcmp(cmdcode_name(static_cast<CmdCode>(200)), "err_unknown") == 0);
    CHECK(std::strcmp(pushkind_name(static_cast<PushKind>(200)), "unknown") == 0);
    CHECK(std::strcmp(sendfailreason_name(static_cast<SendFailReason>(200)), "none") == 0);
    CHECK(std::strcmp(joinrefusereason_name(static_cast<JoinRefuseReason>(200)), "none") == 0);
    CHECK(std::strcmp(peerkeyconf_name(static_cast<Node::PeerKeyConf>(200)), "overheard") == 0);
    // §AB2: the three level strings, pinned verbatim — the app gates "send encrypted" by comparing against them.
    CHECK(std::strcmp(peerkeyconf_name(Node::PeerKeyConf::overheard), "overheard") == 0);
    CHECK(std::strcmp(peerkeyconf_name(Node::PeerKeyConf::authoritative), "authoritative") == 0);
    CHECK(std::strcmp(peerkeyconf_name(Node::PeerKeyConf::pinned), "pinned") == 0);
    // §AB4: the two anchor strings, pinned verbatim — the app decides how strongly to render a position by matching
    // them, so a rename here silently promotes a group-anchored claim to a pairwise one on somebody's map.
    CHECK(std::strcmp(peerlocsrc_name(Node::PeerLocSrc::peer), "peer") == 0);
    CHECK(std::strcmp(peerlocsrc_name(Node::PeerLocSrc::team), "team") == 0);
    CHECK(std::strcmp(peerlocsrc_name(static_cast<Node::PeerLocSrc>(200)), "team") == 0);   // out of range -> the WEAKER anchor
    // The three strings this slice restored/added — pinned verbatim, because the app matches on them.
    CHECK(std::strcmp(sendfailreason_name(SendFailReason::e2e_ack_timeout), "e2e_ack_timeout") == 0);  // command.h documented it all along
    CHECK(std::strcmp(sendfailreason_name(SendFailReason::queue_full), "queue_full") == 0);            // NEW (defer queue full)
    CHECK(std::strcmp(sendfailreason_name(SendFailReason::reprovisioned), "reprovisioned") == 0);      // §clean-join-carriers: a join/create/leave discarded a staged/in-flight DM
    CHECK(std::strcmp(sendfailreason_name(SendFailReason::unsealable), "unsealable") == 0);            // §team-ch-key T-K3: a sealed-only TYPE on a transport that cannot carry it sealed-AND-typed
    CHECK(std::strcmp(pushkind_name(PushKind::team_key_received), "team_key_received") == 0);          // §team-ch-key T-K3: the granted-key notification
    CHECK(std::strcmp(cmdcode_name(CmdCode::err_ack_ring_full), "err_ack_ring_full") == 0);            // enumerator-name convention
    // ★★ §err-reason/B32 — THE TEXT CONSOLE'S SELF-LABELLING INVARIANT, and the only part of that slice native can see.
    // src/fw_main.cpp now prints this mapper's token BARE on the USB console (`> err_no_binding ctr=0 depth=0`), with no
    // `err ` word in front of it, precisely because every refusal's string already begins with `err_`. That print site is
    // compiled by NEITHER native NOR the simulator, so this loop is the only place the convention can be enforced: an
    // enumerator later mapped to a non-`err_` string (say `parked`) would silently make a REFUSAL read as a success line
    // on the bench. `queued` is the single exemption — it is the success code and owns its own word. The `ord()` switch
    // above already breaks the build when an enumerator is added, so this cannot go stale unnoticed (U1: same walker).
    for (unsigned v = 0; v < 10; ++v) {
        const CmdCode c = static_cast<CmdCode>(v);
        if (ord(c) == kUnlisted) continue;
        const char* s = cmdcode_name(c);
        if (c == CmdCode::queued) { CHECK(std::strcmp(s, "queued") == 0); continue; }
        CHECK_MESSAGE(std::strncmp(s, "err_", 4) == 0,
                      "CmdCode enumerator ", v, " maps to \"", std::string(s),
                      "\" which does not begin with \"err_\" — src/fw_main.cpp prints this token BARE, so this refusal"
                      " would read as a SUCCESS line on the text console");
    }
    CHECK(std::strncmp(cmdcode_name(static_cast<CmdCode>(200)), "err_", 4) == 0);   // the out-of-range fallback self-labels too
}

// The same three defects at the LINE the app actually reads — a rendered NDJSON push/ack, not just the mapper.
TEST_CASE("write_push / write_ack — the restored reason strings render (all three used to read \"none\"/\"err_unknown\")") {
    char b[128];
    Push t{}; t.kind = PushKind::send_failed; t.dst = 2; t.ctr = 7; t.reason = SendFailReason::e2e_ack_timeout;
    size_t n = write_push(b, sizeof b, t);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":2,\"ctr\":7,\"reason\":\"e2e_ack_timeout\"}\n");
    Push q{}; q.kind = PushKind::send_failed; q.dst = 4; q.ctr = 9; q.reason = SendFailReason::queue_full;
    n = write_push(b, sizeof b, q);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":4,\"ctr\":9,\"reason\":\"queue_full\"}\n");
    // §clean-join-carriers: the reprovision drop, rendered at the line the companion reads.
    Push rp{}; rp.kind = PushKind::send_failed; rp.dst = 55; rp.ctr = 12; rp.reason = SendFailReason::reprovisioned;
    n = write_push(b, sizeof b, rp);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":55,\"ctr\":12,\"reason\":\"reprovisioned\"}\n");
    n = write_ack(b, sizeof b, CmdResult{CmdCode::err_ack_ring_full, 0, 3});
    CHECK(std::string(b, n) == "{\"ack\":\"err_ack_ring_full\",\"ctr\":0,\"qd\":3,\"dh\":0,\"lp\":0}\n");
    // A reason-LESS send_failed still omits the key entirely (the pre-slice node_cascade.cpp:259 shape) — kept
    // so the legacy/non-e2e giveup wire is unchanged, and so the queue_full push above is provably additive.
    Push bare{}; bare.kind = PushKind::send_failed; bare.dst = 5; bare.ctr = 1;   // reason defaults to none
    n = write_push(b, sizeof b, bare);
    CHECK(std::string(b, n) == "{\"ev\":\"send_failed\",\"dst\":5,\"ctr\":1}\n");
}

// ★★ §AB3: the address-book stream (spec 2026-07-29 §2.1/§2.6(a)) and `nameof`'s two new id fields. Console-only —
// no scenario runs a console verb, and the sim's push bridge renders only ctr/dst/kind, so THIS TEST is the coverage.
TEST_CASE("§AB3 write_peer_row / write_peers_end / write_peers_err — the address-book stream") {
    char b[400];
    // 1. The full row: every optional field present. `conf` is the LEVEL (§2.2), not a boolean.
    Node::PeerBookRow r{};
    r.hash = 0x6C297145u; std::memcpy(r.name, "Ola", 3); r.name_len = 3;
    r.static_id = 34; r.team_id = 228; r.conf = Node::PeerKeyConf::authoritative;
    r.has_key = true; r.peer_confirmed = true; r.static_authoritative = true;
    size_t n = write_peer_row(b, sizeof b, r);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"peer\",\"hash\":1814655301,\"conf\":\"authoritative\",\"confirmed\":true,"
      "\"name\":\"Ola\",\"static_id\":34,\"team_id\":228}\n");
    // 2. The lean row: no name, no ids, unconfirmed — every optional field OMITTED (absence is normal, not an error).
    Node::PeerBookRow lean{};
    lean.hash = 7u; lean.conf = Node::PeerKeyConf::overheard; lean.has_key = true;
    n = write_peer_row(b, sizeof b, lean);
    CHECK(std::string(b, n) == "{\"ev\":\"peer\",\"hash\":7,\"conf\":\"overheard\",\"confirmed\":false}\n");
    // 3. ★ The AMBIGUITY REPORT: a dropped stale team-id alias is NAMED on the line (spec §2.1 forbids silence).
    Node::PeerBookRow amb{};
    amb.hash = 9u; amb.team_id = 231; amb.team_alias_dropped = 1;
    amb.conf = Node::PeerKeyConf::pinned; amb.has_key = true;
    n = write_peer_row(b, sizeof b, amb);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"peer\",\"hash\":9,\"conf\":\"pinned\",\"confirmed\":false,\"team_id\":231,\"team_alias\":1}\n");
    // 4. ★ An AGED key: has_key=false -> "aged":true AND conf already reads "overheard", so a consumer that ignores
    //    `aged` still cannot offer an encryption that would fail (§0.1, the whole reason `conf` is a level).
    Node::PeerBookRow aged{};
    aged.hash = 11u; std::memcpy(aged.name, "Bo", 2); aged.name_len = 2; aged.static_id = 5;
    n = write_peer_row(b, sizeof b, aged);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"peer\",\"hash\":11,\"conf\":\"overheard\",\"confirmed\":false,\"name\":\"Bo\",\"static_id\":5,\"aged\":true}\n");
    // 5. The name is ESCAPED + UTF-8-sanitised like every other body on this surface.
    Node::PeerBookRow esc{};
    esc.hash = 12u; std::memcpy(esc.name, "a\"b\nc", 5); esc.name_len = 5; esc.has_key = true;
    n = write_peer_row(b, sizeof b, esc);
    CHECK(std::string(b, n).find("\"name\":\"a\\\"b\\nc\"") != std::string::npos);
    // 6. The stream terminator + the C2 refusal (the reason names the remedy: plain `peers` IS available over BLE).
    n = write_peers_end(b, sizeof b, 3);
    CHECK(std::string(b, n) == "{\"ev\":\"peers_end\",\"count\":3}\n");
    n = write_peers_err(b, sizeof b, "console_only");
    CHECK(std::string(b, n) == "{\"ev\":\"peers_err\",\"reason\":\"console_only\"}\n");
    // 7. Overflow LATCHES rather than truncating — a half JSON line must never reach the app.
    char tiny[8];
    CHECK(write_peer_row(tiny, sizeof tiny, r) == 0);
    CHECK(write_peers_end(tiny, sizeof tiny, 3) == 0);
    // 8. ★ NOTHING in this slice touched Push: the view is generated, so no push field and no PushKind was needed
    //    (§2.6(b) ruled the ack model synchronous). Push's alignment pad was already spent by AB2 — pinned here too.
    CHECK(sizeof(Push) == 292);
    // 9. ★★ §AB4 — cases 1-8 above are ALSO the "absence renders cleanly" proof: not one of them sets has_location and
    //    not one of their goldens gained a byte, so the four location fields are provably OMITTED by default. That is
    //    the ordinary case (most peers never send a position) and it must never read as an error.
}

// ★★★ §AB4 — the RETAINED POSITION on the address-book row (spec 2026-07-29 §2.7/§2.7.2). Console-only and, on top of
// that, CORPUS-DARK AT THE SOURCE: not one of the 36 scenarios airs a location at all (measured — zero `peer_location`
// events corpus-wide), so nothing but native can see any of this.
TEST_CASE("§AB4 write_peer_row — the retained position rides with its AGE and its TRUST ANCHOR, or not at all") {
    char b[400];
    // 1. A PAIRWISE (DM-sourced) position: all four fields, and `loc_src":"peer"` — the strong anchor.
    Node::PeerBookRow r{};
    r.hash = 0x6C297145u; r.conf = Node::PeerKeyConf::authoritative; r.has_key = true;
    r.has_location = true; r.lat_e7 = 523000000; r.lon_e7 = 134050000; r.loc_age_s = 42;
    r.loc_src = Node::PeerLocSrc::peer;
    size_t n = write_peer_row(b, sizeof b, r);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"peer\",\"hash\":1814655301,\"conf\":\"authoritative\",\"confirmed\":false,"
      "\"lat\":523000000,\"lon\":134050000,\"loc_age_s\":42,\"loc_src\":\"peer\"}\n");
    // 2. ★ NEGATIVE coordinates survive as SIGNED (a southern/western fix). j.i64, not j.u32 — a u32 render would put a
    //    peer in the Pacific instead of Patagonia, and the field is int32_t precisely so it cannot.
    Node::PeerBookRow south{};
    south.hash = 5u; south.has_key = true; south.conf = Node::PeerKeyConf::overheard;
    south.has_location = true; south.lat_e7 = -338680000; south.lon_e7 = -1514400000; south.loc_age_s = 0;
    south.loc_src = Node::PeerLocSrc::peer;
    n = write_peer_row(b, sizeof b, south);
    CHECK(std::string(b, n).find("\"lat\":-338680000,\"lon\":-1514400000,\"loc_age_s\":0") != std::string::npos);
    // 3. ★★ THE GROUP-ANCHORED ROW — `loc_src":"team"`. UNREACHABLE FROM THE NETWORK TODAY (CL2 is spec-only), and the
    //    golden exists exactly so that when CL2 lands it adds a SOURCE and not a schema change: the app's renderer is
    //    already obliged to distinguish "some holder of the team key said so" from "this specific peer said so".
    Node::PeerBookRow grp{};
    grp.hash = 9u; grp.team_id = 228;
    grp.has_location = true; grp.lat_e7 = 1; grp.lon_e7 = 2; grp.loc_age_s = 7200;
    grp.loc_src = Node::PeerLocSrc::team;
    n = write_peer_row(b, sizeof b, grp);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"peer\",\"hash\":9,\"conf\":\"overheard\",\"confirmed\":false,\"team_id\":228,"
      "\"lat\":1,\"lon\":2,\"loc_age_s\":7200,\"loc_src\":\"team\",\"aged\":true}\n");
    // 4. ★ ALL FOUR RIDE TOGETHER OR NONE DO: a row whose coordinates are set but has_location is false emits NOTHING
    //    positional. This is what stops a zeroed/garbage row rendering as a fix at (0,0) — and `loc_age_s` can never be
    //    separated from a position it belongs to, which is the whole stale-fix guarantee.
    Node::PeerBookRow stale{};
    stale.hash = 3u; stale.has_key = true; stale.conf = Node::PeerKeyConf::pinned;
    stale.lat_e7 = 999; stale.lon_e7 = 888; stale.loc_age_s = 5; stale.loc_src = Node::PeerLocSrc::peer;
    stale.has_location = false;
    n = write_peer_row(b, sizeof b, stale);
    CHECK(std::string(b, n) == "{\"ev\":\"peer\",\"hash\":3,\"conf\":\"pinned\",\"confirmed\":false}\n");
    // 5. A maximal row still LATCHES rather than truncating — a half line must never reach the app.
    CHECK(write_peer_row(b, 40, r) == 0);
}

// ★ §AB3: `nameof` now answers from the view, so its line can name the namespace(s) an id-less hash belongs to.
// ADDITIVE — the first two fields and their order are byte-identical to the pre-AB3 shape.
TEST_CASE("§AB3 write_peer_name — the two id fields are additive and omit-when-0") {
    char b[256];
    // the pre-AB3 shapes, unchanged (this is the compatibility assertion, not a new feature)
    size_t n = write_peer_name(b, sizeof b, 0x6C297145u, "Ola", 3);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name\",\"hash\":1814655301,\"name\":\"Ola\"}\n");
    n = write_peer_name(b, sizeof b, 5u, nullptr, 0);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name\",\"hash\":5}\n");
    // §2.5's actual question — "which namespace is 0x6C297145?" — now answered on the line itself
    n = write_peer_name(b, sizeof b, 0x6C297145u, "Ola", 3, /*static_id=*/0, /*team_id=*/228);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name\",\"hash\":1814655301,\"name\":\"Ola\",\"team_id\":228}\n");
    n = write_peer_name(b, sizeof b, 9u, nullptr, 0, /*static_id=*/34, /*team_id=*/228);
    CHECK(std::string(b, n) == "{\"ev\":\"peer_name\",\"hash\":9,\"static_id\":34,\"team_id\":228}\n");
}
