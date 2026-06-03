// MeshRoute — test_node_query.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Q REQ_SYNC route-bootstrap plane (node_query.cpp): the originator boot loop, the RX handler
// (leaf-filter / self-guard / dedup / learn+schedule), the jittered sync-response backoff DRAW,
// the kind=="sync" full-table beacon, and the overheard-useful-beacon suppression. Driven through
// on_init / on_recv / on_timer with an in-memory Hal that records timers, TX bytes, events, and
// scriptable rand. Mirrors dv_dual_sf.lua:8032 / 8064 / 11767 / 9699.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions
// => CHECK only. The backoff draw value is asserted via the scheduled delay_ms, not a raw rand
// count — handle_q's neighbour-learn fires a (coalesced) triggered-beacon jitter draw first.
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

constexpr uint32_t kBeaconTimerId          = 1;
constexpr uint32_t kTriggeredBeaconTimerId = 3;
constexpr uint32_t kReqSyncTimerId         = 14;
constexpr uint32_t kSyncResponseTimerId    = 32;   // ring base [32..47]

struct Ev { std::string type; int from = -1; int dst = -1; int joiner = -1; int delay_ms = -1;
            int rt_total = -1; int requester_mobile = -1; std::string reason; };
struct Timer { uint32_t id; uint32_t delay; };
struct TxFrame { std::vector<uint8_t> bytes; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    int      _rand_ret = -1;          // >=0 overrides (returns lo otherwise)
    int      rand_calls = 0;
    std::vector<Ev>      events;
    std::vector<Timer>   timers;      // every after() (re-arms accumulate)
    std::vector<TxFrame> tx_frames;

    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override {
        TxFrame f; f.bytes.assign(b, b + n); tx_frames.push_back(std::move(f)); return TxResult::ok;
    }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t delay, uint32_t id) override { timers.push_back({ id, delay }); return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { ++rand_calls; return _rand_ret >= 0 ? _rand_ret : lo; }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            if      (std::strcmp(f[i].key, "from") == 0)             e.from = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dest") == 0)             e.dst = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "joiner") == 0)           e.joiner = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "delay_ms") == 0)         e.delay_ms = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "rt_total") == 0)         e.rt_total = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "requester_mobile") == 0) e.requester_mobile = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "reason") == 0 && f[i].s) e.reason = f[i].s;
        }
        events.push_back(std::move(e));
    }
    void     log(const char*) override {}

    int count(const char* t) const { int n = 0; for (const auto& e : events) if (e.type == t) ++n; return n; }
    const Ev* last(const char* t) const { const Ev* r = nullptr; for (const auto& e : events) if (e.type == t) r = &e; return r; }
    int last_delay(uint32_t id) const { int d = -1; for (const auto& t : timers) if (t.id == id) d = static_cast<int>(t.delay); return d; }
    bool armed(uint32_t id) const { for (const auto& t : timers) if (t.id == id) return true; return false; }
};

static size_t mk_q(uint8_t leaf, uint8_t src, uint8_t dest, q_opcode op, bool mobile, std::array<uint8_t,16>& b) {
    q_in in{}; in.leaf_id = leaf; in.src = src; in.dest = dest; in.opcode = op; in.mobile = mobile;
    return pack_q(in, std::span<uint8_t>(b.data(), b.size()));
}
// 1-entry beacon from `src` advertising {dest via next, hops} — n_entries>0 => "useful" for suppression
// and installs a route (so the receiver gains rt entries).
static size_t mk_beacon_route(uint8_t src, uint8_t dest, uint8_t next, uint8_t hops, std::array<uint8_t,64>& b) {
    beacon_entry e{}; e.dest = dest; e.next = next; e.score_bucket = 5; e.is_gateway = false; e.hops = hops;
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x3000u + src;
    in.entries = std::span<const beacon_entry>(&e, 1);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}
static RxMeta meta_at(uint64_t t) { RxMeta m{}; m.snr_db = 9.0f; m.rssi_dbm = -70.0f; m.recv_ms = t; m.src_hint = -1; return m; }

}  // namespace

