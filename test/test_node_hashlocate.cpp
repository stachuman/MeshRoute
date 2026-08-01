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
#include "support/test_hal.h"

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

class TestHal : public mrtest::TestHalBase {
public:
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;          // captured TX bytes (the H forward)

    std::vector<std::pair<uint32_t, uint32_t>> armed;     // §F-XL-1: (delay_ms, timer_id) captured from after()
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    bool     after(uint32_t delay, uint32_t id) override { armed.emplace_back(delay, id); return true; }
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
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);

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
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);

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
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);

    CHECK(node.id_bind_find_by_hash(0x0000CCCC) == 7);   // snooped in transit -> a future resolver (floods shrink)
    CHECK(find_ev(hal.events, "hash_bind_snooped") != nullptr);
}

// ---- ★★ §hashbind-plane (BUG FIX 2026-07-31, register B2): the ingest was PLANE-BLIND -------------------------------
// A TEAM-scoped H is answered with the owner's TEAM LOCAL id (handle_h §F-TR-2), and both ingest paths wrote that id into
// `_id_bind` — the STATIC node_id-indexed plane. Measured corpus-wide as `id_bind_set{node:34,source:"h_relay"|"h_query"}`
// on s24 (34 = T3's team_local_id, its static id is 52); s24 asserts a static BYSTANDER never does this, but teammates
// did it to each other. An I2 breach: §18 lets a team local id numerically collide a real static node_id, and downstream
// a plain send-by-hash then resolves the hash to that id and RREQ-floods it on the STATIC plane (measured on s34: 8 ×
// `r_tx{dst:84,reason:no_route}` for a team local id, gone after this fix).
// ⚠ Both cases assert on the SURVIVING TABLE, not on a return value or an event — the point of the fix is that the
// static plane does not acquire the row. The `team_plane=false` arm inside each case is the discriminating control:
// without it "nothing was written" would also pass on a build where the ingest is simply broken.
TEST_CASE("★★ §hashbind-plane — a TEAM-plane H_ANSWER is NOT cached in the STATIC _id_bind (the consume path)") {
    TestHal hal;
    Node node(hal, /*node_id=*/9, /*key_hash32=*/0x00009999);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);

    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 34; hb.key_hash32 = 0xCCCC0003; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));

    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/true);
    CHECK(node.id_bind_find_by_hash(0xCCCC0003) == -1);  // ★★ THE ASSERTION: the static plane never learns a team local id
    CHECK(find_ev(hal.events, "hash_bind_rx") != nullptr);   // ...and the answer WAS still ingested (drain/telemetry unaffected)

    // CONTROL, same site, same bytes: a STATIC-plane answer still caches exactly as before.
    hal.events.clear();
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);
    CHECK(node.id_bind_find_by_hash(0xCCCC0003) == 34);
}

TEST_CASE("★★ §hashbind-plane — a TEAM-plane H_ANSWER is NOT snooped into the STATIC _id_bind (the cache-on-pass path)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0x0000BBBB);   // a relay — neither querier nor owner
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);

    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 34; hb.key_hash32 = 0xCCCC0003; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));

    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/true);
    CHECK(node.id_bind_find_by_hash(0xCCCC0003) == -1);  // ★★ a relay's cache-on-pass must not cross the plane either
    CHECK(find_ev(hal.events, "hash_bind_snooped") != nullptr);   // the snoop telemetry still fires (the frame is still forwarded)

    hal.events.clear();
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);
    CHECK(node.id_bind_find_by_hash(0xCCCC0003) == 34);  // CONTROL: the static cache-on-pass is untouched
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
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);

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
    node.on_hash_bind_snoop(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);
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
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);

    const Ev* res = find_ev(hal.events, "send_hash_resolved");
    CHECK(res != nullptr);
    if (res) { CHECK(res->key_hash32 == 0x0000EEEE); CHECK(res->node == 6); }  // flown to the resolved id

    // The parked DM has drained — a second identical answer resolves NOTHING (no re-send).
    hal.events.clear();
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);
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

    hal._now = protocol::hash_locate_giveup_ms + 1;      // past the give-up window (P-BUDGET: parked path decoupled from send_defer_ttl_ms)
    node.on_timer(kAgingTimerId);                        // the periodic sweep
    CHECK(find_ev(hal.events, "send_hash_giveup") != nullptr);

    // ...and it's gone — a late answer drains nothing.
    hal.events.clear();
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 6; hb.key_hash32 = 0x0000EEEE; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);
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
    node.on_hash_bind_response(inner.data(), (uint8_t)il, hb.authoritative, /*team_plane=*/false);
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

    hal._now += protocol::hash_locate_giveup_ms;     // let the parked resolve age out (P-BUDGET window)
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
    CHECK(node.peer_name_find(id.key_hash32, nullptr, 0) == 0);      // §AB2: a nameless peerkey stores NO name (the shipped shape)
}

// ★★ §AB2 (address-book spec 2026-07-29 §2.3) — Node::peer_name_set, the engine half of the `peername` verb, and the
// ONE name writer for _peer_keys (peer_key_set now delegates its two name-copy sites to it).
// Nothing in the corpus can reach this: no scenario runs a console verb, and the sim's push bridge emits only
// ctr/dst/kind. These cases are the entire detector.
TEST_CASE("§AB2 peer_name_set — renames a cached peer, refuses an unknown hash, clamps at the cap") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 3);
    Identity id{}; identity_from_seed(id, seed);
    char nm[64] = {};
    // 1. C2: NO ROW -> refuse, and (the explicit spec §2.3 rule) do NOT create a keyless placeholder as a side effect.
    CHECK_FALSE(node.peer_name_set(id.key_hash32, "Ola", 3));
    CHECK(node.peer_key_count() == 0);
    // 2. Cache the key with a name, then RENAME it.
    hal._now = 5000;
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative, "MeshRoute node: 0x6c29", 22));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 22);
    hal._now = 9000;                                                  // time MOVES between the cache and the rename
    CHECK(node.peer_name_set(id.key_hash32, "Ola K", 5));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 5);
    CHECK(std::string(nm, 5) == "Ola K");
    // 3. It touches NOTHING else — not the key, not the confidence, and not last_seen_ms (a rename is not a sighting,
    //    so it must not silently extend the key's TTL lease; that is one of the three reasons it is not peer_key_set).
    uint8_t out[32] = {}; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::authoritative);
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != id.ed_pub[i]) same = false; CHECK(same);
    hal._now = 5000 + protocol::peer_key_ttl_ms;                       // TTL measured from the CACHE at 5000, not the rename at 9000
    CHECK_FALSE(node.peer_key_find(id.key_hash32, out));               // => aged. Had the rename refreshed last_seen, this would still resolve.
    // 4. ...and the aged-but-present row is STILL renameable. Deliberate: `nameof` (peer_name_find) does not age-gate
    //    either, so refusing here would make two verbs disagree about one row — spec §2.5's defect class.
    CHECK(node.peer_name_set(id.key_hash32, "Zed", 3));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 3);
    // 5. Over-cap is CLAMPED at the engine (protocol::peer_name_max) — the console refuses first, so this is the
    //    backstop that keeps PeerKey::name from ever overrunning.
    const std::string over(2 * protocol::peer_name_max, 'X');
    CHECK(node.peer_name_set(id.key_hash32, over.c_str(), 200));       // 200 > cap, truncated to uint8_t at the call
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == protocol::peer_name_max);
    // 6. A null name pointer is refused rather than dereferenced.
    CHECK_FALSE(node.peer_name_set(id.key_hash32, nullptr, 3));
}

// ★★ §AB2 — THE EQUIVALENCE PROOF FOR THE DELEGATION, and it has to be native.
// peer_key_set's two name-copy sites (fresh INSERT and same-hash REFRESH) now call peer_name_set instead of repeating a
// third clamp-and-copy. ⚠ Byte-identity CANNOT verify that: `_peer_keys[].name` reaches no corpus-visible surface —
// its only readers are push_peer_key_cached's body (the sim's push bridge emits ONLY ctr/dst/kind) and src/
// (peer_store_sync / `nameof`), which the sim does not compile. Measured on this tree: 0 of 36 scenarios carry a peer
// name in any form. ⇒ these four assertions are the whole detector for the refactor.
TEST_CASE("§AB2 peer_key_set — both name paths (fresh insert + same-hash refresh) still behave exactly as before") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 45);
    Identity id{}; identity_from_seed(id, seed);
    char nm[64] = {};
    // (a) fresh INSERT with a name.
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::overheard, "first", 5));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 5);
    CHECK(std::string(nm, 5) == "first");
    // (b) same-hash REFRESH replaces it (§1.3: the name is MUTABLE and refreshed on every pubkey message).
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative, "second", 6));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 6);
    CHECK(std::string(nm, 6) == "second");
    // (c) a NAMELESS refresh must NOT clear the stored name (the `name && name_len` guard, unchanged).
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 6);
    // (d) an over-cap name on either path is CLAMPED to protocol::peer_name_max (the on-air callers may hand us a
    //     foreign advertisement of any length; only the operator-typed console paths refuse instead).
    const std::string over(2 * protocol::peer_name_max, 'Z');
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative, over.c_str(), 200));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == protocol::peer_name_max);
    // (e) a fresh INSERT into an evict-recycled slot starts from name_len = 0, then takes the new name — i.e. no name
    //     from the evicted occupant survives (the `name_len = 0` reset before the copy).
    uint8_t s2[32]; for (int i = 0; i < 32; ++i) s2[i] = static_cast<uint8_t>(200 - i);
    Identity id2{}; identity_from_seed(id2, s2);
    CHECK(node.peer_key_set(id2.key_hash32, id2.ed_pub, Node::PeerKeyConf::overheard));   // NO name given
    CHECK(node.peer_name_find(id2.key_hash32, nm, sizeof nm) == 0);
}

// ★ The pinned case the brief singled out: peer_key_set returns EARLY with no name refresh when the stored entry is
// pinned and the incoming confidence is not (its "pinned is IMMUTABLE to an on-air set" rule). A USER-initiated rename
// is not an on-air set, so it must NOT silently no-op — and because peer_name_set never touches the confidence, that
// rule is not even in play. This case pins BOTH halves of that: the on-air no-op AND the operator rename.
TEST_CASE("§AB2 peer_name_set — a rename of a PINNED peer SUCCEEDS (while an on-air set is still a no-op)") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 70);
    Identity id{}; identity_from_seed(id, seed);
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::pinned, "QR label", 8));
    char nm[64] = {};
    // (a) the CONTROL: an on-air (authoritative) set on a pinned row does NOT refresh the name — the early return.
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::authoritative, "on-air name", 11));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 8);
    CHECK(std::string(nm, 8) == "QR label");
    // (b) the OPERATOR rename goes through — the key is IMMUTABLE, the name is MUTABLE (PeerKey's own contract).
    CHECK(node.peer_name_set(id.key_hash32, "Ola (verified)", 14));
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 14);
    CHECK(std::string(nm, 14) == "Ola (verified)");
    // (c) and it is STILL pinned with the SCANNED key — so /mrpeers re-mirrors it as pinned, never demoted.
    uint8_t out[32] = {}; Node::PeerKeyConf conf{};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::pinned);
    bool same = true; for (int i = 0; i < 32; ++i) if (out[i] != id.ed_pub[i]) same = false; CHECK(same);
}

// ★ §AB2: the on_command arm — EVERY refusal the verb can produce below the parser, which is what the spec's gate
// mandates be native. `src/firmware_commands.cpp handle_peername` maps these codes onto the JSON reasons.
TEST_CASE("§AB2 on_command peername — queued / err_unknown_dst / err_too_large, and the echoed hash") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 5);
    Identity id{}; identity_from_seed(id, seed);
    const char* nmtxt = "Ola K";
    Command c{}; c.kind = CmdKind::peername; c.u.peername.key_hash32 = id.key_hash32;
    c.body = reinterpret_cast<const uint8_t*>(nmtxt); c.body_len = 5;
    // 1. unknown hash -> err_unknown_dst ("unknown_hash"), and dst_hash is ECHOED so the app can correlate.
    CmdResult r = node.on_command(c);
    CHECK(r.code == CmdCode::err_unknown_dst);
    CHECK(r.dst_hash == id.key_hash32);
    // 2. cached -> queued, and the name lands.
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::overheard));
    r = node.on_command(c);
    CHECK(r.code == CmdCode::queued);
    CHECK(r.dst_hash == id.key_hash32);
    char nm[64] = {};
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 5);
    CHECK(std::string(nm, 5) == "Ola K");
    // 3. over the cap -> err_too_large ("too_long"), and the stored name is UNCHANGED (refuse, never truncate — C2).
    const std::string over(protocol::peer_name_max + 1, 'X');
    Command o{}; o.kind = CmdKind::peername; o.u.peername.key_hash32 = id.key_hash32;
    o.body = reinterpret_cast<const uint8_t*>(over.c_str()); o.body_len = protocol::peer_name_max + 1;
    CHECK(node.on_command(o).code == CmdCode::err_too_large);
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 5);
    // 4. EXACTLY at the cap is accepted (the boundary is <=, not <).
    const std::string at(protocol::peer_name_max, 'Y');
    o.body = reinterpret_cast<const uint8_t*>(at.c_str()); o.body_len = protocol::peer_name_max;
    CHECK(node.on_command(o).code == CmdCode::queued);
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == protocol::peer_name_max);
    // 5. An EMPTY name never reaches peer_name_set (the console refuses it first; this is the API backstop).
    Command e{}; e.kind = CmdKind::peername; e.u.peername.key_hash32 = id.key_hash32;
    e.body = reinterpret_cast<const uint8_t*>(nmtxt); e.body_len = 0;
    CHECK(node.on_command(e).code == CmdCode::err_too_large);
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == protocol::peer_name_max);   // untouched
}

