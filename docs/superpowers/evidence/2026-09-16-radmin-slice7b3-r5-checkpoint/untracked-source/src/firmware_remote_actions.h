// MeshRoute — 7b-3 firmware interpretation of the core's opaque deferred plan (B402).
#pragma once
#include "mr_features.h"
#include "remote_session.h"
#include "firmware_action_effects.h"
#include "firmware_command_authority.h"
#include <type_traits>

namespace mrfw {

// The ONE checked conversion in each direction. A corrupt byte is none, never a cast-only action.
template<class E> inline uint8_t remote_action_byte(E value, E last) {
    static_assert(std::is_same_v<std::underlying_type_t<E>, uint8_t>);
    const auto byte = static_cast<uint8_t>(value);
    return byte <= static_cast<uint8_t>(last) ? byte : 0;
}
template<class E> inline E remote_action_enum(uint8_t byte, E last) {
    static_assert(std::is_same_v<std::underlying_type_t<E>, uint8_t>);
    return static_cast<E>(byte <= static_cast<uint8_t>(last) ? byte : 0);
}
struct RemoteActionBytes { uint8_t kind, backend; };
inline RemoteActionBytes remote_action_pack(ActionPlan plan) {
    const RemoteActionBytes bytes{remote_action_byte(plan.kind, ActionKind::crash_reboot),
                                  remote_action_byte(plan.backend, ActionBackend::esp_fault)};
    if (bytes.kind != static_cast<uint8_t>(plan.kind) || bytes.backend != static_cast<uint8_t>(plan.backend))
        return {};
    return bytes;
}
inline ActionPlan remote_action_unpack(uint8_t kind, uint8_t backend) {
    const ActionPlan plan{remote_action_enum(kind, ActionKind::crash_reboot),
                          remote_action_enum(backend, ActionBackend::esp_fault)};
    const auto bytes = remote_action_pack(plan);
    return bytes.kind == kind && bytes.backend == backend ? plan : ActionPlan{};
}

inline const char* remote_action_kind_name(uint8_t byte) {
    switch (remote_action_enum(byte, ActionKind::crash_reboot)) {
    case ActionKind::none: return "none";
    case ActionKind::reboot: return "reboot";
    case ActionKind::prep_restart: return "prep-restart";
    case ActionKind::ota: return "ota";
    case ActionKind::factory_reset: return "factory_reset";
    case ActionKind::sleep_on: return "sleep-on";
    case ActionKind::sleep_off: return "sleep-off";
    case ActionKind::crash_hang: return "crash-hang";
    case ActionKind::crash_fault: return "crash-fault";
    case ActionKind::crash_reboot: return "crash-reboot";
    }
    return "none";
}
inline const char* remote_action_outcome_name(uint8_t byte) {
    switch (remote_action_enum(byte, ActionOutcome::unexpected_return)) {
    case ActionOutcome::none: return "none";
    case ActionOutcome::started: return "started";
    case ActionOutcome::completed: return "completed";
    case ActionOutcome::inbox_partial: return "inbox_partial";
    case ActionOutcome::nv_partial: return "nv_partial";
    case ActionOutcome::inbox_nv_partial: return "inbox_nv_partial";
    case ActionOutcome::backend_failed: return "backend_failed";
    case ActionOutcome::unexpected_return: return "unexpected_return";
    }
    return "none";
}

// Policy/authority is checked by exec_console_line. This selects the existing P1 admission for that
// policy family, preserving the local router's exact bare/space boundary and P1's argument grammar.
inline ActionAdmission remote_action_admit(const CommandPolicy& policy, const char* line, size_t len,
                                            ActionSupport support, bool debug) {
    const std::string_view verb(policy.verb), input(line, len);
    if (verb == "reboot" && input == "reboot") return action_reboot_admit(support);
    if ((verb == "prep-restart" || verb == "reboot (alias: prep-restart)") && input == "prep-restart")
        return action_prep_restart_admit();
    if (verb == "ota" && input == "ota") return action_ota_admit(support);
    const size_t n = verb.size();
    if (len < n || input.substr(0, n) != verb || (len != n && line[n] != ' '))
        return {{}, ActionAdmissionStatus::usage};
    if (verb == "factory_reset") return action_factory_reset_admit(line + n, len - n, support);
    if (verb == "sleep") return action_sleep_admit(line + n, len - n, support);
    if (verb == "crashtest") return action_crash_admit(line + n, len - n, debug, support);
    return {{}, ActionAdmissionStatus::unsupported}; // the 36 recorded R-RA-39 exceptions
}

#if MR_FEAT_RADMIN_ACCEPT
meshroute::RemoteTerminal remote_action_prepare(const char*, size_t, const CommandPolicy&,
                                                const CommandContext&, Print&);
void remote_action_service_once();
void remote_action_print_status(Print&);
#endif
} // namespace mrfw
