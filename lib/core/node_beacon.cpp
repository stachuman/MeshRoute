// MeshRoute — lib/core/node_beacon.cpp  (R1/R2 beacon emit + ingest)
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Node methods for the §10 BCN plane: periodic/triggered beacon emit (dirty-only
// differential pages), beacon ingest + DV merge, the discovery fast-cadence exit,
// and the triggered-beacon scheduler. Plus the file-local SNR wire-bucket
// round-trip and the rt_update telemetry helper. Behaviour mirrors dv_dual_sf.lua.
// Part of the Node class (declared in node.h); split out of node.cpp for readability.
#include "node.h"
#include "leaf_config.h"   // R6.1: leaf_config_hash — the misconfig fingerprint

#include "frame_codec.h"
#include "wire.h"          // §7c: wire::cmd_of/flags_of for the pre-parse beacon version gate

#include <span>
#include <cstring>    // strcmp — kind=="sync" full-table gate
#include <cmath>      // lround — duty_ppm for the config_hash

namespace MESHROUTE_NS {

// Beacon-entry array SIZE / absolute ceiling = the theoretical max page (8-B header, no schedule/bitmap/ext):
// (beacon_max_bytes - 8) / 4 = 35. The PER-BEACON cap is computed at runtime by beacon_max_entries() from the
// actual schedule/bitmap/ext bytes this beacon carries (a TRUE byte-budget — no more silent overflow drop).
static constexpr uint8_t kMaxBeaconEntries =
    static_cast<uint8_t>((protocol::beacon_max_bytes - 8) / 4);  // = 35

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
    if (dest == 0xFF || dest == 0 || dest == _node_id || via == 0xFF || via == 0) return;   // §P0: 0 = reserved sentinel
    RtCandidate cand{};
    cand.next_hop = via; cand.score = route_score_from_snr(snr_q4); cand.hops = hops;
    cand.is_gateway = false; cand.last_seen_ms = _hal.now(); cand.learned_leaf = _cfg.leaf_id;
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
    if (sender == 0xFF || sender == 0 || sender == _node_id) return false;   // unknown/reserved id (0/0xFF), or self (§P0)
    mark_dest_seen(sender);                          // §P1: a frame heard FROM a direct neighbour -> freshness stamp...
    clear_peer_suspect(sender, "rx_frame");          // ...and it's demonstrably alive -> clear any timeout/suspect state (Lua dv:9325-9326)
    RtCandidate cand{};
    cand.next_hop         = sender;
    cand.score            = route_score_from_snr(snr_q4);
    cand.hops             = 1;
    cand.is_gateway       = is_gw;
    cand.last_seen_ms     = _hal.now();
    cand.learned_leaf = _cfg.leaf_id;
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

// Build our beacon's type-4 gateway-layer TLV (Lua build_gateway_layer_ext dv:1513). Entries: (a) if WE are a gateway,
// self-advert {our active-leaf node_id -> the OTHER leaf we serve}; (b) EVERY node re-gossips its valid _bridged_layers
// rows. Skip dest_leaf == our active leaf (useless). Dedup gw_id, sort by recency, cap 9. Returns 0 (NO TLV) when empty
// — the single-layer / s18 keystone guard (no entries -> no bytes -> wire byte-identical).
size_t Node::build_gateway_layer_ext(uint8_t* out, size_t cap) {
    prune_aged_bridged_layers(_hal.now());
    const uint8_t active_leaf = _cfg.leaf_id;
    GwLayerEntry entries[protocol::bridged_layers_max_per_tlv];
    uint64_t     seen_ms[protocol::bridged_layers_max_per_tlv];
    uint8_t      n = 0;
    uint8_t      seen_gw[32] = {};                                  // each gw_id at most once (Lua seen_in_tlv)
    auto try_add = [&](uint8_t gw_id, uint8_t dest_leaf, uint64_t ms) {
        if (n >= protocol::bridged_layers_max_per_tlv) return;
        if (dest_leaf == active_leaf) return;                      // advertising the receiver's own leaf is useless
        if (seen_gw[gw_id >> 3] & (1u << (gw_id & 7))) return;
        seen_gw[gw_id >> 3] = static_cast<uint8_t>(seen_gw[gw_id >> 3] | (1u << (gw_id & 7)));
        entries[n] = GwLayerEntry{ gw_id, dest_leaf }; seen_ms[n] = ms; ++n;
    };
    // (a) Self-advert (gateway only): our active-leaf node_id bridges TO the other leaf we serve.
    if (_cfg.is_gateway && _n_layers == 2) {
        const uint8_t active_idx = static_cast<uint8_t>(_active - &_layers[0]);
        const uint8_t other_leaf = static_cast<uint8_t>(_cfg.layers[active_idx ^ 1].layer_id & 0x0F);
        try_add(_node_id, other_leaf, _hal.now());                 // _node_id follows the active leaf
    }
    // (b) Propagate (ALL nodes): re-gossip every valid learned row.
    for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i)
        if (_bridged_layers[i].valid)
            try_add(_bridged_layers[i].gw_id, _bridged_layers[i].dest_leaf, _bridged_layers[i].last_seen_ms);
    if (n == 0) return 0;                                          // KEYSTONE: no entries -> NO TLV (s18 byte-identical)
    // top-K by recency (last_seen desc, then gw_id asc) — Lua dv:1558. Tiny N (<=9): insertion-ish selection sort.
    for (uint8_t a = 0; a < n; ++a)
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < n; ++b)
            if (seen_ms[b] > seen_ms[a] || (seen_ms[b] == seen_ms[a] && entries[b].gw_id < entries[a].gw_id)) {
                const GwLayerEntry te = entries[a]; entries[a] = entries[b]; entries[b] = te;
                const uint64_t     ts = seen_ms[a]; seen_ms[a] = seen_ms[b]; seen_ms[b] = ts;
            }
    return pack_gateway_layer_tlv(entries, n, std::span<uint8_t>(out, cap));
}

