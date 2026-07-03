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
            std::string source; std::string mode; std::string kind; std::string reason; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    int      _rand_ret = -1;          // >=0 overrides rand_range (else returns lo)
    int      rand_calls = 0;
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;          // captured TX bytes
    std::vector<std::pair<uint32_t,uint32_t>> timers;     // (timer_id, delay)
    int      last_rx_sf = -1;
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    void     set_rx_sf(int sf) override { last_rx_sf = sf; }
    uint64_t _busy_until = 0;          // LBT knob: a far-future value (with cfg.lbt_enabled) makes tx_flood DROP (sent=false)
    uint64_t channel_busy_until() override { return _busy_until; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t delay, uint32_t id) override { timers.push_back({ id, delay }); return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { ++rand_calls; return _rand_ret >= 0 ? _rand_ret : lo; }
    void     rand_bytes(uint8_t* o, size_t n) override { for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(rand_range(0, 256)); }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            if      (std::strcmp(f[i].key, "id") == 0)         e.id = f[i].i;
            else if (std::strcmp(f[i].key, "origin") == 0)     e.origin = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "count") == 0)      e.count = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "channel_id") == 0) e.channel_id = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "source") == 0 && f[i].s) e.source = f[i].s;
            else if (std::strcmp(f[i].key, "mode") == 0 && f[i].s)   e.mode = f[i].s;
            else if (std::strcmp(f[i].key, "kind") == 0 && f[i].s)   e.kind = f[i].s;
            else if (std::strcmp(f[i].key, "reason") == 0 && f[i].s) e.reason = f[i].s;
        }
        events.push_back(std::move(e));
    }
    void     log(const char*) override {}
    int count(const char* t) const { int n = 0; for (const auto& e : events) if (e.type == t) ++n; return n; }
    const Ev* last(const char* t) const { const Ev* r = nullptr; for (const auto& e : events) if (e.type == t) r = &e; return r; }
    bool armed(uint32_t id) const { for (const auto& t : timers) if (t.first == id) return true; return false; }
    const std::vector<uint8_t>* last_tx_cmd(uint8_t cmd) const {
        const std::vector<uint8_t>* r = nullptr;
        for (const auto& f : tx_frames) if (!f.empty() && (f[0] >> 4) == cmd) r = &f;
        return r;
    }
};

static NodeConfig basic_cfg() { NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 12); return c; }

// Craft a parsed lean M frame (m_out) for a direct ingest_channel_m call. body kept alive by the caller. leaf 0.
static m_out mk_m(uint32_t id, uint8_t channel_id, uint8_t flavor, const uint8_t* body, uint8_t len) {
    m_out m{}; m.leaf_id = 0; m.channel_msg_id = id; m.channel_id = channel_id; m.flavor = flavor;
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
// A beacon from `src` carrying a CHANNEL_DIGEST ext-TLV advertising `ids`.
static size_t mk_beacon_digest(uint8_t src, const uint32_t* ids, uint8_t count, std::array<uint8_t,64>& b) {
    uint8_t ext[16];
    const size_t en = pack_channel_digest_tlv(ids, count, std::span<uint8_t>(ext, sizeof(ext)));
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x1000u + src;
    in.entries = std::span<const beacon_entry>();
    in.ext = std::span<const uint8_t>(ext, en);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}
// A CHANNEL_PULL Q from `src` to `dest` requesting `ids`.
static size_t mk_q_pull(uint8_t src, uint8_t dest, const uint32_t* ids, uint8_t count, std::array<uint8_t,32>& b) {
    q_in in{}; in.leaf_id = 0; in.src = src; in.dest = dest; in.opcode = q_opcode::channel_pull; in.mobile = false;
    in.channel_ids = std::span<const uint32_t>(ids, count);
    return pack_q(in, std::span<uint8_t>(b.data(), b.size()));
}
// An M_BROADCAST RTS from `src` (next/dst = the puller) advertising `id` at sf_index, with id_lo16.
static size_t mk_m_broadcast_rts(uint8_t src, uint8_t next, uint8_t dst, uint32_t id, uint8_t sf_index,
                                 std::array<uint8_t,16>& b) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = static_cast<uint8_t>(id & 0x0F);
    in.dst = dst; in.sf_index = sf_index; in.rts_flags = RTS_FLAG_M_BROADCAST;
    in.payload_len = 8; in.m_payload_id_lo16 = static_cast<uint16_t>(id & 0xFFFF);
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
// A lean M frame (cmd 0xA) carrying channel_id|flavor|id|body="hi" on leaf `leaf` — the data-SF frame an RTS-M
// announces. Address-less (no next/dst); the byte-0 leaf nibble is the leak gate handle_channel_data checks.
static size_t mk_m_frame(uint8_t leaf, uint32_t id, uint8_t ch, std::array<uint8_t,64>& b) {
    const uint8_t body[2] = { 'h', 'i' };
    m_in in{}; in.leaf_id = leaf; in.channel_id = ch; in.flavor = 0; in.channel_msg_id = id;
    in.body = std::span<const uint8_t>(body, 2);
    return pack_m(in, std::span<uint8_t>(b.data(), b.size()));
}
// A FLOOD RTS-M (43 B) from `src` advertising `id` with the coverage `bm32`, `hop_left`, sf_index.
static size_t mk_flood_rts(uint8_t leaf, uint8_t src, uint32_t id, const uint8_t* bm32, uint8_t hop_left,
                           uint8_t sf_index, std::array<uint8_t,64>& b) {
    rts_in in{}; in.leaf_id = leaf; in.src = src; in.next = 0xFF; in.ctr_lo = static_cast<uint8_t>(id & 0x0F);
    in.dst = hop_left;                                              // FLOOD: dst slot carries hop_left
    in.sf_index = sf_index; in.rts_flags = static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_FLOOD);
    in.payload_len = 8; in.flood_channel_msg_id = id;
    in.flood_bitmap = std::span<const uint8_t>(bm32, 32);
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
static inline bool bm_bit(const uint8_t* bm, uint8_t n) { return (bm[n >> 3] >> (n & 7)) & 1u; }
static inline void bm_set(uint8_t* bm, uint8_t n) { bm[n >> 3] |= static_cast<uint8_t>(1u << (n & 7)); }

constexpr uint32_t kBeaconTimerId       = 1;
constexpr uint32_t kCtsToDataGapTimerId = 7;
constexpr uint32_t kChannelPullTimerId  = 48;
constexpr uint32_t kMBcastClearTimerId  = 56;
constexpr uint32_t kOverhearRetuneTimerId = 57;
constexpr uint32_t kFloodRebcastTimerId = 61;   // base of the [61..63] rebroadcast ring
constexpr uint32_t kChannelReofferTimerId = 70;  // base of the [70..73] origin re-offer ring (Part 2)

// 2026-06-08 redesign: send_channel now FLOODS first (a fire-and-forget m-broadcast flight), THEN the digest
// is the repair backstop. Repair-layer tests must let that flight complete (RTS->DATA gap -> clear) so the
// node is free + the originate-flood isn't mistaken for / doesn't suppress the pull-response under test.
static void drain_originate_flood(Node& node) {
    node.on_timer(kCtsToDataGapTimerId);   // RTS -> DATA-M gap fires -> the flood DATA-M goes out
    node.on_timer(kMBcastClearTimerId);    // fire-and-forget: clear the m-broadcast flight (no ACK)
}

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
        node.ingest_channel_m(mk_m(id, /*ch=*/5, /*flavor=*/0, body, 2), /*from=*/9);
        CHECK(node.channel_buffer_count() == 1);
        CHECK(node.channel_has(id));
        CHECK(hal.count("channel_msg_received") == 1);
    }
    // (REMOVED 2026-06-13: the §7 single-layer PURE-BRIDGE ingest sub-test. is_gateway is now DERIVED=(n_layers==2),
    //  so a single-layer node is NEVER a channel gateway — the gw_env consumer/provider/pure-bridge role is gone.)
}

