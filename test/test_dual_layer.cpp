// MeshRoute — test_dual_layer.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the dual-layer gateway (2026-06-12-gateway-dual-layer-design.md). SLICE 0: the config model
// (LayerConfig / n_layers / layers[]) + on_init's FAIL-LOUD validation gate (§3.2) — a gateway (n_layers==2)
// REQUIRES per-layer layer_id/routing_sf/allowed_sf_bitmap + non-overlapping explicit windows, else on_init
// REFUSES (returns false). Single-layer configs are unaffected: the legacy scalars mirror into layers[0].
// This file grows as the gateway slices land. NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"

#include <string>
#include <vector>
#include "node.h"
#include "frame_codec.h"   // Slice 3e: parse_beacon / parse_beacon_schedule to verify the gateway's advertised schedule
#include "airtime.h"       // §3e: re-derive exchange_airtime_ms from the airtime_ms primitive

using namespace meshroute;

namespace {
// Minimal Hal — on_init only needs the timer/now/sf seams. Slice 3c CAPTURES the radio retunes + cancels so the
// activation tests can assert what activate_layer did (last SF/short-id; which timer ids were cancelled).
class StubHal : public Hal {
public:
    uint64_t _now = 0;
    int      last_set_rx_sf      = -1;
    int      last_set_protocol_id = -1;
    bool     cancelled[80]       = {};   // cancelled[id] = a cancel(id) was seen (kCap=80)
    bool     armed[80]           = {};   // armed[id]     = an after(_, id) was seen
    uint32_t last_delay[80]      = {};   // last_delay[id] = the most recent after() delay for id
    uint8_t  last_tx[256]        = {};   // the most recent tx'd frame (Slice 3e: parse the gateway's advertised schedule)
    size_t   last_tx_len         = 0;
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override {
        last_tx_len = (n < sizeof last_tx) ? n : sizeof last_tx;
        for (size_t i = 0; i < last_tx_len; ++i) last_tx[i] = b[i];
        return TxResult::ok;
    }
    void     set_rx_sf(int sf) override { last_set_rx_sf = sf; }
    double   last_set_rx_freq = 0.0;                     // per-layer freq retune (dual-layer gateway)
    void     set_rx_freq(double mhz) override { last_set_rx_freq = mhz; }
    uint32_t last_set_rx_bw = 0;                          // per-layer BW retune spy (Slice 2)
    int      last_set_rx_cr = -1;                         // per-layer CR retune spy (-1 = never called)
    void     set_rx_bw(uint32_t bw) override { last_set_rx_bw = bw; }
    void     set_rx_cr(uint8_t cr) override  { last_set_rx_cr = cr; }
    uint64_t channel_busy_until() override { return 0; }
    uint64_t _airtime_used_ms = 0;                       // settable: drives the gateway-announce duty-headroom gate
    uint64_t airtime_used_ms(uint64_t) override { return _airtime_used_ms; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t delay, uint32_t id) override { if (id < 80) { armed[id] = true; last_delay[id] = delay; } return true; }
    void     cancel(uint32_t id) override { if (id < 80) cancelled[id] = true; }
    void     set_protocol_id(int id) override { last_set_protocol_id = id; }
    int      _rand_ret = -1;                              // >=0 => force this rand value (clamped); -1 => default (lo)
    int      rand_range(int lo, int hi) override { if (_rand_ret < 0) return lo; int v = _rand_ret; if (v < lo) v = lo; if (hi > lo && v >= hi) v = hi - 1; return v; }
    void     rand_bytes(uint8_t* o, size_t n) override { for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(rand_range(0, 256)); }
    std::vector<std::string> emits;                                                   // §intra-relay: record emit kinds so the drop is assertable
    void     emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind); }
    bool     saw_emit(const char* k) const { for (auto& e : emits) if (e == k) return true; return false; }
    void     log(const char*) override {}
};

// A valid gateway layer (every REQUIRED field set): allowed_sf_bitmap covers the routing SF.
LayerConfig good_layer(uint8_t layer_id, uint8_t sf) {
    LayerConfig L; L.layer_id = layer_id; L.routing_sf = sf; L.allowed_sf_bitmap = static_cast<uint16_t>(1u << sf); return L;
}
}  // namespace

TEST_CASE("dual-layer cfg: a valid single-layer config inits + mirrors the legacy scalars into layers[0]") {
    StubHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 12); cfg.leaf_id = 3;
    CHECK(node.on_init(cfg));                                          // single-layer always inits
    const NodeConfig& c = node.config();
    CHECK(c.n_layers == 1);
    CHECK(c.layers[0].layer_id == 3);                                 // == leaf_id (single-layer)
    CHECK(c.layers[0].routing_sf == 7);
    CHECK(c.layers[0].allowed_sf_bitmap == static_cast<uint16_t>(1u << 12));
}

TEST_CASE("dual-layer cfg: a valid gateway (n_layers==2) inits") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg;
    cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));
}

TEST_CASE("dual-layer cfg: on_init REFUSES a missing REQUIRED per-layer field (fail loud, §3.2)") {
    NodeConfig base; base.n_layers = 2; base.layers[0] = good_layer(1, 8); base.layers[1] = good_layer(2, 9);

    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[1].layer_id = 0;          CHECK_FALSE(n.on_init(c)); }  // layer_id unset
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[0].routing_sf = 0;        CHECK_FALSE(n.on_init(c)); }  // routing_sf unset
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[0].routing_sf = 13;       CHECK_FALSE(n.on_init(c)); }  // routing_sf out of [5..12]
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[1].allowed_sf_bitmap = 0; CHECK_FALSE(n.on_init(c)); }  // no data SF
    { StubHal h; Node n(h, 1, 1); CHECK(n.on_init(base)); }                                                             // the unmodified base is valid
}

TEST_CASE("dual-layer cfg: on_init REFUSES an out-of-range n_layers (> MR_N_LAYERS array bound, fail loud)") {
    // Defense-in-depth (§3.2): the fixed _layers[MR_N_LAYERS] array means a count past it must be refused, not
    // silently accepted (which would set _n_layers past the array end -> OOB). On the native build MR_N_LAYERS==2.
    NodeConfig base; base.n_layers = 2; base.layers[0] = good_layer(1, 8); base.layers[1] = good_layer(2, 9);
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.n_layers = 3;   CHECK_FALSE(n.on_init(c)); }  // 3 > 2
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.n_layers = 44;  CHECK_FALSE(n.on_init(c)); }  // e.g. the host 300->uint8_t(44) alias
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.n_layers = 255; CHECK_FALSE(n.on_init(c)); }  // max uint8_t
    { StubHal h; Node n(h, 1, 1); CHECK(n.on_init(base)); }                                            // n_layers==2 still valid
}

TEST_CASE("dual-layer cfg: on_init REFUSES overlapping explicit windows; accepts non-overlap + derived (0)") {
    NodeConfig base; base.n_layers = 2; base.layers[0] = good_layer(1, 8); base.layers[1] = good_layer(2, 9);
    // window_period_ms defaults to 15000 in both layers; the overlap check reads layers[0].window_period_ms.

    { StubHal h; Node n(h, 1, 1); NodeConfig c = base;
      c.layers[0].window_ms = 10000; c.layers[1].window_ms = 8000;        // 18000 > 15000 -> overlap
      CHECK_FALSE(n.on_init(c)); }
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base;
      c.layers[0].window_ms = 7000;  c.layers[1].window_ms = 7000;        // 14000 <= 15000 -> ok
      CHECK(n.on_init(c)); }
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base;
      c.layers[0].window_ms = 0;     c.layers[1].window_ms = 0;           // 0 = DERIVE at the scheduler -> ok
      CHECK(n.on_init(c)); }
}

TEST_CASE("dual-layer cfg: on_init REFUSES same leaf nibble (§0.8 wire-filter aliasing) + a differing window_period") {
    NodeConfig base; base.n_layers = 2; base.layers[0] = good_layer(1, 8); base.layers[1] = good_layer(2, 9);

    // §0.8: the two layers must differ in their LEAF NIBBLE (layer_id & 0x0F) — it's the byte-0 wire filter, so
    // same-nibble co-channel layers alias. layer_id 1 (leaf 1) + 17 (also leaf 1) must refuse; 1 + 18 (leaf 2) ok.
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[1] = good_layer(17, 9); CHECK_FALSE(n.on_init(c)); }
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[1] = good_layer(18, 9); CHECK(n.on_init(c)); }
    // window_period_ms is the ONE shared cycle -> the two must agree.
    { StubHal h; Node n(h, 1, 1); NodeConfig c = base; c.layers[1].window_period_ms = 20000; CHECK_FALSE(n.on_init(c)); }
}

// ---- SLICE 2b: per-layer dedup NON-ALIASING (the node_mac_rx.cpp:393 fix; spec §8) -----------------------
// Friend of Node (declared in node.h, MESHROUTE_NATIVE-only). White-box seam: point _active at a given leaf +
// read the per-LayerRuntime dedup maps. NO production behavior — the gateway's real leaf-swap is Slice 3.
namespace meshroute {
struct DualLayerTestAccess {
    static void  set_active(Node& n, uint8_t i)  { n._active = &n._layers[i]; }
    static auto& seen(Node& n, uint8_t i)        { return n._layers[i]._seen_origins; }
    static auto& seen_from(Node& n, uint8_t i)   { return n._layers[i]._seen_origin_from; }   // LOOP_DUP prev-hop
    static auto& last_acked(Node& n, uint8_t i)  { return n._layers[i]._last_acked_from; }
    // Q REQ_SYNC plane (the methods are private; reach them via the friend).
    static void  mark_q(Node& n, uint8_t op, uint8_t src, uint8_t dst)   { n.mark_q_responded(op, src, dst); }
    static bool  q_recent(Node& n, uint8_t op, uint8_t src, uint8_t dst) { return n.q_responded_recently(op, src, dst); }
    static auto& sync_pending(Node& n, uint8_t i) { return n._layers[i]._sync_pending; }
    // Slice 3c: leaf activation + the §4 busy-guard (the methods + state are private).
    static void     activate(Node& n, uint8_t i)        { n.activate_layer(i); }
    static bool     swap_blocked(Node& n)               { return n.layer_swap_blocked(); }
    static const void* active_ptr(Node& n)              { return n._active; }
    static const void* layer_ptr(Node& n, uint8_t i)    { return &n._layers[i]; }
    static int16_t  snr_floor(Node& n)                  { return n._routing_snr_floor_q4; }
    static uint32_t sync_timer_id(uint8_t s)            { return Node::kSyncResponseTimerId + s; }
    static void     arm_sync_slot(Node& n, uint8_t s)   { n._active->_sync_pending[s].active = true; }
    static bool     sync_slot_active(Node& n, uint8_t i, uint8_t s) { return n._layers[i]._sync_pending[s].active; }
    // busy-guard inputs (set on the ACTIVE leaf / Node-global)
    static void     set_pending_tx(Node& n, bool v)     { if (v) n._active->_pending_tx.emplace(); else n._active->_pending_tx.reset(); }
    static void     set_pending_rx(Node& n, bool v)     { if (v) n._active->_pending_rx.emplace(); else n._active->_pending_rx.reset(); }
    static void     set_post_ack(Node& n, bool v)       { n._active->_post_ack.pending = v; }
    static void     set_nack_wait(Node& n, bool v)      { n._nack_wait_pending = v; }
    // Slice 3c #4: Principle 11 — channel-plane entry points (private) reached via the friend.
    static bool     origin_admit(Node& n, uint8_t origin, uint32_t msg_id) { return n.channel_origin_admit(origin, msg_id); }
    static void     process_digest(Node& n, uint8_t src, const uint32_t* ids, uint8_t count) { n.process_channel_digest(src, ids, count); }
    static uint32_t channel_pull_timer_id(uint8_t s) { return Node::kChannelPullTimerId + s; }
    // Slice 3c hardening: the Node-global transient stash (guard_defer) + the per-leaf deferred-drain (rehome).
    static void     set_deferred_lbt(Node& n, bool v) { n._deferred_lbt[0].pending = v; }
    static void     set_tx_stash(Node& n, bool v)     { n._tx_stash[0].valid = v; n._tx_stash[0].reissue_pending = v; }   // a GENUINE pending re-issue (valid + a timer armed)
    static void     set_tx_stash_clean(Node& n)       { n._tx_stash[0].valid = true; n._tx_stash[0].reissue_pending = false; }   // a cleanly-sent CTS/ACK: valid buffer, NO re-issue pending
    static void     fire_busy_retry(Node& n, uint8_t slot) { n.retry_stashed(slot); }                                          // the kRadioBusyRetryTimerId handler (the busy re-issue path)
    static bool     stash_reissue_pending(Node& n, uint8_t slot) { return n._tx_stash[slot].reissue_pending; }
    static void     set_deferred(Node& n, uint8_t i, uint8_t cnt, bool armed) { n._layers[i]._deferred_n = cnt; n._layers[i]._drain_armed = armed; }
    static uint32_t drain_timer_id()                  { return Node::kDeferredDrainTimerId; }
    // F2: pin the FORWARD/provider gate (a gateway never rebroadcasts a channel flood).
    static void     arm_flood_slot(Node& n, uint8_t slot)   { n._active->_flood[slot].active = true; }
    static bool     flood_slot_active(Node& n, uint8_t slot) { return n._active->_flood[slot].active; }
    static void     flood_decide(Node& n, uint8_t slot)     { n.flood_forward_decision(slot); }
    static uint32_t flood_rebcast_timer_id(uint8_t slot)    { return Node::kFloodRebcastTimerId + slot; }
    static uint32_t window_timer_id()                       { return Node::kLayerWindowTimerId; }   // Slice 3d scheduler
    static uint32_t beacon_timer_id()                       { return Node::kBeaconTimerId; }         // shared periodic beacon
    static uint64_t last_beacon_ms(Node& n, uint8_t i)      { return n._layers[i]._last_beacon_ms; } // Slice 3d per-leaf beacon
    static void     set_last_beacon(Node& n, uint8_t i, uint64_t v) { n._layers[i]._last_beacon_ms = v; }
    static void     call_gw_beacon(Node& n)                 { n.maybe_emit_gateway_beacon(); }        // the window-activation beacon decision
    static void     inject_dirty_route(Node& n, uint8_t i, uint8_t dest) {                            // a dirty route on leaf i (reactive trigger)
        auto& L = n._layers[i]; L._rt[0].dest = dest; L._rt[0].dirty = true; if (L._rt_count < 1) L._rt_count = 1;
    }
    static uint32_t gw_defer(Node& n, uint8_t gw_node_id)   { return n.gateway_schedule_defer_ms(gw_node_id); }  // Slice 3e.2
    static uint8_t  spread_nibble(Node& n)                  { return n.gateway_spread_nibble(); }                // §3e herd-spread compute
    static uint8_t  direct_neighbors(Node& n)              { return n.count_direct_neighbors(); }
    static uint32_t exchange_airtime(Node& n)              { return n.exchange_airtime_ms(); }                  // §3e RTS+CTS+gap+DATA+ACK
    static uint16_t dm_payload_mean(Node& n)              { return n._dm_payload_mean; }
    static void     add_direct_neighbor(Node& n, uint8_t dest) {                                                // a 1-hop rt entry (herd member)
        auto& L = *n._active; if (L._rt_count >= protocol::cap_routes) return;
        uint8_t i = L._rt_count; L._rt[i].dest = dest; L._rt[i].n = 1;
        L._rt[i].candidates[0].next_hop = dest; L._rt[i].candidates[0].hops = 1; L._rt_count = i + 1;
    }
    static void     store_gw_sched_nibble(Node& n, uint8_t gw_node, uint8_t leafA, uint8_t leafB, uint8_t nib) {
        GatewaySchedule gs{}; gs.valid = true; gs.gw_node_id = gw_node; gs.heard_ms = n._hal.now();
        gs.period_ms = 15000; gs.spread_nibble = nib; gs.n_rec = 2;
        gs.rec[0].leaf_id = leafA; gs.rec[0].window_ms = 7500; gs.rec[0].offset_ms = 0;
        gs.rec[1].leaf_id = leafB; gs.rec[1].window_ms = 7500; gs.rec[1].offset_ms = 7500;
        n.store_gateway_schedule(gs);
    }
    static void     emit(Node& n, const char* kind)         { n.emit_beacon(kind); }                  // Slice 3e F-A: force a beacon
    static void     ingest_bcn(Node& n, const uint8_t* b, size_t len, float snr) { RxMeta m{snr, -60.0f, n._hal.now(), -1}; n.ingest_beacon(b, len, m); }   // §GW: feed a packed beacon through the RX membership filter
    static bool     rt_has(Node& n, uint8_t dest)           { return n.rt_find(dest) != nullptr; }    // §GW: did we learn a route to `dest`?
    static bool     rt_has_global(Node& n, uint8_t dest)    { return n.rt_find(dest, Plane::GLOBAL) != nullptr; }   // §team-multihop: the STATIC _rt ONLY (force GLOBAL past the is_team_peer AUTO dispatch)
    static uint16_t lineage(Node& n)                        { return n._cfg.lineage_id; }             // §GW: our adopted leaf-config lineage (0 = unmanaged)
    static bool     selectable(Node& n, const RtCandidate& c, const PendingTx& pt) { return n.next_hop_selectable(c, pt, false); }   // §intra-relay Edit 3: the sender-side gate
    static bool     is_gwdest(Node& n, uint8_t dest)        { return n.is_gateway_dest(dest); }        // §intra-relay: learned-gateway recognition
    static void     set_intra_relay(Node& n, bool v)        { n._cfg.intra_layer_relay = v; }          // §intra-relay Edit 2: the operator opt-in
    static void     drive_post_ack_forward(Node& n, uint8_t dst, uint8_t origin) {                     // §intra-relay Edit 2: set up a FORWARD PostAck + run do_post_ack
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending = true; pa.is_forward = true; pa.dst = dst; pa.origin = origin;
        pa.ctr = 0x1234; pa.ctr_lo = static_cast<uint8_t>(0x1234 & 0x0F); pa.flags = 0; pa.type = 0;
        pa.previous_hop = 99; pa.inner_len = 0; pa.fwd_remaining = 5; pa.fwd_committed = 1;
        n.do_post_ack();
    }
    static void     store_mobile(Node& n, uint32_t key_hash, uint8_t local_id) {                       // §mobile 3a: register a mobile on this host
        auto& L = *n._active; L._mobile_reg[L._mobile_reg_n++] = { key_hash, local_id, 1, n._hal.now() };
    }
    static void     drive_post_ack_deliver(Node& n, uint8_t origin, uint32_t dst_hash) {               // §mobile 3a: a DM addressed to me carrying a DST_HASH -> run do_post_ack
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending = true; pa.is_forward = false; pa.origin = origin; pa.dst = n._node_id;
        pa.ctr = 0x1234; pa.ctr_lo = 4; pa.flags = DATA_FLAG_DST_HASH; pa.type = 0;
        pa.inner[0] = static_cast<uint8_t>(dst_hash); pa.inner[1] = static_cast<uint8_t>(dst_hash >> 8);
        pa.inner[2] = static_cast<uint8_t>(dst_hash >> 16); pa.inner[3] = static_cast<uint8_t>(dst_hash >> 24);
        pa.inner[4] = origin; pa.inner[5] = 0xAA; pa.inner_len = 6;   // [dst_hash 4B LE][origin][body]
        n.do_post_ack();
    }
    static const PendingTx* pending(Node& n) { return n._active->_pending_tx ? &*n._active->_pending_tx : nullptr; }  // §mobile 3a: the in-flight item (after become_free issues the forward)
    static void     make_registered_mobile(Node& n, uint8_t local_id, uint8_t home_id, uint32_t home_hash) {   // §mobile 3b: a mobile that has adopted a local-id + homed
        n._cfg.is_mobile = true; n.set_identity(local_id, n._key_hash32); n._joined = true;
        n._my_mobile_reg = { true, home_id, local_id, home_hash, n._cfg.leaf_id, 0, n._hal.now() };
    }
    static uint16_t do_send(Node& n, uint8_t dst, const uint8_t* body, uint8_t body_len) { return n.do_send(dst, body, body_len, 0); }  // §mobile 3b: originate a plaintext DM
    static uint16_t do_send_ack(Node& n, uint8_t dst, const uint8_t* body, uint8_t body_len) { return n.do_send(dst, body, body_len, DATA_FLAG_E2E_ACK_REQ); }  // §mobile: a reply-expecting (E2E_ACK) DM
    static uint16_t do_send_override(Node& n, uint8_t dst, const uint8_t* body, uint8_t len, uint32_t oh) { return n.do_send(dst, body, len, 0, CryptIntent::def, oh); }  // §mobile 3c: DM with override_dst_hash
    static void     emit_rreq(Node& n, uint8_t dst) { n.emit_route_request(dst, 16); }  // §mobile: drive route-discovery (test the mobile-local-id RREQ guard)
    static uint16_t send_by_hash(Node& n, uint32_t h, const uint8_t* body, uint8_t len) { return n.send_by_hash(h, body, len, 0, CryptIntent::def); }  // §mobile 3c: send-by-hash trigger
    static void     send_e2e_ack(Node& n, uint8_t to_origin, uint16_t ctr, uint32_t sender_hash = 0) { n.send_e2e_ack(to_origin, ctr, sender_hash); }  // §mobile Fix 5: originate an E2E-ACK to an origin; sender_hash a hosted mobile -> last-mile
    static bool     has_pending_rx(Node& n) { return static_cast<bool>(n._active->_pending_rx); }  // §mobile 3b: a receiver flight opened => the RTS was ADDRESSED (accepted), not overheard
    static void     deactivate_mobile_reg(Node& n) { n._my_mobile_reg.active = false; }  // §mobile 4b: simulate the 2b home-lost reset (active=false, home_id kept) so the guard re-CLAIMs
    static uint64_t mobile_last_heard_home(Node& n) { return n._my_mobile_reg.last_heard_home_ms; }  // §mobile: the home-lost liveness clock
    static void     mobile_discover_fire(Node& n) { n.mobile_discover_fire(); }  // §mobile: drive the reclaim/home-lost FSM tick
    static void     presence_probe_fire(Node& n) { n.presence_probe_fire(); }    // §S6: drive a mobile presence check/probe tick (k_miss -> HOME LOST)
    static void     presence_ingest_probe(Node& n, const uint8_t* f, size_t l, const RxMeta& m) { n.presence_ingest_probe(f, l, m); }  // §S6: feed a home a probe
    static void     presence_roster_fire(Node& n) { n.presence_roster_fire(); }  // §S6: force the home's coalesced roster emit
    static uint8_t  presence_miss(Node& n) { return n._presence_miss; }          // §S6: consecutive unanswered probes
    static int16_t  mobile_snr_q4(Node& n, uint8_t slot) { return n._active->_mobile_snr_q4[slot]; }  // §S6: per-mobile SNR EWMA
    static uint8_t  active_layer(Node& n) { return n.active_layer_id(); }         // §S6: this node's active leaf's full layer id
    static void     presence_setup_prescan(Node& n, uint8_t my_tier, int16_t home_rx_q4) {  // §S6.4-C: force a prescan (weak home) with dwell elapsed
        n._presence_prescan = true; n._presence_my_tier = my_tier; n._presence_home_rx_q4 = home_rx_q4; n._last_adopt_ms = 0; }
    static void     presence_add_cand(Node& n, uint8_t home_id, uint8_t layer, int16_t snr_q4, uint8_t echo_tier, uint64_t first_seen) {  // §S6.4-C D14: inject a candidate (echo_tier 0xFF = unknown)
        uint8_t s = n._presence_cand_n < protocol::cap_presence_candidates ? n._presence_cand_n++ : 0;
        n._presence_cand[s] = { home_id, layer, snr_q4, echo_tier, first_seen, first_seen }; }
    static void     presence_maybe_rehome(Node& n) { n.presence_maybe_rehome(); }
    static uint8_t  mobile_reg_redirect(Node& n, uint8_t slot) { return n._active->_mobile_reg[slot].redirect_home_id; }  // §mobile 4b
    static void     set_mobile_last_heard(Node& n, uint8_t slot, uint64_t ms) { n._active->_mobile_reg[slot].last_heard_ms = ms; }  // §mobile hash-locate: age/refresh the liveness clock
    static uint8_t  mobile_reg_n(Node& n) { return n._active->_mobile_reg_n; }  // §mobile Part 2: count of hosted mobiles
    static bool     mobile_reg_has_pubkey(Node& n, uint8_t slot) { return n._active->_mobile_reg[slot].has_pubkey; }  // §mobile Part 2 Fix 6
    static const uint8_t* mobile_reg_ed_pub(Node& n, uint8_t slot) { return n._active->_mobile_reg[slot].ed_pub; }   // §mobile Part 2 Fix 6
    static void     set_mobile_pubkey(Node& n, uint8_t slot, const uint8_t ed[32]) { for (uint8_t k=0;k<32;++k) n._active->_mobile_reg[slot].ed_pub[k]=ed[k]; n._active->_mobile_reg[slot].has_pubkey=true; }  // §mobile Part 2 Fix 7 setup
    static void     drive_post_ack_pubkey_push(Node& n, uint32_t source_hash, const uint8_t ed[32]) {   // §mobile Part 2 Fix 6: run do_post_ack for a MOBILE_PUBKEY_PUSH DM
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending = true; pa.is_forward = false; pa.origin = 5; pa.dst = n._node_id;
        pa.ctr = 0x1234; pa.ctr_lo = 4; pa.flags = DATA_FLAG_SOURCE_HASH; pa.type = DATA_TYPE_MOBILE_PUBKEY_PUSH;
        pa.inner[0] = 5;   // [origin][source_hash 4B LE][ed_pub 32]
        pa.inner[1]=static_cast<uint8_t>(source_hash); pa.inner[2]=static_cast<uint8_t>(source_hash>>8);
        pa.inner[3]=static_cast<uint8_t>(source_hash>>16); pa.inner[4]=static_cast<uint8_t>(source_hash>>24);
        for (uint8_t k = 0; k < 32; ++k) pa.inner[5+k] = ed[k];
        pa.inner_len = 37;
        n.do_post_ack();
    }
    static uint8_t  mobile_scan_idx(Node& n)      { return n._mobile_scan_idx; }              // §mobile 5a
    static void     add_learned_layer(Node& n, uint8_t layer_id, uint8_t sf) {                // §mobile 5a: inject a learned-directory entry
        LayerRecord& r = n._learned_layers[n._learned_layers_n++]; r.layer_id=layer_id; r.sf=sf; r.freq_khz=868100; r.bw_hz=125000;
    }
    static void     learned_ingest(Node& n, const uint8_t* b, size_t len) { n.learned_layers_ingest(b, len); }  // §mobile 5a
    static uint8_t  learned_layers_n(Node& n) { return n._learned_layers_n; }                 // §mobile 5a
    static void     drive_post_ack_query(Node& n, uint8_t origin, uint32_t source_hash) {     // §mobile 5a: run do_post_ack for a LAYER_QUERY DM
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending=true; pa.is_forward=false; pa.origin=origin; pa.dst=n._node_id;
        pa.ctr=0x1234; pa.ctr_lo=4; pa.flags=DATA_FLAG_SOURCE_HASH; pa.type=DATA_TYPE_MOBILE_LAYER_QUERY;
        pa.inner[0]=origin;   // [origin][source_hash 4B LE][body(0)]
        pa.inner[1]=static_cast<uint8_t>(source_hash); pa.inner[2]=static_cast<uint8_t>(source_hash>>8);
        pa.inner[3]=static_cast<uint8_t>(source_hash>>16); pa.inner[4]=static_cast<uint8_t>(source_hash>>24);
        pa.inner_len=5;
        n.do_post_ack();
    }
    static uint8_t  my_mobile_home_leaf(Node& n)  { return n._my_mobile_reg.home_leaf_id; }   // §mobile 5a: the host's LAYER
    static uint8_t  cfg_leaf_id(Node& n)          { return n._cfg.leaf_id; }                  // §mobile 5a: the adopted operating leaf
    static uint16_t cfg_allowed_sf(Node& n)       { return n._cfg.allowed_sf_bitmap; }        // §mobile: the adopted sf_list
    static void     store_mobile_home_on_leaf(Node& n, uint8_t leaf, uint32_t hash, uint8_t home, uint8_t layer) {  // §5b: cache M->home on a SPECIFIC leaf (the bridge target)
        auto& L = n._layers[leaf]; L._mobile_home_cache[L._mobile_home_cache_n++] = { hash, n._hal.now(), home, 0, layer };
    }
    static void     drive_post_ack_breadcrumb(Node& n, uint32_t source_hash, uint8_t new_home, uint8_t epoch, uint8_t new_home_layer = 0) {  // §mobile 4b/5b: run do_post_ack for a BREADCRUMB DM
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending = true; pa.is_forward = false; pa.origin = 5; pa.dst = n._node_id;
        pa.ctr = 0x1234; pa.ctr_lo = 4; pa.flags = DATA_FLAG_SOURCE_HASH; pa.type = DATA_TYPE_MOBILE_BREADCRUMB;
        pa.inner[0] = 5;   // [origin][source_hash 4B LE][body: new_home, epoch, new_home_layer]
        pa.inner[1]=static_cast<uint8_t>(source_hash); pa.inner[2]=static_cast<uint8_t>(source_hash>>8);
        pa.inner[3]=static_cast<uint8_t>(source_hash>>16); pa.inner[4]=static_cast<uint8_t>(source_hash>>24);
        pa.inner[5]=new_home; pa.inner[6]=epoch; pa.inner[7]=new_home_layer; pa.inner_len=8;
        n.do_post_ack();
    }
    static void     drive_post_ack_e2e_ack(Node& n, uint8_t acker, uint32_t source_hash, uint16_t acked_ctr) {   // §mobile reverse-ack: run do_post_ack for a same-layer E2E_ACK carrying SOURCE_HASH
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending=true; pa.is_forward=false; pa.origin=acker; pa.dst=n._node_id;
        pa.ctr=acked_ctr; pa.ctr_lo=static_cast<uint8_t>(acked_ctr & 0x0F); pa.flags=DATA_FLAG_SOURCE_HASH; pa.type=DATA_TYPE_E2E_ACK;
        pa.inner[0]=acker;   // [origin][source_hash 4B LE][ctr_lo][ctr_hi]
        pa.inner[1]=static_cast<uint8_t>(source_hash); pa.inner[2]=static_cast<uint8_t>(source_hash>>8);
        pa.inner[3]=static_cast<uint8_t>(source_hash>>16); pa.inner[4]=static_cast<uint8_t>(source_hash>>24);
        pa.inner[5]=static_cast<uint8_t>(acked_ctr & 0xFF); pa.inner[6]=static_cast<uint8_t>(acked_ctr >> 8); pa.inner_len=7;
        n.do_post_ack();
    }
    static void     deleg_ack_put(Node& n, uint8_t acker, uint16_t ctr_h, uint16_t ctr_m) { n.deleg_ack_put(acker, ctr_h, ctr_m); }   // §mobile reverse-ack: seed the delegated ctr map
    static void     drive_post_ack_mobile_send(Node& n, uint32_t source_hash_M, uint32_t dst_hash_X, uint16_t ctr_M, uint8_t body0) {   // §mobile reverse-ack: a hosted mobile's MOBILE_SEND arriving at its home
        auto& pa = n._active->_post_ack; pa = PostAck{};
        pa.pending=true; pa.is_forward=false; pa.origin=n._node_id; pa.dst=n._node_id;   // a registered mobile stamps origin=home_id (== this home)
        pa.ctr=ctr_M; pa.ctr_lo=static_cast<uint8_t>(ctr_M & 0x0F);
        pa.flags=DATA_FLAG_DST_HASH|DATA_FLAG_SOURCE_HASH|DATA_FLAG_E2E_ACK_REQ; pa.type=DATA_TYPE_MOBILE_SEND;
        pa.inner[0]=static_cast<uint8_t>(dst_hash_X); pa.inner[1]=static_cast<uint8_t>(dst_hash_X>>8);   // [dst_hash 4B LE]...
        pa.inner[2]=static_cast<uint8_t>(dst_hash_X>>16); pa.inner[3]=static_cast<uint8_t>(dst_hash_X>>24);
        pa.inner[4]=n._node_id;   // [origin]
        pa.inner[5]=static_cast<uint8_t>(source_hash_M); pa.inner[6]=static_cast<uint8_t>(source_hash_M>>8);   // [source_hash 4B LE]
        pa.inner[7]=static_cast<uint8_t>(source_hash_M>>16); pa.inner[8]=static_cast<uint8_t>(source_hash_M>>24);
        pa.inner[9]=body0; pa.inner_len=10;   // [body]
        n.do_post_ack();
    }
    static void     bind_authoritative(Node& n, uint8_t node_id, uint32_t key) {   // authoritative id_bind on the ACTIVE leaf (so key_hash_of_id succeeds)
        auto& L = *n._active;
        L._id_bind[L._id_bind_n].key_hash32 = key; L._id_bind[L._id_bind_n].node_id = node_id;
        L._id_bind[L._id_bind_n].last_seen_ms = n._hal.now(); L._id_bind[L._id_bind_n].confidence = 1; ++L._id_bind_n;
    }
    static uint8_t  pick_hop(Node& n, PendingTx& pt)        { return n.pick_next_cascade_hop(pt); }    // §intra-relay Edit 4: cascade pick (0 = no selectable hop -> rediscover)
    static void     pump(Node& n)                           { n.become_free(); }                      // Slice 4a: the kQueueWakeupTimerId handler
    // Slice 4c.1 bridge accessors
    static void     bind_on_leaf(Node& n, uint8_t leaf, uint8_t node_id, uint32_t key) {
        auto& L = n._layers[leaf];
        L._id_bind[L._id_bind_n].key_hash32 = key; L._id_bind[L._id_bind_n].node_id = node_id;
        L._id_bind[L._id_bind_n].last_seen_ms = n._hal.now(); L._id_bind[L._id_bind_n].confidence = 1;  // authoritative
        ++L._id_bind_n;
    }
    static void     bridge_from(Node& n, uint8_t origin, uint8_t dst, uint16_t ctr, uint8_t flags, const uint8_t* inner, uint8_t inner_len) {
        PostAck pa{}; pa.is_forward = false; pa.origin = origin; pa.dst = dst; pa.ctr = ctr;
        pa.ctr_lo = static_cast<uint8_t>(ctr & 0x0F); pa.flags = flags; pa.inner_len = inner_len;
        for (uint8_t i = 0; i < inner_len; ++i) pa.inner[i] = inner[i];
        auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, inner_len), flags);
        if (ui) n.bridge_cross_layer(pa, *ui);
    }
    static void     bridge_ui(Node& n, const data_unicast_inner& ui) {   // §xl-nibble-match: drive bridge_cross_layer with a hand-built cross-layer inner (bypasses wire encode)
        PostAck pa{}; pa.is_forward = false; pa.origin = ui.origin; pa.ctr = 1; pa.ctr_lo = 1; pa.flags = DATA_FLAG_CROSS_LAYER;
        n.bridge_cross_layer(pa, ui);
    }
    static int            handoff_count(Node& n) { int c = 0; for (auto& h : n._xl_handoffs) if (h.valid) ++c; return c; }
    static const XlHandoff* handoff_first(Node& n) { for (auto& h : n._xl_handoffs) if (h.valid) return &h; return nullptr; }
    static void           drain(Node& n, uint8_t leaf)          { n.drain_xl_handoffs_for_leaf(leaf); }
    static uint8_t        leaf_tx_n(Node& n, uint8_t leaf)      { return n._layers[leaf]._tx_queue_n; }
    static const TxItem&  leaf_tx_at(Node& n, uint8_t leaf, uint8_t i) { return n._layers[leaf]._tx_queue[i]; }
    static void           learn_neighbor(Node& n, uint8_t node_id) { n.learn_direct_neighbor(node_id, 40, false); }   // 1-hop route on the ACTIVE leaf
    static bool           is_team_peer(Node& n, uint8_t id) { return n.is_team_peer(id); }   // §6.4: known same-team peer (route via _rt_team)?
    static bool           team_key_of_id(Node& n, uint8_t id, uint32_t& out) { return n.team_key_of_id(id, out); }   // §enc: team-scoped id->key
    static void           learn_team_neighbor(Node& n, uint8_t id, uint32_t key_hash) {   // §enc: a same-team peer as its beacon would leave it — bitmap + _rt_team route + key cache
        n._active->_team_peer[id >> 3] |= static_cast<uint8_t>(1u << (id & 7));
        (void)n.learn_direct_neighbor(id, 40, false, /*team_plane=*/true);
        n.team_key_set(id, key_hash);
    }
    // §2c team-liveness seams
    static void           team_rts_timeout(Node& n, uint8_t id) { n.record_peer_rts_timeout(id, 0, /*team_plane=*/true); }
    static void           team_clear(Node& n, uint8_t id)       { n.clear_peer_suspect(id, "test", /*team_plane=*/true); }
    static int16_t        team_penalty(Node& n, uint8_t id)     { return n.liveness_penalty_q4(id, /*team_plane=*/true); }
    static int16_t        static_penalty(Node& n, uint8_t id)   { return n.liveness_penalty_q4(id, /*static*/false); }
    static bool           static_liveness_absent(Node& n, uint8_t id) { return n.peer_liveness_slot(id, /*create=*/false) == nullptr; }
    static void           team_route2(Node& n, uint8_t dest, uint8_t next1, uint8_t next2) {   // an _rt_team entry: two 2-hop candidates (next1 primary, next2 alt), equal score
        auto& L = *n._active;
        RtEntry& e = L._rt_team[L._rt_team_count++]; e = RtEntry{}; e.dest = dest; e.n = 2;
        e.candidates[0].next_hop = next1; e.candidates[0].hops = 2; e.candidates[0].score = 100; e.candidates[0].last_seen_ms = 1;
        e.candidates[1].next_hop = next2; e.candidates[1].hops = 2; e.candidates[1].score = 100; e.candidates[1].last_seen_ms = 1;
        L._team_peer[next1 >> 3] |= static_cast<uint8_t>(1u << (next1 & 7));
        L._team_peer[next2 >> 3] |= static_cast<uint8_t>(1u << (next2 & 7));
    }
    static uint8_t        team_primary(Node& n, uint8_t dest) {   // candidates[0].next_hop of the _rt_team route to dest (0 if none)
        for (uint8_t i = 0; i < n._active->_rt_team_count; ++i) if (n._active->_rt_team[i].dest == dest) return n._active->_rt_team[i].candidates[0].next_hop;
        return 0;
    }
    static void           send_xl(Node& n, uint8_t dst_node, uint32_t dst_hash, uint8_t target_layer, const uint8_t* body, uint8_t len, uint8_t flags = 0) { n.send_cross_layer(dst_node, dst_hash, target_layer, body, len, flags); }
    static CmdCode        originate(Node& n, uint32_t dst_hash, const uint8_t* hops, uint8_t hc, const uint8_t* body, uint8_t len, uint8_t flags = 0) { uint16_t ctr = 0; return n.originate_layer_path(dst_hash, hops, hc, body, len, flags, ctr); }
    // Multi-hop gateway discovery (type-4 TLV) accessors.
    static void     ingest_bl(Node& n, uint8_t gw, uint8_t dest_leaf) { n.ingest_bridged_layer(gw, dest_leaf); }
    static uint8_t  select_gw(Node& n, uint8_t target_leaf)          { return n.select_gateway_for_leaf(target_leaf); }
    static size_t   build_gw_ext(Node& n, uint8_t* out, size_t cap)  { return n.build_gateway_layer_ext(out, cap); }
    static int      bl_dest(Node& n, uint8_t gw)                     { for (uint8_t i = 0; i < protocol::cap_bridged_layers; ++i) if (n._bridged_layers[i].valid && n._bridged_layers[i].gw_id == gw) return n._bridged_layers[i].dest_leaf; return -1; }
    static void     hear_on_leaf(Node& n, uint8_t id, uint32_t key)  { n.id_bind_set(id, key, Node::IdBindSource::bcn, Node::IdBindConf::authoritative); }   // an authoritative id_bind = "heard on our leaf"
    static uint8_t        parked_count(Node& n)              { return n._parked_sends_n; }
    static uint8_t        deferred_count(Node& n)            { return n._active->_deferred_n; }
    static void           fill_tx_queue(Node& n, uint8_t leaf)  { n._layers[leaf]._tx_queue_n = Node::kTxQueueCap; }   // simulate a full queue
    static void           clear_tx_queue(Node& n, uint8_t leaf) { n._layers[leaf]._tx_queue_n = 0; }
    static void           store_gw_schedule(Node& n, uint8_t gw_node, uint8_t leaf_served) {   // a known gateway WITHOUT a route (4d.2)
        GatewaySchedule gs{}; gs.valid = true; gs.gw_node_id = gw_node; gs.heard_ms = n._hal.now();
        gs.period_ms = 15000; gs.n_rec = 1; gs.rec[0].leaf_id = leaf_served; gs.rec[0].window_ms = 7500; gs.rec[0].offset_ms = 0;
        n.store_gateway_schedule(gs);
    }
    static void           store_gw_schedule_pair(Node& n, uint8_t gw_node, uint8_t leafA, uint8_t leafB) {  // a gateway serving BOTH leaves (4e)
        GatewaySchedule gs{}; gs.valid = true; gs.gw_node_id = gw_node; gs.heard_ms = n._hal.now(); gs.period_ms = 15000; gs.n_rec = 2;
        gs.rec[0].leaf_id = leafA; gs.rec[0].window_ms = 7500; gs.rec[0].offset_ms = 0;
        gs.rec[1].leaf_id = leafB; gs.rec[1].window_ms = 7500; gs.rec[1].offset_ms = 7500;   // leafB opens later -> a node on leafB defers (held)
        n.store_gateway_schedule(gs);
    }
    static void           send_xl_ack(Node& n, const data_unicast_inner& dm, uint16_t ctr) { n.send_e2e_ack_cross_layer(dm, ctr); }
    // gateway-window broadcast sync (2026-06-20 side-task)
    static uint32_t       align_beacon(Node& n, uint32_t nominal)  { return n.gateway_window_align_beacon(nominal); }
    static uint32_t       gw_base_defer(Node& n, uint8_t gw)        { uint32_t j = 0; return n.gateway_schedule_base_defer_ms(gw, &j); }
    static void           force_exit_discovery(Node& n)            { n._active->_discovery_mode = false; }
    static bool           disc_mode(Node& n, uint8_t i)            { return n._layers[i]._discovery_mode; }              // §per-layer discovery: read a leaf's discovery flag
    static void           seed_disc_bcn(Node& n, uint8_t i, uint16_t c) { n._layers[i]._discovery_bcn_rx_count = c; }    // §per-layer discovery: seed a leaf's beacon-rx count
    static void           run_exit_discovery(Node& n)              { n.maybe_exit_discovery("test"); }                   // §per-layer discovery: exit-check on the ACTIVE leaf
    // gateway reactive route-pull on a cross-layer bridge miss (spec 2026-06-21)
    static void           issue(Node& n, const TxItem& it)         { n.issue_send(it); }
    static void           req_sync(Node& n, bool force)            { n.send_req_sync_q("test", force); }
    static void           set_active_rt_count(Node& n, uint8_t c)  { n._active->_rt_count = c; }
    static uint64_t       last_req_sync_ms(Node& n)                { return n._last_req_sync_tx_ms; }   // set just before the REQ_SYNC tx
    static void           drain_parked(Node& n, uint32_t key, uint8_t resolved, uint8_t layer) { n.drain_parked_sends(key, resolved, layer); }   // simulate the H-answer
    static uint8_t        pending_dst(Node& n)              { return n._active->_pending_tx ? n._active->_pending_tx->dst : 0; }
    static uint8_t        pending_flags(Node& n)            { return n._active->_pending_tx ? n._active->_pending_tx->flags : 0xFF; }
    static bool           parked_cross_layer(Node& n, uint8_t i) { return n._parked_sends[i].cross_layer; }
    static void           set_pending_rx(Node& n, uint8_t from, uint8_t ctr_lo, uint8_t sf, uint8_t payload_len) {  // simulate a prior RTS/CTS so handle_data accepts the DATA
        PendingRx pr{}; pr.from = from; pr.ctr_lo = ctr_lo; pr.chosen_data_sf = sf; pr.payload_len = payload_len;
        pr.set_at_ms = n._hal.now(); pr.expiry_ms = n._hal.now() + 100000;
        n._active->_pending_rx = pr;
    }
    // Slice 4 gateway-exemption verification (test infra only): originate an OWN e2e-ack + peek the last-enqueued item.
    static void           send_ack(Node& n, uint8_t to, uint16_t ctr) { n.send_e2e_ack(to, ctr); }   // an OWN e2e-ack origination (DATA_TYPE_E2E_ACK)
    static const TxItem*  tx_back(Node& n, uint8_t leaf) {                                            // the last-enqueued item on a leaf (nullptr if empty)
        uint8_t k = n._layers[leaf]._tx_queue_n; return k ? &n._layers[leaf]._tx_queue[k - 1] : nullptr;
    }
};
}  // namespace meshroute

