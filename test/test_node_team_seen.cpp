// MeshRoute — test_node_team_seen.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 N1 (spec docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §2.1/§4-N1) — the
// READ-ONLY nearby-team observation cache, in two halves that are deliberately driven separately:
//
//   PART A — the PURE ring (lib/core/team_seen_ring.h), driven with no Node, no HAL and no frame. Every
//     ruled decision lives here — de-duplication, the SNR EWMA fold-in, retention-at-the-read, and the
//     structural first-observed ORDER (refresh in place; on overflow shift the stalest out and append).
//     ⛔ The EWMA expectation is computed by an INDEPENDENT reference loop written out longhand below,
//     never by calling `snr_ewma_update` — a test that asks the implementation what the answer is cannot
//     redden when the implementation is mutated to max-seen or to last-sample.
//
//   PART B — the ONE write site (lib/core/node_beacon.cpp), driven through the existing beacon-injection
//     fixture. What is pinned here is the ELIGIBILITY RULE and, above all, that the observation is
//     READ-ONLY: across a stream of foreign-team beacons the routing tables, the team key/peer state, the
//     content key and our own team id are all unchanged. ⛔ No friend seam is added for any of it.
//
// ⓘ NOT DRIVABLE HERE, and said out loud rather than silently skipped: the `!MR_FEAT_TEAM` inert stubs
// (pin 10). The native env compiles MR_FEAT_TEAM 1 by profile, so the stubs are proven by the board
// builds — which are the gate's — plus the `sizeof(Node)` reveal on the gateway flag set (+0).
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"
#include "team_seen_ring.h"
#include "support/test_hal.h"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// The retention window, spelled once. Read from the constant so a re-tuning of the window re-tunes the
// suite with it; the BOUNDARY behaviour is pinned explicitly in its own case below.
constexpr uint64_t kRetain = protocol::team_seen_retain_ms;
constexpr std::size_t kCap = protocol::cap_team_seen;

// ★ THE INDEPENDENT EWMA REFERENCE — written longhand, on purpose. This is the α = 5/16 seed-if-zero
// recurrence stated as arithmetic, so it is a SPECIFICATION of the expected series rather than a second
// call into the code under test. Mutate the ring to max-seen or to last-sample and this disagrees.
int16_t ref_ewma(const std::vector<int16_t>& samples) {
    int32_t ew = 0;
    for (int16_t s : samples) ew = (ew == 0) ? s : (ew + (((s - ew) * 5) >> 4));
    return static_cast<int16_t>(ew);
}

// ---- Part B fixture: a plain in-memory Hal that records emitted event types + TX frames -------------
class TestHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> ev;
    std::vector<std::vector<uint8_t>> tx_frames;
    TxResult tx(const uint8_t* b, size_t n, const TxParams&) override {
        tx_frames.push_back(std::vector<uint8_t>(b, b + n)); return TxResult::ok;
    }
    void emit(const char* type, const EventField*, size_t) override { ev.push_back(type); }
    int count(const char* t) const { int n = 0; for (const auto& e : ev) if (e == t) ++n; return n; }
};

static RxMeta meta_snr(float snr_db) { RxMeta m{}; m.snr_db = snr_db; m.rssi_dbm = -70.0f; m.recv_ms = 0; m.src_hint = -1; return m; }

// One team beacon from `src`, mobile-by-default, carrying the type-5 team TLV when `team_id != 0`.
static size_t mk_team_beacon(uint8_t src, uint32_t team_id, bool is_mobile, std::array<uint8_t, 64>& b) {
    uint8_t ext[8];
    const size_t en = team_id ? pack_team_id_tlv(team_id, std::span<uint8_t>(ext, sizeof ext)) : 0;
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x5A00u + src; in.is_mobile = is_mobile;
    if (en) in.ext = std::span<const uint8_t>(ext, en);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}

// A teamless receiver on leaf 0 — the JOINER of §3.6.4 point 2.
static void init_joiner(Node& node) {
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.quiet_threshold_ms = 0; cfg.team_id = 0;
    CHECK(node.on_init(cfg));
}

