// MeshRoute — lib/core/node.cpp  (R1 beacon plane + R2 route-plane hardening)
//
// The Node emits §10 BCNs over its rt[dest] primaries (dirty-only in steady
// state) and ingests them into a bounded DV route table with aging/TTL eviction,
// the 3-cycle prune, the triggered-beacon kind, and a discovery fast-cadence
// state machine. Behaviour mirrors dv_dual_sf.lua; the wire is C5 cmd-nibble.
// See docs/specs/2026-05-29-r1-beacon-emit-design.md + 2026-05-30-r2-route-hardening-design.md.
#include "node.h"

#include "frame_codec.h"
#include "wire.h"
#include "airtime.h"

#include <span>

namespace meshroute {

// Timer ids are Node-private members (node.h: kBeaconTimerId / kAgingTimerId /
// kTriggeredBeaconTimerId).

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

Node::Node(Hal& hal, uint8_t node_id, uint32_t key_hash32, const char* name)
    : _hal(hal), _node_id(node_id), _key_hash32(key_hash32) {
    (void)name;  // sim-debug only; the node identifies by node_id / key_hash32
}

int16_t Node::route_score_from_snr(int16_t snr_q4) const {
    return static_cast<int16_t>(snr_q4 - protocol::route_snr_conservatism_q4);
}

void Node::on_init(const NodeConfig& cfg) {
    _cfg = cfg;
    // Lua: (SF_DEMOD_THRESHOLD[routing_sf] or -240) + sf_margin_q4 (dv_dual_sf.lua:8386).
    // The out-of-range fallback is the literal -240 (SF10), NOT table[12].
    const int16_t demod = (_cfg.routing_sf >= 5 && _cfg.routing_sf <= 12)
                          ? protocol::sf_demod_threshold_q4_table[_cfg.routing_sf]
                          : static_cast<int16_t>(-240);
    _routing_snr_floor_q4 = static_cast<int16_t>(demod + protocol::sf_margin_q4);
    _hal.set_rx_sf(_cfg.routing_sf);                       // listen on routing SF

    // Discovery window: boot in fast-cadence / full-page mode until we have heard
    // enough of the mesh or a bounded timeout expires (dv_dual_sf.lua:8399-8401).
    _discovery_started_ms   = _hal.now();
    _discovery_mode         = (protocol::discovery_ms > 0);
    _discovery_until_ms     = _discovery_started_ms + protocol::discovery_ms;
    _discovery_bcn_rx_count = 0;

    // Arm the first beacon spread across the (phase-dependent) period to avoid a
    // mass-boot burst (dv_dual_sf.lua:9027-9035).
    const int first_period = static_cast<int>(in_discovery() ? protocol::discovery_beacon_period_ms
                                                             : _cfg.beacon_period_ms);
    (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, first_period)), kBeaconTimerId);
    // Periodic route-aging sweep (dv_dual_sf.lua:9080-9086).
    (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
}

void Node::on_timer(uint32_t timer_id) {
    switch (timer_id) {
    case kBeaconTimerId: {
        emit_beacon("periodic");      // re-checks discovery at its top (may exit it)
        // Re-arm ±20% jitter [0.8P, 1.2P] inclusive (dv_dual_sf.lua:7858-7864).
        // Period reflects the (possibly just-exited) discovery state. Integer
        // floor division; +1 makes hi inclusive (rand_range is [lo,hi)).
        const uint32_t P  = in_discovery() ? protocol::discovery_beacon_period_ms
                                           : _cfg.beacon_period_ms;
        const int      lo = static_cast<int>(P * 4 / 5);
        const int      hi = static_cast<int>(P * 6 / 5);
        (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(lo, hi + 1)), kBeaconTimerId);
        break;
    }
    case kAgingTimerId:
        age_out_stale_routes();
        (void)_hal.after(_cfg.rt_aging_check_period_ms, kAgingTimerId);
        break;
    case kTriggeredBeaconTimerId:
        _triggered_beacon_pending = false;   // clear BEFORE emit so a re-trigger can re-arm
        emit_beacon("triggered");
        break;
    // ---- R3 data-plane timers ----
    case kRtsTimeoutTimerId:      rts_timeout_fire();      break;
    case kAckTimeoutTimerId:      ack_timeout_fire();      break;
    case kPendingRxExpiryTimerId: pending_rx_expiry_fire();break;
    case kCtsToDataGapTimerId:    do_data_tx();            break;
    case kQueueWakeupTimerId:     become_free();           break;
    case kPostAckTimerId:         do_post_ack();           break;
    case kRetryBackoffTimerId:    tx_rts_retry();          break;
    default:
        break;
    }
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

