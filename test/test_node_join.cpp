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
#include "support/test_hal.h"

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
            int64_t snr_q4 = INT64_MIN;   // §3-D: presence_probe_rx carries the post-update per-mobile EWMA
            bool i_win = false; bool has_iwin = false; std::string reason; };

class TestHal : public mrtest::TestHalBase {
public:
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;

    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override { tx_frames.emplace_back(b, b + n); return TxResult::ok; }
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
                else if (!std::strcmp(fl.key, "snr_q4"))           e.snr_q4 = fl.i;
            } else if (fl.type == EventField::T::boolean) {
                if (!std::strcmp(fl.key, "i_win")) { e.i_win = fl.b; e.has_iwin = true; }
            } else if (fl.type == EventField::T::str) {
                if (!std::strcmp(fl.key, "reason")) e.reason = fl.s ? fl.s : "";
            }
        }
        events.push_back(e);
    }
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

// ============================================================================================================
// ★ set_identity(0) MUST NOT WRITE AN AUTHORITATIVE SELF-BINDING FOR THE RESERVED id 0 (owner ruling 2026-07-27).
// set_identity was the ONLY one of the three id_bind_set(self, authoritative) sites without an `!= 0` guard —
// on_init (node.cpp:341, "node_id 0 is unprovisioned (no identity yet)") and activate_layer (:616) both had one.
// ★★ BYTE-IDENTITY CANNOT SEE ANY OF THIS on the two paths that matter, so these tests are the whole detector:
// the corpus never issues join/create/leave (their verbs live in src/, which the sim does not compile) and
// `addr_conflict_forced_rejoin` appears ZERO times in all 32 scenarios. Measured, not assumed.
// ============================================================================================================
TEST_CASE("★ set_identity(0) writes NO id_bind row — the reserved unprovisioned id is not an identity") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B0B0);
    node.on_init(join_cfg());
    CHECK(node.id_bind_count() == 0);                            // on_init's OWN guard already declined (node_id 0)

    node.set_identity(0, 0x0000B0B0);                            // the unprovision that used to bind {0, ourhash}
    CHECK(node.id_bind_count() == 0);                            // ★ still nothing — this is the fix

    node.set_identity(50, 0x0000B0B0);                           // non-vacuity: a REAL id still self-binds...
    CHECK(node.id_bind_count() == 1);
    CHECK(node.node_id() == 50);
    node.set_identity(0, 0x0000B0B0);                            // ...and dropping back to unprovisioned adds none
    CHECK(node.id_bind_count() == 1);                            //    (the id-50 row remains; it ages out on TTL)
    CHECK(node.node_id() == 0);                                  // ★ the identity ITSELF still went to 0 (not skipped)
}

TEST_CASE("★ set_identity(0) — reset_join_for_reprovision leaves the binding table EMPTY (no id-0 residue)") {
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000C1C1);
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join;
    CHECK(node.on_command(c).code == CmdCode::queued);
    node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);   // adopt -> self-bound under a real id
    CHECK(node.joined());
    CHECK(node.id_bind_count() == 1);

    node.reset_join_for_reprovision();                           // drops our own (prior, ourhash) row, then set_identity(0)

    CHECK(node.node_id() == 0);
    CHECK(node.id_bind_count() == 0);                            // ★ was 1 — an authoritative {node_id:0, ourhash} row
}

TEST_CASE("★ set_identity(0) — forced_rejoin (the HEAL) leaves no id-0 row, and it KEEPS its routes as designed") {
    // THE site that mattered most: node.h:272 says the heal deliberately does NOT clear_routing_state, so nothing
    // wipes _id_bind on the next line here — the id-0 row would have SURVIVED (unlike the verb path). Driven through
    // the REAL wire path (a losing J_DENY tiebreak), not a seam.
    TestHal hal;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000F0F0);    // a HIGH key -> loses the tie
    node.on_init(join_cfg());
    Command c{}; c.kind = CmdKind::join; node.on_command(c); node.on_timer(kJoinListenTimerId); node.on_timer(kJoinClaimGuardTimerId);
    CHECK(node.joined());
    // A peer binding, so "0 rows" below cannot pass just because the table is trivially empty.
    node.test_id_bind_set(/*id=*/60, /*key=*/0x00006060u, /*authoritative=*/true);
    CHECK(node.id_bind_count() == 2);                            // our self-binding + the peer
    hal.events.clear();

    std::array<uint8_t, 16> d{};
    const size_t dn = make_j_deny(/*denied=*/protocol::normal_node_id_min, /*owner_key=*/0x00001111, /*claimant_key=*/0x0000F0F0,
                                  /*owner_epoch=*/5, J_DENY_OWN_ID_DEFENSE, d);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    node.on_recv(d.data(), dn, meta);                            // -> forced_rejoin

    CHECK(hal.find("addr_conflict_forced_rejoin") != nullptr);   // non-vacuous: the heal really ran
    CHECK(node.id_bind_count() == 1);                            // ★ ONLY the peer — no {0, ourhash} row was minted
    // ...and the heal's own contract is intact: the peer binding (the "keeps its routes" half) is untouched.
    uint32_t peer_key = 0;
    CHECK(node.key_hash_of_id(60, peer_key));
    CHECK(peer_key == 0x00006060u);
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

// ============================================================================
// §3-A (2026-07-21 fix slice) — push twins for device-invisible failures, evict-stalest, beacon-fed EWMA.
// ============================================================================

namespace {
// A same-team MOBILE beacon: is_mobile=1 + the type-5 team_id TLV in ext (what teammates broadcast).
size_t make_team_beacon(uint8_t src, uint32_t key_hash32, uint32_t team_id, std::array<uint8_t, 64>& buf,
                        std::array<uint8_t, 8>& tlv) {
    const size_t tn = pack_team_id_tlv(team_id, std::span<uint8_t>(tlv.data(), tlv.size()));
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = key_hash32; in.is_mobile = true;
    in.ext = std::span<const uint8_t>(tlv.data(), tn);
    return pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size()));
}
// A mobile DISCOVER carrying a last_home block (drives the host's _notify_pending stash).
size_t make_j_discover_lasthome(uint32_t key_hash32, uint8_t last_home, uint8_t last_layer,
                                std::array<uint8_t, 20>& buf) {
    j_discover_in in{}; in.leaf_id = 0; in.gateway_capable = false; in.is_mobile = true; in.key_hash32 = key_hash32;
    in.last_home_id = last_home; in.last_home_layer = last_layer; in.last_reg_epoch = 1; in.last_home_key_hash32 = 0;
    return pack_j_discover(in, std::span<uint8_t>(buf.data(), buf.size()));
}
// A presence `check` probe from a hosted mobile (selected home = the host under test).
size_t make_p_probe(uint32_t key_hash32, uint8_t home_id, uint8_t home_layer, uint8_t epoch,
                    std::array<uint8_t, 48>& buf) {
    p_probe_in in{}; in.selected_home_id = home_id; in.selected_home_layer = home_layer;
    in.key_hash32 = key_hash32; in.reg_epoch = epoch;
    return pack_p_probe(in, std::span<uint8_t>(buf.data(), buf.size()));
}
}  // namespace

