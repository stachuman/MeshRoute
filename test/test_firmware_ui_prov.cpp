// MeshRoute — test_firmware_ui_prov.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-15 slice 5 — the native suite for the OLED TEAM-CREATE ADAPTER (`src/firmware_ui_prov.h`), plan §2.1/§8 and
// design §3.6.3.
//
// ★★★ WHY IT EXISTS: the adapter is the ONE place where the owner's 2026-08-19 PHY ruling is expressed, and the two
//     TUs it sits between — `src/firmware_ui.cpp` and `src/firmware_config.cpp` — are compiled by NEITHER the native
//     suite nor the simulator. A precondition written in either would be a rule no gate can drive and no mutation can
//     redden, which is the [[B212]]/[[B220]]/[[B223]] shape this arc has already paid for four times.
//
// ★★ THE INSTRUMENTS ARE COUNTERS, exactly as §PROV-TX's are and for the same reason: three of this slice's
//    requirements are statements about HOW MANY TIMES something happened —
//      · "the refusal applies NOTHING"      -> store writes, the four live counters and `applies` all asserted == 0
//      · "CONFIRM drives EXACTLY ONE"       -> `apply_calls`, asserted == a number
//      · "notify ONLY on the applied arm"   -> `on_applied_calls`, asserted == 0 on every non-applied verdict
//    ⛔ AND EVERY ZERO CARRIES A POSITIVE ARM in the same case (the identical request with the divergence removed), so
//    a zero is evidence rather than an absence — the discipline that stops the ninth green-against-its-own-defect
//    instrument this project has recorded.
//
// ⛔ WHAT THESE CASES MAY NOT BE DESCRIBED AS PROVING: nothing about FLASH. The store is a fake; §PROV-TX's own
//    boundary applies here unchanged, and [[B193]]'s power-cut half is a bench check.
#include "doctest.h"
#include "firmware_ui_prov.h"
#include <cstdint>
#include <cstring>

using mrfw::ProvErr;
using mrfw::ProvPhy;
using mrfw::ProvPhyFloor;
using mrfw::ProvResult;
using mrfw::ProvSnapshot;
using mrfw::ProvVerdict;
using mrfw::ProvisioningService;
using mrfw::TeamRequest;
using mrui::UiProvAnswer;
using mrui::UiProvIntent;
using mrui::UiProvOp;
using mrui::UiProvOutcome;

namespace {

// The persisted record a provisioned mobile holds. ⚠ `is_mobile = 1` matches the live config below for the reason
// §PROV-TX's seed states: otherwise every request would also be a role-projection change.
mrnv::Blob seed_record() {
    mrnv::Blob b{};
    b.magic   = mrnv::kMagic;
    b.version = mrnv::kVersion;
    b.freq_mhz          = 869.4625;          // ⚠ NOT representable in integral kHz — the value the record must keep
    b.bw_hz             = 125000;
    b.routing_sf        = 7;
    b.allowed_sf_bitmap = static_cast<uint16_t>((1u << 6) | (1u << 7));   // TWO data SFs: the [[B211]] shape
    b.is_mobile         = 1;
    b.node_id           = 42;
    b.team_local_id     = 9;
    b.channel_ctr       = 7;
    return b;
}

struct FakeStore : mrfw::ICfgStore {
    mrnv::Blob rec = seed_record();
    bool have = true, write_ok = true;
    int  writes = 0, loads = 0;
    bool load(mrnv::Blob& out) override { ++loads; if (!have) return false; out = rec; return true; }
    bool save(const mrnv::Blob& b) override { ++writes; if (!write_ok) return false; rec = b; return true; }
};
// The live sink — it really MOVES, so "nothing was applied" is a measurement and not an absence.
struct FakeLive : mrfw::IProvLive {
    uint32_t team_id = 0;
    bool     key_present = false;
    ProvPhy  phy{};
    int set_team_calls = 0, install_calls = 0, phy_calls = 0, dad_calls = 0;
    void set_team(uint32_t t) override { ++set_team_calls; team_id = t; key_present = false; }
    void install_key(const uint8_t*, const uint8_t*) override { ++install_calls; key_present = true; }
    void apply_phy(const ProvPhy& p) override { ++phy_calls; phy = p; }
    void fire_dad() override { ++dad_calls; }
    int total() const { return set_team_calls + install_calls + phy_calls + dad_calls; }
};
struct FakeEntropy : mrfw::IEntropy {
    uint8_t seed = 0x33;
    void fill(uint8_t* out, size_t n) override { for (size_t i = 0; i < n; ++i) out[i] = uint8_t(seed + i); }
};

// ★★★ THE DEVICE SEAM's FAKE, and it is the whole point of `ITeamCreateDevice` being an interface: on hardware these
//     four are forwards to `device_cfg_store()`, `g_node`, `prov_service()` and the two bookkeeping calls. Here they
//     are the SAME forwards over fakes — so what the suite drives is the REAL transaction, not a model of it.
// ★ IT CAPTURES THE REQUEST, which is what lets plan §2.1's trap be ASSERTED rather than argued: `last_rq.phy.present`
//   is read off the object the adapter actually handed to `apply_team`.
// §UI-16 K2 — the `/mrteams` keyring the transaction persists a created/imported key into before it writes the
// `/mrcfg` candidate. ⓘ An in-RAM store: this file measures the ADAPTER, and the keyring's own write policy is
// measured by `test/test_firmware_team_keyring.cpp`.
struct FakeKeyStore : mrfw::ITeamKeyStore {
    mrnv::TeamKeyBlob rec{};
    mrnv::TeamKeyRead state = mrnv::TeamKeyRead::absent;
    int saves = 0;
    mrnv::TeamKeyRead load(mrnv::TeamKeyBlob& out) override { out = rec; return state; }
    bool save(const mrnv::TeamKeyBlob& b) override { ++saves; rec = b; state = mrnv::TeamKeyRead::ok; return true; }
};

struct DevFake : mrfw::ITeamCreateDevice {
    FakeStore   store;
    FakeLive    live;
    FakeEntropy ent;
    FakeKeyStore keys;
    mrfw::TeamKeyringService keyring{keys};
    meshroute::NodeConfig cfg{};
    ProvSnapshot snap{};
    ProvPhyFloor floor{};
    ProvisioningService svc{store, live, ent, keyring};

