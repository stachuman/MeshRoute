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
// ★★★ [[B145]] §MH-S2b — THE REAL DEVICE TIMER WHEEL, deliberately, not a second in-test model of one.
// `test_timer_wheel.cpp` already includes it exactly like this. The [[B145]] case below needs the PRODUCTION
// pump semantics (`pop_due(now)` fires everything with `_due <= now`, and `after()` REPLACES a deadline rather
// than stacking one), and re-implementing those in the fixture would reproduce the very mistake the case
// exists to catch: a harness whose control flow differs from production in the dimension under test.
#include "timer_wheel.h"

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
            // §MH-S1b: the OFFER emits (`mobile_offer_scheduled` at the stash, `mobile_offer_tx` at the
            // handoff) carry these two. Captured so a case can assert WHICH OFFER the event describes —
            // "an event of the right name fired" is not evidence that it described the right frame.
            int64_t to_key = -1; int64_t local_id = -1;
            // ★★ §MH-S4b — `mobile_attach_confirmed.reclaims`. It was STRUCTURALLY ALWAYS 0 (the counter was
            // cleared on the line above the emit), i.e. an instrument that could not fail, and no case captured
            // the field to notice. Captured here so the HEALED case can assert a NON-ZERO value — the presence
            // of a field is not evidence that it reports anything ([[B115]]'s lesson).
            int64_t reclaims = -1;
            bool i_win = false; bool has_iwin = false; std::string reason; };

class TestHal : public mrtest::TestHalBase {
public:
    std::vector<Ev> events;
    std::vector<std::vector<uint8_t>> tx_frames;

    // ★★ §MH-S1b (QA round 2) — THE HAL CAN NOW REFUSE, AND THAT IS THE POINT. Round 1 reported the
    // `tx_rejected` half of `TxAdmission` as METAL-ONLY because "`TestHal::tx` always answers `ok`". That was a
    // statement about THIS HARNESS, not about reachability: the capability was ONE FIELD away. `DeviceHal::tx`
    // answers `busy` when its 8-entry outbound ring is full and `too_long` past the SX1262 length register, and
    // `tx_with_retry` turns either into `TxHandOff::rejected` for a slot<0 frame (RTS/beacon — which is what a
    // J DISCOVER/CLAIM/OFFER is tagged as). ⇒ one settable field reproduces exactly that, natively.
    // ⛔ THE LESSON, RECORDED WHERE IT HAPPENED: before declaring something metal-only, ask what ONE parameter
    //    would make it testable. This is the inverse of the instruments-that-cannot-fail this arc is about —
    //    a capability declared UNREACHABLE when it was one field away.
    // ⓘ Defaults to `ok`, and a refused frame is NOT recorded in `tx_frames` (DeviceHal does not retain it
    //   either), so every pre-existing case in this TU is byte-identical — they never set it.
    TxResult tx_answer = TxResult::ok;
    int      tx_calls  = 0;                                          // attempts, refused or not (a refusal must still be an ATTEMPT)
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override {
        ++tx_calls;
        if (tx_answer != TxResult::ok) return tx_answer;             // refused: the HAL keeps nothing, so neither do we
        tx_frames.emplace_back(b, b + n); return TxResult::ok;
    }
    // §S0 (2026-08-07): RECORD the armed timers. TestHalBase::after() accepts and forgets, so no case in this TU could
    // read a DEADLINE — and two of the five §S0 reproductions are *about* a deadline (the un-jittered retry delay and
    // the claim guard armed before transmitter admission). Same seam as test_node_query / _hashlocate / _e2e_ack /
    // _channel already carry (U1: the shape is copied, not invented). Additive + returns the same `true`, so every
    // pre-existing case in this file is byte-identical — they simply never look.
    std::vector<std::pair<uint32_t, uint32_t>> armed;     // (delay_ms, timer_id), in call order
    // ★★★ [[B145]] §MH-S2b — OPT-IN REAL TIMER WHEEL, default OFF. `armed` is a CALL LOG, and a call log is not a
    // model of state: it cannot express that `Hal::after` REPLACES a deadline, and it cannot be DRAINED the way
    // `src/fw_main.cpp` drains one. Both of this round's findings came from that gap — [[B145]] (the same-pump
    // re-entry a one-callback-at-a-time fixture cannot see) and the [[B143]] disproof (`count_armed` counted
    // history and was read as "two guards are live"). Point `wheel` at a `TimerWheel` and this fixture becomes
    // the production pump; leave it null and every pre-existing case is byte-identical (`cancel` was already a
    // no-op and `after` already returned `true`).
    meshroute::TimerWheel* wheel = nullptr;
    bool     after(uint32_t delay, uint32_t id) override {
        armed.emplace_back(delay, id);
        if (wheel) (void)wheel->after(delay, id, _now);
        return true;
    }
    void     cancel(uint32_t id) override { if (wheel) wheel->cancel(id); }
    // The LAST delay armed for `id`, or -1 if that timer was never armed. (A deadline is a number, not a bool.)
    int64_t  last_armed(uint32_t id) const {
        int64_t d = -1; for (const auto& a : armed) if (a.second == id) d = static_cast<int64_t>(a.first); return d;
    }
    int      count_armed(uint32_t id) const {
        int c = 0; for (const auto& a : armed) if (a.second == id) ++c; return c;
    }
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
                else if (!std::strcmp(fl.key, "to_key"))           e.to_key = fl.i;
                else if (!std::strcmp(fl.key, "local_id"))         e.local_id = fl.i;
                else if (!std::strcmp(fl.key, "reclaims"))         e.reclaims = fl.i;   // §MH-S4b: how many re-CLAIMs healed this attachment
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

// ★★★ §MH-S4 §7.1 step 4 — DRIVE A REAL CONFIRMATION. Post-S4 a mobile becomes `attached` (and app-facing
// `registered`) ONLY when its CHOSEN HOME's roster carries its (hash, local id, epoch). This builds exactly that
// roster and hands it to `on_recv`, so every case that means "a fully registered mobile" now reaches that state
// through the same wire evidence the protocol uses, parsed by the production parser.
// ⛔ Deliberately NOT a white-box state poke: the whole point of the slice is that the TRIPLE on the wire is what
//    confirms, so a helper that set the flag directly would bypass the mechanism under test.
// ⓘ `home_layer` must be the mobile's live layer, or the roster is treated as ANOTHER home's (a candidate hint).
size_t make_p_roster_one(uint8_t home_id, uint8_t home_layer, uint32_t mobile_hash, uint8_t local_id,
                         uint8_t epoch, std::array<uint8_t, 64>& buf, uint8_t quality = protocol::presence_q_ok) {
    PRosterEntry e[1] = {{ mobile_hash, local_id, epoch, quality, /*has_key=*/false, /*deleg_fail=*/false }};
    p_roster_in ri{}; ri.home_id = home_id; ri.home_layer = home_layer;
    ri.wire_version = protocol::wire_version; ri.entries = e; ri.count = 1;
    return pack_p_roster(ri, std::span<uint8_t>(buf.data(), buf.size()));
}
void confirm_mobile_via_roster(Node& mob, TestHal& hal, uint8_t home_id, uint8_t home_layer,
                               uint32_t mobile_hash, uint8_t local_id, uint8_t epoch, const RxMeta& meta) {
    std::array<uint8_t, 64> rb{};
    const size_t rn = make_p_roster_one(home_id, home_layer, mobile_hash, local_id, epoch, rb);
    (void)hal;
    mob.on_recv(rb.data(), rn, meta);
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
        node.on_timer(kMobileClaimGuardTimerId);              // window close -> CLAIM + PROVISIONAL adopt
        CHECK(node.mobile_registered());                      // the LINK-LAYER/provisional flag (§7.1 step 1)
        // ★★ §MH-S4 §7.1 — AND THAT IS NOT YET A REGISTRATION. The deregistration push R2 is about is now paired
        // with the CONFIRMED attachment (`mobile_reset_registration` gates it on the attachment plane, so the
        // registered:true / registered:false pair is symmetric by construction). A `claiming` node therefore has
        // nothing to deregister — asserted explicitly below — so this case must first earn a real confirmation.
        CHECK(node.mobile_attach_state() == Node::MobileAttachState::claiming);
        CHECK_FALSE(node.mobile_attached());
        confirm_mobile_via_roster(node, hal, /*home=*/30, /*home_layer=*/node.config().layers[0].layer_id,
                                  /*hash=*/0x0000B7B7u, /*local=*/201, /*epoch=*/1, meta);
        CHECK(node.mobile_attached());                        // ★ §7.1 step 4: the roster triple confirmed it
        Push p{}; while (node.next_push(p)) {}                // drain the registration push(es)

        node.clear_routing_state();
        int dereg = 0, reg = 0;
        while (node.next_push(p)) if (p.kind == PushKind::mobile_reg) { if (!p.relayed) ++dereg; else ++reg; }
        CHECK(dereg == 1);                                    // ★ R2: exactly one registered:false push
        CHECK(reg == 0);
        CHECK_FALSE(node.mobile_registered());
        CHECK_FALSE(node.mobile_attached());
        CHECK(node.mobile_attach_state() == Node::MobileAttachState::dormant);   // §MH-S4: a verb reprovision drops the session
    }
    // (a2) ★★ §MH-S4 §7.1 — THE NEGATIVE CONTROL FOR (a), AND IT IS THE §S0-4 DEFECT AS A ONE-LINER: an
    // UNCONFIRMED (`claiming`) mobile's clear pushes NOTHING, because the app was never told it was registered.
    // This is what makes (a)'s `dereg == 1` a measurement of the confirmation rather than of `active`.
    {
        TestHal hal;
        Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B7B8u);
        NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true;
        node.on_init(mcfg);
        node.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(/*responder=*/30, /*resp_hash=*/0x0000C0C0u, /*local=*/202, /*target=*/0x0000B7B8u, off);
        node.on_recv(off.data(), on, meta);
        node.on_timer(kMobileClaimGuardTimerId);              // CLAIM + provisional adopt, NO roster ever arrives
        CHECK(node.mobile_registered());
        CHECK(node.mobile_attach_state() == Node::MobileAttachState::claiming);
        Push p{}; int reg_true = 0;
        while (node.next_push(p)) if (p.kind == PushKind::mobile_reg && p.relayed) ++reg_true;
        CHECK(reg_true == 0);                                 // ★★ no `registered:true` for an unconfirmed CLAIM
        node.clear_routing_state();
        int dereg = 0;
        while (node.next_push(p)) if (p.kind == PushKind::mobile_reg && !p.relayed) ++dereg;
        CHECK(dereg == 0);                                    // ★★ and therefore no unpaired `registered:false`
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
        CHECK(hal.count("mobile_offer_scheduled") == 0);            // ★ suspended: unprovisioned
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
        CHECK(hal.count("mobile_offer_scheduled") == 0);            // ★ suspended: mid-DAD
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
        CHECK(hal.count("mobile_offer_scheduled") == 1);            // ★ R1: !_joined would have wrongly refused this host
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
// half of the FSM is gated on registration_armed(). Team-DAD rides the SAME FSM tick BEFORE the gate, so a team member
// self-bootstraps its team id regardless of the toggle.
// ★★★ REWRITTEN IN PLACE BY §MH-S4 §4.2 (B101 — never deleted). This case previously asserted, as its whole point,
// that a manual `mobile register` was a ONE-SHOT: "still one — the arm is a ONE-SHOT, no auto re-DISCOVER". §4.2
// overturns exactly that sentence — *"an explicit manual request must not get one unconfirmed RF attempt and silently
// stop… it remains seeking/recovering until success or `mobile unregister`"* — so the assertions are inverted and the
// title now names the durable behaviour. What is UNCHANGED and still asserted is the half the ruling keeps: with
// autoregister OFF and NO request, the mobile is silent and `dormant`.
TEST_CASE("§autoregister — OFF: a lone mobile emits NO autonomous DISCOVER; a manual request is DURABLE (§MH-S4 §4.2)") {
    TestHal hal; hal._now = 100000;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B0B0u);
    NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.mobile_autoregister = false;
    node.on_init(mcfg);
    Push p{}; while (node.next_push(p)) {}
    CHECK(node.mobile_attach_state() == Node::MobileAttachState::dormant);   // ★ §4.2: auto OFF -> boot is DORMANT
    CHECK_FALSE(node.mobile_home_desired());
    node.on_timer(kMobileDiscoverTimerId);                    // the autonomous FSM kick — GATED
    CHECK(hal.count("mobile_discover_tx") == 0);              // ★ nothing on air (autoregister OFF, no request)
    CHECK(node.mobile_attach_state() == Node::MobileAttachState::dormant);   // ★ and the gated tick did NOT invent a `seeking`
    node.mobile_register_current();                           // the app REQUESTS home service (durable, not a one-shot)
    CHECK(node.mobile_home_desired());
    CHECK(node.mobile_attach_state() == Node::MobileAttachState::seeking);   // ★ §4.2: the request is who enters `seeking`
    node.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("mobile_discover_tx") == 1);              // ★ the first DISCOVER
    node.on_timer(kMobileDiscoverTimerId);                    // a later kick (the jittered no-host retry the FSM armed)
    CHECK(hal.count("mobile_discover_tx") == 2);              // ★★ §MH-S4 §4.2: TWO — the request is DURABLE (was pinned at 1 = the retired one-shot)
    node.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("mobile_discover_tx") == 3);              // ★ and it keeps going: "until success or `mobile unregister`"
    // ★★ AND `mobile unregister` IS THE THING THAT STOPS IT — the other half of §4.2, which the one-shot made
    // impossible to express. This is also the NEGATIVE CONTROL for the three DISCOVERs above: it proves they came
    // from the durable request and not from some ungated path that would keep firing regardless.
    node.mobile_unregister();
    CHECK_FALSE(node.mobile_home_desired());
    CHECK(node.mobile_attach_state() == Node::MobileAttachState::dormant);
    CHECK(node.mobile_home_link() == Node::MobileHomeLink::unknown);         // §4.1: "unknown — ... or the home-service state is dormant"
    node.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("mobile_discover_tx") == 3);              // ★★ silent again — exactly 3, no fourth
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

// ★★★ REWRITTEN IN PLACE BY §MH-S4 §4.2 (B101 — never deleted). The old title and its pinned `== 1` asserted "a
// failed manual arm does NOT auto-retry". That IS the §4.2 defect: a no-host round is exactly the failure a durable
// request must survive, and under the one-shot an operator's `mobile register` that met an empty room stopped for
// good. Both halves the ruling KEEPS are still asserted here, unchanged: the retry arms the DISCOVER timer (so the
// no-host backoff is now serviced), and the TEAM plane is untouched throughout (F-PS-1).
TEST_CASE("§autoregister — OFF team member: a failed manual request DOES retry (§MH-S4 §4.2); team plane stays alive") {
    TestHal hal; hal._now = 100000;
    Node node(hal, /*node_id=*/0, /*key_hash32=*/0x0000B2B2u);
    NodeConfig mcfg = join_cfg(); mcfg.is_mobile = true; mcfg.team_id = 0x7EA30000u; mcfg.mobile_autoregister = false;
    node.on_init(mcfg);
    Push p{}; while (node.next_push(p)) {}
    node.mobile_register_current();                           // REQUEST home service (durable)
    node.on_timer(kMobileDiscoverTimerId);                    // team-DAD + the first DISCOVER
    const uint8_t tid = node.team_local_id();
    CHECK(tid != 0);
    CHECK(hal.count("mobile_discover_tx") == 1);
    hal.armed.clear();                                        // ★ so the arm counted below can only be the no-host retry
    node.on_timer(kMobileClaimGuardTimerId);                  // window closes with NO offer -> no host
    CHECK(hal.count("mobile_no_host") == 1);
    // ★★ §MH-S4 §4.2 — THE NO-HOST BACKOFF IS NOW ARMED FOR A MANUALLY-REQUESTED SESSION. Pre-S4 this arm was
    // gated on `_cfg.mobile_autoregister` alone, so with auto OFF the retry timer was never scheduled at all.
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);      // ★ the retry EXISTS (was 0 — nothing was ever armed)
    node.on_timer(kMobileDiscoverTimerId);                    // that armed backoff re-DISCOVER
    CHECK(hal.count("mobile_discover_tx") == 2);              // ★★ a SECOND DISCOVER (was pinned at 1 = the retired one-shot)
    CHECK_FALSE(node.mobile_registered());                    //   still unregistered on the host plane
    CHECK(node.mobile_attach_state() == Node::MobileAttachState::seeking);   // ★ still `seeking`, never a false attach
    CHECK(node.team_local_id() == tid);                       // ★ team plane intact (F-PS-1) — the team id survives
    // NEGATIVE CONTROL for the retry: `mobile unregister` must stop it dead, proving the second DISCOVER above was
    // the durable request's and not an ungated autonomous path.
    node.mobile_unregister();
    node.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("mobile_discover_tx") == 2);              // ★ exactly two — no third
    CHECK(node.team_local_id() == tid);                       // ★★ and `mobile unregister` does NOT touch the team plane
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
// `MR_EMIT("mobile_offer_scheduled", …)` is emitted in node_join.cpp immediately BEFORE `jtx_stash_arm`, and its own comment
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
    return hal.count("mobile_offer_scheduled");
}

constexpr uint32_t kMobileOfferBackoffTimerIdT = 80;   // mirrors Node's private host OFFER de-storm / ring-scan timer

// ★★ §MH-S2 — ADVANCE THE CLOCK TO THE DEADLINE TIMER 80 WAS ARMED FOR, THEN FIRE IT. Read this before "fixing" any
// case back to a bare `on_timer(80)`.
//
// The OFFER stash used to be a single slot with no stored deadline: the timer firing WAS the deadline, so a fixture
// with a frozen clock could fire it and see the frame. §MH-S2 replaces it with a keyed ring whose entries each carry
// their OWN `due_ms`, served by ONE timer re-armed to the earliest — the `park_reflood` / `e2e_ack_deadline` idiom —
// so the scan transmits an entry only when that entry's own time has come. ⇒ a frozen-clock fire now (correctly)
// transmits nothing.
//
// ★ THIS IS THE FIXTURE CATCHING UP WITH REALITY, NOT A WORKAROUND: `Hal::after(delay, id)` fires the timer AT
// `now + delay` on metal and in the sim, which is exactly what advancing by the last armed delay reproduces. The
// cases are STRONGER for it — a deadline is now a number they pass through rather than an assumption.
// ⛔ Do NOT "fix" this in `lib/core` by firing the earliest armed entry whenever the timer fires regardless of its
//    due time: the timer also fires for RESERVATION expiry, so that would transmit an OFFER before its jitter
//    elapsed — the same-millisecond collision the jitter exists to prevent (C2: no unagreed fallback).
void fire_mobile_offer_timer(Node& host, TestHal& hal) {
    const int64_t d = hal.last_armed(kMobileOfferBackoffTimerIdT);
    if (d > 0) hal._now += static_cast<uint64_t>(d);
    host.on_timer(kMobileOfferBackoffTimerIdT);
}

// ★★★ [[B145]] §MH-S2b — THE PRODUCTION TIMER PUMP, COPIED FROM `src/fw_main.cpp:1069` AND NOT PARAPHRASED:
//     for (int id; (id = g_hal.pop_due_timer()) >= 0; ) { g_node.on_timer((uint32_t)id); … }
// with `DeviceHal::pop_due_timer()` being `_wheel.pop_due(_clock.now_ms())` (`lib/hal/device_hal.h:64`).
// ⚠ THE CLOCK IS NOT ADVANCED BY THIS LOOP, and that is the whole point: on metal `millis()` need not tick
// between two iterations, so a handler that re-arms its own timer with delay 0 is due again IMMEDIATELY
// (`pop_due` fires on `_due <= now_ms`) and runs again inside the same pass. `fire_mobile_offer_timer` above
// cannot express that — it invokes ONE callback per call, which models a pump that does not exist.
// ⓘ `cap` bounds a runaway rather than hiding one: the caller asserts the OBSERVABLE (frames on the wire), so a
//   build that spins here fails on the frame count, not by hanging the suite.
int pump_due_timers(Node& n, TestHal& hal, meshroute::TimerWheel& w,
                    std::vector<int>* fired_ids = nullptr, int cap = 64) {
    int fired = 0;
    while (fired < cap) {
        const int id = w.pop_due(hal._now);
        if (id < 0) break;
        if (fired_ids) fired_ids->push_back(id);
        n.on_timer(static_cast<uint32_t>(id));
        ++fired;
    }
    return fired;
}
// How many times `id` fired in a recorded pump.
int fired_count(const std::vector<int>& ids, uint32_t id) {
    int c = 0; for (int v : ids) if (v == static_cast<int>(id)) ++c; return c;
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
    CHECK(hal.count("mobile_offer_scheduled") == 0);                         // ★ THE DEFECT: was 1 — a strong-SNR gateway offered and won

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
    CHECK(hal.count("mobile_offer_scheduled") == 1);                         // ★ the fix did NOT break ordinary hosting

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
        CHECK(h2.count("mobile_offer_scheduled") == 0);                      // B3 opt-out preserved by the shared invariant
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
        CHECK(h3.count("mobile_offer_scheduled") == 0);                      // ★ a mobile registers to a home; it is not one
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

    CHECK(hgw.count("mobile_offer_scheduled") == 0);                         // ★ the gateway is silent
    CHECK(hst.count("mobile_offer_scheduled") == 1);                         // ★ the static host answers
    // ★ the DISCRIMINATION stated as an assertion rather than left to the reader: the two nodes received
    // BYTE-IDENTICAL stimuli at identical SNR and both had `host_mobiles` true, so any difference in outcome is
    // eligibility and nothing else.
    CHECK(hgw.count("mobile_offer_scheduled") != hst.count("mobile_offer_scheduled"));
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
        CHECK(hal.count("mobile_offer_scheduled") == 0);                     // ★ RED if `!is_gateway` is dropped
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
        CHECK(hal.count("mobile_offer_scheduled") == 0);                     // ★ RED if `n_layers == 1` is dropped
    }
}

// ============================================================================
// ★★★ §B132b — THE DELAYED-TRANSMISSION HOLE, AND WHY ROUND 1's TESTS COULD NOT SEE IT.
//
// ⛔ THE HOLE. The OFFER is not transmitted where it is decided. `handle_j`'s DISCOVER responder builds it, emits
// `mobile_offer_scheduled`, and hands it to `jtx_stash_arm` (the §S6/QA-3b de-storm), which arms
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
// ★★ 2026-08-07 §MH-S1b CLOSED THE NAMING HALF: the staging emit is now `mobile_offer_scheduled` (its honest
// meaning) and `mobile_offer_tx` is raised in `lbt_complete` at the ACCEPTED HANDOFF, with `mobile_offer_dropped`
// on a refusal. Every assertion below was RENAMED IN PLACE, not deleted — each still asserts exactly what it did
// (the COMMIT), under the name that now says so. ⚠ The rule the cases embody is UNCHANGED and still binding: the
// verdict is the PARSED FRAME on the wire; an event — even the honest one — is only ever a premise here.
// ⇒ every case below asserts the PARSED J OFFER FRAME in `hal.tx_frames`; the event appears only as a premise.
// ⚠ AND NOT `hal.cancel`: TestHalBase::cancel() is a NO-OP, so the cancels in `mobile_host_pending_clear()` cannot
// carry any assertion here. Each case FIRES timer 80 and reads the wire.
// ============================================================================

// TEST 7 — ★★ THE DISTINCTION ITSELF, PINNED. Then the regression: transition, fire, nothing on the wire.
TEST_CASE("★★★ §B132b/1 — `mobile_offer_scheduled` means COMMITTED, not TRANSMITTED; a gateway transition kills the staged OFFER") {
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
    fire_mobile_offer_timer(n, hal);                                  // kMobileOfferBackoffTimerId (§MH-S2: at its deadline)
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                  // ★★ THE DEFECT: the OFFER went out FROM THE GATEWAY, advertising exactly the invalid home §B132 exists to prevent
    CHECK(hal.tx_frames.empty());                                     // and nothing else was substituted for it either

    // (4) a re-fire must stay silent too (the stash is gone, not merely skipped once).
    fire_mobile_offer_timer(n, hal);
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
    fire_mobile_offer_timer(n, hal);                                  // kMobileOfferBackoffTimerId (§MH-S2: at its deadline)
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
    fire_mobile_offer_timer(n, hal);                                  // the entry is consumed -> a re-fire must not duplicate
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
    fire_mobile_offer_timer(n, hal);                                  // kMobileOfferBackoffTimerId (§MH-S2: at its deadline)
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
    fire_mobile_offer_timer(n, hal);                                  // kMobileOfferBackoffTimerId (§MH-S2: at its deadline)
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

// ============================================================================
// ★★★ §S0 — CHARACTERIZATION REPRODUCTIONS FOR THE MOBILE↔HOME ATTACHMENT DEFECTS
// Spec: docs/superpowers/specs/2026-08-07-mobile-home-attachment-reliability-design.md §11 "S0".
//
// ⛔⛔ READ THIS BEFORE "FIXING" ANY TEST BELOW. Every `§S0-n` case in this block ASSERTS TODAY'S
// DEFECTIVE BEHAVIOUR ON PURPOSE. They are GREEN because the defect is present. They are NOT a
// statement that the behaviour is correct — each one names the defect, the spec section that rules
// against it, and the slice that must REWRITE it.
//
// ★ WHY CHARACTERIZATION AND NOT A RED TEST. The ORIGINAL S0 DRAFT asked for "failing native tests" — the
// CURRENT spec (§11 S0) requires exactly what is below: green, clearly-labelled characterization tests that
// are mutation-proven capable of failing, rewritten in place by the owning fix, never deleted or disabled. But
// `pio test -e native` is the committed gate (D1) and a committed red test is not acceptable: it
// would have to be skipped or disabled, and a disabled test is exactly the instrument that cannot
// fail. This arc has already shipped FOURTEEN instruments that could not fail, which is the whole
// reason S0 exists. So each reproduction is inverted: it pins the WRONG answer, with the RIGHT
// answer written beside it.
//
// ★★ EACH CASE IS THEREFORE THE MUTATION CONTROL, IN REVERSE. When the corresponding fix lands, the
// case MUST go RED. That is the proof the reproduction was really exercising the defective branch —
// the property a test written *after* a fix can never demonstrate. The fixing slice REWRITES the
// assertions in place so the behaviour change is visible in the diff (the B101 precedent, already
// used twice in this arc). ⛔ DO NOT DELETE ONE. A deleted characterization test destroys exactly
// the evidence the rewrite exists to show.
//
// ★ WHAT EVERY CASE ASSERTS: the OBSERVABLE SIDE EFFECT — the parsed frame in `hal.tx_frames`, the
// roster contents, the actual armed timer deadline. NOT an internal flag and NOT an MR_EMIT event.
// This arc's FIFTH "success that isn't" was `erase()` answering `erased` while the record stayed
// readable; the FOURTH was `mobile_offer_tx` meaning *staged*, not *sent* — §MH-S1b renamed that emit to
// `mobile_offer_scheduled` and moved the honest `mobile_offer_tx` to the handoff (see the §B132b header
// above). An event or a bool is not evidence. Events appear below only as PREMISES ("the code under
// test was actually reached"), never as the verdict.
// ============================================================================

namespace {

// Count J frames of one opcode among captured TX. (`count_j_offer_mobile` above is the OFFER-only,
// is_mobile-qualified twin; these two are the DISCOVER/CLAIM siblings the §S0 cases need.)
int count_j_opcode(const std::vector<std::vector<uint8_t>>& frames, j_opcode op) {
    int c = 0;
    for (const auto& f : frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(op)) ++c; }
    return c;
}
// The first J frame of `op` among captured TX (nullopt if none) — so a case can assert the frame's FIELDS.
std::optional<j_out> first_j(const std::vector<std::vector<uint8_t>>& frames, j_opcode op) {
    for (const auto& f : frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(op)) return p; }
    return std::nullopt;
}
// The LAST J frame of `op` among captured TX — the §MH-S4 re-CLAIM twin of `first_j`. A case that drives several
// deadlines must assert the MOST RECENT frame's fields, not the first: `first_j` would keep re-reading the original
// CLAIM and would pass even if every re-CLAIM carried a fresh epoch, which is exactly the property under test.
std::optional<j_out> last_j(const std::vector<std::vector<uint8_t>>& frames, j_opcode op) {
    std::optional<j_out> found;
    for (const auto& f : frames) { auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (p && p->opcode == static_cast<uint8_t>(op)) found = p; }
    return found;
}
// An H (hash-locate) query from `origin` for `key_hash32`. Mirrors `make_h` in test_node_hashlocate.cpp;
// per-TU frame builders are this suite's existing convention (`make_beacon` is likewise duplicated there).
size_t make_h_query(uint8_t origin, uint32_t key_hash32, uint8_t ttl, std::array<uint8_t, 16>& buf) {
    h_in in{}; in.leaf_id = 0; in.origin = origin; in.query_key32 = key_hash32; in.ttl = ttl;
    return pack_h(in, std::span<uint8_t>(buf.data(), buf.size()));
}
int count_h_frames(const std::vector<std::vector<uint8_t>>& frames) {
    int c = 0; for (const auto& f : frames) if (parse_h(std::span<const uint8_t>(f.data(), f.size()))) ++c;
    return c;
}
// §F-XL-1: an H FORWARD is stashed + released by a jittered timer (kHForwardTimerId 81 + slot, 4 slots).
// The in-memory Hal never auto-fires, so a case that asks "was the flood forwarded?" must drive them.
void fire_h_forwards(Node& n) { for (uint32_t id = 81; id < 81 + 4; ++id) n.on_timer(id); }
// The roster entry for `mobile_hash` in the first P-roster among captured TX (nullopt: no roster, or absent).
std::optional<PRosterEntry> roster_entry_for(const std::vector<std::vector<uint8_t>>& frames, uint32_t mobile_hash) {
    for (const auto& f : frames) {
        auto r = parse_p_roster(std::span<const uint8_t>(f.data(), f.size()));
        if (!r) continue;
        for (uint8_t i = 0; i < r->count; ++i) {
            auto e = parse_p_roster_entry(std::span<const uint8_t>(f.data(), f.size()), *r, i);
            if (e && e->key_hash32 == mobile_hash) return e;
        }
    }
    return std::nullopt;
}
int count_p_rosters(const std::vector<std::vector<uint8_t>>& frames) {
    int c = 0; for (const auto& f : frames) if (parse_p_roster(std::span<const uint8_t>(f.data(), f.size()))) ++c;
    return c;
}
// ★ §MH-S4b §7.1 step 3 — the LAST P PROBE on the wire, PARSED. The `searching` bit is the whole substance of the
// finding (a SELECTED probe is required to be IGNORED by a home holding no row for us — `presence_ingest_probe`),
// so it must be read off the FRAME, never off an emit field the producer chose.
std::optional<p_probe_out> last_p_probe(const std::vector<std::vector<uint8_t>>& frames) {
    std::optional<p_probe_out> last;
    for (const auto& f : frames) if (auto p = parse_p_probe(std::span<const uint8_t>(f.data(), f.size()))) last = p;
    return last;
}
int count_p_probes(const std::vector<std::vector<uint8_t>>& frames) {
    int c = 0; for (const auto& f : frames) if (parse_p_probe(std::span<const uint8_t>(f.data(), f.size()))) ++c;
    return c;
}
// A mobile leaf config (single PHY, LBT off) — the §S0 mobile-side twin of `join_cfg()`.
NodeConfig s0_mobile_cfg() { NodeConfig c = join_cfg(); c.is_mobile = true; return c; }

