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

// H5 (2026-07-04, REGULATORY): on overflow the ring COALESCES its two oldest records rather than DROPPING
// the oldest, so used_in_window() can NEVER UNDER-report in-window airtime (which would let the node transmit
// past the legal EU868 1% duty). Over-reporting slightly is the SAFE direction. This asserts: (1) after >kCap
// in-window TXes the reported airtime equals the TRUE in-window sum (nothing lost); (2) a genuinely AGED
// record (end_ms <= cutoff) still drops.
TEST_CASE("AirtimeLedger — overflow COALESCES (never under-reports in-window airtime) [H5]") {
    AirtimeLedger L;
    // 100 TXes (> cap 64), each 3 ms on air, all ending in [1000, 1099] — ALL inside a huge window at now=2000.
    for (int i = 0; i < 100; ++i) L.record(1000 + i, 3);
    // The true in-window sum is 100*3 = 300. Drop-oldest would have reported 64*3 = 192 (a 108 ms UNDER-report,
    // the regulatory bug). Coalesce loses nothing -> used >= the true sum (here exactly 300).
    const uint64_t true_in_window = 300;
    CHECK(L.used_in_window(2000, 1000000) >= true_in_window);   // ★ NO UNDER-REPORT — the hard constraint
    CHECK(L.used_in_window(2000, 1000000) == true_in_window);   // and here it's exact (all in-window, nothing aged)
    // The ring stays bounded (no heap): the merged records occupy <= kCap slots but preserve all airtime.
    CHECK(L.oldest_tx_end_ms() <= 1000 + 99);                   // the oldest surviving end_ms is within the TX span
}

TEST_CASE("AirtimeLedger — a genuinely aged record still drops after coalescing [H5]") {
    AirtimeLedger L;
    // A batch of OLD TXes (end ~ t=1000) then a batch of RECENT ones (end ~ t=1,000,000), enough to overflow.
    for (int i = 0; i < 70; ++i) L.record(1000 + i, 5);            // old batch (will age out of a 60 s window)
    for (int i = 0; i < 70; ++i) L.record(1000000 + i, 5);        // recent batch
    // At now = 1,000,200 with a 60 s window, cutoff = 940,200 -> every OLD record (end ~1000) is aged out; only
    // the recent batch counts. Coalescing merged some old records together, but evict_before still drops any
    // merged record whose (newer) end_ms <= cutoff -> the aged airtime does NOT linger.
    const uint64_t used = L.used_in_window(1000000 + 200, 60000);
    CHECK(used >= 70 * 5);                                        // all 70 recent (in-window) TXes counted (no under-report)
    CHECK(used <= 140 * 5);                                       // and the fully-aged old batch didn't inflate it unboundedly
    CHECK(L.oldest_tx_end_ms() >= 1000000);                      // the oldest surviving record is from the recent batch
}
