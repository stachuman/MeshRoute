// MeshRoute — test/test_firmware_admin_acl.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// §RADMIN slice 3 — the `/mracl` controller ACL (`src/firmware_admin_acl.h` + the record in `src/device_nv.h`).
// Design §§6.4-6.6; rulings R-RA-6 / R-RA-8 / R-RA-21. The rules under test are the design's own:
//   "Slot numbers are stable wire handles, not security identities. Empty entries are not compacted, an ACL-full
//    condition refuses loudly, and adding a duplicate public key is rejected."   (§6.5)
//   "The last owner cannot be removed or demoted remotely. A request cannot remove or demote its own
//    authenticating slot. ACL changes acknowledge success only after the new state is durable."   (§6.6)
//
// ★★★ EVERY CASE COUNTS WRITES — the `/mrteams` rule. "Identical material writes nothing" is measured as
//     `saves == 0`; "a full ACL never evicts" is measured as the ten stored rows being BYTE-IDENTICAL afterwards;
//     "holes stay holes" is measured as the SLOT NUMBERS the next add and the next listing use.
//
// ⛔ THE LIMIT OF THE CLAIM: the store is a FAKE. No NVS/LittleFS write, no flash WEAR, no reset-during-write.
//    `/mracl`'s power-cut behaviour is METAL-ONLY and is bench Part 55a's ([[B193]] qualified `/mrcfg` and
//    `/mrjoin`, ⛔ NOT this record). ⛔ Nothing here proves a two-store atomic commit, because there is none.
// ⛔ AND NOTHING HERE IS A REMOTE REQUEST. The `AclActor{present}` arms below are FUTURE-CALLER SERVICE tests for
//    the rule Slice 6 will reuse — ⛔ never an executed authenticated session, of which this slice has none.
#include "doctest.h"

#include <cstring>
#include <initializer_list>

#include "firmware_admin_acl.h"

using mrfw::AclErr;
using mrfw::AclRole;
using mrfw::AclState;
using mrfw::AdminIdState;

namespace {

struct FakeAclStore : mrfw::IAclStore {
    mrnv::AclBlob rec{};
    mrnv::AclBlob written{};
    mrnv::AclRead state = mrnv::AclRead::absent;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    bool deposit_garbage = true;

    mrnv::AclRead load(mrnv::AclBlob& out) override {
        ++loads;
        if (state != mrnv::AclRead::ok) {
            if (deposit_garbage) std::memset(&out, 0xA5, sizeof out);
            else                 out = rec;
            return state;
        }
        out = rec;
        return mrnv::AclRead::ok;
    }
    bool save(const mrnv::AclBlob& b) override {
        ++saves;
        written = b;
        if (!save_ok) return false;
        rec = b;
        state = mrnv::AclRead::ok;
        return true;
    }
    void empty_valid() { mrnv::acl_blob_init(rec); state = mrnv::AclRead::ok; }
    // Place one row directly, WITHOUT the service — so a case can build a state the verbs cannot reach.
    void place(uint8_t slot, uint8_t role, uint8_t key_fill) {
        rec.rec[slot] = mrnv::AclRow{};
        std::memset(rec.rec[slot].ed_pub, key_fill, 32);
        rec.rec[slot].role = role;
        rec.count = mrfw::acl_census(rec).count;
    }
};

void key_of(uint8_t fill, uint8_t out[32]) { std::memset(out, fill, 32); }

}  // namespace

// ================================================================================================================
// THE RECORD
// ================================================================================================================

TEST_CASE("radmin3/acl: the /mracl record layout is the frozen v1 contract on THIS ABI") {
    CHECK(sizeof(mrnv::AclRow) == 36);
    CHECK(alignof(mrnv::AclRow) == 1);
    CHECK(offsetof(mrnv::AclRow, ed_pub) == 0);
    CHECK(offsetof(mrnv::AclRow, role) == 32);
    CHECK(offsetof(mrnv::AclRow, reserved) == 33);
    CHECK(sizeof(mrnv::AclBlob) == 368);
    CHECK(alignof(mrnv::AclBlob) == 4);
    CHECK(offsetof(mrnv::AclBlob, magic) == 0);
    CHECK(offsetof(mrnv::AclBlob, version) == 4);
    CHECK(offsetof(mrnv::AclBlob, count) == 6);
    CHECK(offsetof(mrnv::AclBlob, rec) == 8);
    CHECK(mrnv::kAclMagic == 0x4D524C31u);
    CHECK(mrnv::kAclVersion == 1);
    CHECK(mrnv::kAclMagic != mrnv::kAdminIdMagic);
    // ★★ THE TEN IS THE CODEC'S TEN — the same fact the header's `static_assert` binds at compile time on every
    //    board. Asserted here too so a native reader sees the number and its source in one place.
    CHECK(mrnv::kAclSlots == 10);
    CHECK(mrnv::kAclSlots == meshroute::kRemoteSlotSessionMax + 1);
    CHECK(mrnv::kAclRoleEmpty == 0);
    CHECK(mrnv::kAclRoleOperator == 1);
    CHECK(mrnv::kAclRoleOwner == 2);
}