void Node::on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    if (len < 1) return;
    switch (wire::cmd_of(bytes[0])) {
        case wire::Cmd::B: ingest_beacon(bytes, len, meta); break;   // R1/R2 beacon
        case wire::Cmd::R: handle_rts (bytes, len, meta); break;     // R3 RTS  -> CTS
        case wire::Cmd::C: handle_cts (bytes, len, meta); break;     // R3 CTS  -> DATA
        case wire::Cmd::D: handle_data(bytes, len, meta); break;     // R3 DATA -> deliver/forward + ACK
        case wire::Cmd::K: handle_ack (bytes, len, meta); break;     // R3 ACK  -> done
        default: break;                                              // N (NACK) deferred; rest ignored
    }
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
    if (rt_changed) schedule_triggered_beacon();
    maybe_emit_rt_full();
}

// ---- route table ------------------------------------------------------------

RtEntry* Node::rt_find(uint8_t dest) {
    for (uint8_t i = 0; i < _rt_count; ++i) {
        if (_rt[i].dest == dest) return &_rt[i];
        if (_rt[i].dest > dest)  return nullptr;          // sorted ascending
    }
    return nullptr;
}

RtEntry* Node::rt_insert(uint8_t dest) {
    if (_rt_count >= protocol::cap_routes) return nullptr;
    uint8_t pos = 0;
    while (pos < _rt_count && _rt[pos].dest < dest) ++pos;
    for (uint8_t i = _rt_count; i > pos; --i) _rt[i] = _rt[i - 1];   // shift right
    _rt[pos]      = RtEntry{};
    _rt[pos].dest = dest;
    _rt_count++;
    return &_rt[pos];
}

bool Node::route_strictly_better(const RtCandidate& a, const RtCandidate& b) const {
    // effective_score == score in R1 (budget/suspect penalties are 0).
    const int16_t av = a.score;
    const int16_t bv = b.score;
    const bool a_viable = av >= _routing_snr_floor_q4;
    const bool b_viable = bv >= _routing_snr_floor_q4;
    if (a_viable && !b_viable) return true;
    if (b_viable && !a_viable) return false;
    if (a_viable && b_viable) {                           // both viable: hops-first
        if (a.hops < b.hops) return true;
        if (a.hops > b.hops) return false;
        return av > bv;
    }
    if (av > bv) return true;                             // both non-viable: score-first
    if (av < bv) return false;
    return a.hops < b.hops;
}

void Node::sort_candidates(RtEntry& e) {
    // Lua comparator (dv_dual_sf.lua:4248-4252):
    //   less(a,b) = strictly_better(a,b) or (not strictly_better(b,a) and score(a) > score(b))
    // Insertion sort (n <= K = 3), stable, deterministic.
    for (uint8_t i = 1; i < e.n; ++i) {
        RtCandidate key = e.candidates[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0) {
            const RtCandidate& cur = e.candidates[j];
            const bool key_less = route_strictly_better(key, cur) ||
                                  (!route_strictly_better(cur, key) && key.score > cur.score);
            if (!key_less) break;
            e.candidates[j + 1] = e.candidates[j];
            --j;
        }
        e.candidates[j + 1] = key;
    }
}

Node::MergeAction Node::rt_merge(uint8_t dest, const RtCandidate& cand) {
    RtEntry* entry = rt_find(dest);
    if (entry == nullptr) {
        entry = rt_insert(dest);
        if (entry == nullptr) { _hal.log("rt full, route dropped"); return MergeAction::none; }
        entry->candidates[0] = cand;
        entry->n     = 1;
        entry->dirty = true;
        return MergeAction::new_dest;
    }

    // Match-by-next_hop: refresh in place if cand strictly better.
    for (uint8_t i = 0; i < entry->n; ++i) {
        if (entry->candidates[i].next_hop == cand.next_hop) {
            if (route_strictly_better(cand, entry->candidates[i])) {
                const bool was_primary = (i == 0);
                entry->candidates[i] = cand;
                sort_candidates(*entry);
                const bool now_primary = (entry->candidates[0].next_hop == cand.next_hop);
                if (now_primary) { entry->dirty = true; return MergeAction::primary_refresh; }
                if (was_primary) { entry->dirty = true; return MergeAction::promote; }
                return MergeAction::alt_install;
            }
            entry->candidates[i].last_seen_ms     = cand.last_seen_ms;   // metadata only
            entry->candidates[i].n2_hop           = cand.n2_hop;
            entry->candidates[i].is_gateway       = cand.is_gateway;
            entry->candidates[i].learned_layer_id = cand.learned_layer_id;
            return MergeAction::none;
        }
    }

    // New next_hop, room to spare.
    if (entry->n < protocol::max_rt_candidates) {
        entry->candidates[entry->n] = cand;
        entry->n++;
        sort_candidates(*entry);
        if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
        return MergeAction::alt_install;
    }

    // Full table: replace the worst (last) only if cand strictly beats it.
    RtCandidate& worst = entry->candidates[entry->n - 1];
    if (!route_strictly_better(cand, worst)) return MergeAction::none;
    worst = cand;
    sort_candidates(*entry);
    if (entry->candidates[0].next_hop == cand.next_hop) { entry->dirty = true; return MergeAction::promote; }
    return MergeAction::alt_install;
}

