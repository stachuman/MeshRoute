// MeshRoute — test_firmware_config_service.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK,
// never REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-13 — the native suite for the TYPED STAGED-CONFIGURATION SERVICE (src/firmware_config_service.h), spec
// 2026-07-31-onboard-oled-ui-design.md §3.6.1. One case per required behaviour, plus the negative halves.
//
// ★★ THE INSTRUMENT IS A WRITE COUNTER, AND THE POINT OF IT IS THAT "the save succeeded" PROVES NOTHING.
// "A no-op save performs zero NV writes" and "one durable write, never per-field" are both statements about HOW MANY
// TIMES the store was written, so the fake store counts `save()` CALLS and the cases assert EXACTLY a number — never
// "at least one". `FakeCfgStore` also carries the positive control's other half: it can FAIL a write, which is the
// only way to see that the live apply happens strictly AFTER durable success.
// ★★ AND THE COUNTER ITSELF IS CONTROLLED (instruments that cannot fail — 24 instances in this project): the first
// case below drives the fake DIRECTLY and requires the count to move, so a counter that had been left unincremented
// could not make every other case pass vacuously.
#include "doctest.h"
#include "firmware_config_service.h"
#include <cstdint>
#include <cstring>
#include <initializer_list>   // the range-for over a braced field list (not dragged in transitively)

using mrfw::CfgApplyClass;
using mrfw::CfgField;
using mrfw::CfgLiveFields;
using mrfw::CfgOpen;
using mrfw::CfgRefresh;
using mrfw::CfgSave;
using mrfw::CfgSet;
using mrfw::CfgValues;
using mrfw::ConfigService;

namespace {

// A stamped, plausible `/mrcfg` record: the version policy is `mrnv::load`'s business, but a service test that
// started from a zeroed blob would be asserting against a record the device would reject.
mrnv::Blob seed_record() {
    mrnv::Blob b{};
    b.magic   = mrnv::kMagic;
    b.version = mrnv::kVersion;
    // the four COVERED fields
    b.ble_mode            = 0;
    b.e2e_dm              = 0;
    b.intro_attach        = 1;
    b.mobile_autoregister = 1;
    // a spread of NON-COVERED fields, each of which this editor must carry through a save untouched
    b.node_id      = 42;
    b.freq_mhz     = 869.4625;
    b.bw_hz        = 125000;
    b.routing_sf   = 8;
    b.channel_ctr  = 7;
    b.team_id      = 0xA5A5A5A5u;
    b.team_local_id = 3;
    b.admin_counter_floor = 99;
    b.team_ch_key_present = 1;
    b.team_ch_pub[0] = 0xC3;
    b.team_ch_priv[0] = 0x7Eu;
    return b;
}

// THE DURABLE SEAM's fake. `writes` is the instrument; `write_ok` and `have` are the two failure modes the service
// must survive without losing the draft.
struct FakeCfgStore : mrfw::ICfgStore {
    mrnv::Blob rec = seed_record();
    bool have     = true;      // false => `load` reports no usable record
    bool write_ok = true;      // false => the write FAILS (the record is not modified)
    int  writes   = 0;         // ★ every `save` ATTEMPT, successful or not
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

// THE EFFECTIVE SEAM's fake — a live sink that really MOVES, the way `NodeConfig`'s members do on hardware, so
// "the live state is unchanged" is a measurement rather than an absence.
// ★ `seen_persisted_at_apply` is the ORDER PROOF: it records what the STORE held at the instant `apply_live` ran, so
// a live-apply that happened before (or instead of) the durable write is visible, not merely suspected.
struct FakeLive : mrfw::ICfgLive {
    CfgValues     eff{};
    int           applies = 0;
    CfgLiveFields last{};
    FakeCfgStore* store = nullptr;
    CfgValues     seen_persisted_at_apply{};
    CfgValues effective() const override { return eff; }
    void apply_live(const CfgLiveFields& f) override {
        ++applies;
        last = f;
        eff.at(CfgField::e2e_dm)              = f.e2e_dm ? 1 : 0;
        eff.at(CfgField::intro_attach)        = f.intro_attach ? 1 : 0;
        eff.at(CfgField::mobile_autoregister) = f.mobile_autoregister ? 1 : 0;
        if (store) seen_persisted_at_apply = mrfw::cfg_values_from_blob(store->rec);
    }
};

// The fixture: a store, a live sink whose EFFECTIVE state starts equal to the persisted record (a freshly booted
// node), and the service over both.
struct Fix {
    FakeCfgStore store;
    FakeLive     live;
    ConfigService svc{store, live};
    Fix() {
        live.eff   = mrfw::cfg_values_from_blob(store.rec);
        live.store = &store;
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("§UI-13 control — the write counter itself counts, so a zero elsewhere is evidence") {
    FakeCfgStore s;
    CHECK(s.writes == 0);
    mrnv::Blob b = s.rec;
    b.e2e_dm = 1;
    CHECK(s.save(b) == true);
    CHECK(s.writes == 1);                       // one call -> one count
    CHECK(s.save(b) == true);
    CHECK(s.writes == 2);                       // ★ it can read HIGHER than one; a stuck counter would fail here
    s.write_ok = false;
    CHECK(s.save(b) == false);
    CHECK(s.writes == 3);                       // a FAILED write is still an attempt, and is still counted
    s.have = false;
    mrnv::Blob out{};
    CHECK(s.load(out) == false);                // the other failure mode the service must survive
}

// --- behaviour 1: persisted snapshot versus editable draft ------------------------------------------------------
TEST_CASE("§UI-13 b1 — opening snapshots the persisted covered fields; editing a row moves the RAM DRAFT ONLY") {
    Fix f;
    const mrnv::Blob before = f.store.rec;
    CHECK(f.svc.is_open() == false);
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.is_open() == true);
    CHECK(f.svc.draft() == mrfw::cfg_values_from_blob(before));      // the snapshot IS the persisted record
    CHECK(f.svc.baseline() == f.svc.draft());
    CHECK(f.svc.config_unsaved() == false);

    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);
    CHECK(f.svc.baseline().at(CfgField::e2e_dm) == 0);               // the baseline does NOT follow the draft
    CHECK(f.svc.config_unsaved() == true);
    // ⛔ no flash write, no live mutation, no radio retune — the three things §3.6.1 forbids before SAVE
    CHECK(f.store.writes == 0);
    CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.live.eff.at(CfgField::e2e_dm) == 0);