TEST_CASE("radmin3/acl: acl_blob_init stamps a VALID, EMPTY, wholly-zeroed record") {
    mrnv::AclBlob b;
    std::memset(&b, 0x5A, sizeof b);
    mrnv::acl_blob_init(b);
    CHECK(b.magic == mrnv::kAclMagic);
    CHECK(b.version == mrnv::kAclVersion);
    CHECK(b.count == 0);
    for (uint8_t i = 0; i < mrnv::kAclSlots; ++i) {
        CHECK(b.rec[i].role == mrnv::kAclRoleEmpty);
        CHECK(mrfw::admin_buf_all_zero(b.rec[i].ed_pub, 32));
        CHECK(mrfw::admin_buf_all_zero(b.rec[i].reserved, 3));
    }
    // ★ AN EMPTY ACL IS **VALID**, not corrupt: a provisioned-but-unowned node is an ordinary state.
    CHECK(mrfw::acl_content_valid(b));
    CHECK(mrfw::acl_state_of(mrnv::AclRead::ok, b) == AclState::ok);
}

TEST_CASE("radmin3/acl: the four-state classifier keeps absent, corrupt and unreadable APART") {
    mrnv::AclBlob b{};
    mrnv::acl_blob_init(b);
    const int N = static_cast<int>(sizeof b);
    CHECK(mrnv::acl_blob_state(b, N) == mrnv::AclRead::ok);
    CHECK(mrnv::acl_blob_state(b, mrnv::kSlotAbsent) == mrnv::AclRead::absent);
    CHECK(mrnv::acl_blob_state(b, -84) == mrnv::AclRead::invalid);
    CHECK(mrnv::acl_blob_state(b, N - 1) == mrnv::AclRead::invalid);
    CHECK(mrnv::acl_blob_state(b, N + 1) == mrnv::AclRead::invalid);
    { mrnv::AclBlob x = b; x.magic = mrnv::kAdminIdMagic; CHECK(mrnv::acl_blob_state(x, N) == mrnv::AclRead::invalid); }
    // ★ BOTH DIRECTIONS, and the LOW one is the discriminating half: a RANGE policy (`0 <= v <= kAclVersion`)
    //   would still refuse version 2, so only version 0 tells EQUALITY from a range. `/mrid`, `/mrpeers`,
    //   `/mrjoin`, `/mrteams` and `/mrui` all take equality for the same reason: there is no migration arm.
    { mrnv::AclBlob x = b; x.version = 2;                 CHECK(mrnv::acl_blob_state(x, N) == mrnv::AclRead::invalid); }
    { mrnv::AclBlob x = b; x.version = 0;                 CHECK(mrnv::acl_blob_state(x, N) == mrnv::AclRead::invalid); }
    { mrnv::SlotIo io; io.backend_failed = true;
      CHECK(mrnv::acl_blob_state(b, mrnv::kSlotAbsent, io) == mrnv::AclRead::io_failed);
      CHECK(mrnv::acl_blob_state(b, N, io) == mrnv::AclRead::io_failed); }
    { mrnv::SlotIo io; io.oversize = true;
      CHECK(mrnv::acl_blob_state(b, N, io) == mrnv::AclRead::invalid); }
}

