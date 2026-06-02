// MeshRoute — test_node_r3.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
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

struct TxFrame { std::string label; std::vector<uint8_t> bytes; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    std::vector<Ev> events;
    std::vector<TxFrame> tx_frames;   // captured TX bytes (to parse DATA hop-budget fields)
    int rand_calls = 0;          // guards the cascade #1 determinism risk: no EXTRA draws

    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        TxFrame f; f.label = p.label ? p.label : "";
        f.bytes.assign(b, b + n); tx_frames.push_back(std::move(f));
        return TxResult::ok;
    }
    void     set_rx_sf(int) override {}
    uint64_t _channel_busy_until = 0;   // R4.5: scriptable LBT busy horizon
    uint64_t channel_busy_until() override { return _channel_busy_until; }
    uint64_t _airtime_used = 0;   // R4.0: scriptable rolling-window airtime for compute_budget_tier
    uint64_t airtime_used_ms(uint64_t) override { return _airtime_used; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      _rand_ret = -1;   // opt-in scriptable rand (>=0 overrides the default `return lo`; -1 = default)
    int      rand_range(int lo, int) override { ++rand_calls; return _rand_ret >= 0 ? _rand_ret : lo; }
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
    const TxFrame* last_tx(const char* label) const {
        const TxFrame* r = nullptr; for (const auto& f : tx_frames) if (f.label == label) r = &f; return r;
    }
};

constexpr uint32_t kRtsTimeoutTimerId    = 4;   // mirror node.h's private constants
constexpr uint32_t kAckTimeoutTimerId    = 5;
constexpr uint32_t kCtsToDataGapTimerId  = 7;
constexpr uint32_t kPostAckTimerId       = 9;
constexpr uint32_t kRetryBackoffTimerId  = 10;
constexpr uint32_t kDeferredDrainTimerId = 11;
constexpr uint32_t kCascadeRequeueTimerId = 12;
constexpr uint32_t kNackWaitTimerId      = 13;
constexpr uint32_t kTriggeredBeaconTimerId = 3;   // R4.2: rerank re-advertises via a triggered beacon
constexpr uint32_t kBeaconTimerId        = 1;     // R4.3 periodic beacon fire
constexpr uint32_t kBeaconJitterTimerId  = 27;    // R4.3 silence-jitter deferred beacon (#D ring base [27..30])
constexpr uint32_t kLbtDeferTimerId      = 15;    // R4.5 LBT busy-channel deferred TX
constexpr uint32_t kRadioBusyRetryTimerId = 19;   // R4.5b on_radio_busy stash-retry (slot base)
constexpr uint32_t kDutyDeferTimerId      = 23;   // #2 tx_with_retry duty-defer re-run (slot base)
constexpr uint32_t kRtsDutyDeferTimerId   = 31;   // #A redo: over-budget RTS duty-defer re-check/hand

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
// DATA with explicit hop-budget fields (for the HOP_BUDGET enforcement tests).
static size_t mk_data_hb(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                         uint8_t hops_remaining, uint8_t committed,
                         const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 32> inner{}; inner[0] = 0; inner[1] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[2 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.hops_remaining = hops_remaining; in.committed_hops = committed; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
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
static size_t mk_cts(uint8_t rx_id, uint8_t tx_id, uint8_t data_sf, std::array<uint8_t, 8>& b) {
    cts_in in{}; in.chosen_data_sf = data_sf; in.already_received = false; in.tx_id = tx_id; in.rx_id = rx_id;
    return pack_cts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_ack_hint(uint8_t to, uint8_t ctr_lo, uint8_t budget_hint, std::array<uint8_t, 8>& b) {
    ack_in in{}; in.ctr_lo = ctr_lo; in.budget_hint = budget_hint; in.snr_bucket = 0; in.to = to;
    return pack_ack(in, std::span<uint8_t>(b.data(), b.size()));
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
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
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
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
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
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
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

// ---- HOP_BUDGET enforcement ------------------------------------------------
static std::optional<data_out> parse_tx_data(const TxFrame* d) {
    if (!d) return std::nullopt;
    return parse_data(std::span<const uint8_t>(d->bytes.data(), d->bytes.size()));
}

TEST_CASE("hop_budget — originator initial budget = min(31, rt_hops + slack)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,2,14}});   // candidate to 5 via 2: hops = 2+1 = 3
    send_cmd(*node, 5, "hi");                               // pending_tx via 2, ctr_lo=1
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    const int rand_before = hal.rand_calls;                // the budget block is pure arithmetic ...
    node->on_timer(kCtsToDataGapTimerId);                  // -> DATA tx
    CHECK(hal.rand_calls - rand_before == 0);              // ... no new draw (determinism golden, review #16)
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) {
        CHECK(pd->hops_remaining   == 6);                  // min(31, 3 + slack(3))
        CHECK(pd->committed_hops   == 0);
        CHECK(pd->prev_fwd_rt_hops == 3);                  // self's rt[5].hops, re-stamped
    }
    delete node;
}

TEST_CASE("hop_budget — forwarder with hops_remaining==0 NACKs HOP_BUDGET (no ACK, no forward)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(7, 5, 9, 1, 14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);   // route to 5
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2, 1, 5, 3, 10, rb); node.on_recv(rb.data(), rn, m2);       // CTS + pending_rx
    const int ack_before = hal.count("ack_tx"), data_before = hal.count("data_tx");
    const size_t dn = mk_data_hb(/*next=*/1, /*dst=*/5, /*ctr=*/3, /*origin=*/7,
                                 /*hops_remaining=*/0, /*committed=*/2, "x", db);
    node.on_recv(db.data(), dn, m2);
    CHECK(hal.count("hop_budget_exceeded") == 1);
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 2); CHECK(nk->reason == 2); }
    CHECK(hal.count("ack_tx") == ack_before);              // NO ACK (NACK in lieu of)
    node.on_timer(kPostAckTimerId);                        // (nothing armed)
    CHECK(hal.count("data_tx") == data_before);            // NO forward
}

TEST_CASE("hop_budget — destination is EXEMPT: hops_remaining==0 AT the dst delivers, no NACK") {
    TestHal hal; Node node(hal, 5, 0xABCD);                // self == the destination
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2, 5, 5, 3, 10, rb); node.on_recv(rb.data(), rn, m2);
    const size_t dn = mk_data_hb(/*next=*/5, /*dst=*/5, /*ctr=*/3, /*origin=*/7, 0, 0, "hi", db);
    node.on_recv(db.data(), dn, m2);
    CHECK(hal.count("ack_tx")  == 1);
    CHECK(hal.count("nack_tx") == 0);                      // dest exempt
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 1);
}

TEST_CASE("hop_budget — forwarder decrements: arriving remaining=2 forwards with remaining=1") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(7, 5, 9, 1, 14, bb);   // route to 5 via 7
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2, 1, 5, 3, 10, rb); node.on_recv(rb.data(), rn, m2);
    const size_t dn = mk_data_hb(1, 5, 3, 7, /*hops_remaining=*/2, /*committed=*/1, "x", db);
    node.on_recv(db.data(), dn, m2);                       // pass -> ACK + forward queued
    CHECK(hal.count("ack_tx") == 1);
    node.on_timer(kPostAckTimerId);                        // do_post_ack -> forward TxItem -> issue_send -> RTS to 7
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(1, 7, 7, cb);                 // CTS from 7 for the forward (tx_id=7)
    node.on_recv(cb.data(), cn, m7);
    node.on_timer(kCtsToDataGapTimerId);                   // -> forwarded DATA tx
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) { CHECK(pd->hops_remaining == 1); CHECK(pd->committed_hops == 2);   // decremented (2->1, committed 1->2)
              CHECK(pd->prev_fwd_rt_hops == 2); }   // re-stamped to self's rt[5].hops (beacon hops 1 + 1)
}

TEST_CASE("hop_budget — sender NACK recovery: terminal giveup + rt.hops bump feeds the NEXT budget") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,2,14}});   // candidate to 5 via 2, hops = 3
    send_cmd(*node, 5, "hi");                               // flight 1 (ctr_lo=1) via 2
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/1, /*reason=*/2, /*payload=*/(4 << 4), nb);  // committed=4
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("rts_giveup")   == 1);                  // TERMINAL giveup
    CHECK(hal.count("path_cascade") == 0);                  // NO cascade
    CHECK(hal.rand_calls - rand_before == 0);               // NO rand on the HOP_BUDGET path
    // the rt.hops bump (3 -> max(3, committed+1=5) = 5) feeds the NEXT send: min(31, 5+3) = 8.
    send_cmd(*node, 5, "hi2");                              // flight 2 (ctr_lo=2)
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(1, 2, 7, cb); node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) CHECK(pd->hops_remaining == 8);                // reflects the bumped rt.hops = 5
    delete node;
}