// ★ §AB2: the OPTIONAL one-shot name on `peerkey` (spec §2.3) — it must land in the SAME peer_key_set name parameter,
// and an over-cap operator label must be REFUSED rather than clamped (the on-air callers may clamp; a typed label
// must not be silently shortened).
TEST_CASE("§AB2 on_command peerkey — the optional one-shot name lands; over-cap is refused, not clamped") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 11);
    Identity id{}; identity_from_seed(id, seed);
    const char* nmtxt = "Scanned Ola";
    Command c{}; c.kind = CmdKind::peerkey;
    for (int i = 0; i < 32; ++i) c.u.peerkey.ed_pub[i] = id.ed_pub[i];
    // over-cap FIRST, so the refusal is proven not to have installed the key either.
    const std::string over(protocol::peer_name_max + 1, 'X');
    c.body = reinterpret_cast<const uint8_t*>(over.c_str()); c.body_len = protocol::peer_name_max + 1;
    CHECK(node.on_command(c).code == CmdCode::err_too_large);
    CHECK(node.peer_key_count() == 0);
    c.body = reinterpret_cast<const uint8_t*>(nmtxt); c.body_len = 11;
    CHECK(node.on_command(c).code == CmdCode::queued);
    char nm[64] = {};
    CHECK(node.peer_name_find(id.key_hash32, nm, sizeof nm) == 11);
    CHECK(std::string(nm, 11) == "Scanned Ola");
    Node::PeerKeyConf conf{}; uint8_t out[32] = {};
    CHECK(node.peer_key_find(id.key_hash32, out, &conf));
    CHECK(conf == Node::PeerKeyConf::pinned);
}

// ★★ §AB2: the PRODUCER half of the §0.1 fix — push_peer_key_cached now carries the CONFIDENCE (Push::peer_conf), read
// back out of the LIVE table. Driven through the real public receive path (on_hash_bind_pubkey), not the private
// emitter, so this is the wire-to-app behaviour and not just an internal call. test_console_json.cpp holds the encoder
// golden for all three levels + the out-of-range fallback.
TEST_CASE("§AB2 peer_key_cached push — carries the STORED confidence (and a pinned peer no longer reports pinned:false)") {
    TestHal hal; Node node(hal, 5, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); node.on_init(cfg);
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 17);
    Identity id{}; identity_from_seed(id, seed);
    hash_bind_pubkey_inner hb{}; hb.target_layer = 0; hb.node_id = 9;
    for (int i = 0; i < 32; ++i) hb.ed_pub[i] = id.ed_pub[i];
    uint8_t inner[34]; const size_t n = pack_hash_bind_pubkey_inner(hb, std::span<uint8_t>(inner, sizeof inner));
    auto drain_conf = [&node](uint32_t hash, uint8_t& conf_out) {
        Push p{}; bool seen = false;
        while (node.next_push(p))
            if (p.kind == PushKind::peer_key_cached && p.sender_hash == hash) { conf_out = p.peer_conf; seen = true; }
        return seen;
    };
    uint8_t conf = 0xFF;
    // 1. An owner's TYPE-5 answer caches AUTHORITATIVE -> the push must say so. This is the row the app may seal to,
    //    and before AB2 it was indistinguishable from `overheard`. ★ It is also != the field's 0 default, which is what
    //    makes this a real read of the table rather than a constant.
    node.on_hash_bind_pubkey(inner, static_cast<uint8_t>(n));
    CHECK(drain_conf(id.key_hash32, conf));
    CHECK(conf == static_cast<uint8_t>(Node::PeerKeyConf::authoritative));
    // 2. ★★ THE OLD LIE, DIRECTLY: pin the same hash (QR import), then let the SAME on-air answer arrive again.
    //    peer_key_set no-ops (pinned is immutable to an on-air set) but still returns true, so the push fires — and it
    //    now reports `pinned`. The retired hardcoded literal emitted `"pinned":false` for exactly this frame.
    CHECK(node.peer_key_set(id.key_hash32, id.ed_pub, Node::PeerKeyConf::pinned));
    conf = 0xFF;
    node.on_hash_bind_pubkey(inner, static_cast<uint8_t>(n));
    CHECK(drain_conf(id.key_hash32, conf));
    CHECK(conf == static_cast<uint8_t>(Node::PeerKeyConf::pinned));
    // 3. The confidence read is the STORED one, not the INCOMING one: the answer above asked for `authoritative` and the
    //    push said `pinned`, so a caller cannot announce a level the table does not hold (U2, one read path).
    Node::PeerKeyConf stored{}; uint8_t out[32] = {};
    CHECK(node.peer_key_find(id.key_hash32, out, &stored));
    CHECK(stored == Node::PeerKeyConf::pinned);
    // ⓘ NOT COVERED HERE, and named rather than glossed: `overheard` and the absent-row safe default (peer_key_find
    // false -> peer_conf stays 0). NO live path caches at `overheard` and none pushes for a hash it did not just cache,
    // so both are unreachable through a public driver; the encoder golden in test_console_json.cpp asserts what they
    // render (including an out-of-range byte), and the default is one field initialiser away in command.h.
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

    // retries 2..park_reflood_max_retries: the entry re-arms itself ~one interval later each time (+ the deterministic
    // jitter, bounded by park_reflood_jitter_ms). P-BUDGET: max_retries was raised so a fragile multi-hop flood gets
    // several INDEPENDENT (>= a beacon period apart) attempts; drive them all and verify each fires.
    for (uint8_t k = 2; k <= protocol::park_reflood_max_retries; ++k) {
        hal._now = 1000 + static_cast<uint32_t>(k) * protocol::park_reflood_retry_ms
                        + static_cast<uint32_t>(k) * protocol::park_reflood_jitter_ms;   // clear each re-arm's jitter margin
        A.on_timer(kParkRefloodTimerId);
        CHECK(hal.countType("send_hash_reflood") == k);
        CHECK(count_h_tx(hal.tx_frames) == static_cast<int>(k) + 1);   // +1 = the park-time flood
    }

    // one more scan past the cap: BOUNDED — no further re-flood
    hal._now = 1000 + static_cast<uint32_t>(protocol::park_reflood_max_retries + 2) * protocol::park_reflood_retry_ms
                    + protocol::park_reflood_jitter_ms;
    A.on_timer(kParkRefloodTimerId);
    CHECK(hal.countType("send_hash_reflood") == protocol::park_reflood_max_retries);       // unchanged
    CHECK(count_h_tx(hal.tx_frames) == static_cast<int>(protocol::park_reflood_max_retries) + 1);   // unchanged

    // the giveup still fires after the decoupled hash_locate_giveup_ms window (the re-flood never removed the bound)
    hal._now = 1000 + protocol::hash_locate_giveup_ms + 1;
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

// =============================================================================
// ★★ §AB3 — THE GENERATED ADDRESS-BOOK VIEW (spec 2026-07-29 §2.1/§2.5/§2.6(a)).
// The view is a pure-read JOIN on key_hash32 over _peer_keys / _id_bind / _team_keys+_team_peer. It is corpus-dark
// twice over (`peers`/`hashof`/`nameof` live in src/ + lib/console, which the sim does not compile; and a pure read
// cannot move a stream anyway), so THESE TESTS ARE THE ONLY THING THAT COVERS IT. Named per the gate-method §E rule.
// =============================================================================
namespace {

struct BookRows {
    std::vector<Node::PeerBookRow> rows;
    static void collect(const Node::PeerBookRow& r, void* ctx) { static_cast<BookRows*>(ctx)->rows.push_back(r); }
    const Node::PeerBookRow* by_hash(uint32_t h) const {
        for (const auto& r : rows) if (r.hash == h) return &r;
        return nullptr;
    }
    const Node::PeerBookRow* by_team(uint8_t t) const {
        for (const auto& r : rows) if (r.team_id == t) return &r;
        return nullptr;
    }
    const Node::PeerBookRow* by_static(uint8_t s) const {
        for (const auto& r : rows) if (r.static_id == s) return &r;
        return nullptr;
    }
};

// A TEAM member, provisioned on both planes: a static node_id (so _id_bind self-seeds) plus is_mobile+team_id+
// team_local_id, which is what team_key_of_id / the view's pass (4) gate on.
Identity ab3_identity(uint8_t salt) {
    uint8_t seed[32]; for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i * 7 + salt);
    Identity id{}; identity_from_seed(id, seed); return id;
}

}  // namespace

TEST_CASE("§AB3 view — MERGE/DEDUP: one hash held by all three tables emits ONE row carrying all three fields") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x1114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_team_local_id(114);
    hal._now = 100000;

    const Identity ann = ab3_identity(3);
    CHECK(node.peer_key_set(ann.key_hash32, ann.ed_pub, Node::PeerKeyConf::authoritative, "Ann", 3));
    node.test_id_bind_set(/*id=*/34, ann.key_hash32, /*authoritative=*/true);        // the STATIC plane knows her as 34
    node.test_learn_route(/*dest=*/228, /*via=*/228, 1, 40, /*team_plane=*/true);    // _team_peer bit for 228
    node.team_key_set(/*id=*/228, ann.key_hash32);                                   // the TEAM plane knows her as 228

    BookRows b;
    const uint16_t n = node.peer_book_walk(/*include_id_rows=*/false, &BookRows::collect, &b);
    CHECK(n == 1);                                                    // ★ ONE row, not three: dedup is on the hash
    CHECK(b.rows.size() == 1);
    const Node::PeerBookRow& r = b.rows[0];
    CHECK(r.hash == ann.key_hash32);
    CHECK(std::string(r.name, r.name_len) == "Ann");
    CHECK(r.static_id == 34);                                         // merged from _id_bind
    CHECK(r.static_authoritative);
    CHECK(r.team_id == 228);                                          // merged from _team_keys — §18 dual identity
    CHECK(r.has_key);
    CHECK(r.conf == Node::PeerKeyConf::authoritative);
    CHECK(r.team_alias_dropped == 0);

    // the same row is reachable by BOTH id-shaped questions and by the hash — one identity, one answer
    Node::PeerBookRow st{}, tm{};
    CHECK(node.peer_book_by_id(34, st, tm) == Node::kPeerBookStatic);
    CHECK(st.hash == ann.key_hash32);
    CHECK(node.peer_book_by_id(228, st, tm) == Node::kPeerBookTeam);
    CHECK(tm.hash == ann.key_hash32);
    Node::PeerBookRow byh{};
    CHECK(node.peer_book_by_hash(ann.key_hash32, byh));
    CHECK(byh.static_id == 34); CHECK(byh.team_id == 228);
}

