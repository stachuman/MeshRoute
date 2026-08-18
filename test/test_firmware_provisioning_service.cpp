// MeshRoute — test_firmware_provisioning_service.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK,
// never REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §PROV-TX — the native suite for the TYPED TEAM-PROVISIONING TRANSACTION (src/firmware_provisioning_service.h),
// spec docs/superpowers/specs/2026-08-17-team-provisioning-transaction-design.md §5, defect [[B207]].
//
// ★★★ WHY THIS FILE EXISTS AT ALL: `handle_team` lives in `src/firmware_config.cpp`, which is compiled by NEITHER the
//     native suite (`test_build_src = no`) NOR the simulator, and no scenario runs a console verb. Every decision the
//     old `handle_team` made — the role refusal, the incomplete-PHY refusal, the key adopt/mint, the switch, the DAD —
//     was therefore unreachable by every automated gate this project has. Moving those decisions into a pure header is
//     what makes the cases below possible; ⇒ a green run here is the FIRST automated cover this verb has ever had.
//
// ★★ THE INSTRUMENTS AND WHY THEY ARE COUNTERS, NOT BOOLEANS. Three of this slice's requirements are statements about
//    HOW MANY TIMES something happened:
//      · "EXACTLY ONE save attempt"                      -> FakeProvStore::writes, asserted == a number
//      · "ZERO calls on every IProvLive method"          -> four separate counters, each asserted == 0
//      · ★★ "and SPEND NO AIRTIME"                       -> FakeProvLive::dad_calls, asserted == 0
//        ⛔ NO ORDERING-ONLY TEST DETECTS THE AIRTIME CLAUSE. That is why it is a count.
// ★★ AND THE COUNTERS THEMSELVES ARE CONTROLLED. This project has recorded EIGHT instruments that were green against
//    the very defect they were written to catch, and *a "no mutation" assertion whose counters are never incremented on
//    the success path would be the ninth*. So: the first case drives the fakes DIRECTLY and requires every counter to
//    move, and every zero-asserting case below carries a POSITIVE ARM in the same case — the identical request with the
//    failure removed — so each zero is evidence rather than an absence.
//
// ★ THE FAKE LIVE SINK MODELS THE ONE CORE BEHAVIOUR THE ORDER DEPENDS ON: `Node::set_team_id` DESTROYS the team
//   channel key (§o3-key-lifetime). `FakeProvLive::set_team` clears its key too — which is what makes "set_team BEFORE
//   install_key" a MEASUREMENT (swap them in the source and the key ends up absent) instead of a claim about line
//   numbers.
//
// ⛔⛔ WHAT THESE CASES MAY NOT BE DESCRIBED AS PROVING (spec §5.1, a QG-required boundary): they prove
//    **"exactly one save attempt, and zero live mutation when the store reports failure."** They do NOT prove that
//    physical NV was unchanged during a partial write — a fake store cannot demonstrate that. That property is
//    conditional on [[B193]] §20.5 on real hardware. ⇒ this slice makes the CALLER transactional; B193 establishes that
//    the STORAGE is. Neither substitutes for the other, and ⛔ this slice does not close B193.
#include "doctest.h"
#include "firmware_provisioning_service.h"
#include "identity.h"                  // meshroute::team_channel_key_derive — used to BUILD a genuinely mismatched pair
#include <cstdint>
#include <cstring>

using mrfw::KeyAction;
using mrfw::ProvErr;
using mrfw::ProvPhy;
using mrfw::ProvSnapshot;
using mrfw::ProvVerdict;
using mrfw::ProvisioningService;
using mrfw::TeamRequest;

namespace {

// A stamped, plausible `/mrcfg` record. ★ It carries a COMPLETE PHY on purpose: without one every non-leave request
// would refuse on `incomplete_phy` and half the matrix below would pass for the wrong reason.
mrnv::Blob prov_seed_record() {
    mrnv::Blob b{};
    b.magic   = mrnv::kMagic;
    b.version = mrnv::kVersion;
    b.freq_mhz          = 869.4625;
    b.bw_hz             = 125000;
    b.routing_sf        = 7;
    b.allowed_sf_bitmap = static_cast<uint16_t>(1u << 7);
    // ★★ IT ALSO AGREES WITH `PFix`'s LIVE ROLE (`cfg.is_mobile = true`), and that is a REQUIREMENT of the fixture
    //    rather than a detail. `no_change` is derived from the candidate's differences against THIS record, and the
    //    candidate always writes the PROJECTED `is_mobile` — so a record that said 0 while the live node said mobile
    //    would make EVERY request a role-projection change and the no-change rows below untestable. ⛔ The three cases
    //    that set `cfg.is_mobile = false` therefore set this back to 0 themselves; otherwise their
    //    `CHECK(rec.is_mobile == 1)` would pass because the seed already said 1, which is the vacuity this comment
    //    exists to stop.
    b.is_mobile = 1;
    // the fields the transaction must PRESERVE unless it has a reason not to
    b.node_id       = 42;
    b.team_local_id = 9;
    b.channel_ctr   = 7;
    b.admin_counter_floor = 99;
    b.e2e_dm        = 1;
    b.intro_attach  = 1;
    return b;
}

struct FakeProvStore : mrfw::ICfgStore {
    mrnv::Blob rec = prov_seed_record();
    bool have     = true;      // false => `load` reports no usable record
    bool write_ok = true;      // false => the write FAILS and the record is NOT modified
    int  writes   = 0;         // ★ every save ATTEMPT, successful or not
    int  loads    = 0;
    bool load(mrnv::Blob& out) override {
        ++loads;
        if (!have) return false;
        out = rec;
        return true;
    }
    bool save(const mrnv::Blob& b) override {
        ++writes;
        if (!write_ok) return false;
        rec = b;
        return true;
    }
};

// THE LIVE SEAM's fake — a sink that really MOVES, so "live state is unchanged" is a measurement, not an absence.
struct FakeProvLive : mrfw::IProvLive {
    uint32_t team_id     = 0;
    bool     key_present = false;
    uint8_t  pub[32]     = {};
    uint8_t  priv[32]    = {};
    ProvPhy  phy{};

    int set_team_calls = 0, install_calls = 0, phy_calls = 0, dad_calls = 0;
    // ★ THE ORDER PROOF: a monotonic tick stamped at each call, so "set_team before install_key" and "DAD last" are
    //   comparisons of recorded facts rather than of source lines.
    int seq = 0, set_team_at = 0, install_at = 0, phy_at = 0, dad_at = 0;
    // ★ THE DURABILITY PROOF: what the STORE held at the instant each live call ran. A live apply that happened before
    //   (or instead of) the durable write is then VISIBLE, not merely suspected.
    FakeProvStore* store = nullptr;
    uint32_t persisted_team_at_set_team = 0xFFFFFFFFu;
    uint32_t persisted_team_at_install   = 0xFFFFFFFFu;