TEST_CASE("DATA-M ingest pushes a channel_recv to the app (origin/channel_id/body); dup raises no 2nd push") {
    const uint8_t body[] = { 'h', 'i' };
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint32_t id = Node::channel_msg_id_mint(/*origin=*/9, 0x4242u, 1);
    node.ingest_channel_m(mk_m(id, /*ch=*/5, /*flavor=*/0, body, 2), /*from=*/9);
    Push pu{}; bool got = false;
    while (node.next_push(pu)) {
        if (pu.kind == PushKind::channel_recv) {
            got = true;
            CHECK(pu.origin == 9);            // the minter (channel_msg_id high byte)
            CHECK(pu.channel_id == 5);
            CHECK(pu.body_len == 2);
            CHECK(pu.body[0] == 'h'); CHECK(pu.body[1] == 'i');
        }
    }
    CHECK(got);                               // a NEW channel message surfaces to the app, like a DM
    // A DUPLICATE ingest (already buffered) must NOT raise a second push.
    node.ingest_channel_m(mk_m(id, 5, 0, body, 2), 9);
    int n = 0; while (node.next_push(pu)) if (pu.kind == PushKind::channel_recv) ++n;
    CHECK(n == 0);
}

TEST_CASE("per-origin anti-spam: distinct-id count caps at the window max; over-cap drops; self bypasses") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint8_t body[] = { 'm' };
    // 20 distinct ids from origin 9 (vary the low bytes) -> all admitted + buffered
    for (int k = 0; k < protocol::cap_channel_origin_events; ++k) {
        const uint32_t id = (uint32_t(9) << 24) | static_cast<uint32_t>(k);
        node.ingest_channel_m(mk_m(id, 5, 0, body, 1), 9);
    }
    CHECK(node.channel_buffer_count() == protocol::cap_channel_origin_events);
    CHECK(hal.count("channel_drop_originator_throttle") == 0);
    // the (cap+1)th DISTINCT id from origin 9 -> dropped (count stays at cap)
    const uint32_t over = (uint32_t(9) << 24) | 0xFFu;
    node.ingest_channel_m(mk_m(over, 5, 0, body, 1), 9);
    CHECK(node.channel_buffer_count() == protocol::cap_channel_origin_events);
    CHECK(hal.count("channel_drop_originator_throttle") == 1);
    CHECK(!node.channel_has(over));
    // a DIFFERENT origin is independent -> admitted
    const uint32_t other = (uint32_t(10) << 24) | 1u;
    node.ingest_channel_m(mk_m(other, 5, 0, body, 1), 10);
    CHECK(node.channel_has(other));
}

TEST_CASE("anti-spam repeat-id refreshes (not re-counts): a re-broadcast can't free a slot for a new id") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint8_t body[] = { 'm' };
    for (int k = 0; k < protocol::cap_channel_origin_events; ++k)   // saturate origin 9 at the cap
        node.ingest_channel_m(mk_m((uint32_t(9) << 24) | static_cast<uint32_t>(k), 5, 0, body, 1), 9);
    CHECK(node.channel_buffer_count() == protocol::cap_channel_origin_events);
    // re-ingest an EXISTING id (origin 9, k=0) -> already-present (refresh), NOT a new entry, NOT a drop
    node.ingest_channel_m(mk_m((uint32_t(9) << 24) | 0u, 5, 0, body, 1), 9);
    CHECK(node.channel_buffer_count() == protocol::cap_channel_origin_events);   // unchanged
    CHECK(hal.count("channel_msg_already_present") == 1);
    CHECK(hal.count("channel_drop_originator_throttle") == 0);                       // the dup was admitted, not dropped
    // a NEW distinct id from origin 9 still drops — the dup did NOT free a slot
    node.ingest_channel_m(mk_m((uint32_t(9) << 24) | 0x99u, 5, 0, body, 1), 9);
    CHECK(hal.count("channel_drop_originator_throttle") == 1);
    CHECK(node.channel_buffer_count() == protocol::cap_channel_origin_events);
}

// ===================== Slice 2: duty-anchored channel cap + burst floors =====================

TEST_CASE("Slice2 — ChannelOriginLedger carries a per-origin last_flood_ms (default 0) sized by cap_channel_origin_events") {
    Node::ChannelOriginLedger L{};
    CHECK(L.n == 0);
    CHECK(L.last_flood_ms == static_cast<uint64_t>(0));   // NEW field, default-zero (the burst-floor stamp)
    CHECK(sizeof(L.ev) / sizeof(L.ev[0]) == static_cast<size_t>(protocol::cap_channel_origin_events));
}

TEST_CASE("Slice2 — channel_origin_admit drops at channel_cap_origin() (computed, not the flat 20) when duty is enabled") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg();
    cfg.duty_cycle = 0.01;                 // enable the duty plane -> channel_cap_origin() is computed (MF2 branch OFF)
    node.on_init(cfg);
    const uint16_t cap = node.channel_cap_origin();   // Slice 1 formula; small + >=1 (C>=1 floor); this SF/BW -> 2
    CHECK(cap >= 1);
    CHECK(cap < protocol::cap_channel_origin_events);  // strictly below the legacy flat 20 (SF12 is expensive)
    // Admit distinct ids from origin 9, stepping >=10s per id so ONLY the count-cap (not the burst floor) can bite.
    int admitted = 0;
    for (int k = 0; k < cap + 3; ++k) {
        hal._now = static_cast<uint64_t>(k + 1) * protocol::channel_min_interval_ms;
        const uint32_t id = (uint32_t(9) << 24) | static_cast<uint32_t>(k);
        if (node.channel_origin_admit(9, id)) ++admitted;
    }
    CHECK(admitted == cap);                                      // capped at the COMPUTED value, not 20
    CHECK(hal.count("channel_drop_originator_throttle") == 3);   // the 3 over-cap ids dropped
    CHECK(hal.count("channel_min_interval_drop") == 0);         // stepped time -> the burst floor never fired
}

TEST_CASE("Slice2 — channel_origin_admit: a too-soon (<10s) 2nd flood from an origin is dropped; >=10s is admitted") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);            // duty disabled -> roomy flat count cap; the interval is the only gate
    // First flood from origin 9 at a NON-ZERO time (so last_flood_ms stamps non-zero and the sentinel is unambiguous).
    hal._now = 1000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 0u) == true);
    CHECK(hal.count("channel_min_interval_drop") == 0);
    // +5000 (<10s): a DISTINCT id from origin 9 -> dropped by the min-interval floor (count still well under cap)
    hal._now = 6000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 1u) == false);
    CHECK(hal.count("channel_min_interval_drop") == 1);
    // +10000 from the first (>=10s): a distinct id -> admitted, interval satisfied
    hal._now = 1000 + static_cast<uint64_t>(protocol::channel_min_interval_ms);
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 2u) == true);
    CHECK(hal.count("channel_min_interval_drop") == 1);         // no new interval drop
    // A DIFFERENT origin is independent -> its first flood soon after is fine (separate last_flood_ms)
    hal._now = 6000;
    CHECK(node.channel_origin_admit(10, (uint32_t(10) << 24) | 0u) == true);
    // A refreshed DUP from origin 9 must NOT be interval-blocked even when it arrives too soon.
    hal._now = 12000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 2u) == true);   // repeat of the last-admitted id -> refresh
    CHECK(hal.count("channel_min_interval_drop") == 1);         // still no new interval drop (the dup path bypasses the floor)
}

