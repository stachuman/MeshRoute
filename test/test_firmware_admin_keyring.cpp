// MeshRoute — test/test_firmware_admin_keyring.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// §RADMIN slice 4 — the `/mrmkeys` CONTROLLER management keyring (`src/firmware_admin_keyring.h` + the record in
// `src/device_nv.h`). Design §§6.2-6.4; rulings R-RA-29 / R-RA-30; register [[B312]] / [[B317]] / [[B321]].
//
// ★★★ WHY EVERY CASE COUNTS WRITES AND DRAWS INSTEAD OF ASSERTING A VERDICT — Slice 3's rule, restated because it
//     is the reason this file is shaped as it is: the verdict is a value the implementation CHOOSES, so a wrong
//     implementation can choose the right value while doing the wrong thing. A WRITE COUNT and a DRAW COUNT are
//     CONSEQUENCES. ⇒ "an unreadable store costs zero writes" is measured as `saves == 0`, and "a refused
//     confirmation costs no entropy" as `draws == 0` — ⛔ never as "it returned refused".
//
// ★★★ THE SEED→IDENTITY EXPECTATIONS ARE THE SHIPPED DERIVATION'S OWN KAT, ⛔ not a re-implementation: every case
//     that checks a returned public key compares it against `meshroute::identity_from_seed` computed IN THE TEST,
//     and one case pins the frozen golden vector `seed={1..32} -> ed_pub d4f8e6f2…` that `test_identity.cpp:104`
//     records against monocypher 4.0.2. ⇒ a derivation change breaks BOTH files, loudly.
//
// ⛔ THE LIMIT OF THE CLAIM: the store is a FAKE. No NVS/LittleFS write, no flash WEAR, no reset-during-write.
//    `/mrmkeys`' power-cut behaviour is METAL-ONLY (bench Part 56). The entropy source is a FAKE too: a counted
//    draw stream is ⛔ NOT a hardware or RF entropy proof, and [[B312]] stays OPEN.
// ⛔⛔ AND ONE HONEST GAP, MARKED RATHER THAN GLOSSED: the service's transient blob is a STACK frame, and reading
//    it after the call returns is undefined behaviour — so the wipe cannot be observed "in situ" by any host test.
//    That is exactly why `SecretWipeGuard` is a NAMED type: the case at the foot of this file drives the guard
//    itself over a carrier that OUTLIVES it, which is fully-defined and mutation-visible. The service's USE of that
//    guard is verified by inspection, ⛔ and this file does not claim otherwise.
#include "doctest.h"

#include <cstring>
#include <initializer_list>

#include "firmware_admin_keyring.h"
#include "identity.h"     // meshroute::identity_from_seed — the REAL derivation the service calls

using mrfw::MgmtKeyErr;
using mrfw::MgmtKeyState;

namespace {

// ---- the COUNTING store ---------------------------------------------------------------------------------
struct FakeKeyStore : mrfw::IMgmtKeyStore {
    mrnv::MgmtKeyBlob rec{};
    mrnv::MgmtKeyBlob written{};
    mrnv::MgmtKeyRead state = mrnv::MgmtKeyRead::absent;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    // ★ On a NON-ok read the fake deposits GARBAGE, deliberately: the real `read_slot` may leave a PARTIAL record
    //   behind, and the service must re-init or refuse rather than trust it (device_nv.h's warning).
    bool deposit_garbage = true;

