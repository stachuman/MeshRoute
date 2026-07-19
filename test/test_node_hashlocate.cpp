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
#include "identity.h"
#include "dm_crypto.h"     // L10: forge a frame that WOULD open under the degenerate (all-zero) shared key
#include "monocypher.h"    // L10: crypto_eddsa_to_x25519 / crypto_x25519 to derive the all-zero shared secret

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

struct Ev { std::string type; int64_t node = -1; int64_t key_hash32 = -1; int64_t ttl = -1;
            int64_t to = -1; int64_t target_layer = -1;
            bool authoritative = false; bool has_auth = false;
            bool hard = false; bool has_hard = false;
            std::string source, table, action; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;          // captured TX bytes (the H forward)

    std::vector<std::pair<uint32_t, uint32_t>> armed;     // §F-XL-1: (delay_ms, timer_id) captured from after()
    int      _rand_ret = -1;                              // §F-XL-1: >=0 overrides rand_range (else returns lo)
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t delay, uint32_t id) override { armed.emplace_back(delay, id); return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { return _rand_ret >= 0 ? _rand_ret : lo; }
    // Crypto RNG: a real HW RNG never returns all-zeros (which e2e_seal_inner now refuses, R7). Emulate a
    // non-degenerate deterministic stream so the e2e seal/open round-trip uses a realistic nonce-seed.
    uint8_t  _rb = 0x11;
    void     rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) { _rb = static_cast<uint8_t>(_rb * 31 + 7); o[i] = (_rb == 0 ? 0xA5 : _rb); }
    }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            const EventField& fl = f[i];
            if (fl.type == EventField::T::i64) {
                if (!std::strcmp(fl.key, "node"))            e.node = fl.i;
                else if (!std::strcmp(fl.key, "key_hash32")) e.key_hash32 = fl.i;
                else if (!std::strcmp(fl.key, "ttl"))        e.ttl = fl.i;
                else if (!std::strcmp(fl.key, "to"))         e.to = fl.i;
                else if (!std::strcmp(fl.key, "target_layer")) e.target_layer = fl.i;
            } else if (fl.type == EventField::T::boolean) {
                if (!std::strcmp(fl.key, "authoritative")) { e.authoritative = fl.b; e.has_auth = true; }
                else if (!std::strcmp(fl.key, "hard"))     { e.hard = fl.b; e.has_hard = true; }
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

// An H query (hash-locate flood) from `origin` for `key_hash32` with `ttl`. hard=true skips the cache (reach owner).
static size_t make_h(uint8_t origin, uint32_t key_hash32, uint8_t ttl, std::span<uint8_t> buf, bool hard = false,
                     bool want_pubkey = false, const uint8_t* requester_ed_pub = nullptr) {
    h_in in{}; in.leaf_id = 0; in.origin = origin; in.key_hash32 = key_hash32; in.ttl = ttl; in.hard = hard; in.want_pubkey = want_pubkey;
    if (want_pubkey) for (int i = 0; i < 32; ++i) in.requester_ed_pub[i] = requester_ed_pub ? requester_ed_pub[i] : uint8_t(0xC0 + i);
    return pack_h(in, buf);   // §2: a WANT_PUBKEY H needs a >=40-B buf (8 hdr + 32 pubkey)
}

const Ev* find_ev(const std::vector<Ev>& evs, const char* type) {
    for (const auto& e : evs) if (e.type == type) return &e;
    return nullptr;
}

// Count H frames among captured TX (the resolver may legitimately TX an F RREQ to route its response home;
// "the flood is suppressed" means no H FORWARD went out, not that the radio was silent).
int count_h_tx(const std::vector<std::vector<uint8_t>>& frames) {
    int c = 0;
    for (const auto& f : frames)
        if (parse_h(std::span<const uint8_t>(f.data(), f.size())).has_value()) ++c;
    return c;
}

constexpr uint32_t kAgingTimerId = 2;                     // mirrors Node's private aging-sweep timer id
constexpr uint32_t kHForwardTimerBase = 81;               // §F-XL-1: mirrors Node::kHForwardTimerId (ring base)
constexpr uint32_t kHForwardSlots     = 4;                // §F-XL-1: mirrors Node::kHForwardSlots
constexpr uint32_t kParkRefloodTimerId = 89;              // §F-SL-1: mirrors Node::kParkRefloodTimerId

// §F-XL-1: the H forward is now STASHED + released by a jittered timer (kHForwardTimerId+slot). The in-memory
// Hal never auto-fires timers, so a test that expects the re-broadcast on-air must drive the armed h_forward
// timer(s) to release the stash. (Re-firing a spent slot is a safe no-op — the fire clears the slot len.)
static void fire_h_forwards(Node& node, TestHal& hal) {
    for (auto& [delay, id] : hal.armed)
        if (id >= kHForwardTimerBase && id < kHForwardTimerBase + kHForwardSlots) node.on_timer(id);
}

// Drive a send-by-hash app command (CmdKind::send with dst_hash set, the address-by-hash path).
static CmdResult send_by_hash_cmd(Node& node, uint32_t dst_hash, const uint8_t* body, uint8_t body_len) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_hash = dst_hash; c.body = body; c.body_len = body_len;
    return node.on_command(c);
}

}  // namespace

TEST_CASE("A0 id_bind — a heard beacon binds the sender's key_hash32 -> node_id") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);   // unprovisioned: no self-binding to confuse counts
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    CHECK(node.id_bind_find_by_hash(0x0000BEEF) == 7);   // own hash resolves to self (we can answer for ourselves)
}

TEST_CASE("A0 id_bind — a binding past its TTL is no longer resolved") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.id_bind_ttl_ms = 5000;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.cap_id_bind = 2;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
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

TEST_CASE("A0 id_bind — an authoritative beacon re-key overwrites the same id's binding") {
    // A beacon is a FIRST-HAND (authoritative) assertion, so a node re-keying (same id, new hash) OVERWRITES,
    // not refuses. (The claimed -> refuse path needs a second-hand source = h_relay; it's covered at Phase C.)
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto feed = [&](uint8_t src, uint32_t h) {
        std::array<uint8_t, 64> b{}; const size_t n = make_beacon(src, h, b); node.on_recv(b.data(), n, meta);
    };
    feed(3, 0x00001111);                                 // id 3 -> hash 1111
    CHECK(node.id_bind_find_by_hash(0x00001111) == 3);
    feed(3, 0x00002222);                                 // id 3 re-keys -> NEW hash; authoritative beacon OVERWRITES
    CHECK(node.id_bind_find_by_hash(0x00002222) == 3);   // the new binding wins
    CHECK(node.id_bind_find_by_hash(0x00001111) == -1);  // the old hash for id 3 is gone
    CHECK(node.id_bind_count() == 1);
}

TEST_CASE("A0 id_bind — an AUTHORITATIVE source overwrites a conflicting claimed binding") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000CAFE);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
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

// ---- Phase A: the handle_h flood + resolve handler ------------------------------------------------

TEST_CASE("A handle_h — own hash resolves (HARD/authoritative) and SUPPRESSES the forward") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000DEAD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();

    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000DEAD, /*ttl=*/4, q);
    node.on_recv(q.data(), n, meta);

    const Ev* r = find_ev(hal.events, "h_resolved");
    CHECK(r != nullptr);
    if (r) { CHECK(r->node == 5); CHECK((r->has_auth && r->authoritative)); }   // we ARE the owner -> hard
    CHECK(find_ev(hal.events, "h_forward") == nullptr);  // SUPPRESSED
    CHECK(count_h_tx(hal.tx_frames) == 0);               // no H re-broadcast (the answer's routing RREQ is fine)
}

TEST_CASE("A handle_h — WARM CASE: a node that cached the owner's beacon answers; the flood stops") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);   // B (a relay), not the owner
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // B heard owner C's (id 7) beacon -> cached (7 -> CCCC) authoritative.
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/7, /*hash=*/0x0000CCCC, bcn);
    node.on_recv(bcn.data(), bn, meta);
    hal.tx_frames.clear();

    // A's (id 9) query for C's hash reaches B. B knows it -> answers, does NOT forward.
    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000CCCC, /*ttl=*/4, q);
    node.on_recv(q.data(), n, meta);

    const Ev* r = find_ev(hal.events, "h_resolved");
    CHECK(r != nullptr);
    if (r) CHECK(r->node == 7);                          // resolved to the owner from the cached binding
    CHECK(find_ev(hal.events, "h_forward") == nullptr);  // the flood STOPS here — never reaches C
    CHECK(count_h_tx(hal.tx_frames) == 0);               // no H forward (the answer's routing RREQ is fine)
}

TEST_CASE("A handle_h — unknown hash FORWARDS with TTL-1 (deduped on a re-flood)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();

    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, q);
    node.on_recv(q.data(), n, meta);
    fire_h_forwards(node, hal);                          // §F-XL-1: release the jittered (stashed) forward

    const Ev* fwd = find_ev(hal.events, "h_forward");
    CHECK(fwd != nullptr);
    if (fwd) CHECK(fwd->ttl == 3);                       // TTL decremented
    CHECK(hal.tx_frames.size() == 1);
    if (!hal.tx_frames.empty()) {
        auto pf = parse_h(std::span<const uint8_t>(hal.tx_frames[0].data(), hal.tx_frames[0].size()));
        CHECK(pf.has_value());
        if (pf) { CHECK(pf->origin == 9); CHECK(pf->key_hash32 == 0x0000FACE); CHECK(pf->ttl == 3); }
    }

    // Re-flood of the SAME (origin, hash) -> deduped, no second forward.
    node.on_recv(q.data(), n, meta);
    CHECK(hal.tx_frames.size() == 1);
}

// L7 (2026-07-04 wave-3): the H `ttl` is an unauthenticated wire byte. A forged ttl=255 would re-flood with a
// 255-hop horizon; the forward path must clamp the effective ttl to flood_hop_max before the -1 decrement.
TEST_CASE("L7 — a forged H ttl=255 is clamped to flood_hop_max on forward") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();

    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/255, q);
    node.on_recv(q.data(), n, meta);
    fire_h_forwards(node, hal);                          // §F-XL-1: release the jittered (stashed) forward

    const Ev* fwd = find_ev(hal.events, "h_forward");
    CHECK(fwd != nullptr);
    if (fwd) CHECK(fwd->ttl == static_cast<int64_t>(protocol::flood_hop_max - 1));   // 255 clamped to 16, then -1 = 15
    CHECK(hal.tx_frames.size() == 1);
    if (!hal.tx_frames.empty()) {
        auto pf = parse_h(std::span<const uint8_t>(hal.tx_frames[0].data(), hal.tx_frames[0].size()));
        CHECK(pf.has_value());
        if (pf) CHECK(pf->ttl == static_cast<uint8_t>(protocol::flood_hop_max - 1));   // the on-wire forwarded ttl is clamped
    }
}

// R4 (review): a relay must PRESERVE want_pubkey across an H forward. Otherwise a MULTI-HOP WANT_PUBKEY E2E bootstrap
// reaches the owner with want_pubkey=0 -> the owner answers a plain hash-bind (no ed_pub) instead of the TYPE-5 pubkey
// -> the requester never caches the recipient's ed_pub -> e2e_seal_inner keeps returning no-pubkey. One-hop works; the
// forward dropped the flag (fwd.want_pubkey defaulted false). The forwarded frame must carry want_pubkey=true.
TEST_CASE("R4 handle_h — a forwarded WANT_PUBKEY query PRESERVES the flag (multi-hop E2E bootstrap)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();

    std::array<uint8_t, 40> q{};   // §2: a WANT_PUBKEY H is 40 B (8 hdr + 32 requester pubkey)
    uint8_t reqpub[32]; for (int i = 0; i < 32; ++i) reqpub[i] = uint8_t(0x50 + i);
    const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, q, /*hard=*/true, /*want_pubkey=*/true, reqpub);
    node.on_recv(q.data(), n, meta);
    fire_h_forwards(node, hal);                          // §F-XL-1: release the jittered (stashed) forward

    CHECK(find_ev(hal.events, "h_forward") != nullptr);
    CHECK(hal.tx_frames.size() == 1);
    if (!hal.tx_frames.empty()) {
        auto pf = parse_h(std::span<const uint8_t>(hal.tx_frames[0].data(), hal.tx_frames[0].size()));
        CHECK(pf.has_value());
        if (pf) { CHECK(pf->ttl == 3); CHECK(pf->hard); CHECK(pf->want_pubkey);   // want_pubkey PRESERVED across the hop
                  bool same = true; for (int i = 0; i < 32; ++i) if (pf->requester_ed_pub[i] != reqpub[i]) same = false;
                  CHECK(same); }   // §2: the requester's pubkey is carried across the forward too
    }
}

