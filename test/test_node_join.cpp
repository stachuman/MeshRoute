// MeshRoute — test_node_join.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// node_id auto-assignment (DAD + self-heal), node_join.cpp. Covers the §6 tiebreak (the crux), candidate
// selection + denied-list aging (§3/§13), the claim->guard->adopt path (§4), the guard-window objection,
// and the heal: handle_j DENY -> forced_rejoin (the loser yields) + a beacon collision -> OWN_ID_DEFENSE.
// Driven through on_command / on_recv / on_timer with an in-memory Hal (rand_range returns lo -> the
// candidate is the lowest free id, deterministic).
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

constexpr uint32_t kJoinClaimGuardTimerId = 58;   // mirrors Node's private DAD guard timer id
constexpr uint32_t kJoinListenTimerId     = 60;   // claim-after-listen window (L1)
constexpr uint32_t kMobileDiscoverTimerId   = 74; // mirrors Node's private mobile-registration DISCOVER kick (node.h:479)
constexpr uint32_t kMobileClaimGuardTimerId = 75; // mirrors Node's private collect-OFFERs window close (node.h:480)
constexpr uint32_t kBeaconTimerId         = 1;    // periodic beacon tick (drives maybe_exit_discovery)

struct Ev { std::string type; int64_t node = -1; int64_t proposed = -1; int64_t denied = -1;
            int64_t claim_epoch = -1; int64_t prior = -1; int64_t their_epoch = -1;
            bool i_win = false; bool has_iwin = false; std::string reason; };

class TestHal : public Hal {
public:
    uint64_t _now = 0;
    int      _rand_lo_bias = 0;                       // rand_range returns lo + bias (0 => lowest free id)
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;

    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
    void     set_rx_sf(int) override {}
    uint64_t channel_busy_until() override { return 0; }
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    uint64_t oldest_tx_end_ms() override { return 0; }
    uint64_t now() override { return _now; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    int      rand_range(int lo, int) override { return lo + _rand_lo_bias; }
    void     rand_bytes(uint8_t* o, size_t n) override { for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(rand_range(0, 256)); }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            const EventField& fl = f[i];
            if (fl.type == EventField::T::i64) {
                if      (!std::strcmp(fl.key, "node"))             e.node = fl.i;
                else if (!std::strcmp(fl.key, "proposed_node_id")) e.proposed = fl.i;
                else if (!std::strcmp(fl.key, "denied_node_id"))   e.denied = fl.i;
                else if (!std::strcmp(fl.key, "claim_epoch"))      e.claim_epoch = fl.i;
                else if (!std::strcmp(fl.key, "prior_node_id"))    e.prior = fl.i;
                else if (!std::strcmp(fl.key, "their_claim_epoch")) e.their_epoch = fl.i;
            } else if (fl.type == EventField::T::boolean) {
                if (!std::strcmp(fl.key, "i_win")) { e.i_win = fl.b; e.has_iwin = true; }
            } else if (fl.type == EventField::T::str) {
                if (!std::strcmp(fl.key, "reason")) e.reason = fl.s ? fl.s : "";
            }
        }
        events.push_back(e);
    }
    void log(const char*) override {}
    const Ev* find(const char* t) const { for (const auto& e : events) if (e.type == t) return &e; return nullptr; }
    int count(const char* t) const { int c = 0; for (const auto& e : events) if (e.type == t) ++c; return c; }
};

NodeConfig join_cfg() { NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.lbt_enabled = false; return c; }