TEST_CASE("B1 — channel_origin_admit caps recording at the ledger bound even when the policy cap exceeds it (no ev[] heap-OOB)") {
    // Regression for the 2026-07-02 gate BLOCKER: with duty ON + a cheap flood SF, the POLICY cap channel_cap_origin()
    // = C/N_active reaches ~32 at SF7 while the per-origin ledger ChannelOriginLedger.ev[] holds only 20. Pre-fix,
    // admit recorded with `if (L.n < cap=32) L.ev[L.n++]` -> once L.n passed 20 it wrote past ev[19] (heap overflow,
    // latent b/c sims run duty-off + no ASAN). The admit now clamps the enforced count to the array bound.
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg();
    cfg.duty_cycle        = 0.01;                               // duty ON -> the SF/mesh formula, not the flat legacy cap
    cfg.allowed_sf_bitmap = (1u << 7);                          // SF7 -> cheapest flood -> raw C ≈ 32 > the 20-entry ledger
    node.on_init(cfg);
    // The OOB PRECONDITION: rt_count()==0 -> N_active==1 -> the policy cap == C (≈32) which EXCEEDS the ledger bound.
    CHECK(node.channel_cap_origin() > protocol::cap_channel_origin_events);
    // Drive MORE distinct floods than the ledger holds, each spaced >= the 10s burst floor so they RECORD. The admit
    // must cap recording at cap_channel_origin_events (ev[]'s size); without the clamp, L.ev[20++] overruns the array.
    int admitted = 0;
    for (int k = 0; k < protocol::cap_channel_origin_events + 8; ++k) {
        hal._now = static_cast<uint64_t>(k + 1) * protocol::channel_min_interval_ms;   // >=10s apart -> passes the floor
        if (node.channel_origin_admit(9, (uint32_t(9) << 24) | static_cast<uint32_t>(k))) ++admitted;
    }
    CHECK(admitted == protocol::cap_channel_origin_events);     // capped at the array bound; the rest dropped, no ev[] overrun
}

TEST_CASE("Slice2 — do_send_channel self-gates own posts at the cap + the 10s floor; no self_originate_observe cap") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);           // duty disabled -> flat count cap; the interval is the near gate
    // First own post at a NON-ZERO time -> buffered + flooded, no block.
    hal._now = 1000;
    (void)send_channel(node, 7, "hello");
    CHECK(node.channel_buffer_count() == 1);
    CHECK(hal.count("send_blocked") == 0);
    drain_originate_flood(node);                                // let the originate flood flight complete
    // 2nd own post +5000 (<10s) -> self-gated by the interval floor: NOT buffered, send_blocked{channel,min_interval}.
    hal._now = 6000;
    (void)send_channel(node, 7, "again");
    CHECK(node.channel_buffer_count() == 1);                    // unchanged — the post was blocked
    CHECK(hal.count("send_blocked") == 1);
    const Ev* b = hal.last("send_blocked");
    CHECK(b != nullptr);
    if (b) { CHECK(b->kind == "channel"); CHECK(b->reason == "min_interval"); }
    // 3rd own post +10000 from the first (>=10s) -> admitted, buffered.
    hal._now = 1000 + static_cast<uint64_t>(protocol::channel_min_interval_ms);
    (void)send_channel(node, 7, "later");
    CHECK(node.channel_buffer_count() == 2);
    CHECK(hal.count("send_blocked") == 1);                      // no new block
}

TEST_CASE("Slice2 — do_send_channel self-gate: over the computed cap emits send_blocked{channel,cap}") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.duty_cycle = 0.01;       // duty on -> small computed cap (SF12 -> 2)
    node.on_init(cfg);
    const uint16_t cap = node.channel_cap_origin();
    CHECK(cap >= 1);
    CHECK(cap < protocol::cap_channel_origin_events);
    // Fill exactly `cap` own posts, stepping >=10s each so only the count-cap (not the burst floor) can bite.
    for (int k = 0; k < cap; ++k) {
        hal._now = static_cast<uint64_t>(k + 1) * protocol::channel_min_interval_ms;
        (void)send_channel(node, 7, "fill");
        drain_originate_flood(node);
    }
    CHECK(node.channel_buffer_count() == cap);
    CHECK(hal.count("send_blocked") == 0);
    // One more own post (>=10s later, so the interval is satisfied) -> blocked by the CAP, not the interval.
    hal._now = static_cast<uint64_t>(cap + 1) * protocol::channel_min_interval_ms;
    (void)send_channel(node, 7, "over");
    CHECK(node.channel_buffer_count() == cap);                  // unchanged — the post was cap-blocked
    CHECK(hal.count("send_blocked") == 1);
    const Ev* b = hal.last("send_blocked");
    CHECK(b != nullptr);
    if (b) { CHECK(b->kind == "channel"); CHECK(b->reason == "cap"); }
}

TEST_CASE("buffer eviction (fallback, no neighbours): the OLDEST goes; ALL others survive in order") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg();
    cfg.duty_cycle = 0.9;   // Slice 2: the own-post self-gate now caps distinct floods/origin; a fat duty makes
                            // channel_cap_origin() > the buffer cap so all cap_channel_buffer+1 own posts admit (this
                            // test exercises buffer EVICTION order, not the anti-spam cap).
    node.on_init(cfg);
    std::vector<uint32_t> ids;                                        // every minted id in send order
    for (int k = 0; k < protocol::cap_channel_buffer; ++k) {
        hal._now = static_cast<uint64_t>(k + 1) * protocol::channel_min_interval_ms;   // >=10s apart -> the self-interval floor never bites
        const CmdResult r = send_channel(node, 7, "fill");
        ids.push_back(Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff)));
    }
    CHECK(node.channel_buffer_count() == protocol::cap_channel_buffer);
    hal._now += protocol::channel_min_interval_ms;
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
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg();
    cfg.duty_cycle = 0.9;   // Slice 2: fat duty -> channel_cap_origin() > buffer cap so all own posts admit (this test
                            // exercises SAFE-vs-oldest eviction, not the anti-spam cap).
    node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon(/*src=*/50, bb); node.on_recv(bb.data(), bn, meta_at(10));  // install 50 as a hops=1 neighbour
    CHECK(node.rt_count() >= 1);
    std::vector<uint32_t> ids;
    for (int k = 0; k < protocol::cap_channel_buffer; ++k) {
        hal._now = static_cast<uint64_t>(k + 1) * protocol::channel_min_interval_ms;   // >=10s apart -> the self-interval floor never bites
        const CmdResult r = send_channel(node, 7, "fill");
        ids.push_back(Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff)));
    }
    // mark a NON-oldest entry (ids[5]) as seen by the only neighbour (50) -> it becomes "safe" (all-seen).
    // (ingest a dup of ids[5] from 50: self-origin bypasses admit; existing -> mark_seen_by(ids[5], 50).)
    const uint8_t body[] = { 'x' };
    node.ingest_channel_m(mk_m(ids[5], 7, 0, body, 1), /*from=*/50);
    hal._now += protocol::channel_min_interval_ms;
    send_channel(node, 7, "overflow");                              // cap+1 -> pick the SAFE entry, not the oldest
    CHECK(node.channel_buffer_count() == protocol::cap_channel_buffer);
    CHECK(!node.channel_has(ids[5]));                               // the all-seen entry was evicted (safe mode)
    CHECK(node.channel_has(ids[0]));                                // the oldest SURVIVED (would die under fallback)
    const Ev* ev = nullptr; for (const auto& e : hal.events) if (e.type == "channel_msg_evicted") ev = &e;
    CHECK(ev); if (ev) CHECK(ev->mode == "safe");
}

TEST_CASE("BCN channel-digest ext-TLV: pack/parse round-trip, count cap, multi-TLV coexistence, bounds") {
    const uint32_t ids[3] = { 0x05BEEF42u, 0xFF1234ABu, 0x01FFFFFFu };
    uint8_t buf[16] = {};
    const size_t n = pack_channel_digest_tlv(ids, 3, std::span<uint8_t>(buf, sizeof(buf)));
    CHECK(n == 1 + 1 + 12);                                         // header + count + 3*4B
    CHECK((buf[0] >> 4) == protocol::bcn_ext_type_channel_digest);
    CHECK((buf[0] & 0x0f) == 1 + 4 * 3);                            // body_len nibble = 13
    CHECK(buf[1] == 3);
    uint32_t out[3] = {};
    CHECK(parse_channel_digest_tlv(std::span<const uint8_t>(buf, n), out, 3) == 3);
    CHECK(out[0] == ids[0]); CHECK(out[1] == ids[1]); CHECK(out[2] == ids[2]);
    // count caps at channel_dirty_max_per_bcn (asking 5 packs 3 -> body_len fits the 4-bit nibble)
    const uint32_t five[5] = { 1, 2, 3, 4, 5 };
    pack_channel_digest_tlv(five, 5, std::span<uint8_t>(buf, sizeof(buf)));
    CHECK(buf[1] == protocol::channel_dirty_max_per_bcn);
    // coexistence: a foreign type-7 TLV (2-byte body) before the digest -> parse skips it
    uint8_t multi[24] = {}; multi[0] = static_cast<uint8_t>((7 << 4) | 2); multi[1] = 0xAA; multi[2] = 0xBB;
    const size_t dn = pack_channel_digest_tlv(ids, 1, std::span<uint8_t>(multi + 3, sizeof(multi) - 3));
    uint32_t out2[3] = {};
    CHECK(parse_channel_digest_tlv(std::span<const uint8_t>(multi, 3 + dn), out2, 3) == 1);
    CHECK(out2[0] == ids[0]);
    // bounds: too-small out -> 0; empty ext -> 0 ids
    uint8_t tiny[3];
    CHECK(pack_channel_digest_tlv(ids, 3, std::span<uint8_t>(tiny, sizeof(tiny))) == 0);
    CHECK(parse_channel_digest_tlv(std::span<const uint8_t>(), out, 3) == 0);
}