    // and the third state is asked about explicitly: PERSISTED, EFFECTIVE and DRAFT now hold two different answers
    CHECK(mrfw::cfg_values_from_blob(f.store.rec).at(CfgField::e2e_dm) == 0);
    CHECK(f.live.effective().at(CfgField::e2e_dm) == 0);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);

    // an unopened service refuses to be edited (fail loud rather than staging into a baseline-less draft)
    Fix g;
    CHECK(g.svc.set(CfgField::e2e_dm, 1) == CfgSet::not_open);
    CHECK(g.svc.config_unsaved() == false);
    CHECK(g.svc.save() == CfgSave::not_open);
    CHECK(g.svc.discard() == CfgRefresh::not_open);
    CHECK(g.svc.reload() == CfgRefresh::not_open);
    CHECK(g.svc.reboot_required() == false);
    CHECK(g.store.writes == 0);
}

TEST_CASE("§UI-13 b1 — a store with no usable record REFUSES to open (C2): no draft, no baseline, no write") {
    Fix f;
    f.store.have = false;
    CHECK(f.svc.open() == CfgOpen::no_record);
    CHECK(f.svc.is_open() == false);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.store.writes == 0);
}

// --- behaviour 2: typed field validation ------------------------------------------------------------------------
TEST_CASE("§UI-13 b2 — typed domains: ble_mode 0..2, the three toggles 0..1; a bad value leaves the draft untouched") {
    CHECK(mrfw::cfg_field_valid(CfgField::ble_mode, 0) == true);
    CHECK(mrfw::cfg_field_valid(CfgField::ble_mode, 1) == true);
    CHECK(mrfw::cfg_field_valid(CfgField::ble_mode, 2) == true);     // `periodic` — still written by serial/BLE
    CHECK(mrfw::cfg_field_valid(CfgField::ble_mode, 3) == false);
    CHECK(mrfw::cfg_field_valid(CfgField::ble_mode, 255) == false);
    for (CfgField t : { CfgField::e2e_dm, CfgField::intro_attach, CfgField::mobile_autoregister }) {
        CHECK(mrfw::cfg_field_valid(t, 0) == true);
        CHECK(mrfw::cfg_field_valid(t, 1) == true);
        CHECK(mrfw::cfg_field_valid(t, 2) == false);                 // ⛔ NOT coerced to 1 (C2)
        CHECK(mrfw::cfg_field_valid(t, 200) == false);
    }

    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::ble_mode, 3) == CfgSet::bad_value);
    CHECK(f.svc.draft().at(CfgField::ble_mode) == 0);                 // fail CLOSED — not half-written
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.set(CfgField::intro_attach, 7) == CfgSet::bad_value);
    CHECK(f.svc.draft().at(CfgField::intro_attach) == 1);
    CHECK(f.svc.set(CfgField::ble_mode, 2) == CfgSet::ok);            // the whole domain is reachable
    CHECK(f.svc.draft().at(CfgField::ble_mode) == 2);
    CHECK(f.store.writes == 0);
}

