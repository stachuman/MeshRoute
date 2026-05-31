// MeshRoute — test_node_r3.cpp
//
// R3 data plane: the RECEIVER flight (RTS->CTS->DATA->delivered) and the
// last_acked dedup TTL gate — paths the idle/lossless scenario gates (t86/t87)
// do not isolate. Driven through on_recv/on_timer with an in-memory Hal.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main());
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

struct Ev { std::string type; int to = -1; int dst = -1; bool dup = false;
            bool has_payload = false; std::string payload; int depth = -1; int ctr = -1;
            int next = -1; int requeue_count = -1; int reason = -1; int from = -1; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    std::vector<Ev> events;
    int rand_calls = 0;          // guards the cascade #1 determinism risk: no EXTRA draws

    TxResult tx(const uint8_t*, size_t, const TxParams&) override { return TxResult::ok; }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { ++rand_calls; return lo; }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            if      (std::strcmp(f[i].key, "to") == 0)  e.to  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dst") == 0) e.dst = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dest") == 0) e.dst = static_cast<int>(f[i].i);  // rt_update uses "dest"
            else if (std::strcmp(f[i].key, "dup") == 0) e.dup = f[i].b;
            else if (std::strcmp(f[i].key, "depth") == 0) e.depth = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "ctr") == 0)   e.ctr   = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "next") == 0)  e.next  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "from") == 0)  e.from  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "reason") == 0) e.reason = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "requeue_count") == 0) e.requeue_count = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "payload") == 0 && f[i].s) { e.has_payload = true; e.payload = f[i].s; }
        }
        events.push_back(e);
    }
    void     log(const char*) override {}

    int count(const char* t) const { int n = 0; for (const auto& e : events) if (e.type == t) ++n; return n; }
    const Ev* last(const char* t) const { const Ev* r = nullptr; for (const auto& e : events) if (e.type == t) r = &e; return r; }
};

constexpr uint32_t kRtsTimeoutTimerId    = 4;   // mirror node.h's private constants
constexpr uint32_t kAckTimeoutTimerId    = 5;
constexpr uint32_t kCtsToDataGapTimerId  = 7;
constexpr uint32_t kPostAckTimerId       = 9;
constexpr uint32_t kRetryBackoffTimerId  = 10;
constexpr uint32_t kDeferredDrainTimerId = 11;
constexpr uint32_t kCascadeRequeueTimerId = 12;
constexpr uint32_t kNackWaitTimerId      = 13;

static size_t mk_nack(uint8_t to, uint8_t ctr_lo, uint8_t reason, uint8_t payload,
                      std::array<uint8_t, 8>& b) {
    nack_in in{}; in.reason = reason; in.ctr_lo = ctr_lo; in.payload = payload; in.to = to;
    return pack_nack(in, std::span<uint8_t>(b.data(), b.size()));
}

// Pack a 1-entry beacon from `src` advertising route {dest via next, hops} (so a
// receiver installs a candidate to `dest` whose next-hop is `src`). Distinct
// scores let the test pin candidate ordering.
static size_t mk_beacon_route(uint8_t src, uint8_t dest, uint8_t next, uint8_t hops,
                              uint8_t score_bucket, std::array<uint8_t, 64>& b) {
    beacon_entry e{}; e.dest = dest; e.next = next; e.score_bucket = score_bucket;
    e.is_gateway = false; e.hops = hops;
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x2000u + src;
    in.entries = std::span<const beacon_entry>(&e, 1);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}

// Drive a sender's primary next-hop to exhaustion (rts_max_retries=3): each
// RTS-timeout that still has budget decrements + arms the backoff; firing the
// backoff re-sends. After the budget is spent the next RTS-timeout cascades.
static void exhaust_rts_same_hop(Node& node) {
    for (int i = 0; i < 3; ++i) {
        node.on_timer(kRtsTimeoutTimerId);   // retries_left-- (arms kRetryBackoffTimerId)
        node.on_timer(kRetryBackoffTimerId); // re-tx_rts_retry on the SAME hop
    }
    node.on_timer(kRtsTimeoutTimerId);        // retries_left==0 -> cascade_to_alt
}