// Locks the handle_data REORDER + the exhaustion-path seen_origins write (review #04/#05/#10):
// Lua runs HOP_BUDGET ABOVE the loop-dup dedup AND records (origin,dst,ctr) on exhaustion, so a
// LATER non-exhausted arrival of the SAME flight via a DIFFERENT prev-hop is caught as LOOP_DUP
// (not accepted+forwarded). Before the fix the C++ ran dedup first and skipped the write.
TEST_CASE("hop_budget — an exhausted frame records seen_origins so a later diff-prev-hop arrival is LOOP_DUP") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(7, 5, 9, 1, 14, bb);   // route to 5 (could forward)
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // copy 1 via prev-hop 2: arrives EXHAUSTED (hops_remaining==0) -> HOP_BUDGET NACK + records
    // seen_origin_from[(origin0,dst5,ctr3)] = 2.
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,3,10,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_hb(1,5,3,0,/*hops_remaining=*/0,/*committed=*/2,"x",db);
                       node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("hop_budget_exceeded") == 1);
    {   // the REAL emitted NACK bytes carry reason=hop_budget + committed in the HIGH nibble (review #03)
        auto pn = parse_nack(std::span<const uint8_t>(hal.last_tx("NACK")->bytes.data(),
                                                      hal.last_tx("NACK")->bytes.size()));
        CHECK(pn.has_value());
        if (pn) { CHECK(pn->reason == protocol::nack_reason_hop_budget);
                  CHECK(((pn->payload >> 4) & 0x0f) == 3); }   // committed 2 -> +1 -> 3
    }
    const int ack_after_copy1 = hal.count("ack_tx");
    // copy 2 via prev-hop 3: SAME (origin,dst,ctr) but NOT exhausted -> HOP_BUDGET passes, then the
    // dedup finds prior_from 2 != 3 -> LOOP_DUP NACK to 3, NO ACK.
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,3,10,rb); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data_hb(1,5,3,0,/*hops_remaining=*/5,/*committed=*/1,"x",db);
                       node.on_recv(db.data(), dn, m3); }
    const Ev* nk2 = hal.last("nack_tx"); CHECK(nk2 != nullptr);
    if (nk2) { CHECK(nk2->to == 3); CHECK(nk2->reason == protocol::nack_reason_loop_dup); }   // LOOP_DUP, not HOP_BUDGET
    CHECK(hal.count("dup_drop") >= 1);
    CHECK(hal.count("ack_tx") == ack_after_copy1);            // the looped dup was NOT ACKed
}

// Locks the requeue budget-threading fix (review #00): a FORWARDED flight that hits a long-busy
// BUSY_RX NACK must keep its inherited hop budget across the requeue. Before the fix the rebuilt
// TxItem zeroed fwd_remaining, so the re-issued DATA carried hops_remaining=0 and the NEXT hop
// terminally HOP_BUDGET-killed an in-transit message that had ample budget.
TEST_CASE("hop_budget — a forwarded flight keeps its budget across a BUSY_RX long-busy requeue") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    { const size_t n = mk_beacon_route(7, 5, 9, 1, 14, bb);     // route to 5 via 7 (primary, score 14)
      RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), n, m7); }
    { const size_t n = mk_beacon_route(8, 5, 9, 1, 13, bb);     // route to 5 via 8 (alt, score 13)
      RxMeta m8{12.0f,-70.0f,0,static_cast<int8_t>(8)}; node.on_recv(bb.data(), n, m8); }
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // become a forwarder: DATA from prev-hop 2 (origin0,dst5,ctr4) arriving remaining=4 -> decrement
    // to fwd_remaining=3, ACK, forward queued.
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2,1,5,4,10,rb); node.on_recv(rb.data(), rn, m2);
    const size_t dn = mk_data_hb(1,5,4,0,/*hops_remaining=*/4,/*committed=*/1,"x",db);
    node.on_recv(db.data(), dn, m2);
    node.on_timer(kPostAckTimerId);                            // do_post_ack -> forward via 7 (RTS to 7)
    { const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr); if (r) CHECK(r->next == 7); }
    // long-busy BUSY_RX NACK from 7 (busy 3200ms > 2000) -> mark 7 blind + requeue the forward.
    std::array<uint8_t,8> nb{}; const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/4, /*reason=*/0, /*payload=*/200, nb);
    RxMeta m7n{8.0f,-80.0f,0,static_cast<int8_t>(7)}; node.on_recv(nb.data(), nn, m7n);
    CHECK(hal.count("tx_requeued") == 1);
    { const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr); if (r) CHECK(r->next == 8); }   // re-issued via the alt
    // CTS from 8 -> forwarded DATA must STILL carry the inherited budget (3), not 0.
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/8, /*data_sf=*/7, cb);
    RxMeta m8c{12.0f,-70.0f,0,static_cast<int8_t>(8)}; node.on_recv(cb.data(), cn, m8c);
    node.on_timer(kCtsToDataGapTimerId);                       // -> forwarded DATA tx
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) { CHECK(pd->hops_remaining == 3); CHECK(pd->committed_hops == 2); }   // budget survived the requeue
}

// ---- R4.0 + R4.1 — duty-cycle budget tier + BUDGET NACK (reason 1) ----------
static Node* mk_budget_node(TestHal& hal, double duty_cycle, uint32_t window_ms) {
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.duty_cycle = duty_cycle; cfg.duty_cycle_window_ms = window_ms;
    node->on_init(cfg);
    return node;
}

TEST_CASE("R4.0 budget tier — thresholds 50/80/95 + disabled = HEALTHY (Lua dv:3560-3571)") {
    using BT = Node::BudgetTier;
    TestHal hal;
    // window 1000ms, duty 0.10 -> budget = floor(0.10*1000) = 100ms. pct = 100*used/100 = used.
    Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/1000);
    const int rand0 = hal.rand_calls;
    hal._airtime_used = 0;   CHECK(node->compute_budget_tier() == BT::healthy);    // 0%
    hal._airtime_used = 49;  CHECK(node->compute_budget_tier() == BT::healthy);    // 49% < 50
    hal._airtime_used = 50;  CHECK(node->compute_budget_tier() == BT::strained);   // 50% -> STRAINED
    hal._airtime_used = 79;  CHECK(node->compute_budget_tier() == BT::strained);   // 79% < 80
    hal._airtime_used = 80;  CHECK(node->compute_budget_tier() == BT::critical);   // 80% -> CRITICAL
    hal._airtime_used = 94;  CHECK(node->compute_budget_tier() == BT::critical);   // 94% < 95
    hal._airtime_used = 95;  CHECK(node->compute_budget_tier() == BT::exhausted);  // 95% -> EXHAUSTED
    hal._airtime_used = 200; CHECK(node->compute_budget_tier() == BT::exhausted);  // >100%
    CHECK(hal.rand_calls - rand0 == 0);   // pure, no draws
    delete node;
    // duty_cycle <= 0 -> disabled -> always HEALTHY even at saturation
    TestHal hal2; Node* off = mk_budget_node(hal2, /*duty=*/0.0, /*window=*/1000);
    hal2._airtime_used = 1000000; CHECK(off->compute_budget_tier() == BT::healthy);
    delete off;
    // plumb-proof (review #12): the r6 gate values (0.1, 1h) derive a NON-ZERO budget (360000ms),
    // so the tier crosses HEALTHY->STRAINED at 50% (180000ms) — not a silent disabled no-op.
    TestHal hal3; Node* r6 = mk_budget_node(hal3, /*duty=*/0.1, /*window=*/3600000);
    hal3._airtime_used = 179999; CHECK(r6->compute_budget_tier() == BT::healthy);    // 49.99% < 50
    hal3._airtime_used = 180000; CHECK(r6->compute_budget_tier() == BT::strained);   // 50% of 360000ms
    delete r6;
}