constexpr uint32_t kMobileOfferBackoffTimerId = 80;   // mirrors Node's private host OFFER de-storm timer (node.h:995)
constexpr uint32_t kPresenceProbeTimerId      = 78;   // mirrors Node's private presence check/retry timer (node.h:993)
constexpr uint32_t kPresenceRosterTimerId     = 79;   // mirrors Node's private roster-coalesce timer (node.h:994)
constexpr uint32_t kLbtDeferTimerId           = 15;   // mirrors Node's private LBT defer ring BASE (node.h:964)

}  // namespace

// ---------------------------------------------------------------------------
// §S0-1 — REWRITTEN IN PLACE BY §MH-S2 (B101: a characterization test is rewritten by its owning fix, NEVER
// deleted or disabled, so the behaviour change is visible in the diff).
//
// WHAT IT USED TO PIN (spec §2.2, and the words are kept so the diff reads as a behaviour change): the host's
// OFFER stash was ONE 13-byte `LayerRuntime::_pending_offer` slot armed through `jtx_stash_arm`, and
// `kMobileOfferBackoffTimerId` was ONE timer with ONE deadline. A second mobile's DISCOVER arriving inside the
// host's 100..1000 ms OFFER jitter OVERWROTE the first mobile's targeted OFFER outright: exactly one OFFER left,
// it belonged to the LAST DISCOVER, and the first mobile was never answered at all. The case asserted
// `count_j_offer_mobile(...) == 1` and `target_key_hash32 == kMobB`, and it went RED when this slice landed —
// measured, not predicted: `2 == 1` and `43537 == 47906` on the pre-rewrite tree.
//
// ★★ AND IT PINNED A SECOND DEFECT IT COULD ONLY OBSERVE, NOT SEPARATE — [[B137]]. The surviving OFFER proposed
// local id 254 and so did the one it destroyed, because `find_free_mobile_id` scanned `_mobile_reg` only and a
// staged OFFER is not a registry row. THE TWO DEFECTS MASKED EACH OTHER: with one OFFER ever flying, "everyone is
// offered the same id" is unobservable. S2 removes the mask, so this rewrite is the FIRST thing that ever
// exercises the concurrent-id path — which is why it must DRIVE the allocation (four real CLAIMs on the wire)
// rather than assume it.
//
// WHAT IT PINS NOW (spec §5.3.2/§5.3.3, gate items 1/17/18/19 + the [[B137]] ruling):
//   • FOUR concurrent DISCOVERs ⇒ FOUR targeted OFFERs, one per mobile, no armed entry overwritten;
//   • the four proposed local ids are DISTINCT — and the four CLAIMs that follow are all accepted with NOT ONE
//     DENY, because the targeted CLAIM-collision DENY is a race backstop and never the allocator;
//   • at most ONE OFFER per timer callback (the de-storm survives the ring);
//   • a duplicate DISCOVER coalesces, a full ring refuses, a reservation expires — the three cases below it.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §S0-1 -> §MH-S2 (spec §5.3.2) — FOUR concurrent DISCOVERs get FOUR OFFERs with FOUR DISTINCT ids, and NO DENY") {
    constexpr uint32_t kMobA = 0x0000AA11u;
    constexpr uint32_t kMobB = 0x0000BB22u;
    constexpr uint32_t kMobC = 0x0000CC33u;
    constexpr uint32_t kMobD = 0x0000DD44u;
    constexpr uint8_t  kHostId = 42;

    // ---- POSITIVE CONTROL FIRST, KEPT VERBATIM IN SPIRIT FROM THE CHARACTERIZATION. Without it, every
    // "N OFFERs appeared" below is satisfiable by a harness that cannot see an OFFER at all. A alone MUST be
    // answered, and its id must be the top-down 254 the allocator has always produced.
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(join_cfg()));
        CHECK(host.can_host_mobiles());
        CHECK(stage_mobile_offer(host, hal, kMobA) == 1);
        CHECK(host.mobile_offers_pending_n() == 1);                 // ★ one slot in use, one reservation live
        hal.tx_frames.clear();
        fire_mobile_offer_timer(host, hal);
        CHECK(count_j_offer_mobile(hal.tx_frames) == 1);            // ★ the harness CAN see A's OFFER...
        auto o = first_j(hal.tx_frames, j_opcode::offer);
        CHECK(o.has_value());
        if (o) { CHECK(o->target_key_hash32 == kMobA);              // ★ ...addressed at A...
                 CHECK(o->proposed_mobile_id == 254); }             // ★ ...with the unchanged top-down id
        // ★ AND THE RESERVATION OUTLIVES THE TRANSMISSION — that residual IS [[B137]]. The slot is still in use
        // after the frame has flown, because the CLAIM has not arrived yet.
        CHECK(host.mobile_offers_pending_n() == 1);
    }

    // ---- THE FIX, DRIVEN END TO END.
    TestHal hal; hal._now = 100000;
    Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    // PREMISE — all four DISCOVERs were RECEIVED and all four OFFERs were COMMITTED, measured as a TRANSITION
    // interleaved with the staging exactly as the characterization did it. A single count at the end cannot tell
    // "four armed" from "one armed four times", and the overwrite was the defect, so the accumulation is the point.
    // (`stage_mobile_offer` clears `events`/`tx_frames` but deliberately NOT `armed`.)
    // ⚠ The clock steps 1 ms per DISCOVER so the four jitter deadlines are DISTINCT — `TestHal` returns the window
    //   floor for every draw, so without this they would tie and the "earliest first" ordering would be arbitrary.
    const uint32_t kMobs[4] = { kMobA, kMobB, kMobC, kMobD };
    hal.armed.clear();
    CHECK(hal.count_armed(kMobileOfferBackoffTimerId) == 0);        // baseline: nothing has armed 80 yet
    for (int i = 0; i < 4; ++i) {
        hal._now = 100000 + static_cast<uint64_t>(i);
        CHECK(stage_mobile_offer(host, hal, kMobs[i]) == 1);        // committed...
        CHECK(hal.count_armed(kMobileOfferBackoffTimerId) == i + 1);// ...and each one armed the ONE timer
        CHECK(host.mobile_offers_pending_n() == i + 1);             // ★★ AND TOOK ITS OWN SLOT — the old build
                                                                    //    would have overwritten the previous one
    }
    CHECK(host.mobile_offer_ring_full_count() == 0);                // ★ four of eight: nothing was refused

    // ★★ THE FIX, ON THE WIRE — and the "at most ONE due OFFER per callback" contract (§5.3.3) is asserted as a
    // TRANSITION too, not as a total: four callbacks, one frame each. A build that flushed the whole ring in one
    // callback would satisfy a final count of 4 and would put four OFFERs into the same millisecond, which is
    // precisely what the jitter exists to prevent.
    hal.tx_frames.clear();
    for (int i = 0; i < 4; ++i) {
        fire_mobile_offer_timer(host, hal);
        CHECK(count_j_offer_mobile(hal.tx_frames) == i + 1);
    }
    // ...and the ring is spent: further callbacks add nothing (no duplicate transmission on a re-fire).
    fire_mobile_offer_timer(host, hal);
    fire_mobile_offer_timer(host, hal);
    CHECK(count_j_offer_mobile(hal.tx_frames) == 4);

    // ★★★ THE VERDICT IS THE PARSED FRAMES, NEVER A COUNTER: each of the four mobiles is addressed exactly once,
    // and the four PROPOSED IDS ARE DISTINCT. The scan asserts its own match count, so a loop that inspected
    // nothing cannot satisfy "no duplicates" vacuously.
    int    inspected = 0;
    int    seen_target[4] = {0, 0, 0, 0};
    uint8_t offered_id[4] = {0, 0, 0, 0};
    for (const auto& f : hal.tx_frames) {
        auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (!p || p->opcode != static_cast<uint8_t>(j_opcode::offer)) continue;
        ++inspected;
        CHECK(p->responder_node_id == kHostId);
        for (int i = 0; i < 4; ++i)
            if (p->target_key_hash32 == kMobs[i]) { ++seen_target[i]; offered_id[i] = p->proposed_mobile_id; }
    }
    CHECK(inspected == 4);                                          // ★ the scan's MATCH COUNT
    for (int i = 0; i < 4; ++i) CHECK(seen_target[i] == 1);         // ★ every mobile answered EXACTLY once
    for (int i = 0; i < 4; ++i) CHECK(offered_id[i] >= protocol::normal_node_id_min);
    for (int i = 0; i < 4; ++i)
        for (int k = i + 1; k < 4; ++k)
            CHECK(offered_id[i] != offered_id[k]);                  // ★★★ [[B137]]: FOUR UNIQUE IDS. The old build
                                                                    //     offered 254 to all four.

    // ★★★ "AT MOST ONE PER CALLBACK" WITH EVERY DEADLINE ALREADY ELAPSED — and this block exists BECAUSE THE
    // MUTATION BATTERY SAID SO. The staggered loop above cannot detect a fire that drains the whole ring: with
    // deadlines 1 ms apart, only one entry is ever due at the instant the timer fires, so "drain everything due"
    // and "transmit exactly one" are indistinguishable there (measured — M-S2-5 reddened nothing until this).
    // ⇒ four fresh mobiles, then the clock jumped PAST all four deadlines, then ONE callback.
    //
    // ⛔⛔ AND HERE IS WHAT THIS BLOCK STILL CANNOT SEE, STATED IN PLACE RATHER THAN DISCOVERED AGAIN — [[B145]].
    // It invokes `on_timer(80)` BY HAND, ONE CALL AT A TIME, so "one OFFER per callback" is all it can measure.
    // Production does not call `on_timer` one at a time: `src/fw_main.cpp:1069` DRAINS every due timer against a
    // clock that need not have advanced, and a handler re-arming itself with delay 0 is due again immediately.
    // ⇒ this block stayed GREEN on a build that put all four OFFERs into one millisecond. It is kept (it is a true
    // statement about the callback, and it is what M-S2-5 needs) and the missing dimension is covered by the
    // REAL-`TimerWheel` case that follows this one. ⛔ Do NOT delete either in favour of the other.
    {
        TestHal h2; h2._now = 500000;
        Node h(h2, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(h.on_init(join_cfg()));
        const uint32_t late[4] = { 0x00051111u, 0x00052222u, 0x00053333u, 0x00054444u };
        for (int i = 0; i < 4; ++i) {
            h2._now = 500000 + static_cast<uint64_t>(i);
            CHECK(stage_mobile_offer(h, h2, late[i]) == 1);
        }
        CHECK(h.mobile_offers_pending_n() == 4);                    // PREMISE: four armed entries
        h2._now = 500000 + protocol::join_offer_backoff_max_ms + 50;// ★ every one of the four is now OVERDUE
        h2.tx_frames.clear();
        h.on_timer(kMobileOfferBackoffTimerId);
        CHECK(count_j_offer_mobile(h2.tx_frames) == 1);             // ★★★ still exactly ONE — the de-storm holds
        h.on_timer(kMobileOfferBackoffTimerId);
        CHECK(count_j_offer_mobile(h2.tx_frames) == 2);             // ★ and the next callback takes the next one
    }

    // ★★★ AND THE GATE DRIVES THE ALLOCATION RATHER THAN ASSUMING IT (spec §12.1 gate 1): all four mobiles now
    // CLAIM the ids they were offered. Every one must be RECORDED, and ⛔ NOT ONE DENY MAY BE EMITTED — a DENY
    // here would mean the reservation failed and the race backstop was doing the allocator's job, which the owner
    // ruling forbids. This is the assertion that "four unique ids" cannot be faked past.
    hal.events.clear(); hal.tx_frames.clear();
    RxMeta cmeta{8.0f, -80.0f, 0, -1};
    for (int i = 0; i < 4; ++i) {
        std::array<uint8_t, 16> cl{};
        const size_t cn = make_j_claim_mobile(kHostId, offered_id[i], kMobs[i], cl);
        host.on_recv(cl.data(), cn, cmeta);
    }
    CHECK(hal.count("mobile_registered") == 4);                     // ★★ all four hosted
    CHECK(hal.count("mobile_id_collision_deny") == 0);              // ★★★ zero collisions...
    CHECK(hal.count("join_deny_sent") == 0);                        // ★★★ ...and zero DENY frames built
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);      // ★★★ ...and none on the wire either
    CHECK(host.mobile_reg_count() == 4);                           // ★ four distinct rows, not one overwritten row
    // ★ THE CLAIM RELEASES THE RESERVATION (it does not wait out `mobile_offer_reservation_ms`): the registry row
    // takes over as the thing that holds the id, and the ring is empty again for the next four mobiles.
    CHECK(host.mobile_offers_pending_n() == 0);
}

// ---------------------------------------------------------------------------
// ★★★ [[B145]] §MH-S2b — THE SAME-MILLISECOND OFFER BURST, MEASURED THROUGH THE **REAL** TIMER WHEEL.
//
// ⛔ THE DEFECT. `mobile_offer_fire` transmits at most one due entry and then calls `mobile_offer_arm_timer`,
// which re-armed a still-overdue remainder with a delay of **ZERO**. `TimerWheel::pop_due` fires on
// `_due <= now`, and the production pump (`src/fw_main.cpp:1069`) keeps popping at a clock it re-reads but
// which need not have moved ⇒ the callback re-entered inside the SAME pass and four overdue entries reached
// the radio in one millisecond — precisely the burst the 100..1000 ms jitter exists to prevent. The
// one-per-callback rule was true and bought nothing.
//
// ⛔⛔ WHY NO EXISTING CASE COULD FAIL. Every §MH-S2 case drives `on_timer(80)` by hand, one call at a time,
// through a fixture whose `after()` only APPENDS TO A CALL LOG. That models a pump that does not exist, in
// exactly the dimension under test. ⇒ this case attaches a REAL `meshroute::TimerWheel` (the production type,
// not a re-implementation) and runs the production drain loop verbatim at a FIXED timestamp.
//
// ★ THE ASSERTIONS ARE THE WIRE AND THE WHEEL, never a counter or a flag: how many OFFER frames a full drain
// produced, and whether timer 80's deadline afterwards is STRICTLY IN THE FUTURE. The second is the fix stated
// as a property — a deadline at `now` IS the defect, whatever the frame count happened to be.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ [[B145]] §MH-S2b — a REAL TimerWheel drain at ONE fixed timestamp emits exactly ONE OFFER") {
    constexpr uint8_t kHostId = 42;
    const uint32_t kMobs[4] = { 0x00061111u, 0x00062222u, 0x00063333u, 0x00064444u };

    meshroute::TimerWheel wheel;
    TestHal hal; hal._now = 500000; hal.wheel = &wheel;               // ★ the fixture IS the device pump now
    Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    for (int i = 0; i < 4; ++i) {
        hal._now = 500000 + static_cast<uint64_t>(i);                 // distinct deadlines, as the case above
        CHECK(stage_mobile_offer(host, hal, kMobs[i]) == 1);
    }
    CHECK(host.mobile_offers_pending_n() == 4);                       // PREMISE: four armed entries
    CHECK(wheel.active(kMobileOfferBackoffTimerId));                  // PREMISE: the WHEEL really holds timer 80

    // ★ Every one of the four deadlines is now in the past, and the clock will NOT move during the drain.
    hal._now = 500000 + protocol::join_offer_backoff_max_ms + 50;
    hal.tx_frames.clear();
    std::vector<int> ids;
    const int fired = pump_due_timers(host, hal, wheel, &ids);

    CHECK(fired >= 1);                                                // PREMISE: the pump is not inert
    // ★★★ THE VERDICT. A full production drain at ONE timestamp put exactly ONE OFFER on the air, and timer 80
    // was entered exactly ONCE. The pre-fix build read `4 == 1` on both — four frames, one millisecond.
    CHECK(count_j_offer_mobile(hal.tx_frames) == 1);
    CHECK(fired_count(ids, kMobileOfferBackoffTimerId) == 1);
    // ★★★ AND THE MECHANISM, ASSERTED SEPARATELY SO THE COUNT CANNOT BE SATISFIED BY ACCIDENT: the remainder
    // is re-armed STRICTLY IN THE FUTURE. A zero-delay re-arm leaves `due_at == now`, which `pop_due` treats
    // as due — that equality IS [[B145]].
    CHECK(wheel.active(kMobileOfferBackoffTimerId));
    CHECK(wheel.due_at(kMobileOfferBackoffTimerId) > hal._now);
    CHECK(wheel.due_at(kMobileOfferBackoffTimerId)
          == hal._now + protocol::mobile_offer_respace_ms);           // ★ the CONSTANT, not a draw
    CHECK(host.mobile_offers_pending_n() == 4);                       // ★ nothing was dropped — 3 armed + 4 reservations

    // ★★ AND TIME RELEASES THEM ONE AT A TIME, not in a clump: each respace tick yields exactly one more OFFER.
    for (int i = 2; i <= 4; ++i) {
        hal._now += protocol::mobile_offer_respace_ms;
        CHECK(pump_due_timers(host, hal, wheel) >= 1);
        CHECK(count_j_offer_mobile(hal.tx_frames) == i);
    }
    // ★ ...and the four went to four DIFFERENT mobiles with four DISTINCT ids — the respace re-orders nothing.
    int inspected = 0, seen[4] = {0, 0, 0, 0};
    uint8_t got[4] = {0, 0, 0, 0};
    for (const auto& f : hal.tx_frames) {
        auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (!p || p->opcode != static_cast<uint8_t>(j_opcode::offer) || !p->is_mobile) continue;
        ++inspected;
        for (int i = 0; i < 4; ++i)
            if (p->target_key_hash32 == kMobs[i]) { ++seen[i]; got[i] = p->proposed_mobile_id; }
    }
    CHECK(inspected == 4);                                            // ★ the scan's MATCH COUNT
    for (int i = 0; i < 4; ++i) CHECK(seen[i] == 1);
    for (int i = 0; i < 4; ++i)
        for (int k = i + 1; k < 4; ++k) CHECK(got[i] != got[k]);

    // ---- ⛔ NEGATIVE CONTROL — THE PUMP CAN FIRE MORE THAN ONCE IN A PASS, so the `== 1` above is a real
    // constraint and not an artefact of a drain that only ever pops once. Two UNRELATED timers armed for the
    // same instant are both drained by the identical loop.
    {
        meshroute::TimerWheel w2;
        TestHal h2; h2._now = 1000; h2.wheel = &w2;
        Node n2(h2, /*node_id=*/43, /*key_hash32=*/0x00004343u);
        CHECK(n2.on_init(join_cfg()));
        w2.after(0, kMobileOfferBackoffTimerId, h2._now);
        w2.after(0, kPresenceRosterTimerId,     h2._now);
        std::vector<int> nids;
        (void)pump_due_timers(n2, h2, w2, &nids);
        CHECK(fired_count(nids, kMobileOfferBackoffTimerId) == 1);    // ★ BOTH ran in ONE pass at ONE clock —
        CHECK(fired_count(nids, kPresenceRosterTimerId)     == 1);    //   the pump is genuinely a multi-fire drain
    }
}

// ---------------------------------------------------------------------------
// ★★★ [[B147]] §MH-S2b — A CLAIM MUST MATCH ITS RESERVATION ON **BOTH** HASH AND ID.
//
// ⛔ THE DEFECT. The mobile CLAIM handler checked the claimed id against REGISTERED rows only, then recorded
// whatever `proposed_node_id` the frame carried and released "the claimant's" reservation BY HASH ALONE. A
// reservation is a promise about a (hash, id) PAIR and half of it was never read ⇒ A is offered X and lets its
// reservation lapse · X is re-promised to B · A's DELAYED CLAIM for X arrives · no registered row holds X yet,
// so A is recorded on B's reserved id — and B's own CLAIM then walks straight into the collision-DENY recovery
// that [[B137]] exists to make unnecessary. Same category error as `LbtKind` alone ([[B142]]).
//
// ★ Each arm asserts an OBSERVABLE: the registry contents, the DENY (or its absence) parsed off the wire, and
// whether B's later CLAIM is clean. ⛔ Not one assertion reads the ring's internals.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ [[B147]] §MH-S2b — a delayed CLAIM cannot take an id RESERVED for another mobile") {
    constexpr uint8_t  kHostId = 42;
    constexpr uint32_t kMobA   = 0x0000A101u;
    constexpr uint32_t kMobB   = 0x0000B202u;
    RxMeta cmeta{8.0f, -80.0f, 0, -1};

    // Offer `mob` an id and TRANSMIT it; returns the proposed local id read off the wire.
    auto offer_and_read_id = [&](Node& h, TestHal& hl, uint32_t mob) -> uint8_t {
        CHECK(stage_mobile_offer(h, hl, mob) == 1);
        hl.tx_frames.clear();
        fire_mobile_offer_timer(h, hl);
        auto o = first_j(hl.tx_frames, j_opcode::offer);
        CHECK(o.has_value());
        return o ? o->proposed_mobile_id : 0;
    };

    // ---- ARM 1 — THE HEADLINE: A's reservation LAPSES, the id is re-promised to B, A's delayed CLAIM arrives.
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(join_cfg()));
        CHECK(host.can_host_mobiles());

        const uint8_t idA = offer_and_read_id(host, hal, kMobA);
        CHECK(idA >= protocol::normal_node_id_min);
        // A never CLAIMs. Run the clock past the reservation and let the scan expire it.
        hal._now += protocol::mobile_offer_reservation_ms + 1;
        host.on_timer(kMobileOfferBackoffTimerId);
        CHECK(host.mobile_offers_pending_n() == 0);                   // PREMISE: A's promise is gone

        const uint8_t idB = offer_and_read_id(host, hal, kMobB);
        CHECK(idB == idA);                                            // ★★ PREMISE: the SAME id was re-promised, to B
        CHECK(host.mobile_offers_pending_n() == 1);                   // ★ and it is a LIVE promise

        // ★ A's DELAYED CLAIM for the id it was offered a reservation-lifetime ago.
        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> cl{};
        const size_t cn = make_j_claim_mobile(kHostId, idA, kMobA, cl);
        host.on_recv(cl.data(), cn, cmeta);

        // ★★★ THE VERDICT: A is NOT recorded on B's reserved id...
        CHECK(host.mobile_reg_count() == 0);
        CHECK(hal.count("mobile_registered") == 0);
        // ...it is REPORTED rather than silently dropped...
        CHECK(hal.count("mobile_claim_reserved_elsewhere") == 1);
        // ...and it is TARGETED-DENIED so it re-DISCOVERs onto a fresh id (the same backstop shape the
        // registered-row collision uses), with the RESERVATION HOLDER named as the owner.
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 1);
        auto d = first_j(hal.tx_frames, j_opcode::deny);
        CHECK(d.has_value());
        if (d) {
            CHECK(d->denied_node_id       == idA);
            CHECK(d->claimant_key_hash32  == kMobA);                  // ★ only A yields...
            CHECK(d->owner_key_hash32     == kMobB);                  // ★ ...and B is named the owner
        }

        // ★★★ AND THE POINT OF THE WHOLE FIX: B's own CLAIM now lands CLEAN — no collision, no DENY. On the
        // pre-fix build A held the row, so this CLAIM produced `mobile_id_collision_deny` and B had to
        // re-register: exactly the recovery [[B137]] exists to avoid.
        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> cb{};
        const size_t bn = make_j_claim_mobile(kHostId, idB, kMobB, cb);
        host.on_recv(cb.data(), bn, cmeta);
        CHECK(hal.count("mobile_registered") == 1);
        CHECK(hal.count("mobile_id_collision_deny") == 0);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);
        CHECK(host.mobile_reg_count() == 1);
        CHECK(host.mobile_offers_pending_n() == 0);                   // ★ the CLAIM handed the promise to the row
    }

    // ---- ARM 2 — THE OTHER HALF OF THE PAIR: the claimant DOES hold a live reservation, for a DIFFERENT id.
    // Its old CLAIM is a stale echo. ⛔ DROP, do NOT deny: a DENY would throw away the id we are currently
    // promising it.
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(join_cfg()));

        const uint8_t idA1 = offer_and_read_id(host, hal, kMobA);     // A's FIRST offer
        hal._now += protocol::mobile_offer_reservation_ms + 1;
        host.on_timer(kMobileOfferBackoffTimerId);                    // A's reservation lapses
        const uint8_t idB = offer_and_read_id(host, hal, kMobB);      // B takes that id
        CHECK(idB == idA1);                                           // PREMISE
        const uint8_t idA2 = offer_and_read_id(host, hal, kMobA);     // A re-DISCOVERs -> a DIFFERENT id
        CHECK(idA2 != idA1);                                          // ★ PREMISE: A now holds a promise for idA2

        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> cl{};
        const size_t cn = make_j_claim_mobile(kHostId, idA1, kMobA, cl);   // A's STALE round-1 CLAIM
        host.on_recv(cl.data(), cn, cmeta);
        CHECK(host.mobile_reg_count() == 0);                          // ★★ not recorded...
        CHECK(hal.count("mobile_claim_stale_id") == 1);               // ★★ ...reported as stale...
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);    // ★★★ ...and NOT denied

        // ★ A's CURRENT CLAIM, for the id actually promised to it, is still accepted.
        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> c2{};
        const size_t n2 = make_j_claim_mobile(kHostId, idA2, kMobA, c2);
        host.on_recv(c2.data(), n2, cmeta);
        CHECK(hal.count("mobile_registered") == 1);
        CHECK(host.mobile_reg_count() == 1);
    }

    // ---- ARM 3 — POSITIVE CONTROL / COMPATIBILITY: a LATE CLAIM whose reservation has aged out, against an id
    // NOBODY else is promised, is still RECORDED. The reservation is an upper bound on a leak, not a licence to
    // reject a mobile that took its time — and without this arm, arms 1 and 2 are satisfiable by a build that
    // rejects every unreserved CLAIM.
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(join_cfg()));

        const uint8_t idA = offer_and_read_id(host, hal, kMobA);
        hal._now += protocol::mobile_offer_reservation_ms + 1;
        host.on_timer(kMobileOfferBackoffTimerId);
        CHECK(host.mobile_offers_pending_n() == 0);                   // PREMISE: no live promise anywhere

        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> cl{};
        const size_t cn = make_j_claim_mobile(kHostId, idA, kMobA, cl);
        host.on_recv(cl.data(), cn, cmeta);
        CHECK(hal.count("mobile_registered") == 1);                   // ★★ recorded, exactly as before the fix
        CHECK(hal.count("mobile_claim_reserved_elsewhere") == 0);
        CHECK(hal.count("mobile_claim_stale_id") == 0);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);
        CHECK(host.mobile_reg_count() == 1);
    }

    // ---- ARM 4 — AN **ELAPSED** RESERVATION BLOCKS NOTHING, EVEN BEFORE THE SWEEP HAS RUN. This arm exists
    // BECAUSE THE MUTATION BATTERY SAID SO: M-B147-3 (`reserve_until_ms <= now` weakened to `== 0`, i.e. "in use
    // until the scan clears it") reddened NOTHING against arms 1-3, because every one of them fires timer 80 to
    // expire the reservation before the CLAIM and so never holds an ELAPSED-BUT-UNSWEPT entry. That is the exact
    // invariant `find_free_mobile_id` states for itself — *"a dropped/failed `_hal.after` must never be able to
    // leak an id permanently"* — and the CLAIM path inherits it, so it must be pinned here too.
    // ⇒ two promises are left to age out with the scan DELIBERATELY not run, and a third mobile claims one of the
    //   dead ids. It must be RECORDED, not refused.
    {
        constexpr uint32_t kMobC = 0x0000C303u;
        TestHal hal; hal._now = 100000;
        Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(join_cfg()));

        const uint8_t idA = offer_and_read_id(host, hal, kMobA);
        const uint8_t idB = offer_and_read_id(host, hal, kMobB);
        CHECK(idA != idB);
        CHECK(host.mobile_offers_pending_n() == 2);                   // PREMISE: two live promises

        hal._now += protocol::mobile_offer_reservation_ms + 1;        // ⛔ and NO `on_timer(80)` — the scan is skipped
        CHECK(host.mobile_offers_pending_n() == 2);                   // ★ PREMISE: the slots are still OCCUPIED...

        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> cl{};
        const size_t cn = make_j_claim_mobile(kHostId, idA, kMobC, cl);
        host.on_recv(cl.data(), cn, cmeta);
        CHECK(hal.count("mobile_registered") == 1);                   // ★★★ ...and yet the DEAD promise blocks nothing
        CHECK(hal.count("mobile_claim_reserved_elsewhere") == 0);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);
        CHECK(host.mobile_reg_count() == 1);
    }

    // ---- ARM 5 — ★★★★ §MH-S2c, THE ORDERING HOLE: A STALE CLAIM MUST BE IDENTIFIED AS STALE **BEFORE** THE
    // REGISTERED-ROW COLLISION CHECK ACTS ON ITS CONTENTS.
    // ⛔ THE DEFECT §MH-S2b LEFT BEHIND. Arms 1-4 all exercise a stale CLAIM against an id that is RESERVED
    //    elsewhere or free. They never put the stale id in a REGISTERED ROW — and that is the one arrangement in
    //    which the two branches disagree. With the registered-row loop running first: A held X · X is now
    //    REGISTERED to B · A re-DISCOVERs and is promised Y · A's DELAYED CLAIM for X arrives ⇒ the collision loop
    //    DENIED A **and called `mobile_offer_release(A)`, destroying the live promise of Y**. `mobile_claim_stale_id`
    //    was never reached. A then re-registered and threw away an id the host was still holding for it — the exact
    //    [[B137]] recovery-churn this whole mechanism exists to prevent, re-entered through the other door.
    // ★ THE PRINCIPLE: a stale frame must be identified as stale before ANY branch acts on its contents, or a check
    //   on those contents consumes state belonging to a NEWER transaction. Same family as [[B142]].
    // ⚠ THE TEMPTING WRONG FIX is *"read the reservation, but still fall through to the registered-row branch"* —
    //   it satisfies "no DENY" only if the fall-through is also suppressed. Assertion ③ (Y RETAINED) is what
    //   separates the two: a fall-through still releases Y on its way out.
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(join_cfg()));

        // 1) A is offered X and lets the promise lapse.
        const uint8_t idX = offer_and_read_id(host, hal, kMobA);
        hal._now += protocol::mobile_offer_reservation_ms + 1;
        host.on_timer(kMobileOfferBackoffTimerId);
        CHECK(host.mobile_offers_pending_n() == 0);                   // PREMISE: A holds nothing

        // 2) X is re-offered to B, and B CLAIMs it — so X is now a REGISTERED ROW, not merely a reservation.
        CHECK(offer_and_read_id(host, hal, kMobB) == idX);            // ★ PREMISE: the same id, re-promised to B
        std::array<uint8_t, 16> cb{};
        const size_t bn = make_j_claim_mobile(kHostId, idX, kMobB, cb);
        host.on_recv(cb.data(), bn, cmeta);
        CHECK(host.mobile_reg_count() == 1);                          // ★★ PREMISE: X is REGISTERED to B
        {
            uint32_t k = 0; uint8_t lid = 0; bool hp = false;
            CHECK(host.mobile_reg_at(0, k, lid, hp));
            CHECK(k == kMobB);
            CHECK(lid == idX);
        }

        // 3) A re-DISCOVERs and is promised a DIFFERENT id, Y.
        const uint8_t idY = offer_and_read_id(host, hal, kMobA);
        CHECK(idY != idX);                                            // ★ PREMISE: a genuinely different promise...
        CHECK(host.mobile_offers_pending_n() == 1);                   // ★ ...and it is LIVE

        // 4) A's DELAYED CLAIM for X arrives — stale, and aimed straight at B's registered row.
        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> ca{};
        const size_t an = make_j_claim_mobile(kHostId, idX, kMobA, ca);
        host.on_recv(ca.data(), an, cmeta);

        // ① NO DENY — the stale echo is dropped, not answered. (Pre-fix: one `mobile_id_collision_deny`.)
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);
        CHECK(hal.count("mobile_id_collision_deny") == 0);
        CHECK(hal.count("mobile_claim_stale_id") == 1);               // ★ and the drop/retain policy IS reached
        // ② B'S REGISTRATION IS UNALTERED — same count, same key, same local id.
        CHECK(host.mobile_reg_count() == 1);
        CHECK(hal.count("mobile_registered") == 0);
        {
            uint32_t k = 0; uint8_t lid = 0; bool hp = false;
            CHECK(host.mobile_reg_at(0, k, lid, hp));
            CHECK(k == kMobB);
            CHECK(lid == idX);
        }
        // ③ ★★★ Y IS RETAINED. This is the assertion the ordering bug fails and the half-fix fails with it.
        CHECK(host.mobile_offers_pending_n() == 1);

        // ④ A's SUBSEQUENT CLAIM FOR Y SUCCEEDS — the promise survived the stale frame and was honoured.
        hal.events.clear(); hal.tx_frames.clear();
        std::array<uint8_t, 16> cy{};
        const size_t yn = make_j_claim_mobile(kHostId, idY, kMobA, cy);
        host.on_recv(cy.data(), yn, cmeta);
        CHECK(hal.count("mobile_registered") == 1);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::deny) == 0);
        CHECK(host.mobile_reg_count() == 2);                          // ★ B on X, A on Y — both hosted, no churn
        CHECK(host.mobile_offers_pending_n() == 0);                   // ★ the CLAIM handed the promise to the row
        {
            uint32_t k = 0; uint8_t lid = 0; bool hp = false;
            CHECK(host.mobile_reg_at(1, k, lid, hp));
            CHECK(k == kMobA);
            CHECK(lid == idY);
        }
    }
}