    int  load_calls = 0, facts_calls = 0, apply_calls = 0, on_applied_calls = 0;
    bool load_answer = true;
    TeamRequest last_rq{};
    ProvResult  last_result{};
    uint8_t     noted_team_local_id = 0xFF;

    DevFake() {
        cfg.is_mobile = true;
        cfg.team_id   = 0;
        snap.key_hash32       = 0xDEADBEEFu;
        snap.mobile_reg_count = 0;
        floor.freq_mhz = 868.0;
        floor.bw_hz    = 125000;
        converge();
    }
    // The CONVERGED node: the radio flies exactly the PHY the record holds. ⓘ BY VALUE, so a later change to either
    // side does not drag the other along — that independence is what makes the divergence cases measurements.
    void converge() {
        snap.live_freq_mhz          = store.rec.freq_mhz;
        snap.live_bw_hz             = store.rec.bw_hz;
        snap.live_routing_sf        = store.rec.routing_sf;
        snap.live_allowed_sf_bitmap = store.rec.allowed_sf_bitmap;
    }
    bool load_record(mrnv::Blob& out) override {
        ++load_calls;
        if (!load_answer) return false;
        return store.load(out);
    }
    void device_facts(ProvSnapshot& s, ProvPhyFloor& f) override { ++facts_calls; s = snap; f = floor; }
    ProvResult apply(TeamRequest& rq, const ProvSnapshot& s) override {
        ++apply_calls;
        last_rq = rq;                                  // ⚠ CAPTURED BEFORE the call: `apply_team` wipes the key halves
        last_result = svc.apply_team(rq, cfg, s);
        return last_result;
    }
    void on_applied(const ProvResult& r) override { ++on_applied_calls; noted_team_local_id = r.persisted_team_local_id; }
    int live_total() const { return live.total(); }
};

UiProvAnswer create(DevFake& d) { return mrfw::ui_prov_create_team(d); }

// ================================================================ §UI-15 slice 6 — the STATIC-JOIN half's fakes
// ★★★ THE SAME DISCIPLINE ONE FEATURE OVER: what runs below is the REAL `JoinProfileService` (slice 2) and the REAL
//     `JoinService` (slice 1) over counting fakes, so "exactly one durable write" and "zero live calls on a refusal"
//     are MEASUREMENTS. ⛔ The adapter is never handed a model of a transaction.
struct FakeJoinStore : mrfw::IJoinStore {
    mrnv::JoinBlob rec{};
    mrnv::JoinRead answer = mrnv::JoinRead::ok;
    int loads = 0, writes = 0;
    FakeJoinStore() { mrnv::join_blob_init(rec); }
    mrnv::JoinRead load(mrnv::JoinBlob& out) override {
        ++loads;
        if (answer == mrnv::JoinRead::ok) out = rec;
        return answer;
    }
    bool save(const mrnv::JoinBlob& b) override { ++writes; rec = b; return true; }
};
// The join transaction's LIVE seam — it really counts, so "nothing was applied" is a measurement and not an absence.
struct FakeJoinLive : mrfw::IJoinLive {
    int        calls = 0;
    mrnv::Blob last{};
    void apply_and_start(const mrnv::Blob& b) override { ++calls; last = b; }
};
// ★ THE DEVICE SEAM's FAKE: on hardware these three are forwards to `join_profile_service()`, `join_service()` and
//   `mr_ui_on_config_saved()`. Here they are the SAME forwards over the same real services.
struct JoinDevFake : mrfw::IJoinDevice {
    FakeStore     cfg;                                   // the `/mrcfg` record the transaction writes through
    FakeJoinStore presets;                               // the `/mrjoin` record the SELECT screen reads
    FakeJoinLive  live;
    mrfw::JoinProfileService psvc{presets};
    mrfw::JoinService        jsvc{cfg, live};

