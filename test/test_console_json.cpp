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
    size_t n = write_ack(b, sizeof b, CmdResult{CmdCode::queued, 7, 1});
    CHECK(std::string(b, n) == "{\"ack\":\"queued\",\"ctr\":7,\"qd\":1}\n");
    n = write_ack(b, sizeof b, CmdResult{CmdCode::err_unknown_dst, 0, 0});
    CHECK(std::string(b, n) == "{\"ack\":\"err_unknown_dst\",\"ctr\":0,\"qd\":0}\n");
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

TEST_CASE("write_push — msg_recv carries sender_hash + escaped body; channel_recv carries channel_msg_id") {
    char b[300];
    Push m{}; m.kind = PushKind::msg_recv; m.origin = 3; m.ctr = 7; m.sender_hash = 3735928559u;  // 0xDEADBEEF
    const char* body = "hi\"x"; m.body_len = 4; std::memcpy(m.body, body, 4);
    size_t n = write_push(b, sizeof b, m);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"msg_recv\",\"origin\":3,\"ctr\":7,\"sender_hash\":3735928559,\"body\":\"hi\\\"x\"}\n");

    Push ch{}; ch.kind = PushKind::channel_recv; ch.origin = 4; ch.channel_id = 3; ch.channel_msg_id = 68298753u;
    const char* cb = "yo"; ch.body_len = 2; std::memcpy(ch.body, cb, 2);
    n = write_push(b, sizeof b, ch);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"channel_recv\",\"origin\":4,\"channel_id\":3,\"channel_msg_id\":68298753,\"body\":\"yo\"}\n");

    Push a{}; a.kind = PushKind::send_acked; a.dst = 5; a.ctr = 7;
    n = write_push(b, sizeof b, a);
    CHECK(std::string(b, n) == "{\"ev\":\"send_acked\",\"dst\":5,\"ctr\":7}\n");
}

TEST_CASE("write_inbox_* — pull stream records + terminator + mark_read ack") {
    char b[400];
    size_t n = write_inbox_dm(b, sizeof b, 42, 2, 7, 3735928559u, 123456ull, "hi", 2);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_dm\",\"seq\":42,\"origin\":2,\"ctr\":7,\"sender_hash\":3735928559,\"rx_ms\":123456,\"body\":\"hi\"}\n");

    n = write_inbox_channel(b, sizeof b, 7, 4, 3, 68298753u, 123456ull, "yo", 2);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"inbox_channel\",\"seq\":7,\"origin\":4,\"channel_id\":3,\"channel_msg_id\":68298753,\"rx_ms\":123456,\"body\":\"yo\"}\n");

    n = write_inbox_end(b, sizeof b, 42, 7, 3, 15);
    CHECK(std::string(b, n) == "{\"ev\":\"inbox_end\",\"dm_seq\":42,\"chan_seq\":7,\"epoch\":3,\"count\":15}\n");

    n = write_inbox_marked(b, sizeof b, "dm", 42);
    CHECK(std::string(b, n) == "{\"ack\":\"mark_read\",\"kind\":\"dm\",\"seq\":42}\n");
}

TEST_CASE("write_err / write_log / write_ready / write_status") {
    char b[200];
    size_t n = write_err(b, sizeof b, "parse", "expected: send <dst> <body>");
    CHECK(std::string(b, n) == "{\"err\":\"parse\",\"msg\":\"expected: send <dst> <body>\"}\n");
    n = write_err(b, sizeof b, "not_started", nullptr);
    CHECK(std::string(b, n) == "{\"err\":\"not_started\"}\n");
    n = write_log(b, sizeof b, "hello");
    CHECK(std::string(b, n) == "{\"log\":\"hello\"}\n");

    NodeConfig c{}; c.routing_sf = 7; c.is_gateway = false; c.leaf_id = 0;
    n = write_ready(b, sizeof b, 3, 0xa1b2c3d4u, c, "existing", 5);
    CHECK(std::string(b, n) ==
      "{\"ev\":\"ready\",\"id\":3,\"key\":\"a1b2c3d4\",\"leaf_id\":0,\"mode\":\"existing\",\"gateway\":false,\"routing_sf\":7,\"inbox_epoch\":5}\n");
    n = write_status(b, sizeof b, 3, 0xa1b2c3d4u, c, "operating");
    CHECK(std::string(b, n) ==
      "{\"ev\":\"status\",\"id\":3,\"key\":\"a1b2c3d4\",\"state\":\"operating\",\"leaf_id\":0,\"gateway\":false,\"routing_sf\":7}\n");
}
