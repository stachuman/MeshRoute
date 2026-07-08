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
#include "identity.h"
#include "frame_codec.h"
#include "leaf_config.h"   // R6.1: real config_hash for the peering-filter test
#include "ram_inbox_store.h"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

struct Ev { std::string type; int to = -1; int dst = -1; bool dup = false;
            bool has_payload = false; std::string payload; int depth = -1; int ctr = -1;
            int next = -1; int requeue_count = -1; int reason = -1; int from = -1;
            bool healed = false; bool has_healed = false; };

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
    uint64_t _oldest_tx_end = 0;  // scriptable oldest in-window TX-end (duty_status recovery calc)
    uint64_t oldest_tx_end_ms() override { return _oldest_tx_end; }
    uint64_t now() override { return _now; }
    uint32_t _slop = 0;                                                   // §CTS-wait: settable metal turnaround slop (rx_window_slop_ms)
    uint32_t rx_window_slop_ms(int) const override { return _slop; }
    uint32_t last_after_delay[16] = {};                                   // §CTS-wait: last after() delay per timer id (id<16)
    bool     after(uint32_t d, uint32_t id) override { if (id < 16) last_after_delay[id] = d; return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      _rand_ret = -1;   // opt-in scriptable rand (>=0 overrides the default `return lo`; -1 = default)
    int      rand_range(int lo, int) override { ++rand_calls; return _rand_ret >= 0 ? _rand_ret : lo; }
    // Crypto RNG (DISTINCT from the weak rand_range above, whose default returns `lo`=0). A real HW RNG never
    // returns an all-zero seed; emulate a non-degenerate deterministic stream so the e2e nonce-seed is realistic.
    // zero_rng=true forces all-zero (to exercise the R7 bad-RNG fail-loud guard in e2e_seal_inner).
    bool     zero_rng = false;
    uint8_t  _rb = 0x11;
    void     rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) { _rb = static_cast<uint8_t>(_rb * 31 + 7); o[i] = zero_rng ? 0 : (_rb == 0 ? 0xA5 : _rb); }
    }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            if      (std::strcmp(f[i].key, "to") == 0)  e.to  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dst") == 0) e.dst = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dest") == 0) e.dst = static_cast<int>(f[i].i);  // rt_update uses "dest"
            else if (std::strcmp(f[i].key, "dup") == 0) e.dup = f[i].b;
            else if (std::strcmp(f[i].key, "healed") == 0) { e.healed = f[i].b; e.has_healed = true; }
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
constexpr uint32_t kQueueWakeupTimerId   = 8;   // become_free re-drain (NAV origination-jitter wake)
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
                     uint8_t plen, std::array<uint8_t, 16>& b, uint8_t rts_flags = 0) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = rts_flags; in.payload_len = plen; in.m_payload_id_lo16 = 0;
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_data(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                      const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;   // [origin][body] — no payload-flags byte
    uint8_t bl = 0; while (body[bl]) { inner[1 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA with explicit hop-budget fields (for the HOP_BUDGET enforcement tests).
static size_t mk_data_hb(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                         uint8_t hops_remaining, uint8_t committed,
                         const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;   // [origin][body] — no payload-flags byte
    uint8_t bl = 0; while (body[bl]) { inner[1 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.hops_remaining = hops_remaining; in.committed_hops = committed; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA with explicit flags + TYPE + a raw body (may contain 0 bytes) — for the E2E ACK tests. The inner is
// the normal-unicast shape [origin][body] (no payload-flags byte); `type` rides the byte-8 TYPE byte (APP).
static size_t mk_data_e2e(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin, uint8_t flags,
                          const uint8_t* body, uint8_t body_len, std::array<uint8_t, 64>& b, uint8_t type = 0) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;
    for (uint8_t i = 0; i < body_len; ++i) inner[1 + i] = body[i];
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = flags; in.type = type; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + body_len);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA carrying a DST_HASH inner ([dst_key_hash32 LE 4B][origin][body]) with the DST_HASH header flag set —
// L2c verify-on-delivery. No payload-flags byte; presence is signalled by the byte-1 flag.
static size_t mk_data_dsthash(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                              uint32_t dst_hash, const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 40> inner{};
    inner[0] = static_cast<uint8_t>(dst_hash);        inner[1] = static_cast<uint8_t>(dst_hash >> 8);
    inner[2] = static_cast<uint8_t>(dst_hash >> 16);  inner[3] = static_cast<uint8_t>(dst_hash >> 24);
    inner[4] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[5 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = DATA_FLAG_DST_HASH; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 5 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA carrying an H_ANSWER (hash-bind) inner: resolves hb_key -> hb_node (authoritative=owner). Routed to
// `dst`; do_post_ack consumes it via on_hash_bind_response (drains a parked redirect/send for hb_key).
static size_t mk_data_hashbind(uint8_t next, uint8_t dst, uint16_t ctr,
                               uint8_t hb_node, uint32_t hb_key, bool authoritative,
                               std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 16> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = hb_node; hb.key_hash32 = hb_key;   // 6-B inner; authoritative via TYPE
    const size_t il = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.type = authoritative ? DATA_TYPE_AUTHORITATIVE_H_ANSWER : DATA_TYPE_H_ANSWER;   // H_ANSWER rides the frame TYPE
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), il);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// §1b CRYPTED DATA: [dst_hash LE 4][origin][body] inner + the CRYPTED flag + an 8-B nonce-seed TRAILER (which
// IS the dedup key after §1b). The inner body is a stand-in: the dedup runs in handle_data BEFORE any open, and
// a forwarder (dst != self) re-tx's a sealed frame verbatim without ever opening it — so no valid seal is needed
// to exercise the dedup/loop path. CRYPTED requires DST_HASH (pack_data rejects CRYPTED && !DST_HASH).
static size_t mk_data_crypted(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin, uint32_t dst_hash,
                              const uint8_t seed8[8], const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 40> inner{};
    inner[0] = uint8_t(dst_hash);       inner[1] = uint8_t(dst_hash >> 8);
    inner[2] = uint8_t(dst_hash >> 16); inner[3] = uint8_t(dst_hash >> 24);
    inner[4] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[5 + bl] = uint8_t(body[bl]); ++bl; }
    data_in in{}; in.addr_len = 0; in.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 5 + bl);
    in.mac = std::span<const uint8_t>(seed8, 8);                  // the 8-B nonce-seed (conditional MAC trailer)
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

TEST_CASE("R3 dedup — seen-origins ROLLS (evict oldest) at the 256 cap instead of refusing the new key") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);

    // Fill to the cap with DISTINCT keys recorded at INCREASING times -> key `base+0` is the oldest (min expiry).
    const uint32_t base = 0x01020300u;
    for (uint16_t i = 0; i < protocol::cap_seen_origins; ++i)
        node.record_seen_origin(base + i, /*from=*/2, /*now=*/uint64_t(1000 + i));
    CHECK(node.seen_origin_count() == protocol::cap_seen_origins);   // 256, all live (TTL 30s)
    CHECK(node.seen_origin_live(base + 0, /*now=*/2000));            // the oldest is present (not expired)

    // One more NEW key past the cap -> ROLL: the oldest (base+0) is evicted, the new key stored, count stays 256.
    const uint32_t fresh = base + protocol::cap_seen_origins;
    node.record_seen_origin(fresh, /*from=*/3, /*now=*/uint64_t(1000 + protocol::cap_seen_origins));
    CHECK(node.seen_origin_count() == protocol::cap_seen_origins);   // STILL 256 — rolled, not grown, not refused
    CHECK_FALSE(node.seen_origin_live(base + 0, /*now=*/2000));      // the oldest was evicted...
    CHECK(node.seen_origin_live(fresh, /*now=*/2000));              // ...and the NEW key IS recorded (the fix)
    CHECK(node.seen_origin_live(base + 1, /*now=*/2000));          // only ONE evicted — the 2nd-oldest survived
}

TEST_CASE("R3 receiver — RTS -> CTS -> DATA -> delivered (we are the destination)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
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

// Inbox integration (persistent-inbox spec §12): a delivered DM lands in the DM store AND the push ring,
// with consistent fields (both are written from the same post-ACK state in do_post_ack).
TEST_CASE("inbox integration — a delivered DM is recorded durably + pushed, fields consistent") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    node.inbox().on_init(&dm, &ch);                          // a backend installs durable stores
    CHECK(node.inbox().enabled());
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };  // immediate sender = bob(1)

    std::array<uint8_t, 16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb), meta);
    std::array<uint8_t, 64> db{};
    hal._now = 2000; node.on_recv(db.data(), mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/0, "hi", db), meta);
    node.on_timer(kPostAckTimerId);                          // do_post_ack: msg_recv push + record_dm together

    // 1) the live push ring received the DM
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);

    // 2) the durable inbox recorded exactly one DM, consistent with that push
    CHECK(dm.count() == 1);
    CHECK(ch.count() == 0);                                  // a DM does not touch the channel store
    struct Got { bool seen; InboxKind kind; uint8_t origin; uint32_t msg_id; std::string body; } g{ false, InboxKind::channel, 0, 0, "" };
    node.inbox().pull(0, 0, [](void* c, const InboxEntry& e) -> bool {
        auto* x = static_cast<Got*>(c);
        x->seen = true; x->kind = e.kind; x->origin = e.origin; x->msg_id = e.msg_id;
        x->body.assign(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len);
        return true;
    }, &g);
    CHECK(g.seen);
    CHECK(g.kind == InboxKind::dm);
    CHECK(g.body == "hi");                                   // the delivered content
    if (got) { CHECK(g.origin == pu.origin); CHECK(g.msg_id == pu.ctr);   // DM msg_id == the ctr; same source (do_post_ack)
               CHECK(g.body == std::string(reinterpret_cast<const char*>(pu.body), pu.body_len)); }
}

TEST_CASE("R3 dedup — retried RTS within last_acked TTL -> already_received CTS; past TTL -> fresh CTS") {
    TestHal hal; Node node(hal, 2, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
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
    // ACK completes flight #1 -> become_free re-drains. Advance past dm_min_interval_ms from msg-a (now=2000)
    // so msg-b (an own DM) clears the Slice 3 burst floor and its RTS issues.
    hal._now = 5200; node.on_recv(ab.data(), an, bob);
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

TEST_CASE("§CTS-wait metal slop: start_rts_timeout adds 2*rx_window_slop_ms (metal turnaround); inert at slop=0") {
    // The CTS round-trip crosses TWO radio turnarounds (sender TX->RX + gateway RX->TX). start_rts_timeout must add
    // 2*rx_window_slop_ms — 0 on the sim/native HAL so the delay is UNCHANGED (native + s18 byte-identical), ~53ms/turnaround on metal.
    auto arm_cts_wait = [](uint32_t slop) -> uint32_t {
        TestHal hal; hal._slop = slop;
        Node node(hal, /*id=*/1, /*key=*/0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 7); cfg.leaf_id = 0;
        node.on_init(cfg);
        node.route_inject(/*dest*/ 20, /*next_hop*/ 20, /*hops*/ 1, /*score*/ 100);   // a direct route -> the send RTSes
        send_cmd(node, /*dst*/ 20, "hi");                                             // originate -> RTS -> start_rts_timeout arms kRtsTimeoutTimerId (4)
        return hal.last_after_delay[kRtsTimeoutTimerId];
    };
    const uint32_t d0 = arm_cts_wait(0);
    const uint32_t dK = arm_cts_wait(37);
    CHECK(d0 > 0);                          // the CTS-wait IS armed (base<<shift + 1)
    CHECK(dK == d0 + 2u * 37u);             // ★ the fix: +2 turnarounds of slop; slop==0 -> inert -> native + s18 unchanged
}

TEST_CASE("§per-layer-id: single-layer canonical_node_id() == node_id() (persist unchanged)") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    CHECK_FALSE(node.config().is_gateway);                 // single-layer node (not a gateway)
    CHECK(node.canonical_node_id() == node.node_id());     // single-layer persists _node_id (the current/DAD-adopted id) — UNCHANGED
    CHECK(node.canonical_node_id() == 7);
}

// ---- Cascade-to-alt walk + no-route defer+Q (the cascade milestone) --------
// Seed a sender (alice=1) with K candidates to dest=5 via distinct next-hops,
// ordered by hops so the candidate order is unambiguous (no score tie).
static Node* mk_sender_with_routes(TestHal& hal, std::vector<std::array<uint8_t,3>> vias) {
    // each via = {next_hop_src, hops_advertised, score_bucket}
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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

// ===== Slice 6: slow-reprobe interception on a one-way sole route =====
// A one-way next-hop stays liveness-HEALTHY (its beacons keep arriving) so §P3 never
// fires on it; without the bidi interception the no-alt giveup would burst into the
// 9-80-RTS try_cascade_requeue. The interception throttles to ONE RTS per
// link_reprobe_ttl_ms while still flying the single sole-route probe.
TEST_CASE("bidi reprobe — a one-way sole route fires its FIRST probe immediately (clock starts at 0)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to dst 5 via next-hop 2
    // Mark next-hop 2 one-way: advertiser 2's complete heard-set OMITS self(1) -> Slice 3 detection sets one_way.
    node->test_update_link_bidi_from_beacon(/*advertiser=*/2, /*entries=*/nullptr, /*n=*/0, /*complete=*/true);
    hal._now = 5000;
    send_cmd(*node, /*dst=*/5, "hi");
    const int rts_before = hal.count("rts_tx");
    exhaust_rts_same_hop(*node);                           // no alt -> one-way interception
    CHECK(hal.count("link_reprobe") == 1);                 // the single throttled probe fired
    // exhaust_rts_same_hop fires 3 same-hop retry RTSs before the cascade; the interception then adds
    // exactly ONE probe RTS (+4 total). The load-bearing point: ONE probe, no try_cascade_requeue burst.
    CHECK(hal.count("rts_tx") == rts_before + 4);          // 3 retries + the single probe, no burst
    CHECK(hal.count("cascade_requeue") == 0);              // the burst requeue was suppressed
    delete node;
}

TEST_CASE("bidi reprobe — one probe per link_reprobe_ttl_ms; non-one-way keeps the legacy requeue burst") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2
    node->test_update_link_bidi_from_beacon(/*advertiser=*/2, nullptr, 0, /*complete=*/true);  // 2 -> one_way
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);                           // probe #1
    CHECK(hal.count("link_reprobe") == 1);
    CHECK(hal.count("cascade_requeue") == 0);              // NO burst
    // A second giveup WITHIN the TTL window must NOT re-probe (throttled, clean giveup, no burst).
    hal._now = 1000 + protocol::link_reprobe_ttl_ms - 1;
    send_cmd(*node, 5, "hi2");
    exhaust_rts_same_hop(*node);
    CHECK(hal.count("link_reprobe") == 1);                 // STILL 1 -> throttled
    CHECK(hal.count("cascade_requeue") == 0);              // still no burst
    // A giveup AFTER the TTL probes again.
    hal._now = 1000 + protocol::link_reprobe_ttl_ms + 1;
    send_cmd(*node, 5, "hi3");
    exhaust_rts_same_hop(*node);
    CHECK(hal.count("link_reprobe") == 2);                 // window elapsed -> a fresh probe

    // CONTROL: a sole route whose next-hop is NOT one_way still takes the legacy requeue burst (no regression).
    TestHal hal2;
    Node* n2 = mk_sender_with_routes(hal2, {{2,1,14}});    // 2 left unknown (never marked one_way)
    hal2._now = 1000;
    send_cmd(*n2, 5, "hi");
    exhaust_rts_same_hop(*n2);
    CHECK(hal2.count("link_reprobe") == 0);                // bidi plane not engaged
    CHECK(hal2.count("cascade_requeue") == 1);             // legacy burst path intact
    delete node; delete n2;
}

TEST_CASE("bidi reprobe — the single probe flies, a CTS recovers (confirmed + degraded cleared + link_recover)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2
    node->test_update_link_bidi_from_beacon(/*advertiser=*/2, nullptr, 0, /*complete=*/true);  // 2 -> one_way
    // Sanity: the sole candidate to dst 5 reads degraded while 2 is one_way.
    // rt_find is private — locate the entry via the public rt_count()/rt_at() seams.
    auto find_rt = [&](uint8_t dest) -> const RtEntry* {
        for (uint8_t i = 0; i < node->rt_count(); ++i) if (node->rt_at(i).dest == dest) return &node->rt_at(i);
        return nullptr;
    };
    const RtEntry* e = find_rt(5);
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->n == 1);
        if (e->n == 1) CHECK(node->candidate_degraded(e->candidates[0]) == true);
    }
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    const int rts_before = hal.count("rts_tx");
    exhaust_rts_same_hop(*node);                           // one-way interception -> ONE probe RTS to 2
    // +4 = 3 same-hop retries (exhaust_rts_same_hop) + the single lucky-marginal probe (no burst).
    CHECK(hal.count("rts_tx") == rts_before + 4);          // the probe actually flew (its rts_tx is the +1 over the retries)
    const Ev* probe = hal.last("rts_tx");
    CHECK(probe != nullptr);
    if (probe) CHECK(probe->next == 2);
    // The probe gets a real CTS from next-hop 2 -> recovery.
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);                      // CTS matched -> note_link_confirmed(2)
    CHECK(hal.count("link_recover") == 1);                 // it WAS one_way -> recovery emitted
    const RtEntry* e2 = find_rt(5);
    CHECK(e2 != nullptr);
    if (e2) {
        CHECK(e2->n == 1);
        if (e2->n == 1) CHECK(node->candidate_degraded(e2->candidates[0]) == false);   // recompute is live -> degraded cleared
    }
    delete node;
}

TEST_CASE("bidi reprobe — §P3 liveness-silent path is orthogonal (RREQ + requeue, no link_reprobe)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2; 2 stays _link_bidi=unknown
    send_cmd(*node, 5, "hi");
    // Drive next-hop 2 to liveness-SILENT (>= peer_silent_penalty_q4) WITHOUT touching the bidi plane.
    // mark_neighbor_silent_for_test does not exist; use the real liveness path (3 same-hop giveups -> SILENT,
    // the proven §P3 idiom). peer_penalty_q4 is the public read accessor for the private liveness_penalty_q4.
    node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9);
    CHECK(node->peer_penalty_q4(2) >= protocol::peer_silent_penalty_q4);   // 2 is now SILENT
    const int rreq_before = hal.count("r_tx");             // §P3 RREQ event is r_tx (emit_route_request)
    node->on_timer(kRtsTimeoutTimerId);                    // silent + no alt -> §P3 RREQ + legacy requeue
    CHECK(hal.count("link_reprobe") == 0);                 // bidi plane NOT engaged (2 is unknown, not one_way)
    CHECK(hal.count("r_tx") > rreq_before);                // §P3 RREQ fired (orthogonal, unaffected)
    CHECK(hal.count("cascade_requeue") == 1);              // legacy requeue path intact
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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

// =============================================================================
// §1b sealed-sender — the CRYPTED dedup key is the 8-B nonce-seed, NOT the
// cleartext (origin,dst,ctr). After §1c seals `origin` the relay can no longer
// read it; the seed (globally unique per message, preserved verbatim on
// forward) is the flight id. These pin that BEFORE 1c so the wire change is a
// one-liner. The DISTINGUISHING test (same header, different seed) is the RED
// driver: under the old origin-keyed dedup copy-2 would false-LOOP_DUP.
// =============================================================================
TEST_CASE("§1b CRYPTED dedup keys on the seed — same (origin,dst,ctr) but a DIFFERENT seed is a DISTINCT flight") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};                         // a route to dst=5 so the forwarder forwards (dedup is pre-forward anyway)
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    const uint8_t S1[8] = { 0xAA,0x01,0x02,0x03, 0x04,0x05,0x06,0x07 };
    const uint8_t S2[8] = { 0xBB,0x11,0x12,0x13, 0x14,0x15,0x16,0x17 };   // DIFFERENT seed, SAME (origin,dst,ctr)
    // copy 1 via prev-hop 2: a CRYPTED forward (origin 0, dst 5, ctr 10), seed S1 -> accepted + records seen(seed1, from=2)
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,18,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_crypted(1,5,10,/*origin=*/0,/*dst_hash=*/0xDEADBEEFu, S1, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("ack_tx") >= 1);
    CHECK(node.seen_origin_count() == 1);                // one flight recorded so far
    const int nack_before = hal.count("nack_tx");
    const int dup_before  = hal.count("dup_drop");
    // copy 2 via prev-hop 3: SAME (origin,dst,ctr) but a DIFFERENT seed S2 -> a SEPARATE message -> NOT a loop-dup.
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,10,18,rb); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S2, "x", db); node.on_recv(db.data(), dn, m3); }
    CHECK(hal.count("nack_tx") == nack_before);          // NO LOOP_DUP — the seed differs (RED before 1b: origin-key would loop)
    CHECK(hal.count("dup_drop") == dup_before);          // not dropped as a dup
    CHECK(node.seen_origin_count() == 2);                // a SECOND distinct flight recorded (vs the retransmit's 1) — the seed IS the key
}

TEST_CASE("§1b CRYPTED loop detection: the SAME seed via a DIFFERENT prev-hop -> LOOP_DUP NACK") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    const uint8_t S[8] = { 0xC0,0xC1,0xC2,0xC3, 0xC4,0xC5,0xC6,0xC7 };
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};                      // copy 1 via prev-hop 2 -> accepted, records seen(seed, from=2)
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,18,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("ack_tx") >= 1);
    const int ack_before = hal.count("ack_tx");
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};                      // copy 2: the SAME seed (a forwarded loop) via prev-hop 3 -> LOOP_DUP
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,10,18,rb); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m3); }
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == 3); }               // nack_reason_loop_dup
    CHECK(hal.count("dup_drop") >= 1);
    CHECK(hal.count("ack_tx") == ack_before);                            // the loop was NOT re-ACKed
}