TEST_CASE("radmin3/acl: the CONTENT policy refuses every ill-formed record, one rule at a time") {
    mrnv::AclBlob good{};
    mrnv::acl_blob_init(good);
    std::memset(good.rec[0].ed_pub, 0x11, 32); good.rec[0].role = mrnv::kAclRoleOwner;
    std::memset(good.rec[4].ed_pub, 0x22, 32); good.rec[4].role = mrnv::kAclRoleOperator;
    good.count = 2;
    CHECK(mrfw::acl_content_valid(good));            // ★ a HOLE at 1..3 is legal — rows are never compacted

    SUBCASE("an ILLEGAL role byte is corrupt, ⛔ not an ignorable row") {
        for (uint8_t bad : { uint8_t(3), uint8_t(4), uint8_t(0x80), uint8_t(0xFF) }) {
            mrnv::AclBlob b = good; b.rec[4].role = bad;
            CHECK_FALSE(mrfw::acl_content_valid(b));
        }
        // ★★ AND THE DISCRIMINATING SHAPE, without which this whole subcase passes for the WRONG REASON: above,
        //    an implementation that SKIPPED the illegal row would still be caught by the count rule (population 1
        //    vs `count` 2). Here `count` AGREES with the skip, so ONLY the role-domain rule can refuse it — which
        //    is the difference between "corrupt" and "an ignorable row".
        mrnv::AclBlob skip{};
        mrnv::acl_blob_init(skip);
        std::memset(skip.rec[0].ed_pub, 0x11, 32); skip.rec[0].role = mrnv::kAclRoleOwner;
        std::memset(skip.rec[4].ed_pub, 0x22, 32); skip.rec[4].role = 3;      // ⛔ outside the closed domain
        skip.count = 1;                                                        // ...and the count AGREES with a skip
        CHECK_FALSE(mrfw::acl_content_valid(skip));
        CHECK(mrfw::acl_state_of(mrnv::AclRead::ok, skip) == AclState::invalid);
    }
    SUBCASE("an occupied row with an ALL-ZERO key grants nothing to nobody") {
        mrnv::AclBlob b = good; std::memset(b.rec[4].ed_pub, 0, 32);
        CHECK_FALSE(mrfw::acl_content_valid(b));
    }
    SUBCASE("a 'deleted' row that KEPT its key is not empty") {
        mrnv::AclBlob b = good; b.rec[4].role = mrnv::kAclRoleEmpty;   // key left behind
        CHECK_FALSE(mrfw::acl_content_valid(b));
        // ★★ THE DISCRIMINATING SHAPE, for the reason the role subcase states: with `count` AGREEING that the row
        //    is gone, ONLY the "an empty row is ALL-ZERO" rule can refuse it. A record that kept a controller's
        //    key in a slot it reports as free is exactly the state that makes a later `add` reuse a live key's row.
        mrnv::AclBlob left{};
        mrnv::acl_blob_init(left);
        std::memset(left.rec[0].ed_pub, 0x11, 32); left.rec[0].role = mrnv::kAclRoleOwner;
        std::memset(left.rec[4].ed_pub, 0x22, 32); left.rec[4].role = mrnv::kAclRoleEmpty;   // ⛔ key left behind
        left.count = 1;                                                                       // count agrees
        CHECK_FALSE(mrfw::acl_content_valid(left));
        CHECK(mrfw::acl_state_of(mrnv::AclRead::ok, left) == AclState::invalid);
    }
    SUBCASE("a stray RESERVED byte is corrupt — the whole-record compare must stay sound") {
        for (int i = 0; i < 3; ++i) {
            mrnv::AclBlob b = good; b.rec[0].reserved[i] = 1;
            CHECK_FALSE(mrfw::acl_content_valid(b));
        }
        mrnv::AclBlob e = good; e.rec[7].reserved[2] = 1;   // …including on an EMPTY row
        CHECK_FALSE(mrfw::acl_content_valid(e));
    }
    SUBCASE("DUPLICATE keys are corrupt — one controller may not hold two slots") {
        mrnv::AclBlob b = good; std::memcpy(b.rec[4].ed_pub, b.rec[0].ed_pub, 32);
        CHECK_FALSE(mrfw::acl_content_valid(b));
    }
    SUBCASE("`count` must EQUAL the population — ⛔ never clamped, never a high-water mark") {
        for (uint16_t n : { uint16_t(0), uint16_t(1), uint16_t(3), uint16_t(10) }) {
            mrnv::AclBlob b = good; b.count = n;
            CHECK_FALSE(mrfw::acl_content_valid(b));
        }
        mrnv::AclBlob over = good; over.count = 11;   // beyond the array — ⛔ refused, never clamped to 10
        CHECK_FALSE(mrfw::acl_content_valid(over));
        mrnv::AclBlob way = good; way.count = 60000;
        CHECK_FALSE(mrfw::acl_content_valid(way));
    }
    SUBCASE("★ a NON-EMPTY ACL WITHOUT AN OWNER is corrupt") {
        mrnv::AclBlob b = good; b.rec[0].role = mrnv::kAclRoleOperator;
        CHECK_FALSE(mrfw::acl_content_valid(b));
        // …and the composed state turns that into `invalid`, ⛔ never `ok`
        CHECK(mrfw::acl_state_of(mrnv::AclRead::ok, b) == AclState::invalid);
    }
    SUBCASE("an EMPTY record needs no owner") {
        mrnv::AclBlob b{}; mrnv::acl_blob_init(b);
        CHECK(mrfw::acl_content_valid(b));
    }
}