TEST_CASE("§AB3 view — the AMBIGUOUS reverse lookup: two team ids on one hash -> FRESHEST wins + the loser is REPORTED") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x2114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_team_local_id(114);

    const Identity ann = ab3_identity(11);
    // ⚠ THIS IS THE STATE THE SPEC WARNS ABOUT, and _team_keys is the table where it is REACHABLE: team_key_set upserts
    // BY ID and never dedups by hash, so a teammate that re-ran team-DAD leaves its OLD (id,hash) row live.
    hal._now = 100000; node.team_key_set(/*old id=*/228, ann.key_hash32);
    hal._now = 200000; node.team_key_set(/*new id=*/231, ann.key_hash32);
    node.test_learn_route(228, 228, 1, 40, /*team_plane=*/true);
    node.test_learn_route(231, 231, 1, 40, /*team_plane=*/true);
    CHECK(node.peer_key_set(ann.key_hash32, ann.ed_pub, Node::PeerKeyConf::authoritative));
    hal._now = 300000;

    BookRows b;
    CHECK(node.peer_book_walk(false, &BookRows::collect, &b) == 1);
    const Node::PeerBookRow& r = b.rows[0];
    CHECK(r.team_id == 231);                    // ★ the FRESHER last_seen_ms wins — never table order
    CHECK(r.team_alias_dropped == 1);           // ★ and the emit is TOLD a loser was dropped, per spec §2.1

    // both ids still ANSWER (each is a live alias in _team_keys) — the view names the fresher one on either query,
    // and the alias count is what makes the disagreement visible instead of silent.
    Node::PeerBookRow st{}, tm{};
    CHECK(node.peer_book_by_id(228, st, tm) == Node::kPeerBookTeam);
    CHECK(tm.hash == ann.key_hash32); CHECK(tm.team_id == 231); CHECK(tm.team_alias_dropped == 1);
    CHECK(node.peer_book_by_id(231, st, tm) == Node::kPeerBookTeam);
    CHECK(tm.team_id == 231);

    // ⚠ FINDING pinned as a test: the STATIC table cannot alias — id_bind_set calls
    // id_bind_evict_other_hash_holders on both accept paths, so a second id claiming the hash EVICTS the first.
    node.test_id_bind_set(40, ann.key_hash32, /*authoritative=*/true);
    node.test_id_bind_set(41, ann.key_hash32, /*authoritative=*/true);
    uint32_t h40 = 0;
    CHECK_FALSE(node.key_hash_of_id(40, h40));                  // 40 was evicted by 41's claim on the same hash
    Node::PeerBookRow r2{};
    CHECK(node.peer_book_by_hash(ann.key_hash32, r2));
    CHECK(r2.static_id == 41);                                  // exactly one static id can hold a hash
}

TEST_CASE("§AB3 view — ALL FOUR id-only / hash-only shapes are representable and distinguishable") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x3114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_team_local_id(114);
    hal._now = 100000;

    // (a) HASH-ONLY: a QR-pinned key with no id in either plane (spec §1.1: most rows have a hash and no name/key —
    //     this is the inverse, a key with no address).
    const Identity qr = ab3_identity(21);
    CHECK(node.peer_key_set(qr.key_hash32, qr.ed_pub, Node::PeerKeyConf::pinned, "QR", 2));
    // (b) HASH + STATIC_ID, no key/name: a heard beacon and nothing else.
    node.test_id_bind_set(/*id=*/50, 0x50505050u, /*authoritative=*/true);
    // (c) HASH + TEAM_ID, no key/name: a teammate's beacon cached its hash but we never fetched its pubkey.
    node.test_learn_route(60, 60, 1, 40, /*team_plane=*/true);
    node.team_key_set(/*id=*/60, 0x60606060u);
    // (d) TEAM-ID-ONLY: a _team_peer bit with NO _team_keys row — a teammate we route to whose hash we never cached.
    //     ★ This is the network-reachable id-only flavour (a multi-hop DV entry carries no key at all).
    node.test_learn_route(70, 70, 2, 40, /*team_plane=*/true);
    // (e) STATIC-ID-ONLY (hash 0). ⚠ REPORTED PREMISE CORRECTION: representable, but NOT a live table state — every
    //     id_bind_set caller passes a beacon/join/self hash, and the hash-uniqueness eviction collapses every hash-0
    //     row into ONE. Constructed here through the test seam only.
    node.test_id_bind_set(/*id=*/80, /*key=*/0u, /*authoritative=*/true);

    BookRows all;
    node.peer_book_walk(/*include_id_rows=*/true, &BookRows::collect, &all);
    const Node::PeerBookRow* a = all.by_hash(qr.key_hash32);   // (a) hash-only
    CHECK(a != nullptr);
    if (a) { CHECK(a->static_id == 0); CHECK(a->team_id == 0); CHECK(a->has_key); CHECK(a->conf == Node::PeerKeyConf::pinned); }
    const Node::PeerBookRow* bb = all.by_hash(0x50505050u);    // (b) hash + static_id
    CHECK(bb != nullptr);
    if (bb) { CHECK(bb->static_id == 50); CHECK(bb->team_id == 0); CHECK_FALSE(bb->has_key); CHECK(bb->name_len == 0); }
    const Node::PeerBookRow* c = all.by_hash(0x60606060u);     // (c) hash + team_id
    CHECK(c != nullptr);
    if (c) { CHECK(c->team_id == 60); CHECK(c->static_id == 0); CHECK_FALSE(c->has_key); }
    const Node::PeerBookRow* d = all.by_team(70);              // (d) team-id ONLY
    CHECK(d != nullptr);
    if (d) { CHECK(d->hash == 0);                              // ★ the id-only shape: an address with no identity yet
             CHECK(d->static_id == 0); CHECK_FALSE(d->has_key); }
    const Node::PeerBookRow* e = all.by_static(80);            // (e) static-id ONLY
    CHECK(e != nullptr);
    if (e) CHECK(e->hash == 0);

    // ⓘ hash 0 is the "no hash" sentinel, never a queryable identity — the by-hash query refuses it outright.
    Node::PeerBookRow z{};
    CHECK_FALSE(node.peer_book_by_hash(0, z));

    // ★ §2.6(a) THE BOUND, and it is what keeps the JSON surface safe: the bounded walk emits ONLY the _peer_keys-backed
    // rows, so the four addressless/keyless shapes above are invisible to it.
    BookRows bounded;
    CHECK(node.peer_book_walk(/*include_id_rows=*/false, &BookRows::collect, &bounded) == 1);
    CHECK(bounded.rows[0].hash == qr.key_hash32);
}

TEST_CASE("§AB3 view — §2.6(a): the bounded book NEVER exceeds cap_peer_keys however full _id_bind gets") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x4114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 100000;
    for (uint16_t id = 20; id <= 200; ++id)                                       // 181 static bindings
        node.test_id_bind_set(static_cast<uint8_t>(id), 0x01000000u + id, /*authoritative=*/true);
    for (uint8_t k = 0; k < 20; ++k) {                                            // 20 keys into a 16-slot cache
        const Identity p = ab3_identity(static_cast<uint8_t>(100 + k));
        (void)node.peer_key_set(p.key_hash32, p.ed_pub, Node::PeerKeyConf::authoritative);
    }
    BookRows bounded, full;
    const uint16_t nb = node.peer_book_walk(false, &BookRows::collect, &bounded);
    const uint16_t nf = node.peer_book_walk(true,  &BookRows::collect, &full);
    CHECK(nb == node.peer_key_count());
    CHECK(nb <= protocol::cap_peer_keys);          // ★ the JSON book is bounded BY CONSTRUCTION, not by paging
    CHECK(nf > nb);                                // the diagnostic list is the big one — console only
    CHECK(nf >= 181);
}

TEST_CASE("§AB3 view — an AGED key keeps its name but reports has_key=false + conf=overheard (never over-claim)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x5114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    const Identity ann = ab3_identity(31);
    CHECK(node.peer_key_set(ann.key_hash32, ann.ed_pub, Node::PeerKeyConf::authoritative, "Ann", 3));
    const Identity pin = ab3_identity(32);
    CHECK(node.peer_key_set(pin.key_hash32, pin.ed_pub, Node::PeerKeyConf::pinned, "Pin", 3));

    Node::PeerBookRow r{};
    CHECK(node.peer_book_by_hash(ann.key_hash32, r));
    CHECK(r.has_key); CHECK(r.conf == Node::PeerKeyConf::authoritative);

    hal._now = 1000 + protocol::peer_key_ttl_ms + 1;                 // the key's lease lapsed
    CHECK(node.peer_book_by_hash(ann.key_hash32, r));                // still a ROW (the name outlives the lease)
    CHECK(std::string(r.name, r.name_len) == "Ann");
    CHECK_FALSE(r.has_key);                                          // ★ peer_key_find would refuse -> a seal WOULD fail
    CHECK(r.conf == Node::PeerKeyConf::overheard);                   // ★ downgraded: never claim a capability we lack
    uint8_t ed[32]; CHECK_FALSE(node.peer_key_find(ann.key_hash32, ed));   // the view agrees with the sealer
    Node::PeerBookRow p{};
    CHECK(node.peer_book_by_hash(pin.key_hash32, p));
    CHECK(p.has_key); CHECK(p.conf == Node::PeerKeyConf::pinned);     // a PINNED key never ages
}

TEST_CASE("§AB3 view — §18: one number answering in BOTH planes reports BOTH rows, never one silently chosen") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x6114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_team_local_id(114);
    hal._now = 100000;
    const Identity stat = ab3_identity(41), team = ab3_identity(42);
    node.test_id_bind_set(/*id=*/20, stat.key_hash32, /*authoritative=*/true);       // static 20
    node.test_learn_route(20, 20, 1, 40, /*team_plane=*/true);
    node.team_key_set(/*id=*/20, team.key_hash32);                                   // TEAM 20 — a DIFFERENT node
    CHECK(node.peer_key_set(stat.key_hash32, stat.ed_pub, Node::PeerKeyConf::authoritative, "Static20", 8));
    CHECK(node.peer_key_set(team.key_hash32, team.ed_pub, Node::PeerKeyConf::authoritative, "Team20", 6));

    Node::PeerBookRow st{}, tm{};
    const uint8_t mask = node.peer_book_by_id(20, st, tm);
    CHECK(mask == (Node::kPeerBookStatic | Node::kPeerBookTeam));    // ★ BOTH, so the caller can print both
    CHECK(st.hash == stat.key_hash32); CHECK(std::string(st.name, st.name_len) == "Static20");
    CHECK(tm.hash == team.key_hash32); CHECK(std::string(tm.name, tm.name_len) == "Team20");
    CHECK(st.hash != tm.hash);                                       // two identities behind one address
    // id 0 (unprovisioned) and 0xFF (reserved) resolve to NOTHING in either plane
    CHECK(node.peer_book_by_id(0, st, tm) == 0);
    CHECK(node.peer_book_by_id(0xFF, st, tm) == 0);
}

