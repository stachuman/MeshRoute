// MeshRoute — lib/core/node.h
//
// The protocol node. Depends ONLY on hal.h (no Arduino/RadioLib/sol/json), so
// it runs unchanged on both HAL backends: FirmwareNode in the simulator and the
// MeshCore-PHY device backend. Bounded, fixed-size state (no heap in hot paths).
//
// S2 ships a SKELETON: on_init arms one timer; on_timer TXes a RAW PLACEHOLDER
// frame (not a real protocol frame — codec is the C-track) and emits mr_node_tx;
// on_recv emits mr_node_rx. The remaining callbacks are no-ops until the
// behaviour track (R1+). This exercises the full Node↔Hal↔SimRadio byte path.
#pragma once
#include "hal.h"
#include <cstddef>
#include <cstdint>

namespace meshroute {

// POD; no heap, no JSON. Only the T/F-class knobs the Lua on_init reads.
// PROTOCOL constants stay in protocol_constants.h (hardcoded on device).
struct NodeConfig {
    bool     is_gateway          = false;
    bool     is_mobile           = false;
    bool     join_required       = false;
    bool     req_sync_on_boot    = true;
    bool     seen_bitmap_enabled = true;
    uint8_t  routing_sf          = 7;
    uint16_t allowed_sf_bitmap   = (1u << 12);   // dv_dual_sf sf_set_to_bitmap
    uint32_t beacon_period_ms    = 900000;
    uint32_t beacon_max_idle_ms  = 900000;
    uint8_t  req_sync_min_routes = 8;
};

class Node {
public:
    Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name = nullptr);

    void on_init(const NodeConfig& cfg);                                 // cfg borrowed
    void on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta);  // bytes valid during call only
    void on_timer(uint32_t timer_id);                                    // dispatch on Node-owned id
    void on_radio_busy(const BusyInfo& info);                            // deferred-TX retry/giveup
    void on_preamble_detected(uint64_t time_ms);                         // SX1262 IRQ / throttle witness
    void on_command(const char* cmd, char* out_reply, size_t reply_cap); // status written to out_reply

private:
    Hal&     _hal;
    uint8_t  _node_id;            // reassignable via _hal.set_protocol_id (join/lease)
    uint32_t _key_hash32;         // stable long identity
    // ... bounded fixed-size protocol state, sized by protocol_constants.h caps (R1+) ...
};

}  // namespace meshroute