TEST_CASE("§UI-13 b2 — the whole-candidate predicate names the FIRST bad field, and every field is checked") {
    CfgValues c{};
    CfgField bad = CfgField::ble_mode;
    CHECK(mrfw::cfg_values_valid(c, bad) == true);                    // all zero is in-domain (not a default policy)
    c.at(CfgField::mobile_autoregister) = 9;
    CHECK(mrfw::cfg_values_valid(c, bad) == false);
    CHECK(bad == CfgField::mobile_autoregister);                      // the LAST field is reached, not just the first
    c.at(CfgField::e2e_dm) = 4;
    CHECK(mrfw::cfg_values_valid(c, bad) == false);
    CHECK(bad == CfgField::e2e_dm);                                   // first-in-order wins
}

// --- behaviour 5 + 8: validate everything BEFORE writing --------------------------------------------------------
TEST_CASE("§UI-13 b5/b8 — one invalid field means ZERO writes, and the draft AND live state both survive") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CfgValues cand = f.svc.draft();
    cand.at(CfgField::e2e_dm)       = 1;                             // a legitimate change …
    cand.at(CfgField::intro_attach) = 6;                             // … and one invalid field in the same candidate
    f.svc.stage_all(cand);
    const mrnv::Blob before = f.store.rec;

    CHECK(f.svc.save() == CfgSave::invalid);
    CHECK(f.store.writes == 0);                                      // ★ nothing was written, not even the good field
    CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);
    CHECK(f.live.applies == 0);
    CHECK(f.live.eff.at(CfgField::e2e_dm) == 0);                     // the OLD live state
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);                   // the draft is RETAINED
    CHECK(f.svc.draft().at(CfgField::intro_attach) == 6);
    CHECK(f.svc.config_unsaved() == true);                            // and so is the marker
    CHECK(mrfw::cfg_save_panel(CfgSave::invalid)[0] == '\0');         // §3.6.1 names no headline for this one
}

// --- behaviour 4: the no-op save --------------------------------------------------------------------------------
TEST_CASE("§UI-13 b4 — a no-op save performs ZERO NV writes; the positive control performs EXACTLY ONE") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 0);                                      // ★ counted, not inferred from "it succeeded"
    CHECK(f.live.applies == 0);                                      // and nothing was re-applied live either

    // set a row and set it straight back: the draft is byte-equal to the baseline again, so this is STILL a no-op —
    // the marker is a VALUE comparison, not an "was edited" latch
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(f.svc.set(CfgField::e2e_dm, 0) == CfgSet::ok);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 0);

    // THE POSITIVE CONTROL: a real change must produce exactly one write
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.store.writes == 1);
    // …and saving again immediately is a no-op once more: no second write
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 1);
}

// --- behaviour 6: ONE durable write -----------------------------------------------------------------------------
TEST_CASE("§UI-13 b6 — four changed fields are ONE durable write, and every non-covered field survives it") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::ble_mode, 1) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::intro_attach, 0) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::mobile_autoregister, 0) == CfgSet::ok);

    // a NON-COVERED field moves under us between open and save (a leased channel_ctr roll is exactly this shape)
    f.store.rec.channel_ctr = 4242;

    const CfgSave r = f.svc.save();
    CHECK(r == CfgSave::saved_reboot);                                // ble_mode moved -> reboot class
    CHECK(f.store.writes == 1);                                      // ★ EXACTLY one, not four (no per-field writes)

    // the covered fields landed …
    CHECK(f.store.rec.ble_mode == 1);
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.intro_attach == 0);
    CHECK(f.store.rec.mobile_autoregister == 0);
    // … and the whole-record write carried the rest through, including the value written AFTER `open()`
    CHECK(f.store.rec.channel_ctr == 4242);                          // not reverted by a stale open-time copy
    CHECK(f.store.rec.node_id == 42);
    CHECK(f.store.rec.routing_sf == 8);
    CHECK(f.store.rec.bw_hz == 125000);
    CHECK(f.store.rec.team_id == 0xA5A5A5A5u);
    CHECK(f.store.rec.team_local_id == 3);
    CHECK(f.store.rec.admin_counter_floor == 99);
    CHECK(f.store.rec.team_ch_key_present == 1);
    CHECK(f.store.rec.team_ch_pub[0] == 0xC3);
    CHECK(f.store.rec.team_ch_priv[0] == 0x7Eu);
    CHECK(f.store.rec.magic == mrnv::kMagic);
    CHECK(f.store.rec.version == mrnv::kVersion);
}

