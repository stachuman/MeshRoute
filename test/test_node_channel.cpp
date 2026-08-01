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
#include "identity.h"            // §team-ch-key T-K3: Identity / identity_from_seed — the two ends of the sealed grant
#include "monocypher.h"          // §team-ch-key: crypto_x25519_public_key — cross-check the minted pair independently
#include "support/test_hal.h"

#include <algorithm>             // §chan-crypt CL2b: std::search — "the packed position is nowhere in the aired body"
#include <array>
#include <cstdlib>               // §chan-crypt CL2b: std::abs on the pack_loc6 quantisation bound
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

struct Ev { std::string type; int64_t id = -1; int origin = -1; int count = -1; int channel_id = -1;
            std::string source; std::string mode; std::string kind; std::string reason; };

class TestHal : public mrtest::TestHalBase {
public:
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;          // captured TX bytes
    std::vector<std::pair<uint32_t,uint32_t>> timers;     // (timer_id, delay)
    int      last_rx_sf = -1;
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    void     set_rx_sf(int sf) override { last_rx_sf = sf; }
    uint64_t _busy_until = 0;          // LBT knob: a far-future value (with cfg.lbt_enabled) makes tx_flood DROP (sent=false)
    uint64_t channel_busy_until() override { return _busy_until; }
    bool     after(uint32_t delay, uint32_t id) override { timers.push_back({ id, delay }); return true; }
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

static CmdResult send_channel(Node& n, uint8_t ch, const char* text, bool team = false, bool global = false,
                              CryptIntent crypt = CryptIntent::def, bool loc = false) {
    Command c{}; c.kind = CmdKind::send_channel; c.u.channel.channel_id = ch;
    c.u.channel.team = team; c.u.channel.global = global;   // §S7 T-B: plane select (plain => GLOBAL/leaf; -t => TEAM; -t -g => BOTH)
    c.u.channel.loc = loc;   // §chan-crypt CL2b: `-l` => attach this node's position INSIDE the seal (defaulted false — every pre-CL2b call site unchanged)
    c.body = reinterpret_cast<const uint8_t*>(text); c.body_len = static_cast<uint8_t>(std::strlen(text));
    c.crypt = crypt;   // §chan-crypt CL1: `-e` => CryptIntent::on (defaulted so every pre-CL1 call site is unchanged — U1)
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
// A beacon from `src` carrying a CHANNEL_DIGEST ext-TLV *and* a type-5 team_id TLV (peer_team = team_id) — i.e.
// a team member's beacon (the team plane rides on the mobile beacon, so is_mobile is set). Drives the §team digest
// gate at node_beacon.cpp:835 (metal 2026-07-17): a FOREIGN-team digest must NOT provoke a CHANNEL_PULL.
static size_t mk_beacon_digest_team(uint8_t src, const uint32_t* ids, uint8_t count, uint32_t team_id,
                                    std::array<uint8_t,64>& b) {
    uint8_t ext[32];
    size_t en = pack_channel_digest_tlv(ids, count, std::span<uint8_t>(ext, sizeof(ext)));
    en += pack_team_id_tlv(team_id, std::span<uint8_t>(ext + en, sizeof(ext) - en));
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x1000u + src; in.is_mobile = true;
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
static size_t mk_team_flood_rts(uint8_t src, uint32_t id, const uint8_t* bm32, uint8_t hop_left, std::array<uint8_t,64>& b) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = 0xFF; in.ctr_lo = static_cast<uint8_t>(id & 0x0F);
    in.dst = hop_left; in.sf_index = 0; in.rts_flags = static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_FLOOD);
    in.payload_len = 8; in.flood_channel_msg_id = id; in.flood_bitmap = std::span<const uint8_t>(bm32, 32); in.mobile_src = true;   // §S7: mobile_src => TEAM flood
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

// ============================ §mobile 6.3 — team channel =====================================

TEST_CASE("§mobile 6.3 — team M-frame codec: team_id round-trips (BE, 11-B hdr); a non-team M is 7-B byte-identical") {
    const uint8_t body[3] = { 'h', 'i', '!' };
    // non-team (flavor without the team bit) -> 7-B header, byte-identical to today
    { m_in in{}; in.leaf_id = 2; in.channel_id = 5; in.flavor = protocol::channel_flavor_public;
      in.channel_msg_id = 0x11223344u; in.body = std::span<const uint8_t>(body, 3);
      std::array<uint8_t, 32> b{}; size_t n = pack_m(in, std::span<uint8_t>(b.data(), b.size()));
      CHECK(n == 7 + 3);
      auto o = parse_m(std::span<const uint8_t>(b.data(), n));
      CHECK(o.has_value());
      if (o) { CHECK(o->team_id == 0); CHECK(o->channel_msg_id == 0x11223344u); CHECK(o->body.size() == 3); } }
    // team (flavor has the team bit) -> 11-B header + team_id (BIG-endian at bytes 7..10)
    { m_in in{}; in.leaf_id = 2; in.channel_id = 5;
      in.flavor = static_cast<uint8_t>(protocol::channel_flavor_public | protocol::channel_flavor_team);
      in.channel_msg_id = 0x11223344u; in.team_id = 0xABCD1234u; in.body = std::span<const uint8_t>(body, 3);
      std::array<uint8_t, 32> b{}; size_t n = pack_m(in, std::span<uint8_t>(b.data(), b.size()));
      CHECK(n == 11 + 3);
      CHECK(b[7] == 0xAB); CHECK(b[8] == 0xCD); CHECK(b[9] == 0x12); CHECK(b[10] == 0x34);   // team_id BE
      auto o = parse_m(std::span<const uint8_t>(b.data(), n));
      CHECK(o.has_value());
      if (o) { CHECK(o->team_id == 0xABCD1234u); CHECK((o->flavor & protocol::channel_flavor_team));
               CHECK(o->body.size() == 3); CHECK(o->body[0] == 'h'); }
      // a truncated team frame (team flag set, < 11 B) -> nullopt (no OOB read of the team_id)
      CHECK_FALSE(parse_m(std::span<const uint8_t>(b.data(), 9)).has_value()); }
}

TEST_CASE("§mobile 6.3 — ingest team gate: same-team ingests; static + other-team drop; a non-team leaf M is ingested by all (planes=both)") {
    const uint8_t body[2] = { 'h', 'i' };
    const uint32_t T = 0xABCD1234u, U = 0x00009999u;
    auto mk_team = [&](uint32_t id, uint32_t team) {
        m_out m = mk_m(id, /*ch=*/5, static_cast<uint8_t>(protocol::channel_flavor_public | protocol::channel_flavor_team), body, 2);
        m.team_id = team; return m; };
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 1);
    // (a) static node + team frame -> DROPPED
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); n.on_init(c);
      n.ingest_channel_m(mk_team(id, T), 9); CHECK(n.channel_buffer_count() == 0); }
    // (b) team-T member + team-T frame -> BUFFERED
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = T; n.on_init(c);
      n.ingest_channel_m(mk_team(id, T), 9); CHECK(n.channel_buffer_count() == 1); }
    // (c) team-T member + team-U frame -> DROPPED (a different team)
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = T; n.on_init(c);
      n.ingest_channel_m(mk_team(id, U), 9); CHECK(n.channel_buffer_count() == 0); }
    // (d) team-T member + a NON-team leaf frame -> BUFFERED (★ planes=both: a team member still hears the leaf channel)
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = T; n.on_init(c);
      n.ingest_channel_m(mk_m(id, 5, protocol::channel_flavor_public, body, 2), 9); CHECK(n.channel_buffer_count() == 1); }
    // (e) static node + a non-team leaf frame -> BUFFERED (unchanged)
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); n.on_init(c);
      n.ingest_channel_m(mk_m(id, 5, protocol::channel_flavor_public, body, 2), 9); CHECK(n.channel_buffer_count() == 1); }
}

TEST_CASE("§mobile 6.3 — a team member's channel post emits a mobile_src RTS-M + a team_id M-frame; a static post does neither") {
    // team member: the FLOOD RTS-M carries mobile_src, the M-frame carries team_id
    { TestHal hal; Node n(hal, 3, 0x1234ABCDu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xABCD1234u; n.on_init(c);
      const CmdResult r = send_channel(n, 7, "team-hi", /*team=*/true); CHECK(r.code == CmdCode::queued);   // §S7 T-B: a TEAM post is explicit `-t`
      const std::vector<uint8_t>* rts = hal.last_tx_cmd(0x1);   // the FLOOD RTS-M
      CHECK(rts != nullptr);
      if (rts) { auto pr = parse_rts(std::span<const uint8_t>(rts->data(), rts->size())); CHECK(pr.has_value()); if (pr) CHECK(pr->mobile_src); }
      drain_originate_flood(n);                                 // fire the M-frame on the data SF
      const std::vector<uint8_t>* mf = hal.last_tx_cmd(0xA);
      CHECK(mf != nullptr);
      if (mf) { auto pm = parse_m(std::span<const uint8_t>(mf->data(), mf->size())); CHECK(pm.has_value());
                if (pm) { CHECK(pm->team_id == 0xABCD1234u); CHECK((pm->flavor & protocol::channel_flavor_team)); } } }
    // static node: no mobile_src, no team_id (byte-identical origination)
    { TestHal hal; Node n(hal, 3, 0x1234ABCDu); NodeConfig c = basic_cfg(); n.on_init(c);
      CHECK(send_channel(n, 7, "hi").code == CmdCode::queued);
      const std::vector<uint8_t>* rts = hal.last_tx_cmd(0x1);
      if (rts) { auto pr = parse_rts(std::span<const uint8_t>(rts->data(), rts->size())); if (pr) CHECK_FALSE(pr->mobile_src); }
      drain_originate_flood(n);
      const std::vector<uint8_t>* mf = hal.last_tx_cmd(0xA);
      if (mf) { auto pm = parse_m(std::span<const uint8_t>(mf->data(), mf->size())); if (pm) CHECK(pm->team_id == 0); } }
}

TEST_CASE("§mobile 6.3 — a static / non-team node does NOT participate in a TEAM (mobile_src) channel flood; a team member does") {
    uint8_t bm[32] = {};   // empty coverage (the receiver is not marked)
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 1);
    auto mk_team_flood = [&](std::array<uint8_t,64>& b) {
        rts_in in{}; in.leaf_id = 0; in.src = 9; in.next = 0xFF; in.ctr_lo = static_cast<uint8_t>(id & 0x0F);
        in.dst = 3 /*hop_left*/; in.sf_index = 0; in.rts_flags = static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_FLOOD);
        in.payload_len = 8; in.flood_channel_msg_id = id; in.flood_bitmap = std::span<const uint8_t>(bm, 32); in.mobile_src = true;
        return pack_rts(in, std::span<uint8_t>(b.data(), b.size())); };
    // static node -> skips: never arms the overhear retune (so it never buffers/re-floods the team message)
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); n.on_init(c);
      std::array<uint8_t,64> b{}; size_t bn = mk_team_flood(b);
      n.on_recv(b.data(), bn, meta_at(1000));
      CHECK(hal.count("channel_overhear_armed") == 0); }       // ★ did not participate in the team flood
    // team member -> participates: arms the overhear retune to catch the M-frame
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xABCD1234u; n.on_init(c);
      std::array<uint8_t,64> b{}; size_t bn = mk_team_flood(b);
      n.on_recv(b.data(), bn, meta_at(1000));
      CHECK(hal.count("channel_overhear_armed") == 1); }       // ★ participating
}

TEST_CASE("§mobile 6.3 — the overhear-retune window sizes for the +4-B team M-frame (mobile_src) so it isn't dropped at high SF") {
    uint8_t bm[32] = {}; bm_set(bm, 5);
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 1);
    // A team member catches a FLOOD RTS-M; return the armed overhear-retune delay. mobile_src=true is a TEAM frame
    // (+4-B team_id tail on the M-frame); mobile_src=false is a plain leaf frame (7-B). Same body/SF -> the team
    // window MUST be larger (it accounts for the extra 4 header bytes), else the team M-frame drops at data SF>=10.
    auto arm_delay = [&](bool mobile_src) -> uint32_t {
        TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg();
        if (mobile_src) { c.is_mobile = true; c.team_id = 0xABCD1234u; }   // §S7 T-B: a TEAM flood -> a team member participates; a plain LEAF flood -> a STATIC participates (an off-grid mobile no longer catches leaf floods)
        n.on_init(c);
        std::array<uint8_t,64> b{}; rts_in in{}; in.leaf_id=0; in.src=9; in.next=0xFF; in.ctr_lo=static_cast<uint8_t>(id & 0x0F);
        in.dst=3; in.sf_index=0; in.rts_flags=static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_FLOOD);
        in.payload_len=20; in.flood_channel_msg_id=id; in.flood_bitmap=std::span<const uint8_t>(bm,32); in.mobile_src=mobile_src;
        size_t bn = pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
        n.on_recv(b.data(), bn, meta_at(1000));
        for (const auto& t : hal.timers) if (t.first == kOverhearRetuneTimerId) return t.second;
        return 0; };
    const uint32_t team_delay = arm_delay(true);
    const uint32_t plain_delay = arm_delay(false);
    CHECK(team_delay > 0); CHECK(plain_delay > 0);
    CHECK(team_delay > plain_delay);   // ★ the team window accounts for the +4-B team_id tail
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

// §team digest gate (metal 2026-07-17): the digest REACTION (process_channel_digest) is team-gated at
// node_beacon.cpp:835. A node must NEVER react to a FOREIGN-team digest — the served M could never pass our
// ingest containment gate (node_channel.cpp:192), so a MISSING->pull ping-pong would run forever (metal: static
// 43 pulled 12ADA20C from team members 106/218 every beacon cycle, indefinitely). These four cases pin the truth
// table. The observable is channel_pull_scheduled (0 = the gate blocked the reaction; 1 = the pull is preserved).
TEST_CASE("digest team-gate: a STATIC node (team_id=0) hearing a TEAM digest does NOT pull (the fix)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);   // team_id=0 -> static
    hal._now = 100; hal._rand_ret = 1234;
    const uint32_t X = (uint32_t(9) << 24) | 0x123456u;                    // origin 9 -> node 2 lacks it (and, being static, could NEVER hold a team M)
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest_team(50, &X, 1, 0xAAAAAAAAu, bb);   // a team member (peer_team=0xAAAAAAAA) advertises X
    node.on_recv(bb.data(), bn, meta_at(100));
    CHECK(hal.count("channel_pull_scheduled") == 0);                       // FOREIGN-team digest -> no reaction (breaks the ping-pong)
    CHECK(!hal.armed(kChannelPullTimerId));
}
TEST_CASE("digest team-gate: a TEAM member hearing an OTHER-team digest does NOT pull (same disease)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xAAAAAAAAu; node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1234;
    const uint32_t X = (uint32_t(9) << 24) | 0x123456u;
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest_team(50, &X, 1, 0xBBBBBBBBu, bb);   // a DIFFERENT team (peer_team=0xBBBBBBBB != ours)
    node.on_recv(bb.data(), bn, meta_at(100));
    CHECK(hal.count("channel_pull_scheduled") == 0);
    CHECK(!hal.armed(kChannelPullTimerId));
}
TEST_CASE("digest team-gate: a TEAM member hearing a SAME-team digest DOES pull (team repair backstop preserved)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xAAAAAAAAu; node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1234;
    const uint32_t X = (uint32_t(9) << 24) | 0x123456u;
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest_team(50, &X, 1, 0xAAAAAAAAu, bb);   // SAME team (peer_team == ours) -> gate lets it through
    node.on_recv(bb.data(), bn, meta_at(100));
    CHECK(hal.count("channel_pull_scheduled") == 1);
    CHECK(hal.armed(kChannelPullTimerId));
}
TEST_CASE("digest team-gate: a TEAM member hearing a STATIC digest (peer_team=0) DOES pull (leaf msgs reach a registered mobile)") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xAAAAAAAAu; node.on_init(cfg);
    hal._now = 100; hal._rand_ret = 1234;
    const uint32_t X = (uint32_t(9) << 24) | 0x123456u;
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest(50, &X, 1, bb);                     // a STATIC beacon (no team TLV -> peer_team==0) -> pullable by everyone
    node.on_recv(bb.data(), bn, meta_at(100));
    CHECK(hal.count("channel_pull_scheduled") == 1);
    CHECK(hal.armed(kChannelPullTimerId));
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

// L7 (2026-07-04 wave-3): the FLOOD RTS-M `dst` slot carries hop_left off the wire (unauthenticated). A forged
// hop_left=255 must be clamped to flood_hop_max on ingest, so the re-flooded TTL can't exceed the mesh diameter.
TEST_CASE("L7 — a forged FLOOD hop_left=255 is clamped to flood_hop_max on ingest") {
    TestHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(9, bb), meta_at(5));   // a live neighbour -> flood target
    const uint32_t id = (uint32_t(5) << 24) | 0x99u;
    uint8_t bm[32] = {}; bm_set(bm, 1);
    std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_flood_rts(0, 1, id, bm, /*hop_left=*/255, 3, rb), meta_at(10));
    std::array<uint8_t,64> db{}; node.on_recv(db.data(), mk_m_frame(0, id, 5, db), meta_at(40));
    const int before = hal.count("flood_tx");
    node.on_timer(kFloodRebcastTimerId);                                  // slot 0 fires
    CHECK(hal.count("flood_tx") == before + 1);                           // still re-floods (clamped, not dropped)
    const std::vector<uint8_t>* rf = nullptr;
    for (auto& f : hal.tx_frames) { auto o = parse_rts(std::span<const uint8_t>(f.data(), f.size())); if (o && o->flood) rf = &f; }
    CHECK(rf != nullptr);
    if (rf) { auto o = parse_rts(std::span<const uint8_t>(rf->data(), rf->size()));
              // ingest clamps 255 -> flood_hop_max(16); the re-flood decrements once -> 15 on the wire dst slot.
              if (o) CHECK(o->dst == static_cast<uint8_t>(protocol::flood_hop_max - 1)); }
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

// §b38 helper — drain every queued Push and summarise the channel_sent outcomes. Returns the count; `last_*` carry
// the final one. Used by the outcome tests below: the CONTRACT is "exactly ONE channel_sent per origination", so the
// count matters as much as the value (a `true` followed by a `false` is the failure this slice exists to prevent).
struct ChSentSummary { int n = 0; int n_true = 0; int n_false = 0; bool last_relayed = false; uint16_t last_ctr = 0; };
static ChSentSummary drain_channel_sent(Node& n) {
    ChSentSummary s{}; Push p{};
    while (n.next_push(p))
        if (p.kind == PushKind::channel_sent) {
            ++s.n; if (p.relayed) ++s.n_true; else ++s.n_false;
            s.last_relayed = p.relayed; s.last_ctr = p.ctr;
        }
    return s;
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
    // §b38 — the NON-TEAM plane is unchanged by this slice and this pins it: exactly ONE channel_sent{relayed:true},
    // at the confirm, carrying the originating ctr. Here `relayed` means "the flood COMPLETED" (see emit_channel_sent).
    const ChSentSummary s = drain_channel_sent(node);
    CHECK(s.n == 1);
    CHECK(s.n_true == 1);
    CHECK(s.last_ctr == r.ctr);
}

// ★ P-BUDGET (s28 class): a TEAM flood does NOT let one overheard relay confirm coverage (a mixed multi-hop team chain
// has far members a single near relay never reached). So a team origin IGNORES the relay-overheard confirm and re-offers
// ALL channel_reoffer_team_max_retries. (Contrast the non-team test above: relay overheard -> zero.)
TEST_CASE("RE-OFFER: a TEAM flood re-offers all its retries DESPITE overhearing a relay (mixed-chain coverage)") {
    static_assert(protocol::channel_reoffer_team_max_retries > protocol::channel_reoffer_max_retries,
                  "team re-offer must be more persistent than the relay-confirmed non-team single shot");
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xABCD1234u; node.on_init(cfg);
    const CmdResult r = send_channel(node, /*ch=*/5, "team-hi", /*team=*/true);   // team origination -> team re-offer registered
    CHECK(r.code == CmdCode::queued);
    const uint32_t id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
    drain_originate_flood(node);
    // OVERHEAR a relay of OUR team message (would CONFIRM+stop a non-team re-offer) — the team path must ignore it.
    // ⚠⚠ FIXED BY §b38 — THIS TEST USED TO BUILD THE WRONG FRAME AND WAS THEREFORE VACUOUS ON ITS OWN PREMISE. It sent
    // mk_flood_rts (mobile_src == 0, a LEAF flood), and this node is an OFF-GRID mobile, so node_mac_rx.cpp's
    // `!(_cfg.is_mobile && !r.mobile_src && !mobile_registered())` gate DROPPED it before handle_flood_rts ->
    // channel_reoffer_confirm was NEVER CALLED and the "DESPITE overhearing a relay" clause tested nothing. It passed
    // because an unconfirmed team origin re-offers all its retries anyway. mk_team_flood_rts (mobile_src == 1) is the
    // frame a real teammate relay actually airs, and it reaches the confirm. Found by the §b38 push assertions below.
    uint8_t fbm[32] = {}; bm_set(fbm, 7); bm_set(fbm, node.team_local_id());
    std::array<uint8_t,64> rb{}; node.on_recv(rb.data(), mk_team_flood_rts(/*src=*/7, id, fbm, 8, rb), meta_at(20));
    // ★★★ §b38 — THE TWO RETRY LOOPS ARE SEPARATE, AND THIS TEST IS WHERE THAT IS PINNED. The NODE's re-offers must
    // keep running (the loop below) while the CONSUMER's outcome is reported ONCE, immediately at the confirm above.
    // Before §b38 the confirm returned before the emit, so this post could only ever end `relayed:false` — a false
    // negative on every team post the whole team received.
    const ChSentSummary at_confirm = drain_channel_sent(node);
    CHECK(at_confirm.n == 1);                                        // reported IMMEDIATELY on the first confirm...
    CHECK(at_confirm.n_true == 1);                                   // ...and truthfully ("at least one relay observed")
    CHECK(at_confirm.last_ctr == r.ctr);
    for (int k = 0; k < protocol::channel_reoffer_team_max_retries; ++k) {   // each fire re-floods despite the relay
        node.on_timer(kChannelReofferTimerId);
        drain_originate_flood(node);
        CHECK(hal.count("channel_reoffer_tx") == k + 1);
    }
    // bounded: past the team cap, no further re-offer
    node.on_timer(kChannelReofferTimerId); drain_originate_flood(node);
    CHECK(hal.count("channel_reoffer_tx") == protocol::channel_reoffer_team_max_retries);
    // ★ AND EXHAUSTION MUST NOT CONTRADICT THE `true` ALREADY SENT — no second push, of either polarity. This is the
    // relay_seen latch; without it the app would see success then failure for one post.
    const ChSentSummary after_exhaustion = drain_channel_sent(node);
    CHECK(after_exhaustion.n == 0);
}