TEST_CASE("R4.1 budget NACK emit — receiver >=CRITICAL refuses an RTS with reason=1 (tier in high nibble)") {
    std::array<uint8_t,16> rb{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    // HEALTHY (10%) -> normal CTS, NO budget NACK
    {
        TestHal hal; Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/1000);   // budget 100ms
        hal._airtime_used = 10;
        const size_t rn = mk_rts(/*src=*/2,/*next=*/1,/*dst=*/9,/*ctr_lo=*/5,/*plen=*/10, rb);
        node->on_recv(rb.data(), rn, m2);
        CHECK(hal.count("cts_tx") == 1);
        CHECK(hal.count("nack_tx") == 0);
        delete node;
    }
    // CRITICAL (85%) on a FRESH node (no stale pending_rx that would BUSY_RX first) -> BUDGET NACK
    // reason=1, tier=2 in the high nibble, NO CTS.
    {
        // Budget 10000ms (window 100000 @ 10%): pct still 85% -> CRITICAL, but realistic enough that the ~36ms NACK
        // FITS the budget (8500+36 <= 10000) so tx_with_retry's duty pre-check (#2) doesn't defer it. A tiny 100ms
        // budget would faithfully duty-DEFER the NACK (the Lua does too) — production 1%/1h budgets fit it easily.
        TestHal hal; Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/100000);
        hal._airtime_used = 8500;
        const size_t rn = mk_rts(/*src=*/3,/*next=*/1,/*dst=*/8,/*ctr_lo=*/6,/*plen=*/10, rb);
        RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)}; node->on_recv(rb.data(), rn, m3);
        const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
        if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == protocol::nack_reason_budget); }
        CHECK(hal.count("cts_tx") == 0);   // NO CTS on the refused RTS
        {   // the REAL emitted NACK bytes carry tier=CRITICAL(2) in the high nibble
            auto pn = parse_nack(std::span<const uint8_t>(hal.last_tx("NACK")->bytes.data(),
                                                          hal.last_tx("NACK")->bytes.size()));
            CHECK(pn.has_value());
            if (pn) { CHECK(pn->reason == protocol::nack_reason_budget);
                      CHECK(((pn->payload >> 4) & 0x0f) == 2); }
        }
        delete node;
    }
}

TEST_CASE("R4.1 budget NACK react — sender blinds the next hop (tier-scaled) + requeues, no draws") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // via2 primary, via3 alt
    hal._now = 1000;
    send_cmd(*node, 5, "hi");                                       // pending_tx via 2, ctr_lo=1
    CHECK(!node->is_blind(2));
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    // BUDGET NACK reason=1, tier=CRITICAL(2) -> blind via2 for budget_blind_critical_ms, requeue.
    const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/1, /*reason=*/protocol::nack_reason_budget,
                              /*payload=*/(2 << 4), nb);
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("blind_observed") == 1);
    CHECK(node->is_blind(2));                                       // via2 now blind
    { const Ev* nr = hal.last("nack_rx"); CHECK(nr != nullptr);
      if (nr) { CHECK(nr->reason == protocol::nack_reason_budget); } }
    CHECK(hal.count("cascade_requeue") == 1);                       // requeued via the helper (caps not hit)
    CHECK(hal.rand_calls - rand_before == 0);                      // DRAW-FREE
    // drain the backoff (requeue_count=1 -> backoff 5000): the re-issue skips the blind via2 -> via3
    hal._now = 6000; node->on_timer(kCascadeRequeueTimerId);
    CHECK(node->is_blind(2));                                       // still blind at 6000 (window 180000)
    { const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
      if (r) CHECK(r->next == 3); }
    delete node;
}

TEST_CASE("R4.1 budget NACK react — tier-scaled blind window (STRAINED < CRITICAL < EXHAUSTED)") {
    // EXHAUSTED(3) gets the longest window; probe is_blind just past the strained window to show
    // EXHAUSTED still blind there while a STRAINED-tier blind would have lapsed.
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        send_cmd(*node, 5, "hi");
        std::array<uint8_t,8> nb{};
        const size_t nn = mk_nack(1, 1, protocol::nack_reason_budget, (3 << 4), nb);   // EXHAUSTED
        RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
        hal._now = protocol::budget_blind_strained_ms + 1;        // past the STRAINED window
        CHECK(node->is_blind(2));                                 // EXHAUSTED window is longer -> still blind
        hal._now = protocol::budget_blind_exhausted_ms + 1;       // past the EXHAUSTED window
        CHECK(!node->is_blind(2));
        delete node;
    }
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        send_cmd(*node, 5, "hi");
        std::array<uint8_t,8> nb{};
        const size_t nn = mk_nack(1, 1, protocol::nack_reason_budget, (1 << 4), nb);   // STRAINED
        RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
        hal._now = protocol::budget_blind_strained_ms + 1;        // past the STRAINED window
        CHECK(!node->is_blind(2));                                // STRAINED window already lapsed
        delete node;
    }
}

// ---- R4.2 — persistent neighbor tier mark + route penalty + ACK budget_hint ----
TEST_CASE("R4.2 tier mark — max-merge (no downgrade) + tier-0 no-op + TTL lazy-prune") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    hal._now = 1000;
    CHECK(node->get_neighbor_tier(2) == 0);                       // unmarked
    node->mark_neighbor_budget_tier(2, /*CRITICAL*/2, "test", true);
    CHECK(node->get_neighbor_tier(2) == 2);
    node->mark_neighbor_budget_tier(2, /*STRAINED*/1, "test", true);   // lower -> max-merge keeps CRITICAL (dv:4323)
    CHECK(node->get_neighbor_tier(2) == 2);
    node->mark_neighbor_budget_tier(2, /*EXHAUSTED*/3, "test", true);  // higher -> upgrades
    CHECK(node->get_neighbor_tier(2) == 3);
    node->mark_neighbor_budget_tier(2, /*HEALTHY*/0, "test", true);    // tier 0 -> no-op
    CHECK(node->get_neighbor_tier(2) == 3);
    hal._now = 1000 + protocol::neighbor_budget_tier_ttl_ms;      // >= TTL from the last set -> lazy prune on read
    CHECK(node->get_neighbor_tier(2) == 0);
    delete node;
}

TEST_CASE("R4.2 route demotion — marking a CRITICAL primary reranks it below the viable alt") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,1,14}});  // via2,via3 EQUAL -> via2 primary
    node->on_timer(kTriggeredBeaconTimerId);                      // flush any pending triggered beacon
    const int rb = hal.rand_calls;
    const int reranked = node->mark_neighbor_budget_tier(2, /*CRITICAL*/2, "nack_budget", /*local_only=*/false);
    CHECK(reranked == 1);                                         // the primary moved
    CHECK(hal.rand_calls - rb == 1);                             // !local_only + primary moved -> ONE triggered-beacon draw
    CHECK(node->get_neighbor_tier(2) == 2);
    CHECK(hal.count("rt_penalty_rerank") == 1);
    CHECK(hal.count("neighbor_budget_mark") == 1);
    send_cmd(*node, 5, "x");                                      // now routes via the alt (via2 demoted, NOT blind)
    const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
    if (r) CHECK(r->next == 3);
    delete node;
}

TEST_CASE("R4.2 BUDGET NACK reaction reranks the route; demotion OUTLIVES the blind window") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,1,14}});  // equal -> via2 primary
    node->on_timer(kTriggeredBeaconTimerId);
    hal._now = 1000;
    send_cmd(*node, 5, "hi");                                     // RTS to via2
    { const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 2); }
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, protocol::nack_reason_budget, (2 << 4), nb);   // CRITICAL from via2
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("neighbor_budget_mark") == 1);               // the react marked via2 ...
    CHECK(hal.count("rt_penalty_rerank") == 1);                 // ... and reranked the route
    CHECK(node->get_neighbor_tier(2) == 2);
    // route demotion (tier TTL 300000) OUTLIVES the short blind window (CRITICAL 180000): past the
    // blind but within the TTL, via2 is no longer blind yet still tier-marked -> route stays demoted.
    hal._now = 1000 + protocol::budget_blind_critical_ms + 1;
    CHECK(!node->is_blind(2));
    CHECK(node->get_neighbor_tier(2) == 2);
    delete node;
}

