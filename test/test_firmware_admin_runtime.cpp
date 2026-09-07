// MeshRoute — test/test_firmware_admin_runtime.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// ★★★ REMOTE-ADMIN v2 SLICE 5 — the TARGET's prepare/commit/discard LIVE-INSTALL ORDERING
//     (`src/firmware_admin_runtime.h`, plus the two Slice 3 services that now enforce it).
//
// Design §6.5: *"candidate copy, full validation, durable save, then LIVE ACTIVATION … save failure leaves the
// old ACL and sessions active … role change or removal invalidates that slot's session only after the new ACL is
// durable."* Every clause below is measured as a COUNT or as a BYTE STATE, never as "the call was made":
//   · a refused PREPARATION costs ZERO saves and ZERO commits;
//   · a failed SAVE costs ZERO commits and leaves the running image EXACTLY as it was;
//   · a NO-OP costs ZERO draws, ZERO saves and ZERO commits;
//   · a successful save COMMITS BEFORE the caller can print anything.
//
// ⛔ THE LIMIT OF THE CLAIM, stated so nothing here reads as more than it is: the store, the entropy source and
//    the runtime are all FAKES. There is no flash, no wear, no power cut and no `Node` — the REAL firmware
//    wiring (`DeviceAdminRuntime`, the boot line, `Node::admin_session_commit`) is proved by
//    `tools/probe_inbox_verbs`, which compiles the actual `src/firmware_commands.cpp`. ⛔ [[B317]] is untouched:
//    "the running state is preserved" is ⛔ NOT a claim that the previous flash bytes survived.
#include "doctest.h"

#include <cstring>

#include "firmware_admin_runtime.h"

using mrfw::AclErr;
using mrfw::AclRole;
using mrfw::AclState;
using mrfw::AdminIdErr;
using mrfw::AdminIdState;
using mrfw::AdminSessionBootState;

namespace {

struct FakeAclStore : mrfw::IAclStore {
    mrnv::AclBlob rec{};
    mrnv::AclRead state = mrnv::AclRead::absent;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    mrnv::AclRead load(mrnv::AclBlob& out) override { ++loads; out = rec; return state; }
    bool save(const mrnv::AclBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::AclRead::ok;
        return true;
    }
};
struct FakeIdStore : mrfw::IAdminIdStore {
    mrnv::AdminIdBlob rec{};
    mrnv::AdminIdRead state = mrnv::AdminIdRead::absent;
    int  saves = 0;
    bool save_ok = true;
    mrnv::AdminIdRead load(mrnv::AdminIdBlob& out) override { out = rec; return state; }
    bool save(const mrnv::AdminIdBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::AdminIdRead::ok;
        return true;
    }
};
struct FakeSeed : mrfw::IAdminSeedSource {
    int  calls = 0;
    bool ok = true;
    uint8_t fill_byte = 0x33;
    bool fill(uint8_t out[32]) override {
        ++calls;
        for (int i = 0; i < 32; ++i) out[i] = ok ? static_cast<uint8_t>(fill_byte + i) : 0;
        return ok;
    }
};

// ★ THE RUNTIME FAKE — it COUNTS, and it can REFUSE at a chosen draw. Counting is the whole instrument: a seam
//   that installed at the wrong moment would still "work" against a fake that only recorded the last value.
struct FakeRuntime : mrfw::IAdminRuntime {
    int      draws = 0, commits = 0, entropy_failures = 0;
    int      refuse_at = -1;              // -1 = never refuse; N = the (N+1)-th draw refuses
    uint64_t next = 0x100;
    meshroute::RemoteSessionInstall last{};
    bool draw_epoch(uint64_t& out) override {
        if (refuse_at >= 0 && draws == refuse_at) { ++draws; return false; }
        ++draws;
        out = next++;
        return true;
    }
    void commit(const meshroute::RemoteSessionInstall& p) override { ++commits; last = p; }
    void entropy_failed() override { ++entropy_failures; }
};

// ★★ [[B341]] — THE REAL CORE STATE, driven exactly as `Node::admin_session_commit` drives it (minus the timer
//    arm, which is Node's). The counting `FakeRuntime` above cannot see this defect at all: it records the LAST
//    plan, and the bug is that a SEQUENCE of individually-correct plans leaves the running block wrong. Only a
//    runtime that ACCUMULATES them — i.e. the production `remote_session_install` — can show it.
struct RealRuntime : mrfw::IAdminRuntime {
    meshroute::RemoteSessionState st{};
    uint64_t next = 0x1000;
    int draws = 0, commits = 0, entropy_failures = 0;
    RealRuntime() { meshroute::remote_session_clear(st); }
    bool draw_epoch(uint64_t& out) override { ++draws; out = next++; return true; }
    void commit(const meshroute::RemoteSessionInstall& p) override {
        ++commits;
        meshroute::remote_session_install(st, p);
    }
    void entropy_failed() override { ++entropy_failures; meshroute::remote_session_mark_entropy_failed(st); }
};

void seed_owner_acl(FakeAclStore& s, uint8_t n) {
    mrnv::acl_blob_init(s.rec);
    for (uint8_t i = 0; i < n; ++i) {
        for (int b = 0; b < 32; ++b) s.rec.rec[i].ed_pub[b] = static_cast<uint8_t>(i * 41 + b + 1);
        s.rec.rec[i].role = mrnv::kAclRoleOwner;
    }
    s.rec.count = n;
    s.state = mrnv::AclRead::ok;
}

}  // namespace