void Node::maybe_emit_rt_full() {
    if (_rt_full_emitted || _cfg.peer_count == 0) return;   // peer_count 0 = sim telemetry off
    if (_rt_count >= _cfg.peer_count) {
        EventField f[] = { { .key = "peers", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.peer_count) } };
        _hal.emit("rt_full", f, 1);
        _rt_full_emitted = true;
    }
}

// ---- R2 route-plane hardening -----------------------------------------------

void Node::rt_remove(uint8_t idx) {
    if (idx >= _rt_count) return;
    for (uint8_t k = idx; k + 1 < _rt_count; ++k) _rt[k] = _rt[k + 1];   // shift down (reverse of rt_insert)
    --_rt_count;
    _rt[_rt_count] = RtEntry{};                                          // scrub vacated slot
}

uint32_t Node::ttl_for_hops(uint8_t hops) const {
    return (hops <= 1) ? _cfg.rt_aging_ttl_neighbor_ms : _cfg.rt_aging_ttl_remote_ms;
}

void Node::age_out_stale_routes() {
    // Walk rt[], evict each candidate past its hop-class TTL, drop empty entries,
    // dirty on primary eviction, one triggered re-beacon if any evicted
    // (dv_dual_sf.lua:5249-5302). ttl<=0 disables aging for that class.
    if (_cfg.rt_aging_ttl_neighbor_ms == 0 && _cfg.rt_aging_ttl_remote_ms == 0) return;
    const uint64_t now = _hal.now();
    bool any_evicted = false;
    uint8_t i = 0;
    while (i < _rt_count) {
        RtEntry& e = _rt[i];
        uint8_t w = 0;                    // compact survivors forward (preserves sort)
        bool primary_evicted = false;
        for (uint8_t r = 0; r < e.n; ++r) {
            const RtCandidate& c = e.candidates[r];
            const uint32_t ttl = ttl_for_hops(c.hops);
            const bool expired = (ttl > 0) && (now >= c.last_seen_ms) &&
                                 (now - c.last_seen_ms >= ttl);
            if (expired) {
                any_evicted = true;
                if (r == 0) primary_evicted = true;
                EventField f[] = {
                    { .key = "dest", .type = EventField::T::i64, .i = static_cast<int64_t>(e.dest) },
                    { .key = "slot", .type = EventField::T::str, .s = (r == 0 ? "primary" : "alt") },
                    { .key = "next", .type = EventField::T::i64, .i = static_cast<int64_t>(c.next_hop) },
                    { .key = "hops", .type = EventField::T::i64, .i = static_cast<int64_t>(c.hops) },
                };
                _hal.emit("rt_aged", f, 4);
            } else {
                if (w != r) e.candidates[w] = e.candidates[r];
                ++w;
            }
        }
        e.n = w;
        if (e.n == 0) {
            rt_remove(i);                 // do NOT advance i (entries shifted down)
        } else {
            if (primary_evicted) e.dirty = true;
            ++i;
        }
    }
    if (any_evicted) schedule_triggered_beacon();
}

