// MeshRoute — lib/core/node_beacon.cpp  (R1/R2 beacon emit + ingest)
//
// Node methods for the §10 BCN plane: periodic/triggered beacon emit (dirty-only
// differential pages), beacon ingest + DV merge, the discovery fast-cadence exit,
// and the triggered-beacon scheduler. Plus the file-local SNR wire-bucket
// round-trip and the rt_update telemetry helper. Behaviour mirrors dv_dual_sf.lua.
// Part of the Node class (declared in node.h); split out of node.cpp for readability.
#include "node.h"

#include "frame_codec.h"

#include <span>

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
static void emit_rt_update(Hal& hal, uint8_t dest, uint8_t next,
                           int16_t score_q4, uint8_t hops, const char* slot) {
    EventField f[] = {
        { .key = "dest",  .type = EventField::T::i64, .i = static_cast<int64_t>(dest) },
        { .key = "next",  .type = EventField::T::i64, .i = static_cast<int64_t>(next) },
        { .key = "score", .type = EventField::T::f64, .f = static_cast<double>(protocol::q4_to_db(score_q4)) },
        { .key = "hops",  .type = EventField::T::i64, .i = static_cast<int64_t>(hops) },
        { .key = "slot",  .type = EventField::T::str, .s = slot },
    };
    hal.emit("rt_update", f, 5);
}

int16_t Node::route_score_from_snr(int16_t snr_q4) const {
    return static_cast<int16_t>(snr_q4 - protocol::route_snr_conservatism_q4);
}

void Node::emit_beacon(const char* kind) {
    maybe_exit_discovery("before_bcn");
    // Steady state sends dirty-only differential beacons; discovery sends full
    // pages (dv_dual_sf.lua:7606: dirty_only = not(in_discovery or kind=="sync")).
    // (The kind=="sync" term rides with the R5 req_sync plane — TODO.)
    const bool dirty_only = !in_discovery();

    // A MOBILE node advertises ZERO route entries (identity-only beacon) — never
    // used as transit (dv_dual_sf.lua:1716-1721).
    beacon_entry entries[kMaxBeaconEntries];
    uint8_t      pack_idx[kMaxBeaconEntries];   // _rt indices packed (for dirty-clear)
    uint8_t n = 0, dirty_n = 0, stable_n = 0, total_dirty = 0;

    if (!_cfg.is_mobile) {
        if (_rt_count == 0) _beacon_offset = 0;          // Lua total==0 path resets the cursor (dv_dual_sf.lua:1761)
        for (uint8_t i = 0; i < _rt_count; ++i) if (_rt[i].dirty) ++total_dirty;
        // Phase 1: dirty entries first (ascending dest — _rt is kept sorted).
        for (uint8_t i = 0; i < _rt_count && n < kMaxBeaconEntries; ++i) {
            if (!_rt[i].dirty) continue;
            pack_idx[n++] = i; ++dirty_n;
        }
        // Phase 2: stable rotation from _beacon_offset, skipped on dirty-only
        // beacons AND when the dirty page already filled (remaining==0) — the Lua
        // gates new_offset on `remaining > 0 and not dirty_only` (dv_dual_sf.lua:1789).
        if (!dirty_only && _rt_count > 0 && n < kMaxBeaconEntries) {
            uint8_t idx = _beacon_offset, steps = 0;
            while (n < kMaxBeaconEntries && steps < _rt_count) {
                const uint8_t ri = static_cast<uint8_t>(idx % _rt_count);
                if (!_rt[ri].dirty) { pack_idx[n++] = ri; ++stable_n; }
                idx = static_cast<uint8_t>(idx + 1); ++steps;
            }
            _beacon_offset = static_cast<uint8_t>(idx % _rt_count);   // advance ONLY when Phase 2 ran
        }
    }

    for (uint8_t k = 0; k < n; ++k) {
        const RtEntry&     re = _rt[pack_idx[k]];
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
    // schedule / seen_bitmap / ext left empty → codec derives has_*=0.

    uint8_t buf[protocol::beacon_max_bytes];
    const size_t len = pack_beacon(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (len == 0) { _hal.log("beacon pack failed (entries overflow)"); return; }

    TxParams p;
    p.sf    = static_cast<int16_t>(_cfg.routing_sf);
    p.label = "BCN";
    const TxResult r = _hal.tx(buf, len, p);
    _last_beacon_tx_ms = _hal.now();

    // Clear dirty ONLY on the dirty entries that landed in THIS beacon — overflow
    // dirty routes stay dirty for the next one (dv_dual_sf.lua:1832-1836).
    for (uint8_t k = 0; k < dirty_n; ++k) _rt[pack_idx[k]].dirty = false;

    EventField f[] = {
        { .key = "n_entries",  .type = EventField::T::i64, .i = static_cast<int64_t>(n) },
        { .key = "rt_total",   .type = EventField::T::i64, .i = static_cast<int64_t>(_rt_count) },
        { .key = "routing_sf", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.routing_sf) },
        { .key = "kind",       .type = EventField::T::str, .s = kind },
        { .key = "result",     .type = EventField::T::i64, .i = static_cast<int64_t>(static_cast<int>(r)) },
    };
    _hal.emit("beacon_tx", f, 5);

    EventField g[] = {
        { .key = "dirty_n",     .type = EventField::T::i64, .i = static_cast<int64_t>(dirty_n) },
        { .key = "stable_n",    .type = EventField::T::i64, .i = static_cast<int64_t>(stable_n) },
        { .key = "total_dirty", .type = EventField::T::i64, .i = static_cast<int64_t>(total_dirty) },
    };
    _hal.emit("beacon_diff_breakdown", g, 3);
}

void Node::ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto parsed = parse_beacon(std::span<const uint8_t>(bytes, len));
    if (!parsed) return;
    const beacon_out& b = *parsed;
    if (b.leaf_id != _cfg.leaf_id) return;                // single-layer filter (R1)
    if (b.src == _node_id) return;                        // ignore our own echo

    {   // beacon_rx — one per received beacon (the gate asserts src)
        EventField f[] = { { .key = "src", .type = EventField::T::i64, .i = static_cast<int64_t>(b.src) } };
        _hal.emit("beacon_rx", f, 1);
    }
    if (in_discovery()) ++_discovery_bcn_rx_count;        // dv_dual_sf.lua:9560-9562

    const uint64_t now         = _hal.now();
    const int16_t  meta_snr_q4 = protocol::db_to_q4(meta.snr_db);
    bool rt_changed = false;                              // any new/promote this beacon → triggered re-beacon

    // Direct route via the beacon sender, hops=1 (dv_dual_sf.lua:9584-9618).
    {
        RtCandidate cand{};
        cand.next_hop         = b.src;
        cand.score            = route_score_from_snr(meta_snr_q4);
        cand.hops             = 1;
        cand.is_gateway       = b.self_gateway;
        cand.last_seen_ms     = now;
        cand.learned_layer_id = _cfg.leaf_id;
        const MergeAction a = rt_merge(b.src, cand);
        // For a DIRECT route, primary_refresh (a known neighbour re-heard at a
        // strictly-better SNR) is a real change: the Lua learn_direct_from_frame
        // pre-learner emits rt_update + fires the triggered beacon on it
        // (dv_dual_sf.lua:7553-7564 + the learned_direct_pre rt_changed at :9611).
        // Without this, the missing schedule_triggered_beacon rand draw desyncs
        // the mt19937 vs the Lua on fluctuating-SNR links. (The carried-DV block
        // below omits primary_refresh — that matches the Lua entry loop :9656.)
        if (a == MergeAction::new_dest || a == MergeAction::promote ||
            a == MergeAction::primary_refresh) {
            emit_rt_update(_hal, b.src, b.src, cand.score, 1, "primary");
            rt_changed = true;
        } else if (a == MergeAction::alt_install) {
            emit_rt_update(_hal, b.src, b.src, cand.score, 1, "alt");
        }
    }

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
        if (combined_hops > protocol::dv_hop_cap) continue;

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
    maybe_exit_discovery(rt_changed ? "rt_update" : "beacon_rx");
    if (rt_changed) {
        schedule_triggered_beacon();
        if (_deferred_n > 0) try_drain_deferred();        // a new route may unblock a deferred send
    }
    maybe_emit_rt_full();
}