TEST_CASE("radmin3/acl: the census counts ROWS, ⛔ never the stored count field") {
    mrnv::AclBlob b{};
    mrnv::acl_blob_init(b);
    std::memset(b.rec[0].ed_pub, 1, 32); b.rec[0].role = mrnv::kAclRoleOwner;
    std::memset(b.rec[3].ed_pub, 2, 32); b.rec[3].role = mrnv::kAclRoleOwner;
    std::memset(b.rec[9].ed_pub, 3, 32); b.rec[9].role = mrnv::kAclRoleOperator;
    b.count = 999;                                   // ⛔ a LYING count field …
    const mrfw::AclCensus c = mrfw::acl_census(b);
    CHECK(c.count == 3);                             // … which the census must ignore
    CHECK(c.owners == 2);
    CHECK(c.operators == 1);
}

TEST_CASE("radmin3/acl: the role vocabulary refuses everything outside the closed domain") {
    AclRole r = AclRole::empty;
    CHECK(mrfw::acl_role_assignable(mrnv::kAclRoleOperator, r)); CHECK(r == AclRole::operator_);
    CHECK(mrfw::acl_role_assignable(mrnv::kAclRoleOwner, r));    CHECK(r == AclRole::owner);
    // ⛔ `empty` IS A LEGAL STORED BYTE BUT NEVER AN ASSIGNABLE ROLE — only `remove` may empty a row.
    CHECK_FALSE(mrfw::acl_role_assignable(mrnv::kAclRoleEmpty, r));
    for (uint8_t bad : { uint8_t(3), uint8_t(9), uint8_t(0xFF) }) CHECK_FALSE(mrfw::acl_role_assignable(bad, r));
    CHECK(std::strcmp(mrfw::acl_role_name(AclRole::operator_), "operator") == 0);
    CHECK(std::strcmp(mrfw::acl_role_name(AclRole::owner), "owner") == 0);
    CHECK(std::strcmp(mrfw::acl_role_name(AclRole::empty), "") == 0);   // ⛔ an empty row is never listed
}

// ================================================================================================================
// THE SERVICE
// ================================================================================================================

TEST_CASE("radmin3/acl: the FIRST OWNER lands on an ABSENT record in EXACTLY ONE write") {
    FakeAclStore st;
    st.state = mrnv::AclRead::absent;
    mrfw::AclService svc(st);
    uint8_t k[32]; key_of(0x41, k);

    const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::owner, k);
    CHECK(r.ok);
    CHECK(r.slot == 0);
    CHECK(r.role == AclRole::owner);
    CHECK(r.changed);
    CHECK(st.saves == 1);                       // ★ the seed and the row land TOGETHER — never two writes
    CHECK(st.rec.magic == mrnv::kAclMagic);
    CHECK(st.rec.count == 1);
    CHECK(st.rec.rec[0].role == mrnv::kAclRoleOwner);
    CHECK(std::memcmp(st.rec.rec[0].ed_pub, k, 32) == 0);
    CHECK(mrfw::admin_buf_all_zero(st.rec.rec[0].reserved, 3));
    CHECK(mrfw::acl_content_valid(st.rec));
}

TEST_CASE("radmin3/acl: ★ an OPERATOR can never be the first credential") {
    for (mrnv::AclRead start : { mrnv::AclRead::absent, mrnv::AclRead::ok }) {
        FakeAclStore st;
        if (start == mrnv::AclRead::ok) st.empty_valid(); else st.state = mrnv::AclRead::absent;
        mrfw::AclService svc(st);
        uint8_t k[32]; key_of(0x42, k);
        const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::operator_, k);
        CHECK_FALSE(r.ok);
        CHECK(r.err == AclErr::first_owner_required);
        CHECK(st.saves == 0);
    }
    // …but once an owner exists, an operator is ordinary.
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t o[32], p[32]; key_of(0x51, o); key_of(0x52, p);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, o).ok);
    const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::operator_, p);
    CHECK(r.ok);
    CHECK(r.slot == 1);
    CHECK(st.saves == 2);
}

TEST_CASE("radmin3/acl: add REQUIRES a valid administration identity — a first owner cannot outrun the root") {
    struct Row { AdminIdState id; AclErr err; };
    for (Row row : { Row{ AdminIdState::absent,    AclErr::identity_absent },
                     Row{ AdminIdState::invalid,   AclErr::identity_invalid },
                     Row{ AdminIdState::io_failed, AclErr::identity_io_failed } }) {
        FakeAclStore st;
        st.state = mrnv::AclRead::absent;
        mrfw::AclService svc(st);
        uint8_t k[32]; key_of(0x61, k);
        const mrfw::AclResult r = svc.add(row.id, AclRole::owner, k);
        CHECK_FALSE(r.ok);
        CHECK(r.err == row.err);
        CHECK(st.saves == 0);
        CHECK(st.loads == 0);   // ⛔ the ACL is not even READ — the root gate is checked first
    }
}