// =============================================================================================================
// The ONE conversion path
// =============================================================================================================
TEST_CASE("§radmin-5/R1 the durable record converts to the live image through ONE path, holes and all") {
    FakeAclStore store;
    seed_owner_acl(store, 3);
    store.rec.rec[1] = mrnv::AclRow{};                    // a HOLE at slot 1 — slot numbers are stable handles
    store.rec.rec[1].role = mrnv::kAclRoleEmpty;
    store.rec.rec[2].role = mrnv::kAclRoleOperator;
    store.rec.count = 2;

    meshroute::AdminAclRow img[meshroute::kRadminAclSlots];
    mrfw::admin_acl_image_from_blob(store.rec, img);
    CHECK(img[0].role == meshroute::kRadminRoleOwner);
    CHECK(img[1].role == meshroute::kRadminRoleEmpty);    // ⛔ the hole is a hole, ⛔ NOT compacted away
    CHECK(img[2].role == meshroute::kRadminRoleOperator);
    CHECK(std::memcmp(img[0].ed_pub, store.rec.rec[0].ed_pub, 32) == 0);
    CHECK(std::memcmp(img[2].ed_pub, store.rec.rec[2].ed_pub, 32) == 0);
    // ★ AN EMPTY ROW IS CANONICAL ZERO in the image, whatever bytes the record happened to hold.
    for (int b = 0; b < 32; ++b) CHECK(img[1].ed_pub[b] == 0);
    CHECK(img[1].reserved == 0);
    // ⛔ AN UNRECOGNISED ROLE NEVER BECOMES A LIVE ROW.
    store.rec.rec[3].role = 0x7F;
    for (int b = 0; b < 32; ++b) store.rec.rec[3].ed_pub[b] = 0xEE;
    mrfw::admin_acl_image_from_blob(store.rec, img);
    CHECK(img[3].role == meshroute::kRadminRoleEmpty);
    for (int b = 0; b < 32; ++b) CHECK(img[3].ed_pub[b] == 0);
}

// =============================================================================================================
// PREPARE — per slot, only where something moved
// =============================================================================================================
TEST_CASE("§radmin-5/R2 an ACL change draws for the CHANGED slots only; unchanged slots keep their epochs") {
    FakeAclStore store;
    seed_owner_acl(store, 3);
    mrnv::AclBlob after = store.rec;
    after.rec[1].role = mrnv::kAclRoleOperator;           // a ROLE change IS a change
    after.rec[2] = mrnv::AclRow{};                        // a REMOVAL
    after.count = 2;

    FakeRuntime rt;
    meshroute::RemoteSessionInstall plan{};
    CHECK(mrfw::admin_runtime_prepare_acl(rt, store.rec, after, plan));
    CHECK(rt.draws == 1);                                 // ⛔ ONE draw: slot 1 only. Slot 0 is unchanged, slot 2 removed.
    CHECK(plan.set_acl);
    CHECK_FALSE(plan.epoch_set[0]);                       // ★ unchanged -> its epoch and its work are untouched
    CHECK(plan.epoch_set[1]);
    CHECK(plan.epoch[1] != 0);
    CHECK(plan.epoch_set[2]);
    CHECK(plan.epoch[2] == 0);                            // ⛔ a REMOVED slot becomes UNUSABLE, not re-minted
    CHECK_FALSE(plan.set_root);                           // ⛔ an ACL change never moves the administration pair
    CHECK_FALSE(plan.invalidate_all);
}