// An identity beacon from `src` carrying `key_hash32` (0 route entries).
size_t make_beacon(uint8_t src, uint32_t key_hash32, std::array<uint8_t, 64>& buf) {
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = key_hash32;
    in.entries = std::span<const beacon_entry>();
    return pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size()));
}
size_t make_j_claim(uint32_t key_hash32, uint8_t proposed, uint8_t epoch, std::array<uint8_t, 16>& buf) {
    j_claim_in in{}; in.leaf_id = 0; in.key_hash32 = key_hash32; in.proposed_node_id = proposed;
    in.lease_age_seconds = 0; in.claim_epoch = epoch; in.nonce = 0;
    return pack_j_claim(in, std::span<uint8_t>(buf.data(), buf.size()));
}
size_t make_j_deny(uint8_t denied, uint32_t owner_key, uint32_t claimant_key, uint8_t owner_epoch,
                   uint8_t reason, std::array<uint8_t, 16>& buf) {
    j_deny_in in{}; in.leaf_id = 0; in.denied_node_id = denied; in.owner_key_hash32 = owner_key;
    in.claimant_key_hash32 = claimant_key; in.owner_lease_age_seconds = 0; in.owner_claim_epoch = owner_epoch;
    in.reason = reason;
    return pack_j_deny(in, std::span<uint8_t>(buf.data(), buf.size()));
}
int count_j_deny(const std::vector<std::vector<uint8_t>>& frames) {
    int c = 0;
    for (const auto& f : frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(j_opcode::deny)) ++c; }
    return c;
}
// §clean-join: a MOBILE DISCOVER (host-side registration probe).
size_t make_j_discover_mobile(uint32_t key_hash32, std::array<uint8_t, 16>& buf) {
    j_discover_in in{}; in.leaf_id = 0; in.gateway_capable = false; in.is_mobile = true; in.key_hash32 = key_hash32;
    return pack_j_discover(in, std::span<uint8_t>(buf.data(), buf.size()));
}
// §clean-join: a MOBILE CLAIM addressed at `host` (records the mobile in the host's _mobile_reg — the append-only path).
size_t make_j_claim_mobile(uint8_t host_id, uint8_t local_id, uint32_t key_hash32, std::array<uint8_t, 16>& buf) {
    j_claim_in in{}; in.leaf_id = 0; in.gateway_capable = false; in.is_mobile = true; in.key_hash32 = key_hash32;
    in.proposed_node_id = local_id; in.lease_age_seconds = 0; in.claim_epoch = 1; in.nonce = 0; in.chosen_host_id = host_id;
    return pack_j_claim(in, std::span<uint8_t>(buf.data(), buf.size()));
}
// §clean-join: a MOBILE OFFER addressed at `target` (the mobile-side collector adopts it on the claim-guard fire).
size_t make_j_offer_mobile(uint8_t responder_id, uint32_t responder_hash, uint8_t local_id, uint32_t target_hash,
                           std::array<uint8_t, 16>& buf) {
    j_offer_in in{}; in.leaf_id = 0; in.gateway_capable = false; in.is_mobile = true;
    in.responder_node_id = responder_id; in.responder_key_hash32 = responder_hash; in.data_sf_bitmap = (1u << 7);
    in.proposed_mobile_id = local_id; in.target_key_hash32 = target_hash;
    return pack_j_offer(in, std::span<uint8_t>(buf.data(), buf.size()));
}

}  // namespace

TEST_CASE("join §6 tiebreak — KEY-ONLY: lower key_hash32 wins; claim_epoch ignored (vestigial)") {
    // lower key wins, higher key yields
    CHECK(Node::join_tiebreak_wins(0, 0x00001111, 0, 0x00002222) == true);
    CHECK(Node::join_tiebreak_wins(0, 0x00002222, 0, 0x00001111) == false);
    // epoch is IGNORED — a higher epoch does NOT rescue a higher key (and vice-versa). This consistency
    // is what lets a third-party mediator (L2a, no epoch) agree with the direct heal.
    CHECK(Node::join_tiebreak_wins(9, 0x00002222, 1, 0x00001111) == false);   // higher epoch, higher key -> still lose
    CHECK(Node::join_tiebreak_wins(1, 0x00001111, 9, 0x00002222) == true);    // lower epoch, lower key -> still win
    // strict total order (never both-win for distinct keys)
    CHECK(Node::join_tiebreak_wins(0, 0x00001111, 0, 0x00002222)
          != Node::join_tiebreak_wins(0, 0x00002222, 0, 0x00001111));
}

TEST_CASE("join — an unprovisioned node claims the lowest free id and ADOPTS when unopposed") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A1A1);     // unprovisioned (no self-seed at init)
    node.on_init(join_cfg());
    CHECK(node.joined() == false);
    hal.events.clear(); hal.tx_frames.clear();

    Command c{}; c.kind = CmdKind::join;
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId);                          // claim-after-listen: the listen window fires the claim

    const Ev* sent = hal.find("join_claim_sent");
    CHECK(sent != nullptr);
    // R6.3/G1: the lowest free id is now 17 (1..16 reserved for gateways), NOT 1. claim_epoch vestigial (not bumped).
    if (sent) { CHECK(sent->proposed == protocol::normal_node_id_min); CHECK(sent->claim_epoch == 0); }
    // a J_CLAIM went on air
    bool claim_tx = false;
    for (const auto& f : hal.tx_frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(j_opcode::claim) && p->proposed_node_id == protocol::normal_node_id_min) claim_tx = true; }
    CHECK(claim_tx);
    CHECK(node.joined() == false);                               // not yet — still in the guard window

    node.on_timer(kJoinClaimGuardTimerId);                       // guard elapses, no objection -> adopt
    CHECK(node.joined() == true);
    CHECK(node.node_id() == protocol::normal_node_id_min);        // R6.3/G1: adopts 17 (lowest free normal id)
    CHECK(hal.find("join_adopted") != nullptr);
}