    int list_calls = 0, apply_calls = 0, on_started_calls = 0;
    mrfw::JoinRequest last_rq{};

    mrfw::ProfileResult list_profiles(mrnv::JoinBlob& out) override { ++list_calls; return psvc.list(out); }
    mrfw::JoinResult apply(const mrfw::JoinRequest& rq) override {
        ++apply_calls;
        last_rq = rq;                                    // ⚠ CAPTURED: the ONE conversion is asserted on this object
        return jsvc.apply_join(rq);
    }
    void on_started(const mrfw::JoinResult&) override { ++on_started_calls; }
};

// A stored preset, as `joinprofile set` would have written it. ⚠ 869.4625 MHz is 869462500 Hz EXACTLY and is ⛔ not
// representable in integral kHz — the value the whole units decision rests on.
mrnv::JoinProfile preset(uint8_t layer, uint8_t sf, uint32_t freq_hz, uint32_t bw_hz) {
    mrnv::JoinProfile p{};
    p.present = 1; p.layer = layer; p.routing_sf = sf; p.freq_hz = freq_hz; p.bw_hz = bw_hz;
    return p;
}
UiProvAnswer join(JoinDevFake& d, const mrnv::JoinProfile& p) { return mrfw::ui_prov_join_static(d, p); }

}  // namespace

// ------------------------------------------------------------------------------------------------------ the control
TEST_CASE("§UI15-PROV control — every instrument moves on the ordinary path, so a zero elsewhere is evidence") {
    DevFake d;
    const UiProvAnswer a = create(d);
    CHECK(a.outcome == UiProvOutcome::created);
    CHECK(d.facts_calls == 1);
    CHECK(d.load_calls == 1);
    CHECK(d.apply_calls == 1);
    CHECK(d.on_applied_calls == 1);
    CHECK(d.store.writes == 1);
    CHECK(d.live.set_team_calls == 1);
    CHECK(d.live.install_calls == 1);
    CHECK(d.live.dad_calls == 1);
}

// -------------------------------------------------------------------------------------- the REQUEST plan §2.1 builds
TEST_CASE("§UI15-PROV the request is a MINT with `phy.present = FALSE` — no retune, no PHY tail, no [[B209]] path") {
    DevFake d;
    const UiProvAnswer a = create(d);
    CHECK(a.outcome == UiProvOutcome::created);
    // ★★★ THE TRAP, ASSERTED ON THE CAPTURED REQUEST: `present = false` is what makes the transaction PRESERVE the
    //     persisted PHY. ⛔ `true` here would route a PHY through `IProvLive::apply_phy` — the [[B209]] path a
    //     provisioning create must never take.
    CHECK(d.last_rq.phy.present == false);
    CHECK(d.last_rq.mint == true);
    CHECK(d.last_rq.key_supplied == false);        // the OLED types no key; `team new` mints one inside the candidate
    CHECK(d.last_rq.team_id == 0);                 // a mint carries no id — the transaction draws it
    // ⇒ and the consequence, measured on the live sink: the radio was NEVER retuned.
    CHECK(d.live.phy_calls == 0);
    // ★ THE MEMBERSHIP OPERATION DID happen: a new team, its key installed, and DAD started LAST.
    CHECK(d.live.set_team_calls == 1);
    CHECK(d.live.install_calls == 1);
    CHECK(d.live.key_present == true);
    CHECK(d.live.dad_calls == 1);
    CHECK(a.team_id != 0);
    CHECK(a.team_id == d.last_result.team_id);
    CHECK(d.live.team_id == a.team_id);
    // ★ THE RECORD KEEPS ITS PHY, byte for byte — including the `sf_list` [[B211]] exists for and the frequency no
    //   integral kHz can hold.
    CHECK(d.store.rec.freq_mhz == 869.4625);
    CHECK(d.store.rec.bw_hz == 125000u);
    CHECK(d.store.rec.routing_sf == 7);
    CHECK(d.store.rec.allowed_sf_bitmap == uint16_t((1u << 6) | (1u << 7)));
    // ★ AND THE BUILD FLOOR IS STILL CARRIED (§3.4): without it a `0` in the record would refuse for the wrong reason.
    CHECK(d.last_rq.floor.freq_mhz == 868.0);
    CHECK(d.last_rq.floor.bw_hz == 125000u);
    // ★ THE POST-SAVE BOOKKEEPING RAN ONCE, with what the CANDIDATE wrote (0 = DAD-pending, design v2's sentinel).
    CHECK(d.on_applied_calls == 1);
    CHECK(d.noted_team_local_id == 0);
    CHECK(d.store.rec.team_local_id == 0);
}