// §b38 — the OTHER team arm: a team post NO teammate ever relays still reports the truth, `relayed:false`, once, at
// exhaustion. (The pre-§b38 behaviour for EVERY team post, right or wrong; after §b38 it is reserved for the case
// that actually earns it.)
TEST_CASE("RE-OFFER §b38: a TEAM post that is NEVER relayed reports channel_sent{relayed:false} once, at exhaustion") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xABCD1234u; node.on_init(cfg);
    const CmdResult r = send_channel(node, /*ch=*/5, "team-hi", /*team=*/true);
    CHECK(r.code == CmdCode::queued);
    drain_originate_flood(node);
    CHECK(drain_channel_sent(node).n == 0);                          // nothing reported while the re-offers still run
    for (int k = 0; k < protocol::channel_reoffer_team_max_retries; ++k) {   // never confirmed -> re-offer every fire
        node.on_timer(kChannelReofferTimerId); drain_originate_flood(node);
        CHECK(drain_channel_sent(node).n == 0);                      // ...and still nothing: the budget is not spent
    }
    node.on_timer(kChannelReofferTimerId); drain_originate_flood(node);      // retries_left == 0 -> give up
    const ChSentSummary s = drain_channel_sent(node);
    CHECK(s.n == 1);
    CHECK(s.n_false == 1);
    CHECK(s.last_ctr == r.ctr);
    node.on_timer(kChannelReofferTimerId); drain_originate_flood(node);      // the slot is freed: no repeat outcome
    CHECK(drain_channel_sent(node).n == 0);
}

// ★★ §b38 — B40's coverage, and the finding it demonstrates: the emitted ctr is the FULL 16-bit origination handle,
// while the ON-WIRE msg-id keeps only its low byte BY DESIGN. Both facts are asserted here, because the second one is
// what stops a future reader from matching this handle against a received id.
// Values required by the register: 255 · 256 · 257 · 65535->1, plus a LOW-BYTE-COLLIDING pair (255 and 511 mint the
// IDENTICAL channel_msg_id) whose outcomes must remain distinguishable.
TEST_CASE("RE-OFFER §b40: channel_sent carries the FULL 16-bit ctr — 255/256/257/wrap, and a low-byte collision stays distinguishable") {
    TestHal hal; Node node(hal, /*id=*/3, 0x1234ABCDu); NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    std::array<uint8_t,64> bb{}; node.on_recv(bb.data(), mk_beacon(7, bb), meta_at(10));
    uint32_t last_id = 0;
    // Post once with next_ctr seeded to `seed`, then OVERHEAR a relay -> the confirm reports the outcome.
    // Returns the pushed ctr; `minted` gets the command's own return value and `msg_id` the on-wire id.
    auto post_and_confirm = [&](uint16_t seed, uint8_t ch, uint16_t& minted, uint32_t& msg_id) -> ChSentSummary {
        hal._now += protocol::channel_min_interval_ms + 1;           // clear the own-origin burst floor
        node.restore_channel_ctr(seed);                              // the self-keyed counter IS the channel ctr
        const CmdResult r = send_channel(node, ch, "x");
        CHECK(r.code == CmdCode::queued);
        minted = r.ctr;
        msg_id = Node::channel_msg_id_mint(3, 0x1234ABCDu, static_cast<uint8_t>(r.ctr & 0xff));
        drain_originate_flood(node);
        uint8_t fbm[32] = {}; bm_set(fbm, 7); bm_set(fbm, 3);
        std::array<uint8_t,64> rb{};
        node.on_recv(rb.data(), mk_flood_rts(0, /*src=*/7, msg_id, fbm, 8, /*sf_index=*/3, rb), meta_at(hal._now));
        return drain_channel_sent(node);
    };
    uint16_t minted = 0; uint32_t msg_id = 0;
    {   ChSentSummary s = post_and_confirm(254, 5, minted, msg_id);          // -> ctr 255, the last value the old code got right
        CHECK(minted == 255); CHECK(s.n == 1); CHECK(s.last_ctr == 255);
        last_id = msg_id; }
    {   ChSentSummary s = post_and_confirm(255, 6, minted, msg_id);          // -> ctr 256: the old code pushed 0
        CHECK(minted == 256); CHECK(s.n == 1); CHECK(s.last_ctr == 256);
        CHECK((msg_id & 0xff) == 0); }                                       // the WIRE really does carry only the low byte
    {   ChSentSummary s = post_and_confirm(256, 7, minted, msg_id);          // -> ctr 257: the old code pushed 1
        CHECK(minted == 257); CHECK(s.n == 1); CHECK(s.last_ctr == 257); }
    {   ChSentSummary s = post_and_confirm(65535, 8, minted, msg_id);        // next_ctr wraps 65535 -> 1 (never 0)
        CHECK(minted == 1); CHECK(s.n == 1); CHECK(s.last_ctr == 1); }
    {   ChSentSummary s = post_and_confirm(510, 9, minted, msg_id);          // -> ctr 511: low byte 0xFF, colliding with 255
        CHECK(minted == 511); CHECK(s.n == 1); CHECK(s.last_ctr == 511);
        CHECK(msg_id == last_id); }                                          // ★ SAME on-wire id, DIFFERENT outcome handle
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

// ============================ §S7 — plane-keyed flood (T-A) + channel plane membership (T-B) =================
// T-A: a team-scoped flood consults the TEAM peer set (_rt_team) for coverage; a static flood consults _rt.
// The SAME functions (flood_set_my_coverage / flood_any_unmarked), plane-keyed — no table mixing.
TEST_CASE("§S7 T-A — a team member re-floods a TEAM flood to an UNMARKED team peer; SILENT when covered (coverage keyed on _rt_team)") {
    const uint32_t T = 0xABCD1234u;
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 1);
    auto run = [&](bool cover_peer) -> int {
        TestHal hal; Node n(hal, /*id=*/50, 0xBEEFu);
        NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = T; n.on_init(c);
        n.set_team_local_id(50);
        n.test_learn_route(60, 60, 1, protocol::db_to_q4(9.0f), /*team_plane=*/true);   // team peer 60, 1 hop
        uint8_t bm[32] = {}; bm_set(bm, 50); if (cover_peer) bm_set(bm, 60);
        std::array<uint8_t,64> b{}; size_t bn = mk_team_flood_rts(9, id, bm, 5, b);
        n.on_recv(b.data(), bn, meta_at(1000));                 // team member participates -> flood-state (awaiting_data)
        const uint8_t body[] = { 't','m' };
        m_out m = mk_m(id, /*ch=*/5, static_cast<uint8_t>(protocol::channel_flavor_public | protocol::channel_flavor_team), body, 2);
        m.team_id = T;
        n.ingest_channel_m(m, /*from=*/9);                       // DATA-M -> flood_forward_decision (team plane)
        return hal.count("flood_rebroadcast_scheduled");
    };
    CHECK(run(/*cover_peer=*/false) == 1);   // team peer 60 UNMARKED -> re-flood on the team plane
    CHECK(run(/*cover_peer=*/true)  == 0);   // team peer 60 COVERED -> silent
}

// ===================== §F-CH-RELAY — HOLDER re-offer on unconfirmed downstream team coverage =====================
// A RELAY (not the origin) that re-broadcasts a TEAM flood and still has an UNMARKED hops-1 team neighbour (a downstream
// member not yet confirmed to hold it) re-offers the cached body — the origin's re-offer only reaches ITS OWN neighbours,
// so 3+-hop members are otherwise stranded. Coverage-driven (seen_by), bounded, deterministic jitter, TEAM-only.

// Drive a team member to become a holder of `id` (RTS-M from `up`, DATA-M), with a downstream team peer `down` that is
// left UNMARKED, then fire the scheduled rebroadcast. Returns the node so the caller can drive the re-offer timer.
static void hold_team_flood(TestHal& hal, Node& n, uint32_t id, uint8_t up, uint8_t down) {
    NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xABCD1234u; n.on_init(c);
    n.set_team_local_id(50);
    n.test_learn_route(down, down, 1, protocol::db_to_q4(9.0f), /*team_plane=*/true);   // downstream team peer, 1 hop
    uint8_t bm[32] = {}; bm_set(bm, 50); bm_set(bm, up);                                // self + upstream covered; `down` UNMARKED
    std::array<uint8_t,64> rb{}; size_t rn = mk_team_flood_rts(up, id, bm, 5, rb);
    n.on_recv(rb.data(), rn, meta_at(1000));                                            // catch RTS-M -> flood-state
    const uint8_t body[] = { 't','m' };
    m_out m = mk_m(id, /*ch=*/5, static_cast<uint8_t>(protocol::channel_flavor_public | protocol::channel_flavor_team), body, 2);
    m.team_id = 0xABCD1234u;
    n.ingest_channel_m(m, /*from=*/up);                                                 // DATA-M -> buffered + flood_forward_decision (rebroadcast scheduled)
    n.on_timer(kFloodRebcastTimerId);                                                   // the relay re-broadcasts (slot 0) -> arms the holder re-offer (down still unmarked)
    drain_originate_flood(n);
}

TEST_CASE("§F-CH-RELAY — a team-flood HOLDER with an UNMARKED downstream peer arms a coverage-driven re-offer, bounded") {
    TestHal hal; Node n(hal, /*id=*/50, 0xBEEFu);
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 7);
    hold_team_flood(hal, n, id, /*up=*/9, /*down=*/60);
    CHECK(hal.armed(kChannelReofferTimerId));                          // a HOLDER re-offer slot was armed after the re-broadcast
    CHECK(hal.count("channel_holder_reoffer_tx") == 0);               // ...but not yet fired
    for (int k = 0; k < protocol::channel_holder_reoffer_max_retries; ++k) {   // downstream stays unmarked -> re-offer each fire
        n.on_timer(kChannelReofferTimerId); drain_originate_flood(n);
        CHECK(hal.count("channel_holder_reoffer_tx") == k + 1);
    }
    n.on_timer(kChannelReofferTimerId); drain_originate_flood(n);      // BOUNDED: past the holder cap -> free, no further re-offer
    CHECK(hal.count("channel_holder_reoffer_tx") == protocol::channel_holder_reoffer_max_retries);
}

TEST_CASE("§F-CH-RELAY — overhearing a downstream peer re-broadcast (seen_by mark) STOPS the holder re-offer (coverage complete)") {
    TestHal hal; Node n(hal, /*id=*/50, 0xBEEFu);
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 8);
    hold_team_flood(hal, n, id, /*up=*/9, /*down=*/60);
    CHECK(hal.armed(kChannelReofferTimerId));
    // Overhear the downstream peer (60) re-broadcasting our buffered id: a TEAM (mobile_src) RTS-M we already hold ->
    // the already-buffered branch marks seen_by(60). Now every hops-1 team neighbour is covered.
    uint8_t bm[32] = {}; bm_set(bm, 60); bm_set(bm, 50);
    std::array<uint8_t,64> rb{}; size_t rn = mk_team_flood_rts(/*src=*/60, id, bm, 5, rb);
    n.on_recv(rb.data(), rn, meta_at(2000));
    for (int k = 0; k < protocol::channel_holder_reoffer_max_retries + 2; ++k) {   // fire well past the cap
        n.on_timer(kChannelReofferTimerId); drain_originate_flood(n);
    }
    CHECK(hal.count("channel_holder_reoffer_tx") == 0);              // downstream now marked -> coverage complete -> ZERO re-offers
    // ★★ §b38 — A HOLDER MUST NEVER PUSH channel_sent. This node is a RELAY: node 9 originated the post, node 50 only
    // carried it. The RTS-M above reached channel_reoffer_confirm through handle_flood_rts's already-buffered branch
    // and found node 50's HOLDER slot — before §b38 that was absorbed by the blanket `if (rp.team) return`, so making
    // the team arm report WITHOUT the holder guard would have every relay claim an origination it never made.
    CHECK(drain_channel_sent(n).n == 0);
}

TEST_CASE("§F-CH-RELAY — a STATIC / leaf flood HOLDER never arms a holder re-offer (team-scoped; delivery-suite inert)") {
    TestHal hal; Node n(hal, /*id=*/50, 0xBEEFu);
    NodeConfig c = basic_cfg(); n.on_init(c);                          // STATIC node (no team)
    n.test_learn_route(70, 70, 1, protocol::db_to_q4(9.0f), /*team_plane=*/false);   // static neighbour 70, UNMARKED below
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 9);
    uint8_t bm[32] = {}; bm_set(bm, 50); bm_set(bm, 9);               // 70 UNMARKED -> the relay WILL re-broadcast
    std::array<uint8_t,64> rb{}; size_t rn = mk_flood_rts(0, 9, id, bm, 5, 0, rb);    // mobile_src=false -> LEAF flood
    n.on_recv(rb.data(), rn, meta_at(1000));
    const uint8_t body[] = { 'x' };
    n.ingest_channel_m(mk_m(id, 5, /*flavor=*/0, body, 1), 9);
    n.on_timer(kFloodRebcastTimerId); drain_originate_flood(n);       // re-broadcasts on the static plane
    CHECK(hal.count("channel_holder_reoffer_tx") == 0);              // no team plane -> no holder re-offer (static/leaf byte-inert)
    CHECK_FALSE(hal.armed(kChannelReofferTimerId));                  // no re-offer slot armed from the relay path
}

TEST_CASE("§S7 T-A — a STATIC flood keys coverage on _rt (NOT _rt_team); the two bitmaps never mix") {
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 1);
    auto run = [&](bool cover_static_nbr) -> int {
        TestHal hal; Node n(hal, /*id=*/50, 0xBEEFu);
        NodeConfig c = basic_cfg(); n.on_init(c);               // STATIC node
        n.test_learn_route(70, 70, 1, protocol::db_to_q4(9.0f), /*team_plane=*/false);   // static neighbour 70
        uint8_t bm[32] = {}; bm_set(bm, 50); bm_set(bm, 9); if (cover_static_nbr) bm_set(bm, 70);   // cover me + the src (a static learns the flood src as a neighbour, line node_mac_rx.cpp:62)
        std::array<uint8_t,64> b{}; size_t bn = mk_flood_rts(0, 9, id, bm, 5, 0, b);    // mobile_src=false (static/leaf)
        n.on_recv(b.data(), bn, meta_at(1000));
        const uint8_t body[] = { 'x' };
        n.ingest_channel_m(mk_m(id, 5, /*flavor=*/0, body, 1), 9);
        return hal.count("flood_rebroadcast_scheduled");
    };
    CHECK(run(/*cover_static_nbr=*/false) == 1);   // static neighbour 70 UNMARKED -> re-flood (static plane, unchanged)
    CHECK(run(/*cover_static_nbr=*/true)  == 0);   // covered -> silent
}

TEST_CASE("§S7 T-B — an OFF-GRID mobile does NOT catch a leaf flood; a REGISTERED mobile catches + ingests but NEVER re-floods") {
    const uint32_t id = Node::channel_msg_id_mint(9, 0x1u, 1);
    uint8_t bm[32] = {}; bm_set(bm, 9);
    // (a) OFF-GRID mobile + a LEAF flood (mobile_src=false) -> does NOT participate
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xAAu; n.on_init(c);
      std::array<uint8_t,64> b{}; size_t bn = mk_flood_rts(0, 9, id, bm, 5, 0, b);
      n.on_recv(b.data(), bn, meta_at(1000));
      CHECK(hal.count("channel_overhear_armed") == 0); }        // off-grid: no catch/ingest of a leaf flood
    // (b) REGISTERED mobile + a LEAF flood -> catches (arms overhear), ingests (record), but NEVER re-floods
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xAAu; n.on_init(c);
      n.test_set_my_mobile_reg(/*home_id=*/17, /*local_id=*/2);
      std::array<uint8_t,64> b{}; size_t bn = mk_flood_rts(0, 9, id, bm, 5, 0, b);
      n.on_recv(b.data(), bn, meta_at(1000));
      CHECK(hal.count("channel_overhear_armed") == 1);          // registered leaf citizen: catches
      const uint8_t body[] = { 'h','i' };
      n.ingest_channel_m(mk_m(id, 5, 0, body, 2), 9);
      CHECK(n.channel_buffer_count() == 1);                     // ingested (record)
      CHECK(hal.count("flood_rebroadcast_scheduled") == 0); }   // ...but NEVER re-floods a leaf flood
}

TEST_CASE("§S7 T-B — send_channel plane select: static -t refused, plain=leaf; registered mobile plain=GLOBAL delegate, -t=TEAM, both; off-grid plain fails loud") {
    // STATIC node: -t refused (fail loud), plain = the leaf post (byte-identical to pre-S7)
    { TestHal hal; Node n(hal, 20, 0xBEEFu); NodeConfig c = basic_cfg(); n.on_init(c);
      CHECK(send_channel(n, 9, "x", /*team=*/true).code == CmdCode::err_no_binding);
      CHECK(send_channel(n, 9, "x").code == CmdCode::queued); CHECK(n.channel_buffer_count() == 1); }
    // OFF-GRID team mobile: plain (GLOBAL) fails loud (no home); -t (TEAM) buffers locally
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xAAu; n.on_init(c);
      CHECK(send_channel(n, 9, "x").code == CmdCode::err_no_binding);   // plain=GLOBAL, no home -> fail loud
      CHECK(n.channel_buffer_count() == 0);
      CHECK(send_channel(n, 9, "tm", /*team=*/true).code == CmdCode::queued);   // -t=TEAM origin
      CHECK(n.channel_buffer_count() == 1); }
    // REGISTERED team mobile: plain (GLOBAL) DELEGATES to the home as MOBILE_SEND + enclosed DATA_TYPE_CHANNEL_POST
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xAAu; n.on_init(c);
      n.test_set_my_mobile_reg(/*home_id=*/17, /*local_id=*/2);
      n.test_suspend_tx_drain(true);                            // hold the flight in the queue (else become_free pulls it into _pending_tx)
      const CmdResult r = send_channel(n, /*ch=*/9, "global-post");   // plain => GLOBAL => delegate
      CHECK(r.code == CmdCode::queued);
      CHECK(n.channel_buffer_count() == 0);                     // a GLOBAL post is NOT buffered locally (the home mints it)
      CHECK(n.test_tx_queue_n() >= 1);
      const uint8_t i = static_cast<uint8_t>(n.test_tx_queue_n() - 1);
      CHECK(n.test_tx_type(i) == DATA_TYPE_MOBILE_SEND);
      CHECK(n.test_tx_dst(i) == 17);                            // to the home
      uint8_t ilen = 0; const uint8_t* inner = n.test_tx_inner(i, ilen);
      auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, ilen), n.test_tx_flags(i));
      CHECK(ui.has_value());
      if (ui && ui->body.size() >= 2) {
          CHECK(ui->has_source_hash);                          // SOURCE_HASH = the mobile (home verifies "ours")
          CHECK(ui->body[0] == DATA_TYPE_CHANNEL_POST);        // enclosed marker
          CHECK(ui->body[1] == 9);                             // channel_id
          CHECK(std::string(reinterpret_cast<const char*>(ui->body.data() + 2), ui->body.size() - 2) == "global-post");
      } }
    // REGISTERED team mobile: BOTH (-t -g) = one TEAM origination (buffered) + one delegated GLOBAL (MOBILE_SEND)
    { TestHal hal; Node n(hal, 2, 0xBEEFu); NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = 0xAAu; n.on_init(c);
      n.test_set_my_mobile_reg(/*home_id=*/17, /*local_id=*/2);
      n.test_suspend_tx_drain(true);                            // hold the flights in the queue
      const CmdResult r = send_channel(n, 9, "both", /*team=*/true, /*global=*/true);
      CHECK(r.code == CmdCode::queued);
      CHECK(n.channel_buffer_count() == 1);                     // the TEAM half is buffered (self-originated flood)
      bool saw_deleg = false;
      for (uint8_t i = 0; i < n.test_tx_queue_n(); ++i) if (n.test_tx_type(i) == DATA_TYPE_MOBILE_SEND) saw_deleg = true;
      CHECK(saw_deleg); }                                       // the GLOBAL half is delegated to the home
}

// ===================== §team-parity T6/B — the per-(PLANE,origin) channel anti-spam ledger =====================
// The corpus CANNOT see this fix: byte-identity across all 34 scenarios is unchanged by it (0/34), because no scenario
// happens to have a team origin and a static origin that COLLIDE numerically. These two cases are therefore the ONLY
// detector for the aliasing half of T6/B, and the second one is deliberately written as the pre-fix counter-example.
TEST_CASE("§T6/B — channel_origin_admit keys the ledger by (PLANE, origin): a TEAM origin N and a STATIC origin N do not share a burst floor") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);            // duty disabled -> roomy count cap; the interval is the gate
    // A LEAF/static M from origin 9 lands first and stamps the STATIC ledger row's last_flood_ms.
    hal._now = 1000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 0u, /*team_plane=*/false) == true);
    CHECK(hal.count("channel_min_interval_drop") == 0);
    // 5 s later (well inside channel_min_interval_ms) a TEAM-scoped M arrives from a TEAMMATE whose team_local_id is
    // ALSO 9 (§18: the two id spaces collide). PRE-T6 this shared ONE ledger row, so the teammate's post was dropped by
    // the static neighbour's burst floor. It must be ADMITTED: it is a different plane, hence a different origin.
    hal._now = 6000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 1u, /*team_plane=*/true) == true);
    CHECK(hal.count("channel_min_interval_drop") == 0);         // ★ the pre-T6 value here was 1
    // Symmetry: the TEAM row now has its OWN floor, so a second team post too soon IS dropped (the fix separates the
    // planes, it does not disable the throttle).
    hal._now = 8000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 2u, /*team_plane=*/true) == false);
    CHECK(hal.count("channel_min_interval_drop") == 1);
    // ...and the STATIC row's own floor is likewise still enforced, untouched by the team traffic in between.
    hal._now = 9000;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 3u, /*team_plane=*/false) == false);
    CHECK(hal.count("channel_min_interval_drop") == 2);
}