    void set_team(uint32_t t) override {
        ++set_team_calls; set_team_at = ++seq;
        team_id = t;
        // ★★ THE MODELLED CORE BEHAVIOUR: `set_team_id` DESTROYS the team channel key (§o3-key-lifetime). Without this
        //    line the ordering requirement would be untestable — and it is the exact reason the install must come after.
        key_present = false;
        memset(pub, 0, sizeof pub);
        memset(priv, 0, sizeof priv);
        if (store) persisted_team_at_set_team = store->rec.team_id;
    }
    void install_key(const uint8_t p[32], const uint8_t s[32]) override {
        ++install_calls; install_at = ++seq;
        memcpy(pub, p, 32);
        memcpy(priv, s, 32);
        key_present = true;
        if (store) persisted_team_at_install = store->rec.team_id;
    }
    void apply_phy(const ProvPhy& p) override { ++phy_calls; phy_at = ++seq; phy = p; }
    void fire_dad() override { ++dad_calls; dad_at = ++seq; }
    int total_calls() const { return set_team_calls + install_calls + phy_calls + dad_calls; }
};

// THE ENTROPY SEAM's fake. ⚠ IT DISTINGUISHES THE TWO DRAWS BY SIZE, and that is stated rather than hidden: the
// transaction draws 4 bytes for the team-id nonce and 32 for the key scalar, in that order (the same order and the same
// source the old inline code used, which is why the simulator's per-node stream cannot move).
// `nonce_seq` is what makes two otherwise-unreachable behaviours testable: `team new` REGENERATING THE CURRENT ID, and
// `team_fnv1a32` RETURNING 0. Neither can be produced against a live CSPRNG.
struct FakeEntropy : mrfw::IEntropy {
    int      nonce_draws = 0, key_draws = 0;
    bool     key_all_zero = false;                // a DEAD CSPRNG: derive() must refuse
    uint32_t nonce_seq[8] = {};
    uint8_t  nonce_n = 0, nonce_i = 0;
    bool     nonce_stick_last = false;            // once the list runs out, keep returning its last value
    uint8_t  key_seed = 0x11;
    void fill(uint8_t* out, size_t n) override {
        if (n == sizeof(uint32_t)) {
            ++nonce_draws;
            uint32_t v = 0;
            if (nonce_n) {
                if (nonce_i < nonce_n) v = nonce_seq[nonce_i++];
                else                   v = nonce_stick_last ? nonce_seq[nonce_n - 1] : 0xC0DE0000u + static_cast<uint32_t>(nonce_draws);
            } else {
                v = 0xA5A50000u + static_cast<uint32_t>(nonce_draws);
            }
            memcpy(out, &v, sizeof v);
            return;
        }
        ++key_draws;
        for (size_t i = 0; i < n; ++i) out[i] = key_all_zero ? 0 : static_cast<uint8_t>(key_seed + i);
    }
};

// The fixture: a store, a live sink, an entropy seam, a LIVE NodeConfig and the service over all three.
// Default live node: an already-MOBILE, TEAMLESS node — the common case, and the one where no role promotion is owed.
struct PFix {
    FakeProvStore store;
    FakeProvLive  live;
    FakeEntropy   ent;
    meshroute::NodeConfig cfg{};
    ProvSnapshot  snap{};
    ProvisioningService svc{store, live, ent};
    // ★★ THE FIXTURE'S OWN LIVE-KEY STORAGE, and it is deliberately NOT the record's buffers. `ProvSnapshot` carries
    //    POINTERS to the live pair (no secret copy, §3.10 — see the struct), and if those pointed INTO `store.rec` then
    //    "the record holds X" and "the node holds X" could never be made to disagree — which is precisely the
    //    divergence the QG-round-3 cases below have to produce. Two independent buffers, so the two facts are two facts.
    uint8_t live_pub[32]  = {};
    uint8_t live_priv[32] = {};
    PFix() {
        cfg.is_mobile = true;
        cfg.team_id   = 0;
        snap.key_hash32       = 0xDEADBEEFu;
        snap.mobile_reg_count = 0;
        live.store = &store;
        // ★★ THE DEFAULT IS A **CONVERGED** NODE: the live radio flies exactly the PHY the record holds, and no team
        //    key is installed (matching `cfg`'s default). ⚠ THIS IS A REQUIREMENT OF THE FIXTURE, not a detail, and it
        //    is the mirror of `prov_seed_record`'s `is_mobile = 1` note: `no_change` now ALSO requires every
        //    explicitly-requested LIVE domain to match, so a fixture whose live PHY was left at 0 would make EVERY
        //    request carrying a PHY tail `applied` and would render the no-change rows below untestable-for-the-
        //    right-reason. ⇒ the cases that need a DIVERGENCE state it themselves, one field at a time.
        converge_live_phy();
    }
    // Copies the record's PHY into the live snapshot BY VALUE — so a later change to the record does NOT drag the live
    // reading along with it (that independence is what makes the divergence cases below measurements).
    void converge_live_phy() {
        snap.live_freq_mhz          = store.rec.freq_mhz;
        snap.live_bw_hz             = store.rec.bw_hz;
        snap.live_routing_sf        = store.rec.routing_sf;
        snap.live_allowed_sf_bitmap = store.rec.allowed_sf_bitmap;
    }
    // The live team key, as the node would hold it. `nullptr` is what `Node::team_channel_pub()` returns with no key.
    void set_live_key(const uint8_t* pub, const uint8_t* priv) {
        memcpy(live_pub, pub, 32);
        memcpy(live_priv, priv, 32);
        snap.live_key_pub  = live_pub;
        snap.live_key_priv = live_priv;
    }
    // The BUILD floor every console request carries. Set on every request so a `0` in the record cannot silently make
    // the incomplete-PHY refusal fire for the wrong reason.
    void floor(TeamRequest& r) const { r.floor.freq_mhz = 868.0; r.floor.bw_hz = 125000; }
    TeamRequest mint()            { TeamRequest r{}; r.mint = true;   floor(r); return r; }
    TeamRequest join(uint32_t id) { TeamRequest r{}; r.team_id = id;  floor(r); return r; }
    TeamRequest leave()           { TeamRequest r{}; r.team_id = 0;   floor(r); return r; }
};

// A well-formed X25519 keypair from an arbitrary scalar — the caller gets the CANONICAL pair, exactly as the
// transaction will derive it, so a matching request cannot fail for an unrelated reason.
bool make_pair(uint8_t seed_byte, uint8_t pub[32], uint8_t priv[32]) {
    uint8_t scalar[32];
    for (int i = 0; i < 32; ++i) scalar[i] = static_cast<uint8_t>(seed_byte + i);
    return meshroute::team_channel_key_derive(pub, priv, scalar);
}

bool all_zero32(const uint8_t* p) {
    for (int i = 0; i < 32; ++i) if (p[i]) return false;
    return true;
}

// The staged PHY a `freq=869 sf=9 bw=250` tail produces.
ProvPhy phy_tail() {
    ProvPhy p{};
    p.present = true;
    p.freq_mhz = 869.0;
    p.routing_sf = 9;
    p.bw_hz = 250000;
    p.allowed_sf_bitmap = static_cast<uint16_t>(1u << 9);
    p.bw_khz = 250.0;
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// ★★ THE CONTROL FOR EVERY OTHER CASE. Drives the fakes DIRECTLY and requires each instrument to MOVE, so a counter
//    left unincremented could not make the zero-asserting cases below pass vacuously.
TEST_CASE("§PROV-TX control — every instrument counts, so a zero elsewhere is evidence") {
    FakeProvStore s;
    FakeProvLive  l;
    l.store = &s;
    FakeEntropy   e;
    CHECK(s.writes == 0);
    CHECK(s.save(prov_seed_record()));
    CHECK(s.writes == 1);
    s.write_ok = false;
    CHECK(!s.save(prov_seed_record()));
    CHECK(s.writes == 2);                       // ★ a FAILED write is still an ATTEMPT and is still counted
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x31, pub, priv));
    l.set_team(7);       CHECK(l.set_team_calls == 1);
    l.install_key(pub, priv); CHECK(l.install_calls == 1); CHECK(l.key_present);
    l.apply_phy(phy_tail()); CHECK(l.phy_calls == 1);
    l.fire_dad();        CHECK(l.dad_calls == 1);
    CHECK(l.total_calls() == 4);
    CHECK(l.set_team_at < l.install_at);
    CHECK(l.install_at  < l.phy_at);
    CHECK(l.phy_at      < l.dad_at);
    // and the entropy fake really distinguishes the two draws
    uint32_t n = 0; e.fill(reinterpret_cast<uint8_t*>(&n), sizeof n);
    uint8_t sc[32]; e.fill(sc, sizeof sc);
    CHECK(e.nonce_draws == 1);
    CHECK(e.key_draws == 1);
    CHECK(n != 0);
    CHECK(!all_zero32(sc));
}

// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("§PROV-TX `team new` — one write, then the live apply in order, and the candidate is COMPLETE") {
    PFix f;
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.err == ProvErr::none);
    CHECK(res.team_id != 0);
    CHECK(res.membership_changed);
    CHECK(res.key_action == KeyAction::install);
    CHECK(res.key_minted);
    CHECK(res.dad_fired);
    CHECK(f.store.writes == 1);                       // ★ EXACTLY ONE
    // the CANDIDATE — what a reboot will restore
    CHECK(f.store.rec.team_id == res.team_id);
    CHECK(f.store.rec.is_mobile == 1);
    CHECK(f.store.rec.team_local_id == 0);            // ★ 0 = DAD PENDING (the documented sentinel)
    CHECK(f.store.rec.node_id == 42);                 // untouched: a moved id converges via `join_changed`
    CHECK(f.store.rec.team_ch_key_present == 1);
    CHECK(!all_zero32(f.store.rec.team_ch_pub));
    CHECK(!all_zero32(f.store.rec.team_ch_priv));     // ⛔ the persisted key is NOT wiped — see the wipe case below
    CHECK(f.store.rec.channel_ctr == 7);              // a non-provisioning field carried through
    CHECK(f.store.rec.admin_counter_floor == 99);
    // the LIVE apply
    CHECK(f.live.set_team_calls == 1);
    CHECK(f.live.install_calls == 1);
    CHECK(f.live.phy_calls == 0);                     // no PHY tail was given
    CHECK(f.live.dad_calls == 1);
    CHECK(f.live.team_id == res.team_id);
    CHECK(f.live.key_present);
    // ORDER: set_team -> install_key -> ... -> DAD LAST
    CHECK(f.live.set_team_at < f.live.install_at);
    CHECK(f.live.dad_at == f.live.seq);
    // and the persisted pair IS the installed pair, byte for byte (what is saved is what is installed)
    CHECK(memcmp(f.store.rec.team_ch_pub,  f.live.pub,  32) == 0);
    CHECK(memcmp(f.store.rec.team_ch_priv, f.live.priv, 32) == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ★★★ THE LOAD-BEARING CASE OF THE WHOLE SLICE.
TEST_CASE("§PROV-TX save FAILURE — zero live calls, zero airtime, EXACTLY ONE save attempt") {
    PFix f;
    const mrnv::Blob before = f.store.rec;
    f.store.write_ok = false;
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::nv_failed);
    CHECK(res.err == ProvErr::nv_save_failed);
    CHECK(f.store.writes == 1);                       // ★ exactly ONE attempt — never zero, never two
    CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);   // the fake record did not move
    // ★★ ZERO CALLS ON EVERY `IProvLive` METHOD…
    CHECK(f.live.set_team_calls == 0);
    CHECK(f.live.install_calls == 0);
    CHECK(f.live.phy_calls == 0);
    CHECK(f.live.total_calls() == 0);
    // …AND THE AIRTIME CLAUSE AS ITS OWN COUNT (no ordering test would catch this one)
    CHECK(f.live.dad_calls == 0);
    CHECK(!res.dad_fired);
    // and the live sink genuinely did not move
    CHECK(f.live.team_id == 0);
    CHECK(!f.live.key_present);

    // ★ THE POSITIVE ARM, in the same case: the IDENTICAL request with the failure removed moves every counter, so the
    //   zeros above are evidence and not an instrument that never fires.
    PFix g;
    TeamRequest r2 = g.mint();
    const mrfw::ProvResult ok = g.svc.apply_team(r2, g.cfg, g.snap);
    CHECK(ok.verdict == ProvVerdict::applied);
    CHECK(g.store.writes == 1);
    CHECK(g.live.set_team_calls == 1);
    CHECK(g.live.install_calls == 1);
    CHECK(g.live.dad_calls == 1);
}

// ---------------------------------------------------------------------------------------------------------------
// ★ THE KEY ORDERING. `set_team_id` CLEARS the key, so installing first would persist a key the node no longer holds.
//   The fake models the clear ⇒ SWAPPING THE TWO IN THE SOURCE MAKES THIS CASE RED (`key_present` false, and the live
//   pair all-zero) rather than merely reordering two ticks.
TEST_CASE("§PROV-TX key ordering — set_team runs BEFORE install_key, and the key SURVIVES") {
    PFix f;
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x51, pub, priv));
    TeamRequest r = f.join(0x1234u);
    r.key_supplied = true;
    memcpy(r.key_pub, pub, 32);
    memcpy(r.key_priv, priv, 32);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.key_action == KeyAction::install);
    CHECK(!res.key_minted);                            // ADOPTED, not MINTED
    CHECK(f.live.set_team_calls == 1);
    CHECK(f.live.install_calls == 1);
    CHECK(f.live.set_team_at < f.live.install_at);      // the ORDER
    CHECK(f.live.key_present);                          // ★ and the key is STILL THERE afterwards — the swap's tell
    CHECK(!all_zero32(f.live.priv));
    CHECK(memcmp(f.live.pub, pub, 32) == 0);
    CHECK(memcmp(f.live.priv, priv, 32) == 0);
    // ★★ AND BOTH LIVE CALLS SAW THE NEW RECORD ALREADY PERSISTED — i.e. the write really preceded them.
    CHECK(f.live.persisted_team_at_set_team == 0x1234u);
    CHECK(f.live.persisted_team_at_install  == 0x1234u);
}

// ---------------------------------------------------------------------------------------------------------------
// ★ THE CROSS-CHECK THE SYNTAX PARSER CANNOT DO. ⚠ Driven by a GENUINELY MISMATCHED-BUT-WELL-FORMED pair (two real
//   keypairs, pub from one and priv from the other) — a syntax-only check would pass this vacuously.
TEST_CASE("§PROV-TX a mismatched but well-formed keypair is refused BEFORE the save") {
    PFix f;
    uint8_t pub_a[32], priv_a[32], pub_b[32], priv_b[32];
    CHECK(make_pair(0x11, pub_a, priv_a));
    CHECK(make_pair(0x77, pub_b, priv_b));
    CHECK(memcmp(pub_a, pub_b, 32) != 0);              // the pairs really are different (non-vacuity)

    TeamRequest bad = f.join(0x2222u);
    bad.key_supplied = true;
    memcpy(bad.key_pub,  pub_b,  32);                  // B's public half…
    memcpy(bad.key_priv, priv_a, 32);                  // …with A's private half
    const mrfw::ProvResult res = f.svc.apply_team(bad, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::key_mismatch);
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
    CHECK(f.live.dad_calls == 0);

    // ★ THE POSITIVE ARM: the MATCHING pair applies, so the refusal above is specific and not a blanket rejection.
    PFix g;
    TeamRequest good = g.join(0x2222u);
    good.key_supplied = true;
    memcpy(good.key_pub,  pub_a,  32);
    memcpy(good.key_priv, priv_a, 32);
    const mrfw::ProvResult ok = g.svc.apply_team(good, g.cfg, g.snap);
    CHECK(ok.verdict == ProvVerdict::applied);
    CHECK(ok.key_action == KeyAction::install);
    CHECK(g.store.writes == 1);
}

