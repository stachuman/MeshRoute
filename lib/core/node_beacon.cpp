// MeshRoute — lib/core/node_beacon.cpp  (R1/R2 beacon emit + ingest)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Node methods for the §10 BCN plane: periodic/triggered beacon emit (dirty-only
// differential pages), beacon ingest + DV merge, the discovery fast-cadence exit,
// and the triggered-beacon scheduler. Plus the file-local SNR wire-bucket
// round-trip and the rt_update telemetry helper. Behaviour mirrors dv_dual_sf.lua.
// Part of the Node class (declared in node.h); split out of node.cpp for readability.
#include "node.h"

#include "frame_codec.h"

#include <span>
#include <cstring>    // strcmp — kind=="sync" full-table gate

namespace meshroute {

// Full-page beacon entry cap that fits beacon_max_bytes (8-B header + 4-B/entry).
static constexpr uint8_t kMaxBeaconEntries =
    static_cast<uint8_t>((protocol::beacon_max_bytes - 8) / 4);

// ---- 4-bit SNR wire-bucket round-trip (dv_dual_sf.lua:829-838) --------------
// bucket centers at -19,-17,..,+11 dB; bin width 2 dB = 32 Q4; lower edge -20 dB.
static int bucket_of_snr_4b(int snr_q4) {
    int b = (snr_q4 + 320) / 32;          // == Lua floor-div after the [0,15] clamp
    if (b < 0)  b = 0;
    if (b > 15) b = 15;
    return b;
}
static int16_t snr_of_bucket_4b(int bucket) {
    return static_cast<int16_t>((-19 + bucket * 2) * 16);
}

// rt_update telemetry (dest is the field the gate asserts; the rest aid the S3
// differential). Free function — keeps the two call sites identical.
static void emit_rt_update(Hal& hal, uint8_t dest, uint8_t next,int16_t score_q4, uint8_t hops, const char* slot) {
    MR_EMIT_TO(hal,"rt_update",EF_I("dest",dest),EF_I("next",next),EF_F("score",protocol::q4_to_db(score_q4)),EF_I("hops",hops),EF_S("slot",slot));
    /*EventField f[] = {
        { .key = "dest",  .type = EventField::T::i64, .i = static_cast<int64_t>(dest) },
        { .key = "next",  .type = EventField::T::i64, .i = static_cast<int64_t>(next) },
        { .key = "score", .type = EventField::T::f64, .f = static_cast<double>(protocol::q4_to_db(score_q4)) },
        { .key = "hops",  .type = EventField::T::i64, .i = static_cast<int64_t>(hops) },
        { .key = "slot",  .type = EventField::T::str, .s = slot },
    };
    hal.emit("rt_update", f, 5);*/
}

int16_t Node::route_score_from_snr(int16_t snr_q4) const {
    return static_cast<int16_t>(snr_q4 - protocol::route_snr_conservatism_q4);
}

// Install a multi-hop route to `dest` via `via` (the immediate forwarder), hops as given — the F
// reverse/forward-path learner. Like learn_direct_neighbor but multi-hop, and it does NOT fire a
// triggered beacon (the F path relies on the periodic dirty-beacon to re-advertise).
void Node::learn_route_via(uint8_t dest, uint8_t via, uint8_t hops, int16_t snr_q4) {
    if (dest == 0xFF || dest == _node_id || via == 0xFF) return;
    RtCandidate cand{};
    cand.next_hop = via; cand.score = route_score_from_snr(snr_q4); cand.hops = hops;
    cand.is_gateway = false; cand.last_seen_ms = _hal.now(); cand.learned_layer_id = _cfg.leaf_id;
    const MergeAction a = rt_merge(dest, cand);
    if (a == MergeAction::new_dest || a == MergeAction::promote || a == MergeAction::primary_refresh)
        emit_rt_update(_hal, dest, via, cand.score, hops, "primary");
    else if (a == MergeAction::alt_install)
        emit_rt_update(_hal, dest, via, cand.score, hops, "alt");
}

// Direct (hops=1) route to a received frame's immediate sender — the C++ learn_rx_source.
// Mirrors the Lua learn_direct_from_frame: build a direct candidate from (sender, rx SNR),
// merge it, emit rt_update. Returns true on a real change so the caller fires the triggered
// beacon. The C++ has no id-bind/dest-seen/liveness plane, so those Lua sub-actions are absent.
bool Node::learn_direct_neighbor(uint8_t sender, int16_t snr_q4, bool is_gw) {
    if (sender == 0xFF || sender == _node_id) return false;   // unknown/reserved id, or self
    RtCandidate cand{};
    cand.next_hop         = sender;
    cand.score            = route_score_from_snr(snr_q4);
    cand.hops             = 1;
    cand.is_gateway       = is_gw;
    cand.last_seen_ms     = _hal.now();
    cand.learned_layer_id = _cfg.leaf_id;
    const MergeAction a = rt_merge(sender, cand);
    if (a == MergeAction::new_dest || a == MergeAction::promote ||
        a == MergeAction::primary_refresh) {
        emit_rt_update(_hal, sender, sender, cand.score, 1, "primary");
        return true;
    } else if (a == MergeAction::alt_install) {
        emit_rt_update(_hal, sender, sender, cand.score, 1, "alt");
    }
    return false;
}

void Node::emit_beacon(const char* kind) {
    // Half-duplex busy skip (Lua send_beacon_page dv:7585): never beacon mid data-exchange. periodic_beacon_fire
    // already guards this, but the TRIGGERED path (kTriggeredBeaconTimerId) reaches here directly — without this
    // the C++ would TX a triggered beacon while busy where the Lua skips, diverging beacon timing (review #02).
    if (_active->_pending_tx || _active->_pending_rx) { _hal.log("beacon_tx skipped (busy in data exchange)"); return; }
    // R4.3 budget-aware skip (dv:7595): at tier >= CRITICAL a BCN is a luxury — preserve the remaining duty
    // budget for forwards already queued. Neighbours keep us via passive last_seen from any frame we send.
    // (compute_budget_tier is draw-free; HEALTHY in every gate, so this is gate-inert.)
    if (compute_budget_tier() >= BudgetTier::critical) {
        MR_EMIT("beacon_skipped_budget", EF_I("tier",static_cast<uint8_t>(compute_budget_tier())),EF_S("kind",kind));
        /*MR_TELEMETRY(EventField f[] = { { .key = "tier", .type = EventField::T::i64, .i = static_cast<uint8_t>(compute_budget_tier()) },{ .key = "kind", .type = EventField::T::str, .s = kind } };_hal.emit("beacon_skipped_budget", f, 2); );
        */
        return;
    }
    maybe_exit_discovery("before_bcn");
    // Steady state sends dirty-only differential beacons; discovery sends full
    // pages (dv_dual_sf.lua:7606: dirty_only = not(in_discovery or kind=="sync")).
    // A "sync" beacon (the REQ_SYNC jittered reply) ALWAYS sends the full table so the
    // route-starved requester catches up in one page (node_query.cpp:sync_response_fire).
    const bool dirty_only = !in_discovery() && std::strcmp(kind, "sync") != 0;

    // A MOBILE node advertises ZERO route entries (identity-only beacon) — never
    // used as transit (dv_dual_sf.lua:1716-1721).
    beacon_entry entries[kMaxBeaconEntries];
    uint8_t      pack_idx[kMaxBeaconEntries];   // _active->_rt indices packed (for dirty-clear)
    uint8_t n = 0, dirty_n = 0, stable_n = 0, total_dirty = 0;

    if (!_cfg.is_mobile) {
        if (_active->_rt_count == 0) _beacon_offset = 0;          // Lua total==0 path resets the cursor (dv_dual_sf.lua:1761)
        for (uint8_t i = 0; i < _active->_rt_count; ++i) if (_active->_rt[i].dirty) ++total_dirty;
        // Phase 1: dirty entries first (ascending dest — _active->_rt is kept sorted).
        for (uint8_t i = 0; i < _active->_rt_count && n < kMaxBeaconEntries; ++i) {
            if (!_active->_rt[i].dirty) continue;
            pack_idx[n++] = i; ++dirty_n;
        }
        // Phase 2: stable rotation from _beacon_offset, skipped on dirty-only
        // beacons AND when the dirty page already filled (remaining==0) — the Lua
        // gates new_offset on `remaining > 0 and not dirty_only` (dv_dual_sf.lua:1789).
        if (!dirty_only && _active->_rt_count > 0 && n < kMaxBeaconEntries) {
            uint8_t idx = _beacon_offset, steps = 0;
            while (n < kMaxBeaconEntries && steps < _active->_rt_count) {
                const uint8_t ri = static_cast<uint8_t>(idx % _active->_rt_count);
                if (!_active->_rt[ri].dirty) { pack_idx[n++] = ri; ++stable_n; }
                idx = static_cast<uint8_t>(idx + 1); ++steps;
            }
            _beacon_offset = static_cast<uint8_t>(idx % _active->_rt_count);   // advance ONLY when Phase 2 ran
        }
    }

    for (uint8_t k = 0; k < n; ++k) {
        const RtEntry&     re = _active->_rt[pack_idx[k]];
        const RtCandidate& pc = re.candidates[0];
        entries[k].dest         = re.dest;
        entries[k].next         = pc.next_hop;
        entries[k].score_bucket = static_cast<uint8_t>(bucket_of_snr_4b(pc.score));
        entries[k].is_gateway   = pc.is_gateway;
        entries[k].hops         = pc.hops;
    }

    beacon_in in{};
    in.leaf_id      = _cfg.leaf_id;
    in.self_gateway = _cfg.is_gateway;
    in.is_mobile    = _cfg.is_mobile;
    in.src          = _node_id;
    in.key_hash32   = _key_hash32;
    in.entries      = std::span<const beacon_entry>(entries, n);
    // Slice 3e: a GATEWAY advertises its window schedule — one schedule_record per leaf, carrying the RECEIVER-ANCHORED
    // countdown (offset_100ms = time until that leaf's window NEXT opens; %period so the active leaf reads ~0 = open now).
    // A node wanting the gateway on its leaf defers its RTS to that window (3e.2). layer_id = the 4-bit leaf nibble (§0.8).
    // (TX-fixup re-stamp at the true TX instant is deferred — the LBT defer is << the 100ms quantization.)
    schedule_record sched[2];
    if (_cfg.n_layers == 2) {
        const uint64_t now    = _hal.now();
        const uint32_t period = _cfg.layers[0].window_period_ms;
        for (uint8_t i = 0; i < 2; ++i) {
            const LayerConfig& L = _cfg.layers[i];
            // §3e F-A: the ACTIVE leaf is open NOW -> countdown 0 EXPLICITLY. Don't lean on (period % period): that only
            // reads 0 at the switch instant — a triggered beacon emitted mid-window would otherwise carry period-elapsed,
            // which both mis-states the schedule and OVERFLOWS the 8-bit offset above 25.5 s. The foreign leaf carries the
            // time to its next open (set_window_anchors keeps it <= one active window, so < period; %period is defensive).
            uint64_t cd = (&_layers[i] == _active)
                          ? 0
                          : ((_layers[i]._next_open_ms > now) ? (_layers[i]._next_open_ms - now) : 0);   // <0 -> 0 (busy-defer slip)
            if (period) cd %= period;
            sched[i].layer_id    = static_cast<uint8_t>(L.layer_id & 0x0F);
            sched[i].routing_sf  = L.routing_sf;
            const uint32_t psec = period / 1000;                                                      // period: seconds, else 5s-units (ceil)
            if (psec <= 255) { sched[i].period_unit_5s = false; sched[i].period_units = static_cast<uint8_t>(psec < 1 ? 1 : psec); }
            else { const uint32_t p5 = (period + 4999) / 5000; sched[i].period_unit_5s = true; sched[i].period_units = static_cast<uint8_t>(p5 > 255 ? 255 : p5); }
            const uint32_t d100 = L.window_ms / 100;
            sched[i].duration_100ms = static_cast<uint8_t>(d100 < 1 ? 1 : (d100 > 255 ? 255 : d100));
            const uint64_t o100 = cd / 100;
            sched[i].offset_100ms   = static_cast<uint8_t>(o100 > 255 ? 255 : o100);
        }
        in.schedule = std::span<const schedule_record>(sched, 2);
    }
    // seen_bitmap left empty → codec derives has_seen_bitmap=0.
    // ROADMAP §3: advertise our dirty channel msgs in the BCN digest ext-TLV (gateways skip — Principle 11).
    // build_channel_digest_ext is draw-free; its ad_count/dirty side effects track per built beacon (dv:1453).
    uint8_t ext_buf[16]; size_t ext_n = 0;
    if (!_cfg.is_gateway) ext_n = build_channel_digest_ext(ext_buf, sizeof(ext_buf));
    in.ext = std::span<const uint8_t>(ext_buf, ext_n);

    uint8_t buf[protocol::beacon_max_bytes];
    const size_t len = pack_beacon(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (len == 0) { _hal.log("beacon pack failed (entries overflow)"); return; }

    TxParams p;
    p.sf    = static_cast<int16_t>(_cfg.routing_sf);
    p.label = "BCN";
    [[maybe_unused]] const bool sent = tx_flood(buf, len, p.sf);   // R4.5 FLOOD LBT + duty pre-check (was a raw _hal.tx)
    _last_beacon_tx_ms = _hal.now();

    // Clear dirty ONLY on the dirty entries that landed in THIS beacon — overflow
    // dirty routes stay dirty for the next one (dv_dual_sf.lua:1832-1836).
    for (uint8_t k = 0; k < dirty_n; ++k) _active->_rt[pack_idx[k]].dirty = false;

    MR_EMIT("beacon_tx", EF_I("n_entries",n),EF_I("rt_total",_active->_rt_count),EF_I("routing_sf",_cfg.routing_sf),EF_S("kind",kind),EF_I("result",sent ? 0 : 2));
    MR_EMIT("beacon_diff_breakdown", EF_I("dirty_n",dirty_n),EF_I("stable_n",stable_n),EF_I("total_dirty",total_dirty));
}

void Node::ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto parsed = parse_beacon(std::span<const uint8_t>(bytes, len));
    if (!parsed) return;
    const beacon_out& b = *parsed;
    if (b.leaf_id != _cfg.leaf_id) return;                // single-layer filter (R1)
    if (b.src == _node_id) {                              // beacon carrying OUR short id...
        if (b.key_hash32 == _key_hash32) return;          // ...and our hash -> a true self-echo; drop
        // ...but a DIFFERENT hash -> an ADDRESS COLLISION (node_id DAD §7). The old guard swallowed this.
        // Defend our id: a J_DENY(OWN_ID_DEFENSE) carrying our claim_epoch makes the impostor run the
        // §6 tiebreak in its DENY handler and yield if it loses. Do NOT record the impostor's binding.
        if (_node_id != 0) addr_conflict_send_deny(_node_id, _key_hash32, b.key_hash32, J_DENY_OWN_ID_DEFENSE);
        return;
    }

    // R4.3 max-idle witness — set AFTER the parse/leaf/self-echo guards (Lua dv:9559), NOT at the on_recv
    // dispatch top: a foreign-leaf or unparseable B-frame must NOT update it, or the max-idle B+C (and hence
    // the silence-jitter draw) desyncs from the Lua on multi-leaf channels (review #00).
    _last_rx_bcn_ms = _hal.now();
    // Hash-locate A0 (Lua dv:9577): every BCN carries the sender's key_hash32 — learn the binding so we can
    // later answer an H query for this node WITHOUT the flood reaching the owner. (parse_beacon already
    // decodes b.key_hash32; this is the "stop discarding the received one" the review called for.)
    id_bind_set(b.src, b.key_hash32, IdBindSource::bcn, IdBindConf::authoritative);   // the owner's own beacon = FIRST-HAND assertion of its key_hash32 (authoritative); a relayed/snooped binding is the claimed second-hand one
    // Parse the channel-digest ext-TLV ONCE (draw-free) so beacon_rx can report how many ids the beacon
    // carries (dv:9614 `channel_digest_ids = #b.channel_digest_ids or 0`); reused by the reaction below.
    // Reported for ALL nodes (even gateways); only the process_channel_digest reaction is gateway-gated.
    uint32_t dids[protocol::channel_dirty_max_per_bcn];
    uint8_t  dn = 0;
    if (b.has_ext) {
        const auto ext = beacon_ext(std::span<const uint8_t>(bytes, len), b);
        dn = parse_channel_digest_tlv(ext, dids, protocol::channel_dirty_max_per_bcn);
    }
    // beacon_rx — one per received beacon (the gate asserts src)
    MR_EMIT("beacon_rx",EF_I("src",b.src),EF_I("channel_digest_ids",dn));
    if (in_discovery()) ++_discovery_bcn_rx_count;        // dv_dual_sf.lua:9560-9562

    const uint64_t now         = _hal.now();
    const int16_t  meta_snr_q4 = protocol::db_to_q4(meta.snr_db);

    // Slice 3e.2: learn a GATEWAY's window schedule from its beacon (self_gateway + has_schedule) so we can later time
    // an RTS to its window (gateway_schedule_defer_ms). visit_start is anchored to `now` (this heard instant — the leaf
    // filter above means the gateway is currently on OUR leaf, so its OUR-leaf record reads ~open-now).
    if (b.self_gateway && b.has_schedule && b.schedule_count > 0) {
        GatewaySchedule gs{};
        gs.valid = true; gs.gw_node_id = b.src; gs.heard_ms = now;
        gs.n_rec = (b.schedule_count > 2) ? 2 : b.schedule_count;
        for (uint8_t i = 0; i < gs.n_rec; ++i) {
            auto r = parse_beacon_schedule(std::span<const uint8_t>(bytes, len), b, i);
            if (!r) { gs.n_rec = i; break; }
            gs.rec[i].leaf_id   = r->layer_id;
            gs.rec[i].window_ms = static_cast<uint32_t>(r->duration_100ms) * 100;
            gs.rec[i].offset_ms = static_cast<uint32_t>(r->offset_100ms) * 100;
            gs.period_ms        = static_cast<uint32_t>(r->period_units) * (r->period_unit_5s ? 5000u : 1000u);
        }
        if (gs.n_rec > 0) store_gateway_schedule(gs);
    }

    // REQ_SYNC de-storm (dv_dual_sf.lua:9699-9709): a USEFUL overheard beacon means a neighbour is
    // already advertising routes to the requester, so cancel any pending jittered sync-response still
    // inside its suppress window — our reply would be redundant. Draw-free. DIVERGENCE: the Lua's
    // useful_bcn = (#entries>0) or (seen_bits>1); we don't extract the seen-bitmap popcount (the codec
    // keeps it opaque), so a present seen-bitmap counts as useful (slightly looser, no draw impact).
    if ((b.n_entries > 0) || b.has_seen_bitmap) {
        for (uint8_t i = 0; i < protocol::cap_sync_response_pending; ++i) {
            SyncPending& p = _active->_sync_pending[i];
            if (p.active && !p.suppressed && now <= p.fire_at
                && (now - p.requested_at) <= protocol::sync_response_suppress_window_ms)
                p.suppressed = true;
        }
    }

    bool rt_changed = false;                              // any new/promote this beacon → triggered re-beacon

    // Direct route via the beacon sender, hops=1 (dv_dual_sf.lua:9584-9618). A DIRECT-route
    // promote/primary_refresh counts as a change (re-beacon) — the carried-DV merge below omits
    // primary_refresh (matches the Lua entry loop :9656).
    if (learn_direct_neighbor(b.src, meta_snr_q4, b.self_gateway)) rt_changed = true;

    // DV merge: each carried entry is a route via the sender (dv_dual_sf.lua:9620-9678).
    for (uint8_t i = 0; i < b.n_entries; ++i) {
        auto pe = parse_beacon_entry(std::span<const uint8_t>(bytes, len), b, i);
        if (!pe) continue;
        const beacon_entry& e = *pe;
        if (e.dest == _node_id) continue;                 // split-horizon
        if (e.next == _node_id) {                         // sender reaches e.dest via US
            rt_prune_cycle(e.dest, b.src);                // drop our looped candidates (dv_dual_sf.lua:9628-9633)
            continue;
        }
        // R2: peer-suspect skip is a no-op (no liveness plane → suspect_level 0).
        const int16_t entry_score_q4 = snr_of_bucket_4b(e.score_bucket);
        const int16_t rx_score_q4    = route_score_from_snr(meta_snr_q4);
        const int16_t combined_score = (rx_score_q4 < entry_score_q4) ? rx_score_q4 : entry_score_q4;
        const int     combined_hops  = static_cast<int>(e.hops) + 1;
        if (combined_hops > _cfg.dv_hop_cap) continue;

        RtCandidate cand{};
        cand.next_hop         = b.src;
        cand.n2_hop           = e.next;
        cand.score            = combined_score;
        cand.hops             = static_cast<uint8_t>(combined_hops);
        cand.is_gateway       = e.is_gateway;
        cand.last_seen_ms     = now;
        cand.learned_layer_id = _cfg.leaf_id;
        const MergeAction a = rt_merge(e.dest, cand);
        if (a == MergeAction::new_dest || a == MergeAction::promote) {
            emit_rt_update(_hal, e.dest, b.src, combined_score, cand.hops, "primary");
            rt_changed = true;
        } else if (a == MergeAction::alt_install) {
            emit_rt_update(_hal, e.dest, b.src, combined_score, cand.hops, "alt");
        }
    }

    // dv_dual_sf.lua:9680-9684 order: discovery re-check, triggered re-beacon on
    // change, then convergence telemetry. (rt_prune_cycle fires its own coalesced
    // triggered beacon when it mutates.)
    // ROADMAP §3 channel gossip: react to a CHANNEL_DIGEST ext-TLV. Placed BEFORE the triggered-beacon
    // trigger below so the pull-jitter DRAW precedes the triggered-beacon draw, matching the Lua stream
    // (dv:9617 calls process_channel_digest before the triggered re-beacon). Gateways skip (Principle 11).
    if (dn && !(_cfg.is_gateway && _cfg.gateway_only)) process_channel_digest(b.src, dids, dn);  // §7 consumer: a gateway+owner consumes digests to pull its own holes; a pure bridge skips
    maybe_exit_discovery(rt_changed ? "rt_update" : "beacon_rx");
    if (rt_changed) {
        schedule_triggered_beacon();
        if (_active->_deferred_n > 0) try_drain_deferred();        // a new route may unblock a deferred send
    }
    drain_resolved_parked_sends();                        // this beacon may have installed the authoritative binding a parked send-by-hash awaits (no-op if none parked)
    maybe_emit_rt_full();
}

// R4.3 max-idle B+C override (dv:7734-7784). If we've been silent >= max_idle, force a beacon UNLESS the
// B+C filter says a neighbour is carrying the refresh load (recent BCN-rx) AND we have nothing new (dirty=0).
// Returns force_idle. emit_events=false on the post-jitter re-check (the Lua recomputes silently, dv:7814-7834).
[[maybe_unused]] static int64_t or_neg1(uint64_t v) { return (v == UINT64_MAX) ? -1 : static_cast<int64_t>(v); }
bool Node::beacon_max_idle_force(uint64_t now, bool emit_events) {
    if (_cfg.beacon_max_idle_ms == 0) return false;
    const uint64_t since_tx = (_last_beacon_tx_ms != 0) ? (now - _last_beacon_tx_ms) : UINT64_MAX;
    if (since_tx < _cfg.beacon_max_idle_ms) return false;                 // not silent long enough
    const uint64_t since_bcn_rx = (_last_rx_bcn_ms != 0) ? (now - _last_rx_bcn_ms) : UINT64_MAX;
    const uint64_t defer_window = _cfg.beacon_max_idle_ms / 3;
    uint8_t dirty_n = 0;
    for (uint8_t i = 0; i < _active->_rt_count; ++i) if (_active->_rt[i].dirty) ++dirty_n;
    const bool skip_clean = (dirty_n == 0 && since_bcn_rx < defer_window); // (B+C) neighbour fresh AND nothing new
    if (emit_events) {
        if (skip_clean) {
            MR_EMIT("beacon_max_idle_skip_clean",EF_I("since_tx_ms",since_tx),EF_I("since_bcn_rx_ms",since_bcn_rx),EF_I("defer_window_ms",defer_window),EF_I("dirty_n",0));
        } else {
            MR_EMIT("beacon_max_idle_force",EF_I("since_tx_ms",or_neg1(since_tx)),EF_I("max_idle_ms",_cfg.beacon_max_idle_ms),EF_I("since_bcn_rx_ms",or_neg1(since_bcn_rx)),EF_I("dirty_n",dirty_n));
        }
    }
    return !skip_clean;                                                   // skip_clean -> fall through (throttle skips)
}

// R4.3 throttle body (dv:7695-7851) — replaces the unconditional emit_beacon("periodic"). The quiet<=0 fast
// path keeps the pre-R4.3 behaviour byte-identical (NO silence-jitter draw); the gate suppresses a beacon when
// the channel is busy; on a quiet channel a silence-jitter draw spreads the TX to combat thundering herds.
void Node::periodic_beacon_fire() {
    if (_cfg.join_required) return;                          // unjoined: no address, can't beacon (dv:7696)
    if (_active->_pending_tx || _active->_pending_rx) return;                  // busy in a data exchange (dv:7699)
    if (_cfg.quiet_threshold_ms == 0) { emit_beacon("periodic"); return; }   // FAST PATH (dv:7704) — NO draw
    const uint64_t now = _hal.now();
    const uint64_t since_rx = (_last_rx_routing_sf_ms != 0) ? (now - _last_rx_routing_sf_ms) : UINT64_MAX;
    const bool force_idle = beacon_max_idle_force(now, /*emit_events=*/true);
    if (since_rx < _cfg.quiet_threshold_ms && !force_idle) {  // channel busy -> skip, NO draw (dv:7785)
        MR_TELEMETRY(
            EventField f[] = { { .key = "since_rx_ms",  .type = EventField::T::i64, .i = static_cast<int64_t>(since_rx) },
                               { .key = "threshold_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.quiet_threshold_ms) },
                               { .key = "stage",        .type = EventField::T::str, .s = "pre_jitter" } };
            _hal.emit("beacon_skipped_busy", f, 3); );
        return;
    }
    // gate passed -> draw the silence-jitter (dv:7795). jitter==0 -> send now; else defer + re-check.
    const int jitter = (_cfg.beacon_silence_jitter_ms > 0)
                       ? _hal.rand_range(0, static_cast<int>(_cfg.beacon_silence_jitter_ms) + 1)
                       : 0;
    if (jitter == 0) { emit_beacon("periodic"); return; }
    // #D: arm a FREE ring slot (not the single kBeaconJitterTimerId) so a 2nd periodic defer landing in the same
    // jitter window doesn't REPLACE the first — both fire, matching the Lua's per-`after` closures (dv:7799). The
    // jitter draw above already happened (draw-count unchanged); only the lost-beacon edge is fixed. Ring full (>4
    // defers stacked, period << jitter — absurd in steady state) -> fall back to slot 0 (the old replace behaviour).
    uint8_t slot = 0;
    for (uint8_t s = 0; s < kBeaconJitterSlots; ++s) { if (!_beacon_jitter_pending[s]) { slot = s; break; } }
    _beacon_jitter_pending[slot] = true;
    (void)_hal.after(static_cast<uint32_t>(jitter), kBeaconJitterTimerId + slot);
}

// R4.3 post-silence-jitter re-check (dv:7801-7849). A neighbour may have beaconed during our jitter window;
// if the channel went busy AND the max-idle override doesn't force us, stand down (they won the race). NO draw.
void Node::deferred_beacon_jitter_fire(uint8_t slot) {
    if (slot < kBeaconJitterSlots) _beacon_jitter_pending[slot] = false;   // #D: free the ring slot
    if (_active->_pending_tx || _active->_pending_rx) return;                  // busy in a data exchange (dv:7802)
    const uint64_t now = _hal.now();
    const uint64_t since = (_last_rx_routing_sf_ms != 0) ? (now - _last_rx_routing_sf_ms) : UINT64_MAX;
    const bool force_idle_post = beacon_max_idle_force(now, /*emit_events=*/false);   // recompute SILENTLY (dv:7814)
    if (since < _cfg.quiet_threshold_ms && !force_idle_post) {
        MR_TELEMETRY(
            EventField f[] = { { .key = "since_rx_ms",  .type = EventField::T::i64, .i = static_cast<int64_t>(since) },
                               { .key = "threshold_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.quiet_threshold_ms) },
                               { .key = "stage",        .type = EventField::T::str, .s = "post_jitter" } };
            _hal.emit("beacon_skipped_busy", f, 3); );
        return;
    }
    emit_beacon("periodic");
}

void Node::schedule_triggered_beacon() {
    if (_cfg.is_mobile) return;                            // mobiles never trigger (dv_dual_sf.lua:7878)
    if (_triggered_beacon_pending) return;                // coalesce BEFORE the rand draw
    _triggered_beacon_pending = true;
    const int lo = protocol::beacon_trigger_jitter_min_ms;
    const int hi = protocol::beacon_trigger_jitter_max_ms;
    uint32_t delay = static_cast<uint32_t>(_hal.rand_range(lo, hi + 1));   // 1st draw (the trigger jitter)
    // R4.3 steady-state min-interval defer (dv:7890-7902): if this triggered beacon would land sooner than
    // min_interval after our last beacon, push it past `earliest` + a FRESH jitter (the conditional 2nd draw).
    // Fires ONLY in steady_state (now >= boot_grace) — under boot grace it NEVER draws (every gate runs <120s),
    // so the gate streams stay byte-identical; the 2nd draw is matched draw-for-draw with the Lua in steady state.
    const uint64_t now = _hal.now();
    const bool steady_state = !in_discovery()
        && (now - _discovery_started_ms >= protocol::beacon_boot_grace_ms);
    if (steady_state && protocol::beacon_trigger_min_interval_ms > 0 && _last_beacon_tx_ms != 0) {
        const uint64_t earliest = _last_beacon_tx_ms + protocol::beacon_trigger_min_interval_ms;
        if (now + delay < earliest) {
            [[maybe_unused]] const uint32_t old_delay = delay;
            delay = static_cast<uint32_t>((earliest - now) + _hal.rand_range(lo, hi + 1));   // 2nd draw
            MR_TELEMETRY(
                EventField f[] = { { .key = "min_interval_ms",   .type = EventField::T::i64, .i = protocol::beacon_trigger_min_interval_ms },
                                   { .key = "old_delay_ms",      .type = EventField::T::i64, .i = old_delay },
                                   { .key = "delay_ms",          .type = EventField::T::i64, .i = delay },
                                   { .key = "since_last_bcn_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(now - _last_beacon_tx_ms) } };
                _hal.emit("beacon_trigger_deferred", f, 4); );
        }
    }
    (void)_hal.after(delay, kTriggeredBeaconTimerId);
}

void Node::maybe_exit_discovery([[maybe_unused]] const char* reason) {
    if (!_discovery_mode) return;
    const uint64_t now = _hal.now();
    const bool timed_out = (_discovery_until_ms > 0) && (now >= _discovery_until_ms);
    if (_discovery_bcn_rx_count >= protocol::discovery_min_bcn_rx ||
        _active->_rt_count >= protocol::discovery_min_routes || timed_out) {
        _discovery_mode = false;
        MR_TELEMETRY(
            EventField f[] = {
                { .key = "reason",     .type = EventField::T::str, .s = reason },
                { .key = "heard_bcn",  .type = EventField::T::i64, .i = static_cast<int64_t>(_discovery_bcn_rx_count) },
                { .key = "rt_total",   .type = EventField::T::i64, .i = static_cast<int64_t>(_active->_rt_count) },
                { .key = "elapsed_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(now - _discovery_started_ms) },
            };
            _hal.emit("bcn_discovery_exit", f, 4); );
    }
}

}  // namespace meshroute