// ★★★ THE §2.5 REGRESSION, VERBATIM FROM THE BENCH TRANSCRIPT (team node 114 asking about 228):
//        hashof 228   -> unknown
//        reqpubkey 228 -> KNEW the hash (0x6C297145, from the TEAM key cache) -> KEY CACHED
//        hashof 228   -> STILL unknown          <- THE BUG
// and the negative half is the point of the test: the fix must NOT have written _id_bind. Writing the team hash into
// the static map is register B2 (closed 2026-07-31, §id-bind-plane), an I2 breach — spec §2.5 forbids it by name.
// A test that only checked "hashof answers" would pass with the forbidden fix in place; these three CHECK_FALSEs are
// what makes it a test of the RIGHT fix.
TEST_CASE("§AB3 §2.5 regression — after reqpubkey <team-id>, the id RESOLVES via the view and _id_bind is NOT written") {
    const Identity self = ab3_identity(114);
    TestHal hal; Node node(hal, /*id=*/114, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_team_local_id(114);
    // ⚠ CHANGED 2026-08-01 (§id-hash S1b, QA finding P1c): this fixture used to run WITHOUT a crypto identity and
    // assert `queued` below — and that assertion was the false-success P1c is about. `emit_hash_query` bails at
    // `want_pubkey && !_crypto_ready` and airs nothing, so the old `queued` was firmware reporting a flood that never
    // happened, and the test was pinning it. The fixture now provisions an identity, which makes the send REAL and
    // lets the same line assert `aired` too. The no-identity path gets its own test (err_no_identity), below.
    node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;

    const Identity peer = ab3_identity(51);           // the bench's 0x6C297145
    node.test_learn_route(/*dest=*/228, /*via=*/228, 1, 40, /*team_plane=*/true);   // heard 228's team beacon
    node.team_key_set(/*id=*/228, peer.key_hash32);                                 // ...which carried its hash

    // ---- BEFORE: `hashof 228`'s OLD read path (key_hash_of_id -> _id_bind only) says unknown. That is the defect.
    uint32_t old_answer = 0;
    CHECK_FALSE(node.key_hash_of_id(228, old_answer));
    CHECK(node.id_bind_find_by_hash(peer.key_hash32) == -1);
    // ★ and key_hash_for_id agrees it holds no STATIC binding for 228 — safe to assert since §idbind-loop bounded its
    //   loop by `_id_bind_n` (until then this very line HUNG the whole native suite: `uint8_t i < 256` never ends).
    CHECK(node.key_hash_for_id(228) == 0);
    // ---- `reqpubkey 228` resolves the hash through team_key_of_id and floods a HARD WANT_PUBKEY (the transcript).
    Command rq{}; rq.kind = CmdKind::reqpubkey;
    rq.u.resolve.dst_hash = 0; rq.u.resolve.dst_id = 228; rq.u.resolve.hard = true;
    rq.u.resolve.plane = static_cast<uint8_t>(Plane::TEAM);
    const CmdResult rr = node.on_command(rq);
    CHECK(rr.code == CmdCode::queued);                // it KNEW the hash — err_no_binding would mean it did not
    CHECK(rr.aired);                                  // ★ §id-hash S1b: and a frame really went out for it
    uint32_t resolved = 0;
    CHECK(node.team_key_of_id(228, resolved));
    CHECK(resolved == peer.key_hash32);               // ★ the SAME function the view's team arm reads

    // ---- the answer arrives: the pubkey is cached (peer_key_set), exactly as the H answer path does. NOTHING else.
    CHECK(node.peer_key_set(peer.key_hash32, peer.ed_pub, Node::PeerKeyConf::authoritative, "Bench", 5));

    // ---- AFTER: `hashof 228` now RESOLVES, through the view.
    Node::PeerBookRow st{}, tm{};
    CHECK(node.peer_book_by_id(228, st, tm) == Node::kPeerBookTeam);
    CHECK(tm.hash == peer.key_hash32);
    CHECK(tm.team_id == 228);
    CHECK(tm.has_key);                                 // and it can be sealed to
    CHECK(tm.conf == Node::PeerKeyConf::authoritative);
    CHECK(std::string(tm.name, tm.name_len) == "Bench");

    // ---- ★★ THE NEGATIVE HALF — the forbidden fix was NOT taken. _id_bind holds nothing for 228 and nothing for
    //      this hash, so no team-scoped answer has leaked into the static plane (I2).
    CHECK_FALSE(node.key_hash_of_id(228, old_answer));
    CHECK(node.id_bind_find_by_hash(peer.key_hash32) == -1);
    CHECK(node.id_bind_find_by_hash(0x6C297145u) == -1);       // and nothing at all was bound for the bench's hash
    CHECK(st.hash == 0);                               // the static arm of the answer is EMPTY, correctly
    // and `nameof 0x<hash>` reaches the SAME row -> the two verbs can no longer disagree about one identity
    Node::PeerBookRow byh{};
    CHECK(node.peer_book_by_hash(peer.key_hash32, byh));
    CHECK(byh.team_id == 228);
    CHECK(std::string(byh.name, byh.name_len) == "Bench");
    CHECK(byh.static_id == 0);
}

TEST_CASE("§AB3 view — it is a PURE READ: walking the book mutates no table and emits no telemetry") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x8114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_team_local_id(114);
    hal._now = 100000;
    const Identity p = ab3_identity(61);
    CHECK(node.peer_key_set(p.key_hash32, p.ed_pub, Node::PeerKeyConf::authoritative, "P", 1));
    node.test_id_bind_set(90, p.key_hash32, /*authoritative=*/true);
    node.test_learn_route(91, 91, 1, 40, /*team_plane=*/true);
    node.team_key_set(91, 0x91919191u);
    const size_t ev_before = hal.events.size();
    const uint16_t keys_before = node.peer_key_count();

    hal._now = 100000 + protocol::peer_key_ttl_ms / 2;     // mid-lease: a refresh here WOULD extend it
    BookRows b1, b2;
    const uint16_t n1 = node.peer_book_walk(true, &BookRows::collect, &b1);
    const uint16_t n2 = node.peer_book_walk(true, &BookRows::collect, &b2);
    CHECK(n1 == n2);                                        // idempotent
    CHECK(hal.events.size() == ev_before);                  // ★ NO telemetry -> it cannot move a scenario stream
    CHECK(node.peer_key_count() == keys_before);            // ★ no eviction, no insert
    hal._now = 100000 + protocol::peer_key_ttl_ms + 1;
    Node::PeerBookRow r{};
    CHECK(node.peer_book_by_hash(p.key_hash32, r));
    CHECK_FALSE(r.has_key);                                 // the walk did NOT refresh last_seen_ms (the lease still lapsed)
    // counting with a null visitor is legal and agrees with the collected count
    CHECK(node.peer_book_walk(true, nullptr, nullptr) == n1);
}

// ✔ §AB3's FINDING, FIXED by §idbind-loop (2026-07-31) and pinned in BOTH directions here. Node::key_hash_for_id
// (node.h) used to loop a `uint8_t` counter against the 256-entry cap_id_bind, so `i < 256` was always true and **a
// MISS never returned** — UB in a const, side-effect-free function, and on a device a hang (watchdog reset) at both of
// its call sites (src/firmware_remote.cpp's `rcmd` seal, src/fw_main.cpp's sealed-rcmd-response open), both reachable
// after `unlock`. The fix — `uint16_t i < _active->_id_bind_n` — closed a SECOND defect in the same bound: the old scan
// covered the whole array, so it could answer out of the stale tail the compacting removers leave behind.
// ★ The miss assertion below is the one §AB3 was forbidden to write (it spun the suite 16 minutes with no output). It
//   is now the direct test of the fix: IF THIS FILE EVER HANGS AGAIN, the loop bound regressed — do not delete it.
TEST_CASE("§idbind-loop — key_hash_for_id terminates on a MISS (was UB: `uint8_t i < 256`) and resolves a HIT") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x9114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    node.test_id_bind_set(/*id=*/33, 0x33333333u, /*authoritative=*/true);
    CHECK(node.key_hash_for_id(33) == 0x33333333u);            // a HIT returns
    CHECK(node.key_hash_for_id(0) == 0);                       // id 0 short-circuits BEFORE the loop
    // ★★ THE MISS — 200 was never bound. Pre-fix this line did not return AT ALL; it is the whole point of the slice.
    CHECK(node.key_hash_for_id(200) == 0);
    CHECK(node.key_hash_for_id(254) == 0);                     // and the top of the id range, where uint8_t wrapped
    // ★ the sibling it now matches (key_hash_of_id) is bounded the same way, on both directions:
    uint32_t h = 0;
    CHECK(node.key_hash_of_id(33, h)); CHECK(h == 0x33333333u);
    CHECK_FALSE(node.key_hash_of_id(200, h));                  // a miss RETURNS here too — `uint16_t i < _id_bind_n`
}

// ★★ §idbind-loop, THE SECOND DEFECT — the one the `uint16_t` alone does NOT fix, and which no reading of the accessor
// reveals: the old scan ran to cap_id_bind (256) rather than the live `_id_bind_n` prefix. The compacting removers
// (id_bind_age_out / id_bind_evict_other_hash_holders / node_join.cpp's prior-id drop) only decrement `_id_bind_n` —
// they never clear the vacated slot — so a row evicted from the TAIL stays byte-intact past the prefix, and the old
// bound walked straight into it and answered with an EVICTED binding's hash.
// Driven through the real REJOIN SELF-HEAL (id_bind_evict_other_hash_holders): one hash maps to exactly ONE node_id, so
// when a node reappears under a new id with the same key, its old id->hash row must go. This is the one eviction path
// that does NOT immediately refill the freed slot (the update branch `return true`s straight after evicting), which is
// exactly why it leaves the tail readable.
TEST_CASE("§idbind-loop — key_hash_for_id is bounded by _id_bind_n, so an EVICTED tail row cannot answer") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x9114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    constexpr uint32_t kH1 = 0xAAAA1111u, kH2 = 0xBBBB2222u;
    // slot 0 = our OWN self-binding (on_init seeds it, node.cpp:524 — id 5 != 0); slot 1 = id 33 (CLAIMED, which keeps
    // the L2a mediation gate shut so this test stays about the bound, not the DENY); slot 2 = id 44 holding kH2 — the
    // TAIL row that will be evicted.
    CHECK(node.id_bind_count() == 1);                          // just the self-binding to start
    CHECK(node.test_id_bind_set(/*id=*/33, kH1, /*authoritative=*/false));
    CHECK(node.test_id_bind_set(/*id=*/44, kH2, /*authoritative=*/true));
    CHECK(node.id_bind_count() == 3);
    CHECK(node.key_hash_for_id(44) == kH2);                    // 44 resolves while it is genuinely bound

    // 33 rehomes onto kH2 (authoritative, same key, new id) -> the self-heal evicts 44's row and compacts to n=2,
    // leaving slot 2 physically intact past the prefix.
    CHECK(node.test_id_bind_set(/*id=*/33, kH2, /*authoritative=*/true));
    CHECK(node.id_bind_count() == 2);                          // the live prefix is self + 33 -> kH2
    CHECK(node.key_hash_for_id(33) == kH2);                    // the SURVIVOR still resolves (the fix is a bound, not a break)

    // ★★ THE ASSERTION THAT DISCRIMINATES: pre-fix this returned kH2 out of the stale slot-1 copy — a hash for an id
    //    whose binding no longer exists. The `_id_bind_n` bound is the ONLY reason it is 0; `uint16_t` alone would
    //    still have read the tail and answered.
    CHECK(node.key_hash_for_id(44) == 0);
    // the properly-bounded sibling and the by-hash resolver agree 44 is gone, and kH2 belongs to 33 alone
    uint32_t h = 0;
    CHECK_FALSE(node.key_hash_of_id(44, h));
    CHECK(node.id_bind_find_by_hash(kH2) == 33);
    CHECK(node.id_bind_find_by_hash(kH1) == -1);               // and kH1's row went with the overwrite
}

// =============================================================================
// ★★★ §AB4 — THE RETAINED-PEER-LOCATION RING + ITS VIEW JOIN (spec 2026-07-29 §2.7/§2.7.1/§2.7.2).
// ★★ WHY THESE TESTS ARE THE ONLY COVERAGE, stated per the gate-method §E rule and MEASURED not assumed: not one of
// the 36 corpus scenarios airs a location at all — `peer_location` / `has_location` / `lat_e7` have ZERO hits across
// every scenario's NDJSON — so the receive site's `if (loc_present)` block is NEVER ENTERED in the corpus and the
// retention is corpus-dark BY CONSTRUCTION. A poison probe on this logic would therefore be VACUOUSLY 0/36 and would
// prove nothing; native is the whole detector. The root cause is a coverage gap in the SIM, not in the firmware: the
// sim's `send` verb has no `-l` flag (its only DM suffix rule is `-t`, dm_plane_from_tail), so no scenario CAN put a
// position on the wire until that verb grows one.
// =============================================================================

TEST_CASE("§AB4 peer_loc_set/find — a position round-trips with its source, and an unknown hash reports absence") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0x0777A777u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    const uint32_t H = 0x6C297145u;
    CHECK(node.peer_loc_count() == 0);
    hal._now = 10000;                                          // 10 s of uptime -> t_s = 10
    CHECK(node.peer_loc_set(H, 523000000, 134050000, Node::PeerLocSrc::peer));
    CHECK(node.peer_loc_count() == 1);
    int32_t lat = 0, lon = 0; uint32_t age = 0xDEADu; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    hal._now = 52000;                                          // 42 s later
    CHECK(node.peer_loc_find(H, lat, lon, age, src));
    CHECK(lat == 523000000);
    CHECK(lon == 134050000);
    CHECK(age == 42);                                          // ★ ms -> s, and the age is DERIVED, never stored
    CHECK(src == Node::PeerLocSrc::peer);                       // the anchor survives the round trip
    // ★ ABSENCE: an unknown hash is `false` and the out-params are UNTOUCHED — not zeroed, so a caller cannot mistake
    //   a miss for a fix at (0,0). This is the NORMAL case for most peers, not an error.
    int32_t lat2 = 12345, lon2 = 54321; uint32_t age2 = 99; Node::PeerLocSrc src2 = Node::PeerLocSrc::team;
    CHECK_FALSE(node.peer_loc_find(0x11111111u, lat2, lon2, age2, src2));
    CHECK(lat2 == 12345); CHECK(lon2 == 54321); CHECK(age2 == 99);
    // C2: hash 0 is the "no hash" sentinel on both sides — never storable, never queryable.
    CHECK_FALSE(node.peer_loc_set(0, 1, 2, Node::PeerLocSrc::peer));
    CHECK(node.peer_loc_count() == 1);                          // and it did NOT consume a slot
    CHECK_FALSE(node.peer_loc_find(0, lat, lon, age, src));
}

TEST_CASE("§AB4 peer_loc_set — a FRESHER position REPLACES the older one for the same hash (no second slot)") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0x0777A777u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    const uint32_t H = 0x6C297145u;
    hal._now = 1000;  CHECK(node.peer_loc_set(H, 100, 200, Node::PeerLocSrc::peer));
    hal._now = 61000; CHECK(node.peer_loc_set(H, 300, 400, Node::PeerLocSrc::peer));   // the peer moved, 60 s on
    CHECK(node.peer_loc_count() == 1);                          // ★ REFRESH IN PLACE — a peer is one row, always
    int32_t lat = 0, lon = 0; uint32_t age = 0; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    CHECK(node.peer_loc_find(H, lat, lon, age, src));
    CHECK(lat == 300); CHECK(lon == 400);                       // the NEW position
    CHECK(age == 0);                                            // ...and the age reset with it (this is the point)
    // ★ A refresh also re-stamps the anchor, so a later weaker (group) claim does not inherit the earlier row's
    //   `peer` strength. Directionality matters: the field describes THIS position, not the peer.
    hal._now = 62000; CHECK(node.peer_loc_set(H, 500, 600, Node::PeerLocSrc::team));
    CHECK(node.peer_loc_find(H, lat, lon, age, src));
    CHECK(src == Node::PeerLocSrc::team);
}