void Node::rt_prune_cycle(uint8_t dest, uint8_t sender) {
    // A beacon from `sender` reaching `dest` via US closes a me->X->sender->me
    // loop for any of our candidates for `dest` whose advertised next-hop
    // (n2_hop) is `sender`. Drop them (dv_dual_sf.lua:5193-5227). Direct
    // candidates (hops==1, n2_hop==0) carry no n2_hop, so the hops>1 guard skips
    // them even when sender==0.
    RtEntry* e = rt_find(dest);
    if (e == nullptr) return;
    uint8_t w = 0;
    bool primary_pruned = false, mutated = false;
    for (uint8_t r = 0; r < e->n; ++r) {
        const RtCandidate& c = e->candidates[r];
        if (c.hops > 1 && c.n2_hop == sender) {
            mutated = true;
            if (r == 0) primary_pruned = true;
            EventField f[] = {
                { .key = "dest",   .type = EventField::T::i64, .i = static_cast<int64_t>(dest) },
                { .key = "via",    .type = EventField::T::i64, .i = static_cast<int64_t>(c.next_hop) },
                { .key = "sender", .type = EventField::T::i64, .i = static_cast<int64_t>(sender) },
            };
            _hal.emit("rt_prune", f, 3);
        } else {
            if (w != r) e->candidates[w] = e->candidates[r];
            ++w;
        }
    }
    if (!mutated) return;
    e->n = w;
    if (e->n == 0) {
        for (uint8_t i = 0; i < _rt_count; ++i)            // e dangles after rt_remove — find idx first
            if (_rt[i].dest == dest) { rt_remove(i); break; }
    } else if (primary_pruned) {
        e->dirty = true;
    }
    schedule_triggered_beacon();
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

// =============================================================================
// R3 — MAC data plane (RTS-CTS-DATA-ACK).  RTS/CTS/ACK ride routing_sf; only
// DATA rides the chosen data_sf (per-frame TxParams.sf). The SENDER never
// retunes its RX; only the RECEIVER does (set_rx_sf(data_sf) after CTS, back to
// routing_sf at DATA/expiry). See docs/specs/2026-05-30-r3-data-plane-design.md.
// =============================================================================

// 2-bit ACK SNR bucket (dv_dual_sf.lua:842; centers -16/-8/+4) — NOT the 4-bit one.
static uint8_t bucket_of_snr_2b(int snr_q4) {
    if (snr_q4 < -192) return 0;        // < -12 dB
    if (snr_q4 <  -64) return 1;        // < -4 dB
    return 2;
}

uint16_t Node::next_ctr(uint8_t dst) {
    uint16_t& c = _peer_send_counter[dst];
    c = (c >= 65535) ? 1 : static_cast<uint16_t>(c + 1);   // wraps 65535->1 (NOT a rand site)
    return c;
}

uint8_t Node::select_data_sf(uint8_t rts_sf_index) const {
    // sf_index=3 (ANY) -> our preferred data SF. 0..2 (pinned) deferred — the R3
    // gate uses ANY=3 (decision Q4). PURE (no rand) — dv_dual_sf.lua:3027.
    (void)rts_sf_index;
    return _cfg.data_sf;
}

uint32_t Node::airtime_routing_ms(uint16_t len) const {
    return airtime_ms(_cfg.routing_sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len);
}
// 3*airtime(routing, Lua RTS_LEN=8) — a TIMING constant from the Lua, NOT the 7-B C++ wire,
// so the retry rand RANGE matches the Lua and the lua-vs-meshroute streams stay aligned.
uint32_t Node::retry_jitter_ms() const { return 3 * airtime_routing_ms(8); }

void Node::do_send(uint8_t dst, const uint8_t* body, uint8_t body_len) {
    const uint16_t ctr = next_ctr(dst);
    TxItem item{};
    item.origin = _node_id; item.dst = dst; item.ctr = ctr; item.ctr_lo = static_cast<uint8_t>(ctr & 0x0F);
    item.inner[0] = 0x00; item.inner[1] = _node_id;      // src_addr_len=0 | origin | body
    for (uint8_t i = 0; i < body_len; ++i) item.inner[2 + i] = body[i];
    item.inner_len = static_cast<uint8_t>(2 + body_len);
    if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = item;
    EventField f[] = {
        { .key = "origin", .type = EventField::T::i64, .i = item.origin },
        { .key = "dst",    .type = EventField::T::i64, .i = item.dst },
        { .key = "ctr",    .type = EventField::T::i64, .i = item.ctr },
        { .key = "depth",  .type = EventField::T::i64, .i = _tx_queue_n },
    };
    _hal.emit("tx_enqueue", f, 4);                       // dm_delivery record-creation key (fid==origin)
    become_free();
}

void Node::become_free() {
    if (_pending_tx || _pending_rx) return;              // half-duplex serialize
    if (_tx_queue_n == 0) return;
    TxItem item = _tx_queue[0];                           // FIFO head (minimal: no priority/requeue ordering)
    for (uint8_t i = 1; i < _tx_queue_n; ++i) _tx_queue[i - 1] = _tx_queue[i];
    --_tx_queue_n;
    issue_send(item);
}

void Node::issue_send(const TxItem& item) {
    RtEntry* e = rt_find(item.dst);
    if (e == nullptr || e->n == 0) {                     // minimal: drop (the defer+Q drain is a later R)
        EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = item.dst } };
        _hal.emit("send_no_route", f, 1);
        return;
    }
    PendingTx pt{};
    pt.origin = item.origin; pt.dst = item.dst; pt.next = e->candidates[0].next_hop;
    pt.ctr_lo = item.ctr_lo; pt.ctr = item.ctr; pt.flags = item.flags;
    pt.inner_len = item.inner_len;
    for (uint8_t i = 0; i < item.inner_len; ++i) pt.inner[i] = item.inner[i];
    pt.chosen_data_sf = 0; pt.retries_left = protocol::rts_max_retries;
    pt.awaiting_cts = true; pt.awaiting_ack = false;
    _pending_tx = pt;
    tx_rts_retry();                                      // packs+emits the RTS + start_rts_timeout
}

