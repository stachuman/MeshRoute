// MeshRoute — authenticated preparation and main-loop-only consumption of one deferred promise.
#include "firmware_remote_actions.h"
#if MR_FEAT_RADMIN_ACCEPT
#include "firmware_remote_activation.h"
#include "fw_context_pure.h"
#include "console_sink.h"
#include "frame_trace.h"
#include "monocypher.h"
#include <cstdio>

namespace mrfw {
namespace {
void request_id(Print& out, uint64_t id) {
    char text[17];
    snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(id));
    out.print(text);
}
void decimal64(Print& out, uint64_t n) {
    char text[21];
    snprintf(text, sizeof text, "%llu", static_cast<unsigned long long>(n));
    out.print(text);
}
const char* phase_name(meshroute::RemoteActionPhase phase) {
    using meshroute::RemoteActionPhase;
    switch (phase) {
    case RemoteActionPhase::none: return "none";
    case RemoteActionPhase::preparing: return "preparing";
    case RemoteActionPhase::prepared: return "prepared";
    case RemoteActionPhase::armed: return "armed";
    case RemoteActionPhase::due: return "due";
    }
    return "none";
}
struct EffectSink final : Print {
    size_t write(uint8_t) override { return 1; }
    size_t write(const uint8_t*, size_t n) override { return n; }
};
struct ActivationReport {
    uint64_t id;
    uint8_t kind;
    static void report(void* context, ActionOutcome outcome) {
        const auto& self = *static_cast<ActivationReport*>(context);
        const uint8_t byte = remote_action_byte(outcome, ActionOutcome::unexpected_return);
        g_node.radmin_action_result(self.kind, byte);
        mrcon.print(F("> remote-action result request_id=")); request_id(mrcon, self.id);
        mrcon.print(F(" outcome=")); mrcon.println(remote_action_outcome_name(byte));
    }
};
}

meshroute::RemoteTerminal remote_action_prepare(const char* line, size_t len, const CommandPolicy& policy,
                                                const CommandContext& ctx, uint32_t configured_ms, Print& out) {
    using meshroute::RemoteTerminal;
    const auto admission = remote_action_admit(policy, line, len, action_build_support(), meshroute::g_mr_trace_on);
    if (admission.status != ActionAdmissionStatus::ready) return RemoteTerminal::refused;
    const auto delay = remote_activation_resolve(configured_ms, remote_activation_live_inputs());
    if (delay.state != ActivationState::default_derived && delay.state != ActivationState::configured)
        return RemoteTerminal::refused;
    const auto bytes = remote_action_pack(admission.plan);
    const auto result = g_node.radmin_prepare_action(ctx.acl_slot, ctx.request_id, bytes.kind, bytes.backend,
                                                    delay.effective_ms);
    if (result != RemoteTerminal::scheduled) return result;
    if (admission.plan.kind == ActionKind::ota)
        out.print(admission.plan.backend == ActionBackend::wifi_ota ? F("> ota backend=wifi\n")
                                                                   : F("> ota backend=ble-dfu\n"));
    mrcon.print(F("> remote-action scheduled request_id=")); request_id(mrcon, ctx.request_id);
    mrcon.print(F(" activation_ms=")); mrcon.print(delay.effective_ms);
    mrcon.print(F(" action=")); mrcon.println(remote_action_kind_name(bytes.kind));
    return result;
}

void remote_action_service_once() {
    meshroute::DeferredActionRecord action{};
    if (!g_node.radmin_take_action(action)) return;
    const auto plan = remote_action_unpack(action.kind, action.backend);
    const auto bytes = remote_action_pack(plan);
    ActivationReport report{action.request_id, bytes.kind};
    const auto trigger = action.trigger;
    crypto_wipe(&action, sizeof action); // no resident/transfer row survives into a non-returning apply
    if (plan.kind == ActionKind::none) {
        ActivationReport::report(&report, ActionOutcome::none);
        return;
    }
    g_node.radmin_action_result(bytes.kind, remote_action_byte(ActionOutcome::started, ActionOutcome::unexpected_return));
    mrcon.print(F("> remote-action activate request_id=")); request_id(mrcon, report.id);
    mrcon.print(F(" trigger=")); mrcon.println(trigger == meshroute::RemoteActionTrigger::ack ? "ack" : "deadline");
    EffectSink sink;
    const ActionObserver observer{&report, ActivationReport::report};
    switch (plan.kind) {
    case ActionKind::reboot: action_reboot_apply(plan.backend, sink, observer); break;
    case ActionKind::prep_restart: action_prep_restart_apply(sink, observer); break;
    case ActionKind::ota: action_ota_apply(plan.backend, sink, observer); break;
    case ActionKind::factory_reset: action_factory_reset_apply(plan.backend, sink, sink, observer); break;
    case ActionKind::sleep_on:
    case ActionKind::sleep_off: action_sleep_apply(ActionKind::sleep_on, sink, observer); break;
    case ActionKind::crash_hang:
    case ActionKind::crash_fault:
    case ActionKind::crash_reboot: action_crash_apply(plan, sink, sink, observer); break;
    case ActionKind::none: break;
    }
}

void remote_action_print_status(Print& out) {
    using meshroute::RemoteActionPhase;
    const auto s = g_node.radmin_action_status(); // one clock snapshot, scalar-only and read-only
    const bool armed = s.phase == RemoteActionPhase::armed || s.phase == RemoteActionPhase::due;
    out.print(F(" radmin_action_phase=")); out.print(phase_name(s.phase));
    out.print(F(" radmin_action_armed=")); out.print(armed ? 1 : 0);
    if (s.phase != RemoteActionPhase::none) {
        out.print(F(" radmin_action_request_id=")); request_id(out, s.request_id);
        out.print(F(" radmin_action_slot=")); out.print(s.controller_slot);
        out.print(F(" radmin_action_kind=")); out.print(remote_action_kind_name(s.kind));
        out.print(F(" radmin_action_activation_ms=")); out.print(s.activation_ms);
        out.print(F(" radmin_action_remaining_ms="));
        if (armed) decimal64(out, s.remaining_ms); else out.print(F("unarmed"));
    }
    out.print(F(" radmin_last_activation_kind=")); out.print(remote_action_kind_name(s.last_kind));
    out.print(F(" radmin_last_activation_outcome=")); out.print(remote_action_outcome_name(s.last_outcome));
}
} // namespace mrfw
#endif