TEST_CASE("join — an objection during the guard window denies the id (no adopt) + retries") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A1A1);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c); node.on_timer(kJoinListenTimerId);  // claims id 17
    hal.events.clear();

    // a different node beacons as id 17 (a conflicting binding appears mid-guard)
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/protocol::normal_node_id_min, /*hash=*/0x0000B2B2, bcn);
    node.on_recv(bcn.data(), bn, meta);

    node.on_timer(kJoinClaimGuardTimerId);                       // guard sees the conflict -> deny, not adopt
    CHECK(node.joined() == false);
    const Ev* denied = hal.find("join_claim_denied");
    CHECK(denied != nullptr);
    if (denied) CHECK(denied->denied == protocol::normal_node_id_min);
}

TEST_CASE("join handle_j CLAIM — a claim for OUR adopted id is denied (OWN_ID_DEFENSE on air)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A1A1);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c); node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);  // adopt id 17
    CHECK(node.joined());
    hal.events.clear(); hal.tx_frames.clear();

    // an impostor claims our id 17 with a different hash
    std::array<uint8_t, 16> j{}; const size_t jn = make_j_claim(/*hash=*/0x0000B2B2, /*proposed=*/protocol::normal_node_id_min, /*epoch=*/1, j);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    node.on_recv(j.data(), jn, meta);

    CHECK(hal.find("join_deny_sent") != nullptr);
    CHECK(count_j_deny(hal.tx_frames) == 1);                     // a J_DENY defending id 17 went out
    CHECK(node.joined());                                        // we keep our id (the impostor must yield)
}

TEST_CASE("join handle_j DENY — losing the tiebreak forces a rejoin (we yield + re-claim)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000F0F0);    // a HIGH key -> loses the tie on equal epoch
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c); node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);  // adopt id 17 (epoch 1)
    CHECK(node.joined());
    CHECK(node.node_id() == protocol::normal_node_id_min);
    hal.events.clear();

    // a competing owner DENies us id 17 with a HIGHER epoch -> we lose -> forced_rejoin
    std::array<uint8_t, 16> d{};
    const size_t dn = make_j_deny(/*denied=*/protocol::normal_node_id_min, /*owner_key=*/0x00001111, /*claimant_key=*/0x0000F0F0,
                                  /*owner_epoch=*/5, J_DENY_OWN_ID_DEFENSE, d);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    node.on_recv(d.data(), dn, meta);

    const Ev* tb = hal.find("addr_conflict_tie_break");
    CHECK(tb != nullptr);
    if (tb) CHECK((tb->has_iwin && tb->i_win == false));        // we lost (their epoch 5 > our 1)
    const Ev* fr = hal.find("addr_conflict_forced_rejoin");
    CHECK(fr != nullptr);
    if (fr) CHECK(fr->prior == protocol::normal_node_id_min);                              // yielded id 17
    // and we re-claimed a DIFFERENT id (id 17 is now denied)
    const Ev* re = hal.find("join_claim_sent");
    CHECK(re != nullptr);
    if (re) CHECK(re->proposed != protocol::normal_node_id_min);
}

TEST_CASE("join handle_j DENY — winning the tiebreak keeps our id (no rejoin)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x00000001);    // a LOW key -> wins the tie on equal epoch
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c); node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);  // adopt id 17 (epoch 1)
    CHECK(node.joined());
    hal.events.clear();

    std::array<uint8_t, 16> d{};
    const size_t dn = make_j_deny(/*denied=*/protocol::normal_node_id_min, /*owner_key=*/0x0000FFFF, /*claimant_key=*/0x00000001,
                                  /*owner_epoch=*/1, J_DENY_OWN_ID_DEFENSE, d);   // equal epoch, our key lower
    RxMeta meta{8.0f, -80.0f, 0, -1};
    node.on_recv(d.data(), dn, meta);

    const Ev* tb = hal.find("addr_conflict_tie_break");
    CHECK((tb && tb->has_iwin && tb->i_win == true));
    CHECK(hal.find("addr_conflict_forced_rejoin") == nullptr);  // we kept id 17
    CHECK(node.joined());
    CHECK(node.node_id() == protocol::normal_node_id_min);
}

TEST_CASE("join — a beacon carrying our id with a DIFFERENT hash triggers the OWN_ID_DEFENSE (heal detector)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A1A1);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c); node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);  // adopt id 17
    CHECK(node.joined());
    hal.events.clear(); hal.tx_frames.clear();

    // an impostor beacons AS id 17 with a different hash — the old self-echo guard would have swallowed this
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/protocol::normal_node_id_min, /*hash=*/0x0000B2B2, bcn);
    node.on_recv(bcn.data(), bn, meta);

    CHECK(hal.find("join_deny_sent") != nullptr);               // we defended our id
    CHECK(count_j_deny(hal.tx_frames) == 1);

    // a TRUE self-echo (our own id + our own hash) is still dropped silently (no defense)
    hal.events.clear(); hal.tx_frames.clear();
    std::array<uint8_t, 64> echo{}; const size_t en = make_beacon(/*src=*/protocol::normal_node_id_min, /*hash=*/0x0000A1A1, echo);
    node.on_recv(echo.data(), en, meta);
    CHECK(hal.find("join_deny_sent") == nullptr);
}