    mrnv::MgmtKeyRead load(mrnv::MgmtKeyBlob& out) override {
        ++loads;
        if (state != mrnv::MgmtKeyRead::ok) {
            if (deposit_garbage) std::memset(&out, 0xA5, sizeof out);
            else                 out = rec;
            return state;
        }
        out = rec;
        return mrnv::MgmtKeyRead::ok;
    }
    bool save(const mrnv::MgmtKeyBlob& b) override {
        ++saves;
        written = b;
        if (!save_ok) return false;
        rec = b;
        state = mrnv::MgmtKeyRead::ok;
        return true;
    }
    void seed_empty() {
        mrnv::mgmt_key_blob_init(rec);
        state = mrnv::MgmtKeyRead::ok;
    }
    // Put slot `n` into a VALID occupied state holding `fill` in every seed byte, fixing `count` from the rows.
    void seed_slot(uint8_t n, uint8_t fill) {
        if (state != mrnv::MgmtKeyRead::ok) seed_empty();
        std::memset(rec.rec[n].seed, fill, sizeof rec.rec[n].seed);
        uint16_t pop = 0;
        for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) if (mrfw::mgmt_key_row_occupied(rec.rec[i])) ++pop;
        rec.count = pop;
    }
};

// ---- the COUNTING entropy source ------------------------------------------------------------------------
// ⛔ IT IS A FAKE DRAW STREAM AND ⛔ NOT A HARDWARE PROOF. What it measures is the SERVICE's discipline: how many
//    times it asks, and what it does with each answer.
struct FakeSeed : mrfw::IAdminSeedSource {
    int     draws  = 0;
    bool    answer = true;
    uint8_t byte   = 0x11;
    bool    zero_result = false;        // a "successful" draw that is all zeros — the dead-RNG shape
    bool fill(uint8_t out[32]) override {
        ++draws;
        std::memset(out, zero_result ? 0x00 : byte, 32);
        return answer;
    }
};

// ---- the FUTURE-CALLER in-use predicate -------------------------------------------------------------------
// ⚠ EVERY CASE THAT SETS `busy` IS A **FUTURE-CALLER SERVICE TEST**, ⛔ NOT an executed live-RPC test: Slice 4 has
//   no producer for this fact and the PRODUCTION binding answers false on every arm (Slice 8a is the first).
struct FakeUse : mrfw::IMgmtKeyUse {
    int  busy_slot = -1;
    bool busy_any  = false;
    bool slot_in_use(uint8_t slot) const override { return busy_slot >= 0 && slot == (uint8_t)busy_slot; }
    bool any_in_use() const override { return busy_any; }
};

const uint8_t kSelf[32] = {
    0xd4, 0xf8, 0xe6, 0xf2, 0x67, 0x27, 0x11, 0x77, 0xc1, 0x1d, 0x17, 0xd3, 0x98, 0x10, 0xd7, 0x47,
    0x16, 0x65, 0x72, 0xa1, 0xb6, 0xdb, 0x8e, 0x35, 0x23, 0x63, 0xd9, 0x78, 0x6e, 0xb0, 0x79, 0x83,
};

void pub_of(const uint8_t seed[32], uint8_t out[32]) {
    meshroute::Identity id{};
    meshroute::identity_from_seed(id, seed);
    std::memcpy(out, id.ed_pub, 32);
}

}  // namespace

// ================================================================================================================
// THE RECORD: layout, storage classification and content policy
// ================================================================================================================

TEST_CASE("radmin4/key: the /mrmkeys record layout is the frozen v1 contract on THIS ABI") {
    CHECK(sizeof(mrnv::MgmtKeyRow) == 36);
    CHECK(alignof(mrnv::MgmtKeyRow) == 1);
    CHECK(offsetof(mrnv::MgmtKeyRow, seed) == 0);
    CHECK(offsetof(mrnv::MgmtKeyRow, reserved) == 32);
    CHECK(sizeof(mrnv::MgmtKeyBlob) == 368);
    CHECK(alignof(mrnv::MgmtKeyBlob) == 4);
    CHECK(offsetof(mrnv::MgmtKeyBlob, magic) == 0);
    CHECK(offsetof(mrnv::MgmtKeyBlob, version) == 4);
    CHECK(offsetof(mrnv::MgmtKeyBlob, count) == 6);
    CHECK(offsetof(mrnv::MgmtKeyBlob, rec) == 8);
    CHECK(mrnv::kMgmtKeySlots == 10);
    CHECK(mrnv::kMgmtKeyMagic == 0x4D524D4Bu);      // 'MRMK'
    CHECK(mrnv::kMgmtKeyVersion == 1);
    // ⛔ ITS OWN MAGIC — a collision with any shipped record would make one store readable as another.
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kIdMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kPeersMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kJoinMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kTeamKeyMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kUiPresetMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kAdminIdMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kAclMagic);
    CHECK(mrnv::kMgmtKeyMagic != mrnv::kTargetMagic);
    // ★ The controller's ten is the GRAMMAR's and is INDEPENDENT of the target's ACL ten (design §6.2). They agree
    //   today; this case exists so that the day one of them moves, nothing silently follows the other.
    CHECK(mrnv::kMgmtKeySlots == 10);
    CHECK(mrnv::kAclSlots == 10);
}