TEST_CASE("§P4 BCN suspect/liveness ext-TLV: pack/parse round-trip, type selection, clamp, coexistence") {
    using meshroute::SuspectEntry;
    // type-1 SUSPECT_NODES: a SILENT-only set -> a bare id list, applied by the receiver as SUSPECT(1)
    const uint8_t ids[3] = { 7, 42, 200 };
    uint8_t buf[24] = {};
    size_t n = meshroute::pack_suspect_nodes_tlv(ids, 3, std::span<uint8_t>(buf, sizeof(buf)));
    CHECK(n == 1 + 3);                                                  // header + 3 ids
    CHECK((buf[0] >> 4) == protocol::bcn_ext_type_suspect_nodes);
    CHECK((buf[0] & 0x0f) == 3);
    SuspectEntry out[8] = {};
    CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), out, 8) == 3);
    CHECK(out[0].node_id == 7);   CHECK(out[0].state == 1);             // type-1 applies as SUSPECT
    CHECK(out[1].node_id == 42);  CHECK(out[1].state == 1);
    CHECK(out[2].node_id == 200); CHECK(out[2].state == 1);
    // type-2 LIVENESS_STATE: a set containing a DEAD peer -> [id,state] pairs (silent=2 / dead=3)
    const SuspectEntry ent[3] = { { 9, 3 }, { 12, 2 }, { 30, 3 } };
    uint8_t buf2[24] = {};
    n = meshroute::pack_liveness_state_tlv(ent, 3, std::span<uint8_t>(buf2, sizeof(buf2)));
    CHECK(n == 1 + 6);                                                  // header + 3*2B
    CHECK((buf2[0] >> 4) == protocol::bcn_ext_type_liveness_state);
    CHECK((buf2[0] & 0x0f) == 6);
    SuspectEntry out2[8] = {};
    CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf2, n), out2, 8) == 3);
    CHECK(out2[0].node_id == 9);  CHECK(out2[0].state == 3);
    CHECK(out2[1].node_id == 12); CHECK(out2[1].state == 2);
    CHECK(out2[2].node_id == 30); CHECK(out2[2].state == 3);
    // n==0 -> 0 bytes (no TLV)
    CHECK(meshroute::pack_suspect_nodes_tlv(ids, 0, std::span<uint8_t>(buf, sizeof(buf))) == 0);
    CHECK(meshroute::pack_liveness_state_tlv(ent, 0, std::span<uint8_t>(buf, sizeof(buf))) == 0);
    // type-2 CLAMP: asking 8 packs at most peer_liveness_state_bcn_max(7) (2*7=14 <= the 4-bit len cap 15)
    SuspectEntry eight[8]; for (uint8_t i = 0; i < 8; ++i) eight[i] = SuspectEntry{ static_cast<uint8_t>(50 + i), 3 };
    n = meshroute::pack_liveness_state_tlv(eight, 8, std::span<uint8_t>(buf2, sizeof(buf2)));
    CHECK((buf2[0] & 0x0f) == 2 * protocol::peer_liveness_state_bcn_max);   // 14, not a wrapped value
    SuspectEntry out3[8] = {};
    CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf2, n), out3, 8) == protocol::peer_liveness_state_bcn_max);
    // coexistence: a foreign type-7 TLV before the suspect TLV -> parse skips it
    uint8_t multi[24] = {}; multi[0] = static_cast<uint8_t>((7 << 4) | 2); multi[1] = 0xAA; multi[2] = 0xBB;
    const size_t sn = meshroute::pack_suspect_nodes_tlv(ids, 2, std::span<uint8_t>(multi + 3, sizeof(multi) - 3));
    SuspectEntry out4[8] = {};
    CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(multi, 3 + sn), out4, 8) == 2);
    CHECK(out4[0].node_id == 7);
    // bounds: empty ext -> 0; odd-length type-2 body would be rejected by parse (here just the empty case)
    CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(), out, 8) == 0);
}

// Holder-aware retirement (2026-06-23): a digest entry now retires on HOLDER COVERAGE, not a blind K=3. With NO live
// 1-hop neighbour (nothing to serve) channel_entry_fully_seen is vacuously true -> it retires after the FIRST AIRED ad
// (the advertised-then-committed beacon still carries the id; the retire applies after TX). Air-honest: commit-on-`sent`.
TEST_CASE("digest emit: a dirty entry is advertised in the BCN digest TLV; with NO neighbour retires after 1 aired ad") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.quiet_threshold_ms = 0;          // fast beacon path (no throttle/jitter)
    node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "hi");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);                                       // complete the flood -> free for the beacon
    node.on_timer(kBeaconTimerId);                                     // beacon #1 (aired): advertises id, THEN commits the retire
    const auto* bcn = hal.last_tx_cmd(0x0); CHECK(bcn);
    if (bcn) {
        auto pb = parse_beacon(std::span<const uint8_t>(bcn->data(), bcn->size())); CHECK(pb.has_value());
        if (pb) {
            const auto ext = beacon_ext(std::span<const uint8_t>(bcn->data(), bcn->size()), *pb);
            uint32_t out[3] = {}; CHECK(parse_channel_digest_tlv(ext, out, 3) == 1); CHECK(out[0] == id);
        }
    }
    CHECK(!node.channel_entry_dirty(id));                             // nn==0 (no neighbour to serve) -> retired after THIS aired ad
    CHECK(node.channel_has(id));                                      // still buffered (answers pulls)
    CHECK(hal.count("channel_dirty_cleared") == 1);
}

// (A) holder-aware EARLY retire (still valid under the reverted K=3): while a live 1-hop neighbour is UNCOVERED the
// entry keeps advertising (dirty), and once that neighbour is known to HOLD it (seen_by covered via its digest
// cross-ref) the next aired ad retires it with reason "seen" — earlier than the K=3 horizon backstop would.
// (The old "stays dirty PAST 3 ads" assertion is gone: with K reverted 16→3 the horizon now retires at 3, and the
// re-offer — not a long K — is the orphan lever; the horizon path is covered by the next test.)
TEST_CASE("digest holder-aware: a covered 1-hop neighbour retires the entry early (reason seen), before the K=3 horizon") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.quiet_threshold_ms = 0;
    node.on_init(cfg);
    std::array<uint8_t,64> nb{}; node.on_recv(nb.data(), mk_beacon(7, nb), meta_at(10));   // neighbour 7 = a live hops==1 node
    const CmdResult r = send_channel(node, 5, "hi");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);
    node.on_timer(kBeaconTimerId);                                   // 1 aired ad: 7 still uncovered -> stays dirty (1 < K=3)
    CHECK(node.channel_entry_dirty(id));
    CHECK(hal.count("channel_dirty_cleared") == 0);
    std::array<uint8_t,64> db{}; node.on_recv(db.data(), mk_beacon_digest(7, &id, 1, db), meta_at(20));  // 7 advertises id -> it HOLDS it -> mark seen_by[7]
    node.on_timer(kBeaconTimerId);                                    // next aired ad (2nd, < horizon 3): now fully covered -> retire EARLY (reason "seen")
    CHECK(!node.channel_entry_dirty(id));
    CHECK(hal.count("channel_dirty_cleared") == 1);
}