TEST_CASE("§T6/B — the two planes' distinct-id COUNT caps are independent (a team origin cannot exhaust a static origin's window)") {
    TestHal hal; Node node(hal, /*id=*/2, 0xBEEFu);
    NodeConfig cfg = basic_cfg();
    cfg.duty_cycle = 0.01;                                      // duty ON -> a small computed cap, so exhaustion is cheap
    node.on_init(cfg);
    const uint16_t cap = node.channel_cap_origin();
    CHECK(cap >= 1);
    // Fill the STATIC row for origin 9 to its cap, stepping >=10 s so only the count cap can bite.
    for (int k = 0; k < cap; ++k) {
        hal._now = static_cast<uint64_t>(k + 1) * protocol::channel_min_interval_ms;
        CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | static_cast<uint32_t>(k), /*team_plane=*/false) == true);
    }
    // One more STATIC id is now correctly throttled...
    hal._now = static_cast<uint64_t>(cap + 1) * protocol::channel_min_interval_ms;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 0x40u, /*team_plane=*/false) == false);
    CHECK(hal.count("channel_drop_originator_throttle") == 1);
    // ...but the TEAM row for the SAME numeric origin 9 is a fresh window and admits. PRE-T6 this returned false: the
    // static neighbour's traffic had consumed the teammate's entire distinct-id budget.
    hal._now = static_cast<uint64_t>(cap + 2) * protocol::channel_min_interval_ms;
    CHECK(node.channel_origin_admit(9, (uint32_t(9) << 24) | 0x50u, /*team_plane=*/true) == true);
    CHECK(hal.count("channel_drop_originator_throttle") == 1);   // ★ the pre-T6 value here was 2
}

// ================= §team-ch-key (T-K1, spec 2026-07-26 §2.1) — the TEAM CHANNEL keypair =====================
// ★ THIS SUITE IS THE ENTIRE DETECTOR for the Node half of T-K1. The scenario corpus cannot reach it at all:
// the simulator's `team` verb REFUSES `team new` by design (NodeRuntimeWrapper.cpp §sim-team-verb — a random
// nonce would break byte-reproducibility), and src/firmware_config.cpp is not compiled into the sim, so no
// scenario can mint, adopt or persist a team channel key. Byte-identity of the corpus proves only that this
// slice stays INERT there; correctness lives here.
namespace {
// A Hal whose crypto RNG yields a scripted byte stream. TestHalBase::rand_bytes defaults to ALL-ZERO (it draws
// through the forced-rand seam), which is precisely the dead-RNG case the mint must refuse — so the refusal
// test needs no override at all and the success tests need this one.
class TkHal : public TestHal {
public:
    uint8_t _fill = 0;                 // 0 => keep the all-zero base behaviour (mint must refuse)
    int     rand_bytes_calls = 0;
    size_t  rand_bytes_total = 0;
    void rand_bytes(uint8_t* o, size_t n) override {
        ++rand_bytes_calls; rand_bytes_total += n;
        if (!_fill) { TestHal::rand_bytes(o, n); return; }
        for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(_fill + i);   // deterministic, non-degenerate
    }
};
static NodeConfig team_cfg(uint32_t team = 0xAAAAAAAAu) {
    NodeConfig c = basic_cfg(); c.is_mobile = true; c.team_id = team; return c;
}
}  // namespace

TEST_CASE("§team-ch-key — a fresh node holds NO team channel key (accessors are nullptr, not a zero buffer)") {
    TkHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = team_cfg(); node.on_init(cfg);
    CHECK(node.team_channel_key_present() == false);
    CHECK(node.team_channel_pub()  == nullptr);       // nullptr, so no caller can mistake zeros for a key
    CHECK(node.team_channel_priv() == nullptr);
    CHECK(hal.rand_bytes_calls == 0);                 // on_init draws NO crypto bytes (the corpus-inertness property)
}

TEST_CASE("§team-ch-key — mint: 32 CSPRNG bytes -> a canonical pair; the flag flips; pub == X25519(priv, 9)") {
    TkHal hal; hal._fill = 0x31; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = team_cfg(); node.on_init(cfg);
    CHECK(node.team_channel_key_mint());
    CHECK(node.team_channel_key_present());
    CHECK(node.team_channel_pub()  != nullptr);
    CHECK(node.team_channel_priv() != nullptr);
    CHECK(hal.rand_bytes_calls  == 1);                 // exactly ONE draw
    CHECK(hal.rand_bytes_total  == 32);                //   of exactly 32 bytes
    const uint8_t* priv = node.team_channel_priv();
    CHECK((priv[0] & 7) == 0);                         // stored CANONICAL (RFC 7748 clamp)
    CHECK((priv[31] & 0x80) == 0);
    CHECK((priv[31] & 0x40) != 0);
    uint8_t want[32]; crypto_x25519_public_key(want, priv);
    CHECK(std::memcmp(node.team_channel_pub(), want, 32) == 0);
}

TEST_CASE("§team-ch-key — C2: a dead crypto RNG (all-zero draw) REFUSES the mint and leaves the node keyless") {
    TkHal hal; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = team_cfg(); node.on_init(cfg);   // _fill 0 => all-zero rand_bytes
    CHECK(node.team_channel_key_mint() == false);
    CHECK(node.team_channel_key_present() == false);   // no flag, no half-keypair
    CHECK(node.team_channel_pub() == nullptr);
    CHECK(hal.rand_bytes_calls == 1);                  // it DID try — the refusal is the RNG check, not a skip
}

TEST_CASE("§team-ch-key — two nodes mint DIFFERENT keys; a re-mint on one node REPLACES its key") {
    TkHal ha; ha._fill = 0x31; Node a(ha, 2, 0xBEEFu); NodeConfig ca = team_cfg(); a.on_init(ca);
    TkHal hb; hb._fill = 0x77; Node b(hb, 3, 0xCAFEu); NodeConfig cb = team_cfg(); b.on_init(cb);
    CHECK(a.team_channel_key_mint());
    CHECK(b.team_channel_key_mint());
    CHECK(std::memcmp(a.team_channel_pub(), b.team_channel_pub(), 32) != 0);

    uint8_t first[32]; std::memcpy(first, a.team_channel_pub(), 32);
    ha._fill = 0x02;                                   // re-key (spec §0/Q5: the v1 revocation story)
    CHECK(a.team_channel_key_mint());
    CHECK(std::memcmp(a.team_channel_pub(), first, 32) != 0);
    CHECK(a.team_channel_key_present());
}

TEST_CASE("§team-ch-key — adopt: a matching pair is accepted VERBATIM (the QR / grant path)") {
    TkHal src; src._fill = 0x31; Node creator(src, 2, 0xBEEFu); NodeConfig cc = team_cfg(); creator.on_init(cc);
    CHECK(creator.team_channel_key_mint());
    uint8_t pub[32], priv[32];
    std::memcpy(pub,  creator.team_channel_pub(),  32);
    std::memcpy(priv, creator.team_channel_priv(), 32);

    // The JOINER adopts. Its own RNG is the dead all-zero one, proving adopt draws NOTHING.
    TkHal hal; Node joiner(hal, 3, 0xCAFEu); NodeConfig cj = team_cfg(); joiner.on_init(cj);
    CHECK(joiner.team_channel_key_adopt(pub, priv));
    CHECK(joiner.team_channel_key_present());
    CHECK(std::memcmp(joiner.team_channel_pub(),  pub,  32) == 0);
    CHECK(std::memcmp(joiner.team_channel_priv(), priv, 32) == 0);
    CHECK(hal.rand_bytes_calls == 0);                  // ★ adopt is DRAW-FREE — the T-K4 QR path costs no entropy
}

TEST_CASE("§team-ch-key — C2 adopt refusals: mismatched pub, all-zero priv, all-zero pub; state untouched") {
    TkHal src; src._fill = 0x31; Node creator(src, 2, 0xBEEFu); NodeConfig cc = team_cfg(); creator.on_init(cc);
    CHECK(creator.team_channel_key_mint());
    uint8_t pub[32], priv[32];
    std::memcpy(pub,  creator.team_channel_pub(),  32);
    std::memcpy(priv, creator.team_channel_priv(), 32);

    TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig cn = team_cfg(); n.on_init(cn);

    uint8_t bad_pub[32]; std::memcpy(bad_pub, pub, 32); bad_pub[7] ^= 0x01;   // ONE flipped bit — a mis-scanned QR
    CHECK(n.team_channel_key_adopt(bad_pub, priv) == false);
    CHECK(n.team_channel_key_present() == false);

    uint8_t zero[32] = {};
    CHECK(n.team_channel_key_adopt(pub, zero) == false);      // all-zero private half
    CHECK(n.team_channel_key_adopt(zero, priv) == false);     // all-zero public half (cannot match a real priv)
    CHECK(n.team_channel_key_present() == false);
    CHECK(n.team_channel_pub() == nullptr);

    // A refused adopt must not have disturbed an ALREADY-HELD key either.
    CHECK(n.team_channel_key_adopt(pub, priv));
    CHECK(n.team_channel_key_adopt(bad_pub, priv) == false);
    CHECK(std::memcmp(n.team_channel_pub(), pub, 32) == 0);
    CHECK(n.team_channel_key_present());
}

TEST_CASE("§team-ch-key — adopt CANONICALISES an unclamped scalar and derives the pub the RFC names") {
    // The T-K3/T-K4 wire may carry a scalar produced by a store-unclamped implementation. Adopt must normalise
    // it and keep the pair valid, which is only true because clamping is idempotent (see identity.h).
    TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig cn = team_cfg(); n.on_init(cn);
    uint8_t raw[32], rfc_pub[32];
    auto hx = [](const char* h, uint8_t* o) { for (int i = 0; i < 32; ++i) {
        auto nib = [](char c) -> uint8_t { return static_cast<uint8_t>(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); };
        o[i] = static_cast<uint8_t>((nib(h[2*i]) << 4) | nib(h[2*i+1])); } };
    hx("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", raw);        // RFC 7748 §6.1, UNCLAMPED
    hx("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", rfc_pub);
    CHECK(n.team_channel_key_adopt(rfc_pub, raw));            // pub matches DESPITE priv arriving unclamped
    CHECK(std::memcmp(n.team_channel_pub(), rfc_pub, 32) == 0);
    CHECK(std::memcmp(n.team_channel_priv(), raw, 32) != 0);     // stored form is the CLAMPED scalar, not the input
    CHECK((n.team_channel_priv()[0] & 7) == 0);
}

TEST_CASE("§team-ch-key — NV round-trip: load(present) restores verbatim; load(!present) cannot fabricate a key") {
    TkHal hal; hal._fill = 0x31; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = team_cfg(); node.on_init(cfg);
    CHECK(node.team_channel_key_mint());
    uint8_t pub[32], priv[32];
    std::memcpy(pub,  node.team_channel_pub(),  32);
    std::memcpy(priv, node.team_channel_priv(), 32);

    // Reboot: a fresh Node, then the fw_main restore.
    TkHal h2; Node rebooted(h2, 2, 0xBEEFu); NodeConfig c2 = team_cfg(); rebooted.on_init(c2);
    rebooted.team_channel_key_load(pub, priv, true);
    CHECK(rebooted.team_channel_key_present());
    CHECK(std::memcmp(rebooted.team_channel_pub(),  pub,  32) == 0);
    CHECK(std::memcmp(rebooted.team_channel_priv(), priv, 32) == 0);
    CHECK(h2.rand_bytes_calls == 0);                   // a boot restore NEVER draws (no re-derivation)

    // The stale/absent-blob path fw_main takes: a ZERO-INITIALISED mrnv::Blob -> present=0 + zero buffers.
    uint8_t zpub[32] = {}, zpriv[32] = {};
    TkHal h3; Node fresh(h3, 2, 0xBEEFu); NodeConfig c3 = team_cfg(); fresh.on_init(c3);
    fresh.team_channel_key_load(zpub, zpriv, false);
    CHECK(fresh.team_channel_key_present() == false);
    CHECK(fresh.team_channel_pub()  == nullptr);
    CHECK(fresh.team_channel_priv() == nullptr);
}

// ★★ §o3-key-lifetime (owner ruling 2026-07-31) — THE KEY LIVES EXACTLY AS LONG AS THE team_id IT WAS GRANTED FOR.
// This case REPLACES T-K1's deliberate tripwire ("the pair is orthogonal to set_team_id … if a later slice clears on
// switch, this test SHOULD fail and be updated deliberately"). It did, and this is that update.
// ⚠ The clear is INERT until CL2a makes posts seal — nothing in this binary or the corpus seals under the pair yet —
// so these asserts ARE the whole detector for the mechanism (the T-K1/T-K3 structural argument).
TEST_CASE("§o3-key-lifetime — set_team_id CLEARS the team channel key on a switch AND on `team 0`") {
    TkHal hal; hal._fill = 0x31; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = team_cfg(0xAAAAAAAAu); node.on_init(cfg);
    CHECK(node.team_channel_key_mint());
    CHECK(node.team_channel_key_present());

    // (1) DIFFERENT NON-ZERO team -> the pair is GONE. The hazard this exists to stop: sealing a post for team B
    //     under team A's key the moment CL2a lands.
    CHECK(node.set_team_id(0xBBBBBBBBu));
    CHECK_FALSE(node.team_channel_key_present());
    CHECK(node.team_channel_pub()  == nullptr);      // ★ the ACCESSORS, not just the flag — a caller must never see a zero buffer
    CHECK(node.team_channel_priv() == nullptr);
    CHECK(node.config().team_id == 0xBBBBBBBBu);     // the switch itself still happened

    // (2) NEGATIVE CONTROL — a key granted AFTER the switch survives, i.e. the clear is scoped to the switch and is
    //     not some blanket "a team member cannot hold a key".
    CHECK(node.team_channel_key_mint());
    CHECK(node.team_channel_key_present());
    uint8_t pub_b[32]; std::memcpy(pub_b, node.team_channel_pub(), 32);

    // (3) SAME id -> a no-op, and it must clear NOTHING. The key is UNRECOVERABLE (no seed derives it), so a
    //     spurious clear here would destroy data only a teammate's re-grant could restore.
    CHECK_FALSE(node.set_team_id(0xBBBBBBBBu));
    CHECK(node.team_channel_key_present());
    CHECK(std::memcmp(node.team_channel_pub(), pub_b, 32) == 0);   // ★ byte-identical, not merely "present"

    // (4) `team 0` (leave) -> CLEARED too. With no team the key applies to nothing.
    CHECK(node.set_team_id(0));
    CHECK(node.config().team_id == 0);
    CHECK_FALSE(node.team_channel_key_present());
    CHECK(node.team_channel_pub() == nullptr);
    // (4b) leaving again is already a no-op (set_team_id returns false at its first line) — nothing to re-clear
    CHECK_FALSE(node.set_team_id(0));
    CHECK_FALSE(node.team_channel_key_present());

    CHECK(hal.rand_bytes_calls == 2);   // ★ exactly the two MINTS — no switch, leave or no-op draws entropy
}

// ⓘ NOT COVERED, deliberately: that the clear WIPES the 64 B rather than only dropping the flag. Once
// `_team_ch_key_present` is false the accessors return nullptr, so the buffers are unobservable through the public
// API and any assert here would be tautological (load zeros, read zeros). crypto_wipe is used regardless — it is the
// right thing for destroying a secret — but the gate for it is code review, not a test.
TEST_CASE("§o3-key-lifetime — `create`/`join`/`leave` still PRESERVE the key (clear_routing_state does NOT clear it)") {
    // ★ THE BOUNDARY THAT MUST NOT MOVE. clear_team_routing_state() is shared by set_team_id AND clear_routing_state
    // (the reprovision verbs), so putting the clear THERE would have silently destroyed the key on every
    // `create`/`join`/`leave` — the exact opposite of firmware_config.cpp's blob_take_team_channel_key, which exists
    // to carry the pair across a reprovision precisely because it is UNRECOVERABLE.
    TkHal hal; hal._fill = 0x31; Node node(hal, 2, 0xBEEFu); NodeConfig cfg = team_cfg(0xAAAAAAAAu); node.on_init(cfg);
    CHECK(node.team_channel_key_mint());
    uint8_t pub[32]; std::memcpy(pub, node.team_channel_pub(), 32);

    node.clear_routing_state();                                  // `create` / `join` / `leave`
    CHECK(node.team_channel_key_present());                      // ★ SURVIVES
    CHECK(std::memcmp(node.team_channel_pub(), pub, 32) == 0);
    CHECK(node.config().team_id == 0xAAAAAAAAu);                 // and the reprovision did not touch team_id either
}


// ================= §team-ch-key (T-K3, spec 2026-07-26 §2.3) — the SEALED key-grant DM (TYPE 19) ===============
// ★ THIS SUITE IS THE ENTIRE DETECTOR, same structural reason as T-K1's above: no scenario reaches a console verb,
// and the simulator's `team` verb refuses `team new`, so no corpus stream can ever carry a TYPE-19 frame. Corpus
// byte-identity proves only that this slice stays INERT there. Correctness lives here.
//
// The suite deliberately splits into three layers, so a failure localises:
//   (1) team_key_grant_receive() called DIRECTLY on a hand-built body — the parse/validate/adopt logic, exhaustively;
//   (2) team_key_grant_send()'s pre-flight refusals — one case per operator-visible reason;
//   (3) the FULL WIRE ROUND TRIP — A seals a real type-19 DATA frame, drives it into B over RTS+DATA, and B ends up
//       holding A's key. Plus the two things only the wire can show: a PLAINTEXT type-19 is dropped, and a grant is
//       never delivered as a DM (no msg_recv, no inbox).
namespace {
constexpr uint32_t kPostAckTimerId = 9;   // Node::kPostAckTimerId (private) — the receiver's deliver-after-ACK fire
// A unicast RTS reserving `plen` bytes for the following DATA (mirrors test_node_r3.cpp's mk_rts).
static size_t mk_unicast_rts(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo, uint8_t plen,
                             std::array<uint8_t, 16>& b, uint8_t addr_len = 0, bool mobile_src = false) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = 0; in.payload_len = plen; in.m_payload_id_lo16 = 0;
    in.addr_len = addr_len; in.mobile_src = mobile_src;
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
// The RTS reserves the END-TO-END inner+MAC length (node_mac.cpp tx_rts), i.e. frame_len - DATA_HDR_LEN - 1 for an
// APP frame (the TYPE byte at offset 8 is header, not payload). Getting this wrong only mis-sizes the RX window, but
// stating it correctly is what makes these tests a faithful wire exercise.
static uint8_t rts_plen(size_t frame_len) { return static_cast<uint8_t>(frame_len - DATA_HDR_LEN - 1); }
// Build the T-K3 grant BODY exactly as Node::team_key_grant_send does: [team_id 4 LE][name_len][name][priv 32].
static uint8_t mk_grant_body(uint32_t team_id, const char* name, const uint8_t priv[32], uint8_t* out) {
    uint8_t n = 0;
    out[n++] = uint8_t(team_id); out[n++] = uint8_t(team_id >> 8);
    out[n++] = uint8_t(team_id >> 16); out[n++] = uint8_t(team_id >> 24);
    const uint8_t nl = name ? static_cast<uint8_t>(std::strlen(name)) : 0;
    out[n++] = nl;
    for (uint8_t i = 0; i < nl; ++i) out[n++] = static_cast<uint8_t>(name[i]);
    for (uint8_t i = 0; i < 32; ++i) out[n++] = priv[i];
    return n;
}
// A team keypair minted off a scripted RNG, without needing a Node to own it.
struct TkPair { uint8_t pub[32]; uint8_t priv[32]; };
static TkPair mk_pair(uint8_t fill) {
    TkHal h; h._fill = fill; Node tmp(h, 2, 0xBEEFu); NodeConfig c = team_cfg(); tmp.on_init(c);
    CHECK(tmp.team_channel_key_mint());
    TkPair p{}; std::memcpy(p.pub, tmp.team_channel_pub(), 32); std::memcpy(p.priv, tmp.team_channel_priv(), 32);
    return p;
}
static Node::TeamKeyGrantRx feed_grant(Node& n, uint32_t team_id, const char* name, const uint8_t priv[32]) {
    uint8_t body[80];
    const uint8_t bn = mk_grant_body(team_id, name, priv, body);
    return n.team_key_grant_receive(body, bn, /*granter_hash=*/0xDEADBEEFu, /*granter_node=*/9);
}
// Drain the WHOLE ring once, then query it — a "find one kind" helper that drains would consume the OTHER kinds a
// test also wants to assert about (this bit once: the msg_recv-absent check swallowed the team_key_received push).
static std::vector<Push> drain_all(Node& n) {
    std::vector<Push> v; Push p{};
    while (n.next_push(p)) v.push_back(p);
    return v;
}
static bool find_push(const std::vector<Push>& v, PushKind want, Push& out) {
    for (const auto& p : v) if (p.kind == want) { out = p; return true; }
    return false;
}
}  // namespace