// ---------------------------------------------------------------------------
// ★★★ [[B143]] DISPROVEN §MH-S2b — "a superseded collect-OFFERs GUARD still fires and closes the NEWER
// transaction's window early". IT DOES NOT, AND THIS CASE IS THE MEASUREMENT THAT SAYS SO.
//
// ⛔ THE INSTRUMENT ERROR. [[B143]] was raised from a throwaway probe that reported *"guards armed: 2"*. That
// number came from a fixture whose `after()` APPENDS TO A CALL LOG (`TestHal::count_armed` — history, not
// state). `Hal::after(delay, id)` REPLACES the pending deadline for that id: `TimerWheel` is a flat array
// indexed by timer id (`lib/hal/timer_wheel.cpp:8`, `_active[timer_id] = true; _due[timer_id] = …`) and
// `test_timer_wheel.cpp:60` already pins exactly that. ⇒ TWO ARM CALLS ON ONE ID ARE ONE LIVE TIMER, AND IT
// HOLDS THE **NEWER** DEADLINE — which is the newer transaction's own window, closing on time.
// ★★ THE LESSON, RECORDED WHERE IT HAPPENED: a harness that records CALLS is not a model of STATE. Same class
//    as [[B145]] one case above, where a fixture that invokes one callback per call modelled a pump that does
//    not exist. Both were found in the same round, in the same file, for the same reason.
// ⓘ This case asserts BOTH readings side by side — the call log's 2 and the wheel's 1 — so the disproof is
//   legible rather than merely stated, and so a future re-derivation of [[B143]] fails here first.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ [[B143]] DISPROVEN §MH-S2b — two DISCOVER handoffs leave ONE live guard, at the NEWER deadline") {
    meshroute::TimerWheel wheel;
    TestHal hal; hal._now = 100000; hal.wheel = &wheel;
    Node m(hal, /*node_id=*/0, /*key_hash32=*/0x00007B7Bu);
    CHECK(m.on_init(s0_mobile_cfg()));                                // is_mobile, LBT off -> the DISCOVER hands off

    // ---- transaction A: the operator arms one registration; the wheel fires the DISCOVER; the guard opens.
    m.mobile_register_current();
    std::vector<int> ids;
    (void)pump_due_timers(m, hal, wheel, &ids);
    CHECK(fired_count(ids, kMobileDiscoverTimerId) == 1);             // PREMISE: DISCOVER A really aired
    CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 1);
    CHECK(wheel.active(kMobileClaimGuardTimerId));
    CHECK(wheel.due_at(kMobileClaimGuardTimerId) == 100000 + protocol::mobile_offer_window_ms);   // A's window: 102000

    // ---- transaction B, 500 ms later — B143's own scenario, to the millisecond.
    hal._now = 100500;
    m.mobile_register_current();
    ids.clear();
    (void)pump_due_timers(m, hal, wheel, &ids);
    CHECK(fired_count(ids, kMobileDiscoverTimerId) == 1);             // PREMISE: DISCOVER B really aired too
    // ★★★ THE TWO READINGS, SIDE BY SIDE. The call log says TWO — this is the exact number the [[B143]] probe
    // reported as "two guards are armed". The WHEEL says ONE, and it holds B's deadline, not A's.
    CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 2);            // ← history
    CHECK(wheel.active(kMobileClaimGuardTimerId));                    // ← state
    CHECK(wheel.due_at(kMobileClaimGuardTimerId) == 100500 + protocol::mobile_offer_window_ms);   // 102500, B's

    // ---- at A's supposed deadline: NOTHING. [[B143]] predicted `mobile_no_host` here, 500 ms early, against B.
    hal._now = 102000;
    hal.events.clear();
    ids.clear();
    (void)pump_due_timers(m, hal, wheel, &ids);
    CHECK(fired_count(ids, kMobileClaimGuardTimerId) == 0);           // ★★★ the superseded guard does not exist
    CHECK(hal.count("mobile_no_host") == 0);                          // ★★★ ...so B's window is not closed early

    // ---- and B's own window closes exactly ONCE, at its own deadline. (No OFFER was collected, so the
    // no-host branch is the correct outcome — reported against B, on time, which is the whole point.)
    hal._now = 102500;
    ids.clear();
    (void)pump_due_timers(m, hal, wheel, &ids);
    CHECK(fired_count(ids, kMobileClaimGuardTimerId) == 1);           // ★★ exactly one close, at 102500
    CHECK(hal.count("mobile_no_host") == 1);
}

// ---------------------------------------------------------------------------
// §MH-S2 §5.3.3 (gate item 18) — A DUPLICATE DISCOVER COALESCES: no second slot, no re-draw, deadline UNMOVED,
// and the SAME reserved id. Every one of those four is a separate way to get it wrong.
// ⛔ The tempting wrong fix this excludes: coalescing by SLOT INDEX instead of by `target_key_hash32`.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S2 (spec §5.3.3) — a duplicate DISCOVER coalesces: one slot, one deadline, one id, no draw") {
    constexpr uint32_t kMobA = 0x0000AA11u;
    constexpr uint32_t kMobB = 0x0000BB22u;

    TestHal hal; hal._now = 100000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    hal.armed.clear();
    const int draws_at_start = hal.rand_calls;
    CHECK(stage_mobile_offer(host, hal, kMobA) == 1);               // A: armed
    CHECK(host.mobile_offers_pending_n() == 1);
    const int  arms_after_first  = hal.count_armed(kMobileOfferBackoffTimerId);
    const int  draws_after_first = hal.rand_calls;
    CHECK(arms_after_first == 1);                                   // PREMISE: exactly one deadline exists
    // ★★ THE DRAW-COUNT PIN (§11 S2 — "preserve the existing single-mobile draw behaviour"). ONE DISCOVER MUST
    // CONSUME EXACTLY ONE `rand_range`, which is what `jtx_stash_arm` consumed before the ring existed (one call,
    // jittered_tx_stash.h). S3 is the arc's only planned RNG re-anchor, so a second draw here would re-anchor the
    // whole mobile plane under the wrong slice. Measured, not assumed.
    CHECK(draws_after_first - draws_at_start == 1);

    // ---- THE DUPLICATE, INSIDE A's JITTER WINDOW.
    hal._now += 10;                                                 // time moved; the deadline must NOT
    CHECK(stage_mobile_offer(host, hal, kMobA) == 0);               // ★ NOTHING was scheduled...
    CHECK(hal.count("mobile_offer_coalesced") == 1);                // ★ ...it coalesced, and says so
    CHECK(host.mobile_offers_pending_n() == 1);                     // ★★ no second slot consumed
    CHECK(hal.count_armed(kMobileOfferBackoffTimerId) == arms_after_first);   // ★★ the deadline was NOT moved
    CHECK(hal.rand_calls == draws_after_first);                     // ★★ and NO jitter was drawn

    // ---- NEGATIVE CONTROL: a DIFFERENT hash is not a duplicate. Without it, "no second slot" is satisfied by a
    // build whose ring never admits anything after the first entry.
    CHECK(stage_mobile_offer(host, hal, kMobB) == 1);
    CHECK(host.mobile_offers_pending_n() == 2);
    CHECK(hal.rand_calls == draws_after_first + 1);                 // ★ a real admission DOES draw

    // ★★★ AND THE TIMER IS ARMED FOR THE **EARLIEST** DEADLINE, NOT THE MOST RECENT ONE — asserted as the ARMED
    // DELAY, which is the only way to see it. A's deadline is `t0 + 100`; B was admitted 10 ms later so B's is
    // `t0 + 110`. `Hal::after` holds ONE deadline per timer id, so arming B's would silently displace A's and
    // strand A's OFFER. ⚠ THIS ASSERTION EXISTS BECAUSE THE MUTATION BATTERY DEMANDED IT: with the fire helper
    // advancing to whatever was last armed, "earliest" and "latest" produce the same frames in the same order
    // (measured — M-S2-8 reddened nothing until this line), so only the DELAY distinguishes them.
    CHECK(hal.last_armed(kMobileOfferBackoffTimerId) == 90);        // = (t0 + 100) - (t0 + 10). B's would be 100.

    // ---- ON THE WIRE: A is answered ONCE, with ONE id. A duplicate that had re-drawn an id would show two
    // different ids across two frames; one that had consumed a slot would show two frames for A.
    hal.tx_frames.clear();
    // ⚠ EXACTLY TWO callbacks — one per armed entry. A third would advance the clock to the next armed deadline,
    // which after both have fired is the 10 s RESERVATION BOUND, and the re-DISCOVER block below needs the
    // reservations still LIVE. (Measured: a 4-iteration loop expired them and read `pending_n == 0`.)
    for (int i = 0; i < 2; ++i) fire_mobile_offer_timer(host, hal);
    int for_a = 0, for_b = 0; uint8_t id_a = 0;
    for (const auto& f : hal.tx_frames) {
        auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (!p || p->opcode != static_cast<uint8_t>(j_opcode::offer)) continue;
        if (p->target_key_hash32 == kMobA) { ++for_a; id_a = p->proposed_mobile_id; }
        if (p->target_key_hash32 == kMobB) ++for_b;
    }
    CHECK(for_a == 1);                                              // ★★ ONE OFFER for A despite TWO DISCOVERs
    CHECK(for_b == 1);                                              // ★ and the control mobile still got its own
    CHECK(id_a == 254);                                             // ★ the first-drawn id, retained across the duplicate

    // ★★★ THE OTHER HALF OF "A DUPLICATE RETAINS ITS RESERVATION", AND IT IS A DIFFERENT PATH: a re-DISCOVER
    // arriving AFTER the OFFER was transmitted (the mobile did not hear it). ⛔ That one must NOT coalesce into
    // silence — the mobile is still waiting — so the entry is RE-ARMED. What it must not do is re-draw the id:
    // `find_free_mobile_id` is idempotent against a LIVE RESERVATION exactly as it is against a registry row.
    // ⚠ THIS BLOCK EXISTS BECAUSE THE MUTATION BATTERY DEMANDED IT: deleting that idempotence loop reddened
    // NOTHING (M-S2-2) while the only duplicate under test was the still-armed one — a coverage gap, not an
    // inert mutation.
    CHECK(host.mobile_offers_pending_n() == 2);                     // PREMISE: both entries fired but stay RESERVED
    hal.tx_frames.clear();
    CHECK(stage_mobile_offer(host, hal, kMobA) == 1);               // ★ re-DISCOVER: a NEW OFFER is scheduled...
    CHECK(host.mobile_offers_pending_n() == 2);                     // ★★ ...into A's OWN slot — no third slot
    fire_mobile_offer_timer(host, hal);
    auto again = first_j(hal.tx_frames, j_opcode::offer);
    CHECK(again.has_value());
    if (again) { CHECK(again->target_key_hash32 == kMobA);
                 CHECK(again->proposed_mobile_id == id_a); }        // ★★★ the SAME id — the reservation held
}

// ---------------------------------------------------------------------------
// §MH-S2 §5.3.2 (gate item 19) — A FULL RING REFUSES EXPLICITLY AND DISTURBS NOTHING: no eviction, no deadline
// movement, the counter increments, and every previously armed OFFER still flies.
// ⛔ The tempting wrong fix this excludes: "the ring overwrites on full" — the §S0-1 defect wearing a ring's clothes.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S2 (spec §5.3.2) — a FULL pending-OFFER ring refuses, and every armed entry survives intact") {
    TestHal hal; hal._now = 100000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    constexpr uint8_t kCap = protocol::cap_pending_mobile_offers;
    CHECK(kCap == 8);                                               // ★ the constant this case is written against
    // ⛔ AND IT IS ITS OWN CONSTANT (§5.3.1): asserting the two are equal would be exactly the conflation the spec
    // forbids, so this asserts only that they are INDEPENDENTLY declared and may be retuned apart.
    CHECK(protocol::cap_mobile_offers == 8);                        // the mobile-side collection cap — a coincidence

    for (uint8_t i = 0; i < kCap; ++i) {
        hal._now = 100000 + i;
        CHECK(stage_mobile_offer(host, hal, /*mobile_hash=*/0x00010000u + i) == 1);
    }
    CHECK(host.mobile_offers_pending_n() == kCap);                  // PREMISE: the ring really is full
    CHECK(host.mobile_offer_ring_full_count() == 0);                // PREMISE: nothing refused yet

    // ---- THE OVERFLOW.
    const int arms_before  = hal.count_armed(kMobileOfferBackoffTimerId);
    const int draws_before = hal.rand_calls;
    CHECK(stage_mobile_offer(host, hal, /*mobile_hash=*/0x0000FFFFu) == 0);   // ★ nothing scheduled
    CHECK(hal.count("mobile_offer_ring_full") == 1);                // ★ refused EXPLICITLY, and says so
    CHECK(host.mobile_offer_ring_full_count() == 1);                // ★ the §10 counter
    CHECK(host.mobile_offers_pending_n() == kCap);                  // ★★ no eviction: still exactly kCap in use
    CHECK(hal.count_armed(kMobileOfferBackoffTimerId) == arms_before);   // ★★ no deadline was moved
    CHECK(hal.rand_calls == draws_before);                          // ★★ and no draw was consumed

    // ---- ON THE WIRE: all kCap originals fly, with kCap DISTINCT ids, and the refused mobile is never addressed.
    hal.tx_frames.clear();
    for (int i = 0; i < kCap + 2; ++i) fire_mobile_offer_timer(host, hal);
    int inspected = 0, refused_seen = 0;
    bool id_used[256] = {};
    int  dup_ids = 0;
    for (const auto& f : hal.tx_frames) {
        auto p = parse_j(std::span<const uint8_t>(f.data(), f.size()));
        if (!p || p->opcode != static_cast<uint8_t>(j_opcode::offer)) continue;
        ++inspected;
        if (p->target_key_hash32 == 0x0000FFFFu) ++refused_seen;
        if (id_used[p->proposed_mobile_id]) ++dup_ids;
        id_used[p->proposed_mobile_id] = true;
    }
    CHECK(inspected == kCap);                                       // ★★ every armed entry survived and flew
    CHECK(refused_seen == 0);                                       // ★ the refused mobile was never answered
    CHECK(dup_ids == 0);                                            // ★★ eight concurrent OFFERs, eight unique ids
}

// ---------------------------------------------------------------------------
// §MH-S2 [[B137]] (gate item 1, last clause) — A RESERVATION EXPIRES IF ITS MOBILE NEVER CLAIMS, and the id
// becomes offerable again. This is the bound that stops a silent mobile leaking an id forever.
// ⛔ The tempting wrong fix this excludes: reserving only AT THE CLAIM. The race is between the OFFER and the
//    CLAIM, so a CLAIM-time reservation is measurably too late — the middle block below is exactly that window.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S2 [[B137]] (spec §5.3.2) — a reservation blocks its id, then EXPIRES and releases it") {
    constexpr uint32_t kMobA = 0x0000AA11u;
    constexpr uint32_t kMobB = 0x0000BB22u;
    constexpr uint32_t kMobC = 0x0000CC33u;

    TestHal hal; hal._now = 100000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    // ---- A is offered an id and TRANSMITTED. It then goes silent — no CLAIM ever arrives.
    CHECK(stage_mobile_offer(host, hal, kMobA) == 1);
    hal.tx_frames.clear();
    fire_mobile_offer_timer(host, hal);
    auto oa = first_j(hal.tx_frames, j_opcode::offer);
    CHECK(oa.has_value());
    const uint8_t id_a = oa ? oa->proposed_mobile_id : 0;
    CHECK(id_a == 254);
    CHECK(host.mobile_offers_pending_n() == 1);                     // ★ the reservation survives the transmission

    // ---- THE WINDOW THE RESERVATION EXISTS FOR: B discovers while A's promise is live and MUST NOT be offered
    // A's id. This is the assertion that a CLAIM-time reservation would fail — there is no CLAIM yet.
    CHECK(stage_mobile_offer(host, hal, kMobB) == 1);
    hal.tx_frames.clear();
    fire_mobile_offer_timer(host, hal);
    auto ob = first_j(hal.tx_frames, j_opcode::offer);
    CHECK(ob.has_value());
    if (ob) { CHECK(ob->target_key_hash32 == kMobB);
              CHECK(ob->proposed_mobile_id != id_a); }              // ★★★ [[B137]]: A's promised id is TAKEN
    const uint8_t id_b = ob ? ob->proposed_mobile_id : 0;
    CHECK(id_b == 253);                                             // ★ and the allocator simply stepped down

    // ---- NOW LET BOTH RESERVATIONS ELAPSE. The scan (timer 80) expires them.
    hal._now += protocol::mobile_offer_reservation_ms;
    hal.events.clear(); hal.tx_frames.clear();
    host.on_timer(kMobileOfferBackoffTimerId);                      // fire AT the reservation bound, no OFFER due
    CHECK(hal.count("mobile_offer_reservation_expired") == 2);      // ★ both, and reported
    CHECK(host.mobile_offers_pending_n() == 0);                     // ★★ the ring is empty again
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                // ★ and expiry transmits NOTHING

    // ---- ...AND THE ID IS OFFERABLE AGAIN. C is a third mobile: it gets 254 back, which is only possible if the
    // reservation really was released (a leaked one would push C to 252).
    CHECK(stage_mobile_offer(host, hal, kMobC) == 1);
    hal.tx_frames.clear();
    fire_mobile_offer_timer(host, hal);
    auto oc = first_j(hal.tx_frames, j_opcode::offer);
    CHECK(oc.has_value());
    if (oc) { CHECK(oc->target_key_hash32 == kMobC);
              CHECK(oc->proposed_mobile_id == id_a); }              // ★★ 254, reclaimed
}

// ---------------------------------------------------------------------------
// §MH-S2 [[B137]] — A MATCHING CLAIM RELEASES THE RESERVATION AND THE REGISTRY ROW TAKES OVER THE ID, so the
// promise is handed on rather than dropped. The negative half is the point: the id stays unavailable AFTERWARDS.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S2 [[B137]] (spec §5.3.2) — the CLAIM releases the reservation and the ROW inherits the id") {
    constexpr uint32_t kMobA = 0x0000AA11u;
    constexpr uint32_t kMobB = 0x0000BB22u;
    constexpr uint8_t  kHostId = 42;
    RxMeta meta{8.0f, -80.0f, 0, -1};

    TestHal hal; hal._now = 100000;
    Node host(hal, kHostId, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    CHECK(stage_mobile_offer(host, hal, kMobA) == 1);
    hal.tx_frames.clear();
    fire_mobile_offer_timer(host, hal);
    auto oa = first_j(hal.tx_frames, j_opcode::offer);
    CHECK(oa.has_value());
    const uint8_t id_a = oa ? oa->proposed_mobile_id : 0;
    CHECK(id_a == 254);
    CHECK(host.mobile_offers_pending_n() == 1);                     // PREMISE: reserved, not yet claimed

    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(kHostId, id_a, kMobA, cl);
    hal.events.clear();
    host.on_recv(cl.data(), cn, meta);
    CHECK(hal.count("mobile_registered") == 1);                     // PREMISE: recorded
    CHECK(host.mobile_offers_pending_n() == 0);                     // ★★ released IMMEDIATELY, not at the bound

    // ★★ AND THE PROMISE WAS HANDED ON, NOT DROPPED: B still cannot have 254, because the registry row holds it
    // now. Without this the release could be "correct" and still hand the same id to the next mobile.
    hal.tx_frames.clear();
    CHECK(stage_mobile_offer(host, hal, kMobB) == 1);
    fire_mobile_offer_timer(host, hal);
    auto ob = first_j(hal.tx_frames, j_opcode::offer);
    CHECK(ob.has_value());
    if (ob) { CHECK(ob->target_key_hash32 == kMobB);
              CHECK(ob->proposed_mobile_id == 253); }               // ★ stepped down past the recorded row
}

// ---------------------------------------------------------------------------
// §MH-S2 §B132 (gate item 3) — ELIGIBILITY IS RE-CHECKED AT THE FIRE, AND IT NOW HAS TO CLEAR A WHOLE RING.
// The §B132b cases above already prove the boundary re-check for ONE staged OFFER; this is the obligation the
// ring ADDS — several armed entries plus their id reservations, all of which must go, on ONE flip.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S2 §B132 — `host_mobiles` off between staging and fire drops the WHOLE ring, not one entry") {
    TestHal hal; hal._now = 100000;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(join_cfg()));
    CHECK(host.can_host_mobiles());

    for (int i = 0; i < 3; ++i) {
        hal._now = 100000 + static_cast<uint64_t>(i);
        CHECK(stage_mobile_offer(host, hal, /*mobile_hash=*/0x00020000u + static_cast<uint32_t>(i)) == 1);
    }
    CHECK(host.mobile_offers_pending_n() == 3);                     // PREMISE: three armed entries + reservations

    // ---- POSITIVE CONTROL FIRST: still eligible ⇒ the first one really would fly. Without it the zero below is
    // satisfied by a build that transmits nothing at all.
    {
        TestHal h2; h2._now = 100000;
        Node ok(h2, /*node_id=*/43, /*key_hash32=*/0x00004343u);
        CHECK(ok.on_init(join_cfg()));
        CHECK(stage_mobile_offer(ok, h2, /*mobile_hash=*/0x00020000u) == 1);
        h2.tx_frames.clear();
        fire_mobile_offer_timer(ok, h2);
        CHECK(count_j_offer_mobile(h2.tx_frames) == 1);
    }

    host.mutable_config().host_mobiles = false;                     // B3: a LIVE console knob, no reboot
    CHECK_FALSE(host.can_host_mobiles());
    hal.tx_frames.clear();
    for (int i = 0; i < 4; ++i) fire_mobile_offer_timer(host, hal);
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                // ★★ NOTHING transmitted, on any callback
    CHECK(host.mobile_offers_pending_n() == 0);                     // ★★ and the reservations went with the frames —
                                                                    //    an ineligible node holds no promised ids
}