TEST_CASE("§radmin-5/R3 a refused draw returns runtime_unavailable BEFORE anything durable happens") {
    FakeAclStore store;
    seed_owner_acl(store, 2);
    mrnv::AclBlob after = store.rec;
    after.rec[0].role = mrnv::kAclRoleOperator;
    FakeRuntime rt;
    rt.refuse_at = 0;                                     // the very first draw refuses
    meshroute::RemoteSessionInstall plan{};
    CHECK_FALSE(mrfw::admin_runtime_prepare_acl(rt, store.rec, after, plan));
    CHECK(rt.commits == 0);
    CHECK(store.saves == 0);
}

TEST_CASE("§radmin-5/R4 a ROOT preparation derives the pair and mints TEN complete epochs, or refuses whole") {
    FakeRuntime rt;
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 7);
    meshroute::RemoteSessionInstall plan{};
    CHECK(mrfw::admin_runtime_prepare_root(rt, seed, plan));
    CHECK(rt.draws == meshroute::kRadminAclSlots);        // ★ TEN, and a COMPLETE candidate before readiness
    CHECK(plan.set_root);
    CHECK(plan.invalidate_all);                           // ⛔ a new root kills every old session
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
        CHECK(plan.epoch_set[i]);
        CHECK(plan.epoch[i] != 0);                        // ⛔ epoch 0 is NEVER installed
    }
    // The pair really is derived from THAT seed — checked against the one derivation authority, not against a copy.
    meshroute::Identity expect{};
    meshroute::identity_from_seed(expect, seed);
    CHECK(std::memcmp(plan.ed_pub, expect.ed_pub, 32) == 0);
    CHECK(std::memcmp(plan.x_secret, expect.x_secret, 32) == 0);

    // ⛔ AN ALL-ZERO SEED IS REFUSED BEFORE A SINGLE DRAW — a dead RNG never becomes a live root.
    FakeRuntime rt2;
    uint8_t dead[32] = {};
    meshroute::RemoteSessionInstall p2{};
    CHECK_FALSE(mrfw::admin_runtime_prepare_root(rt2, dead, p2));
    CHECK(rt2.draws == 0);
    CHECK(rt2.commits == 0);

    // ⛔ AND A PARTIAL DRAW IS NOT PUBLISHED: refusing the eighth of ten refuses the whole preparation.
    FakeRuntime rt3;
    rt3.refuse_at = 7;
    meshroute::RemoteSessionInstall p3{};
    CHECK_FALSE(mrfw::admin_runtime_prepare_root(rt3, seed, p3));
    CHECK(rt3.commits == 0);
}

TEST_CASE("§radmin-5/R5 an ACL RESET needs no entropy at all, and makes every slot unusable") {
    meshroute::RemoteSessionInstall plan{};
    mrfw::admin_runtime_prepare_acl_reset(plan);
    CHECK(plan.set_acl);
    CHECK(plan.invalidate_all);
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
        CHECK(plan.epoch_set[i]);
        CHECK(plan.epoch[i] == 0);
        CHECK(plan.acl[i].role == meshroute::kRadminRoleEmpty);
    }
}

// =============================================================================================================
// THE ORDER, through the real services
// =============================================================================================================
TEST_CASE("§radmin-5/R6 `acl add`: prepare -> save -> commit, and the commit happens EXACTLY once") {
    FakeAclStore store;
    FakeRuntime  rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AclService svc(store, &live);
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);
    const auto r = svc.add(AdminIdState::ok, AclRole::owner, key);
    CHECK(r.ok);
    CHECK(r.changed);
    CHECK(store.saves == 1);
    CHECK(rt.draws == 1);                                  // the ONE new occupied slot
    CHECK(rt.commits == 1);
    CHECK(rt.last.set_acl);
    CHECK(rt.last.acl[0].role == meshroute::kRadminRoleOwner);
    CHECK(std::memcmp(rt.last.acl[0].ed_pub, key, 32) == 0);
    CHECK(rt.last.epoch_set[0]);
    CHECK(rt.last.epoch[0] != 0);
}

