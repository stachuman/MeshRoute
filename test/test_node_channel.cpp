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
    int      _rand_ret = -1;          // >=0 overrides rand_range (else returns lo)
    int      rand_calls = 0;
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;          // captured TX bytes
    std::vector<std::pair<uint32_t,uint32_t>> timers;     // (timer_id, delay)
    int      last_rx_sf = -1;
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    void     set_rx_sf(int sf) override { last_rx_sf = sf; }
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
    for (int k = 0; k < protocol::channel_origin_max_per_window; ++k) {
        const uint32_t id = (uint32_t(9) << 24) | static_cast<uint32_t>(k);
        node.ingest_channel_m(mk_m(id, 5, 0, body, 1), 9);
    }
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
    CHECK(hal.count("channel_drop_originator_throttle") == 0);
    // the (cap+1)th DISTINCT id from origin 9 -> dropped (count stays at cap)
    const uint32_t over = (uint32_t(9) << 24) | 0xFFu;
    node.ingest_channel_m(mk_m(over, 5, 0, body, 1), 9);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
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
    for (int k = 0; k < protocol::channel_origin_max_per_window; ++k)   // saturate origin 9 at the cap
        node.ingest_channel_m(mk_m((uint32_t(9) << 24) | static_cast<uint32_t>(k), 5, 0, body, 1), 9);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);
    // re-ingest an EXISTING id (origin 9, k=0) -> already-present (refresh), NOT a new entry, NOT a drop
    node.ingest_channel_m(mk_m((uint32_t(9) << 24) | 0u, 5, 0, body, 1), 9);
    CHECK(node.channel_buffer_count() == protocol::channel_origin_max_per_window);   // unchanged
    CHECK(hal.count("channel_msg_already_present") == 1);
    CHECK(hal.count("channel_drop_originator_throttle") == 0);                       // the dup was admitted, not dropped
    // a NEW distinct id from origin 9 still drops — the dup did NOT free a slot
    node.ingest_channel_m(mk_m((uint32_t(9) << 24) | 0x99u, 5, 0, body, 1), 9);
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
    node.ingest_channel_m(mk_m(ids[5], 7, 0, body, 1), /*from=*/50);
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

TEST_CASE("digest emit: a dirty entry is advertised in the BCN digest TLV; retires after K=3 ads") {
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.quiet_threshold_ms = 0;          // fast beacon path (no throttle/jitter)
    node.on_init(cfg);
    const CmdResult r = send_channel(node, 7, "hi");
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);                                       // complete the flood -> free for the beacon
    node.on_timer(kBeaconTimerId);                                     // beacon #1
    const auto* bcn = hal.last_tx_cmd(0x0); CHECK(bcn);
    if (bcn) {
        auto pb = parse_beacon(std::span<const uint8_t>(bcn->data(), bcn->size())); CHECK(pb.has_value());
        if (pb) {
            const auto ext = beacon_ext(std::span<const uint8_t>(bcn->data(), bcn->size()), *pb);
            uint32_t out[3] = {}; CHECK(parse_channel_digest_tlv(ext, out, 3) == 1); CHECK(out[0] == id);
        }
    }
    CHECK(node.channel_entry_dirty(id));                               // still dirty after 1 ad
    node.on_timer(kBeaconTimerId); node.on_timer(kBeaconTimerId);      // #2, #3 -> ad_count hits K=3
    CHECK(!node.channel_entry_dirty(id));                             // retired from advertising (still buffered)
    CHECK(node.channel_has(id));
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
            CHECK(bm_bit(bm.data(), 7));    // neighbour
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