TEST_CASE("A handle_h — TTL exhausted (ttl=0) does NOT forward; own-query echo is ignored") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();

    std::array<uint8_t, 16> q0{}; const size_t n0 = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/0, q0);
    node.on_recv(q0.data(), n0, meta);
    CHECK(find_ev(hal.events, "h_rx") != nullptr);       // seen
    CHECK(find_ev(hal.events, "h_forward") == nullptr);  // but TTL exhausted -> no forward
    CHECK(hal.tx_frames.empty());

    hal.events.clear();
    std::array<uint8_t, 16> qself{}; const size_t ns = make_h(/*origin=*/5, /*hash=*/0x0000FACE, /*ttl=*/4, qself);
    node.on_recv(qself.data(), ns, meta);                // origin == self -> our own query echoed
    CHECK(find_ev(hal.events, "h_rx") == nullptr);       // ignored entirely
    CHECK(hal.tx_frames.empty());
}

TEST_CASE("A handle_h HARD — skips the cache and forwards to the owner (verify-on-use escalation)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);   // B (a relay) that cached C's binding
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/7, /*hash=*/0x0000CCCC, bcn);
    node.on_recv(bcn.data(), bn, meta);                  // B caches (7 -> CCCC) — would SOFT-resolve
    hal.events.clear(); hal.tx_frames.clear();

    // A HARD query for the cached hash must NOT be answered from cache — it forwards to reach the owner.
    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000CCCC, /*ttl=*/4, q, /*hard=*/true);
    node.on_recv(q.data(), n, meta);
    fire_h_forwards(node, hal);                          // §F-XL-1: release the jittered (stashed) forward

    CHECK(find_ev(hal.events, "h_resolved") == nullptr); // cache SKIPPED — not answered here
    const Ev* fwd = find_ev(hal.events, "h_forward");
    CHECK(fwd != nullptr);
    if (fwd) { CHECK(fwd->ttl == 3); CHECK((fwd->has_hard && fwd->hard)); }   // h_forward carries the hard variant
    CHECK(count_h_tx(hal.tx_frames) == 1);               // the hard query is re-flooded toward the owner
    if (!hal.tx_frames.empty()) {
        auto pf = parse_h(std::span<const uint8_t>(hal.tx_frames[0].data(), hal.tx_frames[0].size()));
        CHECK((pf.has_value() && pf->hard == true));     // the variant is preserved across the forward
    }

    // But a HARD query for B's OWN hash still resolves (the owner always answers, soft or hard).
    hal.events.clear();
    std::array<uint8_t, 16> qo{}; const size_t no = make_h(/*origin=*/9, /*hash=*/0x0000BBBB, /*ttl=*/4, qo, /*hard=*/true);
    node.on_recv(qo.data(), no, meta);
    const Ev* r = find_ev(hal.events, "h_resolved");
    CHECK(r != nullptr);
    if (r) CHECK(r->node == 5);
}

TEST_CASE("A handle_h — variant-aware dedup: a HARD query is not suppressed by a prior SOFT") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();

    std::array<uint8_t, 16> qs{}; const size_t ns = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, qs, /*hard=*/false);
    node.on_recv(qs.data(), ns, meta);                   // SOFT: forwards (unknown) + marks soft-seen
    fire_h_forwards(node, hal);                          // §F-XL-1: release the jittered (stashed) forward
    CHECK(count_h_tx(hal.tx_frames) == 1);

    std::array<uint8_t, 16> qh{}; const size_t nh = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, qh, /*hard=*/true);
    node.on_recv(qh.data(), nh, meta);                   // HARD: a DIFFERENT variant -> NOT suppressed -> forwards
    fire_h_forwards(node, hal);                          // §F-XL-1: release the second (different-slot) forward
    CHECK(count_h_tx(hal.tx_frames) == 2);

    node.on_recv(qh.data(), nh, meta);                   // a repeat HARD IS suppressed by its own seen-entry
    fire_h_forwards(node, hal);                          // (no new forward armed -> spent slots are a no-op)
    CHECK(count_h_tx(hal.tx_frames) == 2);
}

// ==== F-XL-1 (2026-07-18): jittered h_forward de-storm ==========================================
// Sibling relays that heard the SAME H flood copy used to re-tx it at the identical ms — a deterministic
// collision (no capture) at any common/downstream receiver (s27 hello-m4: T4 behind T3 got neither of
// T2+T3's same-ms forwards). The forward is now STASHED + released by a timer armed at a random delay in
// [h_forward_jitter_min_ms, h_forward_jitter_max_ms]; two siblings drawing different values re-tx apart.
TEST_CASE("F-XL-1 handle_h — the forward is jittered (stashed + timer-armed in [min,max]); no immediate TX") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear(); hal.armed.clear();
    hal._rand_ret = 90;                                  // a deterministic jitter draw inside [20,150]

    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, q);
    node.on_recv(q.data(), n, meta);

    CHECK(find_ev(hal.events, "h_forward") != nullptr);  // the DECISION event still fires at receive time
    CHECK(hal.tx_frames.empty());                        // but NOTHING went on air yet — the re-tx is DEFERRED
    // exactly one h_forward-ring timer armed, at the drawn delay, inside the named window
    int armed_fwd = 0; uint32_t armed_delay = 0, armed_id = 0;
    for (auto& [d, id] : hal.armed)
        if (id >= kHForwardTimerBase && id < kHForwardTimerBase + kHForwardSlots) { ++armed_fwd; armed_delay = d; armed_id = id; }
    CHECK(armed_fwd == 1);
    CHECK(armed_id == kHForwardTimerBase);               // first forward -> ring slot 0
    CHECK(armed_delay == 90);                            // == the rand draw
    CHECK(armed_delay >= protocol::h_forward_jitter_min_ms);
    CHECK(armed_delay <= protocol::h_forward_jitter_max_ms);
    // firing the armed timer releases the stashed re-broadcast
    fire_h_forwards(node, hal);
    CHECK(count_h_tx(hal.tx_frames) == 1);
}

TEST_CASE("F-XL-1 handle_h — two sibling relays draw DIFFERENT jitter -> they re-tx at different ms") {
    // two independent relays hearing the identical H flood copy; each draws its own delay (no same-ms collision)
    auto arm_delay_for = [](int rand_ret) -> uint32_t {
        TestHal hal; Node node(hal, /*id=*/5, /*hash=*/0x0000BBBB);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
        node.on_init(cfg);
        hal._rand_ret = rand_ret; hal.armed.clear();
        std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, q);
        node.on_recv(q.data(), n, RxMeta{8.0f, -80.0f, 0, -1});
        for (auto& [d, id] : hal.armed)
            if (id >= kHForwardTimerBase && id < kHForwardTimerBase + kHForwardSlots) return d;
        return 0xFFFFFFFFu;
    };
    const uint32_t da = arm_delay_for(30);               // sibling A's draw
    const uint32_t db = arm_delay_for(140);              // sibling B's draw
    CHECK(da == 30);
    CHECK(db == 140);
    CHECK(da != db);                                     // the whole point: the siblings do NOT key up together
    CHECK(da >= protocol::h_forward_jitter_min_ms); CHECK(da <= protocol::h_forward_jitter_max_ms);
    CHECK(db >= protocol::h_forward_jitter_min_ms); CHECK(db <= protocol::h_forward_jitter_max_ms);
}

TEST_CASE("F-XL-1 handle_h — the fired (jittered) frame is byte-identical to an immediate forward") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear(); hal.armed.clear();
    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, q);
    node.on_recv(q.data(), n, meta);
    fire_h_forwards(node, hal);
    CHECK(hal.tx_frames.size() == 1);
    // the exact bytes the OLD immediate re-tx would have sent: fwd{leaf 0, origin 9, hash FACE, ttl 4-1=3}
    h_in expect{}; expect.leaf_id = 0; expect.origin = 9; expect.key_hash32 = 0x0000FACE; expect.ttl = 3; expect.hard = false;
    uint8_t eb[8 + 32 + 4 + 1 + 32]; const size_t en = pack_h(expect, std::span<uint8_t>(eb, sizeof(eb)));
    if (!hal.tx_frames.empty()) {
        CHECK(en == hal.tx_frames[0].size());
        bool same = (en == hal.tx_frames[0].size());
        for (size_t i = 0; same && i < en; ++i) if (eb[i] != hal.tx_frames[0][i]) same = false;
        CHECK(same);
    }
}

// ---- Phase B: the hash-bind response (codec round-trip + send-side + receive-side) ----------------

TEST_CASE("B codec — hash-bind inner round-trips (6 B, no payload-flags byte; AUTHORITATIVE via frame TYPE)") {
    std::array<uint8_t, 6> buf{};
    hash_bind_inner in{}; in.target_layer = 2; in.node_id = 7; in.key_hash32 = 0xDEADBEEF; in.authoritative = true;
    const size_t n = pack_hash_bind_inner(in, std::span<uint8_t>(buf.data(), buf.size()));
    CHECK(n == 6);
    // 6-B layout: [target_layer][node_id][key_hash32 LE] — no payload-flags byte (H_ANSWER/AUTHORITATIVE ride
    // the frame TYPE, which the caller sets from `authoritative`).
    CHECK(buf[0] == 2);                                  // target_layer
    CHECK(buf[1] == 7);                                  // node_id
    CHECK(buf[2] == 0xEF); CHECK(buf[3] == 0xBE);        // key_hash32 LE
    CHECK(buf[4] == 0xAD); CHECK(buf[5] == 0xDE);
    auto out = parse_hash_bind_inner(std::span<const uint8_t>(buf.data(), n));
    CHECK(out.has_value());
    if (out) {
        CHECK(out->target_layer == 2);
        CHECK(out->node_id == 7);
        CHECK(out->key_hash32 == 0xDEADBEEF);
    }
    // < 6 B -> nullopt
    CHECK(parse_hash_bind_inner(std::span<const uint8_t>(buf.data(), 5)) == std::nullopt);
    // A NORMAL DM inner ([origin][body], flags=0) round-trips as a unicast (a 6-B span also parses as a
    // hash-bind — the two inners are disambiguated by the frame TYPE, not the inner bytes).
    const uint8_t dm[] = { /*origin=*/3, 'h', 'i' };
    auto uni = parse_unicast_inner(std::span<const uint8_t>(dm, sizeof(dm)), /*flags=*/0);
    CHECK(uni.has_value());
    if (uni) { CHECK(uni->origin == 3); CHECK(uni->body.size() == 2); }
}

TEST_CASE("B send — the resolver enqueues a hash-bind response addressed to the H-query origin") {
    TestHal hal;
    Node node(hal, /*node_id=*/2, /*key_hash32=*/0x0000BBBB);   // the owner of BBBB
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.events.clear();

    std::array<uint8_t, 16> q{}; const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000BBBB, /*ttl=*/4, q);
    node.on_recv(q.data(), n, meta);                     // owner resolves its own hash -> sends the answer

    const Ev* e = find_ev(hal.events, "hash_bind_response_enqueued");
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->to == 9);                               // routed home to the querier
        CHECK(e->node == 2);                             // the resolved node_id
        CHECK(e->key_hash32 == 0x0000BBBB);
        CHECK((e->has_auth && e->authoritative));        // owner answer -> authoritative
    }
    CHECK(find_ev(hal.events, "h_forward") == nullptr);  // and the flood was suppressed
}

TEST_CASE("B receive — the origin consumes an H_ANSWER DATA and parses the binding") {
    TestHal hal;
    Node node(hal, /*node_id=*/9, /*key_hash32=*/0x00009999);   // the querier/origin
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal.events.clear();

    // Craft the hash-bind answer inner (BBBB -> node 2, authoritative) and feed the deliver seam.
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 2; hb.key_hash32 = 0x0000BBBB; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative);

    const Ev* rx = find_ev(hal.events, "hash_bind_rx");
    CHECK(rx != nullptr);
    if (rx) {
        CHECK(rx->node == 2);
        CHECK(rx->key_hash32 == 0x0000BBBB);
        CHECK((rx->has_auth && rx->authoritative));
    }
}

// ---- Phase C: consume (C.1) + cache-on-pass (C.2) -------------------------------------------------

TEST_CASE("C.1 consume — the origin caches the resolved binding (h_query, confidence from the answer)") {
    TestHal hal;
    Node node(hal, /*node_id=*/9, /*key_hash32=*/0x00009999);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    CHECK(node.id_bind_find_by_hash(0x0000BBBB) == -1);  // unknown before the answer

    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 2; hb.key_hash32 = 0x0000BBBB; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative);

    CHECK(node.id_bind_find_by_hash(0x0000BBBB) == 2);   // cached -> now resolvable from id_bind
    Node::IdBindConf conf = Node::IdBindConf::claimed;
    node.id_bind_find_by_hash(0x0000BBBB, &conf);
    CHECK(conf == Node::IdBindConf::authoritative);      // owner answer (AUTHORITATIVE) -> cached authoritative
}

TEST_CASE("C.2 cache-on-pass — a forwarder snoops a relayed answer (h_relay) and becomes a future resolver") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);   // a relay — neither querier nor owner
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal.events.clear();

    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 7; hb.key_hash32 = 0x0000CCCC; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative);

    CHECK(node.id_bind_find_by_hash(0x0000CCCC) == 7);   // snooped in transit -> a future resolver (floods shrink)
    CHECK(find_ev(hal.events, "hash_bind_snooped") != nullptr);
}