TEST_CASE("R4.2 ACK budget_hint — STRAINED forwarder's ACK carries the tier; sender marks it (local_only, no draw)") {
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // EMIT: a STRAINED forwarder (60%) still CTSes (only >=CRITICAL refuses) and ACKs with budget_hint=STRAINED.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
        cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 1000;   // budget 100ms
        node.on_init(cfg);
        hal._airtime_used = 60;                                   // 60% -> STRAINED (CTSes, doesn't refuse)
        RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
        const size_t rn = mk_rts(/*src=*/2,/*next=*/1,/*dst=*/1,/*ctr_lo=*/3,/*plen=*/10, rb);  // dst=self -> deliver path
        node.on_recv(rb.data(), rn, m2);
        CHECK(hal.count("cts_tx") == 1);                          // STRAINED still CTSes
        const size_t dn = mk_data_hb(1, 1, 3, 0, /*hops_remaining=*/5, /*committed=*/0, "x", db);
        node.on_recv(db.data(), dn, m2);
        auto pk = parse_ack(std::span<const uint8_t>(hal.last_tx("ACK")->bytes.data(),
                                                     hal.last_tx("ACK")->bytes.size()));
        CHECK(pk.has_value());
        if (pk) CHECK(pk->budget_hint == 1);                     // STRAINED in the ACK (min(CRITICAL, tier))
    }
    // CONSUME: a sender receiving an ACK with budget_hint=STRAINED marks the next-hop (local_only -> NO beacon/draw).
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        node->on_timer(kTriggeredBeaconTimerId);
        send_cmd(*node, 5, "hi");                                 // RTS to via2
        std::array<uint8_t,8> cb{};
        const size_t cn = mk_cts(1, 2, 7, cb);
        RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
        node->on_timer(kCtsToDataGapTimerId);                    // DATA tx -> awaiting_ack
        const int rb2 = hal.rand_calls;
        std::array<uint8_t,8> ab{};
        const size_t an = mk_ack_hint(/*to=*/1, /*ctr_lo=*/1, /*budget_hint=*/1, ab);   // ACK from via2, STRAINED
        node->on_recv(ab.data(), an, m2);
        CHECK(node->get_neighbor_tier(2) == 1);                  // marked from the ACK
        CHECK(hal.rand_calls - rb2 == 0);                        // local_only -> NO triggered-beacon draw
        delete node;
    }
}

// review #07: a node STRAINED at RTS-time (so it CTSes, doesn't refuse) that climbs to EXHAUSTED by
// DATA-time -> the forward ACK hint must CAP at CRITICAL(2), not carry EXHAUSTED(3). Drive the mid-flight
// tier climb by bumping the scripted airtime between the RTS and the DATA.
TEST_CASE("R4.2 ACK budget_hint — EXHAUSTED at DATA-time caps the forward hint at CRITICAL") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000;     // budget 10000ms (realistic — the CTS/ACK fit it)
    node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._airtime_used = 6000;                                     // 60% STRAINED at RTS -> CTSes (no refuse)
    const size_t rn = mk_rts(/*src=*/2,/*next=*/1,/*dst=*/1,/*ctr_lo=*/3,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m2);
    CHECK(hal.count("cts_tx") == 1);
    hal._airtime_used = 9600;                                     // 96% EXHAUSTED by DATA-time (ACK still fits: 9600+air <= 10000)
    const size_t dn = mk_data_hb(1, 1, 3, 0, /*hops_remaining=*/5, /*committed=*/0, "x", db);
    node.on_recv(db.data(), dn, m2);
    auto pk = parse_ack(std::span<const uint8_t>(hal.last_tx("ACK")->bytes.data(),
                                                 hal.last_tx("ACK")->bytes.size()));
    CHECK(pk.has_value());
    if (pk) CHECK(pk->budget_hint == 2);                         // min(CRITICAL, EXHAUSTED) = CRITICAL(2)
}

// ---- R4.4 — originator anti-spam (1st-hop statistical rate-limit) ----
TEST_CASE("R4.4 compute_originator_metric — distinct ctr_lo, 10s dedup, window prune, apparent=max(0,rts-cts)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    int app; uint32_t air; uint8_t rts, cts;
    // 3 distinct-ctr_lo RTSes from sender 9 (air 10 each) -> rts=3
    hal._now = 1000;
    node.track_originator_observation(9, /*rts*/0, 1, 10);
    node.track_originator_observation(9, /*rts*/0, 2, 10);
    node.track_originator_observation(9, /*rts*/0, 3, 10);
    // a RETRY of ctr_lo=1 within the 10s dedup window -> NOT a new event, air NOT re-added
    hal._now = 5000;
    node.track_originator_observation(9, /*rts*/0, 1, 10);
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 3); CHECK(cts == 0); CHECK(app == 3); CHECK(air == 30);   // dedup: still 3 events, 30ms
    // one CTS from 9 -> cts=1 -> apparent = 3-1 = 2
    node.track_originator_observation(9, /*cts*/1, 1, 10);
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 3); CHECK(cts == 1); CHECK(app == 2); CHECK(air == 40);
    // advance past the window (300000) from the ctr_lo=2/3 events (t=1000) but the ctr_lo=1 rts was
    // refreshed to t=5000 and the cts to now; prune drops the t=1000 events.
    hal._now = 1000 + protocol::originator_window_ms + 1;   // 301001: t=1000 events pruned, t=5000 kept
    node.track_originator_observation(9, /*rts*/0, 4, 10);  // triggers a prune + adds ctr_lo=4
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 2);   // ctr_lo=1 (refreshed to 5000) + ctr_lo=4; the t=1000 ctr_lo=2,3 pruned
    CHECK(app == 1);   // rts 2 - cts 1 (the cts at now is kept) ... apparent = max(0, 2-1) = 1
}

TEST_CASE("R4.4 originator ledger — fixed ring caps at cap_originator_events, evicts oldest, metric stays correct") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    // Overflow the heap-free ring: cap+8 RTSes from sender 9, ctr_lo cycling 0..15, 1s apart. Same ctr_lo
    // recurs every 16s (> the 10s dedup window) so NONE dedup -> cap+8 distinct events, all inside the
    // 5-min window (no prune). The ring must cap at N and evict the oldest 8, not grow unboundedly.
    const int N = protocol::cap_originator_events;
    const int over = 8;
    for (int i = 0; i < N + over; ++i) {
        hal._now = 1000 + (uint64_t)i * 1000;
        node.track_originator_observation(9, /*rts*/0, (uint8_t)(i % 16), 10);
    }
    int app; uint32_t air; uint8_t rts, cts;
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 16);                     // the retained recent N events still cover all 16 distinct ctr_lo
    CHECK(cts == 0);
    CHECK(app == 16);
    CHECK(air == (uint32_t)(N * 10));     // capped at N (oldest `over` evicted) — NOT (N+over)*10 = no unbounded growth
}

TEST_CASE("R4.4 throttle drop — a 1st-hop originator-flooder's RTS is silently dropped; a forwarder isn't") {
    std::array<uint8_t,16> rb{};
    // SPAMMER: 7 distinct-ctr_lo RTSes from sender 9 OVERHEARD (to next=99, not us) -> apparent=7 > 6.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
        RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
        for (uint8_t i = 0; i < 7; ++i) {                       // overheard (next=99) -> tracked, no CTS
            const size_t rn = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/i,/*plen=*/10, rb);
            node.on_recv(rb.data(), rn, m9);
        }
        CHECK(hal.count("cts_tx") == 0);                        // none addressed to us
        // now an RTS from 9 addressed to US -> apparent (8 after tracking this one) > 6 -> DROP
        const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/7,/*plen=*/10, rb);
        node.on_recv(rb.data(), rn, m9);
        CHECK(hal.count("rts_drop_originator_throttle") == 1);
        CHECK(hal.count("cts_tx") == 0);                        // silently dropped, NO CTS
        CHECK(hal.count("nack_tx") == 0);                       // and NO NACK
    }
    // FORWARDER: equal RTS+CTS from sender 9 -> apparent ~= 0 -> an RTS to us is CTSed normally.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
        RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
        std::array<uint8_t,8> cb{};
        for (uint8_t i = 0; i < 7; ++i) {                       // 7 RTS + 7 CTS (distinct rx) from 9 -> apparent ~0
            const size_t rn = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/i,/*plen=*/10, rb);
            node.on_recv(rb.data(), rn, m9);
            // distinct rx_id per flight: CTS dedups by rx_id now, so a balanced forwarder serves
            // distinct upstreams -> cts == rts. (Same-rx repeats would merge — the coarser-count tradeoff.)
            const size_t cn = mk_cts(/*rx_id=*/static_cast<uint8_t>(101 + i), /*tx_id=*/9, /*data_sf=*/7, cb);
            node.on_recv(cb.data(), cn, m9);                    // overheard CTS from 9
        }
        const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/7,/*plen=*/10, rb);
        node.on_recv(rb.data(), rn, m9);
        CHECK(hal.count("rts_drop_originator_throttle") == 0);  // forwarder NOT throttled
        CHECK(hal.count("cts_tx") == 1);                        // CTSed normally
    }
}