TEST_CASE("radmin4/key: mgmt_key_blob_init stamps a VALID EMPTY keyring and mints NOTHING") {
    mrnv::MgmtKeyBlob b;
    std::memset(&b, 0x5A, sizeof b);
    mrnv::mgmt_key_blob_init(b);
    CHECK(b.magic == mrnv::kMgmtKeyMagic);
    CHECK(b.version == mrnv::kMgmtKeyVersion);
    CHECK(b.count == 0);
    for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) {
        CHECK(mrfw::admin_buf_all_zero(b.rec[i].seed, 32));
        CHECK(mrfw::admin_buf_all_zero(b.rec[i].reserved, 4));
        CHECK_FALSE(mrfw::mgmt_key_row_occupied(b.rec[i]));
    }
    CHECK(mrfw::mgmt_key_content_valid(b));
}

TEST_CASE("radmin4/key: the STORAGE classifier keeps its four arms apart") {
    mrnv::MgmtKeyBlob b{};
    mrnv::mgmt_key_blob_init(b);
    const int n = static_cast<int>(sizeof b);
    // ok
    CHECK(mrnv::mgmt_key_blob_state(b, n) == mrnv::MgmtKeyRead::ok);
    // absent — ⛔ and it is tested BEFORE the backend arms below, so the ORDER claim is exercised
    CHECK(mrnv::mgmt_key_blob_state(b, mrnv::kSlotAbsent) == mrnv::MgmtKeyRead::absent);
    // a dead backend outranks `absent` — otherwise a dead store reads as a fresh controller
    mrnv::SlotIo dead;
    dead.backend_failed = true;
    CHECK(mrnv::mgmt_key_blob_state(b, mrnv::kSlotAbsent, dead) == mrnv::MgmtKeyRead::io_failed);
    // an OVER-LENGTH record is invalid, ⛔ never ok on its valid prefix
    mrnv::SlotIo big;
    big.oversize = true;
    CHECK(mrnv::mgmt_key_blob_state(b, n, big) == mrnv::MgmtKeyRead::invalid);
    // short / wrong magic / wrong version
    CHECK(mrnv::mgmt_key_blob_state(b, n - 1) == mrnv::MgmtKeyRead::invalid);
    mrnv::MgmtKeyBlob wrong = b;
    wrong.magic = mrnv::kAclMagic;
    CHECK(mrnv::mgmt_key_blob_state(wrong, n) == mrnv::MgmtKeyRead::invalid);
    wrong = b;
    wrong.version = 2;
    CHECK(mrnv::mgmt_key_blob_state(wrong, n) == mrnv::MgmtKeyRead::invalid);
}