TEST_CASE("§1b CRYPTED retransmit: the SAME seed is a live dup (ONE flight entry), NOT a new flight or a LOOP_DUP") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    const uint8_t S[8] = { 0xD0,0xD1,0xD2,0xD3, 0xD4,0xD5,0xD6,0xD7 };
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    // copy 1 (seed S via prev-hop 2, pl=18) -> accepted, records exactly ONE seen-origin entry (the seed-key).
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,18,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(node.seen_origin_count() == 1);
    const int nack_before = hal.count("nack_tx");
    // copy 2: the SAME seed via the SAME prev-hop 2. The RTS uses a DIFFERENT payload_len (20) so the last-acked cache
    // MISSES (its key includes payload_len) and the DATA actually REACHES the seed-dedup — otherwise the RTS layer
    // absorbs the retry and the dedup never runs (the vacuity the review caught). SAME prev-hop => benign, not a loop.
    hal._now = 2000; { const size_t rn = mk_rts(2,1,5,10,20,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 2100; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(node.seen_origin_count() == 1);               // DEDUPED on the seed: NO new flight entry (a broken seed-key => 2)
    CHECK(hal.count("nack_tx") == nack_before);         // same prev-hop => benign dup, NOT a LOOP_DUP
}

// The CRYPTED seed key and the plaintext (origin,dst,ctr) key share ONE _seen_origins map. They must NEVER alias:
// CRYPTED keys are namespaced into [2^63, 2^64) (top bit forced) and plaintext into [0, 2^32). A crafted seed whose
// low bytes equal a live plaintext key must NOT be mistaken for that flight.
TEST_CASE("§1b cross-type non-collision: a CRYPTED seed aliasing a plaintext (origin,dst,ctr) is a DISTINCT flight") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // a PLAINTEXT flight: origin=0x12, dst=5, ctr=0x5678 -> plaintext sokey = (0x12<<24)|(5<<16)|0x5678 = 0x12055678.
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,/*ctr_lo=*/8,18,rb); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data(1,5,/*ctr=*/0x5678,/*origin=*/0x12,"x",db); node.on_recv(db.data(), dn, m2); }
    CHECK(node.seen_origin_count() == 1);
    const int nack_before = hal.count("nack_tx");
    // a CRYPTED frame whose 8-B seed's LOW 4 bytes (LE) == 0x12055678 and high 4 == 0 -> the OLD 32-bit fold collides
    // with the plaintext key; the 64-bit type-namespaced key does NOT.
    const uint8_t S[8] = { 0x78,0x56,0x05,0x12, 0x00,0x00,0x00,0x00 };
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 2000; { const size_t rn = mk_rts(3,1,5,/*ctr_lo=*/8,18,rb); node.on_recv(rb.data(), rn, m3); }
    hal._now = 2100; { const size_t dn = mk_data_crypted(1,5,/*ctr=*/0x5678,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m3); }
    CHECK(hal.count("nack_tx") == nack_before);         // NO false LOOP_DUP — disjoint namespaces (RED under the 32-bit fold)
    CHECK(node.seen_origin_count() == 2);               // recorded as a DISTINCT flight, not aliased onto the plaintext one
}

// =============================================================================
// S1 (2026-07-04) — txitem_from_pending: the ONE place a TxItem is rebuilt from
// an in-flight PendingTx on requeue. These lock the field-drop class shut: the
// H4/M7 bugs were requeue sites that FORGOT `type` (a typed frame re-flown as a
// junk plain DM) or the 8-B CRYPTED `nonce_seed` (a sealed DM re-flown with a
// zero seed -> recipient Poly1305 tag-fail -> hard delivery loss under exactly
// the congestion that triggers a requeue). RED before the helper existed.
// =============================================================================
TEST_CASE("S1 helper — txitem_from_pending preserves a CRYPTED nonce_seed (not zeroed) across a requeue") {
    PendingTx pt{};
    pt.origin = 0x12; pt.dst = 5; pt.ctr = 0x5678; pt.ctr_lo = 8;
    pt.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH;
    const uint8_t seed[8] = { 0xDE,0xAD,0xBE,0xEF, 0x01,0x02,0x03,0x04 };   // non-zero: a real XChaCha nonce seed
    for (int i = 0; i < 8; ++i) pt.nonce_seed[i] = seed[i];
    pt.inner[0] = 0xAA; pt.inner[1] = 0xBB; pt.inner_len = 2;

    const TxItem it = txitem_from_pending(pt);

    bool seed_ok = true;
    for (int i = 0; i < 8; ++i) if (it.nonce_seed[i] != seed[i]) seed_ok = false;
    CHECK(seed_ok);                                     // the H4 drop: seed survives -> recipient can still open the DM
    CHECK(it.flags == (DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH));
    // sanity: the seed is genuinely non-zero (guards against a test that would pass on an all-zero copy)
    bool any_nonzero = false;
    for (int i = 0; i < 8; ++i) if (it.nonce_seed[i] != 0) any_nonzero = true;
    CHECK(any_nonzero);
}

TEST_CASE("S1 helper — txitem_from_pending preserves a typed frame's DataType across a requeue") {
    PendingTx pt{};
    pt.origin = 3; pt.dst = 9; pt.ctr = 0x1111; pt.ctr_lo = 4;
    pt.type = DATA_TYPE_E2E_ACK;                        // a typed frame (ack/response) — NOT a plain DM
    pt.flags = DATA_FLAG_APP;
    pt.inner[0] = 0x11; pt.inner[1] = 0x00; pt.inner_len = 2;

    const TxItem it = txitem_from_pending(pt);

    CHECK(it.type == DATA_TYPE_E2E_ACK);                // the M7 drop: NOT downgraded to 0 (a junk plain DM, ack lost)
    CHECK(it.type != 0);
}

TEST_CASE("S1 helper — txitem_from_pending copies the full identity + hop-budget core") {
    PendingTx pt{};
    pt.origin = 7; pt.dst = 21; pt.ctr = 0x9ABC; pt.ctr_lo = 12; pt.flags = 0x40; pt.type = 5;
    pt.has_previous_hop = true; pt.previous_hop = 42;   // a relayed item -> is_forward + previous_hop
    pt.is_gw_relay = true;                              // a cross-layer relay keeps RTS_FLAG_RELAY on the requeue
    pt.fwd_remaining = 6; pt.fwd_committed = 2;         // the carried hop budget
    pt.inner[0] = 1; pt.inner[1] = 2; pt.inner[2] = 3; pt.inner_len = 3;
    const uint8_t seed[8] = { 9,8,7,6,5,4,3,2 };
    for (int i = 0; i < 8; ++i) pt.nonce_seed[i] = seed[i];

    const TxItem it = txitem_from_pending(pt);

    CHECK(it.origin == 7); CHECK(it.dst == 21); CHECK(it.ctr == 0x9ABC); CHECK(it.ctr_lo == 12);
    CHECK(it.flags == 0x40); CHECK(it.type == 5);
    CHECK(it.is_forward == true); CHECK(it.previous_hop == 42);   // has_previous_hop -> is_forward
    CHECK(it.is_gw_relay == true);
    CHECK(it.fwd_remaining == 6); CHECK(it.fwd_committed == 2);
    CHECK(it.inner_len == 3);
    bool inner_ok = it.inner[0] == 1 && it.inner[1] == 2 && it.inner[2] == 3;
    CHECK(inner_ok);
    bool seed_ok = true; for (int i = 0; i < 8; ++i) if (it.nonce_seed[i] != seed[i]) seed_ok = false;
    CHECK(seed_ok);
    // the site meta is NOT copied by the helper (the caller applies it) -> defaults hold here
    CHECK(it.requeue_count == 0); CHECK(it.enqueue_time_ms == 0); CHECK(it.next_attempt_ms == 0);
}

// =============================================================================
// Phase 0 (routing-liveness-plane port) — id==0 / 0-sentinel hardening. An
// UNPROVISIONED node (id 0) and the reserved id 0 must NEVER enter routing:
// id-0 nodes don't beacon, src-0 beacons are dropped, dest-0/via-0 candidates
// are rejected. (s18 has no id 0 -> these guards are inert -> byte-identical.)
// =============================================================================
TEST_CASE("§P0 — an id==0 (unprovisioned) node emits NO beacon (the broad emit_beacon guard)") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0x0000ABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; cfg.lbt_enabled = false;
    node.on_init(cfg);
    // learn a route from a VALID sender -> schedules a triggered beacon -> firing it reaches emit_beacon directly
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(7)});
    hal.tx_frames.clear();
    node.on_timer(kTriggeredBeaconTimerId);              // the triggered path bypasses periodic_beacon_fire's join guard
    node.on_timer(kBeaconTimerId);
    int n_bcn = 0; for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x0) ++n_bcn;
    CHECK(n_bcn == 0);                                   // an id==0 node never advertises routes
}
TEST_CASE("§P0 — a received BCN with src==0 is DROPPED (no route learned from the sentinel)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.rt_count() == 0);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(/*src=*/0, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(0)});
    CHECK(node.rt_count() == 0);                         // src==0 dropped: no direct route to 0, no DV route via next_hop 0
}
TEST_CASE("§P0 — a beacon ROUTE-ENTRY with dest==0 is rejected (never a route to the sentinel)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/0, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(7)});
    bool route_to_0 = false, route_to_7 = false;
    for (uint8_t i = 0; i < node.rt_count(); ++i) { if (node.rt_at(i).dest == 0) route_to_0 = true; if (node.rt_at(i).dest == 7) route_to_7 = true; }
    CHECK_FALSE(route_to_0);                             // the dest==0 entry was skipped
    CHECK(route_to_7);                                   // sanity: the DIRECT route to the sender(7) still learned (dest-specific guard, not a blanket drop)
}

// =============================================================================
// Phase 1 (routing-liveness port) — local liveness STATE. RTS/ACK-timeout
// giveups accumulate into suspect(1)/silent(3)/dead(6-over-15min) tiers; a frame
// heard from a peer clears it; dest_seen drives is_next_hop_fresh. DETECTION
// ONLY — not yet applied to routing (Phase 2). (state-only + new telemetry.)
// =============================================================================
TEST_CASE("§P1 peer-liveness STATE — timeout tiers (suspect/silent/dead) + clear + freshness") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    hal._now = 1000;
    CHECK(node.peer_suspect_level(9) == 0);                              // unknown -> healthy
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 1);   // 1 timeout -> SUSPECT (a full RTS-giveup is enough evidence to deprioritise)
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 1);   // 2 -> still SUSPECT (SILENT only at 3)
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 2);   // 3 -> SILENT
    node.clear_peer_suspect(9, "rx_frame"); CHECK(node.peer_suspect_level(9) == 0);   // heard from 9 -> cleared
    for (int i = 0; i < 6; ++i) node.record_peer_rts_timeout(9, 0);      // 6 timeouts, all at t=1000 -> evidence window NOT elapsed
    CHECK(node.peer_suspect_level(9) == 2);                              // -> SILENT, not yet DEAD
    hal._now = 1000 + protocol::peer_dead_evidence_window_ms + 1;        // past the 15-min evidence window
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 3);   // -> DEAD
    node.clear_peer_suspect(9, "rx_frame"); CHECK(node.peer_suspect_level(9) == 0);   // a clear resets even DEAD
    node.record_peer_rts_timeout(5, 0); CHECK(node.peer_suspect_level(5) == 0);   // self is never tiered
    node.record_peer_rts_timeout(0, 0); CHECK(node.peer_suspect_level(0) == 0);   // the 0 sentinel is never tiered
    // FRESHNESS (is_next_hop_fresh — defined, NOT consulted by routing in P1)
    hal._now = 5000000; node.mark_dest_seen(11);
    CHECK(node.is_next_hop_fresh(11));                                   // just seen -> fresh
    CHECK_FALSE(node.is_next_hop_fresh(99));                             // never seen -> not fresh
    CHECK(node.is_next_hop_fresh(5));                                    // self -> always fresh
    hal._now = 5000000 + protocol::next_hop_live_ttl_ms + 1;            // >20 min unseen
    CHECK_FALSE(node.is_next_hop_fresh(11));                             // gone stale
}
TEST_CASE("§seen-bitmap re-port — dest_seen freshness survives for >cap_peer_liveness distinct dests (dedicated map, no LRU loss)") {
    // The Lua dest_seen_ms is an unbounded node_id->ms map; the C++ used to piggyback it on the bounded
    // 64-entry PeerLiveness LRU table, so freshness for the (cap+1)-th..Nth distinct dest was EVICTED — which
    // starved the seen-bitmap benefit (gossip-only peers couldn't stay fresh). The dedicated _dest_seen_ms[256]
    // map has NO eviction: every marked dest stays fresh.
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    hal._now = 1000000;
    constexpr int kN = 100;                                  // > cap_peer_liveness (64)
    for (int id = 10; id < 10 + kN; ++id) node.mark_dest_seen(static_cast<uint8_t>(id));
    // ALL must read fresh — the bounded PeerLiveness LRU would have lost the first (kN - cap) of them.
    for (int id = 10; id < 10 + kN; ++id)
        CHECK(node.is_next_hop_fresh(static_cast<uint8_t>(id)));
    hal._now = 1000000 + protocol::next_hop_live_ttl_ms + 1;  // all age out together past the TTL
    for (int id = 10; id < 10 + kN; ++id)
        CHECK_FALSE(node.is_next_hop_fresh(static_cast<uint8_t>(id)));
}
TEST_CASE("§P1 liveness LRU — eviction keeps a DEAD peer over a healthy one (asymmetric-link safety)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    // fill the table to cap with healthy peers (ids 10.., node 10 stamped FIRST -> oldest dest_seen -> the OLD policy's first victim)
    for (int i = 0; i < protocol::cap_peer_liveness; ++i) { hal._now = 1000 + i; node.mark_dest_seen(static_cast<uint8_t>(10 + i)); }
    // make node 10 DEAD (6 timeouts spanning the evidence window)
    hal._now = 2000; for (int i = 0; i < 6; ++i) node.record_peer_rts_timeout(10, 0);
    hal._now = 2000 + protocol::peer_dead_evidence_window_ms + 1; node.record_peer_rts_timeout(10, 0);
    CHECK(node.peer_suspect_level(10) == 3);
    // overflow: a NEW peer arrives -> table full -> eviction. The fix evicts a HEALTHY slot, NOT the dead one.
    node.mark_dest_seen(200);
    CHECK(node.peer_suspect_level(10) == 3);   // the DEAD peer SURVIVED (old min-dest_seen policy would have evicted it -> 0)
    CHECK(node.is_next_hop_fresh(200));        // the new peer got in (a healthy slot made room)
}

// =============================================================================
// Phase 2 (routing-liveness port) — APPLY the liveness penalty + freshness gate.
// A demoted (suspect/silent/dead) next-hop loses effective_score; a stale one is
// non-viable; on a tier change the routes via it are re-ranked -> traffic reroutes
// to a fresh alt. THIS IS BEHAVIORAL (the byte-identical gate gives way to the
// delivery suite). Recovery (a frame heard) restores the route.
// =============================================================================
namespace { int rt_primary_for(Node& n, uint8_t dest) {   // the current primary next-hop for `dest`, or -1
    for (uint8_t i = 0; i < n.rt_count(); ++i) if (n.rt_at(i).dest == dest) return n.rt_at(i).candidates[0].next_hop;
    return -1; } }
TEST_CASE("§P2 — a DEMOTED (silent) next-hop loses primacy to a fresh alt, and RECOVERS on a heard frame") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    hal._now = 1000; { const size_t n = mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb); node.on_recv(bb.data(), n, RxMeta{22.0f,-60.0f,0,static_cast<int8_t>(2)}); }
    hal._now = 1001; { const size_t n = mk_beacon_route(/*src=*/3, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }
    CHECK(rt_primary_for(node, 5) == 2);                    // strong link via 2 wins initially
    hal._now = 2000;                                        // node 2 goes SILENT (3 RTS-timeout giveups -> 40 dB penalty)
    node.record_peer_rts_timeout(2, 0); node.record_peer_rts_timeout(2, 0); node.record_peer_rts_timeout(2, 0);
    CHECK(node.peer_suspect_level(2) == 2);
    CHECK(rt_primary_for(node, 5) == 3);                    // §P2: the tier-promotion resort rerouted to the fresh alt via 3
    hal._now = 3000; node.clear_peer_suspect(2, "rx_frame");   // a frame heard from 2 -> it's alive -> clear
    CHECK(node.peer_suspect_level(2) == 0);
    CHECK(rt_primary_for(node, 5) == 2);                    // recovered -> the strong link regains primacy (clear re-ranked)
}
TEST_CASE("§P2 — a STALE next-hop (unseen > next_hop_live_ttl) is non-viable, loses to a fresh alt") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    hal._now = 1000; { const size_t n = mk_beacon_route(2, 5, 9, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{22.0f,-60.0f,0,static_cast<int8_t>(2)}); }   // strong via 2
    hal._now = 1001; { const size_t n = mk_beacon_route(3, 5, 9, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }    // weak via 3
    CHECK(rt_primary_for(node, 5) == 2);
    // advance past next_hop_live_ttl with NO frame from node 2 (-> stale); node 3 re-beacons (stays fresh) -> re-sort
    hal._now = 1000 + protocol::next_hop_live_ttl_ms + 1;
    { const size_t n = mk_beacon_route(3, 5, 9, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }   // keeps node 3 fresh
    CHECK_FALSE(node.is_next_hop_fresh(2));                 // node 2 went stale
    node.rt_resort_for_pick(5);                             // freshness is a PICK-TIME gate (Lua) -> force the re-sort a send would do
    CHECK(rt_primary_for(node, 5) == 3);                    // §P2: stale via-2 non-viable -> fresh via-3 wins (pure freshness, no penalty)
}

// =============================================================================
// Phase 3 (routing-liveness port) — the silent-next CASCADE reaction at the sender.
// A flight whose primary next-hop is ALREADY known silent/dead (prior-flight
// evidence) does NOT burn same-hop retries on the dead path — it cascades to a
// viable alt on the FIRST timeout (or, with no alt, fires an RREQ to actively
// rediscover, closing the user's no-alt dead-relay bug). Reads the persisted
// liveness tier; NO per-timeout counting (that churned the suite +17.8% events).
// DRIFT from the spec's literal per-failure/suspect trigger — see the phase report.
// =============================================================================
TEST_CASE("§P3 — a flight on an ALREADY-silent primary cascades to the alt on the FIRST timeout (no dead-path retries)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // via2 (h2) primary, via3 (h3) alt
    send_cmd(*node, /*dst=*/5, "hi");
    const Ev* r1 = hal.last("rts_tx"); CHECK(r1 != nullptr);
    if (r1) CHECK(r1->next == 2);                       // first RTS to the primary (via 2)
    // node 2 goes SILENT from OTHER evidence (3 prior giveups) WHILE this flight is in-air on it
    node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9);
    CHECK(node->peer_suspect_level(2) == 2);            // SILENT
    const int rand_before = hal.rand_calls;
    node->on_timer(kRtsTimeoutTimerId);                // FIRST timeout: primary is silent -> cascade NOW (don't retry)
    CHECK(hal.count("path_cascade") == 1);             // cascaded on the FIRST timeout (vs after 3 retries)
    const Ev* pc = hal.last("path_cascade"); if (pc) { CHECK(pc->next == 3); CHECK(pc->dst == 5); }
    const Ev* r2 = hal.last("rts_tx"); if (r2) CHECK(r2->next == 3);   // re-RTS on the fresh alt (via 3)
    CHECK(hal.count("rts_giveup") == 0);               // walked, did not give up
    CHECK(hal.rand_calls - rand_before == 0);          // NO same-hop retry-jitter draw (didn't retry the dead path)
    delete node;
}
TEST_CASE("§P3 — a silent SINGLE-candidate primary fires an RREQ on exhaustion (active no-alt rediscovery)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2 — no alt
    send_cmd(*node, /*dst=*/5, "hi");
    const Ev* r1 = hal.last("rts_tx"); if (r1) CHECK(r1->next == 2);
    node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9);
    CHECK(node->peer_suspect_level(2) == 2);            // the sole next-hop is SILENT
    const int r_tx_before = hal.count("r_tx");
    node->on_timer(kRtsTimeoutTimerId);                // FIRST timeout: silent + no alt -> RREQ + requeue
    CHECK(hal.count("r_tx") == r_tx_before + 1);       // active rediscovery: an RREQ for the unreachable dst
    const Ev* rq = hal.last("r_tx"); if (rq) CHECK(rq->dst == 5);
    CHECK(hal.count("cascade_requeue") == 1);          // and the flight is requeued (not a hard giveup yet)
    delete node;
}
TEST_CASE("§P3 — a HEALTHY single-candidate primary does NOT fire an RREQ on a normal (congested) giveup") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2, healthy
    send_cmd(*node, /*dst=*/5, "hi");
    const int r_tx_before = hal.count("r_tx");
    exhaust_rts_same_hop(*node);                        // normal congested giveup (primary not silent)
    CHECK(hal.count("cascade_requeue") == 1);           // requeued as before
    CHECK(hal.count("r_tx") == r_tx_before);            // NO RREQ — the RREQ-on-silent is gated on the silent tier
    delete node;
}