// (A) the horizon SAFETY backstop: a never-covered (asymmetric — we hear it, it never pulls from us) neighbour can't
// hold the entry dirty forever; channel_dirty_max_advertisements aired ads retire it (reason "horizon").
TEST_CASE("digest holder-aware: the horizon backstop retires a never-covered neighbour after K_max aired ads") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.quiet_threshold_ms = 0;
    node.on_init(cfg);
    std::array<uint8_t,64> nb{}; node.on_recv(nb.data(), mk_beacon(7, nb), meta_at(10));   // 7 is hops==1 but never pulls
    const CmdResult r = send_channel(node, 5, "hi");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);
    for (uint8_t k = 1; k < protocol::channel_dirty_max_advertisements; ++k) {   // ads 1..K_max-1
        node.on_timer(kBeaconTimerId);
        CHECK(node.channel_entry_dirty(id));                         // still advertising (uncovered, below the horizon)
    }
    node.on_timer(kBeaconTimerId);                                   // the K_max-th aired ad -> horizon retire
    CHECK(!node.channel_entry_dirty(id));
    CHECK(hal.count("channel_dirty_cleared") == 1);
}

// (B) air-honest accounting: an advertisement that DIDN'T air (LBT-suppressed) must NOT burn an ad_count or retire;
// only an AIRED beacon commits. Drive tx_flood->false via a far-future channel-busy (with lbt_enabled).
TEST_CASE("digest air-honest: an LBT-suppressed beacon burns no ad_count / no retire; only an aired beacon commits") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.quiet_threshold_ms = 0; cfg.lbt_enabled = true;
    node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "hi");                  // (busy=0 here -> the originate flood airs)
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);
    hal._busy_until = hal._now + 100000000ull;                        // channel busy far past the flood LBT defer cap -> tx_flood DROPS
    node.on_timer(kBeaconTimerId);                                    // beacon SUPPRESSED (not aired) -> no commit
    CHECK(node.channel_entry_dirty(id));                             // NOT retired: the ad never aired
    CHECK(hal.count("channel_dirty_cleared") == 0);
    hal._busy_until = 0;                                              // channel clear -> the beacon airs
    node.on_timer(kBeaconTimerId);                                    // AIRED: commit -> nn==0 fully-seen -> retire
    CHECK(!node.channel_entry_dirty(id));
    CHECK(hal.count("channel_dirty_cleared") == 1);
}

TEST_CASE("digest ingest -> jittered pull: a missing id schedules (the DRAW) then fires a CHANNEL_PULL Q") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1234;                            // the pull jitter draw
    const uint32_t X = (uint32_t(9) << 24) | 0x123456u;              // origin 9 -> node 2 lacks it
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest(50, &X, 1, bb);
    node.on_recv(bb.data(), bn, meta_at(100));
    CHECK(hal.count("channel_pull_scheduled") == 1);
    const Ev* sch = hal.last("channel_pull_scheduled"); CHECK(sch);
    if (sch) { CHECK(sch->id == static_cast<int64_t>(X)); }
    CHECK(hal.armed(kChannelPullTimerId));                            // slot 0
    node.on_timer(kChannelPullTimerId);                              // fire the pull
    CHECK(hal.count("channel_pull_sent") == 1);
    CHECK(hal.last_tx_cmd(0x6) != nullptr);                          // a CHANNEL_PULL Q went out
}

TEST_CASE("digest ingest -> have it: a known id is NOT pulled (mark seen_by instead)") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "mine");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest(50, &id, 1, bb);              // 50 advertises the id WE hold
    node.on_recv(bb.data(), bn, meta_at(200));
    CHECK(hal.count("channel_pull_scheduled") == 0);
    CHECK(!hal.armed(kChannelPullTimerId));
}

TEST_CASE("digest ingest -> recent dedup: a 2nd digest for the same id within the window does not re-pull") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    hal._now = 100;
    const uint32_t X = (uint32_t(9) << 24) | 0x55u;
    std::array<uint8_t,64> bb{};
    size_t bn = mk_beacon_digest(50, &X, 1, bb); node.on_recv(bb.data(), bn, meta_at(100));
    node.on_timer(kChannelPullTimerId);                              // fire -> channel_pull_recent[X] set
    CHECK(hal.count("channel_pull_sent") == 1);
    hal._now = 200;                                                  // within channel_pull_window_ms (60s)
    bn = mk_beacon_digest(50, &X, 1, bb); node.on_recv(bb.data(), bn, meta_at(200));
    CHECK(hal.count("channel_pull_scheduled") == 1);                 // unchanged -> recent gate blocked the re-pull
}

TEST_CASE("pull overhear-cancel: receiving the msg before the jitter fires suppresses the pull") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 2000;
    const uint32_t X = (uint32_t(9) << 24) | 0x77u;
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest(50, &X, 1, bb); node.on_recv(bb.data(), bn, meta_at(100));
    CHECK(hal.count("channel_pull_scheduled") == 1);
    const uint8_t body[] = { 'z' };                                  // the msg arrives via DATA-M before the jitter
    node.ingest_channel_m(mk_m(X, 7, 0, body, 1), 50);
    CHECK(hal.count("channel_pull_suppressed") == 1);                // cancel_channel_pull fired
    node.on_timer(kChannelPullTimerId);                             // the now-inactive slot -> no tx
    CHECK(hal.count("channel_pull_sent") == 0);
    CHECK(hal.last_tx_cmd(0x6) == nullptr);
}

TEST_CASE("CHANNEL_PULL responder: a held id is re-broadcast as an M-payload with the M_BROADCAST RTS") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "channel-data");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);                                      // complete the flood (else channel_m_in_flight suppresses the pull-response)
    std::array<uint8_t,32> qb{};
    const size_t qn = mk_q_pull(/*src=*/5, /*dest=*/3, &id, 1, qb);   // peer 5 pulls the id FROM us (dest=3)
    node.on_recv(qb.data(), qn, meta_at(100));
    CHECK(hal.count("channel_pull_received") == 1);
    CHECK(hal.count("channel_broadcast_tx") == 1);                    // an M-payload was enqueued
    const auto* rts = hal.last_tx_cmd(0x1);                           // the M-payload flight starts -> RTS (cmd 0x1)
    CHECK(rts);
    if (rts) {
        auto pr = parse_rts(std::span<const uint8_t>(rts->data(), rts->size()));
        CHECK(pr.has_value());
        if (pr) { CHECK(pr->m_broadcast); CHECK(pr->dst == 5); }      // flagged M_BROADCAST; unicast to the puller (5)
    }
}

TEST_CASE("CHANNEL_PULL responder: a pull addressed to someone else is not served; in-flight dedup holds") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "x");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    std::array<uint8_t,32> qb{};
    // pull addressed to node 9 (not us) -> we cancel pending pulls but do NOT serve it
    size_t qn = mk_q_pull(5, /*dest=*/9, &id, 1, qb);
    node.on_recv(qb.data(), qn, meta_at(100));
    CHECK(hal.count("channel_broadcast_tx") == 0);
    // a pull addressed to us, but for an id we don't hold -> nothing broadcast
    const uint32_t unknown = (uint32_t(8) << 24) | 0x11u;
    qn = mk_q_pull(5, /*dest=*/3, &unknown, 1, qb);
    node.on_recv(qb.data(), qn, meta_at(200));
    CHECK(hal.count("channel_broadcast_tx") == 0);
}