// ---- (1) receive: the happy path + the app surface -----------------------------------------------------------
TEST_CASE("§T-K3 receive — a valid grant ADOPTS the key and pushes team_key_received{team,hash,origin,name}") {
    const TkPair src = mk_pair(0x31);
    TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig cfg = team_cfg(0xAAAAAAAAu); n.on_init(cfg);
    CHECK_FALSE(n.team_channel_key_present());                       // a joiner starts keyless (T-K1: `team <id>` mints nothing)

    CHECK(feed_grant(n, 0xAAAAAAAAu, "Alpha Team", src.priv) == Node::TeamKeyGrantRx::adopted);
    CHECK(n.team_channel_key_present());
    CHECK(std::memcmp(n.team_channel_priv(), src.priv, 32) == 0);     // the private half, verbatim
    CHECK(std::memcmp(n.team_channel_pub(),  src.pub,  32) == 0);     // ★ the public half RE-DERIVED — it is not on the wire
    CHECK(hal.rand_bytes_calls == 0);                                 // adopt-from-grant is DRAW-FREE (corpus-inertness property)
    CHECK(hal.count("team_key_grant_rx") == 1);
    CHECK(hal.count("team_key_grant_reject") == 0);

    Push pu{};
    CHECK(find_push(drain_all(n), PushKind::team_key_received, pu));
    CHECK(pu.team_id == 0xAAAAAAAAu);
    CHECK(pu.sender_hash == 0xDEADBEEFu);
    CHECK(pu.origin == 9);
    CHECK(pu.body_len == 10);
    CHECK(std::string(reinterpret_cast<const char*>(pu.body), pu.body_len) == "Alpha Team");
}

TEST_CASE("§T-K3 receive — the grant is IDEMPOTENT and a re-grant OVERWRITES (the v1 re-key story, spec §0/Q5)") {
    const TkPair one = mk_pair(0x31), two = mk_pair(0x77);
    TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig cfg = team_cfg(0xAAAAAAAAu); n.on_init(cfg);
    CHECK(feed_grant(n, 0xAAAAAAAAu, nullptr, one.priv) == Node::TeamKeyGrantRx::adopted);
    CHECK(feed_grant(n, 0xAAAAAAAAu, nullptr, one.priv) == Node::TeamKeyGrantRx::adopted);   // the SAME grant again
    CHECK(std::memcmp(n.team_channel_priv(), one.priv, 32) == 0);
    CHECK(feed_grant(n, 0xAAAAAAAAu, nullptr, two.priv) == Node::TeamKeyGrantRx::adopted);   // a RE-KEY
    CHECK(std::memcmp(n.team_channel_priv(), two.priv, 32) == 0);
    CHECK(std::memcmp(n.team_channel_pub(),  two.pub,  32) == 0);
    CHECK(hal.count("team_key_grant_rx") == 3);
}

TEST_CASE("§T-K3 receive — name absent / at the 32-B cap / OVER the cap") {
    const TkPair src = mk_pair(0x31);
    { TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      CHECK(feed_grant(n, 0xAAAAAAAAu, nullptr, src.priv) == Node::TeamKeyGrantRx::adopted);   // 37-B body, no name
      Push pu{}; CHECK(find_push(drain_all(n), PushKind::team_key_received, pu));
      CHECK(pu.body_len == 0); }                                     // -> the JSON omits "name" entirely
    { TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      const char* n32 = "12345678901234567890123456789012";          // exactly 32
      CHECK(std::strlen(n32) == 32);
      CHECK(feed_grant(n, 0xAAAAAAAAu, n32, src.priv) == Node::TeamKeyGrantRx::adopted);       // 69-B body = the max
      Push pu{}; CHECK(find_push(drain_all(n), PushKind::team_key_received, pu));
      CHECK(pu.body_len == 32); }
    { // name_len 33 -> refused, NOT truncated, and the key is not installed
      TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      uint8_t body[80]; uint8_t bn = mk_grant_body(0xAAAAAAAAu, "1234567890123456789012345678901234", src.priv, body);
      CHECK(bn == 4 + 1 + 34 + 32);
      CHECK(n.team_key_grant_receive(body, bn, 0x1u, 9) == Node::TeamKeyGrantRx::long_name);
      CHECK_FALSE(n.team_channel_key_present());
      CHECK(hal.count("team_key_grant_reject") == 1); }
}

TEST_CASE("§T-K3 receive — C2 refusals: short body, INEXACT length, wrong team, no team, degenerate key") {
    const TkPair src = mk_pair(0x31);
    uint8_t body[80];
    const uint8_t bn = mk_grant_body(0xAAAAAAAAu, "T", src.priv, body);
    CHECK(bn == 4 + 1 + 1 + 32);

    { TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      CHECK(n.team_key_grant_receive(body, 36, 0x1u, 9) == Node::TeamKeyGrantRx::bad_len);      // below the 37-B floor
      CHECK(n.team_key_grant_receive(nullptr, bn, 0x1u, 9) == Node::TeamKeyGrantRx::bad_len);   // null body
      CHECK_FALSE(n.team_channel_key_present()); }
    { // ★ EXACT length, not "at least": one extra byte means the 32 we would slice as tkpriv may not be the 32 sent.
      TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      body[bn] = 0x99;
      CHECK(n.team_key_grant_receive(body, static_cast<uint8_t>(bn + 1), 0x1u, 9) == Node::TeamKeyGrantRx::bad_len);
      CHECK(n.team_key_grant_receive(body, static_cast<uint8_t>(bn - 1), 0x1u, 9) == Node::TeamKeyGrantRx::bad_len);
      CHECK_FALSE(n.team_channel_key_present()); }
    { // spec §2.3 Q2: team_id is carried DEFENSIVELY and a mismatch is refused loud
      TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xBBBBBBBBu); n.on_init(c);
      CHECK(n.team_key_grant_receive(body, bn, 0x1u, 9) == Node::TeamKeyGrantRx::team_mismatch);
      CHECK_FALSE(n.team_channel_key_present());
      CHECK(hal.count("team_key_grant_reject") == 1); }
    { TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0u); n.on_init(c);            // not in a team at all
      CHECK(n.team_key_grant_receive(body, bn, 0x1u, 9) == Node::TeamKeyGrantRx::no_team);
      CHECK_FALSE(n.team_channel_key_present()); }
    { TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      uint8_t zero[32] = {};
      uint8_t zb[80]; const uint8_t zn = mk_grant_body(0xAAAAAAAAu, "T", zero, zb);
      CHECK(n.team_key_grant_receive(zb, zn, 0x1u, 9) == Node::TeamKeyGrantRx::bad_key);        // all-zero scalar -> derive refuses
      CHECK_FALSE(n.team_channel_key_present()); }
    { // a REFUSED grant must not disturb a key we ALREADY hold
      TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig c = team_cfg(0xAAAAAAAAu); n.on_init(c);
      CHECK(feed_grant(n, 0xAAAAAAAAu, "ok", src.priv) == Node::TeamKeyGrantRx::adopted);
      uint8_t bad[80]; const uint8_t badn = mk_grant_body(0x12345678u, "x", src.priv, bad);     // wrong team
      CHECK(n.team_key_grant_receive(bad, badn, 0x1u, 9) == Node::TeamKeyGrantRx::team_mismatch);
      CHECK(n.team_channel_key_present());
      CHECK(std::memcmp(n.team_channel_priv(), src.priv, 32) == 0); }
}

TEST_CASE("§T-K3 receive — an UNCLAMPED private half on the wire is canonicalised, and its pub still matches") {
    // The grant may originate from a store-unclamped implementation (T-K1's identity.h clamping contract). Adopting
    // from the private half alone must normalise it AND yield the pub that scalar really owns.
    TkHal hal; Node n(hal, 3, 0xCAFEu); NodeConfig cfg = team_cfg(0xAAAAAAAAu); n.on_init(cfg);
    uint8_t raw[32]; for (int i = 0; i < 32; ++i) raw[i] = static_cast<uint8_t>(0xA0 + i);
    raw[0] |= 0x07; raw[31] |= 0x80; raw[31] &= 0xBF;                 // deliberately violate all three clamp bits
    CHECK(feed_grant(n, 0xAAAAAAAAu, nullptr, raw) == Node::TeamKeyGrantRx::adopted);
    const uint8_t* p = n.team_channel_priv();
    CHECK((p[0] & 7) == 0); CHECK((p[31] & 0x80) == 0); CHECK((p[31] & 0x40) != 0);
    uint8_t want[32]; crypto_x25519_public_key(want, p);
    CHECK(std::memcmp(n.team_channel_pub(), want, 32) == 0);
}

// ---- (2) send: one case per operator-visible refusal ---------------------------------------------------------
namespace {
// A node ready to grant: in a team, holds a team key, has a crypto identity, and knows `peer`'s pubkey.
static void arm_granter(Node& n, TkHal& hal, const Identity& self, const Identity& peer, uint32_t team = 0xAAAAAAAAu) {
    NodeConfig cfg = team_cfg(team); n.on_init(cfg);
    n.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._fill = 0x31; CHECK(n.team_channel_key_mint());
    n.peer_key_set(peer.key_hash32, peer.ed_pub, Node::PeerKeyConf::authoritative);
}
}  // namespace

TEST_CASE("§T-K3 send — every pre-flight refusal is DISTINCT, and none of them airs a frame") {
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity A{}, B{}; identity_from_seed(A, sa); identity_from_seed(B, sb);

    { // no_team — team_id 0
      TkHal hal; Node n(hal, 2, A.key_hash32); NodeConfig c = team_cfg(0u); n.on_init(c);
      n.set_crypto_identity(A.x_secret, A.ed_pub); hal._fill = 0x31; CHECK(n.team_channel_key_mint());
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::no_team); }
    { // no_key — in a team but keyless (exactly a joiner's state)
      TkHal hal; Node n(hal, 2, A.key_hash32); NodeConfig c = team_cfg(); n.on_init(c);
      n.set_crypto_identity(A.x_secret, A.ed_pub);
      n.peer_key_set(B.key_hash32, B.ed_pub, Node::PeerKeyConf::authoritative);
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::no_key); }
    { // no_identity — a key to grant, but nothing to seal WITH
      TkHal hal; Node n(hal, 2, A.key_hash32); NodeConfig c = team_cfg(); n.on_init(c);
      hal._fill = 0x31; CHECK(n.team_channel_key_mint());
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::no_identity); }
    { // no_pubkey — nothing cached, and an OVERHEARD (sub-authoritative) key is NOT good enough for a private key
      TkHal hal; Node n(hal, 2, A.key_hash32); NodeConfig c = team_cfg(); n.on_init(c);
      n.set_crypto_identity(A.x_secret, A.ed_pub); hal._fill = 0x31; CHECK(n.team_channel_key_mint());
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::no_pubkey);
      n.peer_key_set(B.key_hash32, B.ed_pub, Node::PeerKeyConf::overheard);
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::no_pubkey);
      n.peer_key_set(B.key_hash32, B.ed_pub, Node::PeerKeyConf::pinned);                 // pinned (QR) IS the bar
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) != Node::TeamKeyGrantTx::no_pubkey); }
    { // self / hash 0
      TkHal hal; Node n(hal, 2, A.key_hash32); arm_granter(n, hal, A, B);
      CHECK(n.team_key_grant_send(A.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::self);
      CHECK(n.team_key_grant_send(0, nullptr, 0) == Node::TeamKeyGrantTx::self); }
    { // too_large — a 33-char name (the console caps at 32, so this is defence in depth)
      TkHal hal; Node n(hal, 2, A.key_hash32); arm_granter(n, hal, A, B);
      CHECK(n.team_key_grant_send(B.key_hash32, "123456789012345678901234567890123", 33) == Node::TeamKeyGrantTx::too_large); }
    { // ★ NOT A REFUSAL: a null name with a non-zero length is normalised to "no name", not a crash or a read of null
      TkHal hal; Node n(hal, 2, A.key_hash32); arm_granter(n, hal, A, B);
      CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 200) == Node::TeamKeyGrantTx::queued); }
}

TEST_CASE("§o3-key-lifetime — after a switch, `exportkey`/`grantkey` take the existing NO-KEY paths") {
    // The brief's third check: a switched member must not export zeros. `team exportkey`'s guard is `!pub || !priv`
    // (firmware_config.cpp), which the nullptr accessors satisfy BY CONSTRUCTION. The send half IS core-reachable,
    // so pin the real refusal enum here rather than arguing it.
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity A{}, B{}; identity_from_seed(A, sa); identity_from_seed(B, sb);
    TkHal hal; Node n(hal, 2, A.key_hash32); arm_granter(n, hal, A, B);          // in a team, keyed, identity, B pinned
    CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) != Node::TeamKeyGrantTx::no_key);   // control: it CAN grant

    CHECK(n.set_team_id(0xCCCCCCCCu));                                            // ★ the switch
    CHECK(n.team_key_grant_send(B.key_hash32, nullptr, 0) == Node::TeamKeyGrantTx::no_key);
    CHECK(n.team_channel_pub() == nullptr);                                       // ⇒ exportkey's `!pub` refusal — no zero-key export
}

TEST_CASE("§T-K3 send — the sealed grant is ACTUALLY sealed: CRYPTED + TYPE 19, and tkpriv is nowhere in the frame") {
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity A{}, B{}; identity_from_seed(A, sa); identity_from_seed(B, sb);
    TkHal hal; Node n(hal, 2, A.key_hash32); arm_granter(n, hal, A, B);
    CHECK(n.test_id_bind_set(7, B.key_hash32, /*authoritative=*/true));   // resolved -> flies now
    n.test_suspend_tx_drain(true);                                   // keep the frame IN the queue for the wire-golden read

    uint16_t ctr = 0;
    CHECK(n.team_key_grant_send(B.key_hash32, "Bravo", 5, Plane::AUTO, &ctr) == Node::TeamKeyGrantTx::queued);
    CHECK(ctr != 0);                                                 // airborne, not parked
    CHECK(hal.count("team_key_grant_tx") == 1);
    CHECK(hal.count("team_key_grant_refused") == 0);
    CHECK(n.test_tx_queue_n() == 1);
    uint8_t ilen = 0; const uint8_t* inner = n.test_tx_inner(0, ilen);
    CHECK((n.test_tx_flags(0) & DATA_FLAG_CRYPTED) != 0);             // ★ requirement 1: sealed
    CHECK((n.test_tx_flags(0) & DATA_FLAG_DST_HASH) != 0);            // CRYPTED mandates it (the nonce/aad source)
    CHECK(n.test_tx_type(0) == DATA_TYPE_TEAM_KEY_GRANT);             // ★ and the TYPE survived
    bool leaked = false;                                              // the private half must not be readable anywhere
    for (int i = 0; i + 32 <= ilen; ++i) if (std::memcmp(inner + i, n.team_channel_priv(), 32) == 0) leaked = true;
    CHECK_FALSE(leaked);
}

// ---- (3) the FULL WIRE ROUND TRIP + the two wire-only properties ---------------------------------------------
namespace {
// Seal `body` as a TYPE-19 DM A->B (origin 1, ctr 5) and pack the DATA frame. Mirrors test_node_r3's e2e_seal_AtoB.
static size_t seal_grant_frame(Node& A, const Identity& idA, const Identity& idB, uint8_t next, uint8_t dst,
                               uint8_t addr_len, const uint8_t* body, uint8_t blen,
                               uint8_t* frame, size_t cap, uint8_t* inner, size_t inner_cap, uint8_t seed[8]) {
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t il = A.e2e_seal_inner(inner, inner_cap, seed, flags, idB.key_hash32, /*origin=*/1, /*ctr=*/0x0005,
                                       /*source_hash=*/idA.key_hash32, 0, 0, body, blen, oc);
    if (il == 0) return 0;
    data_in din{}; din.addr_len = addr_len; din.flags = flags; din.type = DATA_TYPE_TEAM_KEY_GRANT;
    din.next = next; din.dst = dst; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(seed, 8);
    return pack_data(din, std::span<uint8_t>(frame, cap));
}
}  // namespace

TEST_CASE("§T-K3 WIRE — the acceptance story: A seals a TYPE-19 grant, B receives it and can now read the team") {
    // Two STATIC team members on one leaf — the plainest of the three v1 transports (same-layer CRYPTED). The team
    // plane variant is the next test; together they cover both `for_*_rts`/`for_me_dst` acceptance shapes.
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sa); identity_from_seed(idB, sb);
    TkHal halA; Node A(halA, 1, idA.key_hash32); arm_granter(A, halA, idA, idB);
    uint8_t body[80];
    const uint8_t bn = mk_grant_body(0xAAAAAAAAu, "Alpha", A.team_channel_priv(), body);
    uint8_t frame[160], inner[128], seed[8];
    const size_t fl = seal_grant_frame(A, idA, idB, /*next=*/2, /*dst=*/2, /*addr_len=*/0, body, bn,
                                       frame, sizeof frame, inner, sizeof inner, seed);
    CHECK(fl > 0);

    // B: same team, STATIC, KEYLESS (the newjoiner state), holds A's key so it can open the seal.
    TkHal halB; Node B(halB, 2, idB.key_hash32);
    NodeConfig cb = basic_cfg(); cb.team_id = 0xAAAAAAAAu; B.on_init(cb);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    CHECK_FALSE(B.team_channel_key_present());                        // ★ BEFORE: cannot read the team's content

    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_unicast_rts(1, /*next=*/2, /*dst=*/2, 5, rts_plen(fl), rb, 0, false), from1);
    CHECK(halB.count("cts_tx") == 1);                                 // the exchange really happened (not a silent overhear)
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);

    CHECK(B.team_channel_key_present());                              // ★ AFTER: adopted off the wire
    if (B.team_channel_key_present()) {
        CHECK(std::memcmp(B.team_channel_priv(), A.team_channel_priv(), 32) == 0);
        CHECK(std::memcmp(B.team_channel_pub(),  A.team_channel_pub(),  32) == 0);
    }
    CHECK(halB.count("team_key_grant_rx") == 1);
    // ★ CONSUMED, not delivered: no msg_recv push, and the grant body never becomes inbox text.
    const std::vector<Push> pushes = drain_all(B);
    Push pu{}; CHECK_FALSE(find_push(pushes, PushKind::msg_recv, pu));
    CHECK(halB.count("delivered") == 0);
    Push kp{}; CHECK(find_push(pushes, PushKind::team_key_received, kp));
    CHECK(kp.sender_hash == idA.key_hash32);                          // the SEALED-sender identity, not a cleartext claim
    CHECK(std::string(reinterpret_cast<const char*>(kp.body), kp.body_len) == "Alpha");
}

TEST_CASE("§T-K3 WIRE — the TEAM-PLANE grant: addressed to the joiner's team_local_id, leaf-agnostic") {
    // The transport the spec's acceptance story actually needs: an OFF-GRID team member grants to another off-grid
    // member over the team plane (addr_len=1, next/dst = the team_local_id). It is NOT a CROSS_LAYER frame even when
    // the two sit on different leaf nibbles (team frames are leaf-exempt), which is exactly why e2e_seal_inner's
    // same-layer-only rule does not bite and why this transport works while the delegated/XL ones are refused.
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 11); sb[i] = uint8_t(60 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sa); identity_from_seed(idB, sb);
    TkHal halA; Node A(halA, 40, idA.key_hash32); arm_granter(A, halA, idA, idB);
    A.set_team_local_id(40);
    uint8_t body[80];
    const uint8_t bn = mk_grant_body(0xAAAAAAAAu, nullptr, A.team_channel_priv(), body);
    uint8_t frame[160], inner[128], seed[8];
    const size_t fl = seal_grant_frame(A, idA, idB, /*next=*/50, /*dst=*/50, /*addr_len=*/1, body, bn,
                                       frame, sizeof frame, inner, sizeof inner, seed);
    CHECK(fl > 0);

    TkHal halB; Node B(halB, 50, idB.key_hash32);
    NodeConfig cb = team_cfg(0xAAAAAAAAu); cb.leaf_id = 0; B.on_init(cb);   // is_mobile -> the team/mobile plane
    B.set_team_local_id(50);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    CHECK_FALSE(B.team_channel_key_present());

    RxMeta from40{ 12.0f, -70.0f, 0, static_cast<int8_t>(40) };
    std::array<uint8_t, 16> rb{};
    halB._now = 1000;
    B.on_recv(rb.data(), mk_unicast_rts(40, /*next=*/50, /*dst=*/50, 5, rts_plen(fl), rb, /*addr_len=*/1, /*mobile_src=*/true), from40);
    CHECK(halB.count("cts_tx") == 1);
    halB._now = 2000; B.on_recv(frame, fl, from40);
    B.on_timer(kPostAckTimerId);

    CHECK(B.team_channel_key_present());                              // ★ the team-plane grant lands
    if (B.team_channel_key_present())
        CHECK(std::memcmp(B.team_channel_priv(), A.team_channel_priv(), 32) == 0);
    Push kp{}; CHECK(find_push(drain_all(B), PushKind::team_key_received, kp));
    CHECK(kp.body_len == 0);                                          // no name given -> the JSON omits it
    CHECK(halB.count("delivered") == 0);
}