// Feed one beacon at absolute time `at_ms` with the given reported SNR.
static void hear(TestHal& hal, Node& node, uint8_t src, uint32_t team_id, bool is_mobile,
                 uint64_t at_ms, float snr_db) {
    std::array<uint8_t, 64> b{};
    const size_t n = mk_team_beacon(src, team_id, is_mobile, b);
    CHECK(n > 0);
    hal._now = at_ms;
    node.on_recv(b.data(), n, meta_snr(snr_db));
}

}  // namespace

// ===================================================================================================
// PART A — the pure ring
// ===================================================================================================

TEST_CASE("§UI-16 N1 pure — the WINDOW and the CAPACITY are pinned AT THEIR DERIVATION, not as bare literals") {
    // ★ THE RULING IS ARITHMETIC, SO THE PIN IS ARITHMETIC (owner ruling R-2(i), 2026-08-22): ten minutes
    // is EXACTLY TWO default team-beacon periods — the smallest window that survives one missed beacon.
    // Written against `NodeConfig`'s own default rather than against the number, so the two cannot drift
    // apart silently: retune the team beacon period and this states which decision must be re-taken.
    const NodeConfig defaults{};
    CHECK(defaults.team_beacon_period_ms == 300000u);
    CHECK(protocol::team_seen_retain_ms == 2u * defaults.team_beacon_period_ms);
    CHECK(protocol::team_seen_retain_ms == 600000u);   // ...and the absolute value, so a config retune cannot move the window by accident
    CHECK(protocol::cap_team_seen == 8);               // owner ruling R-2; 16 B/slot (pinned in team_seen_ring.h) = 128 B of Node RAM
}

TEST_CASE("§UI-16 N1 pure — a first observation SEEDS the record: id, EWMA-seeded SNR, arrival stamp") {
    TeamSeen ring[kCap] = {}; uint8_t n = 0;
    team_seen_ring_observe(ring, n, /*team_id=*/0xABCD1234u, /*sample_q4=*/96, /*src_id=*/210, /*now=*/1000);
    CHECK(n == 1);
    CHECK(ring[0].team_id == 0xABCD1234u);
    CHECK(ring[0].snr_q4 == 96);                 // seed-if-zero: the FIRST sample is adopted outright
    CHECK(ring[0].last_ms == 1000u);
    CHECK(ring[0].src_id == 210);
    CHECK(ring[0].reserved == 0);                // the NAMED tail byte is zero-initialised, never indeterminate
    CHECK(team_seen_ring_live_count(ring, n, /*now=*/1000, kRetain) == 1);
    const TeamSeen* e = team_seen_ring_live_at(ring, n, 0, /*now=*/1000, kRetain);
    CHECK(e != nullptr);
    if (e) CHECK(e->team_id == 0xABCD1234u);
    CHECK(team_seen_ring_live_at(ring, n, 1, /*now=*/1000, kRetain) == nullptr);   // past the end = absent, not a zero record
}

TEST_CASE("§UI-16 N1 pure — DE-DUPLICATION is by TEAM ID: many senders of one team leave ONE entry") {
    TeamSeen ring[kCap] = {}; uint8_t n = 0;
    // Five beacons, four different advertisers, ONE team.
    team_seen_ring_observe(ring, n, 0x11112222u, 80, /*src=*/11, 1000);
    team_seen_ring_observe(ring, n, 0x11112222u, 80, /*src=*/12, 2000);
    team_seen_ring_observe(ring, n, 0x11112222u, 80, /*src=*/13, 3000);
    team_seen_ring_observe(ring, n, 0x11112222u, 80, /*src=*/12, 4000);
    team_seen_ring_observe(ring, n, 0x11112222u, 80, /*src=*/14, 5000);
    CHECK(n == 1);                               // ★ the de-dup key is the TEAM, never the sender
    CHECK(ring[0].last_ms == 5000u);             // the stamp is the MOST RECENT arrival
    CHECK(ring[0].src_id == 14);                 // ...and the advertiser is the last one heard
    // A SECOND team from a sender we have already seen is a SEPARATE entry — the key really is the team.
    team_seen_ring_observe(ring, n, 0x33334444u, 80, /*src=*/12, 6000);
    CHECK(n == 2);
    CHECK(ring[1].team_id == 0x33334444u);
}