TEST_CASE("join L2a — a shared neighbour mediates a collision: J_DENY(MEDIATED) to the key-loser") {
    TestHal hal;
    Node c(hal, /*node_id=*/5, /*key_hash32=*/0x0000C0C0);       // C: a bystander that hears BOTH colliding nodes
    c.on_init(join_cfg());
    hal.events.clear(); hal.tx_frames.clear();
    RxMeta meta{8.0f, -80.0f, 0, -1};

    std::array<uint8_t, 64> ba{}; const size_t na = make_beacon(/*id=*/7, /*key=*/0x00001111, ba);  // node A — lower key = winner
    c.on_recv(ba.data(), na, meta);
    std::array<uint8_t, 64> bb{}; const size_t nb = make_beacon(/*id=*/7, /*key=*/0x00002222, bb);  // node B — higher key = loser
    c.on_recv(bb.data(), nb, meta);

    CHECK(hal.find("addr_conflict_mediated") != nullptr);        // C detected the same-id/different-hash conflict
    bool denied_loser = false;
    for (const auto& f : hal.tx_frames) {
        auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(j_opcode::deny) && p->denied_node_id == 7
            && p->owner_key_hash32 == 0x00001111 && p->claimant_key_hash32 == 0x00002222
            && p->reason == J_DENY_MEDIATED) denied_loser = true;   // owner=winner, claimant=loser, key-only
    }
    CHECK(denied_loser);

    // #1 suppression: the loser/winner keep beaconing their id until the loser renumbers — those repeat
    // beacons re-create the conflict, but within the window they must NOT re-fire a DENY (one per collision).
    const int denies_after_first = count_j_deny(hal.tx_frames);
    CHECK(denies_after_first == 1);
    c.on_recv(ba.data(), na, meta);                              // A again (re-flap)
    c.on_recv(bb.data(), nb, meta);                              // B again
    c.on_recv(ba.data(), na, meta);                              // A again
    CHECK(count_j_deny(hal.tx_frames) == denies_after_first);    // still exactly one DENY (suppressed)
}

// R6.3/G1: the DAD picker must never hand out a gateway id (1..16) — normal nodes pick 17..254. Seed-varied across
// the free-list index so both the low end (17) and high end (254) of the 238-slot pool are covered. A prev id in
// 1..16 is covered transitively: it fails the `prev >= normal_node_id_min` guard -> falls to this same free-scan.
TEST_CASE("join — R6.3/G1: the DAD picker never returns a gateway id (1..16); always 17..254 (seed-varied)") {
    for (int bias : {0, 1, 5, 50, 200, 237}) {          // free_list index (238 slots: 17..254)
        TestHal hal; hal._rand_lo_bias = bias;
        Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B2B2);
        node.on_init(join_cfg());
        Command c{}; c.kind = CmdKind::join;
        CHECK(node.on_command(c).code == CmdCode::queued);
        node.on_timer(kJoinListenTimerId);
        const Ev* sent = hal.find("join_claim_sent");
        CHECK(sent != nullptr);
        if (sent) {
            CHECK(sent->proposed >= protocol::normal_node_id_min);                                   // >= 17
            CHECK(sent->proposed <= 254);
            const bool in_gateway_range = (sent->proposed >= 1 && sent->proposed <= protocol::gateway_node_id_max);
            CHECK_FALSE(in_gateway_range);                                                            // never 1..16
        }
    }
}

// R6.3 `join`/`leave` verbs (live core seam): a PROVISIONED node drops its id live (set_identity 0) + re-DADs a fresh
// NORMAL id — no reboot. (The verb also re-tunes the radio + resets membership; those are device-side / board-build.)
TEST_CASE("join — R6.3 live re-provision: a provisioned node drops its id + re-DADs (17..254), no reboot") {
    TestHal hal;
    Node node(hal, /*node_id=*/50, /*key_hash32=*/0x0000A1A1);       // already provisioned id 50
    node.on_init(join_cfg());
    node.set_identity(0, 0x0000A1A1);                                // the verb's live unprovision
    CHECK(node.node_id() == 0);
    hal.events.clear(); hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::join;
    CHECK(node.on_command(c).code == CmdCode::queued);               // re-DAD live (no reboot)
    node.on_timer(kJoinListenTimerId);
    const Ev* sent = hal.find("join_claim_sent");
    CHECK(sent != nullptr);
    if (sent) CHECK(sent->proposed >= protocol::normal_node_id_min); // re-DADs a normal id (17..254)
}