TEST_CASE("C — a CLAIMED (soft) snoop does NOT override an authoritative binding (the deferred A0 refuse)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/3, /*hash=*/0x00001111, bcn);
    node.on_recv(bcn.data(), bn, meta);                  // authoritative (first-hand beacon): 3 -> 1111
    hal.events.clear();

    // A SOFT (non-authoritative) snooped answer claims id 3 -> a DIFFERENT hash -> CLAIMED conflict -> REFUSE.
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 3; hb.key_hash32 = 0x00002222; hb.authoritative = false;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative);

    CHECK(node.id_bind_find_by_hash(0x00001111) == 3);   // the authoritative binding is kept
    CHECK(node.id_bind_find_by_hash(0x00002222) == -1);  // the claimed conflict refused
    bool conflict = false;
    for (const auto& e : hal.events) if (e.type == "addr_conflict_observed" && e.node == 3) conflict = true;
    CHECK(conflict);
}

// ---- Phase D: send-by-hash (immediate / park+flood / verify-on-use / drain / give-up) -------------

TEST_CASE("D send-by-hash — an AUTHORITATIVE binding sends immediately (no park, no H flood)") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/7, /*hash=*/0x0000CCCC, bcn);
    node.on_recv(bcn.data(), bn, meta);                  // authoritative (first-hand beacon): 7 -> CCCC
    hal.events.clear();

    const uint8_t body[] = { 'h', 'i' };
    const CmdResult r = send_by_hash_cmd(node, /*dst_hash=*/0x0000CCCC, body, sizeof(body));

    CHECK(r.code == CmdCode::queued);
    CHECK(r.ctr != 0);                                   // resolved -> sent now -> a real ctr
    CHECK(find_ev(hal.events, "send_parked_for_hash") == nullptr);  // NOT parked
    CHECK(find_ev(hal.events, "h_tx") == nullptr);                  // and NO flood originated
}

TEST_CASE("D send-by-hash — an UNKNOWN hash parks the DM and floods a SOFT H query") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    hal.events.clear();

    const uint8_t body[] = { 'y', 'o' };
    const CmdResult r = send_by_hash_cmd(node, /*dst_hash=*/0x0000EEEE, body, sizeof(body));

    CHECK(r.code == CmdCode::queued);
    CHECK(r.ctr == 0);                                   // not sent yet — resolving
    const Ev* parked = find_ev(hal.events, "send_parked_for_hash");
    CHECK(parked != nullptr);
    if (parked) CHECK(parked->key_hash32 == 0x0000EEEE);
    const Ev* q = find_ev(hal.events, "h_tx");
    CHECK(q != nullptr);
    if (q) { CHECK(q->key_hash32 == 0x0000EEEE); CHECK((q->has_hard && q->hard == false)); }  // unknown -> SOFT
    CHECK(count_h_tx(hal.tx_frames) == 1);               // the flood actually went on air
}

TEST_CASE("D send-by-hash — a SOFT (claimed) binding parks + floods a HARD query (verify-on-use)") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    // Seed a CLAIMED binding via a soft (non-authoritative) snooped answer: DDDD -> node 4.
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 4; hb.key_hash32 = 0x0000DDDD; hb.authoritative = false;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative);
    Node::IdBindConf conf = Node::IdBindConf::authoritative;
    CHECK(node.id_bind_find_by_hash(0x0000DDDD, &conf) == 4);
    CHECK(conf == Node::IdBindConf::claimed);            // soft -> claimed (not trusted to send blind)
    hal.events.clear();

    const uint8_t body[] = { '?' };
    const CmdResult r = send_by_hash_cmd(node, /*dst_hash=*/0x0000DDDD, body, sizeof(body));

    CHECK(r.ctr == 0);                                   // a soft binding is NOT trusted -> verify first
    CHECK(find_ev(hal.events, "send_parked_for_hash") != nullptr);
    const Ev* q = find_ev(hal.events, "h_tx");
    CHECK(q != nullptr);
    if (q) { CHECK(q->key_hash32 == 0x0000DDDD); CHECK((q->has_hard && q->hard == true)); }  // HARD verify-on-use
}

TEST_CASE("D drain — a hash-bind answer resolves the parked DM and flies it (send_hash_resolved)") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    const uint8_t body[] = { 'h', 'i' };
    send_by_hash_cmd(node, /*dst_hash=*/0x0000EEEE, body, sizeof(body));   // unknown -> parked
    hal.events.clear();

    // The owner's answer arrives: EEEE -> node 6 (authoritative).
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 6; hb.key_hash32 = 0x0000EEEE; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative);

    const Ev* res = find_ev(hal.events, "send_hash_resolved");
    CHECK(res != nullptr);
    if (res) { CHECK(res->key_hash32 == 0x0000EEEE); CHECK(res->node == 6); }  // flown to the resolved id

    // The parked DM has drained — a second identical answer resolves NOTHING (no re-send).
    hal.events.clear();
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative);
    CHECK(find_ev(hal.events, "send_hash_resolved") == nullptr);
}

TEST_CASE("D give-up — a parked DM whose hash never resolves is dropped on the aging sweep") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    const uint8_t body[] = { 'x' };
    send_by_hash_cmd(node, /*dst_hash=*/0x0000EEEE, body, sizeof(body));   // unknown -> parked
    hal.events.clear();

    hal._now = protocol::send_defer_ttl_ms + 1;          // past the give-up window
    node.on_timer(kAgingTimerId);                        // the periodic sweep
    CHECK(find_ev(hal.events, "send_hash_giveup") != nullptr);

    // ...and it's gone — a late answer drains nothing.
    hal.events.clear();
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 6; hb.key_hash32 = 0x0000EEEE; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative);
    CHECK(find_ev(hal.events, "send_hash_resolved") == nullptr);
}

TEST_CASE("D send-by-hash — an oversized body is refused (err_too_large), never parked (no inner[] overrun)") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    hal.events.clear();
    std::array<uint8_t, 240> big{};
    for (auto& b : big) b = 'x';

    // body of dm_max_body_bytes + 1 (240) would overrun TxItem.inner[] at enqueue_data's inner[2+i].
    Command over{}; over.kind = CmdKind::send; over.u.send.dst_hash = 0x0000EEEE;
    over.body = big.data(); over.body_len = static_cast<uint8_t>(protocol::dm_max_body_bytes + 1);
    const CmdResult ro = node.on_command(over);
    CHECK(ro.code == CmdCode::err_too_large);
    CHECK(find_ev(hal.events, "send_parked_for_hash") == nullptr);   // refused BEFORE park
    CHECK(find_ev(hal.events, "h_tx") == nullptr);

    // the SAME bound guards the direct send-by-id path (the latent pre-D overflow).
    Command over_id{}; over_id.kind = CmdKind::send; over_id.u.send.dst_id = 2;
    over_id.body = big.data(); over_id.body_len = static_cast<uint8_t>(protocol::dm_max_body_bytes + 1);
    CHECK(node.on_command(over_id).code == CmdCode::err_too_large);

    // and the exact cap (239) is accepted (unknown hash -> parks).
    Command ok{}; ok.kind = CmdKind::send; ok.u.send.dst_hash = 0x0000EEEE;
    ok.body = big.data(); ok.body_len = protocol::dm_max_body_bytes;
    CHECK(node.on_command(ok).code == CmdCode::queued);
}

// Reconstruct the queried hash from a hash_resolved push (body[0..3] = hash LE).
static uint32_t push_hash(const Push& p) {
    return (uint32_t)p.body[0] | ((uint32_t)p.body[1] << 8) | ((uint32_t)p.body[2] << 16) | ((uint32_t)p.body[3] << 24);
}

TEST_CASE("resolve — own hash and a cached AUTHORITATIVE binding answer immediately (no flood)") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // (a) our OWN hash resolves to self, authoritative, with NO airtime.
    Command rc{}; rc.kind = CmdKind::resolve; rc.u.resolve.dst_hash = 0x00001111; rc.u.resolve.hard = false;
    CHECK(node.on_command(rc).code == CmdCode::queued);
    Push p{};
    CHECK(node.next_push(p));
    CHECK(p.kind == PushKind::hash_resolved);
    CHECK(p.origin == 1);                            // self
    CHECK(p.dst == 1);                               // authoritative
    CHECK(push_hash(p) == 0x00001111u);
    CHECK(count_h_tx(hal.tx_frames) == 0);           // answered from self -> no flood

    // (b) a directly-heard beacon installs an AUTHORITATIVE binding -> resolve answers from cache, no flood.
    std::array<uint8_t, 64> b{};
    const size_t n = make_beacon(/*src=*/7, /*key_hash32=*/0x0000B0B0, b);
    node.on_recv(b.data(), n, meta);
    hal.tx_frames.clear();
    Command rc2{}; rc2.kind = CmdKind::resolve; rc2.u.resolve.dst_hash = 0x0000B0B0; rc2.u.resolve.hard = false;
    CHECK(node.on_command(rc2).code == CmdCode::queued);
    Push p2{};
    CHECK(node.next_push(p2));
    CHECK(p2.kind == PushKind::hash_resolved);
    CHECK(p2.origin == 7);
    CHECK(p2.dst == 1);
    CHECK(push_hash(p2) == 0x0000B0B0u);
    CHECK(count_h_tx(hal.tx_frames) == 0);
}

TEST_CASE("resolve — unknown hash floods H, then the hash-bind answer pushes hash_resolved") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);

    Command rc{}; rc.kind = CmdKind::resolve; rc.u.resolve.dst_hash = 0x0000ABAB; rc.u.resolve.hard = false;
    CHECK(node.on_command(rc).code == CmdCode::queued);
    Push p{};
    CHECK_FALSE(node.next_push(p));                  // unknown -> NO immediate answer
    CHECK(count_h_tx(hal.tx_frames) >= 1);           // it flooded H to find the owner

    // the owner's hash-bind answer arrives (routed to us as an H_ANSWER DATA inner) -> resolve completes.
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 9; hb.key_hash32 = 0x0000ABAB; hb.authoritative = true;
    std::array<uint8_t, 16> inner{};
    const size_t il = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), (uint8_t)il, hb.authoritative);
    CHECK(node.next_push(p));
    CHECK(p.kind == PushKind::hash_resolved);
    CHECK(p.origin == 9);
    CHECK(p.dst == 1);                               // owner answer is authoritative
    CHECK(push_hash(p) == 0x0000ABABu);
}

TEST_CASE("resolve — a hash that never resolves pushes a timeout (node 0) after the TTL") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);

    Command rc{}; rc.kind = CmdKind::resolve; rc.u.resolve.dst_hash = 0x0000DEAD; rc.u.resolve.hard = false;
    CHECK(node.on_command(rc).code == CmdCode::queued);
    Push p{};
    CHECK_FALSE(node.next_push(p));                  // parked, awaiting the flood answer

    hal._now += protocol::send_defer_ttl_ms;         // let the parked resolve age out
    node.on_timer(kAgingTimerId);
    CHECK(node.next_push(p));
    CHECK(p.kind == PushKind::hash_resolved);
    CHECK(p.origin == 0);                            // 0 = unresolved / timeout
    CHECK(push_hash(p) == 0x0000DEADu);
}

TEST_CASE("D re-drain — a beacon that installs the authoritative binding flies a stranded parked DM") {
    TestHal hal;
    Node node(hal, /*node_id=*/1, /*key_hash32=*/0x00001111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    const uint8_t body[] = { 'h', 'i' };
    send_by_hash_cmd(node, /*dst_hash=*/0x0000B3B3, body, sizeof(body));   // unknown -> parked; the H answer is "lost" (never delivered)
    hal.events.clear();

    // bob's periodic beacon arrives carrying his key_hash32 -> AUTHORITATIVE binding -> re-drain on the beacon tick.
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/7, /*hash=*/0x0000B3B3, bcn);
    node.on_recv(bcn.data(), bn, meta);

    const Ev* res = find_ev(hal.events, "send_hash_resolved");
    CHECK(res != nullptr);
    if (res) { CHECK(res->key_hash32 == 0x0000B3B3); CHECK(res->node == 7); }   // flown to the beacon-bound id

    // the parked DM has drained — a second beacon (still authoritative) resolves nothing more (no double-send).
    hal.events.clear();
    node.on_recv(bcn.data(), bn, meta);
    CHECK(find_ev(hal.events, "send_hash_resolved") == nullptr);
}

// =============================================================================
// Phase 1 §6 — E2E peer-pubkey cache (key_hash32 -> ed_pub). Per-LayerRuntime,
// hash-verified, authoritative-never-downgraded, evict-oldest at cap, TTL-aged.
// =============================================================================
TEST_CASE("peer_key — set/find round-trip; a forged ed_pub (hash mismatch) is refused") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 3);
    Identity id{}; identity_from_seed(id, seed);
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative));
    uint8_t out[32] = {}; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != id.ed_pub[i]) same = false;
    CHECK(same); CHECK(conf == Node::PeerKeyConf::authoritative);
    CHECK(node.peer_key_count() == 1);
    CHECK_FALSE(node.peer_key_set(id.key_hash32 ^ 0x1u, id.ed_pub, Node::PeerKeyConf::authoritative));  // hash != ed_pub[:4]
    CHECK_FALSE(node.peer_key_find(id.key_hash32 ^ 0x1u, out));
    CHECK(node.peer_key_count() == 1);                                  // the forged insert did NOT cache
}