TEST_CASE("R4.4 airtime backstop — over the 25%-budget airtime cap drops even when apparent <= max") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> airtime cap floor(0.25*10)=2ms
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    const size_t ov = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb);
    node.on_recv(rb.data(), ov, m9);                            // 1 overheard RTS (airtime >> 2ms cap)
    const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/1,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m9);                            // apparent=2 (<6) but total_air > 2ms cap
    CHECK(hal.count("rts_drop_originator_throttle") == 1);      // dropped via the airtime backstop
    CHECK(hal.count("cts_tx") == 0);
}

// review #00/#01: with duty DISABLED (budget 0) the airtime backstop must be OFF (no airtime share to
// enforce) — NOT a 0 cap that drops every RTS. The COUNT threshold stays active. Guard fixed in BOTH engines.
TEST_CASE("R4.4 airtime backstop OFF when duty disabled (budget 0); count threshold still drops a flooder") {
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,9};
    // (a) duty=0: an overheard + an addressed RTS (apparent 2 < 6, airtime >> any cap) -> NOT dropped, CTSed.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;   // duty_cycle = 0 (disabled)
        node.on_init(cfg);
        const size_t ov = mk_rts(9,99,8,0,10,rb); node.on_recv(rb.data(), ov, m9);
        const size_t rn = mk_rts(9, 1,8,1,10,rb); node.on_recv(rb.data(), rn, m9);
        CHECK(hal.count("rts_drop_originator_throttle") == 0);  // budget 0 -> airtime backstop SKIPPED
        CHECK(hal.count("cts_tx") == 1);
    }
    // (b) duty=0 still drops a COUNT flooder (apparent > 6) — the count path is budget-independent.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;   // duty_cycle = 0
        node.on_init(cfg);
        for (uint8_t i = 0; i < 7; ++i) { const size_t n = mk_rts(9,99,8,i,10,rb); node.on_recv(rb.data(), n, m9); }
        const size_t rn = mk_rts(9, 1, 8, 7, 10, rb); node.on_recv(rb.data(), rn, m9);
        CHECK(hal.count("rts_drop_originator_throttle") == 1);  // count threshold fires regardless of budget
    }
}

// ---- R4.3 — adaptive beacon throttle + silence-jitter (THE determinism golden) ----
static Node* mk_throttle_node(TestHal& hal, uint32_t quiet_ms, uint32_t max_idle_ms) {
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.quiet_threshold_ms = quiet_ms; cfg.beacon_max_idle_ms = max_idle_ms;
    node->on_init(cfg);
    return node;
}

TEST_CASE("R4.3 rand-order golden — silence-jitter draws ONLY when throttled+gate-passed (gates stay byte-identical)") {
    // (a) quiet=0 (the fast path EVERY existing gate uses): a periodic fire draws EXACTLY 1 (the re-arm),
    //     NOT 2 — proves NO silence-jitter draw is added, so the gate streams are unperturbed.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/0, /*max_idle=*/0);
        hal._now = 5000;
        const int rb = hal.rand_calls;
        node->on_timer(kBeaconTimerId);
        CHECK(hal.rand_calls - rb == 1);                  // re-arm only
        CHECK(hal.count("beacon_tx") >= 1);               // fast path emits
        delete node;
    }
    // (b) quiet>0, channel QUIET (since_rx=inf >= quiet -> gate passes): draws 2 (silence-jitter + re-arm).
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;                                // no prior RX -> since_rx = inf -> gate passes
        const int rb = hal.rand_calls;
        node->on_timer(kBeaconTimerId);
        CHECK(hal.rand_calls - rb == 2);                  // silence-jitter THEN re-arm
        delete node;
    }
    // (c) quiet>0, channel BUSY (fresh witness, since_rx=0 < quiet -> gate fails): draws 1 (re-arm only),
    //     emits beacon_skipped_busy, NO beacon, NO silence-jitter draw.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;
        std::array<uint8_t,16> rb_{}; RxMeta m2{8.0f,-80.0f,0,2};
        const size_t rn = mk_rts(2,99,8,0,10,rb_); node->on_recv(rb_.data(), rn, m2);   // a decode -> witness fresh
        const int beacons_before = hal.count("beacon_tx");
        const int rb = hal.rand_calls;
        node->on_timer(kBeaconTimerId);
        CHECK(hal.rand_calls - rb == 1);                  // re-arm only (gate failed -> no silence-jitter draw)
        CHECK(hal.count("beacon_skipped_busy") == 1);
        CHECK(hal.count("beacon_tx") == beacons_before);  // suppressed
        delete node;
    }
}

TEST_CASE("R4.3 witness — on_preamble_detected AND a decode both make the channel look busy (gate suppresses)") {
    // on_preamble_detected sets the witness even without a decode -> the next quiet>0 fire suppresses.
    TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
    hal._now = 200000;
    node->on_preamble_detected(200000);                   // channel busy NOW (IRQ, no decode)
    const int rb = hal.rand_calls;
    node->on_timer(kBeaconTimerId);
    CHECK(hal.rand_calls - rb == 1);                      // gate failed (since_rx=0) -> no silence-jitter draw
    CHECK(hal.count("beacon_skipped_busy") == 1);
    delete node;
}

TEST_CASE("R4.3 deferred jitter re-check — busy during the jitter window stands down; quiet emits") {
    // STAND DOWN: a decode lands during the jitter window -> the deferred fire skips (post_jitter).
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;
        std::array<uint8_t,16> rb_{}; RxMeta m2{8.0f,-80.0f,0,2};
        const size_t rn = mk_rts(2,99,8,0,10,rb_); node->on_recv(rb_.data(), rn, m2);   // fresh witness
        const int beacons_before = hal.count("beacon_tx");
        node->on_timer(kBeaconJitterTimerId);             // deferred re-check: channel busy -> stand down
        CHECK(hal.count("beacon_skipped_busy") == 1);     // stage=post_jitter
        CHECK(hal.count("beacon_tx") == beacons_before);
        delete node;
    }
    // EMIT: still quiet at the deferred fire -> beacon goes out.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;                                // no RX -> since=inf -> still quiet
        const int beacons_before = hal.count("beacon_tx");
        node->on_timer(kBeaconJitterTimerId);
        CHECK(hal.count("beacon_tx") == beacons_before + 1);   // emitted
        delete node;
    }
}

TEST_CASE("Cleanup #D — two periodic beacon defers in one jitter window BOTH fire (ring, not single-timer replace)") {
    // Pre-#D: the 2nd periodic defer's after(kBeaconJitterTimerId) REPLACED the 1st -> only 1 beacon fired where the
    // Lua's per-`after` closures fire both. Ring fix: each defer takes a free slot [27..30]. Draws are unchanged (each
    // periodic fire still draws its silence-jitter); only the lost-beacon edge is closed.
    TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
    hal._now = 200000;                                        // no RX -> since_rx=inf -> quiet (both fires defer)
    hal._rand_ret = 5;                                        // jitter=5 (>0) so periodic_beacon_fire DEFERS, not emit-now
    const int b0 = hal.count("beacon_tx");
    node->on_timer(kBeaconTimerId);                          // periodic fire #1 -> defer ring slot 0
    node->on_timer(kBeaconTimerId);                          // periodic fire #2 -> defer ring slot 1 (NOT a replace)
    CHECK(hal.count("beacon_tx") == b0);                     // both deferred, nothing on air yet
    node->on_timer(kBeaconJitterTimerId + 0);               // slot 0 fires
    node->on_timer(kBeaconJitterTimerId + 1);               // slot 1 fires
    CHECK(hal.count("beacon_tx") == b0 + 2);                // BOTH deferred beacons emitted (pre-#D: only 1)
    delete node;
}