TEST_CASE("§PROV-TX an all-zero private half is refused before the save (degenerate scalar)") {
    PFix f;
    TeamRequest r = f.join(0x3333u);
    r.key_supplied = true;                             // both halves all-zero: syntactically fine, cryptographically dead
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::key_degenerate);
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
}

TEST_CASE("§PROV-TX a DEAD CSPRNG refuses the mint before the save") {
    PFix f;
    f.ent.key_all_zero = true;                         // the 32-byte scalar draw returns zeros
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::keygen_failed);
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
    CHECK(f.live.dad_calls == 0);
    CHECK(f.ent.key_draws == 1);                       // it really was asked (the instrument fired)
}

// ---------------------------------------------------------------------------------------------------------------
// ★★ THE `KeyAction` MATRIX (§3.6.1), all six rows — and the CANDIDATE's key state is asserted alongside the LIVE
//    one in every row, which is what "the candidate and the live apply use the SAME KeyAction" means in practice.
TEST_CASE("§PROV-TX KeyAction matrix — same-team PHY-only PRESERVES with zero key calls") {
    PFix f;
    f.cfg.team_id = 0x4444u;                           // already in this team, and holding a key
    f.store.rec.team_id = 0x4444u;
    f.store.rec.team_ch_key_present = 1;
    f.store.rec.team_ch_pub[0]  = 0xC3;
    f.store.rec.team_ch_priv[0] = 0x7E;
    TeamRequest r = f.join(0x4444u);
    r.phy = phy_tail();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.key_action == KeyAction::preserve);
    CHECK(!res.membership_changed);
    CHECK(f.store.writes == 1);
    // ⛔ ZERO KEY CALLS — the v2 defect this row exists for
    CHECK(f.live.install_calls == 0);
    CHECK(f.live.set_team_calls == 0);                 // membership did not change -> no switch either
    CHECK(f.live.phy_calls == 1);
    CHECK(f.live.dad_calls == 0);                      // ⛔ a same-team PHY update must not spend airtime
    // the candidate's key bytes are LEFT EXACTLY AS LOADED — that IS the preservation
    CHECK(f.store.rec.team_ch_key_present == 1);
    CHECK(f.store.rec.team_ch_pub[0] == 0xC3);
    CHECK(f.store.rec.team_ch_priv[0] == 0x7E);
    // and the PHY really moved, in both the candidate and the live sink
    CHECK(f.store.rec.freq_mhz == 869.0);
    CHECK(f.store.rec.routing_sf == 9);
    CHECK(f.store.rec.bw_hz == 250000);
    CHECK(f.store.rec.allowed_sf_bitmap == static_cast<uint16_t>(1u << 9));
    CHECK(f.live.phy.routing_sf == 9);
}

TEST_CASE("§PROV-TX KeyAction matrix — same-team RE-KEY installs, without a switch and without DAD") {
    PFix f;
    f.cfg.team_id = 0x4444u;
    f.store.rec.team_id = 0x4444u;
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x21, pub, priv));
    TeamRequest r = f.join(0x4444u);
    r.key_supplied = true;
    memcpy(r.key_pub, pub, 32);
    memcpy(r.key_priv, priv, 32);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.key_action == KeyAction::install);
    CHECK(!res.membership_changed);
    CHECK(f.store.writes == 1);
    CHECK(f.live.set_team_calls == 0);                  // ⛔ no switch: nothing is stale
    CHECK(f.live.install_calls == 1);
    CHECK(f.live.dad_calls == 0);                       // ⛔ a re-key must NOT re-DAD
    CHECK(f.store.rec.team_ch_key_present == 1);
    CHECK(memcmp(f.store.rec.team_ch_pub, pub, 32) == 0);
    CHECK(f.live.key_present);
}

TEST_CASE("§PROV-TX KeyAction matrix — a BARE join to another team CLEARS via set_team") {
    PFix f;
    f.cfg.team_id = 0x1111u;
    f.store.rec.team_id = 0x1111u;
    f.store.rec.team_ch_key_present = 1;
    f.store.rec.team_ch_pub[0]  = 0xC3;
    f.store.rec.team_ch_priv[0] = 0x7E;
    f.live.key_present = true;                          // the live node holds the OLD team's key
    TeamRequest r = f.join(0x5555u);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.key_action == KeyAction::clear);
    CHECK(res.membership_changed);
    CHECK(f.store.writes == 1);
    CHECK(f.live.set_team_calls == 1);
    CHECK(f.live.install_calls == 0);                   // ⛔ a joiner generates NOTHING — it RECEIVES a key later
    CHECK(f.live.dad_calls == 1);
    CHECK(!f.live.key_present);                         // cleared, by `set_team` alone
    CHECK(f.store.rec.team_ch_key_present == 0);        // and the candidate agrees with the live apply
    CHECK(all_zero32(f.store.rec.team_ch_pub));
    CHECK(all_zero32(f.store.rec.team_ch_priv));
}

TEST_CASE("§PROV-TX KeyAction matrix — LEAVE clears the key, PRESERVES the PHY and does NOT DAD") {
    PFix f;
    f.cfg.team_id = 0x1111u;
    f.store.rec.team_id = 0x1111u;
    f.store.rec.team_local_id = 9;
    f.store.rec.team_ch_key_present = 1;
    f.store.rec.team_ch_pub[0] = 0xC3;
    f.live.key_present = true;
    const double   freq_before = f.store.rec.freq_mhz;
    const uint32_t bw_before   = f.store.rec.bw_hz;
    const uint8_t  sf_before   = f.store.rec.routing_sf;
    TeamRequest r = f.leave();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.team_id == 0);
    CHECK(res.key_action == KeyAction::clear);
    CHECK(res.membership_changed);
    CHECK(f.store.writes == 1);
    CHECK(f.live.set_team_calls == 1);
    CHECK(f.live.install_calls == 0);
    CHECK(f.live.phy_calls == 0);
    CHECK(f.live.dad_calls == 0);                       // ⛔ leaving never DADs
    CHECK(f.store.rec.team_local_id == 0);
    CHECK(f.store.rec.team_ch_key_present == 0);
    // ★★ THE v4 RULING'S FIRST HALF: leaving a team PRESERVES the PHY — never resets, clears or re-derives it.
    CHECK(f.store.rec.freq_mhz == freq_before);
    CHECK(f.store.rec.bw_hz == bw_before);
    CHECK(f.store.rec.routing_sf == sf_before);
    // R3: leaving does NOT demote — a mobile with no team is a legitimate configuration
    CHECK(f.store.rec.is_mobile == 1);
}

TEST_CASE("§PROV-TX KeyAction matrix — same-team, NOTHING changed: zero saves, zero applies, no_change") {
    PFix f;
    f.cfg.team_id = 0x4444u;
    f.store.rec.team_id = 0x4444u;
    TeamRequest r = f.join(0x4444u);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::no_change);       // ★ a HARD requirement, not an optimisation
    CHECK(res.key_action == KeyAction::preserve);
    CHECK(f.store.writes == 0);                          // ⛔ ZERO saves
    CHECK(f.store.loads == 1);                           // it did read the record (the instrument fired)
    CHECK(f.live.total_calls() == 0);                    // ⛔ ZERO applies
    CHECK(f.live.dad_calls == 0);
    // ⓘ `mr_ui_on_config_saved()` is gated on `verdict == applied` in the console, so this verdict is exactly what
    //   keeps an OLED draft from being told the record moved when it did not.

    // ★ AND `team 0` ON A TEAMLESS NODE IS THE SAME NON-EVENT (and must NOT be refused by O1 — the verb is a
    //   ONE-WAY role gate: R3 says leaving never demotes, so passing "wants static" would trap every member).
    PFix g;
    TeamRequest l = g.leave();
    const mrfw::ProvResult r2 = g.svc.apply_team(l, g.cfg, g.snap);
    CHECK(r2.verdict == ProvVerdict::no_change);
    CHECK(r2.err == ProvErr::none);
    CHECK(g.store.writes == 0);
    CHECK(g.live.total_calls() == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ★★ THE SAME-TEAM MATRIX'S PRESERVATION CLAUSE — the v2 defect: an unconditional `team_local_id = 0` would force a
//    needless team-DAD after reboot and DISCARD a stable, defended local id.
TEST_CASE("§PROV-TX same-team requests PRESERVE team_local_id and node_id; a changed one zeroes only the former") {
    // (a) re-key only  (b) PHY only  (c) both  — each: preserved, and NO DAD
    for (int variant = 0; variant < 3; ++variant) {
        PFix f;
        f.cfg.team_id = 0x4444u;
        f.store.rec.team_id = 0x4444u;
        f.store.rec.team_local_id = 9;
        f.store.rec.node_id = 42;
        TeamRequest r = f.join(0x4444u);
        if (variant == 0 || variant == 2) {
            uint8_t pub[32], priv[32];
            CHECK(make_pair(static_cast<uint8_t>(0x30 + variant), pub, priv));
            r.key_supplied = true;
            memcpy(r.key_pub, pub, 32);
            memcpy(r.key_priv, priv, 32);
        }
        if (variant == 1 || variant == 2) r.phy = phy_tail();
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::applied);
        CHECK(!res.membership_changed);
        CHECK(f.store.writes == 1);
        CHECK(f.store.rec.team_local_id == 9);           // ★ PRESERVED
        CHECK(f.store.rec.node_id == 42);                // ★ PRESERVED
        CHECK(res.persisted_team_local_id == 9);         // ★ and the REPORT agrees — it is not hardcoded 0
        CHECK(f.live.dad_calls == 0);                    // ⛔ no airtime for a same-team update
        CHECK(f.live.set_team_calls == 0);
    }
    // (d) membership CHANGED -> team_local_id 0, node_id still preserved, DAD once and LAST
    PFix g;
    g.cfg.team_id = 0x1111u;
    g.store.rec.team_id = 0x1111u;
    g.store.rec.team_local_id = 9;
    g.store.rec.node_id = 42;
    TeamRequest r = g.join(0x9999u);
    const mrfw::ProvResult res = g.svc.apply_team(r, g.cfg, g.snap);
    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.membership_changed);
    CHECK(g.store.rec.team_local_id == 0);
    CHECK(res.persisted_team_local_id == 0);             // ★ 0 = DAD pending, and the caller is TOLD so (defect ②)
    CHECK(g.store.rec.node_id == 42);
    CHECK(g.live.dad_calls == 1);
    CHECK(g.live.dad_at == g.live.seq);                  // ★ LAST
}