static size_t mk_rts(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo,
                     uint8_t plen, std::array<uint8_t, 16>& b) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = 0; in.payload_len = plen; in.m_payload_id_lo16 = 0;
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_data(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                      const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 32> inner{}; inner[0] = 0; inner[1] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[2 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.visited = {}; in.inner = std::span<const uint8_t>(inner.data(), 2 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// Minimal beacon FROM `src` (one throwaway entry) — installs a DIRECT hops=1
// route to `src` on the receiver, so a send to `src` has a usable next hop.
static size_t mk_beacon(uint8_t src, std::array<uint8_t, 64>& b) {
    beacon_entry e{}; e.dest = 200; e.next = 201; e.score_bucket = 12; e.is_gateway = false; e.hops = 2;
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x1234;
    in.entries = std::span<const beacon_entry>(&e, 1);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_cts(uint8_t to, uint8_t ctr_lo, uint8_t data_sf, std::array<uint8_t, 8>& b) {
    cts_in in{}; in.ctr_lo = ctr_lo; in.chosen_data_sf = data_sf; in.already_received = false; in.to = to;
    return pack_cts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_ack(uint8_t to, uint8_t ctr_lo, std::array<uint8_t, 8>& b) {
    ack_in in{}; in.ctr_lo = ctr_lo; in.budget_hint = 0; in.snr_bucket = 0; in.to = to;
    return pack_ack(in, std::span<uint8_t>(b.data(), b.size()));
}
static CmdResult send_cmd(Node& node, uint8_t dst, const char* body) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.flags = 0;
    c.body = reinterpret_cast<const uint8_t*>(body);
    c.body_len = static_cast<uint8_t>(std::strlen(body));
    return node.on_command(c);
}

}  // namespace

TEST_CASE("R3 receiver — RTS -> CTS -> DATA -> delivered (we are the destination)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 12; cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };   // immediate sender = bob(1)

    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb);
    CHECK(rn > 0);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    CHECK(hal.count("rts_rx") == 1);
    const Ev* cts = hal.last("cts_tx");
    CHECK(cts != nullptr);
    if (cts) { CHECK(cts->to == 1); CHECK_FALSE(cts->dup); }

    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/0, "hi", db);
    CHECK(dn > 0);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    CHECK(hal.count("data_rx") == 1);
    CHECK(hal.count("ack_tx") == 1);
    node.on_timer(kPostAckTimerId);                          // deliver is deferred by the ACK airtime
    const Ev* dlv = hal.last("delivered");
    CHECK(dlv != nullptr);
    if (dlv) { CHECK(dlv->has_payload); CHECK(dlv->payload == "hi"); }
}

TEST_CASE("R3 dedup — retried RTS within last_acked TTL -> already_received CTS; past TTL -> fresh CTS") {
    TestHal hal; Node node(hal, 2, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 12; cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    const size_t dn = mk_data(2, 2, 0x0005, 0, "hi", db);

    hal._now = 1000; node.on_recv(rb.data(), rn, meta);      // RTS -> CTS, pending_rx
    hal._now = 1500; node.on_recv(db.data(), dn, meta);      // DATA -> delivered + last_acked cached @1500
    node.on_timer(kPostAckTimerId);
    const int cts_before = hal.count("cts_tx");

    // retried RTS @ +5s (within the 10s last_acked TTL) -> already_received CTS (dup).
    hal._now = 6500; node.on_recv(rb.data(), rn, meta);
    CHECK(hal.count("cts_tx") == cts_before + 1);
    const Ev* dup = hal.last("cts_tx");
    CHECK(dup != nullptr);
    if (dup) CHECK(dup->dup);

    // retried RTS @ +20s (past the 10s TTL) -> fresh accept, NORMAL CTS (not dup).
    hal._now = 21500; node.on_recv(rb.data(), rn, meta);
    const Ev* fresh = hal.last("cts_tx");
    CHECK(fresh != nullptr);
    if (fresh) CHECK_FALSE(fresh->dup);
}

// WI-4 (R3.x) concurrency micro-gate. The half-duplex single-flight invariant:
// a 2nd same-priority send enqueued WHILE a flight is in progress must NOT issue
// its RTS until the first flight completes and become_free re-drains the queue.
// This is a pure single-node _pending_tx-gate property — t86/t87 keep depth<=1,
// so only a hand-fed mid-flight ordering exercises it.
TEST_CASE("R3.x concurrency — 2nd send waits behind the in-flight one until become_free re-drains") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);   // self = alice(1)
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 12; cfg.leaf_id = 0;
    node.on_init(cfg);

    // Seed a direct route to bob(2) so issue_send has a next hop.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    { bool saw_route_to_bob = false;      // the direct route to bob(2) the send needs
      for (const auto& ev : hal.events) if (ev.type == "rt_update" && ev.dst == 2) saw_route_to_bob = true;
      CHECK(saw_route_to_bob); }

    // Send #1 -> drains immediately (queue was idle) -> exactly one RTS.
    hal._now = 2000; send_cmd(node, /*dst=*/2, "msg-a");
    CHECK(hal.count("tx_enqueue") == 1);
    CHECK(hal.count("rts_tx")     == 1);

    // Send #2 mid-flight (pending_tx set) -> enqueued, but NO new RTS issues.
    hal._now = 2001; send_cmd(node, /*dst=*/2, "msg-b");
    CHECK(hal.count("tx_enqueue") == 2);
    CHECK(hal.count("rts_tx")     == 1);    // <-- the invariant: still 1, msg-b is queued
    const Ev* enq2 = hal.last("tx_enqueue");
    CHECK(enq2 != nullptr);
    if (enq2) CHECK(enq2->depth == 1);      // one item waiting behind the flight

    // Complete the first flight: CTS -> (gap) -> DATA -> ACK. Still no 2nd RTS
    // until the ACK lands and become_free re-drains.
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*to=*/1, /*ctr_lo=*/1, /*data_sf=*/12, cb);
    CHECK(cn > 0);
    hal._now = 2100; node.on_recv(cb.data(), cn, bob);
    CHECK(hal.count("cts_rx") == 1);
    node.on_timer(kCtsToDataGapTimerId);                 // CTS->DATA gap fires -> DATA tx
    CHECK(hal.count("data_tx") == 1);
    CHECK(hal.count("rts_tx")  == 1);                    // msg-b STILL not issued

    std::array<uint8_t, 8> ab{};
    const size_t an = mk_ack(/*to=*/1, /*ctr_lo=*/1, ab);
    CHECK(an > 0);
    hal._now = 2200; node.on_recv(ab.data(), an, bob);
    CHECK(hal.count("ack_rx") == 1);
    // ACK completes flight #1 -> become_free re-drains -> msg-b's RTS issues now.
    CHECK(hal.count("rts_tx") == 2);
    const Ev* rts2 = hal.last("rts_tx");
    CHECK(rts2 != nullptr);
    if (rts2) CHECK(rts2->ctr == 2);                     // the 2nd RTS is msg-b (ctr=2)
}