TEST_CASE("Q REQ_SYNC originator — boot arms the loop; fire sends a Q + re-arms while starved") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.req_sync_on_boot = true; cfg.req_sync_min_routes = 8;
    node.on_init(cfg);
    // on_init arms the boot loop at +req_sync_listen_ms (8000).
    bool boot_armed_8000 = false;
    for (const auto& t : hal.timers) if (t.id == kReqSyncTimerId && t.delay == 8000) boot_armed_8000 = true;
    CHECK(boot_armed_8000);

    hal._now = 8000;
    node.on_timer(kReqSyncTimerId);                          // fire the loop: in discovery + rt_count(0) < 8 -> send
    CHECK(hal.count("q_tx") == 1);
    // The TX is a Q frame (cmd-nibble 0x6).
    CHECK(!hal.tx_frames.empty());
    if (!hal.tx_frames.empty()) CHECK((hal.tx_frames.back().bytes[0] >> 4) == 0x6);
    // Re-armed for another req_sync_retry_ms (30000) since still starved.
    CHECK(hal.last_delay(kReqSyncTimerId) == 30000);
}

TEST_CASE("Q REQ_SYNC originator — route-rich node does NOT send (rt_count >= req_sync_min_routes)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.req_sync_on_boot = true; cfg.req_sync_min_routes = 1;
    node.on_init(cfg);
    std::array<uint8_t,64> bb{};                              // learn one neighbour -> rt_count == 1 >= min(1)
    size_t n = mk_beacon_route(5, 9, 9, 2, bb);
    node.on_recv(bb.data(), n, meta_at(10));
    hal._now = 8000;
    node.on_timer(kReqSyncTimerId);
    CHECK(hal.count("q_tx") == 0);                            // route-rich -> suppressed
}

TEST_CASE("Q REQ_SYNC handler — REQ_SYNC schedules a jittered response; the fire emits a sync beacon") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_enabled = true; cfg.sync_response_min_routes = 0;
    node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1500;                     // backoff draw -> 1500
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(/*leaf=*/0, /*src=*/7, /*dest=*/255, q_opcode::req_sync, /*mobile=*/false, qb);
    node.on_recv(qb.data(), n, meta_at(100));

    CHECK(hal.count("q_rx") == 1);
    const Ev* rx = hal.last("q_rx"); CHECK(rx); if (rx) CHECK(rx->from == 7);
    const Ev* sch = hal.last("sync_response_scheduled"); CHECK(sch);
    if (sch) { CHECK(sch->joiner == 7);
               CHECK(sch->delay_ms == 1500); }              // == the backoff draw
    CHECK(hal.armed(kSyncResponseTimerId));                  // slot 0 timer armed

    size_t tx_before = hal.tx_frames.size();
    node.on_timer(kSyncResponseTimerId);                     // fire slot 0
    CHECK(hal.count("sync_response_tx") == 1);
    CHECK(hal.tx_frames.size() == tx_before + 1);            // a "sync" beacon went out
    if (!hal.tx_frames.empty()) CHECK((hal.tx_frames.back().bytes[0] >> 4) == 0x0);   // cmd-nibble B (beacon)
}

TEST_CASE("Q REQ_SYNC handler — dedup: a second Q from the same requester within TTL does not re-schedule") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 0;
    node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1500;
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(0, 7, 255, q_opcode::req_sync, false, qb);
    node.on_recv(qb.data(), n, meta_at(100));
    hal._now = 200;                                          // within q_respond_ttl_ms (10000)
    node.on_recv(qb.data(), n, meta_at(200));
    CHECK(hal.count("sync_response_scheduled") == 1);        // only the first scheduled
    CHECK(hal.count("q_rx") == 1);                           // the dedup short-circuits before q_rx
}

TEST_CASE("Q REQ_SYNC handler — self-guard + cross-leaf filter both drop silently") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 0;
    node.on_init(cfg);
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(/*leaf=*/0, /*src=*/2, 255, q_opcode::req_sync, false, qb);   // src == self
    node.on_recv(qb.data(), n, meta_at(50));
    n = mk_q(/*leaf=*/1, /*src=*/7, 255, q_opcode::req_sync, false, qb);          // foreign leaf
    node.on_recv(qb.data(), n, meta_at(60));
    CHECK(hal.count("q_rx") == 0);
    CHECK(hal.count("sync_response_scheduled") == 0);
}

TEST_CASE("Q REQ_SYNC suppression — a useful overheard beacon cancels the pending response") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 0;
    node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 2000;                    // fire_at = 100 + 2000 = 2100
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(0, 7, 255, q_opcode::req_sync, false, qb);
    node.on_recv(qb.data(), n, meta_at(100));
    CHECK(hal.count("sync_response_scheduled") == 1);

    hal._now = 500;                                          // <= fire_at, within suppress window
    std::array<uint8_t,64> bb{};
    size_t bn = mk_beacon_route(9, 3, 3, 2, bb);             // n_entries>0 -> useful
    node.on_recv(bb.data(), bn, meta_at(500));

    node.on_timer(kSyncResponseTimerId);                     // fire slot 0 -> suppressed
    CHECK(hal.count("sync_response_suppressed") == 1);
    CHECK(hal.count("sync_response_tx") == 0);
    const Ev* sup = hal.last("sync_response_suppressed"); CHECK(sup); if (sup) CHECK(sup->reason == "heard_useful_bcn");
}