// -------------------------------------------------------------------------- the OWNER's PHY PRECONDITION (plan §2.1)
TEST_CASE("§UI15-PROV live != persisted PHY REFUSES with PHY DIFFERS / USE SERIAL — and applies NOTHING") {
    // ★★★★ FOUR DIVERGENCES, ONE FIELD AT A TIME, because a precondition that compared three of the four would pass
    //      every case a single combined divergence could produce. ⛔ THE FOURTH IS `sf_list`, which is exactly the
    //      field a hand-written equality forgets ([[B211]]) — and the one `mobile register sf=…` moves without
    //      persisting, i.e. the divergence this ruling exists for.
    for (int field = 0; field < 4; ++field) {
        DevFake d;
        switch (field) {
            case 0: d.snap.live_freq_mhz          = 868.0; break;
            case 1: d.snap.live_bw_hz             = 250000; break;
            case 2: d.snap.live_routing_sf        = 9; break;
            case 3: d.snap.live_allowed_sf_bitmap = uint16_t(1u << 7); break;   // the COLLAPSED set
        }
        const UiProvAnswer a = create(d);
        CHECK(a.outcome == UiProvOutcome::phy_differs);
        CHECK(strcmp(mrui::prov_result_head(a), "PHY DIFFERS") == 0);
        CHECK(strcmp(mrui::prov_result_detail(a), "USE SERIAL") == 0);
        // ⛔ NOTHING WAS APPLIED, on every instrument there is: the transaction was never entered, so no write, no
        //    live mutation, no airtime and no notification.
        CHECK(d.apply_calls == 0);
        CHECK(d.store.writes == 0);
        CHECK(d.live_total() == 0);
        CHECK(d.live.dad_calls == 0);
        CHECK(d.on_applied_calls == 0);
        CHECK(a.team_id == 0);
        // ...and the record is untouched, membership included.
        CHECK(d.store.rec.team_id == 0u);
        CHECK(d.store.rec.team_local_id == 9);
    }
    // ★ THE POSITIVE ARM, in the same case: the identical node with the divergence REMOVED creates normally — so the
    //   four zeros above are evidence and not a fixture that could never do anything.
    DevFake ok;
    const UiProvAnswer a = create(ok);
    CHECK(a.outcome == UiProvOutcome::created);
    CHECK(ok.apply_calls == 1);
    CHECK(ok.store.writes == 1);
    CHECK(ok.live.dad_calls == 1);
}

TEST_CASE("§UI15-PROV the precondition's ProvPhy is the PERSISTED one with present=TRUE — never the request's") {
    // ★★★★ THE TWO OBJECTS, PINNED AS TWO. `live_phy_matches` EARLY-RETURNS TRUE on `!present`, so a precondition
    //      built with `present = false` would pass ALWAYS — the silent no-op this case exists to make impossible.
    //      Driving the predicate directly with both spellings is what shows the difference is real rather than stylistic.
    DevFake d;
    d.snap.live_routing_sf = 9;                       // a genuine divergence from the record's 7
    ProvPhy as_written{};                             // ...as the adapter builds it: PERSISTED values, present = TRUE
    as_written.present           = true;
    as_written.freq_mhz          = d.store.rec.freq_mhz;
    as_written.routing_sf        = d.store.rec.routing_sf;
    as_written.bw_hz             = d.store.rec.bw_hz;
    as_written.allowed_sf_bitmap = d.store.rec.allowed_sf_bitmap;
    CHECK(mrfw::live_phy_matches(as_written, d.snap) == false);      // ⇒ the refusal is REACHABLE
    ProvPhy as_the_request{};                                        // ...and the REQUEST's, which is present = false
    CHECK(as_the_request.present == false);
    CHECK(mrfw::live_phy_matches(as_the_request, d.snap) == true);    // ⛔ always true — the trap, made visible
    // ⇒ the adapter uses the first: this node refuses.
    CHECK(create(d).outcome == UiProvOutcome::phy_differs);
    // ...and the request it would have built, on a converged node, still carries the SECOND spelling.
    DevFake conv;
    CHECK(create(conv).outcome == UiProvOutcome::created);
    CHECK(conv.last_rq.phy.present == false);
}

