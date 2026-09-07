// MeshRoute — Slice 7a-0 MAC wait-window refactor
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "doctest.h"
#include "airtime.h"
#include "frame_codec.h"
#include "mac_wait_windows.h"
#include "protocol_constants.h"

using meshroute::ack_wait_delay_ms;
using meshroute::cts_wait_delay_ms;

TEST_CASE("mac wait windows: the 0e reference PHY reproduces every armed window") {
    const auto air = [](uint16_t len) {
        return meshroute::airtime_ms(8, 125000, 5, meshroute::protocol::preamble_sym, len);
    };
    const uint32_t base = air(meshroute::unicast_rts_wire_len(false))
                        + air(meshroute::terminal_cts_wire_len(false));
    // 0e-D's characterized ACK-timer operand is 64 bytes: the MAC's 18-byte
    // length model plus the fixture's 46-byte largest scheduled terminal inner.
    // This refactor tests the window, not a second carrier-cap authority.
    const uint32_t data_air = air(64);
    const uint32_t ack_air = air(3);
    CHECK(base == 166);
    CHECK(data_air == 231);
    CHECK(ack_air == 78);
    CHECK(cts_wait_delay_ms(base, 0, 0) == 167);
    CHECK(cts_wait_delay_ms(base, 1, 0) == 333);
    CHECK(cts_wait_delay_ms(base, 2, 0) == 665);
    CHECK(ack_wait_delay_ms(data_air, ack_air, 0, 0) == 311);
}

TEST_CASE("mac wait windows: CTS retry backoff caps at two and pays two turnarounds") {
    struct Cell { uint8_t attempt; uint32_t without_slop; };
    constexpr Cell cells[] = {{0, 167}, {1, 333}, {2, 665}, {3, 665}, {255, 665}};
    for (const auto& cell : cells) {
        CAPTURE(cell.attempt);
        CHECK(cts_wait_delay_ms(166, cell.attempt, 0) == cell.without_slop);
        CHECK(cts_wait_delay_ms(166, cell.attempt, 53) == cell.without_slop + 106);
    }
}

TEST_CASE("mac wait windows: ACK keeps both airtimes both slops and its margin") {
    CHECK(ack_wait_delay_ms(0, 0, 0, 0) == 2);
    CHECK(ack_wait_delay_ms(11, 0, 0, 0) == 13);
    CHECK(ack_wait_delay_ms(0, 23, 0, 0) == 25);
    CHECK(ack_wait_delay_ms(0, 0, 5, 0) == 7);
    CHECK(ack_wait_delay_ms(0, 0, 0, 7) == 9);
    CHECK(ack_wait_delay_ms(11, 23, 5, 7) == 48);
}

TEST_CASE("mac wait windows: synthetic overflow preserves the original unsigned arithmetic") {
    // Not admissible-PHY examples: these pin the refactor's arithmetic domain.
    // A configuration-budget caller must check overflow before using the helpers.
    constexpr uint32_t max = UINT32_MAX;
    CHECK(cts_wait_delay_ms(max, 1, 0) == max);
    CHECK(cts_wait_delay_ms(max, 2, 0) == max - 2);
    CHECK(cts_wait_delay_ms(0, 0, max) == max);
    CHECK(ack_wait_delay_ms(max, 0, 0, 0) == 1);
    CHECK(ack_wait_delay_ms(max, max, max, max) == max - 1);
}