TEST_CASE("§radmin-5/R7 a FAILED SAVE commits NOTHING — the running image is preserved exactly") {
    FakeAclStore store;
    seed_owner_acl(store, 1);
    store.save_ok = false;
    FakeRuntime  rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AclService svc(store, &live);
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(200 - i);
    const auto r = svc.add(AdminIdState::ok, AclRole::operator_, key);
    CHECK_FALSE(r.ok);
    CHECK(r.err == AclErr::nv_save_failed);
    CHECK(store.saves == 1);                               // the write was ATTEMPTED…
    CHECK(rt.commits == 0);                                // ⛔ …and NOTHING was installed
    CHECK_FALSE(live.armed());                             // ⛔ and the plan was DISCARDED, not left armed
    // ⛔ A LATER `commit()` ON THE SAME OBJECT INSTALLS NOTHING: a discarded plan cannot be resurrected.
    live.commit();
    CHECK(rt.commits == 0);
}

TEST_CASE("§radmin-5/R8 a refused PREPARATION returns runtime_unavailable and never reaches the store") {
    FakeAclStore store;
    seed_owner_acl(store, 1);
    FakeRuntime  rt;
    rt.refuse_at = 0;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AclService svc(store, &live);
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(90 + i);
    const auto r = svc.add(AdminIdState::ok, AclRole::owner, key);
    CHECK_FALSE(r.ok);
    CHECK(r.err == AclErr::runtime_unavailable);
    CHECK(store.saves == 0);                               // ★ ZERO NV WRITES — the whole point of preparing first
    CHECK(rt.commits == 0);
    // ⛔ …AND IT IS A DISTINCT VERDICT from a failed medium and from a dead seed.
    CHECK(r.err != AclErr::nv_save_failed);
    CHECK(r.err != AclErr::store_io_failed);
    // ⛔⛔ A REFUSED PREPARATION LEAVES THE OBJECT **UNARMED**, so a later `commit()` installs NOTHING. Without
    //    this, a preparation that returned `false` while KEEPING its half-built plan would be invisible: the
    //    service happens not to commit on that path, so only an explicit commit can see the difference.
    CHECK_FALSE(live.armed());
    live.commit();
    CHECK(rt.commits == 0);
}

TEST_CASE("§radmin-5/R9 a NO-OP costs zero draws, zero saves, zero commits and zero invalidation") {
    FakeAclStore store;
    seed_owner_acl(store, 2);
    FakeRuntime  rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AclService svc(store, &live);
    const auto r = svc.set(/*slot=*/0, AclRole::owner);     // the SAME role it already holds
    CHECK(r.ok);
    CHECK_FALSE(r.changed);
    CHECK(store.saves == 0);
    CHECK(rt.draws == 0);
    CHECK(rt.commits == 0);
}

TEST_CASE("§radmin-5/R10 a role change invalidates ONLY that slot; the other rows keep their sessions") {
    FakeAclStore store;
    seed_owner_acl(store, 3);
    FakeRuntime  rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AclService svc(store, &live);
    const auto r = svc.set(/*slot=*/2, AclRole::operator_);
    CHECK(r.ok);
    CHECK(r.changed);
    CHECK(store.saves == 1);
    CHECK(rt.commits == 1);
    CHECK(rt.draws == 1);
    CHECK_FALSE(rt.last.epoch_set[0]);                      // ★ untouched
    CHECK_FALSE(rt.last.epoch_set[1]);
    CHECK(rt.last.epoch_set[2]);                            // ★ only the row that moved
    CHECK(rt.last.acl[2].role == meshroute::kRadminRoleOperator);
}

TEST_CASE("§radmin-5/R11 `acl remove` makes that slot unusable and draws nothing for it") {
    FakeAclStore store;
    seed_owner_acl(store, 2);
    FakeRuntime  rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AclService svc(store, &live);
    const auto r = svc.remove(/*slot=*/1);
    CHECK(r.ok);
    CHECK(rt.commits == 1);
    CHECK(rt.draws == 0);                                   // ⛔ a removal needs no entropy
    CHECK(rt.last.epoch_set[1]);
    CHECK(rt.last.epoch[1] == 0);
    CHECK(rt.last.acl[1].role == meshroute::kRadminRoleEmpty);
    CHECK_FALSE(rt.last.epoch_set[0]);
}