// =============================================================================
// Phase 4 (routing-liveness port) — distributed liveness GOSSIP (BCN wire change).
// A node advertises its LOCALLY-observed silent/dead peers in a BCN suspect-TLV
// (type 1 ids-only / type 2 [id,state]); a receiver applies them as a REMOTE
// observation -> the mesh converges, not just the failing node. Anti-storm: a
// gossip-learned tier is applied but NEVER re-advertised (only local rts_timeout
// evidence populates the advertise window).
// =============================================================================
TEST_CASE("§P4 gossip ENCODE — a locally-SILENT peer -> type-1 id TLV; a locally-DEAD peer -> type-2 [id,state] TLV") {
    using meshroute::SuspectEntry;
    uint8_t buf[32]; SuspectEntry got[8];
    {   // SILENT-only -> type-1 SUSPECT_NODES (bare id list)
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
        hal._now = 1000;
        CHECK(node.test_build_suspect_ext(buf, sizeof(buf)) == 0);                 // nothing observed -> no TLV
        node.record_peer_rts_timeout(5, 0); node.record_peer_rts_timeout(5, 0); node.record_peer_rts_timeout(5, 0);
        CHECK(node.peer_suspect_level(5) == 2);
        const size_t n = node.test_build_suspect_ext(buf, sizeof(buf));
        CHECK(n == 1 + 1);
        CHECK((buf[0] >> 4) == protocol::bcn_ext_type_suspect_nodes);
        CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), got, 8) == 1);
        CHECK(got[0].node_id == 5); CHECK(got[0].state == 1);                      // type-1 applies as SUSPECT
    }
    {   // a DEAD peer present -> type-2 LIVENESS_STATE carrying the state byte
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
        hal._now = 1000;
        for (int i = 0; i < 6; ++i) node.record_peer_rts_timeout(6, 0);            // 6 at t=1000 -> SILENT (window not elapsed)
        hal._now = 1000 + protocol::peer_dead_evidence_window_ms + 1;
        node.record_peer_rts_timeout(6, 0);                                        // 7th past the window -> DEAD
        CHECK(node.peer_suspect_level(6) == 3);
        const size_t n = node.test_build_suspect_ext(buf, sizeof(buf));
        CHECK((buf[0] >> 4) == protocol::bcn_ext_type_liveness_state);
        CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), got, 8) == 1);
        CHECK(got[0].node_id == 6); CHECK(got[0].state == 3);
    }
}
TEST_CASE("§P4 gossip APPLY — a remote DEAD demotes the local route to a fresh alt, WITHOUT first-hand evidence") {
    using meshroute::SuspectEntry;
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    hal._now = 1000; { const size_t n = mk_beacon_route(2, 9, 7, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{22.0f,-60.0f,0,static_cast<int8_t>(2)}); }  // strong via 2
    hal._now = 1001; { const size_t n = mk_beacon_route(3, 9, 7, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }   // weak via 3
    CHECK(rt_primary_for(node, 9) == 2);
    SuspectEntry g[1] = { { 2, 3 } };                                              // GOSSIP from node 8: "node 2 is DEAD"
    hal._now = 2000; node.test_apply_suspect_gossip(g, 1, /*bcn_src=*/8);
    CHECK(node.peer_suspect_level(2) == 3);                                        // remote DEAD applied (no own timeout)
    CHECK(rt_primary_for(node, 9) == 3);                                          // -> route reroutes to the fresh alt via 3
}
TEST_CASE("§P4 anti-storm — a gossip-LEARNED tier is NOT re-advertised; only LOCAL evidence is; self/gossiper skipped") {
    using meshroute::SuspectEntry;
    uint8_t buf[32];
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    hal._now = 1000;
    // learn "2 dead" + "9 silent" via gossip from node 8
    SuspectEntry g[2] = { { 2, 3 }, { 9, 2 } };
    node.test_apply_suspect_gossip(g, 2, /*bcn_src=*/8);
    CHECK(node.peer_suspect_level(2) == 3);
    CHECK(node.test_build_suspect_ext(buf, sizeof(buf)) == 0);                     // ANTI-STORM: learned marks are NOT re-gossiped
    // a LOCAL observation IS advertised (proves the encoder works + isolates the anti-storm above)
    node.record_peer_rts_timeout(4, 0); node.record_peer_rts_timeout(4, 0); node.record_peer_rts_timeout(4, 0);   // 4 SILENT locally
    const size_t n = node.test_build_suspect_ext(buf, sizeof(buf));
    CHECK(n > 0);
    SuspectEntry got[8]; const uint8_t c = meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), got, 8);
    CHECK(c == 1); CHECK(got[0].node_id == 4);                                     // ONLY the locally-observed 4 — NOT the gossip-learned 2/9
    // self + gossiper are never marked
    SuspectEntry s[2] = { { 1, 3 }, { 7, 3 } };                                    // id 1 = self, src 7
    node.test_apply_suspect_gossip(s, 2, /*bcn_src=*/7);
    CHECK(node.peer_suspect_level(1) == 0);                                        // never self-mark
    CHECK(node.peer_suspect_level(7) == 0);                                        // never mark the gossiper
}

TEST_CASE("cascade — equal-score candidates keep INSERTION order (Lua-faithful, NO id tie-break)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = duty_cycle; cfg.duty_cycle_window_ms = window_ms;
    node->on_init(cfg);
    return node;
}

TEST_CASE("duty_status — pct/avail/enabled surface the rolling-window budget") {
    TestHal hal;
    Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/3600000);   // budget = 360,000 ms
    hal._airtime_used = 0;                                                 // 0% -> headroom
    auto d0 = node->duty_status();
    CHECK(d0.enabled); CHECK(d0.pct == 0); CHECK(d0.avail_ms == 0);
    hal._airtime_used = 180000;                                           // half budget -> 50%, still headroom
    auto d50 = node->duty_status();
    CHECK(d50.pct == 50); CHECK(d50.avail_ms == 0);
    hal._airtime_used = 360000; hal._oldest_tx_end = 0; hal._now = 0;     // at budget -> 100% (silent)
    auto d100 = node->duty_status();
    CHECK(d100.pct == 100); CHECK(d100.avail_ms > 0);                     // oldest=0 -> full-window fallback
    hal._airtime_used = 400000; hal._oldest_tx_end = 1000; hal._now = 600000;   // recovery = oldest + window - now
    auto dr = node->duty_status();
    CHECK(dr.pct == 100); CHECK(dr.avail_ms == (1000u + 3600000u - 600000u));
    delete node;
    TestHal hal2; Node* off = mk_budget_node(hal2, /*duty=*/0.0, /*window=*/3600000);   // duty<=0 -> disabled (no limit)
    hal2._airtime_used = 9999999;
    auto doff = off->duty_status();
    CHECK_FALSE(doff.enabled); CHECK(doff.pct == 0); CHECK(doff.avail_ms == 0);
    delete off;
}

TEST_CASE("① mobile-as-transit — learn is_mobile, exclude as transit, allow as dest (dv:1325-1334)") {
    TestHal hal;
    Node* node = mk_budget_node(hal, /*duty=*/0.0, /*window=*/3600000);   // id=1, leaf 0
    RxMeta meta{8.0f, -80.0f, 0, -1};
    CHECK_FALSE(node->is_mobile_peer(5));
    // a MOBILE neighbour 5 beacons, carrying a route to dest 9 (=> a 9-via-5 candidate at our node)
    beacon_entry e{}; e.dest = 9; e.next = 7; e.score_bucket = 12; e.hops = 2;
    beacon_in in{}; in.leaf_id = 0; in.src = 5; in.key_hash32 = 0x2005; in.is_mobile = true;
    in.entries = std::span<const beacon_entry>(&e, 1);
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(in, std::span<uint8_t>(bb.data(), bb.size()));
    node->on_recv(bb.data(), bn, meta);

    CHECK(node->is_mobile_peer(5));                          // learned the bit
    CHECK(node->route_uses_mobile_as_transit(9, 5));         // dest 9 via mobile 5 = transit -> excluded
    CHECK_FALSE(node->route_uses_mobile_as_transit(5, 5));   // 5 IS the dest -> deliver TO a mobile is fine
    CHECK_FALSE(node->route_uses_mobile_as_transit(9, 3));   // 3 not mobile -> fine
    CHECK_FALSE(node->route_uses_mobile_as_transit(9, 0));   // next 0 -> not a transit

    auto has = [&](uint8_t d){ for (uint8_t i = 0; i < node->rt_count(); ++i) if (node->rt_at(i).dest == d) return true; return false; };
    CHECK_FALSE(has(9));                                     // rt_merge SKIPPED the 9-via-mobile-5 candidate (dv:4583)
    CHECK(has(5));                                           // but 5 itself is reachable (direct neighbour; next==dest)
    CHECK(hal.count("rt_skip_mobile_transit") >= 1);
    delete node;
}

TEST_CASE("② implicit-ACK — overhearing the next-hop forward our DATA cancels the pending flight (dv:9863)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // dest 5 via 2 (primary) + 3 (alt)
    send_cmd(*node, 5, "hi");                                       // RTS to via 2
    // capture our in-flight RTS's ctr_lo + payload_len (== inner_len + MAC — what a forward must match)
    uint8_t our_ctr_lo = 0, our_plen = 0; bool got = false;
    for (auto& f : hal.tx_frames) { auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pr && pr->src == 1) { our_ctr_lo = pr->ctr_lo; our_plen = pr->payload_len; got = true; } }
    CHECK(got);
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                          // -> DATA tx (awaiting_ack)
    CHECK(node->has_pending_tx());

    // (1) a NON-matching overheard forward (wrong ctr_lo) does NOT cancel
    std::array<uint8_t, 16> nb{}; const size_t nn = mk_rts(/*src=*/2, /*next=*/9, /*dst=*/5, static_cast<uint8_t>(our_ctr_lo ^ 0x0F), our_plen, nb);
    node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("implicit_ack_from_forward") == 0);
    CHECK(node->has_pending_tx());                                 // flight survives a non-match

    // (2) the MATCHING forward (next-hop 2 forwarding OUR ctr_lo/len to dst 5, on to 9) = implicit ACK
    const int rts_before = hal.count("rts_tx");
    std::array<uint8_t, 16> fb{}; const size_t fn = mk_rts(/*src=*/2, /*next=*/9, /*dst=*/5, our_ctr_lo, our_plen, fb);
    node->on_recv(fb.data(), fn, m2);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    CHECK_FALSE(node->has_pending_tx());                           // flight cleared
    node->on_timer(kAckTimeoutTimerId);                           // the ACK timer was cancelled -> NO redundant retry
    CHECK(hal.count("rts_tx") == rts_before);
    delete node;
}

TEST_CASE("④ cascade_effective_max — full budget at/below threshold, shrinks 1:1 above, int-clamp no wrap (dv:6275)") {
    const int thr = protocol::cascade_requeue_load_threshold;
    const int mx  = protocol::cascade_requeue_max;
    CHECK(Node::cascade_effective_max(0) == mx);                       // empty queue -> full budget
    CHECK(Node::cascade_effective_max(static_cast<uint8_t>(thr)) == mx);   // at threshold -> still full
    CHECK(Node::cascade_effective_max(static_cast<uint8_t>(thr + 1)) == (mx - 1 > 0 ? mx - 1 : 0));   // one over -> shrink 1
    CHECK(Node::cascade_effective_max(static_cast<uint8_t>(thr + mx)) == 0);   // budget fully gated
    CHECK(Node::cascade_effective_max(255) == 0);                      // deep backlog -> 0, NO uint8 underflow wrap
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
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
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

TEST_CASE("R4.4 airtime backstop — a sender UNDER the airtime cap is NOT throttled (no false-positive)") {
    // Post-Inc-1 the throttle is airtime-only: the R-C apparent-origination COUNT clause was removed (a
    // missed CTS made a forwarder look like an originator -> false-drops, 168 on s18). The backstop is
    // honesty-independent — it caps a heavy NEIGHBOUR's airtime regardless of originate-vs-forward — so
    // the old "balanced forwarder (rts~=cts) is exempt" distinction no longer applies (a heavy forwarder
    // IS capped). What MUST hold instead, and is the no-false-positive guarantee: a sender whose overheard
    // airtime stays under the cap is never throttled (mirrors s18 dormancy — heaviest legit << cap).
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.04; cfg.duty_cycle_window_ms = 300000;   // budget 12000ms -> airtime cap 3000ms
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    for (uint8_t i = 0; i < 5; ++i) {                           // 5 overheard RTSes from 9 (each ~tens of ms)
        const size_t n = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/i,/*plen=*/10, rb);
        node.on_recv(rb.data(), n, m9);
    }
    const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/5,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m9);                            // addressed to us — total airtime << 3000ms cap
    CHECK(hal.count("rts_drop_originator_throttle") == 0);      // under cap -> NOT throttled
    CHECK(hal.count("cts_tx") == 1);                            // CTSed normally
}

TEST_CASE("R4.4 airtime backstop — over the 25%-budget airtime cap drops even when apparent <= max") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> airtime cap floor(0.35*10)=3ms
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
// enforce) — NOT a 0 cap that drops every RTS. Post-Inc-1 there is NO count fallback, so budget 0 means
// NO throttle at all. Guard fixed in BOTH engines.
TEST_CASE("R4.4 airtime backstop OFF when duty disabled (budget 0) -> no throttle (no count fallback)") {
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,9};
    // duty=0: an overheard + an addressed RTS (airtime >> any cap) -> NOT dropped (backstop skipped), CTSed.
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;   // duty_cycle = 0 (disabled)
    node.on_init(cfg);
    const size_t ov = mk_rts(9,99,8,0,10,rb); node.on_recv(rb.data(), ov, m9);
    const size_t rn = mk_rts(9, 1,8,1,10,rb); node.on_recv(rb.data(), rn, m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 0);  // budget 0 -> backstop SKIPPED, no count fallback
    CHECK(hal.count("cts_tx") == 1);
}

TEST_CASE("R4.4 Inc 2 — DATA airtime feeds the ledger (kind=data) + warn band fires below the drop cap") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.02; cfg.duty_cycle_window_ms = 300000;   // budget 6000ms -> cap 2100ms, warn 1680ms (share 0.35)
    node.on_init(cfg);
    // DATA airtime (kind=2) feeds total_air just like RTS/CTS — that's what gives the backstop teeth
    // (RTS-only airtime never approached the cap). 1800ms lands in the [1680,2100) warn band.
    node.track_originator_observation(9, /*kind=data*/2, /*ctr_lo=*/0, /*air=*/1800);
    int app; uint32_t air; uint8_t rts, cts;
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(air == 1800);   // DATA airtime counted
    CHECK(rts == 0);      // kind=data is neither rts...
    CHECK(cts == 0);      // ...nor cts (no false apparent-origination)
    // An addressed RTS from 9: total_air (1300 + the RTS's own airtime, still < 1500) is in the warn band
    // -> rts_originator_airtime_warn, but NOT over the cap -> CTSed, not dropped.
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/1,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m9);
    CHECK(hal.count("rts_originator_airtime_warn") == 1);   // warn band hit
    CHECK(hal.count("rts_drop_originator_throttle") == 0);  // under cap -> not dropped
    CHECK(hal.count("cts_tx") == 1);                        // warn does not block the CTS
}

// ---- e2e-ack backstop exemption + anti-spoof (2026-07-02) ------------------
// (c) codec round-trip: RTS_FLAG_E2E_ACK survives pack -> parse (it's the 4th free bit of the rts_flags nibble).
TEST_CASE("e2e-ack exemption — RTS_FLAG_E2E_ACK survives the RTS codec round-trip") {
    rts_in in{}; in.leaf_id = 0; in.src = 7; in.next = 3; in.ctr_lo = 5; in.dst = 9;
    in.sf_index = 3; in.rts_flags = RTS_FLAG_E2E_ACK; in.payload_len = 12; in.m_payload_id_lo16 = 0;
    std::array<uint8_t, 16> b{};
    const size_t n = pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
    CHECK(n == 7);                                                  // plain DM RTS (no M_BROADCAST/FLOOD tail)
    auto out = parse_rts(std::span<const uint8_t>(b.data(), n));
    CHECK(out.has_value());
    const rts_out o = out.value_or(rts_out{});
    CHECK((o.rts_flags & RTS_FLAG_E2E_ACK) != 0);                  // the bit round-trips
    CHECK((o.rts_flags & RTS_FLAG_RELAY) == 0);                    // and does NOT alias the neighbouring flags
    CHECK((o.rts_flags & RTS_FLAG_FLOOD) == 0);
    CHECK((o.rts_flags & RTS_FLAG_M_BROADCAST) == 0);
    CHECK_FALSE(o.m_broadcast); CHECK_FALSE(o.flood);
    // Sanity: an RTS with all four flag bits set carries all four (whole-nibble pack/parse).
    in.rts_flags = static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_RELAY | RTS_FLAG_E2E_ACK);
    const size_t n2 = pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
    auto out2 = parse_rts(std::span<const uint8_t>(b.data(), n2));
    CHECK(out2.has_value());
    CHECK((out2.value_or(rts_out{}).rts_flags & RTS_FLAG_E2E_ACK) != 0);
    CHECK((out2.value_or(rts_out{}).rts_flags & RTS_FLAG_RELAY)   != 0);
}

// (a) EXEMPTION: an over-airtime sender's PLAIN DM RTS is DROPPED by the backstop, but the SAME over-budget
// sender's RTS with RTS_FLAG_E2E_ACK set is NOT dropped (CTS proceeds) — an ack is never throttled.
TEST_CASE("e2e-ack exemption — an over-airtime sender's RTS is dropped, but its E2E_ACK RTS is exempt") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> airtime cap floor(0.35*10)=3ms
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    // drive sender 9 over the airtime cap: one overheard RTS (its own airtime >> 3ms cap)
    const size_t ov = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb);
    node.on_recv(rb.data(), ov, m9);
    // a PLAIN DM RTS addressed to us -> DROPPED (backstop fires, no CTS)
    const size_t plain = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/1,/*plen=*/10, rb);
    node.on_recv(rb.data(), plain, m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 1);
    CHECK(hal.count("cts_tx") == 0);
    // the SAME over-budget sender, but the RTS marks RTS_FLAG_E2E_ACK -> NOT dropped, CTS proceeds
    const size_t ackrts = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/2,/*plen=*/10, rb, RTS_FLAG_E2E_ACK);
    node.on_recv(rb.data(), ackrts, m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 1);   // STILL 1 — the ack RTS was exempt (no new drop)
    CHECK(hal.count("cts_tx") == 1);                         // CTSed
}

// (b) ANTI-SPOOF: a marked RTS -> a DATA whose type != DATA_TYPE_E2E_ACK -> e2e_ack_spoof fires + the sender is
// flagged; a SECOND marked RTS from that sender is then DROPPED (the exemption is revoked while flagged).
TEST_CASE("e2e-ack anti-spoof — a lied E2E_ACK bit flags the sender and revokes its exemption") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> cap 3ms (backstop armed)
    node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    // push sender 9 over the airtime cap so the backstop WOULD drop a plain RTS
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb), m9);
    // a marked E2E_ACK RTS addressed to us -> exempt -> CTS + _pending_rx (claimed_e2e_ack)
    hal._now = 1000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/5,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("cts_tx") == 1);
    // the DATA that follows is a PLAIN DM (type 0, NOT DATA_TYPE_E2E_ACK) -> the sender lied
    hal._now = 2000;
    node.on_recv(db.data(), mk_data(/*next=*/1, /*dst=*/1, /*ctr=*/0x0005, /*origin=*/9, "hi", db), m9);
    CHECK(hal.count("e2e_ack_spoof") == 1);                  // caught + flagged
    const Ev* sp = hal.last("e2e_ack_spoof");
    CHECK(sp != nullptr);
    if (sp) CHECK(sp->from == 9);                            // keyed on the PHYSICAL sender (RTS src), not the sealed origin
    // a SECOND marked RTS from 9 is now DROPPED — the exemption is revoked while flagged
    hal._now = 3000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/6,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 1);   // dropped despite the E2E_ACK bit (spoofer flagged)
    CHECK(hal.count("cts_tx") == 1);                         // no new CTS
}

// (d) a GENUINE marked RTS -> a real DATA_TYPE_E2E_ACK -> NO e2e_ack_spoof, the sender is NOT flagged.
TEST_CASE("e2e-ack anti-spoof — a genuine E2E_ACK does NOT flag the sender") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;
    node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb), m9);   // over cap
    hal._now = 1000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/5,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("cts_tx") == 1);
    // a REAL DATA_TYPE_E2E_ACK follows (inner body = acked ctr, 2 B) -> honest, no flag
    hal._now = 2000;
    const uint8_t ack_body[2] = { 0x05, 0x00 };   // acked ctr = 5
    node.on_recv(db.data(),
        mk_data_e2e(/*next=*/1, /*dst=*/1, /*ctr=*/0x0005, /*origin=*/9, /*flags=*/0,
                    ack_body, 2, db, /*type=*/DATA_TYPE_E2E_ACK), m9);   // type!=0 -> pack_data auto-sets DATA_FLAG_APP
    CHECK(hal.count("e2e_ack_spoof") == 0);                  // honest ack -> never flagged
    // a SECOND marked RTS from 9 is STILL exempt (not flagged) -> CTS proceeds
    hal._now = 3000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/6,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 0);   // never dropped (honest sender stays exempt)
    CHECK(hal.count("cts_tx") == 2);
}

TEST_CASE("DM Inc 3 — ACK warn bit round-trips through pack/parse (byte1 rsv nibble, ACK stays 3 B)") {
    uint8_t buf[3];
    // warn=true: fits the byte1 rsv nibble with NO growth; all other fields survive.
    { ack_in in{}; in.ctr_lo = 5; in.budget_hint = 2; in.snr_bucket = 1; in.to = 42; in.warn = true;
      const size_t n = pack_ack(in, std::span<uint8_t>(buf, 3));
      CHECK(n == 3);                                        // 3 B — no growth (vs the Lua's 4 B)
      auto out = parse_ack(std::span<const uint8_t>(buf, 3));
      CHECK(out.has_value());
      const ack_out o = out.value_or(ack_out{});
      CHECK(o.warn == true);
      CHECK(o.ctr_lo == 5);
      CHECK(o.budget_hint == 2);
      CHECK(o.snr_bucket == 1);
      CHECK(o.to == 42); }
    // warn=false round-trips too (bit 0 clear).
    { ack_in in{}; in.ctr_lo = 5; in.budget_hint = 2; in.snr_bucket = 1; in.to = 42; in.warn = false;
      pack_ack(in, std::span<uint8_t>(buf, 3));
      auto out = parse_ack(std::span<const uint8_t>(buf, 3));
      CHECK(out.has_value());
      CHECK(out.value_or(ack_out{}).warn == false); }
}

TEST_CASE("Slice3 — the flat self-cap is removed (no originator_self_defer; own DMs not count-capped)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    // Seed a direct route to bob(2) so an origination reaches issue_send.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // Fire many own DMs, each spaced well past any burst floor: none may hit the (deleted) flat count-cap.
    for (int k = 0; k < 30; ++k) {
        hal._now = 2000 + static_cast<uint64_t>(k) * 10000;   // 10 s apart (> dm_min_interval)
        send_cmd(node, /*dst=*/2, "hi");
        node.on_timer(kQueueWakeupTimerId);                   // let any deferred re-pick drain
    }
    CHECK(hal.count("originator_self_defer") == 0);           // the flat self-cap defer no longer exists
}

// The DM burst floor is CHECKED at become_free (defer-in-place) but only ARMED (_last_dm_origin_ms stamped)
// when an own DM actually flies (issue_send). So it bites a 2nd own DM only once the queue is idle again
// (the single-flight gate already holds a 2nd DM behind an in-flight one). Here DM #1 completes its flight,
// then a 2nd own DM originated < 3 s later is deferred by the floor; one originated >= 3 s later passes.
static void complete_dm_flight_via2(Node& node, TestHal& hal, uint8_t ctr_lo, uint64_t cts_at, uint64_t ack_at) {
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    hal._now = cts_at; node.on_recv(cb.data(), cn, bob);
    node.on_timer(kCtsToDataGapTimerId);                 // CTS->DATA gap -> DATA tx
    std::array<uint8_t, 8> ab{};
    const size_t an = mk_ack(/*to=*/1, ctr_lo, ab);
    hal._now = ack_at; node.on_recv(ab.data(), an, bob);
}

