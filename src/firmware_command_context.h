// MeshRoute — transport-neutral dispatch metadata (Slice 6).
#pragma once
#include <cstddef>
#include <cstdint>

namespace mrfw {

enum class CommandTransport : uint8_t { usb, ble, remote };
enum class CommandAuthority : uint8_t { local, remote_open, remote_operator, remote_owner };
struct CommandContext {
    CommandTransport transport;
    CommandAuthority authority;
    bool physical_presence;
    uint64_t request_id;
    size_t line_max_bytes;
    uint8_t acl_slot = 0xFF;   // authenticated slot only; every local aggregate keeps NONE
};
enum class DispatchOutcome : uint8_t { completed, unmatched, refused, scheduled, internal_failure };
enum class RefuseReason : uint8_t { none, bad_line, authority, unclassified };

// The backend is ACCEPT-only firmware state; CLIENT supplies an immutable local view.
// Native supplies a test backend, while the inbox probe executes the real firmware backend.
const CommandContext& active_command_context();
class CommandContextScope {
public:
    explicit CommandContextScope(const CommandContext& ctx) : previous_(exchange(&ctx)) {}
    ~CommandContextScope() { (void)exchange(previous_); }
    CommandContextScope(const CommandContextScope&) = delete;
    CommandContextScope& operator=(const CommandContextScope&) = delete;
private:
    static const CommandContext* exchange(const CommandContext*);
    const CommandContext* previous_;
};

}  // namespace mrfw
