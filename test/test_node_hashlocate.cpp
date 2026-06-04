// MeshRoute — test_node_hashlocate.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Phase A0 of the H hash-locate plane (node_hashlocate.cpp): the id_bind binding table — the substrate
// the resolver answers from. Verifies that a heard BEACON binds the sender's key_hash32 -> node_id (the
// "stop discarding the received key_hash32" requirement), self-seeding at init, TTL expiry, and the
// table cap refuse. Driven through on_init / on_recv with an in-memory Hal. Mirrors dv_dual_sf.lua
// id_bind (:4677) + the beacon population (:9577).
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK.
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

struct Ev { std::string type; int64_t node = -1; int64_t key_hash32 = -1;
            std::string source, table, action; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    std::vector<Ev> events;

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
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            const EventField& fl = f[i];
            if (fl.type == EventField::T::i64) {
                if (!std::strcmp(fl.key, "node"))            e.node = fl.i;
                else if (!std::strcmp(fl.key, "key_hash32")) e.key_hash32 = fl.i;
            } else if (fl.type == EventField::T::str) {
                if (!std::strcmp(fl.key, "source"))      e.source = fl.s ? fl.s : "";
                else if (!std::strcmp(fl.key, "table"))  e.table  = fl.s ? fl.s : "";
                else if (!std::strcmp(fl.key, "action")) e.action = fl.s ? fl.s : "";
            }
        }
        events.push_back(e);
    }
    void     log(const char*) override {}

    int countType(const char* t) const {
        int c = 0; for (const auto& e : events) if (e.type == t) ++c; return c;
    }
};

// A minimal identity beacon from `src` carrying its key_hash32 (0 route entries — A0 only reads src+hash).
static size_t make_beacon(uint8_t src, uint32_t key_hash32, std::array<uint8_t, 64>& buf) {
    beacon_in in{};
    in.leaf_id = 0; in.src = src; in.key_hash32 = key_hash32;
    in.entries = std::span<const beacon_entry>();
    return pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size()));
}

}  // namespace

TEST_CASE("A0 id_bind — a heard beacon binds the sender's key_hash32 -> node_id") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);   // unprovisioned: no self-binding to confuse counts
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    std::array<uint8_t, 64> b{};
    const size_t n = make_beacon(/*src=*/3, /*key_hash32=*/0xAAAA1111, b);
    CHECK(n > 0);
    hal._now = 1000;
    node.on_recv(b.data(), n, meta);

    CHECK(node.id_bind_find_by_hash(0xAAAA1111) == 3);   // THE substrate: bound from the beacon
    CHECK(node.id_bind_count() == 1);
    CHECK(hal.countType("id_bind_set") == 1);
    CHECK(node.id_bind_find_by_hash(0x12345678) == -1);  // unknown hash -> miss
}

TEST_CASE("A0 id_bind — self binding is seeded at init when provisioned") {
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0x0000BEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    CHECK(node.id_bind_find_by_hash(0x0000BEEF) == 7);   // own hash resolves to self (we can answer for ourselves)
}

TEST_CASE("A0 id_bind — a binding past its TTL is no longer resolved") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.id_bind_ttl_ms = 5000;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    std::array<uint8_t, 64> b{};
    const size_t n = make_beacon(/*src=*/3, /*key_hash32=*/0x0000AAAA, b);
    hal._now = 1000;
    node.on_recv(b.data(), n, meta);
    CHECK(node.id_bind_find_by_hash(0x0000AAAA) == 3);   // fresh -> resolved

    hal._now = 1000 + 5000;                              // exactly TTL later
    CHECK(node.id_bind_find_by_hash(0x0000AAAA) == -1);  // expired -> skipped
}

TEST_CASE("A0 id_bind — table cap refuses a new node_id when full (table_cap_hit)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD); // unprovisioned: no self-binding occupies a slot
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.cap_id_bind = 2;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    auto feed = [&](uint8_t src, uint32_t h) {
        std::array<uint8_t, 64> b{}; const size_t n = make_beacon(src, h, b); node.on_recv(b.data(), n, meta);
    };
    feed(3, 0x00001111); feed(4, 0x00002222);            // 2 distinct -> table full
    CHECK(node.id_bind_count() == 2);
    feed(5, 0x00003333);                                 // 3rd distinct -> refused
    CHECK(node.id_bind_count() == 2);
    CHECK(node.id_bind_find_by_hash(0x00003333) == -1);

    bool refused = false;
    for (const auto& e : hal.events)
        if (e.type == "table_cap_hit" && e.table == "id_bind" && e.action == "refuse" && e.node == 5) refused = true;
    CHECK(refused);
}

TEST_CASE("A0 id_bind — a rehome (same hash, new node_id) evicts the stale id [rejoin self-heal]") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto feed = [&](uint8_t src, uint32_t h) {
        std::array<uint8_t, 64> b{}; const size_t n = make_beacon(src, h, b); node.on_recv(b.data(), n, meta);
    };
    feed(3, 0x0000DEAD);                                 // owner heard as id 3
    CHECK(node.id_bind_find_by_hash(0x0000DEAD) == 3);
    feed(5, 0x0000DEAD);                                 // SAME hash rejoins under a new id 5 (the node rehomed)
    CHECK(node.id_bind_find_by_hash(0x0000DEAD) == 5);   // resolves to the NEW id — unambiguous
    CHECK(node.id_bind_count() == 1);                    // the stale (3 -> DEAD) was evicted, not left to rot
}

TEST_CASE("A0 id_bind — a CLAIMED conflict (same id, different hash) is refused") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto feed = [&](uint8_t src, uint32_t h) {
        std::array<uint8_t, 64> b{}; const size_t n = make_beacon(src, h, b); node.on_recv(b.data(), n, meta);
    };
    feed(3, 0x00001111);                                 // id 3 -> hash 1111 (claimed, via beacon)
    feed(3, 0x00002222);                                 // id 3 now claims a DIFFERENT hash (claimed) -> REFUSE
    CHECK(node.id_bind_find_by_hash(0x00001111) == 3);   // the known binding is kept
    CHECK(node.id_bind_find_by_hash(0x00002222) == -1);  // the conflicting claim was refused
    bool conflict = false;
    for (const auto& e : hal.events) if (e.type == "addr_conflict_observed" && e.node == 3) conflict = true;
    CHECK(conflict);
}

TEST_CASE("A0 id_bind — an AUTHORITATIVE source overwrites a conflicting claimed binding") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000CAFE);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> b{}; const size_t n = make_beacon(/*src=*/3, /*hash=*/0x0000F00D, b);
    node.on_recv(b.data(), n, meta);
    CHECK(node.id_bind_find_by_hash(0x0000F00D) == 3);   // claimed: 3 -> F00D
    // The node itself adopts id 3 with its own key -> AUTHORITATIVE (self) must overwrite the claimed binding.
    node.set_identity(3, 0x0000CAFE);
    CHECK(node.id_bind_find_by_hash(0x0000CAFE) == 3);   // authoritative 3 -> CAFE wins
    CHECK(node.id_bind_find_by_hash(0x0000F00D) == -1);  // the stale claimed hash for id 3 is gone
}
