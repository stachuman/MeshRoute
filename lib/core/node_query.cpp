// MeshRoute — lib/core/node_query.cpp  (Q-frame REQ_SYNC route-bootstrap plane)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Q REQ_SYNC (cmd-nibble 0x6, opcode 1): a route-starved node still in discovery broadcasts a
// REQ_SYNC during boot; any neighbour answers — after a jittered backoff — with a FULL-table
// "sync" beacon, so the joiner pulls the mesh's routing state instead of waiting out the slow
// periodic-beacon rotation. Mirrors the Lua send_req_sync_q / handle_q "Q" path /
// schedule_sync_response (dv_dual_sf.lua:8032 / 8064 / 11767), with two deliberate device
// improvements over the Lua BASELINE — both draw-equivalent below their caps, which realistic
// neighbourhoods never reach: (1) the responder dedup is a fixed evict-oldest ring (Lua: an
// unbounded, never-pruned table that refuses when full); (2) the pending responses live in a
// bounded slot ring fired by one timer-id each (Lua: an unbounded table of after()-closures).
//
// The ONLY rand draw in this plane is the schedule_sync_response backoff — made at the Lua's
// EXACT gate-order (after enabled / min-routes / one-per-requester, before storage) so the
// Lua/C++ mt19937 streams stay aligned; the differential gates depend on it. The neighbour-learn
// in handle_q fires the triggered beacon exactly like the MAC handlers (the Lua's learn_rx_source
// schedules it internally in learn_direct_from_frame), so that jitter draw aligns too.
// Part of Node (declared in node.h).
#include "node.h"
#include "frame_codec.h"
#include "wire.h"          // wire::cmd_byte / Cmd::CFG / flags_of — the C config frame header
#include "leaf_config.h"   // CConfig + pack/parse_c_config + leaf_config_hash + duty_to_bp/bp_to_duty