// Reprovision re-DAD fix: a JOINED node re-DADs on join/create (the verbs). The bug was set_identity(0) leaving
// _joined set -> CmdKind::join idempotent-no-op -> node_id stuck. reset_join_for_reprovision() clears _joined +
// denies the prior id, so the re-DAD runs and picks a FRESH id (!= prior). A fresh node still DADs (existing test).
TEST_CASE("join — reprovision: a joined node re-DADs after reset_join_for_reprovision (new id != prior)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000C3C3);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join;
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId);
    node.on_timer(kJoinClaimGuardTimerId);                       // adopt unopposed -> joined
    CHECK(node.joined());
    const int prior = node.node_id();
    CHECK(prior >= protocol::normal_node_id_min);

    // BUG repro: a bare re-join on a joined node no-ops (idempotent) -> no new claim.
    hal.events.clear();
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId);
    CHECK(hal.find("join_claim_sent") == nullptr);              // idempotent -> nothing happened (the bug)

    // FIX: reset the join FSM, THEN re-DAD.
    hal.events.clear(); hal.tx_frames.clear();
    node.reset_join_for_reprovision();
    CHECK_FALSE(node.joined());
    CHECK(node.node_id() == 0);                                 // unprovisioned
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId);                          // listen window -> claim fires
    const Ev* sent = hal.find("join_claim_sent");
    CHECK(sent != nullptr);
    if (sent) {
        CHECK(sent->proposed >= protocol::normal_node_id_min);  // fresh normal id (17..254)
        CHECK(sent->proposed != prior);                        // deny worked -> NOT the same id
    }
    bool claim_tx = false;                                      // a J_CLAIM went on air
    for (const auto& f : hal.tx_frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(j_opcode::claim)) claim_tx = true; }
    CHECK(claim_tx);
}

// join_adopted push: fires exactly once per adopt — the verb/boot DAD AND the heal re-adopt (the id-change
// staleness fix). reset_join_for_reprovision() is the clean heal seam (drops _joined + denies the prior id).
TEST_CASE("join_adopted push — one per adopt, carrying the (re)adopted id (verb DAD + heal re-adopt)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000E5E5);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join;
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId);
    node.on_timer(kJoinClaimGuardTimerId);                       // adopt unopposed
    CHECK(node.joined());
    const uint8_t first = node.node_id();

    Push p{}; int adopts = 0; uint8_t pushed_id = 0;
    while (node.next_push(p)) if (p.kind == PushKind::join_adopted) { ++adopts; pushed_id = p.dst; }
    CHECK(adopts == 1);
    CHECK(pushed_id == first);

    // heal / reprovision: a fresh DAD picks a NEW id -> a SECOND join_adopted with the re-adopted id
    node.reset_join_for_reprovision();
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId);
    node.on_timer(kJoinClaimGuardTimerId);
    CHECK(node.joined());
    const uint8_t second = node.node_id();
    CHECK(second != first);                                      // the deny worked -> a different id

    adopts = 0; pushed_id = 0;
    while (node.next_push(p)) if (p.kind == PushKind::join_adopted) { ++adopts; pushed_id = p.dst; }
    CHECK(adopts == 1);
    CHECK(pushed_id == second);
}

// leave path: reset_join_for_reprovision() alone (no re-DAD) -> unprovisioned + idle (no claim).
TEST_CASE("join — reprovision: reset alone (leave) leaves the node unprovisioned, no claim") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000D4D4);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);
    CHECK(node.joined());
    hal.events.clear(); hal.tx_frames.clear();
    node.reset_join_for_reprovision();                         // `leave` = reset, NO join command
    CHECK_FALSE(node.joined());
    CHECK(node.node_id() == 0);
    CHECK(hal.find("join_claim_sent") == nullptr);            // idle — no claim until a `join`
}

// Reprovision routing-plane reset: a verb (join/create) on a running node wipes the stale routes + restarts discovery
// at id-adopt (rebuild under the new id). The HEAL (forced_rejoin's shared reset) does NEITHER — same network, routes
// stay valid, no rediscover. The _pending_rediscover flag (set only by the verb path) is the gate.
TEST_CASE("join — reprovision wipes routes + restarts discovery at id-adopt; the heal does not") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000E5E5);
    node.on_init(join_cfg());
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 64> bcn{}; size_t bn = make_beacon(/*src=*/200, /*key=*/0x00009999, bcn);
    node.on_recv(bcn.data(), bn, meta);                          // learn a neighbour route
    CHECK(node.rt_count() >= 1);
    hal._now = 70000; node.on_timer(kBeaconTimerId);            // > discovery_ms -> maybe_exit_discovery
    CHECK_FALSE(node.in_discovery());

    // (1) VERB reprovision: reset + clear routes + mark rediscover, then re-DAD.
    node.reset_join_for_reprovision();
    node.clear_routing_state();
    CHECK(node.rt_count() == 0);                                // routes wiped
    node.set_rediscover_pending(true);
    Command c{}; c.kind = CmdKind::join;
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);  // adopt
    CHECK(node.joined());
    CHECK(node.in_discovery());                                 // RESTARTED under the new id

    // (2) HEAL-style reset (no rediscover flag): re-DAD -> adopt must NOT restart discovery.
    hal._now = 200000; node.on_timer(kBeaconTimerId);          // exit the restarted discovery again
    CHECK_FALSE(node.in_discovery());
    node.reset_join_for_reprovision();                         // the shared reset the heal uses — leaves the flag false
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);
    CHECK(node.joined());
    CHECK_FALSE(node.in_discovery());                          // heal kept routes -> no rediscover
}