// ---------------------------------------------------------------------------------------------------------------
// ★ §1.2.1 — THE PHY TAIL UNDER THE **PROJECTED** ROLE. On a STATIC node the old code silently discarded the PHY
//   arguments because it gated on `c.is_mobile` BEFORE the promotion.
TEST_CASE("§PROV-TX a STATIC node's `team new freq=…` has its PHY HONOURED, and the promotion is reported") {
    PFix f;
    f.cfg.is_mobile = false;                             // STATIC — the case the old gate dropped
    f.store.rec.is_mobile = 0;                           // ⚠ the record AGREES with the live role, so the flip asserted below is a MEASUREMENT
    f.cfg.team_id   = 0;
    TeamRequest r = f.mint();
    r.phy = phy_tail();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.phy.present);
    CHECK(res.role_promoted);                            // RoleFix::forced_mobile, read off the REAL role_enforce
    CHECK(f.live.phy_calls == 1);                        // ⛔ NOT silently dropped
    CHECK(f.live.phy.freq_mhz == 869.0);
    CHECK(f.store.rec.freq_mhz == 869.0);                // and it is PERSISTED
    CHECK(f.store.rec.routing_sf == 9);
    CHECK(f.store.rec.is_mobile == 1);                   // the projected role is persisted too
    CHECK(f.live.dad_calls == 1);                        // a promoted node gets the same team-plane bootstrap
    // ★ AND THE PHY IS APPLIED **AFTER** THE KEY, **BEFORE** THE DAD (the §3.2 order, end to end)
    CHECK(f.live.set_team_at < f.live.install_at);
    CHECK(f.live.install_at  < f.live.phy_at);
    CHECK(f.live.phy_at      < f.live.dad_at);
}

// ---------------------------------------------------------------------------------------------------------------
// ★★ THE v4 RULING'S SECOND HALF: `team 0` WITH ANY PHY ARGUMENT IS REFUSED LOUDLY, BEFORE THE SAVE.
// ⚠ HONEST SCOPE: at THIS boundary "any PHY argument" is `ProvPhy::present`, so the four shapes below vary WHICH
//   fields the staged triplet carries. A `sf=`-alone or `bw=`-alone tail never reaches here at all — the pure
//   `parse_phy_tail` refuses it one layer up with *"PHY args need freq="* (pre-existing behaviour, unchanged) — so it
//   is stated rather than simulated with a request the console cannot build.
TEST_CASE("§PROV-TX `team 0` with ANY PHY argument is REFUSED — zero saves, zero applies, zero airtime") {
    for (int shape = 0; shape < 4; ++shape) {
        PFix f;
        f.cfg.team_id = 0x1111u;                         // in a team, so the leave would otherwise be real work
        f.store.rec.team_id = 0x1111u;
        const mrnv::Blob before = f.store.rec;
        TeamRequest r = f.leave();
        r.phy.present = true;
        if (shape == 0) { r.phy.freq_mhz = 869.0; }
        if (shape == 1) { r.phy.routing_sf = 9; }
        if (shape == 2) { r.phy.bw_hz = 250000; }
        if (shape == 3) { r.phy = phy_tail(); }
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::refused);
        CHECK(res.err == ProvErr::phy_on_leave);
        CHECK(f.store.writes == 0);                       // ⛔ zero saves
        CHECK(f.live.total_calls() == 0);                 // ⛔ zero IProvLive calls
        CHECK(f.live.dad_calls == 0);                     // ⛔ zero airtime
        CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);   // the PHY (and everything else) is UNCHANGED
    }
    // ★ THE POSITIVE ARM: the SAME leave WITHOUT a PHY tail is honoured, so the refusal is about the argument and not
    //   about `team 0`. ⚠ A control that ACCEPTS the tail (v3's withdrawn behaviour) makes the four assertions above
    //   RED, which is what stops that behaviour creeping back.
    PFix g;
    g.cfg.team_id = 0x1111u;
    g.store.rec.team_id = 0x1111u;
    TeamRequest l = g.leave();
    const mrfw::ProvResult ok = g.svc.apply_team(l, g.cfg, g.snap);
    CHECK(ok.verdict == ProvVerdict::applied);
    CHECK(g.store.writes == 1);
    CHECK(g.live.set_team_calls == 1);
}

TEST_CASE("§PROV-TX `team 0` with a key tail is refused before the save") {
    PFix f;
    f.cfg.team_id = 0x1111u;
    f.store.rec.team_id = 0x1111u;
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x41, pub, priv));
    TeamRequest r = f.leave();
    r.key_supplied = true;
    memcpy(r.key_pub, pub, 32);
    memcpy(r.key_priv, priv, 32);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::key_on_leave);
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ★ §3.5 — `team new` MUST NOT REGENERATE THE CURRENT TEAM ID (that would silently make it a same-team RE-KEY).
//   ⓘ This case is only reachable BECAUSE the entropy is injectable; it is unproducible against a live CSPRNG, which
//     is the concrete reason the seam earns its place.
TEST_CASE("§PROV-TX `team new` resamples when the draw reproduces the CURRENT team id") {
    PFix f;
    const uint32_t collide = mrfw::team_fnv1a32(f.snap.key_hash32, 1u);
    f.cfg.team_id       = collide;                       // we are ALREADY in the team the first draw would produce
    f.store.rec.team_id = collide;
    f.ent.nonce_seq[0] = 1u;                             // -> the current id  => must be REJECTED
    f.ent.nonce_seq[1] = 2u;                             // -> a different id  => accepted
    f.ent.nonce_n = 2;
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(f.ent.nonce_draws == 2);                        // ★ it RESAMPLED
    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.team_id == mrfw::team_fnv1a32(f.snap.key_hash32, 2u));
    CHECK(res.team_id != collide);                        // ⛔ NOT a same-team re-key
    CHECK(res.membership_changed);
    CHECK(f.store.rec.team_local_id == 0);                // it really is a NEW team: DAD pending
    CHECK(f.live.set_team_calls == 1);
    CHECK(f.live.dad_calls == 1);
}

// ★★ AND THE ZERO ARM, WITH A REAL PREIMAGE. `team_fnv1a32` has no zero guard, and a 0 would have made `team new`
//    execute `team 0` = LEAVE. ⓘ `team_fnv1a32(5, 0x0B673991) == 0` was found by exhaustive search over the FNV-1a
//    state (the value is verified by the first CHECK, so a changed hash cannot leave this case passing vacuously).
// ⛔⛔ THE CURRENT TEAM IS DELIBERATELY NON-ZERO HERE, AND THAT IS A CORRECTION OF THIS CASE, NOT A DETAIL. The first
//    version left it at 0, so the guard's OTHER clause (`t != live.team_id`) rejected the zero draw as well — and a
//    mutation that DELETED the `t != 0` clause stayed GREEN. Measured, not reasoned: mutation M6 (`t != live.team_id`
//    alone) came out green against that version and is RED against this one. With a non-zero current team the zero
//    exclusion is the ONLY clause that can reject draw #1, which is what makes this case a measurement of it.
TEST_CASE("§PROV-TX `team new` resamples when the draw hashes to ZERO — it never becomes a LEAVE") {
    PFix f;
    f.snap.key_hash32 = 5u;
    CHECK(mrfw::team_fnv1a32(5u, 0x0B673991u) == 0u);     // the preimage is real, not assumed
    f.cfg.team_id       = 0x1234u;                        // ★ non-zero, so ONLY the `t != 0` clause can reject draw #1
    f.store.rec.team_id = 0x1234u;
    CHECK(mrfw::team_fnv1a32(5u, 7u) != 0x1234u);         // …and draw #2 is not the current team either (non-vacuity)
    f.ent.nonce_seq[0] = 0x0B673991u;                     // -> 0  => must be REJECTED
    f.ent.nonce_seq[1] = 7u;                              // -> non-zero => accepted
    f.ent.nonce_n = 2;
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(f.ent.nonce_draws == 2);
    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.team_id != 0);                              // ⛔ NOT a leave
    CHECK(res.team_id == mrfw::team_fnv1a32(5u, 7u));
    CHECK(f.store.rec.team_id != 0);
    CHECK(f.live.dad_calls == 1);                         // and it really joined a team
}

TEST_CASE("§PROV-TX `team new` refuses LOUDLY when no acceptable id can be drawn") {
    PFix f;
    const uint32_t collide = mrfw::team_fnv1a32(f.snap.key_hash32, 1u);
    f.cfg.team_id       = collide;
    f.store.rec.team_id = collide;
    f.ent.nonce_seq[0]    = 1u;
    f.ent.nonce_n         = 1;
    f.ent.nonce_stick_last = true;                        // EVERY draw reproduces the current id
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::id_unavailable);
    CHECK(f.ent.nonce_draws == mrfw::kTeamIdMintTries);   // bounded, and it really tried
    CHECK(f.ent.key_draws == 0);                          // ⛔ no key material was drawn on a refusal
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
    CHECK(f.live.dad_calls == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ★★ §3.10 — THE WIPE, AND ITS **LIMIT**. The transient request buffers are zeroed on BOTH exits; ⛔ the PERSISTED
//    candidate's key and the INSTALLED live key are NOT, because the team key MUST survive in NV and in the node or
//    the node cannot read team traffic. An over-eager wipe here would silently destroy the team.
TEST_CASE("§PROV-TX staged private material is wiped on BOTH exits — but never the persisted or live key") {
    // (a) SUCCESS
    PFix f;
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x61, pub, priv));
    TeamRequest r = f.join(0x7777u);
    r.key_supplied = true;
    memcpy(r.key_pub, pub, 32);
    memcpy(r.key_priv, priv, 32);
    CHECK(!all_zero32(r.key_priv));                       // it really held a secret going in
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(all_zero32(r.key_priv));                        // ★ wiped
    CHECK(all_zero32(r.key_pub));
    // ⛔ AND THE TWO THINGS THAT MUST SURVIVE:
    CHECK(f.store.rec.team_ch_key_present == 1);
    CHECK(!all_zero32(f.store.rec.team_ch_priv));         // the store received a KEY-BEARING blob
    CHECK(memcmp(f.store.rec.team_ch_priv, priv, 32) == 0);
    CHECK(f.live.key_present);                            // and the live node holds it
    CHECK(!all_zero32(f.live.priv));

    // (b) FAILURE (a refusal before the save)
    PFix g;
    TeamRequest bad = g.leave();                          // `team 0` + a key = refused
    bad.key_supplied = true;
    memcpy(bad.key_pub, pub, 32);
    memcpy(bad.key_priv, priv, 32);
    const mrfw::ProvResult r2 = g.svc.apply_team(bad, g.cfg, g.snap);
    CHECK(r2.verdict == ProvVerdict::refused);
    CHECK(all_zero32(bad.key_priv));                      // ★ wiped on the failure arm too
    CHECK(all_zero32(bad.key_pub));

    // (c) FAILURE (the save itself)
    PFix h;
    h.store.write_ok = false;
    TeamRequest nv = h.join(0x8888u);
    nv.key_supplied = true;
    memcpy(nv.key_pub, pub, 32);
    memcpy(nv.key_priv, priv, 32);
    const mrfw::ProvResult r3 = h.svc.apply_team(nv, h.cfg, h.snap);
    CHECK(r3.verdict == ProvVerdict::nv_failed);
    CHECK(all_zero32(nv.key_priv));
    CHECK(h.live.total_calls() == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ★ THE ROLE TRUTH TABLE, reached through the transaction (U1 — the DECISION is `role_set_refusal`, unchanged).
TEST_CASE("§PROV-TX the O2/R4 role refusals stop the transaction before any save") {
    // R4: a GATEWAY is two-layer infrastructure and can never be reached THROUGH someone else
    {
        PFix f;
        f.cfg.is_mobile = false;
        f.store.rec.is_mobile = 0;                        // ⚠ record AGREES with the live role (see prov_seed_record)
        f.cfg.is_gateway = true;
        TeamRequest r = f.mint();
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::refused);
        CHECK(res.err == ProvErr::role_refused);
        CHECK(res.role_refusal == meshroute::RoleSetRefusal::gateway_static);
        CHECK(f.store.writes == 0);
        CHECK(f.live.total_calls() == 0);
        CHECK(f.ent.nonce_draws == 0);                    // ⛔ refused BEFORE any entropy was drawn
    }
    // O2: promoting a HOST orphans its guests (they lose their home and reverse-ack path, unnotified)
    {
        PFix f;
        f.cfg.is_mobile = false;
        f.store.rec.is_mobile = 0;                        // ⚠ record AGREES with the live role (see prov_seed_record)
        f.snap.mobile_reg_count = 3;
        TeamRequest r = f.join(0xABCDu);
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::refused);
        CHECK(res.role_refusal == meshroute::RoleSetRefusal::hosting_mobiles);
        CHECK(f.store.writes == 0);
        CHECK(f.live.total_calls() == 0);
    }
    // ★ THE POSITIVE ARM: the same static node with NO guests and no gateway flag is PROMOTED, not refused.
    {
        PFix f;
        f.cfg.is_mobile = false;
        f.store.rec.is_mobile = 0;                        // ⚠ record AGREES with the live role (see prov_seed_record)
        TeamRequest r = f.join(0xABCDu);
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::applied);
        CHECK(res.role_promoted);
        CHECK(f.store.rec.is_mobile == 1);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// ★ THE INCOMPLETE-PHY REFUSAL, EVALUATED AGAINST THE **STAGED CANDIDATE** — never against live state.