TEST_CASE("§3-A.1 team-DAD pool-full — join_refused{leaf_full} push twin, windowed (mirrors the static twin)") {
    TestHal hal;
    hal._now = 100000;                                        // a realistic nonzero clock (the window's ==0 seed check)
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A1A1u);
    NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.team_id = 0x7EA30000u;
    node.on_init(mcfg);
    Push p{}; while (node.next_push(p)) {}
    // exhaust the team id space: a same-team beacon from EVERY id 17..254 sets its _team_peer bit
    for (int src = protocol::normal_node_id_min; src <= 254; ++src) {
        std::array<uint8_t, 64> b{}; std::array<uint8_t, 8> tlv{};
        const size_t bn = make_team_beacon(static_cast<uint8_t>(src), 0x00100000u + static_cast<uint32_t>(src), 0x7EA30000u, b, tlv);
        RxMeta meta{8.0f, -80.0f, 0, -1};
        node.on_recv(b.data(), bn, meta);
    }
    while (node.next_push(p)) {}                              // drain anything the beacons pushed
    node.team_dad_fire();                                     // 17..254 all taken -> no candidate
    CHECK(hal.count("team_dad_no_free_id") == 1);
    int refused = 0;
    while (node.next_push(p)) if (p.kind == PushKind::join_refused && p.join_reason == JoinRefuseReason::leaf_full) ++refused;
    CHECK(refused == 1);                                      // ★ the push twin fired
    node.team_dad_fire();                                     // inside the window -> emit again but NO second push
    refused = 0;
    while (node.next_push(p)) if (p.kind == PushKind::join_refused) ++refused;
    CHECK(refused == 0);                                      // ★ rate-limited
    hal._now += protocol::join_refused_retry_ms + 1;          // window elapsed
    node.team_dad_fire();
    refused = 0;
    while (node.next_push(p)) if (p.kind == PushKind::join_refused && p.join_reason == JoinRefuseReason::leaf_full) ++refused;
    CHECK(refused == 1);                                      // ★ windowed re-push
}

TEST_CASE("§3-A.1 sf_list mismatch on mobile adopt — join_refused{sf_list_mismatch} push twin (advisory)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // (a) configured low byte (1<<6) != the host's offered bitmap (1<<7) -> the push fires alongside the adopt
    {
        TestHal hal; hal._now = 100000;
        Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A2A2u);
        NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.allowed_sf_bitmap = (1u << 6);
        node.on_init(mcfg);
        node.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(/*responder=*/30, 0x0000C0C0u, /*local=*/201, /*target=*/0x0000A2A2u, off);
        node.on_recv(off.data(), on, meta);                   // OFFER carries data_sf_bitmap = 1<<7
        node.on_timer(kMobileClaimGuardTimerId);              // adopt
        CHECK(node.mobile_registered());
        CHECK(hal.count("mobile_sf_list_mismatch") == 1);
        Push p{}; int mm = 0;
        while (node.next_push(p))
            if (p.kind == PushKind::join_refused && p.join_reason == JoinRefuseReason::sf_list_mismatch) {
                ++mm; CHECK(p.origin == (1u << 6)); CHECK(p.dst == (1u << 7));   // configured vs offered low bytes
            }
        CHECK(mm == 1);                                       // ★ the advisory twin fired exactly once
    }
    // (b) matching lists -> NO push
    {
        TestHal hal; hal._now = 100000;
        Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000A3A3u);
        NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.allowed_sf_bitmap = (1u << 7);
        node.on_init(mcfg);
        node.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(30, 0x0000C0C0u, 201, 0x0000A3A3u, off);
        node.on_recv(off.data(), on, meta);
        node.on_timer(kMobileClaimGuardTimerId);
        CHECK(node.mobile_registered());
        CHECK(hal.count("mobile_sf_list_mismatch") == 0);
        Push p{}; int mm = 0;
        while (node.next_push(p)) if (p.kind == PushKind::join_refused) ++mm;
        CHECK(mm == 0);
    }
}

// §autoregister ruling (2026-07-21): mobile_autoregister=false MUST mean NO DISCOVERs ever — the DISCOVER/registration
// half of the FSM is gated on registration_armed() (autoregister ON, or a one-shot manual `mobile register` arm). Team-DAD
// rides the SAME FSM tick BEFORE the gate, so a team member self-bootstraps its team id regardless of the toggle.
TEST_CASE("§autoregister — OFF: a lone mobile emits NO autonomous DISCOVER; a manual arm drives exactly ONE (one-shot)") {
    TestHal hal; hal._now = 100000;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B0B0u);
    NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.mobile_autoregister = false;
    node.on_init(mcfg);
    Push p{}; while (node.next_push(p)) {}
    node.on_timer(kMobileDiscoverTimerId);                    // the autonomous FSM kick — GATED
    CHECK(hal.count("mobile_discover_tx") == 0);              // ★ nothing on air (autoregister OFF, no arm)
    node.mobile_register_current();                           // the app arms ONE registration
    node.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("mobile_discover_tx") == 1);              // ★ exactly one DISCOVER
    node.on_timer(kMobileDiscoverTimerId);                    // a later autonomous kick (e.g. a post-reset re-DISCOVER)
    CHECK(hal.count("mobile_discover_tx") == 1);              // ★ still one — the arm is a ONE-SHOT, no auto re-DISCOVER
}

TEST_CASE("§autoregister — OFF team member: team-DAD runs (ungated), but ZERO DISCOVERs to any host") {
    TestHal hal; hal._now = 100000;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B1B1u);
    NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.team_id = 0x7EA30000u; mcfg.mobile_autoregister = false;
    node.on_init(mcfg);
    Push p{}; while (node.next_push(p)) {}
    node.on_timer(kMobileDiscoverTimerId);                    // the team boot kick (fires regardless of the toggle)
    CHECK(hal.count("team_dad_claim") == 1);                  // ★ team-DAD ran (it rides the FSM tick, before the gate)
    CHECK(node.team_local_id() != 0);                         //   ...self-assigned a team-plane id (off-grid-reachable)
    CHECK(hal.count("mobile_discover_tx") == 0);              // ★ but NOT a single host-registration DISCOVER
}