TEST_CASE("§UI15-PROV an UNREADABLE record refuses — an unestablished precondition is never treated as satisfied") {
    DevFake d;
    d.load_answer = false;
    const UiProvAnswer a = create(d);
    CHECK(a.outcome == UiProvOutcome::refused);
    CHECK(strcmp(a.reason, mrfw::prov_err_name(ProvErr::nv_load_failed)) == 0);
    CHECK(strcmp(mrui::prov_result_head(a), "CREATE REFUSED") == 0);
    CHECK(strcmp(mrui::prov_result_detail(a), "nv_load_failed") == 0);
    CHECK(d.apply_calls == 0);                       // ⛔ the transaction was never entered
    CHECK(d.store.writes == 0);
    CHECK(d.live_total() == 0);
    CHECK(d.on_applied_calls == 0);
}

// ------------------------------------------------------------------------------------ the VERDICT -> ANSWER mapping
TEST_CASE("§UI15-PROV a FAILED save reports SAVE FAILED, notifies NOTHING and leaves the live node alone") {
    DevFake d;
    d.store.write_ok = false;
    const UiProvAnswer a = create(d);
    CHECK(a.outcome == UiProvOutcome::save_failed);
    CHECK(strcmp(mrui::prov_result_head(a), "SAVE FAILED") == 0);
    CHECK(strcmp(mrui::prov_result_detail(a), "NOTHING CHANGED") == 0);
    CHECK(d.apply_calls == 1);                       // the transaction RAN...
    CHECK(d.store.writes == 1);                      // ...and attempted EXACTLY ONE write
    CHECK(d.live_total() == 0);                      // ⛔ ...which failed, so nothing was applied and no airtime spent
    CHECK(d.live.dad_calls == 0);
    // ⛔⛔ AND IT DID NOT NOTIFY: `mr_ui_on_config_saved` on a write that failed is [[B194]] inverted, and the persist
    //    tracker would then hold an id no record contains.
    CHECK(d.on_applied_calls == 0);
    CHECK(a.team_id == 0);
    CHECK(d.store.rec.team_id == 0u);                // the record still holds the OLD membership
}

TEST_CASE("§UI15-PROV a STAGING refusal carries the SERVICE's own typed reason, and notifies nothing") {
    // O2: promoting a node that HOSTS registered mobiles orphans its guests — the truth table refuses before staging.
    // ⚠ THE NODE MUST BE STATIC FOR O2 TO BE REACHABLE AT ALL (it is a PROMOTION rule), and the record must AGREE with
    //   that role or the request would also be a role-projection change — §PROV-TX's own O2 case says the same.
    DevFake d;
    d.cfg.is_mobile = false;
    d.store.rec.is_mobile = 0;
    d.snap.mobile_reg_count = 2;
    const UiProvAnswer a = create(d);
    CHECK(a.outcome == UiProvOutcome::refused);
    CHECK(d.last_result.verdict == ProvVerdict::refused);
    CHECK(d.last_result.err == ProvErr::role_refused);
    CHECK(strcmp(a.reason, mrfw::prov_err_name(ProvErr::role_refused)) == 0);
    CHECK(strcmp(mrui::prov_result_detail(a), "role_refused") == 0);
    CHECK(d.apply_calls == 1);
    CHECK(d.store.writes == 0);                      // ⛔ a staging refusal spends no write...
    CHECK(d.live_total() == 0);                      // ...no live call and no airtime
    CHECK(d.on_applied_calls == 0);
    // ⓘ ...and the reason is the SERVICE's token verbatim: every `ProvErr` name reaches the panel unchanged, so no
    //   second table can drift from it. (A sample of the arms an OLED create can actually reach.)
    CHECK(strcmp(mrfw::prov_err_name(ProvErr::incomplete_phy), "incomplete_phy") == 0);
    CHECK(strcmp(mrfw::prov_err_name(ProvErr::sf_list_empty), "sf_list_empty") == 0);   // [[B230]]'s arm
    CHECK(strcmp(mrfw::prov_err_name(ProvErr::keygen_failed), "keygen_failed") == 0);
    CHECK(strcmp(mrfw::prov_err_name(ProvErr::nv_save_failed), "nv_save_failed") == 0);
}

TEST_CASE("§UI15-PROV an INCOMPLETE persisted PHY refuses at staging — the create is not a way to repair one") {
    DevFake d;
    d.store.rec.allowed_sf_bitmap = 0;               // no DATA SF at all — [[data-sf-removed]] makes this illegal
    d.converge();                                    // ⇒ live and persisted still AGREE, so the precondition passes
    const UiProvAnswer a = create(d);
    CHECK(a.outcome == UiProvOutcome::refused);
    // ★ [[B230]]: the OLED reaches the SAME finer arm the console does — one classification, two renderers (U1). The
    //   panel prints the token verbatim, so `sf_list_empty` (13 of the 19-column body) is what the operator now reads
    //   instead of the field-list `incomplete_phy`. ⛔ The outcome, the write count and the live count do not move.
    CHECK(d.last_result.err == ProvErr::sf_list_empty);
    CHECK(strcmp(mrui::prov_result_detail(a), "sf_list_empty") == 0);
    CHECK(d.apply_calls == 1);                       // the PRECONDITION passed; the TRANSACTION refused
    CHECK(d.store.writes == 0);
    CHECK(d.live_total() == 0);
    CHECK(d.on_applied_calls == 0);
}