TEST_CASE("§radmin-5/R12 `admin-id` generate/rotate: the pair is prepared BEFORE the save, committed after") {
    FakeIdStore store;
    FakeSeed    seed;
    FakeRuntime rt;
    mrfw::AdminLiveInstall live(rt);
    mrfw::AdminIdService svc(store, seed, &live);
    const auto g = svc.generate();
    CHECK(g.ok);
    CHECK(store.saves == 1);
    CHECK(rt.commits == 1);
    CHECK(rt.draws == meshroute::kRadminAclSlots);
    CHECK(rt.last.set_root);
    CHECK(rt.last.invalidate_all);
    CHECK(std::memcmp(rt.last.ed_pub, g.ed_pub, 32) == 0);  // ★ the INSTALLED public half IS the reported one
    // ⛔ THE ACL IS NOT TOUCHED BY A ROOT CHANGE: the plan installs no rows.
    CHECK_FALSE(rt.last.set_acl);

    // A failed SAVE on a rotate: prepared, refused, nothing installed and the OLD root still runs.
    FakeIdStore s2; FakeSeed sd2; FakeRuntime rt2;
    s2.rec = store.rec; s2.state = mrnv::AdminIdRead::ok; s2.save_ok = false;
    mrfw::AdminLiveInstall live2(rt2);
    mrfw::AdminIdService svc2(s2, sd2, &live2);
    const auto rr = svc2.rotate();
    CHECK_FALSE(rr.ok);
    CHECK(rr.err == AdminIdErr::nv_save_failed);
    CHECK(rt2.commits == 0);
    CHECK_FALSE(live2.armed());

    // A refused PREPARATION on a rotate: zero writes, and the typed runtime verdict.
    FakeIdStore s3; FakeSeed sd3; FakeRuntime rt3;
    s3.rec = store.rec; s3.state = mrnv::AdminIdRead::ok;
    rt3.refuse_at = 3;
    mrfw::AdminLiveInstall live3(rt3);
    mrfw::AdminIdService svc3(s3, sd3, &live3);
    const auto r3 = svc3.rotate();
    CHECK_FALSE(r3.ok);
    CHECK(r3.err == AdminIdErr::runtime_unavailable);
    CHECK(s3.saves == 0);
    CHECK(rt3.commits == 0);
    // ⛔ …and the ROOT preparation leaves the object UNARMED too, so a later commit publishes nothing.
    CHECK_FALSE(live3.armed());
    live3.commit();
    CHECK(rt3.commits == 0);
}

TEST_CASE("§radmin-5/R13 a service with NO seam behaves exactly as Slice 3 left it") {
    FakeAclStore store;
    mrfw::AclService svc(store);                            // ⛔ no runtime at all
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 3);
    const auto r = svc.add(AdminIdState::ok, AclRole::owner, key);
    CHECK(r.ok);
    CHECK(r.changed);
    CHECK(store.saves == 1);
}