TEST_CASE("§T-K3 WIRE — a PLAINTEXT TYPE-19 is DROPPED, never adopted and never delivered as a DM") {
    // Requirement 1's receive half: a grant that arrived unsealed is a bug or an attack. It must not install a key
    // (an attacker would then own the team's content plane) and must not fall through to the inbox (37 raw key bytes).
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sa); identity_from_seed(idB, sb);
    const TkPair evil = mk_pair(0x55);
    uint8_t body[80]; const uint8_t bn = mk_grant_body(0xAAAAAAAAu, "Evil", evil.priv, body);

    uint8_t inner[128];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_SOURCE_HASH);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, /*dst_hash*/ 0,
                                         nullptr, 0, 0, /*origin*/ 1, /*source_hash*/ idA.key_hash32,
                                         body, bn, 0, 0);
    CHECK(il > 0);
    const uint8_t mac4[4] = { 0, 0, 0, 0 };
    uint8_t frame[160];
    data_in din{}; din.addr_len = 0; din.flags = flags; din.type = DATA_TYPE_TEAM_KEY_GRANT;
    din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(mac4, 4);
    const size_t fl = pack_data(din, std::span<uint8_t>(frame, sizeof frame));
    CHECK(fl > 0);

    TkHal halB; Node B(halB, 2, idB.key_hash32);
    NodeConfig cb = basic_cfg(); cb.team_id = 0xAAAAAAAAu; B.on_init(cb);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_unicast_rts(1, 2, 2, 5, rts_plen(fl), rb, 0, false), from1);
    CHECK(halB.count("cts_tx") == 1);                                 // the frame WAS accepted for us — the drop is the type rule, not addressing
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);

    CHECK_FALSE(B.team_channel_key_present());                        // ★ NOT adopted
    CHECK(halB.count("team_key_grant_reject") == 1);                  // dropped LOUD
    CHECK(halB.count("team_key_grant_rx") == 0);
    CHECK(halB.count("delivered") == 0);                              // ★ and NOT delivered as a DM
    const std::vector<Push> pushes = drain_all(B);
    Push pu{}; CHECK_FALSE(find_push(pushes, PushKind::msg_recv, pu));
    Push kp{}; CHECK_FALSE(find_push(pushes, PushKind::team_key_received, kp));
}

// ---- the three STRUCTURAL send-side guards -------------------------------------------------------------------
TEST_CASE("§T-K3 guard — enqueue_data REFUSES a non-CRYPTED TYPE-19 (no cleartext fallback, ever)") {
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sa); identity_from_seed(idB, sb);
    TkHal hal; Node n(hal, 2, idA.key_hash32); arm_granter(n, hal, idA, idB);
    n.test_suspend_tx_drain(true);
    uint8_t body[40] = { 1, 2, 3, 4, 0 };
    // The guard is not reachable through team_key_grant_send (it forces CryptIntent::on), so drive do_send directly —
    // which is exactly the future caller the guard exists for.
    CHECK(n.test_do_send_typed(/*dst=*/7, body, sizeof body, CryptIntent::off, /*override_dst_hash=*/0,
                               /*type=*/DATA_TYPE_TEAM_KEY_GRANT) == 0);
    CHECK(n.test_tx_queue_n() == 0);                                  // nothing was enqueued
    const Ev* e = hal.last("team_key_grant_refused");
    CHECK(e != nullptr); if (e) CHECK(e->reason == "plaintext");
    Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::unsealable);
    // ...and the SAME send with CryptIntent::on is admitted, so the guard discriminates rather than blanket-refusing.
    CHECK(n.test_id_bind_set(7, idB.key_hash32, /*authoritative=*/true));
    CHECK(n.test_do_send_typed(7, body, sizeof body, CryptIntent::on, /*override_dst_hash=*/idB.key_hash32,
                               DATA_TYPE_TEAM_KEY_GRANT) != 0);
    CHECK(n.test_tx_queue_n() == 1);
}

TEST_CASE("§T-K3 guard — enqueue_cross_layer REFUSES a TYPE-19 outright (an XL grant could only be CLEARTEXT)") {
    // e2e_seal_inner refuses DATA_FLAG_CROSS_LAYER (v1 same-layer only) and this path hard-sets it, so a type-19
    // arriving there could only ride in the clear. The sealed XL substitute (SEALED_RELAY) occupies the TYPE byte.
    uint8_t sa[32], sb[32]; for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sa); identity_from_seed(idB, sb);
    TkHal hal; Node n(hal, 2, idA.key_hash32); arm_granter(n, hal, idA, idB);
    n.test_suspend_tx_drain(true);
    uint8_t body[40] = { 1, 2, 3, 4, 0 };
    const uint8_t hops[2] = { 5, 6 };
    uint16_t ctr = 0;
    CHECK_FALSE(n.test_enqueue_cross_layer_typed(/*gw_node=*/3, idB.key_hash32, hops, 2, body, sizeof body,
                                                 &ctr, DATA_TYPE_TEAM_KEY_GRANT));
    CHECK(n.test_tx_queue_n() == 0);
    const Ev* e = hal.last("team_key_grant_refused");
    CHECK(e != nullptr); if (e) CHECK(e->reason == "cross_layer");
    Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::unsealable);
    // Control: the identical XL send with type 0 IS built, so the refusal is type-specific, not a broken path.
    CHECK(n.test_enqueue_cross_layer_typed(3, idB.key_hash32, hops, 2, body, sizeof body, &ctr, /*type=*/0));
    CHECK(n.test_tx_queue_n() == 1);
}

// ---- §chan-crypt CL1 — the `-e` REFUSAL MATRIX (spec 2026-07-30 §2.2) ----------------------------------------
// Four cases, and the two REFUSALS are the point of the slice. The decisions live in on_command (node.cpp) rather than
// the console parser because `CmdKind::send_channel` has THREE producers — console_parse, `testch`'s hand-built Command
// in src/fw_main.cpp, and the simulator's NodeRuntimeWrapper — and on_command is the only seam all three pass. These
// tests therefore drive the shared seam, which is also why they survive CL2 unchanged.
//
// ⚠ WHY EACH REFUSAL ALSO ASSERTS "NOTHING AIRED, NOTHING BUFFERED": a channel post is not a DM — do_send_channel
// BUFFERS it dirty and floods immediately, so a half-honoured refusal would leave the body advertised in the next
// beacon digest and pullable by every neighbour. `err_unsupported` alone would not prove the content stayed home.