TEST_CASE("radmin4/key: the CONTENT policy refuses every non-canonical keyring") {
    mrnv::MgmtKeyBlob b{};
    mrnv::mgmt_key_blob_init(b);
    CHECK(mrfw::mgmt_key_content_valid(b));

    SUBCASE("count must equal the population, ⛔ never a high-water mark") {
        std::memset(b.rec[0].seed, 0x11, 32);
        CHECK_FALSE(mrfw::mgmt_key_content_valid(b));      // count still 0
        b.count = 1;
        CHECK(mrfw::mgmt_key_content_valid(b));
        b.count = 2;
        CHECK_FALSE(mrfw::mgmt_key_content_valid(b));
        b.count = mrnv::kMgmtKeySlots + 1;
        CHECK_FALSE(mrfw::mgmt_key_content_valid(b));
    }
    SUBCASE("a stray reserved byte makes the record invalid — on an EMPTY row too") {
        b.rec[3].reserved[2] = 1;
        CHECK_FALSE(mrfw::mgmt_key_content_valid(b));
    }
    SUBCASE("HOLES are legal and keep their slot numbers") {
        std::memset(b.rec[0].seed, 0x11, 32);
        std::memset(b.rec[7].seed, 0x22, 32);
        b.count = 2;
        CHECK(mrfw::mgmt_key_content_valid(b));
        CHECK_FALSE(mrfw::mgmt_key_row_occupied(b.rec[1]));
    }
    SUBCASE("two slots holding the SAME seed are ONE principal in two rows — invalid") {
        std::memset(b.rec[2].seed, 0x33, 32);
        std::memset(b.rec[5].seed, 0x33, 32);
        b.count = 2;
        CHECK_FALSE(mrfw::mgmt_key_content_valid(b));
        b.rec[5].seed[31] ^= 0x01;                          // one byte apart -> two principals
        CHECK(mrfw::mgmt_key_content_valid(b));
    }
}

TEST_CASE("radmin4/key: the COMPOSED state never collapses storage and content") {
    mrnv::MgmtKeyBlob b{};
    mrnv::mgmt_key_blob_init(b);
    CHECK(mrfw::mgmt_key_state_of(mrnv::MgmtKeyRead::ok, b) == MgmtKeyState::ok);
    CHECK(mrfw::mgmt_key_state_of(mrnv::MgmtKeyRead::absent, b) == MgmtKeyState::absent);
    CHECK(mrfw::mgmt_key_state_of(mrnv::MgmtKeyRead::invalid, b) == MgmtKeyState::invalid);
    CHECK(mrfw::mgmt_key_state_of(mrnv::MgmtKeyRead::io_failed, b) == MgmtKeyState::io_failed);
    // ★ storage `ok` + content BROKEN = invalid, and that is the composition's whole job
    b.count = 4;
    CHECK(mrfw::mgmt_key_state_of(mrnv::MgmtKeyRead::ok, b) == MgmtKeyState::invalid);
}

// ================================================================================================================
// THE SERVICE
// ================================================================================================================

TEST_CASE("radmin4/key: `show self` needs NO store read and survives every unreadable state") {
    for (mrnv::MgmtKeyRead st : { mrnv::MgmtKeyRead::ok, mrnv::MgmtKeyRead::absent,
                                  mrnv::MgmtKeyRead::invalid, mrnv::MgmtKeyRead::io_failed }) {
        FakeKeyStore store;
        FakeSeed seed;
        FakeUse use;
        store.state = st;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        const mrfw::MgmtKeyResult r = svc.show_self();
        CHECK(r.ok);
        CHECK(std::memcmp(r.ed_pub, kSelf, 32) == 0);
        CHECK(store.loads == 0);        // ⛔ the fact does not live in that record
        CHECK(store.saves == 0);
        CHECK(seed.draws == 0);
    }
}