// The R4.2 #00 port: schedule_triggered_beacon draws a SECOND jitter (min-interval defer) ONLY in
// steady_state (now >= boot_grace 120000). Under boot grace it NEVER draws the 2nd -> every <120s gate
// stays byte-identical. (This is what lifts the R4.2 ">120s draw-for-draw" guard.)
TEST_CASE("R4.3 triggered-beacon min-interval — 2nd draw ONLY in steady_state (>120000ms)") {
    // (steady) past boot grace + a recent beacon within min_interval -> 2 draws (jitter + min-interval defer).
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/0, /*max_idle=*/0);   // quiet=0: clean beacon
        hal._now = 190000; node->on_timer(kBeaconTimerId);    // emit -> _last_beacon_tx_ms=190000, discovery exits
        hal._now = 200000;                                    // 200000 - boot(0) >= 120000 -> steady
        const int rb = hal.rand_calls;
        node->schedule_triggered_beacon();
        CHECK(hal.rand_calls - rb == 2);                      // trigger jitter + min-interval 2nd draw
        CHECK(hal.count("beacon_trigger_deferred") == 1);
        delete node;
    }
    // (under boot grace) now < 120000 -> steady_state false -> 1 draw, NO defer.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/0, /*max_idle=*/0);
        hal._now = 50000; node->on_timer(kBeaconTimerId);     // emit at 50000 (discovery exits on timeout)
        hal._now = 100000;                                    // 100000 - 0 < 120000 -> NOT steady
        const int rb = hal.rand_calls;
        node->schedule_triggered_beacon();
        CHECK(hal.rand_calls - rb == 1);                      // jitter only, NO min-interval draw
        CHECK(hal.count("beacon_trigger_deferred") == 0);
        delete node;
    }
}

// review #00: the max-idle witness (_last_rx_bcn_ms) must be set AFTER the leaf guard, so a FOREIGN-leaf
// beacon does NOT count as a routing-refresh — else the B+C skip_clean (and hence the silence-jitter draw)
// desyncs from the Lua on multi-leaf channels. The routing-SF witness (channel-busy) IS set for all frames.
TEST_CASE("R4.3 max-idle witness ignores a foreign-leaf beacon (set after the leaf guard)") {
    TestHal hal; Node node(hal, /*id=*/1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.quiet_threshold_ms = 30000; cfg.beacon_max_idle_ms = 30000;   // small max_idle -> override eligible
    node.on_init(cfg);
    hal._now = 100000;                                        // past max_idle, no prior beacon (since_tx = inf)
    // a well-formed beacon stamped leaf_id=1 (FOREIGN): rejected at the leaf guard -> must NOT set _last_rx_bcn_ms.
    std::array<uint8_t,64> bb{};
    beacon_entry e{}; e.dest = 200; e.next = 201; e.score_bucket = 12; e.is_gateway = false; e.hops = 2;
    beacon_in bin{}; bin.leaf_id = 1; bin.src = 9; bin.key_hash32 = 0x1234;
    bin.entries = std::span<const beacon_entry>(&e, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta m9{12.0f,-70.0f,0,9}; node.on_recv(bb.data(), bn, m9);
    CHECK(hal.count("beacon_rx") == 0);                       // foreign leaf rejected (returns before the bcn witness)
    // periodic fire: the max-idle B+C sees since_bcn_rx = inf (the foreign beacon was NOT counted) + dirty_n=0
    // -> NOT skip_clean -> force_idle. (With the bug, the foreign beacon would set since_bcn_rx=0 -> skip_clean.)
    node.on_timer(kBeaconTimerId);
    CHECK(hal.count("beacon_max_idle_force") == 1);
    CHECK(hal.count("beacon_max_idle_skip_clean") == 0);
}

// ---- R4.5 — LBT (listen-before-talk) pre-checks (THE rand-order golden) ----
static Node* mk_lbt_node(TestHal& hal, bool lbt_enabled, std::vector<std::array<uint8_t,3>> vias) {
    Node* node = new Node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.lbt_enabled = lbt_enabled;
    node->on_init(cfg);
    std::array<uint8_t,64> bb{};
    for (auto& v : vias) {
        RxMeta m{12.0f,-70.0f,0,static_cast<int8_t>(v[0])};
        const size_t n = mk_beacon_route(v[0], 5, 9, v[1], v[2], bb);
        node->on_recv(bb.data(), n, m);
    }
    return node;
}

TEST_CASE("R4.5 rand-order golden — LBT draws ONLY when enabled + channel busy; the deferred re-fire draws nothing") {
    // (a) lbt_enabled=false (every gate): RTS goes straight out even on a busy channel — NO LBT draw.
    {
        TestHal hal; Node* node = mk_lbt_node(hal, /*lbt_enabled=*/false, {{2,1,14}});
        hal._now = 1000; hal._channel_busy_until = 99999;       // busy, but LBT disabled
        const int rb = hal.rand_calls;
        send_cmd(*node, 5, "hi");
        CHECK(hal.rand_calls - rb == 0);                        // disabled -> NO LBT draw
        CHECK(hal.last_tx("RTS") != nullptr);                   // RTS went straight to radio
        delete node;
    }
    // (b) lbt_enabled=true but channel IDLE: no defer, NO draw.
    {
        TestHal hal; Node* node = mk_lbt_node(hal, /*lbt_enabled=*/true, {{2,1,14}});
        hal._now = 1000; hal._channel_busy_until = 0;
        const int rb = hal.rand_calls;
        send_cmd(*node, 5, "hi");
        CHECK(hal.rand_calls - rb == 0);                        // idle -> NO LBT draw
        CHECK(hal.last_tx("RTS") != nullptr);
        delete node;
    }
    // (c) lbt_enabled=true + channel BUSY: ONE LBT draw + tx_lbt_defer, the RTS HELD; the deferred re-fire
    //     sends it with NO further draw (the __lbt_done once-guard).
    {
        TestHal hal; Node* node = mk_lbt_node(hal, /*lbt_enabled=*/true, {{2,1,14}});
        hal._now = 1000; hal._channel_busy_until = 5000;        // busy until 5000
        const int rb = hal.rand_calls;
        send_cmd(*node, 5, "hi");
        CHECK(hal.rand_calls - rb == 1);                        // ONE LBT backoff draw
        CHECK(hal.count("tx_lbt_defer") == 1);
        CHECK(hal.last_tx("RTS") == nullptr);                   // HELD — not on radio yet
        const int rb2 = hal.rand_calls;
        hal._now = 5000; hal._channel_busy_until = 0;           // channel cleared
        node->on_timer(kLbtDeferTimerId);
        CHECK(hal.rand_calls - rb2 == 0);                       // deferred re-fire: NO further draw
        CHECK(hal.last_tx("RTS") != nullptr);                   // now on radio
        delete node;
    }
}

TEST_CASE("R4.5 tx_flood — a beacon is DROPPED when the channel is busy longer than flood_lbt_max_defer_ms") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.lbt_enabled = true; cfg.quiet_threshold_ms = 0;        // quiet=0 fast path -> emit_beacon -> tx_flood
    node.on_init(cfg);
    hal._now = 1000; hal._channel_busy_until = 10000000;       // busy WAY past flood_lbt_max_defer
    const int rb = hal.rand_calls;
    node.on_timer(kBeaconTimerId);
    CHECK(hal.count("tx_flood_skipped") == 1);                 // page dropped
    CHECK(hal.last_tx("BCN") == nullptr);                      // nothing on radio
    CHECK(hal.rand_calls - rb == 1);                           // only the periodic re-arm draw (NO LBT defer draw)
}

// review #00/#02/#03: TWO concurrent LBT defers must BOTH fire — a single stash would clobber the first
// (drop it + desync the rand stream). The ring gives each defer its own slot + timer (Lua per-closure semantics).
TEST_CASE("R4.5 LBT ring — two concurrent deferred NACKs BOTH reach the radio (no single-stash clobber)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0; cfg.lbt_enabled = true;
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    hal._now = 1000; hal._channel_busy_until = 0;
    // make the node BUSY receiving (pending_rx) so further RTSes get BUSY_RX NACKs.
    { const size_t rn = mk_rts(2,1,9,5,10,rb); RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node.on_recv(rb.data(), rn, m2); }
    CHECK(hal.count("cts_tx") >= 1);
    // busy channel + two more RTSes from DIFFERENT senders -> two BUSY_RX NACKs, both LBT-deferred (slots 0,1).
    hal._channel_busy_until = 5000;
    { const size_t rn = mk_rts(3,1,8,6,10,rb); RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)}; node.on_recv(rb.data(), rn, m3); }
    { const size_t rn = mk_rts(4,1,8,7,10,rb); RxMeta m4{8.0f,-80.0f,0,static_cast<int8_t>(4)}; node.on_recv(rb.data(), rn, m4); }
    CHECK(hal.count("tx_lbt_defer") == 2);                     // both deferred (distinct slots, NOT clobbered)
    CHECK(hal.count("tx_lbt_defer_dropped") == 0);
    // fire both slot timers -> BOTH NACKs go to the radio (single stash would send only one).
    hal._now = 5000; hal._channel_busy_until = 0;
    node.on_timer(kLbtDeferTimerId + 0);
    node.on_timer(kLbtDeferTimerId + 1);
    int nack_tx = 0; for (const auto& f : hal.tx_frames) if (f.label == "NACK") ++nack_tx;
    CHECK(nack_tx == 2);
}

