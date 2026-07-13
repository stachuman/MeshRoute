// MeshRoute — test_console_binary.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
#include "doctest.h"
#include "console_binary.h"
#include <cstring>

using namespace meshroute::console::bin;

TEST_CASE("bin TLV primitives — put/reader/get round-trip, LE, overflow") {
    uint8_t b[64];
    size_t off = frame_begin(b, sizeof b, MSG_DUTY);
    CHECK(off == 2);
    CHECK(b[0] == 1);          // ver
    CHECK(b[1] == MSG_DUTY);
    CHECK(put_u8 (b, sizeof b, off, 0x01, 0x42));
    CHECK(put_u16(b, sizeof b, off, 0x02, 0x1234));
    CHECK(put_u32(b, sizeof b, off, 0x03, 0xDEADBEEF));
    CHECK(put_i32(b, sizeof b, off, 0x04, -5));
    // walk it back
    TlvReader r;
    CHECK(reader_init(r, b, off));
    CHECK(r.ver == 1);
    CHECK(r.msg_type == MSG_DUTY);
    uint8_t tag, n; const uint8_t* v;
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x01); CHECK(get_u8(v, n)  == 0x42);
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x02); CHECK(get_u16(v, n) == 0x1234);
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x03); CHECK(get_u32(v, n) == 0xDEADBEEF);
    CHECK(reader_next(r, tag, v, n)); CHECK(tag == 0x04); CHECK(get_i32(v, n) == -5);
    CHECK_FALSE(reader_next(r, tag, v, n));   // end
}

TEST_CASE("bin TLV — overflow returns false, no OOB") {
    uint8_t b[4];                     // room for header + nothing
    size_t off = frame_begin(b, sizeof b, MSG_DUTY);   // off=2
    CHECK(put_u32(b, sizeof b, off, 0x01, 1) == false); // 2+2+4 > 4
    CHECK(off == 2);                  // unchanged on failure
}

TEST_CASE("bin TLV — reader rejects a truncated buffer") {
    uint8_t b[8] = {1, MSG_DUTY, 0x01, 4, 0, 0};  // claims a 4-byte value but only 2 remain
    TlvReader r; CHECK(reader_init(r, b, 6));
    uint8_t tag, n; const uint8_t* v;
    CHECK_FALSE(reader_next(r, tag, v, n));        // len 4 overruns -> false, no read
}

TEST_CASE("bin duty — round-trip") {
    uint8_t b[241];
    size_t n = enc_duty(b, sizeof b, 42, 1500, true);
    CHECK(n > 2); CHECK(n <= 241);
    DutyOut o{};
    CHECK(dec_duty(b, n, o));
    CHECK(o.pct == 42); CHECK(o.avail_ms == 1500u); CHECK(o.enabled == true);
}
TEST_CASE("bin duty — overflow returns 0") {
    uint8_t b[3];                       // header fits, no field
    CHECK(enc_duty(b, sizeof b, 42, 1500, true) == 0);
}
TEST_CASE("bin duty — decoder skips an unknown tag (forward-compat)") {
    uint8_t b[241]; size_t n = enc_duty(b, sizeof b, 7, 0, false);
    size_t off = n; CHECK(put_u16(b, sizeof b, off, 0x7F /*future tag*/, 0xBEEF));  // append unknown
    DutyOut o{}; CHECK(dec_duty(b, off, o));
    CHECK(o.pct == 7); CHECK(o.enabled == false);   // known fields still decoded, unknown skipped
}

TEST_CASE("bin limits — round-trip") {
    meshroute::console::LimitsFields L; L.win_ms = 300000; L.win_left_ms = 42000; L.n = 7;
    L.ch_cap = 5; L.ch_used = 2; L.dm_next_ms = 1200; L.duty_used_ms = 88000;
    uint8_t b[241]; size_t n = enc_limits(b, sizeof b, L);
    CHECK(n > 2); CHECK(n <= 241);
    LimitsOut o{}; CHECK(dec_limits(b, n, o));
    CHECK(o.win_ms == 300000u); CHECK(o.n == 7u); CHECK(o.ch_cap == 5u);
    CHECK(o.dm_next_ms == 1200u); CHECK(o.duty_used_ms == 88000u);
}

TEST_CASE("bin status — round-trip + batt omitted when <0") {
    meshroute::console::StatusFields s; s.uptime_ms = 1234000; s.rx = 88; s.tx = 12; s.txq = 0;
    s.routes = 7; s.pending = true; s.lbt = true; s.batt_mv = -1; s.duty_ms = 340;
    StatusDiag d; d.rxbad = 2; d.stackhw = 352; d.reset_cause = 3; d.nf_dbm = -110;
    uint8_t b[241]; size_t n = enc_status(b, sizeof b, 9, 0xABCD1234, s, d);
    CHECK(n > 2); CHECK(n <= 241);
    StatusOut o{}; CHECK(dec_status(b, n, o));
    CHECK(o.uptime_s == 1234u);    // ms -> s
    CHECK(o.rx == 88u); CHECK(o.routes == 7u); CHECK(o.pending == 1u);
    CHECK(o.rxbad == 2u); CHECK(o.stackhw == 352u); CHECK(o.reset_cause == 3u);
    CHECK(o.nf_dbm == -110); CHECK(o.id == 9u); CHECK(o.key == 0xABCD1234u);
    CHECK(o.batt_mv == 0);         // omitted -> stays default (the caller reads "absent")
}