void Node::schedule_triggered_beacon() {
    if (_cfg.is_mobile) return;                            // mobiles never trigger (dv_dual_sf.lua:7878)
    if (_triggered_beacon_pending) return;                // coalesce BEFORE the rand draw
    _triggered_beacon_pending = true;
    // Single jittered one-shot. The steady-state min-interval rate-limit defer
    // (dv_dual_sf.lua:7894, a conditional 2nd rand draw) is deferred to R4 with
    // the throttle plane (decision Q3=b) — removing the 2nd-draw determinism hazard.
    const int delay = _hal.rand_range(protocol::beacon_trigger_jitter_min_ms,
                                      protocol::beacon_trigger_jitter_max_ms + 1);
    (void)_hal.after(static_cast<uint32_t>(delay), kTriggeredBeaconTimerId);
}

void Node::maybe_exit_discovery(const char* reason) {
    if (!_discovery_mode) return;
    const uint64_t now = _hal.now();
    const bool timed_out = (_discovery_until_ms > 0) && (now >= _discovery_until_ms);
    if (_discovery_bcn_rx_count >= protocol::discovery_min_bcn_rx ||
        _rt_count >= protocol::discovery_min_routes || timed_out) {
        _discovery_mode = false;
        EventField f[] = {
            { .key = "reason",     .type = EventField::T::str, .s = reason },
            { .key = "heard_bcn",  .type = EventField::T::i64, .i = static_cast<int64_t>(_discovery_bcn_rx_count) },
            { .key = "rt_total",   .type = EventField::T::i64, .i = static_cast<int64_t>(_rt_count) },
            { .key = "elapsed_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(now - _discovery_started_ms) },
        };
        _hal.emit("bcn_discovery_exit", f, 4);
    }
}

}  // namespace meshroute