// ---------------------------------------------------------------------------
// §S0-2 — REWRITTEN IN PLACE BY §MH-S3 (2026-08-07).  Spec §2.2 → §5.2 / §5.4.
//
// ⛔⛔ THIS CASE WAS A CHARACTERIZATION REPRODUCTION AND IS NOW A FIX ASSERTION. Same case, same site,
// REWRITTEN rather than replaced, so the behaviour change is visible in the diff (B101; spec §11 S0 rule 3).
// ⛔ It was NOT deleted and NOT disabled.
//
// WHAT IT USED TO PIN (the defect, now fixed): `mobile_claim_guard_fire()`'s no-host arm computed
// `delay = _mobile_backoff_ms` — a pure capped doubling 5 s → 120 s with NO random draw anywhere on the
// path — and `on_init` kicked the FSM with `after(0, …)`. Mobiles powered together therefore stayed
// phase-aligned for as long as they ran and collided in the same OFFER window round after round (§2.2: "a
// several-minute attach time is an expected result of the current design"). The old assertions read:
//     CHECK(d1 == static_cast<int64_t>(expect[round]));   // 5000/10000/20000/40000, exactly
//     CHECK(d1 == d2);                                    // two mobiles, two RNG streams, ONE deadline
//     CHECK(r1_after - r1_before == 0);                   // the retry arm drew NOTHING
//     CHECK(phase1 == phase2);                            // permanently aligned after four rounds
// They went RED when S3 landed — MEASURED, not predicted: `137500 == 175000` on the pre-rewrite tree.
//
// WHAT IT PINS NOW — and it is deliberately ONE case rather than three, because the three draws are ONE
// contract (§5.2: "the draw and its order are part of the simulation contract") and U1 says extend the pin
// rather than fork it. The complete MOBILE-PLANE DRAW INVENTORY of §MH-S3, in the order a mobile spends it:
//
//   (0) a NON-mobile node draws exactly what it always did — the static-plane inertness proof, differential;
//   (A) §5.2 the AUTOMATIC boot kick draws 0..`mobile_boot_jitter_ms`  — ONE draw, in `on_init`;
//       …and a MANUAL/team-only kick draws NOTHING and stays immediate;
//   (B) §5.4 the CLAIM deadline draws 0..`mobile_claim_jitter_ms` on top of the MINIMUM collect window —
//       ONE draw, at the DISCOVER handoff (`lbt_complete`);
//   (C) §5.2 every no-host retry draws EQUAL JITTER over the capped window — ONE draw, per window close;
//   (D) the interval is exactly [window/2, window] — both ends forced and read back, so "equal jitter" is
//       measured rather than described. This is what separates it from a plain `rand(0, window)`.
//
// ★★ HOW THIS IS MADE NON-VACUOUS, and the mechanism is inherited from the characterization version: the
// two mobiles are given DIFFERENT key hashes AND DIFFERENT RNG streams (`_rand_lo_bias` 0 vs 7). Every
// asserted delay is therefore a value that CANNOT be produced without consuming the intended draw from the
// intended stream, and the divergence is exactly the bias — not "some difference".
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §S0-2 → §MH-S3 FIXED (spec §5.2/§5.4) — boot, CLAIM deadline and no-host retry all DRAW, so a fleet DE-synchronizes") {
    // ================================================================================================
    // (0) THE STATIC PLANE DRAWS NOTHING NEW — measured as a DIFFERENTIAL, not asserted as a number.
    // A bare `rand_calls == k` would rot the moment any unrelated boot draw changed; the difference
    // between an is_mobile=false node and an is_mobile=true node on the SAME config cannot.
    // ================================================================================================
    TestHal hs; hs._now = 100000;
    Node st(hs, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(st.on_init(join_cfg()));                                   // ★ the SAME config, is_mobile = FALSE
    const int static_init_draws = hs.rand_calls;
    CHECK(hs.count_armed(kMobileDiscoverTimerId) == 0);              // PREMISE: a static node has no mobile FSM

    // ================================================================================================
    // (A) §5.2 — THE AUTOMATIC BOOT KICK DRAWS A STARTUP JITTER.
    // ================================================================================================
    TestHal h1; h1._now = 100000; h1._rand_lo_bias = 0;
    TestHal h2; h2._now = 100000; h2._rand_lo_bias = 7;              // ★ a DIFFERENT RNG stream
    Node m1(h1, /*node_id=*/0, /*key_hash32=*/0x00001111u);
    Node m2(h2, /*node_id=*/0, /*key_hash32=*/0x00002222u);          // ★ a DIFFERENT identity too
    CHECK(m1.on_init(s0_mobile_cfg()));
    CHECK(m2.on_init(s0_mobile_cfg()));
    // ★★ EXACTLY ONE new draw versus the static node — the boot jitter and nothing else.
    CHECK(h1.rand_calls - static_init_draws == 1);
    CHECK(h2.rand_calls - static_init_draws == 1);
    CHECK(h1.count_armed(kMobileDiscoverTimerId) == 1);
    CHECK(h2.count_armed(kMobileDiscoverTimerId) == 1);
    // ★★ AND THE KICK IS STAGGERED. h1 draws `lo` = 0 (indistinguishable from the old `after(0,…)`, which is
    // why h2 carries the discriminating value): h2 draws lo + 7 and is therefore SEVEN MILLISECONDS LATE.
    CHECK(h1.last_armed(kMobileDiscoverTimerId) == 0);
    CHECK(h2.last_armed(kMobileDiscoverTimerId) == 7);

    // ---- NEGATIVE CONTROL, and it is a REQUIREMENT not a nicety (§5.2: "the manual first attempt is
    // immediate"): a team-only mobile with autoregister OFF is kicked purely so team-DAD runs. There is no
    // automatic attempt to stagger ⇒ it must draw NOTHING and arm at ZERO.
    {
        TestHal ht; ht._now = 100000; ht._rand_lo_bias = 7;          // the same biased stream — a draw WOULD show
        NodeConfig tc = s0_mobile_cfg(); tc.mobile_autoregister = false; tc.team_id = 0x77;
        Node mt(ht, /*node_id=*/0, /*key_hash32=*/0x00003333u);
        CHECK(mt.on_init(tc));
        CHECK(ht.count_armed(kMobileDiscoverTimerId) == 1);          // PREMISE: the kick still happens (§6.4)
        CHECK(ht.last_armed(kMobileDiscoverTimerId) == 0);           // ★ …IMMEDIATE
        CHECK(ht.rand_calls - static_init_draws == 0);               // ★★ …and draw-free
    }

    // ================================================================================================
    // (B) + (C) — the CLAIM deadline and the no-host retry, four rounds of the capped ladder.
    // ================================================================================================
    // The documented capped-doubling ladder is RETAINED (§5.2 "retain capped exponential growth"):
    // `window = min(5 s * 2^attempt, 120 s)`. What changed is that the window is now the input to a draw
    // instead of being the delay itself.
    const uint32_t window[4] = { 5000u, 10000u, 20000u, 40000u };
    uint64_t phase1 = h1._now, phase2 = h2._now;                     // absolute ms of each mobile's next DISCOVER

    for (int round = 0; round < 4; ++round) {
        // ---- mobile 1: DISCOVER, then the window close
        h1.events.clear(); h1.armed.clear();
        const int r1_discover_before = h1.rand_calls;
        m1.on_timer(kMobileDiscoverTimerId);
        CHECK(h1.count("mobile_discover_tx") == 1);                  // PREMISE: the DISCOVER really went out...
        // ★★ (B) §5.4: ONE draw at the handoff, and the guard is armed at the MINIMUM window PLUS it.
        CHECK(h1.rand_calls - r1_discover_before == 1);
        const int64_t g1 = h1.last_armed(kMobileClaimGuardTimerId);
        CHECK(g1 == static_cast<int64_t>(protocol::mobile_offer_window_ms) + 0);   // bias 0 ⇒ the minimum

        const int r1_before = h1.rand_calls;
        m1.on_timer(kMobileClaimGuardTimerId);                       // ...and the window really closed with no host
        CHECK(h1.count("mobile_no_host") == 1);                      // PREMISE: the no-host arm is the branch under test
        const int64_t d1 = h1.last_armed(kMobileDiscoverTimerId);
        CHECK(h1.rand_calls - r1_before == 1);                       // ★★ (C) EXACTLY ONE draw per window close

        // ---- mobile 2, same wall clock, different stream
        h2.events.clear(); h2.armed.clear();
        const int r2_discover_before = h2.rand_calls;
        m2.on_timer(kMobileDiscoverTimerId);
        CHECK(h2.count("mobile_discover_tx") == 1);
        CHECK(h2.rand_calls - r2_discover_before == 1);
        const int64_t g2 = h2.last_armed(kMobileClaimGuardTimerId);
        CHECK(g2 == static_cast<int64_t>(protocol::mobile_offer_window_ms) + 7);   // ★ bias 7 ⇒ SEVEN ms later

        const int r2_before = h2.rand_calls;
        m2.on_timer(kMobileClaimGuardTimerId);
        CHECK(h2.count("mobile_no_host") == 1);
        const int64_t d2 = h2.last_armed(kMobileDiscoverTimerId);
        CHECK(h2.rand_calls - r2_before == 1);

        // ★★ THE FIX: the retry deadline is DRAWN, from the LOWER HALF of the retained capped window.
        CHECK(d1 == static_cast<int64_t>(window[round] / 2));        // ← was `window[round]`, un-jittered
        CHECK(d2 == static_cast<int64_t>(window[round] / 2) + 7);
        CHECK(d1 != d2);                                             // ★ two mobiles, two streams, TWO deadlines
        // ★ and the growth itself survived the change — a jitter that also flattened the ladder would be a
        //   regression this case would otherwise wave through.
        CHECK(window[round] == (round == 0 ? protocol::mobile_discover_backoff_min_ms : 2u * window[round - 1]));

        // advance both clocks to their OWN next attempt (window close + retry delay)
        phase1 += static_cast<uint64_t>(g1) + static_cast<uint64_t>(d1); h1._now = phase1;
        phase2 += static_cast<uint64_t>(g2) + static_cast<uint64_t>(d2); h2._now = phase2;
    }
    // ★ QUANTIFIED PHASE DIVERGENCE — the exact inverse of the assertion this case used to carry. Two draws
    // per round × 4 rounds × 7 ms of stream difference = 56 ms apart, and the gap is CUMULATIVE.
    CHECK(phase1 != phase2);
    CHECK(phase1 == 100000ull + 4ull * protocol::mobile_offer_window_ms + (2500ull + 5000ull + 10000ull + 20000ull));
    CHECK(phase2 - phase1 == 4ull * 2ull * 7ull);

    // ================================================================================================
    // (D) THE INTERVAL IS [window/2, window] — BOTH ENDS FORCED AND READ BACK.
    // This is the assertion that says EQUAL jitter rather than merely "some jitter": a `rand(0, window)`
    // would give 0 at the low end, and a `rand(window/2, window)` would never reach `window` at the high
    // end. `_rand_ret` is clamped into [lo, hi) by the harness, so the two extremes below read the real
    // bounds the production code passed to `rand_range`, not values the test chose.
    // ================================================================================================
    {
        TestHal lo; lo._now = 100000; lo._rand_ret = 0;              // ★ ask for ZERO…
        Node ml(lo, /*node_id=*/0, /*key_hash32=*/0x00005151u);
        CHECK(ml.on_init(s0_mobile_cfg()));
        ml.on_timer(kMobileDiscoverTimerId);
        lo.armed.clear();
        ml.on_timer(kMobileClaimGuardTimerId);
        CHECK(lo.count("mobile_no_host") == 1);                      // PREMISE: the right branch ran
        CHECK(lo.last_armed(kMobileDiscoverTimerId) == 2500);        // ★★ …and got window/2. The NON-ZERO
                                                                     //    lower half is what stops a failed
                                                                     //    fleet storming again immediately.
        CHECK(lo.last_armed(kMobileDiscoverTimerId)
                  == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_discover_backoff_min_ms)));

        TestHal hi; hi._now = 100000; hi._rand_ret = 1000000;        // ★ ask for a huge value…
        Node mh(hi, /*node_id=*/0, /*key_hash32=*/0x00005252u);
        CHECK(mh.on_init(s0_mobile_cfg()));
        mh.on_timer(kMobileDiscoverTimerId);
        hi.armed.clear();
        mh.on_timer(kMobileClaimGuardTimerId);
        CHECK(hi.count("mobile_no_host") == 1);
        CHECK(hi.last_armed(kMobileDiscoverTimerId) == 5000);        // ★★ …and the TOP of the window is
                                                                     //    REACHABLE — `window + 1` as the
                                                                     //    half-open upper bound, not `window`.
        CHECK(hi.last_armed(kMobileDiscoverTimerId)
                  == static_cast<int64_t>(protocol::equal_jitter_hi_excl(protocol::mobile_discover_backoff_min_ms)) - 1);
    }

    // ---- CROSS-CHECK, RETAINED FROM THE CHARACTERIZATION VERSION AND RE-PURPOSED: the HOST-side OFFER
    // de-storm jitter (`jtx_stash_arm`, 100..1000 ms) is §MH-S2's draw, and S3 MUST NOT have moved it. It
    // used to be here as proof that the seam could see a jittered arm at all; now that the mobile side
    // draws visibly, its job is the opposite one — an unchanged-neighbour control on the host plane.
    {
        TestHal g1; g1._now = 100000; g1._rand_lo_bias = 0;
        TestHal g2; g2._now = 100000; g2._rand_lo_bias = 7;
        Node ha(g1, /*node_id=*/42, /*key_hash32=*/0x00004242u);
        Node hb(g2, /*node_id=*/43, /*key_hash32=*/0x00004343u);
        CHECK(ha.on_init(join_cfg()));
        CHECK(hb.on_init(join_cfg()));
        CHECK(stage_mobile_offer(ha, g1, 0x0000D1D1u) == 1);
        CHECK(stage_mobile_offer(hb, g2, 0x0000D1D1u) == 1);
        const int64_t ja = g1.last_armed(kMobileOfferBackoffTimerId);
        const int64_t jb = g2.last_armed(kMobileOfferBackoffTimerId);
        CHECK(ja == 100);                                            // lo of [100, 1001) with bias 0 — UNCHANGED
        CHECK(jb == 107);                                            // lo + 7 — UNCHANGED
        CHECK(ja != jb);
    }

    // ================================================================================================
    // ★★★★ (E) §MH-S4 / §MH-S4b ADD **NO NEW DRAW SITE** — pinned here rather than in a separate case, because the
    // inventory above IS the contract (U1: extend the pin, don't fork it) and "S3 was the arc's only planned
    // re-anchor" is a claim about THIS list. S4 introduces the re-CLAIM, the confirmation deadline and two
    // state planes; none of them may spend randomness.
    //   · `mobile_reclaim_send()`      — `pack_j_claim` + `tx_initiating`, both draw-free;
    //   · `presence_claim_unconfirmed` — spends the ONE `presence_arm_check` draw its own deadline makes.
    // ★★★★ §MH-S4b — AND THE HONEST STATEMENT OF WHAT DID CHANGE, because "no new draw" would otherwise be read
    // as "the same number of draws". §7.1 step 3's solicitation SPLITS the claiming round into TWO deadlines
    // (ask, then wait), so a claiming round now spends **TWO** draws where §MH-S4 spent one. That is a NEW
    // DEADLINE, not a new draw SITE: both come from the single pre-existing `rand_range` in
    // `presence_arm_check`, and each individual fire still spends exactly one. Pinned BOTH ways below — per-fire
    // and per-round — so neither reading can rot silently.
    // ★ MEASURED AS A DIFFERENTIAL against the same node's own steady-state probe, so the pin cannot rot when
    //   an unrelated boot draw changes.
    // ================================================================================================
    {
        RxMeta meta{8.0f, -80.0f, 0, -1};
        TestHal hd; hd._now = 100000;
        Node md(hd, /*node_id=*/0, /*key_hash32=*/0x0000D4D4u);
        CHECK(md.on_init(s0_mobile_cfg()));
        md.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(/*responder=*/44, 0x00004444u, /*local=*/250, /*target=*/0x0000D4D4u, off);
        md.on_recv(off.data(), on, meta);
        md.on_timer(kMobileClaimGuardTimerId);                       // CLAIM + provisional adopt -> `claiming`
        CHECK(md.mobile_attach_state() == Node::MobileAttachState::claiming);   // PREMISE: the branch under test
        // ★★ §MH-S4b §7.1 step 3: the FIRST deadline is SHORT and it is a SOLICITATION, not the steady T.
        CHECK(hd.last_armed(kPresenceProbeTimerId) == static_cast<int64_t>(protocol::presence_claim_solicit_ms));
        CHECK_FALSE(md.mobile_claim_solicited());                    // "we have not asked yet"

        // ---- (E1) the SOLICITATION deadline: a searching probe, NO re-CLAIM, exactly ONE draw.
        const int before_solicit  = hd.rand_calls;
        const int claims_before   = count_j_opcode(hd.tx_frames, j_opcode::claim);
        hd._now += protocol::presence_claim_solicit_ms;
        md.on_timer(kPresenceProbeTimerId);
        CHECK(count_j_opcode(hd.tx_frames, j_opcode::claim) == claims_before);       // ★★ NO re-CLAIM yet — the ask precedes the verdict
        CHECK(md.mobile_claim_retries() == 0);                                       // ★★ …and NO budget spent
        CHECK(md.mobile_claim_solicited());                                          // ★ "we asked and are waiting"
        CHECK(hd.last_armed(kPresenceProbeTimerId) == static_cast<int64_t>(protocol::presence_claim_confirm_ms));
        CHECK(hd.rand_calls - before_solicit == 1);                  // ★★★ ONE draw: the confirmation-deadline jitter, nothing else

        // ---- (E2) the CONFIRMATION deadline: no probe, ONE re-CLAIM, exactly ONE draw.
        const int before_claiming = hd.rand_calls;
        hd._now += protocol::presence_claim_confirm_ms;
        md.on_timer(kPresenceProbeTimerId);
        CHECK(count_j_opcode(hd.tx_frames, j_opcode::claim) == claims_before + 1);   // PREMISE: a re-CLAIM really went out
        CHECK(md.mobile_claim_retries() == 1);                       // PREMISE: the budget really moved
        CHECK_FALSE(md.mobile_claim_solicited());                    // …and the next round will ASK again
        CHECK(hd.rand_calls - before_claiming == 1);                 // ★★★ ONE draw here too
        // ⇒ the per-ROUND figure, stated as a number rather than left implicit:
        CHECK(hd.rand_calls - before_solicit == 2);                  // ★★ TWO deadlines, TWO draws, ONE draw site

        // ---- the ATTACHED steady-state deadline, on the SAME node, as the differential baseline. Confirm it
        //      first (a real roster), then fire one ordinary probe.
        confirm_mobile_via_roster(md, hd, /*home=*/44, md.config().layers[0].layer_id,
                                  /*hash=*/0x0000D4D4u, /*local=*/250, /*epoch=*/1, meta);
        CHECK(md.mobile_attached());                                 // PREMISE: now on the steady path
        const int before_steady = hd.rand_calls;
        const int claims_steady = count_j_opcode(hd.tx_frames, j_opcode::claim);
        hd._now += protocol::presence_check_base_ms;
        md.on_timer(kPresenceProbeTimerId);
        CHECK(hd.count("presence_probe_tx") >= 1);                   // PREMISE: a probe really fired
        CHECK(hd.rand_calls - before_steady == 1);                   // ★★ the steady probe draws ONE too...
        CHECK(count_j_opcode(hd.tx_frames, j_opcode::claim) == claims_steady);   // ★ ...and sends NO CLAIM
        // ⇒ ★★★★ THE PIN: EVERY deadline fire — solicitation, confirmation, and attached steady-state — spends
        //   EXACTLY ONE draw, from the one pre-existing `presence_arm_check` site. §MH-S4b's extra draw per
        //   claiming round is an extra DEADLINE, and the three equal per-fire figures above are what prove that.
    }
}

// ---------------------------------------------------------------------------
// §S0-3 — REWRITTEN IN PLACE BY §MH-S1 (2026-08-07).  Spec §2.3 → §6.1.
//
// ⛔⛔ THIS CASE WAS A CHARACTERIZATION REPRODUCTION AND IS NOW A FIX ASSERTION. It is deliberately the
// SAME case, at the SAME site, rewritten rather than replaced, so the behaviour change is visible in the
// diff (the B101 precedent; spec §11 S0 rule 3). ⛔ It was NOT deleted and NOT disabled.
//
// WHAT IT USED TO PIN (the defect, now fixed): `mobile_discover_fire()` armed `kMobileClaimGuardTimerId`
// for `mobile_offer_window_ms` (2000 ms) on the line after `tx_initiating(...)` — i.e. the 2-second
// collect-OFFERs window was measured from a REQUEST TO SEND. A NAV/LBT defer longer than the window
// therefore CLOSED the collector, reported `mobile_no_host` and doubled the backoff, all while the
// DISCOVER was still sitting in the defer ring. The old assertions read:
//     CHECK(hal.last_armed(kMobileClaimGuardTimerId) == mobile_offer_window_ms);   // armed at the REQUEST
//     m.on_timer(kMobileClaimGuardTimerId); CHECK(hal.count("mobile_no_host") == 1);
//
// ★★ WHY THE FIX IS NOT "CHECK tx_initiating's RETURN VALUE", and this is the finding S0 paid for:
// `tx_initiating` RETURNS `schedule_lbt_defer(...)` on the defer path (node_mac.cpp), i.e. **TRUE
// whenever the defer ring accepts the frame** — `false` means only "ring full ⇒ DROPPED" or "the HAL
// rejected it". A DEFERRED DISCOVER IS REPORTED TO ITS CALLER AS A SUCCESS. S0's M3 mutation gated the
// arm on `!tx_initiating(...)`, matched its anchor EXACTLY ONCE, and reddened NOTHING. ⇒ S1 gave DISCOVER
// an explicit initiating kind (`LbtKind::mobile_discover`) and arms the guard in `lbt_complete`, at the
// accepted LBT/HAL handoff — the same seam `LbtKind::rts` has always used for `start_rts_timeout` (U1).
//
// WHAT IT PINS NOW (spec §6.1, gate item 5): the guard is armed at the HANDOFF and nowhere else, so a
// 5-second defer no longer costs the window — it merely delays it, intact.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §S0-3 → §MH-S1 FIXED (spec §6.1) — a 5 s LBT defer DELAYS the OFFER window; it no longer destroys it") {
    constexpr uint32_t kBusyMs = 5000;                               // ★ 5 s ≫ mobile_offer_window_ms (2 s)
    static_assert(kBusyMs > protocol::mobile_offer_window_ms, "the defer must outlast the window, else nothing is proven");

    TestHal hal; hal._now = 100000;
    NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;        // physical carrier sense ON
    Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000C3C3u);
    CHECK(m.on_init(cfg));
    hal._busy_until = hal._now + kBusyMs;                            // the channel is reserved for 5 s

    hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
    m.on_timer(kMobileDiscoverTimerId);

    // PREMISE 1 — the DEFER REALLY DEFERRED. Without this the case would "pass" on a build where LBT was
    // simply off and the frame went out normally.
    CHECK(hal.count("tx_lbt_defer") == 1);
    const int64_t defer_due = hal.last_armed(kLbtDeferTimerId);      // captured HERE: the deadline THE CODE armed
    CHECK(defer_due == static_cast<int64_t>(kBusyMs));               // slot 0, due at busy_until
    // PREMISE 2 — and NOTHING was transmitted: `tx_initiating` returned on the defer path.
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);
    CHECK(hal.tx_frames.empty());

    // ★★ THE FIX, HALF 1 — the collect-OFFERs guard is NOT armed at the request. `last_armed` returns -1
    // for a timer that was never armed, and the count says the same thing in the form that cannot be
    // satisfied by an arm-then-cancel (TestHal::cancel is a no-op, so a count is the honest instrument).
    CHECK(hal.last_armed(kMobileClaimGuardTimerId) == -1);
    CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);
    // ⇒ nothing can close the window early because NOTHING IS COUNTING DOWN. The old case fired timer 75
    //   here and got `mobile_no_host`; there is now no timer 75 to fire.
    hal._now += protocol::mobile_offer_window_ms;                    // t + 2000 — the old window's whole span
    CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);           // ★ still nothing armed, 2 s later
    CHECK(hal.count("mobile_no_host") == 0);                         // ★★ and no home was blamed for our own defer
    CHECK_FALSE(m.mobile_registered());
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);   // still in the ring, as PREMISE 1 said

    // ★★ THE FIX, HALF 2 — the medium clears, the deferred slot fires, and the DISCOVER and its window
    // arrive TOGETHER. Both counters were 0 immediately above and are 1 immediately below: that PAIRED
    // TRANSITION is the observable, and it is what "the window opens at the handoff" means.
    hal._now = 100000 + static_cast<uint64_t>(defer_due);            // ★ advance to the deadline THE CODE armed
    m.test_fire_lbt_defer(/*slot=*/0);
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 1);   // ★ the frame is on the air...
    CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 1);           // ★★ ...and NOW the window is armed
    // ★ §MH-S3 §5.4: the arm is now `mobile_offer_window_ms + rand(0, mobile_claim_jitter_ms + 1)`. This
    //   TestHal draws `lo` = 0, so the VALUE is unchanged — spelled out rather than left implicit, because
    //   a reader comparing this line to the code would otherwise think one of the two had drifted.
    CHECK(hal.last_armed(kMobileClaimGuardTimerId)
              == static_cast<int64_t>(protocol::mobile_offer_window_ms) + 0);   // + the drawn 0
    // ★★ AND IT IS THE FULL WINDOW, MEASURED FROM TWO CODE-DERIVED NUMBERS. Under the old behaviour the
    // window had been shut for `defer_due - mobile_offer_window_ms` = 3000 ms by this point; now that same
    // subtraction is merely how long the mobile waited before its INTACT 2 s window opened. The arithmetic
    // is retained deliberately — it is the number that made the defect visible, and it still fails on any
    // build whose defer is shorter than its window.
    CHECK(defer_due - static_cast<int64_t>(protocol::mobile_offer_window_ms) == 3000);

    // ---- POSITIVE CONTROL: with a clear channel the same code path transmits AT the request and arms the
    // window there, so the assertions above measure the defer — not an arm that S1 simply deleted.
    {
        TestHal c; c._now = 100000;
        Node m2(c, /*node_id=*/0, /*key_hash32=*/0x0000C4C4u);
        CHECK(m2.on_init(cfg));                                      // same config, LBT on
        c._busy_until = 0;                                           // ...but an idle channel
        m2.on_timer(kMobileDiscoverTimerId);
        CHECK(c.count("tx_lbt_defer") == 0);
        CHECK(count_j_opcode(c.tx_frames, j_opcode::discover) == 1); // ★ immediate handoff, seen by this harness
        CHECK(c.count_armed(kMobileClaimGuardTimerId) == 1);         // ★ armed exactly once, at that handoff
        CHECK(c.last_armed(kMobileClaimGuardTimerId)
                  == static_cast<int64_t>(protocol::mobile_offer_window_ms) + 0);   // §MH-S3 §5.4: + the drawn 0
    }
}

// ---------------------------------------------------------------------------
// §MH-S1 §6.1 — A REFUSED DISCOVER IS A LOCAL TRANSMITTER FACT, NOT "no host".  Gate items 6 and 20.
//
// The other half of §6.1: when admission is REJECTED (as opposed to deferred), the mobile must not report
// `mobile_no_host` — nobody was ever asked — must record `tx_rejected`/`defer_full` as the last attempt
// result, and must leave a BOUNDED retry armed. ⛔ And per §6.4 the failure must not touch the home-link
// plane: it is a statement about our own radio.
//
// HOW THE REJECTION IS PRODUCED HERE: a FULL LBT defer ring. The ring is `kLbtSlots = 4`, so four deferred
// DISCOVERs fill it and the fifth is refused. That is driven, not assumed — the premises below assert both
// the four accepted defers and the one loud refusal.
// ⛔⛔ CORRECTED IN PLACE 2026-08-07 (§MH-S1b, QA round 2). This header used to continue: *"`TestHal::tx`
// always answers `ok`, so the HAL half is unreachable here"*, and the slice reported `tx_rejected` as
// METAL-ONLY on the strength of it. THAT WAS A STATEMENT ABOUT THE HARNESS, NOT ABOUT REACHABILITY — the
// capability was ONE FIELD away (`TestHal::tx_answer`, now added). The `tx_rejected` half is covered
// natively by the three cases below this one, immediate AND deferred. ⇒ the residue that is genuinely
// metal-only is much smaller, and the bench script was re-scoped to it.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S1 (spec §6.1) — a DISCOVER refused by our OWN transmitter reports defer_full + a bounded retry, never mobile_no_host") {
    TestHal hal; hal._now = 100000;
    NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
    Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000C5C5u);
    CHECK(m.on_init(cfg));
    hal._busy_until = hal._now + 5000;                               // every initiating TX defers

    // PREMISE — fill all four defer-ring slots. Each DISCOVER is ACCEPTED (deferred), so none of these is
    // the rejection under test; the count is asserted exactly, not `>=`.
    hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
    for (int i = 0; i < 4; ++i) m.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("tx_lbt_defer") == 4);                           // four accepted...
    CHECK(hal.count("tx_lbt_defer_dropped") == 0);                   // ...and none refused yet
    CHECK(m.mobile_last_result() == Node::MobileAttemptResult::none);// ★ nothing has been recorded so far

    // ---- THE REJECTION: the fifth DISCOVER finds the ring full.
    hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
    m.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("tx_lbt_defer_dropped") == 1);                   // PREMISE: the ring really refused it
    CHECK(hal.tx_frames.empty());                                    // PREMISE: nothing reached the radio

    // ★★ THE STATE READ BACK — the last attempt result names OUR transmitter, with the right one of the
    // two codes (`defer_full`, not `tx_rejected`: the ring refused it, the HAL never saw it).
    CHECK(m.mobile_last_result() == Node::MobileAttemptResult::defer_full);
    // ★★ AND THE HOME IS NOT BLAMED (§6.1/§6.4, gate 20): no `mobile_no_host`, and no window was opened
    // that could later produce one.
    CHECK(hal.count("mobile_no_host") == 0);
    CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);
    CHECK_FALSE(m.mobile_registered());
    CHECK(m.mobile_home_id() == 0);                                  // the home-link plane is untouched
    // ★★ AND THE BOUNDED RETRY IS ARMED (gate 6) — the deadline read back from the harness, exactly once.
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);
    CHECK(hal.last_armed(kMobileDiscoverTimerId)                     // ★ §MH-S3 §5.2: EQUAL-JITTERED (was the flat window)
              == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_offer_window_ms)));   // rand(1000, 2001); this TestHal draws `lo`

    // ---- NEGATIVE CONTROL: the SAME node, the SAME code path, with one ring slot freed, is ACCEPTED —
    // so the assertions above measure the refusal and not a build that has stopped DISCOVERing entirely.
    hal.armed.clear(); hal.events.clear();
    m.test_fire_lbt_defer(/*slot=*/0);                               // drain one slot (it transmits)
    hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
    m.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("tx_lbt_defer") == 1);                           // ★ accepted into the freed slot
    CHECK(hal.count("tx_lbt_defer_dropped") == 0);                   // ★ no refusal this time
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);             // ★ and therefore NO admission retry armed
}