TEST_CASE("Slice3 — dm_min_interval: a <3s 2nd own DM defers, a >=3s one passes") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // DM #1 -> issues immediately (queue idle), stamps _last_dm_origin_ms=5000; complete the flight.
    hal._now = 5000; send_cmd(node, /*dst=*/2, "a");
    CHECK(hal.count("rts_tx") == 1);
    complete_dm_flight_via2(node, hal, /*ctr_lo=*/1, /*cts_at=*/5100, /*ack_at=*/5200);
    CHECK(hal.count("ack_rx") == 1);                 // flight #1 done -> queue idle
    // DM #2 only ~800 ms after DM #1's stamp -> picked at become_free, deferred by the 3 s floor.
    hal._now = 5800; send_cmd(node, /*dst=*/2, "b");
    CHECK(hal.count("rts_tx") == 1);                 // still 1 -> not issued
    CHECK(hal.count("send_blocked") >= 1);           // the DM burst floor tripped
    // Advance past 3 s from DM #1 (>=8000) and re-drain -> DM #2 now issues.
    hal._now = 8001; node.on_timer(kQueueWakeupTimerId);
    CHECK(hal.count("rts_tx") == 2);
}

// MF9: an own DM stamps _last_dm_origin_ms; an own e2e-ack originated < 3 s later must still enqueue AND
// drain — the DM burst floor is exempt for DATA_TYPE_E2E_ACK so a bridge's cross-layer ack-confirms never
// self-throttle. send_e2e_ack is private, so we drive it via the real RX path (an E2E_ACK_REQ DATA to us,
// origin=2 which we have a route to) — this exercises the exemption branch when become_free picks the ack.
TEST_CASE("Slice3 — own e2e-ack origination is NOT throttled by dm_min_interval") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);   // self = alice(1)
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    // Route to bob(2) so both the DM and the e2e-ack (back to origin 2) have a next hop.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bob);

    // An own DM to 2 -> issues + stamps _last_dm_origin_ms, then complete the flight (CTS -> DATA -> ACK)
    // so pending_tx clears and become_free is free to drain the ack next.
    hal._now = 5000; send_cmd(node, /*dst=*/2, "a");
    CHECK(hal.count("rts_tx") == 1);
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    hal._now = 5100; node.on_recv(cb.data(), cn, bob);
    node.on_timer(kCtsToDataGapTimerId);                 // CTS->DATA gap -> DATA tx
    std::array<uint8_t, 8> ab{};
    const size_t an = mk_ack(/*to=*/1, /*ctr_lo=*/1, ab);
    hal._now = 5200; node.on_recv(ab.data(), an, bob);   // ACK -> flight done, pending_tx clears
    CHECK(hal.count("ack_rx") == 1);

    // Now, only ~300 ms after the DM stamped the floor, receive an E2E_ACK_REQ DATA addressed to us (dst=1,
    // origin=2) -> the node originates an own e2e-ack back to 2. Exempt by TYPE -> must enqueue AND issue.
    // The RTS/CTS handshake precedes the DATA (src=2 -> us), matching the E2E-ACK delivery path.
    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/2, /*next=*/1, /*dst=*/1, /*ctr_lo=*/7, /*plen=*/15, rb);
    hal._now = 5500; node.on_recv(rb.data(), rn, bob);
    std::array<uint8_t, 64> db{};
    const uint8_t body[2] = { 'h', 'i' };
    const size_t dn = mk_data_e2e(/*next=*/1, /*dst=*/1, /*ctr=*/0x0007, /*origin=*/2,
                                  DATA_FLAG_E2E_ACK_REQ, body, 2, db);
    hal._now = 5600; node.on_recv(db.data(), dn, bob);
    node.on_timer(kPostAckTimerId);                      // deliver -> originate the E2E ack
    CHECK(hal.count("e2e_ack_tx") == 1);                 // the ack was enqueued despite < dm_min_interval_ms
    CHECK(hal.count("send_blocked") == 0);               // NOT throttled -> the DataType exemption held
    CHECK(hal.count("rts_tx") == 2);                     // and its RTS actually issued (not deferred in place)
}

// ---- R4.3 — adaptive beacon throttle + silence-jitter (THE determinism golden) ----
static Node* mk_throttle_node(TestHal& hal, uint32_t quiet_ms, uint32_t max_idle_ms) {
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.lbt_enabled = lbt_enabled; cfg.nav_enabled = false;   // LBT rand-order tests isolate from NAV (its origination jitter draws a rand)
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; cfg.lbt_enabled = true;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000; cfg.nav_enabled = false;   // budget 10000ms; duty-rand test isolates from NAV
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
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    CHECK(n == 9);   // R6.1: F is 7 + config_hash u16
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
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
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

// M4 (2026-07-04 wave-3): the RREQ hops byte is unauthenticated wire. learn_route_via stores hops = f.hops+1,
// so a forged f.hops==255 wraps (uint8) to a 0-hop reverse route that OUT-RANKS every real route AND re-seeds
// on each re-flood = network-wide poison from ONE crafted frame. The top-of-branch dv_hop_cap gate must drop it
// (no reverse route learned, no rebroadcast), while a below-cap RREQ still learns normally.
TEST_CASE("M4 — RREQ with hops==255 does NOT create a 0-hop poison route (dv_hop_cap gate); below-cap still learns") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto find_rt = [&](uint8_t dest) -> const RtEntry* {
        for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == dest) return &node.rt_at(i);
        return nullptr;
    };
    auto count_rreq = [&]() { int c = 0; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };

    // (1) FORGED RREQ: origin 10, hops == 255 (wraps to 0 on +1 if unguarded).
    f_in bad{}; bad.leaf_id = 0; bad.origin = 10; bad.is_reply = false;
    bad.dst_id = 20; bad.ttl_or_next_hop = 4; bad.hops = 255; bad.relay = 3;
    uint8_t bbuf[16]; const size_t bn = pack_f(bad, std::span<uint8_t>(bbuf, sizeof(bbuf)));
    hal._now = 1000; node.on_recv(bbuf, bn, meta);
    CHECK(hal.count("rreq_drop_hop_cap") == 1);             // gated at the top of the RREQ branch
    CHECK(find_rt(10) == nullptr);                          // NO reverse route learned (would have been 0-hop poison)
    CHECK(count_rreq() == 0);                               // NOT rebroadcast

    // (2) a legitimate below-cap RREQ (origin 11, hops 1) still learns a sane reverse route + rebroadcasts.
    f_in ok{}; ok.leaf_id = 0; ok.origin = 11; ok.is_reply = false;
    ok.dst_id = 20; ok.ttl_or_next_hop = 4; ok.hops = 1; ok.relay = 3;
    uint8_t obuf[16]; const size_t on = pack_f(ok, std::span<uint8_t>(obuf, sizeof(obuf)));
    hal._now = 2000; node.on_recv(obuf, on, meta);
    const RtEntry* e11 = find_rt(11);
    CHECK(e11 != nullptr);
    if (e11) { CHECK(e11->n >= 1); CHECK(e11->candidates[0].hops == 2); }   // f.hops(1)+1 = a sane 2, not 0
    CHECK(count_rreq() == 1);
}

// M6 (2026-07-04 wave-3): nav_duration_rts feeds the unauthenticated RTS payload_len byte into an airtime calc at
// max SF. A forged payload_len=255 must NOT arm NAV any longer than the real hard cap — otherwise a cheap overheard
// RTS silences a victim's TX for seconds. The clamp lives inside nav_duration_rts, so 255 == max_payload_bytes_hard_cap.
TEST_CASE("M6 — nav_duration_rts clamps payload_len (255 == max_payload_bytes_hard_cap, no max-SF blowup)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);   // max SF12
    node.on_init(cfg);
    const uint8_t sf = 12;
    const uint32_t d_cap = node.test_nav_duration_rts(sf, protocol::max_payload_bytes_hard_cap);
    const uint32_t d_255 = node.test_nav_duration_rts(sf, 255);
    CHECK(d_255 == d_cap);                                  // the forged 255 is clamped to the hard cap
    // A below-cap value is still honoured (the clamp only caps, never floors) -> smaller than the capped duration.
    const uint32_t d_small = node.test_nav_duration_rts(sf, 32);
    CHECK(d_small < d_cap);
}

// R6.1 §6.4: the membership gate must cover F (route-discovery is the bypass around the beacon gate). A divergent-config
// F is dropped + NOT relayed (1-hop flood containment); a matching one is processed normally.
TEST_CASE("R6.1 F-gate — a divergent-config F is dropped + NOT relayed; matching is processed") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    const uint16_t my_hash  = meshroute::leaf_config_hash(cfg.allowed_sf_bitmap, 0, 1250, 10000, 3000, nullptr, 0);   // NodeConfig anti-spam defaults
    const uint16_t diverge  = meshroute::leaf_config_hash((1u << 7), 0, 1250, 10000, 3000, nullptr, 0);   // bitmap {7} != {12} -> different hash
    CHECK(diverge != my_hash); CHECK(diverge != 0);

    auto count_rreq = [&]() { int c = 0; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };
    auto feed_rreq = [&](uint16_t ch, uint8_t origin) {
        f_in in{}; in.leaf_id = 0; in.origin = origin; in.is_reply = false;
        in.dst_id = 20; in.ttl_or_next_hop = 4; in.hops = 1; in.relay = 3; in.config_hash = ch;
        uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
        node.on_recv(buf, n, meta);
    };
    // (1) DIVERGENT F -> gate drops it: no reverse path, NO rebroadcast, conflict event.
    hal._now = 1000; feed_rreq(diverge, /*origin=*/10);
    bool rev10 = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 10) rev10 = true;
    CHECK_FALSE(rev10);
    CHECK(count_rreq() == 0);                                // contained to 1 hop (not relayed)
    CHECK(hal.count("leaf_config_conflict") >= 1);
    // (2) MATCHING F -> processed: reverse path + rebroadcast.
    hal._now = 2000; feed_rreq(my_hash, /*origin=*/11);
    bool rev11 = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 11) rev11 = true;
    CHECK(rev11);
    CHECK(count_rreq() == 1);
}

TEST_CASE("F RREP addressed to the origin -> forward path + rrep_arrived") {
    TestHal hal;
    Node node(hal, /*node_id=*/10, /*key_hash32=*/0xABCD);  // we are the origin
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = true;
    in.dst_id = 20; in.ttl_or_next_hop = 10; in.hops = 2; in.relay = 7;   // addressed to us(10); forwarder 7 toward dst
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
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
      uint8_t qb[16]; const size_t qn = pack_f(q, std::span<uint8_t>(qb, sizeof(qb)));
      hal._now = 1000; node.on_recv(qb, qn, meta); }
    const size_t tx_before = hal.tx_frames.size();

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = true;
    in.dst_id = 20; in.ttl_or_next_hop = 5; in.hops = 2; in.relay = 7;    // addressed to us(5)
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
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

// ===== E2E ACK (send_e2e) — do_post_ack hooks =====

TEST_CASE("E2E ACK — destination of an E2E_ACK_REQ DATA replies with an ack to the origin") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);

    std::array<uint8_t, 64> db{};
    const uint8_t body[2] = { 'h', 'i' };
    const size_t dn = mk_data_e2e(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/0,
                                  DATA_FLAG_E2E_ACK_REQ, body, 2, db);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);                          // deliver -> E2E ack reply
    CHECK(hal.count("delivered")  == 1);                     // the DM still delivers to the app
    CHECK(hal.count("e2e_ack_tx") == 1);                     // and we send an end-to-end ack to the origin
    CHECK(hal.count("tx_enqueue") == 0);                     // the ack is NOT an app DM (dm_delivery honesty)
}

TEST_CASE("E2E ACK — origin of an E2E_IS_ACK DATA confirms (no app delivery)") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCD);   // we are the origin being acked
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/4, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);

    std::array<uint8_t, 64> db{};
    const uint8_t acked[2] = { 5, 0 };                       // acked ctr = 5 (LE)
    const size_t dn = mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/2,
                                  /*flags=*/0, acked, 2, db, /*type=*/DATA_TYPE_E2E_ACK);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    const Ev* ack = hal.last("e2e_ack_rx");
    CHECK(ack != nullptr);
    if (ack) { CHECK(ack->from == 2); CHECK(ack->ctr == 5); }  // confirmed the DM we originated (ctr=5)
    CHECK(hal.count("delivered") == 0);                      // an E2E ack is NOT delivered as a message
}

// E2E-ack DURABLE RECEIPT (2026-06-23): the origin RECORDS the ack as a DM-store receipt (type=E2E_ACK, no body) AND
// emits a live send_e2e_acked push -> harness/companion can confirm "the dest got it" (was telemetry-only = invisible on metal).
TEST_CASE("E2E ACK — origin records a durable receipt + a send_e2e_acked push (not telemetry-only)") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCD);   // we are the origin being acked
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    node.inbox().on_init(&dm, &ch);                          // a backend installs durable stores (else record_ack is inert)
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t, 16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/4, rb), meta);
    std::array<uint8_t, 64> db{};
    const uint8_t acked[2] = { 5, 0 };                       // acked ctr = 5 (LE)
    hal._now = 2000; node.on_recv(db.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/2,
                                  /*flags=*/0, acked, 2, db, /*type=*/DATA_TYPE_E2E_ACK), meta);
    node.on_timer(kPostAckTimerId);

    // 1) a live send_e2e_acked push: dst = the acker (2), ctr = the acked ctr (5)
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::send_e2e_acked) { got = true; break; } }
    CHECK(got);
    if (got) { CHECK(pu.dst == 2); CHECK(pu.ctr == 5); }

    // 2) a durable DM-store receipt (type=E2E_ACK, no body), NOT an app delivery
    CHECK(dm.count() == 1);
    CHECK(ch.count() == 0);                                  // a receipt does not touch the channel store
    struct Got { bool seen; InboxKind kind; uint8_t origin; uint32_t msg_id; uint8_t type; uint8_t blen; }
        g{ false, InboxKind::channel, 0, 0, 0, 99 };
    node.inbox().pull(0, 0, [](void* c, const InboxEntry& e) -> bool {
        auto* x = static_cast<Got*>(c);
        x->seen = true; x->kind = e.kind; x->origin = e.origin; x->msg_id = e.msg_id; x->type = e.type; x->blen = e.body_len;
        return true;
    }, &g);
    CHECK(g.seen);
    CHECK(g.kind == InboxKind::dm);
    CHECK(g.type == DATA_TYPE_E2E_ACK);                      // a receipt, distinguished by the type byte
    CHECK(g.origin == 2);                                    // the dest that confirmed delivery
    CHECK(g.msg_id == 5);                                    // = the acked ctr
    CHECK(g.blen == 0);                                      // no body
    CHECK(hal.count("delivered") == 0);                      // still NOT delivered as a message
}

// ===== OTA remote diagnostics (rcmd) — DATA_TYPE_REMOTE_CMD/RESP staging =====
TEST_CASE("rcmd: a REMOTE_CMD DM STAGES into the inbound slot (not inbox/delivered); take drains it; a 2nd-while-pending drops") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCDu);   // we are the target of the command
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t,16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/15, rb), meta);
    std::array<uint8_t,64> db{};
    const uint8_t body[6] = { 's','t','a','t','u','s' };
    hal._now = 2000; node.on_recv(db.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/2,
                                  /*flags=*/0, body, 6, db, /*type=*/DATA_TYPE_REMOTE_CMD), meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 0);                      // a remote cmd is NOT an app delivery

    // a 2nd REMOTE_CMD WHILE one is pending -> dropped (the slot keeps the first; rcmd is human-paced)
    std::array<uint8_t,16> rb2{};
    hal._now = 3000; node.on_recv(rb2.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/10, /*plen=*/15, rb2), meta);
    std::array<uint8_t,64> db2{};
    const uint8_t body2[4] = { 'd','u','t','y' };
    hal._now = 4000; node.on_recv(db2.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x000A, /*origin=*/3,
                                  /*flags=*/0, body2, 4, db2, /*type=*/DATA_TYPE_REMOTE_CMD), meta);
    node.on_timer(kPostAckTimerId);

    Node::RemoteInbound ri;
    CHECK(node.take_remote_inbound(ri));                     // drains the FIRST (the 2nd was dropped)
    CHECK(ri.is_response == false);
    CHECK(ri.from == 2);
    CHECK(ri.len == 6);
    CHECK(std::string(reinterpret_cast<const char*>(ri.body), ri.len) == "status");
    CHECK_FALSE(node.take_remote_inbound(ri));              // slot cleared after the drain
}

TEST_CASE("rcmd: a REMOTE_RESP DM stages as is_response=true; send_remote_cmd/response return the sent ctr") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCDu);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t,16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/15, rb), meta);
    std::array<uint8_t,64> db{};
    const uint8_t body[8] = { 'u','p','=','4','2','s',' ',' ' };
    hal._now = 2000; node.on_recv(db.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/7,
                                  /*flags=*/0, body, 8, db, /*type=*/DATA_TYPE_REMOTE_RESP), meta);
    node.on_timer(kPostAckTimerId);
    Node::RemoteInbound ri;
    CHECK(node.take_remote_inbound(ri));
    CHECK(ri.is_response == true);                          // a RESPONSE, not a command
    CHECK(ri.from == 7);
    // send_* return the assigned ctr (origination ride; we just check they don't refuse the call)
    const uint8_t qb[4] = { 't','e','s','t' };
    (void)node.send_remote_cmd(5, qb, 4);
    (void)node.send_remote_response(5, qb, 4);
}

// ===================== L2c — DST_HASH verify-on-delivery + identity-preserving redirect =====================
// A DM addressed to our node_id but carrying a cleartext DST_HASH naming a DIFFERENT key was misdelivered
// by an id collision: do NOT deliver; FORWARD it (origin + ctr + flags + inner preserved — NOT re-sent) to
// the real owner of want_hash if resolvable, else flood a HARD H to learn it and drop. No renumber (§7.1).

TEST_CASE("L2c — DST_HASH matching our key delivers normally") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash(2, 2, 0x0005, /*origin=*/1, /*dst_hash=*/0xABCD, "hi", db);
    CHECK(dn > 0);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    const Ev* dlv = hal.last("delivered");
    CHECK(dlv != nullptr);
    if (dlv) { CHECK(dlv->has_payload); CHECK(dlv->payload == "hi"); }   // body parsed past the 4-B hash prefix
    CHECK(hal.count("l2c_misdelivery") == 0);
}

TEST_CASE("L2c — DST_HASH mismatch, owner UNKNOWN: HARD H query, drop, no deliver, no renumber") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);       // would-be heal target; proves L2c does NOT renumber
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*dst_hash=*/0xFFFFFFFFu, "hi", db);   // unknown owner
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 0);                          // NOT delivered locally
    CHECK(hal.count("l2c_misdelivery") == 1);
    CHECK(hal.count("l2c_redirect_query") == 1);                 // HARD H flood to learn the owner
    CHECK(hal.count("h_tx") == 1);
    CHECK(hal.count("l2c_redirect_forward") == 0);               // nothing to forward to (unknown)
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);        // §7.1: L2c never renumbers
    CHECK(hal.count("join_deny_sent") == 0);
}

TEST_CASE("L2c — DST_HASH mismatch, owner KNOWN: FORWARD preserves origin + ctr (identity not corrupted)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    // A beacon from owner(3) binds 3 -> key_hash32 0x1234 (id_bind) AND installs a direct route to 3.
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/3, bb);
    RxMeta b3{ 8.0f, -80.0f, 0, static_cast<int8_t>(3) };
    hal._now = 500; node.on_recv(bb.data(), bn, b3);
    // A misdelivered DM (origin=1, ctr=0x0005) addressed to our id(2) but wanting key 0x1234 (owner 3).
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash(2, 2, /*ctr=*/0x0005, /*origin=*/1, /*dst_hash=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 0);                          // not for us
    CHECK(hal.count("l2c_misdelivery") == 1);
    const Ev* rf = hal.last("l2c_redirect_forward");
    CHECK(rf != nullptr);
    if (rf) { CHECK(rf->to == 3); CHECK(rf->ctr == 5); }         // forwarded toward owner 3, ORIGINAL ctr
    // Drive the forward leg (RTS->CTS->DATA) and prove the emitted DATA keeps origin=1 + ctr=5 (not re-sent).
    const Ev* rts = hal.last("rts_tx");
    CHECK(rts != nullptr);
    if (rts) CHECK(rts->next == 3);
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/2, /*tx_id=*/3, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b3);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            CHECK(d->ctr == 0x0005);                             // ORIGINAL ctr preserved (no new send_by_hash ctr)
            CHECK(d->dst == 3);                                  // re-targeted to the real owner
            auto inner = data_inner(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d);
            auto ui = parse_unicast_inner(inner, d->flags);
            CHECK(ui.has_value());
            if (ui) { CHECK(ui->origin == 1);                    // ORIGINAL sender preserved (NOT the redirector id 2)
                      CHECK(ui->has_dst_hash); CHECK(ui->dst_key_hash32 == 0x1234u); }
        }
    }
}

// A misdelivered CRYPTED DM (the dst_hash is cleartext, so the misdelivery branch fires BEFORE any open) must
// re-tx the originator's 8-B nonce-seed verbatim on the redirect leg — else the real owner computes the wrong
// nonce and the seal tag-fails (silent drop). Every other forward path carries the seed; the L2c redirect must too.
TEST_CASE("§1c L2c — a misdelivered CRYPTED DM redirect carries the nonce-seed (else the owner can't open the seal)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/3, bb);   // owner(3): id_bind 3->0x1234 + a route to 3
    RxMeta b3{ 8.0f, -80.0f, 0, static_cast<int8_t>(3) };
    hal._now = 500; node.on_recv(bb.data(), bn, b3);
    const uint8_t S[8] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88 };           // the originator's nonce-seed
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, /*ctr_lo=*/5, 20, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_crypted(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/1, /*dst_hash=*/0x1234u, S, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_misdelivery") == 1);                     // CRYPTED dst_hash names owner 3, not us(2) -> redirect
    const Ev* rts = hal.last("rts_tx"); CHECK(rts != nullptr); if (rts) CHECK(rts->next == 3);
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/2, /*tx_id=*/3, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b3);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            CHECK(d->crypted);
            auto sd = data_nonce_seed(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d);
            CHECK(sd.size() == 8);
            bool seed_ok = (sd.size() == 8); for (size_t i = 0; i < sd.size() && i < 8; ++i) seed_ok = seed_ok && (sd[i] == S[i]);
            CHECK(seed_ok);                                      // the originator's seed must survive the redirect (was zeroed -> RED)
        }
    }
}