// §P4 build the suspect-node gossip TLV from our LOCALLY-observed advertise windows (Lua build_suspect_nodes_ext@1373).
// Collect peers whose advertise window is still open (dead_advertise_until>now => DEAD; else suspect_advertise_until>now
// => SILENT), skip self, sort (state DESC then node_id ASC), then emit type-2 LIVENESS_STATE (2B/entry) if ANY is DEAD,
// else type-1 SUSPECT_NODES (1B/id). 0 = nothing to advertise. Reads ONLY the advertise windows (set by local
// rts_timeout), so a gossip-learned tier is never re-advertised (anti-storm).
size_t Node::build_suspect_ext(uint8_t* out, size_t cap) {
    if (protocol::peer_suspect_bcn_max == 0) return 0;             // hard kill-switch (dv:1374)
    const uint64_t now = _hal.now();
    SuspectEntry rec[protocol::cap_peer_liveness];
    uint8_t n = 0;
    bool any_dead = false;
    for (uint8_t i = 0; i < _active->_peer_liveness_n && n < protocol::cap_peer_liveness; ++i) {
        const PeerLiveness& p = _active->_peer_liveness[i];
        if (p.node_id == 0 || p.node_id == _node_id) continue;     // never gossip about self (dv:1381)
        uint8_t state = 0;
        if      (p.dead_advertise_until_ms    > now) { state = 3; any_dead = true; }
        else if (p.suspect_advertise_until_ms > now) { state = 2; }
        else continue;                                             // advertise window closed (heard-from clears it / TTL)
        rec[n++] = SuspectEntry{ p.node_id, state };
    }
    if (n == 0) return 0;
    // sort: state DESC (dead first so the cap drops silents, not deaths), then node_id ASC (dv:1394-1397). Small N.
    for (uint8_t a = 0; a < n; ++a)
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < n; ++b)
            if (rec[b].state > rec[a].state || (rec[b].state == rec[a].state && rec[b].node_id < rec[a].node_id)) {
                const SuspectEntry t = rec[a]; rec[a] = rec[b]; rec[b] = t;
            }
    if (any_dead)                                                  // type 2: cap to peer_liveness_state_bcn_max (7) so 2N<=14 (Lua wraps at 8 — fixed)
        return pack_liveness_state_tlv(rec, n, std::span<uint8_t>(out, cap));
    uint8_t ids[protocol::cap_peer_liveness];                      // type 1: SILENT-only -> a bare id list (pack clamps to peer_suspect_bcn_max=8)
    for (uint8_t i = 0; i < n; ++i) ids[i] = rec[i].node_id;
    return pack_suspect_nodes_tlv(ids, n, std::span<uint8_t>(out, cap));
}

// §P4 apply a received suspect/liveness TLV (Lua dv:9627-9686): mark each named peer at the advertised tier as a REMOTE
// observation (remote_src = the beacon sender). Skip self (never self-mark) and skip the gossiper itself; ignore state 0.
void Node::apply_suspect_gossip(const SuspectEntry* e, uint8_t n, uint8_t bcn_src) {
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t id = e[i].node_id, state = e[i].state;
        if (id == 0 || id == _node_id || id == bcn_src || state == 0) continue;   // never self; never the gossiper (dv:9634/9665)
        mark_peer_suspect(id, state, state >= 2 ? "bcn_liveness" : "bcn_suspect", /*remote_src=*/bcn_src);
    }
}

// Slice 3 §1: bidirectionality detection from a neighbour's heard-set. `advertiser` = P (we heard P's beacon, so
// P->us works). Scan P's hops==1 entries for [dest==self]: PRESENT => P hears us => us<->P CONFIRMED. ABSENT in a
// COMPLETE page => P does NOT hear us => us->P ONE_WAY. ABSENT in a TRUNCATED page (complete=false) => no change.
// Keys ONLY on dest==self/presence — NEVER reads entries[i].degraded (endpoint override: our live decode of P's
// beacon is proof P->us works, overriding any stale third-party degraded view).
void Node::update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* entries, uint8_t n, bool complete) {
    if (advertiser == 0 || advertiser == 0xFF || advertiser == _node_id) return;   // §P0 sentinel / self
    bool present = false;
    for (uint8_t i = 0; i < n; ++i)
        if (entries[i].hops == 1 && entries[i].dest == _node_id) { present = true; break; }
    if (present) { note_link_confirmed(advertiser); return; }   // CONFIRMED (Slice 2 hook: fan-out + MR_EMIT)
    if (!complete) return;                                      // truncated page -> absence is not authoritative
    if (_active->_link_bidi[advertiser] != static_cast<uint8_t>(LinkBidi::one_way)) {
        _active->_link_bidi[advertiser] = static_cast<uint8_t>(LinkBidi::one_way);
        MR_EMIT("link_one_way", EF_I("next_hop", advertiser));
    }
}

