// MeshRoute — 7b-1 pure executor ordering. These fakes are NOT real handler execution.
#include "doctest.h"
#include "../src/firmware_remote_executor.h"
#include <string>
#include <vector>

namespace mrfw {
namespace {
const CommandContext* test_active_context = nullptr;
constexpr CommandContext test_local_context{CommandTransport::usb, CommandAuthority::local, true, 0, 0};
}
const CommandContext& active_command_context() { return test_active_context ? *test_active_context : test_local_context; }
const CommandContext* CommandContextScope::exchange(const CommandContext* ctx) {
    const CommandContext* previous = test_active_context;
    test_active_context = ctx;
    return previous;
}
}

namespace {
using namespace mrfw;
using namespace meshroute;
struct Target : IRadminTarget {
    bool pending = true, room = true, full = false;
    bool open_pending = false, control_pending = false;
    RadminSend open_send = RadminSend::none;
    RadminSend send = RadminSend::none;
    uint8_t role = kRadminRoleOwner;
    uint8_t slot = 3;
    std::string line = "status";
    std::string bytes;
    std::vector<std::string> order;
    uint16_t cap = 0;
    RemoteTerminal terminal = RemoteTerminal::internal_error;
    bool next_admitted(RadminIngressView& v) override {
        order.emplace_back("next");
        if (!pending) return false;
        v.seen_index = 5; v.slot = slot; v.role = role; v.request_id = 17;
        v.body = {reinterpret_cast<const uint8_t*>(line.data()), line.size()};
        v.route.carrier = static_cast<uint8_t>(RadminCarrierKind::same_layer);
        v.reply_carrier = remote_reply_carrier(v.route);
        return true;
    }
    bool reserve_transcript(uint8_t si, uint16_t n) override {
        CHECK(si == 5); order.emplace_back("reserve"); cap = n;
        if (!room) return false;
        pending = false;
        return true;
    }
    void transcript_append(uint8_t si, const uint8_t* p, size_t n) override {
        CHECK(si == 5); order.emplace_back("append"); bytes.append(reinterpret_cast<const char*>(p), n);
    }
    void transcript_complete(uint8_t si, RemoteTerminal r) override {
        CHECK(si == 5); order.emplace_back("complete"); terminal = r;
    }
    bool tx_queue_full() override { order.emplace_back("full"); return full; }
    RadminSend send_next_frame() override { order.emplace_back("send"); return send; }
    void expire() override { order.emplace_back("expire"); }
    bool service_control() override { order.emplace_back("control"); return control_pending; }
    bool next_open(RadminOpenView& v) override {
        order.emplace_back("open-next"); if (!open_pending) return false;
        v.index = 2; v.request_id = 29;
        v.body = {reinterpret_cast<const uint8_t*>(line.data()), line.size()};
        ReplyRoute route{}; route.carrier = static_cast<uint8_t>(RadminCarrierKind::same_layer);
        v.reply_carrier = remote_reply_carrier(route); return true;
    }
    bool reserve_open(uint8_t i, uint16_t n) override {
        CHECK(i == 2); CHECK(n == 222); order.emplace_back("open-reserve");
        if (!open_pending) return false;
        open_pending = false; line.assign(line.size(), '!'); return true; // overwrite the borrowed input NOW
    }
    void open_append(uint8_t i, const uint8_t* p, size_t n) override {
        CHECK(i == 2); order.emplace_back("open-append"); bytes.append(reinterpret_cast<const char*>(p), n);
    }
    void open_complete(uint8_t i, RemoteTerminal r) override {
        CHECK(i == 2); order.emplace_back("open-complete"); terminal = r;
    }
    RadminSend send_open_frame() override { order.emplace_back("open-send"); return open_send; }
};
struct Exec : IRadminExec {
    unsigned calls = 0;
    CommandContext got{};
    RadminExecResult result{DispatchOutcome::completed, RefuseReason::none};
    RadminExecResult run(const char* p, size_t n, const CommandContext& ctx, IRadminTranscriptSink& sink) override {
        ++calls; got = ctx;
        CHECK(std::string(p, n) == "status");
        sink.append(reinterpret_cast<const uint8_t*>("hello\n"), 6);
        return result;
    }
};
}