// M3 (2026-07-04, crypto): a `sendhashx` (crypt=on) addressed to an UNRESOLVED hash PARKS, then flies when the
// binding arrives. Before the fix ParkedSend carried no crypt intent -> both drains called do_send() with the
// DEFAULT intent (resolves to _cfg.e2e_dm == false) -> the parked-then-drained DM went out CLEARTEXT, silently
// downgrading a confidential send (violating the node.h "never silently falls back to cleartext" invariant).
// The fix stamps p.crypt at park + threads it into do_send at BOTH drains. This drives the drain-on-answer path
// (drain_parked_sends) and asserts the emitted DATA frame is CRYPTED, not plaintext.
TEST_CASE("M3 — a PARKED crypt=on send flies CRYPTED when the binding arrives (no silent cleartext downgrade)") {
    TestHal hal;
    // node 1 with a route to dest 5 via next-hop 2 (the resolved id will be 5 -> uses this route).
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});           // via 2 (h1) to dest 5
    // The recipient B's crypto identity: its key_hash32 == ed_pub[:4]; we cache B's AUTHORITATIVE pubkey so the
    // seal can find it (else e2e_seal_inner fails no_pubkey -> refused, NEVER cleartext — the fail-loud contract).
    uint8_t seedA[32], seedB[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 3); seedB[i] = uint8_t(200 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    node->set_crypto_identity(idA.x_secret, idA.ed_pub);           // node 1 can seal
    node->peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);   // B's pubkey cached
    hal.events.clear();

    // sendhashx: address B by its key_hash32 with crypt=on. The binding (idB.key_hash32 -> 5) is UNKNOWN -> PARK.
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_hash = idB.key_hash32; c.u.send.flags = 0; c.crypt = CryptIntent::on;
    const uint8_t body[] = { 's','e','c' };
    c.body = body; c.body_len = sizeof(body);
    const CmdResult r = node->on_command(c);
    CHECK(r.code == CmdCode::queued);
    CHECK(r.ctr == 0);                                             // parked (resolving), not sent yet
    CHECK(hal.count("send_parked_for_hash") == 1);

    // The owner's answer arrives: idB.key_hash32 -> node 5 (authoritative) -> drain_parked_sends -> do_send(5, .., p.crypt=on).
    std::array<uint8_t, 7> hbin{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 5; hb.key_hash32 = idB.key_hash32; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(hbin.data(), hbin.size()));
    node->on_hash_bind_response(hbin.data(), static_cast<uint8_t>(in), hb.authoritative);
    CHECK(hal.count("send_hash_resolved") == 1);                   // the parked DM drained + flew

    // Pump the flight: RTS went to next-hop 2; feed its CTS -> the CTS->DATA gap fires -> DATA on air.
    const Ev* rts = hal.last("rts_tx"); CHECK(rts != nullptr); if (rts) CHECK(rts->next == 2);
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    RxMeta m2{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);

    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) CHECK(d->crypted);                                 // ★ the DRAINED parked send is CRYPTED, not cleartext (M3)
    }
    delete node;
}

TEST_CASE("L2c — repeated misdeliveries for one hash collapse to ONE redirect action (anti-flood)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    for (uint8_t k = 0; k < 2; ++k) {                            // two misdeliveries, same want_hash, distinct ctr_lo
        const uint8_t ctr = static_cast<uint8_t>(5 + k);
        std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, ctr, 15, rb);
        hal._now = 1000 + 1000 * k; node.on_recv(rb.data(), rn, meta);
        std::array<uint8_t, 64> db{};
        const size_t dn = mk_data_dsthash(2, 2, ctr, 1, /*dst_hash=*/0xFFFFFFFFu, "hi", db);
        hal._now = 1500 + 1000 * k; node.on_recv(db.data(), dn, meta);
        node.on_timer(kPostAckTimerId);
    }
    CHECK(hal.count("delivered") == 0);
    CHECK(hal.count("l2c_misdelivery") == 2);                    // BOTH observed (telemetry every time)
    CHECK(hal.count("l2c_redirect_query") == 1);                 // but only ONE redirect action -> no flood storm
    CHECK(hal.count("l2c_redirect_parked") == 1);               // and only ONE parked entry (ring fills can't grow)
    CHECK(hal.count("l2c_redirect_suppressed") == 1);           // the 2nd copy is suppressed, not parked/flooded
    CHECK(hal.count("h_tx") == 1);
}

// --- confirmation-gated heal: the HARD-H resolution decides forward (stale binding) vs heal (real collision) ---

TEST_CASE("L2c — parked redirect resolves to a DIFFERENT id: forward, NEVER renumber (stale binding)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);       // joined: a heal WOULD renumber — it must not
    // Route to 7 via neighbour 4 (this beacon does NOT bind 0x1234), so a later forward to 7 can issue.
    std::array<uint8_t, 64> rbe{}; const size_t rben = mk_beacon_route(/*src=*/4, /*dest=*/7, /*next=*/4, /*hops=*/2, /*score=*/10, rbe);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) }; hal._now = 500; node.on_recv(rbe.data(), rben, m4);
    // Misdeliver a DM wanting key 0x1234 (unknown) -> park + HARD-H.
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, /*origin=*/1, /*dst_hash=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    CHECK(hal.count("l2c_redirect_forward") == 0);               // nothing forwarded yet (parked, awaiting resolution)
    // HARD-H answer: 0x1234 is at id 7 (the recipient MOVED; not our id) -> forward, no collision.
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/7, /*hb_key=*/0x1234, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_forward") == 1);               // forwarded to the moved recipient
    const Ev* rf = hal.last("l2c_redirect_forward");
    if (rf) { CHECK(rf->to == 7); CHECK(rf->ctr == 5); }         // ORIGINAL ctr preserved
    CHECK(hal.count("l2c_collision_confirmed") == 0);            // NOT a same-id collision
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);        // joined, but NEVER renumbered (no spurious churn)
}

TEST_CASE("L2c — parked redirect resolves to OUR id: CONFIRMED collision, we WIN -> DENY (no renumber)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0x0000ABCDu);  // LOW key -> we keep + DENY
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0xFFFF0000u, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    // HARD-H answer: 0xFFFF0000 is at id 2 (OUR id) -> proven same-id collision.
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0xFFFF0000u, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("addr_conflict_self_defended") == 1);        // the answer did NOT clobber our self-binding
    CHECK(hal.count("l2c_collision_confirmed") == 1);
    CHECK(hal.count("join_deny_sent") == 1);                     // we keep -> DENY the squatter
    if (const Ev* d = hal.last("join_deny_sent")) CHECK(d->reason == 4);   // MEDIATED
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);
}

TEST_CASE("L2c — parked redirect resolves to OUR id, we LOSE (joined): forced_rejoin (yield the id)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xFFFFFFFFu);  // HIGH key -> we yield
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);       // forced_rejoin only fires when DAD-joined
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x00000001u, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0x00000001u, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_collision_confirmed") == 1);
    CHECK(hal.count("addr_conflict_forced_rejoin") == 1);        // we are the squatter -> yield our id
    CHECK(hal.count("join_deny_sent") == 0);
}

// --- additional L2c coverage (hop-budget edge, slot-reuse, send-side stamping, age-out, beacon re-drain) ---

// DST_HASH DATA with an explicit hops_remaining (to drive the destination-exhausted edge).
static size_t mk_data_dsthash_hops(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                                   uint32_t dst_hash, uint8_t hops_remaining, const char* body,
                                   std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 40> inner{};
    inner[0] = static_cast<uint8_t>(dst_hash);        inner[1] = static_cast<uint8_t>(dst_hash >> 8);
    inner[2] = static_cast<uint8_t>(dst_hash >> 16);  inner[3] = static_cast<uint8_t>(dst_hash >> 24);
    inner[4] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[5 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = DATA_FLAG_DST_HASH; in.next = next; in.dst = dst;
    in.hops_remaining = hops_remaining; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 5 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
static CmdResult send_hash_cmd(Node& node, uint32_t dst_hash, const char* body) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 0; c.u.send.dst_hash = dst_hash; c.u.send.flags = 0;
    c.body = reinterpret_cast<const uint8_t*>(body);
    c.body_len = static_cast<uint8_t>(std::strlen(body));
    return node.on_command(c);
}

TEST_CASE("L2c — redirect re-budgets the hop count from rt (no destination-exhaustion underflow)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // Beacon from owner(3) -> authoritative bind 3->0x1234 + a DIRECT route to 3 (rt_hops=1).
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/3, bb);
    RxMeta b3{ 8.0f, -80.0f, 0, static_cast<int8_t>(3) }; hal._now = 500; node.on_recv(bb.data(), bn, b3);
    // A misdelivered DM that arrived AT us EXHAUSTED (hops_remaining==0; the dst is exempt from the NACK).
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash_hops(2, 2, 0x0005, 1, /*dst_hash=*/0x1234, /*hops_remaining=*/0, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);                              // owner known -> immediate forward to 3
    CHECK(hal.count("l2c_redirect_forward") == 1);
    // Drive the forward leg and confirm the emitted DATA's hop budget is rt-derived (1 + slack 3 = 4), NOT the
    // 255->31 saturation a naive inherited budget would produce.
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/2, /*tx_id=*/3, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b3);
    node.on_timer(kCtsToDataGapTimerId);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) { CHECK(d->hops_remaining != 31);                // NOT the underflow saturation
                 CHECK(d->hops_remaining <= 5);                 // rt_hops(1)+slack(3) == 4
                 CHECK(d->ctr == 0x0005); }                     // identity still preserved
    }
}

TEST_CASE("L2c — park_send into a recycled redirect slot is NOT mis-drained as a redirect (slot reset)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    // (1) Misdeliver for 0xAAAA (unknown) -> parks a REDIRECT at slot 0 (is_redirect=true).
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0xAAAAu, "hi", db);
    hal._now = 1100; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    // (2) Age it out (no resolution) -> slot 0 vacated by in-place compaction, leaving stale is_redirect=true.
    hal._now = 1100 + 30000 + 1; node.on_timer(2 /*kAgingTimerId*/);
    CHECK(hal.count("send_hash_giveup") == 1);                  // the redirect is gone; node is idle (no flight)
    // (3) A PLAIN send-by-hash for 0xBBBB (unknown) -> park_send REUSES slot 0; the reset must clear is_redirect.
    hal._now = 35000; send_hash_cmd(node, /*dst_hash=*/0xBBBBu, "yo");
    CHECK(hal.count("send_parked_for_hash") == 1);
    // (4) Resolve 0xBBBB -> id 8. It MUST drain via the plain (do_send) path, NOT the stale redirect branch.
    std::array<uint8_t, 16> rb3{}; size_t rn3 = mk_rts(4, 2, 2, 7, 7, rb3);
    hal._now = 36000; node.on_recv(rb3.data(), rn3, m4);
    std::array<uint8_t, 64> ab2{}; size_t an2 = mk_data_hashbind(2, 2, 0x0007, /*hb_node=*/8, /*hb_key=*/0xBBBBu, true, ab2);
    hal._now = 36100; node.on_recv(ab2.data(), an2, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("send_hash_resolved") == 1);                // plain send-by-hash path (correct, post-reset)
    CHECK(hal.count("l2c_redirect_forward") == 0);             // the recycled slot did NOT re-trigger a redirect
    CHECK(hal.count("l2c_collision_confirmed") == 0);
}

TEST_CASE("L2c send-side — originator stamps DST_HASH from an authoritative id_bind") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    uint32_t hh = 0;
    CHECK_FALSE(node.key_hash_of_id(2, hh));                    // unknown -> no stamp
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/2, bb);   // authoritative bind 2->0x1234 + route
    RxMeta b2{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) }; hal._now = 1000; node.on_recv(bb.data(), bn, b2);
    CHECK(node.key_hash_of_id(2, hh)); CHECK(hh == 0x1234u);
    hal._now = 2000; send_cmd(node, /*dst=*/2, "hi");
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b2);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            auto ui = parse_unicast_inner(data_inner(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d), d->flags);
            CHECK(ui.has_value());
            if (ui) { CHECK(ui->has_dst_hash); CHECK(ui->dst_key_hash32 == 0x1234u); CHECK(ui->origin == 1); }
        }
    }
}

TEST_CASE("L2c — cfg/NV-provisioned LOSER (not joined): collision confirmed but NO renumber (healed=false)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xFFFFFFFFu);  // HIGH key -> would lose; but NOT joined
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // (no restore_join_state -> _joined stays false: an operator-pinned id)
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x00000001u, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0x00000001u, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_collision_confirmed") == 1);
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);       // operator-pinned id is NOT auto-reassigned
    CHECK(hal.count("join_deny_sent") == 0);                    // we lost, so no keep-DENY either
    if (const Ev* cc = hal.last("l2c_collision_confirmed")) CHECK_FALSE(cc->healed);   // surfaced, not healed
}

TEST_CASE("L2c — parked redirect ages out (send_hash_giveup) when its HARD-H never resolves") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0xDEADBEEFu, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    hal._now = 2000 + 30000 + 1;                                // past send_defer_ttl_ms (30s)
    node.on_timer(2 /*kAgingTimerId*/);
    CHECK(hal.count("send_hash_giveup") == 1);                  // the unresolved redirect is given up (not stranded)
    CHECK(hal.count("l2c_redirect_forward") == 0);
    CHECK(hal.count("l2c_collision_confirmed") == 0);
}

TEST_CASE("L2c — parked redirect forwards on the owner's BEACON re-drain (drain_resolved_parked_sends)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    // Owner(9)'s beacon (key 0x1234) lands -> authoritative bind 9->0x1234 + route; drain_resolved forwards.
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/9, bb);   // mk_beacon key == 0x1234
    RxMeta b9{ 8.0f, -80.0f, 0, static_cast<int8_t>(9) }; hal._now = 2500; node.on_recv(bb.data(), bn, b9);
    CHECK(hal.count("l2c_redirect_forward") == 1);
    const Ev* rf = hal.last("l2c_redirect_forward");
    if (rf) { CHECK(rf->to == 9); CHECK(rf->ctr == 5); }        // forwarded to the owner, original ctr
    CHECK(hal.count("l2c_collision_confirmed") == 0);
}

// --- review re-fix regressions: forwarder drop-not-defer + plain send-by-hash resolving to self ---

TEST_CASE("L2c — a no-route redirect DROPS (forwarder semantics), it does NOT defer/send_failed") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    // An H answer (via relay 4) binds 7->0x1234 AUTHORITATIVE but installs NO route to 7 (only to relay 4).
    std::array<uint8_t, 16> rba{}; const size_t rna = mk_rts(4, 2, 2, 9, 7, rba);
    hal._now = 500; node.on_recv(rba.data(), rna, m4);
    std::array<uint8_t, 64> aba{}; const size_t ana = mk_data_hashbind(2, 2, 0x0009, /*hb_node=*/7, /*hb_key=*/0x1234, true, aba);
    hal._now = 600; node.on_recv(aba.data(), ana, m4);
    node.on_timer(kPostAckTimerId);
    // Misdeliver a DM wanting 0x1234: owner 7 is known authoritatively -> immediate forward, but no route to 7.
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_forward") == 1);              // we tried to forward (enqueued)
    CHECK(hal.count("send_no_route") == 1);                     // ...and DROPPED it (forwarder, is_forward=true)
    CHECK(hal.count("send_deferred") == 0);                     // NOT the originator defer path
    CHECK(hal.count("send_deferred_giveup") == 0);             // and thus no send_failed-to-local-app for a transit DM
}

TEST_CASE("L2c/H — a plain send-by-hash that resolves to OUR OWN id gives up (no self-addressed do_send)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // Address an UNKNOWN hash 0xCAFE -> send_by_hash parks (plain) + floods a soft H.
    hal._now = 1000; send_hash_cmd(node, /*dst_hash=*/0xCAFEu, "yo");
    CHECK(hal.count("send_parked_for_hash") == 1);
    // An H answer claims 0xCAFE is at id 2 (OUR id) with a foreign key -> self-guard refuses the bind, and the
    // plain parked send must NOT do_send to ourselves.
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(4, 2, 2, 6, 7, rb);
    hal._now = 2000; node.on_recv(rb.data(), rn, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0xCAFEu, true, ab);
    hal._now = 2100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("addr_conflict_self_defended") == 1);       // the foreign-key bind on our id was refused
    CHECK(hal.count("send_hash_giveup") == 1);                  // the plain send gave up (resolved to self)
    CHECK(hal.count("tx_enqueue") == 0);                        // NO self-addressed do_send
}

// ---- NAV (virtual carrier sense) -------------------------------------------------------------------
// Overheard unicast RTS/CTS reserve the medium (nav_enabled); own unsolicited TX defers until it clears;
// a new addressed RTS during a reservation is ignored. Channel/broadcast RTS + addressed RTS don't set it.

TEST_CASE("NAV — an overheard unicast RTS reserves the medium (only when nav_enabled)") {
    // nav OFF (default) -> overheard RTS sets nothing
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = false;   // explicit OFF (firmware default is now ON)
      node.on_init(cfg);
      std::array<uint8_t, 16> rb{};
      const size_t rn = mk_rts(/*src=*/1, /*next=*/3, /*dst=*/4, /*ctr_lo=*/5, /*plen=*/20, rb);  // next=3 != me(2) -> overheard
      hal._now = 1000; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(node.nav_until_ms() == 0); }
    // nav ON -> overheard unicast RTS reserves into the future
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
      node.on_init(cfg);
      std::array<uint8_t, 16> rb{};
      const size_t rn = mk_rts(1, 3, 4, 5, 20, rb);
      hal._now = 1000; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(node.nav_until_ms() > 1000); }
}

TEST_CASE("NAV — an RTS addressed to us, and a channel/broadcast RTS, do NOT set NAV") {
    TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
    node.on_init(cfg);
    // addressed to us (next=2) -> our exchange, not a reservation to honor
    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, 5, 20, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
    CHECK(node.nav_until_ms() == 0);
    // M_BROADCAST (channel) RTS -> a flood, no CTS to protect -> no NAV
    rts_in mb{}; mb.leaf_id = 0; mb.src = 1; mb.next = 3; mb.dst = 4; mb.ctr_lo = 6; mb.sf_index = 3;
    mb.rts_flags = RTS_FLAG_M_BROADCAST; mb.payload_len = 20; mb.m_payload_id_lo16 = 0xBEEF;
    std::array<uint8_t, 16> mbb{};
    const size_t mbn = pack_rts(mb, std::span<uint8_t>(mbb.data(), mbb.size()));
    CHECK(mbn > 0);
    hal._now = 1100; node.on_recv(mbb.data(), mbn, RxMeta{8.0f, -80.0f, 0, 1});
    CHECK(node.nav_until_ms() == 0);
}

TEST_CASE("NAV — an overheard CTS reserves DATA+ACK; the reservation scales with the data SF") {
    uint64_t d7 = 0, d12 = 0;
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
      node.on_init(cfg);
      std::array<uint8_t, 8> cb{};
      const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/3, /*data_sf=*/7, cb);   // rx_id=1 != me(2) -> overheard
      hal._now = 1000; node.on_recv(cb.data(), cn, RxMeta{8.0f, -80.0f, 0, 3});
      d7 = node.nav_until_ms() - 1000; }
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
      node.on_init(cfg);
      std::array<uint8_t, 8> cb{};
      const size_t cn = mk_cts(1, 3, /*data_sf=*/12, cb);
      hal._now = 1000; node.on_recv(cb.data(), cn, RxMeta{8.0f, -80.0f, 0, 3});
      d12 = node.nav_until_ms() - 1000; }
    CHECK(d7 > 0);
    CHECK(d12 > d7);                                            // SF12 DATA reserves longer than SF7
}

TEST_CASE("NAV — own RTS for a queued DM defers while the medium is reserved, then flies once it clears") {
    TestHal hal; Node node(hal, /*id=*/1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.lbt_enabled = false; cfg.nav_enabled = true;           // isolate NAV as the only defer source
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/2, bb);   // direct route to bob(2)
    hal._now = 1000; node.on_recv(bb.data(), bn, RxMeta{8.0f, -80.0f, 0, 2});
    // reserve the medium (overheard RTS from 3 -> next 4)
    std::array<uint8_t, 16> ob{}; const size_t on = mk_rts(/*src=*/3, /*next=*/4, /*dst=*/5, 7, 20, ob);
    hal._now = 1500; node.on_recv(ob.data(), on, RxMeta{8.0f, -80.0f, 0, 3});
    CHECK(node.nav_until_ms() > 1500);
    // originate to bob -> the flight decides to RTS, but NAV defers the actual hand-off
    hal._now = 1600; send_cmd(node, /*dst=*/2, "hi");
    CHECK(hal.count("rts_tx") == 1);                            // decided to send
    CHECK(hal.last_tx("RTS") == nullptr);                       // ...but NAV deferred it (not handed to the radio)
    // NAV clears -> draining the LBT-defer slot hands the RTS
    hal._now = node.nav_until_ms() + 10; node.on_timer(kLbtDeferTimerId);
    CHECK(hal.last_tx("RTS") != nullptr);
}

TEST_CASE("NAV — addressed RTS during a reservation: dropped iff nav_ignore_rts, answered by default") {
    std::array<uint8_t, 16> ob{}; const size_t on = mk_rts(/*src=*/3, /*next=*/4, /*dst=*/5, 7, 20, ob);  // overheard -> arms NAV
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, 9, 15, rb);  // addressed to us(2)
    // nav_ignore_rts = true (802.11 blanket-NAV): the addressed RTS is dropped under the reservation.
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true; cfg.nav_ignore_rts = true;
      node.on_init(cfg);
      hal._now = 1000; node.on_recv(ob.data(), on, RxMeta{8.0f, -80.0f, 0, 3});
      CHECK(node.nav_until_ms() > 1000);
      hal._now = 1100; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(hal.count("cts_tx") == 0); }                       // dropped under the reservation
    // DEFAULT (nav_ignore_rts = false, sim-tuned): the SAME RTS during a reservation is still ANSWERED.
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;   // nav_ignore_rts defaults false
      node.on_init(cfg);
      hal._now = 1000; node.on_recv(ob.data(), on, RxMeta{8.0f, -80.0f, 0, 3});
      CHECK(node.nav_until_ms() > 1000);                       // reservation IS active
      hal._now = 1100; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(hal.count("cts_tx") == 1); }                       // ...yet the request is answered (defer, don't refuse)
}