TEST_CASE("§autoregister — OFF team member: a failed manual arm does NOT auto-retry; team plane stays alive") {
    TestHal hal; hal._now = 100000;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B2B2u);
    NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.team_id = 0x7EA30000u; mcfg.mobile_autoregister = false;
    node.on_init(mcfg);
    Push p{}; while (node.next_push(p)) {}
    node.mobile_register_current();                           // arm ONE attempt
    node.on_timer(kMobileDiscoverTimerId);                    // team-DAD + one DISCOVER
    const uint8_t tid = node.team_local_id();
    CHECK(tid != 0);
    CHECK(hal.count("mobile_discover_tx") == 1);
    node.on_timer(kMobileClaimGuardTimerId);                  // window closes with NO offer -> no host
    CHECK(hal.count("mobile_no_host") == 1);
    node.on_timer(kMobileDiscoverTimerId);                    // the (now unarmed) backoff/autonomous re-DISCOVER
    CHECK(hal.count("mobile_discover_tx") == 1);              // ★ off-grid-QUIET: no second DISCOVER without a fresh arm
    CHECK_FALSE(node.mobile_registered());                    //   still unregistered on the host plane
    CHECK(node.team_local_id() == tid);                       // ★ team plane intact (F-PS-1) — the team id survives
}

TEST_CASE("§3-D beacon feeds the hosted-mobile SNR EWMA — CLAIM seeds, beacon steps, probe steps (one shared path)") {
    TestHal hal; hal._now = 100000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    host.on_init(join_cfg());
    const uint32_t M = 0x0000D1D1u;
    // CLAIM at +8 dB seeds the EWMA
    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(/*host=*/42, /*local=*/254, M, cl);
    { RxMeta meta{8.0f, -80.0f, 0, -1}; host.on_recv(cl.data(), cn, meta); }
    // ★ the mobile's BEACON at -4 dB must STEP the EWMA (was last_heard-only before the 3-D ruling)
    std::array<uint8_t, 64> b{};
    { beacon_in in{}; in.leaf_id = 0; in.src = 254; in.key_hash32 = M; in.is_mobile = true;
      const size_t bn = pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
      RxMeta meta{-4.0f, -110.0f, 0, -1}; host.on_recv(b.data(), bn, meta); }
    // a probe at +8 dB steps again; its presence_probe_rx emit carries the resulting EWMA
    std::array<uint8_t, 48> pr{};
    const size_t pn = make_p_probe(M, /*home=*/42, /*layer=*/0, /*epoch=*/1, pr);
    { RxMeta meta{8.0f, -80.0f, 0, -1}; host.on_recv(pr.data(), pn, meta); }
    const int16_t seed     = protocol::db_to_q4(8.0f);
    const int16_t after_b  = protocol::snr_ewma_update(seed, protocol::db_to_q4(-4.0f));
    const int16_t expected = protocol::snr_ewma_update(after_b, protocol::db_to_q4(8.0f));
    const int16_t pre_fix  = protocol::snr_ewma_update(seed, protocol::db_to_q4(8.0f));   // what beacon-skipping produced
    CHECK(expected != pre_fix);                               // the test discriminates (not vacuous)
    const Ev* e = hal.find("presence_probe_rx");
    CHECK(e != nullptr);
    if (e) CHECK(e->snr_q4 == expected);                      // ★ the beacon sample is IN the accumulator
}

TEST_CASE("§3-A.6 _notify_pending evicts the STALEST stash when full (not slot 0)") {
    TestHal hal; hal._now = 1000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    host.on_init(join_cfg());
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // fill all 16 stash slots: mobiles h0..h15, each DISCOVERing with last_home=7 (stash_ms ascending)
    for (uint32_t i = 0; i < protocol::cap_host_mobiles; ++i) {
        std::array<uint8_t, 20> d{};
        const size_t dn = make_j_discover_lasthome(0x00001000u + i, /*last_home=*/7, /*last_layer=*/0, d);
        host.on_recv(d.data(), dn, meta);
        hal._now += 10;
    }
    // refresh h0 (slot 0 becomes the FRESHEST -> the old evict-slot-0 would now clobber the wrong entry)
    hal._now = 2000;
    { std::array<uint8_t, 20> d{};
      const size_t dn = make_j_discover_lasthome(0x00001000u, 7, 0, d);
      host.on_recv(d.data(), dn, meta); }
    // overflow with h16 -> must evict the STALEST (h1), NOT slot 0 (h0)
    hal._now = 2010;
    { std::array<uint8_t, 20> d{};
      const size_t dn = make_j_discover_lasthome(0x00002000u, 7, 0, d);
      host.on_recv(d.data(), dn, meta); }
    // h0 CLAIMs -> its stash must still be there -> the old-home breadcrumb originates
    { std::array<uint8_t, 16> cl{};
      const size_t cn = make_j_claim_mobile(42, /*local=*/254, 0x00001000u, cl);
      host.on_recv(cl.data(), cn, meta); }
    CHECK(hal.count("presence_notify_tx") == 1);              // ★ h0 survived (evict-slot-0 would have dropped it)
    // h1 CLAIMs -> its stash was the eviction victim -> NO breadcrumb
    { std::array<uint8_t, 16> cl{};
      const size_t cn = make_j_claim_mobile(42, /*local=*/253, 0x00001001u, cl);
      host.on_recv(cl.data(), cn, meta); }
    CHECK(hal.count("presence_notify_tx") == 1);              // unchanged — h1's stash is gone
}

