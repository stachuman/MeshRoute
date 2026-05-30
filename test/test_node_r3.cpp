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
            bool has_payload = false; std::string payload; };

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
            if      (std::strcmp(f[i].key, "to") == 0)  e.to  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dst") == 0) e.dst = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dup") == 0) e.dup = f[i].b;
            else if (std::strcmp(f[i].key, "payload") == 0 && f[i].s) { e.has_payload = true; e.payload = f[i].s; }
        }
        events.push_back(e);
    }
    void     log(const char*) override {}

    int count(const char* t) const { int n = 0; for (const auto& e : events) if (e.type == t) ++n; return n; }
    const Ev* last(const char* t) const { const Ev* r = nullptr; for (const auto& e : events) if (e.type == t) r = &e; return r; }
};

constexpr uint32_t kPostAckTimerId = 9;   // mirror node.h's private constant

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