// ============ §clean-join reset (spec 2026-07-16) — the `join` clean-slate wipe ============

// Change 1 (+ R3): clear_routing_state() wipes the hosted-mobile registry (append-only bug) AND the team-plane
// liveness mirror (2c), alongside the routes/id-binds it already clears. The old-network state must be void.
TEST_CASE("clean-join — clear_routing_state wipes the hosted-mobile registry + team liveness (Change 1 + R3)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A1A1);
    node.on_init(join_cfg());                                  // static host (host_mobiles defaults true), _node_id==0
    Command c{}; c.kind = CmdKind::join; node.on_command(c);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt id 17 (so a CLAIM can address us)
    CHECK(node.node_id() == protocol::normal_node_id_min);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // register a mobile: a mobile CLAIM addressed at us (chosen_host_id == our id) appends to _mobile_reg (node_join.cpp:235)
    std::array<uint8_t, 16> jc{};
    const size_t jn = make_j_claim_mobile(/*host=*/protocol::normal_node_id_min, /*local=*/200, /*hash=*/0x00C0FFEEu, jc);
    node.on_recv(jc.data(), jn, meta);
    CHECK(node.mobile_reg_count() == 1);

    // learn a route + accrue a TEAM-plane liveness penalty on a next-hop
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/210, /*key=*/0x00009999u, bcn);
    node.on_recv(bcn.data(), bn, meta);
    CHECK(node.rt_count() >= 1);
#if MR_FEAT_TEAM
    node.record_peer_rts_timeout(/*id=*/50, /*ctr_lo=*/1, /*team_plane=*/true);
    CHECK(node.test_team_penalty_q4(50) > 0);                  // suspect tier accrued
#endif

    node.clear_routing_state();
    CHECK(node.mobile_reg_count() == 0);                       // ★ Change 1: the hosted-mobile registry is gone
    CHECK(node.rt_count() == 0);                               // routes wiped (existing behavior, re-asserted)
#if MR_FEAT_TEAM
    CHECK(node.test_team_penalty_q4(50) == 0);                 // ★ R3: the team-liveness mirror is gone
#endif
    CHECK_FALSE(node.mobile_registered());                     // a static host is never a registered mobile
}

// R2: a REGISTERED node's clear emits exactly ONE mobile_reg deregistration push (registered:false);
// an unregistered node's clear emits NONE (active-guarded — no spurious push).
TEST_CASE("clean-join R2 — a registered node's clear pushes one mobile_reg deregister; unregistered pushes none") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // (a) a MOBILE that has registered to a home
    {
        TestHal hal;
        Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B7B7u);
        NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true;
        node.on_init(mcfg);
        node.on_timer(kMobileDiscoverTimerId);                // DISCOVER kick -> arms the offer-collect window
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(/*responder=*/30, /*resp_hash=*/0x0000C0C0u, /*local=*/201, /*target=*/0x0000B7B7u, off);
        node.on_recv(off.data(), on, meta);                   // collect the OFFER
        node.on_timer(kMobileClaimGuardTimerId);              // window close -> CLAIM + adopt
        CHECK(node.mobile_registered());
        Push p{}; while (node.next_push(p)) {}                // drain the registration push(es)

        node.clear_routing_state();
        int dereg = 0, reg = 0;
        while (node.next_push(p)) if (p.kind == PushKind::mobile_reg) { if (!p.relayed) ++dereg; else ++reg; }
        CHECK(dereg == 1);                                    // ★ R2: exactly one registered:false push
        CHECK(reg == 0);
        CHECK_FALSE(node.mobile_registered());
    }
    // (b) a static node that never registered -> no mobile_reg push
    {
        TestHal hal;
        Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B8B8u);
        node.on_init(join_cfg());
        Push p{}; while (node.next_push(p)) {}
        node.clear_routing_state();
        int mreg = 0; while (node.next_push(p)) if (p.kind == PushKind::mobile_reg) ++mreg;
        CHECK(mreg == 0);                                     // ★ R2: no spurious push when never registered
    }
}