void Node::tx_rts_retry() {
    if (!_pending_tx) return;
    PendingTx& pt = *_pending_tx;
    pt.awaiting_cts = true; pt.awaiting_ack = false; pt.chosen_data_sf = 0;
    rts_in rin{};
    rin.leaf_id = _cfg.leaf_id; rin.src = _node_id; rin.next = pt.next; rin.ctr_lo = pt.ctr_lo;
    rin.dst = pt.dst; rin.sf_index = 3 /*ANY*/; rin.rts_flags = 0;
    rin.payload_len = static_cast<uint8_t>(pt.inner_len + 4 /*MAC_LEN*/); rin.m_payload_id_lo16 = 0;
    uint8_t buf[9];
    const size_t l = pack_rts(rin, std::span<uint8_t>(buf, sizeof(buf)));
    if (l == 0) { _hal.log("RTS pack failed"); return; }
    TxParams p; p.sf = static_cast<int16_t>(_cfg.routing_sf); p.label = "RTS";
    _hal.tx(buf, l, p);                                  // RX stays on routing_sf
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("rts_tx", f, 3);
    start_rts_timeout();
}

void Node::handle_rts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    (void)meta;
    auto pr = parse_rts(std::span<const uint8_t>(bytes, len));
    if (!pr) return;
    const rts_out& r = *pr;
    if (r.leaf_id != _cfg.leaf_id) return;
    if (r.next != _node_id) return;                      // not addressed to us as next-hop
    { EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = r.src },
                         { .key = "dst",  .type = EventField::T::i64, .i = r.dst } };
      _hal.emit("rts_rx", f, 2); }

    // last_acked dedup: a retried RTS after we already delivered -> CTS already_received, no re-deliver.
    const uint32_t lakey = (uint32_t(r.src) << 24) | (uint32_t(r.dst) << 16) |
                           (uint32_t(r.ctr_lo) << 8) | r.payload_len;
    auto la = _last_acked_from.find(lakey);
    if (la != _last_acked_from.end() && (_hal.now() - la->second.t_ms) < protocol::last_acked_ttl_ms) {
        // Fresh within the 10s TTL (dv_dual_sf.lua:9861) — the TTL gate is what stops a
        // stale 4-bit ctr_lo alias from false-positiving on slow sustained traffic.
        cts_in cin{}; cin.ctr_lo = r.ctr_lo; cin.chosen_data_sf = la->second.chosen_data_sf;
        cin.already_received = true; cin.to = r.src;
        uint8_t cbuf[3]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, 3));
        TxParams cp; cp.sf = static_cast<int16_t>(_cfg.routing_sf); cp.label = "CTS";
        _hal.tx(cbuf, cl, cp);
        EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                           { .key = "dup", .type = EventField::T::boolean, .b = true } };
        _hal.emit("cts_tx", f, 2);
        return;
    }
    // A retried RTS for the SAME flight while we still await its DATA -> re-CTS + restart
    // the expiry (dv_dual_sf.lua:218 CTS-dup) so the sender's retry gets a fresh CTS.
    if (_pending_rx && _pending_rx->from == r.src && _pending_rx->dst == r.dst &&
        _pending_rx->ctr_lo == r.ctr_lo) {
        cts_in cin{}; cin.ctr_lo = r.ctr_lo; cin.chosen_data_sf = _pending_rx->chosen_data_sf;
        cin.already_received = false; cin.to = r.src;
        uint8_t cbuf[3]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, 3));
        TxParams cp; cp.sf = static_cast<int16_t>(_cfg.routing_sf); cp.label = "CTS";
        _hal.tx(cbuf, cl, cp);
        EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                           { .key = "dup", .type = EventField::T::boolean, .b = true } };
        _hal.emit("cts_tx", f, 2);
        start_pending_rx_expiry(_pending_rx->payload_len);
        return;
    }
    if (_pending_tx || _pending_rx) return;              // busy (different flight) -> drop (NACK deferred)

    const uint8_t sf = select_data_sf(r.sf_index);
    PendingRx prx{}; prx.from = r.src; prx.dst = r.dst; prx.ctr_lo = r.ctr_lo;
    prx.chosen_data_sf = sf; prx.payload_len = r.payload_len; prx.set_at_ms = _hal.now();
    _pending_rx = prx;
    start_pending_rx_expiry(r.payload_len);
    cts_in cin{}; cin.ctr_lo = r.ctr_lo; cin.chosen_data_sf = sf; cin.already_received = false; cin.to = r.src;
    uint8_t cbuf[3]; const size_t cl = pack_cts(cin, std::span<uint8_t>(cbuf, 3));
    TxParams cp; cp.sf = static_cast<int16_t>(_cfg.routing_sf); cp.label = "CTS";
    _hal.tx(cbuf, cl, cp);                               // CTS on routing_sf
    { EventField f[] = { { .key = "to", .type = EventField::T::i64, .i = r.src },
                         { .key = "sf", .type = EventField::T::i64, .i = sf } };
      _hal.emit("cts_tx", f, 2); }
    _hal.set_rx_sf(sf);                                  // NOW retune RX to hear the DATA on the data SF
}