// ======================================================== §UI-15 slice 6 — THE STATIC-JOIN ADAPTER (plan §2.3/§3)
TEST_CASE("§UI15-JOIN control — a selected preset STARTS the join: ONE write, ONE live apply, ONE notification") {
    JoinDevFake d;
    const UiProvAnswer a = join(d, preset(4, 9, 869462500u, 125000u));
    // ⛔⛔ `joining`, ⛔ NEVER `joined`: the transaction has written once and STARTED DAD, and nothing more is known.
    CHECK(a.outcome == UiProvOutcome::joining);
    CHECK(strcmp(mrui::prov_result_head(a), "JOINING") == 0);
    CHECK(strcmp(mrui::prov_result_detail(a), "") == 0);
    CHECK(d.apply_calls == 1);
    CHECK(d.cfg.writes == 1);                        // ★ EXACTLY ONE durable write
    CHECK(d.live.calls == 1);                        // ...and the live retune + re-DAD, AFTER it
    CHECK(d.on_started_calls == 1);                  // ...and §notify-every-save, on this arm alone
    CHECK(a.node_id == 0);                           // ⛔ no id is claimed: DAD has only begun
}

TEST_CASE("§UI15-JOIN ★★ the ONE integral -> double conversion — 869.4625 MHz survives the store round trip") {
    // ★★★★ PLAN §3's UNITS DECISION, MEASURED END TO END: the STORED profile is integral Hz (869462500), the
    //      TRANSIENT request carries the operator's MHz/kHz `double`s, and what lands in `/mrcfg` must be the value
    //      the bench actually flies. ⛔ An integral kHz field anywhere on this path rounds 869462.5 to 869462 and
    //      CHANGES THE FREQUENCY — the defect the Hz-not-kHz ruling exists for.
    JoinDevFake d;
    const UiProvAnswer a = join(d, preset(4, 9, 869462500u, 125000u));
    CHECK(a.outcome == UiProvOutcome::joining);
    CHECK(d.last_rq.freq_mhz == 869.4625);           // ⚠ the REQUEST, captured before the transaction ran
    CHECK(d.last_rq.bw_khz == 125.0);
    CHECK(d.last_rq.layer == 4);
    CHECK(d.last_rq.routing_sf == 9);
    CHECK(d.cfg.rec.freq_mhz == 869.4625);           // ...and the RECORD the transaction composed
    CHECK(d.cfg.rec.bw_hz == 125000u);
    CHECK(d.cfg.rec.routing_sf == 9);
    // ★ A FRACTIONAL BANDWIDTH SURVIVES TOO: 62.5 kHz is a real LoRa BW and is 62500 Hz exactly.
    JoinDevFake d2;
    CHECK(join(d2, preset(4, 7, 868000000u, 62500u)).outcome == UiProvOutcome::joining);
    CHECK(d2.last_rq.bw_khz == 62.5);
    CHECK(d2.cfg.rec.bw_hz == 62500u);
}

TEST_CASE("§UI15-JOIN ★★ a preset ABOVE LAYER 15 persists the FULL byte and the NIBBLE — trap 2's other end") {
    // ★★★ The correlation rule's terms 2 and 3 are only satisfiable because the CANDIDATE keeps the two apart. This
    //     is the same distinction from the writing side: layer 17 persists 17 and leaves leaf 1 on the wire.
    JoinDevFake d;
    CHECK(join(d, preset(17, 9, 869462500u, 125000u)).outcome == UiProvOutcome::joining);
    CHECK(d.cfg.rec.layer0_id == 17);
    CHECK(d.cfg.rec.leaf_id == 1);
    CHECK(d.live.last.layer0_id == 17);              // ...and the LIVE apply was handed the same record
    CHECK(d.live.last.leaf_id == 1);
}

TEST_CASE("§UI15-JOIN an EMPTY slot is not a join — zero transactions, zero writes, zero airtime") {
    JoinDevFake d;
    mrnv::JoinProfile empty{};
    CHECK(empty.present == 0);
    const UiProvAnswer a = join(d, empty);
    CHECK(a.outcome == UiProvOutcome::join_refused);
    CHECK(strcmp(mrui::prov_result_head(a), "JOIN REFUSED") == 0);
    CHECK(strcmp(a.reason, "empty slot") == 0);
    CHECK(d.apply_calls == 0);
    CHECK(d.cfg.writes == 0);
    CHECK(d.live.calls == 0);
    CHECK(d.on_started_calls == 0);
    // ★ THE POSITIVE ARM, in the same case: the identical device with a PRESENT slot joins normally.
    CHECK(join(d, preset(4, 9, 869462500u, 125000u)).outcome == UiProvOutcome::joining);
    CHECK(d.cfg.writes == 1);
}