// ---- per-layer bandwidth (+ CR): LayerConfig.bw_hz/cr + active_bw_hz()/active_cr() (2026-07-04) --------------
// A layer becomes a full (freq, SF, BW, CR) channel. bw_hz/cr default 0 = inherit the global radio_bw_hz/radio_cr;
// the accessor returns the ACTIVE leaf's value (config-index idiom), so a single-layer node is byte-identical.
TEST_CASE("per-layer-bw: LayerConfig carries bw_hz/cr defaulting to 0 (inherit)") {
    LayerConfig L{};
    CHECK(L.bw_hz == 0u);
    CHECK(L.cr == 0);
}

TEST_CASE("per-layer-bw: active_bw_hz/active_cr return the ACTIVE layer's value, global fallback on 0") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2; cfg.radio_bw_hz = 250000; cfg.radio_cr = 5;
    cfg.layers[0] = good_layer(1, 8);                          // bw_hz/cr left 0 -> inherit the global
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].bw_hz = 125000; cfg.layers[1].cr = 8;
    CHECK(node.on_init(cfg));
    CHECK(node.active_bw_hz() == 250000u); CHECK(node.active_cr() == 5);   // layer 0 active -> inherit
    DualLayerTestAccess::set_active(node, 1);
    CHECK(node.active_bw_hz() == 125000u); CHECK(node.active_cr() == 8);   // layer 1 -> its override
    DualLayerTestAccess::set_active(node, 0);
    CHECK(node.active_bw_hz() == 250000u); CHECK(node.active_cr() == 5);   // back to inherit
}

TEST_CASE("per-layer-bw: single-layer node — active_bw_hz()==global radio_bw_hz (migration parity)") {
    StubHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 12);
    cfg.radio_bw_hz = 250000; cfg.radio_cr = 5;
    CHECK(node.on_init(cfg));
    CHECK(node.active_bw_hz() == 250000u);   // the sole layer inherits -> identical to _cfg.radio_bw_hz
    CHECK(node.active_cr() == 5);
}

TEST_CASE("per-layer-bw: the window switch ALWAYS syncs BW/CR to the active leaf's effective PHY (no stale _def_bw)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2; cfg.radio_bw_hz = 250000; cfg.radio_cr = 5;
    cfg.layers[0] = good_layer(1, 8);                                              // layer 0: INHERIT (bw_hz/cr = 0)
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].bw_hz = 125000; cfg.layers[1].cr = 8;
    CHECK(node.on_init(cfg));
    // boot activated layer 0 (inherit) -> retunes to the EFFECTIVE global (NOT skipped — that's the invariant).
    CHECK(hal.last_set_rx_bw == 250000u);   // radio_bw_hz
    CHECK(hal.last_set_rx_cr == 5);         // radio_cr
    // swap to layer 1 (override) -> its own BW/CR.
    DualLayerTestAccess::activate(node, 1);
    CHECK(hal.last_set_rx_bw == 125000u);
    CHECK(hal.last_set_rx_cr == 8);
    // ★ swap BACK to layer 0 (inherit) MUST reset to the global — the stale-_def_bw regression: with an `if (L.bw_hz>0)`
    // guard this would stay 125000 and TX would fly on the wrong BW while active_bw_hz() charges 250000 (charge!=transmit).
    DualLayerTestAccess::activate(node, 0);
    CHECK(hal.last_set_rx_bw == 250000u);   // reset to the global, NOT stale at 125000
    CHECK(hal.last_set_rx_cr == 5);
}

TEST_CASE("per-layer-bw: parse_gateway_cmd bw0/bw1 are kHz (fractional) -> Hz; validate gates illegal PHY") {
    GatewayProvision g{};
    // bw in kHz, matching create/join — 250 -> 250000 Hz, 62.5 (fractional) -> 62500 Hz.
    CHECK(parse_gateway_cmd("l0=1:1:8:8 l1=2:1:9:9 bw0=250 bw1=62.5 cr0=5 cr1=8", g) == GwParseErr::ok);
    CHECK(g.l0.bw_hz == 250000u); CHECK(g.l1.bw_hz == 62500u);   // kHz->Hz, fractional rounded
    CHECK(g.l0.cr == 5);          CHECK(g.l1.cr == 8);
    // validate ACCEPTS legal per-layer PHY (each layer's window derives off ITS OWN bw)
    { GatewayProvision v = g; CHECK(validate_gateway_layers(v.l0, v.l1, 250000, 5) == GwValErr::ok); }
    // REJECTS an illegal per-layer BW (100000 Hz is not an SX1262 bandwidth)
    { GatewayProvision v = g; v.l1.bw_hz = 100000; CHECK(validate_gateway_layers(v.l0, v.l1, 250000, 5) == GwValErr::bad_bw); }
    // REJECTS an illegal per-layer CR
    { GatewayProvision v = g; v.l1.cr = 99;        CHECK(validate_gateway_layers(v.l0, v.l1, 250000, 5) == GwValErr::bad_cr); }
    // 0 = inherit is always OK (the accessor resolves it)
    { GatewayProvision v = g; v.l0.bw_hz = 0; v.l1.cr = 0; CHECK(validate_gateway_layers(v.l0, v.l1, 250000, 5) == GwValErr::ok); }
}

TEST_CASE("per-layer-bw: gateway parse keeps each leaf's OWN node/data_sf (no l0<->l1 bleed)") {
    // l0 = node 2, data 7,9 ; l1 = node 3, data 6,7. parse_gateway_cmd must not bleed one leaf's node/data into the
    // other. (Verified 2026-07-04 after a field report that traced to stale NV, not a code defect — isolation holds.)
    GatewayProvision g{};
    CHECK(parse_gateway_cmd("l0=102:2:8:7,9 l1=100:3:7:6,7 bw0=125 bw1=62.5 freq0=869 freq1=869", g) == GwParseErr::ok);
    CHECK(g.l0.layer_id == 102); CHECK(g.l0.node_id == 2);  CHECK(g.l0.routing_sf == 8);
    CHECK(g.l0.allowed_sf_bitmap == static_cast<uint16_t>((1u << 7) | (1u << 9)));   // 7,9 — NOT l1's 6,7
    CHECK(g.l1.layer_id == 100); CHECK(g.l1.node_id == 3);  CHECK(g.l1.routing_sf == 7);
    CHECK(g.l1.allowed_sf_bitmap == static_cast<uint16_t>((1u << 6) | (1u << 7)));   // 6,7
    CHECK(g.l0.bw_hz == 125000u); CHECK(g.l1.bw_hz == 62500u);
}

TEST_CASE("per-layer-bw: a 2-layer config charges each layer its OWN airtime (charge==transmit invariant)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2; cfg.radio_bw_hz = 250000; cfg.radio_cr = 5;
    cfg.layers[0] = good_layer(1, 9); cfg.layers[0].bw_hz = 250000;   // layer 0: WIDE BW (same SF as layer 1)
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].bw_hz = 125000;   // layer 1: NARROW BW -> ~2x airtime/byte
    CHECK(node.on_init(cfg));
    // active_bw_hz() tracks the active leaf; a same-size frame is charged that leaf's airtime.
    CHECK(node.active_bw_hz() == 250000u);                            // layer 0 active
    const double air_l0 = airtime_ms(9, node.active_bw_hz(), node.active_cr(), protocol::preamble_sym, 120);
    DualLayerTestAccess::activate(node, 1);
    CHECK(node.active_bw_hz() == 125000u);                            // window switch -> layer 1
    const double air_l1 = airtime_ms(9, node.active_bw_hz(), node.active_cr(), protocol::preamble_sym, 120);
    CHECK(air_l1 > air_l0 * 1.5);   // half the BW ~doubles airtime — each layer charged its OWN PHY, not the global
}

// ---- §GW: gateway <-> leaf-config membership exemption (metal 2026-07-05) -----------------------------------
// A gateway must NOT participate in the R6.1 leaf-config membership plane, in EITHER direction:
//   (A) a gateway does not adopt a managed leaf's lineage / fire a config-pull (the stray REQ_SYNC on metal);
//   (B) a managed member DOES route a gateway neighbour (self_gateway) despite the lineage mismatch.
TEST_CASE("§GW exemption (A): a gateway does NOT adopt a managed leaf's lineage; it peers by nibble") {
    // Neighbour = a MANAGED leaf (lineage != 0 -> config_hash != 0) on leaf 6.
    StubHal halN; Node nb(halN, /*id*/ 50, 0xAAAAu);
    NodeConfig cn; cn.routing_sf = 8; cn.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cn.leaf_id = 6;
    cn.lineage_id = 1234; cn.config_epoch = 1;
    CHECK(nb.on_init(cn));
    DualLayerTestAccess::emit(nb, "periodic");                       // pack the managed-leaf beacon -> halN.last_tx
    CHECK(halN.last_tx_len > 0);
    // The gateway: n_layers==2, UNMANAGED (lineage 0), leaf 6 active (102 & 0x0F == 6).
    StubHal halG; Node gw(halG, 1, 0x1u);
    NodeConfig cg; cg.n_layers = 2;
    cg.layers[0] = good_layer(102, 8); cg.layers[0].node_id = 2;     // leaf 6
    cg.layers[1] = good_layer(100, 9); cg.layers[1].node_id = 3;     // leaf 4 (different nibble -> valid gateway)
    CHECK(gw.on_init(cg));                                           // boot activates layer 0 (leaf 6)
    CHECK(DualLayerTestAccess::lineage(gw) == 0);
    DualLayerTestAccess::ingest_bcn(gw, halN.last_tx, halN.last_tx_len, /*snr*/ 10.0f);
    // THE FIX: NO adopt (lineage stays 0) + a route IS learned (peer-by-nibble). Pre-fix: adopt 1234 + REQ_SYNC + NO route.
    CHECK(DualLayerTestAccess::lineage(gw) == 0);
    CHECK(DualLayerTestAccess::rt_has(gw, 50));
}

TEST_CASE("§GW exemption (B): a managed member DOES route a gateway neighbour despite the lineage mismatch") {
    // Neighbour = a GATEWAY (self_gateway, lineage 0), node 2 on leaf 6.
    StubHal halG; Node gw(halG, 1, 0x1u);
    NodeConfig cg; cg.n_layers = 2;
    cg.layers[0] = good_layer(102, 8); cg.layers[0].node_id = 2;     // leaf 6 active -> self_gateway beacon, src=2, lineage 0
    cg.layers[1] = good_layer(100, 9); cg.layers[1].node_id = 3;
    CHECK(gw.on_init(cg));
    DualLayerTestAccess::emit(gw, "periodic");                       // pack the gateway beacon -> halG.last_tx
    CHECK(halG.last_tx_len > 0);
    // The member = a MANAGED leaf (lineage != 0) on leaf 6.
    StubHal halM; Node member(halM, /*id*/ 60, 0xBBBBu);
    NodeConfig cm; cm.routing_sf = 8; cm.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cm.leaf_id = 6;
    cm.lineage_id = 5678; cm.config_epoch = 1;
    CHECK(member.on_init(cm));
    DualLayerTestAccess::ingest_bcn(member, halG.last_tx, halG.last_tx_len, /*snr*/ 10.0f);
    // THE FIX: the managed member learns a route to the gateway (node 2) despite lineage 5678 != 0. Pre-fix: :462 return, no route.
    CHECK(DualLayerTestAccess::rt_has(member, 2));
}

// ---- §intra-layer-relay: senders never route TRANSIT through a gateway (recognition = is_gateway_dest, NOT id-range) --
TEST_CASE("§intra-relay Edit 3: reject TRANSIT through a LEARNED gateway; a normal low-id node is STILL accepted") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 0;
    CHECK(node.on_init(cfg));
    // Seed a LEARNED gateway (id 3) via its schedule -> is_gateway_dest(3)==true. A normal low id (5) is NOT a gateway.
    DualLayerTestAccess::store_gw_schedule(node, /*gw_node*/ 3, /*leaf_served*/ 0);
    CHECK(DualLayerTestAccess::is_gwdest(node, 3));
    CHECK_FALSE(DualLayerTestAccess::is_gwdest(node, 5));   // ★ a low id (1..16) is NOT auto-a-gateway — the reservation is NOT the gate
    PendingTx pt{}; pt.dst = 50;
    RtCandidate viaGw{};   viaGw.next_hop   = 3;            // TRANSIT via the gateway 3 (next_hop != dst 50)
    RtCandidate viaNorm{}; viaNorm.next_hop = 5;            // TRANSIT via a NORMAL low-id node 5 (the regression guard)
    CHECK_FALSE(DualLayerTestAccess::selectable(node, viaGw, pt));   // REJECT: never transit through a gateway
    CHECK(DualLayerTestAccess::selectable(node, viaNorm, pt));       // ACCEPT: a normal low-id relay (no id-range misfire -> s18 + the 41 tests stay green)
    { PendingTx toGw{}; toGw.dst = 3; RtCandidate c{}; c.next_hop = 3;
      CHECK(DualLayerTestAccess::selectable(node, c, toGw)); }       // routing TO the gateway (next_hop==dst) = cross-layer egress -> ACCEPT
}

TEST_CASE("§intra-relay Edit 2: a gateway DROPS an intra-leaf forward (no relay); the opt-in re-enables it") {
    StubHal hal; Node gw(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(102, 8); cfg.layers[0].node_id = 2;   // leaf 6, our id 2
    cfg.layers[1] = good_layer(100, 9); cfg.layers[1].node_id = 3;
    CHECK(gw.on_init(cfg));                                          // gateway, intra_layer_relay=false (default)
    // A FORWARD PostAck: dst=50 is a third-party same-leaf id (!= our node_id 2) -> the forward branch on a gateway.
    DualLayerTestAccess::drive_post_ack_forward(gw, /*dst*/ 50, /*origin*/ 7);
    CHECK(hal.saw_emit("gateway_intra_relay_drop"));                 // dropped: a gateway does NOT relay intra-leaf traffic
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 0) == 0);              // nothing enqueued (not relayed)
    // OPT-IN: intra_layer_relay=true -> the drop no longer fires (it proceeds to the normal forward path).
    hal.emits.clear();
    DualLayerTestAccess::set_intra_relay(gw, true);
    DualLayerTestAccess::drive_post_ack_forward(gw, /*dst*/ 50, /*origin*/ 7);
    CHECK_FALSE(hal.saw_emit("gateway_intra_relay_drop"));           // opt-in: no drop
}

TEST_CASE("§intra-relay Edit 4: an only-a-gateway route -> pick_next_cascade_hop returns 0 (the originator rediscovers)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 0;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::store_gw_schedule(node, /*gw*/ 3, /*leaf*/ 0);          // a LEARNED gateway (id 3)
    CHECK(node.route_inject(/*dest*/ 50, /*next_hop*/ 3, /*hops*/ 2, /*score*/ 100));   // the ONLY route to 50 is via the gateway
    PendingTx pt{}; pt.dst = 50;
    // Edit 3 rejects the gateway transit -> no selectable candidate -> pick returns 0 (node_mac.cpp then defers + RREQs).
    CHECK(DualLayerTestAccess::pick_hop(node, pt) == 0);
    // Sanity: add a NORMAL-node route to the same dest -> now pickable (proves the 0 was the gateway rejection, not a broken route).
    CHECK(node.route_inject(/*dest*/ 50, /*next_hop*/ 5, /*hops*/ 2, /*score*/ 90));    // via a normal low-id node 5
    CHECK(DualLayerTestAccess::pick_hop(node, pt) == 5);
}

// ---- §per-layer discovery: a gateway bootstraps each leaf independently (2026-07-05) ------------------------
TEST_CASE("§per-layer discovery: the boot leaf's exit does NOT starve the far leaf out of fast-cadence discovery") {
    StubHal hal; Node gw(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(102, 8); cfg.layers[0].node_id = 2;
    cfg.layers[1] = good_layer(100, 9); cfg.layers[1].node_id = 3;
    CHECK(gw.on_init(cfg));
    // BOTH leaves enter discovery at boot (per-leaf, not node-global).
    CHECK(DualLayerTestAccess::disc_mode(gw, 0));
    CHECK(DualLayerTestAccess::disc_mode(gw, 1));
    // Drive the BOOT leaf (0, active) to its exit threshold -> it exits, but leaf 1 must STAY in discovery.
    DualLayerTestAccess::seed_disc_bcn(gw, 0, protocol::discovery_min_bcn_rx);
    DualLayerTestAccess::run_exit_discovery(gw);            // maybe_exit_discovery on the active leaf (0)
    CHECK_FALSE(DualLayerTestAccess::disc_mode(gw, 0));     // leaf 0 exited
    CHECK(DualLayerTestAccess::disc_mode(gw, 1));           // ★ leaf 1 STILL in discovery — the boot leaf did NOT starve it
    CHECK_FALSE(gw.in_discovery());                         // active leaf 0 -> not in discovery
    // Switch to leaf 1: still in discovery; seed IT -> it exits on its OWN bootstrap (independent).
    DualLayerTestAccess::activate(gw, 1);
    CHECK(gw.in_discovery());                               // leaf 1 active -> in discovery (its own fast-cadence window)
    DualLayerTestAccess::seed_disc_bcn(gw, 1, protocol::discovery_min_bcn_rx);
    DualLayerTestAccess::run_exit_discovery(gw);
    CHECK_FALSE(DualLayerTestAccess::disc_mode(gw, 1));     // leaf 1 exited independently
}

// ---- §per-layer-id: persist layer0's canonical id, not the active-window mirror (2026-07-05) ----------------
TEST_CASE("§per-layer-id: canonical_node_id() is layer0's id, INDEPENDENT of the active window (no persist clobber)") {
    StubHal hal; Node gw(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(100, 8); cfg.layers[0].node_id = 4;   // layer 0 = leaf 4, id 4 (the canonical nv.node_id)
    cfg.layers[1] = good_layer(102, 9); cfg.layers[1].node_id = 5;   // layer 1 = leaf 6, id 5
    CHECK(gw.on_init(cfg));                                          // boot activates layer 0 -> node_id()==4
    CHECK(gw.node_id() == 4);
    CHECK(gw.canonical_node_id() == 4);
    // Switch to layer 1: node_id() mirrors to 5, but canonical_node_id() MUST stay 4 (the persist-while-on-layer1 case).
    DualLayerTestAccess::activate(gw, 1);
    CHECK(gw.node_id() == 5);                                       // active mirror flipped with the window
    CHECK(gw.canonical_node_id() == 4);                            // ★ a cfg-set snapshot while on layer1 now records 4, not the mirror 5
    DualLayerTestAccess::activate(gw, 0);
    CHECK(gw.canonical_node_id() == 4);
}

// ---- §xl-nibble-match: the cross-layer bridge matches the target leaf by NIBBLE, not the full 8-bit id (2026-07-05) --
TEST_CASE("§xl-nibble-match: bridge resolves the target leaf by NIBBLE (metal full-id gateway; the reverse ack bridges home)") {
    StubHal hal; hal._now = 10000;
    Node g(hal, /*id*/ 1, 0xABCDu);
    NodeConfig gc; gc.n_layers = 2;
    gc.layers[0] = good_layer(100, 8); gc.layers[0].node_id = 5;    // FULL id 100 -> leaf nibble 4
    gc.layers[1] = good_layer(102, 8); gc.layers[1].node_id = 12;   // FULL id 102 -> leaf nibble 6
    CHECK(g.on_init(gc));
    // A reversed 4e ack path targeting the NIBBLE 4 (a single-layer originator reported its layer as leaf_id=4).
    data_unicast_inner ui{}; ui.origin = 7; ui.has_cross_layer = true; ui.n_layers = 2; ui.cur = 1;
    ui.layer_ids[0] = 6; ui.layer_ids[1] = 4;                       // [came-from 102(nibble 6), target = origin nibble 4]
    ui.has_dst_hash = true; ui.dst_key_hash32 = 0x9999u;
    hal.emits.clear();
    DualLayerTestAccess::bridge_ui(g, ui);
    CHECK_FALSE(hal.saw_emit("xl_bridge_refused"));                 // ★ nibble 4 matches layer0 (100&0x0F) -> NOT refused (pre-fix: refused reason=1)
    CHECK(DualLayerTestAccess::handoff_count(g) == 1);             // bridged toward leaf 0 (4f defer -> handoff)
    // Forward path unaffected: a FULL-id target (102) still resolves to leaf 1 (matches pre- AND post-fix).
    data_unicast_inner uf{}; uf.origin = 7; uf.has_cross_layer = true; uf.n_layers = 2; uf.cur = 1;
    uf.layer_ids[0] = 4; uf.layer_ids[1] = 102; uf.has_dst_hash = true; uf.dst_key_hash32 = 0x8888u;
    hal.emits.clear();
    DualLayerTestAccess::bridge_ui(g, uf);
    CHECK_FALSE(hal.saw_emit("xl_bridge_refused"));                 // full-id 102 -> nibble 6 -> leaf 1, still bridges
}

TEST_CASE("dual-layer dedup: the same (origin,dst,ctr) key on two leaves does NOT collide (§8; node_mac_rx.cpp:393)") {
    StubHal hal; Node node(hal, /*id*/1, /*key*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));   // -fno-exceptions => CHECK only (REQUIRE throws); valid gateway always inits

    // The dedup key carries NO layer bits — `sokey = (origin<<24)|(dst<<16)|ctr`. The SAME tuple legitimately
    // occurs on BOTH leaves (ids/ctrs are per-layer namespaces). Pre-fix (one global map) leaf-1's frame would
    // be falsely deduped against leaf-0's; the per-LayerRuntime maps (Slice 2a) isolate them. Assert it.
    const uint32_t K = (uint32_t(5) << 24) | (uint32_t(9) << 16) | 42u;   // origin=5, dst=9, ctr=42
    const uint64_t t = 1000;

    DualLayerTestAccess::set_active(node, 0);
    node.record_seen_origin(K, /*from*/ 7, t);
    CHECK(node.seen_origin_live(K, t));            // recorded on leaf 0
    CHECK(node.seen_origin_count() == 1);

    DualLayerTestAccess::set_active(node, 1);        // switch active leaf (gateway window-swap; Slice 3 wraps it)
    CHECK_FALSE(node.seen_origin_live(K, t));        // THE FIX: the same key is UNKNOWN on leaf 1 (no aliasing)
    CHECK(node.seen_origin_count() == 0);
    CHECK(DualLayerTestAccess::seen_from(node, 1).count(K) == 0);   // the LOOP_DUP prev-hop map is per-layer too

    node.record_seen_origin(K, /*from*/ 8, t);       // record the SAME key on leaf 1, from a DIFFERENT prev-hop
    CHECK(node.seen_origin_live(K, t));
    CHECK(node.seen_origin_count() == 1);
    { auto& sf = DualLayerTestAccess::seen_from(node, 1);                    // leaf 1 records prev-hop 8 ...
      auto it = sf.find(K); CHECK((it != sf.end() && it->second == 8)); }

    DualLayerTestAccess::set_active(node, 0);         // leaf 0 is untouched by leaf-1's record
    CHECK(node.seen_origin_live(K, t));
    CHECK(node.seen_origin_count() == 1);
    { auto& sf = DualLayerTestAccess::seen_from(node, 0);                    // ... leaf 0 KEEPS its own prev-hop 7
      auto it = sf.find(K); CHECK((it != sf.end() && it->second == 7)); }   // (not aliased/overwritten by leaf 1's 8)

    // Structural: the per-leaf dedup maps are DISTINCT objects (seen_origins + _seen_origin_from + last_acked_from).
    CHECK(&DualLayerTestAccess::seen(node, 0)       != &DualLayerTestAccess::seen(node, 1));
    CHECK(&DualLayerTestAccess::seen_from(node, 0)  != &DualLayerTestAccess::seen_from(node, 1));
    CHECK(&DualLayerTestAccess::last_acked(node, 0) != &DualLayerTestAccess::last_acked(node, 1));

    // last_acked_from non-aliasing: an entry on leaf 0 is absent on leaf 1 (default-construct via operator[]).
    const uint32_t LA = (uint32_t(7) << 24) | (uint32_t(9) << 16) | (uint32_t(3) << 8) | 12u;
    DualLayerTestAccess::last_acked(node, 0)[LA];
    CHECK(DualLayerTestAccess::last_acked(node, 0).count(LA) == 1);
    CHECK(DualLayerTestAccess::last_acked(node, 1).count(LA) == 0);
}

TEST_CASE("dual-layer dedup: the Q REQ_SYNC plane (_q_responded / _sync_pending) is per-layer too (Slice 2b)") {
    StubHal hal; Node node(hal, /*id*/1, /*key*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));

    // _q_responded keys on (opcode, src=REMOTE leaf-local id, dest). src=5 is a DIFFERENT physical node on each
    // leaf, so a responder-dedup mark on leaf 0 must NOT suppress the reply owed to node-5 on leaf 1 (the gateway
    // aliasing bug, Principle 5). hal.now()==0 throughout, so the mark is within q_respond_ttl_ms.
    DualLayerTestAccess::set_active(node, 0);
    DualLayerTestAccess::mark_q(node, /*opcode*/ 0, /*src*/ 5, /*dest*/ 0xFF);
    CHECK(DualLayerTestAccess::q_recent(node, 0, 5, 0xFF));         // responded on leaf 0
    DualLayerTestAccess::set_active(node, 1);
    CHECK_FALSE(DualLayerTestAccess::q_recent(node, 0, 5, 0xFF));   // THE FIX: NOT suppressed on leaf 1
    DualLayerTestAccess::mark_q(node, 0, 5, 0xFF);                  // leaf 1 marks independently
    DualLayerTestAccess::set_active(node, 0);
    CHECK(DualLayerTestAccess::q_recent(node, 0, 5, 0xFF));         // leaf 0 still holds its own mark

    // _sync_pending (the jittered full-table-reply ring) is a distinct per-leaf object.
    CHECK(&DualLayerTestAccess::sync_pending(node, 0) != &DualLayerTestAccess::sync_pending(node, 1));
}

// ---- SLICE 3a: SF-weighted anti-phase window derivation (§0.9/§4) + validate-not-clamp (§3.2) -----------
TEST_CASE("dual-layer scheduler: equal-SF gateway derives a 50/50 anti-phase split (Slice 3a, §0.9)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 8);   // SAME SF -> equal weight -> 50/50
    CHECK(node.on_init(cfg));
    const NodeConfig& c = node.config();
    CHECK(c.layers[0].window_ms == 7500);                  // 15000 * 0.5
    CHECK(c.layers[1].window_ms == 7500);
    CHECK(c.layers[0].window_offset_ms == 0);              // layer 0 opens at cycle start
    CHECK(c.layers[1].window_offset_ms == 7500);           // anti-phase: layer 1 opens when layer 0 closes
    CHECK(c.layers[0].window_ms + c.layers[1].window_ms == c.layers[0].window_period_ms);  // fills the period
}

TEST_CASE("dual-layer scheduler: SF-weighted split gives the higher-SF leaf the LONGER window (Slice 3a, §0.9)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);   // SF8 vs SF9
    CHECK(node.on_init(cfg));
    const NodeConfig& c = node.config();
    const uint32_t period = c.layers[0].window_period_ms;
    CHECK(c.layers[0].window_ms > 0);
    CHECK(c.layers[1].window_ms > 0);
    CHECK(c.layers[0].window_ms < c.layers[1].window_ms);             // SF9 (slower/more airtime/byte) -> longer window
    CHECK(c.layers[0].window_ms + c.layers[1].window_ms == period);   // back-to-back, fills the period exactly
    CHECK(c.layers[1].window_offset_ms == c.layers[0].window_ms);     // anti-phase
}

TEST_CASE("dual-layer scheduler: an explicit window_ms fills the remainder + derives the anti-phase offset (Slice 3a)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
    cfg.layers[0].window_ms = 6000;                        // explicit layer 0; layer 1 derives the remainder
    CHECK(node.on_init(cfg));
    const NodeConfig& c = node.config();
    CHECK(c.layers[0].window_ms == 6000);
    CHECK(c.layers[1].window_ms == 9000);                  // 15000 - 6000
    CHECK(c.layers[1].window_offset_ms == 6000);           // anti-phase off the explicit window
}

TEST_CASE("dual-layer scheduler: on_init REFUSES a 0 window_period (validate-not-clamp, §3.2)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
    cfg.layers[0].window_period_ms = 0; cfg.layers[1].window_period_ms = 0;
    CHECK_FALSE(node.on_init(cfg));
}

TEST_CASE("dual-layer cfg: single-layer mirrors node_id into layers[0] + window == whole period (Slice 3a)") {
    StubHal hal; Node node(hal, /*id*/ 42, 0x1);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 12); cfg.leaf_id = 3;
    CHECK(node.on_init(cfg));
    const NodeConfig& c = node.config();
    CHECK(c.layers[0].node_id == 42);                      // the one node_id mirrors into layers[0]
    CHECK(c.layers[0].window_ms == c.layers[0].window_period_ms);   // single-layer: always-on
}

// ---- SLICE 3c: leaf activation (activate_layer) + the §4 busy-guard + sync-response timer migration ----------
TEST_CASE("dual-layer activation: activate_layer swaps the leaf + retunes SF / identity / SNR floor (Slice 3c)") {
    StubHal hal; Node node(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].node_id = 12;
    CHECK(node.on_init(cfg));
    // boot activates leaf 0: active = layers[0]; node_id 5 (overrides the ctor's 1); SF8; leaf nibble 1.
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 0));
    CHECK(node.node_id() == 5);
    CHECK(node.config().routing_sf == 8);
    CHECK(node.config().leaf_id == 1);
    CHECK(hal.last_set_rx_sf == 8);
    CHECK(hal.last_set_protocol_id == 5);
    const int16_t floor_sf8 = DualLayerTestAccess::snr_floor(node);
    // swap to leaf 1
    DualLayerTestAccess::activate(node, 1);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 1));
    CHECK(node.node_id() == 12);                          // per-leaf identity follows the active leaf
    CHECK(node.config().routing_sf == 9);
    CHECK(node.config().leaf_id == 2);                   // layer_id 2 & 0x0F
    CHECK(hal.last_set_rx_sf == 9);                      // radio retuned to the new SF
    CHECK(hal.last_set_protocol_id == 12);
    CHECK(DualLayerTestAccess::snr_floor(node) != floor_sf8);   // SNR floor recomputed per-leaf (SF9 != SF8)
}

