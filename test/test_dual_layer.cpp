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
// Minimal no-op Hal — on_init only needs the timer/now/sf seams; nothing is asserted on the Hal in Slice 0.
class StubHal : public Hal {
public:
    uint64_t _now = 0;
    TxResult tx(const uint8_t*, size_t, const TxParams&) override { return TxResult::ok; }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
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
