// MeshRoute — lib/core/command.h
//
// The app<->firmware command seam. TYPED, bounded, no-heap, transport-agnostic —
// the message path is NOT string-parsed (that is the MeshCore anti-pattern; their
// text CLI is config-only, the message hot path is a typed cmd-code frame). Each
// backend (sim FirmwareNode / device serial-BLE) parses its own wire INTO these
// PODs and calls Node::on_command; lib/core never sees a transport byte.
//
// Mirrors hal.h's discipline: typed PODs in, no std::string/json/heap, bodies
// BORROWED for the call only. See docs/specs/2026-05-30-command-interface-design.md.
#pragma once
#include "protocol_constants.h"
#include <cstddef>
#include <cstdint>

namespace meshroute {

// ---- requests (one cmd-code + a bounded typed payload, like a MeshCore frame) ----
enum class CmdKind : uint8_t { send, send_layer, send_channel, join };

// The four Lua send_* verbs collapse to ONE Send + flag bits (same wire bits as
// dv_dual_sf.lua:2187-2189). Addressed by short id (now) / key_hash32 (later) —
// never a name (the device has no name map; that is forever a backend concern).
// Plain PODs (no in-class initializers) so the union has a trivial default ctor and
// the header stays C++17-includable by the sim (hal.h discipline). flags: E2E=0x08|PRIORITY=0x02.
struct SendCmd        { uint8_t dst_id; uint32_t dst_hash; uint8_t flags; };
struct SendLayerCmd   { uint8_t hops[protocol::gw_env_max_hops]; uint8_t hop_count; uint32_t dst_hash; };
struct SendChannelCmd { uint8_t channel_id; };
struct JoinCmd        { enum Op : uint8_t { discover, claim, deny } op; uint8_t node_id; uint32_t claimant_hash; };

struct Command {
    CmdKind kind = CmdKind::send;
    union {                              // value-init (Command c{}) zero-inits `send` (the first arm)
        SendCmd        send;
        SendLayerCmd   layer;
        SendChannelCmd channel;
        JoinCmd        join;
    } u;
    const uint8_t* body     = nullptr;   // BORROWED for the call only (mirrors hal.h on_recv)
    uint8_t        body_len = 0;
};

// ---- synchronous result (the token; matches MeshCore RESP_CODE_*) ----
enum class CmdCode : uint8_t { queued, err_unknown_dst, err_too_large,
                               err_no_gateway, err_priority_capped, err_no_binding, err_unsupported };
struct CmdResult { CmdCode code = CmdCode::queued; uint16_t ctr = 0; uint8_t queue_depth = 0; };

// ---- async push channel (delivery/ACK/inbound; matches MeshCore PUSH_CODE_*) ----
// Drained by the transport via Node::next_push (CMD_SYNC_NEXT-style). The Node owns
// a bounded ring (cap_push_ring), drop-oldest on overflow (MeshCore offline queue).
enum class PushKind : uint8_t {
    msg_recv,      // a message was delivered to US (origin/body = the inbound text)
    send_acked,    // our send's link ACK returned (ctr = the sent message id)
    send_failed,   // our send gave up (ctr = the sent message id)
};
struct Push {
    PushKind kind = PushKind::msg_recv;
    uint8_t  origin = 0;
    uint8_t  dst = 0;
    uint16_t ctr = 0;
    uint8_t  body[protocol::max_payload_bytes_hard_cap] = {};   // msg_recv text (empty otherwise)
    uint8_t  body_len = 0;
};

}  // namespace meshroute