TEST_CASE("dual-layer activation: layer_swap_blocked is the §4 busy-guard (pending_tx/rx, post_ack, nack_wait) (Slice 3c)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
    CHECK(node.on_init(cfg));
    CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));                                                  // idle -> free to switch
    DualLayerTestAccess::set_pending_tx(node, true);  CHECK(DualLayerTestAccess::swap_blocked(node));      // in-flight tx
    DualLayerTestAccess::set_pending_tx(node, false); CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
    DualLayerTestAccess::set_pending_rx(node, true);  CHECK(DualLayerTestAccess::swap_blocked(node));      // in-flight rx
    DualLayerTestAccess::set_pending_rx(node, false); CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
    DualLayerTestAccess::set_post_ack(node, true);    CHECK(DualLayerTestAccess::swap_blocked(node));      // post-ACK straddles the ACK
    DualLayerTestAccess::set_post_ack(node, false);   CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
    DualLayerTestAccess::set_nack_wait(node, true);   CHECK(DualLayerTestAccess::swap_blocked(node));      // paused BUSY_RX re-RTS
    DualLayerTestAccess::set_nack_wait(node, false);  CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
    // Slice 3c hardening (id-classification guard_defer): the Node-global transient stash also defers the swap.
    DualLayerTestAccess::set_deferred_lbt(node, true);  CHECK(DualLayerTestAccess::swap_blocked(node));     // LBT-deferred frame on the leaving leaf's SF
    DualLayerTestAccess::set_deferred_lbt(node, false); CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
    DualLayerTestAccess::set_tx_stash(node, true);      CHECK(DualLayerTestAccess::swap_blocked(node));     // on-radio-busy/duty re-issue PENDING (a timer armed)
    DualLayerTestAccess::set_tx_stash(node, false);     CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
    // THE BUG FIX (gateway swap-stall): a cleanly-sent CTS/ACK leaves the stash `valid` (only cleared by a newer
    // same-tag TX / an on_radio_busy giveup) — that is NOT mid-exchange and MUST NOT block the swap, or a gateway's
    // first ACK strands its layer scheduler forever and cross-layer DMs never bridge.
    DualLayerTestAccess::set_tx_stash_clean(node);      CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
}

TEST_CASE("swap-fix: the BUSY-retry re-issue (retry_stashed) clears reissue_pending -> no gateway swap deadlock") {
    // Regression for the consolidated-review HIGH: on_radio_busy arms a busy-retry (valid + reissue_pending); when the
    // timer fires, retry_stashed re-transmits via _hal.tx DIRECTLY (not tx_with_retry, which would reset the flag) — so
    // it MUST clear reissue_pending itself, else the gateway's layer swap is blocked forever.
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2; cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::set_tx_stash(node, true);                       // a busy re-issue is armed (valid + reissue_pending) on slot 0
    CHECK(DualLayerTestAccess::swap_blocked(node));                      // blocked while pending
    DualLayerTestAccess::fire_busy_retry(node, 0);                       // kRadioBusyRetryTimerId fires -> re-transmits
    CHECK_FALSE(DualLayerTestAccess::stash_reissue_pending(node, 0));    // THE FIX: cleared on re-issue
    CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));               // -> the leaf swap is free again
}

TEST_CASE("dual-layer hardening: a swap re-homes the leaving leaf's deferred-drain (no stranded no-route DMs) (Slice 3c)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2; cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
    CHECK(node.on_init(cfg));
    // leaf 0 has a no-route DM parked: _deferred_n>0 + the drain armed.
    DualLayerTestAccess::set_deferred(node, /*leaf*/ 0, /*count*/ 1, /*armed*/ true);
    // swap to leaf 1: leaf 0's shared drain timer is CANCELLED (re-homed off the leaving leaf).
    DualLayerTestAccess::activate(node, 1);
    CHECK(hal.cancelled[DualLayerTestAccess::drain_timer_id()]);
    // swap BACK to leaf 0: its deferred-drain is RE-ARMED from preserved state — the parked DM isn't stranded.
    hal.armed[DualLayerTestAccess::drain_timer_id()] = false;     // reset the witness
    DualLayerTestAccess::activate(node, 0);
    CHECK(hal.armed[DualLayerTestAccess::drain_timer_id()]);      // re-armed for leaf 0 on enter
}

// ---- SLICE 3d: the gateway window scheduler (alternate the active leaf; busy-guard defers) -----------------
TEST_CASE("dual-layer scheduler: the window-switch loop alternates the active leaf; busy-guard defers (Slice 3d)") {
    StubHal hal; Node node(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 8);   // EQUAL SF -> 7500/7500 windows
    CHECK(node.on_init(cfg));
    const uint32_t WIN = node.config().layers[0].window_ms;
    CHECK(WIN == 7500);
    const uint32_t WID = DualLayerTestAccess::window_timer_id();
    // boot: active leaf 0; the first window-switch is armed after leaf-0's window.
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 0));
    CHECK(hal.armed[WID]);
    CHECK(hal.last_delay[WID] == WIN);                            // arms after window_ms[0]
    // fire the switch AT the grid boundary (now=7500) -> leaf 1, re-armed to the next boundary (leaf-1's window).
    hal._now = 7500; node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 1));
    CHECK(hal.last_delay[WID] == node.config().layers[1].window_ms);   // 15000-7500 = 7500
    // fire at the next boundary (now=15000) -> back to leaf 0 (the grid's anti-phase alternation).
    hal._now = 15000; node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 0));
    // BUSY-GUARD: at the next boundary (now=22500) mid-exchange (pending_tx) -> HOLD the leaf, re-arm busy-retry (slip).
    hal._now = 22500; DualLayerTestAccess::set_pending_tx(node, true);
    node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 0));   // held on leaf 0
    CHECK(hal.last_delay[WID] == protocol::gateway_layer_busy_retry_ms);                       // busy-retry, not a window
    // once the exchange clears, the next fire snaps to the GRID's current leaf (phase 8500 -> leaf 1).
    hal._now = 23500; DualLayerTestAccess::set_pending_tx(node, false);
    node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 1));
}

// Slice 3d GRID / absolute scheduling: a busy slip does NOT ratchet the phase — the next fire snaps back to the absolute
// grid boundary (the slipped leaf simply loses that much of its window), bounding drift to <= one window.
TEST_CASE("dual-layer scheduler: GRID scheduling — a busy slip snaps back to the absolute grid (no ratchet)") {
    StubHal hal; Node node(hal, /*id*/ 1, 0x1);                       // _now = 0 -> grid epoch 0
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 8);   // 7500/7500, period 15000
    CHECK(node.on_init(cfg));
    const uint32_t WID = DualLayerTestAccess::window_timer_id();
    // a busy slip across the leaf0->leaf1 boundary (now=7500): pending_tx holds the switch on leaf 0.
    hal._now = 7500; DualLayerTestAccess::set_pending_tx(node, true);
    node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 0));   // held
    CHECK(hal.last_delay[WID] == protocol::gateway_layer_busy_retry_ms);
    // busy-retry fires 1s later (now=8500), flight cleared -> snap to grid leaf 1, armed to the ABSOLUTE boundary 15000.
    hal._now = 8500; DualLayerTestAccess::set_pending_tx(node, false);
    node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 1));   // snapped to the grid leaf
    CHECK(hal.last_delay[WID] == 6500);                                // 15000-8500: leaf-1 window SHORTENED by the slip, NOT a fresh 7500 (no ratchet)
    // the grid boundary at 15000 is preserved -> leaf 0 gets its FULL window again (drift did not accumulate).
    hal._now = 15000; node.on_timer(WID);
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 0));
    CHECK(hal.last_delay[WID] == 7500);
}

TEST_CASE("dual-layer beacon: a gateway beacons each leaf on its own cadence at window-activation, NOT the shared timer (Slice 3d)") {
    StubHal hal; hal._now = 10000;                       // past the discovery beacon period (5000) so a leaf is due
    Node node(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(node.on_init(cfg));
    // the SHARED periodic beacon timer is DISABLED for a gateway (its single deadline halves the per-leaf cadence).
    CHECK_FALSE(hal.armed[DualLayerTestAccess::beacon_timer_id()]);
    // boot beaconed leaf 0 (due) at window-activation; leaf 1 not yet visited.
    CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 10000);
    CHECK(DualLayerTestAccess::last_beacon_ms(node, 1) == 0);
    // switch to leaf 1 -> beacon it (due).
    hal._now = 20000;
    node.on_timer(DualLayerTestAccess::window_timer_id());
    CHECK(DualLayerTestAccess::active_ptr(node) == DualLayerTestAccess::layer_ptr(node, 1));
    CHECK(DualLayerTestAccess::last_beacon_ms(node, 1) == 20000);
    // switch back to leaf 0 -> still due (>= cadence since 10000) -> re-beacon on ITS own schedule.
    hal._now = 25000;
    node.on_timer(DualLayerTestAccess::window_timer_id());
    CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 25000);
}

// ---- gateway noise control: steady-state is REACTIVE-ONLY (duty-cycle protection) ---------------------------
// The schedule lets neighbours compute ALL future windows from ONE hearing (gateway_schedule_defer_ms), so re-
// announcing a STATIC schedule on a timer is pure airtime waste -> kills the gateway's duty budget. Steady state:
// beacon ONLY on dirty state (real new info) or a REQ_SYNC pull; the sole unsolicited heartbeat is gated on duty
// headroom + a 3 h floor. Discovery is exempt (a new gateway / fresh two-layer link-up must be discoverable).
TEST_CASE("gateway noise: steady-state is REACTIVE-ONLY — a clean, recently-beaconed gateway does NOT re-announce") {
    StubHal hal; Node node(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(node.on_init(cfg));
    // 1 000 000 ms ≈ 16.7 min: PAST discovery (60 s) AND past the old beacon_period_ms (15 min) — the old periodic
    // clause WOULD fire here — but still << the 3 h announce floor, so reactive-only must stay silent.
    hal._now = 1000000;
    DualLayerTestAccess::set_last_beacon(node, 0, 0);               // route table clean
    DualLayerTestAccess::call_gw_beacon(node);
    CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 0);       // NO re-announce
}

TEST_CASE("gateway noise: a DIRTY route still pushes IMMEDIATELY (reactive), even in steady state") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(node.on_init(cfg));
    hal._now = 100000;
    DualLayerTestAccess::set_last_beacon(node, 0, 90000);
    DualLayerTestAccess::inject_dirty_route(node, 0, /*dest*/ 42);  // new route info -> must propagate now
    DualLayerTestAccess::call_gw_beacon(node);
    CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 100000);  // emitted immediately
}

TEST_CASE("gateway noise: the unsolicited heartbeat fires ONLY with duty headroom AND past the min interval") {
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    cfg.duty_cycle = 0.10;                          // 10% duty -> budget = floor(0.10 * 3.6e6) = 360000 ms/h
    cfg.gw_announce_duty_pct = 5;                   // announce only when used < 5% of budget = 18000 ms
    cfg.gw_announce_min_interval_ms = 10800000;     // 3 h

    // (1) past the 3 h floor + airtime under 5% of budget -> announce
    { StubHal hal; Node node(hal, 1, 0x1); CHECK(node.on_init(cfg));
      hal._airtime_used_ms = 1000;                  // 1 s << 18000
      hal._now = 11000000;                          // > 3 h; leaf0._last = 0 -> interval satisfied
      DualLayerTestAccess::set_last_beacon(node, 0, 0);
      DualLayerTestAccess::call_gw_beacon(node);
      CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 11000000); }      // announced

    // (2) past the floor but airtime OVER 5% of budget -> stay silent (let the bridging traffic carry liveness)
    { StubHal hal; Node node(hal, 1, 0x1); CHECK(node.on_init(cfg));
      hal._airtime_used_ms = 20000;                 // 20 s > 18000 -> no headroom
      hal._now = 11000000;
      DualLayerTestAccess::set_last_beacon(node, 0, 0);
      DualLayerTestAccess::call_gw_beacon(node);
      CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 0); }             // silent

    // (3) headroom OK but < 3 h since the last beacon -> stay silent
    { StubHal hal; Node node(hal, 1, 0x1); CHECK(node.on_init(cfg));
      hal._airtime_used_ms = 1000;
      hal._now = 11000000;
      DualLayerTestAccess::set_last_beacon(node, 0, 11000000 - 1000);        // beaconed 1 s ago
      DualLayerTestAccess::call_gw_beacon(node);
      CHECK(DualLayerTestAccess::last_beacon_ms(node, 0) == 11000000 - 1000); }  // too soon
}

// The `routes` console dump surfaces a gateway route's unique state via this accessor (period / per-leaf windows).
TEST_CASE("routes dump: rt_gateway_schedule returns a heard gateway's stored schedule, nullptr when unknown") {
    StubHal hal; hal._now = 5000; Node node(hal, /*id*/ 7, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(node.on_init(cfg));
    CHECK(node.rt_gateway_schedule(5) == nullptr);                            // never heard -> nullptr
    DualLayerTestAccess::store_gw_schedule_pair(node, /*gw*/ 5, /*leafA*/ 1, /*leafB*/ 2);
    const GatewaySchedule* gs = node.rt_gateway_schedule(5);
    CHECK(gs != nullptr);
    if (!gs) return;
    CHECK(gs->period_ms == 15000);
    CHECK(gs->n_rec == 2);
    CHECK(gs->rec[0].leaf_id == 1); CHECK(gs->rec[0].window_ms == 7500); CHECK(gs->rec[0].offset_ms == 0);
    CHECK(gs->rec[1].leaf_id == 2); CHECK(gs->rec[1].offset_ms == 7500);
}

// Per-layer frequency: a layer is a (freq, SF, leaf) channel; the gateway retunes the RX carrier on a window switch.
TEST_CASE("dual-layer freq: a gateway retunes the RX frequency on each window switch (per-layer channel)") {
    StubHal hal; Node node(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;  cfg.layers[0].freq_mhz = 868.1;
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].node_id = 12; cfg.layers[1].freq_mhz = 869.5;
    CHECK(node.on_init(cfg));                                              // boots on leaf 0 -> retunes to 868.1
    CHECK(hal.last_set_rx_freq == doctest::Approx(868.1));
    DualLayerTestAccess::activate(node, 1);                                // switch to leaf 1
    CHECK(hal.last_set_rx_freq == doctest::Approx(869.5));
    DualLayerTestAccess::activate(node, 0);                                // back to leaf 0
    CHECK(hal.last_set_rx_freq == doctest::Approx(868.1));
}

TEST_CASE("dual-layer freq: a layer with freq_mhz==0 does NOT retune (inherits the boot/global freq)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;  cfg.layers[0].freq_mhz = 868.1;
    cfg.layers[1] = good_layer(2, 9); cfg.layers[1].node_id = 12; cfg.layers[1].freq_mhz = 0.0;   // inherit
    CHECK(node.on_init(cfg));                                              // leaf 0 -> 868.1
    CHECK(hal.last_set_rx_freq == doctest::Approx(868.1));
    DualLayerTestAccess::activate(node, 1);                                // leaf 1 freq 0 -> NO retune
    CHECK(hal.last_set_rx_freq == doctest::Approx(868.1));                 // unchanged
}

TEST_CASE("dual-layer beacon: a gateway ADVERTISES its window schedule — one record/leaf, receiver-anchored countdown (Slice 3e)") {
    StubHal hal; hal._now = 10000;
    Node node(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;   // equal SF -> 7500/7500 windows, 15000 period
    CHECK(node.on_init(cfg));                                       // boot beacons leaf 0 -> hal.last_tx is that beacon
    auto b = parse_beacon(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(b.has_value());
    if (!b.has_value()) return;
    CHECK(b->self_gateway);
    CHECK(b->has_schedule);
    CHECK(b->schedule_count == 2);
    auto r0 = parse_beacon_schedule(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len), *b, 0);
    auto r1 = parse_beacon_schedule(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len), *b, 1);
    CHECK(r0.has_value());
    CHECK(r1.has_value());
    if (!r0.has_value() || !r1.has_value()) return;
    // leaf 0 is ACTIVE at boot -> its window re-opens in a full period -> countdown %period = 0 ("open now").
    CHECK(r0->layer_id == 1);
    CHECK(r0->routing_sf == 8);
    CHECK(r0->duration_100ms == 75);          // 7500ms / 100
    CHECK(r0->offset_100ms == 0);             // active leaf -> reachable now
    CHECK(r0->period_units == 15);            // 15000ms = 15s
    // leaf 1 opens when leaf 0's window closes (7500ms from the boot anchor) -> countdown 7500 -> 75.
    CHECK(r1->layer_id == 2);
    CHECK(r1->offset_100ms == 75);
}

TEST_CASE("dual-layer beacon: a node LEARNS a gateway's schedule + defers its RTS to the gateway's window (Slice 3e.2)") {
    // 1) a gateway emits a beacon on leaf 0 (at boot) — capture it off the wire.
    StubHal ghal; ghal._now = 10000;
    Node gw(ghal, /*id*/ 1, 0xABCDu);
    NodeConfig gcfg; gcfg.n_layers = 2;
    gcfg.layers[0] = good_layer(1, 8); gcfg.layers[0].node_id = 5;    // leaf-0 nibble 1, node_id 5
    gcfg.layers[1] = good_layer(2, 8); gcfg.layers[1].node_id = 12;   // leaf-1 nibble 2
    CHECK(gw.on_init(gcfg));
    CHECK(ghal.last_tx_len > 0);                                      // the boot beacon (carries the schedule)
    // 2) a NORMAL receiver on leaf 1 (= the gateway's active leaf-0 nibble) hears it -> learns gw 5's schedule.
    StubHal rhal; rhal._now = 50000;
    Node rx(rhal, /*id*/ 7, 0x7777u);
    NodeConfig rcfg; rcfg.routing_sf = 8; rcfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); rcfg.leaf_id = 1;
    CHECK(rx.on_init(rcfg));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = rhal._now; meta.src_hint = -1;
    rx.on_recv(ghal.last_tx, ghal.last_tx_len, meta);
    // 3) at the heard instant the gateway is on leaf 1 (our leaf) -> reachable now (defer 0).
    CHECK(DualLayerTestAccess::gw_defer(rx, /*gw_node_id*/ 5) == 0);
    // 4) advance 8000ms — the gateway has moved to the foreign leaf -> defer until our window comes around:
    //    our-leaf phase 8000 >= window 7500 -> defer = period(15000) - 8000 + guard. The booted gateway has <3
    //    neighbours -> advertises nibble 0 (SPARSE) -> guard = 100 + sparse_bonus(200) = 300 -> 15000-8000+300 = 7300.
    rhal._now = 58000;
    CHECK(DualLayerTestAccess::gw_defer(rx, 5) == 7300);
    // an UNKNOWN gateway -> no schedule -> send now.
    CHECK(DualLayerTestAccess::gw_defer(rx, 99) == 0);
}

// §3e herd-spread (Lua gateway_spread_nibble dv:1692): a gateway sizes a 0..15 spread hint from its 1-hop herd.
TEST_CASE("§3e herd-spread: gateway_spread_nibble sizes from the 1-hop herd; < gateway_herd_min advertises 0") {
    StubHal hal; Node gw(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;       // equal SF -> 7500/7500 windows
    CHECK(gw.on_init(cfg));
    CHECK(DualLayerTestAccess::direct_neighbors(gw) == 0);
    CHECK(DualLayerTestAccess::spread_nibble(gw) == 0);                 // empty herd -> 0
    DualLayerTestAccess::add_direct_neighbor(gw, 30);
    DualLayerTestAccess::add_direct_neighbor(gw, 31);
    CHECK(DualLayerTestAccess::spread_nibble(gw) == 0);                 // herd 2 < min 3 -> still 0
    DualLayerTestAccess::add_direct_neighbor(gw, 32);
    DualLayerTestAccess::add_direct_neighbor(gw, 33);
    DualLayerTestAccess::add_direct_neighbor(gw, 34);
    CHECK(DualLayerTestAccess::direct_neighbors(gw) == 5);
    // herd 5 >= min -> nonzero, and equals the formula over the COMPUTED exchange airtime × the slack factor (default 2):
    //   frac = min(herd·exchange·slack, window)/window ; nibble = round(frac·15).
    const uint32_t E = DualLayerTestAccess::exchange_airtime(gw);
    const uint8_t slack = 2;                                            // cfg default
    uint64_t frac_num = static_cast<uint64_t>(5) * E * slack; if (frac_num > 7500) frac_num = 7500;
    const uint8_t expected = static_cast<uint8_t>((frac_num * 15 + 7500 / 2) / 7500);
    CHECK(DualLayerTestAccess::spread_nibble(gw) > 0);
    CHECK(DualLayerTestAccess::spread_nibble(gw) == expected);
}

// §3e slack: the herd-spread unit is exchange_airtime × gw_herd_slack — the bare airtime under-spreads (collision-unsafe),
// so the slack restores the headroom. A larger slack -> a larger (or equal, if clamped) nibble.
TEST_CASE("§3e herd-spread: gw_herd_slack scales the advertised nibble (default 2; cfg-tunable)") {
    auto nib_for_slack = [](uint8_t slack) {
        StubHal hal; Node gw(hal, /*id*/ 1, 0x1);
        NodeConfig cfg; cfg.n_layers = 2; cfg.gw_herd_slack = slack;
        cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
        cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
        gw.on_init(cfg);
        for (uint8_t d = 30; d < 34; ++d) DualLayerTestAccess::add_direct_neighbor(gw, d);   // herd 4 (>= min 3)
        return DualLayerTestAccess::spread_nibble(gw);
    };
    const uint8_t n1 = nib_for_slack(1), n2 = nib_for_slack(2), n4 = nib_for_slack(4);
    CHECK(n1 > 0);                                                      // herd 4 with slack 1 still spreads a little
    CHECK(n2 >= n1);                                                    // more slack -> >= spread (monotonic up to the 15 clamp)
    CHECK(n4 >= n2);
    CHECK(n4 > n1);                                                     // 4x slack genuinely widens the spread
}

// §3e: the per-exchange airtime is COMPUTED (RTS+CTS+gap+DATA+ACK via airtime_ms), not the Lua's fixed 600ms — and it
// includes the CTS->DATA SF-retune gap. DATA len = the rolling mean of payloads passed (bootstrap until the first send).
TEST_CASE("§3e exchange airtime: RTS+CTS+cts_gap+DATA+ACK via airtime_ms; DATA len from the rolling payload mean") {
    StubHal hal; Node gw(hal, /*id*/ 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    // pre-sample: DATA len = the bootstrap assumption (64). Re-derive the composition from the airtime_ms PRIMITIVE.
    const uint32_t bw = cfg.radio_bw_hz; const uint8_t cr = cfg.radio_cr;
    const uint16_t pre = 8 + protocol::gateway_herd_assumed_payload_bytes;        // DATA_HDR_LEN(8) + 64
    const uint32_t expect_pre = meshroute::airtime_ms(8, bw, cr, protocol::preamble_sym, 8)   // RTS
                              + meshroute::airtime_ms(8, bw, cr, protocol::preamble_sym, 4)   // CTS
                              + protocol::cts_to_data_gap_ms                                  // the SF-retune gap
                              + meshroute::airtime_ms(8, bw, cr, protocol::preamble_sym, pre) // DATA @ max_data_sf (8)
                              + meshroute::airtime_ms(8, bw, cr, protocol::preamble_sym, 4);  // ACK
    CHECK(DualLayerTestAccess::exchange_airtime(gw) == expect_pre);
    CHECK(DualLayerTestAccess::exchange_airtime(gw) > protocol::cts_to_data_gap_ms);          // the gap is genuinely included
}

// §3e herd-spread apply (Lua gateway_schedule_defer_ms dv:5072): a sender deferring to a window adds capped jitter.
TEST_CASE("§3e herd-spread: a deferred send adds capped herd-jitter when the gateway advertises nibble>0") {
    StubHal hal; hal._now = 50000;
    Node rx(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(rx.on_init(cfg));
    DualLayerTestAccess::store_gw_sched_nibble(rx, /*gw*/ 5, /*leafA*/ 1, /*leafB*/ 2, /*nib*/ 15);   // heard at 50000
    hal._now = 58000;                                                   // our-leaf phase 8000 >= window 7500 -> defer
    // DENSE (nibble>0): guard = base 100 (no sparse bonus). base defer = 15000 - 8000 + 100 = 7100. best_window = 7500.
    // jmax = (15/15)*7500 = 7500; cap = min(7500 - 2*600, 60%*7500) = min(6300, 4500) = 4500.
    hal._rand_ret = 1000;                                               // jitter draw = 1000 (< jmax)
    CHECK(DualLayerTestAccess::gw_defer(rx, 5) == 7100 + 1000);
    hal._rand_ret = 999999;                                            // rand(0,jmax) half-open -> clamped to jmax-1 = 4499
    CHECK(DualLayerTestAccess::gw_defer(rx, 5) == 7100 + 4499);
    hal._rand_ret = -1;                                                // no forced rand -> draw 0 -> base only
    CHECK(DualLayerTestAccess::gw_defer(rx, 5) == 7100);
}

TEST_CASE("dual-layer beacon: the ACTIVE leaf advertises countdown 0 even MID-window (Slice 3e F-A)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;   // equal SF -> 7500/7500 windows, period 15000
    CHECK(gw.on_init(cfg));                               // active = leaf 0; anchors: leaf0 re-opens in a full period, leaf1 in 7500
    // advance 3000 ms INTO leaf-0's window (NO switch) — the active leaf is still open NOW.
    hal._now = 13000;
    hal.last_tx_len = 0;
    DualLayerTestAccess::emit(gw, "triggered");          // a mid-window (triggered-style) beacon
    auto b = parse_beacon(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(b.has_value());
    if (!b.has_value()) return;
    auto r0 = parse_beacon_schedule(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len), *b, 0);
    auto r1 = parse_beacon_schedule(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len), *b, 1);
    CHECK(r0.has_value());
    CHECK(r1.has_value());
    if (!r0.has_value() || !r1.has_value()) return;
    // F-A: leaf 0 is ACTIVE -> 0 ("open now"), NOT period-elapsed (15000-3000=12000 -> 120 units, the pre-fix wire value).
    CHECK(r0->layer_id == 1);
    CHECK(r0->offset_100ms == 0);
    // leaf 1 (foreign) opens at t=17500; at t=13000 -> 4500 ms away -> 45 units.
    CHECK(r1->layer_id == 2);
    CHECK(r1->offset_100ms == 45);
}

TEST_CASE("dual-layer origination: send_layer parks a cross-layer send + floods an H query (Slice 4d)") {
    StubHal hal; hal._now = 10000;
    Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    hal.last_tx_len = 0;
    Command c{}; c.kind = CmdKind::send_layer; c.u.layer.dst_hash = 0x9999u; c.u.layer.hop_count = 0;
    const char* body = "hi"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
    const CmdResult r = x.on_command(c);
    CHECK(r.code == CmdCode::queued);                         // accepted (parked, resolving)
    CHECK(DualLayerTestAccess::parked_count(x) == 1);
    CHECK(DualLayerTestAccess::parked_cross_layer(x, 0));     // a CROSS-LAYER park (resolves layer+gateway on the H-answer)
    CHECK(hal.last_tx_len > 0);                               // an H query went out
}

// R1 (review ship-blocker): cross-layer DM origination has NO CRYPTED in v1 (same-layer only). A node with
// e2e_dm ON must REFUSE send_layer loudly — NOT silently park/originate a CLEARTEXT cross-layer DATA (which is
// what enqueue_cross_layer does, ignoring _cfg.e2e_dm). Both sub-paths (park-first hop_count==0 + explicit-path
// hop_count>0) must return err_unsupported with nothing parked and no H flood.
TEST_CASE("R1 fail-loud: e2e_dm refuses cross-layer send_layer (v1 same-layer CRYPTED only — NEVER cleartext)") {
    StubHal hal; hal._now = 10000;
    Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    cfg.e2e_dm = true;                                         // E2E on: no cross-layer CRYPTED in v1 -> must REFUSE
    CHECK(x.on_init(cfg));
    hal.last_tx_len = 0;
    const char* body = "hi";
    // park-first sub-path (hop_count == 0): pre-fix would PARK + flood an H query (a silent cleartext-bound accept).
    Command c0{}; c0.kind = CmdKind::send_layer; c0.u.layer.dst_hash = 0x9999u; c0.u.layer.hop_count = 0;
    c0.body = reinterpret_cast<const uint8_t*>(body); c0.body_len = 2;
    const CmdResult r0 = x.on_command(c0);
    CHECK(r0.code == CmdCode::err_unsupported);                // REFUSED loudly
    CHECK(DualLayerTestAccess::parked_count(x) == 0);          // nothing parked (no cleartext drain later)
    CHECK(hal.last_tx_len == 0);                               // NO H flood
    // explicit-path sub-path (hop_count > 0): pre-fix would originate_layer_path -> a cleartext cross-layer DATA.
    Command c1{}; c1.kind = CmdKind::send_layer; c1.u.layer.dst_hash = 0x9999u; c1.u.layer.hop_count = 1; c1.u.layer.hops[0] = 2;
    c1.body = reinterpret_cast<const uint8_t*>(body); c1.body_len = 2;
    CHECK(x.on_command(c1).code == CmdCode::err_unsupported);  // REFUSED loudly (no cleartext cross-layer DATA enqueued)
}


TEST_CASE("dual-layer origination: send_cross_layer builds [my,target] cur=1 + SOURCE_HASH to a schedule-verified gateway (Slice 4d)") {
    // a gateway G beacons -> X learns G's schedule (serves leaves 1+2) AND a 1-hop route to G.
    StubHal ghal; ghal._now = 10000;
    Node gw(ghal, /*id*/ 1, 0xABCDu);
    NodeConfig gcfg; gcfg.n_layers = 2;
    gcfg.layers[0] = good_layer(1, 8); gcfg.layers[0].node_id = 5;
    gcfg.layers[1] = good_layer(2, 8); gcfg.layers[1].node_id = 12;
    CHECK(gw.on_init(gcfg)); CHECK(ghal.last_tx_len > 0);
    StubHal hal; hal._now = 50000;
    Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
    x.on_recv(ghal.last_tx, ghal.last_tx_len, meta);
    CHECK(x.rt_count() >= 1);                                 // a route to G installed from its beacon
    // advance so G is on its FOREIGN leaf -> the cross-layer DM DEFERS (held in the queue, inspectable).
    hal._now = 58000;
    const uint8_t body[3] = { 'h', 'i', '!' };
    DualLayerTestAccess::send_xl(x, /*dst_node*/ 20, /*dst_hash*/ 0x9999u, /*target_layer*/ 2, body, 3);
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 1);         // deferred to G's window -> held
    const TxItem& it = DualLayerTestAccess::leaf_tx_at(x, 0, 0);
    CHECK(it.dst == 5);                                       // MAC dst = the gateway G (node 5)
    CHECK((it.flags & DATA_FLAG_CROSS_LAYER) != 0);
    auto ui = parse_unicast_inner(std::span<const uint8_t>(it.inner, it.inner_len), it.flags);
    CHECK(ui.has_value());
    if (ui) { CHECK(ui->has_cross_layer); CHECK(ui->n_layers == 2); CHECK(ui->cur == 1);
              CHECK(ui->layer_ids[0] == 1); CHECK(ui->layer_ids[1] == 2);   // [our_layer, target_layer]
              CHECK(ui->dst_key_hash32 == 0x9999u);                          // the final recipient Y
              CHECK(ui->has_source_hash); CHECK(ui->source_hash == 0x7777u); // X's stable key (for the reversed ack)
              CHECK(ui->origin == 7); }                                      // X
    CHECK((it.flags & DATA_FLAG_E2E_ACK_REQ) == 0);                          // control: no E2E requested (flags default 0) -> the bit is NOT set
}

TEST_CASE("dual-layer origination: send_cross_layer HONORS the E2E_ACK_REQ flag so Y can ack via the reversed path (Slice 4d/e2e)") {
    // same setup as the send_cross_layer test: X learns a bridging gateway G (serves leaves 1+2) + a route to it.
    StubHal ghal; ghal._now = 10000;
    Node gw(ghal, /*id*/ 1, 0xABCDu);
    NodeConfig gcfg; gcfg.n_layers = 2;
    gcfg.layers[0] = good_layer(1, 8); gcfg.layers[0].node_id = 5;
    gcfg.layers[1] = good_layer(2, 8); gcfg.layers[1].node_id = 12;
    CHECK(gw.on_init(gcfg)); CHECK(ghal.last_tx_len > 0);
    StubHal hal; hal._now = 50000;
    Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
    x.on_recv(ghal.last_tx, ghal.last_tx_len, meta);
    hal._now = 58000;                                        // G on its foreign leaf -> the DM defers (held, inspectable)
    const uint8_t body[2] = { 'h', 'i' };
    // the app requests an E2E ack -> the cross-layer DM MUST carry E2E_ACK_REQ (so Y builds the 4e reversed-path ack; not best-effort).
    DualLayerTestAccess::send_xl(x, /*dst_node*/ 20, /*dst_hash*/ 0x9999u, /*target_layer*/ 2, body, 2, DATA_FLAG_E2E_ACK_REQ);
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 1);
    const TxItem& it = DualLayerTestAccess::leaf_tx_at(x, 0, 0);
    CHECK((it.flags & DATA_FLAG_CROSS_LAYER) != 0);
    CHECK((it.flags & DATA_FLAG_E2E_ACK_REQ) != 0);          // THE FIX: the app's E2E request is threaded onto the cross-layer DM
}

