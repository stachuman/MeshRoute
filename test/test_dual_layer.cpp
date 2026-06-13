// MeshRoute — test_dual_layer.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the dual-layer gateway (2026-06-12-gateway-dual-layer-design.md). SLICE 0: the config model
// (LayerConfig / n_layers / layers[]) + on_init's FAIL-LOUD validation gate (§3.2) — a gateway (n_layers==2)
// REQUIRES per-layer layer_id/routing_sf/allowed_sf_bitmap + non-overlapping explicit windows, else on_init
// REFUSES (returns false). Single-layer configs are unaffected: the legacy scalars mirror into layers[0].
// This file grows as the gateway slices land. NB: test_airtime.cpp provides main(); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"

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
    TxResult tx(const uint8_t*, size_t, const TxParams&) override { return TxResult::ok; }
    void     set_rx_sf(int sf) override { last_set_rx_sf = sf; }
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t id) override { if (id < 80) armed[id] = true; return true; }
    void     cancel(uint32_t id) override { if (id < 80) cancelled[id] = true; }
    void     set_protocol_id(int id) override { last_set_protocol_id = id; }
    int      rand_range(int lo, int) override { return lo; }
    void     emit(const char*, const EventField*, size_t) override {}
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
    static void     set_tx_stash(Node& n, bool v)     { n._tx_stash[0].valid = v; }
    static void     set_deferred(Node& n, uint8_t i, uint8_t cnt, bool armed) { n._layers[i]._deferred_n = cnt; n._layers[i]._drain_armed = armed; }
    static uint32_t drain_timer_id()                  { return Node::kDeferredDrainTimerId; }
    // F2: pin the FORWARD/provider gate (a gateway never rebroadcasts a channel flood).
    static void     arm_flood_slot(Node& n, uint8_t slot)   { n._active->_flood[slot].active = true; }
    static bool     flood_slot_active(Node& n, uint8_t slot) { return n._active->_flood[slot].active; }
    static void     flood_decide(Node& n, uint8_t slot)     { n.flood_forward_decision(slot); }
    static uint32_t flood_rebcast_timer_id(uint8_t slot)    { return Node::kFloodRebcastTimerId + slot; }
};
}  // namespace meshroute

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
    DualLayerTestAccess::set_tx_stash(node, true);      CHECK(DualLayerTestAccess::swap_blocked(node));     // on-radio-busy re-issue stash
    DualLayerTestAccess::set_tx_stash(node, false);     CHECK_FALSE(DualLayerTestAccess::swap_blocked(node));
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