TEST_CASE("§radmin-7/exec ordering reserves before exactly one fake dispatch and completes afterwards") {
    Target t; Exec e;
    radmin_service_once(t, e);
    CHECK(t.order == std::vector<std::string>{"expire", "full", "control", "send", "next", "reserve", "append", "complete"});
    CHECK(e.calls == 1); CHECK(t.cap == 206); CHECK(t.bytes == "hello\n");
    CHECK(t.terminal == RemoteTerminal::completed);
    CHECK(e.got.transport == CommandTransport::remote);
    CHECK(e.got.authority == CommandAuthority::remote_owner);
    CHECK_FALSE(e.got.physical_presence); CHECK(e.got.request_id == 17);
    CHECK(e.got.acl_slot == 3); CHECK(e.got.line_max_bytes == console::remote_command_max_bytes);
    radmin_service_once(t, e); CHECK(e.calls == 1);
}
TEST_CASE("§radmin-7/exec no transcript capacity never calls executor, then operator resumes") {
    Target t; Exec e; t.room = false;
    radmin_service_once(t, e);
    CHECK(e.calls == 0); CHECK(t.pending); CHECK(t.bytes.empty());
    t.room = true; t.role = kRadminRoleOperator;
    radmin_service_once(t, e); CHECK(e.calls == 1);
    CHECK(e.got.authority == CommandAuthority::remote_operator);
}
TEST_CASE("§radmin-7/exec one pending frame consumes the pass on every attempted admission outcome") {
    for (const RadminSend result : {RadminSend::queued, RadminSend::parked, RadminSend::refused}) {
        Target t; Exec e; t.send = result;
        radmin_service_once(t, e);
        CHECK(t.order == std::vector<std::string>{"expire", "full", "control", "send"});
        CHECK(e.calls == 0); CHECK(t.pending);
    }
    Target t; Exec e; t.full = true;
    radmin_service_once(t, e);
    CHECK(e.calls == 1);
    CHECK(t.order.front() == "expire"); CHECK(t.order[1] == "full"); CHECK(t.order[2] == "next");
}
TEST_CASE("§radmin-7/exec disruptive policy refuses before the fake can execute") {
    for (const char* line : {"reboot", "factory_reset confirm", "regen"}) {
        Target t; Exec e; t.line = line;
        const auto* p = command_policy_lookup(t.line.data(), t.line.size());
        CHECK(p != nullptr); if (!p) continue;
        CHECK(p->disruptive); if (!p->disruptive) continue;
        radmin_service_once(t, e);
        CHECK(e.calls == 0); CHECK(t.bytes.empty()); CHECK(t.terminal == RemoteTerminal::refused);
    }
}
TEST_CASE("§radmin-7/exec typed terminal mapping does not scrape handler output") {
    struct Row { DispatchOutcome out; RefuseReason reason; RemoteTerminal result; };
    for (const Row r : {
        Row{DispatchOutcome::completed, RefuseReason::none, RemoteTerminal::completed},
        Row{DispatchOutcome::unmatched, RefuseReason::none, RemoteTerminal::unknown_command},
        Row{DispatchOutcome::refused, RefuseReason::unclassified, RemoteTerminal::unknown_command},
        Row{DispatchOutcome::refused, RefuseReason::authority, RemoteTerminal::refused},
        Row{DispatchOutcome::refused, RefuseReason::bad_line, RemoteTerminal::refused},
        Row{DispatchOutcome::internal_failure, RefuseReason::none, RemoteTerminal::internal_error},
        Row{DispatchOutcome::scheduled, RefuseReason::none, RemoteTerminal::internal_error}}) {
        Target t; Exec e; e.result = {r.out, r.reason};
        radmin_service_once(t, e); CHECK(t.terminal == r.result); CHECK(e.calls == 1);
    }
}
TEST_CASE("§radmin-7/exec SYNTHETIC stale view refuses; real invalidation removes the row") {
    Target t; Exec e; t.role = kRadminRoleEmpty;
    radmin_service_once(t, e); CHECK(e.calls == 0); CHECK(t.terminal == RemoteTerminal::refused);
}
TEST_CASE("§radmin-7/context pure scope restores nested and early-return contexts") {
    CHECK(active_command_context().transport == CommandTransport::usb);
    CHECK(active_command_context().acl_slot == 0xFF);
    const CommandContext remote{CommandTransport::remote, CommandAuthority::remote_owner, false, 17, 201, 3};
    {
        CommandContextScope outer(remote);
        CHECK(active_command_context().acl_slot == 3);
        const auto local_call = [] {
            const CommandContext local{CommandTransport::ble, CommandAuthority::local, false, 0, 274};
            CommandContextScope inner(local);
            CHECK(active_command_context().acl_slot == 0xFF);
            return;
        };
        local_call(); CHECK(active_command_context().acl_slot == 3);
    }
    CHECK(active_command_context().transport == CommandTransport::usb);
}

TEST_CASE("§radmin-7b2/exec control priority and independent open dispatch behind authenticated pool pressure") {
    Target t; Exec exec; t.control_pending = true; t.open_pending = true;
    radmin_service_once(t, exec);
    CHECK(t.order == std::vector<std::string>{"expire", "full", "control"}); CHECK(exec.calls == 0);
    t.control_pending = false; t.room = false; t.order.clear();
    radmin_service_once(t, exec);
    CHECK(t.order == std::vector<std::string>{"expire", "full", "control", "send", "next", "reserve",
                                            "open-send", "open-next", "open-reserve", "open-append", "open-complete"});
    CHECK(exec.calls == 1); CHECK(t.bytes == "hello\n"); CHECK(t.line == "!!!!!!");
    CHECK(exec.got.authority == CommandAuthority::remote_open); CHECK(exec.got.transport == CommandTransport::remote);
    CHECK_FALSE(exec.got.physical_presence); CHECK(exec.got.acl_slot == kRadminNoSlot);
    CHECK(exec.got.request_id == 29); CHECK(exec.got.line_max_bytes == console::remote_command_max_bytes);
    radmin_service_once(t, exec); CHECK(exec.calls == 1);
}

TEST_CASE("§radmin-7b2/exec any attempted open frame consumes the pass and full TX permits owned capture") {
    for (const auto outcome : {RadminSend::queued, RadminSend::parked, RadminSend::refused}) {
        Target t; Exec exec; t.pending = false; t.open_pending = true; t.open_send = outcome;
        radmin_service_once(t, exec);
        CHECK(exec.calls == 0); CHECK(t.open_pending); CHECK(t.order.back() == "open-send");
    }
    Target t; Exec exec; t.full = true; t.control_pending = true; t.open_pending = true;
    radmin_service_once(t, exec);
    CHECK(exec.calls == 1); CHECK(t.open_pending); CHECK(exec.got.authority == CommandAuthority::remote_owner);
    CHECK(t.order == std::vector<std::string>{"expire", "full", "next", "reserve", "append", "complete"});
}