void Node::emit_beacon(const char* kind) {
    // §P0: an UNPROVISIONED node (id 0) must NEVER advertise routes — its id is the reserved sentinel, so neighbours
    // would install a route to "0" that then propagates. Guard the COMMON emit path (covers periodic + triggered +
    // gateway-window + sync), broader than periodic_beacon_fire's join_required gate. A node beacons only once it has
    // claimed a short id.
    if (_node_id == 0) return;
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

    // §F2 byte-budget reorder: build the VARIABLE blocks (schedule / seen-bitmap / ext) FIRST so their on-wire
    // sizes are known, THEN size the route-entry page to whatever bytes remain. A full page + a populated ext
    // TLV can no longer overflow beacon_max_bytes (the old fixed cap ignored ext/schedule → silent drop).
    beacon_in in{};
    in.leaf_id      = _cfg.leaf_id;
    in.self_gateway = _cfg.is_gateway;
    in.is_mobile    = _cfg.is_mobile;
    in.src          = _node_id;
    in.key_hash32   = _key_hash32;
    in.lineage_id   = _cfg.lineage_id;                  // R6.1 leaf-config header (FLAG-DAY: always present)
    in.config_epoch = _cfg.config_epoch;
    in.config_hash  = cfg_config_hash();
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
        in.gateway_spread_nibble = gateway_spread_nibble();      // §3e: advertise the herd-spread hint so senders jitter across the window
    }
    // Seen bitmap: a 32-byte (256-bit) set of every node_id we know is alive — from direct
    // frames (dest_seen_ms) and from route entries (rt[].candidates[0].last_seen_ms), within
    // seen_bitmap_ttl_ms (30 min). The receiver uses it to passively keepalive routes via the
    // sender and to learn indirect connectivity (Lua build_seen_bitmap@1283).
    // §seen-bitmap cost-reduction: include the bitmap ONLY on steady-state (dirty_only) beacons — omit it on
    // full-page (discovery/sync) beacons so those carry the full route-entry budget (fast convergence).
    // A GATEWAY NEVER emits the bitmap: airtime/duty-cycle (it time-multiplexes leaves on a tight window budget),
    // AND a gateway-emitted bitmap is the only path by which freshness would cross the layer boundary — so this
    // also means NO cross-layer bitmap propagation, by design. A gateway STILL RECEIVES + applies others' bitmaps
    // to keep its OWN routes fresh (the ingest_beacon apply path has no gateway exclusion). (NB: sbuf is
    // FUNCTION-scoped — in.seen_bitmap's span must stay valid through pack_beacon; old in-if sbuf was a dangling span.)
    const bool include_bitmap = _cfg.seen_bitmap_enabled && !_cfg.is_mobile && !_cfg.is_gateway && dirty_only;
    uint8_t sbuf[32] = {};
    if (include_bitmap) {
        const uint64_t sb_now = _hal.now();
        const uint64_t sb_cut = (sb_now >= protocol::seen_bitmap_ttl_ms) ? sb_now - protocol::seen_bitmap_ttl_ms : 0;
        // §non-transitive gossip (Lua-prototype decision): EMIT only peers seen DIRECTLY. PeerLiveness.dest_seen_ms
        // is stamped SOLELY by mark_dest_seen (direct frame RX) — apply_seen_bitmap never touches it — so a peer we
        // only learned from ANOTHER node's bitmap is NOT re-emitted. (is_next_hop_fresh still CONSUMES the full
        // _dest_seen_ms[256] array incl. gossiped ids — the rule is consume-all, propagate-direct-only. Re-emitting
        // gossiped freshness laundered stale cross-layer routes into looking perpetually fresh → s15/s16 regression.)
        for (uint8_t i = 0; i < _active->_peer_liveness_n; ++i) {
            const uint8_t nid = _active->_peer_liveness[i].node_id;
            const uint64_t ds = _active->_peer_liveness[i].dest_seen_ms;
            if (nid > 0 && nid < 0xFF && ds != 0 && ds >= sb_cut)
                sbuf[nid >> 3] |= static_cast<uint8_t>(1u << (nid & 7));
        }
        for (uint8_t i = 0; i < _active->_rt_count; ++i) {
            const uint8_t d = _active->_rt[i].dest;
            if (_active->_rt[i].n > 0 && _active->_rt[i].candidates[0].last_seen_ms >= sb_cut)
                sbuf[d >> 3] |= static_cast<uint8_t>(1u << (d & 7));
        }
        sbuf[_node_id >> 3] |= static_cast<uint8_t>(1u << (_node_id & 7));
        in.seen_bitmap = std::span<const uint8_t>(sbuf, 32);
    }
    // ROADMAP §3: advertise our dirty channel msgs in the BCN digest ext-TLV (gateways skip — Principle 11).
    // build_channel_digest_ext is draw-free; its ad_count/dirty side effects track per built beacon (dv:1453).
    uint8_t ext_buf[64]; size_t ext_n = 0;   // §P4: holds digest(<=14) + gateway(<=15) + suspect(<=16) TLVs; pack_beacon checks the total vs beacon_max_bytes
    // SELECT the channel digest (B, 2026-06-23): the build is side-effect-free; the ad_count++/retire is COMMITTED below, ONLY if the beacon airs.
    uint32_t ch_picked[protocol::channel_dirty_max_per_bcn]; uint8_t ch_npicked = 0;
    if (!_cfg.is_gateway) ext_n = build_channel_digest_ext(ext_buf, sizeof(ext_buf), ch_picked, ch_npicked);
    // Multi-hop gateway discovery: append the type-4 gateway-layer TLV (a gateway self-adverts its other leaf; EVERY
    // node re-gossips its _bridged_layers). Returns 0 when there's nothing -> a single-layer node emits NO type-4 TLV
    // (it's not a gateway + never ingests one), so s18 stays wire byte-identical. KEYSTONE GUARD.
    ext_n += build_gateway_layer_ext(ext_buf + ext_n, sizeof(ext_buf) - ext_n);
    // §P4 distributed liveness gossip: append the type-1/2 suspect TLV (our LOCALLY-observed silent/dead peers). ALL
    // non-mobile nodes gossip (the liveness plane is universal, unlike the gateway-gated digest). Order-independent at
    // parse. Empty when we have no fresh advertise window -> no TLV (s18 keeps its routing/delivery, see the gate).
    if (!_cfg.is_mobile) ext_n += build_suspect_ext(ext_buf + ext_n, sizeof(ext_buf) - ext_n);
    in.ext = std::span<const uint8_t>(ext_buf, ext_n);

    // §F2 TRUE byte-budget cap: size the route-entry page to the bytes the variable blocks LEFT — so a full
    // page + a populated ext TLV can never overflow beacon_max_bytes (the old fixed cap=27 ignored ext/schedule
    // → silent whole-beacon drop). With no schedule/bitmap/ext this derives the full 35; with the bitmap, 27.
    const size_t sched_bytes = in.schedule.empty()   ? 0 : (1 + 4 * in.schedule.size());
    const size_t ext_block   = ext_n > 0             ? (1 + ext_n) : 0;
    const uint8_t max_entries = beacon_max_entries(protocol::beacon_max_bytes, sched_bytes,
                                                   include_bitmap ? 32 : 0, ext_block);
    // A MOBILE node advertises ZERO route entries (identity-only beacon) — never used as transit (dv:1716-1721).
    beacon_entry entries[kMaxBeaconEntries];          // array sized at the theoretical max 35; max_entries <= it
    uint8_t      pack_idx[kMaxBeaconEntries];         // _active->_rt indices packed (for dirty-clear)
    uint8_t n = 0, dirty_n = 0, stable_n = 0, total_dirty = 0;
    bool    bidi_census_full = false;   // §5: did the FULL hops==1 set fit THIS beacon (drives heard_set_complete, Task 4)
    if (!_cfg.is_mobile) {
        if (_active->_rt_count == 0) _beacon_offset = 0;          // Lua total==0 path resets the cursor (dv:1761)
        for (uint8_t i = 0; i < _active->_rt_count; ++i) if (_active->_rt[i].dirty) ++total_dirty;
        // Phase 1: dirty entries first (ascending dest — _active->_rt is kept sorted).
        for (uint8_t i = 0; i < _active->_rt_count && n < max_entries; ++i) {
            if (!_active->_rt[i].dirty) continue;
            pack_idx[n++] = i; ++dirty_n;
        }
        // Phase 2: stable rotation from _beacon_offset, skipped on dirty-only beacons AND when the dirty page
        // already filled — Lua gates new_offset on `remaining > 0 and not dirty_only` (dv:1789).
        if (!dirty_only && _active->_rt_count > 0 && n < max_entries) {
            uint8_t idx = _beacon_offset, steps = 0;
            while (n < max_entries && steps < _active->_rt_count) {
                const uint8_t ri = static_cast<uint8_t>(idx % _active->_rt_count);
                if (!_active->_rt[ri].dirty) { pack_idx[n++] = ri; ++stable_n; }
                idx = static_cast<uint8_t>(idx + 1); ++steps;
            }
            _beacon_offset = static_cast<uint8_t>(idx % _active->_rt_count);   // advance ONLY when Phase 2 ran
        }
    }
    // §5 census (MF3): on a steady-state (dirty_only) LEAF beacon, force-inject ALL direct-neighbour (candidates[0].hops==1)
    // entries that the dirty/stable passes did not already pack — a NEW partition pass, NOT Phase-2 reuse (Phase 2 is
    // !dirty_only-gated => dormant here). Does NOT set dirty (the post-pack clear is untouched; no re-dirty-every-period).
    // Gateways skip by construction (OI3 leaf-only — they already skip the bitmap/digest). bidi_census_full tracks whether
    // the FULL hops==1 set fit (drives heard_set_complete, Task 4). M1: set true ONLY inside this gate, so a NON-census
    // beacon (discovery/sync = !dirty_only, gateway, or mobile) leaves it false -> heard_set_complete=false (absence is
    // authoritative ONLY on a beacon that ran the census). Bounded by the LIVE max_entries so it never overflows beacon_max_bytes.
    if (dirty_only && _cfg.n_layers != 2 && !_cfg.is_mobile) {
        bidi_census_full = true;
        for (uint8_t i = 0; i < _active->_rt_count; ++i) {
            if (_active->_rt[i].n == 0 || _active->_rt[i].candidates[0].hops != 1) continue;
            bool already = false;
            for (uint8_t k = 0; k < n; ++k) if (pack_idx[k] == i) { already = true; break; }
            if (already) continue;
            if (n >= max_entries) { bidi_census_full = false; break; }   // ran out of slots -> set incomplete (Task 4)
            pack_idx[n++] = i;
        }
        // MF2 headroom: the FULL set "fit" only if it left >= heard_set_census_min_headroom free slots vs the live cap.
        if (bidi_census_full && (max_entries - n) < protocol::heard_set_census_min_headroom) bidi_census_full = false;
    }
    for (uint8_t k = 0; k < n; ++k) {
        const RtEntry&     re = _active->_rt[pack_idx[k]];
        const RtCandidate& pc = re.candidates[0];
        entries[k].dest         = re.dest;
        entries[k].next         = pc.next_hop;
        entries[k].score_bucket = static_cast<uint8_t>(bucket_of_snr_4b(pc.score));
        entries[k].is_gateway   = pc.is_gateway;
        entries[k].hops         = pc.hops;
        entries[k].degraded     = candidate_degraded(pc);   // §5 transitive: degraded_from_wire OR _link_bidi[next]==one_way (MF5 live recompute)
        if (entries[k].degraded) MR_EMIT("degraded_advertise", EF_I("dest",re.dest),EF_I("next",pc.next_hop));
    }
    in.entries = std::span<const beacon_entry>(entries, n);
    in.heard_set_complete = bidi_census_full;   // §5/MF1 byte-3 bit 4 — authoritative only when the full hops==1 set fit (Task 3/4)
    if (in.heard_set_complete) MR_EMIT("link_census_complete", EF_I("n_entries",n),EF_I("max",max_entries));

    uint8_t buf[protocol::beacon_max_bytes];
    const size_t len = pack_beacon(in, std::span<uint8_t>(buf, sizeof(buf)));
    if (len == 0) {   // F2: byte-overflow. With the byte-budget cap below this should never fire — emit (not silent log) as a fail-loud backstop.
        MR_EMIT("beacon_pack_overflow", EF_I("n_entries",n),EF_I("sched",_cfg.n_layers==2?9:0),EF_I("bitmap",in.seen_bitmap.empty()?0:32),EF_I("ext",static_cast<int64_t>(ext_n)));
        _hal.log("beacon pack failed (byte overflow)");
        return;
    }

    TxParams p;
    p.sf    = static_cast<int16_t>(_cfg.routing_sf);
    p.label = "BCN";
    const bool sent = tx_flood(buf, len, p.sf);   // R4.5 FLOOD LBT + duty pre-check (was a raw _hal.tx)
    _last_beacon_tx_ms = _hal.now();              // OUT OF SCOPE (flagged in the PR): stamped even when !sent — a broader beacon min-interval/Lua-parity concern, its own analysis

    // Clear dirty ONLY on the dirty entries that landed in THIS beacon — overflow
    // dirty routes stay dirty for the next one (dv_dual_sf.lua:1832-1836).
    for (uint8_t k = 0; k < dirty_n; ++k) _active->_rt[pack_idx[k]].dirty = false;
    // Channel digest COMMIT (B, 2026-06-23): burn an ad_count / trigger holder-aware retire ONLY for advertisements that
    // ACTUALLY AIRED. An LBT-suppressed / pack-dropped beacon never orphans a digest entry (the air-honesty fix).
    if (sent) commit_channel_digest_advertised(ch_picked, ch_npicked);

    MR_EMIT("beacon_tx", EF_I("n_entries",n),EF_I("rt_total",_active->_rt_count),EF_I("routing_sf",_cfg.routing_sf),EF_S("kind",kind),EF_I("result",sent ? 0 : 2));
    MR_EMIT("beacon_diff_breakdown", EF_I("dirty_n",dirty_n),EF_I("stable_n",stable_n),EF_I("total_dirty",total_dirty));
}