// ---------------------------------------------------------------------------
// §MH-S1 §6.3 — A REJECTED CLAIM MUST NOT PRODUCE A REGISTRATION.  Gate items 6 and 20.
//
// `mobile_claim_guard_fire()` used to `tx_initiating(CLAIM)` and then adopt UNCONDITIONALLY —
// `set_identity`, `_joined = true`, the app-facing `mobile_reg{registered:true}` push and a triggered
// beacon. When the transmitter definitively refuses the CLAIM, that is a "success that isn't": the app is
// told it is registered at a home that was never sent anything. Gate 6: not reported as a successful send,
// and a bounded retry. §6.3: it is a LOCAL retry condition, NOT evidence the home rejected registration.
//
// ⛔ SCOPE: this is the ADMISSION boundary only. A CLAIM that IS admitted still claim-stands exactly as
// before — confirming an admitted-but-LOST CLAIM against the home's roster is §7.1 and belongs to S4,
// which is why §S0-4 (the lost-CLAIM characterization) is deliberately still green.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S1 (spec §6.3) — a CLAIM refused by our OWN transmitter does NOT register the mobile") {
    TestHal hal; hal._now = 100000;
    NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
    Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000C6C6u);
    CHECK(m.on_init(cfg));
    hal._busy_until = hal._now + 5000;

    // Fill the defer ring FIRST (four deferred DISCOVERs), so the CLAIM below has nowhere to go.
    for (int i = 0; i < 4; ++i) m.on_timer(kMobileDiscoverTimerId);
    CHECK(hal.count("tx_lbt_defer") == 4);
    CHECK(hal.count("tx_lbt_defer_dropped") == 0);

    // Feed ONE OFFER addressed at us, so the guard has something to CLAIM. (Each DISCOVER above cleared the
    // collector, which is why the OFFER is delivered after them, not before.)
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> off{};
    const size_t on = make_j_offer_mobile(/*responder=*/30, /*resp_hash=*/0x0000C0C0u, /*local=*/201,
                                          /*target=*/0x0000C6C6u, off);
    m.on_recv(off.data(), on, meta);
    CHECK(m.mobile_offers_n() == 1);                                 // PREMISE: the guard WILL take the CLAIM branch

    hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
    m.on_timer(kMobileClaimGuardTimerId);

    // PREMISES — the CLAIM branch really ran and the transmitter really refused it.
    CHECK(hal.count("mobile_no_host") == 0);                         // not the no-OFFER branch
    CHECK(hal.count("tx_lbt_defer_dropped") == 1);                   // the ring refused the CLAIM
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);      // ★ nothing on the wire

    // ★★ THE FIX: no registration, because no CLAIM was ever sent.
    CHECK_FALSE(m.mobile_registered());                              // ← was `true` before S1
    CHECK(m.mobile_home_id() == 0);
    CHECK(m.mobile_local_id() == 0);
    CHECK(hal.count("mobile_adopted") == 0);                         // ★ and the app was not told otherwise
    // ★★ CLASSIFIED AS A LOCAL TRANSMITTER FACT (§6.3), with a bounded retry (gate 6).
    CHECK(m.mobile_last_result() == Node::MobileAttemptResult::defer_full);
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);
    CHECK(hal.last_armed(kMobileDiscoverTimerId)                     // ★ §MH-S3 §5.2: EQUAL-JITTERED (was the flat window)
              == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_offer_window_ms)));   // rand(1000, 2001); this TestHal draws `lo`

    // ---- POSITIVE CONTROL: the SAME node, the SAME OFFER, an ACCEPTED CLAIM ⇒ it still claim-stands and
    // adopts, exactly as before S1. Without this, "does not register" would be satisfied by a build that
    // can no longer register at all.
    {
        TestHal c; c._now = 100000;
        Node m2(c, /*node_id=*/0, /*key_hash32=*/0x0000C7C7u);
        CHECK(m2.on_init(cfg));
        c._busy_until = 0;                                           // idle channel -> immediate handoff
        m2.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off2{};
        const size_t on2 = make_j_offer_mobile(30, 0x0000C0C0u, /*local=*/202, /*target=*/0x0000C7C7u, off2);
        m2.on_recv(off2.data(), on2, meta);
        CHECK(m2.mobile_offers_n() == 1);
        c.tx_frames.clear(); c.events.clear();
        m2.on_timer(kMobileClaimGuardTimerId);
        CHECK(count_j_opcode(c.tx_frames, j_opcode::claim) == 1);    // ★ the CLAIM flew...
        CHECK(m2.mobile_registered());                               // ★ ...and the mobile adopted (unchanged)
        CHECK(m2.mobile_local_id() == 202);
        CHECK(c.count("mobile_adopted") == 1);
    }
}

// ---------------------------------------------------------------------------
// §MH-S1 §6.2 — A REFUSED OFFER IS REPORTED, NOT SILENTLY DROPPED.
//
// The host stages its OFFER at DISCOVER time and transmits it 100..1000 ms later on timer 80. `jtx_fire`
// discarded the transmitter's answer, so a definitively-refused OFFER vanished with an emit that claimed it
// had been sent as the last word on it.
//
// §6.2 allows either RESCHEDULING the entry or REPORTING AN EXPLICIT DROP. ⛔ S1 takes the second: a
// bounded reschedule needs a fresh jitter draw and S3 is the arc's only planned RNG re-anchor. The source
// mobile's own retry is then the backstop, exactly as §6.2 requires when the drop branch is taken.
//
// ★★ §MH-S1b (QA round 2) ADDED THE OTHER HALF, THE NAMING, AND THIS CASE ASSERTS IT: §10 says *"an event
// named `mobile_offer_tx` must not continue to mean only 'copied into a stash'"* — round 1 left that
// meaning intact. The staging emit is now `mobile_offer_scheduled`; `mobile_offer_tx` is raised in
// `lbt_complete` at the ACCEPTED HANDOFF. ⇒ the two are asserted here as a PAIR at each of the two
// instants, because either one alone can be satisfied by the wrong build: "scheduled fired" does not say
// the frame flew, and "tx fired" does not say it was not fired at staging time.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S1 (spec §6.2) — a staged OFFER refused by the host's transmitter is reported as an explicit drop") {
    TestHal hal; hal._now = 100000;
    NodeConfig cfg = join_cfg(); cfg.lbt_enabled = true;
    Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
    CHECK(host.on_init(cfg));
    CHECK(host.can_host_mobiles());

    // Stage the OFFER on a CLEAR channel (staging draws the jitter; it does not transmit).
    hal._busy_until = 0;
    CHECK(stage_mobile_offer(host, hal, /*mobile_hash=*/0x0000D5D5u) == 1);
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                 // PREMISE: staged, NOT transmitted
    // ★★ §10, HALF 1 — AT THE STASH THE LOG SAYS *SCHEDULED*, AND SAYS NOTHING ABOUT A TRANSMISSION.
    // (`stage_mobile_offer`'s return value is the `mobile_offer_scheduled` count; this is its twin.)
    CHECK(hal.count("mobile_offer_tx") == 0);                        // ← the whole point of the rename

    // Now reserve the medium and fill all four defer slots with unrelated initiating frames, so the OFFER's
    // own transmission attempt finds the ring full. The host-side initiating TX available here is the
    // presence ROSTER (`tx_initiating`, same ring) — it needs at least one hosted row to have anything to
    // say, which `test_add_host_mobile` seeds directly.
    const uint8_t ed[32] = {};
    host.test_add_host_mobile(/*key_hash32=*/0x0000E1E1u, /*local_id=*/100, ed);
    hal._busy_until = hal._now + 5000;
    hal.events.clear();
    for (int i = 0; i < 4; ++i) host.on_timer(kPresenceRosterTimerId);
    CHECK(hal.count("tx_lbt_defer") == 4);                           // PREMISE: ring full, none refused yet
    CHECK(hal.count("tx_lbt_defer_dropped") == 0);

    hal.events.clear(); hal.tx_frames.clear();
    CHECK(host.mobile_offer_reject_count() == 0);                    // PREMISE: the counter starts at zero
    fire_mobile_offer_timer(host, hal);

    // ★★ THE FIX: the refusal is REPORTED, and the frame is provably not on the wire.
    CHECK(hal.count("tx_lbt_defer_dropped") == 1);                   // PREMISE: the ring refused the OFFER
    CHECK(count_j_offer_mobile(hal.tx_frames) == 0);                 // ★ nothing transmitted...
    CHECK(hal.count("mobile_offer_dropped") == 1);                   // ★★ ...and it did NOT vanish silently
    CHECK(hal.count("mobile_offer_tx") == 0);                        // ★★ ...and NOTHING claimed it was sent
    // ★★★ [[B146]] §MH-S2b — AND THE COUNTER MOVED BY EXACTLY ONE. This is the IMMEDIATE `defer_full` arm; the
    // two HAL-rejection arms (immediate + DEFERRED) assert the same thing in the case below, and the deferred
    // one is the reading that used to be zero.
    CHECK(host.mobile_offer_reject_count() == 1);

    // ---- POSITIVE CONTROL: the SAME host, the SAME staging, an ACCEPTED transmission ⇒ the OFFER flies
    // and NO drop is reported. Without it, "a drop was reported" could be satisfied by a build that reports
    // a drop for every OFFER.
    {
        TestHal c; c._now = 100000;
        Node h2(c, /*node_id=*/43, /*key_hash32=*/0x00004343u);
        CHECK(h2.on_init(cfg));
        c._busy_until = 0;                                           // idle channel throughout
        CHECK(stage_mobile_offer(h2, c, /*mobile_hash=*/0x0000D6D6u) == 1);
        CHECK(c.count("mobile_offer_tx") == 0);                      // ★ still nothing claims a transmission
        c.events.clear(); c.tx_frames.clear();
        fire_mobile_offer_timer(h2, c);
        CHECK(count_j_offer_mobile(c.tx_frames) == 1);               // ★ transmitted
        CHECK(c.count("mobile_offer_dropped") == 0);                 // ★ and no drop reported
        CHECK(h2.mobile_offer_reject_count() == 0);                  // ★★ [[B146]]: an ACCEPTED OFFER counts ZERO
        // ★★ §10, HALF 2 — AND THE HONEST EVENT FIRES HERE, AT THE HANDOFF, EXACTLY ONCE. Paired with the
        // `== 0` above (same host, same OFFER, the two instants), so neither reading is available to a
        // build that emits it in the wrong place.
        CHECK(c.count("mobile_offer_tx") == 1);
        // ...and it DESCRIBES THE FRAME THAT FLEW, field by field, because it reads them back out of the
        // wire bytes rather than trusting what the caller staged. The comparison is against the PARSED
        // frame, not against a constant the test also supplied to the emit path.
        const Ev* tx_ev = c.find("mobile_offer_tx");
        CHECK(tx_ev != nullptr);
        auto flew = first_j(c.tx_frames, j_opcode::offer);
        CHECK(flew.has_value());
        if (tx_ev && flew) {
            CHECK(tx_ev->to_key   == static_cast<int64_t>(flew->target_key_hash32));
            CHECK(tx_ev->local_id == static_cast<int64_t>(flew->proposed_mobile_id));
            CHECK(flew->target_key_hash32 == 0x0000D6D6u);           // ...and that frame is the one staged
        }
    }
}

// ============================================================================
// ★★★ §MH-S1b (QA round 2) — THE HAL-REJECTION ARM, IMMEDIATE AND DEFERRED, FOR ALL THREE FRAMES.
//
// ⛔⛔ WHY THIS BLOCK EXISTS AT ALL: round 1 reported the `tx_rejected` half as METAL-ONLY, because
// `TestHal::tx` unconditionally answered `ok`. That was a statement about the HARNESS, not about
// reachability — `TestHal::tx_answer` is the one field that makes it testable, and `DeviceHal::tx`'s real
// `busy` (its 8-entry outbound ring full) is exactly what it reproduces. For a J DISCOVER/CLAIM/OFFER —
// `FrameTag::beacon`, `retry_slot_of` < 0, no stash, no MAC timeout — `tx_with_retry` turns that into
// `TxHandOff::rejected`, i.e. a DEFINITIVE loss with nothing behind it. That is §6.4's `tx_rejected`.
//
// ★★ AND THE DEFERRED ARM IS THE ONE THAT MATTERS MOST, because it is where round 1 was WRONG rather than
// merely incomplete: `tx_initiating` returns TRUE for a frame merely accepted into the LBT defer ring, and
// round 1 adopted the CLAIM on that `true`. If the HAL then refused the frame when the slot fired, the
// deferred-loss arm in node.cpp recognised only `mobile_discover` ⇒ the mobile sat FALSELY REGISTERED at a
// home that was never sent anything, with NO retry. Each pair below drives the SAME frame through both
// paths so the two cannot be confused.
//
// ★ EVERY CASE ASSERTS THE OBSERVABLE SIDE EFFECT: the parsed frames in `hal.tx_frames` (or their absence),
// the registration state read back through the public accessors, and the deadline actually armed. Events
// are premises ("the branch under test was reached"), never the verdict.
// ============================================================================

TEST_CASE("★★★ §MH-S1b (spec §6.1) — a DISCOVER the HAL REFUSES reports tx_rejected + a bounded retry, on BOTH paths") {
    // ---- ARM A: IMMEDIATE refusal (clear channel, the HAL says busy).
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000CA01u);
        CHECK(m.on_init(cfg));
        hal._busy_until = 0;                                         // no LBT defer: straight to the HAL
        hal.tx_answer = TxResult::busy;                              // ...which refuses (DeviceHal: outbound ring full)

        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.on_timer(kMobileDiscoverTimerId);

        // PREMISES — the frame really was OFFERED to the HAL (not skipped upstream) and really was refused.
        CHECK(hal.tx_calls - calls_before == 1);                     // it reached `_hal.tx` exactly once
        CHECK(hal.count("tx_lbt_defer") == 0);                       // ...by the immediate path, not the ring
        CHECK(hal.count("tx_hal_rejected") == 1);                    // ...and the HAL refused it
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);   // ★ nothing on the wire

        // ★★ THE STATE READ BACK: `tx_rejected`, NOT `defer_full` — the ring was never involved.
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);
        // ★★ NO WINDOW WAS OPENED, so no home can be blamed for our own radio (§6.1/§6.4, gate 20).
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);
        CHECK(hal.count("mobile_no_host") == 0);
        CHECK_FALSE(m.mobile_registered());
        CHECK(m.mobile_home_id() == 0);
        // ★★ AND THE BOUNDED RETRY IS ARMED (gate 6), at the deadline the code chose.
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);
        CHECK(hal.last_armed(kMobileDiscoverTimerId)                     // ★ §MH-S3 §5.2: EQUAL-JITTERED (was the flat window)
              == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_offer_window_ms)));   // rand(1000, 2001); this TestHal draws `lo`

        // ---- NEGATIVE CONTROL, SAME NODE: let the HAL answer `ok` and the identical call succeeds. Without
        // it, every assertion above is satisfied by a build that has stopped DISCOVERing altogether.
        hal.tx_answer = TxResult::ok;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileDiscoverTimerId);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 1);   // ★ on the wire this time
        CHECK(hal.count("tx_hal_rejected") == 0);
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 1);           // ★ and NOW the window opens
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);             // ★ and no admission retry is armed
    }

    // ---- ARM B: DEFERRED, then refused when the slot fires. `tx_initiating` already answered TRUE.
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000CA02u);
        CHECK(m.on_init(cfg));
        hal._busy_until = hal._now + 5000;                           // busy -> the DISCOVER is deferred, and ACCEPTED

        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileDiscoverTimerId);
        CHECK(hal.count("tx_lbt_defer") == 1);                       // PREMISE: accepted into the ring...
        CHECK(hal.count("tx_lbt_defer_dropped") == 0);
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);         // ★ ...so NOTHING was reported yet
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::none);

        // The medium clears, the slot fires — and the HAL has filled up in the meantime.
        hal._now = 100000 + 5000;
        hal.tx_answer = TxResult::busy;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.test_fire_lbt_defer(/*slot=*/0);

        // PREMISES — the deferred frame really was re-offered to the HAL, and really died there.
        CHECK(hal.tx_calls - calls_before == 1);
        CHECK(hal.count("tx_hal_rejected") == 1);
        CHECK(hal.count("tx_deferred_lost") == 1);                   // the generic no-silent-loss report
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);   // ★ never reached the air

        // ★★ THE FIX: the loss is ATTRIBUTED to the DISCOVER, so the window is not opened and the retry is.
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);
        CHECK(hal.last_armed(kMobileDiscoverTimerId)                     // ★ §MH-S3 §5.2: EQUAL-JITTERED (was the flat window)
              == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_offer_window_ms)));   // rand(1000, 2001); this TestHal draws `lo`
        CHECK(hal.count("mobile_no_host") == 0);
        CHECK_FALSE(m.mobile_registered());
    }
}

TEST_CASE("★★★ §MH-S1b (spec §6.3) — a DEFERRED CLAIM does not register the mobile until the HANDOFF, and never if the HAL refuses") {
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // A mobile with ONE collected OFFER and a busy medium, so its CLAIM is DEFERRED (accepted, not sent).
    // Returned by value is impossible (Node holds a Hal&), so this is a lambda over the two locals.
    auto stage_deferred_claim = [&](TestHal& hal, Node& m, uint32_t self_hash, uint8_t offered_id) {
        hal._busy_until = hal._now + 5000;
        m.on_timer(kMobileDiscoverTimerId);                          // DISCOVER: deferred (fills slot 0)
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(/*responder=*/30, /*resp_hash=*/0x0000C0C0u, offered_id,
                                              /*target=*/self_hash, off);
        m.on_recv(off.data(), on, meta);
        CHECK(m.mobile_offers_n() == 1);                             // PREMISE: the guard WILL take the CLAIM branch
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileClaimGuardTimerId);                        // CLAIM: deferred into slot 1
    };

    // ---- ARM A: the deferred CLAIM is later ACCEPTED ⇒ the mobile registers AT THAT MOMENT, not before.
    // ★★ THIS IS THE FIX'S DEFINING OBSERVATION and it is a TRANSITION, measured across one call: `false`
    // immediately after the CLAIM is accepted into the ring, `true` immediately after the slot fires. A
    // single end-state check could not tell the fix from the defect.
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000CB01u);
        CHECK(m.on_init(cfg));
        stage_deferred_claim(hal, m, 0x0000CB01u, /*offered_id=*/211);

        CHECK(hal.count("tx_lbt_defer") == 1);                       // PREMISE: the CLAIM was DEFERRED...
        CHECK(hal.count("tx_lbt_defer_dropped") == 0);               // ...and ACCEPTED (`tx_initiating` -> true)
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);  // ★ but it is NOT on the wire
        // ★★ AND THE MOBILE IS NOT REGISTERED. ← this was `true` before §MH-S1b: the "success that isn't".
        CHECK_FALSE(m.mobile_registered());
        CHECK(m.mobile_home_id() == 0);
        CHECK(m.mobile_local_id() == 0);
        CHECK(hal.count("mobile_adopted") == 0);                     // ★ and the app was NOT told otherwise

        // The medium clears and the slot fires with a working HAL: NOW it registers.
        hal._now = 100000 + 5000;
        hal.events.clear(); hal.tx_frames.clear();
        m.test_fire_lbt_defer(/*slot=*/1);                           // slot 0 = the DISCOVER, slot 1 = the CLAIM
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 1);  // ★ the CLAIM is on the air...
        auto c = first_j(hal.tx_frames, j_opcode::claim);
        CHECK(c.has_value());
        if (c) CHECK(c->proposed_node_id == 211);                    // ★ ...for the id that was offered
        CHECK(m.mobile_registered());                                // ★★ ...and ONLY NOW is it registered
        CHECK(m.mobile_home_id() == 30);
        CHECK(m.mobile_local_id() == 211);
        CHECK(hal.count("mobile_adopted") == 1);
    }

    // ---- ARM B: the deferred CLAIM is REFUSED BY THE HAL when the slot fires ⇒ still no registration, and
    // the loss is attributed to the CLAIM (a bounded LOCAL retry — §6.3: not evidence the home refused us).
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000CB02u);
        CHECK(m.on_init(cfg));
        stage_deferred_claim(hal, m, 0x0000CB02u, /*offered_id=*/212);
        CHECK(hal.count("tx_lbt_defer") == 1);                       // PREMISE: deferred and accepted
        CHECK_FALSE(m.mobile_registered());                          // (the ARM-A transition's first half)

        hal._now = 100000 + 5000;
        hal.tx_answer = TxResult::busy;                              // the HAL has filled up during the wait
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        m.test_fire_lbt_defer(/*slot=*/1);

        // PREMISES — the CLAIM really was re-offered and really died at the HAL.
        CHECK(hal.count("tx_hal_rejected") == 1);
        CHECK(hal.count("tx_deferred_lost") == 1);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);  // ★ never reached the air

        // ★★ THE FIX: no registration was ever created, and the app was never told there was one.
        CHECK_FALSE(m.mobile_registered());
        CHECK(m.mobile_home_id() == 0);
        CHECK(m.mobile_local_id() == 0);
        CHECK(hal.count("mobile_adopted") == 0);
        // ★★ CLASSIFIED AS OUR OWN TRANSMITTER (§6.3/§6.4, gate 20) WITH A BOUNDED RETRY (gate 6).
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);
        CHECK(hal.last_armed(kMobileDiscoverTimerId)                     // ★ §MH-S3 §5.2: EQUAL-JITTERED (was the flat window)
              == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_offer_window_ms)));   // rand(1000, 2001); this TestHal draws `lo`
    }

    // ---- ARM C: IMMEDIATE HAL refusal of the CLAIM (clear channel) ⇒ the same verdict by the other path.
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000CB03u);
        CHECK(m.on_init(cfg));
        hal._busy_until = 0;                                         // clear channel throughout
        m.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(30, 0x0000C0C0u, /*local=*/213, /*target=*/0x0000CB03u, off);
        m.on_recv(off.data(), on, meta);
        CHECK(m.mobile_offers_n() == 1);

        hal.tx_answer = TxResult::busy;                              // the HAL refuses the CLAIM outright
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.on_timer(kMobileClaimGuardTimerId);

        CHECK(hal.tx_calls - calls_before == 1);                     // PREMISE: offered to the HAL once
        CHECK(hal.count("tx_hal_rejected") == 1);                    // PREMISE: and refused
        CHECK(hal.count("tx_lbt_defer") == 0);                       // PREMISE: by the immediate path
        CHECK(hal.count("mobile_no_host") == 0);                     // not the no-OFFER branch
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);  // ★ nothing on the wire

        CHECK_FALSE(m.mobile_registered());                          // ★★ no registration
        CHECK(m.mobile_local_id() == 0);
        CHECK(hal.count("mobile_adopted") == 0);
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);
        CHECK(hal.last_armed(kMobileDiscoverTimerId)                     // ★ §MH-S3 §5.2: EQUAL-JITTERED (was the flat window)
              == static_cast<int64_t>(protocol::equal_jitter_lo(protocol::mobile_offer_window_ms)));   // rand(1000, 2001); this TestHal draws `lo`
    }
}

TEST_CASE("★★★ §MH-S1b (spec §6.2) — an OFFER the HAL REFUSES is reported as a drop on BOTH paths, and never as `mobile_offer_tx`") {
    NodeConfig cfg = join_cfg(); cfg.lbt_enabled = true;

    // ---- ARM A: IMMEDIATE refusal at timer 80 (clear channel, the HAL says busy).
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, /*node_id=*/42, /*key_hash32=*/0x00004242u);
        CHECK(host.on_init(cfg));
        CHECK(host.can_host_mobiles());
        hal._busy_until = 0;
        CHECK(stage_mobile_offer(host, hal, /*mobile_hash=*/0x0000DA01u) == 1);   // staged (scheduled)
        CHECK(hal.count("mobile_offer_tx") == 0);

        hal.tx_answer = TxResult::busy;
        hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        fire_mobile_offer_timer(host, hal);

        CHECK(hal.tx_calls - calls_before == 1);                     // PREMISE: offered to the HAL
        CHECK(hal.count("tx_hal_rejected") == 1);                    // PREMISE: refused
        CHECK(count_j_offer_mobile(hal.tx_frames) == 0);             // ★ nothing on the wire
        CHECK(hal.count("mobile_offer_dropped") == 1);               // ★★ reported, not vanished
        CHECK(hal.count("mobile_offer_tx") == 0);                    // ★★ and NOTHING claimed a transmission
        CHECK(host.mobile_offer_reject_count() == 1);                // ★★ [[B146]]: exactly ONE increment
    }

    // ---- ARM B: the OFFER is DEFERRED at timer 80 and the HAL refuses when the slot fires. Round 1 could
    // not attribute this at all — the deferred-loss arm knew only `mobile_discover`, so the host's OFFER
    // died with the generic line and the waiting mobile got no `mobile_offer_dropped`.
    {
        TestHal hal; hal._now = 100000;
        Node host(hal, /*node_id=*/44, /*key_hash32=*/0x00004444u);
        CHECK(host.on_init(cfg));
        hal._busy_until = 0;
        CHECK(stage_mobile_offer(host, hal, /*mobile_hash=*/0x0000DA02u) == 1);

        hal._busy_until = hal._now + 5000;                           // now reserve the medium: the fire DEFERS
        hal.events.clear(); hal.tx_frames.clear();
        fire_mobile_offer_timer(host, hal);
        CHECK(hal.count("tx_lbt_defer") == 1);                       // PREMISE: deferred and ACCEPTED
        CHECK(hal.count("tx_lbt_defer_dropped") == 0);
        CHECK(hal.count("mobile_offer_dropped") == 0);               // ★ nothing reported yet — correctly
        CHECK(hal.count("mobile_offer_tx") == 0);                    // ★ and nothing claimed sent yet either
        CHECK(count_j_offer_mobile(hal.tx_frames) == 0);
        CHECK(host.mobile_offer_reject_count() == 0);                // ★ ...and nothing counted yet either

        hal._now = 100000 + 5000;
        hal.tx_answer = TxResult::busy;
        hal.events.clear(); hal.tx_frames.clear();
        host.test_fire_lbt_defer(/*slot=*/0);
        CHECK(hal.count("tx_hal_rejected") == 1);                    // PREMISE: died at the HAL
        CHECK(hal.count("tx_deferred_lost") == 1);
        CHECK(count_j_offer_mobile(hal.tx_frames) == 0);             // ★ never reached the air
        CHECK(hal.count("mobile_offer_dropped") == 1);               // ★★ ATTRIBUTED to the OFFER
        CHECK(hal.count("mobile_offer_tx") == 0);                    // ★★ and never claimed sent
        // ★★★ [[B146]] §MH-S2b — THE READING THAT USED TO BE ZERO. This arm reached
        // `mobile_offer_admission_rejected` from `node.cpp`'s deferred-loss arm, which never touched the
        // counter, so the event fired and `mobile_offer_reject_count()` disagreed with it. The case asserted
        // the EVENT only, which is exactly why it could not see that.
        CHECK(host.mobile_offer_reject_count() == 1);

        // ---- POSITIVE CONTROL for this arm: the SAME deferred path with a working HAL emits the honest
        // `mobile_offer_tx` AT THE DEFER FIRE and no drop — so ARM B measures the refusal, not the defer.
        TestHal h2; h2._now = 100000;
        Node host2(h2, /*node_id=*/45, /*key_hash32=*/0x00004545u);
        CHECK(host2.on_init(cfg));
        h2._busy_until = 0;
        CHECK(stage_mobile_offer(host2, h2, /*mobile_hash=*/0x0000DA03u) == 1);
        h2._busy_until = h2._now + 5000;
        fire_mobile_offer_timer(host2, h2);
        CHECK(h2.count("tx_lbt_defer") == 1);
        h2._now = 100000 + 5000;
        h2.events.clear(); h2.tx_frames.clear();
        host2.test_fire_lbt_defer(/*slot=*/0);
        CHECK(count_j_offer_mobile(h2.tx_frames) == 1);              // ★ on the wire at the defer fire...
        CHECK(h2.count("mobile_offer_tx") == 1);                     // ★★ ...and the honest event fires THERE
        CHECK(h2.count("mobile_offer_dropped") == 0);
        CHECK(host2.mobile_offer_reject_count() == 0);               // ★★ [[B146]]: an ACCEPTED deferred OFFER counts ZERO
    }
}

// ============================================================================
// ★★★ [[B142]] (2026-08-07, §MH-S1c) — THE ABA: A STALE DEFERRED COMPLETION CONSUMES *NEWER* STATE.
//
// ⛔⛔ WHAT WAS WRONG, AND WHY THE PREVIOUS ROUND'S FIX DID NOT REACH IT. §MH-S1b correctly moved the CLAIM's
// adopt from the REQUEST to the accepted HANDOFF. But the two things that survive a defer carried NO
// TRANSACTION IDENTITY:
//   · `_mobile_offers[0]` + `_mobile_claim_pending` (node_mobile.cpp) — ONE global boolean, one shared slot;
//   · `lbt_complete` (node_mac.cpp) correlated a completion by `LbtKind` ALONE — no frame, no generation.
// ⇒ CLAIM A enters defer slot A · the operator runs `mobile register` again · DISCOVER B picks a DIFFERENT
//   host and CLAIM B is staged in the SAME slot behind the SAME bool · slot A fires first:
//     ACCEPTED ⇒ A's bytes go on the air and candidate **B** is adopted — registered at a home sent nothing;
//     REJECTED ⇒ the deferred-loss arm clears **B's** stage and arms a retry for an attempt that is gone, so
//                slot B can no longer complete at all.
// ★★ THE REJECTED PATH IS AS DESTRUCTIVE AS THE ACCEPTED ONE. That is why the fix is an early-out placed
//    BEFORE `tx_with_retry` that answers TRUE, and why these cases come in ACCEPTED/REJECTED pairs: a fix
//    that checked the generation only where the adopt happens would leave every REJECTED case red.
// ⚠ THE DEFERRED **DISCOVER** HAS THE SAME SHAPE (it can open a collect-OFFERs window, or report a rejection,
//   for a newer transaction), so both frames are covered — four cases, two paths each.
//
// ★★ EVERY ASSERTION IS AN OBSERVABLE SIDE EFFECT, NEVER THE FLAG: which frame reached the wire (parsed out
// of `hal.tx_frames`, by `proposed_node_id`/`chosen_host_id`, so ARM A's CLAIM and ARM B's are distinguished
// by CONTENT), which candidate was adopted (`mobile_home_id` / `mobile_local_id`), whether a guard or a retry
// was armed (`count_armed`), and what the attempt result says. ⛔ `_mobile_claim_pending` is the DEFECT — a
// test that asserted it would be asserting the thing that cannot tell the two transactions apart.
// ★ Each case ends by driving the NEWER transaction to completion: "slot B can still complete correctly" is
//   half the requirement, and it is the assertion the destructive-rejection failure mode actually breaks.
// ============================================================================

