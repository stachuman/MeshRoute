// MeshRoute — test_sched_send.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the firmware scheduled-send CORE (src/sched_send.h): the entry ring (add/seq/full), the
// due/late/overdue arithmetic driven by a FAKE clock, the tag-body build (byte-identical to the harness
// make_tag + the @<sendms> stamp), and the offset-list parse. The fw_main glue (millis, the Command build,
// g_node TX, Serial) is device-only (bench). NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"
#include "../src/sched_send.h"

#include <string>
#include <cstring>

using namespace mrsched;

TEST_CASE("sched_send: add assigns sequential seq + rejects when the ring is full") {
    Schedule s;
    CHECK(s.add(1000, 5, 0) == 0);
    CHECK(s.add(2000, 5, kAck) == 1);
    CHECK(s.add(3000, 5, 0) == 2);
    CHECK(s.armed() == 3);
    Schedule f;
    for (int i = 0; i < kSchedMax; ++i) CHECK(f.add(100u + (uint32_t)i, 5, 0) >= 0);
    CHECK(f.armed() == kSchedMax);
    CHECK(f.add(99999, 5, 0) == -1);                 // full -> reject
    CHECK(f.armed() == kSchedMax);
}

TEST_CASE("sched_send: next_due returns the EARLIEST due entry, in order, on a fake clock") {
    Schedule s;
    s.add(3000, 5, 0);   // seq 0
    s.add(1000, 5, 0);   // seq 1 (earlier at_ms)
    s.add(2000, 5, 0);   // seq 2
    CHECK(s.next_due(500)  == -1);                   // nothing due yet
    CHECK(s.next_due(1000) == 1);                    // the 1000 ms entry (index 1) is earliest-due
    s.mark_fired(1, 1000);
    CHECK(s.next_due(1500) == -1);                   // 2000/3000 not due
    CHECK(s.next_due(2500) == 2);                    // the 2000 ms entry next
    s.mark_fired(2, 2500);
    CHECK(s.next_due(9000) == 0);                    // the 3000 ms entry last
    s.mark_fired(0, 3000);
    CHECK(s.next_due(9000) == -1);                   // all consumed
    CHECK(s.done());
    CHECK(s.fired == 3);
}

TEST_CASE("sched_send: mark_fired counts deferred (late) vs on-time; mark_dropped counts dropped") {
    Schedule s;
    s.add(1000, 5, 0);   // seq 0
    s.add(2000, 5, 0);   // seq 1
    s.mark_fired(0, 1000 + kLateMs - 1);             // within the late window -> on time
    s.mark_fired(1, 2000 + kLateMs + 100);           // past the late window -> deferred
    CHECK(s.fired == 2);
    CHECK(s.deferred == 1);

    Schedule d;
    d.add(1000, 5, 0);
    CHECK(d.overdue(0, 1000 + kSlackMs + 1));        // overdue past slack
    CHECK_FALSE(d.overdue(0, 1000 + 100));
    d.mark_dropped(0);
    CHECK(d.dropped == 1);
    CHECK(d.done());                                 // a dropped entry is consumed
}

TEST_CASE("sched_send: build_body is byte-identical to make_tag(run,self,seq) + @<sendms>") {
    char b[48];
    const uint8_t n = build_body(b, sizeof b, "a1", 254, 7, 12345);
    CHECK(std::string(b) == "Ta1S254#7@12345");      // T<run>S<self>#<seq>@<sendms>
    CHECK(n == (uint8_t)std::strlen("Ta1S254#7@12345"));
    build_body(b, sizeof b, "RUN9", 3, 0, 0);
    CHECK(std::string(b) == "TRUN9S3#0@0");
}

TEST_CASE("sched_send: parse_offsets reads a comma list + tolerates spaces + caps at max") {
    uint32_t out[8];
    CHECK(parse_offsets("0,2000,5000", out, 8) == 3);
    CHECK(out[0] == 0u); CHECK(out[1] == 2000u); CHECK(out[2] == 5000u);
    CHECK(parse_offsets("100, 200 , 300", out, 8) == 3);   // spaces tolerated
    CHECK(out[2] == 300u);
    CHECK(parse_offsets("", out, 8) == 0);
    CHECK(parse_offsets("1,2,3,4,5", out, 3) == 3);        // capped at max
}

TEST_CASE("sched_send: parse_hash8 (exactly-8-hex) + parse_dec (STRICT — rejects typos, no atol truncation)") {
    uint32_t h = 0;
    CHECK(parse_hash8("1a2b3c4d", h)); CHECK(h == 0x1a2b3c4du);
    CHECK(parse_hash8("DEADBEEF", h)); CHECK(h == 0xDEADBEEFu);
    CHECK_FALSE(parse_hash8("1a2b3c4",  h));    // 7 chars
    CHECK_FALSE(parse_hash8("1a2b3c4d5", h));   // 9 chars
    CHECK_FALSE(parse_hash8("1a2b3c4g", h));    // non-hex
    CHECK_FALSE(parse_hash8("", h));
    uint32_t v = 99;
    CHECK(parse_dec("5", 254, v));   CHECK(v == 5u);
    CHECK(parse_dec("254", 254, v)); CHECK(v == 254u);
    CHECK(parse_dec("0", 255, v));   CHECK(v == 0u);    // a valid channel 0
    CHECK_FALSE(parse_dec("255", 254, v));      // over max
    CHECK_FALSE(parse_dec("1ff", 254, v));      // ★ the atol() trap — "1ff" must REJECT, not truncate to id 1
    CHECK_FALSE(parse_dec("12x", 254, v));
    CHECK_FALSE(parse_dec("7!",  254, v));
    CHECK_FALSE(parse_dec("+3",  254, v));
    CHECK_FALSE(parse_dec("",    254, v));
}

TEST_CASE("sched_send: clear resets the ring + counters + run + the seq") {
    Schedule s;
    s.set_run("r1"); s.add(1000, 5, 0); s.mark_fired(0, 1000);
    CHECK(s.armed() == 1); CHECK(s.fired == 1);
    s.clear();
    CHECK(s.armed() == 0); CHECK(s.fired == 0); CHECK(s.deferred == 0); CHECK(s.dropped == 0);
    CHECK(s.run[0] == '\0');
    CHECK(s.add(500, 9, 0) == 0);                    // seq reset to 0
}