TEST_CASE("Q REQ_SYNC handler — two distinct requesters get two independent pending slots") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 0;
    node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1000;
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(0, /*src=*/7, 255, q_opcode::req_sync, false, qb);
    node.on_recv(qb.data(), n, meta_at(100));
    n = mk_q(0, /*src=*/8, 255, q_opcode::req_sync, false, qb);
    node.on_recv(qb.data(), n, meta_at(100));
    CHECK(hal.count("sync_response_scheduled") == 2);        // distinct requesters -> no cross-dedup
    CHECK(hal.armed(kSyncResponseTimerId));                  // slot 0
    bool slot1 = false; for (const auto& t : hal.timers) if (t.id == kSyncResponseTimerId + 1) slot1 = true;
    CHECK(slot1);                                            // slot 1
}

// ---- gap-closing cases (from the pre-commit adversarial review) --------------------------------

TEST_CASE("Q REQ_SYNC originator — rate-limit window strict-< boundary (re-fire suppressed within, allowed at edge)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.req_sync_on_boot = true; cfg.req_sync_min_routes = 8;
    node.on_init(cfg);
    hal._now = 8000; node.on_timer(kReqSyncTimerId);          // first send -> _last_req_sync_tx_ms = 8000
    CHECK(hal.count("q_tx") == 1);
    hal._now = 8000 + protocol::req_sync_retry_ms - 1;        // now-last = retry-1 < retry -> rate-limited
    node.on_timer(kReqSyncTimerId);
    CHECK(hal.count("q_tx") == 1);
    hal._now = 8000 + protocol::req_sync_retry_ms;            // now-last = retry, NOT < retry -> allowed (strict <)
    node.on_timer(kReqSyncTimerId);
    CHECK(hal.count("q_tx") == 2);
}

TEST_CASE("Q REQ_SYNC ring-full — the (cap+1)th requester DRAWS then drops (determinism: draw consumed before the drop)") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 0;
    cfg.is_mobile = true;   // a mobile responder never schedules a triggered beacon -> the only draws here are backoffs
    node.on_init(cfg);
    const int base = hal.rand_calls;   // on_init draws once (the first-beacon jitter) — baseline it out
    hal._now = 100;
    std::array<uint8_t,16> qb{};
    const int over = protocol::cap_sync_response_pending + 1;
    for (int k = 0; k < over; ++k) {                          // cap+1 distinct requesters at the same instant
        size_t n = mk_q(0, static_cast<uint8_t>(7 + k), 255, q_opcode::req_sync, false, qb);
        node.on_recv(qb.data(), n, meta_at(100));
    }
    CHECK(hal.count("sync_response_scheduled") == protocol::cap_sync_response_pending);  // exactly cap stored
    CHECK(hal.count("sync_response_drop_full") == 1);                                     // the (cap+1)th dropped
    CHECK(hal.rand_calls - base == over);                    // ALL cap+1 drew — the dropped one's backoff is still consumed
}

TEST_CASE("Q REQ_SYNC responder — route-starved skip when rt_count < sync_response_min_routes (no schedule)") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 5;  // demand 5 routes to answer
    node.on_init(cfg);
    hal._now = 100;
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(0, 7, 255, q_opcode::req_sync, false, qb);
    node.on_recv(qb.data(), n, meta_at(100));                // rt_count == 1 (just-learned 7) < 5 -> skip
    CHECK(hal.count("sync_response_skip") == 1);
    CHECK(hal.count("sync_response_scheduled") == 0);
    CHECK(!hal.armed(kSyncResponseTimerId));
}

TEST_CASE("Q REQ_SYNC backoff — both mobile penalties add to the drawn delay") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.sync_response_min_routes = 0; cfg.is_mobile = true;  // responder mobile
    node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1500;                    // base backoff draw = 1500
    std::array<uint8_t,16> qb{};
    size_t n = mk_q(0, 7, 255, q_opcode::req_sync, /*mobile=*/true, qb);   // requester mobile too
    node.on_recv(qb.data(), n, meta_at(100));
    const Ev* sch = hal.last("sync_response_scheduled"); CHECK(sch);
    if (sch) CHECK(sch->delay_ms == 1500 + static_cast<int>(protocol::sync_response_mobile_penalty_ms)
                                         + static_cast<int>(protocol::sync_response_requester_mobile_penalty_ms));
}

