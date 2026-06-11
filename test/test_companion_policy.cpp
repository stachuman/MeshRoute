// MeshRoute — test_companion_policy.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the BLE companion advertising policy (lib/core/companion_policy.h): off never advertises,
// on always, periodic windows on a schedule (+ a boot window), and request_window opens an immediate window.
// Pure logic against explicit `now_ms` values (no clock, no device). NB: test_airtime.cpp provides main();
// -fno-exceptions => CHECK only.
#include "doctest.h"

#include "companion_policy.h"

#include <initializer_list>

using namespace meshroute;

namespace {
constexpr uint32_t kWin    = 30000;     // 30 s window
constexpr uint16_t kPerMin = 15;        // 15 min period
constexpr uint64_t kPerMs  = 15ull * 60000;   // 900000
}

TEST_CASE("companion policy: off never advertises") {
    CompanionPolicy p; p.configure(BleMode::off, kPerMin, kWin);
    for (uint64_t t : {0ull, 1000ull, 900000ull, 9000000ull}) {
        auto r = p.on_tick(t);
        CHECK_FALSE(r.should_advertise);
        CHECK(r.next_change_ms == UINT64_MAX);
    }
    p.request_window(5000);                       // no-op in off
    CHECK_FALSE(p.on_tick(6000).should_advertise);
}

TEST_CASE("companion policy: on always advertises") {
    CompanionPolicy p; p.configure(BleMode::on, kPerMin, kWin);
    for (uint64_t t : {0ull, 50000ull, 5000000ull}) {
        auto r = p.on_tick(t);
        CHECK(r.should_advertise);
        CHECK(r.next_change_ms == UINT64_MAX);
    }
}

TEST_CASE("companion policy: periodic — boot window, then one window per period") {
    CompanionPolicy p; p.configure(BleMode::periodic, kPerMin, kWin);

    // Boot window [0, 30000): advertise.
    { auto r = p.on_tick(0);      CHECK(r.should_advertise);       CHECK(r.next_change_ms == kWin); }
    { auto r = p.on_tick(15000);  CHECK(r.should_advertise);       CHECK(r.next_change_ms == kWin); }
    // Window closes at 30000 -> idle until the next window at kPerMs.
    { auto r = p.on_tick(30000);  CHECK_FALSE(r.should_advertise); CHECK(r.next_change_ms == kPerMs); }
    { auto r = p.on_tick(500000); CHECK_FALSE(r.should_advertise); CHECK(r.next_change_ms == kPerMs); }
    // Next scheduled window opens at kPerMs (900000), lasts kWin.
    { auto r = p.on_tick(kPerMs);          CHECK(r.should_advertise);       CHECK(r.next_change_ms == kPerMs + kWin); }
    { auto r = p.on_tick(kPerMs + 10000);  CHECK(r.should_advertise); }
    { auto r = p.on_tick(kPerMs + kWin);   CHECK_FALSE(r.should_advertise); CHECK(r.next_change_ms == 2 * kPerMs); }
}

TEST_CASE("companion policy: request_window opens an immediate window during idle (periodic)") {
    CompanionPolicy p; p.configure(BleMode::periodic, kPerMin, kWin);
    p.on_tick(0);                                  // boot window
    p.on_tick(30000);                              // window closes -> idle
    CHECK_FALSE(p.on_tick(100000).should_advertise);   // idle mid-period
    p.request_window(100000);                      // press-to-advertise
    { auto r = p.on_tick(100001); CHECK(r.should_advertise);       CHECK(r.next_change_ms == 130000); }  // 100000 + 30000
    { auto r = p.on_tick(130000); CHECK_FALSE(r.should_advertise); }                                     // requested window closed
    // the periodic schedule is untouched — the next scheduled window still opens at kPerMs.
    { auto r = p.on_tick(kPerMs);  CHECK(r.should_advertise); }
}