// P6 (R3.x) determinism golden: the retry-jitter range is identical-by-
// construction with the Lua. retry_jitter_ms = 3*airtime_routing(RTS_LEN=8).
// At routing SF8/BW125/CR5 that is 3*88 = 264 (Lua-verified). Pins node.cpp's
// use of the literal 8 + the x3 so a future wire shortening can't silently
// de-align the forced-retry mt19937 streams.
TEST_CASE("R3.x golden — retry_jitter_ms == 3*airtime_routing(RTS_LEN=8)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.radio_bw_hz = 125000; cfg.radio_cr = 5;
    node.on_init(cfg);
    CHECK(node.retry_jitter_ms() == 264);   // SF8/BW125/CR5

    TestHal hal7; Node node7(hal7, 1, 0);
    NodeConfig cfg7; cfg7.routing_sf = 7; cfg7.radio_bw_hz = 125000; cfg7.radio_cr = 5;
    node7.on_init(cfg7);
    CHECK(node7.retry_jitter_ms() == 132);  // SF7/BW125/CR5
}

// ---- Cascade-to-alt walk + no-route defer+Q (the cascade milestone) --------
// Seed a sender (alice=1) with K candidates to dest=5 via distinct next-hops,
// ordered by hops so the candidate order is unambiguous (no score tie).
static Node* mk_sender_with_routes(TestHal& hal, std::vector<std::array<uint8_t,3>> vias) {
    // each via = {next_hop_src, hops_advertised, score_bucket}
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    node->on_init(cfg);
    std::array<uint8_t,64> bb{};
    for (auto& v : vias) {
        RxMeta m{12.0f, -70.0f, 0, static_cast<int8_t>(v[0])};
        const size_t n = mk_beacon_route(/*src=*/v[0], /*dest=*/5, /*next=*/9, /*hops=*/v[1], /*score=*/v[2], bb);
        node->on_recv(bb.data(), n, m);
    }
    return node;
}