TEST_CASE("§AB4 peer_loc_set — the ring FILLS to cap_peer_loc then evicts the STALEST, never the wrong one") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0x0777A777u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    const uint8_t CAP = protocol::cap_peer_loc;                 // the ring's own member is private; the cap is not
    CHECK(CAP == 16);                                           // pinned: the spec's team-scale sizing
    // Fill it, OLDEST FIRST but with slot 0 deliberately NOT the oldest — hash 0x100 is stamped at t=5 s and the rest
    // walk forward from 10 s, so an "evict slot 0" or "evict the newest" bug is distinguishable from evict-stalest.
    hal._now = 20000; CHECK(node.peer_loc_set(0x100u, 1, 1, Node::PeerLocSrc::peer));
    for (uint8_t i = 1; i < CAP; ++i) {
        hal._now = 1000ull * (10 + i);
        CHECK(node.peer_loc_set(0x100u + i, i, i, Node::PeerLocSrc::peer));
    }
    CHECK(node.peer_loc_count() == CAP);
    hal._now = 5000; CHECK(node.peer_loc_set(0x101u, 42, 42, Node::PeerLocSrc::peer));   // re-stamp 0x101 as the STALEST
    hal._now = 100000;
    CHECK(node.peer_loc_set(0x200u, 9, 9, Node::PeerLocSrc::peer));                      // full -> one must go
    CHECK(node.peer_loc_count() == CAP);                        // ★ bounded: it never grows past the cap
    int32_t lat = 0, lon = 0; uint32_t age = 0; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    CHECK_FALSE(node.peer_loc_find(0x101u, lat, lon, age, src));  // ★ the STALEST was the victim
    CHECK(node.peer_loc_find(0x200u, lat, lon, age, src));        // the newcomer landed
    CHECK(lat == 9);
    CHECK(node.peer_loc_find(0x100u, lat, lon, age, src));        // and slot 0 — NOT the stalest — survived
    CHECK(lat == 1);
    for (uint8_t i = 2; i < CAP; ++i) CHECK(node.peer_loc_find(0x100u + i, lat, lon, age, src));   // every other row intact
}

TEST_CASE("§AB4 peer_loc_find — a BACKWARDS clock reports MAXIMALLY STALE, never age 0") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0x0777A777u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    const uint32_t H = 0x6C297145u;
    hal._now = 500000; CHECK(node.peer_loc_set(H, 1, 2, Node::PeerLocSrc::peer));
    int32_t lat = 0, lon = 0; uint32_t age = 0; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    hal._now = 1000;                                            // the clock moved BACKWARDS
    CHECK(node.peer_loc_find(H, lat, lon, age, src));
    // ★ The failure DIRECTION is the assertion: 0 would render an unknown-vintage position as CURRENT, which is exactly
    //   the misleading-stale-fix mode the RAM-only ruling exists to prevent. Maximally stale makes the app discard it.
    CHECK(age == 0xFFFFFFFFu);
}

TEST_CASE("§AB4 view — the address-book row CARRIES the position, and a peer without one renders cleanly") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x1114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    Identity a = ab3_identity(3), b = ab3_identity(9);
    CHECK(node.peer_key_set(a.key_hash32, a.ed_pub, Node::PeerKeyConf::authoritative));
    CHECK(node.peer_key_set(b.key_hash32, b.ed_pub, Node::PeerKeyConf::authoritative));
    hal._now = 4000; CHECK(node.peer_loc_set(a.key_hash32, 523000000, 134050000, Node::PeerLocSrc::peer));
    hal._now = 34000;                                           // 30 s later
    // (1) the BOUNDED book — the JSON surface, and the ONE that the app actually reads.
    BookRows rows; CHECK(node.peer_book_walk(/*include_id_rows=*/false, BookRows::collect, &rows) == 2);
    const auto* ra = rows.by_hash(a.key_hash32);
    CHECK(ra != nullptr);
    if (ra) {
        CHECK(ra->has_location);
        CHECK(ra->lat_e7 == 523000000);
        CHECK(ra->lon_e7 == 134050000);
        CHECK(ra->loc_age_s == 30);
        CHECK(ra->loc_src == Node::PeerLocSrc::peer);
    }
    // ★ THE ABSENCE ARM, and it is not a formality: b is a fully-known peer (key, authoritative) that simply never
    //   sent a position. It must render as a normal row with NOTHING positional — not a fix at (0,0).
    const auto* rb = rows.by_hash(b.key_hash32);
    CHECK(rb != nullptr);
    if (rb) {
        CHECK(rb->has_key);                                     // known in every other respect
        CHECK_FALSE(rb->has_location);
        CHECK(rb->lat_e7 == 0); CHECK(rb->lon_e7 == 0); CHECK(rb->loc_age_s == 0);
    }
    // (2) the SINGLE-hash query — `nameof`/`hashof` read this, so all three verbs see one position or none.
    Node::PeerBookRow one{};
    CHECK(node.peer_book_by_hash(a.key_hash32, one));
    CHECK(one.has_location); CHECK(one.loc_age_s == 30); CHECK(one.loc_src == Node::PeerLocSrc::peer);
    Node::PeerBookRow none{};
    CHECK(node.peer_book_by_hash(b.key_hash32, none));
    CHECK_FALSE(none.has_location);
}

TEST_CASE("§AB4 view — an UNKEYED hash still shows its position (the shape CL2's channel source will land on)") {
    TestHal hal; Node node(hal, /*id=*/114, /*key=*/0x1114A11Au);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    // A hash we hold an id_bind for but NO ed_pub — i.e. no _peer_keys row, so the bounded book's pass (1) never sees
    // it and only `peers all`'s pass (2) emits it. ★ Under owner ruling O5 this is precisely the shape a TEAM-channel
    // position arrives on (authenticated as a teammate via _team_keys, with no pairwise key), so covering it now is
    // what stops CL2 having to hunt for a missed join.
    const uint32_t H = 0x6C297145u;
    CHECK(node.test_id_bind_set(/*id=*/34, H, /*authoritative=*/true));
    hal._now = 1000; CHECK(node.peer_loc_set(H, 111, 222, Node::PeerLocSrc::peer));
    hal._now = 6000;
    BookRows bounded; CHECK(node.peer_book_walk(/*include_id_rows=*/false, BookRows::collect, &bounded) == 0);  // no key -> not in the book
    BookRows all; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all);
    const auto* r = all.by_hash(H);
    CHECK(r != nullptr);
    if (r) {
        CHECK_FALSE(r->has_key);                                // keyless, exactly the O5 case
        CHECK(r->static_id == 34);
        CHECK(r->has_location);                                 // ★ and the position is STILL joined
        CHECK(r->lat_e7 == 111); CHECK(r->lon_e7 == 222); CHECK(r->loc_age_s == 5);
    }
    // An ID-ONLY row (no hash at all) has no identity to key a position by and must stay positionless.
    CHECK(node.test_id_bind_set(/*id=*/77, /*hash=*/0, /*authoritative=*/false));
    BookRows all2; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all2);
    const auto* idonly = all2.by_static(77);
    CHECK(idonly != nullptr);
    if (idonly) { CHECK(idonly->hash == 0); CHECK_FALSE(idonly->has_location); }
}

TEST_CASE("§AB4 — the ring is RAM-ONLY and its cap is the team-scale 16 (so the RAM cost stays the ledger's 320 B)") {
    // ★ THE ANTI-REGRESSION FOR THE RULING ITSELF. AB1 persists names and authoritative keys; location is the
    //   deliberate exception, because a stale position is worse than none and a captured node must not yield the team's
    //   positions. So there is NO NV record and no peer_loc call anywhere in src/ — a later slice "completing" it would
    //   have to add both, and the reasons are written at the ring in node.h so it does not.
    CHECK(protocol::cap_peer_loc == 16);         // the spec's team-scale sizing; x 20 B/record = 320 B, NOT the briefed 256
    // ⓘ Two properties are pinned by static_assert in node.h instead of here, deliberately: PeerLoc's 20-byte layout
    //   with `offsetof(src) == 16` (a compile-time assert fires on every BOARD toolchain, which a native test cannot),
    //   and sizeof(Node). And the NODE-GLOBAL property — one row per identity, not one per leaf, because a key_hash32 is
    //   layer-independent — is asserted in test_dual_layer.cpp, next to the DualLayerTestAccess that can swap leaves,
    //   rather than by making this file a second friend of Node.
    // A fresh node holds nothing: the zeroed ring reads as empty, so `key_hash32 == 0` really is the unused sentinel.
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0x0777A777u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    CHECK(node.peer_loc_count() == 0);
    int32_t lat = 1, lon = 2; uint32_t age = 3; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    CHECK_FALSE(node.peer_loc_find(0x0777A777u, lat, lon, age, src));   // not even for ITS OWN hash
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ §id-hash S1 (spec docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md §1-A / §3-D9)
//
// THE DEFECT, bench-proven 2026-08-01 and reproduced as an executable BEFORE-arm below: `reqpubkey <bare id>`
// resolved id -> hash through `team_key_of_id` ALONE, whose first line is
//     if (_cfg.team_id == 0 || !is_team_peer(id)) return false;                     (node_routing.cpp:842)
// ⇒ on a STATIC node the verb could not succeed for ANY id, while `hashof <id>` answered the same number
// happily out of `_id_bind`. Two verbs, one question, two tables — §AB3's own diagnosis, whose fix landed on
// `hashof` and never here.
//
// ⚠ COVERAGE NOTE (why this lives in native and not in a scenario): the sim's console has NO by-id reqpubkey
// at all — `NodeRuntimeWrapper.cpp:909` parses `reqpubkey <hex>` only and hard-sets `dst_id = 0`. So the
// corpus cannot reach this arm even though it DOES execute the surrounding reqpubkey code (s22). Measured,
// not assumed; see the BASELINE note's poison matrix.
//
// ★★ EVERY NODE BELOW INSTALLS A CRYPTO IDENTITY, and that is load-bearing rather than boilerplate: a
// WANT_PUBKEY query bails at `if (want_pubkey && !_crypto_ready)` (node_hashlocate.cpp:1524) with
// `h_want_pubkey_no_identity` and NO flood. Without an identity, "no h_tx after a refusal" would be true for
// a SUCCESS too — i.e. the airtime assertions would be vacuous. The first test's positive `h_tx != nullptr`
// is the control that proves they are not. (Found the hard way: the first draft of this block had no
// identity and that assertion was the only thing that failed.)
// ════════════════════════════════════════════════════════════════════════════════════════════════════════

namespace {
// A reqpubkey Command in the exact shape console_parse.cpp now produces. plane: 0 = AUTO (a bare `reqpubkey <id>`),
// 1 = TEAM (`-t`), 2 = GLOBAL/static (`-s`).
Command s1_reqpubkey_by_id(uint8_t id, uint8_t plane) {
    Command c{}; c.kind = CmdKind::reqpubkey;
    c.u.resolve.dst_hash = 0; c.u.resolve.dst_id = id; c.u.resolve.hard = true; c.u.resolve.plane = plane;
    return c;
}
}  // namespace

TEST_CASE("§id-hash S1 — reqpubkey <id> RESOLVES ON THE STATIC PLANE (the bench defect: it could not, for any id)") {
    const Identity self = ab3_identity(101);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);            // spec §0's bench node: static 42
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.team_id = 0;                                                   // ★ A PURE STATIC NODE — no team plane at all
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;

    const uint32_t k186 = 0x61CD83EAu;                                 // spec §0: `hashof 186` answered this
    CHECK(node.test_id_bind_set(/*id=*/186, k186, /*authoritative=*/true));

    // ---- THE BEFORE-ARM, executable: the OLD resolver refuses this id outright, on a node that plainly knows it.
    uint32_t team_answer = 0;
    CHECK_FALSE(node.team_key_of_id(186, team_answer));                // <- the WHOLE of the old resolution path
    uint32_t static_answer = 0;
    CHECK(node.key_hash_of_id(186, static_answer));                    // ...while the STATIC table answers
    CHECK(static_answer == k186);                                      // ★ the two tables disagreed; that WAS the bug

    // ---- AFTER: the verb resolves, floods, and ECHOES the hash it flew for + the plane it flew on (§3-D9).
    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult r = node.on_command(s1_reqpubkey_by_id(186, /*plane=*/0 /*AUTO — a bare `reqpubkey 186`*/));
    CHECK(r.code == CmdCode::queued);                                  // was err_no_binding, for every id, forever
    CHECK(r.dst_hash == k186);                                         // ★ the RESOLVED hash rides the result...
    CHECK(r.plane == 2);                                               // ...and it resolved on the STATIC plane
    // ★★ THE POSITIVE CONTROL for every "no h_tx" assertion in this block: a query DOES fly here, so their absence
    //    elsewhere is a property of the refusal and not of the fixture.
    const Ev* q = find_ev(hal.events, "h_tx");
    CHECK(q != nullptr);
    if (q) CHECK(q->key_hash32 == static_cast<int64_t>(k186));         // ...for the RESOLVED hash, not for 0
    CHECK(find_ev(hal.events, "h_want_pubkey_no_identity") == nullptr);// the fixture really is crypto-ready

    // The resolver is the SHARED one, so `hashof 186` cannot answer differently (that divergence is the defect class).
    Node::PeerBookRow st{}, tm{};
    CHECK(node.peer_book_by_id(186, st, tm) == Node::kPeerBookStatic);
    CHECK(st.hash == k186);
}