TEST_CASE("radmin3/acl: add refuses a ZERO key, a DUPLICATE key and an illegal role — ZERO writes each") {
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t owner[32], zero[32] = {};
    key_of(0x71, owner);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, owner).ok);
    const int saves_after_setup = st.saves;

    { const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::owner, zero);
      CHECK_FALSE(r.ok); CHECK(r.err == AclErr::zero_key); }
    // ⛔ A DUPLICATE **REFUSES**, it does not answer "unchanged": granting the same key twice would give one
    //    controller two slots, and telling the operator "already there" hides the mistake he made.
    { const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::owner, owner);
      CHECK_FALSE(r.ok); CHECK(r.err == AclErr::duplicate_key); }
    { const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::operator_, owner);
      CHECK_FALSE(r.ok); CHECK(r.err == AclErr::duplicate_key); }   // ⛔ a different ROLE is still a duplicate KEY
    { const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::empty, owner);
      CHECK_FALSE(r.ok); CHECK(r.err == AclErr::bad_args); }
    CHECK(st.saves == saves_after_setup);
    CHECK(st.rec.count == 1);
}

TEST_CASE("radmin3/acl: add takes the LOWEST FREE slot and holes STAY holes (§6.5's stable wire handles)") {
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t k[6][32];
    for (int i = 0; i < 6; ++i) key_of(static_cast<uint8_t>(0x80 + i), k[i]);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[0]).ok);        // slot 0
    for (int i = 1; i < 5; ++i) CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[i]).ok);   // 1..4
    CHECK(st.rec.count == 5);

    // Free slot 2 …
    CHECK(svc.remove(2).ok);
    CHECK(st.rec.count == 4);
    CHECK(st.rec.rec[2].role == mrnv::kAclRoleEmpty);
    CHECK(mrfw::admin_buf_all_zero(st.rec.rec[2].ed_pub, 32));          // ⛔ ZEROED, not merely flagged
    // ⛔ AND NOTHING SHIFTED: slot 3 and slot 4 keep their numbers and their keys.
    CHECK(std::memcmp(st.rec.rec[3].ed_pub, k[3], 32) == 0);
    CHECK(std::memcmp(st.rec.rec[4].ed_pub, k[4], 32) == 0);

    // … and the next add REUSES it rather than appending at 5.
    const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::operator_, k[5]);
    CHECK(r.ok);
    CHECK(r.slot == 2);
    CHECK(st.rec.rec[5].role == mrnv::kAclRoleEmpty);
    CHECK(mrfw::acl_content_valid(st.rec));
}

TEST_CASE("radmin3/acl: a FULL ACL refuses LOUDLY and evicts NOTHING") {
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t k[11][32];
    for (int i = 0; i < 11; ++i) key_of(static_cast<uint8_t>(0x90 + i), k[i]);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[0]).ok);
    for (int i = 1; i < 10; ++i) CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[i]).ok);
    CHECK(st.rec.count == 10);
    const mrnv::AclBlob before = st.rec;
    const int saves_before = st.saves;

    const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::operator_, k[10]);
    CHECK_FALSE(r.ok);
    CHECK(r.err == AclErr::acl_full);
    CHECK(st.saves == saves_before);                                     // ⛔ ZERO writes
    CHECK(std::memcmp(&st.rec, &before, sizeof before) == 0);            // ⛔ ten rows BYTE-IDENTICAL
}

TEST_CASE("radmin3/acl: an INVALID or UNREADABLE store costs ZERO writes on every ordinary verb") {
    struct Row { mrnv::AclRead read; AclErr err; };
    for (Row row : { Row{ mrnv::AclRead::invalid,   AclErr::store_invalid },
                     Row{ mrnv::AclRead::io_failed, AclErr::store_io_failed } }) {
        uint8_t k[32]; key_of(0xB1, k);
        { FakeAclStore st; st.state = row.read; mrfw::AclService s(st);
          const mrfw::AclResult r = s.add(AdminIdState::ok, AclRole::owner, k);
          CHECK_FALSE(r.ok); CHECK(r.err == row.err); CHECK(st.saves == 0); }
        { FakeAclStore st; st.state = row.read; mrfw::AclService s(st);
          const mrfw::AclResult r = s.set(0, AclRole::owner);
          CHECK_FALSE(r.ok); CHECK(r.err == row.err); CHECK(st.saves == 0); }
        { FakeAclStore st; st.state = row.read; mrfw::AclService s(st);
          const mrfw::AclResult r = s.remove(0);
          CHECK_FALSE(r.ok); CHECK(r.err == row.err); CHECK(st.saves == 0); }
    }
    // ★★ AND THE CONTENT-INVALID CASE IS `store_invalid` TOO: a storage-ok record whose rows break a rule is not
    //    silently adopted, and ⛔ not partially adopted either.
    FakeAclStore st;
    mrnv::acl_blob_init(st.rec);
    std::memset(st.rec.rec[0].ed_pub, 1, 32); st.rec.rec[0].role = mrnv::kAclRoleOperator;   // no owner
    st.rec.count = 1;
    st.state = mrnv::AclRead::ok;
    mrfw::AclService s(st);
    uint8_t k[32]; key_of(0xB2, k);
    const mrfw::AclResult r = s.add(AdminIdState::ok, AclRole::owner, k);
    CHECK_FALSE(r.ok);
    CHECK(r.err == AclErr::store_invalid);
    CHECK(st.saves == 0);
}