TEST_CASE("§UI-13 b6 — cfg_values_into_blob touches the four covered bytes and nothing else in the record") {
    mrnv::Blob a = seed_record();
    mrnv::Blob b = a;
    CfgValues c = mrfw::cfg_values_from_blob(a);
    c.at(CfgField::ble_mode)            = 2;
    c.at(CfgField::e2e_dm)              = 1;
    c.at(CfgField::intro_attach)        = 0;
    c.at(CfgField::mobile_autoregister) = 0;
    mrfw::cfg_values_into_blob(c, b);
    CHECK(mrfw::cfg_values_from_blob(b) == c);
    // zero the four covered bytes in BOTH records: what remains must be byte-identical
    a.ble_mode = a.e2e_dm = a.intro_attach = a.mobile_autoregister = 0;
    b.ble_mode = b.e2e_dm = b.intro_attach = b.mobile_autoregister = 0;
    CHECK(memcmp(&a, &b, sizeof a) == 0);
}

// --- behaviour 7 + 8: apply live ONLY after durable success ------------------------------------------------------
TEST_CASE("§UI-13 b7/b8 — THE FAILURE PATH: the write fails, so the live state is UNCHANGED and the draft survives") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::mobile_autoregister, 0) == CfgSet::ok);
    const mrnv::Blob before = f.store.rec;
    f.store.write_ok = false;

    CHECK(f.svc.save() == CfgSave::nv_failed);
    CHECK(f.store.writes == 1);                                      // one attempt — never a retry loop, never four
    CHECK(memcmp(&before, &f.store.rec, sizeof before) == 0);        // the PERSISTED state is the old one
    CHECK(f.live.applies == 0);                                      // ★ nothing was applied live
    CHECK(f.live.eff.at(CfgField::e2e_dm) == 0);
    CHECK(f.live.eff.at(CfgField::mobile_autoregister) == 1);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);                   // the draft survives …
    CHECK(f.svc.draft().at(CfgField::mobile_autoregister) == 0);
    CHECK(f.svc.config_unsaved() == true);                            // … and so does the marker
    CHECK(strcmp(mrfw::cfg_save_panel(CfgSave::nv_failed), "SAVE FAILED") == 0);

    // the operator retries once the store recovers: the SAME draft commits, in one write
    f.store.write_ok = true;
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.store.writes == 2);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.live.applies == 1);
    CHECK(f.live.eff.at(CfgField::e2e_dm) == 1);
}

TEST_CASE("§UI-13 b7 — a pre-write reload failure refuses the save with ZERO writes (the record must be preservable)") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    f.store.have = false;                                            // the store can no longer produce the record
    CHECK(f.svc.save() == CfgSave::nv_failed);
    CHECK(f.store.writes == 0);                                      // ⛔ never write a record we could not read first
    CHECK(f.live.applies == 0);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);
    CHECK(f.svc.config_unsaved() == true);
}

TEST_CASE("§UI-13 b7 — ORDER, not just outcome: at the instant apply_live runs, the store ALREADY holds the new value") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::intro_attach, 0) == CfgSet::ok);
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.live.applies == 1);
    CHECK(f.live.seen_persisted_at_apply.at(CfgField::e2e_dm) == 1);        // ★ durable FIRST, live SECOND
    CHECK(f.live.seen_persisted_at_apply.at(CfgField::intro_attach) == 0);
    CHECK(f.live.last.e2e_dm == true);
    CHECK(f.live.last.intro_attach == false);
    CHECK(f.live.last.mobile_autoregister == true);
}

TEST_CASE("§UI-13 b7 — a REBOOT-class field is never applied live: CfgLiveFields structurally cannot carry it") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::ble_mode, 1) == CfgSet::ok);
    CHECK(f.svc.save() == CfgSave::saved_reboot);
    CHECK(f.store.rec.ble_mode == 1);                                // persisted
    CHECK(f.live.eff.at(CfgField::ble_mode) == 0);                   // ★ effective did NOT move — reboot to apply
    CHECK(mrfw::cfg_apply_class(CfgField::ble_mode) == CfgApplyClass::reboot_at);
    for (CfgField t : { CfgField::e2e_dm, CfgField::intro_attach, CfgField::mobile_autoregister })
        CHECK(mrfw::cfg_apply_class(t) == CfgApplyClass::live_now);
}

// --- behaviour 9: reboot_required is INDEPENDENT of config_unsaved ----------------------------------------------
TEST_CASE("§UI-13 b9 — a reboot-needing save is durably SAVED and NO LONGER unsaved; the flag is derived, not latched") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.reboot_required() == false);
    CHECK(f.svc.set(CfgField::ble_mode, 2) == CfgSet::ok);
    CHECK(f.svc.reboot_required() == false);                          // an UNSAVED draft is not a reboot obligation
    CHECK(f.svc.config_unsaved() == true);

    CHECK(f.svc.save() == CfgSave::saved_reboot);
    CHECK(f.store.writes == 1);
    CHECK(f.svc.config_unsaved() == false);                          // ★ the two flags are independent
    CHECK(f.svc.reboot_required() == true);                          // and it stays true until the reboot
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.svc.reboot_required() == true);
    CHECK(f.store.writes == 1);

    // set it BACK to the effective value and save: persisted and effective agree again, so nothing is owed. A latched
    // flag would still be claiming a reboot here.
    CHECK(f.svc.set(CfgField::ble_mode, 0) == CfgSet::ok);
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.store.writes == 2);
    CHECK(f.svc.reboot_required() == false);
    CHECK(f.svc.config_unsaved() == false);
}