// =============================================================================================================
// The BOOT install
// =============================================================================================================
TEST_CASE("§radmin-5/R14 boot: only a valid root AND a valid ACL with an owner installs anything") {
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i * 3 + 5);
    FakeAclStore store;
    seed_owner_acl(store, 2);

    {   // ready
        FakeRuntime rt;
        const auto b = mrfw::admin_runtime_boot(rt, AdminIdState::ok, seed, AclState::ok, store.rec);
        CHECK(b.state == AdminSessionBootState::ready);
        CHECK(b.slots == 2);
        CHECK(rt.commits == 1);
        CHECK(rt.last.set_root);
        CHECK(rt.last.set_acl);
        CHECK(rt.last.acl[0].role == meshroute::kRadminRoleOwner);
        CHECK(std::strcmp(mrfw::admin_session_boot_state_name(b.state), "ready") == 0);
    }
    {   // every disabling prerequisite installs the CLEARED image rather than leaving a stale one
        // ⛔⛔ RE-DERIVED FOR [[B341]]: each arm now asserts BOTH halves — the bad one installs its CLEARED
        //    image AND the good one is installed anyway. The old form asserted `clear_root` on both loops, which
        //    is exactly the defect: it claimed a bad ACL should also throw the (valid) root away.
        for (AdminIdState s : { AdminIdState::absent, AdminIdState::invalid, AdminIdState::io_failed }) {
            FakeRuntime rt;
            const auto b = mrfw::admin_runtime_boot(rt, s, seed, AclState::ok, store.rec);
            CHECK(b.state == AdminSessionBootState::disabled);
            CHECK(b.slots == 0);
            CHECK(rt.commits == 1);
            CHECK(rt.last.clear_root);                       // the BAD half: cleared, never left stale
            CHECK(rt.last.invalidate_all);
            // ★ AND THE VALID ACL HALF IS STILL INSTALLED, with a prepared epoch per occupied row — so the
            //   `admin-id reset confirm` that follows a reboot makes the node accepting IMMEDIATELY.
            CHECK(rt.last.set_acl);
            CHECK(rt.last.acl[0].role == meshroute::kRadminRoleOwner);
            CHECK(rt.draws == 2);                            // two occupied rows, one epoch each; ⛔ no root draws
            CHECK(rt.last.epoch[0] != 0);
            CHECK(rt.last.epoch[1] != 0);
        }
        for (AclState s : { AclState::absent, AclState::invalid, AclState::io_failed }) {
            FakeRuntime rt;
            const auto b = mrfw::admin_runtime_boot(rt, AdminIdState::ok, seed, s, store.rec);
            CHECK(b.state == AdminSessionBootState::disabled);
            CHECK(rt.commits == 1);
            CHECK_FALSE(rt.last.clear_root);                 // ★ the VALID ROOT half is installed…
            CHECK(rt.last.set_root);
            CHECK(rt.draws == meshroute::kRadminAclSlots);
            CHECK(rt.last.set_acl);                          // …and the BAD ACL half installs its CLEARED image
            for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) {
                CHECK(rt.last.acl[i].role == meshroute::kRadminRoleEmpty);
                CHECK(rt.last.epoch[i] == 0);
            }
        }
    }
    {   // a valid ACL with NO OWNER is not readiness either — the same conjunction the verbs already use.
        // ⛔⛔ RE-DERIVED FOR [[B341]], the old expectation kept visible: this asserted `rt.draws == 0`, which was
        //    true only because the old boot installed NOTHING when the conjunction failed. The per-half boot
        //    installs the VALID half, so the ROOT half still mints its ten epochs — and the ACL half installs its
        //    CLEARED image with zero draws, because an ownerless record grants nothing. ⇒ 10, not 0, and it is
        //    derived from what the boot now does rather than accommodated to whatever it printed.
        // ⓘ `acl_content_valid` already REFUSES an occupied-but-ownerless record, so this fixture is a state the
        //   store cannot legitimately produce; it is kept as the direct proof of the owner term in the
        //   conjunction, and its `ok` label is the fixture's, not a claim about a real record.
        FakeAclStore ops;
        seed_owner_acl(ops, 1);
        ops.rec.rec[0].role = mrnv::kAclRoleOperator;
        FakeRuntime rt;
        const auto b = mrfw::admin_runtime_boot(rt, AdminIdState::ok, seed, AclState::ok, ops.rec);
        CHECK(b.state == AdminSessionBootState::disabled);
        CHECK(b.slots == 0);
        CHECK(rt.draws == meshroute::kRadminAclSlots);   // the ROOT half's ten; the ACL half draws NOTHING
        CHECK(rt.commits == 1);
        CHECK(rt.last.set_root);                          // ★ the valid half WAS installed…
        CHECK(rt.last.set_acl);
        CHECK(rt.last.acl[0].role == meshroute::kRadminRoleEmpty);   // …and the bad half installed its CLEARED image
    }
    {   // ⛔ A COLD-BOOT ENTROPY REFUSAL is its own state, installs nothing and reports through its own seam.
        FakeRuntime rt;
        rt.refuse_at = 4;
        const auto b = mrfw::admin_runtime_boot(rt, AdminIdState::ok, seed, AclState::ok, store.rec);
        CHECK(b.state == AdminSessionBootState::entropy_failed);
        CHECK(b.slots == 0);
        CHECK(rt.commits == 0);
        CHECK(rt.entropy_failures == 1);
        CHECK(std::strcmp(mrfw::admin_session_boot_state_name(b.state), "entropy_failed") == 0);
    }
}

