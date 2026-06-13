// MeshRoute — test_timer_wheel.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native verification of the H1 device timer wheel (lib/hal/timer_wheel) — fire ordering,
// cancel, re-arm-by-id, the bounded caller-id contract. CHECK-only (-fno-exceptions);
// main() is provided by test_airtime.cpp.
#include "doctest.h"

#include "timer_wheel.h"
#include "iclock.h"

using meshroute::TimerWheel;

TEST_CASE("accumulate_millis_wrap — a monotonic 64-bit epoch across the ~49-day millis() wrap") {
    uint32_t last = 0; uint64_t high = 0;
    using meshroute::accumulate_millis_wrap;
    CHECK(accumulate_millis_wrap(0, last, high) == 0);
    CHECK(accumulate_millis_wrap(1000, last, high) == 1000);
    CHECK(accumulate_millis_wrap(0xFFFFFFFF, last, high) == 0xFFFFFFFFull);   // just before wrap
    CHECK(accumulate_millis_wrap(5, last, high) == 0x100000005ull);          // wrapped -> +2^32, monotonic
    CHECK(accumulate_millis_wrap(100, last, high) == 0x100000064ull);        // keeps climbing
    CHECK(accumulate_millis_wrap(0xFFFFFFF0, last, high) == 0x1FFFFFFF0ull);  // approach 2nd wrap
    CHECK(accumulate_millis_wrap(0, last, high) == 0x200000000ull);          // 2nd wrap -> +2*2^32
}

TEST_CASE("TimerWheel — earliest-due fires first, deactivated after firing") {
    TimerWheel w;
    CHECK(w.after(100, /*id=*/5, /*now=*/1000));   // due 1100
    CHECK(w.after(50,  /*id=*/7, /*now=*/1000));   // due 1050
    CHECK(w.pop_due(1049) == -1);                  // nothing due yet
    CHECK(w.pop_due(1050) == 7);                   // 7 (earlier) fires first
    CHECK(w.pop_due(1050) == -1);                  // 7 is one-shot -> gone
    CHECK(w.pop_due(1100) == 5);                   // 5 now due
    CHECK(w.pop_due(1100) == -1);
}

TEST_CASE("TimerWheel — same-deadline ties fire in ascending id order (deterministic)") {
    TimerWheel w;
    CHECK(w.after(100, /*id=*/9, 1000));           // due 1100
    CHECK(w.after(100, /*id=*/3, 1000));           // due 1100
    CHECK(w.after(100, /*id=*/6, 1000));           // due 1100
    CHECK(w.pop_due(1100) == 3);                   // smallest id first
    CHECK(w.pop_due(1100) == 6);
    CHECK(w.pop_due(1100) == 9);
    CHECK(w.pop_due(1100) == -1);
}

TEST_CASE("TimerWheel — cancel is idempotent and prevents the fire") {
    TimerWheel w;
    CHECK(w.after(10, /*id=*/4, 2000));            // due 2010
    CHECK(w.active(4));
    CHECK(w.due_at(4) == 2010);
    w.cancel(4);
    CHECK(!w.active(4));
    w.cancel(4);                                   // idempotent
    w.cancel(63);                                  // inactive id -> no-op
    CHECK(w.pop_due(9000) == -1);                  // cancelled never fires
}

TEST_CASE("TimerWheel — re-arm-by-id replaces the pending deadline (the Hal contract)") {
    TimerWheel w;
    CHECK(w.after(100, /*id=*/4, 2000));           // due 2100
    CHECK(w.after(10,  /*id=*/4, 2000));           // RE-ARM 4 -> due 2010 (replaces, NOT a 2nd timer)
    CHECK(w.due_at(4) == 2010);
    CHECK(w.pop_due(2010) == 4);                   // fires at the NEW deadline
    CHECK(w.pop_due(2100) == -1);                  // the old 2100 deadline is gone (single slot)
}

TEST_CASE("TimerWheel — a fired timer that re-arms with a positive delay does not re-fire this tick") {
    TimerWheel w;
    CHECK(w.after(0, /*id=*/1, 5000));             // due 5000 (immediately)
    int id = w.pop_due(5000);
    CHECK(id == 1);
    w.after(20, 1, 5000);                          // re-arm 1 for 5020 (as a periodic timer would)
    CHECK(w.pop_due(5000) == -1);                  // not due again this tick -> drain loop terminates
    CHECK(w.pop_due(5020) == 1);                   // fires next tick
}

TEST_CASE("TimerWheel — out-of-range caller id is rejected (bounded cap 80)") {
    TimerWheel w;
    CHECK(w.after(10, /*id=*/79, 0));              // last valid id (kCap=80; Slice-3 gateway band is 64..79)
    CHECK_FALSE(w.after(10, /*id=*/80, 0));        // == kCap -> rejected
    CHECK_FALSE(w.after(10, /*id=*/255, 0));
    CHECK(!w.active(80));
    CHECK(w.pop_due(100) == 79);                   // the valid one still fires
}

TEST_CASE("TimerWheel — a Slice-3 gateway-band id (64..79) arms + fires (kCap raised 64->80)") {
    TimerWheel w;
    CHECK(w.after(50, /*id=*/64, 1000));           // kLayerWindowTimerId (layer 0)
    CHECK(w.after(50, /*id=*/69, 1000));           // kLayerBeaconTimerId + 1 (layer 1)
    CHECK(w.active(64)); CHECK(w.active(69));
    CHECK(w.earliest_due() == 1050);
    CHECK(w.pop_due(1050) == 64);                  // smallest-id-first on a tie
    CHECK(w.pop_due(1050) == 69);
    CHECK(!w.active(64)); CHECK(!w.active(69));     // one-shot
}

TEST_CASE("TimerWheel — earliest_due returns the min active deadline (UINT64_MAX when idle)") {
    TimerWheel w;
    CHECK(w.earliest_due() == UINT64_MAX);         // nothing armed -> idle
    CHECK(w.after(100, /*id=*/5, /*now=*/1000));   // due 1100
    CHECK(w.after(50,  /*id=*/7, /*now=*/1000));   // due 1050
    CHECK(w.after(300, /*id=*/2, /*now=*/1000));   // due 1300
    CHECK(w.earliest_due() == 1050);               // the soonest deadline (sleep target)
    CHECK(w.pop_due(1050) == 7);                   // fire it
    CHECK(w.earliest_due() == 1100);               // next soonest
    w.cancel(5);
    CHECK(w.earliest_due() == 1300);               // 5 cancelled -> 2 is next
    w.cancel(2);
    CHECK(w.earliest_due() == UINT64_MAX);         // none left -> idle again
}