// ============================================================================
// ★★ §B132 — A GATEWAY MUST NEVER BE ELIGIBLE AS A MOBILE HOME.
//
// THE DEFECT (owner-reported, METAL-CONFIRMED): a gateway time-multiplexes ONE radio across TWO leaves in
// alternating windows, so for ~half of every cycle it is not listening on the mobile's leaf at all. A home owes its
// mobile CONTINUOUS service — registration/presence, last-mile DATA, hash/pubkey proxy answers, reverse ACK and
// delegation, home liveness — which a gateway can structurally only provide half the time. On the bench a gateway
// serving leaves 6/5 in 7.5 s windows reported `hosted-mobiles n=1 / hash=0xF7C0F666 local_id=254 pubkey=yes`
// while the mobile reported `REGISTERED home=5`.
//
// ★ A send that HAPPENS to align with the gateway's window still succeeds, and THAT is what let this hide. The
// state even looked healthy (`hosting=1`, `pubkey=yes`). ⇒ these tests assert ELIGIBILITY, never a delivery rate.
//
// THE SITES that were wrong, all now consuming ONE accessor (Node::can_host_mobiles, node.h):
//   (a) J DISCOVER->OFFER  — tested `is_mobile || !host_mobiles`; `host_mobiles` DEFAULTS TRUE, so a gateway heard
//       the leaf-exempt mobile DISCOVER and offered a strong-SNR home;
//   (b) CLAIM acceptance   — had NO eligibility test at all, only `chosen_host_id != _node_id`;
//   (c) presence_ingest_probe — re-spelled (a) verbatim, and its comment asserted it was the "SAME gate" as (a).
//       It was. Both were wrong, which is why the invariant is now defined exactly once (U1).
//
// ★★★ HOW THESE TESTS WERE BUILT, because the FIRST VERSION OF THEM COULD NOT FAIL AND THE MUTATION MATRIX SAID SO.
// Reverting BOTH gateway clauses to the pre-fix expression left every one of them GREEN. Cause: on_init also forces
// `host_mobiles = false` on a gateway (C3), so `host_mobiles &&` short-circuits FIRST and the clauses are never
// reached. The two defences MASK EACH OTHER, and a test that exercises a gateway "normally" measures only the
// force-off. ⇒ every gateway test below FORCES `host_mobiles` BACK ON after on_init, so the gateway clauses are the
// ONLY remaining defence — and that is also exactly the property gate item 6 asks for ("`cfg set host_mobiles on`
// cannot make a gateway eligible").
//
// ★★ AND `!is_gateway` vs `n_layers == 1` CANNOT BE SEPARATED BY A NORMALLY-INITED GATEWAY — on_init derives
// `is_gateway = (n_layers == 2)`, so on the happy path either clause alone suffices and dropping ONE stays green.
// Each is therefore measured in the state that ISOLATES it (§B132/6): `is_gateway` true with `n_layers == 1`, and
// the REFUSED-on_init state (`n_layers == 2` with `is_gateway` false). See §B132/6 for why both states are real.
// ============================================================================

namespace {
// A VALID dual-layer gateway config — it MUST pass on_init's §3.2 gate, otherwise these tests would be exercising
// the config refusal instead of the eligibility invariant (the CHECK on on_init in each test enforces that).
// Two details are load-bearing and BOTH were found by a premise CHECK failing, not by reasoning:
//  (1) `layer_id = 16` (0x10) — validate_gateway_layers REFUSES layer_id 0, but activate_layer stamps
//      `_cfg.leaf_id = layer_id & 0x0F`, so a nibble of 0 is what makes the active leaf 0. That matters because the
//      J CLAIM is NOT leaf-exempt (only DISCOVER and the mobile-side OFFER are), so a claim built by the helpers
//      above (leaf_id 0) would be dropped by the foreign-layer filter BEFORE reaching the gate under test — and
//      §B132/2 would have passed for a reason with nothing to do with the fix. Layer 1 uses leaf 7, a DIFFERENT
//      nibble, as §0.8 requires.
//  (2) `node_id = 5` on BOTH layers — activate_layer stamps `_node_id = L.node_id`, so leaving it 0 would put the
//      node behind the `_node_id == 0` mid-join suspend, which masks the eligibility gate entirely.
// ⇒ the gateway ends up as reachable, and as provisioned, as the static host in the positive control; the ONLY
//   difference between them is eligibility.
NodeConfig gw_join_cfg() {
    NodeConfig c = join_cfg();
    c.n_layers = 2;
    for (uint8_t i = 0; i < 2; ++i) {
        c.layers[i].layer_id          = static_cast<uint8_t>(i == 0 ? 16 : 7);   // nibble 0 / nibble 7 — differ (§0.8)
        c.layers[i].node_id           = 5;                                       // the bench's gateway identity
        c.layers[i].routing_sf        = 7;
        c.layers[i].allowed_sf_bitmap = static_cast<uint16_t>(1u << 7);
    }
    return c;
}
// ★★ Init a gateway AND FORCE `host_mobiles` BACK ON. Without this the tests measure on_init's force-off instead of
// the gateway clauses (proven: reverting both clauses left them green). Returns false if on_init refused.
// ⓘ This is ALSO the realistic hostile case: `host_mobiles` is a LIVE console knob, so a running gateway can be
// told `cfg set host_mobiles on` at any moment. The core must refuse regardless of what that byte says.
bool init_gateway_hostile(Node& n) {
    if (!n.on_init(gw_join_cfg())) return false;
    if (n.config().host_mobiles) return false;              // C3: on_init must have forced it OFF first
    n.mutable_config().host_mobiles = true;                  // ...now force it back ON — the clauses must still refuse
    return n.config().host_mobiles && !n.can_host_mobiles(); // eligible-by-byte, INELIGIBLE by invariant
}
// ★★★ THE ONLY HONEST WITNESS FOR "AN OFFER WENT OUT" — THE PARSED FRAME ON THE WIRE.
// `MR_EMIT("mobile_offer_tx", …)` is emitted in node_join.cpp immediately BEFORE `jtx_stash_arm`, and its own comment
// says why ("the OFFER is committed"). The frame is transmitted 100..1000 ms LATER, from the
// kMobileOfferBackoffTimerId handler. ⇒ THE EVENT MEANS *COMMITTED*, NOT *TRANSMITTED*, so counting it cannot
// distinguish "an OFFER went out" from "an OFFER was staged and then correctly suppressed" — which is the entire
// question §B132b asks. Every §B132b case below therefore asserts THIS, and the event only as a premise.
int count_j_offer_mobile(const std::vector<std::vector<uint8_t>>& frames) {
    int c = 0;
    for (const auto& f : frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(j_opcode::offer) && p->is_mobile) ++c; }
    return c;
}
// STAGE (but do not transmit) a mobile OFFER on `host` by feeding it a mobile DISCOVER. Returns the COMMITTED-event
// count so the caller can assert the commit really happened — without that premise a later "no frame" assertion is
// vacuous (a build that stages nothing would pass it).
int stage_mobile_offer(Node& host, TestHal& hal, uint32_t mobile_hash) {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> disc{};
    const size_t dn = make_j_discover_mobile(mobile_hash, disc);
    hal.events.clear(); hal.tx_frames.clear();
    host.on_recv(disc.data(), dn, meta);
    return hal.count("mobile_offer_tx");
}
}  // namespace