TEST_CASE("§UI15-JOIN a DOMAIN refusal carries the TRANSACTION's own typed reason, and spends nothing") {
    // ⓘ REACHABLE ON DEVICE: `joinprofile set` validates, but a record written by an older build — or one whose
    //   bytes are valid-but-wrong — can hold an SF of 0. The screen must say WHICH field, which is exactly why slice
    //   1 kept the four domain arms distinct where the console renders them with one usage line.
    JoinDevFake d;
    const UiProvAnswer a = join(d, preset(4, /*sf=*/0, 869462500u, 125000u));
    CHECK(a.outcome == UiProvOutcome::join_refused);
    CHECK(strcmp(a.reason, mrfw::join_err_name(mrfw::JoinErr::invalid_sf)) == 0);
    CHECK(strcmp(mrui::prov_result_detail(a), "invalid_sf") == 0);
    CHECK(d.apply_calls == 1);                       // the transaction RAN and refused...
    CHECK(d.cfg.loads == 0);                         // ...before it loaded anything
    CHECK(d.cfg.writes == 0);
    CHECK(d.live.calls == 0);
    CHECK(d.on_started_calls == 0);
    // ...and a layer of 0 (the unset value) refuses in its own arm, so the four are not one refusal wearing four names.
    JoinDevFake d2;
    CHECK(strcmp(join(d2, preset(0, 9, 869462500u, 125000u)).reason,
                 mrfw::join_err_name(mrfw::JoinErr::invalid_layer)) == 0);
}

TEST_CASE("§UI15-JOIN a FAILED save reports SAVE FAILED, notifies NOTHING and leaves the live node alone") {
    JoinDevFake d;
    d.cfg.write_ok = false;
    const UiProvAnswer a = join(d, preset(4, 9, 869462500u, 125000u));
    CHECK(a.outcome == UiProvOutcome::save_failed);
    CHECK(strcmp(mrui::prov_result_head(a), "SAVE FAILED") == 0);
    CHECK(strcmp(mrui::prov_result_detail(a), "NOTHING CHANGED") == 0);
    CHECK(d.apply_calls == 1);                       // the transaction RAN...
    CHECK(d.cfg.writes == 1);                        // ...and attempted EXACTLY ONE write
    CHECK(d.live.calls == 0);                        // ⛔ ...which failed, so no retune and no DAD airtime
    // ⛔⛔ AND IT DID NOT NOTIFY: §notify-every-save's condition is a write that HAPPENED ([[B194]] inverted).
    CHECK(d.on_started_calls == 0);
}

TEST_CASE("§UI15-JOIN an unreadable `/mrcfg` refuses — and it is the TRANSACTION's arm, not an invented one") {
    JoinDevFake d;
    d.cfg.have = false;
    const UiProvAnswer a = join(d, preset(4, 9, 869462500u, 125000u));
    CHECK(a.outcome == UiProvOutcome::join_refused);
    CHECK(strcmp(a.reason, mrfw::join_err_name(mrfw::JoinErr::nv_load_failed)) == 0);
    CHECK(d.apply_calls == 1);
    CHECK(d.cfg.writes == 0);
    CHECK(d.live.calls == 0);
    CHECK(d.on_started_calls == 0);
}

