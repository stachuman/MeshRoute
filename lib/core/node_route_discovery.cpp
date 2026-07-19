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
// §team-multihop: team_plane -> the team-PRIVATE ledger (_rreq_seen_team) so a team RREQ can't alias a static one (§18).
bool Node::rreq_seen_recently(uint8_t origin, uint8_t dst, bool team_plane) {
    const uint64_t now    = _hal.now();
    const uint64_t cutoff = (now >= protocol::route_request_seen_ttl_ms) ? now - protocol::route_request_seen_ttl_ms : 0;
#if MR_FEAT_TEAM
    if (team_plane) {
        for (uint8_t i = 0; i < _active->_rreq_seen_team_n; ++i)
            if (_active->_rreq_seen_team[i].origin == origin && _active->_rreq_seen_team[i].dst == dst && _active->_rreq_seen_team[i].t_ms >= cutoff) return true;
        return false;
    }
#else
    (void)team_plane;
#endif
    for (uint8_t i = 0; i < _active->_rreq_seen_n; ++i)
        if (_active->_rreq_seen[i].origin == origin && _active->_rreq_seen[i].dst == dst && _active->_rreq_seen[i].t_ms >= cutoff) return true;
    return false;
}
void Node::mark_rreq_seen(uint8_t origin, uint8_t dst, bool team_plane) {
    const uint64_t now = _hal.now();
#if MR_FEAT_TEAM
    if (team_plane) {
        for (uint8_t i = 0; i < _active->_rreq_seen_team_n; ++i)
            if (_active->_rreq_seen_team[i].origin == origin && _active->_rreq_seen_team[i].dst == dst) { _active->_rreq_seen_team[i].t_ms = now; return; }
        constexpr uint8_t cap_team_seen = static_cast<uint8_t>(sizeof(_active->_rreq_seen_team) / sizeof(_active->_rreq_seen_team[0]));
        if (_active->_rreq_seen_team_n < cap_team_seen) { _active->_rreq_seen_team[_active->_rreq_seen_team_n++] = { origin, dst, now }; return; }
        uint8_t o = 0;
        for (uint8_t i = 1; i < _active->_rreq_seen_team_n; ++i) if (_active->_rreq_seen_team[i].t_ms < _active->_rreq_seen_team[o].t_ms) o = i;
        _active->_rreq_seen_team[o] = { origin, dst, now };
        return;
    }
#else
    (void)team_plane;
#endif
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
bool Node::rreq_rate_ok(uint8_t dst, uint8_t ttl, bool team_plane) {
    const uint64_t now = _hal.now();
#if MR_FEAT_TEAM
    if (team_plane) {                                     // §team-multihop: team-PRIVATE rate ledger (right-sized, back-pressure not LRU)
        for (uint8_t i = 0; i < _active->_rreq_last_team_n; ++i) {
            if (_active->_rreq_last_team[i].dst != dst) continue;
            const bool window_open = (now - _active->_rreq_last_team[i].t_ms) >= protocol::route_request_seen_ttl_ms;
            const bool escalate    = ttl > _active->_rreq_last_team[i].ttl;
            if (!window_open && !escalate) return false;
            _active->_rreq_last_team[i].t_ms = now; _active->_rreq_last_team[i].ttl = ttl; return true;
        }
        constexpr uint8_t cap_team_last = static_cast<uint8_t>(sizeof(_active->_rreq_last_team) / sizeof(_active->_rreq_last_team[0]));
        if (_active->_rreq_last_team_n >= cap_team_last) return false;   // bounded in-flight discovery budget (refuse, don't evict)
        _active->_rreq_last_team[_active->_rreq_last_team_n++] = { dst, ttl, now };
        return true;
    }
#else
    (void)team_plane;
#endif
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

// §F-XL-2: stash a built RREQ-forward frame into the round-robin ring + arm a de-stormed fire (kRreqForwardTimerId+slot).
// Called by BOTH the static and team relay-forward paths — the packed F frame is self-contained (pack_f baked in the
// leaf_id + team scope), so ONE ring serves both planes. Only the RELAY forward is de-stormed; an ORIGINATED RREQ
// (emit_route_request) and an RREP (send_route_reply) tx immediately as before (they are not the same-ms sibling class).
//
// ★ De-storm ONLY a forward that still PROPAGATES (forwarded ttl > 0): a terminal forward (ttl now 0) is not re-sent
// by whoever hears it, so the multi-hop sibling-collision the de-storm targets cannot occur past it — delaying it buys
// nothing and only shifts the event schedule of a timing-fragile scenario. The caller passes the forwarded ttl and
// sends a terminal forward immediately (see the two call sites). The jitter is a rand_range draw, mirroring F-XL-1;
// because a terminal forward is NOT stashed, the draw is only consumed on a propagating forward.
void Node::rreq_forward_stash(const uint8_t* buf, size_t n) {
    if (n == 0 || n > sizeof(_rreq_forward_stash[0].buf)) return;
    const uint8_t slot = _rreq_forward_rr;
    _rreq_forward_rr = static_cast<uint8_t>((_rreq_forward_rr + 1) % kRreqForwardSlots);
    RreqForwardStash& st = _rreq_forward_stash[slot];
    for (size_t i = 0; i < n; ++i) st.buf[i] = buf[i];
    st.len = static_cast<uint8_t>(n);
    const uint32_t jit = static_cast<uint32_t>(_hal.rand_range(protocol::rreq_forward_jitter_min_ms,
                                                               protocol::rreq_forward_jitter_max_ms + 1));
    (void)_hal.after(jit, kRreqForwardTimerId + slot);
}

// §F-XL-2: fire a de-stormed (jittered) rreq_forward from its ring slot. The frame is self-contained (leaf_id / team
// scope packed in), so it tx's regardless of the currently-active layer. A slot with len==0 has already fired / never armed.
void Node::rreq_forward_fire(uint8_t slot) {
    if (slot >= kRreqForwardSlots) return;
    RreqForwardStash& st = _rreq_forward_stash[slot];
    if (st.len == 0) return;
    tx_initiating(st.buf, st.len, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    st.len = 0;
}

// Originate an RREQ for `dst` (Lua emit_route_request). relay=self (we are the first forwarder).
void Node::emit_route_request(uint8_t dst, uint8_t ttl, bool team_plane) {
#if MR_FEAT_TEAM
    if (team_plane) {   // §team-multihop: TEAM-plane RREQ — origin/relay = team_local_id, team_scoped; team-private rate ledger; NO leaf/hosted-mobile/managed guards (a team member is unmanaged + leaf-agnostic)
        if (dst == 0xFF || team_local_id() == 0 || dst == team_local_id()) return;
        if (!rreq_rate_ok(dst, ttl, /*team_plane=*/true)) return;
        f_in tin{};
        tin.leaf_id = _cfg.leaf_id; tin.origin = team_local_id(); tin.is_reply = false;
        tin.dst_id = dst; tin.ttl_or_next_hop = ttl; tin.hops = 0; tin.relay = team_local_id();
        tin.config_hash = cfg_config_hash(); tin.team_scoped = true; tin.team_id = _cfg.team_id;   // config_hash carried but NOT gated on team RX (leaf-agnostic); team_id is the scope
        uint8_t tbuf[16];
        const size_t tn = pack_f(tin, std::span<uint8_t>(tbuf, sizeof(tbuf)));
        if (tn == 0) return;
        MR_TELEMETRY(
            EventField f[] = { { .key = "dst",    .type = EventField::T::i64, .i = dst },
                               { .key = "ttl",    .type = EventField::T::i64, .i = ttl },
                               { .key = "reason", .type = EventField::T::str, .s = "team_no_route" } };
            _hal.emit("r_tx", f, 3); );
        tx_initiating(tbuf, tn, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
        return;
    }
#else
    (void)team_plane;
#endif
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
void Node::send_route_reply(uint8_t origin, uint8_t dst, uint8_t hops_to_dst, bool team_plane) {
    RtEntry* e = rt_find(origin, team_plane ? Plane::TEAM : Plane::AUTO);   // §team-multihop: reverse path on the TEAM plane (_rt_team); static keeps the AUTO default -> byte-identical
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
#if MR_FEAT_TEAM
    if (team_plane) { in.relay = team_local_id(); in.team_scoped = true; in.team_id = _cfg.team_id; }   // §team-multihop: relay = our team id; scope the reply
#else
    (void)team_plane;
#endif
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
    if (f.team_scoped) {   // §team-multihop: a team-scoped F is NEVER handled by the static F body. The drop is UNCONDITIONAL — even on a !MR_FEAT_TEAM (gateway/static-only) build this is a bare drop, so a co-located team's F is never learned into the static _rt or re-flooded on the static plane (the cross-plane leak the separation audit caught). Static F (team_scoped=false) falls through UNCHANGED -> s18-inert.
#if MR_FEAT_TEAM
        handle_f_team(f, meta);   // same-team-only, on _rt_team (full static/other-team separation)
#endif
        return;
    }
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
        if (n) { if (fwd.ttl_or_next_hop > 0) rreq_forward_stash(buf, n);       // §F-XL-2: de-storm a PROPAGATING forward (jitter so sibling relays don't collide same-ms; LBT defers the later)
                 else tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0); }  // a TERMINAL forward (ttl now 0) never re-propagates -> no downstream collision to de-storm -> send now
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

#if MR_FEAT_TEAM
// §team-multihop (spec 2026-07-15 Plane 2): the TEAM-plane F handler. FULL SEPARATION is enforced by the gate below —
// processed ONLY by a same-team, team-DAD'd member (is_mobile + our team_id == f.team_id + team_local_id != 0). A STATIC
// node (team_id==0), a WRONG-team member (team_id != f.team_id), or a non-team mobile DROPS the frame here → a team RREQ/RREP
// never touches the static _rt / _rreq_seen / _rreq_last / _id_bind, and never crosses into another team. The team plane is a
// leaf-AGNOSTIC PHY overlay keyed by team_id (a mixed team spans leaves), so there is NO leaf_id / config_hash gate — team_id
// IS the membership. Mirrors the static handle_f, but origin/dst/relay/next-hop are team_local_ids and every route/dedup op
// runs on the TEAM plane (_rt_team + the team-private ledgers).
void Node::handle_f_team(const f_out& f, const RxMeta& meta) {
    const uint8_t me = team_local_id();
    if (!(_cfg.is_mobile && _cfg.team_id != 0 && f.team_id == _cfg.team_id && me != 0)) return;   // SEPARATION: same-team member only (static / wrong-team / non-team / un-DAD'd -> drop)
    const uint8_t prev = f.relay;
    if (prev == 0xFF || prev == me) return;
    const int16_t snr_q4 = protocol::db_to_q4(meta.snr_db);

    if (!f.is_reply) {                                     // ----------------- team RREQ -----------------
        if (f.origin == me) return;                                                    // our own flood, heard back
        if (f.hops >= _cfg.dv_hop_cap) { MR_EMIT("rreq_drop_hop_cap", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("hops", f.hops)); return; }
        MR_EMIT("rreq_rx", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("ttl", f.ttl_or_next_hop), EF_I("hops", f.hops), EF_S("plane", "team"));
        learn_route_via(f.origin, prev, static_cast<uint8_t>(f.hops + 1), snr_q4, /*team_plane=*/true);   // reverse path -> _rt_team
        if (f.dst_id == me) {                                                          // we are the target
            MR_EMIT("rreq_resolved_self", EF_I("origin", f.origin), EF_S("plane", "team"));
            send_route_reply(f.origin, me, 0, /*team_plane=*/true); return;
        }
        RtEntry* de = rt_find(f.dst_id, Plane::TEAM);                                   // cached team route -> answer
        if (de != nullptr && de->n > 0) {
            MR_EMIT("rreq_resolved_cached", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("hops", de->candidates[0].hops), EF_S("plane", "team"));
            send_route_reply(f.origin, f.dst_id, de->candidates[0].hops, /*team_plane=*/true); return;
        }
        if (rreq_seen_recently(f.origin, f.dst_id, /*team_plane=*/true)) return;        // team flood dedup
        mark_rreq_seen(f.origin, f.dst_id, /*team_plane=*/true);
        if (f.ttl_or_next_hop == 0) return;                                            // TTL exhausted
        MR_EMIT("rreq_forward", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("ttl", static_cast<int64_t>(f.ttl_or_next_hop - 1)), EF_I("hops", static_cast<int64_t>(f.hops + 1)), EF_S("plane", "team"));
        f_in fwd{};
        fwd.leaf_id = _cfg.leaf_id; fwd.origin = f.origin; fwd.is_reply = false;
        fwd.dst_id  = f.dst_id; fwd.ttl_or_next_hop = static_cast<uint8_t>(f.ttl_or_next_hop - 1);
        fwd.hops    = static_cast<uint8_t>(f.hops + 1); fwd.relay = me;
        fwd.config_hash = f.config_hash; fwd.team_scoped = true; fwd.team_id = f.team_id;   // preserve the TEAM scope across the forward
        uint8_t buf[16];
        const size_t n = pack_f(fwd, std::span<uint8_t>(buf, sizeof(buf)));
        if (n) { if (fwd.ttl_or_next_hop > 0) rreq_forward_stash(buf, n);       // §F-XL-2: same de-storm as the static path (propagating forward only; shared self-contained-frame ring)
                 else tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0); }  // terminal team forward -> send now
    } else {                                               // ----------------- team RREP -----------------
        if (f.ttl_or_next_hop != me) return;                                           // unicast: only the addressed next-hop acts
        if (f.hops > static_cast<uint8_t>(2 * _cfg.dv_hop_cap)) { MR_EMIT("rrep_drop_hop_cap", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("hops", f.hops)); return; }
        MR_EMIT("rrep_rx", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("hops", f.hops), EF_S("plane", "team"));
        learn_route_via(f.dst_id, prev, static_cast<uint8_t>(f.hops + 1), snr_q4, /*team_plane=*/true);   // forward path -> _rt_team
        if (f.origin == me) { MR_EMIT("rrep_arrived", EF_I("dst", f.dst_id), EF_S("plane", "team")); return; }   // we asked -> the team route is installed (drains the parked send)
        RtEntry* rev = rt_find(f.origin, Plane::TEAM);                                  // loop-breaker: reverse next-hop == prev -> a ping-pong
        if (rev != nullptr && rev->n > 0 && rev->candidates[0].next_hop == prev) { MR_EMIT("rrep_drop_loopback", EF_I("origin", f.origin), EF_I("dst", f.dst_id), EF_I("via", prev)); return; }
        send_route_reply(f.origin, f.dst_id, static_cast<uint8_t>(f.hops + 1), /*team_plane=*/true);   // relay onward
    }
}
#endif

}  // namespace meshroute
