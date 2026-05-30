// MeshRoute — lib/core/node.cpp  (R1: beacon emit + route-table build)
//
// The Node emits a periodic §10 BCN over each rt[dest] primary candidate and
// ingests received beacons into a bounded DV route table. Behaviour mirrors
// dv_dual_sf.lua (beacon_fire / the on_recv "B" branch / rt_merge); the wire is
// the C5 cmd-nibble form. See docs/specs/2026-05-29-r1-beacon-emit-design.md.
#include "node.h"

#include "frame_codec.h"
#include "wire.h"

#include <span>

namespace meshroute {

// Beacon timer id (the Node owns the id namespace; see hal.h).
static constexpr uint32_t kBeaconTimerId = 1;

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

    // Arm the first beacon spread across the period to avoid a mass-boot burst
    // (dv_dual_sf.lua:9027-9035). R1 uses a single configured period (the
    // discovery fast-cadence state machine is a later iteration).
    const int first_period = static_cast<int>(_cfg.beacon_period_ms);
    (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(0, first_period)), kBeaconTimerId);
}

void Node::on_timer(uint32_t timer_id) {
    if (timer_id != kBeaconTimerId) return;
    emit_beacon();
    // Re-arm unconditionally with ±20% jitter [0.8P, 1.2P] inclusive
    // (dv_dual_sf.lua:7858-7864). Integer floor division; +1 makes hi inclusive
    // (rand_range is [lo,hi)). The same id re-arms in place.
    const uint32_t P  = _cfg.beacon_period_ms;
    const int      lo = static_cast<int>(P * 4 / 5);
    const int      hi = static_cast<int>(P * 6 / 5);
    (void)_hal.after(static_cast<uint32_t>(_hal.rand_range(lo, hi + 1)), kBeaconTimerId);
}

void Node::emit_beacon() {
    // FULL page: one entry per rt[dest] primary, ascending dest (the table is
    // kept sorted). R1 defers dirty-only differential paging. A MOBILE node
    // advertises ZERO route entries (identity-only beacon) — it must never be
    // used as transit (dv_dual_sf.lua:1716-1721). This suppression also closes
    // the rt_merge mobile-as-transit case for R1's beacon plane: a neighbour
    // never receives carried entries from a mobile node, so it cannot install a
    // route via it. (The explicit rt_merge route_uses_mobile_as_transit guard,
    // dv_dual_sf.lua:4485, rides with the peer-mobility plane in a later iteration.)
    beacon_entry entries[kMaxBeaconEntries];
    uint8_t n = 0;
    if (!_cfg.is_mobile) {
        for (uint8_t i = 0; i < _rt_count && n < kMaxBeaconEntries; ++i) {
            const RtCandidate& p = _rt[i].candidates[0];
            entries[n].dest         = _rt[i].dest;
            entries[n].next         = p.next_hop;
            entries[n].score_bucket = static_cast<uint8_t>(bucket_of_snr_4b(p.score));
            entries[n].is_gateway   = p.is_gateway;
            entries[n].hops         = p.hops;
            ++n;
        }
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

    EventField f[] = {
        { .key = "n_entries",  .type = EventField::T::i64, .i = static_cast<int64_t>(n) },
        { .key = "rt_total",   .type = EventField::T::i64, .i = static_cast<int64_t>(_rt_count) },
        { .key = "routing_sf", .type = EventField::T::i64, .i = static_cast<int64_t>(_cfg.routing_sf) },
        { .key = "result",     .type = EventField::T::i64, .i = static_cast<int64_t>(static_cast<int>(r)) },
    };
    _hal.emit("beacon_tx", f, 4);
}

void Node::on_recv(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    if (len < 1) return;
    if (wire::cmd_of(bytes[0]) != wire::Cmd::B) return;   // R1: only beacons exist
    ingest_beacon(bytes, len, meta);
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

    const uint64_t now         = _hal.now();
    const int16_t  meta_snr_q4 = protocol::db_to_q4(meta.snr_db);

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
        if (a == MergeAction::new_dest || a == MergeAction::promote)
            emit_rt_update(_hal, b.src, b.src, cand.score, 1, "primary");
        else if (a == MergeAction::alt_install)
            emit_rt_update(_hal, b.src, b.src, cand.score, 1, "alt");
    }

    // DV merge: each carried entry is a route via the sender (dv_dual_sf.lua:9620-9678).
    for (uint8_t i = 0; i < b.n_entries; ++i) {
        auto pe = parse_beacon_entry(std::span<const uint8_t>(bytes, len), b, i);
        if (!pe) continue;
        const beacon_entry& e = *pe;
        if (e.dest == _node_id) continue;                 // split-horizon
        if (e.next == _node_id) continue;                 // R1: rt_prune_cycle deferred (R2)
        // R1: peer-suspect skip is a no-op (no liveness plane → suspect_level 0).
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
        if (a == MergeAction::new_dest || a == MergeAction::promote)
            emit_rt_update(_hal, e.dest, b.src, combined_score, cand.hops, "primary");
        else if (a == MergeAction::alt_install)
            emit_rt_update(_hal, e.dest, b.src, combined_score, cand.hops, "alt");
    }

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

// ---- callbacks deferred to later R-iterations -------------------------------
void Node::on_radio_busy(const BusyInfo& info)     { (void)info; }       // R4 (LBT defer)
void Node::on_preamble_detected(uint64_t time_ms)  { (void)time_ms; }    // R4 (throttle witness)
void Node::on_command(const char* cmd, char* out_reply, size_t reply_cap) {
    (void)cmd;
    if (out_reply && reply_cap > 0) out_reply[0] = '\0';
}

}  // namespace meshroute