TEST_CASE("cascade — primary RTS exhausts -> walk to the alternate candidate") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // via2 (h2) primary, via3 (h3) alt
    send_cmd(*node, /*dst=*/5, "hi");
    const Ev* r1 = hal.last("rts_tx"); CHECK(r1 != nullptr);
    if (r1) CHECK(r1->next == 2);                       // first RTS to the primary (via 2)
    const int rand_before = hal.rand_calls;
    exhaust_rts_same_hop(*node);                        // primary fails -> cascade
    CHECK(hal.count("path_cascade") == 1);
    const Ev* pc = hal.last("path_cascade"); CHECK(pc != nullptr);
    if (pc) { CHECK(pc->next == 3); CHECK(pc->dst == 5); }   // walked to the alternate (via 3)
    const Ev* r2 = hal.last("rts_tx"); CHECK(r2 != nullptr);
    if (r2) CHECK(r2->next == 3);                       // re-RTS on the alt
    CHECK(hal.count("rts_giveup") == 0);                // walked, did not give up
    // DETERMINISM (spec risk #1): exactly rts_max_retries(3) same-hop retry-jitter
    // draws on the primary, and ZERO on the cascade switch. An extra draw here would
    // de-align the lua/meshroute mt19937 streams.
    CHECK(hal.rand_calls - rand_before == 3);
    delete node;
}

TEST_CASE("cascade — full K=3 walk: primary -> alt1 -> alt2, then exhaustion requeues") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14},{4,3,14}});  // via2,via3,via4
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);                        // via2 -> via3
    exhaust_rts_same_hop(*node);                        // via3 -> via4
    CHECK(hal.count("path_cascade") == 2);
    const Ev* pc = hal.last("path_cascade"); if (pc) CHECK(pc->next == 4);
    exhaust_rts_same_hop(*node);                        // via4 fails, no untried candidate -> requeue
    CHECK(hal.count("cascade_requeue") == 1);
    const Ev* rq = hal.last("cascade_requeue"); if (rq) CHECK(rq->requeue_count == 1);
    CHECK(hal.count("rts_giveup") == 0);                // requeued, not yet given up
    delete node;
}