// --- behaviour 3: conflict detection, and its negative half -----------------------------------------------------
TEST_CASE("§UI-13 b3 — an external write to a COVERED field raises CFG! RELOAD and REFUSES the save") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.conflict() == false);

    // serial/BLE keep their immediate-write path (§3.6.1) and then notify
    f.store.rec.intro_attach = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    CHECK(strcmp(mrfw::cfg_save_panel(CfgSave::conflict), "CFG! RELOAD") == 0);

    CHECK(f.svc.save() == CfgSave::conflict);
    CHECK(f.store.writes == 0);                                      // ⛔ NOT last-writer-wins
    CHECK(f.store.rec.intro_attach == 0);                            // the companion's change stands
    CHECK(f.live.applies == 0);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);                   // the draft is still there to resolve
    CHECK(f.svc.config_unsaved() == true);
}

TEST_CASE("§UI-13 b3 — the commit-time detector stands alone: an UNANNOUNCED external write still cannot be overwritten") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    f.store.rec.mobile_autoregister = 0;                             // nobody called note_external_write
    CHECK(f.svc.conflict() == false);                                // so no marker yet …
    CHECK(f.svc.save() == CfgSave::conflict);                        // … and the SAVE still refuses
    CHECK(f.svc.conflict() == true);                                 // the refusal raises the marker itself
    CHECK(f.store.writes == 0);
    CHECK(f.store.rec.mobile_autoregister == 0);
}

TEST_CASE("§UI-13 b3 — CHANGE, NOTIFY, REVERT, SAVE: a raised latch refuses the save even once the bytes agree again") {
    // ★★ THE THIRD STATE OF THE CONFLICT DOMAIN, and the one a byte comparison alone cannot name:
    //   {bytes differ} · {bytes match, latch clear} · {BYTES MATCH, LATCH SET}.
    // An external writer changes a covered field (the latch goes up), then puts the original bytes back. `save()`'s
    // byte comparison now passes — so without the latch gate the object would REPORT a conflict and SAVE ANYWAY,
    // which is last-writer-wins arriving through the back door: the operator was told to RELOAD or DISCARD and did
    // neither, and whatever else that writer did in between was never re-read.
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);

    const uint8_t original = f.store.rec.intro_attach;
    f.store.rec.intro_attach = 0;                                    // the companion writes …
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);
    f.store.rec.intro_attach = original;                             // … and then puts it back
    CHECK(mrfw::cfg_values_from_blob(f.store.rec) == f.svc.baseline());   // the BYTES match again …
    CHECK(f.svc.conflict() == true);                                 // … and the latch is still up

    CHECK(f.svc.save() == CfgSave::conflict);                        // ★ refused on the latch, not on the bytes
    CHECK(f.store.writes == 0);
    CHECK(f.store.loads == 1);                                       // and it did not even need to read the store
    CHECK(f.live.applies == 0);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);                   // draft and marker both survive the refusal
    CHECK(f.svc.config_unsaved() == true);

    // the two ways out §3.6.1 names both clear the latch and let the same draft commit
    CHECK(f.svc.reload() == CfgRefresh::ok);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.intro_attach == original);
}

TEST_CASE("§UI-13 b3 — DISCARD is the other way out of a latched conflict, and it clears it too") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    const uint8_t original = f.store.rec.mobile_autoregister;
    f.store.rec.mobile_autoregister = 0;
    f.svc.note_external_write(f.store.rec);
    f.store.rec.mobile_autoregister = original;                      // reverted: the bytes agree, the latch does not
    CHECK(f.svc.save() == CfgSave::conflict);
    CHECK(f.store.writes == 0);
    CHECK(f.svc.discard() == CfgRefresh::ok);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.config_unsaved() == false);                          // discard threw the draft away, as it must
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 0);
}