// ---- R4.5b — on_radio_busy stash retry (the busy-channel response retry) ----
TEST_CASE("R4.5b on_radio_busy — a blocked DATA clears awaiting_ack + retries from the stash (3x, one draw each) then gives up") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                                  // RTS
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                     // DATA tx -> awaiting_ack + DATA stashed
    CHECK(hal.last_tx("DATA") != nullptr);
    // a blocked DATA (the sim's safety-net couldn't send it): on_radio_busy(tag=DATA=2).
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    const int rb = hal.rand_calls;
    node->on_radio_busy(bi);                                  // retry #1 (retries 3->2)
    CHECK(hal.count("data_tx_blocked") == 1);
    CHECK(hal.rand_calls - rb == 1);                          // ONE retry-jitter draw
    node->on_timer(kRadioBusyRetryTimerId + 1);              // slot 1 = DATA -> re-issue
    int data_n = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data_n;
    CHECK(data_n == 2);                                       // original + the retry
    // exhaust the remaining retries -> giveup on the 4th block.
    node->on_radio_busy(bi);                                  // retry #2 (2->1)
    node->on_radio_busy(bi);                                  // retry #3 (1->0)
    const int rb2 = hal.rand_calls;
    node->on_radio_busy(bi);                                  // retries_left==0 -> giveup, NO draw
    CHECK(hal.count("tx_giveup") == 1);
    CHECK(hal.rand_calls - rb2 == 0);
    delete node;
}

TEST_CASE("R4.5b on_radio_busy — a DATA retry re-arms the ACK wait (port divergence: Lua on_handed re-arms, ours must too)") {
    // Regression guard: Lua's DATA on_handed (dv:10270-10278) sets awaiting_ack + start_ack_timeout, and the stash
    // retry re-fires on_handed. OUR retry_stashed re-sends the bytes only; without the explicit DATA re-arm the
    // re-sent DATA flies but the sender stays !awaiting_ack with no ack-timeout -> the returning ACK is dropped +
    // the flight never completes. Assert a matching ACK is ACCEPTED after the retry (only possible if re-armed).
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                                  // RTS
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                     // DATA tx -> awaiting_ack + DATA stashed
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi);                                  // clears awaiting_ack + cancels ack-timeout
    node->on_timer(kRadioBusyRetryTimerId + 1);              // retry: re-tx DATA + RE-ARM awaiting_ack + ack-timeout
    std::array<uint8_t,8> ab{}; const size_t an = mk_ack(1, 1, ab);
    RxMeta m3{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(ab.data(), an, m3);
    CHECK(hal.count("ack_rx") == 1);                          // re-armed -> the ACK completes the flight
    delete node;
}

// ---- shared-Lua-bug fixes (project_meshroute_shared_lua_bugs) ----
TEST_CASE("Shared-bug #1 — a DATA giveup releases the stranded flight so the TX queue drains") {
    // Pre-fix: on_radio_busy(DATA) cleared awaiting_ack + cancelled the ack-timeout; the exhausted-stash giveup then
    // returned with _pending_tx STILL set + no recovery timer, so become_free() was blocked behind a dead flight and
    // a queued 2nd message never sent. Fix: the DATA giveup resets pending_tx + become_free (mirror DATA-M dv:12151).
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "a");                                  // msg1 -> RTS
    send_cmd(*node, 5, "b");                                  // msg2 -> queued behind msg1
    int rts0 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts0;
    CHECK(rts0 == 1);                                         // only msg1 has RTSed
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                     // msg1 -> DATA tx (awaiting_ack + DATA stashed)
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi); node->on_radio_busy(bi); node->on_radio_busy(bi);   // 3 retries (3->0)
    node->on_radio_busy(bi);                                  // retries exhausted -> giveup + release the flight
    CHECK(hal.count("tx_giveup") == 1);
    int rts1 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts1;
    CHECK(rts1 == 2);                                         // msg2 drained + RTSed (the queue was NOT stranded)
    const Ev* r2 = hal.last("rts_tx"); CHECK(r2 != nullptr);
    if (r2) CHECK(r2->ctr == 2);                              // the 2nd RTS is msg2
    delete node;
}