TEST_CASE("§UI-16 N1 pure — the SNR is EWMA-SMOOTHED, never max-seen and never last-sample") {
    TeamSeen ring[kCap] = {}; uint8_t n = 0;
    // A strong first sample followed by a run of weak ones — the sequence that separates the three
    // candidate policies. A single-sample fixture passes under ALL of them, which is why this is a run.
    const std::vector<int16_t> series{ 160, -80, -80, -80, -80 };   // +10 dB, then four at −5 dB
    for (int16_t s : series) team_seen_ring_observe(ring, n, 0x9999u, s, /*src=*/7, 1000);
    const int16_t expect = ref_ewma(series);
    CHECK(n == 1);
    CHECK(ring[0].snr_q4 == expect);
    // ★ THE TWO REFUSED POLICIES, asserted as INEQUALITIES so this case reddens if either is substituted:
    CHECK(ring[0].snr_q4 != 160);                // ⛔ NOT "strongest seen in the window" — max-seen never decays
    CHECK(ring[0].snr_q4 != -80);                // ⛔ NOT the raw last sample either — the series is smoothed
    CHECK(ring[0].snr_q4 < 160);                 // ...and it has genuinely DECAYED away from the strong moment
    CHECK(ring[0].snr_q4 > -80);
    // And it keeps decaying toward the weak level as the run continues.
    const int16_t before = ring[0].snr_q4;
    team_seen_ring_observe(ring, n, 0x9999u, -80, /*src=*/7, 2000);
    CHECK(ring[0].snr_q4 < before);
}

TEST_CASE("§UI-16 N1 pure — RETENTION IS APPLIED AT THE READ, and the boundary is INCLUSIVE") {
    TeamSeen ring[kCap] = {}; uint8_t n = 0;
    team_seen_ring_observe(ring, n, 0xAAAAu, 64, 1, /*now=*/1000);
    team_seen_ring_observe(ring, n, 0xBBBBu, 64, 2, /*now=*/1000 + kRetain);   // one window later
    CHECK(n == 2);
    // At exactly `1000 + kRetain` the first entry's age is EXACTLY the window: it is still LIVE
    // (recent_ring_cutoff's clamped-inclusive boundary, shared with every other ring in this tree).
    CHECK(team_seen_ring_live_count(ring, n, 1000 + kRetain, kRetain) == 2);
    // One millisecond later it has aged out of the READ — while the array itself is untouched.
    CHECK(team_seen_ring_live_count(ring, n, 1001 + kRetain, kRetain) == 1);
    CHECK(n == 2);                                                     // ⛔ no sweep, no timer, no mutation at read
    CHECK(ring[0].team_id == 0xAAAAu);
    // ...and the surviving entry is the one the reader indexes at 0 — the stale one is SKIPPED, not shown.
    const TeamSeen* e = team_seen_ring_live_at(ring, n, 0, 1001 + kRetain, kRetain);
    CHECK(e != nullptr);
    if (e) CHECK(e->team_id == 0xBBBBu);
    CHECK(team_seen_ring_live_at(ring, n, 1, 1001 + kRetain, kRetain) == nullptr);
    // A clock that moved BACKWARDS cannot underflow the cutoff into a huge number (the clamped form).
    CHECK(team_seen_ring_live_count(ring, n, /*now=*/10, kRetain) == 2);
}