TEST_CASE("peer_key — authoritative is never downgraded by an overheard insert") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 9);
    Identity id{}; identity_from_seed(id, seed);
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative));
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::overheard));   // same hash, LOWER conf
    Node::PeerKeyConf conf{}; uint8_t out[32];
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::authoritative);                                    // stayed authoritative
}

TEST_CASE("peer_key — evict the least-recently-seen when the cache is full (cap_peer_keys)") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    Identity first{};
    for (int k = 0; k <= protocol::cap_peer_keys; ++k) {               // cap+1 distinct keys, strictly increasing last_seen
        uint8_t seed[32] = {}; seed[0] = static_cast<uint8_t>(k + 1); seed[1] = 0x5A;
        Identity id{}; identity_from_seed(id, seed);
        hal._now = 1000ull + static_cast<uint64_t>(k) * 10;
        CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative));
        if (k == 0) first = id;
    }
    CHECK(node.peer_key_count() == protocol::cap_peer_keys);           // rolled, not grown
    uint8_t out[32];
    CHECK_FALSE(node.peer_key_find(first.key_hash32, out));            // the oldest was evicted
}

TEST_CASE("peer_key — aged past peer_key_ttl_ms; age_out compacts it away") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 21);
    Identity id{}; identity_from_seed(id, seed);
    hal._now = 1000; CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative));
    hal._now = 1000 + protocol::peer_key_ttl_ms;                       // exactly at TTL -> aged
    uint8_t out[32];
    CHECK_FALSE(node.peer_key_find(id.key_hash32, out));
    node.peer_key_age_out();
    CHECK(node.peer_key_count() == 0);
}

// §1 PINNED tier (E2E peer-key provisioning, 2026-06-16): a QR/manually-scanned key is PINNED — a 3rd tier above
// authoritative that is NEVER overwritten by an on-air answer, NEVER LRU-evicted, and NEVER aged out.
TEST_CASE("PINNED peer key — an on-air answer NEVER overwrites a pinned key (grind-collision resistance, §1)") {
    TestHal hal; Node node(hal, 1, 0xAAAA);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);
    Identity id{}; identity_from_seed(id, seed);
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::pinned));        // QR-scanned -> pinned
    // an on-air answer: SAME key_hash32 (== ed_pub[:4]) but a DIFFERENT full ed_pub (a prefix grind-collision)
    uint8_t fake[32]; for (int i = 0; i < 32; ++i) fake[i] = id.ed_pub[i]; fake[8] ^= 0xFF;   // [:4] unchanged -> passes hash-verify
    CHECK(node.peer_key_set(id.key_hash32, fake, Node::PeerKeyConf::authoritative));      // accepted call, but must be a NO-OP
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::pinned);                                             // still pinned
    bool kept = true; for (int i = 0; i < 32; ++i) if (out[i] != id.ed_pub[i]) kept = false;
    CHECK(kept);                                                                          // the SCANNED key survived the grind
}

TEST_CASE("PINNED peer key — never LRU-evicted; the oldest NON-pinned is evicted instead (§1)") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t pseed[32] = {}; pseed[0] = 0xF0; pseed[1] = 0x11;
    Identity pin{}; identity_from_seed(pin, pseed);
    hal._now = 1000; CHECK(node.peer_key_set(pin.key_hash32, pin.ed_pub, Node::PeerKeyConf::pinned));   // OLDEST last_seen
    for (int k = 0; k < protocol::cap_peer_keys; ++k) {               // cap MORE distinct non-pinned -> forces evictions
        uint8_t seed[32] = {}; seed[0] = static_cast<uint8_t>(k + 1); seed[1] = 0x33;
        Identity id{}; identity_from_seed(id, seed);
        hal._now = 2000ull + static_cast<uint64_t>(k) * 10;
        CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative));
    }
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(pin.key_hash32, out, &conf));            // the pinned key SURVIVED the eviction churn
    CHECK(conf == Node::PeerKeyConf::pinned);
}

TEST_CASE("PINNED peer key — never ages out; an all-pinned full cache refuses a new insert (peer_key_full) (§1)") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 21);
    Identity id{}; identity_from_seed(id, seed);
    hal._now = 1000; CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::pinned));
    hal._now = 1000 + protocol::peer_key_ttl_ms * 3;                  // WAY past the TTL
    uint8_t out[32];
    CHECK(node.peer_key_find(id.key_hash32, out));                    // pinned NEVER ages -> still found
    node.peer_key_age_out();
    CHECK(node.peer_key_count() == 1);                                // age_out kept the pinned entry

    for (int k = 1; k < protocol::cap_peer_keys; ++k) {              // fill the rest of the cache with pinned keys
        uint8_t s[32] = {}; s[0] = static_cast<uint8_t>(k + 1); s[1] = 0x77;
        Identity p{}; identity_from_seed(p, s);
        CHECK(node.peer_key_set(p.key_hash32, p.ed_pub, Node::PeerKeyConf::pinned));
    }
    CHECK(node.peer_key_count() == protocol::cap_peer_keys);          // 16 pinned, cache full
    uint8_t s2[32] = {}; s2[0] = 0xEE; s2[1] = 0x99;
    Identity extra{}; identity_from_seed(extra, s2);
    CHECK_FALSE(node.peer_key_set(extra.key_hash32, extra.ed_pub, Node::PeerKeyConf::authoritative));  // all-pinned -> REFUSE
    CHECK(find_ev(hal.events, "peer_key_full") != nullptr);
    CHECK(node.peer_key_count() == protocol::cap_peer_keys);          // nothing was evicted
}

// =============================================================================
// Phase 1 §4/§5 — E2E seal/open round-trip (the crypto core of seal-on-send /
// open-on-receive, exercised directly via the Node helpers).
// =============================================================================
TEST_CASE("e2e seal/open — A seals a DM to B, B opens it; the inner is actually encrypted; tamper/ctr/spoof all drop") {
    TestHal halA, halB;
    uint8_t seedA[32], seedB[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);   // each learns the other's authoritative pubkey
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);

    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[12] = { 's','e','c','r','e','t','-','h','e','l','l','o' };
    uint8_t inner[128], seed[8];
    Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t n = A.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/idB.key_hash32,
                                      /*origin=*/1, /*ctr=*/7, /*source_hash=*/idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(n == 4 + (1 + 4 + 12) + 16);                              // §1c: aad(dst_hash 4) + ct{origin 1 + source_hash 4 + body 12} + tag(16)
    CHECK(oc == Node::SealOutcome::ok);
    bool leaked = false;                                            // the body must NOT be cleartext anywhere in the inner
    for (size_t i = 0; i + 12 <= n; ++i) { bool m = true; for (int j = 0; j < 12; ++j) if (inner[i+j] != body[j]) m = false; if (m) leaked = true; }
    CHECK_FALSE(leaked);

    uint32_t got_sh = 0, got_origin = 0; bool got_loc = true; int32_t la = 1, lo = 1; uint8_t out[64] = {}; uint8_t outlen = 0;
    CHECK(B.e2e_open_inner(inner, n, seed, flags, /*ctr=*/7, /*sender_hash=*/idA.key_hash32, got_origin, got_sh, got_loc, la, lo, out, outlen));
    CHECK(got_sh == idA.key_hash32);                               // the sealed source_hash == the resolved sender (anti-spoof)
    CHECK(got_origin == 1);                                        // §1c: origin recovered from the SEAL (pt[0]), not cleartext
    CHECK_FALSE(got_loc);
    CHECK(outlen == 12);
    bool same = true; for (int i = 0; i < 12; ++i) if (out[i] != body[i]) same = false; CHECK(same);

    uint8_t t[128]; for (size_t i = 0; i < n; ++i) t[i] = inner[i]; t[6] ^= 0x01;   // a tampered ciphertext byte
    CHECK_FALSE(B.e2e_open_inner(t, n, seed, flags, 7, idA.key_hash32, got_origin, got_sh, got_loc, la, lo, out, outlen));
    CHECK_FALSE(B.e2e_open_inner(inner, n, seed, flags, /*wrong ctr*/ 8, idA.key_hash32, got_origin, got_sh, got_loc, la, lo, out, outlen));
    CHECK_FALSE(B.e2e_open_inner(inner, n, seed, flags, 7, /*wrong sender*/ idA.key_hash32 ^ 0x5u, got_origin, got_sh, got_loc, la, lo, out, outlen));
}

// =============================================================================
// §S4 SEALED_RELAY — the delegated / cross-layer sealed carrier. A seals a relay
// BODY to B under A's own identity; the seal ctr is CARRIED (not the frame ctr);
// B opens it DIRECTED (source_hash names the sender, no trial). The sealed origin
// byte is IGNORED; the sealed source_hash is anti-spoof-verified against the
// clear one the caller passes.
// =============================================================================
TEST_CASE("§S4 SEALED_RELAY — A seals a relay body to B, B opens it directed; the body is actually encrypted") {
    TestHal halA, halB;
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 3); sB[i] = uint8_t(90 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);   // A knows B (to seal)
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);   // B knows A (directed open)
    const uint8_t body[10] = { 'x','l','-','s','e','a','l','e','d','!' };
    uint8_t rbody[128]; Node::SealOutcome oc = Node::SealOutcome::ok;
    const uint8_t rn = A.build_sealed_relay_body(/*target=*/idB.key_hash32, body, sizeof body, rbody, sizeof rbody, oc);
    CHECK(oc == Node::SealOutcome::ok);
    CHECK(rn == 2 + 8 + (1 + 4 + 10) + 16);                        // [seal_ctr 2][seed8 8][ct{origin 1+source_hash 4+body 10}+tag 16]
    bool leaked = false;                                           // the plaintext must not appear in the relay body
    for (size_t i = 0; i + 10 <= rn; ++i) { bool m = true; for (int j = 0; j < 10; ++j) if (rbody[i+j] != body[j]) m = false; if (m) leaked = true; }
    CHECK_FALSE(leaked);
    uint8_t out[64] = {}; uint8_t ol = 0;
    CHECK(B.e2e_open_relay(rbody, rn, /*source_hash=*/idA.key_hash32, out, ol));   // directed by the clear sender
    CHECK(ol == 10);
    bool same = true; for (int i = 0; i < 10; ++i) if (out[i] != body[i]) same = false; CHECK(same);
}

TEST_CASE("§S4 SEALED_RELAY — the directed open under the WRONG source_hash fails loud (anti-spoof / no key)") {
    TestHal halA, halB, halC;
    uint8_t sA[32], sB[32], sC[32]; for (int i = 0; i < 32; ++i) { sA[i]=uint8_t(i+3); sB[i]=uint8_t(90-i); sC[i]=uint8_t(i+40); }
    Identity idA{}, idB{}, idC{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB); identity_from_seed(idC, sC);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    B.peer_key_set(idC.key_hash32, idC.ed_pub, Node::PeerKeyConf::authoritative);   // B also holds a DECOY key
    const uint8_t body[4] = { 't','e','s','t' };
    uint8_t rbody[128]; Node::SealOutcome oc = Node::SealOutcome::ok;
    const uint8_t rn = A.build_sealed_relay_body(idB.key_hash32, body, sizeof body, rbody, sizeof rbody, oc);
    CHECK(rn > 0);
    uint8_t out[64]; uint8_t ol = 0;
    CHECK_FALSE(B.e2e_open_relay(rbody, rn, /*wrong sender=*/idC.key_hash32, out, ol));   // C's key won't open A's seal
    CHECK(ol == 0);
    CHECK(B.e2e_open_relay(rbody, rn, /*right sender=*/idA.key_hash32, out, ol));         // A's key does
}