TEST_CASE("§UI-13 b3 — a CONVERGENT external write is still a conflict: the baseline is what must match, not the draft") {
    // ★ THE THREE STATES ARE ALL DISTINCT HERE AND THE ANSWER DEPENDS ON WHICH TWO ARE COMPARED: the operator's draft
    // and the persisted record now AGREE (both e2e_dm=1) while the BASELINE says 0. §3.6.1 refuses the save whenever
    // the fingerprint no longer matches — so a `draft == persisted` test alone would report `no_change` and clear a
    // conflict nobody resolved.
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    f.store.rec.e2e_dm = 1;                                          // the companion made the same change first
    CHECK(f.svc.save() == CfgSave::conflict);
    CHECK(f.store.writes == 0);
    CHECK(f.svc.conflict() == true);
    // RELOAD adopts the new baseline; the draft already agrees, so there is nothing left to save
    CHECK(f.svc.reload() == CfgRefresh::ok);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 0);
}

TEST_CASE("§UI-13 b9 — a LIVE-class field whose effective and persisted values disagree owes NO reboot") {
    // The reboot obligation is a property of the REBOOT-class fields only. A live-class field can legitimately differ
    // between persisted and effective (a companion wrote `/mrcfg` and the running node has not been told), and that is
    // a different problem with a different answer — not "reboot to apply".
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    f.store.rec.e2e_dm = 1;                                          // persisted moves …
    CHECK(f.svc.reload() == CfgRefresh::ok);                         // … and becomes the baseline
    CHECK(f.svc.baseline().at(CfgField::e2e_dm) == 1);
    CHECK(f.live.eff.at(CfgField::e2e_dm) == 0);                     // … while EFFECTIVE has not moved
    CHECK(f.svc.reboot_required() == false);                         // ★ live-class: no reboot is owed
    // and the reboot-class field is the control for that zero
    f.store.rec.ble_mode = 1;
    CHECK(f.svc.reload() == CfgRefresh::ok);
    CHECK(f.svc.reboot_required() == true);
}

TEST_CASE("§UI-13 b3 NEGATIVE HALF — a NON-covered external write raises no conflict and never sets the marker") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);

    // every one of these is a real `/mrcfg` field this editor does not cover — a leased send counter, a team switch,
    // an admin replay floor, a radio retune, a re-DAD'd node id, a team-plane id
    f.store.rec.channel_ctr         = 1234;
    f.store.rec.team_id             = 0x11223344u;
    f.store.rec.team_local_id       = 9;
    f.store.rec.admin_counter_floor = 4096;
    f.store.rec.node_id             = 77;
    f.store.rec.routing_sf          = 9;
    f.store.rec.freq_mhz            = 868.1;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == false);                                 // ★ the marker is NOT raised
    CHECK(f.svc.config_unsaved() == true);                           // (still unsaved for the operator's own reason)

    // and the save goes through, in one write, carrying every one of those values forward
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.channel_ctr == 1234);
    CHECK(f.store.rec.team_id == 0x11223344u);
    CHECK(f.store.rec.team_local_id == 9);
    CHECK(f.store.rec.admin_counter_floor == 4096);
    CHECK(f.store.rec.node_id == 77);
    CHECK(f.store.rec.routing_sf == 9);
}

TEST_CASE("§UI-13 b3 NEGATIVE HALF — a RUNTIME change cannot reach the service at all, so no marker exists to set") {
    // Routes, registrations, the battery reading and the unread counters have NO `/mrcfg` representation: the only
    // external input this service accepts is a record, and only four of its bytes are ever read. This case pins the
    // reachable half of that argument — an external write that is byte-identical in the covered fields, however much
    // else moved, is silence.
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    mrnv::Blob unrelated = f.store.rec;
    unrelated.channel_ctr = 65535;
    unrelated.lineage_id  = 5;
    unrelated.config_epoch = 12;
    unrelated.claim_epoch  = 3;
    f.svc.note_external_write(unrelated);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 0);
    // an external write that DOES move a covered field is the control for this case's zero
    unrelated.e2e_dm = 1;
    f.svc.note_external_write(unrelated);
    CHECK(f.svc.conflict() == true);
}

TEST_CASE("§UI-13 b3 — note_external_write on a CLOSED service is inert (there is no baseline to compare)") {
    Fix f;
    mrnv::Blob moved = f.store.rec;
    moved.e2e_dm = 1;
    f.svc.note_external_write(moved);
    CHECK(f.svc.conflict() == false);
}

// --- behaviour 9: DISCARD / RELOAD -------------------------------------------------------------------------------
TEST_CASE("§UI-13 b9 — DISCARD reloads the persisted values and clears both the draft and the marker") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::ble_mode, 2) == CfgSet::ok);
    f.store.rec.intro_attach = 0;                                    // an external change lands too
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);

    CHECK(f.svc.discard() == CfgRefresh::ok);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.config_unsaved() == false);
    CHECK(f.svc.draft() == mrfw::cfg_values_from_blob(f.store.rec)); // the draft IS the persisted record now
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 0);                   // the operator's edits are gone …
    CHECK(f.svc.draft().at(CfgField::ble_mode) == 0);
    CHECK(f.svc.draft().at(CfgField::intro_attach) == 0);             // … and the companion's change is adopted
    CHECK(f.store.writes == 0);                                      // a discard never writes
    CHECK(f.svc.save() == CfgSave::no_change);
    CHECK(f.store.writes == 0);
}