TEST_CASE("NAV — a fresh own DM origination is jittered (nav_enabled), de-syncing simultaneous originators") {
    // nav ON -> the origination is held by the jitter (rand forced > 0), then flies once it elapses
    TestHal hal; Node node(hal, /*id=*/1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.lbt_enabled = false; cfg.nav_enabled = true;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/2, bb);
    hal._now = 1000; node.on_recv(bb.data(), bn, RxMeta{8.0f, -80.0f, 0, 2});      // route to bob(2)
    hal._rand_ret = 200;                                                            // force a non-zero jitter draw
    hal._now = 2000; send_cmd(node, /*dst=*/2, "hi");
    CHECK(hal.count("rts_tx") == 0);                                                // held by the jitter (not even decided yet)
    CHECK(hal.last_tx("RTS") == nullptr);
    hal._now = 2300; node.on_timer(kQueueWakeupTimerId);                            // jitter elapsed -> drain
    CHECK(hal.last_tx("RTS") != nullptr);                                           // now it flies

    // control: nav OFF -> no origination jitter -> the RTS is handed at once
    TestHal h2; Node n2(h2, /*id=*/1, 0xABCD);
    NodeConfig c2; c2.routing_sf = 7; c2.allowed_sf_bitmap = (1u << 12); c2.leaf_id = 0; c2.lbt_enabled = false; c2.nav_enabled = false;   // explicit OFF (firmware default is now ON)
    n2.on_init(c2);
    std::array<uint8_t, 64> b2{}; const size_t bn2 = mk_beacon(2, b2);
    h2._now = 1000; n2.on_recv(b2.data(), bn2, RxMeta{8.0f, -80.0f, 0, 2});
    h2._rand_ret = 200; h2._now = 2000; send_cmd(n2, /*dst=*/2, "hi");
    CHECK(h2.last_tx("RTS") != nullptr);
}

// -----------------------------------------------------------------------------
// Origination — LOCATION piggyback (spec 2026-06-14 §3). enqueue_data sets
// DATA_FLAG_LOCATION iff app_dm && loc_in_dm && (lat||lon) — NEVER on relays/acks.
// Drive a full origination to the DATA frame and read the inner back off the wire.
// -----------------------------------------------------------------------------
namespace {
struct OrigLoc { bool flag = false; bool has_loc = false; int32_t lat = 0, lon = 0; };
static OrigLoc originate_dm_loc(bool loc_in_dm, int32_t lat, int32_t lon) {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.loc_in_dm = loc_in_dm; cfg.lat_e7 = lat; cfg.lon_e7 = lon;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{};
    RxMeta m{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    node.on_recv(bb.data(), mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb), m);
    send_cmd(node, /*dst=*/5, "hi");                          // app DM origination -> RTS to next-hop 2
    std::array<uint8_t, 8> cb{};
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 100; node.on_recv(cb.data(), mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb), bob);
    node.on_timer(kCtsToDataGapTimerId);                      // CTS->DATA gap -> DATA tx
    OrigLoc r{};
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        if (d) {
            r.flag = (d->flags & DATA_FLAG_LOCATION) != 0;
            auto inner = data_inner(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d);
            auto ui = parse_unicast_inner(inner, d->flags);
            if (ui && ui->has_location) { r.has_loc = true; r.lat = ui->lat_e7; r.lon = ui->lon_e7; }
        }
    }
    return r;
}
}  // namespace

TEST_CASE("origination — loc_in_dm + nonzero location sets DATA_FLAG_LOCATION (coords round-trip)") {
    OrigLoc r = originate_dm_loc(/*loc_in_dm=*/true, 523000000, 134050000);
    CHECK(r.flag);
    CHECK(r.has_loc);
    long dlat = static_cast<long>(r.lat) - 523000000; if (dlat < 0) dlat = -dlat;
    long dlon = static_cast<long>(r.lon) - 134050000; if (dlon < 0) dlon = -dlon;
    CHECK(dlat <= 512); CHECK(dlon <= 512);
}

TEST_CASE("origination — LOCATION NOT set when toggle off, nor when location is (0,0)") {
    CHECK_FALSE(originate_dm_loc(/*loc_in_dm=*/false, 523000000, 134050000).flag);   // opted out
    CHECK_FALSE(originate_dm_loc(/*loc_in_dm=*/true,  0, 0).flag);                    // opted in but no fix
}

// -----------------------------------------------------------------------------
// Receive — a delivered DM that carried LOCATION surfaces the coords on the
// msg_recv Push + emits a peer_location telemetry (spec 2026-06-14 §5). DATA only
// (M receive is deferred).
// -----------------------------------------------------------------------------
TEST_CASE("receive — a delivered DM with LOCATION surfaces coords on the Push + emits peer_location") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };          // immediate sender = 1
    const uint8_t bodytext[] = { 'h', 'i' };
    const int32_t LAT = 523000000, LON = 134050000;
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_SOURCE_HASH | DATA_FLAG_LOCATION);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, /*dst_hash*/ 0,
                                         nullptr, 0, 0, /*origin*/ 1, /*source_hash*/ 0xCAFEF00Du,
                                         bodytext, sizeof bodytext, LAT, LON);
    CHECK(il > 0);
    std::array<uint8_t, 16> rb{};
    hal._now = 1000;
    node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5,
                                   /*plen=*/static_cast<uint8_t>(il + 4), rb), meta);
    uint8_t frame[96]; const uint8_t mac4[4] = { 0, 0, 0, 0 };
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(mac4, 4);
    const size_t fl = pack_data(din, std::span<uint8_t>(frame, sizeof frame));
    CHECK(fl > 0);
    hal._now = 2000; node.on_recv(frame, fl, meta);
    node.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) {
        CHECK(pu.has_location);
        long dlat = static_cast<long>(pu.lat_e7) - LAT; if (dlat < 0) dlat = -dlat;
        long dlon = static_cast<long>(pu.lon_e7) - LON; if (dlon < 0) dlon = -dlon;
        CHECK(dlat <= 512); CHECK(dlon <= 512);
    }
    CHECK(hal.count("peer_location") == 1);                          // telemetry emitted for the sim/gate
}

TEST_CASE("receive — a delivered DM WITHOUT location leaves the Push unset (no peer_location)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(1, 2, 2, 5, 15, rb), meta);
    std::array<uint8_t, 64> db{};
    hal._now = 2000; node.on_recv(db.data(), mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/1, "hi", db), meta);
    node.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got); if (got) CHECK_FALSE(pu.has_location);
    CHECK(hal.count("peer_location") == 0);
}

// -----------------------------------------------------------------------------
// Phase 1 §4 — seal-on-send WIRING: an e2e_dm origination must emit a CRYPTED
// DATA (CRYPTED|DST_HASH flags, the 8-B nonce-seed trailer, body sealed). Drives
// the real enqueue_data -> issue_send (seed thread) -> do_data_tx (trailer) path.
// -----------------------------------------------------------------------------
TEST_CASE("e2e wiring — an e2e_dm origination emits a CRYPTED DATA (8-B trailer, body NOT cleartext)") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    // A learns node 2 (=B): a beacon from src=2 binds 2 -> idB.key_hash32 (authoritative) + a direct route to 2.
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    A.on_recv(bb.data(), bn, bm);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);

    send_cmd(A, /*dst=*/2, "secret-dm-xyz");                  // a CRYPTED origination (e2e_dm on, B's pubkey known)
    std::array<uint8_t, 8> cb{}; RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 100; A.on_recv(cb.data(), mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb), bob);
    A.on_timer(kCtsToDataGapTimerId);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            CHECK(d->crypted);                               // CRYPTED set on the wire
            CHECK(d->dst_hash);                              // CRYPTED => DST_HASH
            CHECK(data_mac(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d).size() == 8);  // 8-B nonce-seed trailer
            const char* secret = "secret-dm-xyz"; bool leaked = false;
            for (size_t i = 0; i + 13 <= dataf->bytes.size(); ++i) { bool mm = true; for (int j = 0; j < 13; ++j) if (dataf->bytes[i+j] != uint8_t(secret[j])) mm = false; if (mm) leaked = true; }
            CHECK_FALSE(leaked);                             // the body is sealed (never cleartext on the wire)
        }
    }
}

// Shared setup for the e2e fail-loud tests: A (e2e_dm on) learns B's authoritative pubkey + id_bind (so DST_HASH
// resolves and "no pubkey" is ruled out). Caller decides whether to install A's crypto identity / oversize the body.
static void e2e_learn_peer(Node& A, TestHal& hal, const Identity& idB) {
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    A.on_recv(bb.data(), bn, bm);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
}
static bool saw_ev(const TestHal& hal, const char* type) {
    for (const auto& e : hal.events) if (e.type == type) return true;
    return false;
}

// R3 (review): with e2e_dm ON but NO crypto identity installed (set_crypto_identity never called -> _x_secret is
// zeros), e2e_seal_inner must NOT seal under a zero key (a silent self-blackhole the recipient tag-fails). It must
// FAIL LOUD (e2e_no_identity) and enqueue nothing — never cleartext, never a bogus-key frame, no WANT_PUBKEY flood.
TEST_CASE("R3 fail-loud: e2e_dm without a crypto identity refuses to seal (no zero-key blackhole)") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    // DELIBERATELY no A.set_crypto_identity(...) -> _crypto_ready == false.
    e2e_learn_peer(A, hal, idB);
    send_cmd(A, /*dst=*/2, "secret-no-id");
    CHECK(saw_ev(hal, "e2e_no_identity"));                   // fail loud
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));                  // nothing enqueued (no bogus-key frame)
    CHECK_FALSE(saw_ev(hal, "h_tx"));                        // no spurious WANT_PUBKEY flood (we HAVE the pubkey)
    CHECK_FALSE(saw_ev(hal, "e2e_no_pubkey"));               // not misreported as a missing-pubkey
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;                 // §2/§5: the app is WARNED with the reason
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::no_identity); }
}

// R7 (review): a crypto RNG that returns an all-zero nonce seed collapses nonce uniqueness to the 16-bit ctr ->
// keystream reuse under the static per-pair key (catastrophic). e2e_seal_inner must refuse loudly (e2e_bad_rng),
// never seal with a degenerate nonce.
TEST_CASE("R7 fail-loud: an all-zero crypto seed refuses to seal (no nonce-reuse)") {
    TestHal hal; hal.zero_rng = true;                       // emulate a broken crypto RNG (all-zero seed)
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    e2e_learn_peer(A, hal, idB);
    send_cmd(A, /*dst=*/2, "secret-bad-rng");
    CHECK(saw_ev(hal, "e2e_bad_rng"));                      // fail loud
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));                 // nothing sealed/enqueued
    CHECK_FALSE(saw_ev(hal, "h_tx"));                       // no flood (the RNG is broken, not the pubkey)
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::bad_rng); }
}

// R2 (review): the cleartext enqueue fit-gates omit the +16 Poly1305 tag, so a body in [217,232] sets the DM flags
// and passes them, but e2e_seal_inner then overflows the inner. The shared 0-return handler can't tell overflow from
// no-pubkey, so it misfires a HARD WANT_PUBKEY flood (for a key it HOLDS) and silently drops the DM (anti-flood +
// lost-message bug). The oversize case must fail loud (e2e_seal_too_large) with NO flood.
TEST_CASE("R2 fail-loud: an oversize CRYPTED DM fails loud + does NOT flood (anti-flood / lost-message)") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    e2e_learn_peer(A, hal, idB);
    const std::string big(220, 'x');                       // in [217,232]: cleartext gates pass, CRYPTED (+16 tag) overflows
    send_cmd(A, /*dst=*/2, big.c_str());
    CHECK(saw_ev(hal, "e2e_seal_too_large"));              // fail loud, distinct from no-pubkey
    CHECK_FALSE(saw_ev(hal, "h_tx"));                      // NO spurious WANT_PUBKEY flood
    CHECK_FALSE(saw_ev(hal, "e2e_no_pubkey"));             // not misreported as a missing pubkey
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));                // the DM is not enqueued (it can never fit)
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::too_large); }
}

// §5 (E2E peer-key provisioning, 2026-06-16): key acquisition is USER-DRIVEN, never silently automated. A no-pubkey
// CRYPTED send no longer auto-floods WANT_PUBKEY — it WARNS the app (send_failed{no_pubkey}) and DROPS. The user then
// requests the key on-air (`reqpubkey`) or scans a QR (`peerkey`).
TEST_CASE("§5 no-auto-query — a no-pubkey CRYPTED send warns (send_failed{no_pubkey}) + drops, NO WANT_PUBKEY flood") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    // A learns node 2's id_bind (DST_HASH resolves) but NOT its pubkey -> the seal hits no_pubkey.
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    A.on_recv(bb.data(), bn, bm);                          // (deliberately NO A.peer_key_set for idB)
    send_cmd(A, /*dst=*/2, "secret-no-key");
    CHECK(saw_ev(hal, "e2e_no_pubkey"));                   // fail loud
    CHECK_FALSE(saw_ev(hal, "h_tx"));                      // §5: NO auto WANT_PUBKEY flood (the user must reqpubkey)
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));               // never cleartext
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::no_pubkey); }   // the app is warned -> offer Request-key / Scan-QR
}

// §8b (per-message crypt): a single DM's crypt is decided PER message (sendhashx/sendhash), not only by the global
// e2e_dm. want_crypt = (intent==on)?true : (intent==off)?false : e2e_dm. Drive the full flight + read the DATA frame's
// CRYPTED bit for all four (intent × e2e_dm) corners.
TEST_CASE("§8b per-message crypt — intent overrides e2e_dm; default follows it") {
    auto run = [](bool e2e_dm_on, CryptIntent intent) -> bool {            // returns: was the on-air DATA CRYPTED?
        TestHal hal;
        uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
        Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
        Node A(hal, /*id=*/1, idA.key_hash32);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = e2e_dm_on;
        A.on_init(cfg);
        A.set_crypto_identity(idA.x_secret, idA.ed_pub);
        e2e_learn_peer(A, hal, idB);                                       // id_bind(2) + B's authoritative pubkey
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 2; c.u.send.flags = 0; c.crypt = intent;
        const char* body = "msg"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 3;
        A.on_command(c);
        std::array<uint8_t, 8> cb{}; RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
        hal._now = 100; A.on_recv(cb.data(), mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb), bob);
        A.on_timer(kCtsToDataGapTimerId);
        for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) {
            auto d = parse_data(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
            if (d) return d->crypted;
        }
        return false;
    };
    CHECK(run(/*e2e_dm=*/false, CryptIntent::on));         // force CRYPTED even with e2e_dm OFF (sendhashx)
    CHECK_FALSE(run(/*e2e_dm=*/true,  CryptIntent::off));  // force PLAIN even with e2e_dm ON (sendhash)
    CHECK(run(/*e2e_dm=*/true,  CryptIntent::def));        // default follows e2e_dm (on -> crypted)
    CHECK_FALSE(run(/*e2e_dm=*/false, CryptIntent::def));  // default follows e2e_dm (off -> plain)
}

// -----------------------------------------------------------------------------
// Phase 1 §5 — open-on-receive WIRING: B receives a CRYPTED DATA, do_post_ack
// opens it (seed from the trailer, sender from origin->id_bind) and delivers the
// DECRYPTED plaintext to the app push. Drives handle_data (seed capture) -> do_post_ack.
// -----------------------------------------------------------------------------
TEST_CASE("e2e wiring — B opens a received CRYPTED DM and delivers the plaintext to the push") {
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);

    // A seals a DM to B (A holds B's pubkey).
    TestHal halA; Node A(halA, 1, idA.key_hash32); A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[9] = { 't','o','p','-','s','e','c','r','t' };
    uint8_t inner[96], seed[8];
    Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t il = A.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/idB.key_hash32,
                                       /*origin=*/1, /*ctr=*/0x0005, /*source_hash=*/idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(il > 0);
    uint8_t frame[128];
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(seed, 8);
    const size_t fl = pack_data(din, std::span<uint8_t>(frame, sizeof frame));
    CHECK(fl == 8 + il + 8);                                  // hdr + inner + 8-B nonce-seed trailer

    // B receives it. B holds A's pubkey + learns A's binding (origin 1 -> idA.key_hash32) from a beacon.
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32; bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/static_cast<uint8_t>(il + 8), rb), from1);
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (B.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) {
        CHECK(pu.body_len == 9);
        bool same = true; for (int i = 0; i < 9; ++i) if (pu.body[i] != body[i]) same = false;
        CHECK(same);                                         // the DECRYPTED plaintext was delivered
        CHECK(pu.sender_hash == idA.key_hash32);             // the verified sender
        CHECK(pu.enc);                                       // §8b: a sealed DM is stamped enc=true
    }
}

// §3 — the E2E-ack is gated on a SUCCESSFUL open and TARGETS THE DECRYPTED origin (sealed since §1c). A sealed DM the
// receiver can't open is dropped BEFORE the ack — so a sender that gets no ack assumes "not delivered or not decrypted"
// and retries (the contract's only recovery; there is no per-message "locked" state).
namespace {
// Build a CRYPTED DM A->B (origin=1, ctr=5) with the given extra flags + drive it into B; returns the packed frame len.
size_t e2e_seal_AtoB(Node& A, const Identity& idA, const Identity& idB, uint8_t extra_flags, const char* body,
                     uint8_t* frame, size_t cap, uint8_t* inner, size_t inner_cap, uint8_t seed[8]) {
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | extra_flags;
    uint8_t blen = 0; while (body[blen]) ++blen;
    Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t il = A.e2e_seal_inner(inner, inner_cap, seed, flags, idB.key_hash32, /*origin=*/1, /*ctr=*/0x0005,
                                       /*source_hash=*/idA.key_hash32, 0, 0, reinterpret_cast<const uint8_t*>(body), blen, oc);
    if (il == 0) return 0;
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(seed, 8);
    return pack_data(din, std::span<uint8_t>(frame, cap));
}
}  // namespace
TEST_CASE("§3 e2e-ack — fires only after a successful open, TARGETING the recovered origin") {
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halA; Node A(halA, 1, idA.key_hash32); A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    uint8_t frame[128], inner[96], seed[8];
    const size_t fl = e2e_seal_AtoB(A, idA, idB, /*extra=*/DATA_FLAG_E2E_ACK_REQ, "ack-me", frame, sizeof frame, inner, sizeof inner, seed);
    CHECK(fl > 0);
    // B holds A's key + a route to A (beacon), opens the DM, delivers, and ACKs.
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    std::array<uint8_t, 64> bb{}; beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32; bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts(1, 2, 2, 5, static_cast<uint8_t>(fl - 8), rb), from1);
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);
    const Ev* ack = halB.last("e2e_ack_tx");
    CHECK(ack != nullptr);
    if (ack) CHECK(ack->dst == 1);   // §1c+§3: the cleartext origin is GONE, so dst=1 can ONLY be the origin recovered from the seal
}
TEST_CASE("§3 e2e-ack — a CRYPTED DM the receiver can't open is dropped with NO ack") {
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halA; Node A(halA, 1, idA.key_hash32); A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    uint8_t frame[128], inner[96], seed[8];
    const size_t fl = e2e_seal_AtoB(A, idA, idB, /*extra=*/DATA_FLAG_E2E_ACK_REQ, "ack-me", frame, sizeof frame, inner, sizeof inner, seed);
    CHECK(fl > 0);
    // B does NOT hold A's key -> trial-decrypt finds no candidate -> e2e_open_no_key, silent drop (BEFORE any ack).
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts(1, 2, 2, 5, static_cast<uint8_t>(fl - 8), rb), from1);
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);
    CHECK(halB.count("e2e_open_no_key") >= 1);   // dropped (no key)
    CHECK(halB.count("e2e_ack_tx") == 0);        // and NO ack — a sender seeing no ack must retry (the recovery model)
}

// R6.1 leaf-config membership filter (§3.3): the misconfig gate — a same-leaf neighbour whose advertised config_hash
// diverges from ours is NOT peered (no route learned); a matching one IS. Uses REAL non-zero config_hashes (the test
// default 0 is the "no fingerprint" sentinel that bypasses the gate, so we set the hash explicitly here).
TEST_CASE("R6.1 peering filter — divergent leaf config is not peered; matching config peers (misconfig gate)") {
    TestHal hal; Node A(hal, /*id*/ 1, 0x1111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    cfg.allowed_sf_bitmap = (1u << 12); cfg.duty_cycle = 0.01;       // bitmap {12}, duty 1%, name ""
    CHECK(A.on_init(cfg));
    const uint32_t a_hash = meshroute::leaf_config_hash(cfg.allowed_sf_bitmap, meshroute::duty_to_bp(0.01),
        meshroute::frac_to_bp(cfg.channel_active_fraction), meshroute::ms_to_u16(cfg.channel_min_interval_ms),
        meshroute::ms_to_u16(cfg.dm_min_interval_ms), cfg.leaf_name, cfg.leaf_name_len);   // must match cfg_config_hash()

    auto route_to = [](Node& n, uint8_t dest) {
        for (uint8_t i = 0; i < n.rt_count(); ++i) if (n.rt_at(i).dest == dest) return true;
        return false;
    };
    auto feed = [&](uint32_t config_hash, uint8_t src) {
        beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x2000u + src;
        in.config_hash = config_hash;                                // a REAL advertised fingerprint
        std::array<uint8_t, 64> b{};
        const size_t n = pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
        RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
        A.on_recv(b.data(), n, meta);
    };

    // (1) DIVERGENT config (bitmap {7} != {12}) -> different hash -> NOT peered.
    const uint32_t diverge = meshroute::leaf_config_hash((1u << 7), meshroute::duty_to_bp(0.01),
        meshroute::frac_to_bp(cfg.channel_active_fraction), meshroute::ms_to_u16(cfg.channel_min_interval_ms),
        meshroute::ms_to_u16(cfg.dm_min_interval_ms), nullptr, 0);   // only the bitmap differs -> divergent hash
    CHECK(diverge != a_hash);
    feed(diverge, /*src*/ 2);
    CHECK_FALSE(route_to(A, 2));
    // (2) MATCHING config -> peered (route to src installed).
    feed(a_hash, /*src*/ 3);
    CHECK(route_to(A, 3));
}

// R6.2 config-sync: (A) an unmanaged node hearing a MANAGED beacon adopts the lineage (un-synced) + CONFIG_PULLs;
// (B) a synced member answering a CONFIG_PULL emits a C config frame (cmd 0xB). (Full pull->answer->adopt = the gate.)
TEST_CASE("R6.2 config-sync — unmanaged node pulls on hearing a managed beacon; a member answers") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // (A) JOIN-PULL
    TestHal hj; Node J(hj, /*id=*/7, 0x7777);
    NodeConfig jc; jc.routing_sf = 7; jc.leaf_id = 0; jc.allowed_sf_bitmap = (1u << 12); J.on_init(jc);
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = 0x2002;
    bin.lineage_id = 0xABCD; bin.config_epoch = 3; bin.config_hash = 0x9999;   // managed, non-zero hash
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    hj._now = 1000; J.on_recv(bb.data(), bn, meta);
    CHECK(hj.count("config_pull_tx") >= 1);                 // J pulled the config
    CHECK(J.config().lineage_id == 0xABCD);                 // adopted the target lineage...
    CHECK(J.config().config_epoch == 0);                    // ...but un-synced (epoch 0) -> participation-gated until adopt

    // (B) ANSWER
    TestHal hm; Node M(hm, /*id=*/2, 0x2002);
    NodeConfig mc; mc.routing_sf = 7; mc.leaf_id = 0; mc.allowed_sf_bitmap = (1u << 7) | (1u << 9);
    mc.lineage_id = 0xABCD; mc.config_epoch = 3; M.on_init(mc);
    q_in q{}; q.leaf_id = 0; q.src = 7; q.dest = 2; q.opcode = q_opcode::config_pull; q.pull_lineage = 0xABCD; q.pull_epoch = 0;
    std::array<uint8_t, 16> qb{}; size_t qn = pack_q(q, std::span<uint8_t>(qb.data(), qb.size()));
    hm._now = 1000; M.on_recv(qb.data(), qn, meta);
    CHECK(hm.count("c_config_tx") >= 1);                    // a member answers the pull with a C frame
}