namespace {
// Drive an operator `mobile register` and let the resulting DISCOVER fire. (The console verb arms the
// one-shot + `after(0, …)`; the in-memory Hal never auto-fires, so the case must run the timer itself.)
void operator_reregister(Node& m) { m.mobile_register_current(); m.on_timer(kMobileDiscoverTimerId); }
// Feed `m` a targeted mobile OFFER from `host`/`host_hash` proposing `local_id`.
void feed_offer(Node& m, uint8_t host, uint32_t host_hash, uint8_t local_id, uint32_t self_hash) {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    std::array<uint8_t, 16> off{};
    const size_t n = make_j_offer_mobile(host, host_hash, local_id, self_hash, off);
    m.on_recv(off.data(), n, meta);
}
}  // namespace

TEST_CASE("★★★ [[B142]]/1 ACCEPTED — a STALE deferred DISCOVER neither transmits nor opens a window for the NEWER transaction") {
    // The stale slot fires with a WORKING HAL — the "accepted" path.
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000BA01u);
        CHECK(m.on_init(cfg));

        hal._busy_until = hal._now + 5000;                           // busy -> DISCOVER A is DEFERRED into slot 0
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileDiscoverTimerId);
        CHECK(hal.count("tx_lbt_defer") == 1);                       // PREMISE: transaction A is in the ring...
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);   // ...and nothing has aired

        // THE OPERATOR RE-REGISTERS on a now-clear channel: transaction B DISCOVERs immediately and opens ITS
        // window. Slot A is still pending and now belongs to nobody.
        hal._busy_until = 0;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        operator_reregister(m);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 1);       // PREMISE: B is on the air
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 1);               // PREMISE: B's window is open

        // ★★ THE STALE SLOT FIRES. It must do NOTHING AT ALL.
        hal._now = 100000 + 5000;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.test_fire_lbt_defer(/*slot=*/0);

        CHECK(hal.tx_calls - calls_before == 0);                     // ★ NOT TRANSMITTED — the HAL was never asked
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);       // ★ NO GUARD ARMED — B's window is not doubled
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);         // ★ NO RETRY SCHEDULED
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::none);   // ★ B's transaction is not reported on
        CHECK(hal.count("tx_deferred_lost") == 0);                   // ★ NOT a loss — a deliberate cancel
        CHECK(hal.count("mobile_tx_cancelled_stale") == 1);          // PREMISE: the cancel branch was the one taken

        // ★ AND TRANSACTION B STILL COMPLETES CORRECTLY — the requirement's other half.
        feed_offer(m, /*host=*/31, /*host_hash=*/0x0000C1C1u, /*local_id=*/212, /*self=*/0x0000BA01u);
        CHECK(m.mobile_offers_n() == 1);
        hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileClaimGuardTimerId);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 1);
        CHECK(m.mobile_registered());
        CHECK(m.mobile_home_id() == 31);
        CHECK(m.mobile_local_id() == 212);
    }

}

TEST_CASE("★★★ [[B142]]/2 REJECTED — a STALE deferred DISCOVER that the HAL would refuse reports NOTHING and retries NOTHING") {
    // ★★ The destructive half: pre-fix this reported OUR OWN transmitter as having refused transaction B and
    // armed B a retry it never asked for (and `mobile_admission_rejected` re-arms the one-shot, so that retry
    // then re-DISCOVERs and destroys B's open window).
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000BA02u);
        CHECK(m.on_init(cfg));

        hal._busy_until = hal._now + 5000;
        m.on_timer(kMobileDiscoverTimerId);                          // DISCOVER A -> slot 0
        CHECK(hal.count("tx_lbt_defer") == 1);

        hal._busy_until = 0;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        operator_reregister(m);                                      // DISCOVER B, immediate
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 1);
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 1);

        hal._now = 100000 + 5000;
        hal.tx_answer = TxResult::busy;                              // the HAL has filled up during the wait
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.test_fire_lbt_defer(/*slot=*/0);

        CHECK(hal.tx_calls - calls_before == 0);                     // ★ the HAL is never even asked...
        CHECK(hal.count("tx_hal_rejected") == 0);                    // ★ ...so there is no refusal to attribute
        CHECK(hal.count("tx_deferred_lost") == 0);
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::none);   // ★★ B IS NOT REPORTED AS REFUSED
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);         // ★★ AND NO RETRY IS ARMED FOR IT
        CHECK(hal.count_armed(kMobileClaimGuardTimerId) == 0);
        CHECK(hal.count("mobile_tx_cancelled_stale") == 1);          // PREMISE: the cancel branch

        // ★ B's window survives intact and completes, with a working HAL again.
        hal.tx_answer = TxResult::ok;
        feed_offer(m, 31, 0x0000C1C1u, /*local_id=*/213, /*self=*/0x0000BA02u);
        CHECK(m.mobile_offers_n() == 1);
        hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileClaimGuardTimerId);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 1);
        CHECK(m.mobile_registered());
        CHECK(m.mobile_local_id() == 213);
    }
}

// Stage: DISCOVER (clear) -> OFFER from `host` -> guard fires on a BUSY medium, so the CLAIM is DEFERRED.
// A free function, not a per-case lambda: both CLAIM cases below need the identical staging, and Node holds
// a Hal& so the two objects must be passed in rather than returned.
namespace {
void stage_claim_deferred(TestHal& hal, Node& m, uint8_t host, uint32_t host_hash,
                          uint8_t local_id, uint32_t self_hash) {
    hal._busy_until = 0;
    m.on_timer(kMobileDiscoverTimerId);                              // DISCOVER: immediate, opens the window
    feed_offer(m, host, host_hash, local_id, self_hash);
    CHECK(m.mobile_offers_n() == 1);                                 // PREMISE: the guard WILL take the CLAIM branch
    hal._busy_until = hal._now + 5000;                               // busy: the CLAIM is DEFERRED, not sent
    m.on_timer(kMobileClaimGuardTimerId);
}
}  // namespace

TEST_CASE("★★★ [[B142]]/3 ACCEPTED — a STALE deferred CLAIM neither airs ITS frame nor adopts the NEWER transaction's candidate") {
    // ★★★ THE ABA IN ITS PUREST FORM: pre-fix this transmitted CLAIM **A** (proposed 211, host 30) while
    // adopting candidate **B** (212, host 31) — a registration at a home that was sent nothing.
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000BB01u);
        CHECK(m.on_init(cfg));

        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        stage_claim_deferred(hal, m, /*host=*/30, 0x0000C0C0u, /*local_id=*/211, /*self=*/0x0000BB01u);
        CHECK(hal.count("tx_lbt_defer") == 1);                       // PREMISE: CLAIM A is in slot 0...
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);  // ...and has NOT aired
        CHECK_FALSE(m.mobile_registered());                          // ...and has NOT registered anything (§MH-S1b)

        // THE OPERATOR RE-REGISTERS. Transaction B DISCOVERs on a clear channel, collects a DIFFERENT host's
        // OFFER, and stages CLAIM B — into the SAME `_mobile_offers[0]`, behind the SAME bool, in slot 1.
        hal._now = 100000 + 1000;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        hal._busy_until = 0;
        operator_reregister(m);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 1);   // PREMISE: B's DISCOVER aired
        feed_offer(m, /*host=*/31, 0x0000C1C1u, /*local_id=*/212, /*self=*/0x0000BB01u);
        CHECK(m.mobile_offers_n() == 1);                             // PREMISE: A's collected OFFER is GONE
        hal._busy_until = hal._now + 5000;
        hal.events.clear(); hal.tx_frames.clear();
        m.on_timer(kMobileClaimGuardTimerId);                        // CLAIM B -> slot 1 (slot 0 is still A)
        CHECK(hal.count("tx_lbt_defer") == 1);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);

        // ★★★ THE STALE SLOT FIRES FIRST.
        hal._now = 100000 + 6000;
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.test_fire_lbt_defer(/*slot=*/0);

        CHECK(hal.tx_calls - calls_before == 0);                     // ★ CLAIM A IS NOT TRANSMITTED
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 0);
        CHECK_FALSE(m.mobile_registered());                          // ★★ AND CANDIDATE B IS NOT ADOPTED
        CHECK(m.mobile_home_id() == 0);
        CHECK(m.mobile_local_id() == 0);
        CHECK(hal.count("mobile_adopted") == 0);                     // ★ the app was told nothing
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);         // ★ nothing rescheduled
        CHECK(hal.count("tx_deferred_lost") == 0);
        CHECK(hal.count("mobile_tx_cancelled_stale") == 1);          // PREMISE: the cancel branch was taken

        // ★★ AND SLOT B STILL COMPLETES CORRECTLY — with B's frame, at B's host, for B's id.
        hal.events.clear(); hal.tx_frames.clear();
        m.test_fire_lbt_defer(/*slot=*/1);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 1);
        auto c = first_j(hal.tx_frames, j_opcode::claim);
        CHECK(c.has_value());
        if (c) { CHECK(c->proposed_node_id == 212);                  // ★ B's id, NOT A's 211
                 CHECK(c->chosen_host_id  == 31); }                  // ★ B's host, NOT A's 30
        CHECK(m.mobile_registered());
        CHECK(m.mobile_home_id() == 31);
        CHECK(m.mobile_local_id() == 212);
        CHECK(hal.count("mobile_adopted") == 1);
    }

}

TEST_CASE("★★★ [[B142]]/4 REJECTED — a STALE deferred CLAIM the HAL would refuse does not destroy the NEWER transaction's stage") {
    // ★★★ THE CONTROL THAT SEPARATES THE TWO PATHS: a fix that checked the generation only where the ADOPT
    // happens would leave this red — the deferred-loss arm would still clear **B's** stage and arm a retry,
    // and slot B would then fly a CLAIM that registers NOTHING.
    {
        TestHal hal; hal._now = 100000;
        NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;
        Node m(hal, /*node_id=*/0, /*key_hash32=*/0x0000BB02u);
        CHECK(m.on_init(cfg));

        stage_claim_deferred(hal, m, /*host=*/30, 0x0000C0C0u, /*local_id=*/221, /*self=*/0x0000BB02u);
        CHECK_FALSE(m.mobile_registered());

        hal._now = 100000 + 1000;
        hal._busy_until = 0;
        hal.events.clear(); hal.tx_frames.clear();
        operator_reregister(m);
        feed_offer(m, /*host=*/31, 0x0000C1C1u, /*local_id=*/222, /*self=*/0x0000BB02u);
        CHECK(m.mobile_offers_n() == 1);
        hal._busy_until = hal._now + 5000;
        m.on_timer(kMobileClaimGuardTimerId);                        // CLAIM B -> slot 1

        hal._now = 100000 + 6000;
        hal.tx_answer = TxResult::busy;                              // the HAL would refuse the stale frame
        hal.armed.clear(); hal.events.clear(); hal.tx_frames.clear();
        const int calls_before = hal.tx_calls;
        m.test_fire_lbt_defer(/*slot=*/0);

        CHECK(hal.tx_calls - calls_before == 0);                     // ★ never offered — cancelled before the HAL
        CHECK(hal.count("tx_hal_rejected") == 0);
        CHECK(hal.count("tx_deferred_lost") == 0);                   // ★ NOT reported as a loss...
        CHECK(m.mobile_last_result() == Node::MobileAttemptResult::none);   // ★★ ...so B is not blamed...
        CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);         // ★★ ...and B gets no retry it did not ask for
        CHECK_FALSE(m.mobile_registered());
        CHECK(hal.count("mobile_tx_cancelled_stale") == 1);          // PREMISE: the cancel branch

        // ★★★ THE PROOF THAT NEWER STATE SURVIVED: slot B, with a working HAL, still ADOPTS. Pre-fix the
        // rejected stale slot had cleared the shared stage, so this CLAIM aired and registered NOTHING.
        hal.tx_answer = TxResult::ok;
        hal.events.clear(); hal.tx_frames.clear();
        m.test_fire_lbt_defer(/*slot=*/1);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == 1);
        auto c = first_j(hal.tx_frames, j_opcode::claim);
        CHECK(c.has_value());
        if (c) { CHECK(c->proposed_node_id == 222); CHECK(c->chosen_host_id == 31); }
        CHECK(m.mobile_registered());                                // ★★ the adopt that the defect destroyed
        CHECK(m.mobile_home_id() == 31);
        CHECK(m.mobile_local_id() == 222);
        CHECK(hal.count("mobile_adopted") == 1);
    }
}

// ---------------------------------------------------------------------------
// ★★★★ §S0-4 — REWRITTEN IN PLACE BY §MH-S4 (2026-08-08). Spec §2.4 / §7.1, gate items 7 and 8.
//
// ⛔ NOT DELETED AND NOT DISABLED (B101). This case was committed GREEN by §S0 asserting TODAY'S DEFECTIVE
// BEHAVIOUR, and §MH-S4 is the slice that owned its rewrite. The diff of this block IS the behaviour change.
//
// WHAT IT USED TO ASSERT, verbatim in intent, so the change is readable without `git log`:
//   · `CHECK(mob.mobile_registered())` right after a DROPPED CLAIM — the mobile believed it was registered;
//   · `CHECK(reg_true == 1)` — the app-facing `mobile_reg{registered:true}` push, emitted unconfirmed;
//   · `CHECK(count_j_opcode(..., claim) == claims_at_adopt)` FOUR TIMES across a full presence cycle — "NOT ONE
//     re-CLAIM", because `presence_claim_max_retries` had zero consumers and `_presence_reg_confirmed` had zero
//     readers, so neither could heal a CLAIM lost to an RX collision;
//   · `CHECK(elapsed == 135000ull)` — the quantified window of false `registered:true`, ended not by a re-CLAIM
//     but by HOME LOST plus a full re-DISCOVER.
//
// WHAT IT ASSERTS NOW (§7.1 steps 1-6):
//   · the CLAIM is transmitted and the offered id is adopted PROVISIONALLY — state `claiming`, NOT `attached`;
//   · ⛔ the app is told NOTHING: zero `registered:true` pushes, and `mobile_attached()` is false throughout;
//   · every confirmation deadline re-sends THE SAME CLAIM — same chosen host, same local id, SAME EPOCH — up to
//     `presence_claim_max_retries` (3), so the dropped CLAIM has three chances to heal that it never had;
//   · exhaustion returns the FSM to `seeking` with `last_result = claim_unconfirmed`, never to a false
//     registration and NOT via `presence_home_lost`;
//   · the HOME-LINK plane is never moved to `confirmed` (nor, while claiming, to `checking`/`lost`) — nothing
//     about the home was ever measured (§4.1/§6.4).
//
// ★ ONE NUMBER DELIBERATELY DID NOT CHANGE IN §MH-S4, because pretending otherwise would have been the
//   interesting lie: the give-up boundary stayed 135 000 ms (T=120 s + 3 x 5 s). That was never the defect.
//
// ★★★★ EXTENDED IN PLACE AGAIN BY §MH-S4b (2026-08-08), and THIS time the boundary moves — to 60 000 ms — because
// the reason it was 135 s was itself a defect. §MH-S4 armed the STEADY check period as the confirmation deadline and
// then spent a re-CLAIM in the same callback that sent the probe, so (a) nothing asked for 120 s and (b) the ask
// could not be answered before the verdict. §7.1 step 3 asks for a SHORT SEARCHING probe followed by time for its
// roster; that is now what happens, in TWO deadlines per round, and the substate `claim_solicited` is what tells
// "we asked and are waiting" from "we asked and were answered". SIDE C is rewritten around that pair; ⛔ nothing
// was deleted, and the §MH-S4 assertions that still hold are kept verbatim.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §S0-4 REWRITTEN (§MH-S4 §7.1, EXTENDED §MH-S4b) — a DROPPED CLAIM yields `claiming`, a SHORT SEARCHING solicitation, three same-epoch re-CLAIMs, then `seeking` — never a false registration") {
    constexpr uint32_t kMob  = 0x0000B7B7u;
    constexpr uint32_t kHome = 0x00004242u;
    constexpr uint8_t  kHomeId = 42;
    RxMeta meta{8.0f, -80.0f, 0, -1};

    TestHal hm; hm._now = 100000;                                    // the MOBILE's world
    TestHal hh; hh._now = 100000;                                    // the HOME's world
    Node mob (hm, /*node_id=*/0,       kMob);
    Node home(hh, /*node_id=*/kHomeId, kHome);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(home.on_init(join_cfg()));
    CHECK(home.can_host_mobiles());
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::seeking);   // §MH-S4 §4.1: autoregister ON -> `seeking` from boot
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);
    CHECK_FALSE(mob.mobile_home_confirmed_ever());

    // --- a REAL handshake, driven frame by frame, so the CLAIM under test is the genuine article.
    mob.on_timer(kMobileDiscoverTimerId);
    auto disc = first_j(hm.tx_frames, j_opcode::discover);
    CHECK(disc.has_value());                                         // PREMISE: the mobile really DISCOVERed
    home.on_recv(hm.tx_frames.back().data(), hm.tx_frames.back().size(), meta);
    hh.tx_frames.clear();
    fire_mobile_offer_timer(home, hh);
    CHECK(count_j_offer_mobile(hh.tx_frames) == 1);                  // PREMISE: the home really OFFERed
    auto off = first_j(hh.tx_frames, j_opcode::offer);
    CHECK(off.has_value());
    const uint8_t offered_id = off ? off->proposed_mobile_id : uint8_t(0);
    // PREMISE: a real local id was offered. PINNED EXACTLY, not `!= 0` — `find_free_mobile_id`
    // (node_join.cpp:73) walks TOP-DOWN from 254 and this home's registry is empty, so 254 is the only
    // correct answer. `!= 0` would also have accepted a picker that returned the home's own id, a denied
    // id, or a reserved one.
    CHECK(offered_id == 254);
    mob.on_recv(hh.tx_frames.back().data(), hh.tx_frames.back().size(), meta);

    hm.tx_frames.clear();
    mob.on_timer(kMobileClaimGuardTimerId);                          // window close -> CLAIM + PROVISIONAL adopt

    // PREMISE — the mobile really transmitted a well-formed CLAIM aimed at THIS home. The defect being fixed is
    // that the CLAIM was LOST, so the frame must demonstrably exist before we drop it.
    auto claim = first_j(hm.tx_frames, j_opcode::claim);
    CHECK(claim.has_value());
    if (claim) { CHECK(claim->chosen_host_id == kHomeId);
                 CHECK(claim->proposed_node_id == offered_id);
                 CHECK(claim->key_hash32 == kMob); }
    CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == 1);
    const uint8_t claim_epoch = claim ? claim->claim_epoch : uint8_t(0xFF);
    CHECK(claim_epoch == 1);                                         // PINNED: the first CLAIM of a fresh mobile carries epoch 1

    // ⛔ THE DROP: the CLAIM is never delivered to `home` (an RX collision on real air).

    // ★★★★ SIDE A — THE FIX. The mobile is `claiming`, and THE APP IS TOLD NOTHING.
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);   // ← was: mobile_registered() read as a registration
    CHECK_FALSE(mob.mobile_attached());                              // ★★ the app-facing truth: NOT registered
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);   // ★ nothing about the home has been measured
    CHECK_FALSE(mob.mobile_home_confirmed_ever());                   // ★ so there is no confirmation age to render
    CHECK(mob.mobile_claim_retries() == 0);                          // the budget is untouched so far
    // ⓘ The PROVISIONAL adoption is intended and is asserted, not glossed over (§7.1 step 1): the mobile really
    //   does operate under the offered local id — that is what lets a home answer it at all — and
    //   `mobile_registered()` is the LINK-LAYER flag that says so. What changed is that this flag is no longer
    //   what any surface reads.
    CHECK(mob.mobile_registered());
    CHECK(mob.mobile_home_id() == kHomeId);
    CHECK(mob.node_id() == offered_id);
    {
        int reg_true = 0;
        Push p{}; while (mob.next_push(p)) if (p.kind == PushKind::mobile_reg && p.relayed) ++reg_true;
        CHECK(reg_true == 0);                                        // ★★★ was `== 1`: the unconfirmed app-facing push is GONE
    }

    // SIDE B — the home still has no row and says so on the wire (unchanged premise: the CLAIM really was lost).
    CHECK(home.mobile_reg_count() == 0);
    hh.tx_frames.clear();
    home.on_timer(kPresenceRosterTimerId);
    CHECK(count_p_rosters(hh.tx_frames) == 0);                       // an empty registry emits no roster at all
    CHECK_FALSE(roster_entry_for(hh.tx_frames, kMob).has_value());

    // ★★★★ SIDE C — AND NOW IT HEALS ITSELF. Drive the confirmation deadlines the mobile armed at adopt and show
    // that EACH ONE re-sends THE SAME CLAIM (§7.1 steps 5-6), which is the exact opposite of what this case
    // previously proved. ⚠ The home is deliberately still deaf, so the re-CLAIMs go unanswered — this measures the
    // RETRY MACHINERY, not the healing (the healing has its own case below).
    // ★★★★ §MH-S4b §7.1 step 3 — THE DEADLINE IS NOW **SHORT** AND A CLAIMING ROUND IS **TWO** FIRES, NOT ONE.
    // §MH-S4 armed the STEADY check period here (`presence_check_base_ms`, 120 000 ms) and then, in the very
    // callback that sent the probe, spent a re-CLAIM — so the probe could not possibly be answered first and, worse,
    // it was a SELECTED probe, which a home that missed the CLAIM is required to IGNORE. Both halves are fixed:
    // the ask is a SHORT SEARCHING probe, and the verdict waits for its own deadline.
    CHECK(hm.last_armed(kPresenceProbeTimerId) == static_cast<int64_t>(protocol::presence_claim_solicit_ms));
    CHECK_FALSE(mob.mobile_claim_solicited());
    uint64_t elapsed = 0;
    int probes = 0;
    // ---- the SOLICITATION half, factored so the loop below and the exhaustion round below share ONE definition of
    //      "ask and wait" (U1). It asserts the two things that make the ask real: a probe on the wire, SEARCHING.
    auto solicit_round = [&](int expect_claims) {
        hm._now += protocol::presence_claim_solicit_ms; elapsed += protocol::presence_claim_solicit_ms;
        hm.events.clear();
        const int probes_before = count_p_probes(hm.tx_frames);
        mob.on_timer(kPresenceProbeTimerId);
        probes += hm.count("presence_probe_tx");
        CHECK(count_p_probes(hm.tx_frames) == probes_before + 1);     // PREMISE: a probe really went on the wire
        auto pr = last_p_probe(hm.tx_frames);
        CHECK(pr.has_value());
        if (pr) {
            CHECK(pr->searching());                                  // ★★★★ §7.1 step 3: SEARCHING — was SELECTED, which the
            CHECK(pr->selected_home_id == 0);                        //      home that missed the CLAIM must IGNORE
            CHECK(pr->key_hash32 == kMob);                           // ★ …but it is still OUR probe
        }
        CHECK(mob.mobile_claim_solicited());                         // ★★ "we asked and are waiting" — the substate
        CHECK(hm.last_armed(kPresenceProbeTimerId) == static_cast<int64_t>(protocol::presence_claim_confirm_ms));
        CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == expect_claims);   // ★★★ the ask spends NO re-CLAIM
    };
    for (int i = 0; i < static_cast<int>(protocol::presence_claim_max_retries); ++i) {
        solicit_round(/*expect_claims=*/i + 1);                      // ask (1 original + i re-CLAIMs so far)
        CHECK(mob.mobile_claim_retries() == i);                       // ★★ …and the budget is STILL untouched by the ask
        // ---- the CONFIRMATION half: the roster window expired in silence, so ONE re-CLAIM is spent.
        hm._now += protocol::presence_claim_confirm_ms; elapsed += protocol::presence_claim_confirm_ms;
        hm.events.clear();
        const int probes_before = count_p_probes(hm.tx_frames);
        mob.on_timer(kPresenceProbeTimerId);
        CHECK(count_p_probes(hm.tx_frames) == probes_before);        // ★★ NO second probe: the verdict does not re-ask
        // ★★ THE RE-CLAIM, ASSERTED ON THE WIRE — not on a counter, and not on the emit.
        CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == i + 2);            // 1 original + (i+1) re-CLAIMs
        auto rc = last_j(hm.tx_frames, j_opcode::claim);
        CHECK(rc.has_value());
        if (rc) {
            CHECK(rc->chosen_host_id    == kHomeId);                 // ★ THE SAME chosen host
            CHECK(rc->proposed_node_id  == offered_id);              // ★ THE SAME local id — no re-draw
            CHECK(rc->claim_epoch       == claim_epoch);             // ★★ THE SAME EPOCH — this is what makes it "the same CLAIM"
            CHECK(rc->key_hash32        == kMob);
        }
        CHECK(mob.mobile_claim_retries() == i + 1);                   // the budget is spent by the TRANSMISSION
        CHECK_FALSE(mob.mobile_claim_solicited());                    // ★ the next round asks again
        CHECK(hm.last_armed(kPresenceProbeTimerId) == static_cast<int64_t>(protocol::presence_claim_solicit_ms));
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);
        CHECK_FALSE(mob.mobile_attached());                          // ★ still never registered, the whole time
        CHECK(hm.count("presence_home_lost") == 0);                  // ★★ and NOT via the home-lost ladder
        CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);   // ★ nothing measured about the home -> plane unmoved
    }
    CHECK(probes == static_cast<int>(protocol::presence_claim_max_retries));      // PREMISE: the probes really fired too
    CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == 1 + protocol::presence_claim_max_retries);
    // ★★★ EXHAUSTION — "reset and return to `seeking` rather than remaining falsely registered" (§7.1 step 6).
    // ⓘ It takes a FULL round: the budget is exhausted, but the mobile still ASKS one last time before declaring
    //   silence — which is right, because a roster arriving in that window would still confirm it.
    solicit_round(/*expect_claims=*/1 + protocol::presence_claim_max_retries);   // the fourth and last ask (probes -> 4)
    CHECK(probes == static_cast<int>(protocol::presence_claim_max_retries) + 1);
    hm._now += protocol::presence_claim_confirm_ms; elapsed += protocol::presence_claim_confirm_ms;
    hm.events.clear();
    mob.on_timer(kPresenceProbeTimerId);
    CHECK(hm.count("mobile_claim_exhausted") == 1);                  // PREMISE: the exhaustion branch, not some other reset
    CHECK(hm.count("presence_home_lost") == 0);                      // ★★ was the ONLY way out before; it is no longer used
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::seeking);   // ★★★ `seeking`, NOT `recovering` — there was never an attachment to recover
    CHECK_FALSE(mob.mobile_attached());
    CHECK_FALSE(mob.mobile_registered());
    CHECK(mob.mobile_last_result() == Node::MobileAttemptResult::claim_unconfirmed);   // §10's code, now actually reachable
    CHECK(mob.mobile_claim_retries() == 0);                          // budget released for the next attachment
    CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == 1 + protocol::presence_claim_max_retries);   // ★ exhaustion does NOT send a 5th
    CHECK(hm.count_armed(kMobileDiscoverTimerId) >= 1);              // ★ a FULL re-DISCOVER is armed (fresh id, fresh epoch)
    CHECK_FALSE(mob.mobile_claim_solicited());                       // ★ the session is over — no ask is outstanding
    // ★★★★ §MH-S4b — **AND THE WINDOW HAS NOW CHANGED, HAVING DELIBERATELY NOT CHANGED IN §MH-S4.** §MH-S4 kept the
    // pre-existing 135 000 ms (T = 120 s + 3 × 5 s) and said so, because the boundary was never the defect. §MH-S4b
    // moves it, and the arithmetic is stated rather than merely re-pinned: FOUR rounds of
    // (`presence_claim_solicit_ms` 3 000 + `presence_claim_confirm_ms` 12 000) = 60 000 ms.
    // ★ THE FIGURE THAT ACTUALLY MATTERS IS THE FIRST ONE, NOT THE LAST: the FIRST confirmation opportunity is now
    //   at ~3 s (and in a live network usually earlier still — the home schedules a roster when it RECORDS the
    //   CLAIM), where §MH-S4 asked nothing at all for 120 s. Faster give-up is a side effect; a fast ASK is the fix.
    CHECK(elapsed == 4ull * (protocol::presence_claim_solicit_ms + protocol::presence_claim_confirm_ms));
    CHECK(elapsed == 60000ull);                                      // ★ spelled out too, so the constants cannot drift unnoticed
    {   // ★★★ THE HEADLINE, RE-STATED AS THE APP SEES IT: across the entire 135 s the companion received not one
        // `registered:true`. This is the assertion the whole slice exists for.
        int reg_true = 0, reg_false = 0;
        Push p{}; while (mob.next_push(p)) if (p.kind == PushKind::mobile_reg) { if (p.relayed) ++reg_true; else ++reg_false; }
        CHECK(reg_true == 0);
        CHECK(reg_false == 0);                                       // ★ and no unpaired `registered:false` either (symmetry)
    }

    // ---- POSITIVE CONTROL (RETAINED VERBATIM): had the CLAIM landed, the home WOULD hold the row and WOULD
    // roster it. This is what makes "the home has no row" a measurement of the loss rather than of a harness that
    // cannot register.
    {
        TestHal hc; hc._now = 100000;
        Node h2(hc, /*node_id=*/kHomeId, kHome);
        CHECK(h2.on_init(join_cfg()));
        std::array<uint8_t, 16> cl{};
        const size_t cn = make_j_claim_mobile(kHomeId, offered_id, kMob, cl);
        h2.on_recv(cl.data(), cn, meta);
        CHECK(h2.mobile_reg_count() == 1);
        hc.tx_frames.clear();
        h2.on_timer(kPresenceRosterTimerId);
        auto e = roster_entry_for(hc.tx_frames, kMob);
        CHECK(e.has_value());                                        // ★ a REAL registration is visible on the wire
        if (e) CHECK(e->local_id == offered_id);
    }
}


