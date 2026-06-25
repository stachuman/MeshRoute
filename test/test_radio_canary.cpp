// MeshRoute — test_radio_canary.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the radio-Module canary's PURE comparison logic (lib/hal/radio_canary.h): diff() finds the first
// changed byte (the device snapshots the RadioLib Module + its HAL and re-checks each loop subsystem), and dword_at()
// reads the corrupted before/after dword for the durable record. The device glue (getMod / the snapshot / the reset)
// is bench-only. NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"
#include "../lib/hal/radio_canary.h"

using namespace mrcanary;

TEST_CASE("radio_canary: diff returns the FIRST differing byte offset, or -1 if identical") {
    uint8_t a[64], b[64];
    for (int i = 0; i < 64; ++i) a[i] = b[i] = (uint8_t)(i * 7 + 1);
    CHECK(diff(a, b, 64) == -1);                       // identical -> intact
    b[0] = (uint8_t)(a[0] ^ 0xFF); CHECK(diff(a, b, 64) == 0);     // a clobber of the first byte
    b[0] = a[0]; b[40] = (uint8_t)(a[40] ^ 1); CHECK(diff(a, b, 64) == 40);   // first change at 40 (mid-region)
    b[40] = a[40]; a[63] = 1; b[63] = 2; CHECK(diff(a, b, 64) == 63);         // the last byte
    CHECK(diff(a, a, 64) == -1);                       // same buffer -> intact
}

TEST_CASE("radio_canary: dword_at reads the little-endian dword, clamped near the end") {
    const uint8_t p[8] = { 0x78, 0x56, 0x34, 0x12, 0xEF, 0xBE, 0xAD, 0xDE };
    CHECK(dword_at(p, 0, 8) == 0x12345678u);
    CHECK(dword_at(p, 4, 8) == 0xDEADBEEFu);
    CHECK(dword_at(p, 6, 8) == 0xDEADBEEFu);           // off 6 > n-4(=4) -> clamps to 4 (still in-bounds)
    CHECK(dword_at(p, 7, 8) == 0xDEADBEEFu);
}
