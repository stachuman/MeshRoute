// MeshRoute — test_node_channel.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Phase 1 of the channel-message gossip plane (node_channel.cpp): the channel_msg_id mint (bit-exact
// vs the Lua — the differential gate depends on it), send_channel origination, DATA-M ingestion +
// per-origin anti-spam admission (distinct-count, repeat-id refresh, over-cap drop, self bypass,
// gateway skip), and buffer eviction. Driven through on_command / ingest_channel_m with an in-memory
// Hal. Mirrors dv_dual_sf.lua: channel_msg_id :2239, channel_origin_admit :3456, DATA-M :10942,
// send_channel :12126.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions =>
// CHECK only. This plane is DRAW-FREE in Phase 1 (the pull jitter is Phase 2).
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

struct Ev { std::string type; int64_t id = -1; int origin = -1; int count = -1; int channel_id = -1;
            std::string source; std::string mode; };

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
            if      (std::strcmp(f[i].key, "id") == 0)         e.id = f[i].i;
            else if (std::strcmp(f[i].key, "origin") == 0)     e.origin = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "count") == 0)      e.count = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "channel_id") == 0) e.channel_id = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "source") == 0 && f[i].s) e.source = f[i].s;
            else if (std::strcmp(f[i].key, "mode") == 0 && f[i].s)   e.mode = f[i].s;
        }
        events.push_back(std::move(e));
    }
    void     log(const char*) override {}
    int count(const char* t) const { int n = 0; for (const auto& e : events) if (e.type == t) ++n; return n; }
};

static NodeConfig basic_cfg() { NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; return c; }

// Craft a DATA-M inner for ingest_channel_m. body kept alive by the caller's array.
static data_m_inner mk_m(uint32_t id, uint8_t channel_id, uint8_t flavor, const uint8_t* body, uint8_t len) {
    data_m_inner m{}; m.channel_msg_id = id; m.channel_id = channel_id; m.flavor = flavor;
    m.body = std::span<const uint8_t>(body, len); return m;
}

static CmdResult send_channel(Node& n, uint8_t ch, const char* text) {
    Command c{}; c.kind = CmdKind::send_channel; c.u.channel.channel_id = ch;
    c.body = reinterpret_cast<const uint8_t*>(text); c.body_len = static_cast<uint8_t>(std::strlen(text));
    return n.on_command(c);
}
// A 0-entry (identity) beacon from `src` — installs src as a hops==1 direct neighbour on the receiver.
static size_t mk_beacon(uint8_t src, std::array<uint8_t,64>& b) {
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x1000u + src;
    in.entries = std::span<const beacon_entry>();
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}
static RxMeta meta_at(uint64_t t) { RxMeta m{}; m.snr_db = 9.0f; m.rssi_dbm = -70.0f; m.recv_ms = t; m.src_hint = -1; return m; }

}  // namespace

TEST_CASE("channel_msg_id mint is bit-exact: origin<<24 | (key_hash32 LOW 16)<<8 | ctr low 8") {
    // The mapper claimed key_hash32 HIGH 16; the Lua (dv:2239) is `& 0xffff` = LOW 16. Pin it.
    CHECK(Node::channel_msg_id_mint(5, 0xDEADBEEFu, 0x42) == 0x05BEEF42u);
    CHECK(Node::channel_msg_id_mint(0xFF, 0x00001234u, 0xAB) == 0xFF1234ABu);
    CHECK(Node::channel_msg_id_mint(1, 0x0000FFFFu, 0xFF) == 0x01FFFFFFu);
    CHECK(((Node::channel_msg_id_mint(9, 0xABCD1234u, 7) >> 24) & 0xff) == 9);   // origin recoverable from the high byte
}

TEST_CASE("send_channel buffers a dirty entry; the id matches the minted ctr; oversize + unprovisioned refused") {
    TestHal hal; Node node(hal, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const CmdResult r = send_channel(node, /*ch=*/7, "hello-channel");
    CHECK(r.code == CmdCode::queued);
    CHECK(node.channel_buffer_count() == 1);
    const uint32_t sent_id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    CHECK(node.channel_has(sent_id));
    CHECK(node.channel_entry_dirty(sent_id));                                       // originated dirty -> advertised next BCN
    CHECK(node.channel_payload_eq(sent_id, reinterpret_cast<const uint8_t*>("hello-channel"), 13));  // payload round-trips
    CHECK(hal.count("channel_msg_received") == 1);

    // oversize (> channel_msg_max_payload_bytes) -> err_too_large, not buffered
    std::string big(protocol::channel_msg_max_payload_bytes + 1, 'x');
    const CmdResult ro = send_channel(node, 7, big.c_str());
    CHECK(ro.code == CmdCode::err_too_large);
    CHECK(node.channel_buffer_count() == 1);   // unchanged

    // unprovisioned (node_id==0) -> refused
    TestHal hal0; Node n0(hal0, /*id=*/0, /*key=*/0x1u); NodeConfig c0 = basic_cfg(); n0.on_init(c0);
    CHECK(send_channel(n0, 7, "x").code == CmdCode::err_unprovisioned);
    CHECK(n0.channel_buffer_count() == 0);
}

TEST_CASE("DATA-M ingest: a received channel msg is admitted + buffered; a gateway skips the merge") {
    const uint8_t body[] = { 'h', 'i' };
    {
        TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
        const uint32_t id = Node::channel_msg_id_mint(/*origin=*/9, 0x4242u, 1);
        node.ingest_channel_m(mk_m(id, /*ch=*/5, /*flavor=*/0, body, 2), /*next=*/2, /*dst=*/2, /*from=*/9);
        CHECK(node.channel_buffer_count() == 1);
        CHECK(node.channel_has(id));
        CHECK(hal.count("channel_msg_received") == 1);
    }
    {   // Principle 11: a gateway does NOT buffer channel gossip (it still forwards via the DATA path)
        TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); cfg.is_gateway = true; node.on_init(cfg);
        const uint32_t id = Node::channel_msg_id_mint(9, 0x4242u, 1);
        node.ingest_channel_m(mk_m(id, 5, 0, body, 2), 2, 2, 9);
        CHECK(node.channel_buffer_count() == 0);
    }
}