// ============================================================================
// ★★★★ §MH-S4 — THE CONFIRMED-ATTACHMENT FSM AND THE TWO INDEPENDENT PLANES.
// Spec §4.1 (three planes) · §4.2 (mobile_autoregister policy) · §6.4 + [[B139]] (admission is a LOCAL plane)
// · §7.1 (confirmation without a new wire message) · §7.2 (link-confidence rules) · §10 (diagnostics).
// Gate items 7, 8, 9, 10, 20, 21, 22, 23, plus the identity-is-the-TRIPLE requirement.
//
// ★ WHAT EVERY CASE BELOW ASSERTS: the OBSERVABLE SIDE EFFECT — the parsed frame on the wire, the emitted
// push, the rendered state — never an internal flag alone and never two constants the case itself assigned.
// Events appear as PREMISES ("the branch under test was really reached"), never as the verdict.
// ============================================================================
namespace {

// Bring a fresh mobile all the way to CONFIRMED `attached` through the real handshake: DISCOVER -> OFFER ->
// CLAIM -> chosen-home roster carrying the triple. Returns the offered/adopted local id.
// ⓘ The OFFER is synthesized (`make_j_offer_mobile`) rather than driven from a second Node, deliberately: these
//   cases are about the MOBILE's FSM, and a synthetic OFFER lets each one pin the local id it wants to test with.
//   The §S0-4 rewrite above drives a REAL home end-to-end, which is where that coverage lives.
uint8_t attach_mobile_confirmed(Node& mob, TestHal& hal, uint8_t home_id, uint32_t home_hash,
                                uint8_t local_id, uint32_t mob_hash, const RxMeta& meta) {
    mob.on_timer(kMobileDiscoverTimerId);
    std::array<uint8_t, 16> off{};
    const size_t on = make_j_offer_mobile(home_id, home_hash, local_id, mob_hash, off);
    mob.on_recv(off.data(), on, meta);
    mob.on_timer(kMobileClaimGuardTimerId);                          // CLAIM + provisional adopt -> `claiming`
    confirm_mobile_via_roster(mob, hal, home_id, mob.config().layers[0].layer_id,
                              mob_hash, local_id, /*epoch=*/1, meta);
    return local_id;
}

}  // namespace

// ---------------------------------------------------------------------------
// GATE 7 — "a lost first CLAIM is healed by same-epoch re-CLAIM and roster confirmation."
// The §S0-4 rewrite proves the retry machinery against a permanently deaf home; THIS case closes the loop with
// a REAL home that hears the SECOND CLAIM, and asserts the healing on the home's own wire output.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 gate 7 (§7.1) — a LOST first CLAIM is healed by the same-epoch re-CLAIM: the home records it and the roster confirms") {
    constexpr uint32_t kMob = 0x0000C7C7u, kHome = 0x00004242u;
    constexpr uint8_t  kHomeId = 42;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hm; hm._now = 100000;
    TestHal hh; hh._now = 100000;
    Node mob (hm, /*node_id=*/0,       kMob);
    Node home(hh, /*node_id=*/kHomeId, kHome);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(home.on_init(join_cfg()));

    mob.on_timer(kMobileDiscoverTimerId);
    home.on_recv(hm.tx_frames.back().data(), hm.tx_frames.back().size(), meta);
    hh.tx_frames.clear();
    fire_mobile_offer_timer(home, hh);
    auto off = first_j(hh.tx_frames, j_opcode::offer);
    CHECK(off.has_value());                                          // PREMISE: a real OFFER
    const uint8_t offered = off ? off->proposed_mobile_id : uint8_t(0);
    mob.on_recv(hh.tx_frames.back().data(), hh.tx_frames.back().size(), meta);
    hm.tx_frames.clear();
    mob.on_timer(kMobileClaimGuardTimerId);                          // CLAIM #1 — DROPPED (never handed to `home`)
    CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == 1);        // PREMISE: CLAIM #1 really existed
    CHECK(home.mobile_reg_count() == 0);                             // PREMISE: and really was lost
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);
    CHECK_FALSE(mob.mobile_attached());
    Push p{}; while (mob.next_push(p)) {}                            // drain (there is nothing to drain — asserted in §S0-4)

    // ---- ★★★★ §MH-S4b §7.1 step 3: the SOLICITATION deadline first (short, SEARCHING, spends nothing), THEN the
    //      confirmation deadline -> the SAME CLAIM again, and THIS one is delivered.
    hm._now += protocol::presence_claim_solicit_ms;
    mob.on_timer(kPresenceProbeTimerId);
    { auto pr = last_p_probe(hm.tx_frames);
      CHECK(pr.has_value());
      if (pr) CHECK(pr->searching());                                // ★★ the ask a claim-less home will actually ANSWER
      CHECK(mob.mobile_claim_solicited());
      CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == 1); }    // ★ still just CLAIM #1 — the ask is not the retry
    hm._now += protocol::presence_claim_confirm_ms;
    mob.on_timer(kPresenceProbeTimerId);
    auto rc = last_j(hm.tx_frames, j_opcode::claim);
    CHECK(rc.has_value());
    CHECK(count_j_opcode(hm.tx_frames, j_opcode::claim) == 2);
    if (rc) { CHECK(rc->proposed_node_id == offered); CHECK(rc->claim_epoch == 1); }   // ★ same id, same epoch
    home.on_recv(hm.tx_frames.back().data(), hm.tx_frames.back().size(), meta);        // ★ the re-CLAIM LANDS
    CHECK(home.mobile_reg_count() == 1);                             // ★★ the home now holds the row

    // ---- the home rosters (the CLAIM scheduled it) and the mobile confirms.
    hh.tx_frames.clear();
    home.on_timer(kPresenceRosterTimerId);
    auto e = roster_entry_for(hh.tx_frames, kMob);
    CHECK(e.has_value());                                            // PREMISE: the row is on the wire
    if (e) { CHECK(e->local_id == offered); CHECK(e->reg_epoch == 1); }
    hm._now += 500;
    mob.on_recv(hh.tx_frames.back().data(), hh.tx_frames.back().size(), meta);

    // ★★★★ THE HEALING, ON EVERY SURFACE AT ONCE.
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::attached);
    CHECK(mob.mobile_attached());
    CHECK(mob.mobile_local_id() == offered);
    CHECK(mob.mobile_last_result() == Node::MobileAttemptResult::confirmed);
    CHECK(mob.mobile_claim_retries() == 0);                          // budget released on confirmation
    CHECK_FALSE(mob.mobile_claim_solicited());                       // ★ §MH-S4b: the ask was ANSWERED — not still outstanding
    CHECK(hm.count("mobile_attach_confirmed") == 1);                 // PREMISE: the promotion branch, exactly once
    // ★★★★ §MH-S4b — **`reclaims` REPORTS A NON-ZERO COUNT, AND THAT IS THE WHOLE ASSERTION.** §MH-S4 cleared
    // `_mobile_claim_retries` on the line ABOVE this emit, so the field was structurally always 0: an instrument
    // that cannot fail, on the one event whose job is to say "this attachment was HEALED rather than clean". This
    // case is the healed one — exactly ONE re-CLAIM landed it — so the honest reading is 1.
    // ⛔ NOT "the field is present": that is what let the zero survive. The VALUE is asserted.
    { const Ev* c = hm.find("mobile_attach_confirmed");
      CHECK(c != nullptr);
      if (c) { CHECK(c->reclaims == 1);                              // ★★★ was structurally 0
               CHECK(c->local_id == offered); } }
    {   // ★★ GATE 8's positive half: the app is told NOW — and only now — exactly once.
        int reg_true = 0; Push q{};
        while (mob.next_push(q)) if (q.kind == PushKind::mobile_reg && q.relayed) {
            ++reg_true; CHECK(q.origin == kHomeId); CHECK(q.dst == offered); CHECK(q.ctr == 1);
        }
        CHECK(reg_true == 1);
    }
    // ★★ GATE 21 — A MATCHING ROSTER CONFIRMS **BOTH** PLANES, with the confirmation age stamped.
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed);
    CHECK(mob.mobile_home_confirmed_ever());
    CHECK(mob.mobile_home_confirm_age_ms() == 0);                    // stamped at `now` — age zero AT the confirmation
    hm._now += 420000;                                               // ...and it AGES (7 minutes)
    CHECK(mob.mobile_home_confirm_age_ms() == 420000);               // ★★ "Home confirmed 7 min ago", measured

    // ★★★ ONCE-ONLY: a healthy home rosters every T. A second matching roster must NOT re-push.
    hh.tx_frames.clear();
    home.on_timer(kPresenceRosterTimerId);
    if (!hh.tx_frames.empty()) mob.on_recv(hh.tx_frames.back().data(), hh.tx_frames.back().size(), meta);
    { int reg_true = 0; Push q{}; while (mob.next_push(q)) if (q.kind == PushKind::mobile_reg && q.relayed) ++reg_true;
      CHECK(reg_true == 0); }                                        // ★★ no duplicate registration event
    CHECK(hm.count("mobile_attach_confirmed") == 1);                 // ★ still exactly one promotion
    CHECK(mob.mobile_home_confirm_age_ms() == 0);                    // ★ but the AGE was refreshed by the new evidence
}

// ---------------------------------------------------------------------------
// ★★★ IDENTITY IS THE **TRIPLE** `(mobile_hash, local_id, reg_epoch)` (§4.1/§7.1) — this arc's recurring
// category error, one layer down (hash alone -> [[B147]]; `seq` without `InboxKind` -> [[B133]]; `LbtKind`
// alone -> [[B142]]). All THREE two-of-three combinations are driven, and each must FAIL to confirm.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 §7.1 — only the FULL (hash, local_id, epoch) triple confirms: every two-of-three match is refused") {
    constexpr uint32_t kMob = 0x0000AB01u, kHomeHash = 0x00005050u;
    constexpr uint8_t  kHomeId = 50, kLocal = 231;
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // (a) hash ✓ + epoch ✓ + local_id ✗  -> REFUSED (the arm this slice added)
    {
        TestHal hal; hal._now = 100000;
        Node mob(hal, /*node_id=*/0, kMob);
        CHECK(mob.on_init(s0_mobile_cfg()));
        mob.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
        mob.on_recv(off.data(), on, meta);
        mob.on_timer(kMobileClaimGuardTimerId);
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);   // PREMISE
        hal.events.clear();
        confirm_mobile_via_roster(mob, hal, kHomeId, mob.config().layers[0].layer_id,
                                  kMob, /*local=*/static_cast<uint8_t>(kLocal - 1), /*epoch=*/1, meta);   // ★ WRONG id
        CHECK(hal.count("presence_local_id_mismatch") == 1);         // PREMISE: the new arm, not some other branch
        CHECK_FALSE(mob.mobile_attached());                          // ★★ DID NOT CONFIRM
        CHECK(mob.mobile_home_link() != Node::MobileHomeLink::confirmed);
        CHECK_FALSE(mob.mobile_home_confirmed_ever());               // ★ and stamped no age
        CHECK(hal.count("mobile_attach_confirmed") == 0);
        int reg_true = 0; Push p{};
        while (mob.next_push(p)) if (p.kind == PushKind::mobile_reg && p.relayed) ++reg_true;
        CHECK(reg_true == 0);                                        // ★★ the app was NOT told
    }
    // (b) hash ✓ + local_id ✓ + epoch ✗  -> REFUSED (the pre-existing epoch arm, re-asserted so the triple is complete)
    {
        TestHal hal; hal._now = 100000;
        Node mob(hal, /*node_id=*/0, kMob);
        CHECK(mob.on_init(s0_mobile_cfg()));
        mob.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
        mob.on_recv(off.data(), on, meta);
        mob.on_timer(kMobileClaimGuardTimerId);
        hal.events.clear();
        confirm_mobile_via_roster(mob, hal, kHomeId, mob.config().layers[0].layer_id,
                                  kMob, kLocal, /*epoch=*/9, meta);  // ★ WRONG epoch
        CHECK(hal.count("presence_epoch_mismatch") == 1);            // PREMISE
        CHECK_FALSE(mob.mobile_attached());                          // ★★ DID NOT CONFIRM
        CHECK_FALSE(mob.mobile_home_confirmed_ever());
        CHECK(hal.count("mobile_attach_confirmed") == 0);
    }
    // (c) local_id ✓ + epoch ✓ + hash ✗  -> REFUSED, and it takes the ROSTER-ABSENT path (our hash is simply
    //     not in the roster), which for an UNCONFIRMED mobile means a bounded re-CLAIM — asserted on the wire.
    {
        TestHal hal; hal._now = 100000;
        Node mob(hal, /*node_id=*/0, kMob);
        CHECK(mob.on_init(s0_mobile_cfg()));
        mob.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> off{};
        const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
        mob.on_recv(off.data(), on, meta);
        mob.on_timer(kMobileClaimGuardTimerId);
        const int claims = count_j_opcode(hal.tx_frames, j_opcode::claim);
        hal.events.clear();
        confirm_mobile_via_roster(mob, hal, kHomeId, mob.config().layers[0].layer_id,
                                  /*hash=*/0x0000BAD0u, kLocal, /*epoch=*/1, meta);   // ★ SOMEBODY ELSE's hash
        CHECK(hal.count("presence_roster_absent") == 1);             // PREMISE
        CHECK_FALSE(mob.mobile_attached());                          // ★★ DID NOT CONFIRM
        CHECK_FALSE(mob.mobile_home_confirmed_ever());
        CHECK(hal.count("mobile_attach_confirmed") == 0);
        // ★★ §7.1 step 5: never-confirmed + absent => THE SAME CLAIM again (not a re-DISCOVER)
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == claims + 1);
        auto rc = last_j(hal.tx_frames, j_opcode::claim);
        CHECK(rc.has_value());
        if (rc) { CHECK(rc->proposed_node_id == kLocal); CHECK(rc->claim_epoch == 1); }
        CHECK(mob.mobile_claim_retries() == 1);
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);
    }
    // (d) POSITIVE CONTROL — all three match => confirms. Without this, (a)-(c) could be passing because the
    //     harness cannot confirm at all.
    {
        TestHal hal; hal._now = 100000;
        Node mob(hal, /*node_id=*/0, kMob);
        CHECK(mob.on_init(s0_mobile_cfg()));
        const uint8_t id = attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta);
        CHECK(id == kLocal);
        CHECK(mob.mobile_attached());                                // ★★ the harness CAN confirm
        CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed);
    }
}

// ---------------------------------------------------------------------------
// ★★★ §7.1 step 5 — THE ROSTER-ABSENT FORK, AND THE FLAG THAT SEPARATES ITS TWO ARMS. Same wire evidence,
// two different correct actions; `_presence_reg_confirmed` (node.h read #2) is what decides. The
// never-confirmed arm is covered above (c); THIS case is the ALREADY-CONFIRMED arm, which must NOT re-CLAIM.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 §7.1 — a roster that omits an ALREADY-CONFIRMED mobile re-DISCOVERs (never a re-CLAIM)") {
    constexpr uint32_t kMob = 0x0000AC02u, kHomeHash = 0x00005151u;
    constexpr uint8_t  kHomeId = 51, kLocal = 230;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
    CHECK(mob.mobile_attached());                                    // PREMISE: confirmed first
    Push p{}; while (mob.next_push(p)) {}
    const int claims = count_j_opcode(hal.tx_frames, j_opcode::claim);
    hal.events.clear(); hal.armed.clear();

    // the home reboots/evicts: it rosters SOMEBODY ELSE and our hash is gone
    confirm_mobile_via_roster(mob, hal, kHomeId, mob.config().layers[0].layer_id,
                              /*hash=*/0x0000BAD1u, /*local=*/229, /*epoch=*/1, meta);
    CHECK(hal.count("presence_roster_absent") == 1);                 // PREMISE
    CHECK(hal.count("mobile_reset") == 1);                           // PREMISE: the RESET arm, not the re-CLAIM arm
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == claims); // ★★ NOT ONE re-CLAIM — the id/epoch are dead
    CHECK_FALSE(mob.mobile_registered());
    CHECK_FALSE(mob.mobile_attached());
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::recovering);   // ★★ `recovering`, not `seeking`: there WAS an attachment
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == 1);             // ★ a staggered re-DISCOVER
    { int reg_false = 0; Push q{};
      while (mob.next_push(q)) if (q.kind == PushKind::mobile_reg && !q.relayed) ++reg_false;
      CHECK(reg_false == 1); }                                       // ★ the app IS told, because it was told `true` earlier
    // ★ THE AGE SURVIVES THE LOSS AND IS STILL RENDERABLE — §4.1 asks for the age of the LATEST confirmation,
    //   which is exactly what a "Home confirmed 7 min ago" panel must show while recovering.
    CHECK(mob.mobile_home_confirmed_ever());
}

// ---------------------------------------------------------------------------
// ★★★★ GATE 20 + [[B139]] — A LOCAL TX-ADMISSION FAILURE MUST NOT MOVE THE HOME-LINK PLANE.
// THE DEFECT (registered as B139, MEASURED by inspection during §MH-S1): `presence_probe_fire` did
// `++_presence_miss` UNCONDITIONALLY, discarding `tx_initiating`'s result — so `presence_probe_k_miss + 1`
// probes OUR OWN RADIO REFUSED reached `presence_home_lost` -> `mobile_reset_registration` -> a full
// re-DISCOVER. A busy channel could deregister a mobile from a home that was working perfectly.
// ★ THE ONE PARAMETER THAT MAKES IT TESTABLE is `TestHal::tx_answer` — the lesson this arc keeps re-learning.
// ---------------------------------------------------------------------------
TEST_CASE("★★★★ §MH-S4 gate 20 / [[B139]] — probes OUR OWN transmitter refuses NEVER walk the home link to checking or lost") {
    constexpr uint32_t kMob = 0x0000B139u, kHomeHash = 0x00005252u;
    constexpr uint8_t  kHomeId = 52, kLocal = 228;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
    CHECK(mob.mobile_attached());                                    // PREMISE: a healthy CONFIRMED attachment
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed);
    Push p{}; while (mob.next_push(p)) {}

    // ⛔ OUR RADIO NOW REFUSES EVERYTHING. Nothing about the home has changed.
    hal.tx_answer = TxResult::busy;
    const int tx_before = static_cast<int>(hal.tx_frames.size());
    // Drive TWO MORE than the k_miss ladder needs: pre-fix, k_miss+1 refusals were enough to reach home-lost.
    for (int i = 0; i < static_cast<int>(protocol::presence_probe_k_miss) + 3; ++i) {
        hal._now += protocol::presence_probe_retry_ms;
        mob.on_timer(kPresenceProbeTimerId);
    }
    CHECK(static_cast<int>(hal.tx_frames.size()) == tx_before);      // PREMISE: not one frame left the radio
    CHECK(hal.count("presence_probe_refused") == static_cast<int>(protocol::presence_probe_k_miss) + 3);  // PREMISE: every refusal was seen

    // ★★★★ THE FIX, ON ALL THREE PLANES.
    CHECK(hal.count("presence_home_lost") == 0);                     // ★★★ was k_miss+1 refusals -> HOME LOST
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed); // ★★★ NOT `lost` AND NOT `checking` (gate 20 asserts both)
    CHECK(mob.mobile_attached());                                    // ★★ the attachment survived intact
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::attached);
    CHECK(mob.mobile_home_id() == kHomeId);
    CHECK(mob.mobile_local_id() == kLocal);
    // ★ AND IT IS REPORTED — in `last result`, which is where §6.4/§10 say a local transmitter failure belongs.
    CHECK(mob.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);
    { int any = 0; Push q{}; while (mob.next_push(q)) if (q.kind == PushKind::mobile_reg) ++any;
      CHECK(any == 0); }                                             // ★★ the app was NOT told the registration changed
    CHECK(hal.count_armed(kPresenceProbeTimerId) >= 1);              // ★ and the check keeps being re-armed (self-healing)

    // ★★★★ THE NEGATIVE CONTROL, AND IT IS THE ONE THAT MAKES THE WHOLE CASE NON-VACUOUS: with the radio
    // WORKING, the same number of unanswered probes DOES reach `lost`. Without this, "the link stayed
    // confirmed" could equally mean the ladder is broken outright.
    hal.tx_answer = TxResult::ok;
    hal.events.clear();
    bool saw_checking = false;
    for (int i = 0; i < static_cast<int>(protocol::presence_probe_k_miss) + 2; ++i) {
        hal._now += protocol::presence_probe_retry_ms;
        mob.on_timer(kPresenceProbeTimerId);
        if (mob.mobile_home_link() == Node::MobileHomeLink::checking) saw_checking = true;
    }
    CHECK(saw_checking);                                             // ★★ an ADMITTED unanswered probe DOES say `checking`
    CHECK(hal.count("presence_home_lost") == 1);                     // ★★★ ...and the ladder DOES reach HOME LOST
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::lost);
    CHECK_FALSE(mob.mobile_attached());
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::recovering);
}

// ---------------------------------------------------------------------------
// GATE 22 — "a home beacon alone cannot confirm the link": receiving the home's beacons while probes go
// unanswered still walks the link to `lost`. §7.2: inbound beacons are ONE-WAY hints — hearing the home
// proves only that we hear the home. ⚠ This is the case that would have caught the tempting zero-byte reuse
// of `_my_mobile_reg.last_heard_home_ms` as the confirmation stamp (see node.h).
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 gate 22 (§7.2) — the home's BEACONS cannot confirm the link; unanswered probes still reach `lost`") {
    constexpr uint32_t kMob = 0x0000BC22u, kHomeHash = 0x00005353u;
    constexpr uint8_t  kHomeId = 53, kLocal = 227;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed); // PREMISE
    const uint64_t confirmed_at = hal._now;

    // the home keeps beaconing loudly, but never answers a probe
    for (int i = 0; i < static_cast<int>(protocol::presence_probe_k_miss) + 2; ++i) {
        std::array<uint8_t, 64> bcn{};
        const size_t bn = make_beacon(/*src=*/kHomeId, /*key=*/kHomeHash, bcn);
        hal._now += protocol::presence_probe_retry_ms;
        mob.on_recv(bcn.data(), bn, meta);                           // ★ a HEARD home — a one-way hint
        mob.on_timer(kPresenceProbeTimerId);
    }
    CHECK(hal.count("presence_probe_tx") >= static_cast<int>(protocol::presence_probe_k_miss) + 2);  // PREMISE: probes really went out
    CHECK(hal.count("presence_home_lost") == 1);                     // ★★★ the beacons did NOT rescue the link
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::lost);
    // ★★ AND THE CONFIRMATION AGE IS STILL THE OLD ONE — the beacons did not refresh it. This is the
    //    display-shaped-field assertion: a heard beacon must never make the panel say "confirmed just now".
    CHECK(mob.mobile_home_confirmed_ever());
    CHECK(mob.mobile_home_confirm_age_ms() == hal._now - confirmed_at);
    CHECK(mob.mobile_home_confirm_age_ms() > 0);
}

// ---------------------------------------------------------------------------
// GATE 23 — "a mesh no-route result does not mean the home link is lost." §4.1's third plane (MESH SERVICE)
// is answered by the result of that specific send and by nothing else; an unroutable destination leaves the
// home link `confirmed` while its own evidence is fresh.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 gate 23 (§4.1) — an unroutable DESTINATION leaves the HOME LINK confirmed") {
    constexpr uint32_t kMob = 0x0000BD23u, kHomeHash = 0x00005454u;
    constexpr uint8_t  kHomeId = 54, kLocal = 226;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed); // PREMISE
    Push p{}; while (mob.next_push(p)) {}
    hal.events.clear();

    // ★ a send to a hash NOBODY can resolve, through the PUBLIC app seam (`on_command`), so this is the same
    //   path a companion takes. The mesh plane's own outcome is §8/S5's business; what matters here is leakage.
    const uint8_t body[] = { 0x01, 0x02, 0x03 };
    Command c{}; c.kind = CmdKind::send; c.body = body; c.body_len = sizeof body;
    c.u.send.dst_hash = 0x0000FEEDu;
    const CmdResult r = mob.on_command(c);
    CHECK(r.code != CmdCode::err_unprovisioned);                     // PREMISE: the send really was accepted for routing
    int pushes = 0; Push q{};
    while (mob.next_push(q)) ++pushes;

    // ★★★ WHATEVER the mesh plane reported, the HOME-LINK plane is untouched — that is the entire gate.
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed);
    CHECK(mob.mobile_attached());
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::attached);
    CHECK(hal.count("presence_home_lost") == 0);
    CHECK(mob.mobile_home_confirm_age_ms() == 0);                    // ★ and the age was not disturbed either
    (void)pushes;   // the mesh-plane outcome is S5/§8's business; this case pins only that it did not LEAK into the home-link plane
}

// ---------------------------------------------------------------------------
// GATES 9 + 10 — "auto OFF emits nothing at boot; manual register keeps retrying and then confirms when a
// home appears" and "presence monitoring and weak-home candidate canvass run after a manual attach with auto
// OFF". §4.2's whole point: manual and automatic starts share ONE FSM.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 gates 9+10 (§4.2) — auto OFF: silent at boot, then a MANUAL session retries, confirms, and gets a full presence plane") {
    constexpr uint32_t kMob = 0x0000B910u, kHomeHash = 0x00005555u;
    constexpr uint8_t  kHomeId = 55, kLocal = 225;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    NodeConfig mc = s0_mobile_cfg(); mc.mobile_autoregister = false;
    CHECK(mob.on_init(mc));

    // ★ GATE 9a — NOTHING at boot.
    CHECK(hal.tx_frames.empty());
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == 0);             // not even a kick was armed
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::dormant);
    mob.on_timer(kMobileDiscoverTimerId);                            // even if something fires it, the gate holds
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 0);

    // ★ GATE 9b — a MANUAL session keeps retrying while no home answers.
    mob.mobile_register_current();
    mob.on_timer(kMobileDiscoverTimerId);
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 1);
    mob.on_timer(kMobileClaimGuardTimerId);                          // no OFFER -> no host
    CHECK(hal.count("mobile_no_host") == 1);                         // PREMISE
    CHECK(hal.count_armed(kMobileDiscoverTimerId) >= 1);             // ★ a retry was armed even with auto OFF
    mob.on_timer(kMobileDiscoverTimerId);
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == 2);   // ★★ it really retried

    // ★ GATE 9c — ...and CONFIRMS when a home finally appears.
    std::array<uint8_t, 16> off{};
    const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
    mob.on_recv(off.data(), on, meta);
    mob.on_timer(kMobileClaimGuardTimerId);
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);
    CHECK_FALSE(mob.mobile_attached());                              // ★ still not a registration
    confirm_mobile_via_roster(mob, hal, kHomeId, mob.config().layers[0].layer_id, kMob, kLocal, /*epoch=*/1, meta);
    CHECK(mob.mobile_attached());                                    // ★★★ confirmed under auto OFF
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed);
    CHECK(mob.config().mobile_autoregister == false);                // ★ the flag was never touched (§4.2 compatibility)

    // ★★★ GATE 10 — THE PRESENCE PLANE REALLY RUNS. Pre-S4 `presence_arm_check` returned immediately unless
    // `_cfg.mobile_autoregister`, so a manually-attached mobile had NO confirmation deadline, NO home-loss
    // detection and NO candidate canvass at all. Asserted as an ARMED TIMER and then as a real probe on the wire.
    CHECK(hal.count_armed(kPresenceProbeTimerId) >= 1);              // ★★ the check timer EXISTS (was 0)
    hal.events.clear();
    hal._now += protocol::presence_check_base_ms;
    mob.on_timer(kPresenceProbeTimerId);
    CHECK(hal.count("presence_probe_tx") == 1);                      // ★★ a real probe, from a manually-attached mobile
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::checking);  // ★ and the plane moves as it should
    // ...and home-loss detection works too (the ladder is live, not merely armed once)
    for (int i = 0; i < static_cast<int>(protocol::presence_probe_k_miss) + 2; ++i) {
        hal._now += protocol::presence_probe_retry_ms;
        mob.on_timer(kPresenceProbeTimerId);
    }
    CHECK(hal.count("presence_home_lost") == 1);                     // ★★ full recovery machinery under auto OFF
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::recovering);
    CHECK(mob.mobile_home_desired());                                // ★★ and the session is STILL desired -> it will re-attach
}

// ---------------------------------------------------------------------------
// ★★★ §7.1's BOUNDED BUDGET CANNOT BE RESET BY ITS OWN RETRY. The re-CLAIM deliberately does NOT go through
// `mobile_claim_adopt` (which calls `presence_on_adopt`, which zeroes `_mobile_claim_retries`) — if it did,
// the bounded retry of §7.1 would become an UNBOUNDED loop and `presence_claim_max_retries` would be
// decorative again. node.h documents the reasoning; this case measures it.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 §7.1 — a re-CLAIM does NOT re-adopt, so the bounded retry budget cannot reset itself") {
    constexpr uint32_t kMob = 0x0000BE11u, kHomeHash = 0x00005656u;
    constexpr uint8_t  kHomeId = 56, kLocal = 224;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    mob.on_timer(kMobileDiscoverTimerId);
    std::array<uint8_t, 16> off{};
    const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
    mob.on_recv(off.data(), on, meta);
    mob.on_timer(kMobileClaimGuardTimerId);
    CHECK(hal.count("mobile_adopted") == 1);                         // PREMISE: exactly one adopt so far

    // ⓘ §MH-S4b: each round is TWO deadline fires — the short SEARCHING solicitation (§7.1 step 3), then the
    //   confirmation deadline that actually spends the retry. The re-adopt question is unchanged; only the number
    //   of fires is, so the loop is updated in place rather than the assertion.
    for (int i = 0; i < static_cast<int>(protocol::presence_claim_max_retries); ++i) {
        hal._now += protocol::presence_claim_solicit_ms;
        mob.on_timer(kPresenceProbeTimerId);                         // ask
        CHECK(mob.mobile_claim_retries() == i);                      // ★ the ask spends nothing
        hal._now += protocol::presence_claim_confirm_ms;
        mob.on_timer(kPresenceProbeTimerId);                         // silence -> one re-CLAIM
        CHECK(mob.mobile_claim_retries() == i + 1);                  // ★★ MONOTONIC — never reset by the retry itself
        CHECK(hal.count("mobile_adopted") == 1);                     // ★★★ and STILL exactly one adopt: no re-adopt
    }
    // ★ THE BUDGET IS THEREFORE REACHABLE, which is the whole point: a self-resetting counter would loop forever.
    hal._now += protocol::presence_claim_solicit_ms;
    mob.on_timer(kPresenceProbeTimerId);
    hal._now += protocol::presence_claim_confirm_ms;
    mob.on_timer(kPresenceProbeTimerId);
    CHECK(hal.count("mobile_claim_exhausted") == 1);
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::seeking);
    CHECK(hal.count("mobile_adopted") == 1);                         // ★ never re-adopted, all the way to exhaustion
}