// ---- explicit-path send_layer origination (console/companion, §5) ---------------------------------------
// Shared setup: a gateway G beacons (serves leaves 1+2) -> X (on leaf 1) learns G's schedule + a route. Then X
// originates along a USER-supplied layer path (no H-query); the path is prepended with X's own layer (cur=1).
namespace {
Node* make_x_learning_gw(StubHal& ghal, Node& gw, StubHal& hal, Node& x) {
    ghal._now = 10000;
    NodeConfig gcfg; gcfg.n_layers = 2;
    gcfg.layers[0] = good_layer(1, 8); gcfg.layers[0].node_id = 5;
    gcfg.layers[1] = good_layer(2, 8); gcfg.layers[1].node_id = 12;
    CHECK(gw.on_init(gcfg)); CHECK(ghal.last_tx_len > 0);
    hal._now = 50000;
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
    x.on_recv(ghal.last_tx, ghal.last_tx_len, meta);
    CHECK(x.rt_count() >= 1);
    hal._now = 58000;                                        // G on its FOREIGN leaf -> the DM defers (held, inspectable)
    return &x;
}
}  // namespace

TEST_CASE("send_layer: a single-hop explicit path builds [my,target] cur=1 to the gateway for that leaf") {
    StubHal ghal; Node gw(ghal, /*id*/ 1, 0xABCDu);
    StubHal hal;  Node x(hal, /*id*/ 7, 0x7777u);
    make_x_learning_gw(ghal, gw, hal, x);
    const uint8_t body[2] = { 'h', 'i' };
    const uint8_t hops[1] = { 2 };                           // user supplies destination layer 2; X prepends its own (1)
    DualLayerTestAccess::originate(x, /*dst_hash*/ 0x9999u, hops, /*hop_count*/ 1, body, 2, DATA_FLAG_E2E_ACK_REQ);
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 1);
    const TxItem& it = DualLayerTestAccess::leaf_tx_at(x, 0, 0);
    CHECK(it.dst == 5);                                       // routed to G (serves leaf 2)
    CHECK((it.flags & DATA_FLAG_CROSS_LAYER) != 0);
    CHECK((it.flags & DATA_FLAG_E2E_ACK_REQ) != 0);          // flags threaded through
    auto ui = parse_unicast_inner(std::span<const uint8_t>(it.inner, it.inner_len), it.flags);
    CHECK(ui.has_value());
    if (ui) { CHECK(ui->has_cross_layer); CHECK(ui->n_layers == 2); CHECK(ui->cur == 1);
              CHECK(ui->layer_ids[0] == 1); CHECK(ui->layer_ids[1] == 2);   // [our_layer, hops[0]]
              CHECK(ui->dst_key_hash32 == 0x9999u);
              CHECK(ui->has_source_hash); CHECK(ui->source_hash == 0x7777u); CHECK(ui->origin == 7); }
}

TEST_CASE("send_layer: a two-hop explicit path builds [my,h0,h1] n_layers=3 cur=1 to the gateway for h0's leaf") {
    StubHal ghal; Node gw(ghal, /*id*/ 1, 0xABCDu);
    StubHal hal;  Node x(hal, /*id*/ 7, 0x7777u);
    make_x_learning_gw(ghal, gw, hal, x);
    const uint8_t body[2] = { 'h', 'i' };
    const uint8_t hops[2] = { 2, 3 };                        // path [my=1, 2, 3]; routed to the gateway serving hops[0]=leaf 2
    DualLayerTestAccess::originate(x, /*dst_hash*/ 0x9999u, hops, /*hop_count*/ 2, body, 2);
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 1);
    const TxItem& it = DualLayerTestAccess::leaf_tx_at(x, 0, 0);
    CHECK(it.dst == 5);                                       // first hop routes to G (serves leaf 2)
    auto ui = parse_unicast_inner(std::span<const uint8_t>(it.inner, it.inner_len), it.flags);
    CHECK(ui.has_value());
    if (ui) { CHECK(ui->n_layers == 3); CHECK(ui->cur == 1);
              CHECK(ui->layer_ids[0] == 1); CHECK(ui->layer_ids[1] == 2); CHECK(ui->layer_ids[2] == 3); }
}

TEST_CASE("send_layer: on_command REFUSES hops[0] == our own layer (self-layer misconfig, fail loud)") {
    StubHal ghal; Node gw(ghal, /*id*/ 1, 0xABCDu);
    StubHal hal;  Node x(hal, /*id*/ 7, 0x7777u);
    make_x_learning_gw(ghal, gw, hal, x);
    Command c{}; c.kind = CmdKind::send_layer; c.u.layer.dst_hash = 0x9999u;
    c.u.layer.hop_count = 1; c.u.layer.hops[0] = 1;          // 1 == X's own layer
    const char* body = "hi"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
    const CmdResult r = x.on_command(c);
    CHECK(r.code == CmdCode::err_unsupported);               // refused (no silent fix)
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 0);        // nothing enqueued
}

TEST_CASE("send_layer: no gateway serves hops[0]'s leaf -> SYNCHRONOUS err_no_gateway, nothing enqueued, NO orphan push") {
    StubHal ghal; Node gw(ghal, /*id*/ 1, 0xABCDu);
    StubHal hal;  Node x(hal, /*id*/ 7, 0x7777u);
    make_x_learning_gw(ghal, gw, hal, x);                    // G serves leaves 1+2 only
    const uint8_t body[2] = { 'h', 'i' };
    const uint8_t hops[1] = { 9 };                           // leaf 9: NO known gateway serves it
    const CmdCode code = DualLayerTestAccess::originate(x, /*dst_hash*/ 0x9999u, hops, /*hop_count*/ 1, body, 2);
    CHECK(code == CmdCode::err_no_gateway);                  // the failure is returned SYNCHRONOUSLY (the app holds the handle)
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 0);        // nothing enqueued
    Push p{};
    CHECK_FALSE(x.next_push(p));                             // and NO orphan push (was the dst=0/ctr=0 uncorrelatable push)
}

TEST_CASE("send_layer: on_command returns the full send-handle (ctr + dst_hash + packed layer_path)") {
    StubHal ghal; Node gw(ghal, /*id*/ 1, 0xABCDu);
    StubHal hal;  Node x(hal, /*id*/ 7, 0x7777u);
    make_x_learning_gw(ghal, gw, hal, x);                    // G serves leaves 1+2 -> hops[0]=2 routes
    Command c{}; c.kind = CmdKind::send_layer; c.u.layer.dst_hash = 0x9999u;
    c.u.layer.hop_count = 2; c.u.layer.hops[0] = 2; c.u.layer.hops[1] = 3;   // path [2,3] -> (2<<8)|3 = 0x0203
    const char* body = "hi"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
    const CmdResult r = x.on_command(c);
    CHECK(r.code == CmdCode::queued);
    CHECK(r.ctr != 0);                                       // a real correlation token (NOT 0)
    CHECK(r.dst_hash == 0x9999u);                            // echoes the target key
    CHECK(r.layer_path == 0x0203u);                          // hops packed MSB-first (hops[0] high byte)
}

TEST_CASE("send handle: sendhash echoes dst_hash (layer_path 0); plain send echoes neither") {
    StubHal hal; Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    DualLayerTestAccess::learn_neighbor(x, 30);             // a route so do_send succeeds
    const char* body = "hi";
    { Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 30; c.u.send.dst_hash = 0;
      c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
      const CmdResult r = x.on_command(c);
      CHECK(r.code == CmdCode::queued); CHECK(r.dst_hash == 0); CHECK(r.layer_path == 0); }   // id-addressed
    { Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 0; c.u.send.dst_hash = 0xDEADBEEFu;
      c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
      const CmdResult r = x.on_command(c);
      CHECK(r.dst_hash == 0xDEADBEEFu); CHECK(r.layer_path == 0); }                            // hash-addressed
}

TEST_CASE("dual-layer ack: Y builds a REVERSED-path cross-layer E2E ack back to the original sender (Slice 4e)") {
    StubHal hal; hal._now = 50000;
    Node y(hal, /*id*/ 30, 0x9999u);                         // Y, the recipient on layer B (leaf 2)
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 2;
    CHECK(y.on_init(cfg));
    DualLayerTestAccess::store_gw_schedule_pair(y, /*gw*/ 5, /*leafA*/ 1, /*leafB*/ 2);   // G bridges A(1)<->B(2)
    DualLayerTestAccess::learn_neighbor(y, 5);               // route to G
    // the inbound cross-layer DM from X@A: layer_ids=[1,2], SOURCE_HASH=X(0x7777), DST_HASH=Y(0x9999), origin X(7).
    data_unicast_inner dm{};
    dm.origin = 7; dm.has_cross_layer = true; dm.n_layers = 2; dm.cur = 1; dm.layer_ids[0] = 1; dm.layer_ids[1] = 2;
    dm.has_source_hash = true; dm.source_hash = 0x7777u;
    dm.has_dst_hash = true; dm.dst_key_hash32 = 0x9999u;
    DualLayerTestAccess::send_xl_ack(y, dm, /*acked_ctr*/ 42);
    // the ack DEFERS to G's window (Y's leaf opens later) -> held in the queue, inspectable.
    CHECK(DualLayerTestAccess::leaf_tx_n(y, 0) == 1);
    const TxItem& it = DualLayerTestAccess::leaf_tx_at(y, 0, 0);
    CHECK(it.dst == 5);                                       // MAC dst = the reverse gateway
    CHECK(it.type == DATA_TYPE_E2E_ACK);
    CHECK((it.flags & DATA_FLAG_CROSS_LAYER) != 0);
    auto ui = parse_unicast_inner(std::span<const uint8_t>(it.inner, it.inner_len), it.flags);
    CHECK(ui.has_value());
    if (ui) { CHECK(ui->has_cross_layer); CHECK(ui->n_layers == 2); CHECK(ui->cur == 1);
              CHECK(ui->layer_ids[0] == 2); CHECK(ui->layer_ids[1] == 1);     // REVERSED [B,A]
              CHECK(ui->dst_key_hash32 == 0x7777u);                            // addressed BACK to X
              CHECK(ui->has_source_hash); CHECK(ui->source_hash == 0x9999u);   // from Y
              CHECK(ui->origin == 30);                                         // Y
              CHECK(ui->body.size() == 2);
              CHECK((static_cast<uint16_t>(ui->body[0]) | (static_cast<uint16_t>(ui->body[1]) << 8)) == 42); }   // the acked ctr
}

TEST_CASE("dual-layer ack: a cross-layer DM WITHOUT SOURCE_HASH -> NO ack (never ack the local-leaf origin) (Slice 4e)") {
    StubHal hal; hal._now = 50000;
    Node y(hal, /*id*/ 30, 0x9999u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 2;
    CHECK(y.on_init(cfg));
    DualLayerTestAccess::store_gw_schedule_pair(y, 5, 1, 2);
    DualLayerTestAccess::learn_neighbor(y, 5);
    data_unicast_inner dm{};
    dm.origin = 7; dm.has_cross_layer = true; dm.n_layers = 2; dm.cur = 1; dm.layer_ids[0] = 1; dm.layer_ids[1] = 2;
    dm.has_source_hash = false;                              // NO stable sender key -> can't address the ack
    DualLayerTestAccess::send_xl_ack(y, dm, 42);
    CHECK(DualLayerTestAccess::leaf_tx_n(y, 0) == 0);        // FAIL LOUD: no ack built (not a wrong-node ack)
}

// R1 gap (adversarial review of the R1 fix): send_e2e_ack_cross_layer is a PARALLEL hand-built cross-layer
// origination that does NOT funnel through enqueue_cross_layer's e2e_dm choke. An e2e_dm node that receives a
// cleartext cross-layer DM with E2E_ACK_REQ would emit a CLEARTEXT cross-layer ack (leaking the acked_ctr),
// breaching the R1 "no cleartext cross-layer frame while e2e_dm" invariant. With e2e_dm on it must REFUSE.
TEST_CASE("R1 gap: e2e_dm refuses the cross-layer E2E ack (no cleartext cross-layer frame on air)") {
    StubHal hal; hal._now = 50000;
    Node y(hal, /*id*/ 30, 0x9999u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 2;
    cfg.e2e_dm = true;                                       // E2E on: v1 has no cross-layer CRYPTED -> never emit a cleartext xl ack
    CHECK(y.on_init(cfg));
    DualLayerTestAccess::store_gw_schedule_pair(y, /*gw*/ 5, /*leafA*/ 1, /*leafB*/ 2);   // a reverse gateway + route EXIST,
    DualLayerTestAccess::learn_neighbor(y, 5);              // so ONLY e2e_dm can block the ack (rules out no-gateway).
    data_unicast_inner dm{};
    dm.origin = 7; dm.has_cross_layer = true; dm.n_layers = 2; dm.cur = 1; dm.layer_ids[0] = 1; dm.layer_ids[1] = 2;
    dm.has_source_hash = true; dm.source_hash = 0x7777u;
    dm.has_dst_hash = true; dm.dst_key_hash32 = 0x9999u;
    DualLayerTestAccess::send_xl_ack(y, dm, /*acked_ctr*/ 42);
    CHECK(DualLayerTestAccess::leaf_tx_n(y, 0) == 0);       // REFUSED loudly: no cleartext cross-layer ack on the air
}

TEST_CASE("dual-layer origination: a routeless bridging gateway -> PARK + reactive RREQ, not give-up (Slice 4d.2)") {
    StubHal hal; hal._now = 50000;
    Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    // a gateway G(5) serving leaf 2 is KNOWN (we hold its schedule) but there is NO route to it.
    DualLayerTestAccess::store_gw_schedule(x, /*gw*/ 5, /*leaf_served*/ 2);
    CHECK(x.rt_count() == 0);
    hal.last_tx_len = 0;
    const uint8_t body[2] = { 'h', 'i' };
    DualLayerTestAccess::send_xl(x, /*dst_node*/ 20, /*dst_hash*/ 0x9999u, /*target_layer*/ 2, body, 2);
    // NOT a give-up: the DM is PARKED for the route (deferred) + a reactive RREQ for G went out.
    CHECK_FALSE(x.has_pending_tx());                          // no route -> not in-flight
    CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 0);         // left the tx_queue (moved to _deferred)
    CHECK(DualLayerTestAccess::deferred_count(x) == 1);       // parked in _deferred (the park half)
    CHECK(hal.last_tx_len > 0);                               // an RREQ (F frame) went out (the reactive half)
    auto f = parse_f(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(f.has_value());
    if (f) { CHECK_FALSE(f->is_reply); CHECK(f->dst_id == 5); }   // RREQ for the gateway G(5)
}

TEST_CASE("dual-layer origination: the H-answer drains a parked send_layer into a CROSS_LAYER DM; same-layer short-circuits (Slice 4d)") {
    // G beacons -> X learns G's schedule (serves leaves 1+2) + a route to G.
    StubHal ghal; ghal._now = 10000;
    Node gw(ghal, /*id*/ 1, 0xABCDu);
    NodeConfig gcfg; gcfg.n_layers = 2;
    gcfg.layers[0] = good_layer(1, 8); gcfg.layers[0].node_id = 5;
    gcfg.layers[1] = good_layer(2, 8); gcfg.layers[1].node_id = 12;
    CHECK(gw.on_init(gcfg)); CHECK(ghal.last_tx_len > 0);
    StubHal hal; hal._now = 50000;
    Node x(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(x.on_init(cfg));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
    x.on_recv(ghal.last_tx, ghal.last_tx_len, meta);
    hal._now = 58000;   // G on its foreign leaf -> the cross-layer DM defers (held, inspectable)

    // (A) cross-layer: send_layer to Y(0x9999), the H-answer says target_layer=2 (!= our leaf 1) -> CROSS_LAYER DM.
    { Command c{}; c.kind = CmdKind::send_layer; c.u.layer.dst_hash = 0x9999u;
      const char* b = "hi"; c.body = reinterpret_cast<const uint8_t*>(b); c.body_len = 2;
      CHECK(x.on_command(c).code == CmdCode::queued);
      DualLayerTestAccess::drain_parked(x, /*key*/ 0x9999u, /*resolved Y*/ 20, /*target_layer*/ 2);   // the H-answer
      CHECK(DualLayerTestAccess::parked_count(x) == 0);                      // consumed
      CHECK(DualLayerTestAccess::leaf_tx_n(x, 0) == 1);
      const TxItem& it = DualLayerTestAccess::leaf_tx_at(x, 0, 0);
      CHECK((it.flags & DATA_FLAG_CROSS_LAYER) != 0); CHECK(it.dst == 5); }   // -> bridged via G

    // (B) §5.1 short-circuit: a send_layer whose H-answer says target_layer == OUR leaf (1) -> a PLAIN same-layer DM.
    { DualLayerTestAccess::learn_neighbor(x, 30);            // route to node 30 so the same-layer DM flies (in-flight)
      Command c{}; c.kind = CmdKind::send_layer; c.u.layer.dst_hash = 0x4444u;
      const char* b = "yo"; c.body = reinterpret_cast<const uint8_t*>(b); c.body_len = 2;
      CHECK(x.on_command(c).code == CmdCode::queued);
      DualLayerTestAccess::drain_parked(x, /*key*/ 0x4444u, /*resolved*/ 30, /*target_layer*/ 1);   // same leaf -> do_send (NOT cross-layer)
      CHECK(x.has_pending_tx());                             // the plain DM went straight in-flight (route to 30, no gateway defer)
      CHECK(DualLayerTestAccess::pending_dst(x) == 30);      // addressed DIRECTLY to node 30, NOT routed via a gateway
      CHECK((DualLayerTestAccess::pending_flags(x) & DATA_FLAG_CROSS_LAYER) == 0); }   // PLAIN, not cross-layer
}

TEST_CASE("dual-layer bridge: a gateway re-injects a cross-layer DM onto the far leaf + seeds loop suppression (Slice 4c.1)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;    // leaf 0: layer_id 1, node_id 5 (G is active HERE at boot)
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;   // leaf 1: layer_id 2, node_id 12
    CHECK(gw.on_init(cfg));
    // bind the recipient Y (key 0x9999 -> node 20) on the FAR leaf (leaf 1) so the cross-leaf resolve succeeds.
    DualLayerTestAccess::bind_on_leaf(gw, /*leaf*/ 1, /*node*/ 20, /*key*/ 0x9999u);

    // a cross-layer DM inner: dst_hash=Y(0x9999) (NOT the gateway's 0xABCD), layer_ids=[1,2] cur=1, origin=X(7), body.
    const uint8_t ids[2] = { 1, 2 };
    const uint8_t body[3] = { 'h', 'i', '!' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t inner_len = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, /*origin*/ 7, 0, body, 3, 0, 0);
    CHECK(inner_len > 0);

    // bridge it (addressed to G's leaf-0 node_id 5 => deliver branch => CROSS_LAYER fork => transit => bridge).
    DualLayerTestAccess::bridge_from(gw, /*origin*/ 7, /*dst*/ 5, /*ctr*/ 42, flags, inner, static_cast<uint8_t>(inner_len));

    // a handoff is buffered for the FAR leaf, recipient resolved, inner preserved.
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);
    const XlHandoff* h = DualLayerTestAccess::handoff_first(gw);
    CHECK(h != nullptr);
    if (h) { CHECK(h->target_leaf == 1); CHECK(h->dst_node_id == 20); CHECK(h->origin == 7); CHECK(h->ctr == 42);
             CHECK((h->flags & DATA_FLAG_CROSS_LAYER) != 0); CHECK(h->inner_len == inner_len); }
    // drain on the far leaf's activation -> the re-inject lands on leaf 1's tx_queue as a fresh-budget gw_relay leg.
    DualLayerTestAccess::set_active(gw, 1);
    DualLayerTestAccess::drain(gw, 1);
    CHECK(DualLayerTestAccess::handoff_count(gw) == 0);            // consumed
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 1) == 1);
    const TxItem& it = DualLayerTestAccess::leaf_tx_at(gw, 1, 0);
    CHECK(it.origin == 7); CHECK(it.dst == 20); CHECK(it.ctr == 42);
    CHECK(it.is_gw_relay); CHECK(it.is_forward); CHECK(it.inner_len == inner_len);   // identity preserved, relay-marked
    // LOOP SUPPRESSION (seeded AT DRAIN, 4f): the far leaf's _seen_origins now holds (origin 7, dst 20, ctr 42).
    const uint32_t sokey = (static_cast<uint32_t>(7) << 24) | (static_cast<uint32_t>(20) << 16) | 42u;
    CHECK(DualLayerTestAccess::seen(gw, 1).count(sokey) == 1);
}

// L13 (2026-07-04): a SINGLE-layer node (n_layers<2) must NEVER bridge — only a dual-layer gateway does. A
// crafted CROSS_LAYER DM whose target layer_id == the single node's OWN leaf layer_id would (pre-fix) match the
// leaf-scan loop (target_leaf=0), fill the cap-1 _xl_handoffs slot AND induce an H-flood for ~60 s (a cheap DoS
// on a node that has no business bridging). bridge_cross_layer now refuses at the TOP when _n_layers<2. Assert
// the single-layer node enqueues NO handoff for the exact frame that a dual-layer gateway WOULD bridge.
TEST_CASE("L13 — a SINGLE-layer node refuses a crafted cross-layer handoff (no _xl_handoffs slot, no H-flood)") {
    // The crafted CROSS_LAYER inner: dst_hash != our key, layer_ids=[1] cur=0 -> target layer_id 1. On the
    // single-layer node below, layer 1 IS its own leaf -> the leaf-scan loop WOULD match (target_leaf=0) pre-fix.
    const uint8_t ids[1] = { 1 };
    const uint8_t body[2] = { 'h', 'i' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, /*dst_hash*/ 0x9999u,
                                         ids, /*n_layers*/ 1, /*cur*/ 0, /*origin*/ 7, 0, body, 2, 0, 0);
    CHECK(il > 0);

    // --- the single-layer node (n_layers defaults to 1): MUST refuse ---
    { StubHal hal; hal._now = 10000;
      Node leaf(hal, /*id*/ 5, 0xABCDu);
      NodeConfig cfg;                                       // n_layers left at 1 (a normal node)
      cfg.routing_sf = 8; cfg.leaf_id = 1; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
      CHECK(leaf.on_init(cfg));
      DualLayerTestAccess::bind_on_leaf(leaf, /*leaf*/ 0, /*node*/ 20, /*key*/ 0x9999u);   // even with the binding resolvable...
      DualLayerTestAccess::bridge_from(leaf, /*origin*/ 7, /*dst*/ 5, /*ctr*/ 42, flags, inner, static_cast<uint8_t>(il));
      CHECK(DualLayerTestAccess::handoff_count(leaf) == 0);   // ★ REFUSED at the top (n_layers<2) — no slot filled, no H-flood
    }

    // --- CONTROL: a dual-layer gateway (n_layers==2) with the SAME crafted frame DOES bridge (count 1) ---
    // Proves the frame is otherwise valid + would fill the slot; only the single-layer guard makes the difference.
    { StubHal hal; hal._now = 10000;
      Node gw(hal, /*id*/ 1, 0xBEEFu);
      NodeConfig cfg; cfg.n_layers = 2;
      cfg.layers[0] = good_layer(/*layer_id*/ 1, 8); cfg.layers[0].node_id = 5;   // leaf 0 carries layer_id 1 (the target)
      cfg.layers[1] = good_layer(/*layer_id*/ 2, 8); cfg.layers[1].node_id = 12;
      CHECK(gw.on_init(cfg));
      DualLayerTestAccess::bind_on_leaf(gw, /*leaf*/ 0, /*node*/ 20, /*key*/ 0x9999u);
      DualLayerTestAccess::bridge_from(gw, /*origin*/ 7, /*dst*/ 5, /*ctr*/ 42, flags, inner, static_cast<uint8_t>(il));
      CHECK(DualLayerTestAccess::handoff_count(gw) == 1);   // the dual-layer gateway DOES bridge the same frame
    }
}

TEST_CASE("dual-layer bridge: the re-injected relay RTS carries RTS_FLAG_RELAY end-to-end (Slice 4c.2)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    DualLayerTestAccess::bind_on_leaf(gw, /*leaf*/ 1, /*node*/ 20, /*key*/ 0x9999u);
    // bridge a cross-layer DM (addressed to G) -> a handoff for leaf 1.
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[2] = { 'h', 'i' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, /*origin*/ 7, 0, body, 2, 0, 0);
    DualLayerTestAccess::bridge_from(gw, /*origin*/ 7, /*dst*/ 5, /*ctr*/ 42, flags, inner, static_cast<uint8_t>(il));
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);
    // far leaf: install a 1-hop route to Y(20), drain the handoff, then service the queue -> the relay RTS fires.
    DualLayerTestAccess::set_active(gw, 1);
    DualLayerTestAccess::learn_neighbor(gw, 20);
    DualLayerTestAccess::drain(gw, 1);
    hal.last_tx_len = 0;
    DualLayerTestAccess::pump(gw);                         // become_free -> issue_send (threads is_gw_relay) -> tx_rts_retry
    CHECK(hal.last_tx_len > 0);
    auto r = parse_rts(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(r.has_value());
    if (r) { CHECK((r->rts_flags & RTS_FLAG_RELAY) != 0);   // the relay marker is ON the wire -> the receiver exempts it from anti-spam
             CHECK(r->next == 20); }                        // routed to Y on the far leaf
}

TEST_CASE("dual-layer bridge: the DELIVER-branch fork routes a cross-layer TRANSIT DM to bridge, not inbox (Slice 4c.1 keystone)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    DualLayerTestAccess::bind_on_leaf(gw, /*leaf*/ 1, /*node*/ 20, /*key*/ 0x9999u);
    // a CROSS_LAYER DATA addressed to G's leaf-0 node_id (5) -> deliver branch (d.dst==self). dst_hash=Y(0x9999)!=G's
    // key -> the fork must TRANSIT it (bridge), not deliver to the gateway's inbox.
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[2] = { 'h', 'i' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, /*origin*/ 7, 0, body, 2, 0, 0);
    uint8_t frame[96]; const uint8_t mac4[4] = { 0, 0, 0, 0 };
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 5; din.dst = 5; din.hops_remaining = 31; din.ctr = 42;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(mac4, 4);
    const size_t fl = pack_data(din, std::span<uint8_t>(frame, sizeof frame));
    CHECK(fl > 0);
    // simulate the prior RTS/CTS (handle_data requires a matching _pending_rx; ctr 42 -> ctr_lo4 = 0x0A).
    DualLayerTestAccess::set_pending_rx(gw, /*from*/ 7, /*ctr_lo*/ 0x0A, /*sf*/ 8, /*payload_len*/ static_cast<uint8_t>(il + 4));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
    gw.on_recv(frame, fl, meta);                 // handle_data -> ACK + arm the post-ack
    gw.on_timer(9 /*kPostAckTimerId*/);          // do_post_ack -> the deliver-branch CROSS_LAYER fork -> bridge
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);   // BRIDGED (the fork fired), NOT delivered to the gateway inbox
    const XlHandoff* h = DualLayerTestAccess::handoff_first(gw);
    CHECK(h != nullptr); if (h) { CHECK(h->target_leaf == 1); CHECK(h->dst_node_id == 20); CHECK(h->origin == 7); }
}

TEST_CASE("dual-layer bridge: an UNKNOWN far-leaf binding DEFERS the handoff -> H-flood -> resolves on the binding (Slice 4f)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    // bridge a cross-layer DM to Y (key 0x9999) on leaf 2 -- but Y is NOT bound on leaf 2 yet.
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[2] = { 'h', 'i' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, /*origin*/ 7, 0, body, 2, 0, 0);
    DualLayerTestAccess::bridge_from(gw, /*origin*/ 7, /*dst*/ 5, /*ctr*/ 42, flags, inner, static_cast<uint8_t>(il));
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);            // deferred (unresolved binding)

    // drain on leaf 2 with the binding STILL unknown -> floods an H query + KEEPS the handoff (not re-injected).
    DualLayerTestAccess::set_active(gw, 1);
    hal.last_tx_len = 0;
    DualLayerTestAccess::drain(gw, 1);
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);            // still deferred
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 1) == 0);            // NOT re-injected
    CHECK(hal.last_tx_len > 0);                                   // an H query for the binding went out

    // the binding ARRIVES (Y(0x9999) -> node 20 on leaf 2) -> the next drain resolves + re-injects.
    DualLayerTestAccess::bind_on_leaf(gw, /*leaf*/ 1, /*node*/ 20, /*key*/ 0x9999u);
    DualLayerTestAccess::drain(gw, 1);
    CHECK(DualLayerTestAccess::handoff_count(gw) == 0);            // consumed
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 1) == 1);            // re-injected
    CHECK(DualLayerTestAccess::leaf_tx_at(gw, 1, 0).dst == 20);
    const uint32_t sokey = (static_cast<uint32_t>(7) << 24) | (static_cast<uint32_t>(20) << 16) | 42u;
    CHECK(DualLayerTestAccess::seen(gw, 1).count(sokey) == 1);    // loop-seeded at the (deferred) drain
}

TEST_CASE("dual-layer bridge: an unknown far-leaf binding aged past the TTL gives up LOUD (Slice 4f)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[1] = { 'x' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0xBEEFu, ids, 2, 1, 7, 0, body, 1, 0, 0);
    DualLayerTestAccess::bridge_from(gw, 7, 5, 77, flags, inner, static_cast<uint8_t>(il));
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);            // deferred
    // advance past the defer TTL -> the next drain GIVES UP (dropped, never re-injected).
    hal._now += meshroute::protocol::gateway_handoff_defer_ttl_ms + 1;
    DualLayerTestAccess::set_active(gw, 1);
    DualLayerTestAccess::drain(gw, 1);
    CHECK(DualLayerTestAccess::handoff_count(gw) == 0);            // dropped (TTL giveup)
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 1) == 0);            // NOT re-injected
}

TEST_CASE("dual-layer bridge: a resolved handoff on a FULL queue is KEPT for retry, not dropped (Slice 4f / review HIGH #1)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    DualLayerTestAccess::bind_on_leaf(gw, /*leaf*/ 1, /*node*/ 20, /*key*/ 0x9999u);   // resolved at bridge
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[2] = { 'h', 'i' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, 7, 0, body, 2, 0, 0);
    DualLayerTestAccess::bridge_from(gw, 7, 5, 42, flags, inner, static_cast<uint8_t>(il));
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);
    // drain with the far leaf's tx_queue FULL -> the resolved handoff must be KEPT (retry), NOT dropped.
    DualLayerTestAccess::set_active(gw, 1);
    DualLayerTestAccess::fill_tx_queue(gw, 1);
    DualLayerTestAccess::drain(gw, 1);
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);            // KEPT (the fix); a pre-fix drop would be 0
    // the queue clears -> the next drain re-injects it (no loss).
    DualLayerTestAccess::clear_tx_queue(gw, 1);
    DualLayerTestAccess::drain(gw, 1);
    CHECK(DualLayerTestAccess::handoff_count(gw) == 0);            // now consumed
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 1) == 1);
    CHECK(DualLayerTestAccess::leaf_tx_at(gw, 1, 0).dst == 20);
}

TEST_CASE("dual-layer bridge: the H-answer re-drain HOOK resolves a deferred handoff immediately (Slice 4f)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    // bridge to an UNKNOWN binding (0x9999) on leaf 2 -> a deferred handoff.
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[2] = { 'h', 'i' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, 7, 0, body, 2, 0, 0);
    DualLayerTestAccess::bridge_from(gw, 7, 5, 42, flags, inner, static_cast<uint8_t>(il));
    CHECK(DualLayerTestAccess::handoff_count(gw) == 1);            // deferred (unresolved)

    // the gateway is on the TARGET leaf (1) when the H-answer (0x9999 -> node 20) arrives -> the hook re-drains NOW.
    DualLayerTestAccess::set_active(gw, 1);
    hash_bind_inner hb{}; hb.target_layer = 2; hb.node_id = 20; hb.key_hash32 = 0x9999u;
    uint8_t hbuf[16]; const size_t hn = pack_hash_bind_inner(hb, std::span<uint8_t>(hbuf, sizeof hbuf));
    CHECK(hn > 0);
    gw.on_hash_bind_response(hbuf, static_cast<uint8_t>(hn), /*authoritative*/ true);   // -> id_bind_set + the 4f re-drain hook
    CHECK(DualLayerTestAccess::handoff_count(gw) == 0);            // resolved + drained IMMEDIATELY (no wait for the next visit)
    CHECK(DualLayerTestAccess::leaf_tx_n(gw, 1) == 1);
    CHECK(DualLayerTestAccess::leaf_tx_at(gw, 1, 0).dst == 20);
}