TEST_CASE("§UI-16 N1 pure — ORDER IS FIRST-OBSERVED AND STRUCTURAL: a refresh does NOT move an entry") {
    TeamSeen ring[kCap] = {}; uint8_t n = 0;
    team_seen_ring_observe(ring, n, 0x100u, 64, 1, 1000);
    team_seen_ring_observe(ring, n, 0x200u, 64, 2, 2000);
    team_seen_ring_observe(ring, n, 0x300u, 64, 3, 3000);
    // Refresh the OLDEST entry hard — under a move-to-end policy it would jump to the tail.
    team_seen_ring_observe(ring, n, 0x100u, 64, 1, 9000);
    CHECK(n == 3);
    CHECK(ring[0].team_id == 0x100u);            // ★ still FIRST — the list cannot re-order under the cursor
    CHECK(ring[1].team_id == 0x200u);
    CHECK(ring[2].team_id == 0x300u);
    CHECK(ring[0].last_ms == 9000u);             // ...refreshed in place
}

TEST_CASE("§UI-16 N1 pure — OVERFLOW SHIFTS THE STALEST OUT AND APPENDS: survivors keep their order, the newcomer is LAST") {
    TeamSeen ring[kCap] = {}; uint8_t n = 0;
    for (uint8_t i = 0; i < static_cast<uint8_t>(kCap); ++i)
        team_seen_ring_observe(ring, n, 0x1000u + i, 64, i, 1000u + i);
    CHECK(n == static_cast<uint8_t>(kCap));
    // Refresh the FIRST-observed entry so the stalest stamp is no longer at index 0 — this is what
    // separates "drop the head" from "drop the stalest", and the ruling is the stalest.
    team_seen_ring_observe(ring, n, 0x1000u, 64, 0, 50000);
    // One more team than the ring can hold.
    team_seen_ring_observe(ring, n, 0xDEAD0001u, 64, 99, 60000);
    CHECK(n == static_cast<uint8_t>(kCap));                  // still bounded — nothing grew
    CHECK(ring[0].team_id == 0x1000u);                       // the refreshed first-observed entry SURVIVES
    CHECK(ring[1].team_id == 0x1002u);                       // 0x1001 (the stalest) was the victim
    for (uint8_t i = 1; i + 1 < static_cast<uint8_t>(kCap); ++i)
        CHECK(ring[i].team_id == 0x1000u + (i + 1));         // survivors kept their RELATIVE order
    CHECK(ring[kCap - 1].team_id == 0xDEAD0001u);            // ★ the newcomer is APPENDED LAST, never dropped
    // ⛔ and the NEWEST is never the victim: the team we just added is readable.
    bool found_new = false;
    for (uint8_t i = 0; i < n; ++i) if (ring[i].team_id == 0xDEAD0001u) found_new = true;
    CHECK(found_new);
}

// ===================================================================================================
// PART B — the ONE write site
// ===================================================================================================

TEST_CASE("§UI-16 N1 site — a FOREIGN team's mobile beacon lands ONE entry: parsed id, seeded SNR, arrival stamp") {
    TestHal hal; Node node(hal, /*id=*/40, /*key=*/0x7001u);
    init_joiner(node);
    hear(hal, node, /*src=*/213, /*team_id=*/0xFEED0001u, /*is_mobile=*/true, /*at=*/5000, /*snr=*/6.0f);
    CHECK(node.team_seen_count() == 1);
    const TeamSeen* e = node.team_seen_at(0);
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->team_id == 0xFEED0001u);
        CHECK(e->snr_q4  == protocol::db_to_q4(6.0f));   // seeded from THIS beacon's reported SNR
        CHECK(e->last_ms == 5000u);                      // stamped with the arrival instant
        CHECK(e->src_id  == 213);
    }
    // A second, DIFFERENT foreign team is a second row, in first-observed order.
    hear(hal, node, /*src=*/77, /*team_id=*/0xFEED0002u, /*is_mobile=*/true, /*at=*/6000, /*snr=*/6.0f);
    CHECK(node.team_seen_count() == 2);
    const TeamSeen* e1 = node.team_seen_at(1);
    CHECK(e1 != nullptr);
    if (e1) CHECK(e1->team_id == 0xFEED0002u);
    CHECK(node.team_seen_at(2) == nullptr);
}