TEST_CASE("§chan-crypt — `-e` WITHOUT `-t` is REFUSED (a global channel has no content key); nothing airs") {
    TestHal hal; Node node(hal, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);
    const CmdResult r = send_channel(node, /*ch=*/7, "secret", /*team=*/false, /*global=*/false, CryptIntent::on);
    CHECK(r.code == CmdCode::err_unsupported);
    CHECK(r.ctr == 0);
    CHECK(node.channel_buffer_count() == 0);          // ★ not buffered => never advertised in a BCN digest
    CHECK(hal.tx_frames.empty());                     // ★ no RTS-M, no DATA-M: nothing reached the air at all
    const Ev* e = hal.last("channel_crypt_refused");
    CHECK(e != nullptr); if (e) { CHECK(e->reason == "no_team"); CHECK(e->channel_id == 7); }
    Push pu{}; CHECK(find_push(drain_all(node), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::unsealable);    // U1: the existing enumerator, no new one appended
    // ★ SAME-SITE CONTROL: the IDENTICAL post without `-e` succeeds and airs. The gate discriminates on the crypt
    // intent — it is not a broken/blanket-refusing path.
    TestHal h2; Node n2(h2, 3, 0x1234ABCDu); NodeConfig c2 = basic_cfg(); n2.on_init(c2);
    CHECK(send_channel(n2, 7, "secret").code == CmdCode::queued);
    CHECK(n2.channel_buffer_count() == 1);
    CHECK_FALSE(h2.tx_frames.empty());
    CHECK(h2.count("channel_crypt_refused") == 0);
    // `-g -e` (explicit GLOBAL + seal) takes the same arm: still no team, still no key.
    TestHal h3; Node n3(h3, 3, 0x1234ABCDu); NodeConfig c3 = basic_cfg(); n3.on_init(c3);
    CHECK(send_channel(n3, 7, "secret", /*team=*/false, /*global=*/true, CryptIntent::on).code == CmdCode::err_unsupported);
    CHECK(n3.channel_buffer_count() == 0);
    const Ev* e3 = h3.last("channel_crypt_refused");
    CHECK(e3 != nullptr); if (e3) CHECK(e3->reason == "no_team");
}

TEST_CASE("§chan-crypt — `-t -g -e` is REFUSED because the GLOBAL copy would air the same text in the CLEAR") {
    // The trap the spec calls out: `-t -g` is BOTH planes, so honouring `-e` would seal one copy and broadcast a
    // byte-identical cleartext twin — an eavesdropper reads the twin and the seal buys nothing. Self-cancelling ⇒ refuse.
    TestHal hal; Node node(hal, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xABCD1234u; node.on_init(cfg);
    const CmdResult r = send_channel(node, /*ch=*/7, "both-planes", /*team=*/true, /*global=*/true, CryptIntent::on);
    CHECK(r.code == CmdCode::err_unsupported);
    CHECK(node.channel_buffer_count() == 0);          // ★ NEITHER copy was minted — not the team one either
    CHECK(hal.tx_frames.empty());
    const Ev* e = hal.last("channel_crypt_refused");
    CHECK(e != nullptr); if (e) CHECK(e->reason == "global_clear_copy");
    Push pu{}; CHECK(find_push(drain_all(node), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::unsealable);
    // ★ SAME-SITE CONTROL: `-t -g` WITHOUT `-e` still posts on both planes (unchanged §S7 T-B behaviour). The refusal
    // is caused by the crypt intent, not by the BOTH plane select.
    TestHal h2; Node n2(h2, 3, 0x1234ABCDu);
    NodeConfig c2 = basic_cfg(); c2.is_mobile = true; c2.team_id = 0xABCD1234u; n2.on_init(c2);
    CHECK(send_channel(n2, 7, "both-planes", /*team=*/true, /*global=*/true).code == CmdCode::queued);
    CHECK(n2.channel_buffer_count() >= 1);
    CHECK(h2.count("channel_crypt_refused") == 0);
}

TEST_CASE("§chan-crypt CL2a — `-t -e` on a member holding NO team content key refuses `no_key`/unsealable; `-t` alone unaffected") {
    // The CL1 `not_implemented` stub is GONE; this is the arm that replaced it. Membership is NOT readership, so a
    // team member with no CONTENT key cannot honour `-e` — and the remedy is a GRANT, not re-joining the team.
    TestHal hal; Node node(hal, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xABCD1234u; node.on_init(cfg);
    CHECK_FALSE(node.team_channel_key_present());
    const CmdResult r = send_channel(node, /*ch=*/7, "team-secret", /*team=*/true, /*global=*/false, CryptIntent::on);
    CHECK(r.code == CmdCode::err_unsupported);
    CHECK(node.channel_buffer_count() == 0);          // ★ NOT posted in clear as a fallback (C2: never downgrade)
    CHECK(hal.tx_frames.empty());
    const Ev* e = hal.last("channel_crypt_refused");
    CHECK(e != nullptr); if (e) CHECK(e->reason == "no_key");
    Push pu{}; CHECK(find_push(drain_all(node), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::unsealable);
    // ★ SAME-SITE CONTROL: `-t` WITHOUT `-e` still floods the team plane in clear (unchanged). A keyless member must
    // keep being able to post, and plaintext is always openable — that row of the matrix is deliberately untouched.
    TestHal h2; Node n2(h2, 3, 0x1234ABCDu);
    NodeConfig c2 = basic_cfg(); c2.is_mobile = true; c2.team_id = 0xABCD1234u; n2.on_init(c2);
    CHECK(send_channel(n2, 7, "team-secret", /*team=*/true).code == CmdCode::queued);
    CHECK(n2.channel_buffer_count() == 1);
    CHECK_FALSE(h2.tx_frames.empty());
    CHECK(h2.count("channel_crypt_refused") == 0);
}

TEST_CASE("§chan-crypt — ORDER: the team-MEMBERSHIP refusal still wins over the crypt matrix on a non-team node") {
    // `-t …` on a static node has no team plane at all, so err_no_binding is the ROOT cause and must be reported
    // ahead of any crypt reason — otherwise the operator chases sealing when what he lacks is a team. (This is also
    // the MR_FEAT_TEAM 0 board shape, where team_member is a compile-time false.)
    TestHal hal; Node node(hal, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); node.on_init(cfg);                  // static: is_mobile=false, team_id=0
    CHECK(send_channel(node, 7, "x", /*team=*/true, /*global=*/false, CryptIntent::on).code == CmdCode::err_no_binding);
    CHECK(send_channel(node, 7, "x", /*team=*/true, /*global=*/true,  CryptIntent::on).code == CmdCode::err_no_binding);
    CHECK(hal.count("channel_crypt_refused") == 0);                   // ★ the crypt matrix was never consulted
    CHECK(node.channel_buffer_count() == 0);
    CHECK(hal.tx_frames.empty());
    // And the earlier guards still precede BOTH: an oversize `-e` post is err_too_large, not a crypt refusal.
    TestHal h2; Node n2(h2, 3, 0x1234ABCDu); NodeConfig c2 = basic_cfg(); n2.on_init(c2);
    std::string big(protocol::channel_msg_max_payload_bytes + 1, 'x');
    CHECK(send_channel(n2, 7, big.c_str(), /*team=*/false, /*global=*/false, CryptIntent::on).code == CmdCode::err_too_large);
    CHECK(h2.count("channel_crypt_refused") == 0);
}

TEST_CASE("§chan-crypt — CryptIntent::off behaves exactly like `def` (there is nothing to opt out of yet)") {
    // `off` would mean "air this one in the clear", which is a channel post's only behaviour today. The opt-out
    // arrives with CL2's team_channel_crypt (O2: a CONFIG toggle, not a per-send flag), so `off` must NOT be
    // mistaken for a third case here — only `on` may trigger the matrix.
    TestHal hal; Node node(hal, 3, 0x1234ABCDu);
    NodeConfig cfg = basic_cfg(); cfg.is_mobile = true; cfg.team_id = 0xABCD1234u; node.on_init(cfg);
    CHECK(send_channel(node, 7, "clear", /*team=*/true, /*global=*/false, CryptIntent::off).code == CmdCode::queued);
    CHECK(node.channel_buffer_count() == 1);
    CHECK(hal.count("channel_crypt_refused") == 0);
}

// ============================ §chan-crypt CL2a — the SEALED team channel post ============================
// The corpus is STRUCTURALLY BLIND to this whole slice: no simulator node can hold a team content key (every
// establish path — mint / adopt / adopt_priv / NV load / the sealed TYPE-19 grant — is reachable only from `src/`,
// which the sim does not build), so `team_channel_key_present()` is false for all 36 scenarios and nothing seals.
// ⇒ these tests are the ONLY coverage of the crypto. They are written accordingly: KATs and negative cases, not
// round-trips (a round-trip passes with both halves wrong in the same way — test_dm_crypto.cpp's own lesson).
namespace {

// A deterministic, non-degenerate 32-byte scalar -> the SAME content key on every node given the same `fill`.
static void give_team_key(Node& n, uint8_t fill) {
    uint8_t priv[32]; for (int i = 0; i < 32; ++i) priv[i] = static_cast<uint8_t>(fill + i);
    CHECK(n.team_channel_key_adopt_priv(priv));
}
// The canonical (RFC-7748-clamped) form of that scalar, plus its public half — what the node actually stored.
static void ref_team_pair(uint8_t fill, uint8_t pub[32], uint8_t canon_priv[32]) {
    uint8_t priv[32]; for (int i = 0; i < 32; ++i) priv[i] = static_cast<uint8_t>(fill + i);
    CHECK(meshroute::team_channel_key_derive(pub, canon_priv, priv));
}
// ★ THE REFERENCE DERIVATIONS — recomputed here from the SPEC WORDING, not by calling the node's helpers. If the
// domain string, the truncation, the nonce input ORDER or the AAD layout ever drift, these stop agreeing.
static void ref_content_key(const uint8_t canon_priv[32], uint8_t key[32]) {
    const char dom[] = "MR-TEAM-CH-v1";
    uint8_t msg[13 + 32];
    std::memcpy(msg, dom, 13); std::memcpy(msg + 13, canon_priv, 32);
    uint8_t full[64]; crypto_blake2b(full, 64, msg, sizeof msg);
    std::memcpy(key, full, 32);
}
static void ref_nonce(const uint8_t seed8[8], uint16_t ctr, uint32_t x32, uint8_t nonce[24]) {
    uint8_t msg[8 + 2 + 4];
    std::memcpy(msg, seed8, 8);
    msg[8] = uint8_t(ctr); msg[9] = uint8_t(ctr >> 8);
    msg[10] = uint8_t(x32); msg[11] = uint8_t(x32 >> 8); msg[12] = uint8_t(x32 >> 16); msg[13] = uint8_t(x32 >> 24);
    uint8_t full[64]; crypto_blake2b(full, 64, msg, sizeof msg);
    std::memcpy(nonce, full, 24);
}
static void ref_aad(uint32_t tk_hash, uint32_t msg_id, uint8_t channel_id, uint8_t aad[9]) {
    aad[0] = uint8_t(tk_hash); aad[1] = uint8_t(tk_hash >> 8); aad[2] = uint8_t(tk_hash >> 16); aad[3] = uint8_t(tk_hash >> 24);
    aad[4] = uint8_t(msg_id);  aad[5] = uint8_t(msg_id >> 8);  aad[6] = uint8_t(msg_id >> 16);  aad[7] = uint8_t(msg_id >> 24);
    aad[8] = channel_id;
}
// Post `-t` and hand back the M frame that actually aired (flavor + the on-wire body).
struct AiredM { bool ok = false; uint8_t flavor = 0; uint32_t id = 0; std::vector<uint8_t> body; };
static AiredM post_team_and_capture(TestHal& hal, Node& n, uint8_t ch, const char* text,
                                    CryptIntent crypt = CryptIntent::def, bool loc = false) {
    AiredM out{};
    if (send_channel(n, ch, text, /*team=*/true, /*global=*/false, crypt, loc).code != CmdCode::queued) return out;
    drain_originate_flood(n);
    const std::vector<uint8_t>* mf = hal.last_tx_cmd(0xA);
    if (!mf) return out;
    auto pm = parse_m(std::span<const uint8_t>(mf->data(), mf->size()));
    if (!pm) return out;
    out.ok = true; out.flavor = pm->flavor; out.id = pm->channel_msg_id;
    out.body.assign(pm->body.begin(), pm->body.end());
    return out;
}

}  // namespace

TEST_CASE("§chan-crypt CL2a — KAT: the sealed body is [seal_ctr 2][seed8 8][ct‖tag] and opens under an INDEPENDENTLY recomputed key/nonce/AAD") {
    constexpr uint32_t T = 0xABCD1234u;
    TkHal hal; hal._fill = 0x41;                       // deterministic, non-degenerate nonce seed
    Node n(hal, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig c = team_cfg(T); n.on_init(c);
    give_team_key(n, /*fill=*/0x10);
    const AiredM m = post_team_and_capture(hal, n, /*ch=*/7, "team-secret", CryptIntent::on);
    CHECK(m.ok);
    if (!m.ok) return;
    // (1) the flavor carries BOTH bits, and crypted is never set alone.
    CHECK((m.flavor & protocol::channel_flavor_crypted) != 0);
    CHECK((m.flavor & protocol::channel_flavor_team) != 0);
    // (2) the body layout + its exact length. ★ §chan-crypt CL2b: the SEALED PLAINTEXT is the INNER
    //     `[flags u8][loc 6?][text]`, so a text-only post costs ONE byte more than the bare text it used to seal.
    const size_t txt_len = std::strlen("team-secret");
    const size_t ct_len  = 1 + txt_len;                       // [flags 0x01][text] — no position on this post
    CHECK(m.body.size() == ct_len + protocol::channel_seal_overhead_bytes);
    CHECK(protocol::channel_seal_overhead_bytes == 26);
    const uint16_t seal_ctr = static_cast<uint16_t>(m.body[0] | (m.body[1] << 8));
    CHECK(seal_ctr == 1);                                     // ++_channel_seal_ctr, pre-incremented, first seal
    for (int i = 0; i < 8; ++i) CHECK(m.body[2 + i] == static_cast<uint8_t>(0x41 + i));   // seed8 CARRIED verbatim
    // ★ the ciphertext is NOT the plaintext (the one assertion that would catch a no-op "seal")
    CHECK(std::memcmp(m.body.data() + 11, "team-secret", txt_len) != 0);
    // (3) INDEPENDENT open: recompute the key, the nonce and the AAD from the spec and unlock with monocypher.
    uint8_t pub[32], canon[32]; ref_team_pair(0x10, pub, canon);
    uint8_t key[32]; ref_content_key(canon, key);
    uint8_t nonce[24]; ref_nonce(m.body.data() + 2, seal_ctr, /*x32=*/m.id, nonce);       // ★ x32 = the channel_msg_id
    uint8_t aad[9];   ref_aad(meshroute::key_hash32_of(pub), m.id, /*channel_id=*/7, aad);
    uint8_t pt[64];
    CHECK(crypto_aead_unlock(pt, m.body.data() + 10 + ct_len, key, nonce, aad, sizeof aad,
                             m.body.data() + 10, ct_len) == 0);
    // ★★ THE INNER, byte for byte: a flags byte that says TEXT-ONLY, then the text. Nothing about the seal, the
    //    nonce or the AAD changed to carry it — the inner is opaque to all three, which is the whole reason CL2b
    //    needed no wire_version bump.
    CHECK(pt[0] == protocol::channel_inner_flag_text);
    CHECK((pt[0] & protocol::channel_inner_flag_location) == 0);
    CHECK(std::memcmp(pt + 1, "team-secret", txt_len) == 0);
    // (4) EVERY AAD field is genuinely bound — perturb one at a time, each must fail the tag.
    uint8_t bad[9];
    ref_aad(meshroute::key_hash32_of(pub) ^ 1u, m.id, 7, bad);
    CHECK(crypto_aead_unlock(pt, m.body.data() + 10 + ct_len, key, nonce, bad, sizeof bad, m.body.data() + 10, ct_len) != 0);
    ref_aad(meshroute::key_hash32_of(pub), m.id ^ 1u, 7, bad);
    CHECK(crypto_aead_unlock(pt, m.body.data() + 10 + ct_len, key, nonce, bad, sizeof bad, m.body.data() + 10, ct_len) != 0);
    ref_aad(meshroute::key_hash32_of(pub), m.id, 8, bad);
    CHECK(crypto_aead_unlock(pt, m.body.data() + 10 + ct_len, key, nonce, bad, sizeof bad, m.body.data() + 10, ct_len) != 0);
    // (5) and the NONCE really is the msg-id one: the same body under a team-key-hash nonce must NOT open.
    uint8_t wrong_nonce[24]; ref_nonce(m.body.data() + 2, seal_ctr, meshroute::key_hash32_of(pub), wrong_nonce);
    ref_aad(meshroute::key_hash32_of(pub), m.id, 7, aad);
    CHECK(crypto_aead_unlock(pt, m.body.data() + 10 + ct_len, key, wrong_nonce, aad, sizeof aad, m.body.data() + 10, ct_len) != 0);
}

TEST_CASE("★★ §chan-crypt CL2a — NONCE UNIQUENESS: never reused across posts, and NOT EVEN between two members sharing the key + seed + ctr") {
    constexpr uint32_t T = 0xABCD1234u;
    // (a) SAME node, two posts of the SAME text: the carried ctr advances and the nonce differs. (The seed is forced
    //     CONSTANT here on purpose — it isolates the ctr's contribution instead of letting randomness hide a bug.)
    TkHal hal; hal._fill = 0x41;
    Node n(hal, 3, 0x1234ABCDu); NodeConfig c = team_cfg(T); n.on_init(c);
    give_team_key(n, 0x10);
    n.mutable_config().channel_min_interval_ms = 0;           // the burst floor is not what this case is about
    const AiredM a = post_team_and_capture(hal, n, 7, "same", CryptIntent::on);
    hal.tx_frames.clear();
    hal._now += 1;
    const AiredM b = post_team_and_capture(hal, n, 7, "same", CryptIntent::on);
    CHECK(a.ok); CHECK(b.ok);
    if (a.ok && b.ok) {
        const uint16_t ca = static_cast<uint16_t>(a.body[0] | (a.body[1] << 8));
        const uint16_t cb = static_cast<uint16_t>(b.body[0] | (b.body[1] << 8));
        CHECK(ca == 1); CHECK(cb == 2);                       // the CARRIED ctr advanced
        uint8_t na[24], nb[24];
        ref_nonce(a.body.data() + 2, ca, a.id, na);
        ref_nonce(b.body.data() + 2, cb, b.id, nb);
        CHECK(std::memcmp(na, nb, 24) != 0);                  // ⇒ no keystream reuse
        CHECK(std::memcmp(a.body.data() + 10, b.body.data() + 10, 4) != 0);   // and the ciphertexts differ
    }
    // (b) ★★ THE SHARED-KEY HAZARD — TWO DIFFERENT MEMBERS, one team key, an IDENTICAL forced seed and an identical
    //     ctr (both are on their first seal). A per-node counter cannot separate them and the random seed has been
    //     deliberately neutralised, so the ONLY thing left is the channel_msg_id bound into the nonce.
    TkHal hA; hA._fill = 0x41; Node A(hA, /*id=*/3, /*key=*/0x1234ABCDu); NodeConfig cA = team_cfg(T); A.on_init(cA);
    TkHal hB; hB._fill = 0x41; Node B(hB, /*id=*/4, /*key=*/0x5678BEEFu); NodeConfig cB = team_cfg(T); B.on_init(cB);
    give_team_key(A, 0x10); give_team_key(B, 0x10);           // the SAME content key on both
    const AiredM ma = post_team_and_capture(hA, A, 7, "same", CryptIntent::on);
    const AiredM mb = post_team_and_capture(hB, B, 7, "same", CryptIntent::on);
    CHECK(ma.ok); CHECK(mb.ok);
    if (ma.ok && mb.ok) {
        CHECK(std::memcmp(ma.body.data(), mb.body.data(), 10) == 0);   // identical [seal_ctr][seed8] — the collision setup
        CHECK(ma.id != mb.id);                                          // ...but different wire identities
        uint8_t na[24], nb[24];
        ref_nonce(ma.body.data() + 2, 1, ma.id, na);
        ref_nonce(mb.body.data() + 2, 1, mb.id, nb);
        CHECK(std::memcmp(na, nb, 24) != 0);                            // ★ DIFFERENT NONCES anyway
        CHECK(std::memcmp(ma.body.data() + 10, mb.body.data() + 10, 4) != 0);   // ⇒ different keystream
        // ★★ AND THE COUNTERFACTUAL, which is the whole argument for choosing the msg_id over the team key hash:
        // with the TEAM KEY HASH in the nonce's 32-bit slot (the shape the dispatch had settled on) these two
        // members would have produced the SAME nonce under the SAME key — textbook keystream reuse.
        uint8_t pub[32], canon[32]; ref_team_pair(0x10, pub, canon);
        uint8_t ka[24], kb[24];
        ref_nonce(ma.body.data() + 2, 1, meshroute::key_hash32_of(pub), ka);
        ref_nonce(mb.body.data() + 2, 1, meshroute::key_hash32_of(pub), kb);
        CHECK(std::memcmp(ka, kb, 24) == 0);
    }
}

TEST_CASE("§chan-crypt CL2a — R7: a DEAD crypto RNG (all-zero seed) REFUSES to seal; nothing is buffered, flooded or downgraded") {
    constexpr uint32_t T = 0xABCD1234u;
    TkHal hal;                                                // _fill 0 -> the base ALL-ZERO stream = R7 the dead-RNG case
    Node n(hal, 3, 0x1234ABCDu); NodeConfig c = team_cfg(T); n.on_init(c);
    give_team_key(n, 0x10);
    const CmdResult r = send_channel(n, 7, "secret", /*team=*/true, /*global=*/false, CryptIntent::on);
    CHECK(r.code == CmdCode::queued);                         // the command was accepted; the SEAL then refused
    CHECK(r.ctr == 0);                                        // ...and reported "not sent" the way the self-gate does
    CHECK(n.channel_buffer_count() == 0);                     // ★ NOT buffered in clear (C2: refuse, never downgrade)
    CHECK(hal.tx_frames.empty());                             // ★ and nothing aired
    const Ev* e = hal.last("channel_seal_failed");
    CHECK(e != nullptr); if (e) CHECK(e->reason == "bad_rng");
    Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::bad_rng);
}

TEST_CASE("§chan-crypt CL2a/CL2b/CL2c — the SEALED size cap bounds the INNER: text 173, 163 with `-l`; refused, never truncated") {
    constexpr uint32_t T = 0xABCD1234u;
    CHECK(protocol::channel_seal_max_plaintext_bytes == 174);   // 200 - 26 — the INNER's cap, not the text's
    // ★★★ §chan-crypt CL2c — THE WIRE CODEPOINTS AND WIDTHS, PINNED NUMERICALLY. This block exists because a poison
    // probe found the gap: renumbering `channel_inner_flag_source` 0x04 → 0x08 left the ENTIRE suite green, because
    // every other assertion names the constant symbolically and is therefore self-referential. These bits and widths
    // are on the wire between two independently-built nodes, so a silent renumber is a fleet-splitting change — it must
    // fail HERE, at the definition, not on metal. (Same discipline as node.h's PeerKeyConf encoding assert.)
    CHECK(protocol::channel_inner_flag_text      == 0x01);
    CHECK(protocol::channel_inner_flag_location  == 0x02);
    CHECK(protocol::channel_inner_flag_source    == 0x04);
    CHECK(protocol::channel_inner_flags_known    == 0x07);
    CHECK(protocol::channel_inner_location_bytes == 6);
    CHECK(protocol::channel_inner_source_bytes   == 4);
    // ★ §chan-crypt CL2c — the overhead is DERIVED FROM THE FLAGS BYTE, and every combination is pinned here
    // because the size gate, the assembly and the parse all read this one function.
    CHECK(protocol::channel_inner_overhead(0) == 1);                                            // [flags]
    CHECK(protocol::channel_inner_overhead(protocol::channel_inner_flag_text) == 1);            // [flags][text]
    CHECK(protocol::channel_inner_overhead(protocol::channel_inner_flag_source) == 5);          // [flags][src4]
    CHECK(protocol::channel_inner_overhead(protocol::channel_inner_flag_location) == 7);        // [flags][loc6] — an inner the PARSE refuses (bit1 needs bit2), sized anyway: the helper is layout, not policy
    CHECK(protocol::channel_inner_overhead(static_cast<uint8_t>(protocol::channel_inner_flag_source
                                                             | protocol::channel_inner_flag_location)) == 11);   // [flags][src4][loc6] — the `-l` shape
    // ...and the SEND side's composer never emits a position without a sender (the one cross-bit rule, in one place).
    CHECK(protocol::channel_inner_flags(true,  false) == protocol::channel_inner_flag_text);
    CHECK(protocol::channel_inner_flags(false, true)  == (protocol::channel_inner_flag_location
                                                        | protocol::channel_inner_flag_source));
    CHECK(protocol::channel_inner_flags(true,  true)  == (protocol::channel_inner_flag_text
                                                        | protocol::channel_inner_flag_location
                                                        | protocol::channel_inner_flag_source));
    const size_t text_cap     = protocol::channel_seal_max_plaintext_bytes - 1;    // 173
    const size_t text_cap_loc = protocol::channel_seal_max_plaintext_bytes - 11;   // 163 — CL2c's 4 B moved it from 167
    CHECK(text_cap == 173); CHECK(text_cap_loc == 163);
    TkHal hal; hal._fill = 0x41;
    Node n(hal, 3, 0x1234ABCDu); NodeConfig c = team_cfg(T); n.on_init(c);
    give_team_key(n, 0x10);
    const std::string over(text_cap + 1, 'x');                  // 174: legal plain, one byte too long sealed
    const CmdResult r = send_channel(n, 7, over.c_str(), /*team=*/true, /*global=*/false, CryptIntent::on);
    CHECK(r.code == CmdCode::err_too_large);
    CHECK(n.channel_buffer_count() == 0);
    CHECK(hal.tx_frames.empty());
    const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "too_large");
    Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::too_large);
    // SAME-SITE CONTROL: exactly at the cap it seals and airs, and the aired body is inner+overhead exactly.
    TkHal h2; h2._fill = 0x41; Node n2(h2, 3, 0x1234ABCDu); NodeConfig c2 = team_cfg(T); n2.on_init(c2);
    give_team_key(n2, 0x10);
    const std::string at(text_cap, 'x');
    const AiredM ma = post_team_and_capture(h2, n2, 7, at.c_str(), CryptIntent::on);
    CHECK(ma.ok);
    if (ma.ok) CHECK(ma.body.size() == protocol::channel_msg_max_payload_bytes);   // 174 inner + 26 = 200, the carrier cap
    CHECK(n2.channel_buffer_count() == 1);
    // ★ §chan-crypt CL2b/CL2c — WITH `-l` the cap drops by the 6 position bytes AND CL2c's 4 sender bytes, and BOTH
    // SIDES of the NEW boundary are measured.
    TkHal h4; h4._fill = 0x41; Node n4(h4, 3, 0x1234ABCDu);
    NodeConfig c4 = team_cfg(T); c4.lat_e7 = 521000000; c4.lon_e7 = 210000000; c4.channel_min_interval_ms = 0;
    n4.on_init(c4); give_team_key(n4, 0x10);
    const std::string over_loc(text_cap_loc + 1, 'y');          // 164 + 11 = 175 > 174 -> REFUSED
    CHECK(send_channel(n4, 7, over_loc.c_str(), /*team=*/true, /*global=*/false, CryptIntent::on, /*loc=*/true).code
          == CmdCode::err_too_large);
    CHECK(n4.channel_buffer_count() == 0);
    CHECK(h4.tx_frames.empty());
    // ...and the SAME 164 bytes fit WITHOUT `-l` — the same-site control that proves the 10 header bytes refused it.
    CHECK(send_channel(n4, 7, over_loc.c_str(), /*team=*/true, /*global=*/false, CryptIntent::on).code == CmdCode::queued);
    // ★★ AND THE CL2c-SPECIFIC CONTROL, which is what pins the boundary to the SOURCE_HASH rather than to "some
    // header": the four bytes that used to fit under CL2b's 167 now REFUSE, and the same text minus four fits.
    { TkHal h4b; h4b._fill = 0x41; Node n4b(h4b, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg(T); c.lat_e7 = 521000000; c.lon_e7 = 210000000; c.channel_min_interval_ms = 0;
      n4b.on_init(c); give_team_key(n4b, 0x10);
      CHECK(send_channel(n4b, 7, std::string(167, 'y').c_str(), /*team=*/true, /*global=*/false,
                         CryptIntent::on, /*loc=*/true).code == CmdCode::err_too_large);   // CL2b's old cap
      CHECK(send_channel(n4b, 7, std::string(163, 'y').c_str(), /*team=*/true, /*global=*/false,
                         CryptIntent::on, /*loc=*/true).code == CmdCode::queued); }        // CL2c's cap
    // AT the located cap: 163 + 11 = 174 -> seals and airs a full 200-B body.
    TkHal h5; h5._fill = 0x41; Node n5(h5, 3, 0x1234ABCDu);
    NodeConfig c5 = team_cfg(T); c5.lat_e7 = 521000000; c5.lon_e7 = 210000000; n5.on_init(c5);
    give_team_key(n5, 0x10);
    const AiredM mb = post_team_and_capture(h5, n5, 7, std::string(text_cap_loc, 'z').c_str(), CryptIntent::on, /*loc=*/true);
    CHECK(mb.ok);
    if (mb.ok) CHECK(mb.body.size() == protocol::channel_msg_max_payload_bytes);
    // ...and the SAME 175 bytes still post fine WITHOUT `-e` on a node with no key (the PLAIN cap is unchanged: 200).
    TestHal h3; Node n3(h3, 3, 0x1234ABCDu); NodeConfig c3 = team_cfg(T); n3.on_init(c3);
    CHECK(send_channel(n3, 7, std::string(175, 'x').c_str(), /*team=*/true).code == CmdCode::queued);
}

TEST_CASE("★ §chan-crypt CL2a — RECEIVE: a keyholder opens the post (enc=1); a WRONG key and NO key both fail CLOSED and still RELAY") {
    constexpr uint32_t T = 0xABCD1234u;
    // Produce one real sealed post from a keyholder.
    TkHal hs; hs._fill = 0x41; Node S(hs, /*id=*/3, /*key=*/0x1234ABCDu);
    NodeConfig cs = team_cfg(T); S.on_init(cs); give_team_key(S, 0x10);
    const AiredM m = post_team_and_capture(hs, S, /*ch=*/7, "team-secret", CryptIntent::on);
    CHECK(m.ok); if (!m.ok) return;
    auto deliver = [&](Node& R, TestHal& hr) {
        m_out mm = mk_m(m.id, /*ch=*/7, m.flavor, m.body.data(), static_cast<uint8_t>(m.body.size()));
        mm.team_id = T; R.ingest_channel_m(mm, /*from=*/9); (void)hr;
    };
    // (a) RIGHT key -> opened, channel_recv carries the PLAINTEXT and enc=true.
    { TestHal hr; Node R(hr, /*id=*/5, /*key=*/0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      deliver(R, hr);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.enc);
      CHECK(pu.body_len == std::strlen("team-secret"));
      CHECK(std::memcmp(pu.body, "team-secret", pu.body_len) == 0);
      CHECK(R.channel_buffer_count() == 1);                   // buffered (still SEALED — it may have to be re-served)
      CHECK(hr.count("channel_crypt_undecryptable") == 0); }
    // (b) WRONG key (a stale one after a re-key) -> FAILS CLOSED: no channel_recv at all, team_channel_no_key instead.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, /*fill=*/0x77);                        // a DIFFERENT content key
      deliver(R, hr);
      const auto v = drain_all(R);
      Push pu{}; CHECK_FALSE(find_push(v, PushKind::channel_recv, pu));   // ★ nothing forged, nothing partial
      CHECK(find_push(v, PushKind::team_channel_no_key, pu));
      CHECK(pu.channel_msg_id == m.id); CHECK(pu.channel_id == 7); CHECK(pu.team_id == T);
      const Ev* e = hr.last("channel_crypt_undecryptable");
      CHECK(e != nullptr); if (e) CHECK(e->reason == "open_failed");
      CHECK(R.channel_buffer_count() == 1); }                 // ★ STILL BUFFERED -> still relayed (content-blind)
    // (c) NO key -> the same closed failure, but the telemetry names the other cause.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      deliver(R, hr);
      const auto v = drain_all(R);
      Push pu{}; CHECK_FALSE(find_push(v, PushKind::channel_recv, pu));
      CHECK(find_push(v, PushKind::team_channel_no_key, pu));
      const Ev* e = hr.last("channel_crypt_undecryptable");
      CHECK(e != nullptr); if (e) CHECK(e->reason == "no_key");
      // ★★ THE CONTENT-BLIND-RELAY PROOF, and it is the assertion that matters most in this case: the un-keyed node
      // still HOLDS the post and still SERVES it, byte-for-byte sealed, to a peer that pulls it. A member without
      // the key must never sever the channel for the members who have it.
      CHECK(R.channel_buffer_count() == 1);
      std::array<uint8_t,32> qb{};
      const size_t qn = mk_q_pull(/*src=*/6, /*dest=*/5, &m.id, 1, qb);
      R.on_recv(qb.data(), qn, meta_at(100));
      CHECK(hr.count("channel_broadcast_tx") == 1);
      R.on_timer(kCtsToDataGapTimerId);                       // RTS -> the M frame itself
      const std::vector<uint8_t>* served = hr.last_tx_cmd(0xA);
      CHECK(served != nullptr);
      if (served) { auto pm = parse_m(std::span<const uint8_t>(served->data(), served->size()));
                    CHECK(pm.has_value());
                    if (pm) { CHECK((pm->flavor & protocol::channel_flavor_crypted) != 0);
                              CHECK(pm->body.size() == m.body.size());
                              CHECK(std::memcmp(pm->body.data(), m.body.data(), m.body.size()) == 0); } } }
    // (d) a TAMPERED ciphertext byte -> the tag catches it, same closed failure.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      std::vector<uint8_t> tampered = m.body; tampered[12] ^= 0x01;
      m_out mm = mk_m(m.id, 7, m.flavor, tampered.data(), static_cast<uint8_t>(tampered.size()));
      mm.team_id = T; R.ingest_channel_m(mm, 9);
      Push pu{}; CHECK_FALSE(find_push(drain_all(R), PushKind::channel_recv, pu)); }
    // (e) RE-ATTRIBUTION: the same sealed body republished under a DIFFERENT channel_msg_id must NOT open (the AAD
    //     and the nonce both bind the id) — a relay cannot re-label someone else's post as its own.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      m_out mm = mk_m(m.id ^ 0x01000000u, 7, m.flavor, m.body.data(), static_cast<uint8_t>(m.body.size()));
      mm.team_id = T; R.ingest_channel_m(mm, 9);
      Push pu{}; CHECK_FALSE(find_push(drain_all(R), PushKind::channel_recv, pu)); }
}

TEST_CASE("§chan-crypt CL2a — the team_channel_no_key push is RATE-LIMITED; the telemetry is not") {
    constexpr uint32_t T = 0xABCD1234u;
    TkHal hs; hs._fill = 0x41; Node S(hs, 3, 0x1234ABCDu);
    NodeConfig cs = team_cfg(T); S.on_init(cs); give_team_key(S, 0x10);
    S.mutable_config().channel_min_interval_ms = 0;
    std::vector<AiredM> posts;
    for (int i = 0; i < 3; ++i) { hs.tx_frames.clear(); hs._now += 1;
        posts.push_back(post_team_and_capture(hs, S, 7, i == 0 ? "one" : i == 1 ? "two" : "three", CryptIntent::on)); }
    TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);   // NO key
    for (const auto& p : posts) { CHECK(p.ok); if (!p.ok) continue;
        m_out mm = mk_m(p.id, 7, p.flavor, p.body.data(), static_cast<uint8_t>(p.body.size()));
        mm.team_id = T; R.ingest_channel_m(mm, 9); }
    int pushes = 0; for (const auto& p : drain_all(R)) if (p.kind == PushKind::team_channel_no_key) ++pushes;
    CHECK(pushes == 1);                                        // ★ one prompt, not one per unreadable post
    CHECK(hr.count("channel_crypt_undecryptable") == 3);       // ...while the bench still sees every occurrence
    // ...and once the window elapses the app is prompted again (the key may have arrived meanwhile).
    hr._now += protocol::team_channel_no_key_push_min_ms;
    { const auto& p = posts[0];
      m_out mm = mk_m(p.id ^ 0x00000055u, 7, p.flavor, p.body.data(), static_cast<uint8_t>(p.body.size()));
      mm.team_id = T; R.ingest_channel_m(mm, 9); }
    Push pu{}; CHECK(find_push(drain_all(R), PushKind::team_channel_no_key, pu));
}

TEST_CASE("★ §chan-crypt CL2a — team_channel_crypt DEFAULT-ON: `-t` seals when a key is held, and the CONFIG toggle is the only opt-out") {
    constexpr uint32_t T = 0xABCD1234u;
    // (a) key held, default config, NO `-e` -> SEALED anyway (T-K2 §2.5).
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg(T); CHECK(c.team_channel_crypt); n.on_init(c);
      give_team_key(n, 0x10);
      const AiredM m = post_team_and_capture(hal, n, 7, "auto", CryptIntent::def);
      CHECK(m.ok); if (m.ok) { CHECK((m.flavor & protocol::channel_flavor_crypted) != 0);
                               CHECK(m.body.size() == 1 + 4 + protocol::channel_seal_overhead_bytes); } }   // §CL2b: [flags]["auto"] + seal
    // (b) the OPT-OUT: cfg team_channel_crypt = 0 -> plaintext, byte-for-byte the pre-CL2a post.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg(T); c.team_channel_crypt = false; n.on_init(c);
      give_team_key(n, 0x10);
      const AiredM m = post_team_and_capture(hal, n, 7, "auto", CryptIntent::def);
      CHECK(m.ok); if (m.ok) { CHECK((m.flavor & protocol::channel_flavor_crypted) == 0);
                               CHECK(m.body.size() == 4);
                               CHECK(std::memcmp(m.body.data(), "auto", 4) == 0); } }
    // (c) ...and `-e` STILL seals with the toggle off — the toggle governs the DEFAULT, not the capability.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg(T); c.team_channel_crypt = false; n.on_init(c);
      give_team_key(n, 0x10);
      const AiredM m = post_team_and_capture(hal, n, 7, "auto", CryptIntent::on);
      CHECK(m.ok); if (m.ok) CHECK((m.flavor & protocol::channel_flavor_crypted) != 0); }
    // (d) NO key, default config -> plaintext, unchanged. A keyless member must keep posting.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg(T); n.on_init(c);
      const AiredM m = post_team_and_capture(hal, n, 7, "auto", CryptIntent::def);
      CHECK(m.ok); if (m.ok) { CHECK((m.flavor & protocol::channel_flavor_crypted) == 0);
                               CHECK(m.body.size() == 4); } }
}