TEST_CASE("cascade — single candidate: requeue caps then a true giveup") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2
    hal._now = 1000;                                    // enqueue_time; age cap is 60000 from here
    send_cmd(*node, 5, "hi");
    // Each requeue holds the flight by next_attempt_ms (backoff 5000/10000/20000);
    // advance the clock past each before re-draining (must stay < 61000 so the COUNT
    // cap — not the age cap — is what finally gives up).
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 1);
    hal._now = 6000;  node->on_timer(kCascadeRequeueTimerId);
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 2);
    hal._now = 16000; node->on_timer(kCascadeRequeueTimerId);
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 3);
    hal._now = 36000; node->on_timer(kCascadeRequeueTimerId);
    CHECK(hal.count("rts_giveup") == 0);                // still no giveup after 3 requeues
    exhaust_rts_same_hop(*node);                        // requeue_count==3 -> count cap -> giveup
    CHECK(hal.count("path_cascade_exhausted") == 1);
    CHECK(hal.count("rts_giveup") == 1);
    delete node;
}

TEST_CASE("cascade — single candidate: the total-AGE cap gives up before the count cap") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 1);   // requeue_count=1 (< max 3)
    // Jump past the original enqueue + total-age cap, then re-fly: the age cap fires
    // (now - enqueue_time_ms(1000) >= 60000) even though requeue_count is only 1.
    hal._now = 1000 + protocol::cascade_requeue_total_max_ms + 1;
    node->on_timer(kCascadeRequeueTimerId);            // re-drain -> re-fly
    exhaust_rts_same_hop(*node);                       // exhausts -> try_cascade_requeue -> age cap -> giveup
    CHECK(hal.count("cascade_requeue") == 1);          // NOT a 2nd requeue
    CHECK(hal.count("path_cascade_exhausted") == 1);
    CHECK(hal.count("rts_giveup") == 1);
    delete node;
}

TEST_CASE("cascade — ACK-timeout resets the flight (re-RTS) before walking") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});
    send_cmd(*node, 5, "hi");                           // RTS to via 2
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*to=*/1, /*ctr_lo=*/1, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);                   // CTS -> gap timer
    node->on_timer(kCtsToDataGapTimerId);               // -> DATA tx (awaiting_ack)
    CHECK(hal.count("data_tx") == 1);
    const int rts_before = hal.count("rts_tx");
    node->on_timer(kAckTimeoutTimerId);                 // ACK lost: reset awaiting flags, arm backoff
    node->on_timer(kRetryBackoffTimerId);               // -> re-RTS (NOT stuck in awaiting_ack)
    CHECK(hal.count("rts_tx") == rts_before + 1);       // the flight re-RTS'd on the same hop (via 2)
    const Ev* rr = hal.last("rts_tx"); if (rr) CHECK(rr->next == 2);
    delete node;
}

TEST_CASE("cascade — ACK-timeout exhaustion walks to the alternate (Risk #5 full path)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});
    send_cmd(*node, 5, "hi");
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*to=*/1, /*ctr_lo=*/1, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);               // -> DATA (awaiting_ack, retries_left=3)
    CHECK(hal.count("data_tx") == 1);
    // First ACK-timeout resets to a re-RTS (retries 3->2); the remaining failures are
    // RTS-timeouts (the flight is back in awaiting_cts) until retries_left==0 -> cascade.
    node->on_timer(kAckTimeoutTimerId);  node->on_timer(kRetryBackoffTimerId);   // 3->2, re-RTS
    node->on_timer(kRtsTimeoutTimerId);  node->on_timer(kRetryBackoffTimerId);   // 2->1, re-RTS
    node->on_timer(kRtsTimeoutTimerId);  node->on_timer(kRetryBackoffTimerId);   // 1->0, re-RTS
    node->on_timer(kRtsTimeoutTimerId);                                          // 0 -> cascade
    CHECK(hal.count("path_cascade") == 1);
    const Ev* pc = hal.last("path_cascade"); if (pc) CHECK(pc->next == 3);       // walked to the alt
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 3);                // fresh RTS on the alt (awaiting_cts)
    delete node;
}