TEST_CASE("overhear ARM: an M_BROADCAST RTS for a LACKED id retunes RX to the advertised data SF") {
    TestHal hal; Node node(hal, 2, 0xBEEFu);
    NodeConfig cfg = basic_cfg(); cfg.allowed_sf_bitmap = (1u << 9); node.on_init(cfg);   // single data SF = 9
    const uint32_t id = (uint32_t(9) << 24) | 0x1234u;                                    // origin 9 -> we lack it
    std::array<uint8_t,16> rb{};
    const size_t rn = mk_m_broadcast_rts(/*src=*/5, /*next=*/7, /*dst=*/7, id, /*sf_index=*/0, rb);  // not addressed to us
    node.on_recv(rb.data(), rn, meta_at(100));
    CHECK(hal.count("channel_overhear_armed") == 1);
    CHECK(hal.last_rx_sf == 9);                                  // retuned to the advertised SF (max allowed = 9)
    CHECK(hal.armed(kOverhearRetuneTimerId));                    // retune-back armed
    node.on_timer(kOverhearRetuneTimerId);                       // ... fires -> back to routing_sf
    CHECK(hal.last_rx_sf == cfg.routing_sf);
}

TEST_CASE("overhear ARM: an M_BROADCAST RTS for a HELD id does NOT retune (id_lo16 skip)") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.allowed_sf_bitmap = (1u << 9); node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "mine");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    std::array<uint8_t,16> rb{};
    const size_t rn = mk_m_broadcast_rts(5, 7, 7, id, 0, rb);    // advertises an id we HOLD
    node.on_recv(rb.data(), rn, meta_at(100));
    CHECK(hal.count("channel_overhear_armed") == 0);            // skipped (we have it)
    CHECK(hal.last_rx_sf == cfg.routing_sf);                    // never left routing_sf
}

TEST_CASE("M-broadcast fire-and-forget: pull -> RTS(M_BROADCAST) -> (gap) DATA-M -> clear, no CTS/ACK") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.allowed_sf_bitmap = (1u << 9); node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "data");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    std::array<uint8_t,32> qb{};
    node.on_recv(qb.data(), mk_q_pull(/*src=*/5, /*dest=*/3, &id, 1, qb), meta_at(100));   // 5 pulls from us
    const auto* rts = hal.last_tx_cmd(0x1); CHECK(rts);                 // M_BROADCAST RTS
    if (rts) { auto pr = parse_rts(std::span<const uint8_t>(rts->data(), rts->size())); CHECK(pr); if (pr) CHECK(pr->m_broadcast); }
    CHECK(hal.armed(kCtsToDataGapTimerId));                            // RTS->DATA gap (no CTS wait)
    node.on_timer(kCtsToDataGapTimerId);                              // gap fires -> DATA-M
    CHECK(hal.last_tx_cmd(0xA) != nullptr);                           // lean M frame (cmd 0xA) went out
    CHECK(hal.armed(kMBcastClearTimerId));                            // clear armed (no ACK wait)
    CHECK(node.has_pending_tx());
    node.on_timer(kMBcastClearTimerId);                              // clear fires
    CHECK(!node.has_pending_tx());
}

TEST_CASE("M-frame ingest: a leaf-matching M frame (cmd 0xA) is buffered promiscuously (no addressing)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);   // leaf 0
    const uint32_t id = (uint32_t(9) << 24) | 0xABu;
    std::array<uint8_t,64> db{};
    const size_t dn = mk_m_frame(/*leaf=*/0, id, /*ch=*/5, db);
    node.on_recv(db.data(), dn, meta_at(100));
    CHECK(node.channel_has(id));                                       // buffered (our leaf, no addressing)
}

TEST_CASE("M-frame leaf gate: an M frame for a FOREIGN leaf is dropped at ingest, NOT buffered (the leak fix)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); cfg.leaf_id = 0; node.on_init(cfg);
    const uint32_t id = (uint32_t(9) << 24) | 0xCDu;
    std::array<uint8_t,64> db{};
    // A stray M frame stamped leaf 3 (e.g. punched in from an adjacent layer) — byte-0 nibble != ours -> drop.
    node.on_recv(db.data(), mk_m_frame(/*leaf=*/3, id, /*ch=*/5, db), meta_at(100));
    CHECK(!node.channel_has(id));                                      // dropped before buffering (cross-leaf leak plugged)
    CHECK(hal.count("channel_msg_received") == 0);
    // ... and a matching-leaf one for the same id IS buffered (the gate is leaf-selective, not a blanket drop).
    node.on_recv(db.data(), mk_m_frame(/*leaf=*/0, id, /*ch=*/5, db), meta_at(110));
    CHECK(node.channel_has(id));
}

// ===================== FLOOD plane (2026-06-08 redesign) — the fast-primary state machine =====================

// 2026-06-26: the originator seeds {self + hops==1 neighbours} (the FRUGAL seed — KEPT). Part 1's "honest" empty/
// {self}-only seed was DROPPED: a 24-seed sweep showed it regresses coverage (more rebroadcast contention) with no
// orphan benefit. The {neighbours} seed is a deliberate divergence from the Lua's empty seed; the re-offer covers the gap.
TEST_CASE("FLOOD originate: do_send_channel seeds {self + hops==1 neighbours} into the RTS-M bitmap, broadcasts") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(7, bb), meta_at(10));   // neighbour 7 (hops==1)
    send_channel(node, 5, "hi");
    CHECK(hal.count("flood_tx") == 1);
    const std::vector<uint8_t>* rts = nullptr;                                             // the emitted FLOOD RTS-M
    for (auto& f : hal.tx_frames) { auto o = parse_rts(std::span<const uint8_t>(f.data(), f.size())); if (o && o->flood) rts = &f; }
    CHECK(rts != nullptr);
    if (rts) {
        auto o = parse_rts(std::span<const uint8_t>(rts->data(), rts->size())); CHECK(o.has_value());
        if (o) {
            CHECK(o->next == 0xFF);
            auto bm = rts_flood_bitmap(std::span<const uint8_t>(rts->data(), rts->size()), *o);
            CHECK(bm.size() == 32);
            CHECK(bm_bit(bm.data(), 3));    // my bit
            CHECK(bm_bit(bm.data(), 7));    // neighbour (frugal seed)
        }
    }
}

TEST_CASE("FLOOD receive: a fresh RTS-M retunes to catch the DATA-M; a 2nd (dup) does not re-retune") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint32_t id = (uint32_t(5) << 24) | 0x1234u;
    uint8_t bm[32] = {}; bm_set(bm, 1);                                     // only the sender marked
    std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, /*src=*/1, id, bm, 8, /*sf_index=*/3, rb), meta_at(20));
    CHECK(hal.count("channel_overhear_armed") == 1);
    CHECK(hal.armed(kOverhearRetuneTimerId));
    node.on_recv(rb.data(), mk_flood_rts(0, 1, id, bm, 8, 3, rb), meta_at(25));   // duplicate -> active state, no new retune
    CHECK(hal.count("channel_overhear_armed") == 1);
}

TEST_CASE("FLOOD forward: unmarked neighbour -> rebroadcast scheduled; all-marked -> silent") {
    {   // an unmarked neighbour (9) -> arm a rebroadcast
        TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
        std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(9, bb), meta_at(5));
        const uint32_t id = (uint32_t(5) << 24) | 0x22u;
        uint8_t bm[32] = {}; bm_set(bm, 1);                                 // 9 NOT marked
        std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, 1, id, bm, 8, 3, rb), meta_at(10));
        std::array<uint8_t,64> db{}; node.on_recv(db.data(), mk_m_frame(0, id, 5, db), meta_at(40));  // DATA-M body
        CHECK(node.channel_has(id));
        CHECK(hal.count("flood_rebroadcast_scheduled") == 1);
        CHECK(hal.armed(kFloodRebcastTimerId));                            // slot 0
    }
    {   // every neighbour already marked -> stay silent
        TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
        std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(9, bb), meta_at(5));
        const uint32_t id = (uint32_t(5) << 24) | 0x33u;
        uint8_t bm[32] = {}; bm_set(bm, 1); bm_set(bm, 9);                 // 9 IS marked -> covered
        std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, 1, id, bm, 8, 3, rb), meta_at(10));
        std::array<uint8_t,64> db{}; node.on_recv(db.data(), mk_m_frame(0, id, 5, db), meta_at(40));
        CHECK(node.channel_has(id));
        CHECK(hal.count("flood_rebroadcast_scheduled") == 0);             // silent (self-terminating)
    }
}

