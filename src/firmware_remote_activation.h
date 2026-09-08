// MeshRoute — src/firmware_remote_activation.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// Pure effective-value policy. Raw NV/global is the only resident value; readouts resolve live.
#pragma once
#include "remote_activation.h"

namespace mrfw {
enum class ActivationState : uint8_t { default_derived, configured, below_floor, above_ceiling, impossible_phy };
struct ActivationResolved {
    uint32_t effective_ms;
    uint32_t floor_ms;
    uint32_t default_ms;
    ActivationState state;
};
inline ActivationResolved remote_activation_resolve(uint32_t persisted_ms,
                                                    const meshroute::ActivationBudgetInputs& in) {
    const uint32_t floor = meshroute::remote_action_activation_min_ms(in);
    const uint32_t def = meshroute::remote_action_activation_default_ms(in);
    if (def > meshroute::remote_action_activation_max_ms)
        return {0, floor, def, ActivationState::impossible_phy};
    if (persisted_ms == 0) return {def, floor, def, ActivationState::default_derived};
    if (persisted_ms < floor) return {0, floor, def, ActivationState::below_floor};
    if (persisted_ms > meshroute::remote_action_activation_max_ms)
        return {0, floor, def, ActivationState::above_ceiling};
    return {persisted_ms, floor, def, ActivationState::configured};
}
inline const char* activation_state_name(ActivationState state) {
    switch (state) {
        case ActivationState::default_derived: return "default_derived";
        case ActivationState::configured: return "configured";
        case ActivationState::below_floor: return "below_floor";
        case ActivationState::above_ceiling: return "above_ceiling";
        case ActivationState::impossible_phy: return "impossible_phy";
    }
    return "invalid";
}
// One hardware binding, defined in firmware_commands.cpp. No hardware include in this pure header.
meshroute::ActivationBudgetInputs remote_activation_live_inputs();
}  // namespace mrfw