TEST_CASE("dual-layer bridge: a cross-layer DM addressed to the gateway ITSELF is REFUSED-not-bridged when malformed; not-our-layer refused (Slice 4c.1)") {
    StubHal hal; hal._now = 10000;
    Node gw(hal, /*id*/ 1, 0xABCDu);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(2, 8); cfg.layers[1].node_id = 12;
    CHECK(gw.on_init(cfg));
    const uint8_t body[1] = { 'x' };
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    // target layer 9 is NOT one of the gateway's leaves (1/2) -> REFUSE (no handoff, no default leaf).
    { const uint8_t ids[2] = { 1, 9 };
      const size_t n = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0x9999u, ids, 2, 1, 7, 0, body, 1, 0, 0);
      DualLayerTestAccess::bridge_from(gw, 7, 5, 42, flags, inner, static_cast<uint8_t>(n));
      CHECK(DualLayerTestAccess::handoff_count(gw) == 0); }
    // target leaf 2 is ours, but the recipient hash is UNKNOWN on it -> 4f DEFERS (buffer UNRESOLVED, not drop).
    { const uint8_t ids[2] = { 1, 2 };
      const size_t n = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, 0xBEEFu, ids, 2, 1, 7, 0, body, 1, 0, 0);
      DualLayerTestAccess::bridge_from(gw, 7, 5, 43, flags, inner, static_cast<uint8_t>(n));
      CHECK(DualLayerTestAccess::handoff_count(gw) == 1);                 // DEFERRED (4f), not dropped
      const XlHandoff* h = DualLayerTestAccess::handoff_first(gw);
      CHECK(h != nullptr); if (h) { CHECK(h->dst_node_id == 0); CHECK(h->dst_key_hash32 == 0xBEEFu); } }  // UNRESOLVED, awaiting the binding
}

TEST_CASE("dual-layer send: an RTS to a time-multiplexing gateway DEFERS to its window, then fires when it opens (Slice 4a / 3e.2b)") {
    // 1) a gateway G emits a beacon on leaf 0 (nibble 1, node_id 5) — capture it off the wire. now >= the discovery
    //    beacon period so the boot beacon is due (maybe_emit_gateway_beacon: now - _last_beacon_ms(0) >= period).
    StubHal ghal; ghal._now = 10000;
    Node gw(ghal, /*id*/ 1, 0xABCDu);
    NodeConfig gcfg; gcfg.n_layers = 2;
    gcfg.layers[0] = good_layer(1, 8); gcfg.layers[0].node_id = 5;
    gcfg.layers[1] = good_layer(2, 8); gcfg.layers[1].node_id = 12;   // equal SF -> 7500/7500 windows, period 15000
    CHECK(gw.on_init(gcfg));
    CHECK(ghal.last_tx_len > 0);

    // 2) a normal SENDER on leaf 1 hears G -> learns BOTH a 1-hop route to G (id 5) AND G's window schedule.
    StubHal hal; hal._now = 50000;
    Node tx(hal, /*id*/ 7, 0x7777u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    CHECK(tx.on_init(cfg));
    RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
    tx.on_recv(ghal.last_tx, ghal.last_tx_len, meta);
    CHECK(tx.rt_count() >= 1);                  // a route to G (id 5) installed from its beacon

    // 3) advance so G is on its FOREIGN leaf (deaf to us): heard at 50000, at +8000 the defer is 7300 ms (G has 0
    //    neighbours -> advertises spread nibble 0 = SPARSE -> guard = 100 + sparse_bonus(200); 15000-8000+300 = 7300).
    hal._now = 58000;
    hal.last_tx_len = 0;
    const char* body = "hi!";
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 5; c.u.send.flags = 0;
    c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 3;
    const CmdResult r = tx.on_command(c);
    CHECK(r.code == CmdCode::queued);
    // DEFERRED: no flight installed, NO RTS on the wire, the DM HELD in the queue (next_attempt_ms = the window open).
    CHECK_FALSE(tx.has_pending_tx());
    CHECK(hal.last_tx_len == 0);
    CHECK(r.queue_depth == 1);

    // 4) G's leaf-1 (= our) window re-opens at the deferred wakeup 58000+7300 = 65300 -> the queue wakeup fires the RTS.
    hal._now = 65300;
    DualLayerTestAccess::pump(tx);              // become_free (the kQueueWakeupTimerId handler)
    CHECK(tx.has_pending_tx());                 // flight now live -> the held DM went out
    CHECK(hal.last_tx_len > 0);                 // an RTS hit the wire
}

TEST_CASE("dual-layer config: a window beyond the 8-bit wire range is REFUSED, not clamped (Slice 3e F-C)") {
    // equal-SF derivation splits the period 50/50; schedule_record duration/offset are 8-bit x100ms (max 25.5 s, no escape).
    // period 51000 -> 25500/25500 == the wire max -> ACCEPT (the boundary fits exactly).
    { StubHal h; Node gw(h, /*id*/ 1, 0xABCDu);
      NodeConfig c; c.n_layers = 2;
      c.layers[0] = good_layer(1, 8); c.layers[0].node_id = 5;  c.layers[0].window_period_ms = 51000;
      c.layers[1] = good_layer(2, 8); c.layers[1].node_id = 12; c.layers[1].window_period_ms = 51000;
      CHECK(gw.on_init(c)); }
    // period 51002 -> 25501/25501 > the wire max -> REFUSE (fail loud — a clamp would silently break the defer phase math).
    { StubHal h; Node gw(h, /*id*/ 1, 0xABCDu);
      NodeConfig c; c.n_layers = 2;
      c.layers[0] = good_layer(1, 8); c.layers[0].node_id = 5;  c.layers[0].window_period_ms = 51002;
      c.layers[1] = good_layer(2, 8); c.layers[1].node_id = 12; c.layers[1].window_period_ms = 51002;
      CHECK_FALSE(gw.on_init(c)); }
}

TEST_CASE("dual-layer activation: a swap DRAINS the leaving leaf's sync-response ring (no stale timer on the wrong leaf) (Slice 3c)") {
    StubHal hal; Node node(hal, 1, 0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
    CHECK(node.on_init(cfg));
    // active leaf 0: arm a pending jittered sync-response at slot 3 — its timer id (kSyncResponseTimerId+3) is SHARED across leaves.
    DualLayerTestAccess::arm_sync_slot(node, 3);
    CHECK(DualLayerTestAccess::sync_slot_active(node, 0, 3));
    // swap to leaf 1: leaf 0's slot 3 must be CLEARED + its timer cancelled, so a later fire can't act on leaf 1's ring.
    DualLayerTestAccess::activate(node, 1);
    CHECK_FALSE(DualLayerTestAccess::sync_slot_active(node, 0, 3));               // drained on leave
    CHECK(hal.cancelled[DualLayerTestAccess::sync_timer_id(3)]);                  // the shared timer id was cancelled
    CHECK_FALSE(DualLayerTestAccess::sync_slot_active(node, 1, 3));               // leaf 1's own ring untouched
}

// ---- SLICE 3c #4: Principle 11 — a dual-layer gateway (n_layers==2) is OUT of the channel gossip plane ------
TEST_CASE("dual-layer Principle 11: a gateway never originates or pulls channel gossip (Slice 3c #4)") {
    const uint32_t heard_id = 0x07000002u;                 // a channel msg-id (origin 7) the node does not hold
    // Single-layer reference: a normal node ADMITS a fresh origin (the plane is live).
    { StubHal hal; Node node(hal, /*id*/ 1, 0x1);
      NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
      CHECK(node.on_init(cfg));
      CHECK(DualLayerTestAccess::origin_admit(node, /*origin*/ 5, /*msg_id*/ 0x05000001u));   // normal node admits
    }
    // Gateway: admits NOTHING + schedules NO pull on a heard digest (Principle 11 -> the RAM cap_channel_buffer=8 holds).
    { StubHal hal; Node node(hal, 1, 0x1);
      NodeConfig cfg; cfg.n_layers = 2;
      cfg.layers[0] = good_layer(1, 8); cfg.layers[1] = good_layer(2, 9);
      CHECK(node.on_init(cfg));
      CHECK_FALSE(DualLayerTestAccess::origin_admit(node, 5, 0x05000001u));       // a gateway never originates channel msgs
      const uint32_t ids[1] = { heard_id };
      DualLayerTestAccess::process_digest(node, /*src*/ 7, ids, 1);
      bool any_pull = false;
      for (uint8_t s = 0; s < 8; ++s) if (hal.armed[DualLayerTestAccess::channel_pull_timer_id(s)]) any_pull = true;
      CHECK_FALSE(any_pull);                                                       // a gateway never schedules a channel pull
      // F2: a gateway never FORWARDS — flood_forward_decision frees the slot + arms NO rebroadcast (the durable
      // 'receive-maybe-later, never forward' invariant; the serve gate handle_channel_pull uses the same n_layers==2 early-return).
      DualLayerTestAccess::arm_flood_slot(node, 0);
      CHECK(DualLayerTestAccess::flood_slot_active(node, 0));
      DualLayerTestAccess::flood_decide(node, 0);
      CHECK_FALSE(DualLayerTestAccess::flood_slot_active(node, 0));                 // freed, not rebroadcast
      CHECK_FALSE(hal.armed[DualLayerTestAccess::flood_rebcast_timer_id(0)]);       // no rebroadcast timer armed
    }
}

// ---- multi-hop gateway discovery (type-4 bridged_layers TLV, 2026-06-14) -----------------------------------
// NB: Node is constructed IN PLACE in each case (it has a self-pointer _active = &_layers[0] — a by-value return
// would dangle it). cfg = a plain single-layer node on leaf 1.
#define INIT_LEAF1(VAR, ID, KEY) StubHal hal; Node VAR(hal, (ID), (KEY)); \
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; CHECK(VAR.on_init(cfg))

TEST_CASE("bridged_layers: ingest last-write-wins; build returns 0 when empty (s18 keystone), >0 after a learned row") {
    INIT_LEAF1(n, 1, 0x1);
    uint8_t buf[32];
    CHECK(DualLayerTestAccess::build_gw_ext(n, buf, sizeof buf) == 0);    // single-layer, no rows -> NO type-4 TLV (s18 byte-identical)
    DualLayerTestAccess::ingest_bl(n, /*gw*/ 5, /*dest_leaf*/ 2);
    CHECK(DualLayerTestAccess::bl_dest(n, 5) == 2);
    DualLayerTestAccess::ingest_bl(n, /*gw*/ 5, /*dest_leaf*/ 3);         // SAME gw_id -> overwrite (one row per gw_id)
    CHECK(DualLayerTestAccess::bl_dest(n, 5) == 3);
    CHECK(DualLayerTestAccess::build_gw_ext(n, buf, sizeof buf) > 0);     // a NON-gateway now RE-GOSSIPS the learned row (propagation)
}

TEST_CASE("bridged_layers selection: a gw 2 hops away (in _bridged_layers, NOT _gw_schedules) is selectable") {
    INIT_LEAF1(x, 7, 0x7777u);
    CHECK(DualLayerTestAccess::select_gw(x, /*target_leaf*/ 2) == 0);     // nothing known yet
    DualLayerTestAccess::ingest_bl(x, /*gw*/ 5, /*dest_leaf*/ 2);         // type-4 TLV (multi-hop): G bridges 1<->2, schedule NEVER heard
    DualLayerTestAccess::learn_neighbor(x, 5);                            // a route to G (DV; multi-hop in reality)
    CHECK(DualLayerTestAccess::select_gw(x, 2) == 5);                     // Pass 1: a routed gw bridging leaf 2 -> selected (was 0 before the fix)
}

TEST_CASE("bridged_layers Pass-2 seen-guard: REJECT a propagated gw never heard on our leaf (cross-layer TLV leak)") {
    INIT_LEAF1(x, 7, 0x7777u);
    DualLayerTestAccess::ingest_bl(x, /*gw*/ 9, /*dest_leaf*/ 2);         // a TLV that LEAKED in: gw 9 bridges leaf 2, but no route + never heard on our leaf
    CHECK(DualLayerTestAccess::select_gw(x, 2) == 0);                     // seen-guard REJECTS (no id_bind for gw 9 on our leaf)
    DualLayerTestAccess::hear_on_leaf(x, /*id*/ 9, /*key*/ 0x9999u);      // now we've actually heard gw 9 on our leaf
    CHECK(DualLayerTestAccess::select_gw(x, 2) == 9);                     // Pass 2 accepts -> caller enqueues + fires a ROUTE_QUERY
}

TEST_CASE("bridged_layers: two gateways on ONE leaf bridging DIFFERENT leaves -> the RIGHT gw per target") {
    // The decision motivator (spec): 'is a gateway' is not enough — one leaf can host several gateways, each to a
    // different layer. Selection must pick the gw bridging the TARGET leaf.
    INIT_LEAF1(x, 7, 0x7777u);
    DualLayerTestAccess::ingest_bl(x, /*gwA*/ 5, /*dest_leaf*/ 2); DualLayerTestAccess::learn_neighbor(x, 5);
    DualLayerTestAccess::ingest_bl(x, /*gwB*/ 6, /*dest_leaf*/ 3); DualLayerTestAccess::learn_neighbor(x, 6);
    CHECK(DualLayerTestAccess::select_gw(x, 2) == 5);                     // -> the gw bridging leaf 2
    CHECK(DualLayerTestAccess::select_gw(x, 3) == 6);                     // -> the gw bridging leaf 3
}

// ---- `gateway` one-command provisioning: parse_gateway_cmd + the shared validate predicate -----------------
TEST_CASE("§gateway parse_gateway_cmd — a valid line fills both leaves; validate derives the SF-weighted windows") {
    GatewayProvision g{};
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=5:11:9:7,9", g) == GwParseErr::ok);
    CHECK(g.l0.layer_id == 4); CHECK(g.l0.node_id == 10); CHECK(g.l0.routing_sf == 8);
    CHECK(g.l0.allowed_sf_bitmap == ((1u << 7) | (1u << 9)));
    CHECK(g.l1.layer_id == 5); CHECK(g.l1.node_id == 11); CHECK(g.l1.routing_sf == 9);
    CHECK(g.l1.allowed_sf_bitmap == ((1u << 7) | (1u << 9)));
    // window derive happens in the shared predicate (the SAME one on_init runs)
    CHECK(validate_gateway_layers(g.l0, g.l1, 125000, 5) == GwValErr::ok);
    CHECK(g.l0.window_ms > 0);
    CHECK(g.l1.window_ms == g.l0.window_period_ms - g.l0.window_ms);     // both-derive fills the period
    CHECK(g.l1.window_offset_ms == g.l0.window_ms);                      // anti-phase
    CHECK(g.l1.window_ms > g.l0.window_ms);                              // SF9 leaf gets the longer window
}
TEST_CASE("§gateway parse_gateway_cmd — order-independent tokens + optionals") {
    GatewayProvision g{};
    CHECK(parse_gateway_cmd("period=20000 l1=5:11:9:7 gateway_only=1 l0=4:10:8:7,9,10 freq0=868.1 freq1=869.5", g) == GwParseErr::ok);
    CHECK(g.l0.window_period_ms == 20000); CHECK(g.l1.window_period_ms == 20000);
    CHECK(g.l0.allowed_sf_bitmap == ((1u << 7) | (1u << 9) | (1u << 10)));
    CHECK(g.l1.allowed_sf_bitmap == (1u << 7));
    CHECK(g.gateway_only == true);
    CHECK(g.l0.freq_mhz == doctest::Approx(868.1));
    CHECK(g.l1.freq_mhz == doctest::Approx(869.5));
}
TEST_CASE("§gateway parse_gateway_cmd — freq0/freq1 are optional; omitted => 0 (inherit), non-positive => bad_freq") {
    GatewayProvision g{};
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=5:11:9:7,9", g) == GwParseErr::ok);     // omitted
    CHECK(g.l0.freq_mhz == doctest::Approx(0.0));                                     // 0 = inherit at apply
    CHECK(g.l1.freq_mhz == doctest::Approx(0.0));
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=5:11:9:7,9 freq0=0", g)    == GwParseErr::bad_freq);   // must be > 0 if present
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=5:11:9:7,9 freq1=-1", g)   == GwParseErr::bad_freq);
}
TEST_CASE("§gateway parse_gateway_cmd — error matrix; the 1..16 reservation is NOT enforced") {
    GatewayProvision g{};
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9", g)                   == GwParseErr::missing_l1);
    CHECK(parse_gateway_cmd("l1=5:11:9:7,9", g)                   == GwParseErr::missing_l0);
    CHECK(parse_gateway_cmd("l0=4:0:8:7,9 l1=5:11:9:7,9", g)      == GwParseErr::bad_node);    // node 0
    CHECK(parse_gateway_cmd("l0=4:255:8:7,9 l1=5:11:9:7,9", g)    == GwParseErr::bad_node);    // node 255 (0xFF reserved)
    CHECK(parse_gateway_cmd("l0=0:10:8:7,9 l1=5:11:9:7,9", g)     == GwParseErr::bad_leaf);    // leaf 0
    CHECK(parse_gateway_cmd("l0=4:10:13:7,9 l1=5:11:9:7,9", g)    == GwParseErr::bad_ctrl_sf); // SF 13
    CHECK(parse_gateway_cmd("l0=4:10:8: l1=5:11:9:7,9", g)        == GwParseErr::bad_data_sf); // empty data list
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=5:11:9:7,9 x=1", g) == GwParseErr::unknown_opt);
    // RESERVATION NOT ENFORCED: gateway node ids like 110 / 21 (outside 1..16) parse fine (Join-time convention)
    CHECK(parse_gateway_cmd("l0=10:110:8:7,9 l1=5:21:9:7,9", g)   == GwParseErr::ok);
    // the two leaves MAY share a node_id
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=5:10:9:7,9", g)     == GwParseErr::ok);
}
TEST_CASE("§gateway — leaf-nibble clash is caught by validate (parity with on_init), not parse") {
    GatewayProvision g{};
    CHECK(parse_gateway_cmd("l0=4:10:8:7,9 l1=20:11:9:7,9", g) == GwParseErr::ok);    // leaf 20 is a valid value
    CHECK(validate_gateway_layers(g.l0, g.l1, 125000, 5) == GwValErr::leaf_nibble_clash);  // 4 == (20 & 0x0F)
}

// ---- gateway-window broadcast sync (2026-06-20 side-task): bias the PERIODIC beacon to a gw-neighbour window-open ----
// Core invariant: ZERO extra beacons — the nominal cadence is preserved; the fire time is only pushed to the first
// window-open at/after it (added delay < one window-period). A no-op without a gateway neighbour / in discovery.
TEST_CASE("gw-window sync: periodic beacon biases to the gateway's window-open; added delay < one window-period") {
    StubHal hal; hal._now = 100000; Node node(hal, /*id*/ 7, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; cfg.beacon_period_ms = 30000;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::force_exit_discovery(node);
    DualLayerTestAccess::store_gw_schedule(node, /*gw*/ 5, /*leaf_served*/ 1);   // heard=now(100000), period 15000, leaf-1 window [0,7500)
    hal._now = 110000;                                                           // phase 10000 -> our window CLOSED; next open at +5000
    const uint32_t base = DualLayerTestAccess::gw_base_defer(node, 5);
    CHECK(base > 0);                                                             // not in-window -> a real defer
    const uint32_t nominal = 30000;
    const uint32_t delay = DualLayerTestAccess::align_beacon(node, nominal);
    CHECK(delay >= nominal);                                                     // never fires EARLIER than the cadence (no extra beacons)
    CHECK(delay <  nominal + 15000);                                             // added delay < one window-period
    CHECK((delay - base) % 15000 == 0);                                          // lands exactly at a window-open (nibble 0 -> no jitter)
}

TEST_CASE("gw-window sync: no gateway neighbour -> plain cadence (identical to today)") {
    StubHal hal; hal._now = 100000; Node node(hal, 7, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; cfg.beacon_period_ms = 30000;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::force_exit_discovery(node);
    CHECK(DualLayerTestAccess::align_beacon(node, 30000) == 30000);              // no schedule held -> unchanged
}

TEST_CASE("gw-window sync: in-discovery AND gw-in-window-now both skip the bias") {
    StubHal hal; hal._now = 100000; Node node(hal, 7, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; cfg.beacon_period_ms = 30000;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::store_gw_schedule(node, 5, 1);                          // window open NOW (phase 0)
    CHECK(DualLayerTestAccess::align_beacon(node, 30000) == 30000);             // (i) still in discovery -> no bias
    DualLayerTestAccess::force_exit_discovery(node);
    CHECK(DualLayerTestAccess::gw_base_defer(node, 5) == 0);                    // in-window -> defer 0
    CHECK(DualLayerTestAccess::align_beacon(node, 30000) == 30000);             // (ii) gw in-window-now -> skip
}

TEST_CASE("gw-window sync: bias is bounded (< one window-period added) across all phases -> beacon count stays bounded") {
    StubHal hal; hal._now = 100000; Node node(hal, 7, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; cfg.beacon_period_ms = 30000;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::force_exit_discovery(node);
    DualLayerTestAccess::store_gw_schedule(node, 5, 1);                          // heard=100000
    for (uint32_t ph = 100; ph < 15000; ph += 250) {                            // sweep the window phase
        hal._now = 100000 + ph;
        const uint32_t delay = DualLayerTestAccess::align_beacon(node, 30000);
        CHECK(delay >= 30000);                                                   // never earlier than cadence
        CHECK(delay <  30000 + 15000 + 7500);                                    // <= one window-period step + intra-window jitter span
    }
}

// ---- gateway reactive route-pull on a cross-layer bridge miss (spec 2026-06-21 §6.1) ----------------------
// A gateway time-multiplexes its layers and may permanently miss a far-layer route (dirty-only steady-state beacons).
// On a bridged DM with no far-layer route it must reactively PULL (REQ_SYNC force) + DEFER (not drop) the transit leg.
TEST_CASE("gw route-pull: send_req_sync_q(force) bypasses boot-flag + route-rich guards; 30s rate-limit holds") {
    StubHal hal; hal._now = 100000; Node node(hal, /*id*/ 5, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    cfg.req_sync_on_boot = false; cfg.req_sync_min_routes = 8;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::force_exit_discovery(node);
    DualLayerTestAccess::set_active_rt_count(node, 10);                       // route-RICH (>= min 8) + boot-flag OFF
    hal.last_tx_len = 0;
    DualLayerTestAccess::req_sync(node, /*force=*/false);
    CHECK(hal.last_tx_len == 0);                                             // (i) guarded -> nothing sent
    hal.last_tx_len = 0;
    DualLayerTestAccess::req_sync(node, /*force=*/true);                     // (ii) force bypasses BOTH guards
    auto q = parse_q(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(q.has_value());
    if (q) CHECK(q->opcode == static_cast<uint8_t>(q_opcode::req_sync));
    hal.last_tx_len = 0;
    DualLayerTestAccess::req_sync(node, /*force=*/true);                     // (iii) within retry_ms -> rate-limited (no Q-storm)
    CHECK(hal.last_tx_len == 0);
    hal._now += protocol::req_sync_retry_ms + 1;
    hal.last_tx_len = 0;
    DualLayerTestAccess::req_sync(node, /*force=*/true);                     // (iv) past the window -> sends again
    CHECK(hal.last_tx_len > 0);
}

TEST_CASE("gw route-pull: a gateway-relay no-route leg PULLS + DEFERS (not drop); a normal forwarder still drops") {
    StubHal hal; hal._now = 100000; Node node(hal, /*id*/ 5, 0x1);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; cfg.req_sync_on_boot = false;
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::force_exit_discovery(node);
    // (a) GW-RELAY transit DM, no route to dst -> force REQ_SYNC + DEFER (parked, not dropped). NOTE: defer_send also
    // fires a ttl=1 RREQ probe (emit_route_request) which is the LAST frame on air, so we witness the forced pull via
    // the rate-limit stamp (set just before the REQ_SYNC tx), not last_tx. A real REQ_SYNC frame is covered above.
    TxItem gw{}; gw.dst = 99; gw.is_forward = true; gw.is_gw_relay = true; gw.enqueue_time_ms = hal._now;
    DualLayerTestAccess::issue(node, gw);
    CHECK(DualLayerTestAccess::last_req_sync_ms(node) == hal._now);          // the forced pull fired
    CHECK(DualLayerTestAccess::deferred_count(node) == 1);                   // parked, NOT dropped
    // (d) a NORMAL forwarder (not gw-relay), no route -> DROP: no pull, nothing newly deferred (unchanged dv:7041-7048)
    hal._now += protocol::req_sync_retry_ms + 1;                             // advance past the rate-limit so a pull COULD fire (isolates the is_gw_relay gate)
    const uint64_t before = DualLayerTestAccess::last_req_sync_ms(node);
    TxItem fwd{}; fwd.dst = 98; fwd.is_forward = true; fwd.is_gw_relay = false; fwd.enqueue_time_ms = hal._now;
    DualLayerTestAccess::issue(node, fwd);
    CHECK(DualLayerTestAccess::last_req_sync_ms(node) == before);            // NO pull (unchanged)
    CHECK(DualLayerTestAccess::deferred_count(node) == 1);                   // still only the gw-relay one -> the forwarder dropped
}

// ---- SLICE 4: gateway anti-spam-exemption VERIFICATION (no production change) -------------------------------
// Invariant-pinning tests for the design's "Gateway cross-layer relays — EXEMPT" (MF5/MF6/MF9): a dual-layer
// gateway (n_layers==2) is already OUTSIDE every anti-spam gate Slices 2/3 added. If any of these go RED, a
// Slice-2/3 gate regressed and now bites the bridge — report it as a Slice-2/3 bug, do NOT relax the test.

// Task 1 — the gateway channel plane admits NOTHING: channel_origin_admit early-returns false out-of-plane
// (node_channel.cpp:79, n_layers==2) BEFORE any channel_cap_origin() / 10 s-floor math. So the channel cap can
// never be reached to bite a bridged channel flood (MF5).
TEST_CASE("gateway exemption: channel_origin_admit early-returns false out-of-plane (n_layers==2) — cap_origin never runs on a bridge") {
    StubHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));                                    // valid gateway inits (-fno-exceptions => CHECK only)

    // Distinct origins, first-ever ids, low count: on a NON-gateway these would ADMIT (cf. the single-layer
    // reference in the Principle-11 case above); here every one is dropped purely by the n_layers==2 guard.
    for (uint8_t origin = 20; origin < 30; ++origin)
        CHECK_FALSE(DualLayerTestAccess::origin_admit(node, origin, /*msg_id=*/0xAA000000u | origin));
    // Even the gateway's OWN node_id as origin is refused out-of-plane (the self-bypass at :80 is never reached).
    CHECK_FALSE(DualLayerTestAccess::origin_admit(node, /*origin=*/1, /*msg_id=*/0x01000001u));
}

// Task 2 — a BRIDGED DM carries is_forward=true, so it can never enter the Slice-3 dm_min_interval branch
// (node_mac.cpp:396, which is gated on origin==_node_id && !is_forward && !is_channel_m). NOTE (API drift vs the
// plan): bridge_from does NOT land a forward directly on a leaf tx_queue — bridge_cross_layer buffers an XlHandoff;
// the is_forward=true/is_gw_relay=true TxItem is only created by drain_xl_handoffs_for_leaf on the ACTIVATED target
// leaf with the recipient bound (see node_mac_rx.cpp:823-863 + the "re-injects a cross-layer DM" case above). We
// follow that real two-step path; the invariant under test (a bridged leg is is_forward, thus DM-floor-exempt) holds.
TEST_CASE("gateway exemption: a bridged DM (is_forward) skips dm_min_interval — the self-throttle lives inside !is_forward") {
    StubHal hal; hal._now = 10000;
    Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8); cfg.layers[0].node_id = 5;
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9); cfg.layers[1].node_id = 12;
    CHECK(node.on_init(cfg));
    // Two distinct recipients bound on the FAR leaf (leaf 1) so both cross-layer resolves succeed.
    DualLayerTestAccess::bind_on_leaf(node, /*leaf*/ 1, /*node*/ 20, /*key*/ 0x9990u);
    DualLayerTestAccess::bind_on_leaf(node, /*leaf*/ 1, /*node*/ 21, /*key*/ 0x9991u);

    // Bridge two cross-layer DM legs back-to-back at the SAME timestamp (0 ms apart). Each -> one XlHandoff.
    const uint8_t ids[2] = { 1, 2 }; const uint8_t body[2] = { 'h', 'i' };
    uint8_t innerA[64], innerB[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH);
    const size_t ilA = pack_unicast_inner(std::span<uint8_t>(innerA, sizeof innerA), flags, 0x9990u, ids, 2, 1, /*origin*/ 40, 0, body, 2, 0, 0);
    const size_t ilB = pack_unicast_inner(std::span<uint8_t>(innerB, sizeof innerB), flags, 0x9991u, ids, 2, 1, /*origin*/ 41, 0, body, 2, 0, 0);
    CHECK(ilA > 0); CHECK(ilB > 0);
    DualLayerTestAccess::bridge_from(node, /*origin=*/40, /*dst=*/5, /*ctr=*/0x11, flags, innerA, static_cast<uint8_t>(ilA));
    DualLayerTestAccess::bridge_from(node, /*origin=*/41, /*dst=*/5, /*ctr=*/0x12, flags, innerB, static_cast<uint8_t>(ilB));
    CHECK(DualLayerTestAccess::handoff_count(node) == 2);        // both buffered as handoffs for the far leaf

    // Activate the far leaf + install routes to both recipients, then drain -> both re-inject as forward legs.
    DualLayerTestAccess::set_active(node, 1);
    DualLayerTestAccess::learn_neighbor(node, 20);
    DualLayerTestAccess::learn_neighbor(node, 21);
    DualLayerTestAccess::drain(node, 1);

    // Both bridged legs landed on leaf 1's tx_queue (bridge routes to the far layer).
    uint8_t queued = static_cast<uint8_t>(DualLayerTestAccess::leaf_tx_n(node, 0) + DualLayerTestAccess::leaf_tx_n(node, 1));
    CHECK(queued >= 2);
    bool both_forward = true;
    for (uint8_t leaf = 0; leaf < 2; ++leaf)
        for (uint8_t i = 0; i < DualLayerTestAccess::leaf_tx_n(node, leaf); ++i)
            if (!DualLayerTestAccess::leaf_tx_at(node, leaf, i).is_forward) both_forward = false;
    CHECK(both_forward);                                        // every bridged leg carries is_forward=true (skips the DM branch)
}

// Task 3 — a gateway's OWN e2e-ack (DATA_TYPE_E2E_ACK) DOES enter the own-origin DM branch (origin==self,
// !is_forward, !is_channel_m) but is exempted by DataType (node_mac.cpp:394-396, MF9), so the 3 s dm_min_interval
// can't delay a cross-layer delivery confirmation. Fire two acks < dm_min_interval apart: neither must be deferred.
TEST_CASE("gateway exemption: OWN e2e-ack (DATA_TYPE_E2E_ACK) is dm_min_interval-exempt — bridge acks never self-throttle") {
    StubHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.n_layers = 2;
    cfg.layers[0] = good_layer(/*layer_id*/ 1, /*sf*/ 8);
    cfg.layers[1] = good_layer(/*layer_id*/ 2, /*sf*/ 9);
    CHECK(node.on_init(cfg));
    DualLayerTestAccess::learn_neighbor(node, /*node_id=*/7);   // a 1-hop route (on the active leaf 0) so the ack drains

    hal._now = 100000;
    DualLayerTestAccess::send_ack(node, /*to_origin=*/7, /*acked_ctr=*/0x33);
    DualLayerTestAccess::send_ack(node, /*to_origin=*/7, /*acked_ctr=*/0x34);      // < dm_min_interval_ms later
    const TxItem* last = DualLayerTestAccess::tx_back(node, /*leaf=*/0);
    CHECK(last != nullptr);
    if (last) {
        CHECK(last->type == DATA_TYPE_E2E_ACK);                 // typed as an ack (the DataType the exemption keys on)
        CHECK_FALSE(last->is_forward);                          // an own-origination (would enter the DM branch)
    }
    DualLayerTestAccess::pump(node);                            // become_free must NOT defer an ack via dm_min_interval
    DualLayerTestAccess::pump(node);
    // If MF9 held, neither ack was deferred by dm_min_interval; if it regressed, an ack lingers with a future
    // next_attempt_ms. Assert nothing is stuck behind a dm_min_interval defer.
    bool any_deferred = false;
    for (uint8_t leaf = 0; leaf < 2; ++leaf)
        for (uint8_t i = 0; i < DualLayerTestAccess::leaf_tx_n(node, leaf); ++i)
            if (DualLayerTestAccess::leaf_tx_at(node, leaf, i).type == DATA_TYPE_E2E_ACK &&
                DualLayerTestAccess::leaf_tx_at(node, leaf, i).next_attempt_ms > hal._now) any_deferred = true;
    CHECK_FALSE(any_deferred);
}

TEST_CASE("§mobile 3a — host last-mile forward: re-address a DM for a hosted mobile to its local-id + addr_len=1") {
    StubHal hal; Node host(hal, 1, 0xAA01u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0;
    CHECK(host.on_init(cfg));
    DualLayerTestAccess::store_mobile(host, /*key*/ 0xB0Bu, /*local*/ 17);   // this host registers mobile 0xB0B -> local 17
    // NB: NO route to 17 installed -> the forward can only fly via Fix 4 (mobile-next is a DIRECT 1-hop send, no rt_find).
    hal.emits.clear();
    DualLayerTestAccess::drive_post_ack_deliver(host, /*origin*/ 42, /*dst_hash*/ 0xB0Bu);
    CHECK(hal.saw_emit("mobile_lastmile_fwd"));
    const PendingTx* pt = DualLayerTestAccess::pending(host);   // become_free issued the forward -> the in-flight state
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->dst == 17);            // ★ re-addressed to the mobile's LOCAL id
        CHECK(pt->addr_len == 1);        // ★ the mobile mark rides the forward RTS (byte-3)
        CHECK(pt->mobile_src == false);  // a host forward, NOT a mobile origination
        CHECK(pt->origin == 42);         // the real originator is preserved (anti-spam)
    }

    // control: a host with NO registered mobile -> the fork is dormant (_mobile_reg_n==0) -> no re-address here
    StubHal hal2; Node host2(hal2, 1, 0xAA02u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=0; host2.on_init(c2);
    hal2.emits.clear();
    DualLayerTestAccess::drive_post_ack_deliver(host2, 42, 0xB0Bu);
    CHECK_FALSE(hal2.saw_emit("mobile_lastmile_fwd"));   // _mobile_reg_n==0 -> byte-identical (no fork)
    CHECK(DualLayerTestAccess::leaf_tx_n(host2, 0) == 0);
}

TEST_CASE("§mobile 3b outbound — a registered mobile bills home (origin), self-marks, and routes via home_node") {
    StubHal hal; Node mob(hal, /*id=*/0, /*key=*/0x9999u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true;
    CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/ 20, /*home*/ 5, /*home_hash*/ 0x5050u);
    const uint8_t body[] = { 0x01, 0x02, 0x03 };
    DualLayerTestAccess::do_send(mob, /*dst*/ 99, body, sizeof body);   // originate a DM to any dst -> routed via home
    const PendingTx* pt = DualLayerTestAccess::pending(mob);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->origin == 5);          // ★ Fix 4: billed to the home_node (an accountable GLOBAL id; E2E identity rides sender_hash)
        CHECK(pt->next == 5);            // ★ Fix 3: routed via the 1-hop home_node (NOT rt_find(99))
        CHECK(pt->mobile_src == true);   // ★ Fix 3: self-marked -> the host keeps our local-id out of the global rt (A1)
    }
}

TEST_CASE("§mobile 3b receive — the mark disambiguates a colliding id: a mobile accepts addr_len=1, a static accepts addr_len=0") {
    auto accepts = [](bool is_mobile, uint8_t addr_len) -> bool {
        StubHal hal; Node node(hal, /*id=*/20, /*key=*/0x2020u);
        NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=is_mobile;
        node.on_init(cfg);
        RxMeta meta{ 9.0f, -70.0f, 0, static_cast<int8_t>(-1) };
        rts_in r{}; r.leaf_id=4; r.src=50; r.next=20; r.ctr_lo=1; r.dst=20; r.sf_index=0; r.rts_flags=0; r.payload_len=1; r.addr_len=addr_len;
        uint8_t b[9]; size_t n = pack_rts(r, b); node.on_recv(b, n, meta);
        return DualLayerTestAccess::has_pending_rx(node);   // a receiver flight opened => ADDRESSED (accepted)
    };
    CHECK(accepts(/*is_mobile*/ true,  /*addr_len*/ 1));       // ★ the mobile accepts the marked last-mile RTS
    CHECK_FALSE(accepts(/*is_mobile*/ false, /*addr_len*/ 1)); // ★ a colliding STATIC id 20 treats the marked RTS as overheard
    CHECK(accepts(/*is_mobile*/ false, /*addr_len*/ 0));       // a normal (unmarked) RTS to static id 20 -> accepted (unchanged)
    CHECK_FALSE(accepts(/*is_mobile*/ true,  /*addr_len*/ 0)); // a mobile ignores a global-addressed (unmarked) frame merely matching its local-id
}

TEST_CASE("§mobile 3c — mobile_home cache: find/set/coexist/TTL") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; CHECK(n.on_init(cfg));
    CHECK(n.mobile_home_find(0xAAAAu) == -1);              // empty -> miss
    hal._now = 1000; n.mobile_home_set(0xAAAAu, 19);
    CHECK(n.mobile_home_find(0xAAAAu) == 19);              // M -> home 19
    n.mobile_home_set(0xBBBBu, 19);                        // a 2nd mobile sharing the SAME home (no bijection)
    CHECK(n.mobile_home_find(0xBBBBu) == 19);
    CHECK(n.mobile_home_find(0xAAAAu) == 19);              // both coexist
    hal._now = 1000 + protocol::mobile_home_cache_ttl_ms;  // TTL expiry
    CHECK(n.mobile_home_find(0xAAAAu) == -1);              // dropped on read
}

TEST_CASE("§mobile 3c — Fix 5: override_dst_hash stamps the DM's DST_HASH = M (not key_hash_of_id(N))") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; CHECK(n.on_init(cfg));
    DualLayerTestAccess::learn_neighbor(n, 19);           // a route to the home so the DM flies
    const uint8_t body[] = {0xAA};
    DualLayerTestAccess::do_send_override(n, /*dst*/19, body, 1, /*override=*/0xB0B1u);
    const PendingTx* pt = DualLayerTestAccess::pending(n);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK((pt->flags & DATA_FLAG_DST_HASH) != 0);
        const uint32_t inner_hash = pt->inner[0] | (pt->inner[1]<<8) | (pt->inner[2]<<16) | (static_cast<uint32_t>(pt->inner[3])<<24);
        CHECK(inner_hash == 0xB0B1u);                     // ★ the QUERIED mobile hash M (so the home forwards), NOT N's own hash
    }
}