// TEST 1 — the OFFER. A gateway hears the (leaf-exempt) mobile DISCOVER and must stay SILENT.
// ★ Asserts the SIDE EFFECT (no OFFER frame emitted), never an internal flag.
TEST_CASE("★ §B132/1 — a GATEWAY emits NO mobile OFFER even with host_mobiles forced ON") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> disc{};
    const size_t dn = make_j_discover_mobile(/*hash=*/0x0000D1D1u, disc);

    TestHal hal; hal._now = 100000;
    Node gw(hal, /*node_id=*/5, /*key_hash32=*/0x00000005u);          // identity 5 — the bench's gateway id
    CHECK(init_gateway_hostile(gw));                                  // ★ valid gateway, host_mobiles forced back ON
    // Premise checks — without these the test could pass for a reason unrelated to the fix:
    CHECK(gw.config().is_gateway);                                    // DERIVED by on_init from n_layers==2
    CHECK(gw.config().n_layers == 2);
    CHECK(gw.config().host_mobiles);                                  // ★ the byte says YES, so it cannot be doing the work
    CHECK(gw.node_id() != 0);                                         // ★ NOT the `_node_id == 0` suspend — that would mask this gate

    hal.events.clear();
    gw.on_recv(disc.data(), dn, meta);
    CHECK(hal.count("mobile_offer_tx") == 0);                         // ★ THE DEFECT: was 1 — a strong-SNR gateway offered and won

    // ⓘ THE OTHER LEAF is asserted in test_dual_layer.cpp ("§B132/1b"), which owns DualLayerTestAccess and can
    // therefore really swap the active leaf. It is NOT asserted here by a fake swap. ★ And note WHY it is a
    // separate concern rather than a formality: a mobile DISCOVER is LEAF-EXEMPT on the host side (node_join
    // handle_j `mobile_exempt`), so it reaches this decision REGARDLESS of which leaf is active — which is how a
    // gateway came to answer a DISCOVER from a leaf it was not even serving.
}

// TEST 2 — the CLAIM. A CLAIM addressed AT the gateway (stale, or forged with no preceding OFFER) must be IGNORED.
// ★ This is the leg the tempting half-fix ("just gate the OFFER") leaves wide open, and it is the leg that makes the
// invalid home LOAD-BEARING: the registry entry is what makes the gateway start proxying for the mobile.
// (Mutation-confirmed: removing this gate alone turns this test RED and nothing else.)
TEST_CASE("★ §B132/2 — a stale/forged mobile CLAIM addressed at a GATEWAY is IGNORED (no registry entry)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node gw(hal, /*node_id=*/5, /*key_hash32=*/0x00000005u);
    CHECK(init_gateway_hostile(gw));
    CHECK(gw.mobile_reg_count() == 0);

    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(/*host=*/5, /*local=*/254, /*hash=*/0xF7C0F666u, cl);   // the bench's hash, ADDRESSED at the gateway
    hal.events.clear();
    gw.on_recv(cl.data(), cn, meta);
    CHECK(gw.mobile_reg_count() == 0);                                // ★ THE DEFECT: was 1 — recorded without ever asking whether we may host

    gw.on_recv(cl.data(), cn, meta);                                  // a REFRESH must not sneak in either (same gate)
    CHECK(gw.mobile_reg_count() == 0);
}

// TEST 3 — the presence PROBE + ROSTER, exercised through the ROLE TRANSITION, which is the only way to put a live
// registry entry on a gateway now that the CLAIM path refuses one. The register requires exactly this: "a transition
// into gateway role must clear any hosted-mobile and pending-host runtime state".
// ★★ WHY THE TRANSITION IS THE ONLY HONEST STIMULUS: on a gateway with an EMPTY registry, presence_ingest_probe
// finds no entry and presence_emit_roster returns at its `_mobile_reg_n == 0` early-out — so both would emit
// nothing WITH OR WITHOUT the fix. Mutation-confirmed: the first version of this test stayed GREEN when the probe
// gate AND the roster gate were both deleted. A REAL hosted entry is what makes the two gates load-bearing.
TEST_CASE("★ §B132/3 — a node that BECOMES a gateway with a live hosted mobile answers NO probe and emits NO roster") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    const uint32_t M = 0xF7C0F666u;                                   // the bench's mobile hash

    // (1) start as an ORDINARY STATIC host and really host the mobile — so the registry entry is genuine.
    Node n(hal, /*node_id=*/5, /*key_hash32=*/0x00000005u);
    CHECK(n.on_init(join_cfg()));
    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(/*host=*/5, /*local=*/254, M, cl);
    n.on_recv(cl.data(), cn, meta);
    CHECK(n.mobile_reg_count() == 1);                                 // ★ the entry EXISTS — the early-out is out of the way
    CHECK(n.can_host_mobiles());                                      // and it was legitimately eligible at this point

    // (2) it is reprovisioned into a gateway THROUGH REAL `on_init` — and that config is REFUSED.
    // ★★ WHY THE REFUSED PATH RATHER THAN A SUCCESSFUL INIT (and rather than the two `mutable_config()` pokes this
    // test used in round 1, which exercised only the PREDICATE and never any implemented cleanup): a SUCCESSFUL
    // gateway `on_init` now CLEARS the hosted registry (§B132b/5), which would put `presence_emit_roster` back behind
    // its own `_mobile_reg_n == 0` early-out and make (4) below vacuous AGAIN — the exact round-1 defect. The REFUSED
    // path is the one state that is BOTH real and registry-preserving: `_cfg = cfg` is assigned BEFORE
    // `validate_gateway_layers`' early return while every clear is AFTER it, and `src/fw_main.cpp` merely prints
    // "config = REFUSED" and keeps the node running (see §B132/6b). ⇒ real code runs, the hosted entry survives, and
    // the two gates under test are the only thing left that can suppress the probe answer and the roster.
    NodeConfig bad = gw_join_cfg();
    bad.layers[1].layer_id = 0;                                       // §3.2 REQUIRED field missing -> validate refuses
    CHECK_FALSE(n.on_init(bad));                                      // ★ REAL on_init, and it REFUSED
    CHECK(n.config().n_layers == 2);                                  // ★ the dual-layer count SURVIVED the refusal
    CHECK(n.config().host_mobiles);                                   // ★ the byte still says YES — not what refuses
    CHECK_FALSE(n.can_host_mobiles());                                // ★ the invariant now refuses (via `n_layers == 1`)
    CHECK(n.mobile_reg_count() == 1);                                 // ★ the entry is STILL PHYSICALLY PRESENT (every clear is after the early return)

    // (3) the mobile's check probe naming this node as home must NOT be answered...
    std::array<uint8_t, 48> pr{};
    const size_t pn = make_p_probe(M, /*home=*/5, /*layer=*/0, /*epoch=*/1, pr);
    hal.events.clear();
    n.on_recv(pr.data(), pn, meta);
    CHECK(hal.count("presence_probe_rx") == 0);                       // ★ an ineligible node serves no presence

    // (4) ...and NO roster may be emitted, even though a hosted entry is sitting right there.
    n.on_timer(79);                                                   // kPresenceRosterTimerId
    CHECK(hal.count("presence_roster_tx") == 0);                      // ★ no "I am your home" advertisement
}