namespace MESHROUTE_NS {

// ---- responder dedup ring (Lua q_responded_to; key opcode|src|dest, ttl q_respond_ttl_ms) ------
// REQ_SYNC carries no key_hash32 (that was the removed HASH_QUERY field), so the Lua key's
// key_hash32 term is always 0 here — we key on (opcode, src, dest) only.
bool Node::q_responded_recently(uint8_t opcode, uint8_t src, uint8_t dest) {
    return recent_ring_hit(_active->_q_responded, _active->_q_responded_n,
                           QResponded{ opcode, src, dest, 0 }, _hal.now(), protocol::q_respond_ttl_ms);
}
// NB the shared ring evicts the oldest when full where the Lua REFUSES — equivalent below cap, and
// more robust for a long-running device (the F-dedup idiom).
void Node::mark_q_responded(uint8_t opcode, uint8_t src, uint8_t dest) {
    recent_ring_mark(_active->_q_responded, _active->_q_responded_n,
                     QResponded{ opcode, src, dest, _hal.now() });
}

// ---- originator (Lua send_req_sync_q dv:8032; NO rand draw) -------------------------------------
// §team-parity T0/T4: `team_plane` threads the plane INTO the originator so the team-scoped pull reuses this ONE
// function instead of forking a second (U1). ✔ T4 WIRED IT: node_mac.cpp's originator antidote now passes the flight's
// own plane, and a team-scoped pull airs `q_opcode::team_sync` + the 4-B team_id tail. The other three call sites
// (node_query.cpp boot discovery, node_mac.cpp's gw-relay pull, the native test helper) still pass false.
void Node::send_req_sync_q(const char* reason, bool force, bool team_plane) {
    (void)reason;                                              // sim-debug log string only (the Lua logs it)
    // §P0 (mirrors emit_beacon's id-0 guard): an UNPROVISIONED node (id 0) must NEVER REQ_SYNC — its src would be
    // the reserved sentinel 0, so receivers learn a route to "0" (which then propagates) + schedule a sync-response
    // addressed to 0. A node REQ_SYNCs only once it has claimed a short id (boot discovery LISTENs + DADs first).
    // The force path (gw-relay no-route reactive pull) is gateway-only -> always id != 0, so it is unaffected.
    // ✔ §team-parity T4: an OFF-GRID team member has _node_id == 0 (it never registered with a static host — s29's T3,
    // s23's whole chain) but a team-DAD'd _team_local_id, and on the team plane THAT is the id the pull travels under,
    // so the reserved-sentinel argument above does not reach it. The carve-out is copied in shape from emit_beacon's
    // own (node_beacon.cpp:251), which exists for exactly this node (U1).
    // Static reduction: team_plane==false at the three static call sites ⇒ `_node_id == 0`, the pre-T4 expression.
    // Build profiles: under MR_FEAT_TEAM 0 (the three gateway_* envs) team_local_id() is the node.h:197 stub returning
    // 0, so the added conjunct folds to a compile-time false and the guard is TEXTUALLY the pre-T4 one there.
    if (_node_id == 0 && !(team_plane && _cfg.is_mobile && _cfg.team_id != 0 && team_local_id() != 0)) return;
    // §mobile Option A: a MOBILE never route-bootstraps on the static plane — its src is a home-assigned LOCAL id (a REQ_SYNC
    // broadcast would leak it into every static _rt, the dest=17 bench bug). A mobile reaches the mesh via its home (route
    // learned from registration + beacons); it needs no full-table pull. The force path is gateway-only (never mobile).
    // §team-parity T0: the refusal is now PLANE-SCOPED — it forbids a mobile from pulling on the STATIC plane, which
    // is exactly what the comment above argues for.
    // ✔ §team-parity T4 (§3/T4) FILLS THE HOLE T0 OPENED. The static refusal stands unchanged; a TEAM-scoped pull is a
    // different frame on a different plane — src is the team_local_id, the scope is the team_id on the wire, and
    // handle_q's I7 gate means only same-team members ever answer — so nothing about it can reach a static `_rt`.
    // The membership predicate is the SAME one `send -t` (node.cpp:1138) and the team beacon (node_beacon.cpp:378)
    // use — U1, ONE definition of "this node is on the team plane".
    // ★ C2: a team-scoped pull REFUSES when this node is not team-ready. It must never silently DOWNGRADE to a static
    // REQ_SYNC — that would air our node_id into every static _rt, exactly the bug the paragraph above prevents.
    // Static reduction: team_plane==false ⇒ only the `else if` survives ⇒ `_cfg.is_mobile`, the pre-T0 expression, on
    // every build profile. Under MR_FEAT_TEAM 0 the `if` body's predicate is compile-time false ⇒ a bare return.
    if (team_plane) { if (!(_cfg.is_mobile && _cfg.team_id != 0 && team_local_id() != 0)) return; }
    else if (_cfg.is_mobile) return;
    if (!force && !_cfg.req_sync_on_boot) return;
    const uint64_t now = _hal.now();
    // ⚠ §team-parity T4 — `_last_req_sync_tx_ms` is deliberately NOT plane-split, and the reason is that the two
    // populations are DISJOINT by construction: a mobile can never originate a static pull and a non-mobile can never
    // originate a team pull (the guard directly above), and the gw-relay force path is gateway-only. No node ever
    // originates on both planes ⇒ no cross-plane aliasing on this timestamp. If a future slice gives a static node a
    // team pull — or makes a gateway team-capable — this MUST become a per-plane pair.
    if (_last_req_sync_tx_ms != 0 && (now - _last_req_sync_tx_ms) < protocol::req_sync_retry_ms) return;
    // ⚠ §team-parity T4 — MISSING, deliberately: this route-rich skip reads the STATIC `_rt_count` on both planes. It
    // is UNREACHABLE on the team plane today (the one team caller, node_mac.cpp's originator antidote, always passes
    // force=true), so plane-splitting it now would be dead code. It becomes WRONG the moment a non-force team caller
    // exists: an off-grid member's `_rt_count` is 0 forever and says nothing about its `_rt_team`. Whoever adds that
    // caller must split this read. (`_cfg.req_sync_min_routes` defaults to 0, the other reason nothing observes it.)
    if (!force && _active->_rt_count >= _cfg.req_sync_min_routes) return;  // route-rich -> no need (force: missing THIS route, ask anyway)
    _last_req_sync_tx_ms = now;
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.dest = 0xFF;   // broadcast
    // ✔ §team-parity T4: a team-scoped pull travels under our TEAM id — the id every teammate's `_team_peer` /
    // `_rt_team` keys on and the id the team beacon's own src carries (node_beacon.cpp:286). Airing `_node_id` here
    // would be the mixed-id leak: a HOMED member's node_id is a home-assigned STATIC-plane local id that §18-collides
    // a teammate's team id. Static reduction: team_plane==false ⇒ `_node_id`, the pre-T4 expression.
    in.src = team_plane ? team_local_id() : _node_id;
    in.opcode = team_plane ? q_opcode::team_sync : q_opcode::req_sync; in.mobile = _cfg.is_mobile;
    if (team_plane) in.team_id = _cfg.team_id;                      // the scope; pack_q REFUSES a team_sync with team_id==0 (C2)
    uint8_t buf[8];                                                 // req_sync = 4 B; team_sync = 8 B (4 + the team_id tail)
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    // ⚠ `rt_total` stays the STATIC count on BOTH planes on purpose: adding a field to this emit — or changing its
    // meaning — would rewrite every static scenario's q_tx line. The `opcode` field already discriminates the planes
    // (1 = static REQ_SYNC, 0 = TEAM_SYNC), which is all the forensics need.
    MR_EMIT("q_tx", EF_I("opcode", static_cast<uint8_t>(in.opcode)), EF_I("rt_total", _active->_rt_count),
            EF_I("requester_mobile", _cfg.is_mobile ? 1 : 0));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// ---- boot loop (Lua req_sync_loop dv:9167; first fire armed at +req_sync_listen_ms in on_init) --
void Node::req_sync_loop_fire() {
    if (!in_discovery()) return;
    send_req_sync_q("discovery");
    if (in_discovery() && _active->_rt_count < _cfg.req_sync_min_routes)
        (void)_hal.after(protocol::req_sync_retry_ms, kReqSyncTimerId);
}

// ---- Q RX dispatch (Lua handle_q "Q" path dv:11767) --------------------------------------------
void Node::handle_q(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pq = parse_q(std::span<const uint8_t>(bytes, len));
    if (!pq) return;
    const q_out& q = *pq;
    // ★★ §team-parity T4 — INVARIANT I7, and THE ONE definition of "this TEAM_SYNC Q is ours". The leaf-exemption,
    // the loop guard and the admission drop below all read THIS single bool, so they can never drift apart (U1).
    // Deliberately NOT wrapped in `#if MR_FEAT_TEAM`: `same_team` is ungated (node.h:157) and `team_local_id()` is the
    // node.h:197 stub returning 0 under MR_FEAT_TEAM 0, so on the three gateway_* envs the conjunction folds to a
    // COMPILE-TIME false — such a build exempts nothing and drops every team_sync at the admission gate. Same shape as
    // handle_f's team_scoped drop (node_route_discovery.cpp:213), where the `return` is likewise outside the #if.
    // Static reduction: `same_team()` is `_cfg.team_id != 0 && …`, false for every static node and every lone mobile
    // ⇒ team_sync_for_us == false ⇒ all three uses below reduce to their pre-T4 expressions, verbatim.
    const bool team_sync_for_us = q.opcode == static_cast<uint8_t>(q_opcode::team_sync)
                                  && _cfg.is_mobile && same_team(q.team_id) && team_local_id() != 0;
    // §P2-1 (mixed-leaf team), extended to the Q plane by T4: a same-team TEAM_SYNC is leaf-EXEMPT exactly as a
    // same-team beacon is (ingest_beacon, node_beacon.cpp:495 — U1). A mixed-leaf team spans nibbles by design
    // (node_beacon.cpp:491-515), s29 runs one, and the bench config that shipped to metal was leaf 4 vs leaf 7 — without
    // this the pull is answered ONLY by teammates that happen to share our nibble, i.e. the mechanism half-works on
    // precisely the deployed configuration. Every other Q kind keeps the unconditional drop.
    // ⚠ REPORTED, NOT FIXED (pre-existing, C1): `channel_pull` — the OTHER team Q, node_channel.cpp:524/1181 — is
    // still dropped here on a foreign nibble, and it also airs `src = _node_id` rather than the team id. Same gap,
    // different frame; it belongs to whoever owns the team-channel plane, not to T4.
    if (q.leaf_id != _cfg.leaf_id && !team_sync_for_us) return;   // cross-network filter — drop foreign Q first
    // Learn the Q sender as a 1-hop neighbour (Lua learn_rx_source -> learn_direct_from_frame, which
    // fires the triggered beacon internally on a real learn; self / invalid id are no-ops inside).
    // §mobile: a mobile-marked Q's src is a home-assigned LOCAL id, NOT a global static identity -> NEVER learn it into
    // the static _rt (a mobile is reached via home_id+hash; a local id §18-collides a static id + goes stale as it roams).
    // Mirrors the RTS guard node_mac_rx.cpp:47 (`!r.mobile_src`). q.mobile==false for a static Q -> unchanged (s18 byte-identical).
    // §team-parity T0: plane made explicit (was learn_direct_neighbor's default). Static reduction: `false` IS the old
    // default ⇒ identical call.
    // ✔ §team-parity T2 (§3/T2 row 7) — DONE by the else-arm below; this guard is UNCHANGED (I2).
    if (!q.mobile && learn_direct_neighbor(q.src, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/false)) schedule_triggered_beacon();
#if MR_FEAT_TEAM
    // ✔ §team-parity T2 (§3/T2 row 7): a mobile-marked Q's src is a LOCAL id; when it is a KNOWN teammate's, the Q proves
    // a 1-hop team neighbour. Restricted to is_team_peer(q.src) for the same reason as the RTS row: an unknown
    // mobile-marked src could be a foreign team's or a plain mobile's and must not be admitted to _team_peer.
    // team_id==0 ⇒ _team_peer all-zero ⇒ inert (static byte-identical).
    // ★ THE SPEC IS WRONG THAT THIS "PAIRS WITH T4": it says T4 "is what makes a team Q exist to learn from in the first
    // place". A team Q existed BEFORE T4 — node_channel.cpp:524/1181 send a `channel_pull` Q with `mobile = _cfg.is_mobile`,
    // and s28 carries 17 such receptions (opcode 3) whose src is a team local id. T4 added a SECOND kind (team_sync);
    // it was never a precondition for this row.
    // ✔ V1 UPDATE (T4): T2 wrote here that "the Q frame carries no team id (that tail is T4's)" and that the leaf drop
    // has no same_team exemption. BOTH are now only HALF true, and the halves matter:
    //   • a TEAM_SYNC does carry a team_id and IS leaf-exempt (see team_sync_for_us above), so a mixed-leaf teammate's
    //     team_sync reaches this line and can refresh the team plane;
    //   • a CHANNEL_PULL still carries NO team id, so it is still dropped on a foreign nibble and still cannot be
    //     admitted on src alone. ⚠ THAT gap is unchanged and remains OPEN — it needs a team_id on the pull frame
    //     (a wire change to a second Q shape), which is the team-channel plane's slice, not T4's.
    else if (q.mobile && is_team_peer(q.src)
             && learn_direct_neighbor(q.src, protocol::db_to_q4(meta.snr_db), false, /*team_plane=*/true)) schedule_triggered_beacon();
#endif
    // ✔ §team-parity T4: on the team plane OUR id is `team_local_id()`, not `_node_id` — a HOMED member's `_node_id` is
    // a home-assigned STATIC-plane local id that §18-collides a teammate's team id, so comparing against it would make
    // us silently ignore that teammate's pull (a suppress-direction plane collision, C3).
    // Static reduction: team_sync_for_us==false ⇒ `q.src == _node_id`, the pre-T4 expression.
    if (q.src == (team_sync_for_us ? team_local_id() : _node_id)) return;   // loop guard — never answer ourselves
    // ★★ §team-parity T4 — INVARIANT I7, half two: a TEAM_SYNC that is not ours is DROPPED HERE, BEFORE the responder
    // dedup ring and BEFORE the q_rx emit, so a static node / a foreign team's member / a not-yet-DAD'd member spends
    // NO state on it whatsoever ("a static node ignores the opcode"). The `return` is outside every `#if`, so a
    // MR_FEAT_TEAM 0 gateway build — where team_sync_for_us is compile-time false — drops EVERY team_sync here rather
    // than falling through into the static REQ_SYNC body below. Static reduction: a static node never sees opcode 0
    // (no packer emitted it before T4), so this line is unreachable on the static plane and s18-inert by construction.
    if (q.opcode == static_cast<uint8_t>(q_opcode::team_sync) && !team_sync_for_us) return;
    if (q_responded_recently(q.opcode, q.src, q.dest)) return;   // recently answered this query -> skip
    mark_q_responded(q.opcode, q.src, q.dest);
    MR_EMIT("q_rx", EF_I("from", q.src), EF_I("dest", q.dest), EF_I("opcode", q.opcode), EF_I("requester_mobile", q.mobile ? 1 : 0));
    // ✔ §team-parity T4 (§3/T4): the TEAM-scoped full-table pull. Admission already happened at the I7 gate above, so
    // reaching here means this IS a same-team pull and we ARE a DAD'd member. The response needs NO plane argument:
    // emit_beacon self-selects (team_active ⇒ src = _team_local_id and src_rt = _rt_team, node_beacon.cpp:286/389) and
    // kind=="sync" is the one kind that bypasses dirty_only (:272), so the reply carries our WHOLE `_rt_team` via the
    // Phase-2 rotation — one round trip for a teammate's entire table, which is the point of the slice.
    // ⚠ MISSING, stated plainly: schedule_sync_response's route-starved skip reads the STATIC `_rt_count`
    // (node_query.cpp, the `route_n` line) and its `rt_total` telemetry does too, so on a team pull both describe the
    // wrong plane. Inert today — `_cfg.sync_response_min_routes` defaults to 0 and nothing in the tree sets it — but a
    // deployment that raises it would silently mute off-grid members (their `_rt_count` is 0 forever). Left unsplit
    // rather than adding a plane parameter that only one of two callers would ever vary.
    if (q.opcode == static_cast<uint8_t>(q_opcode::team_sync)) {
        schedule_sync_response(q.src, q.mobile);
        return;
    }
    if (q.opcode == static_cast<uint8_t>(q_opcode::req_sync)) {
        schedule_sync_response(q.src, q.mobile);
        return;
    }
    if (q.opcode == static_cast<uint8_t>(q_opcode::channel_pull)) {
        uint32_t ids[16]; uint8_t nids = 0;                       // pulls carry few ids (usually 1); cap the parse
        for (uint8_t i = 0; i < q.channel_id_count && nids < 16; ++i) {
            const auto cid = parse_q_channel_id(std::span<const uint8_t>(bytes, len), q, i);
            if (cid) ids[nids++] = *cid;
        }
        handle_channel_pull(q.src, q.dest, ids, nids);
        return;
    }
    if (q.opcode == static_cast<uint8_t>(q_opcode::config_pull)) {
        // R6.2 §4.2 durability: ANY member of the requested lineage at >= the requested epoch answers (config lives in
        // every puller, survives the originator leaving). We answer only if WE are a synced member of that lineage.
        if (_cfg.lineage_id != 0 && _cfg.lineage_id == q.pull_lineage &&
            _cfg.config_epoch > 0 && _cfg.config_epoch >= q.pull_epoch)
            send_c_config(q.src);
        return;
    }
    // Any other opcode is unknown -> silent.
}

// ---- R6.2 CONFIG_PULL / C config-answer frame --------------------------------------------------
// 1-hop pull of a leaf's config from a heard member (the joiner/stale node asks directly; needs no F).
void Node::send_config_pull(uint8_t to, uint16_t lineage, uint16_t epoch) {
    if (to == 0 || to == 0xFF) return;
    const uint64_t now = _hal.now();
    if (_last_config_pull_tx_ms != 0 && (now - _last_config_pull_tx_ms) < protocol::config_pull_retry_ms) return;
    _last_config_pull_tx_ms = now;
    q_in in{};
    in.leaf_id = _cfg.leaf_id; in.src = _node_id; in.dest = to;
    in.opcode = q_opcode::config_pull; in.mobile = _cfg.is_mobile;
    in.pull_lineage = lineage; in.pull_epoch = epoch;
    uint8_t buf[8];
    const size_t n = pack_q(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (n == 0) return;
    MR_EMIT("config_pull_tx", EF_I("to", to), EF_I("lineage", lineage), EF_I("epoch", epoch));
    tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// Answer a CONFIG_PULL with a C control frame on routing_sf (cmd 0xB), 1-hop direct to the puller. UNLIKE the old
// routed CONFIG_ANSWER (a DATA needing the RTS/CTS handshake + a data SF), a control-plane frame reaches a joiner that
// has NO data sf_list yet — the whole bootstrap fix. Best-effort: a lost C is re-answered when the puller re-pulls.
void Node::send_c_config(uint8_t to) {
    if (to == 0 || to == 0xFF) return;
    CConfig cc{};
    cc.allowed_sf_bitmap  = _cfg.allowed_sf_bitmap;
    cc.duty_bp            = duty_to_bp(_cfg.duty_cycle);
    cc.active_fraction_bp = frac_to_bp(_cfg.channel_active_fraction);       // anti-spam v2: promote the 3 knobs onto the wire
    cc.ch_interval_ms     = ms_to_u16(_cfg.channel_min_interval_ms);
    cc.dm_interval_ms     = ms_to_u16(_cfg.dm_min_interval_ms);
    cc.config_epoch       = _cfg.config_epoch;
    cc.leaf_name_len      = _cfg.leaf_name_len;
    for (uint8_t i = 0; i < _cfg.leaf_name_len && i < protocol::leaf_name_max; ++i) cc.leaf_name[i] = _cfg.leaf_name[i];
    uint8_t frame[3 + 12 + protocol::leaf_name_max];                        // [cmd|leaf][src][dst] + body (sf·duty·frac·chI·dmI·epoch·name)
    frame[0] = wire::cmd_byte(wire::Cmd::CFG, static_cast<uint8_t>(_cfg.leaf_id & 0x0F));
    frame[1] = _node_id;
    frame[2] = to;
    const size_t bn = pack_c_config(cc, frame + 3, sizeof(frame) - 3);
    if (bn == 0) return;
    MR_EMIT("c_config_tx", EF_I("to", to), EF_I("epoch", cc.config_epoch));
    tx_initiating(frame, 3 + bn, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
}

// C config frame RX (cmd 0xB): header [cmd|leaf][src][dst]. Adopt only if it's addressed to us on our leaf nibble.
// An empty-sf_list joiner reaches HERE (control plane) where the old routed CONFIG_ANSWER could never arrive.
void Node::handle_c(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    if (len < 3) return;
    const uint8_t leaf = wire::flags_of(bytes[0]);
    const uint8_t dst  = bytes[2];
    if (leaf != _cfg.leaf_id) return;                                      // not our leaf nibble
    if (_node_id == 0 || dst != _node_id) return;                          // must be addressed to us (we hold an id)
    adopt_c_config(bytes + 3, len - 3);
}

// Adopt a pulled config from a C-frame body (cfg + recompute hash on next beacon + a persist Push for the device).
// lineage_id is NOT on the wire — it's our target, set when we heard the managed beacon that triggered the pull (so we
// must already have one). Guard: never older than ours; the SAME epoch IS adopted (the §4.1 LWW loser pulls the winner).
void Node::adopt_c_config(const uint8_t* body, size_t len) {
    if (_cfg.lineage_id == 0) return;                                      // no target lineage -> nothing to sync to
    CConfig cc{};
    if (!parse_c_config(body, len, cc)) return;
    if (cc.config_epoch < _cfg.config_epoch) return;                       // not newer -> ignore
    // No-change guard is HASH-based (not just bitmap) so a name/duty-only LWW write at the SAME epoch still adopts.
    const uint16_t incoming_hash = leaf_config_hash(cc.allowed_sf_bitmap, cc.duty_bp, cc.active_fraction_bp,
                                                    cc.ch_interval_ms, cc.dm_interval_ms, cc.leaf_name, cc.leaf_name_len);
    if (_cfg.config_epoch == cc.config_epoch && incoming_hash == cfg_config_hash()) return;
    if (cc.config_epoch > _max_seen_epoch) _max_seen_epoch = cc.config_epoch;   // R6.3: adopting bumps our max-seen
    _cfg.config_epoch = cc.config_epoch;
    _cfg.allowed_sf_bitmap = cc.allowed_sf_bitmap;
    _cfg.duty_cycle = bp_to_duty(cc.duty_bp);
    _cfg.channel_active_fraction = bp_to_frac(cc.active_fraction_bp);       // anti-spam v2: adopt the 3 promoted knobs live
    _cfg.channel_min_interval_ms = cc.ch_interval_ms;
    _cfg.dm_min_interval_ms      = cc.dm_interval_ms;
    recompute_duty_budget();                                                // R6.3 §2(b): adopted duty applies live (no reboot)
    _cfg.leaf_name_len = cc.leaf_name_len;
    for (uint8_t i = 0; i < cc.leaf_name_len && i < protocol::leaf_name_max; ++i) _cfg.leaf_name[i] = cc.leaf_name[i];
    MR_EMIT("leaf_config_adopted", EF_I("lineage", _cfg.lineage_id), EF_I("epoch", cc.config_epoch),
            EF_I("sf_bitmap", cc.allowed_sf_bitmap));
    schedule_triggered_beacon();                                            // re-advertise the adopted config -> propagate
    Push pu{}; pu.kind = PushKind::config_adopted; enqueue_push(pu);        // device: persist to NV
}

// R6.3 §4.1: an OPERATOR config write. The caller has already mutated the leaf fields (allowed_sf_bitmap / duty_cycle /
// leaf_name) via mutable_config(); this commits the change as a deliberate, propagating bump: epoch = max_seen + 1,
// recompute (config_hash is derived on the next beacon), re-advertise, persist. The operator-command gate IS the
// "deliberate intent" marker — a merely-misconfigured node never calls this, so never propagates. Managed leaves only
// (lineage 0 = unmanaged has no epoch plane). LWW (ties -> higher key_hash32) resolves a concurrent same-epoch write
// in the beacon filter. Returns false (no-op) on an unmanaged leaf.
bool Node::leaf_config_write() {
    if (_cfg.lineage_id == 0) return false;                                 // unmanaged -> no epoch plane to propagate within
    uint16_t base = _max_seen_epoch > _cfg.config_epoch ? _max_seen_epoch : _cfg.config_epoch;
    _cfg.config_epoch = static_cast<uint16_t>(base >= 65534 ? 65534 : base + 1);   // saturate: a u16 wrap to 0 makes leaf_config_synced() read false forever
    _max_seen_epoch   = _cfg.config_epoch;
    MR_EMIT("leaf_config_write", EF_I("epoch", _cfg.config_epoch), EF_I("hash", static_cast<int64_t>(cfg_config_hash())));
    schedule_triggered_beacon();                                            // re-advertise immediately -> neighbours go stale -> pull
    Push pu{}; pu.kind = PushKind::config_adopted; enqueue_push(pu);        // device: persist the new {epoch, config}
    return true;
}

// ---- jittered full-table response (Lua schedule_sync_response dv:8064; the ONLY draw) ----------
void Node::schedule_sync_response(uint8_t requester, bool requester_mobile) {
    if (!_cfg.sync_response_enabled) return;
    const uint8_t route_n = _active->_rt_count;
    if (route_n < _cfg.sync_response_min_routes) {              // route-starved responder skip (inert at default min=0)
        MR_EMIT("sync_response_skip", EF_I("joiner", requester), EF_S("reason", "rt_small"), EF_I("rt_total", route_n));
        return;
    }
    // One pending response per requester (Lua sync_response_pending[key]) — BEFORE the draw.
    for (uint8_t i = 0; i < protocol::cap_sync_response_pending; ++i)
        if (_active->_sync_pending[i].active && _active->_sync_pending[i].requester == requester) return;
    // THE DRAW — rand_range(min, max+1) == Lua self:rand(lo, hi+1) (dv:8083). Placed here, at the
    // Lua's exact gate-order, so the streams stay aligned even when the ring is full below (the Lua
    // has no ring — it always draws + stores).
    uint32_t delay = static_cast<uint32_t>(_hal.rand_range(protocol::sync_response_backoff_min_ms,
                                                           protocol::sync_response_backoff_max_ms + 1));
    if (_cfg.is_mobile)   delay += protocol::sync_response_mobile_penalty_ms;             // dv:8085
    if (requester_mobile) delay += protocol::sync_response_requester_mobile_penalty_ms;   // dv:8088
    int slot = -1;
    for (uint8_t i = 0; i < protocol::cap_sync_response_pending; ++i)
        if (!_active->_sync_pending[i].active) { slot = static_cast<int>(i); break; }
    if (slot < 0) {                                            // ring full (device cap; Lua unbounded) — drop AFTER the draw
        MR_EMIT("sync_response_drop_full", EF_I("joiner", requester));
        return;
    }
    const uint64_t now = _hal.now();
    _active->_sync_pending[slot] = { .active = true, .suppressed = false, .requester = requester,
                            .requester_mobile = requester_mobile, .requested_at = now, .fire_at = now + delay };
    MR_EMIT("sync_response_scheduled", EF_I("joiner", requester), EF_I("delay_ms", static_cast<int64_t>(delay)), EF_I("rt_total", route_n),
            EF_I("requester_mobile", requester_mobile ? 1 : 0));
    (void)_hal.after(delay, kSyncResponseTimerId + static_cast<uint32_t>(slot));
}

// ---- response fire (Lua the schedule_sync_response after()-closure body dv:8108) ----------------
void Node::sync_response_fire(uint8_t slot) {
    if (slot >= protocol::cap_sync_response_pending) return;
    SyncPending& p = _active->_sync_pending[slot];
    if (!p.active) return;                                     // already fired / never armed
    p.active = false;
    if (p.suppressed) {                                        // a useful beacon was overheard in-window -> stand down
        MR_EMIT("sync_response_suppressed", EF_I("joiner", p.requester), EF_S("reason", "heard_useful_bcn"));
        return;
    }
    MR_EMIT("sync_response_tx", EF_I("joiner", p.requester), EF_I("rt_total", _active->_rt_count));
    emit_beacon("sync");                                       // full-table page (dirty_only=false for kind=="sync")
}

}  // namespace meshroute