TEST_CASE("§S4 SEALED_RELAY — a GARBAGE sealed origin byte does NOT break identity (origin is ignored; source_hash rules)") {
    TestHal halA, halB;
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i]=uint8_t(i+3); sB[i]=uint8_t(90-i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    // Hand-build a relay body with a GARBAGE sealed origin (0xAB): seal [origin=0xAB][source_hash=A][body] to B.
    const uint8_t body[5] = { 'h','e','l','l','o' };
    uint8_t inner[96], seed[8]; Node::SealOutcome oc = Node::SealOutcome::ok;
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    const size_t sn = A.e2e_seal_inner(inner, sizeof inner, seed,
                                       DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH, /*dst=*/idB.key_hash32,
                                       /*origin=*/0xAB, /*ctr=*/321, /*source_hash=*/idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(sn > 0);
    uint8_t rbody[128]; rbody[0] = 321 & 0xFF; rbody[1] = (321 >> 8) & 0xFF;   // seal_ctr = 321
    for (int i = 0; i < 8; ++i) rbody[2 + i] = seed[i];
    for (size_t i = 0; i < sn - 4; ++i) rbody[10 + i] = inner[4 + i];          // ct||tag (skip the 4-B aad prefix)
    const uint8_t rn = static_cast<uint8_t>(10 + (sn - 4));
    uint8_t out[64]; uint8_t ol = 0;
    CHECK(B.e2e_open_relay(rbody, rn, /*source_hash=*/idA.key_hash32, out, ol));   // opens despite the garbage origin
    CHECK(ol == 5);
    bool same = true; for (int i = 0; i < 5; ++i) if (out[i] != body[i]) same = false; CHECK(same);
}

TEST_CASE("§S4 -K — suppresses the INTRO attach for one plaintext send; -K on a sealed send is a harmless no-op") {
    TestHal hal;
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 5); sB[i] = uint8_t(70 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);   // A holds B's key (so -e can seal to B)
    A.test_id_bind_set(9, idB.key_hash32, /*authoritative=*/true);
    A.test_suspend_tx_drain(true);
    const uint8_t body[2] = { 'h', 'i' };
    // Baseline: a first-contact plaintext send WITHOUT -K attaches INTRO (B unconfirmed).
    { Command c{}; c.kind = CmdKind::send; c.u.send.dst_hash = idB.key_hash32; c.body = body; c.body_len = 2;
      const uint8_t n0 = A.test_tx_queue_n(); (void)A.on_command(c);
      CHECK(A.test_tx_queue_n() > n0);
      if (A.test_tx_queue_n() > n0) CHECK(A.test_tx_type(n0) == DATA_TYPE_INTRO); }
    // -K suppresses the attach -> a PLAIN DM (type 0), no key prefix.
    { Command c{}; c.kind = CmdKind::send; c.u.send.dst_hash = idB.key_hash32; c.body = body; c.body_len = 2; c.no_intro = true;
      const uint8_t n1 = A.test_tx_queue_n(); (void)A.on_command(c);
      CHECK(A.test_tx_queue_n() > n1);
      if (A.test_tx_queue_n() > n1) CHECK(A.test_tx_type(n1) == 0); }
    // -K -e (sealed) is a no-op: a sealed send never attaches INTRO anyway -> a CRYPTED frame (type 0, CRYPTED flag).
    { Command c{}; c.kind = CmdKind::send; c.u.send.dst_hash = idB.key_hash32; c.body = body; c.body_len = 2; c.no_intro = true; c.crypt = CryptIntent::on;
      const uint8_t n2 = A.test_tx_queue_n(); (void)A.on_command(c);
      CHECK(A.test_tx_queue_n() > n2);
      if (A.test_tx_queue_n() > n2) {
          CHECK(A.test_tx_type(n2) != DATA_TYPE_INTRO);
          uint8_t il = 0; (void)A.test_tx_inner(n2, il);
          CHECK((A.test_tx_flags(n2) & DATA_FLAG_CRYPTED) != 0); } }   // sealed, not attached
}

// =============================================================================
// §1a sealed-sender — TRIAL DECRYPTION: there is no cleartext sender hint on a
// CRYPTED frame; the receiver tries each cached peer key and the Poly1305 tag
// is the oracle that identifies the sender. A node that doesn't hold the
// sender's key has no candidate that opens -> drop.
// =============================================================================
TEST_CASE("§1a trial decrypt — picks the sender's key out of the cache, recovers origin; an un-held sender finds no candidate") {
    TestHal halA, halB;
    uint8_t seedA[32], seedB[32], seedC[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); seedC[i] = uint8_t(i + 60); }
    Identity idA{}, idB{}, idC{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB); identity_from_seed(idC, seedC);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    B.peer_key_set(idC.key_hash32, idC.ed_pub, Node::PeerKeyConf::authoritative);   // a DECOY, installed FIRST (must not false-accept)
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);   // A's real key

    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[5] = { 'h','e','l','l','o' };
    uint8_t inner[96], seed[8]; Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t n = A.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/idB.key_hash32,
                                      /*origin=*/1, /*ctr=*/7, /*source_hash=*/idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(n > 0); CHECK(oc == Node::SealOutcome::ok);

    uint32_t sender = 0, origin = 0, src = 0; bool loc = true; int32_t lat = 1, lon = 1; uint8_t out[64] = {}; uint8_t outlen = 0;
    CHECK(B.e2e_open_trial(inner, n, seed, flags, /*ctr=*/7, sender, origin, src, loc, lat, lon, out, outlen));
    CHECK(sender == idA.key_hash32);                                // the tag picked A's key out of {decoy, A}
    CHECK(src == idA.key_hash32);                                   // sealed source_hash == sender (anti-spoof)
    CHECK(origin == 1);                                             // recovered origin
    CHECK(outlen == 5); { bool same = true; for (int i = 0; i < 5; ++i) if (out[i] != body[i]) same = false; CHECK(same); }

    // a node with B's identity but only the DECOY key -> no candidate opens -> drop
    TestHal halX; Node X(halX, 2, idB.key_hash32); X.on_init(cfg); X.set_crypto_identity(idB.x_secret, idB.ed_pub);
    X.peer_key_set(idC.key_hash32, idC.ed_pub, Node::PeerKeyConf::authoritative);
    uint32_t s2 = 0, o2 = 0, sr2 = 0; bool l2 = false; int32_t a2 = 0, b2 = 0; uint8_t out2[64]; uint8_t ol2 = 0;
    CHECK_FALSE(X.e2e_open_trial(inner, n, seed, flags, 7, s2, o2, sr2, l2, a2, b2, out2, ol2));
}

// =============================================================================
// §1a — a PINNED key (QR-scanned, conf=2) must SEAL and OPEN. The pre-redesign
// gate compared `conf != authoritative`, which wrongly excluded pinned.
// =============================================================================
TEST_CASE("§1a — a PINNED peer key seals AND opens (conf>=authoritative gate; the pinned-exclusion bug)") {
    TestHal halA, halB;
    uint8_t seedA[32], seedB[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::pinned);          // PINNED both directions (QR scan)
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::pinned);

    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[3] = { 'h','i','!' };
    uint8_t inner[96], seed[8]; Node::SealOutcome oc = Node::SealOutcome::no_pubkey;
    const size_t n = A.e2e_seal_inner(inner, sizeof inner, seed, flags, idB.key_hash32,
                                      /*origin=*/1, /*ctr=*/7, idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(n > 0); CHECK(oc == Node::SealOutcome::ok);               // PINNED key SEALS (was rejected before the gate fix)

    uint32_t sender = 0, origin = 0, src = 0; bool loc = false; int32_t lat = 0, lon = 0; uint8_t out[64] = {}; uint8_t outlen = 0;
    CHECK(B.e2e_open_trial(inner, n, seed, flags, 7, sender, origin, src, loc, lat, lon, out, outlen));   // PINNED key OPENS
    CHECK(sender == idA.key_hash32);
    CHECK(origin == 1);
}

// =============================================================================
// §1c sealed-sender — origin is SEALED inside the ciphertext (pt[0]), not in the
// cleartext AAD. A relay (or any overhearer) parsing a CRYPTED inner gets NO
// cleartext origin; only the holder of the per-pair key RECOVERS it from the seal.
// This is the privacy property: relays can't tell who originated a DM.
// =============================================================================
TEST_CASE("§1c sealed origin — origin is RECOVERED from the seal, NOT readable in the cleartext inner") {
    TestHal halA, halB;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);

    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[4] = { 'p','i','n','g' };
    const uint8_t ORIGIN = 0x42;                                    // a distinctive origin that must NOT leak in cleartext
    uint8_t inner[96], seed[8]; Node::SealOutcome oc = Node::SealOutcome::no_pubkey;
    const size_t n = A.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/idB.key_hash32,
                                      ORIGIN, /*ctr=*/9, /*source_hash=*/idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(n > 0); CHECK(oc == Node::SealOutcome::ok);
    // The wire parse must NOT surface a cleartext origin for a CRYPTED inner (it lives sealed in pt[0]).
    auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, n), flags);
    CHECK(ui.has_value());
    if (ui) CHECK(ui->origin == 0);                                // §1c: NO cleartext origin (was inner[4]==0x42 pre-1c -> RED)
    // B (the key holder) RECOVERS origin from the decrypted seal.
    uint32_t got_sender = 0, got_origin = 0, got_src = 0; bool loc = true; int32_t la = 1, lo = 1; uint8_t out[64] = {}; uint8_t outlen = 0;
    CHECK(B.e2e_open_trial(inner, n, seed, flags, /*ctr=*/9, got_sender, got_origin, got_src, loc, la, lo, out, outlen));
    CHECK(got_origin == ORIGIN);                                   // recovered from inside the ciphertext
    CHECK(got_sender == idA.key_hash32);
    CHECK(outlen == 4);
    bool same = true; for (int i = 0; i < 4; ++i) if (out[i] != body[i]) same = false; CHECK(same);
}

TEST_CASE("e2e seal — refuses (returns 0) when the recipient pubkey is unknown (fail-loud, never cleartext)") {
    TestHal hal; uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = uint8_t(i + 5);
    Identity id{}; identity_from_seed(id, seed);
    Node A(hal, 1, id.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); A.on_init(cfg);
    A.set_crypto_identity(id.x_secret, id.ed_pub);
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[3] = { 1, 2, 3 };
    uint8_t inner[64], s8[8];
    Node::SealOutcome oc = Node::SealOutcome::ok;
    CHECK(A.e2e_seal_inner(inner, sizeof inner, s8, flags, /*dst=unknown*/ 0xDEADBEEFu, 1, 7, id.key_hash32, 0, 0, body, 3, oc) == 0);
    CHECK(oc == Node::SealOutcome::no_pubkey);                      // unknown dst -> no_pubkey (the only case that floods)
}

// L10 (2026-07-04, crypto): a peer advertising a LOW-ORDER X25519 point drives the ECDH shared secret ALL-ZERO
// -> dm_kdf yields a key ANY observer can reproduce -> a "sealed" DM is decryptable by everyone while the sender
// believes it confidential. The seal AND open paths now constant-time REJECT an all-zero shared secret. Seam: an
// ALL-ZERO Ed25519 pubkey converts (crypto_eddsa_to_x25519) to a low-order X25519 point whose ECDH with ANY
// secret is all-zero (verified against monocypher directly) — the exact reachable prod path (a cached peer key).
TEST_CASE("L10 — an all-zero ECDH shared secret is REJECTED at seal AND open (low-order X25519 defense)") {
    TestHal hal;
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = uint8_t(i + 11);
    Identity id{}; identity_from_seed(id, seed);
    Node A(hal, 1, id.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); A.on_init(cfg);
    A.set_crypto_identity(id.x_secret, id.ed_pub);

    // The malicious peer's ed_pub = all zeros (a low-order point). Its key_hash32 == ed_pub[:4] == 0, so
    // peer_key_set's hash-verify accepts it; the seal/open then derive an all-zero shared secret.
    uint8_t evil_ed[32] = {};
    const uint32_t evil_hash = 0;                                  // ed_pub[:4] LE == 0
    CHECK(A.peer_key_set(evil_hash, evil_ed, Node::PeerKeyConf::authoritative));   // hash-verifiable -> cached

    // SEAL to the low-order peer -> must REFUSE (return 0, NOT seal under a public secret). We reuse the
    // no_pubkey fail-loud outcome (a refuse-to-send, never cleartext) — the point is the seal produces NO frame.
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[3] = { 9, 8, 7 };
    uint8_t inner[64], s8[8];
    Node::SealOutcome oc = Node::SealOutcome::ok;
    CHECK(A.e2e_seal_inner(inner, sizeof inner, s8, flags, /*dst=*/evil_hash, /*origin=*/1, /*ctr=*/7,
                           id.key_hash32, 0, 0, body, 3, oc) == 0);   // ★ SEAL refused under the degenerate secret
    CHECK(oc == Node::SealOutcome::no_pubkey);                     // refuse-to-send (no frame emitted), never cleartext

    // OPEN: FORGE a frame that WOULD open validly under the all-zero shared key (exactly what any observer could
    // craft once the secret is public), then confirm e2e_open_inner REJECTS it — proving the zero-check DROPS a
    // frame BEFORE dm_open would succeed (not merely a tag mismatch on junk).
    uint8_t peer_x[32]; ed_pub_to_x25519(peer_x, evil_ed);         // low-order point
    uint8_t shared[32]; crypto_x25519(shared, id.x_secret, peer_x);  // == all zeros (the public secret)
    uint8_t fkey[32]; dm_kdf(fkey, shared, id.key_hash32, evil_hash);   // open derives dm_kdf(shared, _key_hash32, sender_hash)
    const uint8_t seed8[8] = { 1,2,3,4,5,6,7,8 };
    const uint16_t ctr = 7;
    uint8_t fnonce[24]; dm_nonce(fnonce, seed8, ctr, id.key_hash32);    // open uses _key_hash32 (we are dst)
    // The forged inner = [aad 4][ct][tag 16]. aad = [dst_hash 4 LE] = our key (matches the open's aad slice).
    uint8_t aad[4] = { uint8_t(id.key_hash32), uint8_t(id.key_hash32 >> 8),
                       uint8_t(id.key_hash32 >> 16), uint8_t(id.key_hash32 >> 24) };
    const uint8_t pt[5] = { /*origin*/ 0x42, 'h','i','!','!' };    // no SOURCE_HASH set below -> just [origin][body]
    uint8_t forged[64] = {}; for (int i = 0; i < 4; ++i) forged[i] = aad[i];
    uint8_t ftag[DM_TAG_LEN];
    dm_seal(forged + 4, ftag, fkey, fnonce, aad, 4, pt, sizeof pt);
    for (int i = 0; i < DM_TAG_LEN; ++i) forged[4 + sizeof(pt) + i] = ftag[i];
    const size_t flen = 4 + sizeof(pt) + DM_TAG_LEN;
    const uint8_t open_flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH;   // NO SOURCE_HASH -> pt is [origin][body]
    uint32_t got_origin = 1, got_sh = 1; bool got_loc = true; int32_t la = 1, lo = 1; uint8_t out[64] = {}; uint8_t outlen = 1;
    CHECK_FALSE(A.e2e_open_inner(forged, flen, seed8, open_flags, ctr, /*sender_hash=*/evil_hash,
                                 got_origin, got_sh, got_loc, la, lo, out, outlen));   // ★ OPEN rejects the zero-key frame it WOULD otherwise decrypt
}