TEST_CASE("§UI-16 N1 site — the ELIGIBILITY RULE: a non-zero team id on a MOBILE beacon, and nothing else") {
    {   // (A) NO TLV at all (team_id 0) — the s18/static shape. ⛔ Nothing is recorded.
        TestHal hal; Node node(hal, /*id=*/41, /*key=*/0x7002u);
        init_joiner(node);
        hear(hal, node, /*src=*/50, /*team_id=*/0, /*is_mobile=*/true, 5000, 6.0f);
        CHECK(node.team_seen_count() == 0);
        CHECK(node.team_seen_at(0) == nullptr);
    }
    {   // (B) a STATIC advertiser carrying a team TLV — not a team member's beacon. ⛔ Nothing recorded.
        TestHal hal; Node node(hal, /*id=*/42, /*key=*/0x7003u);
        init_joiner(node);
        hear(hal, node, /*src=*/51, /*team_id=*/0xFEED0003u, /*is_mobile=*/false, 5000, 6.0f);
        CHECK(node.team_seen_count() == 0);
    }
    {   // (C) OUR OWN team is recorded like any other — the own-team filter belongs to the READER (N2),
        //     so "which teams are audible" has exactly one definition.
        TestHal hal; Node node(hal, /*id=*/0, /*key=*/0x7004u);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
        cfg.quiet_threshold_ms = 0; cfg.is_mobile = true; cfg.team_id = 0xC0FFEE01u;
        CHECK(node.on_init(cfg));
        hear(hal, node, /*src=*/213, /*team_id=*/0xC0FFEE01u, /*is_mobile=*/true, 5000, 6.0f);
        CHECK(node.team_seen_count() == 1);
        const TeamSeen* e = node.team_seen_at(0);
        CHECK(e != nullptr);
        if (e) CHECK(e->team_id == 0xC0FFEE01u);
    }
}

TEST_CASE("§UI-16 N1 site — de-duplicated across SENDERS, and retention applies to the node's read too") {
    TestHal hal; Node node(hal, /*id=*/43, /*key=*/0x7005u);
    init_joiner(node);
    for (uint8_t s = 200; s < 205; ++s) hear(hal, node, s, 0xFEED0004u, true, 5000 + s, 6.0f);
    CHECK(node.team_seen_count() == 1);          // five beacons, five senders, ONE team
    hal._now = 5000 + 204 + kRetain;             // exactly one window after the last arrival
    CHECK(node.team_seen_count() == 1);
    hal._now = 5000 + 204 + kRetain + 1;         // ...and one millisecond past it
    CHECK(node.team_seen_count() == 0);
    CHECK(node.team_seen_at(0) == nullptr);
}

TEST_CASE("§UI-16 N1 site — a frame that fails the WIRE-VERSION gate or PARSE records nothing") {
    {   // (A) foreign wire_version: byte 3's low nibble is read RAW, before parse, and refuses.
        TestHal hal; Node node(hal, /*id=*/44, /*key=*/0x7006u);
        init_joiner(node);
        std::array<uint8_t, 64> b{};
        const size_t n = mk_team_beacon(/*src=*/213, /*team_id=*/0xFEED0005u, /*is_mobile=*/true, b);
        CHECK(n > 0);
        std::array<uint8_t, 64> bad = b;
        bad[3] = static_cast<uint8_t>((bad[3] & 0xF0) | ((protocol::wire_version + 1) & 0x0F));
        hal._now = 5000;
        node.on_recv(bad.data(), n, meta_snr(6.0f));
        CHECK(node.team_seen_count() == 0);           // ★ the write site sits AFTER the gate, so nothing lands
        // SAME-SITE CONTROL — without which this case would pass for the wrong reason: the UNMUTATED
        // frame, same node, same instant, DOES land. So it really is the version gate doing the work.
        node.on_recv(b.data(), n, meta_snr(6.0f));
        CHECK(node.team_seen_count() == 1);
    }
    {   // (B) a truncated frame that parse_beacon refuses.
        TestHal hal; Node node(hal, /*id=*/45, /*key=*/0x7007u);
        init_joiner(node);
        std::array<uint8_t, 64> b{};
        const size_t n = mk_team_beacon(/*src=*/213, /*team_id=*/0xFEED0006u, /*is_mobile=*/true, b);
        CHECK(n > 8);
        hal._now = 5000;
        node.on_recv(b.data(), 6, meta_snr(6.0f));    // >= 4 and cmd/leaf-nibble OK, but unparseable
        CHECK(node.team_seen_count() == 0);
        node.on_recv(b.data(), n, meta_snr(6.0f));    // SAME-SITE CONTROL: the whole frame lands
        CHECK(node.team_seen_count() == 1);
    }
}