// ---------------------------------------------------------------------------
// ★★★★ §MH-S4b — **THE RE-CLAIM BUDGET IS SPENT BY THE TRANSMISSION, NOT BY THE DECISION TO TRANSMIT.**
// §MH-S4 incremented `_mobile_claim_retries` on the line BEFORE `mobile_reclaim_send()`, so a mobile whose own
// radio refused all three retries reported `claim_unconfirmed` and re-DISCOVERed with **NOT ONE re-CLAIM ever on
// the air** — the failure the budget exists to bound, reached without using the resource it bounds. Fifth
// appearance of the rule in this arc: [[B84]]'s `_tries`, [[B145]]/[[B146]]'s OFFER counters, [[B139]]'s
// `_presence_miss`, this.
// ★ THE TRIGGER IS §7.1 STEP 5, DRIVEN OFF THE WIRE (a chosen-home roster that rosters SOMEBODY ELSE), not the
//   silence timer — deliberately, because with the HAL refusing everything the solicitation PROBE is refused too
//   and the silence deadline is never reached. Using the RX-side trigger isolates the CLAIM's admission.
// ---------------------------------------------------------------------------
TEST_CASE("★★★★ §MH-S4b — a re-CLAIM OUR OWN TRANSMITTER REFUSES spends NO budget: the mobile stays `claiming`, never `claim_unconfirmed` with nothing sent") {
    constexpr uint32_t kMob = 0x0000B201u, kHomeHash = 0x00005858u, kOther = 0x0000A1A2u;
    constexpr uint8_t  kHomeId = 58, kLocal = 219;
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // ---- the SUBJECT: the transmitter refuses every re-CLAIM.
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    mob.on_timer(kMobileDiscoverTimerId);
    std::array<uint8_t, 16> off{};
    const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
    mob.on_recv(off.data(), on, meta);
    mob.on_timer(kMobileClaimGuardTimerId);                          // CLAIM #1 + provisional adopt
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);   // PREMISE
    const int claims_at_adopt = count_j_opcode(hal.tx_frames, j_opcode::claim);
    CHECK(claims_at_adopt == 1);                                     // PREMISE: CLAIM #1 really aired
    const uint8_t layer = mob.config().layers[0].layer_id;

    hal.tx_answer = TxResult::busy;                                  // ⛔ OUR OWN radio now refuses everything
    // MORE ROUNDS THAN THE BUDGET: if the request spent the budget, round 4 would exhaust it.
    for (int i = 0; i < static_cast<int>(protocol::presence_claim_max_retries) + 2; ++i) {
        hal.events.clear();
        std::array<uint8_t, 64> rb{};
        const size_t rn = make_p_roster_one(kHomeId, layer, /*mobile_hash=*/kOther, /*local=*/200, /*epoch=*/1, rb);
        mob.on_recv(rb.data(), rn, meta);
        CHECK(hal.count("presence_roster_absent") == 1);             // PREMISE: §7.1 step 5's branch really ran
        CHECK(hal.count("mobile_tx_rejected") == 1);                 // PREMISE: …and the transmitter really refused
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == claims_at_adopt);   // ★★★ NOTHING reached the air
        CHECK(mob.mobile_claim_retries() == 0);                      // ★★★★ …so NOTHING was spent
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);      // ★★ still claiming, round after round
        CHECK(hal.count("mobile_claim_exhausted") == 0);             // ★★★ never "unconfirmed" on refusals alone
        CHECK(mob.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);  // §6.4/§10: OUR transmitter…
        CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);             // ⛔ …and the home is not blamed
    }
    // ---- THE RADIO RECOVERS: the SAME trigger now airs a frame and spends EXACTLY one retry.
    hal.tx_answer = TxResult::ok;
    hal.events.clear();
    { std::array<uint8_t, 64> rb{};
      const size_t rn = make_p_roster_one(kHomeId, layer, kOther, 200, 1, rb);
      mob.on_recv(rb.data(), rn, meta); }
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == claims_at_adopt + 1);   // ★★ NOW it is on the air…
    CHECK(mob.mobile_claim_retries() == 1);                                        // ★★ …and NOW it costs one
    { auto rc = last_j(hal.tx_frames, j_opcode::claim);
      CHECK(rc.has_value());
      if (rc) { CHECK(rc->proposed_node_id == kLocal); CHECK(rc->claim_epoch == 1); } }   // ★ still THE SAME CLAIM

    // ---- ★★★ NEGATIVE CONTROL — the roster-absent trigger really does reach the budget and really does exhaust
    //      it, so the subject above cannot be passing because the trigger is broken.
    {
        TestHal hc; hc._now = 100000;
        Node mc(hc, /*node_id=*/0, kMob);
        CHECK(mc.on_init(s0_mobile_cfg()));
        mc.on_timer(kMobileDiscoverTimerId);
        std::array<uint8_t, 16> o2{};
        const size_t n2 = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, o2);
        mc.on_recv(o2.data(), n2, meta);
        mc.on_timer(kMobileClaimGuardTimerId);
        const uint8_t l2 = mc.config().layers[0].layer_id;
        for (int i = 0; i < static_cast<int>(protocol::presence_claim_max_retries); ++i) {
            std::array<uint8_t, 64> rb{};
            const size_t rn = make_p_roster_one(kHomeId, l2, kOther, 200, 1, rb);
            mc.on_recv(rb.data(), rn, meta);
            CHECK(mc.mobile_claim_retries() == i + 1);               // ★ a WORKING transmitter DOES spend it
        }
        hc.events.clear();
        std::array<uint8_t, 64> rb{};
        const size_t rn = make_p_roster_one(kHomeId, l2, kOther, 200, 1, rb);
        mc.on_recv(rb.data(), rn, meta);
        CHECK(hc.count("mobile_claim_exhausted") == 1);              // ★★ and DOES exhaust
        CHECK(mc.mobile_attach_state() == Node::MobileAttachState::seeking);
        CHECK(mc.mobile_last_result() == Node::MobileAttemptResult::claim_unconfirmed);
    }
}

// ---------------------------------------------------------------------------
// ★★★ §MH-S4b §4.1/§10 — THE CONFIRMATION AGE IS 64-BIT **AT THE SOURCE TOO**, not only in the serializer.
// `src/firmware_config.cpp` cast this accessor to `uint32_t` to fit a u32 status field, so the rendered age wrapped
// at ~49.7 days and a months-stale confirmation displayed as a fresh one. The serializer half is pinned in
// `test_console_json.cpp`; THIS is the core half — the accessor itself must carry an age past UINT32_MAX.
// ⓘ Why it matters at all: a `uint32_t` version of the backing stamp was MEASURED at +0 bytes and DECLINED under
//   M3 precisely to avoid this arithmetic. The cast reintroduced it downstream for free.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4b §4.1 — the home-confirmation AGE survives past UINT32_MAX (a u32 would wrap at ~49.7 days)") {
    constexpr uint32_t kMob = 0x0000B204u, kHomeHash = 0x00005A5Au;
    constexpr uint8_t  kHomeId = 90, kLocal = 216;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(s0_mobile_cfg()));
    CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
    CHECK(mob.mobile_attached());                                    // PREMISE: a real confirmation exists
    CHECK(mob.mobile_home_confirmed_ever());
    CHECK(mob.mobile_home_confirm_age_ms() == 0);                    // PREMISE: stamped at `now`

    // ---- 57.9 days later. A u32 age would read 705 032 704 ms (≈ 8.2 days) — "confirmed last week".
    hal._now += 5000000000ull;
    const uint64_t age = mob.mobile_home_confirm_age_ms();
    CHECK(age == 5000000000ull);                                     // ★★★ the full 64-bit age
    CHECK(age > static_cast<uint64_t>(UINT32_MAX));                  // ★★ …explicitly past the wrap point
    CHECK(static_cast<uint32_t>(age) == 705032704u);                  // ★ and the truncation this names is REAL: it is
                                                                     //   what the cast used to render
    CHECK(mob.mobile_home_confirmed_ever());                         // ★ still "ever confirmed" — the age is stale, not absent
    // ⓘ Nothing about the home was re-measured, so the plane is unchanged: the AGE is the honest rendering of a
    //   point-in-time confirmation, which is §4.1's whole rule.
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::confirmed);
}

// ---------------------------------------------------------------------------
// ★★★★ §MH-S4b — **THE DEFERRED HALF, AND THE BOUNDARY IS THE ONE [[B139]] HAD TO GET RIGHT TOO.**
// `tx_initiating` answers TRUE for a frame merely ACCEPTED INTO the LBT defer ring, so a DEFERRED re-CLAIM
// legitimately IS in flight and MUST be counted — only a DEFINITIVE refusal may be free. The remaining case is a
// frame that deferred (counted) and was then refused by the HAL when the defer fired: it never reached the air, so
// the retry is REFUNDED.
// ⛔ AND IT MUST NOT REACH `mobile_admission_rejected`: that is the PRE-attachment FSM's backoff, which arms
//    `kMobileDiscoverTimerId` and would throw a live provisional attachment away for a local radio hiccup. The
//    discriminator is `_mobile_claim_pending` (a FIRST CLAIM still holds it; a re-CLAIM never sets it) — the
//    [[B147]] rule of identifying the transaction BEFORE acting on it.
// ---------------------------------------------------------------------------
TEST_CASE("★★★★ §MH-S4b — a DEFERRED re-CLAIM the HAL later refuses REFUNDS its retry and does NOT enter the pre-attachment backoff") {
    constexpr uint32_t kMob = 0x0000B202u, kHomeHash = 0x00005959u, kOther = 0x0000A1A3u;
    constexpr uint8_t  kHomeId = 59, kLocal = 218;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hal; hal._now = 100000;
    NodeConfig cfg = s0_mobile_cfg(); cfg.lbt_enabled = true;        // ★ required: only LBT can DEFER
    Node mob(hal, /*node_id=*/0, kMob);
    CHECK(mob.on_init(cfg));

    hal._busy_until = 0;                                             // clear channel: CLAIM #1 goes out immediately
    mob.on_timer(kMobileDiscoverTimerId);
    std::array<uint8_t, 16> off{};
    const size_t on = make_j_offer_mobile(kHomeId, kHomeHash, kLocal, kMob, off);
    mob.on_recv(off.data(), on, meta);
    mob.on_timer(kMobileClaimGuardTimerId);
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);   // PREMISE
    const int claims_at_adopt = count_j_opcode(hal.tx_frames, j_opcode::claim);
    CHECK(claims_at_adopt == 1);
    const uint8_t layer = mob.config().layers[0].layer_id;

    // ---- the channel goes busy, so the re-CLAIM DEFERS. It is in flight ⇒ it COUNTS.
    hal._busy_until = hal._now + 5000;
    hal.armed.clear(); hal.events.clear();
    const int discover_arms_before = hal.count_armed(kMobileDiscoverTimerId);
    { std::array<uint8_t, 64> rb{};
      const size_t rn = make_p_roster_one(kHomeId, layer, kOther, 200, 1, rb);
      mob.on_recv(rb.data(), rn, meta); }
    CHECK(hal.count("presence_roster_absent") == 1);                 // PREMISE: §7.1 step 5 ran
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == claims_at_adopt);   // PREMISE: nothing aired — it DEFERRED
    CHECK(hal.count("mobile_tx_rejected") == 0);                     // PREMISE: and it was NOT a refusal
    CHECK(mob.mobile_claim_retries() == 1);                          // ★★★ a DEFERRED re-CLAIM COUNTS — it is in flight

    // ---- ⛔ THE DEFER NOW DIES AT THE RADIO QUEUE.
    hal._now += 6000;
    hal.tx_answer = TxResult::busy;
    hal.armed.clear(); hal.events.clear();
    mob.test_fire_lbt_defer(/*slot=*/0);
    CHECK(hal.count("tx_deferred_lost") == 1);                       // PREMISE: the late-refusal arm really ran
    CHECK(hal.count("mobile_reclaim_refunded") == 1);                // PREMISE: …and it was recognised as a RE-CLAIM
    CHECK(mob.mobile_claim_retries() == 0);                          // ★★★★ REFUNDED — the frame never aired
    CHECK(count_j_opcode(hal.tx_frames, j_opcode::claim) == claims_at_adopt);   // ★ …which is the fact being refunded for
    CHECK(mob.mobile_attach_state() == Node::MobileAttachState::claiming);      // ★★ the attachment SURVIVES
    CHECK(mob.mobile_registered());                                  // ★ the provisional adoption is untouched
    CHECK(mob.mobile_home_id() == kHomeId);
    CHECK(mob.mobile_last_result() == Node::MobileAttemptResult::tx_rejected);  // §6.4/§10: OUR transmitter
    CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);             // ⛔ the home is not implicated
    // ★★★ AND THE PRE-ATTACHMENT BACKOFF DID **NOT** RUN — this is the assertion that separates the two arms:
    //     `mobile_admission_rejected` would have armed `kMobileDiscoverTimerId` (and spent a jitter draw on it).
    CHECK(hal.count_armed(kMobileDiscoverTimerId) == discover_arms_before);
    CHECK(hal.count("mobile_no_host") == 0);

    // ---- ★★★ CONTROL: a FIRST CLAIM dying on the same arm STILL takes the pre-attachment path, so the
    //      discriminator is doing work rather than disabling the branch for everyone.
    {
        TestHal hc; hc._now = 100000;
        NodeConfig c2 = s0_mobile_cfg(); c2.lbt_enabled = true;
        Node mc(hc, /*node_id=*/0, /*key_hash32=*/0x0000B203u);
        CHECK(mc.on_init(c2));
        stage_claim_deferred(hc, mc, kHomeId, kHomeHash, /*local_id=*/217, /*self=*/0x0000B203u);
        CHECK_FALSE(mc.mobile_registered());                         // PREMISE: the FIRST CLAIM has not adopted
        hc._now += 6000;
        hc.tx_answer = TxResult::busy;
        hc.armed.clear(); hc.events.clear();
        mc.test_fire_lbt_defer(/*slot=*/0);
        CHECK(hc.count("tx_deferred_lost") == 1);                    // PREMISE: same arm
        CHECK(hc.count("mobile_reclaim_refunded") == 0);             // ★★ NOT treated as a re-CLAIM…
        CHECK(hc.count("mobile_tx_rejected") == 1);                  // ★★ …it took `mobile_admission_rejected`…
        CHECK(hc.count_armed(kMobileDiscoverTimerId) == 1);          // ★★★ …and got its bounded retry-DISCOVER
    }
}

// ---------------------------------------------------------------------------
// §4.2/§4.3 — `mobile unregister`: end the session, return to `dormant`, ⛔ and put NOTHING on the air.
//
// ⛔⛔ ARM (b) IS REWRITTEN IN PLACE BY §MH-S4b (B101 — not deleted), AND BOTH ITS ASSERTION AND ITS METHOD WERE
// WRONG. §MH-S4 concluded that on an `autoregister=1` device the verb ends the session and "the autonomous FSM
// then re-enters `seeking` on its own", and it demonstrated that by calling `on_timer(kMobileDiscoverTimerId)`
// BY HAND — the very timer `mobile_unregister()` had just cancelled, and one that production schedules no
// replacement for. ⚠ **A test that injects the timer production would have to schedule is a harness modelling
// something that does not exist** — the same class as [[B145]]'s hand-pumped callbacks and [[B143]]'s
// call-history-as-state. What it actually measured was that the FSM entry gate `registration_armed()` was still
// TRUE, i.e. THE DEFECT: §4.3's post-condition ("return to `dormant`") held for one member while the machine
// stayed armed, so the device was neither dormant nor seeking.
// ⇒ §MH-S4b makes `_mobile_home_desired` the effective session state, so `dormant` is now the answer for BOTH
//   `autoregister` values, and this arm proves it **through a REAL `meshroute::TimerWheel` driven by the
//   production drain** (`pump_due_timers`) rather than by hand: after the verb, a full drain fires NOTHING.
//   Autonomy is then shown resuming from a PRODUCTION path only — an explicit `mobile register`, whose own
//   `after(0, …)` the same drain picks up.
// ⛔ Neither arm may put a frame on the air: §4.3 adds no deregistration wire message.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §MH-S4 §4.3 (REWRITTEN §MH-S4b) — `mobile unregister` ends the session silently and BOTH `autoregister` arms stay dormant until a manual re-register") {
    constexpr uint32_t kMob = 0x0000BF43u, kHomeHash = 0x00005757u;
    constexpr uint8_t  kHomeId = 57, kLocal = 223;
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // ================= (a) AUTO OFF — the verb's product case: dormant, and it STAYS dormant =================
    {
        TestHal hal; hal._now = 100000;
        Node mob(hal, /*node_id=*/0, kMob);
        NodeConfig mc = s0_mobile_cfg(); mc.mobile_autoregister = false;
        CHECK(mob.on_init(mc));
        mob.mobile_register_current();                               // the operator asks for home service
        CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
        CHECK(mob.mobile_attached());                                // PREMISE
        Push p{}; while (mob.next_push(p)) {}
        const size_t frames_before = hal.tx_frames.size();
        hal.events.clear();

        mob.mobile_unregister();

        CHECK(hal.tx_frames.size() == frames_before);                // ★★★ §4.3: NO deregistration wire message
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::dormant);
        CHECK_FALSE(mob.mobile_attached());
        CHECK_FALSE(mob.mobile_registered());
        CHECK_FALSE(mob.mobile_home_desired());
        CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);   // §4.1: dormant => unknown
        CHECK_FALSE(mob.mobile_home_confirmed_ever());               // ★ no session, no age to render
        CHECK(mob.mobile_last_result() == Node::MobileAttemptResult::none);
        { int reg_false = 0; Push q{};
          while (mob.next_push(q)) if (q.kind == PushKind::mobile_reg && !q.relayed) ++reg_false;
          CHECK(reg_false == 1); }                                   // ★ the app IS told the registration ended
        // ★★ AND IT STAYS QUIET — no autonomous re-attachment, no probe, not one frame.
        mob.on_timer(kMobileDiscoverTimerId);
        CHECK(hal.tx_frames.size() == frames_before);
        mob.on_timer(kPresenceProbeTimerId);
        CHECK(hal.tx_frames.size() == frames_before);
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::dormant);
    }
    // ====== (b) AUTO ON — ★★★★ §MH-S4b: IT STAYS DORMANT TOO, AND THE PROOF USES THE PRODUCTION PUMP ======
    {
        meshroute::TimerWheel wheel;
        TestHal hal; hal._now = 100000; hal.wheel = &wheel;          // ★ the fixture IS the device pump: `after` arms the
                                                                     //   real wheel and `cancel` really cancels
        Node mob(hal, /*node_id=*/0, kMob);
        CHECK(mob.on_init(s0_mobile_cfg()));                         // autoregister ON
        CHECK(mob.mobile_home_desired());                            // ★★ §MH-S4b: the session state is SEEDED from the flag at on_init
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::seeking);
        hal._now += protocol::mobile_boot_jitter_ms + 1;              // let the jittered boot kick come due
        CHECK(pump_due_timers(mob, hal, wheel) >= 1);                 // PREMISE: the AUTOMATIC session really is running
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) >= 1);
        CHECK(attach_mobile_confirmed(mob, hal, kHomeId, kHomeHash, kLocal, kMob, meta) == kLocal);
        CHECK(mob.mobile_attached());                                // PREMISE
        Push p{}; while (mob.next_push(p)) {}
        const size_t frames_before  = hal.tx_frames.size();
        const int    discovers_before = count_j_opcode(hal.tx_frames, j_opcode::discover);

        mob.mobile_unregister();

        CHECK(hal.tx_frames.size() == frames_before);                // ★★★ still SILENT — the verb never transmits
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::dormant);   // ★ the session really did end...
        CHECK_FALSE(mob.mobile_attached());
        CHECK_FALSE(mob.mobile_registered());
        CHECK_FALSE(mob.mobile_home_desired());                      // ★★ …and the effective session state is CLEARED
        CHECK(mob.mobile_home_link() == Node::MobileHomeLink::unknown);
        { int reg_false = 0; Push q{};
          while (mob.next_push(q)) if (q.kind == PushKind::mobile_reg && !q.relayed) ++reg_false;
          CHECK(reg_false == 1); }
        // ★★★★ AND NOW THE ASSERTION §MH-S4 GOT BACKWARDS, MEASURED THE HONEST WAY: advance the clock well past
        // every period the FSM could plausibly use and run the PRODUCTION DRAIN. It must fire NOTHING, because the
        // verb cancelled the timers and production schedules no replacement. ⛔ No `on_timer` is called by hand
        // here — that is exactly the harness error being corrected.
        CHECK_FALSE(wheel.active(kMobileDiscoverTimerId));           // ★ the wheel itself says the timer is gone
        CHECK_FALSE(wheel.active(kPresenceProbeTimerId));
        CHECK_FALSE(wheel.active(kMobileClaimGuardTimerId));
        const int probes_before = count_p_probes(hal.tx_frames);
        hal._now += 10ull * protocol::presence_check_base_ms;        // 20 minutes of wall clock
        std::vector<int> ids;
        // ⓘ The pump legitimately fires the NODE's own periodic timers (beacon / route aging / REQ_SYNC) — a dormant
        //   mobile is still a radio node, and §4.3 ends the ATTACHMENT session, not the node. So the assertion is on
        //   the three MOBILE-FSM timer ids and on the mobile-plane FRAMES, never on a bare total.
        (void)pump_due_timers(mob, hal, wheel, &ids);
        CHECK(fired_count(ids, kMobileDiscoverTimerId)   == 0);      // ★★★ the FSM entry point never runs again
        CHECK(fired_count(ids, kPresenceProbeTimerId)    == 0);      // ★★ no presence plane either
        CHECK(fired_count(ids, kMobileClaimGuardTimerId) == 0);
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == discovers_before);   // ★★ not one DISCOVER in 20 minutes
        CHECK(count_p_probes(hal.tx_frames) == probes_before);       // ★★ …and not one probe
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::dormant);
        // ★★★ AUTONOMY RESUMES FROM A PRODUCTION PATH ONLY — the operator's `mobile register`, whose own
        // `after(0, kMobileDiscoverTimerId)` the SAME drain then picks up. No injected timer, no re-read of the flag.
        mob.mobile_register_current();
        CHECK(mob.mobile_home_desired());
        CHECK(mob.mobile_attach_state() == Node::MobileAttachState::seeking);
        CHECK(pump_due_timers(mob, hal, wheel) >= 1);                // ★ production armed it; the pump found it
        CHECK(count_j_opcode(hal.tx_frames, j_opcode::discover) == discovers_before + 1);
    }
}

// ---------------------------------------------------------------------------
// §S0-5 — A HOST ROW SURVIVES BEYOND 25 MINUTES.  Spec §2.6 / §9.1.
//
// DEFECT PINNED HERE: `mobile_liveness_ms` (protocol_constants.h:681) = 1 500 000 ms = 25 min has
// exactly ONE consumer — the hash-locate proxy gate at node_hashlocate.cpp:1098. Past that age the home
// stops proxy-answering for the mobile and NOTHING ELSE HAPPENS: the `_mobile_reg` row is never
// compacted, so indefinitely afterwards the dead mobile is still in the registry, still advertised in
// every emitted P roster, and still holding its local id against `find_free_mobile_id`. The
// documentation's 25-minute "prune" does not exist.
//
// CORRECT BEHAVIOUR (spec §9.1-§9.3, gate items 13/15), to be asserted by the S5 rewrite: at
// `>= mobile_liveness_ms` the row is physically compacted out of `_mobile_reg` AND the parallel SNR
// array by one `mobile_reg_remove(slot, reason)` primitive; it leaves the roster, releases its local id
// and frees its host slot.
// ⇒ ★ WHEN S5 LANDS THIS CASE GOES RED (`mobile_reg_count()` 1→0, the roster entry disappears, and the
//   id is re-offered). REWRITE IT; don't delete it.
//
// ★★ THE INSTRUMENT-FIRED PROOF. A "the row is still there" assertion is worthless if the clock never
// really crossed the boundary — the test would pass on a build that never advanced time at all. The
// proxy gate is the ONE behaviour that DOES change at 25 minutes, so it is used as the boundary witness:
// the same H query is answered-and-suppressed at boundary-1 ms and FORWARDED at boundary. Everything
// asserted after that is therefore known to be past the deadline.
// ---------------------------------------------------------------------------
TEST_CASE("★★★ §S0-5 CHARACTERIZATION (spec §2.6) — past mobile_liveness_ms the host row is still in the registry AND in the roster") {
    constexpr uint32_t kDead   = 0x0000D1D1u;     // the mobile that goes silent
    constexpr uint8_t  kDeadId = 254;
    constexpr uint8_t  kHomeId = 42;
    constexpr uint64_t kT0     = 100000;
    RxMeta meta{8.0f, -80.0f, 0, -1};

    TestHal hal; hal._now = kT0;
    Node home(hal, kHomeId, /*key_hash32=*/0x00004242u);
    CHECK(home.on_init(join_cfg()));
    CHECK(home.can_host_mobiles());

    // t0: the mobile registers for real (a J CLAIM stamps `last_heard_ms = now`).
    std::array<uint8_t, 16> cl{};
    const size_t cn = make_j_claim_mobile(kHomeId, kDeadId, kDead, cl);
    home.on_recv(cl.data(), cn, meta);
    CHECK(home.mobile_reg_count() == 1);                              // PREMISE: there IS a row to outlive its deadline
    // ...and it is genuinely advertised while alive.
    hal.tx_frames.clear();
    home.on_timer(kPresenceRosterTimerId);
    {
        auto e = roster_entry_for(hal.tx_frames, kDead);
        CHECK(e.has_value());
        if (e) CHECK(e->local_id == kDeadId);
    }
    // ⛔ THE MOBILE NOW GOES SILENT FOREVER — no beacon, no probe, no CLAIM refreshes `last_heard_ms`.

    // ---- BOUNDARY WITNESS (a): at 25 min MINUS 1 ms the home still proxies, so the row is LIVE.
    hal._now = kT0 + protocol::mobile_liveness_ms - 1;
    hal.events.clear(); hal.tx_frames.clear();
    {
        std::array<uint8_t, 16> q{};
        const size_t qn = make_h_query(/*origin=*/9, kDead, /*ttl=*/4, q);
        home.on_recv(q.data(), qn, meta);
        fire_h_forwards(home);
        CHECK(hal.count("h_resolved") == 1);                          // the home ANSWERED as the mobile's location authority
        CHECK(count_h_frames(hal.tx_frames) == 0);                    // ★ and SUPPRESSED the flood — the live-proxy behaviour
    }

    // ---- BOUNDARY WITNESS (b): at exactly 25 min the proxy STOPS. ★ This is the measurement that proves
    // the clock really crossed `mobile_liveness_ms`; every assertion below it is therefore past the deadline.
    hal._now = kT0 + protocol::mobile_liveness_ms;
    hal.events.clear(); hal.tx_frames.clear();
    {
        std::array<uint8_t, 16> q{};
        const size_t qn = make_h_query(/*origin=*/10, kDead, /*ttl=*/4, q);   // a fresh origin: not the dedup ring
        home.on_recv(q.data(), qn, meta);
        fire_h_forwards(home);
        CHECK(hal.count("h_resolved") == 0);                          // ★ no proxy answer — the ONE time-based effect
        CHECK(hal.count("h_forward") == 1);
        CHECK(count_h_frames(hal.tx_frames) == 1);                    // ★ the flood is passed on instead, ON THE WIRE
    }

    // ★★ THE DEFECT, AT THE BOUNDARY: everything else about the row is untouched.
    CHECK(home.mobile_reg_count() == 1);                              // ← S5 must make this 0
    hal.tx_frames.clear();
    home.on_timer(kPresenceRosterTimerId);
    {
        auto e = roster_entry_for(hal.tx_frames, kDead);
        CHECK(e.has_value());                                         // ★★ STILL ADVERTISED as a hosted mobile
        if (e) CHECK(e->local_id == kDeadId);
    }
    // ...and the local id is STILL RESERVED: a brand-new mobile DISCOVERing now is offered a DIFFERENT id.
    {
        CHECK(stage_mobile_offer(home, hal, /*mobile_hash=*/0x0000E5E5u) == 1);
        hal.tx_frames.clear();
        fire_mobile_offer_timer(home, hal);
        auto o = first_j(hal.tx_frames, j_opcode::offer);
        CHECK(o.has_value());
        if (o) { CHECK(o->target_key_hash32 == 0x0000E5E5u);
                 CHECK(o->proposed_mobile_id != kDeadId);             // ★ 254 is held by a mobile dead for 25 minutes
                 CHECK(o->proposed_mobile_id == kDeadId - 1); }       // the picker simply walks down past it
    }

    // ★★ AND FAR BEYOND IT: a second full liveness period later — 50 minutes of silence — nothing has aged
    // out. The row is not slow to expire; it does not expire.
    hal._now = kT0 + 2ull * protocol::mobile_liveness_ms;
    CHECK(home.mobile_reg_count() == 1);                              // ★ exactly the dead row — the new mobile above was
                                                                      // only OFFERed, and a staged OFFER is not a registry
                                                                      // row (it never CLAIMed), so this 1 IS the corpse
    hal.tx_frames.clear();
    home.on_timer(kPresenceRosterTimerId);
    {
        auto e = roster_entry_for(hal.tx_frames, kDead);
        CHECK(e.has_value());                                         // ★ 50 minutes silent, still in the roster
        if (e) CHECK(e->local_id == kDeadId);
    }
}
