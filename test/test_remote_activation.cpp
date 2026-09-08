// MeshRoute — test_remote_activation.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "doctest.h"
#include "remote_activation.h"
#include <cstdio>

using namespace meshroute;
namespace {
ActivationBudgetInputs reference() {
    return {8, 8, 125000, 5, protocol::preamble_sym, 0, 0, remote_scheduled_terminal_inner_len, false};
}
}

TEST_CASE("remote activation: the ruled reference is 7006/14012, never the withdrawn single attempt") {
    const auto in = reference();
    CHECK(remote_scheduled_terminal_inner_len == 46);
    CHECK(remote_scheduled_reply_path_budget_ms(in) == 7006);
    CHECK(remote_action_activation_min_ms(in) == 7006);
    CHECK(remote_action_activation_default_ms(in) == 14012);
    CHECK(remote_action_activation_max_ms == 299999);
    CHECK(remote_action_activation_min_ms(in) != 6506);
    CHECK(remote_action_activation_default_ms(in) != 13012);
}

TEST_CASE("remote activation: largest terminal derives from the codec carrier matrix") {
    size_t largest = 0;
    for (bool wrapper : {false, true}) for (uint8_t depth = 0; depth <= protocol::gw_env_max_hops; ++depth) {
        RemoteCarrier c{};
        c.outer_data_type = wrapper ? DATA_TYPE_MOBILE_SEND : DATA_TYPE_REMOTE_RESP;
        c.enclosed_type = wrapper ? DATA_TYPE_REMOTE_RESP : 0;
        c.wrapper = wrapper; c.cross_layer = depth != 0; c.path_depth = depth;
        c.dst_hash_on_wire = true;
        size_t cap = 0;
        const auto st = remote_body_cap(c, cap);
        if (wrapper && depth == protocol::gw_env_max_hops) { CHECK(st == RemoteStatus::bad_carrier); continue; }
        CHECK(st == RemoteStatus::ok);
        if (st != RemoteStatus::ok) continue;
        const size_t inner = protocol::max_payload_bytes_hard_cap - cap
            + kRemoteOverheadAuthResponse + sizeof(RemoteTerminal) + sizeof(uint32_t);
        if (inner > largest) largest = inner;
    }
    CHECK(largest == remote_scheduled_terminal_inner_len);
}

TEST_CASE("remote activation: every routing turnaround and the separate DATA turnaround is priced") {
    auto in = reference();
    in.slop_routing_ms = 53;
    CHECK(remote_scheduled_reply_path_budget_ms(in) == 7006 + 7 * 53);
    in.slop_data_ms = 71;
    CHECK(remote_scheduled_reply_path_budget_ms(in) == 7006 + 7 * 53 + 71);
    in.slop_routing_ms = 0;
    CHECK(remote_scheduled_reply_path_budget_ms(in) == 7006 + 71);
}

TEST_CASE("remote activation: source PHY and length inputs move the budget") {
    const auto base = reference();
    const auto budget = remote_scheduled_reply_path_budget_ms(base);
    auto in = base; in.routing_sf = 10; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    in = base; in.data_sf = 10; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    in = base; in.bw_hz = 62500; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    in = base; in.cr = 8; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    in = base; in.preamble_sym += 16; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    in = base; in.terminal_inner_len += 32; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    in = base; in.crypted_flight = true; CHECK(remote_scheduled_reply_path_budget_ms(in) > budget);
    for (uint8_t sf : {uint8_t{7}, uint8_t{9}, uint8_t{12}}) {
        in = base; in.routing_sf = sf; in.data_sf = sf;
        CHECK(remote_action_activation_default_ms(in) == 2 * remote_action_activation_min_ms(in));
        CHECK(remote_action_activation_default_ms(in) != 14012);
    }
}

TEST_CASE("remote activation: search real SX1262 bandwidths for an impossible default") {
    // Each is an actual SX1262 bandwidth admitted by the console's 7..500 kHz domain.
    // Search, don't manufacture an invalid PHY or alter the fixed ceiling to reach the arm.
    bool found = false;
    for (uint8_t sf = 5; sf <= 12 && !found; ++sf)
        for (uint32_t bw : {7800u, 10400u, 15600u, 20800u, 31250u, 41700u, 62500u, 125000u, 250000u, 500000u})
            for (uint8_t cr = 5; cr <= 8 && !found; ++cr) {
                auto in = reference(); in.routing_sf = sf; in.data_sf = sf; in.bw_hz = bw; in.cr = cr;
                const auto floor = remote_action_activation_min_ms(in);
                const auto def = remote_action_activation_default_ms(in);
                if (def <= remote_action_activation_max_ms) continue;
                CHECK(floor != UINT32_MAX);
                CHECK(def == 2 * floor);
                std::printf("[activation search] SF%u BW%u CR%u preamble=%u slop=0 floor=%u default=%u ceiling=%u\n",
                            sf, bw, cr, in.preamble_sym, floor, def, remote_action_activation_max_ms);
                found = true;
            }
    CHECK(found);
}

TEST_CASE("remote activation: arithmetic saturates as impossible before either MAC helper can wrap") {
    auto in = reference(); in.slop_routing_ms = UINT32_MAX;
    CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    CHECK(remote_action_activation_default_ms(in) == UINT32_MAX);
    in = reference(); in.slop_data_ms = UINT32_MAX;
    CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.slop_routing_ms = 700000000;
    CHECK(remote_action_activation_min_ms(in) == UINT32_MAX); // each helper fits; the sum does not
    in = reference(); in.slop_data_ms = 3000000000u;
    CHECK(remote_action_activation_min_ms(in) == 3000007006u);
    CHECK(remote_action_activation_default_ms(in) == UINT32_MAX); // only doubling overflows
}

TEST_CASE("remote activation: invalid PHY or unrepresentable carrier is refused, never defaulted") {
    auto in = reference(); in.data_sf = 0; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.routing_sf = 32; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.routing_sf = 4; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.data_sf = 13; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.bw_hz = 0; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.bw_hz = 500001; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.cr = 4; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.cr = 9; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.terminal_inner_len = UINT16_MAX; CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
    in = reference(); in.terminal_inner_len = 241; in.crypted_flight = true;
    CHECK(remote_action_activation_min_ms(in) == UINT32_MAX);
}