TEST_CASE("bin cfg — round-trip, fits 241 for a single-layer node") {
    meshroute::NodeConfig c{}; c.routing_sf = 8; c.allowed_sf_bitmap = (1<<7)|(1<<9);
    c.leaf_id = 1; c.is_gateway = false; c.is_mobile = false; c.team_id = 0xABCD; c.config_epoch = 4;
    c.radio_bw_hz = 125000; c.radio_cr = 5;
    meshroute::console::CfgExtras x; x.node_id = 17; x.freq_hz = 869525000; x.tx_power = 14; x.lat_e7 = 522297000;
    uint8_t b[241]; size_t n = enc_cfg(b, sizeof b, c, x);
    CHECK(n > 2); CHECK(n <= 241);
    CfgOut o{}; CHECK(dec_cfg(b, n, o));
    CHECK(o.node_id == 17u); CHECK(o.freq_hz == 869525000u); CHECK(o.routing_sf == 8u);
    CHECK(o.sf_list == ((1<<7)|(1<<9))); CHECK(o.team_id == 0xABCDu); CHECK(o.lat_e7 == 522297000);
    CHECK(o.tx_power == 14); CHECK(o.bw == 125000u);
}

TEST_CASE("bin routes — pack N, round-trip, truncated flag") {
    meshroute::console::RouteRow rows[3];
    rows[0] = {10, 11, 1, 40, false, 1, 500, 1};
    rows[1] = {12, 11, 2, 30, true, 1, 1500, 2};
    rows[2] = {13, 12, 3, 20, false, 2, 2500, 1};
    uint8_t b[241]; uint8_t trunc = 9;
    size_t n = enc_routes(b, sizeof b, rows, 3, &trunc);
    CHECK(n > 2); CHECK(trunc == 0);
    RouteOut o{}; CHECK(dec_routes(b, n, o));
    CHECK(o.n == 3); CHECK(o.truncated == 0);
    CHECK(o.rows[1].dest == 12); CHECK(o.rows[1].gw == true); CHECK(o.rows[1].age_ms == 1500u); CHECK(o.rows[2].hops == 3);
}
TEST_CASE("bin routes — truncates when over cap") {
    meshroute::console::RouteRow rows[40];
    for (uint8_t i = 0; i < 40; ++i) rows[i] = {uint8_t(20+i), 11, 1, 10, false, 1, 100, 1};
    uint8_t small[64]; uint8_t trunc = 0;
    size_t n = enc_routes(small, sizeof small, rows, 40, &trunc);
    CHECK(n <= 64); CHECK(trunc > 0);          // some omitted
    RouteOut o{}; CHECK(dec_routes(small, n, o));
    CHECK(o.n < 40); CHECK(o.truncated == trunc);
}

TEST_CASE("bin faults — round-trip + truncated") {
    FaultRow rows[2] = { {3, 0x20001234, 0x0000ABCD, 1}, {7, 0xDEAD0000, 0x0000BEEF, 5} };
    uint8_t b[241]; uint8_t trunc = 9;
    size_t n = enc_faults(b, sizeof b, rows, 2, &trunc);
    CHECK(n > 2); CHECK(trunc == 0);
    FaultOut o{}; CHECK(dec_faults(b, n, o));
    CHECK(o.n == 2); CHECK(o.rows[0].cause == 3); CHECK(o.rows[0].pc == 0x20001234u);
    CHECK(o.rows[1].lr == 0x0000BEEFu); CHECK(o.rows[1].count == 5);
}

TEST_CASE("bin gateway — 2-layer config + schedule round-trip") {
    GatewayFields g{}; g.n_layers = 2; g.window_period_ms = 8000;
    g.leaf[0] = {1, 1, 8, uint16_t((1<<7)|(1<<9)), 125000, 5, 2000, 0};
    g.leaf[1] = {2, 1, 9, uint16_t(1<<10), 250000, 6, 2000, 4000};
    uint8_t b[241]; size_t n = enc_gateway(b, sizeof b, g);
    CHECK(n > 2); CHECK(n <= 241);
    GatewayOut o{}; CHECK(dec_gateway(b, n, o));
    CHECK(o.g.n_layers == 2); CHECK(o.g.window_period_ms == 8000u);
    CHECK(o.g.leaf[0].sf_list == ((1<<7)|(1<<9))); CHECK(o.g.leaf[0].bw == 125000u);
    CHECK(o.g.leaf[1].routing_sf == 9); CHECK(o.g.leaf[1].window_offset_ms == 4000u);
}

TEST_CASE("bin — a wrong-msg-type buffer is rejected by every decoder") {
    uint8_t b[241]; size_t n = enc_duty(b, sizeof b, 1, 0, false);
    LimitsOut lo{}; CHECK_FALSE(dec_limits(b, n, lo));   // duty frame, limits decoder -> false
    StatusOut so{}; CHECK_FALSE(dec_status(b, n, so));
    CfgOut co{};    CHECK_FALSE(dec_cfg(b, n, co));
    RouteOut ro{};  CHECK_FALSE(dec_routes(b, n, ro));
}
TEST_CASE("bin — a zero-length / 1-byte buffer never crashes a decoder") {
    DutyOut o{}; uint8_t one = MSG_DUTY;
    CHECK_FALSE(dec_duty(nullptr, 0, o));
    CHECK_FALSE(dec_duty(&one, 1, o));
}
TEST_CASE("bin — status decoder tolerates a truncated value at the tail") {
    uint8_t b[241]; size_t n = enc_status(b, sizeof b, 1, 2, meshroute::console::StatusFields{}, StatusDiag{});
    // chop the last byte off -> the final TLV overruns -> reader_next stops early, no OOB (ASAN)
    StatusOut o{}; CHECK(dec_status(b, n - 1, o));   // returns true, partial fields, no crash
}