// =============================================================================
// Phase 1 §6 — the over-the-air pubkey wire: WANT_PUBKEY query -> owner TYPE-5 answer -> cache.
// =============================================================================
TEST_CASE("e2e pubkey wire — on_hash_bind_pubkey caches the owner's ed_pub (authoritative)") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 7);
    Identity id{}; identity_from_seed(id, seed);
    hash_bind_pubkey_inner hb{}; hb.target_layer = 0; hb.node_id = 9;
    for (int i = 0; i < 32; ++i) hb.ed_pub[i] = id.ed_pub[i];
    uint8_t inner[34]; const size_t n = pack_hash_bind_pubkey_inner(hb, std::span<uint8_t>(inner, sizeof inner));
    node.on_hash_bind_pubkey(inner, static_cast<uint8_t>(n));
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));            // resolved by key_hash32 (== ed_pub[:4])
    CHECK(conf == Node::PeerKeyConf::authoritative);
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != id.ed_pub[i]) same = false; CHECK(same);
    // §7: caching a recipient's key also pushes peer_key_cached so the app prompts "secure send ready — resend".
    Push p{}; bool cached = false;
    while (node.next_push(p)) if (p.kind == PushKind::peer_key_cached) { cached = true; break; }
    CHECK(cached);
    CHECK(p.sender_hash == id.key_hash32);                           // which contact's key arrived
}

// §6 (E2E peer-key provisioning): `reqpubkey <hash>` is the user-triggered on-air request — now the ONLY thing that
// fires a WANT_PUBKEY (besides a relay forwarding one). Exactly ONE HARD + want_pubkey H query for the asked hash.
TEST_CASE("§6 reqpubkey — fires ONE hard WANT_PUBKEY H query carrying OUR pubkey (mutual)") {
    TestHal hal; Node node(hal, 5, 0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = uint8_t(i + 5);
    Identity me{}; identity_from_seed(me, seed);
    node.set_crypto_identity(me.x_secret, me.ed_pub);               // §2: reqpubkey now needs our own pubkey to attach
    hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::reqpubkey; c.u.resolve.dst_hash = 0x0000FACE;
    const CmdResult r = node.on_command(c);
    CHECK(r.code == CmdCode::queued);
    int n_h = 0; bool hard_wp = false; bool pub_ok = false;
    for (const auto& f : hal.tx_frames) {
        auto pf = parse_h(std::span<const uint8_t>(f.data(), f.size()));
        if (pf) { ++n_h;
            if (pf->hard && pf->want_pubkey && pf->key_hash32 == 0x0000FACEu) {
                hard_wp = true;
                bool same = true; for (int i = 0; i < 32; ++i) if (pf->requester_ed_pub[i] != me.ed_pub[i]) same = false;
                pub_ok = same;
            } }
    }
    CHECK(n_h == 1);                                                 // exactly one query (not a storm)
    CHECK(hard_wp);                                                  // HARD + want_pubkey for the requested hash
    CHECK(pub_ok);                                                   // §2: the H carries OUR ed_pub so the owner caches us
}

// §2: a reqpubkey from a node with NO crypto identity must FAIL LOUD (can't provide our pubkey for the mutual cache) — no flood.
TEST_CASE("§2 reqpubkey without a crypto identity -> fail loud (h_want_pubkey_no_identity), no H flood") {
    TestHal hal; Node node(hal, 5, 0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);   // NOTE: no set_crypto_identity
    hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::reqpubkey; c.u.resolve.dst_hash = 0x0000FACE;
    node.on_command(c);
    CHECK(find_ev(hal.events, "h_want_pubkey_no_identity") != nullptr);
    int n_h = 0; for (const auto& f : hal.tx_frames) if (parse_h(std::span<const uint8_t>(f.data(), f.size()))) ++n_h;
    CHECK(n_h == 0);                                                 // no WANT_PUBKEY H flooded without an identity
}

// §2 MUTUAL — the WANT_PUBKEY owner CACHES the requester's key (from the H's appended pubkey) BEFORE answering, so it
// can decrypt the requester's future sealed DMs (the exchange provisions BOTH directions in one round, no QR/2nd req).
TEST_CASE("§2 handle_h — a WANT_PUBKEY owner CACHES the requester's key + answers TYPE-5") {
    TestHal hal;
    uint8_t oseed[32], rseed[32]; for (int i = 0; i < 32; ++i) { oseed[i] = uint8_t(i + 1); rseed[i] = uint8_t(200 - i); }
    Identity owner_id{}, req_id{}; identity_from_seed(owner_id, oseed); identity_from_seed(req_id, rseed);
    Node owner(hal, /*id=*/5, owner_id.key_hash32);                  // owner is authoritative for its OWN hash
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    owner.on_init(cfg);
    owner.set_crypto_identity(owner_id.x_secret, owner_id.ed_pub);   // crypto_ready so it can answer TYPE-5
    RxMeta meta{8.0f, -80.0f, 0, -1};
    hal.tx_frames.clear();
    std::array<uint8_t, 40> q{};                                     // a WANT_PUBKEY H for the owner's hash, carrying the requester's pubkey
    const size_t n = make_h(/*origin=*/9, owner_id.key_hash32, /*ttl=*/4, q, /*hard=*/true, /*want_pubkey=*/true, req_id.ed_pub);
    owner.on_recv(q.data(), n, meta);
    // the owner CACHED the requester's authoritative key
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(owner.peer_key_find(req_id.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::authoritative);
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != req_id.ed_pub[i]) same = false; CHECK(same);
    // review#3: it ALSO learned the requester's id_bind (node 9 -> requester_hash) so it can ADDRESS a seal-back w/o a beacon
    CHECK(owner.id_bind_find_by_hash(req_id.key_hash32) == 9);
    // review#10/#11: a peer_key_cached PUSH (not just telemetry) fires so the app knows it can securely reply
    Push pu{}; bool pushed = false;
    while (owner.next_push(pu)) if (pu.kind == PushKind::peer_key_cached && pu.sender_hash == req_id.key_hash32) { pushed = true; break; }
    CHECK(pushed);
    CHECK(find_ev(hal.events, "peer_key_cached") != nullptr);        // the §7-aligned telemetry (hash + node)
    CHECK(find_ev(hal.events, "hash_bind_pubkey_response_enqueued") != nullptr);   // and it answers TYPE-5 (its own pubkey back)
}

// §2 review#1 — the WANT_PUBKEY answer is gated on OWN-HASH (node_id==_node_id), NOT just `authoritative`. A non-owner
// cache-holder that resolves a SOFT want_pubkey via its id_bind must NOT answer with its OWN pubkey (a blackhole) nor
// cache the requester — it falls through to the plain hash-bind resolve (and the flood still reaches the true owner).
TEST_CASE("§2 review#1 — a non-owner cache-holder does NOT answer a SOFT WANT_PUBKEY with its own key") {
    TestHal hal;
    uint8_t hseed[32], rseed[32]; for (int i = 0; i < 32; ++i) { hseed[i] = uint8_t(i + 9); rseed[i] = uint8_t(150 - i); }
    Identity holder_id{}, req_id{}; identity_from_seed(holder_id, hseed); identity_from_seed(req_id, rseed);
    Node holder(hal, /*id=*/5, holder_id.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    holder.on_init(cfg);
    holder.set_crypto_identity(holder_id.x_secret, holder_id.ed_pub);
    const uint32_t owner_hash = 0x0000FACE;                          // a hash the holder knows the owner(7) of, but is NOT
    // seed the holder's authoritative id_bind via a beacon FROM owner(7) carrying owner_hash (7 -> owner_hash)
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 7; be.next = 7; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 7; bin.key_hash32 = owner_hash; bin.entries = std::span<const beacon_entry>(&be, 1);
    holder.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(7)});
    hal.tx_frames.clear();
    std::array<uint8_t, 40> q{};                                     // a SOFT (hard=false) WANT_PUBKEY for owner_hash
    const size_t n = make_h(/*origin=*/9, owner_hash, /*ttl=*/4, q, /*hard=*/false, /*want_pubkey=*/true, req_id.ed_pub);
    holder.on_recv(q.data(), n, RxMeta{8.0f, -80.0f, 0, -1});
    uint8_t out[32]; Node::PeerKeyConf pc{};
    CHECK_FALSE(holder.peer_key_find(req_id.key_hash32, out, &pc));  // did NOT cache the requester (we're not the owner)
    CHECK(find_ev(hal.events, "hash_bind_pubkey_response_enqueued") == nullptr);   // and did NOT send a wrong-key TYPE-5
}

// §2 review#14 — a WANT_PUBKEY H is its OWN flood-dedup variant: a prior plain HARD H for the same (origin,hash) must
// NOT suppress the later WANT_PUBKEY forward (else multi-hop mutual provisioning fails behind a prior locate).
TEST_CASE("§2 review#14 — a prior plain HARD H does NOT suppress a later WANT_PUBKEY H forward") {
    TestHal hal; Node relay(hal, /*id=*/5, 0x0000BBBB);             // a relay (NOT the owner of 0xFACE) -> it FORWARDS
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    relay.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // a plain HARD H for (origin 9, hash 0xFACE) -> the relay forwards + marks (9,0xFACE,hard,!wp) seen
    std::array<uint8_t, 16> q1{}; const size_t n1 = make_h(/*origin=*/9, 0x0000FACE, /*ttl=*/4, q1, /*hard=*/true);
    relay.on_recv(q1.data(), n1, meta);
    fire_h_forwards(relay, hal);                                    // §F-XL-1: release the jittered (stashed) forward
    const int fwd_after_plain = count_h_tx(hal.tx_frames);
    CHECK(fwd_after_plain >= 1);
    // a HARD WANT_PUBKEY H for the SAME (origin, hash) -> a DIFFERENT variant -> must STILL forward (not deduped)
    uint8_t reqpub[32]; for (int i = 0; i < 32; ++i) reqpub[i] = uint8_t(0x70 + i);
    std::array<uint8_t, 40> q2{}; const size_t n2 = make_h(/*origin=*/9, 0x0000FACE, /*ttl=*/4, q2, /*hard=*/true, /*want_pubkey=*/true, reqpub);
    relay.on_recv(q2.data(), n2, meta);
    fire_h_forwards(relay, hal);                                    // §F-XL-1: release the second (different-slot) forward
    int wp_fwd = 0;
    for (const auto& f : hal.tx_frames) { auto pf = parse_h(std::span<const uint8_t>(f.data(), f.size())); if (pf && pf->want_pubkey) ++wp_fwd; }
    CHECK(wp_fwd == 1);                                              // the WANT_PUBKEY variant was forwarded despite the prior plain HARD
}

// §3 (E2E peer-key provisioning): `peerkey` installs a scanned pubkey as a PINNED (verified, MITM-resistant) key.
TEST_CASE("§3 peerkey — on_command installs a PINNED (verified) peer key") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 3);
    Identity id{}; identity_from_seed(id, seed);
    Command c{}; c.kind = CmdKind::peerkey;
    for (int i = 0; i < 32; ++i) c.u.peerkey.ed_pub[i] = id.ed_pub[i];
    CHECK(node.on_command(c).code == CmdCode::queued);
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));            // resolved by the DERIVED key_hash32 (== ed_pub[:4])
    CHECK(conf == Node::PeerKeyConf::pinned);                        // a QR scan -> a verified PINNED key
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != id.ed_pub[i]) same = false; CHECK(same);
}


