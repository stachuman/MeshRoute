// MeshRoute — lib/core/node_route_discovery.cpp  (F-frame AODV route discovery)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// On-demand same-layer route discovery (AODV-style RREQ/RREP, PROTOCOL §3.7b). When an
// originator has no route to a dst it floods an RREQ (expanding ring: ttl=1 probe, then a
// requery at dv_hop_cap); the RREQ lays the REVERSE path back to the origin at every relay,
// and the dst — or an intermediate node holding a cached route — answers with a unicast RREP
// that lays the FORWARD path on the way home. Mirrors the Lua emit_route_request /
// send_route_reply + the F on_recv path, EXCEPT the next-hop for path learning comes from the
// on-wire `relay` byte (decision b), never the PHY sender — so it works on metal (src_hint=-1).
// Draw-free this phase (the RREP de-storm jitter is deferred). Part of Node (declared in node.h).
#include "node.h"
#include "frame_codec.h"

namespace meshroute {

// Relay-side flood dedup (Lua route_request_seen; key origin|dst; route_request_seen_ttl_ms window).
bool Node::rreq_seen_recently(uint8_t origin, uint8_t dst) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::route_request_seen_ttl_ms) ? now - protocol::route_request_seen_ttl_ms : 0;
    for (uint8_t i = 0; i < _rreq_seen_n; ++i)
        if (_rreq_seen[i].origin == origin && _rreq_seen[i].dst == dst && _rreq_seen[i].t_ms >= cutoff) return true;
    return false;
}
void Node::mark_rreq_seen(uint8_t origin, uint8_t dst) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _rreq_seen_n; ++i)
        if (_rreq_seen[i].origin == origin && _rreq_seen[i].dst == dst) { _rreq_seen[i].t_ms = now; return; }
    if (_rreq_seen_n < protocol::cap_route_request_seen) {
        _rreq_seen[_rreq_seen_n++] = { origin, dst, now };
    } else {                                              // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _rreq_seen_n; ++i) if (_rreq_seen[i].t_ms < _rreq_seen[o].t_ms) o = i;
        _rreq_seen[o] = { origin, dst, now };
    }
}

// Originator-side rate limit (Lua route_request_last): suppress a re-flood of the same dst within
// the window UNLESS the TTL escalates (the ttl=1 probe -> dv_hop_cap requery is always allowed).
bool Node::rreq_rate_ok(uint8_t dst, uint8_t ttl) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _rreq_last_n; ++i) {
        if (_rreq_last[i].dst != dst) continue;
        const bool window_open = (now - _rreq_last[i].t_ms) >= protocol::route_request_seen_ttl_ms;
        const bool escalate    = ttl > _rreq_last[i].ttl;
        if (!window_open && !escalate) return false;      // recent + same/lower ttl -> suppress
        _rreq_last[i].t_ms = now; _rreq_last[i].ttl = ttl; return true;
    }
    if (_rreq_last_n < protocol::cap_route_request_last) {
        _rreq_last[_rreq_last_n++] = { dst, ttl, now };
    } else {
        uint8_t o = 0;
        for (uint8_t i = 1; i < _rreq_last_n; ++i) if (_rreq_last[i].t_ms < _rreq_last[o].t_ms) o = i;
        _rreq_last[o] = { dst, ttl, now };
    }
    return true;
}

// Originate an RREQ for `dst` (Lua emit_route_request). relay=self (we are the first forwarder).
void Node::emit_route_request(uint8_t dst, uint8_t ttl) {
    if (dst == 0xFF || dst == _node_id) return;
    if (!rreq_rate_ok(dst, ttl)) return;
    f_in in{};
    in.leaf_id = _cfg.leaf_id; in.origin = _node_id; in.is_reply = false;
    in.dst_id  = dst; in.ttl_or_next_hop = ttl; in.hops = 0; in.relay = _node_id;
    uint8_t buf[8];
    const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = dst },
                       { .key = "ttl", .type = EventField::T::i64, .i = ttl } };
    _hal.emit("rreq", f, 2);
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// Send an RREP back toward `origin` along the reverse path (Lua send_route_reply).
void Node::send_route_reply(uint8_t origin, uint8_t dst, uint8_t hops_to_dst) {
    RtEntry* e = rt_find(origin);
    if (e == nullptr || e->n == 0) {
        EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = origin } };
        _hal.emit("rrep_drop_no_reverse", f, 1);
        return;
    }
    const uint8_t next_hop = e->candidates[0].next_hop;
    f_in in{};
    in.leaf_id = _cfg.leaf_id; in.origin = origin; in.is_reply = true;
    in.dst_id  = dst; in.ttl_or_next_hop = next_hop; in.hops = hops_to_dst; in.relay = _node_id;
    uint8_t buf[8];
    const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                       { .key = "dst",    .type = EventField::T::i64, .i = dst },
                       { .key = "next",   .type = EventField::T::i64, .i = next_hop } };
    _hal.emit("rrep", f, 3);
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// F frame handler (Lua on_recv F path). prev = the on-wire immediate forwarder (decision b).
void Node::handle_f(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pf = parse_f(std::span<const uint8_t>(bytes, len));
    if (!pf) return;
    const f_out& f = *pf;
    if (f.leaf_id != _cfg.leaf_id) return;
    const uint8_t prev = f.relay;
    if (prev == 0xFF || prev == _node_id) return;
    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);

    if (!f.is_reply) {                                     // ----------------- RREQ -----------------
        if (f.origin == _node_id) return;                  // our own flood, heard back
        learn_route_via(f.origin, prev, static_cast<uint8_t>(f.hops + 1), snr_q4);     // reverse path
        if (f.dst_id == _node_id) { send_route_reply(f.origin, _node_id, 0); return; } // we are the target
        RtEntry* de = rt_find(f.dst_id);                                               // cached route -> answer
        if (de != nullptr && de->n > 0) { send_route_reply(f.origin, f.dst_id, de->candidates[0].hops); return; }
        if (rreq_seen_recently(f.origin, f.dst_id)) return;                            // flood dedup
        mark_rreq_seen(f.origin, f.dst_id);
        if (f.ttl_or_next_hop == 0) return;                                            // TTL exhausted
        f_in fwd{};
        fwd.leaf_id = _cfg.leaf_id; fwd.origin = f.origin; fwd.is_reply = false;
        fwd.dst_id  = f.dst_id; fwd.ttl_or_next_hop = static_cast<uint8_t>(f.ttl_or_next_hop - 1);
        fwd.hops    = static_cast<uint8_t>(f.hops + 1); fwd.relay = _node_id;
        uint8_t buf[8];
        const size_t n = pack_f(fwd, std::span<uint8_t>(buf, sizeof(buf)));
        if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    } else {                                               // ----------------- RREP -----------------
        if (f.ttl_or_next_hop != _node_id) return;         // unicast: only the addressed next-hop acts
        learn_route_via(f.dst_id, prev, static_cast<uint8_t>(f.hops + 1), snr_q4);     // forward path
        if (f.origin == _node_id) {                        // we asked -> the route to dst is now installed
            EventField ev[] = { { .key = "dst", .type = EventField::T::i64, .i = f.dst_id } };
            _hal.emit("rrep_arrived", ev, 1);              // the armed 1s deferred-send drain flies the send
            return;
        }
        send_route_reply(f.origin, f.dst_id, static_cast<uint8_t>(f.hops + 1));        // relay onward
    }
}

}  // namespace meshroute
