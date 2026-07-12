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

#include <cstdio>   // snprintf for the table_cap_hit "dst:N" key (Lua parity)

namespace MESHROUTE_NS {

// Relay-side flood dedup (Lua route_request_seen; key origin|dst; route_request_seen_ttl_ms window).
bool Node::rreq_seen_recently(uint8_t origin, uint8_t dst) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::route_request_seen_ttl_ms) ? now - protocol::route_request_seen_ttl_ms : 0;
    for (uint8_t i = 0; i < _active->_rreq_seen_n; ++i)
        if (_active->_rreq_seen[i].origin == origin && _active->_rreq_seen[i].dst == dst && _active->_rreq_seen[i].t_ms >= cutoff) return true;
    return false;
}
void Node::mark_rreq_seen(uint8_t origin, uint8_t dst) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_rreq_seen_n; ++i)
        if (_active->_rreq_seen[i].origin == origin && _active->_rreq_seen[i].dst == dst) { _active->_rreq_seen[i].t_ms = now; return; }
    if (_active->_rreq_seen_n < protocol::cap_route_request_seen) {
        _active->_rreq_seen[_active->_rreq_seen_n++] = { origin, dst, now };
    } else {                                              // ring full -> evict the oldest
        uint8_t o = 0;
        for (uint8_t i = 1; i < _active->_rreq_seen_n; ++i) if (_active->_rreq_seen[i].t_ms < _active->_rreq_seen[o].t_ms) o = i;
        _active->_rreq_seen[o] = { origin, dst, now };
    }
}

// Originator-side rate limit (Lua route_request_last): suppress a re-flood of the same dst within
// the window UNLESS the TTL escalates (the ttl=1 probe -> dv_hop_cap requery is always allowed).
bool Node::rreq_rate_ok(uint8_t dst, uint8_t ttl) {
    const uint64_t now = _hal.now();
    for (uint8_t i = 0; i < _active->_rreq_last_n; ++i) {
        if (_active->_rreq_last[i].dst != dst) continue;
        const bool window_open = (now - _active->_rreq_last[i].t_ms) >= protocol::route_request_seen_ttl_ms;
        const bool escalate    = ttl > _active->_rreq_last[i].ttl;
        if (!window_open && !escalate) return false;      // recent + same/lower ttl -> suppress
        _active->_rreq_last[i].t_ms = now; _active->_rreq_last[i].ttl = ttl; return true;
    }
    // NEW dst. Full table -> REFUSE the new dst (Lua route_request_last cap, table_cap_hit "refuse"),
    // NOT evict-oldest: a bounded in-flight-discovery budget is back-pressure, not LRU churn.
    if (_active->_rreq_last_n >= _cfg.cap_route_request_last) {
        MR_TELEMETRY(
            char keybuf[12]; std::snprintf(keybuf, sizeof(keybuf), "dst:%u", static_cast<unsigned>(dst));
            EventField f[] = { { .key = "table",  .type = EventField::T::str, .s = "route_request_last" },
                               { .key = "cap",    .type = EventField::T::i64, .i = _cfg.cap_route_request_last },
                               { .key = "size",   .type = EventField::T::i64, .i = _active->_rreq_last_n },
                               { .key = "action", .type = EventField::T::str, .s = "refuse" },
                               { .key = "key",    .type = EventField::T::str, .s = keybuf } };
            _hal.emit("table_cap_hit", f, 5);
        );
        return false;
    }
    _active->_rreq_last[_active->_rreq_last_n++] = { dst, ttl, now };
    return true;
}