TEST_CASE("defer — originator send with no route is held, then drained when a route appears") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    send_cmd(node, /*dst=*/5, "later");                 // no route -> DEFER (not drop)
    CHECK(hal.count("send_deferred") == 1);
    CHECK(hal.count("send_no_route") == 0);             // originator defers, never drops
    CHECK(hal.count("rts_tx") == 0);
    // a beacon installs a route to 5 -> drain-on-rt_changed -> the held send flies
    std::array<uint8_t,64> bb{}; RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    const size_t n = mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), n, m2);
    CHECK(hal.count("rts_tx") >= 1);
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 2);
}

TEST_CASE("defer — TTL-first giveup: a held send with no route ages out on the periodic drain") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    send_cmd(node, 5, "lost");
    CHECK(hal.count("send_deferred") == 1);
    hal._now = 1000 + protocol::send_defer_ttl_ms + 1;  // past the defer TTL, still no route
    node.on_timer(kDeferredDrainTimerId);               // periodic drain -> TTL giveup (checked BEFORE route-exists)
    CHECK(hal.count("send_deferred_giveup") == 1);
    CHECK(hal.count("rts_tx") == 0);
}

TEST_CASE("defer — TTL-FIRST beats route-exists: past-TTL held send gives up even when a route arrives") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    send_cmd(node, 5, "trapped");                       // deferred (no route)
    CHECK(hal.count("send_deferred") == 1);
    // Age PAST the TTL, THEN install a route. The beacon triggers try_drain_deferred,
    // which checks TTL BEFORE route-exists -> the send gives up, it does NOT fly. This
    // is the s12 defer_ttl_route_exists_trap guard: a route arriving past-TTL must not
    // resurrect a stale held send.
    hal._now = 1000 + protocol::send_defer_ttl_ms + 1;
    std::array<uint8_t,64> bb{}; RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    const size_t n = mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), n, m2);                     // route to 5 now exists, but TTL already passed
    CHECK(hal.count("send_deferred_giveup") == 1);      // TTL-first -> giveup
    CHECK(hal.count("rts_tx") == 0);                    // did NOT fly despite the fresh route
}

// ---- NACK plane (BUSY_RX + LOOP_DUP) ---------------------------------------
TEST_CASE("nack — BUSY_RX emit: a 2nd-flight RTS into a busy receiver gets a NACK") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    const size_t rn = mk_rts(/*src=*/2, /*next=*/1, /*dst=*/9, /*ctr_lo=*/5, /*plen=*/10, rb);
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; node.on_recv(rb.data(), rn, m2);   // flight A -> CTS + pending_rx
    CHECK(hal.count("cts_tx") >= 1);
    const size_t rn2 = mk_rts(/*src=*/3, /*next=*/1, /*dst=*/8, /*ctr_lo=*/7, /*plen=*/10, rb);
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 1100; node.on_recv(rb.data(), rn2, m3);  // flight B (different) -> BUSY_RX NACK to 3
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == 0); }
}

TEST_CASE("nack — a busy SENDER (pending_tx) stays SILENT (no NACK)") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                            // pending_tx via 2
    std::array<uint8_t,16> rb{};
    const size_t rn = mk_rts(3, 1, 8, 7, 10, rb); RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    node->on_recv(rb.data(), rn, m3);
    CHECK(hal.count("nack_tx") == 0);                   // SILENT while sending (busy_for would lie)
    CHECK(hal.count("rts_drop_pending_tx") == 1);
    delete node;
}

TEST_CASE("nack — BUSY_RX recovery (short busy): mark blind + nack_wait re-RTS SAME hop, ONE new draw") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                            // pending_tx via 2, ctr_lo=1
    const int rts_before = hal.count("rts_tx");
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/1, /*reason=*/0, /*payload=*/10, nb);  // busy 160ms <= 2000
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("nack_rx") == 1);
    CHECK(hal.count("blind_observed") == 1);            // via 2 marked blind
    CHECK(hal.count("tx_requeued") == 0);               // short busy -> wait, NOT requeue
    CHECK(hal.rand_calls - rand_before == 1);           // N1: exactly ONE new draw
    node->on_timer(kNackWaitTimerId);                   // wait elapsed -> re-RTS SAME hop
    CHECK(hal.count("rts_tx") == rts_before + 1);
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 2);   // same hop (BUSY_RX never path-switches)
    delete node;
}