TEST_CASE("radmin4/key: boot reports the four states READ-ONLY and never counts on a non-ok one") {
    SUBCASE("absent") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        const mrfw::MgmtKeyBoot b = svc.boot_report();
        CHECK(b.state == MgmtKeyState::absent);
        CHECK(b.count == 0);
        CHECK(store.saves == 0);
        CHECK(seed.draws == 0);
    }
    SUBCASE("ok with two keys") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_slot(0, 0x11);
        store.seed_slot(4, 0x22);
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        const mrfw::MgmtKeyBoot b = svc.boot_report();
        CHECK(b.state == MgmtKeyState::ok);
        CHECK(b.count == 2);
        CHECK(store.saves == 0);
    }
    SUBCASE("io_failed reports ZERO as a NON-ACTIVE count, ⛔ not as an empty flash") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.state = mrnv::MgmtKeyRead::io_failed;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        const mrfw::MgmtKeyBoot b = svc.boot_report();
        CHECK(b.state == MgmtKeyState::io_failed);
        CHECK(b.count == 0);
        CHECK(store.saves == 0);
    }
    SUBCASE("invalid") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        store.rec.count = 3;                      // content broken, storage fine
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.boot_report().state == MgmtKeyState::invalid);
        CHECK(store.saves == 0);
    }
}

TEST_CASE("radmin4/key: `list` and `show keyN` REFUSE an unreadable keyring rather than implying an empty one") {
    for (mrnv::MgmtKeyRead st : { mrnv::MgmtKeyRead::invalid, mrnv::MgmtKeyRead::io_failed }) {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.state = st;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        const mrfw::MgmtKeyList l = svc.list();
        CHECK_FALSE(l.ok);
        CHECK(l.count == 0);
        CHECK((l.err == MgmtKeyErr::store_invalid || l.err == MgmtKeyErr::store_io_failed));
        const mrfw::MgmtKeyResult r = svc.show(0);
        CHECK_FALSE(r.ok);
        // ⛔ AND `export` TOO — the arm that hands out a SECRET. Found by `--target=radmin4key` K24: without this
        //    row, deleting the state gate let the fake's 0xA5 garbage be returned as a real seed.
        const mrfw::MgmtKeySeed x = svc.export_seed(0);
        CHECK_FALSE(x.ok);
        CHECK((x.err == MgmtKeyErr::store_invalid || x.err == MgmtKeyErr::store_io_failed));
        CHECK(mrfw::admin_buf_all_zero(x.seed, 32));    // ⛔ not one byte of the unreadable record leaves
        CHECK(store.saves == 0);
        CHECK(seed.draws == 0);
    }
}

TEST_CASE("radmin4/key: `list` derives every occupied slot's PUBLIC identity from ONE load") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    store.seed_slot(0, 0x11);
    store.seed_slot(3, 0x22);
    store.seed_slot(9, 0x33);
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    const mrfw::MgmtKeyList l = svc.list();
    CHECK(l.ok);
    CHECK(l.count == 3);
    CHECK(store.loads == 1);            // ⛔ ONE load, ⛔ not one per slot
    CHECK(store.saves == 0);
    for (uint8_t k : { 0, 3, 9 }) {
        uint8_t want[32];
        pub_of(store.rec.rec[k].seed, want);
        CHECK(l.rec[k].occupied);
        CHECK(std::memcmp(l.rec[k].ed_pub, want, 32) == 0);
    }
    for (uint8_t k : { 1, 2, 4, 5, 6, 7, 8 }) CHECK_FALSE(l.rec[k].occupied);
}

TEST_CASE("radmin4/key: `show keyN` — the ten slots are addressable and an empty one is not_found") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    for (uint8_t k = 0; k < mrnv::kMgmtKeySlots; ++k) store.seed_slot(k, static_cast<uint8_t>(0x40 + k));
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    for (uint8_t k = 0; k < mrnv::kMgmtKeySlots; ++k) {
        const mrfw::MgmtKeyResult r = svc.show(k);
        CHECK(r.ok);
        CHECK(r.slot == k);
        uint8_t want[32];
        pub_of(store.rec.rec[k].seed, want);
        CHECK(std::memcmp(r.ed_pub, want, 32) == 0);
    }
    CHECK(svc.show(mrnv::kMgmtKeySlots).err == MgmtKeyErr::bad_args);
    CHECK(svc.show(200).err == MgmtKeyErr::bad_args);
    store.rec.rec[5] = mrnv::MgmtKeyRow{};
    store.rec.count = mrnv::kMgmtKeySlots - 1;
    CHECK(svc.show(5).err == MgmtKeyErr::not_found);
    CHECK(store.saves == 0);
}

