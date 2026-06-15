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

    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { return lo; }
    void     rand_bytes(uint8_t* o, size_t n) override { for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(rand_range(0, 256)); }
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
static size_t make_h(uint8_t origin, uint32_t key_hash32, uint8_t ttl, std::array<uint8_t, 16>& buf, bool hard = false) {
    h_in in{}; in.leaf_id = 0; in.origin = origin; in.key_hash32 = key_hash32; in.ttl = ttl; in.hard = hard;
    return pack_h(in, std::span<uint8_t>(buf.data(), buf.size()));
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
    CHECK(count_h_tx(hal.tx_frames) == 1);

    std::array<uint8_t, 16> qh{}; const size_t nh = make_h(/*origin=*/9, /*hash=*/0x0000FACE, /*ttl=*/4, qh, /*hard=*/true);
    node.on_recv(qh.data(), nh, meta);                   // HARD: a DIFFERENT variant -> NOT suppressed -> forwards
    CHECK(count_h_tx(hal.tx_frames) == 2);

    node.on_recv(qh.data(), nh, meta);                   // a repeat HARD IS suppressed by its own seen-entry
    CHECK(count_h_tx(hal.tx_frames) == 2);
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