TEST_CASE("Q REQ_SYNC suppression — window boundary: at now==fire_at suppresses; past fire_at does not") {
    std::array<uint8_t,16> qb{}; std::array<uint8_t,64> bb{};
    {   // now == fire_at -> within (now <= fire_at) -> SUPPRESSED
        TestHal hal; Node node(hal, 2, 0xBEEF);
        NodeConfig cfg; cfg.routing_sf=7; cfg.leaf_id=0; cfg.sync_response_min_routes=0;
        node.on_init(cfg);
        hal._now=100; hal._rand_ret=400;                     // fire_at = 100 + 400 = 500
        size_t n=mk_q(0,7,255,q_opcode::req_sync,false,qb); node.on_recv(qb.data(),n,meta_at(100));
        hal._now=500;                                        // == fire_at
        size_t bn=mk_beacon_route(9,3,3,2,bb); node.on_recv(bb.data(),bn,meta_at(500));
        node.on_timer(kSyncResponseTimerId);
        CHECK(hal.count("sync_response_suppressed")==1);
        CHECK(hal.count("sync_response_tx")==0);
    }
    {   // now > fire_at -> NOT suppressible -> the reply still fires
        TestHal hal; Node node(hal, 2, 0xBEEF);
        NodeConfig cfg; cfg.routing_sf=7; cfg.leaf_id=0; cfg.sync_response_min_routes=0;
        node.on_init(cfg);
        hal._now=100; hal._rand_ret=400;                     // fire_at = 500
        size_t n=mk_q(0,7,255,q_opcode::req_sync,false,qb); node.on_recv(qb.data(),n,meta_at(100));
        hal._now=501;                                        // past fire_at
        size_t bn=mk_beacon_route(9,3,3,2,bb); node.on_recv(bb.data(),bn,meta_at(501));
        node.on_timer(kSyncResponseTimerId);
        CHECK(hal.count("sync_response_suppressed")==0);
        CHECK(hal.count("sync_response_tx")==1);
    }
}

TEST_CASE("Q REQ_SYNC sync beacon is FULL-TABLE out of discovery (kind=='sync' carries stable routes a periodic omits)") {
    TestHal hal; Node node(hal, 2, 0xBEEF);
    NodeConfig cfg; cfg.routing_sf=7; cfg.leaf_id=0; cfg.sync_response_min_routes=0; cfg.quiet_threshold_ms=0;  // beacon fast path (no throttle draw)
    node.on_init(cfg);
    // Exit discovery: ingest >= discovery_min_bcn_rx beacons from distinct srcs (installs routes, dirty).
    hal._now=1000;
    std::array<uint8_t,64> bb{};
    for (uint8_t s=5; s<=7; ++s) { size_t bn=mk_beacon_route(s, static_cast<uint8_t>(50+s), s, 2, bb); node.on_recv(bb.data(),bn,meta_at(1000)); }
    CHECK(hal.count("bcn_discovery_exit") >= 1);             // out of discovery after >=3 beacons
    // A periodic beacon packs + clears the dirty routes -> they become STABLE (Phase-2-only material).
    node.on_timer(kBeaconTimerId);
    const int stable = static_cast<int>(node.rt_count());
    CHECK(stable >= 3);
    // REQ_SYNC arrives; the jittered reply must emit the FULL table, not the dirty-only subset.
    hal._now=2000; hal._rand_ret=500;
    std::array<uint8_t,16> qb{}; size_t n=mk_q(0,9,255,q_opcode::req_sync,false,qb);
    node.on_recv(qb.data(),n,meta_at(2000));                 // learns 9 -> one NEW dirty route on top of the stable ones
    const size_t tx_before = hal.tx_frames.size();
    node.on_timer(kSyncResponseTimerId);                    // emit_beacon("sync")
    CHECK(hal.tx_frames.size() == tx_before + 1);
    if (hal.tx_frames.size() == tx_before + 1) {
        const auto& f = hal.tx_frames.back();
        auto pb = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        CHECK(pb.has_value());
        // Full-table: every route present. A broken kind=="sync" gate (dirty_only=true out of
        // discovery) would carry ONLY the single just-learned dirty route to 9.
        if (pb) CHECK(static_cast<int>(pb->n_entries) == static_cast<int>(node.rt_count()));
        if (pb) CHECK(pb->n_entries > 1);
    }
}