// Originate an RREQ for `dst` (Lua emit_route_request). relay=self (we are the first forwarder).
void Node::emit_route_request(uint8_t dst, uint8_t ttl) {
    if (dst == 0xFF || dst == _node_id) return;
    // §mobile (2026-07-11): NEVER RREQ a HOSTED mobile's LOCAL id. A mobile local id is INVISIBLE to the static plane, so the
    // RREQ can never resolve -> it re-floods forever (the bench airtime storm + the deep path that overflowed the loop stack).
    // A hosted mobile is reached by the last-mile (addr_len=1), not route discovery. Defense-in-depth: the delegate model
    // already keeps a mobile off the hash-locate plane. Inert for a non-host (_mobile_reg_n==0).
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].mobile_local_id == dst) return;
    if (!leaf_config_synced()) return;                            // R6.1 §6.4: an un-synced managed joiner must not flood F
    if (!rreq_rate_ok(dst, ttl)) return;
    f_in in{};
    in.leaf_id = _cfg.leaf_id; in.origin = _node_id; in.is_reply = false;
    in.dst_id  = dst; in.ttl_or_next_hop = ttl; in.hops = 0; in.relay = _node_id;
    in.config_hash = cfg_config_hash();                            // R6.1 §6.4: stamp our leaf fingerprint
    uint8_t buf[16];
    const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "dst",    .type = EventField::T::i64, .i = dst },
                           { .key = "ttl",    .type = EventField::T::i64, .i = ttl },
                           { .key = "reason", .type = EventField::T::str, .s = "no_route" } };   // both call sites are no-route triggers (Lua dv:5689/6920 pass "no_route")
        _hal.emit("r_tx", f, 3);
    );
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// Send an RREP back toward `origin` along the reverse path (Lua send_route_reply).
void Node::send_route_reply(uint8_t origin, uint8_t dst, uint8_t hops_to_dst) {
    RtEntry* e = rt_find(origin);
    if (e == nullptr || e->n == 0) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = origin } };
            _hal.emit("rrep_drop_no_reverse", f, 1);
        );
        return;
    }
    const uint8_t next_hop = e->candidates[0].next_hop;
    f_in in{};
    in.leaf_id = _cfg.leaf_id; in.origin = origin; in.is_reply = true;
    in.dst_id  = dst; in.ttl_or_next_hop = next_hop; in.hops = hops_to_dst; in.relay = _node_id;
    in.config_hash = cfg_config_hash();                            // R6.1 §6.4: stamp our leaf fingerprint
    uint8_t buf[16];
    const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_TELEMETRY(
        EventField f[] = { { .key = "origin", .type = EventField::T::i64, .i = origin },
                           { .key = "dst",    .type = EventField::T::i64, .i = dst },
                           { .key = "next",   .type = EventField::T::i64, .i = next_hop } };
        _hal.emit("rrep", f, 3);
    );
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// F frame handler (Lua on_recv F path). prev = the on-wire immediate forwarder (decision b).
void Node::handle_f(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pf = parse_f(std::span<const uint8_t>(bytes, len));
    if (!pf) return;
    const f_out& f = *pf;
    if (f.leaf_id != _cfg.leaf_id) return;
    // §mobile (2026-07-11): a MOBILE is a LEAF — it does NOT participate in static route discovery (F: RREQ/RREP). It never
    // relays a flood, never learns a static reverse-path, and never RREPs its own (static-invisible) LOCAL id. It reaches the
    // mesh via its home (learned from registration + beacons, not RREQ); the team plane routes via _rt_team (beacons), not F.
    // Same leaf-principle as the H-flood guard (node_hashlocate.cpp). A static node (is_mobile=false) is unchanged.
    if (_cfg.is_mobile) return;
    // R6.1 §6.4: the membership gate must cover F (route-discovery flood is the bypass around the beacon gate). Drop +
    // do-NOT-relay an F whose leaf fingerprint diverges from ours -> contains a misconfigured node's flood to 1 hop.
    // config_hash==0 = "no fingerprint" (BLAKE2b never yields 0; only an unprovisioned/legacy F) -> no gate (legacy).
    if (f.config_hash != 0 && f.config_hash != cfg_config_hash()) {
        MR_EMIT("leaf_config_conflict", EF_I("src", f.relay), EF_I("origin", f.origin), EF_S("frame", "F"),
                EF_I("their_hash", static_cast<int64_t>(f.config_hash)), EF_I("my_hash", static_cast<int64_t>(cfg_config_hash())));
        return;
    }
    const uint8_t prev = f.relay;
    if (prev == 0xFF || prev == _node_id) return;
    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);

    if (!f.is_reply) {                                     // ----------------- RREQ -----------------
        if (f.origin == _node_id) return;                  // our own flood, heard back
        // M4: f.hops is an UNAUTHENTICATED wire byte. learn_route_via below stores hops = f.hops+1, so a forged
        // f.hops==255 wraps (uint8) to a 0-hop entry that OUT-RANKS every real route (rt sort is hops-ascending)
        // AND re-seeds on each re-flood = network-wide poison from one crafted frame. Gate at the top (before
        // learn_route_via AND the re-flood) exactly like the RREP dv_hop_cap backstop below. A legitimate RREQ's
        // hops can't reach dv_hop_cap (it's the TTL bound) — beyond that it's forged/looped -> drop, don't learn.
        if (f.hops >= _cfg.dv_hop_cap) {
            MR_EMIT("rreq_drop_hop_cap", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("hops", f.hops));
            return;
        }
        MR_TELEMETRY(
            EventField f2[] = { { .key = "origin", .type = EventField::T::i64, .i = f.origin },
                                { .key = "dst",    .type = EventField::T::i64, .i = f.dst_id },
                                { .key = "ttl",    .type = EventField::T::i64, .i = f.ttl_or_next_hop },
                                { .key = "hops",   .type = EventField::T::i64, .i = f.hops } };
            _hal.emit("rreq_rx", f2, 4); );                                            // dv:11689
        learn_route_via(f.origin, prev, static_cast<uint8_t>(f.hops + 1), snr_q4);     // reverse path
        if (f.dst_id == _node_id) {                                                    // we are the target
            MR_TELEMETRY(
                EventField f2[] = { { .key = "origin", .type = EventField::T::i64, .i = f.origin } };
                _hal.emit("rreq_resolved_self", f2, 1); );                             // dv:11705
            send_route_reply(f.origin, _node_id, 0); return;
        }
        RtEntry* de = rt_find(f.dst_id);                                               // cached route -> answer
        if (de != nullptr && de->n > 0) {
            MR_TELEMETRY(
                EventField f2[] = { { .key = "origin", .type = EventField::T::i64, .i = f.origin },
                                    { .key = "dst",    .type = EventField::T::i64, .i = f.dst_id },
                                    { .key = "hops",   .type = EventField::T::i64, .i = de->candidates[0].hops } };
                _hal.emit("rreq_resolved_cached", f2, 3); );                           // dv:11715
            send_route_reply(f.origin, f.dst_id, de->candidates[0].hops); return;
        }
        if (rreq_seen_recently(f.origin, f.dst_id)) return;                            // flood dedup
        mark_rreq_seen(f.origin, f.dst_id);
        if (f.ttl_or_next_hop == 0) return;                                            // TTL exhausted
        MR_TELEMETRY(
            EventField f2[] = { { .key = "origin", .type = EventField::T::i64, .i = f.origin },
                                { .key = "dst",    .type = EventField::T::i64, .i = f.dst_id },
                                { .key = "ttl",    .type = EventField::T::i64, .i = static_cast<int64_t>(f.ttl_or_next_hop - 1) },
                                { .key = "hops",   .type = EventField::T::i64, .i = static_cast<int64_t>(f.hops + 1) } };
            _hal.emit("rreq_forward", f2, 4); );                                       // dv:11728
        f_in fwd{};
        fwd.leaf_id = _cfg.leaf_id; fwd.origin = f.origin; fwd.is_reply = false;
        fwd.dst_id  = f.dst_id; fwd.ttl_or_next_hop = static_cast<uint8_t>(f.ttl_or_next_hop - 1);
        fwd.hops    = static_cast<uint8_t>(f.hops + 1); fwd.relay = _node_id;
        fwd.config_hash = f.config_hash;                          // preserve the originator's fingerprint (gate-passed -> == ours)
        uint8_t buf[16];
        const size_t n = pack_f(fwd, std::span<uint8_t>(buf, sizeof(buf)));
        if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    } else {                                               // ----------------- RREP -----------------
        if (f.ttl_or_next_hop != _node_id) return;         // unicast: only the addressed next-hop acts
        // Loop/over-cap backstop: unlike the RREQ (TTL-bounded + rreq_seen-deduped), the unicast RREP relay had NO
        // bound. An inconsistent reverse path (A->origin via B, B->origin via A — e.g. after a link drop the routes
        // disagree) ping-pongs the reply forever, `hops` climbing unbounded (observed 90+ on metal -> an F storm).
        // f.hops accumulates the answerer's distance-to-dst (<= dv_hop_cap, a cached route) PLUS the reverse hops back
        // to origin (<= dv_hop_cap, the RREQ TTL), so a VALID reply can legitimately reach ~2x dv_hop_cap (far-cacher
        // long-alt routes — seen in s18). Only beyond that is it necessarily a loop -> drop. (A tighter, hop-independent
        // bound needs an RREP dedup; this is just the unbounded-loop safety net — the user's metal loop hit 90+.)
        if (f.hops > static_cast<uint8_t>(2 * _cfg.dv_hop_cap)) {
            MR_EMIT("rrep_drop_hop_cap", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("hops", f.hops));
            return;
        }
        MR_TELEMETRY(
            EventField f2[] = { { .key = "origin", .type = EventField::T::i64, .i = f.origin },
                                { .key = "dst",    .type = EventField::T::i64, .i = f.dst_id },
                                { .key = "hops",   .type = EventField::T::i64, .i = f.hops } };
            _hal.emit("rrep_rx", f2, 3); );                                            // dv:11743
        learn_route_via(f.dst_id, prev, static_cast<uint8_t>(f.hops + 1), snr_q4);     // forward path
        if (f.origin == _node_id) {                        // we asked -> the route to dst is now installed
            MR_TELEMETRY(
                EventField ev[] = { { .key = "dst", .type = EventField::T::i64, .i = f.dst_id } };
                _hal.emit("rrep_arrived", ev, 1); );       // the armed 1s deferred-send drain flies the send
            return;
        }
        // Loop-breaker (the real cure; the hop-cap above is the backstop for longer cycles): if our reverse route to
        // the origin points BACK at `prev` — the node we just received this reply from — relaying bounces it straight
        // back = a 2-node ping-pong (the inconsistent-route loop after a link drop). A LEGITIMATE relay never has
        // next-hop == prev (that would mean the path to the origin runs back toward the dst), so this drops only loops
        // — no false positives, unlike a by-hops dedup which can't tell a long alt-path reply from a loop.
        RtEntry* rev = rt_find(f.origin);
        if (rev != nullptr && rev->n > 0 && rev->candidates[0].next_hop == prev) {
            MR_EMIT("rrep_drop_loopback", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("via", prev));
            return;
        }
        send_route_reply(f.origin, f.dst_id, static_cast<uint8_t>(f.hops + 1));        // relay onward
    }
}

}  // namespace meshroute