// ★ C config frame (cmd 0xB) BOOTSTRAP: a managed joiner with allowed_sf_bitmap==0 (no data SF — the old routed
// CONFIG_ANSWER could NEVER reach it) DOES receive its config on the control plane. After adopt: sf_list/duty/name
// set, synced (epoch>0), and the post-adopt config_hash EQUALS the source's (§5 — proves no perpetual re-pull loop).
TEST_CASE("C config frame — empty-sf_list joiner adopts on a C frame; hash matches the source (§5)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    const uint16_t src_bitmap = (1u << 6) | (1u << 7);     // the mother's SF set (SF6+SF7)
    const double   src_duty   = 0.001;                      // 0.1% -> duty_bp 10
    const uint16_t src_epoch  = 5;
    const char*    src_name   = "hub";

    // the joiner: NO data SF + un-synced, target lineage already set (it heard the managed beacon that triggered the pull)
    TestHal hj; Node J(hj, /*id=*/7, 0x7777);
    NodeConfig jc; jc.routing_sf = 7; jc.leaf_id = 0; jc.allowed_sf_bitmap = 0; J.on_init(jc);
    J.mutable_config().lineage_id = 0xABCD;
    CHECK(J.config().allowed_sf_bitmap == 0);

    // anti-spam v2: the mother provisions the 3 promoted knobs at non-default values -> the joiner must adopt them.
    const uint16_t src_frac_bp = 2500;    // 0.25
    const uint16_t src_ch_ms   = 15000;
    const uint16_t src_dm_ms   = 4000;
    // a C frame addressed to the joiner: [cmd 0xB | leaf 0][src=2][dst=7] + body
    meshroute::CConfig cc{}; cc.allowed_sf_bitmap = src_bitmap; cc.duty_bp = meshroute::duty_to_bp(src_duty);
    cc.active_fraction_bp = src_frac_bp; cc.ch_interval_ms = src_ch_ms; cc.dm_interval_ms = src_dm_ms;
    cc.config_epoch = src_epoch; cc.leaf_name_len = 3; for (int i = 0; i < 3; ++i) cc.leaf_name[i] = src_name[i];
    uint8_t frame[3 + 12 + 16]; frame[0] = static_cast<uint8_t>((0xB << 4) | 0); frame[1] = 2; frame[2] = 7;
    const size_t bn = meshroute::pack_c_config(cc, frame + 3, sizeof(frame) - 3);
    hj._now = 1000; J.on_recv(frame, 3 + bn, meta);

    CHECK(J.config().allowed_sf_bitmap == src_bitmap);     // sf_list adopted (was 0)
    CHECK(J.config().config_epoch == src_epoch);           // synced (epoch>0) -> participation gate lifts
    CHECK(J.config().leaf_name_len == 3);
    // the 3 promoted anti-spam knobs adopted live into the joiner's NodeConfig
    CHECK(J.config().channel_active_fraction == doctest::Approx(0.25f));
    CHECK(J.config().channel_min_interval_ms == 15000u);
    CHECK(J.config().dm_min_interval_ms == 4000u);
    CHECK(hj.count("leaf_config_adopted") >= 1);
    // §5: the joiner's recomputed config_hash now EQUALS the source's -> no re-pull (the round-trip-through-the-gate invariant)
    const uint16_t src_hash = meshroute::leaf_config_hash(src_bitmap, meshroute::duty_to_bp(src_duty),
        src_frac_bp, src_ch_ms, src_dm_ms, src_name, 3);
    const uint16_t joiner_hash = meshroute::leaf_config_hash(J.config().allowed_sf_bitmap,
        meshroute::duty_to_bp(J.config().duty_cycle), meshroute::frac_to_bp(J.config().channel_active_fraction),
        meshroute::ms_to_u16(J.config().channel_min_interval_ms), meshroute::ms_to_u16(J.config().dm_min_interval_ms),
        J.config().leaf_name, J.config().leaf_name_len);
    CHECK(joiner_hash == src_hash);
}

// R6.2 §6.4 participation gate: an un-synced MANAGED node (lineage!=0, epoch 0) must NOT originate app DMs / F —
// only CONFIG_PULL. A synced node (epoch>0) or UNMANAGED node (lineage 0) originates freely.
TEST_CASE("R6.2 participation gate — un-synced managed node blocks app-DM origination; synced originates") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // un-synced: lineage set (joining) but epoch 0 -> do_send blocked, send_failed{joining}.
    TestHal hu; Node U(hu, /*id=*/7, 0x7777);
    NodeConfig uc; uc.routing_sf = 7; uc.leaf_id = 0; uc.allowed_sf_bitmap = (1u << 12);
    uc.lineage_id = 0xABCD; uc.config_epoch = 0;   // managed target, NOT yet synced
    U.on_init(uc);
    send_cmd(U, /*dst=*/9, "hi");
    CHECK(hu.count("send_failed") >= 1);             // un-synced origination refused (send_failed{joining})
    CHECK(hu.count("tx_enqueue") == 0);             // and nothing enqueued
    // synced: same shape but epoch>0 -> originates (the DM is enqueued).
    TestHal hs; Node S(hs, /*id=*/8, 0x8888);
    NodeConfig sc; sc.routing_sf = 7; sc.leaf_id = 0; sc.allowed_sf_bitmap = (1u << 12);
    sc.lineage_id = 0xABCD; sc.config_epoch = 1;    // synced member
    S.on_init(sc);
    send_cmd(S, /*dst=*/9, "hi");
    CHECK(hs.count("tx_enqueue") >= 1);             // originated (enqueued)
}

// R6.3 §4.1 dynamic config write: an operator write on a MANAGED node bumps epoch = max_seen+1 + re-advertises;
// an UNMANAGED node (lineage 0) write is a no-op (no epoch plane).
TEST_CASE("R6.3 leaf_config_write — managed bumps epoch=max_seen+1 + re-advertises; unmanaged is a no-op") {
    TestHal h; Node n(h, /*id*/5, 0xAAAA);
    NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 7); c.duty_cycle = 0.01;
    c.lineage_id = 0xABCD; c.config_epoch = 3; n.on_init(c);
    n.mutable_config().allowed_sf_bitmap = (1u << 7) | (1u << 9);     // operator changes the data-SF set...
    CHECK(n.leaf_config_write());                                     // ...and commits (deliberate intent)
    CHECK(n.config().config_epoch == 4);                             // 3 -> max_seen(3)+1
    CHECK(h.count("leaf_config_write") == 1);
    // unmanaged -> no-op (returns false, epoch untouched)
    TestHal h2; Node u(h2, 6, 0xBBBB);
    NodeConfig uc; uc.routing_sf = 7; uc.leaf_id = 0; uc.allowed_sf_bitmap = (1u << 7); uc.lineage_id = 0; u.on_init(uc);
    u.mutable_config().allowed_sf_bitmap = (1u << 9);
    CHECK_FALSE(u.leaf_config_write());
    CHECK(u.config().config_epoch == 0);
}

// R6.3 §4.1 LWW: a same-lineage, same-epoch, DIFFERENT-hash beacon resolves by key_hash32 — I LOSE to a higher key
// (pull + adopt theirs, no bump), I WIN vs a lower key (keep mine, no pull). One-sided -> converges, no epoch war.
TEST_CASE("R6.3 LWW tiebreak — same-epoch diff-hash: lose to a higher key (pull), win vs a lower key (keep)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto run = [&](uint32_t my_key, uint32_t their_key, bool expect_pull) {
        TestHal h; Node n(h, /*id*/5, my_key);
        NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 7); c.duty_cycle = 0.01;
        c.lineage_id = 0xABCD; c.config_epoch = 4; n.on_init(c);
        const uint16_t my_hash = meshroute::leaf_config_hash((1u << 7), 10000, 0, 0, 0, nullptr, 0);   // arbitrary base for the XOR-diverge below
        beacon_in b{}; b.leaf_id = 0; b.src = 2; b.key_hash32 = their_key;
        b.lineage_id = 0xABCD; b.config_epoch = 4;
        b.config_hash = static_cast<uint16_t>(my_hash ^ 0x5A5A);                              // guaranteed != my_hash, non-zero
        std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(b, std::span<uint8_t>(bb.data(), bb.size()));
        h._now = 1000; n.on_recv(bb.data(), bn, meta);
        CHECK(h.count("leaf_config_conflict") >= 1);                  // the same-epoch divergence is detected
        CHECK((h.count("config_pull_tx") >= 1) == expect_pull);      // loser pulls; winner doesn't
        CHECK(n.config().config_epoch == 4);                         // NEVER bumps on a tie (no epoch war)
    };
    run(/*my*/0x1000, /*their*/0x2000, /*expect_pull=*/true);        // their key higher -> I lose -> pull
    run(/*my*/0x2000, /*their*/0x1000, /*expect_pull=*/false);       // their key lower  -> I win  -> keep
}

// R6.3 provisioning verbs (live core seam): reset_leaf_epoch_state resets BOTH config_epoch AND _max_seen_epoch, so a
// fresh lineage (create) doesn't inherit the old leaf's epoch numbering. Proven via the epoch a subsequent write yields.
TEST_CASE("R6.3 reset_leaf_epoch_state — resets epoch + max_seen (no leak into a fresh lineage)") {
    TestHal h; Node n(h, /*id*/5, 0xAAAA);
    NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 7); c.duty_cycle = 0.01;
    c.lineage_id = 0xABCD; c.config_epoch = 5; n.on_init(c);
    n.mutable_config().allowed_sf_bitmap = (1u << 9); CHECK(n.leaf_config_write());
    CHECK(n.config().config_epoch == 6);                             // 5 -> 6; leaf_config_write also sets _max_seen_epoch=6
    n.mutable_config().lineage_id = 0x1234; n.reset_leaf_epoch_state(1);   // 'create' a fresh lineage at epoch 1
    CHECK(n.config().config_epoch == 1);
    n.mutable_config().allowed_sf_bitmap = (1u << 11); CHECK(n.leaf_config_write());
    CHECK(n.config().config_epoch == 2);                             // 1 -> 2 (NOT 7) -> max_seen was reset, no leak
}

// R6.3 §7c: a beacon advertising a DIFFERENT wire_version is refused (no peer) + a RATE-LIMITED join_refused; a
// same-version beacon is processed normally.
TEST_CASE("R6.3 §7c — a foreign wire_version beacon is refused + rate-limited; same-version is processed") {
    TestHal h; Node n(h, /*id*/5, 0xAAAA);
    NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; n.on_init(c);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    beacon_in b{}; b.leaf_id = 0; b.src = 2; b.key_hash32 = 0x2002;
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(b, std::span<uint8_t>(bb.data(), bb.size()));
    bb[3] = static_cast<uint8_t>((bb[3] & 0xF0) | 0x0E);              // wire_version 14 != ours (1)
    h._now = 1000; n.on_recv(bb.data(), bn, meta);
    CHECK(h.count("join_refused") == 1);                             // refused (visible, not just telemetry-dropped)
    h._now = 2000; n.on_recv(bb.data(), bn, meta);                   // within the cooldown
    CHECK(h.count("join_refused") == 1);                             // rate-limited -> NOT per-beacon
    // a same-version beacon (untouched nibble) is NOT refused
    std::array<uint8_t, 64> ok{}; size_t okn = pack_beacon(b, std::span<uint8_t>(ok.data(), ok.size()));
    h._now = 3000; n.on_recv(ok.data(), okn, meta);
    CHECK(h.count("join_refused") == 1);                             // unchanged
}

// ============================================================================
// SLICE 2 (asymmetric-link-aware routing, 2026-06-29): store the bidi plane.
// State-only / delivery-neutral: no penalty rides effective_score yet.
// ============================================================================

TEST_CASE("bidi constants — LinkBidi zero-default + constant seeds") {
    using namespace meshroute;
    // LinkBidi: unknown MUST be 0 so a zeroed _link_bidi slot reads as 'unknown'.
    CHECK(static_cast<uint8_t>(LinkBidi::unknown) == 0);
    CHECK(static_cast<uint8_t>(LinkBidi::confirmed) == 1);
    CHECK(static_cast<uint8_t>(LinkBidi::one_way) == 2);
    // Seeds (contract): one_way penalty == peer_silent_penalty_q4 (640 Q4).
    CHECK(protocol::bidi_penalty_one_way_q4 == protocol::peer_silent_penalty_q4);
    CHECK(protocol::bidi_penalty_one_way_q4 == 640);
    // Confirmation freshness TTL == next_hop_live_ttl_ms (20 min).
    CHECK(protocol::bidi_confirm_ttl_ms == protocol::next_hop_live_ttl_ms);
    CHECK(protocol::bidi_confirm_ttl_ms == 1200000u);
    // Slow-reprobe TTL + census headroom seeds.
    CHECK(protocol::link_reprobe_ttl_ms == 60000u);
    CHECK(protocol::heard_set_census_min_headroom == 4);
}

TEST_CASE("bidi state — _link_bidi defaults to unknown; degraded_from_wire defaults false") {
    using namespace meshroute;
    TestHal hal;                                  // defined at top of test_node_r3.cpp
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    // A zero-initialized LayerRuntime: every link reads 'unknown'.
    CHECK(node.link_bidi_state(0)   == LinkBidi::unknown);
    CHECK(node.link_bidi_state(42)  == LinkBidi::unknown);
    CHECK(node.link_bidi_state(254) == LinkBidi::unknown);
    // RtCandidate's new wire-inherited field defaults false (a value-initialized candidate).
    RtCandidate c{};
    CHECK(c.degraded_from_wire == false);
}

TEST_CASE("bidi note_link_confirmed — sets confirmed + stamps confirmed_ms") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    hal._now = 500000;
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    node.note_link_confirmed(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    CHECK(node.link_bidi_confirmed_ms(9) == 500000u);
    // A later re-confirm refreshes the timestamp (still confirmed).
    hal._now = 700000;
    node.note_link_confirmed(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    CHECK(node.link_bidi_confirmed_ms(9) == 700000u);
    // Other links untouched.
    CHECK(node.link_bidi_state(8) == LinkBidi::unknown);
}

TEST_CASE("bidi decay_link_bidi — confirmed decays to UNKNOWN past TTL, never to one_way") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    hal._now = 100000;
    node.note_link_confirmed(9);                                   // confirmed @ 100000
    // Not yet stale: just under the TTL -> stays confirmed.
    hal._now = 100000 + protocol::bidi_confirm_ttl_ms - 1;
    node.decay_link_bidi(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    // At/over the TTL: confirmed -> UNKNOWN (MF6: never one_way — staleness is not positive absence evidence).
    hal._now = 100000 + protocol::bidi_confirm_ttl_ms;
    node.decay_link_bidi(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    // A one_way link is NOT touched by decay (positive evidence persists until gossip/CTS flips it).
    TestHal hal2;
    Node n2(hal2, /*node_id=*/7, /*key_hash32=*/0xABCD);
    n2.on_init(cfg);
    n2.set_link_bidi_for_test(5, LinkBidi::one_way);               // test seam
    hal2._now = 99999999;                                         // way past any TTL
    n2.decay_link_bidi(5);
    CHECK(n2.link_bidi_state(5) == LinkBidi::one_way);
}

TEST_CASE("bidi candidate_degraded — live OR of wire-inherited bit and local one_way") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RtCandidate c{}; c.next_hop = 9; c.score = 0; c.hops = 2;
    // Neither component set -> not degraded.
    CHECK(node.candidate_degraded(c) == false);
    // Wire-inherited bit alone -> degraded.
    c.degraded_from_wire = true;
    CHECK(node.candidate_degraded(c) == true);
    // Clear the wire bit; mark the local link one_way -> degraded (the LIVE component).
    c.degraded_from_wire = false;
    node.set_link_bidi_for_test(9, LinkBidi::one_way);
    CHECK(node.candidate_degraded(c) == true);
    // confirmed local link, no wire bit -> NOT degraded (recomputed live, no stuck-degraded cache).
    node.set_link_bidi_for_test(9, LinkBidi::confirmed);
    CHECK(node.candidate_degraded(c) == false);
    // unknown local link, no wire bit -> NOT degraded.
    node.set_link_bidi_for_test(9, LinkBidi::unknown);
    CHECK(node.candidate_degraded(c) == false);
}

TEST_CASE("bidi hook — a real CTS from our flight's next-hop confirms the link") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    cfg.allowed_sf_bitmap = (1u << 7);                 // permit a data SF so a flight can arm
    node.on_init(cfg);
    hal._now = 1000;
    // Install a route to dest 20 via next-hop 9 and originate a DM so a pending_tx awaits a CTS from 9.
    CHECK(node.route_inject(/*dest=*/20, /*next_hop=*/9, /*hops=*/2, /*score_q4=*/(12 << 4)));
    send_cmd(node, /*dst=*/20, "x");                   // originate via the public send_cmd helper (do_send is private)
    CHECK(node.has_pending_tx());
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    // The real handle_cts flight-match is rx_id==self && tx_id==next (ctr_lo match was dropped — see
    // node_mac_rx.cpp:330): a CTS from 9 (tx_id=9) clearing us (rx_id=7) pins our flight. Use the existing
    // mk_cts idiom (the same one the RTS->CTS->DATA flight tests above use).
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/7, /*tx_id=*/9, /*data_sf=*/7, cb);
    CHECK(cn > 0);
    if (cn > 0) {
        RxMeta meta{8.0f, -80.0f, 0, -1};
        hal._now = 1100;
        node.on_recv(cb.data(), cn, meta);
        // The real CTS from our next-hop confirms 9 is bidirectional.
        CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    }
}

// ---- Slice 3: detection scan + degraded-from-wire inheritance ---------------

TEST_CASE("update_link_bidi_from_beacon: present->confirmed, absent+complete->one_way, absent+incomplete->no change") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // (a) PRESENT: advertiser 7's beacon lists [dest=5, next=5] -> "7 hears 5" -> 5<->7 confirmed.
    beacon_entry present[1] = {};
    present[0].dest = 5; present[0].next = 5; present[0].hops = 1;
    node.test_update_link_bidi_from_beacon(/*advertiser=*/7, present, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::confirmed));
    CHECK(node.bidi_penalty_q4(7) == 0);                     // confirmed => no penalty

    // (b) ABSENT + COMPLETE: advertiser 8's COMPLETE page omits dest=5 -> 8 does NOT hear 5 -> 5->8 one_way.
    beacon_entry other[1] = {};
    other[0].dest = 99; other[0].next = 99; other[0].hops = 1;   // some unrelated dest, NOT self
    node.test_update_link_bidi_from_beacon(/*advertiser=*/8, other, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(8) == static_cast<uint8_t>(LinkBidi::one_way));
    CHECK(node.bidi_penalty_q4(8) == protocol::bidi_penalty_one_way_q4);

    // (c) ABSENT + INCOMPLETE: advertiser 9 truncated its page (complete=false) -> NO state change (stays unknown=0).
    node.test_update_link_bidi_from_beacon(/*advertiser=*/9, other, /*n=*/1, /*complete=*/false);
    CHECK(node.link_bidi_at(9) == static_cast<uint8_t>(LinkBidi::unknown));
    CHECK(node.bidi_penalty_q4(9) == 0);
}

TEST_CASE("endpoint override: a [dest==self] entry confirms (never degrades) the receiver's own link") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // Advertiser 7 lists [dest=5] WITH the degraded wire-bit set (a stale third-party view that 7->5 is one-way).
    // The endpoint that RECEIVED 7's beacon has LIVE proof 7->5 works (it just decoded it), so the scan treats the
    // present self-entry as a CONFIRMATION and ignores the degraded bit entirely (design §1 endpoint override).
    beacon_entry e[1] = {};
    e[0].dest = 5; e[0].next = 5; e[0].hops = 1; e[0].degraded = true;
    node.test_update_link_bidi_from_beacon(/*advertiser=*/7, e, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::confirmed));   // NOT one_way, despite degraded bit
    CHECK(node.bidi_penalty_q4(7) == 0);
}

TEST_CASE("rt_merge: degraded_from_wire is inherited from the incoming entry and CLEARS on a clean re-advert") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);

    // Build advertiser 2's beacon carrying [dest=9, next=8, degraded=1] -> a degraded route to 9 via 2.
    auto make_beacon = [&](bool degraded, std::vector<uint8_t>& out) {
        beacon_entry ent[1] = {};
        ent[0].dest = 9; ent[0].next = 8; ent[0].hops = 2;
        ent[0].score_bucket = 12; ent[0].degraded = degraded;
        beacon_in in{}; in.leaf_id = node.active_layer_id() & 0x0F; in.src = 2; in.key_hash32 = 0x2222;
        in.entries = std::span<const beacon_entry>(ent, 1);
        out.resize(protocol::beacon_max_bytes);
        const size_t len = pack_beacon(in, std::span<uint8_t>(out.data(), out.size()));
        CHECK(len > 0);
        if (len > 0) out.resize(len);
    };

    RxMeta meta{}; meta.snr_db = 6.0f;

    // (1) DEGRADED advert -> the installed candidate for dest 9 via 2 carries degraded_from_wire.
    std::vector<uint8_t> bcn1; make_beacon(/*degraded=*/true, bcn1);
    node.test_ingest_beacon(bcn1.data(), bcn1.size(), meta);
    const RtEntry* e1 = nullptr;
    for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 9) e1 = &node.rt_at(i);
    CHECK(e1 != nullptr);
    if (e1 != nullptr) {
        bool found_deg = false;
        for (uint8_t j = 0; j < e1->n; ++j) if (e1->candidates[j].next_hop == 2) found_deg = e1->candidates[j].degraded_from_wire;
        CHECK(found_deg == true);
    }

    // (2) CLEAN re-advert (same route, degraded=0) -> degraded_from_wire CLEARS (fresh recompute, not sticky-OR).
    std::vector<uint8_t> bcn2; make_beacon(/*degraded=*/false, bcn2);
    hal._now += 1000;
    node.test_ingest_beacon(bcn2.data(), bcn2.size(), meta);
    const RtEntry* e2 = nullptr;
    for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 9) e2 = &node.rt_at(i);
    CHECK(e2 != nullptr);
    if (e2 != nullptr) {
        bool still_deg = false;
        for (uint8_t j = 0; j < e2->n; ++j) if (e2->candidates[j].next_hop == 2) still_deg = e2->candidates[j].degraded_from_wire;
        CHECK(still_deg == false);
    }
}