TEST_CASE("radmin4/key: `generate` mints ONLY into a vacant slot, at a cost of exactly ONE write") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    store.seed_empty();
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    const mrfw::MgmtKeyResult r = svc.generate(2);
    CHECK(r.ok);
    CHECK(r.slot == 2);
    CHECK(seed.draws == 1);
    CHECK(store.saves == 1);
    CHECK(store.rec.count == 1);
    uint8_t want[32];
    pub_of(store.rec.rec[2].seed, want);
    CHECK(std::memcmp(r.ed_pub, want, 32) == 0);
    // ⛔ an occupied slot is REFUSED, never replaced — and it costs zero draws and zero writes
    const int saves = store.saves, draws = seed.draws;
    const mrfw::MgmtKeyResult again = svc.generate(2);
    CHECK(again.err == MgmtKeyErr::occupied);
    CHECK(store.saves == saves);
    CHECK(seed.draws == draws);
}

TEST_CASE("radmin4/key: `generate` on an ABSENT record seeds and fills in ONE write") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;    // state == absent
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    CHECK(svc.generate(0).ok);
    CHECK(store.saves == 1);                            // ⛔ the header and the row land TOGETHER, never twice
    CHECK(store.rec.magic == mrnv::kMgmtKeyMagic);
    CHECK(store.rec.count == 1);
}

TEST_CASE("radmin4/key: every REFUSED state costs ZERO writes and ZERO draws") {
    SUBCASE("io_failed") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.state = mrnv::MgmtKeyRead::io_failed;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.generate(0).err == MgmtKeyErr::store_io_failed);
        CHECK(store.saves == 0);
        CHECK(seed.draws == 0);
    }
    SUBCASE("invalid") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.state = mrnv::MgmtKeyRead::invalid;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.generate(0).err == MgmtKeyErr::store_invalid);
        CHECK(store.saves == 0);
        CHECK(seed.draws == 0);
    }
}

TEST_CASE("radmin4/key: a REFUSED or DEAD draw mints nothing and publishes no key") {
    SUBCASE("the provider says NO") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        seed.answer = false;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        const mrfw::MgmtKeyResult r = svc.generate(0);
        CHECK(r.err == MgmtKeyErr::entropy_failed);
        CHECK(seed.draws == 1);                          // it ASKED
        CHECK(store.saves == 0);
        CHECK(mrfw::admin_buf_all_zero(r.ed_pub, 32));   // ⛔ no key is published on a refusal
    }
    SUBCASE("an ALL-ZERO 'successful' draw — the dead-RNG shape") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        seed.zero_result = true;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.generate(0).err == MgmtKeyErr::entropy_failed);
        CHECK(seed.draws == 1);
        CHECK(store.saves == 0);
        CHECK(store.rec.count == 0);
    }
}

