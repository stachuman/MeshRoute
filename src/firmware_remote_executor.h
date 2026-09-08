// MeshRoute — bounded remote executor ordering (7b-1). No Arduino, NV or global sink.
#pragma once
#include "remote_session.h"
#include "console_line.h"
#include "firmware_command_context.h"
#include "firmware_command_authority.h"

namespace mrfw {

struct IRadminTarget {
    virtual ~IRadminTarget() = default;
    virtual bool next_admitted(meshroute::RadminIngressView&) = 0;
    virtual bool reserve_transcript(uint8_t, uint16_t) = 0;
    virtual void transcript_append(uint8_t, const uint8_t*, size_t) = 0;
    virtual void transcript_complete(uint8_t, meshroute::RemoteTerminal) = 0;
    virtual bool tx_queue_full() = 0;
    virtual meshroute::RadminSend send_next_frame() = 0;
};
struct IRadminTranscriptSink {
    virtual ~IRadminTranscriptSink() = default;
    virtual void append(const uint8_t*, size_t) = 0;
};
struct RadminExecResult {
    DispatchOutcome outcome = DispatchOutcome::internal_failure;
    RefuseReason refuse = RefuseReason::none;
};
struct IRadminExec {
    virtual ~IRadminExec() = default;
    virtual RadminExecResult run(const char*, size_t, const CommandContext&, IRadminTranscriptSink&) = 0;
};

inline bool radmin_disruptive_refused(const CommandPolicy* policy, const CommandContext& ctx) {
    return ctx.transport == CommandTransport::remote && policy && policy->disruptive;
}

inline meshroute::RemoteTerminal radmin_terminal(RadminExecResult r) {
    using meshroute::RemoteTerminal;
    switch (r.outcome) {
    case DispatchOutcome::completed: return RemoteTerminal::completed;
    case DispatchOutcome::unmatched: return RemoteTerminal::unknown_command;
    case DispatchOutcome::refused:
        return r.refuse == RefuseReason::unclassified ? RemoteTerminal::unknown_command : RemoteTerminal::refused;
    case DispatchOutcome::scheduled: // MISSING: 7b-3 owns scheduling; no success producer in this slice.
    case DispatchOutcome::internal_failure: return RemoteTerminal::internal_error;
    }
    return RemoteTerminal::internal_error;
}

inline void radmin_service_once(IRadminTarget& target, IRadminExec& exec) {
    using namespace meshroute;
    if (!target.tx_queue_full() && target.send_next_frame() != RadminSend::none) return;
    RadminIngressView v{};
    if (!target.next_admitted(v)) return;
    size_t cap = 0;
    if (remote_body_cap(v.reply_carrier, cap) != RemoteStatus::ok || cap <= kRemoteOverheadAuthResponse) return;
    if (!target.reserve_transcript(v.seen_index, static_cast<uint16_t>(cap - kRemoteOverheadAuthResponse))) return;

    struct Sink final : IRadminTranscriptSink {
        Sink(IRadminTarget& t, uint8_t s) : target(t), seen(s) {}
        void append(const uint8_t* p, size_t n) override { target.transcript_append(seen, p, n); }
        IRadminTarget& target;
        uint8_t seen;
    } sink(target, v.seen_index);

    // Defensive stale-view check only: real invalidation removes the admitted row before this call.
    if (v.slot >= kRadminAclSlots || (v.role != kRadminRoleOwner && v.role != kRadminRoleOperator)) {
        target.transcript_complete(v.seen_index, RemoteTerminal::refused);
        return;
    }
    const CommandContext ctx{CommandTransport::remote,
        v.role == kRadminRoleOwner ? CommandAuthority::remote_owner : CommandAuthority::remote_operator,
        false, v.request_id, console::remote_command_max_bytes, v.slot};
    const char* line = reinterpret_cast<const char*>(v.body.data());
    const CommandPolicy* policy = command_policy_lookup(line, v.body.size());
    if (radmin_disruptive_refused(policy, ctx)) {
        // MISSING: 7b-3's scheduler. Never invoke the disruptive handler from this context.
        target.transcript_complete(v.seen_index, RemoteTerminal::refused);
        return;
    }
    const RadminExecResult result = exec.run(line, v.body.size(), ctx, sink);
    target.transcript_complete(v.seen_index, radmin_terminal(result));
}

// Runtime glue, defined only on ACCEPT builds in firmware_commands.cpp; no resident adapter objects.
void remote_executor_service_once();

} // namespace mrfw