TEST_CASE("per-origin anti-spam: distinct-id count caps at the window max; over-cap drops; self bypasses") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint8_t body[] = { 'm' };
    // 20 distinct ids from origin 9 (vary the low bytes) -> all admitted + buffered
    for (int k = 0; k < protocol::channel_origin_max_per_window; ++k) {
        const uint32_t id = (uint32_t(9) << 24) | static_cast<uint32_t>(k);
        node.ingest_channel_m(mk_m(id, 5, 0, body, 1), 2, 2, 9);
    }
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
    CHECK(hal.count("channel_drop_originator_throttle") == 0);
    // the (cap+1)th DISTINCT id from origin 9 -> dropped (count stays at cap)
    const uint32_t over = (uint32_t(9) << 24) | 0xFFu;
    node.ingest_channel_m(mk_m(over, 5, 0, body, 1), 2, 2, 9);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
    CHECK(hal.count("channel_drop_originator_throttle") == 1);
    CHECK(!node.channel_has(over));
    // a DIFFERENT origin is independent -> admitted
    const uint32_t other = (uint32_t(10) << 24) | 1u;
    node.ingest_channel_m(mk_m(other, 5, 0, body, 1), 2, 2, 10);
    CHECK(node.channel_has(other));
}

TEST_CASE("anti-spam repeat-id refreshes (not re-counts): a re-broadcast can't free a slot for a new id") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint8_t body[] = { 'm' };
    for (int k = 0; k < protocol::channel_origin_max_per_window; ++k)   // saturate origin 9 at the cap
        node.ingest_channel_m(mk_m((uint32_t(9) << 24) | static_cast<uint32_t>(k), 5, 0, body, 1), 2, 2, 9);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
    // re-ingest an EXISTING id (origin 9, k=0) -> already-present (refresh), NOT a new entry, NOT a drop
    node.ingest_channel_m(mk_m((uint32_t(9) << 24) | 0u, 5, 0, body, 1), 2, 2, 9);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);   // unchanged
    CHECK(hal.count("channel_msg_already_present") == 1);
    CHECK(hal.count("channel_drop_originator_throttle") == 0);                       // the dup was admitted, not dropped
    // a NEW distinct id from origin 9 still drops — the dup did NOT free a slot
    node.ingest_channel_m(mk_m((uint32_t(9) << 24) | 0x99u, 5, 0, body, 1), 2, 2, 9);
    CHECK(hal.count("channel_drop_originator_throttle") == 1);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
}

TEST_CASE("buffer eviction (fallback, no neighbours): the OLDEST goes; ALL others survive in order") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::vector<uint32_t> ids;                                        // every minted id in send order
    for (int k = 0; k < protocol::cap_channel_buffer; ++k) {
        const CmdResult r = send_channel(node, 7, "fill");
        ids.push_back(Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff)));
    }
    CHECK(node.channel_buffer_count() == protocol::cap_channel_buffer);
    const CmdResult ov = send_channel(node, 7, "overflow");          // cap+1 -> evict oldest (no neighbours -> fallback)
    const uint32_t ov_id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(ov.ctr & 0xff));
    CHECK(node.channel_buffer_count() == protocol::cap_channel_buffer);
    CHECK(!node.channel_has(ids[0]));                                // the oldest is gone
    bool all_survive = true;                                         // FIFO intact: a memmove off-by-one would lose/dup one
    for (size_t k = 1; k < ids.size(); ++k) if (!node.channel_has(ids[k])) all_survive = false;
    CHECK(all_survive);
    CHECK(node.channel_has(ov_id));                                  // the new entry landed
    CHECK(hal.count("channel_msg_evicted") == 1);
}

TEST_CASE("buffer eviction (safe): an entry seen by ALL 1-hop neighbours is evicted before the oldest") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon(/*src=*/50, bb); node.on_recv(bb.data(), bn, meta_at(10));  // install 50 as a hops=1 neighbour
    CHECK(node.rt_count() >= 1);
    std::vector<uint32_t> ids;
    for (int k = 0; k < protocol::cap_channel_buffer; ++k) {
        const CmdResult r = send_channel(node, 7, "fill");
        ids.push_back(Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff)));
    }
    // mark a NON-oldest entry (ids[5]) as seen by the only neighbour (50) -> it becomes "safe" (all-seen).
    // (ingest a dup of ids[5] from 50: self-origin bypasses admit; existing -> mark_seen_by(ids[5], 50).)
    const uint8_t body[] = { 'x' };
    node.ingest_channel_m(mk_m(ids[5], 7, 0, body, 1), /*next=*/3, /*dst=*/3, /*from=*/50);
    send_channel(node, 7, "overflow");                              // cap+1 -> pick the SAFE entry, not the oldest
    CHECK(node.channel_buffer_count() == protocol::cap_channel_buffer);
    CHECK(!node.channel_has(ids[5]));                               // the all-seen entry was evicted (safe mode)
    CHECK(node.channel_has(ids[0]));                                // the oldest SURVIVED (would die under fallback)
    const Ev* ev = nullptr; for (const auto& e : hal.events) if (e.type == "channel_msg_evicted") ev = &e;
    CHECK(ev); if (ev) CHECK(ev->mode == "safe");
}
