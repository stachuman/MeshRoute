// MeshRoute — lib/core/node.h
//
// The protocol node. Depends ONLY on hal.h (no Arduino/RadioLib/sol/json), so
// it runs unchanged on both HAL backends: FirmwareNode in the simulator and the
// MeshCore-PHY device backend. Bounded, fixed-size state (no heap in hot paths).
//
// R1 (beacon emit): on_init reads NodeConfig, listens on routing SF, and arms a
// periodic beacon. on_timer emits a §10 BCN (C5 pack_beacon) over each rt[dest]
// primary, then re-arms with ±20% jitter. on_recv ingests beacons (parse_beacon
// → DV route-table merge) and emits beacon_rx / rt_update / rt_full. The MAC /
// data plane (RTS/CTS/DATA/ACK) and the throttle/discovery/triggered machinery
// are later R-iterations — see docs/specs/2026-05-29-r1-beacon-emit-design.md.
#pragma once
#include "hal.h"
#include "protocol_constants.h"
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
    uint32_t quiet_threshold_ms  = 30000;        // beacon throttle gate; <=0 = unthrottled (R1 fast path)
    uint8_t  leaf_id             = 0;            // layer id (single-layer R1 = 0)
    uint16_t peer_count          = 0;            // host-set (N-1); 0 = no rt_full emit (sim telemetry)
};

// One route candidate (DV). Mirrors the Lua rt[dest].candidates[i] fields
// (dv_dual_sf.lua:9646-9654). score is Q4 dB. Budget/suspect penalties are 0
// until later R-iterations, so effective_score == score in R1.
struct RtCandidate {
    uint8_t  next_hop         = 0;
    int16_t  score            = 0;   // Q4 dB
    uint8_t  hops             = 0;
    uint64_t last_seen_ms     = 0;
    uint8_t  n2_hop           = 0;   // advertised next-hop (for the R2 3-cycle prune)
    bool     is_gateway       = false;
    uint8_t  learned_layer_id = 0;
};
struct RtEntry {
    uint8_t     dest = 0;
    RtCandidate candidates[protocol::max_rt_candidates];
    uint8_t     n     = 0;           // candidates in use (1..K)
    bool        dirty = false;       // set when candidates[0] changes (R2/R4 dirty-only beacons)
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
    // ---- beacon emit / ingest (R1) -----------------------------------------
    void emit_beacon();                                            // build + pack + tx + emit + re-arm
    void ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta);
    int16_t route_score_from_snr(int16_t snr_q4) const;            // dv_dual_sf.lua:3053

    // ---- route table (DV merge) --------------------------------------------
    enum class MergeAction : uint8_t { none, new_dest, primary_refresh, promote, alt_install };
    RtEntry*    rt_find(uint8_t dest);
    RtEntry*    rt_insert(uint8_t dest);                           // sorted insert; nullptr if full
    MergeAction rt_merge(uint8_t dest, const RtCandidate& cand);   // dv_dual_sf.lua:4484
    void        sort_candidates(RtEntry& e);
    bool        route_strictly_better(const RtCandidate& a, const RtCandidate& b) const;  // :4227
    void        maybe_emit_rt_full();

    Hal&     _hal;
    uint8_t  _node_id;            // reassignable via _hal.set_protocol_id (join/lease)
    uint32_t _key_hash32;         // stable long identity
    NodeConfig _cfg;             // borrowed copy from on_init
    int16_t  _routing_snr_floor_q4 = 0;   // SF_DEMOD_THRESHOLD[routing_sf] + sf_margin_q4
    RtEntry  _rt[protocol::cap_routes];
    uint8_t  _rt_count = 0;       // distinct dests, kept sorted ascending by dest
    bool     _rt_full_emitted = false;
};

}  // namespace meshroute