TEST_CASE("§mobile 3c — Fix 4: send_by_hash hits the mobile_home cache -> sends to home with DST_HASH=M, no giveup") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; CHECK(n.on_init(cfg));
    DualLayerTestAccess::learn_neighbor(n, 19);
    n.mobile_home_set(0xB0B1u, 19);                       // cached: mobile M -> home 19
    hal.emits.clear();
    const uint8_t body[] = {0xAA};
    DualLayerTestAccess::send_by_hash(n, 0xB0B1u, body, 1);
    const PendingTx* pt = DualLayerTestAccess::pending(n);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->dst == 19);                             // ★ routed to the HOME (the cache hit), NOT parked/flooded
        CHECK((pt->flags & DATA_FLAG_DST_HASH) != 0);
        const uint32_t inner_hash = pt->inner[0] | (pt->inner[1]<<8) | (pt->inner[2]<<16) | (static_cast<uint32_t>(pt->inner[3])<<24);
        CHECK(inner_hash == 0xB0B1u);                     // ★ carries M so the home last-mile-forwards, not consumes
    }
    CHECK_FALSE(hal.saw_emit("send_hash_giveup"));        // resolved via the cache -> no park/giveup
}

TEST_CASE("§mobile Fix 5 (§18) — E2E-ACK when the origin's GLOBAL id == the mobile's LOCAL id: via home, no loop, no self-drop") {
    // The mobile is local-id 20; it received a DM from a GLOBAL origin ALSO numbered 20. It must ack origin 20 WITHOUT
    // (a) self-dropping (dst 20 == its own local id) or (b) looping back to itself. Fix 3 routes the ack via the home;
    // A1 keeps the mobile's local-20 out of the home's rt so the forward resolves to the GLOBAL node 20.
    StubHal hal; Node mob(hal, 0, 0x2020u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true;
    CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/20, /*home*/30, /*home_hash*/0x3030u);
    DualLayerTestAccess::send_e2e_ack(mob, /*to_origin*/20, /*ctr*/0x1234);
    const PendingTx* pt = DualLayerTestAccess::pending(mob);
    CHECK(pt != nullptr);                     // ★ NOT self-dropped — the mobile acks even though dst(20)==its own local id
    if (pt) {
        CHECK(pt->dst == 20);                 // the origin (the ack's final dest)
        CHECK(pt->next == 30);                // ★ routed via the HOME_node (Fix 3), NEVER rt_find(20)->itself
        CHECK(pt->mobile_src == true);        // ★ self-marked -> the home keeps 20 out of its rt (A1)
        CHECK(pt->origin == 30);              // ★ billed to the home (Fix 4)
        CHECK(pt->type == DATA_TYPE_E2E_ACK);
    }
    // at the HOME: the ack's RTS (mobile_src=1, src=20) must NOT add an rt entry for 20 -> rt_find(20) stays the GLOBAL node
    StubHal hhal; Node home(hhal, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    RxMeta meta{9.0f,-70.0f,0,static_cast<int8_t>(-1)};
    rts_in r{}; r.leaf_id=4; r.src=20; r.next=30; r.ctr_lo=4; r.dst=20; r.sf_index=0; r.rts_flags=RTS_FLAG_E2E_ACK; r.payload_len=2; r.mobile_src=true;
    uint8_t b[9]; size_t n=pack_rts(r,b);
    const uint8_t rc0 = home.rt_count();
    home.on_recv(b, n, meta);
    CHECK(home.rt_count() == rc0);            // ★ A1: the home did NOT learn the mobile's local-20 -> the ack forwards to GLOBAL-20, no loop
}

TEST_CASE("§mobile — a home E2E-acks its OWN hosted mobile's DM via a last-mile (addr_len=1), NOT a self-send to its own id") {
    // The reported bug: a registered mobile DMs its HOME; the mobile stamps origin=home_id, so the home delivered the
    // message then tried send_e2e_ack(to_origin=home_id) == a self-send (31->31) that never leaves -> the mobile never
    // learns its DM arrived. Fix: the home recognizes sender_hash as one of ITS hosted mobiles and re-addresses the ack
    // as a direct last-mile DM to the mobile's LOCAL id (addr_len=1), the way it relays TO a mobile.
    StubHal hal; Node home(hal, 31, 0xAD20B6EAu);   // this IS the home (its own key hash)
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*key*/0xC0FFEEu, /*local*/17);   // the home hosts mobile 0xC0FFEE -> local 17
    // the mobile's DM arrived stamped origin=home_id(31), source_hash=the mobile's hash. The home acks with sender_hash set.
    DualLayerTestAccess::send_e2e_ack(home, /*to_origin*/31, /*ctr*/0x0006, /*sender_hash*/0xC0FFEEu);
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);                     // ★ NOT a dropped self-send
    if (pt) {
        CHECK(pt->dst == 17);                 // ★ re-addressed to the mobile's LOCAL id (not to_origin=31=self)
        CHECK(pt->addr_len == 1);             // ★ the last-mile mark (dst is a mobile local id)
        CHECK(pt->mobile_src == false);       // a HOST last-mile origination, NOT a mobile self-origination
        CHECK(pt->origin == 31);              // billed to the home (this node)
        CHECK(pt->type == DATA_TYPE_E2E_ACK);
    }
}

TEST_CASE("§mobile reverse-ack Piece 1 — the acker echoes the mobile's source_hash on the E2E-ack (sender != origin); a static sender does NOT (s18-safe gate)") {
    // X (static, id 70) acks a DM whose origin is a mobile's home (31, hash AD20B6EA) but whose SENDER is the MOBILE
    // (hash C0FFEE). sender_hash != key_hash_of_id(origin) -> X stamps SOURCE_HASH=M on the ack so M's home recognizes +
    // last-miles it. A STATIC sender (source_hash == the origin's own key) trips the gate FALSE -> plain ack.
    StubHal hal; Node x(hal, 70, 0x7070u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; CHECK(x.on_init(cfg));
    DualLayerTestAccess::bind_authoritative(x, /*home id*/31, /*home hash*/0xAD20B6EAu);   // X knows the home's key
    DualLayerTestAccess::learn_neighbor(x, 31);                                            // ...and a 1-hop route to it
    DualLayerTestAccess::send_e2e_ack(x, /*to_origin=home*/31, /*ctr*/0x0006, /*sender_hash=M*/0xC0FFEEu);
    const PendingTx* pt = DualLayerTestAccess::pending(x);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->dst == 31);                              // routes to the home (the mobile's stamped origin)
        CHECK((pt->flags & DATA_FLAG_SOURCE_HASH) != 0);   // ★ carries the mobile's stable identity ON the wire
        CHECK(pt->type == DATA_TYPE_E2E_ACK);
        const uint32_t sh = pt->inner[1] | (pt->inner[2]<<8) | (pt->inner[3]<<16) | (static_cast<uint32_t>(pt->inner[4])<<24);
        CHECK(sh == 0xC0FFEEu);                            // inner = [origin][source_hash=M 4B LE][ctr] -> M at inner[1..4]
    }
    // STATIC sender: origin(31) IS the sender -> source_hash == key_hash_of_id(31) -> NO echo (byte-identical plain ack)
    StubHal hal2; Node x2(hal2, 70, 0x7070u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; CHECK(x2.on_init(c2));
    DualLayerTestAccess::bind_authoritative(x2, 31, 0xAD20B6EAu);
    DualLayerTestAccess::learn_neighbor(x2, 31);
    DualLayerTestAccess::send_e2e_ack(x2, /*to_origin*/31, /*ctr*/0x0006, /*sender_hash=origin's own key*/0xAD20B6EAu);
    const PendingTx* pt2 = DualLayerTestAccess::pending(x2);
    CHECK(pt2 != nullptr);
    if (pt2) CHECK((pt2->flags & DATA_FLAG_SOURCE_HASH) == 0);   // ★ no echo -> plain ack
}

TEST_CASE("§mobile reverse-ack Piece 2 — a home last-miles an E2E-ack whose source_hash is a hosted mobile (direct: ctr passes through)") {
    StubHal hal; Node home(hal, 31, 0xAD20B6EAu);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xC0FFEEu, /*local*/17);
    // an E2E-ack arrives from X(70) carrying source_hash=M(C0FFEE), acking ctr 6 (M's own ctr — a DIRECT send)
    DualLayerTestAccess::drive_post_ack_e2e_ack(home, /*acker*/70, /*source_hash=M*/0xC0FFEEu, /*acked_ctr*/0x0006);
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->dst == 17);                    // ★ last-miled to the mobile's local id
        CHECK(pt->addr_len == 1);
        CHECK(pt->type == DATA_TYPE_E2E_ACK);
        CHECK(pt->inner[1] == 0x06);             // ctr passes through (direct) — last-mile inner = [origin][ctr_lo][ctr_hi]
        CHECK(pt->inner[2] == 0x00);
    }
    CHECK(hal.saw_emit("mobile_reverse_ack"));
    // a NON-hosted source_hash -> NOT intercepted (falls through to normal ack receipt; no last-mile)
    StubHal hal2; Node home2(hal2, 31, 0xAD20B6EAu);
    NodeConfig hc2; hc2.routing_sf=8; hc2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc2.leaf_id=4; CHECK(home2.on_init(hc2));
    DualLayerTestAccess::store_mobile(home2, 0xC0FFEEu, 17);
    DualLayerTestAccess::drive_post_ack_e2e_ack(home2, 70, /*source_hash*/0xDEADu, 0x0006);
    CHECK_FALSE(hal2.saw_emit("mobile_reverse_ack"));
}

TEST_CASE("§mobile reverse-ack Step 3 — a home re-originating a delegated MOBILE_SEND records the ctr_H->ctr_M map") {
    StubHal hal; Node home(hal, 31, 0xAD20B6EAu);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xC0FFEEu, /*local*/17);   // the home hosts M
    DualLayerTestAccess::bind_authoritative(home, /*X id*/70, /*X hash*/0x7070u);   // ...and knows the target X
    DualLayerTestAccess::learn_neighbor(home, 70);                                  // ...with a 1-hop route (resolves NOW, no park)
    DualLayerTestAccess::drive_post_ack_mobile_send(home, /*M*/0xC0FFEEu, /*X hash*/0x7070u, /*ctr_M*/0x0006, /*body*/0xAB);
    CHECK(hal.saw_emit("deleg_ack_put"));       // ★ the re-origination recorded ctr_H->ctr_M
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);                       // the re-originated DM is in flight to X
    if (pt) {
        CHECK(pt->dst == 70);                                  // routed to the target X
        CHECK((pt->flags & DATA_FLAG_SOURCE_HASH) != 0);       // carries SOURCE_HASH=M so X's ack echoes M's identity
    }
}

TEST_CASE("§mobile reverse-ack Step 3 — a home translates a delegated target-ack (ctr_H) back to the mobile's ctr (ctr_M) on the last-mile") {
    StubHal hal; Node home(hal, 31, 0xAD20B6EAu);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xC0FFEEu, /*local*/17);
    DualLayerTestAccess::deleg_ack_put(home, /*acker X*/70, /*ctr_H*/0x000Cu, /*ctr_M*/0x0006u);   // a prior delegated re-origination
    // X(70) acks ctr_H=0x0C, carrying source_hash=M -> the home last-miles to M but with the MOBILE's own ctr (6)
    DualLayerTestAccess::drive_post_ack_e2e_ack(home, /*acker*/70, /*source_hash=M*/0xC0FFEEu, /*acked=ctr_H*/0x000Cu);
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->dst == 17); CHECK(pt->addr_len == 1); CHECK(pt->type == DATA_TYPE_E2E_ACK);
        CHECK(pt->inner[1] == 0x06);   // ★ TRANSLATED to ctr_M=6 (NOT the wire ctr_H=0x0C)
        CHECK(pt->inner[2] == 0x00);
    }
    CHECK(hal.saw_emit("mobile_reverse_ack"));
}

TEST_CASE("§mobile — a reply-expecting static send FAILS LOUD when the mobile has no routable home (no RREQ storm); a registered mobile proceeds") {
    // An UNREGISTERED mobile stamps origin=_node_id (a mobile local id no static node routes to). A reply-expecting DM to
    // a STATIC target would make the target flood RREQ for that origin (storm) + the ack never returns. Refuse it loud.
    StubHal hal; Node m(hal, 17, 0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=4; cfg.is_mobile=true; CHECK(m.on_init(cfg));
    const uint8_t body[2] = {'h','i'};
    DualLayerTestAccess::do_send_ack(m, /*static dst*/70, body, 2);
    CHECK(hal.saw_emit("send_failed"));                       // ★ fail loud (reason=mobile_no_home)
    CHECK(DualLayerTestAccess::pending(m) == nullptr);        // ★ nothing enqueued -> no un-ackable DM, no storm
    // a REGISTERED mobile (home=146) makes the reverse-ack routable -> the same send proceeds (origin=home_id, via home)
    StubHal h2; Node m2(h2, 0, 0x1717u);
    NodeConfig c2; c2.routing_sf=7; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); c2.leaf_id=4; c2.is_mobile=true; CHECK(m2.on_init(c2));
    DualLayerTestAccess::make_registered_mobile(m2, /*local*/17, /*home*/146, /*home_hash*/0x9292u);
    DualLayerTestAccess::learn_neighbor(m2, 146);            // a route to the home (first hop)
    h2.emits.clear();
    DualLayerTestAccess::do_send_ack(m2, /*static dst*/70, body, 2);
    CHECK_FALSE(h2.saw_emit("send_failed"));                 // ★ registered -> NOT blocked (proceeds via the home)
    // a PLAIN (no-ack) send from the unregistered mobile is still allowed best-effort (only reply-expecting is gated)
    StubHal h3; Node m3(h3, 17, 0x1717u);
    NodeConfig c3; c3.routing_sf=7; c3.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); c3.leaf_id=4; c3.is_mobile=true; CHECK(m3.on_init(c3));
    DualLayerTestAccess::learn_neighbor(m3, 70);
    DualLayerTestAccess::do_send(m3, /*static dst*/70, body, 2);
    CHECK_FALSE(h3.saw_emit("send_failed"));                 // ★ plain send not gated (one-way best-effort)
}

TEST_CASE("§S6 — home-loss is PROBE-driven (the beacon-timeout rule is RETIRED): mobile_discover_fire never drops a homed mobile; k_miss unanswered probes do") {
    // The old max(90s, 2×beacon_ms) beacon-timeout home-lost rule is GONE. mobile_discover_fire is now a REGISTRATION-EVENT
    // entry only — calling it while homed is a no-op (never drops). Home-loss is detected by the presence probe cycle:
    // presence_probe_fire escalates and, after k_miss+1 unanswered probes, drops the home (mobile_reg{registered:false}).
    StubHal hal;   // _now = 0
    Node mob(hal, 0, 0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=4; cfg.is_mobile=true; cfg.beacon_period_ms=900000; CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/146, /*home_hash*/0x9292u);
    CHECK(mob.mobile_home_id() == 146);
    hal._now = 2000000;                           // way past any old beacon-timeout
    DualLayerTestAccess::mobile_discover_fire(mob);
    CHECK(mob.mobile_home_id() == 146);           // ★ mobile_discover_fire NO LONGER drops (retired beacon-timeout rule)
    // probe cycle with no roster answer: send k_miss+1 probes, then HOME LOST
    for (uint8_t i = 0; i <= protocol::presence_probe_k_miss + 1; ++i) { hal._now += 6000; DualLayerTestAccess::presence_probe_fire(mob); }
    CHECK(mob.mobile_home_id() == 0);             // ★ a genuine home-loss IS detected by the probe cycle (minutes)
}

TEST_CASE("§mobile — a registered mobile packs the DM INNER origin as its HOME (routable), not its node_id (the reverse-ack dest)") {
    // THE root-cause bug: stamp_origin set item.origin=home_id, but the inner was packed with _node_id -> the recipient
    // delivered on / E2E-acked to the mobile's static-invisible node_id (17) -> RREQ storm. The inner origin must be the
    // home (routable) so the ack reaches the home (which last-miles it).
    StubHal hal0; Node mob0(hal0, 0, 0x1717u);
    NodeConfig cfg0; cfg0.routing_sf=7; cfg0.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg0.leaf_id=4; cfg0.is_mobile=true; CHECK(mob0.on_init(cfg0));
    DualLayerTestAccess::make_registered_mobile(mob0, /*local*/17, /*home*/31, /*home_hash*/0x3131u);
    DualLayerTestAccess::learn_neighbor(mob0, 31);            // a route to the home (first hop)
    const uint8_t body0[2] = {'h','i'};
    DualLayerTestAccess::do_send(mob0, /*static dst*/70, body0, 2);
    const PendingTx* pt0 = DualLayerTestAccess::pending(mob0);
    CHECK(pt0 != nullptr);
    if (pt0) {
        CHECK(pt0->origin == 31);        // the RTS/billing origin was already home_id
        CHECK(pt0->inner[0] == 31);      // ★ the INNER origin is now the HOME too (was node_id 17) -> the ack is routable
    }
}

TEST_CASE("§mobile — the home-lost clock is refreshed by the home's CTS (not only its sparse beacon)") {
    StubHal hal;   // _now = 0
    Node mob(hal, 0, 0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=4; cfg.is_mobile=true; CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/146, /*home_hash*/0x9292u);   // last_heard_home_ms = 0
    CHECK(DualLayerTestAccess::mobile_last_heard_home(mob) == 0);
    hal._now = 500000;
    cts_in c{}; c.tx_id = 146; c.rx_id = 17; c.chosen_data_sf = 7; c.payload_len = 2;   // a CTS from the HOME clearing OUR flight
    uint8_t buf[8]; size_t n = pack_cts(c, buf);
    mob.on_recv(buf, n, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(DualLayerTestAccess::mobile_last_heard_home(mob) == 500000);   // ★ refreshed by the CTS, no beacon needed
    // a CTS from a NON-home id does NOT refresh
    hal._now = 900000;
    cts_in c2{}; c2.tx_id = 70; c2.rx_id = 17; c2.chosen_data_sf = 7; c2.payload_len = 2;
    uint8_t buf2[8]; size_t n2 = pack_cts(c2, buf2);
    mob.on_recv(buf2, n2, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(DualLayerTestAccess::mobile_last_heard_home(mob) == 500000);   // ★ unchanged (not our home)
}

TEST_CASE("§mobile 4a — mobile_home cache: freshest-epoch wins (wrap-aware)") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; CHECK(n.on_init(cfg));
    n.mobile_home_set(0xAAAAu, 11, 5); CHECK(n.mobile_home_find(0xAAAAu) == 11);
    n.mobile_home_set(0xAAAAu, 12, 6); CHECK(n.mobile_home_find(0xAAAAu) == 12);   // fresher epoch -> re-home
    n.mobile_home_set(0xAAAAu, 13, 4); CHECK(n.mobile_home_find(0xAAAAu) == 12);   // stale (older epoch, diff home) -> ignored
    n.mobile_home_set(0xBBBBu, 11, 255); CHECK(n.mobile_home_find(0xBBBBu) == 11); // fresh hash
    n.mobile_home_set(0xBBBBu, 12, 0);   CHECK(n.mobile_home_find(0xBBBBu) == 12); // ★ wrap: epoch 0 is fresher than 255
}

TEST_CASE("§mobile 4a — MOBILE_H_ANSWER caches M->home+epoch with NO id_bind; a plain H_ANSWER id_binds + no cache") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; CHECK(n.on_init(cfg));
    // a MOBILE_H_ANSWER: M1=0xB0B1 -> home 30, epoch 7
    hash_bind_inner hb{}; hb.target_layer=0; hb.node_id=30; hb.key_hash32=0xB0B1u; hb.epoch=7;
    uint8_t inner[7]; size_t il = pack_hash_bind_inner(hb, inner, /*mobile=*/true);
    const uint16_t ib0 = n.id_bind_count();
    n.on_mobile_hash_bind_response(inner, static_cast<uint8_t>(il));
    CHECK(n.mobile_home_find(0xB0B1u) == 30);       // ★ cached M->home
    CHECK(n.id_bind_count() == ib0);                // ★ NO id_bind (a mobile proxy stays out of id_bind -> no repeat hard-verify)
    // a plain H_ANSWER: M2=0xC0C0 -> 40 -> the id_bind path, NO cache (the 3c heuristic is gone)
    hash_bind_inner hb2{}; hb2.target_layer=0; hb2.node_id=40; hb2.key_hash32=0xC0C0u;
    uint8_t inner2[6]; size_t il2 = pack_hash_bind_inner(hb2, inner2, /*mobile=*/false);
    n.on_hash_bind_response(inner2, static_cast<uint8_t>(il2), /*authoritative=*/false);
    CHECK(n.id_bind_count() == ib0 + 1);            // ★ the plain H_ANSWER DID id_bind (40->M2)
    CHECK(n.mobile_home_find(0xC0C0u) == -1);       // ★ and did NOT populate the mobile-home cache (heuristic retired)
}

TEST_CASE("§mobile 4a — a host proxying a hosted mobile emits DATA_TYPE_MOBILE_H_ANSWER + the epoch") {
    StubHal hal; Node host(hal, 30, 0x3030u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; CHECK(host.on_init(cfg));
    DualLayerTestAccess::store_mobile(host, /*key*/0xB0B1u, /*local*/17);   // store_mobile records epoch=1
    DualLayerTestAccess::learn_neighbor(host, 50);                          // a route to the querier so the answer flies
    std::array<uint8_t,8> hbuf{};
    size_t hn = pack_h({/*leaf*/4, /*origin*/50, /*key*/0xB0B1u, /*ttl*/3, /*hard*/false}, hbuf);
    host.on_recv(hbuf.data(), hn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(host);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER);   // ★ the distinct mobile-proxy TYPE (not a plain H_ANSWER)
        auto o = parse_hash_bind_inner(std::span<const uint8_t>(pt->inner, pt->inner_len));
        CHECK(o.has_value());
        if (o) { CHECK(o->node_id == 30); CHECK(o->key_hash32 == 0xB0B1u); CHECK(o->epoch == 1); }   // M -> home(30), epoch 1
    }
}

TEST_CASE("§mobile hash-locate Fix 1/2 — a registered mobile is SILENT on a static HARD locate for its own hash; the home proxies it (HARD too)") {
    // (a) the MOBILE: a HARD static locate for its OWN hash -> IGNORED — no answer (local id off the static plane) AND no relay (a mobile is a leaf, not a flood relay). The HOME is the location authority.
    StubHal hm; Node mob(hm, 17, 0xB0B1u);
    NodeConfig mc; mc.routing_sf=8; mc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); mc.leaf_id=4; mc.is_mobile=true; CHECK(mob.on_init(mc));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/30, /*home_hash*/0x3030u);
    DualLayerTestAccess::learn_neighbor(mob, 50);                 // a route so an answer COULD fly -> proves the silence is a choice, not a dead link
    h_in q{}; q.leaf_id=4; q.origin=50; q.key_hash32=0xB0B1u; q.ttl=3; q.hard=true;   // ★ HARD (an e2e_ack_req locate drives this)
    std::array<uint8_t,16> qb{}; size_t qn = pack_h(q, std::span<uint8_t>(qb.data(), qb.size()));
    mob.on_recv(qb.data(), qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(hm.saw_emit("h_resolved"));                       // ★ Fix 1: a registered mobile skips its own-hash resolution on the static plane
    CHECK_FALSE(hm.saw_emit("h_forward"));                        // ★ 2026-07-11: a mobile does NOT relay a static H-flood either (leaf) -> the HOME hears it via static nodes + proxies

    // (b) the HOME hosting M (live) answers the SAME HARD locate with a MOBILE_H_ANSWER(home_id) — Fix 2 dropped the old !h.hard gate
    StubHal hh; Node home(hh, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);   // last_heard = now -> live
    DualLayerTestAccess::learn_neighbor(home, 50);
    home.on_recv(qb.data(), qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER);            // ★ proxy answer -> resolves to the HOME, never the local id 17
        auto o = parse_hash_bind_inner(std::span<const uint8_t>(pt->inner, pt->inner_len));
        CHECK(o.has_value());
        if (o) { CHECK(o->node_id == 30); CHECK(o->key_hash32 == 0xB0B1u); }
    }
}