TEST_CASE("radmin3/acl: set — the SAME role costs ZERO writes and says `unchanged`") {
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t o[32], p[32]; key_of(0xC1, o); key_of(0xC2, p);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, o).ok);
    CHECK(svc.add(AdminIdState::ok, AclRole::operator_, p).ok);
    const int saves = st.saves;

    const mrfw::AclResult same = svc.set(1, AclRole::operator_);
    CHECK(same.ok);
    CHECK_FALSE(same.changed);                     // ⛔ `unchanged`, never `updated`
    CHECK(same.slot == 1);
    CHECK(same.role == AclRole::operator_);
    CHECK(st.saves == saves);                      // ⛔ ZERO writes — identical material never touches flash

    const mrfw::AclResult moved = svc.set(1, AclRole::owner);
    CHECK(moved.ok);
    CHECK(moved.changed);
    CHECK(st.saves == saves + 1);
    CHECK(st.rec.rec[1].role == mrnv::kAclRoleOwner);
    CHECK(st.rec.count == 2);                      // ⛔ a ROLE change never moves the population
}

TEST_CASE("radmin3/acl: set/remove refuse an EMPTY or OUT-OF-RANGE slot") {
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t o[32]; key_of(0xD1, o);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, o).ok);
    const int saves = st.saves;
    for (uint8_t s : { uint8_t(1), uint8_t(5), uint8_t(9) }) {
        CHECK(svc.set(s, AclRole::owner).err == AclErr::slot_empty);
        CHECK(svc.remove(s).err == AclErr::slot_empty);
    }
    for (uint8_t s : { uint8_t(10), uint8_t(11), uint8_t(255) }) {
        CHECK(svc.set(s, AclRole::owner).err == AclErr::bad_args);
        CHECK(svc.remove(s).err == AclErr::bad_args);
    }
    // …and on an ABSENT record every slot is empty.
    FakeAclStore fresh; fresh.state = mrnv::AclRead::absent;
    mrfw::AclService s2(fresh);
    CHECK(s2.set(0, AclRole::owner).err == AclErr::slot_empty);
    CHECK(s2.remove(0).err == AclErr::slot_empty);
    CHECK(fresh.saves == 0);
    CHECK(st.saves == saves);
}

TEST_CASE("radmin3/acl: ★ THE LAST OWNER cannot be removed or demoted — by ANY caller") {
    SUBCASE("the only row") {
        FakeAclStore st; st.empty_valid();
        mrfw::AclService svc(st);
        uint8_t o[32]; key_of(0xE1, o);
        CHECK(svc.add(AdminIdState::ok, AclRole::owner, o).ok);
        const int saves = st.saves;
        CHECK(svc.remove(0).err == AclErr::last_owner);
        CHECK(svc.set(0, AclRole::operator_).err == AclErr::last_owner);
        CHECK(st.saves == saves);
        CHECK(st.rec.count == 1);
    }
    SUBCASE("one owner among operators") {
        FakeAclStore st; st.empty_valid();
        mrfw::AclService svc(st);
        uint8_t k[3][32];
        for (int i = 0; i < 3; ++i) key_of(static_cast<uint8_t>(0xE5 + i), k[i]);
        CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[0]).ok);
        CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[1]).ok);
        CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[2]).ok);
        CHECK(svc.remove(0).err == AclErr::last_owner);
        CHECK(svc.set(0, AclRole::operator_).err == AclErr::last_owner);
        // ★ TWO owners make the first one removable — the design's rotation: add the replacement, THEN drop the old.
        CHECK(svc.set(1, AclRole::owner).ok);
        const mrfw::AclResult gone = svc.remove(0);
        CHECK(gone.ok);
        CHECK(st.rec.rec[0].role == mrnv::kAclRoleEmpty);
        CHECK(mrfw::acl_content_valid(st.rec));
        // …and now slot 1 is the last owner again.
        CHECK(svc.remove(1).err == AclErr::last_owner);
    }
    SUBCASE("an OPERATOR is freely removable") {
        FakeAclStore st; st.empty_valid();
        mrfw::AclService svc(st);
        uint8_t k[2][32]; key_of(0xEA, k[0]); key_of(0xEB, k[1]);
        CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[0]).ok);
        CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[1]).ok);
        CHECK(svc.remove(1).ok);
        CHECK(st.rec.count == 1);
    }
}