// Change 3 (R1-revised predicate): the host suspends OFFERs while _node_id==0 (unprovisioned / mid-DAD), but a
// pinned host (_node_id!=0, _joined==false forever) MUST keep hosting — the gate is on _node_id, NOT !_joined.
TEST_CASE("clean-join Change 3 — no OFFER while _node_id==0; a pinned id keeps hosting (R1)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> disc{};
    const size_t dn = make_j_discover_mobile(/*hash=*/0x0000D1D1u, disc);

    // (1) unprovisioned host (_node_id==0) -> NO offer
    {
        TestHal hal; Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A2A2u);
        node.on_init(join_cfg());
        CHECK(node.node_id() == 0);
        node.on_recv(disc.data(), dn, meta);
        CHECK(hal.count("mobile_offer_tx") == 0);            // ★ suspended: unprovisioned
    }
    // (2) mid-DAD after a reprovision (adopt, then reset -> _node_id==0) -> NO offer
    {
        TestHal hal; Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A3A3u);
        node.on_init(join_cfg());
        Command c{}; c.kind = CmdKind::join; node.on_command(c);
        node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);
        CHECK(node.joined());
        node.reset_join_for_reprovision();                   // set_identity(0) + _joined=false -> mid-DAD window
        CHECK(node.node_id() == 0);
        hal.events.clear();
        node.on_recv(disc.data(), dn, meta);
        CHECK(hal.count("mobile_offer_tx") == 0);            // ★ suspended: mid-DAD
    }
    // (3) an operator-PINNED host (`cfg set node_id`): _node_id!=0, _joined==false FOREVER -> MUST keep hosting
    {
        TestHal hal; Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A4A4u);
        node.on_init(join_cfg());
        node.set_identity(/*node_id=*/50, /*key_hash32=*/0x0000A4A4u);   // pinned: id set, joined stays false
        CHECK_FALSE(node.joined());
        CHECK(node.node_id() == 50);
        hal.events.clear();
        node.on_recv(disc.data(), dn, meta);
        CHECK(hal.count("mobile_offer_tx") == 1);            // ★ R1: !_joined would have wrongly refused this host
    }
}

// R4: the channel plane is old-network state — clear_routing_state (the reprovision verbs) must wipe the buffered
// channel messages so they can't flood into the NEW network (moved from clear_learned_state; prep-restart unchanged).
TEST_CASE("clean-join R4 — clear_routing_state wipes the buffered channel messages") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000E1E1u);
    NodeConfig ccfg = join_cfg(); ccfg.allowed_sf_bitmap = (1u << 7);   // send_channel refuses on an empty sf_list (err_no_data_sf, node.cpp:910) — the fail-loud data-SF rule
    node.on_init(ccfg);
    Command jc{}; jc.kind = CmdKind::join; node.on_command(jc);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt id 17 (send_channel needs provisioning)
    CHECK(node.joined());

    Command c{}; c.kind = CmdKind::send_channel; c.u.channel.channel_id = 7;
    const char* text = "old-network-msg"; c.body = reinterpret_cast<const uint8_t*>(text);
    c.body_len = static_cast<uint8_t>(std::strlen(text));
    node.on_command(c);
    CHECK(node.channel_buffer_count() >= 1);

    node.clear_routing_state();
    CHECK(node.channel_buffer_count() == 0);                 // ★ R4: no old-network channel content survives the reprovision
}

// ============================================================================
// §S0 — the hosted-mobile local-id ALIAS bug (spec 2026-07-17 §6; metal-exposed at static ids 17..20).
// Three parts: (1) find_free_mobile_id allocates TOP-DOWN + excludes known statics; (1b) an authoritative
// static binding EVICTS an aliasing hosted mobile; (2) the transit filter carves out a confirmed static
// next-hop; (3) the drain->re-defer oscillation gives up (send_failed{no_route}) at the bound.
// ============================================================================

// PART 1a — cold-boot / empty-knowledge allocation starts at the TOP of the range (254), never 17 (the metal
// bug picked 18 == static S2). And it EXCLUDES ids the node knows are static (id_bind + routes).
TEST_CASE("§S0 find_free_mobile_id — TOP-DOWN allocation (254), excludes known-static ids") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x00005151u);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt id 17
    CHECK(node.node_id() == protocol::normal_node_id_min);

    // empty knowledge (no id_bind, no routes yet, no hosted mobiles) -> the TOP id, NOT 17..
    CHECK(node.test_find_free_mobile_id(0x00AA0001u) == 254);
    // idempotent: the same key re-offers the same id (it is not yet registered, so still the top)
    CHECK(node.test_find_free_mobile_id(0x00AA0001u) == 254);

    // a known STATIC binding for 254 (a beacon-heard neighbour) must be skipped -> next id down
    CHECK(node.test_id_bind_set(254, 0x00BB00BBu, /*authoritative=*/true));
    CHECK(node.test_find_free_mobile_id(0x00AA0002u) == 253);
    // and a route dest at 253 is also excluded (DV-reachable static) -> 252
    std::array<uint8_t, 64> bcn{}; const size_t bn = make_beacon(/*src=*/253, /*key=*/0x00CC00CCu, bcn);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    node.on_recv(bcn.data(), bn, meta);                       // learns a direct route to 253
    CHECK(node.test_find_free_mobile_id(0x00AA0003u) == 252);
}