TEST_CASE("Shared-bug #2 — tx_with_retry duty pre-check defers an over-budget DATA, then re-issues when budget frees") {
    // Lua tx_with_retry (dv:3615-3635) duty-pre-checks + self-defers an over-budget frame; ours used to always
    // _hal.tx -> the sim's duty hard-block bounced it via on_radio_busy, consuming a stash retry per bounce. Fix:
    // the duty pre-check defers (no _hal.tx) + a timer re-runs tx_with_retry from the stash. Draw-free.
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000;   // budget 10000ms
    node.on_init(cfg);
    std::array<uint8_t,64> bb{}; RxMeta mb{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    const size_t bn = mk_beacon_route(/*src=*/2,/*dest=*/5,/*next=*/9,/*hops=*/1,/*score=*/14, bb);
    node.on_recv(bb.data(), bn, mb);                           // route to 5 via 2
    hal._airtime_used = 0;                                     // RTS/CTS fit
    send_cmd(node, 5, "hi");                                   // -> RTS (slot<0, NOT duty-pre-checked here)
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    hal._airtime_used = 9990;                                  // near budget -> the DATA (slot>=0) won't fit
    node.on_recv(cb.data(), cn, m2);
    const int rb = hal.rand_calls;
    node.on_timer(kCtsToDataGapTimerId);                      // do_data_tx -> tx_with_retry(DATA) -> OVER budget -> defer
    CHECK(hal.count("duty_cycle_blocked") == 1);
    int data0 = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data0;
    CHECK(data0 == 0);                                        // DATA NOT handed to the radio (deferred)
    CHECK(hal.rand_calls - rb == 0);                          // the duty defer is DRAW-FREE
    // review #1/#9: a duty-deferred DATA must NOT arm awaiting_ack (the Lua clears it, dv:10281-10283). If it did, the
    // short ack-timeout would fire before the long duty wait + draw a rand + re-RTS. Prove the ACK wait is disarmed by
    // firing a stray ack-timeout: it must no-op (zero draws, no re-RTS) — the bug the original test masked.
    int rtsBefore = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rtsBefore;
    const int rb2 = hal.rand_calls;
    node.on_timer(kAckTimeoutTimerId);                        // would draw + re-RTS if awaiting_ack were wrongly armed
    CHECK(hal.rand_calls - rb2 == 0);                         // NO spurious draw -> awaiting_ack was false on the defer
    int rtsAfter = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rtsAfter;
    CHECK(rtsAfter == rtsBefore);                             // NO spurious re-RTS from a premature ack-timeout
    hal._airtime_used = 0;                                     // budget frees
    node.on_timer(kDutyDeferTimerId + 1);                     // duty_defer_fire(DATA slot) -> tx_with_retry -> now fits
    int data1 = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data1;
    CHECK(data1 == 1);                                        // DATA re-issued after the budget freed
    // the re-issue RE-ARMS the ACK wait (anchored to the real send), so a matching ACK now completes the flight
    std::array<uint8_t,8> ab{}; const size_t an = mk_ack(1, 1, ab);
    node.on_recv(ab.data(), an, m2);
    CHECK(hal.count("ack_rx") == 1);                          // re-armed on the re-hand -> ACK accepted
}

TEST_CASE("Cleanup #A (redo) — over-budget RTS duty-deferred in the dedicated slot (flight_gen-safe), re-checks, then hands when budget frees") {
    // The #2 duty pre-check is slot>=0 only; #A duty-checks the RTS in lbt_complete + defers it in a DEDICATED slot
    // (not the shared LBT ring — that reuse was net-worse, review wgvbtirmu), flight_gen-keyed so the long wait is
    // safe. Draw-free; gate-inert. (start_rts_timeout is armed on the hand — the deliberate drift; not separately
    // assertable with the no-op TestHal::after.)
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.data_sf = 7; cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000;   // budget 10000ms
    node.on_init(cfg);
    std::array<uint8_t,64> bb{}; RxMeta mb{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    const size_t bn = mk_beacon_route(2,5,9,1,14,bb); node.on_recv(bb.data(),bn,mb);
    hal._airtime_used = 9990;                                   // near budget -> the RTS won't fit
    const int rb = hal.rand_calls;
    send_cmd(node, 5, "hi");                                    // RTS -> lbt_complete -> OVER budget -> dedicated duty defer
    int rts = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts;
    CHECK(rts == 0);                                            // NOT handed (deferred, not sim-bounced)
    CHECK(hal.count("duty_cycle_blocked") >= 1);
    CHECK(hal.rand_calls - rb == 0);                            // the defer is DRAW-FREE
    node.on_timer(kRtsDutyDeferTimerId);                       // STILL over budget -> re-defer (re-check), not handed
    rts = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts;
    CHECK(rts == 0);                                           // re-deferred, still off air
    hal._airtime_used = 0;                                      // budget frees
    node.on_timer(kRtsDutyDeferTimerId);                      // now fits -> hand the RTS (+ arm the CTS-wait)
    rts = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts;
    CHECK(rts == 1);                                           // handed once the budget freed
    CHECK(hal.rand_calls - rb == 0);                           // the whole defer/re-defer/hand path is DRAW-FREE
}

// Cleanup #B (pick_next_cascade_hop now refresh_route_order-s first, dv:5434) is exercised by the r5_cascade
// differential gate (which drives pick_next_cascade_hop through cascade_to_alt) and reuses the sort_candidates +
// resort_routes_for_neighbor_penalty machinery covered by the R4.2 demotion tests above ("route demotion — marking a
// CRITICAL primary reranks it below the viable alt"). A standalone catch-up-flip unit test is impractical: the
// snr/advertised bucketing collapses controlled candidate scores into one bucket (equal -> a stable re-sort can't
// flip back) or drops the weak candidate below the viability floor. (Review #12, LOW — covered, not separately tested.)

TEST_CASE("R4.5b on_radio_busy — a blocked RTS re-RTSes via the already-armed rts_timeout (port divergence fix)") {
    // Regression guard: Lua dv:12091 clears awaiting_cts on a blocked RTS, but its rts_timeout_fire ignores
    // awaiting_cts (captures ctr_lo) and retries. OUR rts_timeout_fire uses awaiting_cts as the staleness key, so
    // on_radio_busy(RTS) must NOT clear it — else the armed timeout bails and the blocked RTS is stranded forever.
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                                  // RTS -> awaiting_cts + rts_timeout armed
    int rts0 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts0;
    CHECK(rts0 == 1);
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=RTS*/0, /*sf=*/7, /*busy_until=*/0};
    const int rb = hal.rand_calls;
    node->on_radio_busy(bi);
    CHECK(hal.count("rts_tx_blocked") == 1);
    CHECK(hal.count("tx_giveup") == 0);                       // RTS is NOT stash-retried
    CHECK(hal.rand_calls - rb == 0);                          // on_radio_busy itself takes no retry draw for an RTS
    // the armed rts_timeout must still fire + drive a re-RTS (proves awaiting_cts was left intact).
    node->on_timer(kRtsTimeoutTimerId);                       // -> rts_timeout_fire -> retries_left-- + backoff draw
    node->on_timer(kRetryBackoffTimerId);                     // -> re-RTS
    int rts1 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts1;
    CHECK(rts1 == 2);                                         // the blocked RTS was actually retried
    delete node;
}

// ===== F route discovery (RREQ/RREP) — node_route_discovery.cpp =====
// F floods TX with the beacon tag, so the tests identify RREQ/RREP by parse_f + is_reply.

TEST_CASE("F RREQ addressed to us -> reverse path + RREP toward the forwarder") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = false;
    in.dst_id = 5; in.ttl_or_next_hop = 4; in.hops = 2; in.relay = 3;   // origin 10 seeks us(5); forwarder 3
    uint8_t buf[8]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    CHECK(n == 7);
    hal._now = 1000; node.on_recv(buf, n, meta);

    bool saw_rev = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 10) saw_rev = true;
    CHECK(saw_rev);                                          // reverse route to the origin installed

    bool saw_rrep = false; f_out rrep{};
    for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size()));
        if (p && p->is_reply) { saw_rrep = true; rrep = *p; }
    }
    CHECK(saw_rrep);
    if (saw_rrep) {
        CHECK(rrep.origin == 10); CHECK(rrep.dst_id == 5);
        CHECK(rrep.ttl_or_next_hop == 3);                    // unicast back to the immediate forwarder
        CHECK(rrep.relay == 5);                              // we stamped ourselves as the relay
    }
}

TEST_CASE("F RREQ relayed (no route) -> reverse path + ttl-decremented rebroadcast + flood dedup") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = false;
    in.dst_id = 20; in.ttl_or_next_hop = 4; in.hops = 1; in.relay = 3;
    uint8_t buf[8]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 1000; node.on_recv(buf, n, meta);

    bool saw_rev = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 10) saw_rev = true;
    CHECK(saw_rev);

    auto count_rreq = [&]() { int c = 0; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };
    CHECK(count_rreq() == 1);                                // one rebroadcast
    f_out fwd{}; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) fwd = *p; }
    CHECK(fwd.origin == 10); CHECK(fwd.dst_id == 20);
    CHECK(fwd.ttl_or_next_hop == 3);                         // ttl 4 -> 3
    CHECK(fwd.hops == 2);                                    // hops 1 -> 2
    CHECK(fwd.relay == 5);                                   // we are the new forwarder

    hal._now = 2000; node.on_recv(buf, n, meta);             // SAME (origin,dst) again
    CHECK(count_rreq() == 1);                                // deduped: no second rebroadcast
}

TEST_CASE("F RREP addressed to the origin -> forward path + rrep_arrived") {
    TestHal hal;
    Node node(hal, /*node_id=*/10, /*key_hash32=*/0xABCD);  // we are the origin
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = true;
    in.dst_id = 20; in.ttl_or_next_hop = 10; in.hops = 2; in.relay = 7;   // addressed to us(10); forwarder 7 toward dst
    uint8_t buf[8]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 1000; node.on_recv(buf, n, meta);

    bool saw_fwd = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 20) saw_fwd = true;
    CHECK(saw_fwd);                                          // forward route to dst installed
    CHECK(hal.count("rrep_arrived") == 1);
}

TEST_CASE("F RREP relayed (not the origin) -> forward path + RREP onward along reverse path") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // Lay a reverse route to origin 10 (via 3) first, so the RREP can route onward.
    { f_in q{}; q.leaf_id = 0; q.origin = 10; q.is_reply = false; q.dst_id = 99;
      q.ttl_or_next_hop = 4; q.hops = 1; q.relay = 3;
      uint8_t qb[8]; const size_t qn = pack_f(q, std::span<uint8_t>(qb, sizeof(qb)));
      hal._now = 1000; node.on_recv(qb, qn, meta); }
    const size_t tx_before = hal.tx_frames.size();

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = true;
    in.dst_id = 20; in.ttl_or_next_hop = 5; in.hops = 2; in.relay = 7;    // addressed to us(5)
    uint8_t buf[8]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 2000; node.on_recv(buf, n, meta);

    bool saw_fwd = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 20) saw_fwd = true;
    CHECK(saw_fwd);
    bool saw_onward = false; f_out rr{};
    for (size_t i = tx_before; i < hal.tx_frames.size(); ++i) {
        auto p = parse_f(std::span<const uint8_t>(hal.tx_frames[i].bytes.data(), hal.tx_frames[i].bytes.size()));
        if (p && p->is_reply) { saw_onward = true; rr = *p; }
    }
    CHECK(saw_onward);
    if (saw_onward) {
        CHECK(rr.origin == 10); CHECK(rr.dst_id == 20);
        CHECK(rr.ttl_or_next_hop == 3);                      // toward origin via the reverse next-hop (3)
        CHECK(rr.relay == 5);
    }
}