TEST_CASE("radmin4/key: `import` takes the caller's seed, refuses the degenerate one, and never half-writes") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    store.seed_empty();
    // ⓘ SELF IS DELIBERATELY A DIFFERENT PRINCIPAL HERE: `kSelf` IS `identity_from_seed({1..32}).ed_pub`, so using
    //   it as this node's own identity would make the import below a legitimate `duplicate` — which is the NEXT
    //   case's subject, not this one's.
    uint8_t other_self[32] = {};
    other_self[0] = 0x99;
    mrfw::MgmtKeyService svc(store, seed, use, other_self);
    uint8_t s[32];
    for (int i = 0; i < 32; ++i) s[i] = static_cast<uint8_t>(i + 1);
    const mrfw::MgmtKeyResult r = svc.import(7, s);
    CHECK(r.ok);
    CHECK(r.slot == 7);
    CHECK(seed.draws == 0);                              // ⛔ import NEVER draws
    CHECK(store.saves == 1);
    CHECK(std::memcmp(store.rec.rec[7].seed, s, 32) == 0);
    // ★ THE FROZEN GOLDEN VECTOR (test_identity.cpp:104): seed={1..32} -> EXACTLY this public key. The chain
    //   "service -> identity_from_seed -> monocypher 4.0.2" is pinned here as a literal, not as a re-derivation.
    CHECK(std::memcmp(r.ed_pub, kSelf, 32) == 0);
    uint8_t zero[32] = {};
    const mrfw::MgmtKeyResult bad = svc.import(1, zero);
    CHECK(bad.err == MgmtKeyErr::bad_material);
    CHECK(store.saves == 1);                             // unchanged
}

TEST_CASE("radmin4/key: a DUPLICATE derived identity is refused — against SELF and against another slot") {
    // ★ `kSelf` IS `identity_from_seed({1..32}).ed_pub` (the frozen golden vector), so importing that seed
    //   reproduces this node's own messaging identity — exactly the collapse design §6.2 forbids.
    uint8_t s1[32];
    for (int i = 0; i < 32; ++i) s1[i] = static_cast<uint8_t>(i + 1);
    uint8_t check[32];
    pub_of(s1, check);
    CHECK(std::memcmp(check, kSelf, 32) == 0);         // the KAT chain, asserted rather than assumed

    SUBCASE("against SELF") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.import(0, s1).err == MgmtKeyErr::duplicate);
        CHECK(store.saves == 0);
    }
    SUBCASE("against ANOTHER SLOT") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        uint8_t other[32] = {};
        other[0] = 0x99;
        mrfw::MgmtKeyService svc(store, seed, use, other);
        CHECK(svc.import(4, s1).ok);
        const int saves = store.saves;
        CHECK(svc.import(6, s1).err == MgmtKeyErr::duplicate);
        CHECK(store.saves == saves);
    }
    SUBCASE("a DEAD RNG that returns the SAME bytes twice cannot fill two slots") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        uint8_t other[32] = {};
        other[0] = 0x99;
        mrfw::MgmtKeyService svc(store, seed, use, other);
        CHECK(svc.generate(0).ok);
        const int saves = store.saves;
        CHECK(svc.generate(1).err == MgmtKeyErr::duplicate);   // the fake repeats `byte` on every draw
        CHECK(store.saves == saves);
    }
}

TEST_CASE("radmin4/key: `export` returns the SEED of an occupied slot and writes nothing") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    store.seed_slot(8, 0x77);
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    const mrfw::MgmtKeySeed s = svc.export_seed(8);
    CHECK(s.ok);
    CHECK(s.slot == 8);
    for (int i = 0; i < 32; ++i) CHECK(s.seed[i] == 0x77);
    CHECK(store.saves == 0);
    CHECK(svc.export_seed(3).err == MgmtKeyErr::not_found);
    CHECK(svc.export_seed(99).err == MgmtKeyErr::bad_args);
    CHECK(store.saves == 0);
}

TEST_CASE("radmin4/key: `remove` frees ONE slot, leaves a HOLE and refuses an IN-USE key") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    store.seed_slot(1, 0x11);
    store.seed_slot(2, 0x22);
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    const mrfw::MgmtKeyResult r = svc.remove(1);
    CHECK(r.ok);
    CHECK(r.slot == 1);
    CHECK(store.saves == 1);
    CHECK(store.rec.count == 1);
    CHECK(mrfw::admin_buf_all_zero(store.rec.rec[1].seed, 32));      // zeroed IN PLACE
    CHECK(mrfw::mgmt_key_row_occupied(store.rec.rec[2]));            // ⛔ NOT compacted — slot 2 keeps its number
    CHECK(svc.remove(1).err == MgmtKeyErr::not_found);

    // ⚠ FUTURE-CALLER SERVICE TEST — no live RPC exists in Slice 4.
    FakeUse busy;
    busy.busy_slot = 2;
    mrfw::MgmtKeyService svc2(store, seed, busy, kSelf);
    const int loads = store.loads, saves = store.saves;
    CHECK(svc2.remove(2).err == MgmtKeyErr::in_use);
    CHECK(store.saves == saves);
    CHECK(store.loads == loads);                                     // ⛔ refused BEFORE any read
}