// R6.1: the config fingerprint over THIS node's ACTIVE-leaf config (activate_layer mirrors a gateway's per-layer
// allowed_sf_bitmap into _cfg, so a gateway hashes its active leaf's SF set — matching that leaf's members).
uint16_t Node::cfg_config_hash() const {
    // §5: hash over the EXACT C-frame wire forms — duty as duty_bp (0.01% units), SF set as the u8 list, plus the
    // anti-spam v2 knobs (active_fraction_bp / ch_interval_ms / dm_interval_ms), all inside leaf_config_hash. A mother
    // + a joiner MUST derive identical bytes here or the joiner re-pulls forever.
    return leaf_config_hash(_cfg.allowed_sf_bitmap, duty_to_bp(_cfg.duty_cycle),
                            frac_to_bp(_cfg.channel_active_fraction), ms_to_u16(_cfg.channel_min_interval_ms),
                            ms_to_u16(_cfg.dm_min_interval_ms), _cfg.leaf_name, _cfg.leaf_name_len);
}

void Node::ingest_beacon(const uint8_t* bytes, size_t len, const RxMeta& meta) {
    // §7c: cross-version gate FIRST, off the FIXED byte-3 low nibble — a foreign-version beacon may not even parse, so
    // read the version straight from the raw frame (after the byte-0 cmd + leaf-nibble filter), before parse_beacon.
    if (len < 4 || wire::cmd_of(bytes[0]) != wire::Cmd::B) return;
    if (wire::flags_of(bytes[0]) != _cfg.leaf_id) return;               // foreign leaf nibble -> not ours
    const uint8_t their_wire_ver = static_cast<uint8_t>(bytes[3] & 0x0F);
    if (their_wire_ver != protocol::wire_version) {                     // incompatible wire -> refuse + tell the operator (Push, not telemetry)
        const uint64_t now = _hal.now();
        if (_last_join_refused_ms == 0 || now - _last_join_refused_ms >= protocol::join_refused_retry_ms) {
            _last_join_refused_ms = now;
            Push pu{}; pu.kind = PushKind::join_refused; pu.join_reason = JoinRefuseReason::wire_version;
            pu.origin = their_wire_ver; pu.dst = protocol::wire_version; enqueue_push(pu);
            MR_EMIT("join_refused", EF_S("reason", "wire_version"), EF_I("their_ver", their_wire_ver), EF_I("my_ver", protocol::wire_version));
        }
        return;                                                        // don't peer, don't parse a foreign-version format
    }
    auto parsed = parse_beacon(std::span<const uint8_t>(bytes, len));
    if (!parsed) return;
    const beacon_out& b = *parsed;
    if (b.leaf_id != _cfg.leaf_id) return;                // single-layer nibble filter (R1) — coarse leaf match first
    // R6.1 leaf-config membership filter (§3.3): same nibble is NOT enough — refuse to peer across a config divergence
    // (the misconfig gate). Compares the advertised lineage/epoch/config_hash against ours.
    // §GW (metal 2026-07-05): a GATEWAY is exempt from the R6.1 leaf-config membership plane in BOTH directions —
    //   (A) a gateway (is_gateway ≡ n_layers==2) is NOT a member of any leaf-config plane (it bridges multiple
    //       leaves/lineages) -> skip the whole filter; peer by nibble, never adopt a lineage or fire a config-pull
    //       (the metal bug: an unmanaged gateway hearing a MANAGED leaf adopted its lineage + fired a stray REQ_SYNC).
    //       ⚠ This also skips the same-lineage epoch reconcile below — correct for TODAY's explicit-config (lineage-0)
    //       gateway; revisit if a future join_as_gateway ever makes a gateway a MANAGED member on one of its layers.
    //   (B) a NON-gateway hearing a GATEWAY neighbour (b.self_gateway) must NOT refuse it: a gateway's lineage/config
    //       never matches ours, so peer by nibble (fall through to route-learning), else members can't route to/from it.
    if (b.config_hash != 0 && !_cfg.is_gateway) {         // config_hash==0 = NO fingerprint advertised (unprovisioned/pre-R6.1
                                                          // peer; BLAKE2b never yields 0, so a real configured node always
                                                          // advertises non-zero) -> peer by leaf nibble (legacy), no config gate.
        const uint16_t my_lineage = _cfg.lineage_id;
        const uint16_t my_hash    = cfg_config_hash();
        if (b.self_gateway) {
            // (B) leaf-side: peer with a gateway neighbour by nibble — empty body, fall through to route-learning.
        } else if (my_lineage == 0 && b.lineage_id == 0) {   // BOTH UNMANAGED -> legacy: peer iff config matches (§6.2 backward-compat)
            if (b.config_hash != my_hash) {
                MR_EMIT("leaf_config_conflict", EF_I("src", b.src), EF_I("their_hash", static_cast<int64_t>(b.config_hash)),
                        EF_I("my_hash", static_cast<int64_t>(my_hash)));
                return;                                   // divergent config on an unmanaged leaf -> do NOT peer
            }
        } else if (my_lineage == 0 && b.lineage_id != 0) {  // R6.2: I'm UNMANAGED, neighbour is a MANAGED member of this leaf ->
            if (_node_id != 0) {                            //   JOIN it: adopt the lineage as target (un-synced: epoch stays 0) + PULL its config.
                _cfg.lineage_id = b.lineage_id;             //   (need an id first to receive the routed CONFIG_ANSWER.)
                MR_EMIT("leaf_join_pull", EF_I("src", b.src), EF_I("lineage", b.lineage_id));
                send_config_pull(b.src, b.lineage_id, 0);
            }
            return;                                       // not a member yet -> don't peer
        } else if (b.lineage_id != my_lineage) {
            return;                                       // foreign managed lineage (or neighbour unmanaged while I'm managed) -> ignore
        } else {                                          // SAME managed lineage -> track max-seen epoch (§4.1 write basis), then compare
            if (b.config_epoch > _max_seen_epoch) _max_seen_epoch = b.config_epoch;
            if (b.config_epoch == _cfg.config_epoch) {
                if (b.config_hash != my_hash) {           // same epoch, different hash -> §4.1 concurrent-write conflict
                    MR_EMIT("leaf_config_conflict", EF_I("src", b.src), EF_I("epoch", _cfg.config_epoch),
                            EF_I("their_hash", static_cast<int64_t>(b.config_hash)), EF_I("my_hash", static_cast<int64_t>(my_hash)));
                    // §4.1 LWW: the config from the HIGHER key_hash32 is canonical. I LOSE (their key higher) -> pull + adopt
                    // it at the SAME epoch (NO bump — bumping would start an epoch war). I WIN -> keep mine; they pull from me
                    // when they hear my beacon. Stable key comparison -> one-sided, converges, no flapping (design §3.3/§4.1).
                    if (b.key_hash32 > _key_hash32) send_config_pull(b.src, my_lineage, _cfg.config_epoch);
                    return;                               // don't peer while the conflict persists
                }
                // same lineage + same (epoch, hash) -> peer (fall through)
            } else if (b.config_epoch > _cfg.config_epoch) {  // neighbour is NEWER -> I'm stale -> PULL the newer config (R6.2)
                MR_EMIT("leaf_stale", EF_I("src", b.src), EF_I("my_epoch", _cfg.config_epoch), EF_I("their_epoch", b.config_epoch));
                send_config_pull(b.src, my_lineage, _cfg.config_epoch);
                return;                                   // don't peer on the diverging config until adopted
            } else {
                return;                                   // neighbour is older -> ignore (it heals later)
            }
        }
    }
    if (b.src == 0) return;                               // §P0: a BCN from the reserved sentinel id (an unprovisioned node) -> DROP (never learn a route via/to 0)
    // §mobile 2b (static-safety): a mobile beacons with a LOCAL id that MAY collide a global id — the last-mile MARK
    // disambiguates it, NOT the global id-plane. So a mobile's beacon MUST NOT drive the DAD self-defense OR the global
    // id_bind (else a colliding global node wrongly DENYs the mobile / pollutes hash-locate). It STILL sets the
    // mobile-peer bit below (avoid-as-transit). s18 has no mobiles -> b.is_mobile==false -> this runs exactly as before (byte-identical).
    if (!b.is_mobile && b.src == _node_id) {              // beacon carrying OUR short id (a real global collision)...
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
    if (!b.is_mobile)                                     // §mobile 2b: a mobile's LOCAL id stays OUT of the global hash-locate plane
        id_bind_set(b.src, b.key_hash32, IdBindSource::bcn, IdBindConf::authoritative);   // the owner's own beacon = FIRST-HAND assertion of its key_hash32 (authoritative); a relayed/snooped binding is the claimed second-hand one
    // §mobile 2b: a mobile stamps hearing its HOME's (static) beacon -> reset the home-lost timeout (the FSM checks it).
    if (_cfg.is_mobile && _my_mobile_reg.active && b.src == _my_mobile_reg.home_id && b.key_hash32 == _my_mobile_reg.home_key_hash32)
        _my_mobile_reg.last_heard_home_ms = _hal.now();
    // Parse the channel-digest ext-TLV ONCE (draw-free) so beacon_rx can report how many ids the beacon
    // carries (dv:9614 `channel_digest_ids = #b.channel_digest_ids or 0`); reused by the reaction below.
    // Reported for ALL nodes (even gateways); only the process_channel_digest reaction is gateway-gated.
    uint32_t dids[protocol::channel_dirty_max_per_bcn];
    uint8_t  dn = 0;
    if (b.has_ext) {
        const auto ext = beacon_ext(std::span<const uint8_t>(bytes, len), b);
        dn = parse_channel_digest_tlv(ext, dids, protocol::channel_dirty_max_per_bcn);
        // Multi-hop gateway discovery (type-4 TLV): ingest gw_id->dest_leaf. The leaf filter above means this beacon
        // is on OUR leaf, so gw_id is a node_id on our leaf. Skip our own id + a dest_leaf == our leaf (Lua dv:1540).
        GwLayerEntry gle[protocol::bridged_layers_max_per_tlv];
        const uint8_t gn = parse_gateway_layer_tlv(ext, gle, protocol::bridged_layers_max_per_tlv);
        for (uint8_t i = 0; i < gn; ++i)
            if (gle[i].gw_id != _node_id && gle[i].dest_leaf != _cfg.leaf_id)
                ingest_bridged_layer(gle[i].gw_id, gle[i].dest_leaf);
        // §P4 distributed liveness gossip (dv:9627-9686): a received suspect/liveness TLV demotes our routes via the
        // named peers based on the SENDER's first-hand observation — so a node that hasn't itself timed out via a dead
        // relay still reroutes. apply marks them REMOTE (no re-advertise, local_only resort = anti-storm). Wire body
        // <=15 B -> at most 15 ids (type-1) / 7 pairs (type-2).
        SuspectEntry susp[16];
        const uint8_t sn = parse_suspect_tlv(ext, susp, 16);
        if (sn > 0) apply_suspect_gossip(susp, sn, b.src);
    }
    // §P1 seen-bitmap apply (Lua apply_seen_bitmap@1357): the sender's view of who's alive — stamp every
    // seen peer fresh + refresh last_seen_ms on our route candidates whose next_hop is the sender (passive
    // route keepalive). BUGFIX 2026-06-18: this was mis-nested INSIDE `if (b.has_ext)` above, so it only ran
    // on the rare ext-carrying beacon (seen_bitmap_rx==0 on s18) — paying the 32-B beacon cost with NO benefit
    // (s18 108→98). The seen-bitmap is its OWN beacon section (has_seen_bitmap), independent of has_ext.
    // NOTE: only stamps nodes that ALREADY have a PeerLiveness entry (create=false) — gossip-only peers don't
    // get entries (would bloat the 64-entry table + evict real direct neighbours).
    if (_cfg.seen_bitmap_enabled && !_cfg.is_mobile && b.has_seen_bitmap) {
        const auto sbm = beacon_seen_bitmap(std::span<const uint8_t>(bytes, len), b);
        if (sbm.size() == 32) {
            const uint64_t s_now = _hal.now();
            uint8_t applied = 0, refreshed = 0;
            for (int id = 1; id <= 254; ++id) {
                if (id == _node_id) continue;
                if (!(sbm[static_cast<size_t>(id >> 3)] & (1u << (id & 7)))) continue;
                // FULL Lua apply (apply_seen_bitmap@1357): stamp the dedicated freshness map for EVERY gossiped
                // peer — incl. gossip-only peers we have no direct PeerLiveness entry for (that indirect-freshness
                // IS the mechanism's value). The map has no eviction, so this can't bloat/evict the liveness table.
                _active->_dest_seen_ms[id] = s_now; ++applied;
                RtEntry* e = rt_find(static_cast<uint8_t>(id));
                if (e) {
                    for (uint8_t j = 0; j < e->n; ++j)
                        if (e->candidates[j].next_hop == b.src)
                            { e->candidates[j].last_seen_ms = s_now; ++refreshed; }
                }
            }
            MR_EMIT("seen_bitmap_rx", EF_I("from", b.src), EF_I("applied", applied), EF_I("refreshed", refreshed));
        }
    }
    // beacon_rx — one per received beacon (the gate asserts src)
    MR_EMIT("beacon_rx",EF_I("src",b.src),EF_I("channel_digest_ids",dn));
    if (in_discovery()) ++_active->_discovery_bcn_rx_count;   // §per-leaf: a leaf-1 beacon counts toward leaf 1's bootstrap, not leaf 0's (dv_dual_sf.lua:9560-9562)

    const uint64_t now         = _hal.now();
    const int16_t  meta_snr_q4 = protocol::db_to_q4(meta.snr_db);

    // Slice 3e.2: learn a GATEWAY's window schedule from its beacon (self_gateway + has_schedule) so we can later time
    // an RTS to its window (gateway_schedule_defer_ms). visit_start is anchored to `now` (this heard instant — the leaf
    // filter above means the gateway is currently on OUR leaf, so its OUR-leaf record reads ~open-now).
    if (b.self_gateway && b.has_schedule && b.schedule_count > 0) {
        GatewaySchedule gs{};
        gs.valid = true; gs.gw_node_id = b.src; gs.heard_ms = now;
        gs.spread_nibble = b.gateway_spread_nibble;             // §3e: keep the advertised herd-spread hint for the defer jitter
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
    // inside its suppress window — our reply would be redundant. Draw-free. useful_bcn = (#entries>0)
    // or (seen_bits>1): at least ONE other node besides self in the bitmap = the sender has real
    // network visibility. A bitmap with only self (popcount==1) doesn't suppress.
    bool useful = b.n_entries > 0;
    if (!useful && b.has_seen_bitmap) {
        const auto sbm = beacon_seen_bitmap(std::span<const uint8_t>(bytes, len), b);
        if (sbm.size() == 32) {
            uint8_t bits = 0;
            for (size_t i = 0; i < 32; ++i) {
                uint8_t v = sbm[i];
                while (v) { bits += v & 1; v >>= 1; }
            }
            useful = (bits > 1);
        }
    }
    if (useful) {
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
    if (b.is_mobile) _active->_mobile_peer[b.src >> 3] |= static_cast<uint8_t>(1u << (b.src & 7));   // ① learn mobility (SET-only, dv:9603-9604) -> avoid as transit

    // DV merge: each carried entry is a route via the sender (dv_dual_sf.lua:9620-9678).
    for (uint8_t i = 0; i < b.n_entries; ++i) {
        auto pe = parse_beacon_entry(std::span<const uint8_t>(bytes, len), b, i);
        if (!pe) continue;
        const beacon_entry& e = *pe;
        if (e.dest == _node_id) continue;                 // split-horizon
        if (e.next == _node_id) {                         // sender reaches e.dest via US (incl. e.next==0 when WE are unprovisioned)
            rt_prune_cycle(e.dest, b.src);                // drop our looped candidates (dv_dual_sf.lua:9628-9633)
            continue;
        }
        if (e.dest == 0 || e.next == 0) continue;         // §P0: never learn a route TO, or with an n2_hop OF, the reserved sentinel id 0
                                                          // (AFTER the cycle-prune so an unprovisioned node's own-id=0 still prunes)
        // §P2: skip a beacon-carried route whose ADVERTISED next-hop (e.next) is known
        // silent/dead by the receiver — the sender's path A→B→C is compromised when B is
        // dead (Lua dv:9770-9776). Installing this route would leak a broken path into the
        // table and waste RTS attempts before the timeout cascade fires its own RREQ.
        if (liveness_penalty_q4(e.next) >= protocol::peer_silent_penalty_q4) {
            MR_EMIT("rt_skip_silent_n2", EF_I("dest", e.dest), EF_I("via", b.src),
                    EF_I("advertised_next", e.next));
            continue;
        }
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
        cand.learned_leaf = _cfg.leaf_id;
        cand.degraded_from_wire = e.degraded;   // Slice 3: inherit the advertiser's degraded wire-bit (recomputed per merge)
        const MergeAction a = rt_merge(e.dest, cand);
        if (a == MergeAction::new_dest || a == MergeAction::promote) {
            emit_rt_update(_hal, e.dest, b.src, combined_score, cand.hops, "primary");
            rt_changed = true;
        } else if (a == MergeAction::alt_install) {
            emit_rt_update(_hal, e.dest, b.src, combined_score, cand.hops, "alt");
        }
    }

    // Slice 3 §1: bidirectionality detection. Re-collect the beacon's hops==1 entries as the advertiser's heard-set
    // and scan for our own id: present => the advertiser hears us => confirmed; absent in a complete page => one_way.
    // (Separate from the DV-merge loop above, which split-horizon-skips dest==self and never sees the self-entry.)
    {
        static beacon_entry heard[kMaxBeaconEntries];   // static: ingest_beacon is non-reentrant (single loop-task RX dispatch, node.cpp:761) — keep this ~210 B OFF the loop stack (jump-to-0x0 stack-pressure discipline)
        uint8_t hn = 0;
        for (uint8_t i = 0; i < b.n_entries && hn < kMaxBeaconEntries; ++i) {
            auto pe = parse_beacon_entry(std::span<const uint8_t>(bytes, len), b, i);
            if (pe) heard[hn++] = *pe;
        }
        update_link_bidi_from_beacon(b.src, heard, hn, b.heard_set_complete);
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
        && (now - _active->_discovery_started_ms >= protocol::beacon_boot_grace_ms);
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
    if (!_active->_discovery_mode) return;
    const uint64_t now = _hal.now();
    const bool timed_out = (_active->_discovery_until_ms > 0) && (now >= _active->_discovery_until_ms);
    if (_active->_discovery_bcn_rx_count >= protocol::discovery_min_bcn_rx ||
        _active->_rt_count >= protocol::discovery_min_routes || timed_out) {
        _active->_discovery_mode = false;
        MR_TELEMETRY(
            EventField f[] = {
                { .key = "reason",     .type = EventField::T::str, .s = reason },
                { .key = "heard_bcn",  .type = EventField::T::i64, .i = static_cast<int64_t>(_active->_discovery_bcn_rx_count) },
                { .key = "rt_total",   .type = EventField::T::i64, .i = static_cast<int64_t>(_active->_rt_count) },
                { .key = "elapsed_ms", .type = EventField::T::i64, .i = static_cast<int64_t>(now - _active->_discovery_started_ms) },
            };
            _hal.emit("bcn_discovery_exit", f, 4); );
    }
}

}  // namespace meshroute
