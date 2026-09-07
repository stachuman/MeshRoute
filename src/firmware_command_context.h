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
};
enum class DispatchOutcome : uint8_t { completed, unmatched, refused, scheduled, internal_failure };
enum class RefuseReason : uint8_t { none, bad_line, authority, unclassified };

}  // namespace mrfw
