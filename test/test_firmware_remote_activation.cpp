// MeshRoute — test_firmware_remote_activation.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "doctest.h"
#include "firmware_remote_activation.h"
#include "console_json.h"
#include "firmware_config_parse.h"
#include <cstring>

using namespace mrfw;
namespace {
meshroute::ActivationBudgetInputs reference() {
    return {8, 8, 125000, 5, meshroute::protocol::preamble_sym, 0, 0,
            meshroute::remote_scheduled_terminal_inner_len, false};
}
}
TEST_CASE("firmware activation: zero and both inclusive configured boundaries, no clamp outside") {
    const auto in = reference();
    const auto zero = remote_activation_resolve(0, in);
    CHECK(zero.state == ActivationState::default_derived);
    CHECK(zero.effective_ms == 14012);
    CHECK(zero.floor_ms == 7006);
    CHECK(zero.default_ms == 14012);
    for (uint32_t value : {7006u, 20000u, 299999u}) {
        const auto r = remote_activation_resolve(value, in);
        CHECK(r.state == ActivationState::configured);
        CHECK(r.effective_ms == value);
    }
    const auto below = remote_activation_resolve(7005, in);
    CHECK(below.state == ActivationState::below_floor); CHECK(below.effective_ms == 0);
    const auto above = remote_activation_resolve(300000, in);
    CHECK(above.state == ActivationState::above_ceiling); CHECK(above.effective_ms == 0);
}
TEST_CASE("firmware activation: impossible PHY outranks zero and every configured value") {
    auto in = reference(); in.routing_sf = 12; in.data_sf = 12; in.bw_hz = 7800; in.cr = 8;
    for (uint32_t value : {0u, 20000u, 299999u, UINT32_MAX}) {
        const auto r = remote_activation_resolve(value, in);
        CHECK(r.state == ActivationState::impossible_phy);
        CHECK(r.effective_ms == 0);
        CHECK(r.default_ms > meshroute::remote_action_activation_max_ms);
    }
    in = reference(); in.data_sf = 0;
    CHECK(remote_activation_resolve(0, in).state == ActivationState::impossible_phy);
}
TEST_CASE("firmware activation: re-read PHY changes default and invalidates an old configured promise") {
    auto in = reference();
    CHECK(remote_activation_resolve(7006, in).state == ActivationState::configured);
    in.routing_sf = 12; in.data_sf = 12;
    CHECK(remote_activation_resolve(7006, in).state == ActivationState::below_floor);
    CHECK(remote_activation_resolve(7006, in).effective_ms == 0);
    CHECK(remote_activation_resolve(0, in).effective_ms != 14012);
    in = reference();
    CHECK(remote_activation_resolve(7006, in).effective_ms == 7006);
}
TEST_CASE("firmware activation: state names and JSON twin carry the resolved value") {
    const ActivationState states[] = {ActivationState::default_derived, ActivationState::configured,
        ActivationState::below_floor, ActivationState::above_ceiling, ActivationState::impossible_phy};
    const char* names[] = {"default_derived", "configured", "below_floor", "above_ceiling", "impossible_phy"};
    for (size_t i = 0; i < 5; ++i) {
        CHECK(std::strcmp(activation_state_name(states[i]), names[i]) == 0);
        meshroute::console::CfgExtras extras{};
        extras.remote_action_activation_state = activation_state_name(states[i]);
        extras.remote_action_activation_ms = i == 0 ? 14012 : i == 1 ? 20000 : 0;
        meshroute::NodeConfig cfg{}; char text[4096]{};
        CHECK(meshroute::console::write_cfg(text, sizeof text, cfg, extras) > 0);
        CHECK(std::strstr(text, "\"remote_action_activation_state\":") != nullptr);
        CHECK(std::strstr(text, names[i]) != nullptr);
        const char* value = i == 0 ? "\"remote_action_activation_ms\":14012"
            : i == 1 ? "\"remote_action_activation_ms\":20000" : "\"remote_action_activation_ms\":0";
        CHECK(std::strstr(text, value) != nullptr);
    }
    CHECK(std::strcmp(activation_state_name(static_cast<ActivationState>(255)), "invalid") == 0);
}
TEST_CASE("firmware activation: existing strict decimal parser admits zero and rejects malformed promises") {
    for (const char* s : {"0", "20000", "299999"}) { uint32_t v = 123; CHECK(parse_seq_arg(s, v)); }
    for (const char* s : {"", "-1", "+20000", "20000x", "20000 1", "4294967296"}) {
        uint32_t v = 123; CHECK_FALSE(parse_seq_arg(s, v)); CHECK(v == 123);
    }
}