void Node::handle_cts(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pc = parse_cts(std::span<const uint8_t>(bytes, len));
    if (!pc) return;
    const cts_out& c = *pc;
    if (c.to != _node_id) return;
    if (!_pending_tx || !_pending_tx->awaiting_cts || _pending_tx->ctr_lo != c.ctr_lo) return;
    if (static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) return;
    _hal.cancel(kRtsTimeoutTimerId);                     // else it fires same-tick and burns a retry
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired rts_timeout
    _pending_tx->awaiting_cts = false;
    _pending_tx->chosen_data_sf = c.chosen_data_sf;
    { EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) },
                         { .key = "sf",   .type = EventField::T::i64, .i = c.chosen_data_sf } };
      _hal.emit("cts_rx", f, 2); }
    if (c.already_received) { _pending_tx.reset(); become_free(); return; }   // already delivered upstream
    (void)_hal.after(protocol::cts_to_data_gap_ms, kCtsToDataGapTimerId);     // fixed 5ms gap (NOT rand)
}

void Node::do_data_tx() {
    if (!_pending_tx || _pending_tx->awaiting_ack || _pending_tx->chosen_data_sf == 0) return;
    PendingTx& pt = *_pending_tx;
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in din{};
    din.addr_len = 0; din.flags = pt.flags; din.next = pt.next; din.dst = pt.dst;
    din.hops_remaining = protocol::hop_budget_max_initial; din.committed_hops = 0;
    din.prev_fwd_rt_hops = 0; din.ctr = pt.ctr;
    din.visited = {};                                    // empty -> 6 zero bytes
    din.inner = std::span<const uint8_t>(pt.inner, pt.inner_len);
    din.mac   = std::span<const uint8_t>(mac, 4);
    uint8_t buf[protocol::lora_max_frame_bytes];
    const size_t dlen = pack_data(din, std::span<uint8_t>(buf, sizeof(buf)));
    if (dlen == 0) { _hal.log("DATA pack failed"); return; }
    TxParams p; p.sf = static_cast<int16_t>(pt.chosen_data_sf); p.label = "DATA";
    _hal.tx(buf, dlen, p);                               // DATA on the chosen data SF; RX stays routing_sf
    EventField f[] = {
        { .key = "dst",  .type = EventField::T::i64, .i = pt.dst },
        { .key = "next", .type = EventField::T::i64, .i = pt.next },
        { .key = "ctr",  .type = EventField::T::i64, .i = pt.ctr },
    };
    _hal.emit("data_tx", f, 3);
    pt.awaiting_ack = true;
    start_ack_timeout();
}

void Node::handle_data(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pd = parse_data(std::span<const uint8_t>(bytes, len));
    if (!pd) return;
    const data_out& d = *pd;
    if (d.next != _node_id) return;
    if (!_pending_rx || _pending_rx->ctr_lo != d.ctr_lo4) return;
    const uint8_t from = static_cast<uint8_t>(meta.src_hint);
    { EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = from },
                         { .key = "dst",  .type = EventField::T::i64, .i = d.dst } };
      _hal.emit("data_rx", f, 2); }
    const uint8_t rx_sf = _pending_rx->chosen_data_sf;
    const uint8_t pl    = _pending_rx->payload_len;
    _hal.cancel(kPendingRxExpiryTimerId);
    _hal.set_rx_sf(_cfg.routing_sf);                     // receiver retunes back
    _pending_rx.reset();
    // last_acked cache: a retried RTS gets CTS already_received=1 instead of re-delivery.
    const uint32_t lakey = (uint32_t(from) << 24) | (uint32_t(d.dst) << 16) |
                           (uint32_t(d.ctr_lo4) << 8) | pl;
    const uint64_t nowm = _hal.now();
    for (auto it = _last_acked_from.begin(); it != _last_acked_from.end(); )   // prune expired (10s TTL)
        { if ((nowm - it->second.t_ms) >= protocol::last_acked_ttl_ms) it = _last_acked_from.erase(it); else ++it; }
    if (_last_acked_from.size() < protocol::cap_seen_origins)                  // bounded (reuse the 256 cap)
        _last_acked_from[lakey] = LastAcked{ rx_sf, nowm };
    // ACK on routing_sf (2-bit SNR bucket; budget_hint=0 in R3).
    ack_in ain{}; ain.ctr_lo = d.ctr_lo4; ain.budget_hint = 0;
    ain.snr_bucket = bucket_of_snr_2b(protocol::db_to_q4(meta.snr_db)); ain.to = from;
    uint8_t abuf[3]; const size_t al = pack_ack(ain, std::span<uint8_t>(abuf, 3));
    TxParams ap; ap.sf = static_cast<int16_t>(_cfg.routing_sf); ap.label = "ACK";
    _hal.tx(abuf, al, ap);
    { EventField f[] = { { .key = "to",  .type = EventField::T::i64, .i = from },
                         { .key = "ctr", .type = EventField::T::i64, .i = d.ctr } };
      _hal.emit("ack_tx", f, 2); }
    // origin from the DATA inner; seen-origin dedup (forward double-delivery guard).
    auto inner = data_inner(std::span<const uint8_t>(bytes, len), d);
    auto ui = parse_unicast_inner(inner);
    const uint8_t origin = ui ? ui->origin : from;
    const uint32_t sokey = (uint32_t(origin) << 24) | (uint32_t(d.dst) << 16) | d.ctr;
    auto so = _seen_origins.find(sokey);
    if (so != _seen_origins.end() && so->second > nowm) { become_free(); return; }   // LIVE dup -> ACK only
    for (auto it = _seen_origins.begin(); it != _seen_origins.end(); )               // prune expired (30s TTL)
        { if (it->second <= nowm) it = _seen_origins.erase(it); else ++it; }
    if (_seen_origins.size() < protocol::cap_seen_origins)                           // bounded (256)
        _seen_origins[sokey] = nowm + protocol::seen_origin_ttl_ms;
    // defer deliver/forward by the ACK airtime so it doesn't share a sim step with the ACK.
    _post_ack = PostAck{};
    _post_ack.pending = true; _post_ack.is_forward = (d.dst != _node_id);
    _post_ack.origin = origin; _post_ack.dst = d.dst; _post_ack.ctr_lo = d.ctr_lo4;
    _post_ack.ctr = d.ctr; _post_ack.flags = d.flags; _post_ack.previous_hop = from;
    _post_ack.inner_len = static_cast<uint8_t>(inner.size() <= protocol::max_payload_bytes_hard_cap
                                               ? inner.size() : protocol::max_payload_bytes_hard_cap);
    for (uint8_t i = 0; i < _post_ack.inner_len; ++i) _post_ack.inner[i] = inner[i];
    (void)_hal.after(airtime_routing_ms(3) + 1, kPostAckTimerId);
}