TEST_CASE("§id-hash S1 — a forced plane must MATCH: `-t` on a static-only node refuses, echoing the plane searched") {
    const Identity self = ab3_identity(102);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.team_id = 0;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;
    CHECK(node.test_id_bind_set(/*id=*/186, 0x61CD83EAu, /*authoritative=*/true));

    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult t = node.on_command(s1_reqpubkey_by_id(186, /*plane=*/1 /*`-t`*/));
    CHECK(t.code == CmdCode::err_no_binding);          // the id is known — but NOT on the plane the operator named
    CHECK(t.plane == 1);                               // ★ the echo is what lets the console say "drop -t"
    CHECK(t.dst_hash == 0);                            // nothing resolved, so nothing is echoed as resolved
    CHECK(find_ev(hal.events, "h_tx") == nullptr);     // ★ a refusal spends NO AIRTIME (control: test 1 flies one)
    CHECK(hal.tx_frames.empty());

    // `-s` on the same node is the same query the bare form picked, and it succeeds.
    hal.events.clear();
    const CmdResult s = node.on_command(s1_reqpubkey_by_id(186, /*plane=*/2 /*`-s`*/));
    CHECK(s.code == CmdCode::queued);
    CHECK(s.dst_hash == 0x61CD83EAu);
    CHECK(s.plane == 2);
    CHECK(find_ev(hal.events, "h_tx") != nullptr);     // ...and this same fixture CAN fly one
}

TEST_CASE("§id-hash S1 — an id UNKNOWN in both planes refuses with plane 0 = 'neither', and floods nothing") {
    const Identity self = ab3_identity(103);
    TestHal hal; Node node(hal, /*id=*/114, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;   // a DUAL-plane node, so "neither" is a real answer not a stub
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub); node.set_team_local_id(114);
    hal._now = 100000;

    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult r = node.on_command(s1_reqpubkey_by_id(109, /*plane=*/0));   // spec §0's unresolvable 109
    CHECK(r.code == CmdCode::err_no_binding);
    CHECK(r.plane == 0);                               // ★ 0 = neither plane was selected, which is the honest echo
    CHECK(r.dst_hash == 0);
    CHECK(find_ev(hal.events, "h_tx") == nullptr);     // C2: never flood a query for hash 0
    CHECK(hal.tx_frames.empty());
    // SAME-FIXTURE CONTROL: give this node a team binding for 109 and the identical command now flies.
    node.test_learn_route(/*dest=*/109, /*via=*/109, 1, 40, /*team_plane=*/true);
    node.team_key_set(/*id=*/109, 0x1090109Au);
    hal.events.clear();
    const CmdResult r2 = node.on_command(s1_reqpubkey_by_id(109, /*plane=*/0));
    CHECK(r2.code == CmdCode::queued); CHECK(r2.plane == 1); CHECK(r2.dst_hash == 0x1090109Au);
    CHECK(find_ev(hal.events, "h_tx") != nullptr);
    // ⓘ §3-D9's "unresolved id on a dual-plane node -> require a flag" arm is deliberately NOT implemented in S1, and
    //   the first half of this test is why it CANNOT be: the refusal happens BEFORE any plane is selected, so a flag
    //   would change nothing observable. It becomes live in S4a, where an unresolved id does fly a by-id query.
}

TEST_CASE("§id-hash S1 §3-D9 — one number in BOTH planes refuses err_ambiguous_plane; `-s`/`-t` then pick, and differ") {
    const Identity self = ab3_identity(104);
    TestHal hal; Node node(hal, /*id=*/114, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub); node.set_team_local_id(114);
    hal._now = 100000;

    // The §18 collision, built exactly as the §AB3 view test builds it: static 20 and TEAM 20 are DIFFERENT peers.
    const Identity stat = ab3_identity(41), team = ab3_identity(42);
    CHECK(stat.key_hash32 != team.key_hash32);
    CHECK(node.test_id_bind_set(/*id=*/20, stat.key_hash32, /*authoritative=*/true));
    node.test_learn_route(20, 20, 1, 40, /*team_plane=*/true);
    node.team_key_set(/*id=*/20, team.key_hash32);

    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult amb = node.on_command(s1_reqpubkey_by_id(20, /*plane=*/0));
    CHECK(amb.code == CmdCode::err_ambiguous_plane);   // ★ NOT err_no_binding: the remedy is the opposite one
    CHECK(amb.plane == 0);                             // nothing was selected — that IS the report
    CHECK(amb.dst_hash == 0);
    CHECK(find_ev(hal.events, "h_tx") == nullptr);     // ★★ and no airtime is spent guessing: the whole point of D9
    CHECK(hal.tx_frames.empty());

    // Forced, the two planes give the two DIFFERENT hashes — so guessing would have queried the wrong peer.
    hal.events.clear();
    const CmdResult t = node.on_command(s1_reqpubkey_by_id(20, /*plane=*/1));
    CHECK(t.code == CmdCode::queued); CHECK(t.dst_hash == team.key_hash32); CHECK(t.plane == 1);
    CHECK(find_ev(hal.events, "h_tx") != nullptr);     // ...the same fixture floods happily once a plane is named
    CHECK_FALSE(hal.tx_frames.empty());                // (and a real frame reached the radio, not just the emit)
    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult s = node.on_command(s1_reqpubkey_by_id(20, /*plane=*/2));
    CHECK(s.dst_hash == stat.key_hash32); CHECK(s.plane == 2);   // it RESOLVED the static row (the D9 half)
    CHECK(s.dst_hash != t.dst_hash);                   // ★ the counterfactual: one silent pick = the wrong hash, 50 %
    // ★★ §id-hash S1b (QA finding P1c) — B47 IS NOW FIXED, AND THIS ASSERTION IS THE PROOF. This fixture is an
    //    UNREGISTERED (off-grid) mobile, so the GLOBAL query resolves its hash and then bails inside emit_hash_query
    //    at `want_pubkey && mobile_req && origin == _node_id && !team_scoped` — a mobile's node_id is a LOCAL id the
    //    owner has no route back to. It airs NOTHING.
    //    ⚠ THIS LINE USED TO ASSERT `queued`, and that was the false success: the firmware reported a flood that
    //    provably did not happen, and over BLE it became `{"ev":"reqpubkey_sent"}`. Now the outcome is reported.
    CHECK(s.code == CmdCode::err_no_gateway);          // ★ was CmdCode::queued — the honest answer for "no way back"
    CHECK_FALSE(s.aired);                              // ★ and the BLE-visible bit says so, so no reqpubkey_sent
    CHECK(find_ev(hal.events, "h_want_pubkey_mobile_no_route") != nullptr);
    CHECK(find_ev(hal.events, "h_tx") == nullptr);
    CHECK(hal.tx_frames.empty());
    // ...while the TEAM arm on the SAME node in the SAME test DID air and DID set the bit (the same-fixture control).
    CHECK(t.aired);
}

TEST_CASE("§id-hash S1 — the TEAM arm is UNREGRESSED: a bare id on a team-only node still resolves via _team_keys") {
    const Identity self = ab3_identity(105);
    TestHal hal; Node node(hal, /*id=*/114, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub); node.set_team_local_id(114);
    hal._now = 100000;
    const Identity peer = ab3_identity(51);
    node.test_learn_route(/*dest=*/228, /*via=*/228, 1, 40, /*team_plane=*/true);
    node.team_key_set(/*id=*/228, peer.key_hash32);

    // 228 exists ONLY in the team plane, so the bare form picks TEAM with no flag — the pre-S1 behaviour, preserved.
    const CmdResult bare = node.on_command(s1_reqpubkey_by_id(228, /*plane=*/0));
    CHECK(bare.code == CmdCode::queued);
    CHECK(bare.dst_hash == peer.key_hash32);
    CHECK(bare.plane == 1);                            // ★ TEAM, chosen because it is the ONLY plane that holds 228
    // and the explicit `-t` (what every existing caller sends) is identical
    const CmdResult forced = node.on_command(s1_reqpubkey_by_id(228, /*plane=*/1));
    CHECK(forced.code == CmdCode::queued); CHECK(forced.dst_hash == peer.key_hash32); CHECK(forced.plane == 1);
    // ★ AND the negative half §AB3 established: no team answer leaked into the static map (I2).
    uint32_t sh = 0;
    CHECK_FALSE(node.key_hash_of_id(228, sh));
    CHECK(node.id_bind_find_by_hash(peer.key_hash32) == -1);
}

TEST_CASE("§id-hash S1 — a CLAIMED static binding does not satisfy reqpubkey (the authoritative floor is unchanged)") {
    const Identity self = ab3_identity(106);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.team_id = 0;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;
    // §3-D6 puts `reqpubkey` at the `claimed` floor — but that needs S3's floor parameter, which does not exist yet.
    // S1 therefore inherits key_hash_of_id's AUTHORITATIVE-only gate, and this pins that so S3's change is VISIBLE
    // (an assertion that must flip is worth more than an absent one).
    CHECK(node.test_id_bind_set(/*id=*/77, 0x77777777u, /*authoritative=*/false));
    const CmdResult r = node.on_command(s1_reqpubkey_by_id(77, /*plane=*/0));
    CHECK(r.code == CmdCode::err_no_binding);          // ✖ MISSING (deferred to S3): the `claimed` floor + `actual`
    CHECK(r.plane == 0);
    // control: the SAME id at authoritative resolves — so the refusal above is the confidence gate, not the id
    CHECK(node.test_id_bind_set(/*id=*/77, 0x77777777u, /*authoritative=*/true));
    const CmdResult r2 = node.on_command(s1_reqpubkey_by_id(77, /*plane=*/0));
    CHECK(r2.code == CmdCode::queued); CHECK(r2.dst_hash == 0x77777777u); CHECK(r2.plane == 2);
}

TEST_CASE("§id-hash S1 — the by-HASH form is untouched: same plane, and dst_hash echoes what was asked") {
    const Identity self = ab3_identity(107);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;
    Command c{}; c.kind = CmdKind::reqpubkey;
    c.u.resolve.dst_hash = 0xDEADBEEFu; c.u.resolve.dst_id = 0; c.u.resolve.hard = true;
    c.u.resolve.plane = 2;                             // what `reqpubkey 0xdeadbeef` has always produced
    const CmdResult r = node.on_command(c);
    CHECK(r.code == CmdCode::queued);
    CHECK(r.dst_hash == 0xDEADBEEFu);                  // the transport echoes THIS, with no lookup of its own
    CHECK(r.plane == 2);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ §id-hash S2 (spec §1-C plane symmetry / §1-D self-skip) — `peers all` told two different stories about
// the same condition, one per plane. Bench evidence, spec §0, ONE node:
//     [peer] hash=0x7B18ADA2 static_id=245(auth)   [peer] team_id=114   <- routable, unidentifiable: LISTED
//     [route] dest=48  next=186 hops=3             [peers] count=2      <- routable, unidentifiable: INVISIBLE
//     [route] dest=109 next=186 hops=2
// ...and the one row it did print for the static plane was OURSELVES (`static_id=42(auth)` on node 42).
// ════════════════════════════════════════════════════════════════════════════════════════════════════════

TEST_CASE("§id-hash S2 §1-D — the book no longer lists US: the self-binding is present in _id_bind and SKIPPED") {
    const Identity self = ab3_identity(120);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 100000;

    // ★ THE BEFORE-ARM: the self row really IS in the table (on_init self-seeds it), so the skip below is a live
    //   filter, not an assertion about an empty table.
    CHECK(node.id_bind_find_by_hash(self.key_hash32) == 42);
    uint32_t own = 0;
    CHECK(node.key_hash_of_id(42, own));                 // ...and it is AUTHORITATIVE, which is why it printed "(auth)"
    CHECK(own == self.key_hash32);

    BookRows all; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all);
    CHECK(all.by_static(42) == nullptr);                 // ★ gone: an address book is a list of OTHERS
    CHECK(all.by_hash(self.key_hash32) == nullptr);
    CHECK(all.rows.empty());                             // and on a fresh node that is the WHOLE book

    // CONTROL: a genuine peer still lists, so the filter is `us`, not `everything`.
    CHECK(node.test_id_bind_set(/*id=*/186, 0x61CD83EAu, /*authoritative=*/true));
    BookRows all2; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all2);
    CHECK(all2.rows.size() == 1);
    const auto* p = all2.by_static(186);
    CHECK(p != nullptr);
    if (p) { CHECK(p->hash == 0x61CD83EAu); CHECK(p->static_authoritative); }
    CHECK(all2.by_static(42) == nullptr);
}