TEST_CASE("radmin4/key: `reset` recovers ONLY a corrupt record and is not a bulk-delete escape") {
    SUBCASE("invalid -> an EMPTY valid keyring, one write") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        store.rec.count = 5;                                // content broken
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.recover().ok);
        CHECK(store.saves == 1);
        CHECK(store.rec.count == 0);
        CHECK(mrfw::mgmt_key_content_valid(store.rec));
        CHECK(seed.draws == 0);                             // ⛔ recovery MINTS NOTHING
    }
    SUBCASE("ok and absent are not_invalid, at zero writes") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.recover().err == MgmtKeyErr::not_invalid);       // absent
        store.seed_slot(0, 0x11);
        CHECK(svc.recover().err == MgmtKeyErr::not_invalid);       // ok
        CHECK(store.saves == 0);
    }
    SUBCASE("io_failed refuses EVEN the recovery verb") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.state = mrnv::MgmtKeyRead::io_failed;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.recover().err == MgmtKeyErr::store_io_failed);
        CHECK(store.saves == 0);
    }
    SUBCASE("ANY in-use key refuses the reset — a FUTURE-CALLER service test") {
        FakeKeyStore store; FakeSeed seed; FakeUse use;
        store.seed_empty();
        store.rec.count = 5;
        use.busy_any = true;
        mrfw::MgmtKeyService svc(store, seed, use, kSelf);
        CHECK(svc.recover().err == MgmtKeyErr::in_use);
        CHECK(store.saves == 0);
        CHECK(store.loads == 0);
    }
}

TEST_CASE("radmin4/key: a FAILED save publishes no success and activates nothing") {
    FakeKeyStore store; FakeSeed seed; FakeUse use;
    store.seed_empty();
    store.save_ok = false;
    mrfw::MgmtKeyService svc(store, seed, use, kSelf);
    const mrfw::MgmtKeyResult r = svc.generate(0);
    CHECK(r.err == MgmtKeyErr::nv_save_failed);
    CHECK(store.saves == 1);                                 // it TRIED exactly once
    CHECK(store.rec.count == 0);                             // and the stored record did not move
    CHECK(mrfw::admin_buf_all_zero(r.ed_pub, 32));
    // ⛔ AND THE FAILURE DOES NOT CLAIM THE OLD BYTES SURVIVED — the fake's `written` shows what was attempted.
    CHECK(store.written.count == 1);
}

TEST_CASE("radmin4/key: SecretWipeGuard zeroes a LIVE carrier that outlives it (⛔ never a dangling frame)") {
    // ★★★ THE ONE FULLY-DEFINED OBSERVATION OF THE WIPE, and the reason `SecretWipeGuard` is a NAMED type: the
    //     carrier is declared OUTSIDE the scope, so the bytes are read while the object is still alive.
    mrnv::MgmtKeyBlob live{};
    std::memset(&live, 0xC3, sizeof live);
    CHECK_FALSE(mrfw::admin_buf_all_zero(reinterpret_cast<const uint8_t*>(&live), sizeof live));
    { mrfw::SecretWipeGuard<mrnv::MgmtKeyBlob> g{live}; }
    CHECK(mrfw::admin_buf_all_zero(reinterpret_cast<const uint8_t*>(&live), sizeof live));
}
