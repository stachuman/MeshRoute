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
      "{\"ev\":\"ready\",\"id\":3,\"key\":\"a1b2c3d4\",\"leaf_id\":0,\"lineage\":0,\"epoch\":0,\"level\":0,\"synced\":true,\"mode\":\"existing\",\"gateway\":false,\"routing_sf\":7,\"inbox_epoch\":5,\"now_ms\":123456789012,\"duty_pct\":0,\"duty_avail_ms\":0}\n");
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 5, 99ull, "Bench \"5\"", 9);  // /mrid name, escaped
    CHECK(std::string(b, n) ==
      "{\"ev\":\"ready\",\"id\":3,\"key\":\"a1b2c3d4\",\"name\":\"Bench \\\"5\\\"\",\"leaf_id\":0,\"lineage\":0,\"epoch\":0,\"level\":0,\"synced\":true,\"mode\":\"existing\",\"gateway\":false,\"routing_sf\":7,\"inbox_epoch\":5,\"now_ms\":99,\"duty_pct\":0,\"duty_avail_ms\":0}\n");
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
    r.dest = 2; r.next = 4; r.hops = 2; r.score = -48; r.gw = true; r.layer = 7; r.age_ms = 5000; r.cand = 1;
    size_t n = write_route(b, sizeof b, r);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"route\",\"dest\":2,\"next\":4,\"hops\":2,\"score\":-48,\"gw\":true,\"layer\":7,\"age_ms\":5000,\"cand\":1}\n");
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
      "\"gateway\":false,\"mobile\":false,\"ble_mode\":\"on\",\"ble_period\":15,\"ble_pin\":123456,"
      "\"lat_e7\":522297000,\"lon_e7\":-41000000}\n");
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
    CHECK(std::string(b, n) == "{\"ev\":\"config_adopted\",\"lineage\":41153,\"epoch\":3,\"leaf\":\"hub\",\"level\":2}\n");
    // (3) managed ready carries lineage/epoch/leaf/level/synced
    n = write_ready(b, sizeof b, 17, 0xa1b2c3d4u, c, "existing", 0, 0ull);
    CHECK(std::string(b, n).find("\"lineage\":41153,\"epoch\":3,\"leaf\":\"hub\",\"level\":2,\"synced\":true") != std::string::npos);
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