// =============================================================================
// §S3 (cross-layer mobile first-contact, parts 2+3) — the home as key custodian:
// the reqpubkey requester's key reaches the hosted mobile so the mobile can DECRYPT
// its future sealed DMs (closes the recipient-side decrypt gap, node_hashlocate).
// =============================================================================

// §S3 part2 — the HOME proxy-answer branch was DROPPING the requester's appended key. Now it CACHES it AND
// FORWARDS it to the hosted mobile as a 1-hop DATA_TYPE_MOBILE_KEY_FORWARD last-mile (wire golden below).
TEST_CASE("§S3 part2 — the HOME proxy-answer caches the requester key AND forwards it to the hosted mobile (wire golden)") {
    TestHal hal;
    uint8_t mseed[32], sseed[32]; for (int i = 0; i < 32; ++i) { mseed[i] = uint8_t(i + 40); sseed[i] = uint8_t(90 - i); }
    Identity M{}, S{}; identity_from_seed(M, mseed); identity_from_seed(S, sseed);
    Node home(hal, /*id=*/5, /*key=*/0x00005555);                    // a static HOST (not is_mobile)
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    home.on_init(cfg);
    home.test_add_host_mobile(M.key_hash32, /*local_id=*/200, M.ed_pub);   // a LIVE hosted mobile with its pubkey
    home.test_suspend_tx_drain(true);                                // keep the forward in the queue so the wire golden can read it
    hal.tx_frames.clear(); hal.events.clear();
    std::array<uint8_t, 40> q{};                                     // a HARD WANT_PUBKEY H for M's hash from requester S(9), carrying S's ed_pub
    const size_t n = make_h(/*origin=*/9, M.key_hash32, /*ttl=*/4, q, /*hard=*/true, /*want_pubkey=*/true, S.ed_pub);
    home.on_recv(q.data(), n, RxMeta{8.0f, -80.0f, 0, -1});
    // (a) the home CACHED the requester's authoritative key (was DROPPED before this fix)
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(home.peer_key_find(S.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::authoritative);
    // (b) it answered the requester (TYPE-13) AND forwarded the requester's key to the mobile (TYPE-16)
    CHECK(find_ev(hal.events, "mobile_pubkey_answer_tx") != nullptr);
    CHECK(find_ev(hal.events, "mobile_key_forward_tx")   != nullptr);
    // (c) wire golden: the queued TYPE-16 item -> addr_len=1, dst=local_id, inner = [origin][S.ed_pub 32][name_len=0]
    int fwd = -1;
    for (uint8_t i = 0; i < home.test_tx_queue_n(); ++i) if (home.test_tx_type(i) == DATA_TYPE_MOBILE_KEY_FORWARD) { fwd = i; break; }
    CHECK(fwd >= 0);
    if (fwd >= 0) {
        CHECK(home.test_tx_addr_len(static_cast<uint8_t>(fwd)) == 1);
        CHECK(home.test_tx_dst(static_cast<uint8_t>(fwd)) == 200);
        uint8_t len = 0; const uint8_t* inr = home.test_tx_inner(static_cast<uint8_t>(fwd), len);
        CHECK(len == 1 + 32 + 1);                                    // [origin][ed_pub 32][name_len=0]
        bool same = true; for (int i = 0; i < 32; ++i) if (inr[1 + i] != S.ed_pub[i]) same = false; CHECK(same);
        CHECK(inr[1 + 32] == 0);                                     // make_h attaches no name -> name_len 0
    }
}

// §S3 part2 (mobile side) — the KEY_FORWARD handler caches the forwarded key + pushes peer_key_cached; a too-short
// or all-zero (degenerate) body is rejected (the key is self-derived, so an independent-hash mismatch cannot occur).
TEST_CASE("§S3 part2 — the mobile KEY_FORWARD handler caches + pushes; malformed/degenerate rejected") {
    TestHal hal;
    uint8_t sseed[32]; for (int i = 0; i < 32; ++i) sseed[i] = uint8_t(70 - i);
    Identity S{}; identity_from_seed(S, sseed);
    Node m(hal, /*id=*/200, /*key=*/0x00001234);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.is_mobile = true;
    m.on_init(cfg);
    uint8_t body[33 + 3]; for (int i = 0; i < 32; ++i) body[i] = S.ed_pub[i]; body[32] = 3; body[33] = 'a'; body[34] = 'b'; body[35] = 'c';
    m.on_mobile_key_forward(body, 36);
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(m.peer_key_find(S.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::authoritative);
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != S.ed_pub[i]) same = false; CHECK(same);
    Push p{}; bool cached = false;
    while (m.next_push(p)) if (p.kind == PushKind::peer_key_cached && p.sender_hash == S.key_hash32) { cached = true; break; }
    CHECK(cached);
    // too short (< 33 B) -> dropped (no crash, nothing cached beyond the valid one above)
    const uint16_t before = 0; (void)before;
    m.on_mobile_key_forward(body, 10);
    // an all-zero key -> rejected
    uint8_t zero[33] = {}; zero[32] = 0;
    m.on_mobile_key_forward(zero, 33);
    uint8_t z[32]; Node::PeerKeyConf zc{};
    CHECK_FALSE(m.peer_key_find(0, z, &zc));                         // hash 0 (the all-zero key) never cached
}

// §S3 part3 — a REGISTERED mobile overhearing a WANT_PUBKEY H for its OWN hash caches the requester's key TX-free:
// no answer, no relay, no TX (the home answers on its behalf, part 2). Covers the sender-in-RF-range case for free.
TEST_CASE("§S3 part3 — a mobile overhearing a WANT_PUBKEY for its OWN hash caches the requester key WITHOUT any TX") {
    TestHal hal;
    uint8_t sseed[32]; for (int i = 0; i < 32; ++i) sseed[i] = uint8_t(33 + i);
    Identity S{}; identity_from_seed(S, sseed);
    Node m(hal, /*id=*/200, /*key=*/0x0000ABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.is_mobile = true;
    m.on_init(cfg);
    hal.tx_frames.clear(); hal.events.clear();
    std::array<uint8_t, 40> q{};                                     // a WANT_PUBKEY H for M's OWN hash (0xABCD), carrying S's key
    const size_t n = make_h(/*origin=*/9, /*hash=*/0x0000ABCD, /*ttl=*/4, q, /*hard=*/true, /*want_pubkey=*/true, S.ed_pub);
    m.on_recv(q.data(), n, RxMeta{8.0f, -80.0f, 0, -1});
    uint8_t out[32]; Node::PeerKeyConf conf{};
    CHECK(m.peer_key_find(S.key_hash32, out, &conf));                // cached the requester's key
    CHECK(conf == Node::PeerKeyConf::authoritative);
    CHECK(hal.tx_frames.empty());                                    // NO TX at all (no answer)
    CHECK(find_ev(hal.events, "h_resolved") == nullptr);             // did not answer/resolve
    CHECK(find_ev(hal.events, "h_forward")  == nullptr);             // did not relay the flood
    CHECK(m.test_tx_queue_n() == 0);                                 // nothing queued either
}

// §S3 ★ ACCEPTANCE — the END-TO-END gap closure: a static S reqpubkeys mobile M (via M's home); S gets M's key
// (existing), and M gets S's key (NEW, via the forward) -> S's sealed DM now OPENS at M (was a silent drop).
TEST_CASE("§S3 acceptance — static->registered-mobile reqpubkey: M gets S's key -> S's sealed DM OPENS at M") {
    TestHal hhal, mhal, shal;
    uint8_t mseed[32], sseed[32]; for (int i = 0; i < 32; ++i) { mseed[i] = uint8_t(i + 11); sseed[i] = uint8_t(200 - i); }
    Identity M{}, S{}; identity_from_seed(M, mseed); identity_from_seed(S, sseed);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    Node home(hhal, /*id=*/5, /*key=*/0x00005555); home.on_init(cfg);
    home.test_add_host_mobile(M.key_hash32, /*local_id=*/200, M.ed_pub);
    NodeConfig mcfg = cfg; mcfg.is_mobile = true;
    Node m(mhal, /*id=*/200, M.key_hash32); m.on_init(mcfg); m.set_crypto_identity(M.x_secret, M.ed_pub);
    Node sender(shal, /*id=*/9, S.key_hash32); sender.on_init(cfg); sender.set_crypto_identity(S.x_secret, S.ed_pub);
    sender.peer_key_set(M.key_hash32, M.ed_pub, Node::PeerKeyConf::authoritative);   // S already holds M's key (the pubkey answer)
    home.test_suspend_tx_drain(true);                                // keep the forward queued so we can read its body
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[7] = { 's','e','a','l','e','d','!' };
    uint8_t inner[128], seed[8]; Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t sn = sender.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/M.key_hash32, /*origin=*/9,
                                            /*ctr=*/7, /*source_hash=*/S.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(sn > 0); CHECK(oc == Node::SealOutcome::ok);
    // BEFORE M holds S's key -> trial decrypt has no candidate -> FAILS (the silent-drop gap)
    { uint32_t snd = 0, org = 0, src = 0; bool loc = false; int32_t la = 0, lo = 0; uint8_t o[64]; uint8_t ol = 0;
      CHECK_FALSE(m.e2e_open_trial(inner, sn, seed, flags, /*ctr=*/7, snd, org, src, loc, la, lo, o, ol)); }
    // the reqpubkey flow: S's WANT_PUBKEY reaches M's home -> the home forwards S's key to M
    std::array<uint8_t, 40> q{};
    const size_t qn = make_h(/*origin=*/9, M.key_hash32, /*ttl=*/4, q, /*hard=*/true, /*want_pubkey=*/true, S.ed_pub);
    home.on_recv(q.data(), qn, RxMeta{8.0f, -80.0f, 0, -1});
    int fwd = -1; for (uint8_t i = 0; i < home.test_tx_queue_n(); ++i) if (home.test_tx_type(i) == DATA_TYPE_MOBILE_KEY_FORWARD) { fwd = i; break; }
    CHECK(fwd >= 0);
    if (fwd >= 0) {
        uint8_t len = 0; const uint8_t* inr = home.test_tx_inner(static_cast<uint8_t>(fwd), len);   // inner = [origin][body]
        m.on_mobile_key_forward(inr + 1, static_cast<uint8_t>(len - 1));                            // deliver the last-mile body to M
    }
    // NOW M holds S's key -> the sealed DM OPENS (gap closed)
    uint32_t snd = 0, org = 0, src = 0; bool loc = false; int32_t la = 0, lo = 0; uint8_t o[64]; uint8_t ol = 0;
    CHECK(m.e2e_open_trial(inner, sn, seed, flags, /*ctr=*/7, snd, org, src, loc, la, lo, o, ol));
    CHECK(snd == S.key_hash32);
    CHECK(ol == 7);
    bool same = true; for (int i = 0; i < 7; ++i) if (o[i] != body[i]) same = false; CHECK(same);
}

// §S3 part2 dedup — a same-requester second reqpubkey (retry) re-answers the requester but does NOT re-forward the key.
TEST_CASE("§S3 part2 dedup — a same-requester second reqpubkey does NOT re-forward") {
    TestHal hal;
    uint8_t mseed[32], sseed[32]; for (int i = 0; i < 32; ++i) { mseed[i] = uint8_t(i + 40); sseed[i] = uint8_t(90 - i); }
    Identity M{}, S{}; identity_from_seed(M, mseed); identity_from_seed(S, sseed);
    Node home(hal, /*id=*/5, /*key=*/0x00005555);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    home.on_init(cfg);
    home.test_add_host_mobile(M.key_hash32, /*local_id=*/200, M.ed_pub);
    std::array<uint8_t, 40> q{};
    const size_t n = make_h(/*origin=*/9, M.key_hash32, /*ttl=*/4, q, /*hard=*/true, /*want_pubkey=*/true, S.ed_pub);
    home.on_recv(q.data(), n, RxMeta{8.0f, -80.0f, 0, -1});
    CHECK(hal.countType("mobile_key_forward_tx") == 1);
    home.on_recv(q.data(), n, RxMeta{8.0f, -80.0f, 0, -1});          // identical retry
    CHECK(hal.countType("mobile_key_forward_tx") == 1);              // still 1 -> deduped (last_key_fwd_hash32)
}

// =============================================================================
// §S2 — DATA_TYPE_INTRO first-contact pubkey attach (SEND side + D1 attach rule +
// peer_confirmed). The RECEIVE / strip-deliver / delegation round-trip lives in
// test_dual_layer.cpp (it needs the do_post_ack drivers). NB: CHECK only (this
// TU builds -fno-exceptions -> REQUIRE is illegal); guard derefs with `if`.
// =============================================================================
TEST_CASE("§S2 INTRO wire golden — a first-contact plaintext hash send rides as INTRO: [ed_pub 32][name_len][name] before the body") {
    TestHal hal;
    uint8_t seedA[32]; for (int i = 0; i < 32; ++i) seedA[i] = uint8_t(i + 1);
    Identity idA{}; identity_from_seed(idA, seedA);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    const uint32_t Bhash = 0x1234ABCDu;
    A.test_id_bind_set(9, Bhash, /*authoritative=*/true);   // A holds an AUTHORITATIVE binding -> send NOW (do_send), not park
    A.test_suspend_tx_drain(true);                          // keep the frame queued to read it
    const uint8_t body[3] = { 'h', 'i', '!' };
    (void)send_by_hash_cmd(A, Bhash, body, 3);
    CHECK(A.test_tx_queue_n() >= 1);
    if (A.test_tx_queue_n() >= 1) {
        CHECK(A.test_tx_type(0) == DATA_TYPE_INTRO);
        uint8_t ilen = 0; const uint8_t* inner = A.test_tx_inner(0, ilen);
        auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, ilen), DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH);
        CHECK(ui.has_value());
        if (ui) {
            CHECK(ui->has_dst_hash);    CHECK(ui->dst_key_hash32 == Bhash);
            CHECK(ui->has_source_hash); CHECK(ui->source_hash == idA.key_hash32);
            const size_t want = static_cast<size_t>(33) + ui->body[32] + 3;
            CHECK(ui->body.size() >= static_cast<size_t>(35));
            if (ui->body.size() >= 33) {
                bool edok = true; for (int i = 0; i < 32; ++i) if (ui->body[i] != idA.ed_pub[i]) edok = false; CHECK(edok);
                const uint32_t edh = uint32_t(ui->body[0]) | (uint32_t(ui->body[1]) << 8) | (uint32_t(ui->body[2]) << 16) | (uint32_t(ui->body[3]) << 24);
                CHECK(edh == idA.key_hash32);                    // ed_pub[:4] == source_hash (the receiver's self-consistency check)
                const uint8_t nlen = ui->body[32];
                CHECK(want == ui->body.size());
                if (ui->body.size() == want) {
                    const uint8_t* msg = ui->body.data() + 33 + nlen;
                    CHECK(msg[0] == 'h'); CHECK(msg[1] == 'i'); CHECK(msg[2] == '!');   // the app message rides UNCHANGED after the prefix
                }
            }
        }
    }
}

