// Shared simple-action admission and effects. No deferred state or remote dispatch (7b-3-P1).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "firmware_config_parse.h" // the existing exact confirmation-token authority

class Print;

namespace mrfw {

enum class ActionKind : uint8_t {
    none, reboot, prep_restart, ota, factory_reset,
    sleep_on, sleep_off, crash_hang, crash_fault, crash_reboot
};
enum class ActionBackend : uint8_t { none, nrf_reset, esp_reset, nrf_dfu, wifi_ota, nrf_fault, esp_fault };
enum class ActionAdmissionStatus : uint8_t { ready, unsupported, confirmation, debug_disabled, usage };
struct ActionPlan {
    ActionKind kind = ActionKind::none;
    ActionBackend backend = ActionBackend::none;
};
struct ActionAdmission {
    ActionPlan plan{};
    ActionAdmissionStatus status = ActionAdmissionStatus::usage;
};
struct ActionSupport {
    ActionBackend reboot = ActionBackend::none;
    ActionBackend ota = ActionBackend::none;
    ActionBackend fault = ActionBackend::none;
    bool power_save = false;
};

// Defined in the board TU: device_fault's backend macros are private to that TU.
ActionSupport action_build_support();

inline ActionAdmission action_backend_admit(ActionKind kind, ActionBackend backend) {
    return {{kind, backend}, backend == ActionBackend::none ? ActionAdmissionStatus::unsupported
                                                          : ActionAdmissionStatus::ready};
}
inline ActionAdmission action_reboot_admit(ActionSupport support) {
    return action_backend_admit(ActionKind::reboot, support.reboot);
}
inline ActionAdmission action_ota_admit(ActionSupport support) {
    return action_backend_admit(ActionKind::ota, support.ota);
}
inline ActionAdmission action_prep_restart_admit() {
    return {{ActionKind::prep_restart, ActionBackend::none}, ActionAdmissionStatus::ready};
}
inline ActionAdmission action_factory_reset_admit(const char* arg, size_t n, ActionSupport support) {
    if (!parse_confirm_token(arg, n)) return {{}, ActionAdmissionStatus::confirmation};
    return action_backend_admit(ActionKind::factory_reset, support.reboot);
}
inline ActionAdmission action_sleep_admit(const char* arg, size_t n, ActionSupport support) {
    while (n && *arg == ' ') { ++arg; --n; }
    const auto kind = n >= 3 && !strncmp(arg, "off", 3) ? ActionKind::sleep_off : ActionKind::sleep_on;
    return {{kind, ActionBackend::none}, support.power_save ? ActionAdmissionStatus::ready
                                                         : ActionAdmissionStatus::unsupported};
}
inline ActionAdmission action_crash_admit(const char* arg, size_t n, bool debug, ActionSupport support) {
    if (!debug) return {{}, ActionAdmissionStatus::debug_disabled};
    while (n && *arg == ' ') { ++arg; --n; }
    if (n >= 4 && !strncmp(arg, "hang", 4))
        return {{ActionKind::crash_hang, ActionBackend::none}, ActionAdmissionStatus::ready};
    if (n >= 5 && !strncmp(arg, "fault", 5)) return action_backend_admit(ActionKind::crash_fault, support.fault);
    if (n >= 6 && !strncmp(arg, "reboot", 6)) return action_backend_admit(ActionKind::crash_reboot, support.reboot);
    return {{}, ActionAdmissionStatus::usage};
}

enum class ActionOutcome : uint8_t {
    none, started, completed, inbox_partial, nv_partial, inbox_nv_partial, backend_failed, unexpected_return
};
// Call-scoped only. A future deferred consumer supplies its observer/sink after consuming its owned plan.
// Local wrappers omit the observer and retain their original output destinations and operation order.
struct ActionObserver {
    void* context = nullptr;
    void (*report)(void*, ActionOutcome) = nullptr;
};
inline ActionOutcome action_report(ActionObserver observer, ActionOutcome outcome) {
    if (observer.report) observer.report(observer.context, outcome);
    return outcome;
}

ActionOutcome action_reboot_apply(ActionBackend, Print&, ActionObserver = {},
                                 ActionOutcome reset_outcome = ActionOutcome::started);
ActionOutcome action_ota_apply(ActionBackend, Print&, ActionObserver = {}); // idempotent entry, never stop
ActionOutcome action_prep_restart_apply(Print&, ActionObserver = {});
ActionOutcome action_factory_reset_apply(ActionBackend, Print& out, Print& reboot_out, ActionObserver = {});
ActionOutcome action_sleep_apply(ActionKind, Print&, ActionObserver = {});
ActionOutcome action_crash_apply(ActionPlan, Print& out, Print& reboot_out, ActionObserver = {});

} // namespace mrfw