// PART 1b — an AUTHORITATIVE static binding landing for an id we already gave a hosted mobile EVICTS the mobile
// (it re-registers via the presence plane). This is the "later binding conflict" self-heal.
TEST_CASE("§S0 id_bind_set — an authoritative static binding evicts an aliasing hosted mobile") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x00005252u);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt id 17
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // host a mobile whose local id is 200 (the alias-to-be)
    std::array<uint8_t, 16> jc{};
    const size_t jn = make_j_claim_mobile(/*host=*/protocol::normal_node_id_min, /*local=*/200, /*hash=*/0x00C0FFEEu, jc);
    node.on_recv(jc.data(), jn, meta);
    CHECK(node.mobile_reg_count() == 1);

    // a CLAIMED (second-hand) binding for 200 must NOT evict (too weak a signal — only a first-hand authoritative
    // beacon reclaims the id). The binding is accepted as a NEW entry (mobiles keep no static id_bind), but the
    // hosted mobile stays put.
    CHECK(node.test_id_bind_set(200, 0x00DEAD01u, /*authoritative=*/false));
    CHECK(node.mobile_reg_count() == 1);
    CHECK(hal.count("mobile_evict_alias") == 0);
    // ... but an AUTHORITATIVE static binding (the real static's own beacon) DOES evict the aliasing mobile
    CHECK(node.test_id_bind_set(200, 0x00DEAD01u, /*authoritative=*/true));
    CHECK(node.mobile_reg_count() == 0);                      // ★ evicted -> it re-registers onto a fresh (top) id
    CHECK(hal.count("mobile_evict_alias") == 1);
    // re-binding the SAME static hash is a no-op eviction (already gone) — no crash, no double emit
    CHECK(node.test_id_bind_set(200, 0x00DEAD01u, /*authoritative=*/true));
    CHECK(hal.count("mobile_evict_alias") == 1);
}

// PART 2 — the transit filter must NOT reject a route through a CONFIRMED STATIC just because its id also set the
// mobile-peer bit (the alias). It MUST still reject a genuine mobile transit (an other-home mobile, no static binding).
TEST_CASE("§S0 route_uses_mobile_as_transit — static carve keeps a confirmed static transit legal") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x00005353u);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt id 17

    // id 50 set the mobile-peer bit (an is_mobile beacon) but has NO static binding -> a genuine mobile transit
    node.test_mark_mobile_peer(50);
    CHECK(node.test_route_uses_mobile_as_transit(/*dest=*/60, /*next=*/50) == true);   // reject: relay THROUGH a mobile

    // now an AUTHORITATIVE static binding for 50 arrives (no hosted mobile on 50 -> no eviction) -> it is a
    // CONFIRMED STATIC; the alias carve makes it a LEGAL transit again (the metal "can't route dest via S2" fix)
    CHECK(node.test_id_bind_set(50, 0x00577A71u, /*authoritative=*/true));
    CHECK(node.test_route_uses_mobile_as_transit(/*dest=*/60, /*next=*/50) == false);  // ★ carve: real static -> allow

    // deliver-TO-a-mobile (next==dest) is always fine, regardless (the existing carve-out)
    node.test_mark_mobile_peer(70);
    CHECK(node.test_route_uses_mobile_as_transit(/*dest=*/70, /*next=*/70) == false);
}

// PART 3 — the defer-loop giveup: a send re-drained past the bound fails loud (send_failed{no_route}) and is NOT
// re-parked, breaking the "send_deferred/send_drained every 1s FOREVER" burn.
TEST_CASE("§S0 defer_send — bounded re-drains give up with send_failed{no_route}") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x00005454u);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt id 17
    Push p{}; while (node.next_push(p)) {}                     // drain any adopt push

    // below the bound: parks normally (no giveup)
    node.test_defer_send(/*dst=*/60, /*ctr=*/1, /*redrain_count=*/protocol::send_defer_max_redrains - 1);
    CHECK(node.test_deferred_count() == 1);
    { bool saw_fail = false; while (node.next_push(p)) if (p.kind == PushKind::send_failed) saw_fail = true; CHECK_FALSE(saw_fail); }

    // AT the bound: fails loud, does NOT park (deferred count unchanged)
    node.test_defer_send(/*dst=*/61, /*ctr=*/2, /*redrain_count=*/protocol::send_defer_max_redrains);
    CHECK(node.test_deferred_count() == 1);                    // still just the first one
    bool giveup = false;
    while (node.next_push(p))
        if (p.kind == PushKind::send_failed && p.dst == 61 && p.ctr == 2 && p.reason == SendFailReason::no_route) giveup = true;
    CHECK(giveup);                                            // ★ send_failed{no_route}
    CHECK(hal.count("send_deferred_giveup") == 1);
}
