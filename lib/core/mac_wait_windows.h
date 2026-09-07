// MeshRoute — MAC wait-window arithmetic
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Slice 7a-0: extracted from Node::start_rts_timeout / start_ack_timeout.
// Operands remain the caller's flight-derived airtimes and HAL turnaround slops.
// Preserve the MAC's uint32_t arithmetic, including its retry cap and margins;
// admission/overflow policy for a proposed configuration belongs to its caller.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute
#endif

#include <cstdint>

namespace MESHROUTE_NS {

inline constexpr uint32_t cts_wait_delay_ms(uint32_t base_air_ms, uint8_t attempt, uint32_t slop_ms) {
    const uint32_t shift = attempt < 2 ? attempt : 2;
    return (base_air_ms << shift) + 2u * slop_ms + 1u;
}

inline constexpr uint32_t ack_wait_delay_ms(uint32_t data_air_ms, uint32_t ack_air_ms,
                                           uint32_t slop_data_ms, uint32_t slop_routing_ms) {
    return data_air_ms + ack_air_ms + slop_data_ms + slop_routing_ms + 2u;
}

}  // namespace MESHROUTE_NS