TEST_CASE("FLOOD rebroadcast: re-floods {coverage + me} with hop_left-1; hop_left<=1 -> TTL drop") {
    {   // a healthy hop_left re-floods with hop_left-1 + the extended coverage
        TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
        std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(9, bb), meta_at(5));
        const uint32_t id = (uint32_t(5) << 24) | 0x44u;
        uint8_t bm[32] = {}; bm_set(bm, 1);
        std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, 1, id, bm, /*hop_left=*/8, 3, rb), meta_at(10));
        std::array<uint8_t,64> db{}; node.on_recv(db.data(), mk_m_frame(0, id, 5, db), meta_at(40));
        const int before = hal.count("flood_tx");
        node.on_timer(kFloodRebcastTimerId);                              // slot 0 fires
        CHECK(hal.count("flood_tx") == before + 1);                       // re-flooded
        const std::vector<uint8_t>* rf = nullptr;
        for (auto& f : hal.tx_frames) { auto o = parse_rts(std::span<const uint8_t>(f.data(), f.size())); if (o && o->flood) rf = &f; }
        if (rf) { auto o = parse_rts(std::span<const uint8_t>(rf->data(), rf->size()));
                  if (o) { CHECK(o->dst == 7);                            // hop_left 8 -> 7 (rides the dst slot)
                           auto m = rts_flood_bitmap(std::span<const uint8_t>(rf->data(), rf->size()), *o);
                           CHECK(bm_bit(m.data(), 2)); CHECK(bm_bit(m.data(), 9)); } }   // +me +neighbour
    }
    {   // hop_left == 1 -> the rebroadcast would reach 0 -> TTL drop (no re-flood)
        TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
        std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(9, bb), meta_at(5));
        const uint32_t id = (uint32_t(5) << 24) | 0x55u;
        uint8_t bm[32] = {}; bm_set(bm, 1);
        std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, 1, id, bm, /*hop_left=*/1, 3, rb), meta_at(10));
        std::array<uint8_t,64> db{}; node.on_recv(db.data(), mk_m_frame(0, id, 5, db), meta_at(40));
        const int before = hal.count("flood_tx");
        node.on_timer(kFloodRebcastTimerId);
        CHECK(hal.count("flood_tx") == before);                           // NO re-flood
        CHECK(hal.count("flood_hop_exhausted") == 1);
    }
}

TEST_CASE("FLOOD fast-self-pull (§4.4): caught the RTS-M, missed the DATA-M -> pull from src on retune") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const uint32_t id = (uint32_t(5) << 24) | 0x66u;
    uint8_t bm[32] = {}; bm_set(bm, 1);
    std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, /*src=*/4, id, bm, 8, 3, rb), meta_at(10));
    CHECK(hal.armed(kOverhearRetuneTimerId));
    node.on_timer(kOverhearRetuneTimerId);                                // window closed, DATA-M never arrived
    CHECK(hal.count("channel_pull_sent") == 1);                           // fast-self-pull fired (trigger=flood_fast)
    CHECK(hal.last_tx_cmd(0x6) != nullptr);                               // a CHANNEL_PULL Q (cmd 0x6) to src
}

TEST_CASE("FLOOD leaf-mismatch: a foreign-leaf RTS-M is dropped (no overhear, no state)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); cfg.leaf_id = 0; node.on_init(cfg);
    const uint32_t id = (uint32_t(5) << 24) | 0x77u;
    uint8_t bm[32] = {}; bm_set(bm, 1);
    std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(/*leaf=*/3, 1, id, bm, 8, 3, rb), meta_at(10));  // leaf 3 != 0
    CHECK(hal.count("channel_overhear_armed") == 0);
}

// (REMOVED 2026-06-13: two §7 single-layer channel-gateway TEST_CASEs — "FLOOD pure-bridge leak guard" and
//  "FLOOD gateway+owner CONSUMES but PROVIDER-off". is_gateway is now DERIVED=(n_layers==2), so a single-layer node
//  cannot be a channel gateway; the gw_env pure-bridge / consumer-not-provider role no longer exists. A dual-layer
//  gateway skips the WHOLE channel plane (Principle 11, n_layers==2 gates) — covered by test_dual_layer.cpp.)

// ★ channel send-ctr persistence (reboot id-reuse fix). On metal the self-keyed _peer_send_counter is RAM-only, so a
// reboot resets ctr->0 and the origin re-mints channel_msg_ids it already used -> holders dedup-drop them as
// `already-buffered`. Persisting + restoring the self-keyed ctr CONTINUES it across reboot (no re-mint). Host-tested
// via the channel_ctr()/restore_channel_ctr() accessors (the device NV pack/restore is bench-verified).
TEST_CASE("channel ctr persist — reboot CONTINUES the ctr (no id re-mint); restore_channel_ctr is the fix") {
    const uint8_t ID = 254; const uint32_t KEY = 0xC0FFEE00u;
    TestHal h1; Node n1(h1, ID, KEY); { NodeConfig c = basic_cfg(); n1.on_init(c); }
    CHECK(send_channel(n1, 0, "x").code == CmdCode::queued);
    const uint16_t k = n1.channel_ctr();
    CHECK(k == 1);                                                    // first channel ctr
    const uint32_t id_pre = Node::channel_msg_id_mint(ID, KEY, static_cast<uint8_t>(k));

    // NEGATIVE control (the bug): a fresh node = the reboot WITHOUT restore re-mints the SAME ctr -> the SAME id.
    TestHal hb; Node nbug(hb, ID, KEY); { NodeConfig c = basic_cfg(); nbug.on_init(c); }
    CHECK(send_channel(nbug, 0, "x").code == CmdCode::queued);
    CHECK(nbug.channel_ctr() == k);
    CHECK(Node::channel_msg_id_mint(ID, KEY, static_cast<uint8_t>(nbug.channel_ctr())) == id_pre);   // dup id -> dropped

    // THE FIX: the reboot WITH restore_channel_ctr continues at k+1 -> a NEW id, never reused.
    TestHal hf; Node nfix(hf, ID, KEY); { NodeConfig c = basic_cfg(); nfix.on_init(c); }
    nfix.restore_channel_ctr(k);
    CHECK(send_channel(nfix, 0, "x").code == CmdCode::queued);
    CHECK(nfix.channel_ctr() == static_cast<uint16_t>(k + 1));
    CHECK(Node::channel_msg_id_mint(ID, KEY, static_cast<uint8_t>(nfix.channel_ctr())) != id_pre);   // distinct id
}

TEST_CASE("channel ctr — channel_ctr() 0 before any send (v14->0 default); restore_channel_ctr round-trips") {
    TestHal h; Node n(h, 7, 0xABCDu); { NodeConfig c = basic_cfg(); n.on_init(c); }
    CHECK(n.channel_ctr() == 0);                                      // fresh / migrated-v14 record -> no false continuity
    n.restore_channel_ctr(1234);
    CHECK(n.channel_ctr() == 1234);                                   // restored value visible
    CHECK(send_channel(n, 0, "x").code == CmdCode::queued);
    CHECK(n.channel_ctr() == 1235);                                   // the next send continues from the restored base
}

// prep-restart (2026-06-24): clear_learned_state() empties the learned tables (routes + channel buffer + pending)
// but KEEPS the provisioning (node_id / leaf / sf_list) + the stable identity, and the node re-learns afterwards.
TEST_CASE("prep-restart: clear_learned_state empties routes/channel/pending, KEEPS config+identity, re-learns after") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xABCDu);
    NodeConfig cfg = basic_cfg(); cfg.allowed_sf_bitmap = (1u << 7) | (1u << 9);   // leaf_id stays 0 to match mk_beacon's leaf
    node.on_init(cfg);
    std::array<uint8_t,64> nb{}; node.on_recv(nb.data(), mk_beacon(7, nb), meta_at(10));   // a 1-hop neighbour -> a route
    send_channel(node, 5, "hi"); drain_originate_flood(node);                              // a buffered channel msg
    CHECK(node.rt_count() > 0);
    CHECK(node.channel_buffer_count() == 1);
    const uint8_t  id0 = node.node_id(); const uint8_t leaf0 = node.config().leaf_id;
    const uint16_t sf0 = node.config().allowed_sf_bitmap; const uint32_t key0 = node.key_hash32();

    node.clear_learned_state();

    CHECK(node.rt_count() == 0);                          // routes gone
    CHECK(node.channel_buffer_count() == 0);              // channel buffer gone
    CHECK_FALSE(node.has_pending_tx());                   // no in-flight TX stranded
    CHECK(node.node_id() == id0);                         // provisioning + identity UNCHANGED
    CHECK(node.config().leaf_id == leaf0);
    CHECK(node.config().allowed_sf_bitmap == sf0);
    CHECK(node.key_hash32() == key0);
    std::array<uint8_t,64> nb2{}; node.on_recv(nb2.data(), mk_beacon(9, nb2), meta_at(20));   // re-learns (clean reset, not a break)
    CHECK(node.rt_count() > 0);
}