TEST_CASE("§PROV-TX an incomplete STAGED PHY refuses; the build floor resolves a zero in the record") {
    // (a) the record has no DATA-SF set at all -> refuse
    {
        PFix f;
        f.store.rec.allowed_sf_bitmap = 0;
        TeamRequest r = f.join(0xBEEFu);
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::refused);
        CHECK(res.err == ProvErr::incomplete_phy);
        CHECK(f.store.writes == 0);
        CHECK(f.live.total_calls() == 0);
    }
    // (b) an out-of-domain routing_sf in the record -> refuse
    {
        PFix f;
        f.store.rec.routing_sf = 4;
        TeamRequest r = f.join(0xBEEFu);
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::refused);
        CHECK(res.err == ProvErr::incomplete_phy);
    }
    // (c) ★ THE STAGED TAIL REPAIRS IT: the same broken record plus a complete PHY tail is ACCEPTED, which is what
    //     "evaluate the CANDIDATE" means — a live-state check could not see the repair.
    {
        PFix f;
        f.store.rec.allowed_sf_bitmap = 0;
        f.store.rec.routing_sf = 4;
        TeamRequest r = f.join(0xBEEFu);
        r.phy = phy_tail();
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::applied);
        CHECK(f.store.writes == 1);
        CHECK(f.store.rec.allowed_sf_bitmap == static_cast<uint16_t>(1u << 9));
    }
    // (d) a ZERO freq/bw in the record is resolved by the BUILD FLOOR, not refused
    {
        PFix f;
        f.store.rec.freq_mhz = 0.0;
        f.store.rec.bw_hz    = 0;
        TeamRequest r = f.join(0xBEEFu);
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::applied);
        // …and with NO floor supplied the same request refuses, so the floor is doing the work (non-vacuity)
        PFix g;
        g.store.rec.freq_mhz = 0.0;
        g.store.rec.bw_hz    = 0;
        TeamRequest r2 = g.join(0xBEEFu);
        r2.floor = mrfw::ProvPhyFloor{};
        const mrfw::ProvResult res2 = g.svc.apply_team(r2, g.cfg, g.snap);
        CHECK(res2.verdict == ProvVerdict::refused);
        CHECK(res2.err == ProvErr::incomplete_phy);
    }
    // (e) LEAVE is exempt — it never asks about the PHY at all
    {
        PFix f;
        f.cfg.team_id = 0x1111u;
        f.store.rec.team_id = 0x1111u;
        f.store.rec.allowed_sf_bitmap = 0;
        TeamRequest r = f.leave();
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
        CHECK(res.verdict == ProvVerdict::applied);
    }
}

// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("§PROV-TX an unreadable record refuses without a write (the non-provisioning fields cannot be preserved)") {
    PFix f;
    f.store.have = false;
    TeamRequest r = f.mint();
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::nv_load_failed);
    CHECK(f.store.loads == 1);
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
    CHECK(f.live.dad_calls == 0);
    // ⚠ CHANGED, AND SAID PLAINLY RATHER THAN QUIETLY RELAXED: this used to assert `nonce_draws == 0`. The role/id
    //   PROJECTION now runs BEFORE the record is loaded, because its ~256-B `NodeConfig` copy must not coexist with the
    //   `mrnv::Blob` the load needs (QG defect ③) — and the `mrnv::Blob` cannot exist before the load either. So the
    //   4-byte team-id nonce IS drawn on this arm. ⓘ What that costs: four bytes of CSPRNG on a doomed request.
    //   ⛔ What it does NOT cost — and this is the clause that actually matters — is ANY write, ANY live apply or ANY
    //   airtime, all asserted above. ★ The ROLE refusal still precedes every draw (see the O2/R4 case's
    //   `nonce_draws == 0`), so "refuse before spending entropy" is retained exactly where it is a policy decision.
    CHECK(f.ent.nonce_draws == 1);
    CHECK(f.ent.key_draws == 0);                          // ⛔ and NO key material was drawn
}

// ---------------------------------------------------------------------------------------------------------------
// ★★★ QG DEFECT ① — `no_change` MUST COME FROM THE CANDIDATE'S ACTUAL DIFFERENCES, NOT FROM THE REQUEST'S SHAPE.
//     The withdrawn rule was `!membership_changed && key_action == preserve && !phy.present`, and each of the three
//     cases below is one of its three measurable consequences. ⛔ Every one carries its control IN THE SAME CASE: the
//     nearly-identical request that DOES differ, so a `no_change` here is a statement about VALUES and can never be
//     "the transaction ignores this argument".
TEST_CASE("§PROV-TX ① a re-supplied IDENTICAL PHY is no_change — zero saves, zero retune") {
    PFix f;
    f.cfg.team_id       = 0x4444u;
    f.store.rec.team_id = 0x4444u;
    TeamRequest r = f.join(0x4444u);
    r.phy.present           = true;                       // the tail names EXACTLY what the record already holds
    r.phy.freq_mhz          = f.store.rec.freq_mhz;
    r.phy.routing_sf        = f.store.rec.routing_sf;
    r.phy.bw_hz             = f.store.rec.bw_hz;
    r.phy.allowed_sf_bitmap = f.store.rec.allowed_sf_bitmap;
    // ★ AND THE LIVE RADIO IS ON THAT SAME PHY (the fixture's converged default, asserted here so this case cannot
    //   pass because the live reading happened to be ignored — see the QG-round-3 cases for the divergent arms).
    CHECK(f.snap.live_freq_mhz          == r.phy.freq_mhz);
    CHECK(f.snap.live_bw_hz             == r.phy.bw_hz);
    CHECK(f.snap.live_routing_sf        == r.phy.routing_sf);
    CHECK(f.snap.live_allowed_sf_bitmap == r.phy.allowed_sf_bitmap);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::no_change);          // ⛔ the old rule said `applied` here (phy.present)
    CHECK(f.store.writes == 0);                            // ⛔ ZERO saves
    CHECK(f.live.phy_calls == 0);                          // ⛔ and NO live retune of the PHY it is already on
    CHECK(f.live.total_calls() == 0);
    CHECK(f.store.loads == 1);                             // it really did read the record (the instrument fired)

    // ★★ FOUR CONTROLS, ONE PER TRACKED FIELD: change exactly one value of the same tail and it must be APPLIED. A
    //    tracker that compared only some of the four would leave one of these GREEN-as-no_change, i.e. RED here.
    for (int which = 0; which < 4; ++which) {
        PFix g;
        g.cfg.team_id       = 0x4444u;
        g.store.rec.team_id = 0x4444u;
        TeamRequest q = g.join(0x4444u);
        q.phy.present           = true;
        q.phy.freq_mhz          = g.store.rec.freq_mhz;
        q.phy.routing_sf        = g.store.rec.routing_sf;
        q.phy.bw_hz             = g.store.rec.bw_hz;
        q.phy.allowed_sf_bitmap = g.store.rec.allowed_sf_bitmap;
        if (which == 0) q.phy.freq_mhz          = g.store.rec.freq_mhz + 0.5;
        if (which == 1) q.phy.routing_sf        = 9;
        if (which == 2) q.phy.bw_hz             = 250000;
        if (which == 3) q.phy.allowed_sf_bitmap = static_cast<uint16_t>(1u << 9);
        const mrfw::ProvResult ok = g.svc.apply_team(q, g.cfg, g.snap);
        CHECK(ok.verdict == ProvVerdict::applied);
        CHECK(g.store.writes == 1);
        CHECK(g.live.phy_calls == 1);
        CHECK(g.live.dad_calls == 0);                      // still a same-team update: no airtime
    }
}