TEST_CASE("radmin3/acl: ★ the SELF-SLOT rule (a FUTURE-CALLER service test, ⛔ not an executed remote request)") {
    // ⛔ THE LOCAL USB PATH SUPPLIES NO ACTING SLOT, so none of this is reachable from a console line today. It is
    //    tested HERE, at the service, so Slice 6's authenticated caller REUSES the rule instead of re-writing it.
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t k[3][32];
    for (int i = 0; i < 3; ++i) key_of(static_cast<uint8_t>(0xF1 + i), k[i]);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[0]).ok);
    CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[1]).ok);
    CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[2]).ok);
    const int saves = st.saves;

    mrfw::AclActor self0; self0.present = true; self0.slot = 0;
    CHECK(svc.remove(0, self0).err == AclErr::self_slot);                 // ⛔ cannot remove itself
    CHECK(svc.set(0, AclRole::operator_, self0).err == AclErr::self_slot); // ⛔ cannot DEMOTE itself
    CHECK(st.saves == saves);

    // ★ A SAME-ROLE self assignment is a legal NO-OP — it is not a demotion.
    const mrfw::AclResult noop = svc.set(0, AclRole::owner, self0);
    CHECK(noop.ok);
    CHECK_FALSE(noop.changed);
    CHECK(st.saves == saves);

    // ★ ANOTHER slot is unaffected by the actor.
    CHECK(svc.remove(2, self0).ok);
    // ★ A self PROMOTION is not a demotion; WHO may promote is Slice 6's authority question, ⛔ not this rule's.
    mrfw::AclActor self1; self1.present = true; self1.slot = 1;
    CHECK(svc.set(1, AclRole::operator_).ok);
    const mrfw::AclResult up = svc.set(1, AclRole::owner, self1);
    CHECK(up.ok);
    CHECK(up.changed);
    // ⛔ …and with NO actor the same operations are ordinary.
    CHECK(svc.remove(0).ok);
}

TEST_CASE("radmin3/acl: recovery reinitialises ONLY an INVALID record and GRANTS NOBODY") {
    SUBCASE("invalid -> an empty VALID record, one write") {
        FakeAclStore st; st.state = mrnv::AclRead::invalid;
        mrfw::AclService svc(st);
        const mrfw::AclResult r = svc.recover();
        CHECK(r.ok);
        CHECK(st.saves == 1);
        CHECK(mrfw::acl_content_valid(st.rec));
        CHECK(st.rec.count == 0);
        CHECK(mrfw::acl_census(st.rec).owners == 0);      // ⛔ recovery INVENTS NO OWNER (design §6.4)
    }
    SUBCASE("a content-invalid record is recoverable too") {
        FakeAclStore st;
        mrnv::acl_blob_init(st.rec);
        std::memset(st.rec.rec[0].ed_pub, 1, 32); st.rec.rec[0].role = mrnv::kAclRoleOperator;   // ownerless
        st.rec.count = 1;
        st.state = mrnv::AclRead::ok;
        mrfw::AclService svc(st);
        CHECK(svc.recover().ok);
        CHECK(st.saves == 1);
        CHECK(st.rec.count == 0);
    }
    struct Row { mrnv::AclRead read; bool seed_ok; AclErr err; };
    for (Row row : { Row{ mrnv::AclRead::absent,    false, AclErr::not_invalid },
                     Row{ mrnv::AclRead::ok,        true,  AclErr::not_invalid },
                     Row{ mrnv::AclRead::io_failed, false, AclErr::store_io_failed } }) {
        FakeAclStore st;
        st.state = row.read;
        if (row.seed_ok) mrnv::acl_blob_init(st.rec);
        mrfw::AclService svc(st);
        const mrfw::AclResult r = svc.recover();
        CHECK_FALSE(r.ok);
        CHECK(r.err == row.err);
        CHECK(st.saves == 0);
    }
}

TEST_CASE("radmin3/acl: a FAILED SAVE publishes no success — and claims nothing about the flash") {
    FakeAclStore st; st.empty_valid();
    mrfw::AclService svc(st);
    uint8_t k[32]; key_of(0x3A, k);
    st.save_ok = false;
    const mrfw::AclResult r = svc.add(AdminIdState::ok, AclRole::owner, k);
    CHECK_FALSE(r.ok);
    CHECK(r.err == AclErr::nv_save_failed);
    CHECK(st.saves == 1);                         // it TRIED exactly once
    CHECK(st.rec.count == 0);                     // the fake's committed state did not move …
    // ⚠⚠ … but the CANDIDATE reached the seam, which is the nRF52 remove-then-write exposure ([[B317]]) the
    //    console text must never contradict with "nothing was written".
    CHECK(st.written.count == 1);
    CHECK(std::memcmp(st.written.rec[0].ed_pub, k, 32) == 0);
}

