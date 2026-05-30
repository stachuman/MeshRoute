// MeshRoute — test_node_r2.cpp
//
// R2 route-plane hardening: the 3-cycle prune (rt_prune_cycle), which the
// line-topology scenario gate (t85) cannot exercise (0 rt_prune confirmed). The
// prune fires only on a cyclic me->X->N->me path, so it gets a dedicated unit
// test here (R2 decision Q1=a), driven through on_recv with a minimal in-memory
// Hal so a crafted beacon sequence builds the loop and triggers the prune.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
// -fno-exceptions => CHECK only; guard optional derefs with `if`.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

struct Captured { std::string type; bool has_dest = false; int dest = 0; };

// Minimal in-memory Hal: deterministic clock + rng, captures emits. Enough to
// drive Node::on_init / on_recv for a behaviour unit test (foundation for R3+).
class TestHal : public Hal {
public:
    uint64_t _now = 0;
    std::vector<Captured> events;

    TxResult tx(const uint8_t*, size_t, const TxParams&) override { return TxResult::ok; }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { return lo; }   // deterministic
    void     emit(const char* type, const EventField* f, size_t n) override {
        Captured c; c.type = type;
        for (size_t i = 0; i < n; ++i) {
            if (std::strcmp(f[i].key, "dest") == 0 && f[i].type == EventField::T::i64) {
                c.has_dest = true; c.dest = static_cast<int>(f[i].i);
            }
        }
        events.push_back(c);
    }
    void     log(const char*) override {}

    bool sawDest(const char* type, int dest) const {
        for (const auto& e : events)
            if (e.type == type && e.has_dest && e.dest == dest) return true;
        return false;
    }
};

// Pack a 1-entry beacon from `src` advertising route {dest via next, hops}.
static size_t make_beacon(uint8_t src, uint8_t dest, uint8_t next, uint8_t hops,
                          std::array<uint8_t, 64>& buf) {
    beacon_entry e{};
    e.dest = dest; e.next = next; e.score_bucket = 12; e.is_gateway = false; e.hops = hops;
    beacon_in in{};
    in.leaf_id = 0; in.src = src; in.key_hash32 = 0x1234;
    in.entries = std::span<const beacon_entry>(&e, 1);
    return pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size()));
}

}  // namespace

TEST_CASE("R2 prune — a 3-cycle me->X->N->me candidate is dropped on a next==self entry") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // Beacon 1: X=3 advertises dest=5 via next=9 (N), hops=2 -> we install
    // rt[5] = {next_hop=3, n2_hop=9, hops=3}.
    std::array<uint8_t, 64> b1{};
    const size_t n1 = make_beacon(/*src=*/3, /*dest=*/5, /*next=*/9, /*hops=*/2, b1);
    CHECK(n1 > 0);
    hal._now = 1000;
    node.on_recv(b1.data(), n1, meta);
    CHECK(hal.sawDest("rt_update", 5));        // route to 5 installed (via 3, n2_hop=9)
    CHECK_FALSE(hal.sawDest("rt_prune", 5));

    // Beacon 2: N=9 advertises dest=5 via next=0 (US) -> closes me->3->9->me;
    // rt_prune_cycle drops the candidate whose n2_hop==9.
    std::array<uint8_t, 64> b2{};
    const size_t n2 = make_beacon(/*src=*/9, /*dest=*/5, /*next=*/0, /*hops=*/1, b2);
    CHECK(n2 > 0);
    hal._now = 2000;
    node.on_recv(b2.data(), n2, meta);
    CHECK(hal.sawDest("rt_prune", 5));         // the looped candidate was pruned
}

TEST_CASE("R2 prune — a DIRECT candidate (hops==1, n2_hop==0) is NOT pruned by sender 0") {
    // The hops>1 guard protects direct candidates: their value-init n2_hop==0
    // must not be mistaken for sender 0. Self is node 5 so sender 0 is not a
    // self-echo (which on_recv filters).
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // Direct neighbour 3 -> rt[3] = {next_hop=3, hops=1, n2_hop=0}.
    std::array<uint8_t, 64> b1{};
    const size_t n1 = make_beacon(/*src=*/3, /*dest=*/9, /*next=*/8, /*hops=*/2, b1);
    CHECK(n1 > 0);
    hal._now = 1000;
    node.on_recv(b1.data(), n1, meta);
    CHECK(hal.sawDest("rt_update", 3));        // direct route to 3 (hops=1, n2_hop=0)

    // sender=0 advertises dest=3 via next=5 (US) -> prune lookup on rt[3]. Its
    // only candidate is DIRECT (n2_hop=0); without the hops>1 guard, n2_hop(0)==
    // sender(0) would falsely prune it.
    std::array<uint8_t, 64> b2{};
    const size_t n2 = make_beacon(/*src=*/0, /*dest=*/3, /*next=*/5, /*hops=*/1, b2);
    CHECK(n2 > 0);
    hal._now = 2000;
    node.on_recv(b2.data(), n2, meta);
    CHECK_FALSE(hal.sawDest("rt_prune", 3));   // direct candidate spared by the hops>1 guard
}