TEST_CASE("§PROV-TX ① an IDENTICAL re-supplied key is no_change — it is NOT reinstalled") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x83, pub, priv));
    PFix f;
    f.cfg.team_id       = 0x4444u;
    f.store.rec.team_id = 0x4444u;
    mrfw::blob_put_team_channel_key(f.store.rec, pub, priv);   // the record ALREADY holds this exact canonical pair
    f.set_live_key(pub, priv);                                 // ★ …AND SO DOES THE LIVE NODE (QG round 3: NV agreeing
                                                               //   is no longer enough — see the divergent arms below)
    TeamRequest r = f.join(0x4444u);
    r.key_supplied = true;
    memcpy(r.key_pub,  pub,  32);
    memcpy(r.key_priv, priv, 32);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::no_change);          // ⛔ the old rule said `applied` (key_action == install)
    CHECK(f.store.writes == 0);
    CHECK(f.live.install_calls == 0);                      // ⛔ NOT reinstalled
    CHECK(f.live.total_calls() == 0);
    CHECK(res.key_action == KeyAction::install);           // ⓘ the ACTION is still what WOULD have applied; nothing did

    // ★ CONTROL (a): a DIFFERENT well-formed pair on the same request IS installed.
    {
        uint8_t pub2[32], priv2[32];
        CHECK(make_pair(0x29, pub2, priv2));
        CHECK(memcmp(pub, pub2, 32) != 0);                 // the pairs really differ (non-vacuity)
        PFix g;
        g.cfg.team_id       = 0x4444u;
        g.store.rec.team_id = 0x4444u;
        mrfw::blob_put_team_channel_key(g.store.rec, pub, priv);
        g.set_live_key(pub2, priv2);                       // ⚠ LIVE already holds the REQUESTED pair, so the live
                                                           //   conjunct says "matches" and only the RECORD difference
                                                           //   can decide — this control stays about `differs` alone.
        TeamRequest q = g.join(0x4444u);
        q.key_supplied = true;
        memcpy(q.key_pub,  pub2,  32);
        memcpy(q.key_priv, priv2, 32);
        const mrfw::ProvResult ok = g.svc.apply_team(q, g.cfg, g.snap);
        CHECK(ok.verdict == ProvVerdict::applied);
        CHECK(g.store.writes == 1);
        CHECK(g.live.install_calls == 1);
        CHECK(memcmp(g.store.rec.team_ch_pub, pub2, 32) == 0);
    }
    // ★ CONTROL (b): the SAME pair while the record holds NO key at all is a real change — this is the clause that
    //   catches a tracker comparing only the 64 bytes and not the `present` flag (all-zero record vs an all-zero key).
    {
        PFix h;
        h.cfg.team_id       = 0x4444u;
        h.store.rec.team_id = 0x4444u;
        CHECK(h.store.rec.team_ch_key_present == 0);
        h.set_live_key(pub, priv);                         // ⚠ again: LIVE already matches the request, so the ONLY
                                                           //   difference is the record's `present` flag (the point).
        TeamRequest q = h.join(0x4444u);
        q.key_supplied = true;
        memcpy(q.key_pub,  pub,  32);
        memcpy(q.key_priv, priv, 32);
        const mrfw::ProvResult ok = h.svc.apply_team(q, h.cfg, h.snap);
        CHECK(ok.verdict == ProvVerdict::applied);
        CHECK(h.store.writes == 1);
        CHECK(h.live.install_calls == 1);
        CHECK(h.store.rec.team_ch_key_present == 1);
    }
}

// ★★★ THE DANGEROUS ONE. Same team, no key, no PHY — the EXACT shape the withdrawn rule classified `no_change` — but
//     the stored record's `is_mobile` disagrees with the projection, so a needed role fix was being DISCARDED.
TEST_CASE("§PROV-TX ① a ROLE-PROJECTION-ONLY correction is APPLIED and reaches NV, never silently discarded") {
    PFix f;
    f.cfg.team_id       = 0x4444u;                        // live: in a team ⇒ mobile by R2
    f.store.rec.team_id = 0x4444u;
    f.store.rec.is_mobile = 0;                            // ⛔ the RECORD disagrees: a reboot would come up STATIC
    TeamRequest r = f.join(0x4444u);                      // no key, no PHY, same id
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);           // ⛔ the old rule returned no_change and dropped the fix
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.is_mobile == 1);                    // ★ THE FIX REACHED NV
    CHECK(res.key_action == KeyAction::preserve);
    CHECK(!res.membership_changed);
    // …and NOTHING ELSE moved: the correction is a pure record repair
    CHECK(f.store.rec.team_local_id == 9);
    CHECK(f.store.rec.node_id == 42);
    CHECK(f.store.rec.team_id == 0x4444u);
    CHECK(f.live.total_calls() == 0);                     // ⛔ no switch, no key call, no retune, NO AIRTIME
    CHECK(f.live.dad_calls == 0);
    CHECK(res.persisted_team_local_id == 9);

    // ★ THE CONTROL: with the record ALREADY agreeing, the byte-identical request is `no_change` — so the verdict
    //   above is caused by the disagreement and not by "a bare same-team join always writes".
    PFix g;
    g.cfg.team_id       = 0x4444u;
    g.store.rec.team_id = 0x4444u;
    CHECK(g.store.rec.is_mobile == 1);                    // the seed agrees (non-vacuity of the control)
    TeamRequest q = g.join(0x4444u);
    const mrfw::ProvResult same = g.svc.apply_team(q, g.cfg, g.snap);
    CHECK(same.verdict == ProvVerdict::no_change);
    CHECK(g.store.writes == 0);
    CHECK(g.live.total_calls() == 0);
}

// ★★ AND THE OTHER CONJUNCT, which a differences-only rule would get WRONG. `no_change` is
//    `!membership_changed && !differs`: if the STORED RECORD already names the team the request asks for while the LIVE
//    node is in a different one, the candidate shows NO difference — yet the live switch and the DAD are still owed. A
//    rule reading `!differs` alone would report `no_change` and leave the node in its OLD team with a record that says
//    otherwise. ⓘ The record is given `team_local_id = 0` deliberately, so the zeroing clause cannot supply the
//    difference and this case measures the conjunct itself.
TEST_CASE("§PROV-TX ① a record that already names the target team does NOT make a real switch a no-change") {
    PFix f;
    f.cfg.team_id             = 0x1111u;                  // LIVE: in team 0x1111
    f.store.rec.team_id       = 0x2222u;                  // RECORD: already says 0x2222 (a half-applied predecessor)
    f.store.rec.team_local_id = 0;                        // …and already 0, so no clause but membership can differ
    TeamRequest r = f.join(0x2222u);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(res.membership_changed);
    CHECK(f.store.writes == 1);
    CHECK(f.live.set_team_calls == 1);                    // ★ the LIVE switch really happens
    CHECK(f.live.team_id == 0x2222u);
    CHECK(f.live.dad_calls == 1);                         // ★ and the team-DAD is spent, as a real join must
    CHECK(res.key_action == KeyAction::clear);
}

// ★★ AND ITS MIRROR, which is the ONLY shape in which the tracker's `team_id` clause can be the DECIDING one — added
//    because a mutation that deleted that clause came out GREEN against every other case (measured, not reasoned:
//    control N5). LIVE is already in the requested team, so `membership_changed` is false and cannot carry the
//    verdict; the RECORD names a different one, so a reboot would land in the WRONG team. That record must be repaired.
TEST_CASE("§PROV-TX ① a record whose team_id disagrees with the live team is REPAIRED, not called no_change") {
    PFix f;
    f.cfg.team_id       = 0x2222u;                        // LIVE: already in 0x2222
    f.store.rec.team_id = 0x1111u;                        // RECORD: stale — a reboot would rejoin 0x1111
    TeamRequest r = f.join(0x2222u);                      // no key, no PHY, and NOT a membership change
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(!res.membership_changed);                       // ⇒ the team_id DIFFERENCE is the only thing that can decide
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.team_id == 0x2222u);                // ★ the record is REPAIRED
    CHECK(f.store.rec.team_local_id == 9);                // …and the stable local id is kept (no membership change)
    CHECK(f.live.total_calls() == 0);                     // ⛔ nothing live is owed: no switch, no key, no PHY, no DAD
}

// ---------------------------------------------------------------------------------------------------------------
// ★★★ QG ROUND 3 — AN EXPLICITLY REQUESTED CHANGE MUST NEVER BE DISCARDED BECAUSE **NV** ALREADY HOLDS ITS VALUE.
//     The rule the two cases below measure:
//         no_change = the candidate equals the persisted record
//                 AND membership already matches
//                 AND every EXPLICITLY REQUESTED **LIVE** domain already matches
//     ⛔ THE PREVIOUS RULE WAS RECORD-ONLY (`!membership_changed && !differs`), and the failure was reachable on the
//     real device rather than hypothetical: `mobile register freq=…` retunes the radio and moves `_cfg.layers[0]`
//     WITHOUT persisting (`Node::adopt_mobile_phy`, `lib/core/node.cpp:869-890`), so `team <current> <the record's own
//     PHY>` yielded a candidate byte-identical to NV -> `no_change` -> THE RADIO STAYED WHERE IT WAS and the operator
//     was told nothing needed doing.
// ★★ WHY EACH CASE SNAPSHOTS THE RECORD AND REQUIRES IT **BYTE-IDENTICAL** AFTERWARDS: that is what proves the RECORD
//    conjunct could not have produced the `applied` verdict. Without it these cases would pass just as well against a
//    tracker that noticed some record difference, and the live conjunct would be untested — the vacuity this project has
//    recorded ten times. ⇒ the live reading is the ONLY thing that can decide them.
TEST_CASE("§PROV-TX QG3 — record PHY == the request but the LIVE PHY differs: applied, EXACTLY ONE PHY application") {
    // ★ FOUR ARMS, ONE PER TRACKED FIELD. A predicate comparing only some of the four leaves one arm reporting
    //   `no_change`, i.e. RED here — which is what makes all four comparisons load-bearing rather than decorative.
    for (int which = 0; which < 4; ++which) {
        PFix f;
        f.cfg.team_id       = 0x4444u;
        f.store.rec.team_id = 0x4444u;
        // the tail names EXACTLY what the RECORD holds -> the candidate cannot differ from it
        TeamRequest r = f.join(0x4444u);
        r.phy.present           = true;
        r.phy.freq_mhz          = f.store.rec.freq_mhz;
        r.phy.routing_sf        = f.store.rec.routing_sf;
        r.phy.bw_hz             = f.store.rec.bw_hz;
        r.phy.allowed_sf_bitmap = f.store.rec.allowed_sf_bitmap;
        // …but the LIVE radio has been moved out from under it by `mobile register`, in exactly ONE field
        if (which == 0) f.snap.live_freq_mhz          = f.store.rec.freq_mhz + 0.5;
        if (which == 1) f.snap.live_routing_sf        = 12;
        if (which == 2) f.snap.live_bw_hz             = 500000;
        if (which == 3) f.snap.live_allowed_sf_bitmap = static_cast<uint16_t>(1u << 12);
        const mrnv::Blob before = f.store.rec;
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

        CHECK(res.verdict == ProvVerdict::applied);          // ⛔ the record-only rule reported `no_change` here
        CHECK(res.err == ProvErr::none);
        CHECK(f.live.phy_calls == 1);                        // ★ EXACTLY ONE PHY application — asserted as a COUNT
        CHECK(f.live.phy.freq_mhz          == r.phy.freq_mhz);
        CHECK(f.live.phy.routing_sf        == r.phy.routing_sf);
        CHECK(f.live.phy.bw_hz             == r.phy.bw_hz);
        CHECK(f.live.phy.allowed_sf_bitmap == r.phy.allowed_sf_bitmap);
        CHECK(f.store.writes == 1);                          // one save ATTEMPT, as `applied` requires
        // ★★ AND THE RECORD IS BYTE-IDENTICAL TO WHAT IT ALREADY WAS ⇒ the `differs` conjunct was FALSE and could not
        //    have decided this verdict. (On device `mrnv::save` coalesces such a write, so no flash is spent.)
        CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);
        // ⛔ AND NOTHING ELSE HAPPENED — in particular NO AIRTIME: a same-team PHY repair is not a join.
        CHECK(f.live.set_team_calls == 0);
        CHECK(f.live.install_calls == 0);
        CHECK(f.live.dad_calls == 0);
        CHECK(!res.dad_fired);
    }
    // ★ THE CONTROL, IN THE SAME CASE: the byte-identical request against a CONVERGED live radio is `no_change` with
    //   zero saves and zero retunes. So the four arms above are caused by the DIVERGENCE and not by "a PHY tail always
    //   applies" — and the no-change row of §3.6.1 still holds.
    PFix g;
    g.cfg.team_id       = 0x4444u;
    g.store.rec.team_id = 0x4444u;
    TeamRequest q = g.join(0x4444u);
    q.phy.present           = true;
    q.phy.freq_mhz          = g.store.rec.freq_mhz;
    q.phy.routing_sf        = g.store.rec.routing_sf;
    q.phy.bw_hz             = g.store.rec.bw_hz;
    q.phy.allowed_sf_bitmap = g.store.rec.allowed_sf_bitmap;
    const mrfw::ProvResult ok = g.svc.apply_team(q, g.cfg, g.snap);
    CHECK(ok.verdict == ProvVerdict::no_change);
    CHECK(g.store.writes == 0);
    CHECK(g.live.phy_calls == 0);
    CHECK(g.live.total_calls() == 0);
}