// =============================================================================================================
// [[B341]] — LIVE ACTIVATION MUST COMPOSE ACROSS A REBOOT
//
// ★★★ THE PROPERTY, STATED ONCE: **after any sequence of durable successes, the running readiness and slot count
//     equal what a fresh boot on the same medium installs.** A console `ok` that describes state the node does
//     not hold is this codebase's recurring "a success that isn't", and here it was invisible — no line printed
//     after boot would have shown it.
// ⛔ THESE CASES DRIVE THE **REAL** `RemoteSessionState` through `remote_session_install`, exactly as
//    `Node::admin_session_commit` does. The counting `FakeRuntime` cannot see this defect: it records the LAST
//    plan, and the bug is that a sequence of individually-correct plans leaves the block wrong.
// =============================================================================================================
TEST_CASE("§radmin-5/R16 [[B341]] sequence B: generate -> REBOOT with no ACL -> `acl add owner` leaves the node ACCEPTING") {
    RealRuntime  rt;
    FakeIdStore  ids;
    FakeSeed     seed;
    FakeAclStore acl;
    {   // ---- session 1: provision the ROOT only. The ACL record is still absent. --------------------------
        mrfw::AdminLiveInstall live(rt);
        mrfw::AdminIdService svc(ids, seed, &live);
        const auto g = svc.generate();
        CHECK(g.ok);
        CHECK(ids.saves == 1);
    }
    // ---- REBOOT: `setup()` re-reads BOTH records and installs. ------------------------------------------
    {
        mrnv::AdminIdBlob idb{};
        const mrfw::AdminIdState idst = mrfw::admin_id_state_of(ids.load(idb), idb);
        mrnv::AclBlob ab{};
        const mrfw::AclState acst = mrfw::acl_state_of(acl.load(ab), ab);
        CHECK(idst == AdminIdState::ok);
        CHECK(acst == AclState::absent);
        const auto b = mrfw::admin_runtime_boot(rt, idst, idb.seed, acst, ab);
        CHECK(b.state == AdminSessionBootState::disabled);   // no ACL yet — correctly not accepting
        // ★ …but the VALID ROOT HALF IS INSTALLED. Before [[B341]]'s fix this was 0 and everything below failed.
        CHECK(rt.st.root_present == 1);
    }
    // ---- session 2: grant the first owner. It must ACTIVATE, not merely persist. -------------------------
    {
        mrfw::AdminLiveInstall live(rt);
        mrfw::AclService svc(acl, &live);
        uint8_t key[32];
        for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);
        const auto r = svc.add(AdminIdState::ok, AclRole::owner, key);
        CHECK(r.ok);
        CHECK(r.changed);
        CHECK(acl.saves == 1);
    }
    // ★★ THE PROPERTY: the durable success really did activate.
    CHECK(rt.st.root_present == 1);
    CHECK(rt.st.acl_occupied == 1);
    CHECK(rt.st.epoch[0] != 0);
    CHECK(meshroute::remote_session_accepting(rt.st));
    // …and a fresh boot on the SAME medium installs the SAME readiness — the two can never disagree again.
    {
        mrnv::AdminIdBlob idb{}; mrnv::AclBlob ab{};
        const auto b = mrfw::admin_runtime_boot(rt, mrfw::admin_id_state_of(ids.load(idb), idb), idb.seed,
                                                mrfw::acl_state_of(acl.load(ab), ab), ab);
        CHECK(b.state == AdminSessionBootState::ready);
        CHECK(b.slots == 1);
        CHECK(meshroute::remote_session_accepting(rt.st));
        CHECK(rt.st.acl_occupied == 1);
    }
}