// TEST 4 — ★★ THE POSITIVE CONTROL. The SAME stimuli on an ORDINARY STATIC node must all still work.
// Without this, tests 1-3 are satisfied by a node that hosts nobody — including by hard-wiring the invariant false.
// (Mutation-confirmed and COUNTED: hard-wiring `can_host_mobiles()` to false fails 17 cases — 4 §B132 and
// 13 PRE-EXISTING hosting cases. That corroboration is why this control is trustworthy rather than self-referential.)
TEST_CASE("★★ §B132/4 POSITIVE CONTROL — an ordinary STATIC node still OFFERs, RECORDS and ROSTERS; mobile + opt-out still refuse") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));                                  // single-layer, host_mobiles defaults TRUE
    CHECK(host.can_host_mobiles());                                   // the invariant holds for the node that SHOULD host
    CHECK(host.config().host_mobiles);                                // ★ on_init's gateway force-off did NOT touch a static node
    CHECK(host.config().n_layers == 1);
    CHECK_FALSE(host.config().is_gateway);

    // (a) it OFFERs
    std::array<uint8_t, 16> disc{};
    const size_t dn = make_j_discover_mobile(/*hash=*/0x0000D1D1u, disc);
    hal.events.clear();
    host.on_recv(disc.data(), dn, meta);
    CHECK(hal.count("mobile_offer_tx") == 1);                         // ★ the fix did NOT break ordinary hosting

    // (b) it RECORDS a CLAIM
    const uint32_t M = 0x0000D1D1u;
    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(/*host=*/42, /*local=*/254, M, cl);
    host.on_recv(cl.data(), cn, meta);
    CHECK(host.mobile_reg_count() == 1);                              // ★

    // (c) it answers a probe AND emits a roster
    std::array<uint8_t, 48> pr{};
    const size_t pn = make_p_probe(M, /*home=*/42, /*layer=*/0, /*epoch=*/1, pr);
    hal.events.clear();
    host.on_recv(pr.data(), pn, meta);
    CHECK(hal.count("presence_probe_rx") == 1);                       // ★ the probe answer the gateway must never give
    host.on_timer(79);                                                // kPresenceRosterTimerId
    CHECK(hal.count("presence_roster_tx") == 1);                      // ★ the roster the gateway must never send

    // (d) the DELIBERATE opt-out (B3) still works — host_mobiles=false on a plain static node
    {
        TestHal h2; h2._now = 100000;
        Node opted(h2, /*node_id=*/43, /*key_hash32=*/0x00004343u);
        NodeConfig oc = join_cfg(); oc.host_mobiles = false;
        CHECK(opted.on_init(oc));
        CHECK_FALSE(opted.can_host_mobiles());
        opted.on_recv(disc.data(), dn, meta);
        CHECK(h2.count("mobile_offer_tx") == 0);                      // B3 opt-out preserved by the shared invariant
    }
    // (e) ★ A MOBILE NEVER HOSTS — the invariant's `!is_mobile` clause. Added because the mutation matrix showed
    // NOTHING in the whole suite went red when that clause was dropped: a pre-existing coverage hole, not a new one.
    {
        TestHal h3; h3._now = 100000;
        Node mob(h3, /*node_id=*/44, /*key_hash32=*/0x00004444u);
        NodeConfig mc = join_cfg(); mc.is_mobile = true; mc.host_mobiles = true;   // host_mobiles ON, so it is is_mobile that must refuse
        CHECK(mob.on_init(mc));
        CHECK(mob.config().host_mobiles);
        CHECK_FALSE(mob.can_host_mobiles());
        h3.events.clear();
        mob.on_recv(disc.data(), dn, meta);
        CHECK(h3.count("mobile_offer_tx") == 0);                      // ★ a mobile registers to a home; it is not one
    }
}

// TEST 5 — the BENCH TOPOLOGY, reproduced. A gateway and a static host BOTH audible to one mobile: only the static
// host may offer. This is the scenario the owner measured, and the one a per-site fix could still get wrong (if the
// gateway refused at CLAIM but still OFFERed, it would waste airtime AND advertise an invalid home the mobile may
// select over the valid one).
TEST_CASE("★ §B132/5 — gateway + static host both audible: ONLY the static host offers") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> disc{};
    const size_t dn = make_j_discover_mobile(/*hash=*/0xF7C0F666u, disc);

    TestHal hgw; hgw._now = 100000;
    Node gw(hgw, /*node_id=*/5, /*key_hash32=*/0x00000005u);          // the bench's gateway identity 5
    CHECK(init_gateway_hostile(gw));                                  // host_mobiles forced ON — the clauses must carry it

    TestHal hst; hst._now = 100000;
    Node stat(hst, /*node_id=*/42, /*key_hash32=*/0x00004242u);       // the always-on static node the mobile SHOULD pick
    CHECK(stat.on_init(join_cfg()));

    hgw.events.clear(); hst.events.clear();
    gw.on_recv(disc.data(), dn, meta);                                // same DISCOVER, same SNR, both hear it
    stat.on_recv(disc.data(), dn, meta);

    CHECK(hgw.count("mobile_offer_tx") == 0);                         // ★ the gateway is silent
    CHECK(hst.count("mobile_offer_tx") == 1);                         // ★ the static host answers
    // ★ the DISCRIMINATION stated as an assertion rather than left to the reader: the two nodes received
    // BYTE-IDENTICAL stimuli at identical SNR and both had `host_mobiles` true, so any difference in outcome is
    // eligibility and nothing else.
    CHECK(hgw.count("mobile_offer_tx") != hst.count("mobile_offer_tx"));
}