TEST_CASE("§PROV-TX QG3 — record key == the request but the LIVE key differs/absent: EXACTLY ONE installation") {
    uint8_t pub[32], priv[32], pub2[32], priv2[32];
    CHECK(make_pair(0x4D, pub,  priv));
    CHECK(make_pair(0x6B, pub2, priv2));
    CHECK(memcmp(pub, pub2, 32) != 0);                       // the two pairs really differ (non-vacuity)
    // THREE ARMS: (0) the live node holds NO key · (1) it holds a DIFFERENT pair · (2) ★ its PUBLIC half matches while
    // its PRIVATE half does not — the shape a `pub`-only comparison would call a no-change, leaving a node that cannot
    // decrypt its own team traffic. `team_channel_key_load` installs VERBATIM with no re-derivation, so a record whose
    // halves disagree ([[B193]]'s partial-write question) produces exactly that live state.
    for (int arm = 0; arm < 3; ++arm) {
        PFix f;
        f.cfg.team_id       = 0x4444u;
        f.store.rec.team_id = 0x4444u;
        mrfw::blob_put_team_channel_key(f.store.rec, pub, priv);   // the RECORD already holds the requested pair
        if (arm == 1) f.set_live_key(pub2, priv2);
        if (arm == 2) f.set_live_key(pub, priv2);                  // pub matches, priv does NOT
        if (arm == 0) { CHECK(f.snap.live_key_pub == nullptr); }   // the fixture's default: no key installed
        TeamRequest r = f.join(0x4444u);
        r.key_supplied = true;
        memcpy(r.key_pub,  pub,  32);
        memcpy(r.key_priv, priv, 32);
        const mrnv::Blob before = f.store.rec;
        const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

        CHECK(res.verdict == ProvVerdict::applied);          // ⛔ the record-only rule reported `no_change` here
        CHECK(res.key_action == KeyAction::install);
        CHECK(f.live.install_calls == 1);                    // ★ EXACTLY ONE key installation — asserted as a COUNT
        CHECK(f.live.key_present);
        CHECK(memcmp(f.live.pub,  pub,  32) == 0);
        CHECK(memcmp(f.live.priv, priv, 32) == 0);           // ★ the PRIVATE half is now the record's, which is arm 2's point
        CHECK(f.store.writes == 1);
        // ★★ the record did not move ⇒ `differs` was FALSE and the LIVE conjunct is the only possible cause
        CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);
        CHECK(f.live.set_team_calls == 0);                   // same team: no switch…
        CHECK(f.live.phy_calls == 0);                        // …no PHY tail was given…
        CHECK(f.live.dad_calls == 0);                        // …and ⛔ NO AIRTIME
    }
    // ★ THE CONTROL: with the LIVE node already holding the very pair supplied, the identical request is `no_change`
    //   with zero saves and zero installs (the §3.6.1 row, and defect ①(b), both still hold).
    PFix g;
    g.cfg.team_id       = 0x4444u;
    g.store.rec.team_id = 0x4444u;
    mrfw::blob_put_team_channel_key(g.store.rec, pub, priv);
    g.set_live_key(pub, priv);
    TeamRequest q = g.join(0x4444u);
    q.key_supplied = true;
    memcpy(q.key_pub,  pub,  32);
    memcpy(q.key_priv, priv, 32);
    const mrfw::ProvResult ok = g.svc.apply_team(q, g.cfg, g.snap);
    CHECK(ok.verdict == ProvVerdict::no_change);
    CHECK(g.store.writes == 0);
    CHECK(g.live.install_calls == 0);
    CHECK(g.live.total_calls() == 0);
}

// ★ THE TWO PREDICATES DRIVEN DIRECTLY, for the arms a transaction-level case cannot isolate — and ⓘ marked honestly:
//   the `!phy.present` and the `preserve`/`clear` early returns are UNREACHABLE AS DECIDERS through `apply_team`
//   (a request naming no PHY/key cannot owe one), so a mutation deleting either early return is caught HERE and
//   nowhere else. That is the whole reason this case exists beside the two transaction cases above.
TEST_CASE("§PROV-TX QG3 — the live-domain predicates: nothing requested means nothing owed") {
    ProvSnapshot s{};
    s.live_freq_mhz = 868.0; s.live_bw_hz = 125000; s.live_routing_sf = 7;
    s.live_allowed_sf_bitmap = static_cast<uint16_t>(1u << 7);
    ProvPhy none{};                                           // .present == false
    CHECK(mrfw::live_phy_matches(none, s));                    // ⛔ an absent tail is never "owed", whatever live holds
    ProvPhy same{}; same.present = true; same.freq_mhz = 868.0; same.bw_hz = 125000;
    same.routing_sf = 7; same.allowed_sf_bitmap = static_cast<uint16_t>(1u << 7);
    CHECK(mrfw::live_phy_matches(same, s));
    ProvPhy other = same; other.freq_mhz = 869.0;
    CHECK(!mrfw::live_phy_matches(other, s));                  // …and it really can say no (the instrument fires)

    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x37, pub, priv));
    CHECK(mrfw::live_key_matches(KeyAction::preserve, pub, priv, s));   // no key named -> nothing owed, live absent
    CHECK(mrfw::live_key_matches(KeyAction::clear,    pub, priv, s));
    CHECK(!mrfw::live_key_matches(KeyAction::install, pub, priv, s));   // ★ an ABSENT live key owes the install
    s.live_key_pub = pub; s.live_key_priv = priv;
    CHECK(mrfw::live_key_matches(KeyAction::install, pub, priv, s));
    uint8_t other_priv[32]; memcpy(other_priv, priv, 32); other_priv[31] = static_cast<uint8_t>(priv[31] ^ 0x01);
    s.live_key_priv = other_priv;
    CHECK(!mrfw::live_key_matches(KeyAction::install, pub, priv, s));   // ★ pub matches, priv does not -> owed
}

// ---------------------------------------------------------------------------------------------------------------
// ★★★ QG DEFECT ② — A NEWLY ASSIGNED `team_local_id` MUST BE ABLE TO REACH NV.
// ⚠ HONEST SCOPE, STATED BEFORE THE CASE RATHER THAN AFTER IT: `g_persist_team_local_id` and `persist_cfg_if_needed()`
//   live in `src/fw_main.cpp`, which the native suite does not compile. This case therefore MODELS the two lines that
//   decide the outcome, and the model is deliberately one comparison so it cannot flatter the fix:
//     · the tracker is written ONLY at the boot restore (`src/fw_main.cpp:797`) and after `persist_cfg_if_needed`'s own
//       successful save (`:1042`) — an `ICfgStore::save` from the transaction does not touch it;
//     · `team_changed = g_node.team_local_id() != g_persist_team_local_id` (`:1030`) is what decides whether the newly
//       DAD'd id is written back at all.
//   ⛔ WHAT IT DOES NOT ESTABLISH: that the console adapter actually performs the sync. That is a source fact in a TU
//   no automated build compiles, and it is pinned by `tools/probe_prov_tx` S7 with its own negative control.
struct TrackerModel {
    uint8_t tracker = 0;                                            // g_persist_team_local_id
    bool would_persist(uint8_t live_id) const { return live_id != tracker; }   // fw_main.cpp:1030's `team_changed`
};

TEST_CASE("§PROV-TX ② the SAVED team_local_id is reported, so a re-used OLD id still persists") {
    const uint8_t kOldId = 9;                             // the id the OLD team DAD'd; restored into the tracker at boot
    PFix f;
    f.cfg.team_id             = 0x1111u;
    f.store.rec.team_id       = 0x1111u;
    f.store.rec.team_local_id = kOldId;
    TrackerModel synced;   synced.tracker   = kOldId;     // fw_main.cpp:797 — primed from NV
    TrackerModel unsynced; unsynced.tracker = kOldId;     // the DEFECT: nothing ever updates it

    TeamRequest r = f.join(0x9999u);                      // a DIFFERENT team ⇒ the candidate persists 0 = DAD pending
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);
    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(f.store.rec.team_local_id == 0);
    CHECK(res.persisted_team_local_id == 0);              // ★ the service REPORTS what it wrote
    synced.tracker = res.persisted_team_local_id;         // ★ what the adapter does, on the applied arm only

    // ★★ THE DETERMINISTIC OLD-ID-REUSE CASE: team-DAD draws EXACTLY the number the old team had.
    CHECK(synced.would_persist(kOldId));                  // ⇒ persist_cfg_if_needed writes it; NV converges
    // ⛔ AND THE CONTROL — the defect itself, in the same case:
    CHECK(!unsynced.would_persist(kOldId));               // team_changed FALSE ⇒ NV stays 0 ⇒ the next boot re-DADs
    // ★ and the model is not vacuous: any OTHER drawn id persists under either tracker, which is why only the
    //   old-id-REUSE draw could expose this and why the test pins that draw rather than a random one.
    CHECK(synced.would_persist(static_cast<uint8_t>(kOldId + 1)));
    CHECK(unsynced.would_persist(static_cast<uint8_t>(kOldId + 1)));
    // ⓘ LEAVE is the other membership change, and it is consistent: the candidate stores 0 and the live `set_team(0)`
    //   clears the id, so tracker 0 == live 0 and no further write is owed.
    PFix g;
    g.cfg.team_id             = 0x1111u;
    g.store.rec.team_id       = 0x1111u;
    g.store.rec.team_local_id = kOldId;
    TeamRequest l = g.leave();
    const mrfw::ProvResult lr = g.svc.apply_team(l, g.cfg, g.snap);
    CHECK(lr.verdict == ProvVerdict::applied);
    CHECK(lr.persisted_team_local_id == 0);
    CHECK(g.live.dad_calls == 0);
}

// ---------------------------------------------------------------------------------------------------------------
// ★★ THE ONE CONVERSION AUTHORITY (U2, owner-ruled v4 §4): the explicit-material helper, driven directly. It is the
//    function `blob_take_team_channel_key` in the device TU now DELEGATES to, so a field dropped here would be dropped
//    on every path at once — which is exactly why there is one of it and not an overload pair.
TEST_CASE("§PROV-TX blob_put_team_channel_key writes all three fields, and null material means NO key") {
    mrnv::Blob b = prov_seed_record();
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x91, pub, priv));
    mrfw::blob_put_team_channel_key(b, pub, priv);
    CHECK(b.team_ch_key_present == 1);
    CHECK(memcmp(b.team_ch_pub,  pub,  32) == 0);
    CHECK(memcmp(b.team_ch_priv, priv, 32) == 0);
    mrfw::blob_put_team_channel_key(b, nullptr, nullptr);
    CHECK(b.team_ch_key_present == 0);
    CHECK(all_zero32(b.team_ch_pub));
    CHECK(all_zero32(b.team_ch_priv));
    // half-null is treated as NO key (an unflagged buffer must never reach NV as if it were one)
    mrfw::blob_put_team_channel_key(b, pub, nullptr);
    CHECK(b.team_ch_key_present == 0);
    CHECK(all_zero32(b.team_ch_priv));
}