TEST_CASE("§mobile hash-locate Fix 2 — the home proxies a HARD locate ONLY while the mobile is live (liveness gate, not a black hole)") {
    StubHal hal; Node home(hal, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);   // last_heard = now(=0)
    DualLayerTestAccess::learn_neighbor(home, 50);
    // (a) STALE: advance the clock past mobile_liveness_ms -> NO proxy answer (node_id stays -1 -> forward -> the locate times out = "unreachable")
    hal._now = protocol::mobile_liveness_ms + 1;
    h_in q{}; q.leaf_id=4; q.origin=50; q.key_hash32=0xB0B1u; q.ttl=3; q.hard=true;
    std::array<uint8_t,16> qb{}; size_t qn = pack_h(q, std::span<uint8_t>(qb.data(), qb.size()));
    home.on_recv(qb.data(), qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(hal.saw_emit("h_resolved"));
    CHECK(hal.saw_emit("h_forward"));
    CHECK(DualLayerTestAccess::pending(home) == nullptr);        // no MOBILE_H_ANSWER queued
    // (b) refresh last_heard (a re-CLAIM heartbeat) -> the proxy answers again (fresh origin sidesteps flood-dedup; give it a route so the answer can promote)
    hal.emits.clear();
    DualLayerTestAccess::set_mobile_last_heard(home, 0, hal._now);
    DualLayerTestAccess::learn_neighbor(home, 51);
    h_in q2 = q; q2.origin = 51;
    std::array<uint8_t,16> qb2{}; size_t qn2 = pack_h(q2, std::span<uint8_t>(qb2.data(), qb2.size()));
    home.on_recv(qb2.data(), qn2, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER);        // ★ live again -> proxy resumes
}

TEST_CASE("§mobile hash-locate liveness — a hosted mobile's BEACON refreshes the proxy clock (a STATIONARY mobile never black-holes)") {
    // Regression for the black-hole: a homed mobile that stays put never re-CLAIMs (the discover timer short-circuits while the
    // home is heard), so CLAIM would be the ONLY last_heard_ms write -> the proxy goes stale ~mobile_liveness_ms later even
    // though the mobile is alive. The mobile's periodic BEACON (beacon_period_ms < mobile_liveness_ms) must refresh it.
    StubHal hal; Node home(hal, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);   // last_heard = now(=0)
    DualLayerTestAccess::learn_neighbor(home, 50);
    hal._now = protocol::mobile_liveness_ms + 100;                        // age past liveness -> WITHOUT a refresh the proxy is stale
    // the hosted mobile beacons (key_hash32=M). ★ config_hash is DIVERGENT (a mobile adopts only the host's PHY, not its
    // config-plane) -> the beacon must survive the R6.1 membership filter via the is_mobile exemption to reach the refresh.
    beacon_in bin{}; bin.leaf_id=4; bin.src=17; bin.key_hash32=0xB0B1u; bin.is_mobile=true; bin.config_hash=0xDEADBEEFu;
    std::array<uint8_t,64> bb{}; size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    home.on_recv(bb.data(), bn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    // a HARD locate now RESOLVES (the beacon proved liveness) -> a MOBILE_H_ANSWER, not "unreachable"
    h_in q{}; q.leaf_id=4; q.origin=50; q.key_hash32=0xB0B1u; q.ttl=3; q.hard=true;
    std::array<uint8_t,16> qb{}; size_t qn = pack_h(q, std::span<uint8_t>(qb.data(), qb.size()));
    home.on_recv(qb.data(), qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER);
    // a mobile beacon for a DIFFERENT hash does NOT refresh M (no false liveness)
    StubHal h2; Node home2(h2, 30, 0x3030u); CHECK(home2.on_init(hc));
    DualLayerTestAccess::store_mobile(home2, 0xB0B1u, 17);
    h2._now = protocol::mobile_liveness_ms + 100;
    beacon_in other{}; other.leaf_id=4; other.src=18; other.key_hash32=0xC0C0u; other.is_mobile=true; other.config_hash=0xDEADBEEFu;   // some OTHER mobile
    std::array<uint8_t,64> ob{}; size_t obn = pack_beacon(other, std::span<uint8_t>(ob.data(), ob.size()));
    home2.on_recv(ob.data(), obn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    home2.on_recv(qb.data(), qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK(DualLayerTestAccess::pending(home2) == nullptr);               // ★ M still stale (the other mobile's beacon didn't refresh it)
    CHECK(h2.saw_emit("h_forward"));
}

TEST_CASE("§mobile hash-locate Fix 1/1b — a TEAM-scoped locate gets the mobile's authoritative answer; a wrong/absent team_id stays silent") {
    StubHal hal; Node mob(hal, 17, 0xB0B1u);
    NodeConfig mc; mc.routing_sf=8; mc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); mc.leaf_id=4; mc.is_mobile=true; mc.team_id=0xABCD1234u; CHECK(mob.on_init(mc));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/30, /*home_hash*/0x3030u);
    DualLayerTestAccess::learn_neighbor(mob, 50);
    // team-scoped locate for the mobile's own hash, MATCHING team_id -> the mobile IS the endpoint on the team plane -> answers AUTHORITATIVELY with its local id
    h_in tq{}; tq.leaf_id=4; tq.origin=50; tq.key_hash32=0xB0B1u; tq.ttl=3; tq.hard=true; tq.team_scoped=true; tq.team_id=0xABCD1234u;
    std::array<uint8_t,16> tb{}; size_t tn = pack_h(tq, std::span<uint8_t>(tb.data(), tb.size()));
    mob.on_recv(tb.data(), tn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK(hal.saw_emit("h_resolved"));
    const PendingTx* pt = DualLayerTestAccess::pending(mob);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_AUTHORITATIVE_H_ANSWER);    // ★ authoritative binding to the LOCAL id (teammates route to it via the 6.2 team table)
        auto o = parse_hash_bind_inner(std::span<const uint8_t>(pt->inner, pt->inner_len));
        CHECK(o.has_value());
        if (o) CHECK(o->node_id == 17);
    }
    // a WRONG team_id -> a team-scoped H is TEAM-plane-only (spec 2026-07-15 §2 separation): a non-member DROPS it before any
    // answer OR forward, so team traffic never rides the static plane. (fresh origin sidesteps dedup.)
    hal.emits.clear();
    h_in wq = tq; wq.origin=51; wq.team_id=0x00009999u;
    std::array<uint8_t,16> wb{}; size_t wn = pack_h(wq, std::span<uint8_t>(wb.data(), wb.size()));
    mob.on_recv(wb.data(), wn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(hal.saw_emit("h_resolved"));                     // ★ wrong team -> not our locate -> no answer
    CHECK_FALSE(hal.saw_emit("h_forward"));                      // ★ §2 separation: a wrong-team node DROPS a team H (does NOT re-flood it onto the static plane)
    // a pure STATIC node (team_id==0) likewise DROPS a team-scoped H — the s24 assertion-2 axis at unit scope.
    StubHal hs; Node st(hs, 40, 0x4040u);
    NodeConfig sc; sc.routing_sf=8; sc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); sc.leaf_id=4; CHECK(st.on_init(sc));   // is_mobile=false, team_id=0
    DualLayerTestAccess::learn_neighbor(st, 50);
    h_in sq = tq; sq.origin=52; sq.key_hash32=0x9AA9u;           // a team locate the static node can neither own nor answer
    std::array<uint8_t,16> sb{}; size_t sn = pack_h(sq, std::span<uint8_t>(sb.data(), sb.size()));
    st.on_recv(sb.data(), sn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(hs.saw_emit("h_forward"));                       // ★ a static node never relays a team-scoped H
    CHECK_FALSE(hs.saw_emit("h_resolved"));
}

TEST_CASE("§S6 — a home caches a hosted mobile's key from the PROBE's HAS_PUBKEY block (hash-verified); rejects an inconsistent key + a non-hosted M") {
    // §S6 A.4: key custody rides the presence probe (RETIRES the TYPE-12 push). presence_ingest_probe caches ed_pub on
    // the hosted mobile's registry entry iff ed_pub[:4] LE == key_hash32 (self-consistency), then schedules a roster whose
    // has_key bit confirms custody. Same self-check + host-gating the old push handler had.
    const RxMeta m8{8.0f, -80.0f, 0, static_cast<int8_t>(-1)};
    auto mk_probe = [](uint32_t key, const uint8_t ed[32], std::array<uint8_t,42>& buf) -> size_t {
        p_probe_in p{}; p.selected_home_id = 30; p.selected_home_layer = 4;   // rev2: a CHECK probe selecting home 30 (else searching -> no key cache)
        p.key_hash32 = key; p.reg_epoch = 0; p.has_pubkey = true; for (int i=0;i<32;++i) p.ed_pub[i]=ed[i];
        return pack_p_probe(p, std::span<uint8_t>(buf.data(), buf.size()));
    };
    StubHal hal; Node home(hal, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);
    uint8_t ed[32] = {}; ed[0]=0xB1; ed[1]=0xB0; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(i);   // ed_pub[:4] LE == 0x0000B0B1 == M
    std::array<uint8_t,42> pb{}; size_t pn = mk_probe(0xB0B1u, ed, pb);
    DualLayerTestAccess::presence_ingest_probe(home, pb.data(), pn, m8);
    CHECK(DualLayerTestAccess::mobile_reg_has_pubkey(home, 0));
    { const uint8_t* c = DualLayerTestAccess::mobile_reg_ed_pub(home, 0); bool same=true; for (int i=0;i<32;++i) if (c[i]!=ed[i]) same=false; CHECK(same); }
    // an INCONSISTENT key (ed_pub[:4] != key_hash32) -> rejected
    StubHal h2; Node home2(h2, 30, 0x3030u); CHECK(home2.on_init(hc));
    DualLayerTestAccess::store_mobile(home2, 0xB0B1u, 17);
    uint8_t bad[32] = {}; bad[0]=0xFF; bad[1]=0xFF;   // hashes to 0x0000FFFF != 0xB0B1
    std::array<uint8_t,42> bb{}; size_t bn = mk_probe(0xB0B1u, bad, bb);
    DualLayerTestAccess::presence_ingest_probe(home2, bb.data(), bn, m8);
    CHECK_FALSE(DualLayerTestAccess::mobile_reg_has_pubkey(home2, 0));   // ★ rejected
    // a probe for a NON-hosted M -> dropped (empty registry -> cheap return)
    StubHal h3; Node home3(h3, 30, 0x3030u); CHECK(home3.on_init(hc));
    std::array<uint8_t,42> nb{}; size_t nn = mk_probe(0xB0B1u, ed, nb);
    DualLayerTestAccess::presence_ingest_probe(home3, nb.data(), nn, m8);
    CHECK(DualLayerTestAccess::mobile_reg_n(home3) == 0);   // ★ non-host: no cache, byte-identical
}

TEST_CASE("§mobile Part 2 Fix 7 — a home answers a WANT_PUBKEY locate for its LIVE mobile with a MOBILE_H_ANSWER_PUBKEY; silent without the key") {
    StubHal hal; Node home(hal, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    // ★ the home is CRYPTO-READY with its OWN key -> if Fix 7 didn't precede the owner branch, a live proxy (node_id==_node_id) would fall into it and leak the HOME's key (TYPE 5). The TYPE-13 + mobile-ed assertions below catch that reordering regression.
    uint8_t hxs[32]; for (int i=0;i<32;++i) hxs[i]=static_cast<uint8_t>(0x80+i);
    uint8_t hed[32] = {}; hed[0]=0x30; hed[1]=0x30; for (int i=4;i<32;++i) hed[i]=static_cast<uint8_t>(0xC0);   // home ed_pub[:4]==0x3030 (the home's key), distinct from the mobile's
    home.set_crypto_identity(hxs, hed);
    CHECK(home.crypto_ready());
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);
    DualLayerTestAccess::learn_neighbor(home, 50);
    uint8_t ed[32] = {}; ed[0]=0xB1; ed[1]=0xB0; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(0x40+i);
    // (a) NO cached key yet -> a WANT_PUBKEY HARD locate gets NO answer (silent; the sender's reqpubkey retries)
    h_in wq{}; wq.leaf_id=4; wq.origin=50; wq.key_hash32=0xB0B1u; wq.ttl=3; wq.hard=true; wq.want_pubkey=true;
    std::array<uint8_t,48> wb{}; size_t wn = pack_h(wq, std::span<uint8_t>(wb.data(), wb.size()));
    home.on_recv(wb.data(), wn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK(DualLayerTestAccess::pending(home) == nullptr);        // ★ no answer without the key
    CHECK_FALSE(hal.saw_emit("mobile_pubkey_answer_tx"));
    // (b) cache the key (a Fix 6 push) -> the SAME locate now gets the pubkey answer (fresh origin sidesteps dedup)
    DualLayerTestAccess::set_mobile_pubkey(home, 0, ed);
    DualLayerTestAccess::learn_neighbor(home, 51);
    h_in wq2 = wq; wq2.origin = 51;
    std::array<uint8_t,48> wb2{}; size_t wn2 = pack_h(wq2, std::span<uint8_t>(wb2.data(), wb2.size()));
    home.on_recv(wb2.data(), wn2, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER_PUBKEY);
        CHECK(pt->inner_len == 7 + 32 + 1);   // §1.3: + the appended [name_len] byte (this hosted mobile has no name -> name_len=0, no name bytes)
        auto o = parse_hash_bind_inner(std::span<const uint8_t>(pt->inner, 7));
        CHECK(o.has_value());
        if (o) { CHECK(o->node_id == 30); CHECK(o->key_hash32 == 0xB0B1u); }   // ★ home routing
        bool same=true; for (int i=0;i<32;++i) if (pt->inner[7+i]!=ed[i]) same=false; CHECK(same);   // ★ carries the mobile's ed_pub
    }
}

TEST_CASE("§mobile Part 2 — a STALE (redirect) home FORWARDS a WANT_PUBKEY locate (to the new home) instead of answering/suppressing; a PLAIN locate still redirects") {
    StubHal hal; Node home(hal, 30, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(home.on_init(hc));
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);
    DualLayerTestAccess::learn_neighbor(home, 50);
    DualLayerTestAccess::drive_post_ack_breadcrumb(home, /*source_hash*/0xB0B1u, /*new_home*/99, /*epoch*/7, /*new_home_layer*/4);   // M re-homed to 99 -> this home is now STALE (redirect)
    CHECK(DualLayerTestAccess::mobile_reg_redirect(home, 0) == 99);
    // (a) a WANT_PUBKEY locate: the stale home holds NO key -> FORWARD the flood (so it reaches the NEW home which cached the key), do NOT answer/suppress -> no black hole
    h_in wq{}; wq.leaf_id=4; wq.origin=50; wq.key_hash32=0xB0B1u; wq.ttl=3; wq.hard=true; wq.want_pubkey=true;
    std::array<uint8_t,48> wb{}; size_t wn = pack_h(wq, std::span<uint8_t>(wb.data(), wb.size()));
    home.on_recv(wb.data(), wn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK(hal.saw_emit("h_forward"));                            // ★ forwarded on to the new home
    CHECK_FALSE(hal.saw_emit("mobile_pubkey_answer_tx"));
    CHECK(DualLayerTestAccess::pending(home) == nullptr);        // ★ no keyless answer queued
    // (b) a PLAIN (location) locate still gets the redirect answer -> new home 99 (unchanged; fresh origin sidesteps dedup)
    hal.emits.clear();
    DualLayerTestAccess::learn_neighbor(home, 51);
    h_in pq{}; pq.leaf_id=4; pq.origin=51; pq.key_hash32=0xB0B1u; pq.ttl=3; pq.hard=false;
    std::array<uint8_t,16> pb{}; size_t pn = pack_h(pq, std::span<uint8_t>(pb.data(), pb.size()));
    home.on_recv(pb.data(), pn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER);
        auto o = parse_hash_bind_inner(std::span<const uint8_t>(pt->inner, pt->inner_len));
        CHECK(o.has_value());
        if (o) CHECK(o->node_id == 99);                         // ★ redirect -> the new home
    }
}

TEST_CASE("§mobile Part 2 Fix 8 — a MOBILE_H_ANSWER_PUBKEY caches peer_key(M) + M->home, NEVER an id_bind to the local id") {
    StubHal hal; Node sender(hal, 7, 0x0707u);
    NodeConfig sc; sc.routing_sf=8; sc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); sc.leaf_id=4; CHECK(sender.on_init(sc));
    uint8_t ed[32] = {}; ed[0]=0xB1; ed[1]=0xB0; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(0x20+i);
    // build the answer inner: [7B hash_bind: layer=4, home=30, hash=M, epoch=2][ed_pub 32]
    hash_bind_inner hb{}; hb.target_layer=4; hb.node_id=30; hb.key_hash32=0xB0B1u; hb.epoch=2;
    uint8_t inner[7+32]; size_t n = pack_hash_bind_inner(hb, std::span<uint8_t>(inner,7), /*mobile=*/true);
    for (int i=0;i<32;++i) inner[n+i]=ed[i];
    sender.on_mobile_hash_bind_pubkey_response(inner, static_cast<uint8_t>(n+32));
    // peer_key[M] cached authoritative
    uint8_t got[32]; Node::PeerKeyConf conf;
    CHECK(sender.peer_key_find(0xB0B1u, got, &conf));
    { bool same=true; for (int i=0;i<32;++i) if (got[i]!=ed[i]) same=false; CHECK(same); }
    CHECK(static_cast<uint8_t>(conf) >= static_cast<uint8_t>(Node::PeerKeyConf::authoritative));
    // M -> home(30, layer 4) cached; and NO id_bind to a local id
    uint8_t hl=0; CHECK(sender.mobile_home_find(0xB0B1u, &hl) == 30);
    CHECK(hl == 4);
    CHECK(sender.id_bind_find_by_hash(0xB0B1u) == -1);   // ★ never id_bind'd -> the local id stays off the sender's static plane
}

TEST_CASE("§mobile Part 2 Fix 6 — a mobile with a crypto identity pushes its ed_pub to the (new) home on adopt; no identity -> no push") {
    StubHal hal; Node mob(hal, 0, 0x9999u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; CHECK(mob.on_init(cfg));
    uint8_t xs[32]; for (int i=0;i<32;++i) xs[i]=static_cast<uint8_t>(i+1);
    uint8_t ed[32] = {}; ed[0]=0x99; ed[1]=0x99; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(i);   // ed_pub[:4] == 0x00009999 == the mobile's key
    mob.set_crypto_identity(xs, ed);
    CHECK(mob.crypto_ready());
    j_offer_in off{}; off.leaf_id=4; off.is_mobile=true; off.responder_node_id=40; off.responder_key_hash32=0x4040u; off.data_sf_bitmap=0x06; off.proposed_mobile_id=21;
    off.target_key_hash32 = mob.key_hash32(); uint8_t ob[13]; size_t on = pack_j_offer(off, ob); mob.on_recv(ob, on, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    hal.emits.clear();
    mob.on_timer(75 /*kMobileClaimGuardTimerId*/);
    CHECK_FALSE(hal.saw_emit("mobile_pubkey_push"));   // ★ §S6: the TYPE-12 push is RETIRED — no push on adopt
    // §S6 A.4: the key now rides the FIRST presence probe (HAS_PUBKEY). Firing the probe timer emits a probe.
    hal.emits.clear();
    mob.on_timer(78 /*kPresenceProbeTimerId*/);
    CHECK(hal.saw_emit("presence_probe_tx"));          // ★ the crypto mobile probes (carrying its key until the roster confirms)
    // a keyless mobile still probes, just without the key block (byte-identical presence, no crypto)
    StubHal h2; Node mob2(h2, 0, 0x8888u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; c2.is_mobile=true; CHECK(mob2.on_init(c2));
    j_offer_in of2{}; of2.leaf_id=4; of2.is_mobile=true; of2.responder_node_id=40; of2.responder_key_hash32=0x4040u; of2.data_sf_bitmap=0x06; of2.proposed_mobile_id=22;
    of2.target_key_hash32 = mob2.key_hash32(); uint8_t ob2[13]; size_t on2 = pack_j_offer(of2, ob2); mob2.on_recv(ob2, on2, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    h2.emits.clear();
    mob2.on_timer(75);
    CHECK_FALSE(h2.saw_emit("mobile_pubkey_push"));    // ★ no push path exists any more
    h2.emits.clear();
    mob2.on_timer(78);
    CHECK(h2.saw_emit("presence_probe_tx"));           // ★ keyless mobile still probes (presence works without crypto)
}

TEST_CASE("§mobile Wave 2 — the owner's WANT_PUBKEY answer to a MOBILE requester is a DST_HASH DM (routes to origin=the mobile's home, which last-miles it — NOT the unroutable mobile local id)") {
    StubHal hal; Node owner(hal, /*id=*/155, /*key=*/0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(owner.on_init(hc));
    uint8_t xs[32]; for (int i=0;i<32;++i) xs[i]=static_cast<uint8_t>(0x80+i);
    uint8_t ed[32] = {}; ed[0]=0x30; ed[1]=0x30; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(0xC0);   // ed_pub[:4]==0x3030 == owner's key
    owner.set_crypto_identity(xs, ed);
    DualLayerTestAccess::learn_neighbor(owner, 99);                              // a route to the mobile's home (99)
    // a MOBILE requester (mobile_req=1), origin=99 (its home), requesting the owner's (0x3030) pubkey
    uint8_t red[32] = {}; red[0]=0xCD; red[1]=0xAB; for (int i=4;i<32;++i) red[i]=static_cast<uint8_t>(i);   // requester key 0xABCD (the mobile)
    h_in wq{}; wq.leaf_id=4; wq.origin=99; wq.key_hash32=0x3030u; wq.ttl=3; wq.hard=true; wq.want_pubkey=true; wq.mobile_req=true;
    for (int i=0;i<32;++i) wq.requester_ed_pub[i]=red[i];
    std::array<uint8_t,48> wb{}; size_t wn = pack_h(wq, std::span<uint8_t>(wb.data(), wb.size()));
    owner.on_recv(wb.data(), wn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(owner);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY);
        CHECK((pt->flags & DATA_FLAG_DST_HASH) != 0);                            // ★ DST_HASH set (=the mobile) -> routes to origin(=home 99) + the home last-miles
        CHECK(pt->next == 99);                                                   // ★ addressed to the home, not the mobile's local id
    }
}

TEST_CASE("§name — a WANT_PUBKEY H carrying the requester's name -> the OWNER caches hash->name (peer_name_find), WITH the pubkey (symmetric to the answer frames)") {
    StubHal hal; Node owner(hal, /*id=*/155, /*key=*/0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; CHECK(owner.on_init(hc));
    uint8_t xs[32]; for (int i=0;i<32;++i) xs[i]=static_cast<uint8_t>(0x80+i);
    uint8_t ed[32] = {}; ed[0]=0x30; ed[1]=0x30; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(0xC0);   // owner's own key 0x3030
    owner.set_crypto_identity(xs, ed);
    // a requester with key 0xABCD (ed[:4] LE) + name "Rover"
    uint8_t red[32] = {}; red[0]=0xCD; red[1]=0xAB; for (int i=4;i<32;++i) red[i]=static_cast<uint8_t>(i);
    h_in wq{}; wq.leaf_id=4; wq.origin=99; wq.key_hash32=0x3030u; wq.ttl=3; wq.hard=true; wq.want_pubkey=true;
    for (int i=0;i<32;++i) wq.requester_ed_pub[i]=red[i];
    const char* nm = "Rover"; wq.name_len=5; for (int i=0;i<5;++i) wq.name[i]=static_cast<uint8_t>(nm[i]);
    std::array<uint8_t,64> wb{}; size_t wn = pack_h(wq, std::span<uint8_t>(wb.data(), wb.size()));
    CHECK(wn > 40);                                                          // the H carries the appended name
    owner.on_recv(wb.data(), wn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    char out[32]; uint8_t on = owner.peer_name_find(0xABCDu, out, sizeof out);
    CHECK(std::string(out, on) == "Rover");                                  // ★ the owner cached the requester's NAME alongside its pubkey
}

TEST_CASE("§mobile step 1 — a REGISTERED mobile's plain GLOBAL reqpubkey DOES flood, stamping origin=home_id (routable) — the step-2 guard suppresses ONLY the no-return-path (unregistered) case") {
    StubHal hal; Node m(hal, /*id=*/17, /*key=*/0x1111u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; CHECK(m.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(m, /*local_id=*/17, /*home_id=*/138, /*home_hash=*/0xBEEFu);
    uint8_t xs[32], ed[32]; for (int i=0;i<32;++i){ xs[i]=static_cast<uint8_t>(0x40+i); ed[i]=static_cast<uint8_t>(0x11+i); }
    m.set_crypto_identity(xs, ed);
    Command cg{}; cg.kind = CmdKind::reqpubkey; cg.u.resolve.dst_hash = 0x2222u; cg.u.resolve.plane = 2 /*GLOBAL*/;
    m.on_command(cg);
    CHECK(hal.saw_emit("h_tx"));                                          // ★ NOT suppressed — a registered mobile has a return path (its home)
    auto ph = parse_h(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(ph.has_value());
    if (ph) { CHECK(ph->want_pubkey); CHECK(ph->mobile_req); CHECK_FALSE(ph->team_scoped); CHECK(ph->origin == 138); }   // ★ origin=home_id 138 (routable), NOT the mobile's LOCAL id 17
}

TEST_CASE("§mobile Part 2 / Wave 2 — a HOME reqpubkey'ing its OWN hosted mobile resolves the pubkey from _mobile_reg (no H flood; the home can then seal to it)") {
    StubHal hal; Node home(hal, /*id=*/155, /*key=*/0x9999u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; CHECK(home.on_init(cfg));
    uint8_t ed[32] = {}; ed[0]=0xB6; ed[1]=0x31; ed[2]=0x09; ed[3]=0xCD; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(i);  // ed_pub[:4] LE == the mobile's key_hash32
    const uint32_t mhash = 0xCD0931B6u;
    DualLayerTestAccess::store_mobile(home, mhash, 40);               // host the mobile @ local id 40
    DualLayerTestAccess::set_mobile_pubkey(home, 0, ed);             // the mobile pushed its pubkey
    Command c{}; c.kind = CmdKind::reqpubkey; c.u.resolve.dst_hash = mhash; c.u.resolve.plane = 2 /*GLOBAL*/;
    home.on_command(c);
    CHECK_FALSE(hal.saw_emit("h_tx"));                                // ★ NO H flood
    CHECK(hal.saw_emit("peer_key_cached"));                           // ★ resolved locally from _mobile_reg
    uint8_t pk[32]; CHECK(home.peer_key_find(mhash, pk));            // ★ the home can now seal to its hosted mobile
}

TEST_CASE("§mobile delegate — a registered mobile's send_by_hash hands off to its HOME (MOBILE_SEND), it does NOT self-hash-locate (no RREQ storm)") {
    StubHal hal; Node mob(hal, 0, 0xAAAAu);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=4; cfg.is_mobile=true; CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/31, /*home_hash*/0x3131u);
    const uint8_t body[] = { 'h','i' };
    DualLayerTestAccess::send_by_hash(mob, /*target_hash=*/0x8CC9BDFFu, body, 2);
    // ★ the send is wrapped as a MOBILE_SEND DM to the HOME (31) — the mobile never emits an H query (origin=local id) that
    //   would trigger the RREQ storm. The home resolves + re-originates (do_post_ack fork), reply returns by SOURCE_HASH.
    const meshroute::PendingTx* pt = DualLayerTestAccess::pending(mob);
    CHECK(pt != nullptr);
    if (pt) { CHECK(pt->dst == 31); CHECK(pt->type == DATA_TYPE_MOBILE_SEND); }
}

TEST_CASE("§mobile — a host NEVER RREQs a hosted mobile's LOCAL id (last-mile, not route discovery); a non-hosted id DOES") {
    StubHal hal; Node host(hal, 31, 0x3131u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=4; CHECK(host.on_init(cfg));
    DualLayerTestAccess::store_mobile(host, /*key*/0x1717u, /*local*/17);       // host hosts mobile 0x1717 -> local id 17
    hal.last_tx_len = 0;
    DualLayerTestAccess::emit_rreq(host, /*dst=*/17);                            // route-discovery for the hosted mobile's LOCAL id
    CHECK(hal.last_tx_len == 0);                                                 // ★ NO F/RREQ emitted (the storm guard)
    DualLayerTestAccess::emit_rreq(host, /*dst=*/50);                            // a NON-hosted id -> discovery proceeds
    CHECK(hal.last_tx_len > 0);                                                  // control: proves the guard is scoped to hosted mobiles
}

TEST_CASE("§mobile 6.4 — team-DAD self-assigns a persistent _team_local_id (no host); the guard confirms; a non-team node is a no-op") {
    StubHal hal; Node mob(hal, 0, 0xAAAAu);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
    CHECK(mob.team_local_id() == 0);
    mob.team_dad_fire();
    CHECK(mob.team_local_id() >= 17);                         // ★ self-assigned a normal team-plane id, NO static host
    CHECK(hal.armed[77]);                                     // the guard window (kTeamDadGuardTimerId=77) is armed
    CHECK(hal.saw_emit("team_dad_claim"));
    // ★ the claim beacon is EMITTED NOW (src=_team_local_id), not deferred to a triggered-beacon (which is a mobile no-op)
    { auto cb = parse_beacon(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
      CHECK(cb.has_value()); if (cb) CHECK(cb->src == mob.team_local_id()); }
    hal.emits.clear();
    mob.on_timer(77);
    CHECK(hal.saw_emit("team_dad_adopted"));                  // ★ no same-team conflict during the window -> confirmed
    // a non-team node -> team_dad_fire is a no-op
    StubHal h2; Node stat(h2, 5, 0x5555u); NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; CHECK(stat.on_init(c2));
    stat.team_dad_fire();
    CHECK(stat.team_local_id() == 0);
}

TEST_CASE("§team-multihop Plane 2 — a team-scoped F: a same-team member processes it on _rt_team; a STATIC node + a WRONG-team member DROP it (full separation)") {
    const uint32_t T = 0x12345678u;
    // A same-team member team-DADs an id, then receives a team RREQ TARGETING it -> answers on the TEAM plane; the reverse
    // route lands in _rt_team, NEVER the static _rt (the §18 separation).
    StubHal hb; Node b(hb, 0, 0xB0B0u);
    NodeConfig bc; bc.routing_sf=8; bc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); bc.leaf_id=4; bc.is_mobile=true; bc.team_id=T; CHECK(b.on_init(bc));
    b.team_dad_fire();
    const uint8_t tid = b.team_local_id();
    CHECK(tid >= 17);
    f_in tf{}; tf.leaf_id=4; tf.origin=201; tf.is_reply=false; tf.dst_id=tid; tf.ttl_or_next_hop=8; tf.hops=0; tf.relay=201;
    tf.config_hash=0; tf.team_scoped=true; tf.team_id=T;
    std::array<uint8_t,13> buf{}; const size_t n = pack_f(tf, std::span<uint8_t>(buf.data(), buf.size())); CHECK(n == 13);
    hb.emits.clear();
    b.on_recv(buf.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK(hb.saw_emit("rreq_resolved_self"));                    // ★ B is the target -> a team RREP
    CHECK(DualLayerTestAccess::is_team_peer(b, 201));            // ★ reverse route learned on the TEAM plane (_rt_team)
    CHECK_FALSE(DualLayerTestAccess::rt_has_global(b, 201));     // ★ SEPARATION: the team F never wrote the static _rt (GLOBAL-forced)
    // (1) a STATIC node (team_id==0) DROPS the same team F — no processing, no static route.
    StubHal hs; Node st(hs, 40, 0x4040u);
    NodeConfig sc; sc.routing_sf=8; sc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); sc.leaf_id=4; CHECK(st.on_init(sc));
    st.on_recv(buf.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(hs.saw_emit("rreq_rx")); CHECK_FALSE(hs.saw_emit("rreq_resolved_self"));
    CHECK_FALSE(DualLayerTestAccess::rt_has(st, 201));          // ★ a team F never touches a static node's _rt
    // (2) a WRONG-team member (team_id != T) DROPS it — no cross-team leakage.
    StubHal hw; Node wt(hw, 0, 0x7070u);
    NodeConfig wc; wc.routing_sf=8; wc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); wc.leaf_id=4; wc.is_mobile=true; wc.team_id=0x99999999u; CHECK(wt.on_init(wc));
    wt.team_dad_fire();
    hw.emits.clear();
    wt.on_recv(buf.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(hw.saw_emit("rreq_rx")); CHECK_FALSE(hw.saw_emit("rreq_resolved_self"));   // ★ other team -> dropped
}

TEST_CASE("§team-multihop 2c — team liveness demotes a dead relay (candidates flip proactively) + recovery-on-heard; the STATIC plane is untouched") {
    const uint32_t T = 0x12345678u;
    StubHal h; Node n(h, 0, 0xA0A0u);
    NodeConfig c; c.routing_sf=8; c.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c.leaf_id=4; c.is_mobile=true; c.team_id=T; CHECK(n.on_init(c));
    n.team_dad_fire();
    DualLayerTestAccess::team_route2(n, /*dest=*/200, /*R=*/50, /*R2=*/51);   // a team route to B via R (primary) + R2 (alt), equal score
    CHECK(DualLayerTestAccess::team_primary(n, 200) == 50);                   // R is the primary
    for (int i = 0; i < 3; ++i) DualLayerTestAccess::team_rts_timeout(n, 50); // R times out 3x -> SILENT (the proactive-demotion evidence)
    CHECK(DualLayerTestAccess::team_penalty(n, 50) == protocol::peer_silent_penalty_q4);   // ★ TEAM liveness demoted R
    CHECK(DualLayerTestAccess::team_primary(n, 200) == 51);                   // ★ the demotion re-sorted _rt_team NOW -> candidates[0] = R2 (the next flight skips the dead relay)
    // ★ SEPARATION: the STATIC plane is byte-untouched — no static _peer_liveness slot for id 50, static penalty 0
    CHECK(DualLayerTestAccess::static_penalty(n, 50) == 0);
    CHECK(DualLayerTestAccess::static_liveness_absent(n, 50));
    DualLayerTestAccess::team_clear(n, 50);                                   // recovery-on-heard (a team frame from R)
    CHECK(DualLayerTestAccess::team_penalty(n, 50) == 0);                     // ★ recovery cleared the demotion -> R usable again
}

TEST_CASE("§mobile 6.4 — a TEAM mobile triggers beacons (off-grid convergence); a lone/roaming mobile does NOT") {
    // Fix (a): an off-grid team of mobiles has no static node to carry convergence, so hearing a new team peer MUST fire
    // the reciprocal beacon. A team mobile's schedule_triggered_beacon arms the triggered-beacon timer (id 3).
    StubHal hal; Node tm(hal, 0, 0xAAAAu);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(tm.on_init(cfg));
    tm.schedule_triggered_beacon();
    CHECK(hal.armed[3u /*kTriggeredBeaconTimerId*/]);          // ★ a TEAM mobile DOES trigger (its is_mobile beacon is still dropped from a static _rt)
    // a lone/roaming mobile (no team) still never triggers -> s18 + the mobile-DM sims stay byte-identical
    StubHal h2; Node lm(h2, 0, 0xBBBBu);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; c2.is_mobile=true; CHECK(lm.on_init(c2));   // team_id=0
    lm.schedule_triggered_beacon();
    CHECK_FALSE(h2.armed[3u /*kTriggeredBeaconTimerId*/]);     // ★ a lone mobile does NOT trigger (Lua parity)
}

TEST_CASE("§mobile 6.4 — a team RTS addressed to our team-local id reverse-learns a _rt_team route to its src (so we can reply)") {
    // Fix (b): team routes are otherwise beacon-only; an off-grid team's 15-min periodic beacons + join-order can miss a
    // peer, so a received team RTS (mobile_src + addr_len=1, addressed to OUR team id) learns a 1-hop reverse team route.
    StubHal hal; Node b(hal, 0, 0xB0B0u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xC036D02Bu; CHECK(b.on_init(cfg));
    b.team_dad_fire();
    const uint8_t tid = b.team_local_id();                   // B's team local id (== node_id off-grid)
    CHECK(tid >= 17);
    const uint8_t peer = (tid >= 100) ? static_cast<uint8_t>(tid - 1) : static_cast<uint8_t>(tid + 1);   // A's team local id (!= tid; avoid 0/0xFF so learn_direct_neighbor doesn't reject it)
    CHECK_FALSE(DualLayerTestAccess::is_team_peer(b, peer));  // not known yet
    RxMeta meta{9.0f,-70.0f,0,static_cast<int8_t>(-1)};
    rts_in r{}; r.leaf_id=4; r.src=peer; r.next=tid; r.dst=tid; r.ctr_lo=3; r.sf_index=0; r.rts_flags=0; r.payload_len=10; r.addr_len=1; r.mobile_src=true;
    uint8_t buf[9]; size_t n = pack_rts(r, buf);
    b.on_recv(buf, n, meta);
    CHECK(DualLayerTestAccess::is_team_peer(b, peer));        // ★ learned into _rt_team -> B can now route a reply to A
    // control: a STATIC RTS (mobile_src=0, addr_len=0) to our id learns into the static _rt, NOT _rt_team
    rts_in r2{}; r2.leaf_id=4; r2.src=50; r2.next=tid; r2.dst=tid; r2.ctr_lo=4; r2.sf_index=0; r2.rts_flags=0; r2.payload_len=10; r2.addr_len=0; r2.mobile_src=false;
    uint8_t buf2[9]; size_t n2 = pack_rts(r2, buf2);
    b.on_recv(buf2, n2, meta);
    CHECK_FALSE(DualLayerTestAccess::is_team_peer(b, 50));    // ★ a static-marked RTS never becomes a team peer
}

TEST_CASE("§enc — a team peer's key (from its beacon) is cached team-scoped -> a send BY team_local_id gets DST_HASH (so it can be ENCRYPTED)") {
    // A teammate's key_hash32 rides its beacon but was DROPPED for is_mobile beacons -> an encrypted send by team_local_id
    // had no DST_HASH -> couldn't seal (had to use the hash). Cache it team-scoped (NOT _id_bind) -> DST_HASH derivable.
    StubHal hal; Node m(hal, 0, 0x1111u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xC036D02Bu; CHECK(m.on_init(cfg));
    m.team_dad_fire();                                        // our own team_local_id
    DualLayerTestAccess::learn_team_neighbor(m, /*peer*/25, /*key*/0x9E47BBDEu);   // as a same-team beacon would
    uint32_t k = 0;
    CHECK(DualLayerTestAccess::team_key_of_id(m, 25, k));     // ★ resolved from the team-scoped cache
    CHECK(k == 0x9E47BBDEu);
    // a plaintext send by team_local_id 25 now carries DST_HASH = 25's key (the CRYPTED path REQUIRES this before it seals)
    const uint8_t body[2] = {'h','i'};
    DualLayerTestAccess::do_send(m, /*team dst*/25, body, 2);
    const PendingTx* pt = DualLayerTestAccess::pending(m);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK((pt->flags & DATA_FLAG_DST_HASH) != 0);        // ★ DST_HASH now set (was absent -> encrypt couldn't seal)
        const uint32_t dh = pt->inner[0] | (pt->inner[1]<<8) | (pt->inner[2]<<16) | (static_cast<uint32_t>(pt->inner[3])<<24);
        CHECK(dh == 0x9E47BBDEu);                            // inner = [dst_hash 4B][origin]... -> the teammate's key
    }
    // a NON-team-peer id -> no team key -> no DST_HASH from this path (never the static plane)
    uint32_t k2 = 0;
    CHECK_FALSE(DualLayerTestAccess::team_key_of_id(m, /*not a peer*/99, k2));
}

TEST_CASE("§mobile 6.4 — a team RTS from a DIFFERENT leaf is accepted (a mixed-registration team spans leaves)") {
    // Bench: a registered dual member (adopted its home's leaf=5) heard an off-grid teammate's team RTS (leaf=0) but
    // DROPPED it at the leaf gate -> no CTS -> the sender retried -> FAILED. The team plane is leaf-agnostic (shared PHY +
    // team_id); a team RTS to our team_local_id must be accepted regardless of leaf.
    StubHal hal; Node rcv(hal, 0, 0x9E47u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=5; cfg.is_mobile=true; cfg.team_id=0x631C4F86u; CHECK(rcv.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(rcv, /*local*/17, /*home*/31, /*home_hash*/0x3131u);   // DUAL: registered (node_id=17), leaf 5
    rcv.team_dad_fire();                                      // assign a team_local_id (keeps node_id=17)
    const uint8_t tid = rcv.team_local_id();
    CHECK(tid >= 17);
    RxMeta meta{9.0f,-70.0f,0,static_cast<int8_t>(-1)};
    // an off-grid teammate on leaf=0 sends a team RTS to our team_local_id (addr_len=1)
    rts_in r{}; r.leaf_id=0; r.src=251; r.next=tid; r.dst=tid; r.ctr_lo=0; r.sf_index=0; r.rts_flags=0; r.payload_len=1; r.addr_len=1; r.mobile_src=true;
    uint8_t b[9]; size_t n = pack_rts(r, b);
    rcv.on_recv(b, n, meta);
    CHECK(DualLayerTestAccess::has_pending_rx(rcv));          // ★ accepted despite leaf 0 != 5 (opens a receiver flight -> will CTS)
}

TEST_CASE("§mobile 6.4 — an OFF-GRID team member self-provisions node_id = _team_local_id (so the existing mobile link-layer carries team DMs); beacon src = team id; a static beacon src=node_id") {
    StubHal hal; Node mob(hal, 0, 0xAAAAu);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
    CHECK(mob.node_id() == 0);                                // pre-DAD: unprovisioned (no static host, no team id yet)
    mob.team_dad_fire();
    const uint8_t tid = mob.team_local_id();
    CHECK(mob.node_id() == tid);                              // ★ §6.4 Option X: off-grid, the team-DAD'd id IS the node_id -> the whole mobile link-layer delivers team unicast DMs
    mob.test_emit_beacon("periodic");
    CHECK(hal.last_tx_len > 0);                               // ★ it DID beacon (provisioned on the off-grid team plane)
    auto pb = parse_beacon(std::span<const uint8_t>(hal.last_tx, hal.last_tx_len));
    CHECK(pb.has_value());
    if (pb) { CHECK(pb->src == tid); CHECK(pb->is_mobile); }  // ★ src = _team_local_id (== node_id off-grid)
    // a static node -> src = node_id (unchanged)
    StubHal h2; Node stat(h2, 30, 0x3030u); NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; CHECK(stat.on_init(c2));
    stat.test_emit_beacon("periodic");
    auto sb = parse_beacon(std::span<const uint8_t>(h2.last_tx, h2.last_tx_len));
    CHECK(sb.has_value());
    if (sb) CHECK(sb->src == 30);
}

TEST_CASE("§mobile 6.4 — team-DAD DEFENSE: a same-team claim of our CONFIRMED id -> DENY; TENTATIVE -> re-pick; other-team -> ignore") {
    auto mk_team_bcn = [](uint8_t src, uint32_t key, uint32_t team, std::array<uint8_t,64>& buf) -> size_t {
        uint8_t ext[8]; size_t en = pack_team_id_tlv(team, std::span<uint8_t>(ext, sizeof ext));
        beacon_in in{}; in.leaf_id=4; in.src=src; in.key_hash32=key; in.is_mobile=true; in.ext=std::span<const uint8_t>(ext, en);
        return pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size())); };
    const RxMeta m{8.0f,-80.0f,0,static_cast<int8_t>(-1)};
    // (a) CONFIRMED + a same-team claim (different key) -> DENY (defend), keep our id
    { StubHal hal; Node mob(hal, 0, 0xAAAAu);
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
      mob.team_dad_fire(); mob.on_timer(77);                 // confirm
      const uint8_t tid = mob.team_local_id(); hal.emits.clear();
      std::array<uint8_t,64> b{}; size_t bn = mk_team_bcn(tid, 0xBBBBu, 0xABCD1234u, b);
      mob.on_recv(b.data(), bn, m);
      CHECK(hal.saw_emit("team_dad_defense"));
      CHECK(mob.team_local_id() == tid); }
    // (b) TENTATIVE (guard open) + a same-team claim -> yield + re-pick a DIFFERENT id
    { StubHal hal; Node mob(hal, 0, 0xAAAAu);
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
      mob.team_dad_fire();
      const uint8_t tid = mob.team_local_id(); hal.emits.clear();
      std::array<uint8_t,64> b{}; size_t bn = mk_team_bcn(tid, 0xBBBBu, 0xABCD1234u, b);
      mob.on_recv(b.data(), bn, m);
      CHECK(hal.saw_emit("team_dad_repick"));
      CHECK(mob.team_local_id() != tid); CHECK(mob.team_local_id() >= 17); }
    // (c) a DIFFERENT-team beacon claiming our id -> NO defense (not our team plane)
    { StubHal hal; Node mob(hal, 0, 0xAAAAu);
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
      mob.team_dad_fire(); mob.on_timer(77);
      const uint8_t tid = mob.team_local_id(); hal.emits.clear();
      std::array<uint8_t,64> b{}; size_t bn = mk_team_bcn(tid, 0xBBBBu, 0x99999999u, b);
      mob.on_recv(b.data(), bn, m);
      CHECK_FALSE(hal.saw_emit("team_dad_defense")); }
    // (d) CONFIRMED but our key LOSES the tiebreak (claimant key < ours) -> WE re-pick (deterministic convergence, no dead DENY)
    { StubHal hal; Node mob(hal, 0, 0xAAAAu);   // our key 0xAAAA
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
      mob.team_dad_fire(); mob.on_timer(77);
      const uint8_t tid = mob.team_local_id(); hal.emits.clear();
      std::array<uint8_t,64> b{}; size_t bn = mk_team_bcn(tid, 0x1111u, 0xABCD1234u, b);   // claimant key 0x1111 < our 0xAAAA -> we lose
      mob.on_recv(b.data(), bn, m);
      CHECK(hal.saw_emit("team_dad_repick"));
      CHECK(mob.team_local_id() != tid); }
}

TEST_CASE("§mobile 6.4 — a static registration moves node_id but leaves _team_local_id / team_id intact (dual plane)") {
    StubHal hal; Node mob(hal, 0, 0xAAAAu);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(mob.on_init(cfg));
    mob.team_dad_fire(); mob.on_timer(77);
    const uint8_t tid = mob.team_local_id();
    CHECK(tid >= 17);
    mob.set_identity(42, 0xAAAAu);                            // simulate the static CLAIM adopt (home-assigned id 42)
    CHECK(mob.node_id() == 42);                               // ★ static plane moved
    CHECK(mob.team_local_id() == tid);                       // ★ team plane UNCHANGED
    CHECK(mob.config().team_id == 0xABCD1234u);              // team_id intact
}

TEST_CASE("§mobile 6.4 — an OFF-GRID team SWITCH / leave-then-rejoin re-provisions node_id to the NEW team id (Option X invariant survives the console clearing _team_local_id)") {
    // (A) direct switch team-A -> team-B (mirrors handle_team: pre-zero _team_local_id, set new team_id, re-DAD)
    { StubHal hal; Node mob(hal, 0, 0xAAAAu);
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xAAAA1111u; CHECK(mob.on_init(cfg));
      mob.team_dad_fire();
      const uint8_t tid_a = mob.team_local_id();
      CHECK(mob.node_id() == tid_a);                         // off-grid: node_id == team id (Option X)
      mob.set_team_local_id(0);                              // handle_team pre-zeroes the stale team-DAD id...
      mob.mutable_config().team_id = 0xBBBB2222u;            // ...then joins team-B...
      mob.team_dad_fire();                                   // ...and re-DADs. old_tid is now 0, but !_my_mobile_reg.active still moves node_id.
      const uint8_t tid_b = mob.team_local_id();
      CHECK(tid_b >= 17);
      CHECK(mob.node_id() == tid_b);                         // ★ node_id MOVED to the new team id (NOT the stale tid_a) -> the team-DM handshake src matches the beacon src
    }
    // (B) leave (team 0) then rejoin a different team
    { StubHal hal; Node mob(hal, 0, 0xCCCCu);
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.team_id=0xAAAA1111u; CHECK(mob.on_init(cfg));
      mob.team_dad_fire();
      mob.set_team_local_id(0);                              // `team 0` (leave)
      mob.mutable_config().team_id = 0xBBBB2222u;            // `team <B>` (rejoin)
      mob.team_dad_fire();
      CHECK(mob.team_local_id() >= 17);
      CHECK(mob.node_id() == mob.team_local_id());           // ★ Option X invariant restored after leave-then-rejoin
    }
}

TEST_CASE("§S6/D10 — the mobile-sent breadcrumb is RETIRED; a re-home carries last_home in the j_discover (the NEW home notifies)") {
    // D10: the OLD-home notify is now originated by the NEW home (survives a mobile sleeping right after adopting).
    // The mobile-side emit (mobile_breadcrumb_tx) is GONE; last_home rides the mobile's DISCOVER +3-B block instead.
    StubHal hal; Node mob(hal, 0, 0x9999u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/20, /*home H1*/30, /*hash*/0x3030u);
    DualLayerTestAccess::deactivate_mobile_reg(mob);          // home-lost reset: active=false, home_id=30 preserved
    j_offer_in off{}; off.leaf_id=4; off.is_mobile=true; off.responder_node_id=40; off.responder_key_hash32=0x4040u; off.data_sf_bitmap=0x06; off.proposed_mobile_id=21;
    off.target_key_hash32 = mob.key_hash32(); uint8_t ob[13]; size_t on = pack_j_offer(off, ob); mob.on_recv(ob, on, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    hal.emits.clear();
    mob.on_timer(75 /*kMobileClaimGuardTimerId*/);
    CHECK_FALSE(hal.saw_emit("mobile_breadcrumb_tx"));        // ★ RETIRED — the mobile no longer originates the breadcrumb
    CHECK(mob.mobile_home_id() == 40);                       // adopted the new home
}

TEST_CASE("§S6/D10 — the NEW home originates the old-home notify: a re-home DISCOVER (last_home=30) + CLAIM -> presence_notify_tx to 30") {
    StubHal hal; Node home(hal, 40, 0x4040u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.host_mobiles=true; CHECK(home.on_init(cfg));
    DualLayerTestAccess::learn_neighbor(home, 30);           // a route to the OLD home
    // a mobile DISCOVERs at this (new) home carrying last_home=30 (same layer 4) -> the home OFFERs + STASHES the notify.
    j_discover_in d{}; d.leaf_id=4; d.is_mobile=true; d.key_hash32=0xB0B1u; d.last_home_id=30; d.last_home_layer=4; d.last_reg_epoch=3;
    uint8_t db[9]; size_t dn = pack_j_discover(d, db); home.on_recv(db, dn, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(hal.saw_emit("mobile_offer_tx"));
    // the mobile CLAIMs the offered id at this host -> record -> the NEW home fires the breadcrumb to 30.
    hal.emits.clear();
    j_claim_in c{}; c.leaf_id=4; c.is_mobile=true; c.key_hash32=0xB0B1u; c.proposed_node_id=21; c.claim_epoch=4; c.chosen_host_id=40;
    uint8_t cb[11]; size_t cn = pack_j_claim(c, cb); home.on_recv(cb, cn, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(hal.saw_emit("presence_notify_tx"));               // ★ NEW home -> old-home (30) redirect breadcrumb (D10)
    DualLayerTestAccess::pump(home);
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_BREADCRUMB);
        CHECK(pt->dst == 30);                                // ★ addressed to the OLD home
        const uint32_t sh = pt->inner[1] | (pt->inner[2]<<8) | (pt->inner[3]<<16) | (static_cast<uint32_t>(pt->inner[4])<<24);
        CHECK(sh == 0xB0B1u);                                // ★ SOURCE_HASH=M (the old home attributes it to _mobile_reg[M])
        CHECK(pt->inner[5] == 40);                           // body[0] = the NEW home id
    }
    // a FIRST registration (no last_home) -> no notify
    StubHal h2; Node home2(h2, 41, 0x4141u); NodeConfig c2=cfg; CHECK(home2.on_init(c2));
    j_discover_in d2{}; d2.leaf_id=4; d2.is_mobile=true; d2.key_hash32=0xC0C1u;   // last_home = 0 (fresh)
    uint8_t d2b[9]; size_t d2n = pack_j_discover(d2, d2b); home2.on_recv(d2b, d2n, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    h2.emits.clear();
    j_claim_in c3{}; c3.leaf_id=4; c3.is_mobile=true; c3.key_hash32=0xC0C1u; c3.proposed_node_id=22; c3.claim_epoch=1; c3.chosen_host_id=41;
    uint8_t c3b[11]; size_t c3n = pack_j_claim(c3, c3b); home2.on_recv(c3b, c3n, RxMeta{9.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK_FALSE(h2.saw_emit("presence_notify_tx"));          // ★ fresh reg -> no old home -> no notify
}

TEST_CASE("§S6 rev2 — a home PRUNES its stale entry when a probe SELECTS a different home (instant registry self-heal); only the selected home answers") {
    // Two homes both hold M. M's next CHECK probe selects home B (50). Home A (40), seeing selected != self, prunes M NOW
    // and does NOT answer; home B refreshes + answers. Backstops the D10 notify + the liveness prune.
    StubHal ha; Node homeA(ha, 40, 0x4040u);
    NodeConfig ca; ca.routing_sf=8; ca.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); ca.leaf_id=4; ca.host_mobiles=true; CHECK(homeA.on_init(ca));
    DualLayerTestAccess::store_mobile(homeA, /*M*/0xB0B1u, /*local*/17);
    CHECK(DualLayerTestAccess::mobile_reg_n(homeA) == 1);
    // a CHECK probe selecting home 50 (NOT homeA) -> homeA prunes M, stays silent
    p_probe_in p{}; p.selected_home_id = 50; p.selected_home_layer = 4; p.key_hash32 = 0xB0B1u; p.reg_epoch = 0;
    std::array<uint8_t,42> pb{}; size_t pn = pack_p_probe(p, pb);
    ha.emits.clear();
    DualLayerTestAccess::presence_ingest_probe(homeA, pb.data(), pn, RxMeta{20.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(DualLayerTestAccess::mobile_reg_n(homeA) == 0);   // ★ stale entry pruned
    CHECK(ha.saw_emit("presence_prune_stale"));
    // the SELECTED home (B=50) refreshes + schedules a roster (answers)
    StubHal hb; Node homeB(hb, 50, 0x5050u); NodeConfig cb=ca; CHECK(homeB.on_init(cb));
    DualLayerTestAccess::store_mobile(homeB, 0xB0B1u, 17);
    DualLayerTestAccess::presence_ingest_probe(homeB, pb.data(), pn, RxMeta{20.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(DualLayerTestAccess::mobile_reg_n(homeB) == 1);   // ★ selected home keeps + refreshes
    DualLayerTestAccess::presence_roster_fire(homeB);
    CHECK(hb.saw_emit("presence_roster_tx"));               // ★ only the selected home answers
}

TEST_CASE("§S6 rev2/D14 — proactive re-home ranks by the WORSE of the two directions: a strong->me but weak-echo candidate must NOT win") {
    // home is WEAK both ways (tier weak). A candidate with strong cand->me but a WEAK echo (me->cand) has bottleneck=weak
    // -> must NOT trigger a re-home; a candidate strong BOTH ways does.
    StubHal hal; Node mob(hal, 0, 0x9999u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.mobile_autoregister=true; CHECK(mob.on_init(cfg));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/30, /*hash*/0x3030u);
    const uint8_t L = DualLayerTestAccess::active_layer(mob);
    hal._now = 1000000;                                                        // dwell elapsed (last_adopt=0)
    // candidate A: strong cand->me (snr 55) but echo WEAK (me->A) -> bottleneck weak
    DualLayerTestAccess::presence_setup_prescan(mob, /*my_tier=*/protocol::presence_q_weak, /*home_rx_q4=*/protocol::db_to_q4(14.0f));
    DualLayerTestAccess::presence_add_cand(mob, /*home*/60, L, /*snr*/protocol::db_to_q4(55.0f), /*echo_tier*/protocol::presence_q_weak, /*first_seen*/0);
    hal.emits.clear();
    DualLayerTestAccess::presence_maybe_rehome(mob);
    CHECK_FALSE(hal.saw_emit("presence_rehome"));                               // ★ asymmetric candidate rejected on the bottleneck
    // candidate C: strong BOTH ways -> bottleneck strong -> re-home
    DualLayerTestAccess::presence_setup_prescan(mob, protocol::presence_q_weak, protocol::db_to_q4(14.0f));
    DualLayerTestAccess::presence_add_cand(mob, /*home*/70, L, protocol::db_to_q4(55.0f), /*echo_tier*/protocol::presence_q_strong, /*first_seen*/0);
    hal.emits.clear();
    DualLayerTestAccess::presence_maybe_rehome(mob);
    CHECK(hal.saw_emit("presence_rehome"));                                     // ★ balanced-strong candidate wins
}

TEST_CASE("§S6/QA-1 — reprovision -> probe -> roster-absent -> staggered re-register (join<->presence integration)") {
    // A re-joined home's registry is wiped (clear_routing_state) + it is briefly _node_id==0 (mid-DAD). The presence plane
    // must (a) suspend roster/probe while _node_id==0, and (b) a former mobile whose hash is absent from the (empty) roster
    // re-registers (staggered). Here: a home with node_id==0 ingests a probe -> NO roster/state; a mobile hearing a roster
    // WITHOUT its hash re-registers.
    StubHal hh; Node home(hh, 0 /*unprovisioned*/, 0x3030u);
    NodeConfig hc; hc.routing_sf=8; hc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); hc.leaf_id=4; hc.host_mobiles=true; CHECK(home.on_init(hc));
    p_probe_in p{}; p.selected_home_id = 0; p.key_hash32 = 0xB0B1u; std::array<uint8_t,42> pb{}; size_t pn = pack_p_probe(p, pb);
    hh.emits.clear();
    DualLayerTestAccess::presence_ingest_probe(home, pb.data(), pn, RxMeta{20.0f,-70.0f,0,static_cast<int8_t>(-1)});
    DualLayerTestAccess::presence_roster_fire(home);
    CHECK_FALSE(hh.saw_emit("presence_roster_tx"));            // ★ QA-1: _node_id==0 -> no roster (no home_id=0 garbage), no re-accept
    // a registered mobile hears its home's roster that does NOT contain it -> re-register (staggered)
    StubHal hm; Node mob(hm, 0, 0xB0B1u);
    NodeConfig mc; mc.routing_sf=8; mc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); mc.leaf_id=4; mc.is_mobile=true; mc.mobile_autoregister=true; CHECK(mob.on_init(mc));
    DualLayerTestAccess::make_registered_mobile(mob, /*local*/17, /*home*/30, /*hash*/0x3030u);
    // roster from home 30 with a DIFFERENT mobile (our hash ABSENT)
    PRosterEntry other[1] = {{ 0xCAFEu, 18, 0, protocol::presence_q_ok, false }};
    p_roster_in ri{}; ri.home_id = 30; ri.home_layer = DualLayerTestAccess::active_layer(mob); ri.entries = other; ri.count = 1;
    std::array<uint8_t,40> rb{}; size_t rn = pack_p_roster(ri, rb);
    hm.emits.clear();
    mob.on_recv(rb.data(), rn, RxMeta{20.0f,-70.0f,0,static_cast<int8_t>(-1)});
    CHECK(hm.saw_emit("presence_roster_absent"));             // ★ hash absent -> re-register triggered
    CHECK(mob.mobile_home_id() == 0);                         // dropped registration (re-DISCOVER staggered)
}

TEST_CASE("§S6/QA-2a — a non-hosting STATIC node fed P frames: no crash, no TX, no registry state (leaf-free dispatch is type-gated)") {
    StubHal hal; Node st(hal, 42, 0x4242u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.host_mobiles=false; CHECK(st.on_init(cfg));  // a true NON-HOST (is_mobile=false, host_mobiles=false)
    // a CHECK probe (not hosted), a SEARCHING probe, and a roster — all via on_recv (the real leaf-free dispatch)
    p_probe_in cp{}; cp.selected_home_id=99; cp.selected_home_layer=4; cp.key_hash32=0x1234u; std::array<uint8_t,42> cb{}; size_t cn=pack_p_probe(cp,cb);
    p_probe_in sp{}; sp.key_hash32=0x1234u; std::array<uint8_t,42> sbf{}; size_t sn=pack_p_probe(sp,sbf);
    PRosterEntry e1[1]={{0x1234u,17,0,protocol::presence_q_ok,false}}; p_roster_in ro{}; ro.home_id=99; ro.home_layer=4; ro.entries=e1; ro.count=1;
    std::array<uint8_t,40> rb{}; size_t rn=pack_p_roster(ro,rb);
    hal.emits.clear();
    st.on_recv(cb.data(), cn, RxMeta{10.0f,-80.0f,0,static_cast<int8_t>(-1)});   // check probe: not hosted -> ignore
    // a SEARCHING probe DOES make ANY node with host_mobiles echo; a pure non-host with 0 registry + host_mobiles default:
    st.on_recv(sbf.data(), sn, RxMeta{10.0f,-80.0f,0,static_cast<int8_t>(-1)});
    st.on_recv(rb.data(), rn, RxMeta{10.0f,-80.0f,0,static_cast<int8_t>(-1)});   // roster: a non-mobile ignores (dispatch is_mobile-gated)
    CHECK(DualLayerTestAccess::mobile_reg_n(st) == 0);                            // ★ no registry state created
    CHECK(st.mobile_home_id() == 0);                                             // not a mobile -> no reg
    // (host_mobiles default OFF -> even the searching probe schedules nothing; if a deployment enables hosting, an empty
    //  home answering a search is intentional and rate-limited.)
}

TEST_CASE("§mobile 4b — the old home records the redirect + answers future H-queries with the NEW home") {
    StubHal hal; Node home(hal, 30, 0x3030u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; CHECK(home.on_init(cfg));
    DualLayerTestAccess::store_mobile(home, /*M*/0xB0B1u, /*local*/17);   // this host hosts mobile M
    DualLayerTestAccess::learn_neighbor(home, 50);                        // a route to a future querier
    // feed a breadcrumb: M moved to new home 99, epoch 7
    DualLayerTestAccess::drive_post_ack_breadcrumb(home, /*source_hash*/0xB0B1u, /*new_home*/99, /*epoch*/7, /*new_home_layer*/6);
    CHECK(DualLayerTestAccess::mobile_reg_redirect(home, 0) == 99);       // ★ redirect recorded against _mobile_reg[M]
    // an H-query for M now resolves to M->99 (the redirect), NOT M->30
    std::array<uint8_t,8> hbuf{}; size_t hn = pack_h({/*leaf*/4, /*origin*/50, /*key*/0xB0B1u, /*ttl*/3, /*hard*/false}, hbuf);
    home.on_recv(hbuf.data(), hn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(-1)});
    const PendingTx* pt = DualLayerTestAccess::pending(home);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_H_ANSWER);
        auto o = parse_hash_bind_inner(std::span<const uint8_t>(pt->inner, pt->inner_len));
        CHECK(o.has_value());
        if (o) { CHECK(o->node_id == 99); CHECK(o->epoch == 7); CHECK(o->target_layer == 6); }   // ★ redirect answer M -> new home 99, epoch 7, LAYER 6 (§5b)
    }
    // a breadcrumb whose SOURCE_HASH is NOT a hosted mobile -> ignored (no redirect change)
    DualLayerTestAccess::drive_post_ack_breadcrumb(home, /*source_hash*/0xDEADu, /*new_home*/88, /*epoch*/9);
    CHECK(DualLayerTestAccess::mobile_reg_redirect(home, 0) == 99);       // unchanged (0xDEAD isn't hosted -> dropped)
}

TEST_CASE("§mobile 5a — scan cycles [own PHY] ∪ LEARNED directory; adopt the host's layer") {
    StubHal hal; Node mob(hal, 0, 0x7777u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=3; cfg.is_mobile=true;
    cfg.layers[0].layer_id=3; cfg.layers[0].routing_sf=8; cfg.layers[0].allowed_sf_bitmap=static_cast<uint16_t>(1u<<8);
    CHECK(mob.on_init(cfg));
    DualLayerTestAccess::add_learned_layer(mob, /*layer*/5, /*sf*/9);   // learned directory = [layer 5 / SF9]; a host lives ONLY there
    RxMeta meta{9.0f,-70.0f,0,static_cast<int8_t>(-1)};
    // DISCOVER on idx 0 (own layer 3) -> no host -> the guard advances to the learned entry
    hal._now=1000; mob.on_timer(74 /*kMobileDiscoverTimerId*/);
    CHECK(DualLayerTestAccess::mobile_scan_idx(mob) == 0);
    hal._now=1500; mob.on_timer(75 /*kMobileClaimGuardTimerId*/);
    CHECK(DualLayerTestAccess::mobile_scan_idx(mob) == 1);  // ★ no host on our own layer -> advanced to the learned layer
    // DISCOVER on idx 1 (learned layer 5) -> a host offers -> CLAIM + adopt layer 5
    hal._now=2000; mob.on_timer(74);
    j_offer_in off{}; off.leaf_id=5; off.is_mobile=true; off.responder_node_id=45; off.responder_key_hash32=0x4545u;
    off.data_sf_bitmap=static_cast<uint8_t>(1u<<1); off.proposed_mobile_id=21;
    off.target_key_hash32 = mob.key_hash32(); uint8_t ob[13]; size_t on = pack_j_offer(off, ob); mob.on_recv(ob, on, meta);
    hal._now=2500; mob.on_timer(75);                        // guard -> adopt
    CHECK(mob.mobile_home_id() == 45);
    CHECK(DualLayerTestAccess::my_mobile_home_leaf(mob) == 5);   // ★ home_leaf_id == the LEARNED layer 5, NOT the start layer 3 (E1 fix)
    CHECK(DualLayerTestAccess::cfg_leaf_id(mob) == (5 & 0x0F));  // ★ adopted the host's operating layer (adopt_mobile_phy)
}

TEST_CASE("§mobile 5b — the mobile_home cache stores the home's LAYER (from MOBILE_H_ANSWER.target_layer)") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; CHECK(n.on_init(cfg));
    hash_bind_inner hb{}; hb.target_layer=7; hb.node_id=30; hb.key_hash32=0xB0B1u; hb.epoch=3;   // home 30 on layer 7
    uint8_t inner[7]; size_t il = pack_hash_bind_inner(hb, inner, /*mobile=*/true);
    n.on_mobile_hash_bind_response(inner, static_cast<uint8_t>(il));
    uint8_t layer = 0;
    CHECK(n.mobile_home_find(0xB0B1u, &layer) == 30);
    CHECK(layer == 7);                                           // ★ the home's layer is cached (for cross-layer routing)
}

TEST_CASE("§mobile 5b — sender routes cross-layer when the home is on another layer; same-layer stays the 4a path") {
    StubHal hal; Node n(hal, 5, 0x5050u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; CHECK(n.on_init(cfg));
    const uint8_t body[] = {0xAA};
    // cross-layer: cached M -> home 20 on layer 7 (!= our layer 4) -> send_cross_layer (no gateway here -> xl_send_no_gateway), NOT a same-layer DM
    n.mobile_home_set(0xB0B1u, 20, /*epoch*/1, /*home_layer*/7);
    hal.emits.clear();
    DualLayerTestAccess::send_by_hash(n, 0xB0B1u, body, 1);
    CHECK(hal.saw_emit("xl_send_no_gateway"));                   // ★ went the cross-layer path (not a same-layer do_send)
    CHECK(DualLayerTestAccess::pending(n) == nullptr);           // no same-layer flight to 20
    // same-layer: cached M2 -> home 21 on OUR layer (active_layer_id) -> the 4a same-layer path (do_send override=M2)
    uint8_t al = n.active_layer_id();
    n.mobile_home_set(0xC0C0u, 21, /*epoch*/1, /*home_layer*/al);
    DualLayerTestAccess::learn_neighbor(n, 21);
    DualLayerTestAccess::send_by_hash(n, 0xC0C0u, body, 1);
    const PendingTx* pt = DualLayerTestAccess::pending(n);
    CHECK(pt != nullptr);
    if (pt) CHECK(pt->dst == 21);                               // ★ same-layer -> do_send to the home (4a)
}

TEST_CASE("§mobile 5b — the cross-layer bridge resolves a MOBILE on the target leaf to its home") {
    StubHal hal; hal._now = 10000;
    Node g(hal, /*id*/1, 0xABCDu);
    NodeConfig gc; gc.n_layers=2;
    gc.layers[0]=good_layer(100,8); gc.layers[0].node_id=5;      // FULL id 100 -> leaf nibble 4
    gc.layers[1]=good_layer(102,8); gc.layers[1].node_id=12;     // FULL id 102 -> leaf nibble 6
    CHECK(g.on_init(gc));
    DualLayerTestAccess::store_mobile_home_on_leaf(g, /*leaf*/1, /*M*/0xB0B1u, /*home*/40, /*layer*/102);   // M's home 40 is on the TARGET leaf
    data_unicast_inner ui{}; ui.origin=7; ui.has_cross_layer=true; ui.n_layers=2; ui.cur=1;
    ui.layer_ids[0]=4; ui.layer_ids[1]=6;                        // target = leaf nibble 6 (leaf 1)
    ui.has_dst_hash=true; ui.dst_key_hash32=0xB0B1u;
    hal.emits.clear();
    DualLayerTestAccess::bridge_ui(g, ui);
    CHECK_FALSE(hal.saw_emit("xl_bridge_refused"));
    CHECK(DualLayerTestAccess::handoff_count(g) == 1);
    const XlHandoff* h = DualLayerTestAccess::handoff_first(g);
    CHECK(h != nullptr);
    if (h) { CHECK(h->dst_node_id == 40); CHECK(h->dst_key_hash32 == 0xB0B1u); }   // ★ resolved to the mobile's HOME (40) on the target leaf; inner dst_hash=M rides intact
}

TEST_CASE("§mobile 5a — a gateway answers a LAYER_QUERY with its bridged layers (dst_hash=M); a non-gateway ignores") {
    StubHal hal; Node g(hal, 1, 0xABCDu);
    NodeConfig gc; gc.n_layers=2;
    gc.layers[0]=good_layer(100,8); gc.layers[0].node_id=5; gc.layers[0].freq_mhz=868.1;
    gc.layers[1]=good_layer(102,9); gc.layers[1].node_id=12; gc.layers[1].freq_mhz=915.0;
    CHECK(g.on_init(gc));
    DualLayerTestAccess::learn_neighbor(g, 30);                 // a route to the home (origin) so the answer flies
    hal.emits.clear();
    DualLayerTestAccess::drive_post_ack_query(g, /*origin=home*/30, /*source_hash=M*/0xB0B1u);
    CHECK(hal.saw_emit("mobile_layer_answer_tx"));
    const PendingTx* pt = DualLayerTestAccess::pending(g);
    CHECK(pt != nullptr);
    if (pt) {
        CHECK(pt->type == DATA_TYPE_MOBILE_LAYER_ANSWER);
        const uint32_t dh = pt->inner[0] | (pt->inner[1]<<8) | (pt->inner[2]<<16) | (static_cast<uint32_t>(pt->inner[3])<<24);
        CHECK(dh == 0xB0B1u);                                  // ★ DST_HASH = M -> the home last-mile-forwards the answer to the mobile
    }
    StubHal h2; Node s(h2, 2, 0xEEEEu);                         // a non-gateway (n_layers != 2) ignores the query
    NodeConfig sc; sc.routing_sf=8; sc.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); sc.leaf_id=0; CHECK(s.on_init(sc));
    h2.emits.clear();
    DualLayerTestAccess::drive_post_ack_query(s, 30, 0xB0B1u);
    CHECK_FALSE(h2.saw_emit("mobile_layer_answer_tx"));
}

TEST_CASE("§mobile 5a — the mobile ingests a LAYER_ANSWER into its learned directory (dedup)") {
    StubHal hal; Node mob(hal, 20, 0x2020u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=0; cfg.is_mobile=true; CHECK(mob.on_init(cfg));
    uint8_t body[64]; body[0]=2; size_t off=1;                  // [count=2][record A][record B]
    LayerRecord ra{}; ra.layer_id=5; ra.freq_khz=868100; ra.sf=9; ra.bw_hz=125000;
    LayerRecord rb{}; rb.layer_id=7; rb.freq_khz=915000; rb.sf=7; rb.bw_hz=250000;
    off += pack_layer_record(ra, std::span<uint8_t>(body+off, sizeof(body)-off));
    off += pack_layer_record(rb, std::span<uint8_t>(body+off, sizeof(body)-off));
    DualLayerTestAccess::learned_ingest(mob, body, off);
    CHECK(DualLayerTestAccess::learned_layers_n(mob) == 2);     // ★ 2 records learned
    DualLayerTestAccess::learned_ingest(mob, body, off);        // re-ingest the SAME -> upsert, not duplicate
    CHECK(DualLayerTestAccess::learned_layers_n(mob) == 2);     // ★ still 2
}

TEST_CASE("§mobile console — mobile_autoregister gates the boot-arm; OFF -> only `mobile register` arms the FSM") {
    { StubHal hal; Node mob(hal, 0, 0x7777u);                   // ON (default): on_init auto-arms the discover FSM (today)
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true;
      CHECK(mob.on_init(cfg));
      CHECK(mob.mobile_autoregister_on());
      CHECK(hal.armed[74]);                                    // ★ boot-armed the discover FSM (autoregister ON)
    }
    { StubHal hal; Node mob(hal, 0, 0x8888u);                   // OFF: no autonomy; the app drives it
      NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true; cfg.mobile_autoregister=false;
      CHECK(mob.on_init(cfg));
      CHECK_FALSE(mob.mobile_autoregister_on());
      CHECK_FALSE(hal.armed[74]);                              // ★ NO boot-arm (autoregister OFF)
      hal.emits.clear();
      mob.mobile_register_current();                           // the app command
      CHECK(hal.armed[74]);                                    // ★ the command arms the FSM
      hal._now=1000; mob.on_timer(74);
      CHECK(hal.saw_emit("mobile_discover_tx"));               // ★ + it DISCOVERs (works even with autoregister OFF)
    }
}

TEST_CASE("§mobile offer-adopt — a mobile adopts the HOST's leaf + sf_list from the OFFER (not its own)") {
    StubHal hal; Node mob(hal, 0, 0x7777u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true;  // SEED: leaf 0, sf_list SF7-only
    cfg.layers[0].layer_id=0; cfg.layers[0].routing_sf=8; cfg.layers[0].allowed_sf_bitmap=static_cast<uint16_t>(1u<<7);
    CHECK(mob.on_init(cfg));
    RxMeta meta{9.0f,-70.0f,0,static_cast<int8_t>(-1)};
    hal._now=1000; mob.on_timer(74);                          // DISCOVER (resets offers, arms the guard)
    j_offer_in off{}; off.leaf_id=4; off.is_mobile=true; off.responder_node_id=222; off.responder_key_hash32=0xDDDDu;
    off.data_sf_bitmap=static_cast<uint8_t>((1u<<6)|(1u<<7)); off.proposed_mobile_id=17;   // host on leaf 4, sf_list {SF6|SF7}
    off.target_key_hash32 = mob.key_hash32(); uint8_t ob[13]; size_t on = pack_j_offer(off, ob); mob.on_recv(ob, on, meta);
    mob.on_timer(75);                                          // guard -> adopt
    CHECK(mob.mobile_home_id() == 222);
    CHECK(DualLayerTestAccess::cfg_leaf_id(mob) == 4);        // ★ adopted the HOST's leaf 4 (was 0)
    CHECK(DualLayerTestAccess::my_mobile_home_leaf(mob) == 4);// ★ home_leaf_id = the host's leaf
    const uint16_t sf = DualLayerTestAccess::cfg_allowed_sf(mob);
    CHECK((sf & (1u<<6)) != 0);                               // ★ SF6 adopted (from the host's list — else last-mile SF6 DATA is missed)
    CHECK((sf & (1u<<7)) != 0);                               // ★ SF7 present
}

TEST_CASE("§mobile offer-adopt — single-PHY adopt sets the config but does NOT retune the radio (no blind window)") {
    StubHal hal; Node mob(hal, 0, 0x8888u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true;
    cfg.layers[0].layer_id=0; cfg.layers[0].routing_sf=8; cfg.layers[0].allowed_sf_bitmap=static_cast<uint16_t>(1u<<7);
    CHECK(mob.on_init(cfg));
    RxMeta meta{9.0f,-70.0f,0,static_cast<int8_t>(-1)};
    hal._now=1000; mob.on_timer(74);
    j_offer_in off{}; off.leaf_id=4; off.is_mobile=true; off.responder_node_id=222; off.responder_key_hash32=0xDDDDu;
    off.data_sf_bitmap=static_cast<uint8_t>((1u<<6)|(1u<<7)); off.proposed_mobile_id=17;
    off.target_key_hash32 = mob.key_hash32(); uint8_t ob[13]; size_t on = pack_j_offer(off, ob); mob.on_recv(ob, on, meta);
    hal.last_set_rx_freq = 999.0;                             // sentinel — a retune would overwrite it
    mob.on_timer(75);                                          // adopt (single-PHY -> retune_radio=false)
    CHECK(DualLayerTestAccess::cfg_leaf_id(mob) == 4);        // ★ config adopted (leaf)
    CHECK(hal.last_set_rx_freq == 999.0);                    // ★ NO radio retune (single-PHY is already tuned -> no spurious blind window)
}