// TEST 6 — ★★ THE TWO CLAUSES, EACH MEASURED IN THE STATE THAT ISOLATES IT.
// `!is_gateway` and `n_layers == 1` LOOK redundant because on_init derives `is_gateway = (n_layers == 2)`, so on any
// normally-inited node either one alone suffices and dropping ONE stays green (mutation-confirmed: MUT-A and MUT-B
// were both green before this test existed). They are NOT redundant, because the identity holds only AFTER A
// SUCCESSFUL on_init — and each half below is a state a real device can actually be in.
TEST_CASE("★★ §B132/6 — !is_gateway and n_layers==1 are each LOAD-BEARING (measured in the states that separate them)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> disc{};
    const size_t dn = make_j_discover_mobile(/*hash=*/0x0000D1D1u, disc);

    // (a) `is_gateway` TRUE with `n_layers == 1` — isolates `!is_gateway`.
    // REAL PROVENANCE: on the device `is_gateway` and `n_layers` are SEPARATELY-PERSISTED NV bytes
    // (src/fw_main.cpp: `cfg.is_gateway = nv.is_gateway` and `cfg.n_layers` from `nv.n_layers`), and
    // `host_mobiles` is a live console knob — so a stale/partial NV blob or a live poke reaches this state.
    {
        TestHal hal; hal._now = 100000;
        Node n(hal, /*node_id=*/5, /*key_hash32=*/0x00000005u);
        CHECK(n.on_init(join_cfg()));                                 // single-layer: on_init derives is_gateway = false
        n.mutable_config().is_gateway = true;                         // ...now assert the gateway role on a 1-layer node
        CHECK(n.config().n_layers == 1);                              // ★ so `n_layers == 1` PASSES and cannot be refusing
        CHECK(n.config().host_mobiles);                               // ★ and the byte says YES
        CHECK_FALSE(n.can_host_mobiles());                            // ⇒ only `!is_gateway` can be doing the work
        hal.events.clear();
        n.on_recv(disc.data(), dn, meta);
        CHECK(hal.count("mobile_offer_tx") == 0);                     // ★ RED if `!is_gateway` is dropped
    }

    // (b) `n_layers == 2` with `is_gateway` FALSE — isolates `n_layers == 1`. ★ THE REFUSED-on_init STATE, and it is
    // REACHABLE AND NON-FATAL: node.cpp assigns `_cfg = cfg` BEFORE validate_gateway_layers' early return, while the
    // `is_gateway` derivation is AFTER it, and src/fw_main.cpp only PRINTS "config = REFUSED" and keeps the node
    // running. So a dual-layer config that fails validation leaves n_layers == 2 with is_gateway carrying whatever
    // the caller/NV supplied — which can legitimately be false. Here that is reproduced through on_init ITSELF (an
    // invalid gateway config), not by hand, so it is the real path and not a hypothetical.
    {
        TestHal hal; hal._now = 100000;
        Node n(hal, /*node_id=*/5, /*key_hash32=*/0x00000005u);
        NodeConfig bad = gw_join_cfg();
        bad.layers[1].layer_id = 0;                                   // §3.2 REQUIRED field missing -> validate refuses
        CHECK_FALSE(n.on_init(bad));                                  // ★ on_init REFUSED — and the node keeps running
        CHECK(n.config().n_layers == 2);                              // ★ the dual-layer count SURVIVED the refusal
        CHECK_FALSE(n.config().is_gateway);                           // ★ and the derivation never ran ⇒ the identity is BROKEN here
        CHECK(n.config().host_mobiles);                               // ★ the force-off never ran either (it is after the return)
        CHECK_FALSE(n.can_host_mobiles());                            // ⇒ only `n_layers == 1` can be doing the work
        hal.events.clear();
        n.on_recv(disc.data(), dn, meta);
        CHECK(hal.count("mobile_offer_tx") == 0);                     // ★ RED if `n_layers == 1` is dropped
    }
}

// ============================================================================
// ★★★ §B132b — THE DELAYED-TRANSMISSION HOLE, AND WHY ROUND 1's TESTS COULD NOT SEE IT.
//
// ⛔ THE HOLE. The OFFER is not transmitted where it is decided. `handle_j`'s DISCOVER responder builds it, emits
// `mobile_offer_tx`, and hands it to `jtx_stash_arm` (the §S6/QA-3b de-storm), which arms
// kMobileOfferBackoffTimerId for a jitter of 100..1000 ms. The timer handler in `node.cpp` then transmitted it
// UNCONDITIONALLY. ⇒ eligibility was checked at the moment of DECISION and never at the moment the frame LEFT.
//
// ★★ AND IT IS REACHABLE WITHOUT A REBOOT — two of the ways are LIVE console knobs (`src/firmware_config.cpp`):
//   • `cfg set mobile 1` — LIVE (`lc.is_mobile = …`, `live` left true). `role_set_refusal`'s O2 clause refuses a
//     promotion only while `mobile_reg_count() != 0`, and A STAGED OFFER IS NOT A HOSTED MOBILE, so the guard does
//     not see it. A node can therefore become a MOBILE inside the jitter window and then advertise itself as a home.
//   • `cfg set host_mobiles off` — LIVE (`persist = false`), i.e. the B3 opt-out has the same window.
//   ⓘ HONEST SCOPE, MEASURED: the *gateway* transition of the register's own wording is NOT reachable this way —
//     `is_gateway` is derived in `on_init` only and `cfg set n_layers` is `live = false` ("reboot to apply"), so on a
//     device becoming a gateway means a REBOOT and hence a fresh, empty LayerState. The `on_init` cleanup is
//     therefore DEFENCE IN DEPTH (it covers a re-init and any inconsistent runtime state); the boundary re-check is
//     what closes the live, reachable window. Both shipped; each is attributed by its own case below.
//
// ★★★ AND THE DEFECT-CLASS: ROUND 1 ASSERTED `mobile_offer_tx` AS PROOF OF TRANSMISSION. It is not — it fires
// BEFORE the stash, and its own source comment says so ("the EMIT stays here (the OFFER is committed)"). A test that
// counts it cannot tell "an OFFER went out" from "an OFFER was staged and then correctly suppressed". That is the
// FOURTH instance in this project of one shape: A CONTRACT EVENT ASSERTING A PHYSICAL ACT, REACHABLE FROM A PATH
// THAT TRANSMITTED NOTHING (after `emit_hash_query`, `tx_initiating`, `tx_with_retry`/`DeviceHal::tx`).
// ⇒ every case below asserts the PARSED J OFFER FRAME in `hal.tx_frames`; the event appears only as a premise.
// ⚠ AND NOT `hal.cancel`: TestHalBase::cancel() is a NO-OP, so the cancels in `mobile_host_pending_clear()` cannot
// carry any assertion here. Each case FIRES timer 80 and reads the wire.
// ============================================================================

// TEST 7 — ★★ THE DISTINCTION ITSELF, PINNED. Then the regression: transition, fire, nothing on the wire.
TEST_CASE("★★★ §B132b/1 — `mobile_offer_tx` means COMMITTED, not TRANSMITTED; a gateway transition kills the staged OFFER") {
    TestHal hal; hal._now = 100000;
    Node n(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(n.on_init(join_cfg()));                                     // an ordinary, legitimately eligible static host
    CHECK(n.can_host_mobiles());

    // (1) ★★ THE DEFECT-CLASS ASSERTION: the event fires, and NOTHING IS ON THE WIRE YET.
    CHECK(stage_mobile_offer(n, hal, /*mobile_hash=*/0x0000D1D1u) == 1);   // committed...
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                       // ★ ...and NOT transmitted. This is why counting the event proves nothing about the wire.

    // (2) inside the jitter window the node becomes a GATEWAY, through REAL on_init with a VALID gateway config.
    CHECK(init_gateway_hostile(n));                                   // ★ real on_init + host_mobiles forced back ON (so it cannot be the byte doing the work)
    CHECK(n.config().is_gateway);
    CHECK(n.config().host_mobiles);
    CHECK_FALSE(n.can_host_mobiles());

    // (3) the backoff timer fires. ★ THE FRAME MUST NOT LEAVE.
    hal.tx_frames.clear();
    n.on_timer(80);                                                   // kMobileOfferBackoffTimerId
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                  // ★★ THE DEFECT: the OFFER went out FROM THE GATEWAY, advertising exactly the invalid home §B132 exists to prevent
    CHECK(hal.tx_frames.empty());                                     // and nothing else was substituted for it either

    // (4) a re-fire must stay silent too (the stash is gone, not merely skipped once).
    n.on_timer(80);
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);
}