void Node::do_post_ack() {
    if (!_post_ack.pending) return;
    const PostAck pa = _post_ack;
    _post_ack.pending = false;
    if (!pa.is_forward) {
        // deliver: body = inner[2..] (skip src_addr_len + origin), null-terminated for the event.
        char body[protocol::max_payload_bytes_hard_cap + 1];
        const uint8_t blen = (pa.inner_len > 2) ? static_cast<uint8_t>(pa.inner_len - 2) : 0;
        for (uint8_t i = 0; i < blen; ++i) body[i] = static_cast<char>(pa.inner[2 + i]);
        body[blen] = '\0';
        EventField f[] = {
            { .key = "origin",  .type = EventField::T::i64, .i = pa.origin },
            { .key = "dst",     .type = EventField::T::i64, .i = pa.dst },
            { .key = "ctr",     .type = EventField::T::i64, .i = pa.ctr },
            { .key = "payload", .type = EventField::T::str, .s = body },     // dm_delivery keys (dst, payload)
        };
        _hal.emit("delivered", f, 4);
        become_free();
    } else {
        TxItem it{};
        it.origin = pa.origin; it.dst = pa.dst; it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo;
        it.flags = pa.flags; it.is_forward = true; it.previous_hop = pa.previous_hop;
        it.inner_len = pa.inner_len;
        for (uint8_t i = 0; i < pa.inner_len; ++i) it.inner[i] = pa.inner[i];
        if (_tx_queue_n < kTxQueueCap) _tx_queue[_tx_queue_n++] = it;
        become_free();
    }
}

void Node::handle_ack(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    auto pk = parse_ack(std::span<const uint8_t>(bytes, len));
    if (!pk) return;
    const ack_out& k = *pk;
    if (k.to != _node_id) return;
    if (!_pending_tx || !_pending_tx->awaiting_ack || _pending_tx->ctr_lo != k.ctr_lo) return;
    if (static_cast<uint8_t>(meta.src_hint) != _pending_tx->next) return;
    _hal.cancel(kAckTimeoutTimerId);
    _hal.cancel(kRetryBackoffTimerId);                   // drop a stale retry armed by a just-fired ack_timeout
    EventField f[] = { { .key = "from", .type = EventField::T::i64, .i = static_cast<uint8_t>(meta.src_hint) },
                       { .key = "ctr",  .type = EventField::T::i64, .i = _pending_tx->ctr } };
    _hal.emit("ack_rx", f, 2);
    _pending_tx.reset();
    become_free();
}