TEST_CASE("§UI-13 b9 — RELOAD is a THREE-WAY MERGE: an untouched field adopts theirs, an edited field keeps mine") {
    // ★ OWNER-RULED 2026-08-13 ([[B192]], ledger §1.22): this is the ruled behaviour, not a candidate — an UNCHANGED
    // draft field adopts the current persisted value, an EDITED one stays unsaved in the draft, and DISCARD remains
    // the full reset (the case above). ⇒ these assertions pin a ruling; ⛔ do not relax them into a re-design.
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);             // the operator edits e2e_dm only
    f.store.rec.intro_attach        = 0;                            // the companion changes two OTHER fields
    f.store.rec.mobile_autoregister = 0;
    f.svc.note_external_write(f.store.rec);
    CHECK(f.svc.conflict() == true);

    CHECK(f.svc.reload() == CfgRefresh::ok);
    CHECK(f.svc.conflict() == false);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);                  // mine survives …
    CHECK(f.svc.draft().at(CfgField::intro_attach) == 0);            // … theirs is adopted, not overwritten
    CHECK(f.svc.draft().at(CfgField::mobile_autoregister) == 0);
    CHECK(f.svc.baseline() == mrfw::cfg_values_from_blob(f.store.rec));
    CHECK(f.svc.config_unsaved() == true);                           // one field still differs from the new baseline
    CHECK(f.store.writes == 0);

    // and the save now goes through WITHOUT reverting either companion change — the property last-writer-wins breaks
    CHECK(f.svc.save() == CfgSave::saved);
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.intro_attach == 0);
    CHECK(f.store.rec.mobile_autoregister == 0);
}

TEST_CASE("§UI-13 b9 — a DISCARD or RELOAD that cannot read the store keeps the draft and the marker (C2)") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    f.store.rec.intro_attach = 0;
    f.svc.note_external_write(f.store.rec);
    f.store.have = false;
    CHECK(f.svc.discard() == CfgRefresh::nv_failed);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);
    CHECK(f.svc.conflict() == true);
    CHECK(f.svc.reload() == CfgRefresh::nv_failed);
    CHECK(f.svc.draft().at(CfgField::e2e_dm) == 1);
    CHECK(f.svc.conflict() == true);
    CHECK(f.store.writes == 0);
}

// --- §3.6.1: BACK and blanking PRESERVE the draft ---------------------------------------------------------------
TEST_CASE("§UI-13 — BACK, blanking and RE-ENTERING SETTINGS all preserve the draft; a silent discard is forbidden") {
    Fix f;
    CHECK(f.svc.open() == CfgOpen::ok);
    CHECK(f.svc.set(CfgField::e2e_dm, 1) == CfgSet::ok);
    CHECK(f.svc.set(CfgField::ble_mode, 1) == CfgSet::ok);
    const CfgValues staged = f.svc.draft();

    f.svc.on_back();
    CHECK(f.svc.draft() == staged);
    CHECK(f.svc.config_unsaved() == true);
    f.svc.on_blank();
    CHECK(f.svc.draft() == staged);
    CHECK(f.svc.config_unsaved() == true);
    // ★ re-entry is the third door, and it is the one a re-`open()` would have walked the draft out of
    CHECK(f.svc.open() == CfgOpen::already_open);
    CHECK(f.svc.draft() == staged);
    CHECK(f.svc.config_unsaved() == true);
    CHECK(f.store.writes == 0);
    CHECK(f.live.applies == 0);

    // the draft is still committable after all three
    CHECK(f.svc.save() == CfgSave::saved_reboot);
    CHECK(f.store.writes == 1);
    CHECK(f.store.rec.e2e_dm == 1);
    CHECK(f.store.rec.ble_mode == 1);
}

