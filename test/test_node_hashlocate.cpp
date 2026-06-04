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

}  // namespace

TEST_CASE("A0 id_bind — a heard beacon binds the sender's key_hash32 -> node_id") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);   // unprovisioned: no self-binding to confuse counts
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    CHECK(node.id_bind_find_by_hash(0x0000BEEF) == 7);   // own hash resolves to self (we can answer for ourselves)
}

TEST_CASE("A0 id_bind — a binding past its TTL is no longer resolved") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.id_bind_ttl_ms = 5000;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.cap_id_bind = 2;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.lbt_enabled = false;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.lbt_enabled = false;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.lbt_enabled = false;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.lbt_enabled = false;
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

TEST_CASE("B codec — hash-bind inner round-trips (H_ANSWER + AUTHORITATIVE + binding survive)") {
    std::array<uint8_t, 7> buf{};
    hash_bind_inner in{}; in.target_layer = 2; in.node_id = 7; in.key_hash32 = 0xDEADBEEF; in.authoritative = true;
    const size_t n = pack_hash_bind_inner(in, std::span<uint8_t>(buf.data(), buf.size()));
    CHECK(n == 7);
    CHECK((buf[0] & PAYLOAD_FLAG_H_ANSWER) != 0);        // typed by the payload-flag, no magic
    CHECK((buf[0] & PAYLOAD_FLAG_AUTHORITATIVE) != 0);
    CHECK((buf[0] & PAYLOAD_FLAG_CRYPTED) == 0);         // cleartext so relays can cache-on-pass
    auto out = parse_hash_bind_inner(std::span<const uint8_t>(buf.data(), n));
    CHECK(out.has_value());
    if (out) {
        CHECK(out->target_layer == 2);
        CHECK(out->node_id == 7);
        CHECK(out->key_hash32 == 0xDEADBEEF);
        CHECK(out->authoritative == true);
    }
    // a cached (soft) answer: AUTHORITATIVE clear
    in.authoritative = false; pack_hash_bind_inner(in, std::span<uint8_t>(buf.data(), buf.size()));
    auto soft = parse_hash_bind_inner(std::span<const uint8_t>(buf.data(), 7));
    CHECK((soft.has_value() && soft->authoritative == false));
    // a NORMAL DM inner ([payload-flags=0][origin][body]) is NOT a hash-bind answer + still round-trips as a unicast.
    const uint8_t dm[] = { 0x00, /*origin=*/3, 'h', 'i' };
    CHECK(parse_hash_bind_inner(std::span<const uint8_t>(dm, sizeof(dm))) == std::nullopt);
    auto uni = parse_unicast_inner(std::span<const uint8_t>(dm, sizeof(dm)));
    CHECK(uni.has_value());
    if (uni) { CHECK(uni->origin == 3); CHECK(uni->body.size() == 2); }
}

TEST_CASE("B send — the resolver enqueues a hash-bind response addressed to the H-query origin") {
    TestHal hal;
    Node node(hal, /*node_id=*/2, /*key_hash32=*/0x0000BBBB);   // the owner of BBBB
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.lbt_enabled = false;
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
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    node.on_init(cfg);
    hal.events.clear();

    // Craft the hash-bind answer inner (BBBB -> node 2, authoritative) and feed the deliver seam.
    std::array<uint8_t, 7> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 2; hb.key_hash32 = 0x0000BBBB; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    node.on_hash_bind_response(inner.data(), static_cast<uint8_t>(in));

    const Ev* rx = find_ev(hal.events, "hash_bind_rx");
    CHECK(rx != nullptr);
    if (rx) {
        CHECK(rx->node == 2);
        CHECK(rx->key_hash32 == 0x0000BBBB);
        CHECK((rx->has_auth && rx->authoritative));
    }
}