// TEST 8 — ★★ THE POSITIVE CONTROL FOR THE WHOLE §B132b GROUP. Without it, every "no OFFER frame" assertion above is
// satisfied by a build that never transmits an OFFER at all — including one where timer 80 does nothing.
TEST_CASE("★★ §B132b/2 POSITIVE CONTROL — without the transition, firing timer 80 DOES transmit the staged OFFER") {
    TestHal hal; hal._now = 100000;
    Node n(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(n.on_init(join_cfg()));
    CHECK(stage_mobile_offer(n, hal, /*mobile_hash=*/0x0000D1D1u) == 1);
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                  // staged only

    hal.tx_frames.clear();
    n.on_timer(80);                                                   // kMobileOfferBackoffTimerId
    CHECK(count_j_offer_mobile(hal.tx_frames) == 1);                  // ★ the frame really is on the wire — the harness CAN see a transmission
    // ...and the transmitted frame is addressed at the discovering mobile and names US as the home (so it is the
    // OFFER under discussion and not some other J frame that happened to be emitted).
    {
        int checked = 0;
        for (const auto& f : hal.tx_frames) {
            auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
            if (!p || p->opcode != static_cast<uint8_t>(j_opcode::offer)) continue;
            CHECK(p->target_key_hash32 == 0x0000D1D1u);               // ★ §S6: addressed AT the discovering mobile
            CHECK(p->responder_node_id == 42);                        // ★ and it advertises THIS node as the home
            ++checked;
        }
        CHECK(checked == 1);
    }
    n.on_timer(80);                                                   // the slot is consumed -> a re-fire must not duplicate
    CHECK(count_j_offer_mobile(hal.tx_frames) == 1);
}

// TEST 9 — ★★ ATTRIBUTION FOR THE BOUNDARY RE-CHECK ALONE. The REFUSED-`on_init` path returns BEFORE the cleanup, so
// the stash is provably still armed here and `mobile_host_pending_clear()` has demonstrably not run. ⇒ this case is
// RED iff the timer-boundary `can_host_mobiles()` re-check is removed, whether or not the cleanup exists.
// (This is the masking cure the brief demanded: the two defences cannot both cover this state.)
TEST_CASE("★★ §B132b/3 — the REFUSED-on_init state (cleanup never ran) still transmits NO staged OFFER") {
    TestHal hal; hal._now = 100000;
    Node n(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(n.on_init(join_cfg()));
    CHECK(stage_mobile_offer(n, hal, /*mobile_hash=*/0x0000D1D1u) == 1);   // ★ armed, and the commit is proven

    NodeConfig bad = gw_join_cfg();
    bad.layers[1].layer_id = 0;                                       // §3.2 REQUIRED field missing -> validate refuses
    CHECK_FALSE(n.on_init(bad));                                      // ★ REFUSED — returns before the force-off AND before the cleanup
    CHECK(n.config().n_layers == 2);                                  // ★ the state the `n_layers == 1` clause exists for
    CHECK_FALSE(n.config().is_gateway);                               // ★ the derivation never ran
    CHECK(n.config().host_mobiles);                                   // ★ the force-off never ran ⇒ the byte still says YES
    CHECK_FALSE(n.can_host_mobiles());

    hal.tx_frames.clear();
    n.on_timer(80);                                                   // kMobileOfferBackoffTimerId
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                  // ★ ONLY the transmission-boundary re-check can produce this zero
}

// TEST 10 — ★★ ATTRIBUTION FOR THE `on_init` CLEANUP ALONE. Here the node is ELIGIBLE AGAIN at fire time, so the
// boundary re-check PASSES and cannot suppress anything: the only thing that can keep the stale OFFER off the wire is
// that the gateway init DROPPED it. ⇒ RED iff `mobile_host_pending_clear()` is removed from `on_init`.
// ⓘ The second `on_init` is a TEST INSTRUMENT, not a claimed device path (a device role change is reboot-to-apply,
// see the group header). It exists to isolate the cleanup, and it is honest about what it isolates.
TEST_CASE("★★ §B132b/4 — a gateway init DROPS the staged OFFER, so it cannot fire even once the node is eligible again") {
    TestHal hal; hal._now = 100000;
    Node n(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(n.on_init(join_cfg()));
    CHECK(stage_mobile_offer(n, hal, /*mobile_hash=*/0x0000D1D1u) == 1);   // ★ armed as a legitimate host

    CHECK(init_gateway_hostile(n));                                   // becomes a gateway -> the cleanup runs
    CHECK(n.on_init(join_cfg()));                                     // ...and back to an ordinary single-layer host
    CHECK(n.can_host_mobiles());                                      // ★ ELIGIBLE at fire time ⇒ the boundary re-check PASSES and is inert here

    hal.tx_frames.clear();
    n.on_timer(80);                                                   // kMobileOfferBackoffTimerId
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                   // ★ ONLY the on_init cleanup can produce this zero
}

// TEST 11 — the register's gate item 6 half the round-1 tests never executed: the transition cleanup itself, driven
// through REAL `on_init` with a VALID gateway config (round 1's transition poked two config fields and deliberately
// left the registry in place, so the implemented cleanup had never run in any test).
TEST_CASE("★★ §B132b/5 — a SUCCESSFUL gateway on_init CLEARS the hosted-mobile registry (real on_init, not a poke)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node n(hal, /*node_id=*/5, /*key_hash32=*/0x00000005u);
    CHECK(n.on_init(join_cfg()));

    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(/*host=*/5, /*local=*/254, /*hash=*/0xF7C0F666u, cl);   // the bench's hash
    n.on_recv(cl.data(), cn, meta);
    CHECK(n.mobile_reg_count() == 1);                                 // ★ a GENUINE hosted entry, recorded while eligible

    CHECK(init_gateway_hostile(n));                                   // ★ REAL on_init with a valid gateway config
    CHECK(n.mobile_reg_count() == 0);                                 // ★ the registry is GONE — `routes` can no longer print `hosted-mobiles n=1` on a gateway
    CHECK_FALSE(n.can_host_mobiles());

    // and the roster path is doubly dead now: no entry to advertise AND the emit-side gate refuses.
    hal.events.clear(); hal.tx_frames.clear();
    n.on_timer(79);                                                   // kPresenceRosterTimerId
    CHECK(hal.count("presence_roster_tx") == 0);
    CHECK(hal.tx_frames.empty());
}