// --- behaviour 9: every outcome is named, and the two ruled panel strings are exact -----------------------------
TEST_CASE("§UI-13 b9 — the outcome enums are total, and only the two spec-named headlines have panel bytes") {
    CHECK(strcmp(mrfw::cfg_open_name(CfgOpen::ok), "ok") == 0);
    CHECK(strcmp(mrfw::cfg_open_name(CfgOpen::already_open), "already_open") == 0);
    CHECK(strcmp(mrfw::cfg_open_name(CfgOpen::no_record), "no_record") == 0);
    CHECK(strcmp(mrfw::cfg_set_name(CfgSet::ok), "ok") == 0);
    CHECK(strcmp(mrfw::cfg_set_name(CfgSet::not_open), "not_open") == 0);
    CHECK(strcmp(mrfw::cfg_set_name(CfgSet::bad_value), "bad_value") == 0);
    CHECK(strcmp(mrfw::cfg_refresh_name(CfgRefresh::ok), "ok") == 0);
    CHECK(strcmp(mrfw::cfg_refresh_name(CfgRefresh::not_open), "not_open") == 0);
    CHECK(strcmp(mrfw::cfg_refresh_name(CfgRefresh::nv_failed), "nv_failed") == 0);

    const CfgSave all[] = { CfgSave::saved, CfgSave::saved_reboot, CfgSave::no_change,
                            CfgSave::invalid, CfgSave::conflict, CfgSave::nv_failed, CfgSave::not_open };
    for (CfgSave r : all) {
        CHECK(mrfw::cfg_save_name(r) != nullptr);
        CHECK(strcmp(mrfw::cfg_save_name(r), "?") != 0);             // no enumerator falls off the switch
        CHECK(mrfw::cfg_save_panel(r) != nullptr);                   // never null — a renderer may print it directly
    }
    CHECK(strcmp(mrfw::cfg_save_name(CfgSave::saved), "saved") == 0);
    CHECK(strcmp(mrfw::cfg_save_name(CfgSave::saved_reboot), "saved_reboot") == 0);
    CHECK(strcmp(mrfw::cfg_save_name(CfgSave::no_change), "no_change") == 0);
    CHECK(strcmp(mrfw::cfg_save_panel(CfgSave::conflict), "CFG! RELOAD") == 0);
    CHECK(strcmp(mrfw::cfg_save_panel(CfgSave::nv_failed), "SAVE FAILED") == 0);
    CHECK(mrfw::cfg_save_panel(CfgSave::saved)[0] == '\0');
    CHECK(mrfw::cfg_save_panel(CfgSave::saved_reboot)[0] == '\0');
    CHECK(mrfw::cfg_save_panel(CfgSave::no_change)[0] == '\0');
    CHECK(mrfw::cfg_save_panel(CfgSave::not_open)[0] == '\0');

    for (uint8_t i = 0; i < mrfw::kCfgFieldCount; ++i) {
        const CfgField f = static_cast<CfgField>(i);
        CHECK(strcmp(mrfw::cfg_field_name(f), "?") != 0);
    }
    CHECK(strcmp(mrfw::cfg_field_name(CfgField::ble_mode), "ble_mode") == 0);
    CHECK(strcmp(mrfw::cfg_field_name(CfgField::e2e_dm), "e2e_dm") == 0);
    CHECK(strcmp(mrfw::cfg_field_name(CfgField::intro_attach), "intro_attach") == 0);
    CHECK(strcmp(mrfw::cfg_field_name(CfgField::mobile_autoregister), "mobile_autoregister") == 0);
}

// --- the covered set is exactly the four durable fields ---------------------------------------------------------
TEST_CASE("§UI-13 — the covered fields round-trip through the durable Blob, in both directions") {
    mrnv::Blob b = seed_record();
    b.ble_mode = 2; b.e2e_dm = 1; b.intro_attach = 0; b.mobile_autoregister = 1;
    const CfgValues c = mrfw::cfg_values_from_blob(b);
    CHECK(c.at(CfgField::ble_mode) == 2);
    CHECK(c.at(CfgField::e2e_dm) == 1);
    CHECK(c.at(CfgField::intro_attach) == 0);
    CHECK(c.at(CfgField::mobile_autoregister) == 1);
    mrnv::Blob out = seed_record();
    mrfw::cfg_values_into_blob(c, out);
    CHECK(out.ble_mode == 2);
    CHECK(out.e2e_dm == 1);
    CHECK(out.intro_attach == 0);
    CHECK(out.mobile_autoregister == 1);
    CHECK(mrfw::cfg_values_from_blob(out) == c);
    // each covered field is DISTINCT — a from/into pair that crossed two fields would pass a same-value round-trip
    for (uint8_t i = 0; i < mrfw::kCfgFieldCount; ++i) {
        mrnv::Blob one{};
        CfgValues probe{};
        probe.at(static_cast<CfgField>(i)) = 1;
        mrfw::cfg_values_into_blob(probe, one);
        const CfgValues back = mrfw::cfg_values_from_blob(one);
        CHECK(back == probe);
        uint8_t set_bytes = 0;
        for (uint8_t j = 0; j < mrfw::kCfgFieldCount; ++j) set_bytes = uint8_t(set_bytes + (back.v[j] ? 1 : 0));
        CHECK(set_bytes == 1);
    }
    // and the value carrier's equality is a real comparison, not a stub
    CfgValues x{}, y{};
    CHECK(x == y);
    y.at(CfgField::mobile_autoregister) = 1;
    CHECK(x != y);
}