TEST_CASE("§id-hash S2 §1-C — a routed-but-unkeyed STATIC dest is now a row, exactly as its team twin already was") {
    const Identity self = ab3_identity(121);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;     // BOTH planes live, so the symmetry is testable on one node
    node.on_init(cfg); node.set_team_local_id(114);
    hal._now = 100000;

    // The bench's three static dests, routed and unbound...
    node.test_learn_route(/*dest=*/48,  /*via=*/186, 3, 40, /*team_plane=*/false);
    node.test_learn_route(/*dest=*/59,  /*via=*/186, 2, 40, /*team_plane=*/false);
    node.test_learn_route(/*dest=*/109, /*via=*/186, 2, 40, /*team_plane=*/false);
    // ...and the bench's two team ids, routed and unbound — pass (4)'s existing shape, kept as the SYMMETRY CONTROL.
    node.test_learn_route(/*dest=*/114, /*via=*/114, 1, 40, /*team_plane=*/true);
    node.test_learn_route(/*dest=*/214, /*via=*/214, 1, 40, /*team_plane=*/true);
    CHECK(node.rt_count() >= 3);

    BookRows all; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all);
    for (uint8_t d : {48, 59, 109}) {
        const auto* r = all.by_static(d);
        CHECK(r != nullptr);                             // ★ THE FIX: the static plane finally answers
        if (r) {
            CHECK(r->hash == 0);                         // id-only: we route to it, we cannot name it
            CHECK_FALSE(r->static_authoritative);        // there is no binding to vouch for (the renderer omits the tag)
            CHECK_FALSE(r->has_key);
            CHECK(r->team_id == 0);                      // a static row does not borrow the team plane's identity
        }
    }
    CHECK(all.by_team(214) != nullptr);                  // the team twin still works — this is a SYMMETRY fix, not a swap
    CHECK(all.by_static(42) == nullptr);                 // ...and §1-D still holds: we are not in our own book

    // DEDUP: bind 59 and it collapses to ONE row, the KEYED one — never a duplicate id-only twin.
    CHECK(node.test_id_bind_set(/*id=*/59, 0x59595959u, /*authoritative=*/true));
    BookRows all2; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all2);
    int n59 = 0; for (const auto& r : all2.rows) if (r.static_id == 59) ++n59;
    CHECK(n59 == 1);
    const auto* r59 = all2.by_static(59);
    if (r59) { CHECK(r59->hash == 0x59595959u); CHECK(r59->static_authoritative); }
    // ★ AND the dedup is against _id_bind MEMBERSHIP, not against key_hash_of_id: a CLAIMED binding is invisible to
    //   that accessor but pass (2) still emits it, so testing through the accessor would print 48 twice.
    CHECK(node.test_id_bind_set(/*id=*/48, 0x48484848u, /*authoritative=*/false));
    uint32_t unused = 0;
    CHECK_FALSE(node.key_hash_of_id(48, unused));        // <- the accessor says "nothing here"...
    BookRows all3; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all3);
    int n48 = 0; for (const auto& r : all3.rows) if (r.static_id == 48) ++n48;
    CHECK(n48 == 1);                                     // ...and the book still emits exactly one row for 48
}

TEST_CASE("§id-hash S2 §2.6(a) — the JSON book (include_id_rows=false) is UNTOUCHED by the new pass") {
    const Identity self = ab3_identity(122), peer = ab3_identity(123);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 100000;
    CHECK(node.peer_key_set(peer.key_hash32, peer.ed_pub, Node::PeerKeyConf::authoritative, "P", 1));
    for (uint8_t d = 50; d < 60; ++d) node.test_learn_route(d, 186, 2, 40, /*team_plane=*/false);

    BookRows bounded; const uint16_t nb = node.peer_book_walk(/*include_id_rows=*/false, BookRows::collect, &bounded);
    CHECK(nb == 1);                                      // ★ only the _peer_keys row — no route rows leaked into it
    CHECK(bounded.rows.size() == 1);
    CHECK(bounded.rows[0].hash == peer.key_hash32);
    // ...while the diagnostic form sees all ten. That difference IS the §2.6(a) bound the BLE transport relies on.
    BookRows all; const uint16_t na = node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all);
    CHECK(na == 11);
    for (uint8_t d = 50; d < 60; ++d) CHECK(all.by_static(d) != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ §id-hash S2b (spec §1-E) — id_bind_set was NOT upgrade-only. A LIVE bug, needing no new feature:
// `_id_bind[i].confidence = incoming` ran unconditionally on a matching row, so one relayed soft H answer
// (h_relay/claimed, node_hashlocate.cpp:1252) DEMOTED a first-hand beacon binding, and key_hash_of_id — which
// hard-filters non-authoritative rows (:148) — then refused DST_HASH for that peer until the next beacon.
// The sibling store peer_key_set has had the opposite (correct) rule since Phase 1 (:255-261).
//
// ⓘ The fixture drives `claimed` through test_id_bind_set (source `bcn`). The demotion is a function of
// CONFIDENCE alone, so this is the faithful shape of the h_relay path; `source` itself has no public reader
// and deliberately did not gain one for a test (C1).
// ════════════════════════════════════════════════════════════════════════════════════════════════════════

TEST_CASE("§id-hash S2b §1-E — a CLAIMED sighting cannot demote an AUTHORITATIVE binding (the seal path survives)") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xD0D0D0D0u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    const uint32_t H = 0x61CD83EAu;

    CHECK(node.test_id_bind_set(/*id=*/186, H, /*authoritative=*/true));    // a first-hand beacon
    uint32_t out = 0;
    CHECK(node.key_hash_of_id(186, out)); CHECK(out == H);                  // the seal/DST_HASH path can use it

    hal._now = 2000;
    CHECK(node.test_id_bind_set(/*id=*/186, H, /*authoritative=*/false));   // ★ the relayed claim, SAME hash
    Node::IdBindConf conf = Node::IdBindConf::claimed;
    CHECK(node.id_bind_find_by_hash(H, &conf) == 186);
    CHECK(conf == Node::IdBindConf::authoritative);                         // ★ NOT demoted (was: claimed)
    out = 0;
    CHECK(node.key_hash_of_id(186, out));                                   // ★ and the reader still answers
    CHECK(out == H);
    // ...and the display agrees, so `peers`/`hashof` cannot report a downgrade that did not happen.
    BookRows all; node.peer_book_walk(/*include_id_rows=*/true, BookRows::collect, &all);
    const auto* r = all.by_static(186);
    CHECK(r != nullptr);
    if (r) CHECK(r->static_authoritative);
}

TEST_CASE("§id-hash S2b — UPGRADE still applies, and a claimed row still refreshes on a claimed sighting") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xD1D1D1D1u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.id_bind_ttl_ms = 5000;
    node.on_init(cfg);
    const uint32_t H = 0x77778888u;

    hal._now = 1000;
    CHECK(node.test_id_bind_set(/*id=*/90, H, /*authoritative=*/false));    // claimed first
    Node::IdBindConf conf = Node::IdBindConf::authoritative;
    CHECK(node.id_bind_find_by_hash(H, &conf) == 90); CHECK(conf == Node::IdBindConf::claimed);
    uint32_t out = 0;
    CHECK_FALSE(node.key_hash_of_id(90, out));                              // claimed is below the seal floor

    // (a) claimed -> claimed REFRESHES: an unverified row's lease is its own to extend.
    hal._now = 1000 + 4000;
    CHECK(node.test_id_bind_set(/*id=*/90, H, /*authoritative=*/false));
    hal._now = 1000 + 4000 + 4000;                                          // > TTL from the FIRST sighting
    CHECK(node.id_bind_find_by_hash(H) == 90);                              // still alive -> it was refreshed

    // (b) claimed -> AUTHORITATIVE upgrades (the direction the fix must NOT block).
    CHECK(node.test_id_bind_set(/*id=*/90, H, /*authoritative=*/true));
    conf = Node::IdBindConf::claimed;
    CHECK(node.id_bind_find_by_hash(H, &conf) == 90); CHECK(conf == Node::IdBindConf::authoritative);
    out = 0;
    CHECK(node.key_hash_of_id(90, out)); CHECK(out == H);
}

TEST_CASE("§id-hash S2b — a CLAIMED sighting does not extend an AUTHORITATIVE row's lease (owner-specified)") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xD2D2D2D2u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.id_bind_ttl_ms = 5000;
    node.on_init(cfg);
    const uint32_t H = 0x99990000u;

    hal._now = 1000;
    CHECK(node.test_id_bind_set(/*id=*/91, H, /*authoritative=*/true));     // first-hand at t=1000
    hal._now = 1000 + 4000;
    CHECK(node.test_id_bind_set(/*id=*/91, H, /*authoritative=*/false));    // hearsay at t=5000
    hal._now = 1000 + 5000;                                                 // exactly TTL after the FIRST-HAND one
    // ★ EXPIRED. Under the old code the claim refreshed last_seen_ms to 5000 and this row would live to t=10000 —
    //   i.e. a third party's say-so kept a first-hand binding alive. `last_seen_ms` on an authoritative row means
    //   "when we last had FIRST-HAND evidence", and only first-hand evidence may move it (spec §3-D5c's symmetry).
    CHECK(node.id_bind_find_by_hash(H) == -1);
    uint32_t out = 0;
    CHECK_FALSE(node.key_hash_of_id(91, out));
    // CONTROL, same fixture, same timings, first-hand instead of hearsay at t=5000 -> the lease DOES move.
    TestHal h2; Node n2(h2, /*id=*/42, /*key=*/0xD3D3D3D3u);
    NodeConfig c2; c2.routing_sf = 7; c2.leaf_id = 0; c2.allowed_sf_bitmap = (1u << 12); c2.id_bind_ttl_ms = 5000;
    n2.on_init(c2);
    h2._now = 1000;  CHECK(n2.test_id_bind_set(/*id=*/91, H, /*authoritative=*/true));
    h2._now = 5000;  CHECK(n2.test_id_bind_set(/*id=*/91, H, /*authoritative=*/true));
    h2._now = 6000;  CHECK(n2.id_bind_find_by_hash(H) == 91);               // alive: an authoritative re-sighting refreshed it
}

TEST_CASE("§id-hash S2b — the CONFLICT arm is untouched: an authoritative rebind still wins, a claimed one still refuses") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xD4D4D4D4u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    const uint32_t H1 = 0x11112222u, H2 = 0x33334444u;
    CHECK(node.test_id_bind_set(/*id=*/33, H1, /*authoritative=*/false));   // claimed row for 33
    CHECK_FALSE(node.test_id_bind_set(/*id=*/33, H2, /*authoritative=*/false));  // a CLAIMED conflict still refuses
    CHECK(node.id_bind_find_by_hash(H1) == 33);
    CHECK(node.id_bind_find_by_hash(H2) == -1);
    CHECK(node.test_id_bind_set(/*id=*/33, H2, /*authoritative=*/true));    // an AUTHORITATIVE conflict still overwrites
    Node::IdBindConf conf = Node::IdBindConf::claimed;
    CHECK(node.id_bind_find_by_hash(H2, &conf) == 33); CHECK(conf == Node::IdBindConf::authoritative);
    CHECK(node.id_bind_find_by_hash(H1) == -1);
    // ...and the SELF-defence is still the first gate: nothing may rebind our own id away from our own key.
    CHECK_FALSE(node.test_id_bind_set(/*id=*/42, 0xBADBAD00u, /*authoritative=*/true));
    uint32_t own = 0; CHECK(node.key_hash_of_id(42, own)); CHECK(own == 0xD4D4D4D4u);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ §id-hash S1b / S2b-fix — the four QA blockers (assessment 2026-08-01), each with its own arm.
//
// ★ `ble_claims_sent` below is the fw_main predicate VERBATIM (`src/fw_main.cpp`: `cmd.kind == reqpubkey &&
//   r.code == queued && r.aired`). `src/` is compiled by neither native nor the simulator, so mirroring the
//   condition here is the closest a test can get to asserting the BLE-visible disposition — which is what the
//   assessment asked for, and what the old tests missed by checking only `h_tx` absence.
// ════════════════════════════════════════════════════════════════════════════════════════════════════════

namespace {
// The exact disposition `{"ev":"reqpubkey_sent"}` is keyed on. Keep in step with src/fw_main.cpp.
bool ble_claims_sent(const CmdResult& r) { return r.code == CmdCode::queued && r.aired; }
}  // namespace

