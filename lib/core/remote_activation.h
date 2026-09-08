// MeshRoute — lib/core/remote_activation.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// R-RA-20/23: first-hop TERMINAL{scheduled} budget. Pure; no scheduling consumer in Slice 7a.
#pragma once
#include "airtime.h"
#include "frame_codec.h"
#include "mac_wait_windows.h"
#include "protocol_constants.h"
#include "remote_codec.h"

namespace MESHROUTE_NS {

struct ActivationBudgetInputs {
    uint8_t routing_sf;
    uint8_t data_sf;
    uint32_t bw_hz;
    uint8_t cr;
    uint16_t preamble_sym;
    uint32_t slop_routing_ms;
    uint32_t slop_data_ms;
    uint16_t terminal_inner_len;
    bool crypted_flight;
};

// Same carrier maximum as 0e-C: origin + both hashes + full path (count/cursor/ids).
// A typed wrapper uses one hop fewer and spends that byte on its enclosed type, so cannot exceed it.
// The body is the codec's authenticated response envelope + result:u8 + activation_ms:u32 LE.
inline constexpr uint16_t remote_scheduled_terminal_inner_len =
    sizeof(uint8_t) + 2 * sizeof(uint32_t) + 2 * sizeof(uint8_t) + protocol::gw_env_max_hops
    + kRemoteOverheadAuthResponse + sizeof(RemoteTerminal) + sizeof(uint32_t);
inline constexpr uint32_t remote_action_activation_max_ms = protocol::e2e_ack_deadline_xl_ms - 1;

namespace remote_activation_detail {
inline uint32_t bounded_ms(uint64_t n) {
    return n > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(n);
}
}  // namespace remote_activation_detail

inline uint32_t remote_scheduled_reply_path_budget_ms(const ActivationBudgetInputs& in) {
    // Reject invalid PHY before airtime_ms (its shift/division expects validated inputs).
    // This includes an empty data-SF set; never invent a fallback SF.
    if (in.routing_sf < 5 || in.routing_sf > 12 || in.data_sf < 5 || in.data_sf > 12
        || in.bw_hz < 7000 || in.bw_hz > 500000 || in.cr < 5 || in.cr > 8
        || in.terminal_inner_len > protocol::max_payload_bytes_hard_cap) return UINT32_MAX;
    const auto routing_air = [&](uint16_t len) {
        return airtime_ms(in.routing_sf, in.bw_hz, in.cr, in.preamble_sym, len);
    };
    const auto data_air = [&](uint16_t len) {
        return airtime_ms(in.data_sf, in.bw_hz, in.cr, in.preamble_sym, len);
    };
    const uint8_t flags = DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH | DATA_FLAG_CROSS_LAYER
        | (in.crypted_flight ? DATA_FLAG_CRYPTED : 0);
    const size_t terminal_len = data_frame_len(flags, DATA_TYPE_REMOTE_RESP, in.terminal_inner_len);
    if (terminal_len > protocol::lora_max_frame_bytes) return UINT32_MAX;
    const uint32_t rts_air = routing_air(static_cast<uint16_t>(unicast_rts_wire_len(in.crypted_flight)));
    const uint32_t cts_air = routing_air(3);   // ordinary on-air CTS, not terminal_cts_wire_len
    const uint32_t ack_air = routing_air(3);   // MAC's on-air ACK
    const uint64_t cts_base_wide = uint64_t{rts_air}
        + routing_air(static_cast<uint16_t>(terminal_cts_wire_len(in.crypted_flight)));
    if (cts_base_wide > UINT32_MAX) return UINT32_MAX;
    const uint32_t cts_base = static_cast<uint32_t>(cts_base_wide);
    uint64_t cts_wait = 0;
    for (uint16_t attempt = 0; attempt <= protocol::rts_max_retries; ++attempt) {
        const uint8_t shift = attempt < 2 ? static_cast<uint8_t>(attempt) : 2;
        // Range checks only: the MAC helper still owns the actual wait calculation.
        if (in.slop_routing_ms > (UINT32_MAX - 1u) / 2u
            || cts_base > ((UINT32_MAX - 1u - 2u * in.slop_routing_ms) >> shift)) return UINT32_MAX;
        cts_wait += cts_wait_delay_ms(cts_base, static_cast<uint8_t>(attempt), in.slop_routing_ms);
    }
    // Preserve the MAC's ACK length model, distinct from the real on-air frame length above.
    const uint32_t ack_data_air = data_air(static_cast<uint16_t>(18u + in.terminal_inner_len));
    if (uint64_t{ack_data_air} + ack_air + in.slop_data_ms + in.slop_routing_ms + 2u > UINT32_MAX)
        return UINT32_MAX;
    const uint32_t ack_wait = ack_wait_delay_ms(ack_data_air, ack_air, in.slop_data_ms, in.slop_routing_ms);
    const uint64_t total = uint64_t{rts_air} + cts_air + data_air(static_cast<uint16_t>(terminal_len)) + ack_air
        + protocol::cts_to_data_gap_ms
        + uint64_t{protocol::rts_max_retries} * protocol::rts_busy_retry_ms
        + cts_wait + ack_wait + protocol::cascade_requeue_base_ms;
    return remote_activation_detail::bounded_ms(total);
}

inline uint32_t remote_action_activation_min_ms(const ActivationBudgetInputs& in) {
    return remote_scheduled_reply_path_budget_ms(in);
}
inline uint32_t remote_action_activation_default_ms(const ActivationBudgetInputs& in) {
    return remote_activation_detail::bounded_ms(uint64_t{2} * remote_scheduled_reply_path_budget_ms(in));
}
}  // namespace MESHROUTE_NS