// ------------------------------------------------------------------------- the SELECT screen's ONE read (plan §3)
TEST_CASE("§UI15-JOIN the profile read carries the SERVICE's own answer for all FOUR store states") {
    // ★★ THE FOUR-STATE READ IS SLICE 2's AND IS NOT RE-DERIVED HERE: the adapter's whole job is to carry it, so the
    //    panel can tell an ordinary fresh device from a corrupt record from a store that would not open.
    {   // ok, with two presets
        JoinDevFake d;
        d.presets.rec.prof[0] = preset(4, 9, 869462500u, 125000u);
        d.presets.rec.prof[3] = preset(9, 7, 868000000u, 125000u);
        const mrui::UiJoinList l = mrfw::ui_prov_join_profiles(d);
        CHECK(d.list_calls == 1);
        CHECK(l.served == true);
        CHECK(l.res.verdict == mrfw::ProfileVerdict::ok);
        CHECK(mrui::join_list_count(l) == 2);
        CHECK(mrui::join_sel_rows(l).n == 3);        // two slots + BACK
        CHECK(strcmp(mrui::join_store_head(l), "") == 0);
        CHECK(l.rec.prof[0].freq_hz == 869462500u);  // ⛔ the record arrives INTEGRAL — no conversion on the read path
    }
    {   // absent — an ordinary fresh device
        JoinDevFake d;
        d.presets.answer = mrnv::JoinRead::absent;
        const mrui::UiJoinList l = mrfw::ui_prov_join_profiles(d);
        CHECK(l.served == true);
        CHECK(l.res.verdict == mrfw::ProfileVerdict::empty);
        CHECK(strcmp(mrui::join_store_head(l), "NO PROFILES") == 0);
    }
    {   // invalid — the RECORD is wrong
        JoinDevFake d;
        d.presets.answer = mrnv::JoinRead::invalid;
        const mrui::UiJoinList l = mrfw::ui_prov_join_profiles(d);
        CHECK(l.res.verdict == mrfw::ProfileVerdict::refused);
        CHECK(l.res.err == mrfw::ProfileErr::store_invalid);
        CHECK(strcmp(mrui::join_store_head(l), "PROFILE STORE") == 0);
        CHECK(strcmp(mrui::join_store_detail(l), "INVALID") == 0);
    }
    {   // ⛔⛔ io_failed — the STORE would not open, and it must NEVER read as absent or as invalid
        JoinDevFake d;
        d.presets.answer = mrnv::JoinRead::io_failed;
        const mrui::UiJoinList l = mrfw::ui_prov_join_profiles(d);
        CHECK(l.res.verdict == mrfw::ProfileVerdict::refused);
        CHECK(l.res.err == mrfw::ProfileErr::store_io_failed);
        CHECK(strcmp(mrui::join_store_head(l), "STORAGE FAILURE") == 0);
        CHECK(strcmp(mrui::join_store_detail(l), "CHECK faults") == 0);
    }
    // ⛔ AND THE READ WRITES NOTHING, on every state: `list()` is READ-ONLY and repairing on a read is exactly the
    //    silent fallback the record exists to avoid.
    for (mrnv::JoinRead st : { mrnv::JoinRead::ok, mrnv::JoinRead::absent,
                               mrnv::JoinRead::invalid, mrnv::JoinRead::io_failed }) {
        JoinDevFake d;
        d.presets.answer = st;
        (void)mrfw::ui_prov_join_profiles(d);
        CHECK(d.presets.writes == 0);
        CHECK(d.cfg.writes == 0);
        CHECK(d.apply_calls == 0);
        CHECK(d.live.calls == 0);
    }
}

// -------------------------------------------------------------------------------------------- the intent dispatch
TEST_CASE("§UI15-PROV the adapter dispatches on the INTENT, and `none` performs nothing at all") {
    DevFake d;
    JoinDevFake j;
    mrfw::UiProvisionAdapter ad(d, j);
    UiProvIntent none{};
    CHECK(none.op == UiProvOp::none);                // the zero value is the inert one
    const UiProvAnswer a0 = ad.perform(none);
    CHECK(a0.outcome == UiProvOutcome::none);
    CHECK(d.facts_calls == 0);
    CHECK(d.load_calls == 0);
    CHECK(d.apply_calls == 0);
    CHECK(d.store.writes == 0);
    CHECK(d.live_total() == 0);
    CHECK(j.apply_calls == 0);                       // ⛔ ...and it did not reach the OTHER act either
    CHECK(j.cfg.writes == 0);
    // ...and the create intent goes to the create act, once per call.
    UiProvIntent mk{}; mk.op = UiProvOp::create_team;
    const UiProvAnswer a1 = ad.perform(mk);
    CHECK(a1.outcome == UiProvOutcome::created);
    CHECK(d.apply_calls == 1);
    CHECK(d.store.writes == 1);
    CHECK(j.apply_calls == 0);                       // ⛔ a CREATE never runs the JOIN transaction
    // ★★ §UI-15 slice 6 — THE SECOND OP, ON THE SAME SEAM, AND IT CARRIES ITS PROFILE WITH IT: a slot index would
    //    have made the adapter re-read the store, so what was SHOWN and what is JOINED could differ.
    UiProvIntent jn{}; jn.op = UiProvOp::join_static; jn.join = preset(4, 9, 869462500u, 125000u);
    const UiProvAnswer a2 = ad.perform(jn);
    CHECK(a2.outcome == UiProvOutcome::joining);
    CHECK(j.apply_calls == 1);
    CHECK(j.cfg.writes == 1);
    CHECK(j.last_rq.freq_mhz == 869.4625);
    CHECK(d.apply_calls == 1);                       // ⛔ ...and a JOIN never runs the TEAM transaction
    // ...and `profiles()` reaches the join device and nothing else.
    const mrui::UiJoinList l = ad.profiles();
    CHECK(l.served == true);
    CHECK(j.list_calls == 1);
    CHECK(d.load_calls == 1);                        // unchanged by the read (the create above spent it)
}