TEST_CASE("§id-hash S2b-fix (QA P1a) — a CLAIMED rehome cannot EVICT an authoritative holder of the same hash") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xE0E0E0E0u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    const uint32_t H = 0x61CD83EAu;

    // 1. a first-hand beacon binds H to id 10.
    CHECK(node.test_id_bind_set(/*id=*/10, H, /*authoritative=*/true));
    uint32_t out = 0;
    CHECK(node.key_hash_of_id(10, out)); CHECK(out == H);

    // 2. ★ THE ATTACK, and it is the SAME demotion class S2b exists to remove, arriving through the REVERSE
    //    uniqueness rule instead of the matching row: a relayed soft answer claims H under a DIFFERENT id. Before
    //    this fix the new-node_id path called id_bind_evict_other_hash_holders(H, 20) unconditionally, deleting the
    //    authoritative row, and then inserted {20, H, claimed}.
    hal.events.clear();
    CHECK_FALSE(node.test_id_bind_set(/*id=*/20, H, /*authoritative=*/false));   // REFUSED, loudly
    CHECK(find_ev(hal.events, "addr_rehome_refused") != nullptr);

    // 3. the authoritative binding is intact, and the READER the whole rule protects still answers.
    out = 0;
    CHECK(node.key_hash_of_id(10, out)); CHECK(out == H);
    Node::IdBindConf conf = Node::IdBindConf::claimed;
    CHECK(node.id_bind_find_by_hash(H, &conf) == 10);
    CHECK(conf == Node::IdBindConf::authoritative);
    CHECK(node.key_hash_for_id(20) == 0);                       // and id 20 was never inserted
    uint32_t none = 0;
    CHECK_FALSE(node.key_hash_of_id(20, none));

    // 4. ★ POSITIVE CONTROL — the AUTHORITATIVE rehome this eviction exists for still works, on the same fixture.
    //    Without this, "20 is absent" would be equally true of a fix that broke the rejoin self-heal outright.
    CHECK(node.test_id_bind_set(/*id=*/20, H, /*authoritative=*/true));
    CHECK(node.id_bind_find_by_hash(H) == 20);                  // the hash moved...
    CHECK_FALSE(node.key_hash_of_id(10, none));                 // ...and the stale id-10 row was evicted
    out = 0;
    CHECK(node.key_hash_of_id(20, out)); CHECK(out == H);
}

TEST_CASE("§id-hash S2b-fix (QA P1a) — claimed->claimed rehome stays NEWEST-WINS (owner ruling), self is protected") {
    TestHal hal; Node node(hal, /*id=*/42, /*key=*/0xE1E1E1E1u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    hal._now = 1000;
    const uint32_t H = 0x12345678u;

    // Two CLAIMS have no trust ordering between them, so the newer one still wins — preserving current behaviour is
    // what keeps this a fix rather than a redesign (C1). Only the AUTHORITATIVE tier is protected.
    CHECK(node.test_id_bind_set(/*id=*/30, H, /*authoritative=*/false));
    CHECK(node.id_bind_find_by_hash(H) == 30);
    CHECK(node.test_id_bind_set(/*id=*/31, H, /*authoritative=*/false));   // accepted
    CHECK(node.id_bind_find_by_hash(H) == 31);
    CHECK(node.key_hash_for_id(30) == 0);                                   // ...and 30 was evicted, as before

    // ★ OUR OWN self-binding is authoritative and never expires, so a claim on OUR hash is refused by the same rule —
    //   the strongest case for it, since losing our own row corrupts every hash-locate answer we give.
    hal.events.clear();
    CHECK_FALSE(node.test_id_bind_set(/*id=*/33, 0xE1E1E1E1u, /*authoritative=*/false));
    CHECK(find_ev(hal.events, "addr_rehome_refused") != nullptr);
    uint32_t own = 0;
    CHECK(node.key_hash_of_id(42, own)); CHECK(own == 0xE1E1E1E1u);

    // ★ AN EXPIRED authoritative holder must NOT block: it is invisible to every reader already, so blocking on it
    //   would freeze the table until the age-out sweep runs. Same TTL gate as key_hash_of_id (U1).
    TestHal h2; Node n2(h2, /*id=*/42, /*key=*/0xE2E2E2E2u);
    NodeConfig c2; c2.routing_sf = 7; c2.leaf_id = 0; c2.allowed_sf_bitmap = (1u << 12); c2.id_bind_ttl_ms = 5000;
    n2.on_init(c2);
    h2._now = 1000; CHECK(n2.test_id_bind_set(/*id=*/40, H, /*authoritative=*/true));
    h2._now = 1000; CHECK_FALSE(n2.test_id_bind_set(/*id=*/41, H, /*authoritative=*/false));   // fresh -> blocked
    h2._now = 1000 + 5000;                                                                     // ...now expired
    CHECK(n2.test_id_bind_set(/*id=*/41, H, /*authoritative=*/false));                         // -> allowed
    CHECK(n2.id_bind_find_by_hash(H) == 41);
}

TEST_CASE("§id-hash S1b (QA P1c) — NO CRYPTO IDENTITY: err_no_identity, nothing aired, and BLE claims nothing") {
    const Identity self = ab3_identity(130);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.team_id = 0;
    node.on_init(cfg);                                   // ★ deliberately NO set_crypto_identity
    hal._now = 100000;
    CHECK(node.test_id_bind_set(/*id=*/186, 0x61CD83EAu, /*authoritative=*/true));

    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult r = node.on_command(s1_reqpubkey_by_id(186, /*plane=*/0));
    // ★★ THE FALSE SUCCESS THIS REMOVES: the id resolves, so the arm reached emit_hash_query — which bails at
    //    `want_pubkey && !_crypto_ready` and airs NOTHING, while on_command used to answer `queued` and fw_main then
    //    rendered `{"ev":"reqpubkey_sent"}`, i.e. "the request was flooded". Both the source comment and the
    //    companion contract claimed this path "keeps its existing error ack"; there was no such ack.
    CHECK(r.code == CmdCode::err_no_identity);
    CHECK_FALSE(r.aired);
    CHECK_FALSE(ble_claims_sent(r));                     // ★ the BLE-visible disposition, not just telemetry
    CHECK(r.dst_hash == 0x61CD83EAu);                    // it still says WHICH target, and on which plane
    CHECK(r.plane == 2);
    CHECK(find_ev(hal.events, "h_want_pubkey_no_identity") != nullptr);
    CHECK(find_ev(hal.events, "h_tx") == nullptr);
    CHECK(hal.tx_frames.empty());

    // ★ SAME-FIXTURE POSITIVE CONTROL: provision an identity and the identical command flies and claims sent.
    node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult ok = node.on_command(s1_reqpubkey_by_id(186, /*plane=*/0));
    CHECK(ok.code == CmdCode::queued);
    CHECK(ok.aired);
    CHECK(ble_claims_sent(ok));
    CHECK(find_ev(hal.events, "h_tx") != nullptr);
    CHECK_FALSE(hal.tx_frames.empty());
}

TEST_CASE("§id-hash S1b (QA P1c) — a DEGENERATE target (our own hash / 0) refuses instead of claiming a flood") {
    const Identity self = ab3_identity(131);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;

    hal.events.clear(); hal.tx_frames.clear();
    Command own{}; own.kind = CmdKind::reqpubkey;
    own.u.resolve.dst_hash = self.key_hash32; own.u.resolve.hard = true; own.u.resolve.plane = 2;
    const CmdResult r = node.on_command(own);            // `reqpubkey 0x<our own hash>`
    CHECK(r.code == CmdCode::err_unsupported);           // was `queued` + reqpubkey_sent for a frame that never flew
    CHECK_FALSE(r.aired);
    CHECK_FALSE(ble_claims_sent(r));
    CHECK(find_ev(hal.events, "h_tx") == nullptr);
    CHECK(hal.tx_frames.empty());
    // ★ SAME-FIXTURE CONTROL: any OTHER hash on the same node flies.
    hal.events.clear();
    Command other = own; other.u.resolve.dst_hash = self.key_hash32 ^ 1u;
    const CmdResult ok = node.on_command(other);
    CHECK(ok.code == CmdCode::queued); CHECK(ok.aired); CHECK(ble_claims_sent(ok));
    CHECK(find_ev(hal.events, "h_tx") != nullptr);
}

TEST_CASE("§id-hash S1b §3-D9 (QA P2) — the SAME hash in BOTH planes is still TWO planes: ambiguous, and `-t` works") {
    const Identity self = ab3_identity(132);
    TestHal hal; Node node(hal, /*id=*/114, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub); node.set_team_local_id(114);
    hal._now = 100000;

    // ★ ONE identity, id 20 on BOTH planes — the case the resolver used to de-duplicate away. That de-dup was a
    //   DISPLAY choice, and S1 turned the mask into an AIRTIME decision, where it produced two wrong answers.
    const Identity both = ab3_identity(60);
    CHECK(node.test_id_bind_set(/*id=*/20, both.key_hash32, /*authoritative=*/true));
    node.test_learn_route(20, 20, 1, 40, /*team_plane=*/true);
    node.team_key_set(/*id=*/20, both.key_hash32);

    Node::PeerBookRow st{}, tm{};
    const uint8_t mask = node.peer_book_by_id(20, st, tm);
    CHECK(mask == (Node::kPeerBookStatic | Node::kPeerBookTeam));   // ★ BOTH bits — was kPeerBookStatic alone
    CHECK(st.hash == both.key_hash32); CHECK(tm.hash == both.key_hash32);
    CHECK(tm.team_id == 20);

    // (a) bare AUTO is AMBIGUOUS — it used to silently select STATIC.
    hal.events.clear(); hal.tx_frames.clear();
    const CmdResult amb = node.on_command(s1_reqpubkey_by_id(20, /*plane=*/0));
    CHECK(amb.code == CmdCode::err_ambiguous_plane);
    CHECK_FALSE(amb.aired);
    CHECK(find_ev(hal.events, "h_tx") == nullptr);

    // (b) ★ explicit `-t` SENDS — it used to see has_team == false and refuse err_no_binding for a team binding
    //     that plainly exists. Hash equality never made the two planes' routes or return paths equal.
    hal.events.clear();
    const CmdResult t = node.on_command(s1_reqpubkey_by_id(20, /*plane=*/1));
    CHECK(t.code == CmdCode::queued); CHECK(t.aired); CHECK(t.plane == 1);
    CHECK(t.dst_hash == both.key_hash32);
    CHECK(find_ev(hal.events, "h_tx") != nullptr);

    // (c) explicit `-s` resolves the static row (this fixture is an off-grid mobile, so the GLOBAL query has no
    //     return path and honestly refuses — P1c — but the RESOLUTION half is what `-s` selects and it is correct).
    const CmdResult s = node.on_command(s1_reqpubkey_by_id(20, /*plane=*/2));
    CHECK(s.plane == 2); CHECK(s.dst_hash == both.key_hash32);
    CHECK(s.code == CmdCode::err_no_gateway); CHECK_FALSE(s.aired);
}

TEST_CASE("§id-hash S1b (QA P1c) — the HOSTED-MOBILE cache hit is a SUCCESS that must not claim a flood") {
#if MR_FEAT_MOBILE
    const Identity self = ab3_identity(133), guest = ab3_identity(134);
    TestHal hal; Node node(hal, /*id=*/42, self.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg); node.set_crypto_identity(self.x_secret, self.ed_pub);
    hal._now = 100000;
    // Host the guest with its pubkey via the EXISTING white-box seam (U1) — the full CLAIM+probe path is s22's job.
    node.test_add_host_mobile(guest.key_hash32, /*local_id=*/200, guest.ed_pub);

    hal.events.clear(); hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::reqpubkey;
    c.u.resolve.dst_hash = guest.key_hash32; c.u.resolve.hard = true; c.u.resolve.plane = 2;
    const CmdResult r = node.on_command(c);
    // ★ A GENUINE SUCCESS — the key is cached and the app learns it from the peer_key_cached push — that airs
    //   NOTHING. `queued` is right; `reqpubkey_sent` ("the request was flooded") is not, and `aired` is the bit
    //   that lets the transport tell them apart. This is why `aired` is not redundant with `code == queued`.
    CHECK(r.code == CmdCode::queued);
    CHECK_FALSE(r.aired);
    CHECK_FALSE(ble_claims_sent(r));
    CHECK(find_ev(hal.events, "peer_key_cached") != nullptr);   // the honest report the app actually gets
    CHECK(find_ev(hal.events, "h_tx") == nullptr);
    CHECK(hal.tx_frames.empty());
    uint8_t ed[32];
    CHECK(node.peer_key_find(guest.key_hash32, ed));            // ...and the key really is available now
#endif
}