// ===================== Part 2: channel ORIGIN RE-OFFER (spec 2026-06-25-channel-origin-reoffer.md) =====================
// 2026-06-26: confirmation is a DEDICATED "did I overhear a RELAY of my message?" signal (channel_reoffer_confirm),
// NOT the seen_by set — so it is independent of the {neighbours} seed and of digest/pull marks. Until a relay of the
// post is overheard the origin re-floods the cached body up to channel_reoffer_max_retries; the moment it is, ZERO.

TEST_CASE("RE-OFFER: an unconfirmed origin re-floods on each timer fire up to the cap, then frees the slot") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(7, bb), meta_at(10));   // a live neighbour = a flood target
    send_channel(node, 5, "hi");
    drain_originate_flood(node);
    CHECK(hal.armed(kChannelReofferTimerId));                         // a re-offer slot was armed at origination
    CHECK(hal.count("channel_reoffer_tx") == 0);                      // the origination flood is not itself a re-offer
    for (int k = 0; k < protocol::channel_reoffer_max_retries; ++k) { // no relay overheard -> a re-flood each fire
        node.on_timer(kChannelReofferTimerId);
        drain_originate_flood(node);
        CHECK(hal.count("channel_reoffer_tx") == k + 1);
    }
    const int reoffers = hal.count("channel_reoffer_tx");
    node.on_timer(kChannelReofferTimerId);                           // retries exhausted -> free, NO re-flood
    CHECK(hal.count("channel_reoffer_tx") == reoffers);              // unchanged
    CHECK(hal.count("channel_reoffer_tx") == protocol::channel_reoffer_max_retries);
}

TEST_CASE("RE-OFFER: a confirmed origin (it overhears a RELAY of its message) never re-floods") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(7, bb), meta_at(10));
    const CmdResult r = send_channel(node, 5, "hi");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);
    // OVERHEAR a relay (node 7) rebroadcasting OUR message: a FLOOD RTS-M for our id from another node -> confirmation.
    // (NOT a digest advert and NOT a seen_by mark — the dedicated relay-overheard signal.)
    uint8_t fbm[32] = {}; bm_set(fbm, 7); bm_set(fbm, 3);
    std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, /*src=*/7, id, fbm, 8, /*sf_index=*/3, rb), meta_at(20));
    for (int k = 0; k < protocol::channel_reoffer_max_retries + 2; ++k) {   // fire well past the cap
        node.on_timer(kChannelReofferTimerId);
        drain_originate_flood(node);
    }
    CHECK(hal.count("channel_reoffer_tx") == 0);                     // confirmed (relay overheard) -> ZERO re-offers
}

TEST_CASE("RE-OFFER: the re-offer timer delay is channel_reoffer_delay_ms + the deterministic jitter (mt19937 path)") {
    TestHal hal; hal._rand_ret = 0;                                  // pin jitter to 0 -> delay == base (deterministic, not Math.random)
    Node node(hal, /*id=*/3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(7, bb), meta_at(10));
    send_channel(node, 5, "hi");
    drain_originate_flood(node);
    bool found = false;
    for (const auto& t : hal.timers)
        if (t.first == kChannelReofferTimerId) { CHECK(t.second == protocol::channel_reoffer_delay_ms); found = true; break; }
    CHECK(found);
}

TEST_CASE("Node::limits_snapshot — live values for a known config") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.duty_cycle = 0.0;   // shipped default -> duty disabled
    node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon(/*src=*/50, bb); node.on_recv(bb.data(), bn, meta_at(10));  // rt_count() -> 1
    CHECK(node.rt_count() >= 1);
    Node::LimitsSnapshot s = node.limits_snapshot();
    CHECK(s.win_ms == protocol::originator_window_ms);   // 300000
    CHECK(s.n == node.rt_count());
    CHECK(s.ch_sf == 12);                                // basic_cfg allowed_sf_bitmap == (1u<<12) -> max_data_sf()==12 (private, pinned by value)
    CHECK(s.ch_cap == node.channel_cap_origin());
    CHECK(s.duty_ms == node.channel_duty_budget_ms());   // 0 when duty disabled
    CHECK(s.duty_ms == 0);
    CHECK(s.ch_ceiling == 0);                            // C == 0 when duty disabled (legacy-flat-cap regime)
    CHECK(s.ch_next_ms == 0);                            // fresh node, no prior flood/DM -> ready now
    CHECK(s.dm_next_ms == 0);
    CHECK(s.dm_min_ms == protocol::dm_min_interval_ms);
    CHECK(s.ch_min_ms == protocol::channel_min_interval_ms);

    // duty ENABLED -> duty_ms == the 5-min D (1% * 300000 = 3000), NOT the 1-hour budget (MF1)
    TestHal hal2; Node n2(hal2, 3, 0x1234ABCDu);
    NodeConfig c2 = basic_cfg(); c2.duty_cycle = 0.01; n2.on_init(c2);
    Node::LimitsSnapshot s2 = n2.limits_snapshot();
    CHECK(s2.duty_ms == 3000);                           // == channel_duty_budget_ms(), 5-min basis
    CHECK(s2.duty_ms == n2.channel_duty_budget_ms());
    CHECK(s2.ch_ceiling >= 1);                           // C >= 1 floor when duty enabled
}

// Slice 6 integration: the outcome-feedback machinery is reachable through a real Node — emit_send_blocked
// enqueues a drainable send_blocked push, and the origin re-offer EXHAUSTION path (channel_reoffer_fire's
// give-up branch) enqueues channel_sent{relayed:false}. basic_cfg has a data SF (1<<12 -> max_data_sf()==12),
// so a posted message registers a re-offer slot whose retries exhaust via retries_left==0 (not data-incapable).
TEST_CASE("Slice 6: emit_send_blocked + emit_channel_sent{relayed:false} reachable through a real Node") {
    TestHal hal; Node node(hal, /*id=*/20, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);           // provisions a data SF (12) -> channel-capable

    // 6a: the self-gate helper enqueues a send_blocked the companion can drain.
    node.emit_send_blocked(/*channel=*/true, SendFailReason::min_interval, /*next_ms=*/7300);
    Push p{};
    CHECK(node.next_push(p));
    CHECK(p.kind == PushKind::send_blocked);
    CHECK(p.blocked_channel == true);
    CHECK(p.reason == SendFailReason::min_interval);
    CHECK(p.next_ms == 7300);

    // 6c: post a channel message (registers an origin re-offer slot at slot 0, retries_left=1). Never overhear
    // a relay; fire the re-offer timer until retries exhaust, then drain and confirm channel_sent{relayed:false}.
    hal._now = 1000;
    (void)send_channel(node, /*ch=*/7, "hi");                  // origination flood + channel_reoffer_register(id)
    drain_originate_flood(node);                               // let the originate flight complete (no ACK)
    // First fire: retries_left 1 -> 0, re-floods + re-arms. Second fire: retries_left==0 -> exhaustion give-up.
    node.on_timer(kChannelReofferTimerId + 0);
    node.on_timer(kChannelReofferTimerId + 0);
    bool saw_no_relay = false;
    while (node.next_push(p))
        if (p.kind == PushKind::channel_sent && !p.relayed) saw_no_relay = true;
    CHECK(saw_no_relay);
}