TEST_CASE("§UI-16 N1 site — ★★ ZERO TELEMETRY ON THE NEW PATH: the observation emits NOTHING") {
    // ⛔⛔ WHY THIS IS A PINNED NUMBER AND NOT A NAME TEST: the refusal being pinned is "no MR_EMIT ANYWHERE
    // on the new path", and a control that greps for one chosen event name only catches the name it
    // guessed. The absence of telemetry is what keeps the five TEAM corpus scenarios byte-identical across
    // this slice — an emit here would re-anchor them in the same run as the behaviour change and make the
    // delta unattributable (C4 applied to telemetry). ⓘ The corpus half of that control is the gate's; this
    // is its native half, and it reddens the moment an emit is added to the observation.
    TestHal hal; Node node(hal, /*id=*/47, /*key=*/0x7009u);
    init_joiner(node);
    const size_t ev0 = hal.ev.size();
    hear(hal, node, /*src=*/213, /*team_id=*/0xFEED2001u, /*is_mobile=*/true, /*at=*/5000, /*snr=*/6.0f);
    CHECK(node.team_seen_count() == 1);            // the observation really did run...
    CHECK(hal.ev.size() - ev0 == 1);               // ...and the ONLY event the beacon produced is the
                                                   // pre-existing per-beacon `beacon_rx`. ⛔ N1 adds none.
}

TEST_CASE("§UI-16 N1 site — ★★★ THE OBSERVATION IS READ-ONLY: a stream of foreign-team beacons moves NO other plane") {
    TestHal hal; Node node(hal, /*id=*/46, /*key=*/0x7008u);
    init_joiner(node);
    // Baseline, taken on the initialised node before a single beacon is heard.
    const uint8_t  rt0      = node.rt_count();
    const uint8_t  rtteam0  = node.rt_team_count();
    const uint16_t idbind0  = node.id_bind_count();
    const uint16_t pkeys0   = node.peer_key_count();
    const uint32_t myteam0  = node.config().team_id;
    const size_t   tx0      = hal.tx_frames.size();
    // Three foreign teams, several advertisers each, repeated — the shape a joiner actually sits in.
    const uint32_t teams[3] = { 0xFEED1001u, 0xFEED1002u, 0xFEED1003u };
    uint64_t t = 5000;
    for (int round = 0; round < 3; ++round)
        for (int ti = 0; ti < 3; ++ti)
            for (uint8_t s = 210; s < 214; ++s) { hear(hal, node, s, teams[ti], true, t, 3.0f); t += 100; }
    // The cache DID fill — otherwise everything below would pass for the wrong reason.
    CHECK(node.team_seen_count() == 3);
    // ★ AND NOTHING ELSE MOVED.
    CHECK(node.rt_count()      == rt0);        // no static route learned from a foreign team's advertiser
    CHECK(node.rt_team_count() == rtteam0);    // ⛔ no team-plane DV merge
    CHECK(node.id_bind_count() == idbind0);    // ⛔ no id->hash binding
    CHECK(node.peer_key_count() == pkeys0);    // ⛔ no peer-key cache write
    CHECK(node.config().team_id == myteam0);   // ⛔ we did not join anything
    CHECK_FALSE(node.team_channel_key_present());   // ⛔ and we hold no content key
    for (uint8_t s = 210; s < 214; ++s) {
        uint32_t h = 0;
        CHECK_FALSE(node.is_team_peer(s));      // ⛔ _team_peer untouched
        CHECK_FALSE(node.team_key_of_id(s, h)); // ⛔ _team_keys untouched
    }
    CHECK(hal.tx_frames.size() == tx0);         // ⛔ THE SCAN TRANSMITS NOTHING
}