TEST_CASE("★★ §chan-crypt CL2a — the implicit seal is scoped to a TEAM-ONLY post: a keyholder's GLOBAL and `-t -g` posts are UNCHANGED") {
    // THE REGRESSION THIS PINS: an unqualified `want_crypt = -e || (crypt_cfg && key_held)` makes a keyholder's plain
    // GLOBAL post hit the `no_team` refusal and its `-t -g` post hit `global_clear_copy` — two invocations that work
    // today would start FAILING purely because a content key arrived. Both spec rows say "unchanged".
    constexpr uint32_t T = 0xABCD1234u;
    TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
    NodeConfig c = team_cfg(T); n.on_init(c);
    give_team_key(n, 0x10);
    n.mutable_config().channel_min_interval_ms = 0;
    // plain (GLOBAL): a mobile delegates to its home and has none here -> mobile_no_home, which is the PRE-CL2a
    // outcome for this node shape. What matters is that it is NOT a crypt refusal.
    CHECK(send_channel(n, 7, "global").code == CmdCode::err_no_binding);
    CHECK(hal.count("channel_crypt_refused") == 0);
    // `-t -g` on a keyholder: accepted, and the TEAM copy is deliberately CLEAR (sealing it while the global twin
    // airs the same text would defeat the seal) — announced by channel_crypt_skipped, never silently.
    hal._now += 1;
    CHECK(send_channel(n, 7, "both", /*team=*/true, /*global=*/true).code == CmdCode::queued);
    CHECK(hal.count("channel_crypt_refused") == 0);
    const Ev* sk = hal.last("channel_crypt_skipped");
    CHECK(sk != nullptr); if (sk) CHECK(sk->reason == "global_clear_copy");
    drain_originate_flood(n);
    const std::vector<uint8_t>* mf = hal.last_tx_cmd(0xA);
    CHECK(mf != nullptr);
    if (mf) { auto pm = parse_m(std::span<const uint8_t>(mf->data(), mf->size()));
              CHECK(pm.has_value()); if (pm) CHECK((pm->flavor & protocol::channel_flavor_crypted) == 0); }
    // and the two permanent refusals still fire for EXPLICIT `-e` only, on a keyholder exactly as on a keyless node.
    CHECK(send_channel(n, 7, "x", /*team=*/false, /*global=*/false, CryptIntent::on).code == CmdCode::err_unsupported);
    CHECK(send_channel(n, 7, "x", /*team=*/true,  /*global=*/true,  CryptIntent::on).code == CmdCode::err_unsupported);
}

// =============================================================================
// ★★★ §chan-crypt CL2b — `send_channel -t -l -e`: A POSITION INSIDE THE SEAL
// (spec 2026-07-30 §2.2.1 + ruling O6, T-K2 §2.2 as corrected, AB4 §2.7)
// =============================================================================
// The corpus is STRUCTURALLY BLIND to all of this for the SAME reason it was blind to CL2a — no simulator node can
// hold a team content key, and `-l` is not in the sim's send_channel grammar either — so these tests are the ONLY
// coverage. They are written as KATs, negatives and refusal-matrices rather than round-trips wherever a round-trip
// could pass with both halves wrong in the same way.
namespace {

// A team member WITH a fix. lat/lon are a real place at 1e-7 deg so pack_loc6's ~11 m quantisation is exercised
// (the 6-B encoding is LOSSY on purpose — a test that used 0,0 would not notice if it silently packed nothing).
constexpr int32_t kLat = 521234567;    // +52.1234567
constexpr int32_t kLon = -12345678;    // -1.2345678
static NodeConfig team_cfg_loc(uint32_t team = 0xABCD1234u) {
    NodeConfig c = team_cfg(team); c.lat_e7 = kLat; c.lon_e7 = kLon; return c;
}
// The ~11 m-quantised round trip of a coordinate — what a receiver MUST see, and never the raw e7 value.
static void quantised(int32_t lat_in, int32_t lon_in, int32_t& lat_out, int32_t& lon_out) {
    uint8_t six[6]; pack_loc6(lat_in, lon_in, std::span<uint8_t>(six, 6));
    CHECK(unpack_loc6(std::span<const uint8_t>(six, 6), lat_out, lon_out));
}
// Open an aired sealed body with the INDEPENDENT reference derivations and hand back the raw INNER.
static std::vector<uint8_t> ref_open_inner(const AiredM& m, uint8_t key_fill, uint8_t channel_id) {
    std::vector<uint8_t> out;
    if (!m.ok || m.body.size() < protocol::channel_seal_overhead_bytes) return out;
    uint8_t pub[32], canon[32]; ref_team_pair(key_fill, pub, canon);
    uint8_t key[32]; ref_content_key(canon, key);
    const uint16_t sc = static_cast<uint16_t>(m.body[0] | (m.body[1] << 8));
    uint8_t nonce[24]; ref_nonce(m.body.data() + 2, sc, m.id, nonce);
    uint8_t aad[9];   ref_aad(meshroute::key_hash32_of(pub), m.id, channel_id, aad);
    const size_t ct_len = m.body.size() - protocol::channel_seal_overhead_bytes;
    std::vector<uint8_t> pt(ct_len ? ct_len : 1, 0);
    if (crypto_aead_unlock(pt.data(), m.body.data() + 10 + ct_len, key, nonce, aad, sizeof aad,
                           m.body.data() + 10, ct_len) != 0) return out;
    pt.resize(ct_len);
    return pt;
}
// FORGE a sealed body carrying an ARBITRARY inner, under the same key a node with `key_fill` holds. This is what
// makes the malformed-inner cases testable at all: our own sender can never produce them, so they must be built.
static std::vector<uint8_t> forge_sealed(const uint8_t* inner, size_t inner_len, uint32_t msg_id,
                                         uint8_t channel_id, uint8_t key_fill, uint16_t seal_ctr = 1) {
    uint8_t pub[32], canon[32]; ref_team_pair(key_fill, pub, canon);
    uint8_t key[32]; ref_content_key(canon, key);
    uint8_t seed[8]; for (int i = 0; i < 8; ++i) seed[i] = static_cast<uint8_t>(0x41 + i);
    uint8_t nonce[24]; ref_nonce(seed, seal_ctr, msg_id, nonce);
    uint8_t aad[9];    ref_aad(meshroute::key_hash32_of(pub), msg_id, channel_id, aad);
    std::vector<uint8_t> body(2 + 8 + inner_len + 16, 0);
    body[0] = static_cast<uint8_t>(seal_ctr); body[1] = static_cast<uint8_t>(seal_ctr >> 8);
    for (int i = 0; i < 8; ++i) body[2 + i] = seed[i];
    crypto_aead_lock(body.data() + 10, body.data() + 10 + inner_len, key, nonce, aad, sizeof aad,
                     inner_len ? inner : nullptr, inner_len);
    return body;
}
// Deliver a team-scoped sealed M to `R`.
static void deliver_sealed(Node& R, uint32_t id, uint8_t channel_id, const std::vector<uint8_t>& body, uint32_t team) {
    m_out mm = mk_m(id, channel_id, static_cast<uint8_t>(protocol::channel_flavor_team | protocol::channel_flavor_crypted),
                    body.data(), static_cast<uint8_t>(body.size()));
    mm.team_id = team; R.ingest_channel_m(mm, /*from=*/9);
}
// Teach `R` that team_local_id `src` is a same-team peer whose stable key_hash32 is 0x1000+src (mk_beacon_digest_team's
// own convention) — the ONLY route by which a full 32-bit sender hash is derivable at ingest.
static void learn_team_peer(Node& R, uint8_t src, uint32_t team, uint32_t advertise_id) {
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_digest_team(src, &advertise_id, 1, team, bb);
    R.on_recv(bb.data(), bn, meta_at(10));
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// THE SEND SIDE — ruling O6, row for row
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("★★★ §chan-crypt CL2b — RULING O6, EVERY ROW: `-l` is refused exactly when the post would NOT be sealed") {
    constexpr uint32_t T = 0xABCD1234u;
    // (1) `-t -l` + key held + team_channel_crypt ON (the DEFAULT) -> ★ OK, sealed by the node default. NO `-e` typed.
    //     This is the row that makes the rule "a position never travels in clear" rather than "always type -e":
    //     the post IS sealed, so the position is safe, so it must succeed — exactly as `send -l` succeeds under e2e_dm.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); CHECK(c.team_channel_crypt); n.on_init(c);
      give_team_key(n, 0x10);
      const AiredM m = post_team_and_capture(hal, n, 7, "here", CryptIntent::def, /*loc=*/true);
      CHECK(m.ok);
      if (m.ok) { CHECK((m.flavor & protocol::channel_flavor_crypted) != 0);
                  // [flags 1][source_hash 4][loc 6] + strlen("here") — §chan-crypt CL2c added the middle four.
                  CHECK(m.body.size() == 1 + 4 + 6 + 4 + protocol::channel_seal_overhead_bytes); }
      CHECK(hal.count("channel_crypt_refused") == 0); }
    // (2) `-t -l` + NO team key -> REFUSE unsealable. want_crypt is false (the implicit term REQUIRES a key), so the
    //     location gate fires before any of the `-e` matrix. Nothing airs.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); n.on_init(c);
      const CmdResult r = send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::def, /*loc=*/true);
      CHECK(r.code == CmdCode::err_unsupported);
      CHECK(n.channel_buffer_count() == 0); CHECK(hal.tx_frames.empty());
      const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "loc_unsealed");
      Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
      CHECK(pu.reason == SendFailReason::unsealable); }
    // (3) `-t -l` + team_channel_crypt 0 -> REFUSE unsealable. The operator opted OUT of sealing, so the post would go
    //     in clear; the position may not ride it. (Key held — this is not a key problem, and the reason says so.)
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); c.team_channel_crypt = false; n.on_init(c);
      give_team_key(n, 0x10);
      CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::def, /*loc=*/true).code
            == CmdCode::err_unsupported);
      CHECK(n.channel_buffer_count() == 0); CHECK(hal.tx_frames.empty());
      const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "loc_unsealed");
      // ...and `-e` STILL works with the toggle off: the toggle governs the DEFAULT, not the capability.
      CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::on, /*loc=*/true).code
            == CmdCode::queued); }
    // (4) `-t -l -e` -> ★ OK, explicit. THE OWNER'S TARGET FORM.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); n.on_init(c); give_team_key(n, 0x10);
      const AiredM m = post_team_and_capture(hal, n, 7, "here", CryptIntent::on, /*loc=*/true);
      CHECK(m.ok); if (m.ok) CHECK((m.flavor & protocol::channel_flavor_crypted) != 0); }
    //     ...and `-t -l -e` with NO key refuses `no_key`, not `loc_unsealed` — want_crypt is TRUE (explicit `-e`), so
    //     the key pre-flight owns it and the operator is told to get a key, not to enable encryption.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); n.on_init(c);
      CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::on, /*loc=*/true).code
            == CmdCode::err_unsupported);
      const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "no_key"); }
    // (5) `-l` with NO `-t` -> REFUSE unsealable. A GLOBAL channel has no content key in this system, ever.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); n.on_init(c); give_team_key(n, 0x10);
      CHECK(send_channel(n, 7, "here", /*team=*/false, /*global=*/false, CryptIntent::def, /*loc=*/true).code
            == CmdCode::err_unsupported);
      CHECK(n.channel_buffer_count() == 0); CHECK(hal.tx_frames.empty());
      const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "loc_unsealed");
      // and `-l -e` without `-t` refuses on the SAME enumerator via the `no_team` arm (one remedy: post with -t).
      Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
      CHECK(pu.reason == SendFailReason::unsealable);
      CHECK(send_channel(n, 7, "here", /*team=*/false, /*global=*/false, CryptIntent::on, /*loc=*/true).code
            == CmdCode::err_unsupported);
      const Ev* e2 = hal.last("channel_crypt_refused"); CHECK(e2 != nullptr); if (e2) CHECK(e2->reason == "no_team"); }
    // (6) `-t -g -l -e` -> REFUSE. Already refused for `-t -g -e`; with a position the GLOBAL copy would air
    //     COORDINATES in clear, so the reason is STRONGER, not different — and the existing arm still owns it.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); n.on_init(c); give_team_key(n, 0x10);
      CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/true, CryptIntent::on, /*loc=*/true).code
            == CmdCode::err_unsupported);
      CHECK(n.channel_buffer_count() == 0); CHECK(hal.tx_frames.empty());
      const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "global_clear_copy");
      // ⚠ and `-t -g -l` WITHOUT `-e` on a keyholder is refused too, by the LOCATION gate: `-t -g` scopes the implicit
      //   seal off (want_crypt false), so both copies would be clear. Different arm, same correct outcome.
      TkHal h2; h2._fill = 0x41; Node n2(h2, 3, 0x1234ABCDu);
      NodeConfig c2 = team_cfg_loc(T); n2.on_init(c2); give_team_key(n2, 0x10);
      CHECK(send_channel(n2, 7, "here", /*team=*/true, /*global=*/true, CryptIntent::def, /*loc=*/true).code
            == CmdCode::err_unsupported);
      const Ev* e2 = h2.last("channel_crypt_refused"); CHECK(e2 != nullptr); if (e2) CHECK(e2->reason == "loc_unsealed"); }
    // (7) `-t -l` with NO FIX (0,0) -> REFUSE `no_location`, and it is DISTINCT from unsealable on purpose: telling an
    //     operator with no GPS to enable encryption sends him after the wrong remedy.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg(T); CHECK(c.lat_e7 == 0); CHECK(c.lon_e7 == 0); n.on_init(c);
      give_team_key(n, 0x10);
      CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::def, /*loc=*/true).code
            == CmdCode::err_unsupported);
      CHECK(n.channel_buffer_count() == 0); CHECK(hal.tx_frames.empty());
      const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "no_fix");
      Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
      CHECK(pu.reason == SendFailReason::no_location);
      // ORDER: unsealable WINS over no_fix — a post that is both unsealed and fix-less reports the LEAK it would have
      // caused, not the missing fix (verbatim node_mac.cpp's `-l` ordering, U1).
      TkHal h2; h2._fill = 0x41; Node n2(h2, 3, 0x1234ABCDu);
      NodeConfig c2 = team_cfg(T); n2.on_init(c2);                        // no fix AND no key
      CHECK(send_channel(n2, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::def, /*loc=*/true).code
            == CmdCode::err_unsupported);
      const Ev* e2 = h2.last("channel_crypt_refused"); CHECK(e2 != nullptr); if (e2) CHECK(e2->reason == "loc_unsealed"); }
    // (8) CONTROL — every one of the rows above leaves an ORDINARY post untouched: no `-l`, no key, plain `-t`, works.
    { TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
      NodeConfig c = team_cfg_loc(T); n.on_init(c);
      CHECK(send_channel(n, 7, "plain", /*team=*/true).code == CmdCode::queued);
      CHECK(hal.count("channel_crypt_refused") == 0); }
}

TEST_CASE("★ §chan-crypt CL2b — `flags == 0` (no text AND no position) is REFUSED; a position-only post is NOT") {
    constexpr uint32_t T = 0xABCD1234u;
    // (a) an EMPTY sealed post: 26 bytes of seal wrapped around nothing. Refused (spec §2.2.1).
    TkHal hal; hal._fill = 0x41; Node n(hal, 3, 0x1234ABCDu);
    NodeConfig c = team_cfg_loc(T); c.channel_min_interval_ms = 0; n.on_init(c);
    give_team_key(n, 0x10);
    CHECK(send_channel(n, 7, "", /*team=*/true, /*global=*/false, CryptIntent::on).code == CmdCode::err_unsupported);
    CHECK(n.channel_buffer_count() == 0); CHECK(hal.tx_frames.empty());
    const Ev* e = hal.last("channel_crypt_refused"); CHECK(e != nullptr); if (e) CHECK(e->reason == "empty");
    // ⚠ NO send_failed push accompanies it — none of the three reusable reasons is true and appending an enumerator
    //   was out of scope. The SYNCHRONOUS err_unsupported is the app-facing answer. Pinned so the gap is visible.
    Push pu{}; CHECK_FALSE(find_push(drain_all(n), PushKind::send_failed, pu));
    // (b) SAME-SITE CONTROL: the same empty text WITH `-l` is flags = 0x06 and is perfectly legal — "here I am".
    //     ★ §chan-crypt CL2c: 0x06, not CL2b's 0x02 — a position always brings its sender.
    hal._now += 1;
    const AiredM m = post_team_and_capture(hal, n, 7, "", CryptIntent::on, /*loc=*/true);
    CHECK(m.ok);
    if (m.ok) CHECK(m.body.size() == 1 + 4 + 6 + protocol::channel_seal_overhead_bytes);
    const std::vector<uint8_t> inner = ref_open_inner(m, 0x10, 7);
    CHECK(inner.size() == 11);
    if (inner.size() == 11) CHECK(inner[0] == (protocol::channel_inner_flag_location    // bits 1+2, no text bit
                                             | protocol::channel_inner_flag_source));
    // (c) SECOND CONTROL: an empty PLAINTEXT post is still accepted, exactly as before CL2b. The flags byte exists
    //     only inside the seal, so nothing about the plain path changed (C1 — tightening it is a different slice).
    TestHal h2; Node n2(h2, 3, 0x1234ABCDu); NodeConfig c2 = team_cfg(T); n2.on_init(c2);
    CHECK(send_channel(n2, 7, "", /*team=*/true).code == CmdCode::queued);
    CHECK(n2.channel_buffer_count() == 1);
}

TEST_CASE("★★★ §chan-crypt CL2b/CL2c — KAT: the sealed inner is [flags 0x07][key_hash32 4 LE][pack_loc6 6][text], and NOTHING is in clear") {
    constexpr uint32_t T = 0xABCD1234u;
    constexpr uint32_t kSenderHash = 0x1234ABCDu;
    TkHal hal; hal._fill = 0x41; Node n(hal, /*id=*/3, /*key=*/kSenderHash);
    NodeConfig c = team_cfg_loc(T); n.on_init(c);
    give_team_key(n, 0x10);
    const char* txt = "at the col";
    const AiredM m = post_team_and_capture(hal, n, /*ch=*/7, txt, CryptIntent::on, /*loc=*/true);
    CHECK(m.ok); if (!m.ok) return;
    const size_t txt_len = std::strlen(txt);
    CHECK(m.body.size() == 1 + 4 + 6 + txt_len + protocol::channel_seal_overhead_bytes);
    // ★ INDEPENDENT open (the reference key/nonce/AAD, recomputed from the spec — see ref_* above).
    const std::vector<uint8_t> inner = ref_open_inner(m, 0x10, 7);
    CHECK(inner.size() == 1 + 4 + 6 + txt_len);
    if (inner.size() != 1 + 4 + 6 + txt_len) return;
    // (1) the FLAGS byte: all three bits, nothing else. This is the assertion the whole encoding argument rests on —
    //     an ENUM could not have represented text+location at all, and CL2c's third field would have cost it FOUR
    //     more codepoints.
    CHECK(inner[0] == (protocol::channel_inner_flag_text | protocol::channel_inner_flag_location
                                                         | protocol::channel_inner_flag_source));
    CHECK(inner[0] == 0x07);   // ★ the LITERAL on-wire byte — a symbolic check alone cannot catch a renumber
    // (2) ★★★ §chan-crypt CL2c — THE SENDER, fixed-size and FIRST (mirroring the DATA inner's field order, where
    //     source_hash also precedes location), 4 bytes LITTLE-ENDIAN, and it is the node's own stable key_hash32.
    const uint32_t src = static_cast<uint32_t>(inner[1]) | (static_cast<uint32_t>(inner[2]) << 8)
                       | (static_cast<uint32_t>(inner[3]) << 16) | (static_cast<uint32_t>(inner[4]) << 24);
    CHECK(src == kSenderHash);
    //     ...and it AGREES with the msg-id's 16 hash bits, which is what makes the receiver's cross-check meaningful.
    CHECK((src & 0xffffu) == ((m.id >> 8) & 0xffffu));
    // (3) the POSITION, fixed-size and SECOND — 6 bytes not 8, at offset 5 now that the sender precedes it.
    int32_t lat = 0, lon = 0;
    CHECK(unpack_loc6(std::span<const uint8_t>(inner.data() + 5, 6), lat, lon));
    int32_t qlat = 0, qlon = 0; quantised(kLat, kLon, qlat, qlon);
    CHECK(lat == qlat); CHECK(lon == qlon);
    CHECK(std::abs(static_cast<long>(lat) - kLat) < 2000);       // ~11 m quantisation, not a different coordinate
    CHECK(std::abs(static_cast<long>(lon) - kLon) < 2000);
    // (4) the TEXT, variable-length and LAST.
    CHECK(std::memcmp(inner.data() + 11, txt, txt_len) == 0);
    // (5) ★★ NOTHING OF IT IS IN CLEAR ON THE WIRE — neither the text, nor the packed position bytes, nor the 4-B
    //     sender hash appears anywhere in the aired body. This is the assertion the privacy rule actually needs; the
    //     length checks above would pass even if the seal were a no-op.
    const std::string wire(reinterpret_cast<const char*>(m.body.data()), m.body.size());
    CHECK(wire.find(txt) == std::string::npos);
    uint8_t six[6]; pack_loc6(kLat, kLon, std::span<uint8_t>(six, 6));
    CHECK(std::search(m.body.begin(), m.body.end(), six, six + 6) == m.body.end());
    const uint8_t src_le[4] = { uint8_t(kSenderHash), uint8_t(kSenderHash >> 8),
                                uint8_t(kSenderHash >> 16), uint8_t(kSenderHash >> 24) };
    CHECK(std::search(m.body.begin(), m.body.end(), src_le, src_le + 4) == m.body.end());
    // (6) and the flavor says CRYPTED+TEAM — a position can only ever ride a sealed team post.
    CHECK((m.flavor & protocol::channel_flavor_crypted) != 0);
    CHECK((m.flavor & protocol::channel_flavor_team) != 0);
}