TEST_CASE("§S2 D1 attach rule — unknown/unconfirmed peer attaches INTRO; a CONFIRMED (sealed-opened) peer is PLAIN; a plaintext cache never confirms") {
    TestHal halA, halB;
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 1); sB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    Node A(halA, 1, idA.key_hash32), B(halB, 2, idB.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg); B.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);   // A KNOWS B's key (cache) ...
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);   // B holds A's key (so B can seal TO A below)
    CHECK_FALSE(A.peer_confirmed(idB.key_hash32));                                  // ... but a plaintext cache does NOT confirm
    A.test_id_bind_set(2, idB.key_hash32, /*authoritative=*/true);
    A.test_suspend_tx_drain(true);
    const uint8_t body[2] = { 'y', 'o' };
    const uint8_t n0 = A.test_tx_queue_n();
    (void)send_by_hash_cmd(A, idB.key_hash32, body, 2);
    CHECK(A.test_tx_queue_n() > n0);
    if (A.test_tx_queue_n() > n0) CHECK(A.test_tx_type(n0) == DATA_TYPE_INTRO);     // unconfirmed -> attaches
    // B seals a DM to A; A opens it (trial decrypt) -> peer_confirmed(B) is set HERE (only on a sealed open)
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t sb[4] = { 'p', 'o', 'n', 'g' }; uint8_t inner[96], seed[8]; Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t sn = B.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/idA.key_hash32, 2, 7, idB.key_hash32, 0, 0, sb, 4, oc);
    CHECK(sn > 0);
    uint32_t snd = 0, org = 0, src = 0; bool loc = false; int32_t la = 0, lo = 0; uint8_t o[64]; uint8_t ol = 0;
    CHECK(A.e2e_open_trial(inner, sn, seed, flags, 7, snd, org, src, loc, la, lo, o, ol));
    CHECK(A.peer_confirmed(idB.key_hash32));                                        // NOW confirmed
    const uint8_t n1 = A.test_tx_queue_n();
    (void)send_by_hash_cmd(A, idB.key_hash32, body, 2);
    CHECK(A.test_tx_queue_n() > n1);
    if (A.test_tx_queue_n() > n1) {
        CHECK(A.test_tx_type(n1) == 0);                                             // CONFIRMED -> plain DM, no INTRO prefix
        uint8_t il = 0; const uint8_t* in2 = A.test_tx_inner(n1, il);
        auto ui = parse_unicast_inner(std::span<const uint8_t>(in2, il), DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH);
        CHECK(ui.has_value());
        if (ui) CHECK(ui->body.size() == 2);                                        // just "yo" — no key prefix
    }
}

TEST_CASE("§S2 too-large fallback — an INTRO that would overflow the DM body cap sends PLAIN (delivery beats key bootstrap) + telemetry") {
    TestHal hal;
    uint8_t seedA[32]; for (int i = 0; i < 32; ++i) seedA[i] = uint8_t(i + 7);
    Identity idA{}; identity_from_seed(idA, seedA);
    Node A(hal, 1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    const uint32_t Bhash = 0x22223333u;
    A.test_id_bind_set(9, Bhash, /*authoritative=*/true);
    A.test_suspend_tx_drain(true);
    uint8_t big[meshroute::protocol::dm_max_body_bytes];                            // a body at the cap -> prefix can't fit
    for (size_t i = 0; i < sizeof big; ++i) big[i] = uint8_t('a' + (i % 26));
    (void)send_by_hash_cmd(A, Bhash, big, static_cast<uint8_t>(sizeof big));
    CHECK(A.test_tx_queue_n() >= 1);
    if (A.test_tx_queue_n() >= 1) CHECK(A.test_tx_type(0) == 0);                    // sent PLAIN (no attach)
    CHECK(hal.countType("intro_attach_too_large") == 1);                           // fail-loud telemetry
}

TEST_CASE("§S2 intro_attach cfg OFF + no-identity — never attach (the escape hatch + the s18-inert gate)") {
    // cfg off, with an identity -> plain
    { TestHal hal; uint8_t s[32]; for (int i = 0; i < 32; ++i) s[i] = uint8_t(i + 3); Identity id{}; identity_from_seed(id, s);
      Node A(hal, 1, id.key_hash32); NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false; cfg.intro_attach = false;
      A.on_init(cfg); A.set_crypto_identity(id.x_secret, id.ed_pub);
      A.test_id_bind_set(9, 0x55556666u, true); A.test_suspend_tx_drain(true);
      const uint8_t b[2] = { 'h', 'i' }; (void)send_by_hash_cmd(A, 0x55556666u, b, 2);
      CHECK(A.test_tx_queue_n() >= 1); if (A.test_tx_queue_n() >= 1) CHECK(A.test_tx_type(0) == 0); }   // cfg off -> plain
    // cfg on (default) but NO crypto identity -> plain (s18-inert gate)
    { TestHal hal; Node A(hal, 1, 0x0000ABCD); NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
      A.on_init(cfg);   // NO set_crypto_identity -> _crypto_ready false
      A.test_id_bind_set(9, 0x77778888u, true); A.test_suspend_tx_drain(true);
      const uint8_t b[2] = { 'h', 'i' }; (void)send_by_hash_cmd(A, 0x77778888u, b, 2);
      CHECK(A.test_tx_queue_n() >= 1); if (A.test_tx_queue_n() >= 1) CHECK(A.test_tx_type(0) == 0); }   // no identity -> plain (never attach)
}

// ==== F-SL-1 (2026-07-19): bounded jittered H re-flood for a parked unresolved send ==============
// A send-by-hash to an UNKNOWN hash parks + floods ONE soft H at park time. In a quiet net that single
// flood can die; without a retry the parked send just ages out to send_hash_giveup. F-SL-1 re-emits the H
// every park_reflood_retry_ms (jittered) while parked, bounded to park_reflood_max_retries, then the giveup
// still fires. This test drives the reflood scan timer (kParkRefloodTimerId) — TestHal never auto-fires.
TEST_CASE("§F-SL-1 — a parked unresolved send re-floods (bounded + jittered) then still gives up") {
    TestHal hal;
    Node A(hal, /*id=*/1, /*key_hash32=*/0xAAAA1111u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg);
    hal._now = 1000;
    const uint32_t unknown = 0x1234ABCDu;
    const uint8_t body[3] = { 'h', 'i', '!' };
    (void)send_by_hash_cmd(A, unknown, body, 3);
    CHECK(hal.countType("send_parked_for_hash") == 1);          // parked (no binding)
    const int h_at_park = count_h_tx(hal.tx_frames);
    CHECK(h_at_park == 1);                                       // ONE soft H flood at park time

    // the reflood scan timer is armed at exactly park_reflood_retry_ms (jitter draw returns lo=0 by default)
    uint32_t reflood_delay = 0; bool armed_reflood = false;
    for (auto& [d, id] : hal.armed) if (id == kParkRefloodTimerId) { reflood_delay = d; armed_reflood = true; }
    CHECK(armed_reflood);
    CHECK(reflood_delay == protocol::park_reflood_retry_ms);

    // retry 1: advance to the deadline + fire the scan -> a SECOND H flood + the telemetry (the first deadline is a
    // FIXED offset — no park-time jitter draw)
    hal._now = 1000 + protocol::park_reflood_retry_ms;
    A.on_timer(kParkRefloodTimerId);
    CHECK(hal.countType("send_hash_reflood") == 1);
    CHECK(count_h_tx(hal.tx_frames) == 2);

    // retry 2: the entry re-armed itself ~one interval later (+ the deterministic jitter, bounded by park_reflood_jitter_ms)
    hal._now = 1000 + 2 * protocol::park_reflood_retry_ms + protocol::park_reflood_jitter_ms;
    A.on_timer(kParkRefloodTimerId);
    CHECK(hal.countType("send_hash_reflood") == 2);
    CHECK(count_h_tx(hal.tx_frames) == 3);

    // retry 3: BOUNDED — park_reflood_max_retries=2, so no third re-flood
    hal._now = 1000 + 4 * protocol::park_reflood_retry_ms + protocol::park_reflood_jitter_ms;
    A.on_timer(kParkRefloodTimerId);
    CHECK(hal.countType("send_hash_reflood") == 2);             // unchanged
    CHECK(count_h_tx(hal.tx_frames) == 3);                      // unchanged

    // the giveup still fires after the TTL (the re-flood never removed the bound); _now is already well past the TTL
    A.on_timer(kAgingTimerId);
    CHECK(hal.countType("send_hash_giveup") >= 1);
}

// F-SL-1: the re-flood resolves the send on a retry — a binding that arrives between park and giveup drains the
// parked DM, and no further re-flood fires (stop-on-resolve). Mirrors the s27 post-re-home quiet-net recovery.
TEST_CASE("§F-SL-1 — a binding arriving after a re-flood drains the parked send; re-flood then stops") {
    TestHal hal;
    Node A(hal, /*id=*/1, /*key_hash32=*/0xAAAA2222u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg);
    hal._now = 1000;
    const uint32_t target = 0x5678BEEFu;
    const uint8_t body[2] = { 'y', 'o' };
    (void)send_by_hash_cmd(A, target, body, 2);
    CHECK(hal.countType("send_parked_for_hash") == 1);

    // one re-flood fires
    hal._now = 1000 + protocol::park_reflood_retry_ms;
    A.on_timer(kParkRefloodTimerId);
    CHECK(hal.countType("send_hash_reflood") == 1);

    // the owner (node 9) beacons carrying `target` -> authoritative binding + the beacon-tick re-drain
    // flies the parked DM (the resolution the re-flood was fishing for) -> the parked entry is removed
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn; const size_t bn = make_beacon(/*src=*/9, target, bcn);
    A.on_recv(bcn.data(), bn, meta);

    // the next scan finds nothing to re-flood -> the count stays put (stop-on-resolve)
    hal._now = 1000 + 2 * protocol::park_reflood_retry_ms;
    A.on_timer(kParkRefloodTimerId);
    CHECK(hal.countType("send_hash_reflood") == 1);            // no further re-flood
    CHECK(hal.countType("send_hash_giveup") == 0);            // resolved, never gave up
}

// F-SL-1 s18-inertness: an AUTHORITATIVE binding sends immediately — no park, so NO re-flood timer is armed
// (the mechanism is dormant on the static-plane no-park path that dominates s18).
TEST_CASE("§F-SL-1 — a resolved send never parks -> no re-flood timer armed (s18-class inert)") {
    TestHal hal;
    Node A(hal, /*id=*/1, /*key_hash32=*/0xAAAA3333u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    A.on_init(cfg);
    const uint32_t known = 0x0A0B0C0Du;
    A.test_id_bind_set(9, known, /*authoritative=*/true);
    hal._now = 1000;
    const uint8_t body[2] = { 'o', 'k' };
    (void)send_by_hash_cmd(A, known, body, 2);
    CHECK(hal.countType("send_parked_for_hash") == 0);         // sent NOW, not parked
    bool armed_reflood = false;
    for (auto& [d, id] : hal.armed) { (void)d; if (id == kParkRefloodTimerId) armed_reflood = true; }
    CHECK_FALSE(armed_reflood);
}