TEST_CASE("nack — BUSY_RX recovery (long busy): blind + requeue, the re-issue SKIPS the blind hop") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});  // via2 primary, via3 alt
    send_cmd(*node, 5, "hi");                            // pending_tx via 2
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, 0, /*payload=*/200, nb);   // busy 3200ms > 2000 -> requeue
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("blind_observed") == 1);            // via 2 blind
    CHECK(hal.count("tx_requeued") == 1);               // long busy -> requeue (next_attempt=0 -> re-issues now)
    const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
    if (r) CHECK(r->next == 3);                          // is_blind(2) -> the re-issue picks via 3, not the blind via 2
    delete node;
}

TEST_CASE("nack — LOOP_DUP recovery: cascade to the alternate (NO new draw)") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{4,2,14}});  // via2 primary, via4 alt
    send_cmd(*node, 5, "hi");                            // pending_tx via 2
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, /*reason=*/3, /*payload=*/9, nb);   // LOOP_DUP
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("path_cascade") == 1);
    CHECK(hal.count("tx_loop_alt") == 1);
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 4);   // cascaded to via 4
    CHECK(hal.rand_calls - rand_before == 0);           // the LOOP_DUP re-RTS draws NO jitter
    delete node;
}

TEST_CASE("nack — LOOP_DUP miss: DIRECT giveup, NOT requeue (Lua dv:10588)") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2
    send_cmd(*node, 5, "hi");
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, 3, 9, nb); RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("path_cascade_exhausted") == 1);
    CHECK(hal.count("rts_giveup") == 1);
    CHECK(hal.count("cascade_requeue") == 0);           // DIRECT giveup, NOT a requeue
    delete node;
}

TEST_CASE("nack — LOOP_DUP emit: same flight via a different prev-hop NACKs the sender") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};                        // merge(1) needs a route to dst=5 to forward
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // copy 1 via b1=2: RTS+DATA (origin0,dst5,ctr10) -> ACK + record seen_origin_from=2 + (forward pending)
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,10,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data(1,5,10,0,"x",db); node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("ack_tx") >= 1);
    // copy 2 via b2=3: SAME (origin,dst,ctr) -> prev-hop 3 != recorded 2 -> LOOP_DUP NACK to 3, NO ACK
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    const int ack_before = hal.count("ack_tx");
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,10,10,rb); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data(1,5,10,0,"x",db); node.on_recv(db.data(), dn, m3); }
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == 3); }
    CHECK(hal.count("dup_drop") >= 1);
    CHECK(hal.count("ack_tx") == ack_before);           // the looped dup was NOT re-ACKed
}

TEST_CASE("cascade — equal-score candidates keep INSERTION order (Lua-faithful, NO id tie-break)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    RxMeta m9{12.0f, -70.0f, 0, static_cast<int8_t>(9)};   // via 9 arrives FIRST
    RxMeta m4{12.0f, -70.0f, 0, static_cast<int8_t>(4)};   // via 4 arrives second
    size_t n;
    // advertised next=7 (a third party — not self=1, not the beacon senders 9/4,
    // else split-horizon would drop the route). Equal hops(1)+score(14) => a true tie.
    n = mk_beacon_route(9, 5, 7, 1, 14, bb); node.on_recv(bb.data(), n, m9);
    n = mk_beacon_route(4, 5, 7, 1, 14, bb); node.on_recv(bb.data(), n, m4);
    send_cmd(node, 5, "tie");
    const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
    // via 9 arrived first -> stays primary (insertion order), exactly like the Lua
    // (route_strictly_better returns false on a tie). An id tie-break would wrongly
    // pick via 4 and DIVERGE from the Lua reference.
    if (r) CHECK(r->next == 9);
}