void Node::start_rts_timeout() {
    const uint32_t base = airtime_routing_ms(8) + airtime_routing_ms(3);   // Lua RTS_LEN=8 + CTS (timing matches Lua)
    const uint8_t  attempt = static_cast<uint8_t>(protocol::rts_max_retries -
                              (_pending_tx ? _pending_tx->retries_left : 0));
    const uint32_t shift = attempt < 2 ? attempt : 2;                       // x2 backoff, cap x4
    (void)_hal.after((base << shift) + 1, kRtsTimeoutTimerId);
}
void Node::start_ack_timeout() {
    const uint8_t  sf  = _pending_tx ? _pending_tx->chosen_data_sf : _cfg.data_sf;
    const uint16_t len = static_cast<uint16_t>(18 + (_pending_tx ? _pending_tx->inner_len : 0));
    const uint32_t base = airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len)
                        + airtime_routing_ms(3);
    (void)_hal.after(base + 2, kAckTimeoutTimerId);
}
void Node::start_pending_rx_expiry(uint8_t payload_len) {
    const uint8_t  sf  = _pending_rx ? _pending_rx->chosen_data_sf : _cfg.data_sf;
    const uint16_t len = static_cast<uint16_t>(14 + payload_len);
    const uint32_t t = airtime_routing_ms(3) + protocol::cts_to_data_gap_ms +
                       airtime_ms(sf, _cfg.radio_bw_hz, _cfg.radio_cr, protocol::preamble_sym, len) + 2;
    (void)_hal.after(t, kPendingRxExpiryTimerId);
}

void Node::rts_timeout_fire() {
    if (!_pending_tx || !_pending_tx->awaiting_cts) return;          // stale (CTS already matched)
    if (_pending_rx) { (void)_hal.after(protocol::rts_busy_retry_ms, kRtsTimeoutTimerId); return; }
    if (_pending_tx->retries_left > 0) {
        --_pending_tx->retries_left;
        const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // RNG site #1
        (void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
    } else {
        EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = _pending_tx->dst },
                           { .key = "ctr", .type = EventField::T::i64, .i = _pending_tx->ctr } };
        _hal.emit("rts_giveup", f, 2);
        _pending_tx.reset();
        become_free();
    }
}
void Node::ack_timeout_fire() {
    if (!_pending_tx || !_pending_tx->awaiting_ack) return;
    if (_pending_rx) { (void)_hal.after(protocol::rts_busy_retry_ms, kAckTimeoutTimerId); return; }
    if (_pending_tx->retries_left > 0) {
        --_pending_tx->retries_left;
        _pending_tx->awaiting_ack = false; _pending_tx->awaiting_cts = false; _pending_tx->chosen_data_sf = 0;
        const int jit = _hal.rand_range(0, static_cast<int>(retry_jitter_ms()) + 1);   // RNG site #2
        (void)_hal.after(static_cast<uint32_t>(jit), kRetryBackoffTimerId);
    } else {
        EventField f[] = { { .key = "dst", .type = EventField::T::i64, .i = _pending_tx->dst },
                           { .key = "ctr", .type = EventField::T::i64, .i = _pending_tx->ctr } };
        _hal.emit("data_ack_giveup", f, 2);
        _pending_tx.reset();
        become_free();
    }
}
void Node::pending_rx_expiry_fire() {
    if (!_pending_rx) return;
    _hal.set_rx_sf(_cfg.routing_sf);
    _pending_rx.reset();
    _hal.emit("data_rx_timeout", nullptr, 0);
    become_free();
}

void Node::on_command(const char* cmd, char* out_reply, size_t reply_cap) {
    if (out_reply && reply_cap > 0) out_reply[0] = '\0';
    if (!cmd) return;
    // "send <numeric-dst> <body>" — the host (SimController) name-resolves to id (Q1=b).
    const char* p = cmd;
    if (!(p[0] == 's' && p[1] == 'e' && p[2] == 'n' && p[3] == 'd' && p[4] == ' ')) return;
    p += 5;
    while (*p == ' ') ++p;
    if (*p < '0' || *p > '9') return;
    unsigned dst = 0;
    while (*p >= '0' && *p <= '9') { dst = dst * 10 + static_cast<unsigned>(*p - '0'); ++p; }
    if (dst > 254) return;
    while (*p == ' ') ++p;
    const char* body = p;
    size_t blen = 0;
    while (body[blen] != '\0') ++blen;
    if (blen == 0) return;
    const size_t cap = protocol::max_payload_bytes_hard_cap - 2;          // inner = 2 + body
    if (blen > cap) blen = cap;
    do_send(static_cast<uint8_t>(dst), reinterpret_cast<const uint8_t*>(body), static_cast<uint8_t>(blen));
    if (out_reply && reply_cap > 2) { out_reply[0] = 'O'; out_reply[1] = 'K'; out_reply[2] = '\0'; }
}

// ---- callbacks deferred to later R-iterations -------------------------------
void Node::on_radio_busy(const BusyInfo& info)     { (void)info; }       // R4 (LBT defer)
void Node::on_preamble_detected(uint64_t time_ms)  { (void)time_ms; }    // R4 (throttle witness)

}  // namespace meshroute