TEST_CASE("ingest_beacon drives update_link_bidi_from_beacon: complete page omitting self -> one_way") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // Advertiser 7's COMPLETE beacon (heard_set_complete=true) lists [dest=9,next=9,hops=1] but NOT self(5)
    // -> 7 does not hear 5 -> 5->7 one_way must be set by ingest_beacon's scan call.
    beacon_entry ent[1] = {};
    ent[0].dest = 9; ent[0].next = 9; ent[0].hops = 1; ent[0].score_bucket = 12;
    beacon_in in{}; in.leaf_id = node.active_layer_id() & 0x0F; in.src = 7; in.key_hash32 = 0x7777;
    in.heard_set_complete = true;                            // Slice 1 wire bit (byte-3 b4)
    in.entries = std::span<const beacon_entry>(ent, 1);
    std::vector<uint8_t> buf(protocol::beacon_max_bytes);
    const size_t len = pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size()));
    CHECK(len > 0);
    if (len > 0) {
        buf.resize(len);
        RxMeta meta{}; meta.snr_db = 6.0f;
        node.test_ingest_beacon(buf.data(), buf.size(), meta);
        CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::one_way));
        CHECK(node.bidi_penalty_q4(7) == protocol::bidi_penalty_one_way_q4);
    }
}

// ── Asymmetric-link-aware routing, SLICE 4: bidi penalty in effective_score ───
TEST_CASE("§bidi — bidi_penalty_q4 is silent_penalty for one_way, 0 for unknown/confirmed") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.bidi_penalty_q4(2) == 0);                                  // unknown (zeroed slot) -> 0
    node.note_link_confirmed(2);                                          // confirmed (Slice 2)
    CHECK(node.bidi_penalty_q4(2) == 0);                                  // confirmed -> 0
    node.test_set_link_one_way(3);                                        // one_way
    CHECK(node.bidi_penalty_q4(3) == protocol::bidi_penalty_one_way_q4);  // one_way -> the full penalty
    CHECK(node.bidi_penalty_q4(3) == protocol::peer_silent_penalty_q4);   // seed == silent class
}

TEST_CASE("§bidi — a one_way next-hop drops effective_score by the bidi penalty (vs an unknown peer)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));
    CHECK(rt_primary_for(node, 5) == 2);                 // insertion-order tie holds (no bidi state yet)
    node.test_set_link_one_way(2);                       // via 2 is now one_way
    node.note_link_confirmed(3);                         // via 3 confirmed (fan-out re-sorts)
    CHECK(rt_primary_for(node, 5) == 3);                 // §bidi: confirmed via-3 now beats penalized one_way via-2
}

TEST_CASE("§bidi — a confirm/one_way transition re-ranks routes via that next-hop (fan-out)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));   // primary via 2
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));   // alt via 3
    CHECK(rt_primary_for(node, 5) == 2);
    node.test_set_link_one_way(2);          // transition on next-hop 2 -> fans out a re-sort NOW
    CHECK(rt_primary_for(node, 5) == 3);    // via-2 penalized, via-3 (unknown=0) promoted by the transition fan-out
    node.note_link_confirmed(2);            // recovery transition on next-hop 2 -> fan out again (penalty clears)
    CHECK(node.bidi_penalty_q4(2) == 0);    // via-2 recovered: no longer penalized
    CHECK(rt_primary_for(node, 5) == 3);    // stable sort (no id tie-break) keeps via-3 primary on the now-tie — recovery clears the penalty but does NOT spuriously flap the primary back
}

TEST_CASE("§bidi — route_strictly_better ranks confirmed > unknown > one_way at equal score/hops") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/4, /*hops=*/1, /*score=*/200));
    node.test_set_link_one_way(2);          // via 2 = one_way
    node.note_link_confirmed(4);            // via 4 = confirmed  [via 3 stays unknown]
    node.rt_resort_for_pick(5);             // force the full re-sort under the bidi penalties
    CHECK(rt_primary_for(node, 5) != 2);    // one_way is demoted out of primacy
    const RtEntry* e = nullptr; for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 5) e = &node.rt_at(i);
    CHECK(e != nullptr);
    if (e) CHECK(e->candidates[e->n - 1].next_hop == 2);   // one_way sorts LAST among the three
}

TEST_CASE("§bidi — a SOLE one_way route stays selectable: the DM still fires an RTS (no delivery loss)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // ONLY route to dst 5 is via next-hop 2
    node->test_set_link_one_way(2);                        // that sole next-hop is now authoritatively one_way
    CHECK(node->bidi_penalty_q4(2) == protocol::bidi_penalty_one_way_q4);   // it IS penalized in the score
    send_cmd(*node, /*dst=*/5, "hi");                      // originate — must NOT be dropped for lack of a viable hop
    const Ev* r = hal.last("rts_tx");
    CHECK(r != nullptr);                                   // an RTS WAS sent (sole one_way stayed selectable)
    if (r) CHECK(r->next == 2);                            // ...at the one_way next-hop (delivery not lost)
    CHECK(hal.count("send_no_route") == 0);                // not failed as "no route" — the route is viable-for-pick
    delete node;
}

// ── Companion-contract gap fixes: D7 — per-peer DM ctr survives reboot ────────
TEST_CASE("D7 — per-peer ctr floor: a fresh peer resumes above the persisted high-water; peer_ctr_high = max") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_peer_ctr_floor(100);                      // the persisted pre-reboot high-water seeds the boot floor
    CHECK(node.test_next_ctr(9) == 101);                   // a peer NOT yet sent-to this boot resumes ABOVE the floor (no ctr re-mint -> no companion dedup-collision)
    CHECK(node.test_next_ctr(9) == 102);                   // continues from there
    node.test_next_ctr(10);                                // another fresh peer is also floored -> 101
    CHECK(node.peer_ctr_high() == 102);                    // max over ALL peers (9->102, 10->101)
    CHECK(node.test_next_ctr(9) == 103);                   // an already-active peer above the floor is NOT reset down
    node.restore_channel_ctr(500);                         // the self/channel counter is just one _peer_send_counter entry
    CHECK(node.peer_ctr_high() == 500);                    // ...and it feeds the lease high-water too (channel id-reuse fix subsumed)
}

// ── Anti-spam v2 duty-channel-cap, SLICE 0 (inert) ───────────────────────────
TEST_CASE("Slice0 — channel_active_fraction Cfg field default + settable (inert)") {
    NodeConfig cfg;
    CHECK(cfg.channel_active_fraction == doctest::Approx(0.125f));   // seed default (deployment knob, not a wire const)
    cfg.channel_active_fraction = 0.25f;
    CHECK(cfg.channel_active_fraction == doctest::Approx(0.25f));
}

TEST_CASE("Slice0 — channel_duty_budget_ms() is the 5-min D (MF1), 0 when duty disabled (MF2)") {
    {   // duty ENABLED at 1%: D = 0.01 * originator_window_ms(300000) = 3000 ms — the cap basis the limits JSON shows
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.duty_cycle = 0.01;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 3000u);
        CHECK(node.channel_duty_budget_ms() != 36000u);    // MF1 guard: NOT the 1-HOUR budget (0.01*3600000)
    }
    {   // duty DISABLED (shipped default 0.0) -> D == 0 (the legacy-flat-cap sentinel)
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 0u);
    }
    {   // a 10% band scales linearly: 0.10 * 300000 = 30000 ms
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.duty_cycle = 0.10;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 30000u);
    }
}

TEST_CASE("channel_cap_origin — MF2: duty disabled -> legacy flat cap") {
    TestHal hal;
    Node* off = mk_budget_node(hal, /*duty=*/0.0, /*window=*/3600000);   // duty<=0 -> channel_duty_budget_ms()==0
    CHECK(off->channel_duty_budget_ms() == 0u);
    CHECK(off->channel_cap_origin() == meshroute::protocol::cap_channel_origin_legacy);   // == 20
    delete off;
}

TEST_CASE("channel_cap_origin — MF1/MF3 formula: SF, N, and C>=1 floor") {
    // routing SF7 / BW250000 / CR5 / preamble 16; duty 1% over the 5-min window => D = 3000 ms.
    // T_ch(SF) = airtime_routing_ms(43) + airtime_ms(SF,250000,5,16,39); C = max(1, 3000/T_ch).
    auto mk = [](TestHal& h, int data_sf) {
        Node* n = new Node(h, /*id=*/1, /*key=*/0xABCD);
        NodeConfig c; c.routing_sf = 7; c.radio_bw_hz = 250000; c.radio_cr = 5;
        c.allowed_sf_bitmap = (1u << data_sf);                 // single DATA SF -> max_data_sf()==data_sf
        c.duty_cycle = 0.01; c.duty_cycle_window_ms = 3600000; // D (5-min) = 0.01*300000 = 3000
        c.channel_active_fraction = 0.125f;
        n->on_init(c);
        return n;
    };
    auto inject_n = [](Node& n, int N) {
        for (int i = 0; i < N; ++i) n.route_inject(static_cast<uint8_t>(20 + i), /*next=*/2, /*hops=*/2, /*score=*/160);
    };
    // pinned: SF9, small N (N_active floors at 1) -> cap == C
    TestHal h9; Node* n9 = mk(h9, 9);
    CHECK(n9->channel_duty_budget_ms() == 3000u);              // MF1: 5-min D, NOT the 1-h budget
    inject_n(*n9, 4); CHECK(n9->rt_count() == 4);
    const uint16_t cap_sf9_smallN = n9->channel_cap_origin();  // == C (N_active=1)
    delete n9;
    // N dependence (SF9): cap ∝ 1/N — fresh node per N for an exact ratio
    TestHal ha; Node* na = mk(ha, 9); inject_n(*na, 40); CHECK(na->rt_count() == 40);
    const uint16_t cap_sf9_N40 = na->channel_cap_origin(); delete na;       // N_active=floor(0.125*40)=5 -> C/5
    TestHal hb; Node* nb = mk(hb, 9); inject_n(*nb, 100); CHECK(nb->rt_count() == 100);
    const uint16_t cap_sf9_N100 = nb->channel_cap_origin(); delete nb;      // N_active=floor(0.125*100)=12 -> C/12
    CHECK(cap_sf9_N100 <= cap_sf9_N40);                                     // more originators -> smaller share
    CHECK(cap_sf9_N100 >= 1);                                              // clamp floor
    // SF dependence: higher SF -> larger T_ch -> lower cap (same small N)
    TestHal h7; Node* n7 = mk(h7, 7); inject_n(*n7, 4); const uint16_t cap_sf7 = n7->channel_cap_origin(); delete n7;
    TestHal h12; Node* n12 = mk(h12, 12); inject_n(*n12, 4); const uint16_t cap_sf12 = n12->channel_cap_origin(); delete n12;
    // RELATIONAL invariants (robust to the exact airtime): SF7 > SF9 > SF12 caps; N40 < smallN
    CHECK(cap_sf7 > cap_sf9_smallN);
    CHECK(cap_sf9_smallN > cap_sf12);
    CHECK(cap_sf9_N40 < cap_sf9_smallN);
    CHECK(cap_sf9_N40 >= 1);                                   // clamp floor
    // C>=1 floor: tiny D (duty 0.0001 -> D=30 << T_ch) must NOT invert the clamp
    TestHal ht; Node* nt = new Node(ht, 1, 0xABCD);
    NodeConfig ct; ct.routing_sf = 7; ct.radio_bw_hz = 250000; ct.radio_cr = 5;
    ct.allowed_sf_bitmap = (1u << 9); ct.duty_cycle = 0.0001; ct.duty_cycle_window_ms = 3600000;
    ct.channel_active_fraction = 0.125f; nt->on_init(ct);
    CHECK(nt->channel_duty_budget_ms() == 30u);               // 0.0001*300000
    CHECK(nt->channel_cap_origin() == 1);                     // D/T_ch=0 -> C floored to 1 -> cap 1 (no inversion)
    delete nt;
}

TEST_CASE("§mobile 2a — host accepts a mobile (DISCOVER->OFFER, CLAIM registers); static mesh unaffected") {
    TestHal hal; Node host(hal, /*id=*/20, /*key=*/0xAA20);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 4;  // host_mobiles default true
    host.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(-1) };

    // (1) a mobile DISCOVER on a FOREIGN leaf (7) -> leaf-exempt -> the host emits a mobile OFFER
    std::array<uint8_t, 6> db{};
    size_t dn = pack_j_discover({ /*leaf_id=*/7, /*gw=*/false, /*is_mobile=*/true, /*key=*/0xB0B1u }, db);
    hal._now = 1000; host.on_recv(db.data(), dn, meta);
    CHECK(hal.count("mobile_offer_tx") == 1);                 // leaf-exempt worked (a foreign-leaf mobile DISCOVER is accepted)

    // (2) a NON-mobile DISCOVER on a foreign leaf -> leaf filter still applies -> NO offer (static path byte-unchanged)
    std::array<uint8_t, 6> db2{};
    size_t dn2 = pack_j_discover({ 7, false, /*is_mobile=*/false, 0xBEEFu }, db2);
    hal._now = 2000; host.on_recv(db2.data(), dn2, meta);
    CHECK(hal.count("mobile_offer_tx") == 1);                 // unchanged -> the non-mobile foreign DISCOVER was leaf-filtered

    // (3) a mobile CLAIM -> claim-stands: registered, NO reply; idempotent re-CLAIM keeps ONE slot
    std::array<uint8_t, 11> cb{};
    size_t cn = pack_j_claim({ /*leaf_id=*/4, false, /*is_mobile=*/true, /*key=*/0xB0B1u, /*proposed=*/40, /*lease=*/0, /*epoch=*/1, /*nonce=*/0, /*chosen_host=*/20 }, cb);
    hal._now = 3000; host.on_recv(cb.data(), cn, meta);
    CHECK(hal.count("mobile_registered") == 1);
    CHECK(host.mobile_reg_count() == 1);
    hal._now = 4000; host.on_recv(cb.data(), cn, meta);       // same key -> refresh, not a new slot
    CHECK(host.mobile_reg_count() == 1);

    // (4) STATIC regression: a non-mobile CLAIM is handled by the static path (learns the binding); _mobile_reg untouched
    std::array<uint8_t, 11> sc{};
    size_t scn = pack_j_claim({ 4, false, /*is_mobile=*/false, 0xC0C0u, /*proposed=*/50, 0, 1, 0 }, sc);
    hal._now = 5000; host.on_recv(sc.data(), scn, meta);
    CHECK(host.mobile_reg_count() == 1);                      // static CLAIM did NOT touch the mobile registry
    CHECK(hal.count("mobile_registered") == 2);               // still 2 (from the two mobile CLAIMs in step 3) — the static CLAIM added NO mobile registration

    // (5) §chosen-host fix: a mobile CLAIM addressed at a DIFFERENT host (chosen_host_id != us) -> NOT recorded.
    // We are only a flood-hearer, not the host the mobile chose — so we must not mint ourselves a host (else we'd falsely proxy).
    std::array<uint8_t, 11> fb{};
    size_t fn = pack_j_claim({ /*leaf=*/4, false, /*is_mobile=*/true, /*key=*/0xF00Du, /*proposed=*/41, 0, 1, 0, /*chosen_host=*/99 }, fb);
    hal._now = 6000; host.on_recv(fb.data(), fn, meta);
    CHECK(host.mobile_reg_count() == 1);                      // ★ NOT recorded (host 20 != chosen 99)
    CHECK(hal.count("mobile_registered") == 2);               // unchanged — no false host minted
}

TEST_CASE("§mobile 2b — mobile FSM: DISCOVER, collect OFFERs, CLAIM the strongest, adopt; static never arms") {
    constexpr uint32_t kMobDisc = 74, kMobGuard = 75;        // mirror node.h's kMobileDiscover/ClaimGuardTimerId
    TestHal hal; Node mob(hal, /*id=*/0, /*key=*/0x7777);   // unprovisioned mobile
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 4; cfg.is_mobile = true;
    mob.on_init(cfg);
    RxMeta m5{ 5.0f, -80.0f, 0, static_cast<int8_t>(-1) }, m9{ 9.0f, -70.0f, 0, static_cast<int8_t>(-1) };

    // (1) DISCOVER fires (a DISCOVER frame goes out)
    hal._now = 1000; mob.on_timer(kMobDisc);
    CHECK(hal.count("mobile_discover_tx") == 1);

    // (2) collect two OFFERs (from a FOREIGN leaf 7 -> the mobile-OFFER leaf-exemption lets them in)
    auto feed_offer = [&](uint8_t resp, uint32_t rk, uint8_t local, RxMeta& meta) {
        j_offer_in o{}; o.leaf_id=7; o.is_mobile=true; o.responder_node_id=resp; o.responder_key_hash32=rk;
        o.data_sf_bitmap=0x06; o.proposed_mobile_id=local;
        uint8_t buf[9]; size_t n = pack_j_offer(o, buf); mob.on_recv(buf, n, meta);
    };
    feed_offer(30, 0x3030u, 100, m5);
    feed_offer(31, 0x3131u, 101, m9);                       // stronger SNR
    CHECK(mob.mobile_offers_n() == 2);

    // (3) the guard fires -> CLAIM the STRONGEST (local 101 from responder 31) + adopt
    hal._now = 4000; mob.on_timer(kMobGuard);
    CHECK(hal.count("mobile_adopted") == 1);
    CHECK(mob.node_id() == 101);                            // adopted the stronger offer's local-id
    CHECK(mob.mobile_home_id() == 31);                      // homed to the stronger responder

    // (4) no-host: a fresh mobile with no OFFERs -> no adopt, re-arms (backoff)
    TestHal h2; Node mob2(h2, 0, 0x8888u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; c2.is_mobile=true; mob2.on_init(c2);
    h2._now=1000; mob2.on_timer(kMobDisc); h2._now=4000; mob2.on_timer(kMobGuard);
    CHECK(mob2.mobile_home_id() == 0);                      // not adopted (no host)
    CHECK(h2.count("mobile_no_host") == 1);

    // (5) STATIC: a non-mobile node never arms the FSM -> on_timer(kMobDisc) is a no-op
    TestHal h3; Node stat(h3, 20, 0xAA20u);
    NodeConfig c3; c3.routing_sf=8; c3.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c3.leaf_id=4; c3.is_mobile=false; stat.on_init(c3);
    h3._now=1000; stat.on_timer(kMobDisc); stat.on_timer(kMobGuard);
    CHECK(h3.count("mobile_discover_tx") == 0);             // a static node never DISCOVERs
    CHECK(stat.mobile_home_id() == 0);
}

TEST_CASE("§mobile 3a — host H-query proxy: answers for a hosted mobile; a non-host does not") {
    TestHal hal; Node host(hal, /*id=*/20, /*key=*/0xAA20u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4;
    host.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(-1) };
    // register a mobile (a mobile CLAIM -> _mobile_reg[0xB0B1 -> local 40])
    std::array<uint8_t, 11> cb{};
    size_t cn = pack_j_claim({ /*leaf_id=*/4, false, /*is_mobile=*/true, /*key=*/0xB0B1u, /*proposed=*/40, 0, 1, 0, /*chosen_host=*/20 }, cb);
    hal._now = 1000; host.on_recv(cb.data(), cn, meta);
    CHECK(host.mobile_reg_count() == 1);

    // feed a SOFT H-query for the mobile's hash -> the host PROXY-answers (the mobile's own beacon id_bind is skipped, 2b)
    std::array<uint8_t, 8> hb{};
    size_t hn = pack_h({ /*leaf_id=*/4, /*origin=*/30, /*key=*/0xB0B1u, /*ttl=*/3, /*hard=*/false }, hb);
    hal._now = 2000; host.on_recv(hb.data(), hn, meta);
    CHECK(hal.count("h_resolved") == 1);      // the host answered as a proxy for its hosted mobile

    // control: a node hosting NO mobile cannot resolve the mobile's hash -> no answer (byte-identical)
    TestHal h2; Node host2(h2, 21, 0xBB21u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; host2.on_init(c2);
    h2._now=2000; host2.on_recv(hb.data(), hn, meta);
    CHECK(h2.count("h_resolved") == 0);       // _mobile_reg_n==0 -> no proxy
}

TEST_CASE("§mobile 3b A1 — a mobile_src RTS's local-id stays OUT of the global rt (the collision fix); a normal RTS learns") {
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4;
    node.on_init(cfg);
    RxMeta meta{ 9.0f, -70.0f, 0, static_cast<int8_t>(-1) };
    auto feed_rts = [&](uint8_t src, bool mobile_src) {
        rts_in r{}; r.leaf_id=4; r.src=src; r.next=99; r.ctr_lo=1; r.dst=99; r.sf_index=0; r.rts_flags=0; r.payload_len=1;
        r.mobile_src=mobile_src;
        uint8_t b[9]; size_t n = pack_rts(r, b); node.on_recv(b, n, meta);
    };
    // a NORMAL RTS from src 50 -> learned as a 1-hop neighbour (rt grows)
    const uint8_t rc0 = node.rt_count();
    hal._now=1000; feed_rts(/*src*/ 50, /*mobile_src*/ false);
    CHECK(node.rt_count() == rc0 + 1);        // learned
    // a MOBILE_SRC RTS from src 51 (a LOCAL id) -> NOT learned (A1: stays out of the global rt)
    const uint8_t rc1 = node.rt_count();
    hal._now=2000; feed_rts(/*src*/ 51, /*mobile_src*/ true);
    CHECK(node.rt_count() == rc1);            // ★ NOT learned -> a mobile's local-id can't collide the global rt
}