TEST_CASE("§radmin-5/R17 [[B341]] sequence D: a valid owner ACL + a CORRUPT root -> REBOOT -> `admin-id reset confirm` leaves the node ACCEPTING") {
    RealRuntime  rt;
    FakeIdStore  ids;
    FakeSeed     seed;
    FakeAclStore acl;
    seed_owner_acl(acl, 1);                       // a valid ACL holding one OWNER
    ids.state = mrnv::AdminIdRead::invalid;       // …and a CORRUPT `/mradmid`
    // ---- REBOOT ------------------------------------------------------------------------------------------
    {
        mrnv::AdminIdBlob idb{};
        const mrfw::AdminIdState idst = mrfw::admin_id_state_of(ids.load(idb), idb);
        mrnv::AclBlob ab{};
        const mrfw::AclState acst = mrfw::acl_state_of(acl.load(ab), ab);
        CHECK(idst == AdminIdState::invalid);
        CHECK(acst == AclState::ok);
        const auto b = mrfw::admin_runtime_boot(rt, idst, idb.seed, acst, ab);
        CHECK(b.state == AdminSessionBootState::disabled);   // no usable root — correctly not accepting
        CHECK(rt.st.root_present == 0);
        // ★ …but the VALID ACL HALF IS INSTALLED, with a prepared epoch. Before the fix this was 0.
        CHECK(rt.st.acl_occupied == 1);
        CHECK(rt.st.epoch[0] != 0);
    }
    // ---- the operator recovers the root ------------------------------------------------------------------
    {
        mrfw::AdminLiveInstall live(rt);
        mrfw::AdminIdService svc(ids, seed, &live);
        const auto r = svc.recover();                       // `admin-id reset confirm`
        CHECK(r.ok);
        CHECK(ids.saves == 1);
    }
    // ★★ THE PROPERTY.
    CHECK(rt.st.root_present == 1);
    CHECK(rt.st.acl_occupied == 1);                          // ⛔ the ACL SURVIVED the root install
    CHECK(rt.st.epoch[0] != 0);                              // ⛔ …and no slot dropped to epoch 0
    CHECK(meshroute::remote_session_accepting(rt.st));
    {
        mrnv::AdminIdBlob idb{}; mrnv::AclBlob ab{};
        const auto b = mrfw::admin_runtime_boot(rt, mrfw::admin_id_state_of(ids.load(idb), idb), idb.seed,
                                                mrfw::acl_state_of(acl.load(ab), ab), ab);
        CHECK(b.state == AdminSessionBootState::ready);
        CHECK(b.slots == 1);
    }
}

TEST_CASE("§radmin-5/R18 [[B341]] a BAD half still installs ITS cleared half — no stale live ACL, no stale root") {
    RealRuntime rt;
    // Start from a fully provisioned running block…
    {
        meshroute::RemoteSessionInstall p{};
        p.set_root = true;
        for (int i = 0; i < 32; ++i) { p.x_secret[i] = static_cast<uint8_t>(i + 9); p.ed_pub[i] = static_cast<uint8_t>(i + 3); }
        p.set_acl = true;
        for (int i = 0; i < 32; ++i) p.acl[0].ed_pub[i] = static_cast<uint8_t>(i + 5);
        p.acl[0].role = meshroute::kRadminRoleOwner;
        p.epoch[0] = 0x77; p.epoch_set[0] = true;
        meshroute::remote_session_install(rt.st, p);
        CHECK(meshroute::remote_session_accepting(rt.st));
    }
    // …then boot with BOTH records gone. ⛔ Neither half may survive.
    mrnv::AclBlob empty{};
    mrnv::acl_blob_init(empty);
    uint8_t seed[32] = {};
    const auto b = mrfw::admin_runtime_boot(rt, AdminIdState::absent, seed, AclState::absent, empty);
    CHECK(b.state == AdminSessionBootState::disabled);
    CHECK(rt.st.root_present == 0);
    CHECK(rt.st.acl_occupied == 0);
    CHECK_FALSE(meshroute::remote_session_accepting(rt.st));
    for (uint8_t i = 0; i < meshroute::kRadminAclSlots; ++i) CHECK(rt.st.epoch[i] == 0);
}

TEST_CASE("§radmin-5/R15 the durable roles and the live image's roles are the SAME bytes") {
    CHECK(mrnv::kAclRoleEmpty    == meshroute::kRadminRoleEmpty);
    CHECK(mrnv::kAclRoleOperator == meshroute::kRadminRoleOperator);
    CHECK(mrnv::kAclRoleOwner    == meshroute::kRadminRoleOwner);
    CHECK(mrnv::kAclSlots        == meshroute::kRadminAclSlots);
}
