// MeshRoute — test_airtime_ledger.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native verification of the device Hal's airtime ledger (H0). The algorithm
// MUST match the sim's FirmwareNode::airtimeUsedInWindow so device == sim.
// CHECK-only (-fno-exceptions); main() is provided by test_airtime.cpp.
#include "doctest.h"

#include "airtime_ledger.h"

using namespace meshroute;

TEST_CASE("AirtimeLedger — sliding window sum + eviction + oldest") {
    AirtimeLedger L;
    CHECK(L.used_in_window(1000, 60000) == 0);
    CHECK(L.oldest_tx_end_ms() == 0);

    L.record(1000, 100);                       // a TX ending at t=1000, 100 ms on air
    L.record(2000, 200);
    CHECK(L.used_in_window(2000, 60000) == 300);   // both inside a 60 s window
    CHECK(L.oldest_tx_end_ms() == 1000);

    // window = 500 ms => cutoff = 1500; the t=1000 entry (end <= cutoff) is evicted.
    CHECK(L.used_in_window(2000, 500) == 200);
    CHECK(L.oldest_tx_end_ms() == 2000);
    CHECK(L.used_in_window(2000, 0) == 0);          // zero window => 0
}

TEST_CASE("AirtimeLedger — ring drop-oldest at cap (no heap, bounded)") {
    AirtimeLedger L;
    for (int i = 0; i < 100; ++i) L.record(1000 + i, 1);   // 100 records > cap 64
    CHECK(L.used_in_window(2000, 1000000) == 64);          // only the last 64 survive
    CHECK(L.oldest_tx_end_ms() == 1000 + 36);              // 100-64 = 36 oldest dropped
}