TEST_CASE("radmin3/acl: read() leaves a VALID EMPTY record on every non-ok arm — never partial bytes") {
    for (mrnv::AclRead s : { mrnv::AclRead::absent, mrnv::AclRead::invalid, mrnv::AclRead::io_failed }) {
        FakeAclStore st; st.state = s; st.deposit_garbage = true;
        mrfw::AclService svc(st);
        mrnv::AclBlob out{};
        const AclState got = svc.read(out);
        CHECK(got != AclState::ok);
        CHECK(mrfw::acl_content_valid(out));       // ★ deterministic, printable, empty
        CHECK(out.count == 0);
        CHECK(mrfw::acl_census(out).count == 0);
        CHECK(st.saves == 0);
    }
}

TEST_CASE("radmin3/acl: the BOOT report VALIDATES and REPORTS — zero writes on all four states") {
    struct Row { mrnv::AclRead read; bool build; AclState want; uint8_t count, owners, operators; };
    for (Row row : { Row{ mrnv::AclRead::absent,    false, AclState::absent,    0, 0, 0 },
                     Row{ mrnv::AclRead::invalid,   false, AclState::invalid,   0, 0, 0 },
                     Row{ mrnv::AclRead::io_failed, false, AclState::io_failed, 0, 0, 0 },
                     Row{ mrnv::AclRead::ok,        true,  AclState::ok,        3, 2, 1 } }) {
        FakeAclStore st;
        st.state = row.read;
        if (row.build) {
            mrnv::acl_blob_init(st.rec);
            st.place(0, mrnv::kAclRoleOwner, 0x01);
            st.place(4, mrnv::kAclRoleOwner, 0x02);
            st.place(9, mrnv::kAclRoleOperator, 0x03);
        }
        mrfw::AclService svc(st);
        const mrfw::AclBoot b = svc.boot_report();
        CHECK(b.state == row.want);
        CHECK(b.census.count == row.count);
        CHECK(b.census.owners == row.owners);
        CHECK(b.census.operators == row.operators);
        CHECK(st.saves == 0);
        CHECK(st.loads == 1);
    }
}

TEST_CASE("radmin3/acl: stable slots SURVIVE a reload, holes included") {
    FakeAclStore st; st.empty_valid();
    {
        mrfw::AclService svc(st);
        uint8_t k[4][32];
        for (int i = 0; i < 4; ++i) key_of(static_cast<uint8_t>(0x21 + i), k[i]);
        CHECK(svc.add(AdminIdState::ok, AclRole::owner, k[0]).ok);
        for (int i = 1; i < 4; ++i) CHECK(svc.add(AdminIdState::ok, AclRole::operator_, k[i]).ok);
        CHECK(svc.remove(2).ok);
    }
    // A SECOND service over the SAME store — the reboot shape.
    mrfw::AclService again(st);
    mrnv::AclBlob out{};
    CHECK(again.read(out) == AclState::ok);
    CHECK(out.count == 3);
    CHECK(out.rec[0].role == mrnv::kAclRoleOwner);
    CHECK(out.rec[1].role == mrnv::kAclRoleOperator);
    CHECK(out.rec[2].role == mrnv::kAclRoleEmpty);      // ⛔ the hole is still a hole …
    CHECK(out.rec[3].role == mrnv::kAclRoleOperator);   // ⛔ … and slot 3 still has ITS number
}

TEST_CASE("radmin3/acl: provisioning readiness is a READ-ONLY CONJUNCTION of the two records") {
    mrfw::AclCensus owned;  owned.count = 1; owned.owners = 1;
    mrfw::AclCensus none;
    CHECK(mrfw::admin_provisioning_ready(AdminIdState::ok, AclState::ok, owned));
    // ⛔ EVERY term is load-bearing: a valid root with an empty ACL is NOT provisioned …
    CHECK_FALSE(mrfw::admin_provisioning_ready(AdminIdState::ok, AclState::ok, none));
    // … and an owned ACL without a valid root is not either.
    for (AdminIdState s : { AdminIdState::absent, AdminIdState::invalid, AdminIdState::io_failed })
        CHECK_FALSE(mrfw::admin_provisioning_ready(s, AclState::ok, owned));
    for (AclState s : { AclState::absent, AclState::invalid, AclState::io_failed })
        CHECK_FALSE(mrfw::admin_provisioning_ready(AdminIdState::ok, s, owned));
}