// ---------------------------------------------------------------------------------------------------------------
// ★★★ [[B211]] — A TEAM PHY TAIL PRESERVES THE PERSISTED `sf_list`. Owner-ruled 2026-08-18: a team PHY tail PRESERVES
//     the persisted `allowed_sf_bitmap`, `mobile register` is UNCHANGED, and the success line REPORTS the resolved set.
//
// ⛔ WHAT THE DEFECT WAS: `parse_phy_tail` builds a fresh `LayerConfig{}` and sets `allowed_sf_bitmap = 1u << pa.sf`,
//    and `handle_team` copied that into the request — so the ROUTING `sf=` also COLLAPSED the DATA SF set. A node
//    booted `data sf = 6,7` came back `data sf = 7` on metal, and it PERSISTED. The console now sends `0` =
//    "not specified" and the transaction RESOLVES it from the PERSISTED RECORD.
//
// ★★ WHY THESE ARE COUNTED AND NOT ONLY STATED (QG — the most recent vacuous instrument had two state assertions pass
//    in BOTH arms and only a counter discriminated): the preservation is observable at THREE independent places —
//    the CANDIDATE that was written, the RESULT the console renders, and the PHY that was APPLIED live — and the case
//    below counts how many of the three hold the preserved set. Under the pin-1b mutation (resolve from
//    `snap.live_allowed_sf_bitmap`) the verdict, the write count and the apply count are all IDENTICAL; only that
//    count moves, 3 -> 0.
namespace {
// {6,7} — the metal fixture's own DATA set ("receiver picks the fastest by SNR"), and {7} — what a `mobile register
// sf=7` collapses the LIVE bitmap to WITHOUT persisting ([[B207]], the divergence bench 27.8 exercises).
constexpr uint16_t kSfList67 = static_cast<uint16_t>((1u << 6) | (1u << 7));
constexpr uint16_t kSfList7  = static_cast<uint16_t>(1u << 7);

// The request a `team <id> freq=… sf=… bw=…` tail now produces: every field the operator TYPED, and `0` for the DATA
// SF set the operator did NOT type. ⛔ The `0` is the whole point — a fixture that filled it in would test nothing.
void b211_tail(TeamRequest& r, double freq, uint8_t routing_sf, uint32_t bw_hz) {
    r.phy.present           = true;
    r.phy.freq_mhz          = freq;
    r.phy.routing_sf        = routing_sf;
    r.phy.bw_hz             = bw_hz;
    r.phy.allowed_sf_bitmap = 0;          // ★ "not specified" — resolved from the PERSISTED RECORD
    r.phy.bw_khz            = bw_hz / 1000.0;
}
}  // namespace

TEST_CASE("§B211 pin 1 — a team PHY tail PRESERVES the multi-SF sf_list and PERSISTS it") {
    PFix f;
    f.cfg.team_id       = 0x5150u;
    f.store.rec.team_id = 0x5150u;
    f.store.rec.allowed_sf_bitmap = kSfList67;      // the record holds {6,7}
    f.converge_live_phy();                          // …and so does the radio: the ONLY change asked for is the freq
    TeamRequest r = f.join(0x5150u);
    b211_tail(r, 869.0, 7, 125000);                 // routing SF 7 — the value that used to collapse the set
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::applied);     // the freq really moved, so this is not a no-change row
    CHECK(res.err == ProvErr::none);
    CHECK(f.store.writes == 1);
    CHECK(f.live.phy_calls == 1);
    // ★ THE THREE OBSERVATION POINTS, counted (see the family comment)
    int preserved = 0;
    if (f.store.rec.allowed_sf_bitmap  == kSfList67) ++preserved;   // the CANDIDATE — what a reboot restores
    if (res.phy.allowed_sf_bitmap      == kSfList67) ++preserved;   // the RESULT — what the console echoes
    if (f.live.phy.allowed_sf_bitmap   == kSfList67) ++preserved;   // the APPLIED PHY — what the radio flies
    CHECK(preserved == 3);
    CHECK(f.store.rec.allowed_sf_bitmap != kSfList7);               // ⛔ SF6 was NOT dropped — the defect, named
    // …and everything the tail DID name landed
    CHECK(f.store.rec.freq_mhz   == 869.0);
    CHECK(f.store.rec.routing_sf == 7);
    CHECK(f.store.rec.bw_hz      == 125000);
    CHECK(f.live.dad_calls == 0);                                   // a same-team PHY repair spends no airtime
}

TEST_CASE("§B211 pin 1b — resolution reads the PERSISTED record, NOT the live bitmap (they diverge)") {
    // ★★ THE DIVERGENCE FIXTURE, and it is the REAL [[B207]] condition rather than a contrived one: `mobile register
    //    sf=7` collapses the LIVE bitmap and does NOT persist, so the record can hold {6,7} while the radio holds {7}.
    //    Pin 1 alone PASSES if the coder resolves from `snap.live_allowed_sf_bitmap`; this case is what separates them.
    PFix f;
    f.cfg.team_id       = 0x5151u;
    f.store.rec.team_id = 0x5151u;
    f.store.rec.allowed_sf_bitmap = kSfList67;          // STORED  {6,7}
    f.converge_live_phy();
    f.snap.live_allowed_sf_bitmap = kSfList7;           // LIVE    {7}   ⇐ the divergence
    TeamRequest r = f.join(0x5151u);
    // the tail names EXACTLY the record's freq/bw and routing SF 7 — so the ONLY thing that can differ is the SF set
    b211_tail(r, f.store.rec.freq_mhz, 7, f.store.rec.bw_hz);
    const mrnv::Blob before = f.store.rec;
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    // The live radio does not fly the resolved {6,7}, so the request IS owed -> applied, exactly one of each.
    CHECK(res.verdict == ProvVerdict::applied);
    CHECK(f.store.writes == 1);
    CHECK(f.live.phy_calls == 1);
    int preserved = 0;
    if (f.store.rec.allowed_sf_bitmap == kSfList67) ++preserved;
    if (res.phy.allowed_sf_bitmap     == kSfList67) ++preserved;
    if (f.live.phy.allowed_sf_bitmap  == kSfList67) ++preserved;
    CHECK(preserved == 3);                              // ★ 3 with the record as the source; 0 with the live bitmap
    // ★★ AND THE RECORD IS BYTE-IDENTICAL TO WHAT IT ALREADY WAS: resolving from LIVE would have written {7} here,
    //    i.e. LAUNDERED the un-persisted collapse into NV — the defect one layer down.
    CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);
    CHECK(f.live.dad_calls == 0);
}

TEST_CASE("§B211 pin 2 — no_change SURVIVES the change: record == live == request is still 0 saves, 0 applies") {
    // ★★★ THE 27.8/27.9 REGRESSION GUARD. If the unresolved `0` reached `differs` or `live_phy_matches`, a same-PHY
    //     re-apply would look like a change and would apply FOREVER. Both metal checks depend on this row.
    PFix f;
    f.cfg.team_id       = 0x5152u;
    f.store.rec.team_id = 0x5152u;
    f.store.rec.allowed_sf_bitmap = kSfList67;
    f.converge_live_phy();                              // record ≡ live, including the {6,7} set
    TeamRequest r = f.join(0x5152u);
    b211_tail(r, f.store.rec.freq_mhz, f.store.rec.routing_sf, f.store.rec.bw_hz);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::no_change);
    CHECK(f.store.writes == 0);                         // ★ ZERO saves
    CHECK(f.live.total_calls() == 0);                   // ★ ZERO live applies — every seam counter, not just phy
    CHECK(res.key_action == KeyAction::preserve);
    // ★ THE NON-VACUITY ARM, in the same case: move ONE live field and the identical request applies again. So the
    //   no_change above is caused by the convergence and not by "an unspecified sf_list is never a change".
    PFix g;
    g.cfg.team_id       = 0x5152u;
    g.store.rec.team_id = 0x5152u;
    g.store.rec.allowed_sf_bitmap = kSfList67;
    g.converge_live_phy();
    g.snap.live_freq_mhz = g.store.rec.freq_mhz + 0.5;
    TeamRequest q = g.join(0x5152u);
    b211_tail(q, g.store.rec.freq_mhz, g.store.rec.routing_sf, g.store.rec.bw_hz);
    const mrfw::ProvResult res2 = g.svc.apply_team(q, g.cfg, g.snap);
    CHECK(res2.verdict == ProvVerdict::applied);
    CHECK(g.store.writes == 1);
    CHECK(g.live.phy_calls == 1);
    CHECK(g.store.rec.allowed_sf_bitmap == kSfList67);  // …and it STILL preserves the set
}

TEST_CASE("§B211 pin 5 — an unspecified sf_list over an EMPTY record is still REFUSED, never a silent pass") {
    // ⚠ Resolution must not turn a real "no DATA SF" configuration into a pass (C2). ⓘ This is also the one
    //   OPERATOR-VISIBLE consequence of the slice: before it, `team <id> freq=… sf=9 …` would have repaired an empty
    //   record by inventing `sf_list={9}`; now the record's emptiness is REPORTED instead of silently papered over.
    PFix f;
    f.cfg.team_id       = 0x5153u;
    f.store.rec.team_id = 0x5153u;
    f.store.rec.allowed_sf_bitmap = 0;                  // a genuinely empty DATA SF set
    f.converge_live_phy();
    TeamRequest r = f.join(0x5153u);
    b211_tail(r, 869.0, 9, 125000);
    const mrfw::ProvResult res = f.svc.apply_team(r, f.cfg, f.snap);

    CHECK(res.verdict == ProvVerdict::refused);
    CHECK(res.err == ProvErr::incomplete_phy);
    CHECK(f.store.writes == 0);
    CHECK(f.live.total_calls() == 0);
    // ★ THE DISCRIMINATOR: the SAME empty record with an EXPLICIT set in the tail is accepted — so the refusal is
    //   caused by the resolved bitmap being empty, not by "a tail over an empty record always refuses".
    PFix g;
    g.cfg.team_id       = 0x5153u;
    g.store.rec.team_id = 0x5153u;
    g.store.rec.allowed_sf_bitmap = 0;
    g.converge_live_phy();
    TeamRequest q = g.join(0x5153u);
    b211_tail(q, 869.0, 9, 125000);
    q.phy.allowed_sf_bitmap = static_cast<uint16_t>((1u << 9) | (1u << 10));   // explicitly named -> no resolution
    const mrfw::ProvResult res2 = g.svc.apply_team(q, g.cfg, g.snap);
    CHECK(res2.verdict == ProvVerdict::applied);
    CHECK(g.store.rec.allowed_sf_bitmap == static_cast<uint16_t>((1u << 9) | (1u << 10)));
}