TEST_CASE("★★★ §chan-crypt CL2c — a `-l` post from a node with NO IDENTITY (key_hash32 == 0) is REFUSED, not aired unattributed") {
    // The one thing that could produce a located post naming NOBODY. C2: refuse rather than air bit2 with a zero hash
    // (which the receiver would refuse anyway — both sides enforce the same invariant, at both ends).
    constexpr uint32_t T = 0xABCD1234u;
    TkHal hal; hal._fill = 0x41; Node n(hal, /*id=*/3, /*key_hash32=*/0);
    NodeConfig c = team_cfg_loc(T); c.channel_min_interval_ms = 0; n.on_init(c);
    give_team_key(n, 0x10);
    CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::on, /*loc=*/true).code
          == CmdCode::err_unsupported);
    CHECK(n.channel_buffer_count() == 0);            // nothing minted, nothing buffered
    CHECK(hal.tx_frames.empty());                    // ★ and nothing aired
    const Ev* e = hal.last("channel_crypt_refused");
    CHECK(e != nullptr); if (e) CHECK(e->reason == "no_identity");
    Push pu{}; CHECK(find_push(drain_all(n), PushKind::send_failed, pu));
    CHECK(pu.reason == SendFailReason::no_identity);   // U1 — the existing enumerator, rendered "(no crypto identity)"
    // SAME-SITE CONTROL (a): the SAME node WITHOUT `-l` posts fine — the refusal is the position's, not the seal's.
    CHECK(send_channel(n, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::on).code == CmdCode::queued);
    CHECK(n.channel_buffer_count() == 1);
    // SAME-SITE CONTROL (b): the SAME `-l` post from a node WITH an identity is accepted at the same site.
    TkHal h2; h2._fill = 0x41; Node n2(h2, 3, 0x1234ABCDu);
    NodeConfig c2 = team_cfg_loc(T); n2.on_init(c2); give_team_key(n2, 0x10);
    CHECK(send_channel(n2, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::on, /*loc=*/true).code
          == CmdCode::queued);
    // ORDER: identity is checked AFTER the confidentiality refusals — a `-l` post that is BOTH unsealable and
    // identity-less reports the leak it would have caused, exactly as the no-fix row does.
    TkHal h3; h3._fill = 0x41; Node n3(h3, 3, /*key_hash32=*/0);
    NodeConfig c3 = team_cfg_loc(T); n3.on_init(c3);                    // no key AND no identity
    CHECK(send_channel(n3, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::def, /*loc=*/true).code
          == CmdCode::err_unsupported);
    const Ev* e3 = h3.last("channel_crypt_refused"); CHECK(e3 != nullptr); if (e3) CHECK(e3->reason == "loc_unsealed");
    // ...and BEFORE the no-fix row: identity is the deeper lack, so a node with neither is told to provision first.
    TkHal h4; h4._fill = 0x41; Node n4(h4, 3, /*key_hash32=*/0);
    NodeConfig c4 = team_cfg(T); n4.on_init(c4); give_team_key(n4, 0x10);   // key held, NO fix, NO identity
    CHECK(send_channel(n4, 7, "here", /*team=*/true, /*global=*/false, CryptIntent::on, /*loc=*/true).code
          == CmdCode::err_unsupported);
    const Ev* e4 = h4.last("channel_crypt_refused"); CHECK(e4 != nullptr); if (e4) CHECK(e4->reason == "no_identity");
}

// ─────────────────────────────────────────────────────────────────────────────
// THE RECEIVE SIDE — parse, surface, retain
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("★★★ §chan-crypt CL2b/CL2c — RECEIVE: the text is stripped of its header, the position + CARRIED sender ride the push, and AB4 RETAINS under the carried hash") {
    constexpr uint32_t T = 0xABCD1234u;
    // The sender is team_local_id 3 with key_hash32 0x1003 — kept at mk_beacon_digest_team's 0x1000+src value so the
    // CL2b arms below (which DID depend on a beacon) still read as the same scenario. ★ §chan-crypt CL2c: the receiver
    // no longer needs that beacon at all — the hash is carried in the sealed inner. Arm (b) is what measures it.
    TkHal hs; hs._fill = 0x41; Node S(hs, /*id=*/3, /*key=*/0x00001003u);
    NodeConfig cs = team_cfg_loc(T); S.on_init(cs); give_team_key(S, 0x10);
    const AiredM m = post_team_and_capture(hs, S, /*ch=*/7, "at the col", CryptIntent::on, /*loc=*/true);
    CHECK(m.ok); if (!m.ok) return;
    CHECK((m.id >> 24) == 3);                                  // origin = the sender's plane-local id
    CHECK(((m.id >> 8) & 0xffffu) == 0x1003u);                 // ...and 16 bits of its stable key hash
    int32_t qlat = 0, qlon = 0; quantised(kLat, kLon, qlat, qlon);

    // (a) ★ THE WHOLE FEATURE, END TO END: a keyholder that KNOWS the sender opens the post, gets the TEXT ONLY on
    //     the push (the flags byte and the 6 position bytes are consumed, never shown), the position on the push's
    //     own fields, and a RETAINED address-book row anchored `team`.
    { TestHal hr; Node R(hr, /*id=*/5, /*key=*/0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      learn_team_peer(R, /*src=*/3, T, /*advertise_id=*/m.id);
      CHECK(R.peer_loc_count() == 0);                          // nothing retained before the post
      deliver_sealed(R, m.id, 7, m.body, T);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.enc);
      CHECK(pu.body_len == std::strlen("at the col"));         // ★ the HEADER IS GONE from the app's body
      CHECK(std::memcmp(pu.body, "at the col", pu.body_len) == 0);
      CHECK(pu.has_location);                                  // the SAME Push fields a located DM sets (U1)
      CHECK(pu.lat_e7 == qlat); CHECK(pu.lon_e7 == qlon);
      CHECK(pu.sender_hash == 0x1003u);                        // ★ §chan-crypt CL2c: WHOSE post it is, on the push
      // ★★ AB4's `team` arm, LIVE. One call, no schema change — the row is keyed by the sender's FULL key_hash32.
      CHECK(R.peer_loc_count() == 1);
      int32_t la = 0, lo = 0; uint32_t age = 0; Node::PeerLocSrc src = Node::PeerLocSrc::peer;
      CHECK(R.peer_loc_find(0x1003u, la, lo, age, src));
      CHECK(la == qlat); CHECK(lo == qlon);
      CHECK(src == Node::PeerLocSrc::team);                    // ★ GROUP-anchored, never mis-labelled `peer`
      CHECK_FALSE(R.peer_loc_find(0x1004u, la, lo, age, src)); // and under no other hash
      const Ev* e = hr.last("peer_location"); CHECK(e != nullptr);
      CHECK(hr.count("peer_location_unattributed") == 0);
      CHECK(hr.count("channel_inner_source_mismatch") == 0); }
      // ⓘ The durable record needs no separate assertion here: record_channel and this push are fed the SAME
      //   `app_body`/`app_len` pair from one expression, so the body checked above IS what is inboxed. (This harness
      //   also has no inbox backend attached — chan_newest_seq() is 0 — so an assertion on it would be vacuous.)

    // (b) ★★★ §chan-crypt CL2c — THE SLICE, MEASURED: the SAME post to a receiver that has heard NO BEACON from the
    //     sender is now fully attributed and RETAINED. Under CL2b this arm emitted `peer_location_unattributed`
    //     {unknown_team_peer} and stored nothing — the attribution depended on the receiver's beacon history, which is
    //     exactly the dependency the carried hash removes. ⚠ NOTE THE CONTRAST WITH ARM (a): the only difference
    //     between them is the missing `learn_team_peer` call, so this pair isolates the change to that dependency.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      deliver_sealed(R, m.id, 7, m.body, T);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.has_location);
      CHECK(pu.sender_hash == 0x1003u);                        // read off the SEALED INNER, not off `_team_keys`
      CHECK(R.peer_loc_count() == 1);                          // ★ RETAINED — CL2b stored nothing here
      int32_t la = 0, lo = 0; uint32_t age = 0; Node::PeerLocSrc src = Node::PeerLocSrc::peer;
      CHECK(R.peer_loc_find(0x1003u, la, lo, age, src));
      CHECK(src == Node::PeerLocSrc::team);
      CHECK(hr.count("peer_location_unattributed") == 0); }    // the whole failure MODE is gone, not merely quieter

    // (c) ★★ THE CROSS-CHECK STILL EARNS ITS KEEP, and it is now a SELF-consistency test on one frame: the carried
    //     hash and the msg-id's 16 hash bits both live inside the AEAD envelope and our sender makes them agree by
    //     construction, so a disagreement is a FORGED or CORRUPT post. §chan-crypt CL2c REFUSES it outright — no
    //     channel_recv, nothing inboxed, nothing retained — where CL2b pushed the position and only declined to store
    //     it. Showing an app a position the node itself judges inconsistent is what AB4's rule forbids.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      learn_team_peer(R, /*src=*/3, T, /*advertise_id=*/m.id);
      // Forge a post whose SEALED INNER names 0x1003 while its msg-id carries OTHER hash bits.
      const uint32_t bad_id = (3u << 24) | (0x7777u << 8) | 0x01u;
      uint8_t inner[1 + 4 + 6 + 2];
      inner[0] = static_cast<uint8_t>(protocol::channel_inner_flag_text | protocol::channel_inner_flag_location
                                                                        | protocol::channel_inner_flag_source);
      inner[1] = 0x03; inner[2] = 0x10; inner[3] = 0x00; inner[4] = 0x00;      // 0x00001003 LE
      pack_loc6(kLat, kLon, std::span<uint8_t>(inner + 5, 6));
      inner[11] = 'h'; inner[12] = 'i';
      const std::vector<uint8_t> body = forge_sealed(inner, sizeof inner, bad_id, 7, /*key_fill=*/0x10);
      deliver_sealed(R, bad_id, 7, body, T);
      const auto v = drain_all(R);
      Push pu{};
      CHECK_FALSE(find_push(v, PushKind::channel_recv, pu));   // ★ REFUSED — the app never sees it
      CHECK_FALSE(find_push(v, PushKind::team_channel_no_key, pu));   // we HOLD the key; never prompt for one
      CHECK(R.peer_loc_count() == 0);                          // ★ and nothing retained
      CHECK(R.channel_buffer_count() == 1);                    // ★ STILL BUFFERED -> still relayed (content-blind relay)
      const Ev* e = hr.last("channel_inner_source_mismatch"); CHECK(e != nullptr);
      CHECK(hr.count("channel_inner_malformed") == 0);         // a DISTINCT diagnosis from a structural fault
      // SAME-SITE CONTROL: the identical inner under a msg-id whose bits DO match is accepted at the same site.
      const uint32_t good_id = (3u << 24) | (0x1003u << 8) | 0x02u;
      const std::vector<uint8_t> ok = forge_sealed(inner, sizeof inner, good_id, 7, 0x10);
      deliver_sealed(R, good_id, 7, ok, T);
      CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.sender_hash == 0x1003u);
      CHECK(R.peer_loc_count() == 1); }

    // (c2) ★ A ZERO carried hash is refused by the same gate — it names nobody, so `peer_loc_set` could not key on it
    //      and the app would render an unattributed pin. The send side refuses to originate one; this is the other end
    //      of the same invariant. (Forged: our own sender cannot build it.)
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      const uint32_t zero_id = (3u << 24) | (0x0000u << 8) | 0x01u;   // id bits 0 too, so ONLY the zero test can fire
      uint8_t inner[1 + 4 + 6 + 2] = {};
      inner[0] = static_cast<uint8_t>(protocol::channel_inner_flag_text | protocol::channel_inner_flag_location
                                                                        | protocol::channel_inner_flag_source);
      pack_loc6(kLat, kLon, std::span<uint8_t>(inner + 5, 6));
      inner[11] = 'h'; inner[12] = 'i';
      const std::vector<uint8_t> body = forge_sealed(inner, sizeof inner, zero_id, 7, 0x10);
      deliver_sealed(R, zero_id, 7, body, T);
      Push pu{}; CHECK_FALSE(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(R.peer_loc_count() == 0);
      CHECK(R.channel_buffer_count() == 1);                    // still relayed
      const Ev* e = hr.last("channel_inner_source_mismatch"); CHECK(e != nullptr); }

    // (c3) ★★★ §chan-crypt CL2c — bit1 WITHOUT bit2 is MALFORMED: a position with no sender is exactly what this
    //      slice removes, so it never reaches the app. This is the receive half of the required pairing; the send half
    //      is structural (channel_inner_flags cannot compose it).
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      const uint32_t id2 = (3u << 24) | (0x1003u << 8) | 0x03u;
      uint8_t inner[1 + 6 + 2];                                          // CL2b's OLD wire shape, verbatim
      inner[0] = static_cast<uint8_t>(protocol::channel_inner_flag_text | protocol::channel_inner_flag_location);
      pack_loc6(kLat, kLon, std::span<uint8_t>(inner + 1, 6));
      inner[7] = 'h'; inner[8] = 'i';
      const std::vector<uint8_t> body = forge_sealed(inner, sizeof inner, id2, 7, 0x10);
      deliver_sealed(R, id2, 7, body, T);
      const auto v = drain_all(R);
      Push pu{};
      CHECK_FALSE(find_push(v, PushKind::channel_recv, pu));             // ★ nothing the app could attribute
      CHECK_FALSE(find_push(v, PushKind::team_channel_no_key, pu));      // not a key problem
      CHECK(R.peer_loc_count() == 0);
      CHECK(R.channel_buffer_count() == 1);                              // still relayed
      CHECK(hr.count("channel_inner_malformed") == 1);
      CHECK(hr.count("channel_inner_source_mismatch") == 0);             // structural, not a mismatch
      // SAME-SITE CONTROL: the SAME post with bit2 added (and the 4 bytes present) is accepted at the same site.
      uint8_t ok_inner[1 + 4 + 6 + 2];
      ok_inner[0] = static_cast<uint8_t>(protocol::channel_inner_flag_text | protocol::channel_inner_flag_location
                                                                           | protocol::channel_inner_flag_source);
      ok_inner[1] = 0x03; ok_inner[2] = 0x10; ok_inner[3] = 0x00; ok_inner[4] = 0x00;
      pack_loc6(kLat, kLon, std::span<uint8_t>(ok_inner + 5, 6));
      ok_inner[11] = 'h'; ok_inner[12] = 'i';
      const uint32_t id3 = (3u << 24) | (0x1003u << 8) | 0x04u;
      deliver_sealed(R, id3, 7, forge_sealed(ok_inner, sizeof ok_inner, id3, 7, 0x10), T);
      CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.has_location); CHECK(pu.sender_hash == 0x1003u);
      CHECK(R.peer_loc_count() == 1); }

    // (c4) ★ bit2 WITHOUT bit1 IS LEGAL — a text post that names its sender. Nothing in this node ORIGINATES one (the
    //      composer sets bit2 only beside a position), but the reader accepts it, because forbidding a COMBINATION is
    //      the enum thinking the flags byte exists to avoid. It attributes the TEXT and retains NO position.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      const uint32_t id4 = (3u << 24) | (0x1003u << 8) | 0x05u;
      uint8_t inner[1 + 4 + 2];
      inner[0] = static_cast<uint8_t>(protocol::channel_inner_flag_text | protocol::channel_inner_flag_source);
      inner[1] = 0x03; inner[2] = 0x10; inner[3] = 0x00; inner[4] = 0x00;
      inner[5] = 'h'; inner[6] = 'i';
      deliver_sealed(R, id4, 7, forge_sealed(inner, sizeof inner, id4, 7, 0x10), T);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.enc);
      CHECK(pu.body_len == 2); CHECK(pu.body[0] == 'h'); CHECK(pu.body[1] == 'i');   // the 5-B header consumed
      CHECK(pu.sender_hash == 0x1003u);                        // ★ the text IS attributed
      CHECK_FALSE(pu.has_location);
      CHECK(R.peer_loc_count() == 0);                          // ★ ...and no position is invented for it
      CHECK(hr.count("channel_inner_malformed") == 0);
      CHECK(hr.count("peer_location") == 0); }

    // (d) CONTROL — a TEXT-ONLY sealed post retains NOTHING and sets no position, i.e. the retention is driven by
    //     bit1 and not by "it was sealed". (Also the CL2a behaviour, unchanged.)
    { TkHal h2; h2._fill = 0x41; Node S2(h2, 3, 0x00001003u);
      NodeConfig c2 = team_cfg_loc(T); S2.on_init(c2); give_team_key(S2, 0x10);
      const AiredM t = post_team_and_capture(h2, S2, 7, "no position here", CryptIntent::on);
      CHECK(t.ok); if (!t.ok) return;
      TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10); learn_team_peer(R, 3, T, t.id);
      deliver_sealed(R, t.id, 7, t.body, T);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK_FALSE(pu.has_location);
      CHECK(pu.body_len == std::strlen("no position here"));
      // ★ §chan-crypt CL2c: a TEXT-ONLY post from OUR sender carries NO bit2 — the composer sets it only beside a
      //   position, so a plain sealed post is byte-for-byte what CL2a/CL2b aired and its push names no sender.
      //   (A peer that DOES name one on a text post is accepted; arm (c4) above.)
      CHECK(pu.sender_hash == 0);
      CHECK(R.peer_loc_count() == 0);
      CHECK(hr.count("peer_location") == 0); }

    // (e) A POSITION-ONLY post ("here I am"): the push has an EMPTY body and the position, and the row is retained.
    { TkHal h2; h2._fill = 0x41; Node S2(h2, 3, 0x00001003u);
      NodeConfig c2 = team_cfg_loc(T); S2.on_init(c2); give_team_key(S2, 0x10);
      const AiredM p = post_team_and_capture(h2, S2, 7, "", CryptIntent::on, /*loc=*/true);
      CHECK(p.ok); if (!p.ok) return;
      TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10); learn_team_peer(R, 3, T, p.id);
      deliver_sealed(R, p.id, 7, p.body, T);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.body_len == 0);
      CHECK(pu.has_location);
      CHECK(R.peer_loc_count() == 1); }
}

TEST_CASE("★★ §chan-crypt CL2b — a MALFORMED inner is a CONTENT drop, NOT a key problem: no channel_recv, no key prompt, still relayed") {
    constexpr uint32_t T = 0xABCD1234u;
    const uint32_t id = (3u << 24) | (0x1003u << 8) | 0x01u;
    struct Case { const char* name; std::vector<uint8_t> inner; };
    // Every way an inner can contradict itself. All are UNPRODUCIBLE by our own sender — they are forged here under
    // the RIGHT key so the tag PASSES, which is exactly what separates this class from "cannot decrypt".
    // ★ §chan-crypt CL2c: bit2 (0x04) is now KNOWN, so the unknown-bit probe moved to 0x08, and every case that used
    // to set bit1 alone must now set bit2 with it (bit1 without bit2 is its own refusal, tested in arm (c3) above).
    const std::vector<Case> cases = {
        { "zero-length inner",            {} },
        { "flags == 0",                   { 0x00 } },
        { "unknown bit (a v3 field)",     { 0x09, 'h', 'i' } },
        { "src bit, inner too short",     { 0x04, 1, 2 } },
        { "loc bit, inner too short",     { 0x06, 1, 2, 3, 4, 5, 6, 7 } },
        { "text bit, no text byte",       { 0x01 } },
        { "src+loc, trailing bytes",      { 0x06, 3, 0x10, 0, 0, 1, 2, 3, 4, 5, 6, 'x' } },
        { "src bit only, trailing bytes", { 0x04, 3, 0x10, 0, 0, 'x' } },
    };
    for (const Case& k : cases) {
        TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
        give_team_key(R, 0x10);
        const std::vector<uint8_t> body = forge_sealed(k.inner.data(), k.inner.size(), id, 7, /*key_fill=*/0x10);
        deliver_sealed(R, id, 7, body, T);
        const auto v = drain_all(R);
        Push pu{};
        CHECK_MESSAGE(!find_push(v, PushKind::channel_recv, pu), k.name);          // nothing the app could render
        CHECK_MESSAGE(!find_push(v, PushKind::team_channel_no_key, pu), k.name);   // ★ we HOLD the key — never prompt
        CHECK_MESSAGE(hr.count("channel_inner_malformed") == 1, k.name);
        CHECK_MESSAGE(hr.count("channel_crypt_undecryptable") == 0, k.name);       // the tag PASSED; this is not that
        CHECK_MESSAGE(R.channel_buffer_count() == 1, k.name);                      // ★ STILL BUFFERED -> still relayed
        CHECK_MESSAGE(R.peer_loc_count() == 0, k.name);                            // and nothing half-parsed was stored
    }
    // SAME-SITE CONTROL: the minimal WELL-FORMED inner at the same site opens and delivers.
    { TestHal hr; Node R(hr, 5, 0xAAAA1111u); NodeConfig cr = team_cfg(T); R.on_init(cr);
      give_team_key(R, 0x10);
      const uint8_t ok_inner[2] = { protocol::channel_inner_flag_text, 'x' };
      const std::vector<uint8_t> body = forge_sealed(ok_inner, sizeof ok_inner, id, 7, 0x10);
      deliver_sealed(R, id, 7, body, T);
      Push pu{}; CHECK(find_push(drain_all(R), PushKind::channel_recv, pu));
      CHECK(pu.body_len == 1); CHECK(pu.body[0] == 'x');
      CHECK(hr.count("channel_inner_malformed") == 0); }
}
