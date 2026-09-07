// MeshRoute — test/test_firmware_admin_targets.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// §RADMIN slice 4 — the `/mrtargets` CONTROLLER target book (`src/firmware_admin_targets.h` + the record in
// `src/device_nv.h`). Design §§6.2-6.4; rulings R-RA-12 / R-RA-28 / R-RA-29 / R-RA-30; register [[B317]].
//
// ★★★ WRITE COUNTS, NOT VERDICTS — the sibling keyring suite's rule and its reason.
//
// ★★★ THE FINGERPRINT EXPECTATIONS ARE SLICE 3'S FROZEN INDEPENDENT LITERALS, REUSED VERBATIM. They are the same
//     values `test/test_firmware_admin_identity.cpp:102-111` carries (that file's `kFpRef`, which is in an
//     anonymous namespace and therefore TU-private), produced BEFORE the helper existed by CPython 3.11.2's
//     `hashlib.blake2b(digest_size=64)` outside both repositories, itself anchored on the RFC 7693 Appendix A
//     known-answer vector for BLAKE2b-512("abc"). ⛔ NO expected fingerprint here was produced by the code under
//     test, and the corrupted-literal control below proves the comparison discriminates.
//
// ⛔ THE LIMIT OF THE CLAIM: the store is a FAKE. No NVS/LittleFS write, no flash WEAR, no reset-during-write;
//    `/mrtargets`' power-cut behaviour is METAL-ONLY (bench Part 56). And a stored path proves ⛔ NOTHING about
//    reachability — that is Slice 8b's question, at send time.
#include "doctest.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "firmware_admin_targets.h"

using mrfw::TargetErr;
using mrfw::TargetState;

namespace {

struct FakeTargetStore : mrfw::ITargetStore {
    mrnv::TargetBlob rec{};
    mrnv::TargetBlob written{};
    mrnv::TargetRead state = mrnv::TargetRead::absent;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    bool deposit_garbage = true;

    mrnv::TargetRead load(mrnv::TargetBlob& out) override {
        ++loads;
        if (state != mrnv::TargetRead::ok) {
            if (deposit_garbage) std::memset(&out, 0xA5, sizeof out);
            else                 out = rec;
            return state;
        }
        out = rec;
        return mrnv::TargetRead::ok;
    }
    bool save(const mrnv::TargetBlob& b) override {
        ++saves;
        written = b;
        if (!save_ok) return false;
        rec = b;
        state = mrnv::TargetRead::ok;
        return true;
    }
    void seed_empty() { mrnv::target_blob_init(rec); state = mrnv::TargetRead::ok; }
};

// ⚠ EVERY CASE THAT SETS `busy` IS A **FUTURE-CALLER SERVICE TEST**, ⛔ NOT an executed live-RPC test.
struct FakeTargetUse : mrfw::ITargetUse {
    int  busy_slot = -1;
    bool busy_any  = false;
    bool slot_in_use(uint8_t slot) const override { return busy_slot >= 0 && slot == (uint8_t)busy_slot; }
    bool any_in_use() const override { return busy_any; }
};

// Slice 3's FROZEN INDEPENDENT reference pairs (see the head note). ⛔ Not regenerated here.
struct FpVec { const char* pub_hex; const char* fp_hex; };
const FpVec kFpRef[] = {
    { "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "83b5ade6991342ed" },
    { "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "5c52920a7263e39d" },
    { "e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "d70a8754d94c8f21" },
};
void unhex32(const char* h, uint8_t out[32]) {
    for (int i = 0; i < 32; ++i) {
        auto nib = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            return static_cast<uint8_t>(c - 'a' + 10);
        };
        out[i] = static_cast<uint8_t>((nib(h[2 * i]) << 4) | nib(h[2 * i + 1]));
    }
}

mrfw::TargetSpec spec_of(const char* label, uint8_t key_fill, uint32_t hash,
                         uint8_t h0 = 0, uint8_t h1 = 0, uint8_t h2 = 0) {
    mrfw::TargetSpec s;
    std::memset(s.admin_pub, key_fill, sizeof s.admin_pub);
    s.key_hash32 = hash;
    const size_t n = std::strlen(label);
    std::memcpy(s.label, label, n);
    s.label_len = static_cast<uint8_t>(n);
    if (h0) { s.hops[s.hop_count++] = h0; }
    if (h1) { s.hops[s.hop_count++] = h1; }
    if (h2) { s.hops[s.hop_count++] = h2; }
    return s;
}

}  // namespace

// ================================================================================================================
// THE RECORD
// ================================================================================================================

TEST_CASE("radmin4/targets: the /mrtargets record layout is the frozen v1 contract on THIS ABI") {
    CHECK(sizeof(mrnv::TargetRow) == 64);
    CHECK(alignof(mrnv::TargetRow) == 4);
    CHECK(offsetof(mrnv::TargetRow, admin_pub) == 0);
    CHECK(offsetof(mrnv::TargetRow, key_hash32) == 32);
    CHECK(offsetof(mrnv::TargetRow, hops) == 36);
    CHECK(offsetof(mrnv::TargetRow, hop_count) == 40);
    CHECK(offsetof(mrnv::TargetRow, label_len) == 41);
    CHECK(offsetof(mrnv::TargetRow, label) == 42);
    CHECK(offsetof(mrnv::TargetRow, flags) == 58);
    CHECK(offsetof(mrnv::TargetRow, reserved) == 59);
    CHECK(sizeof(mrnv::TargetBlob) == 2056);
    CHECK(alignof(mrnv::TargetBlob) == 4);
    CHECK(offsetof(mrnv::TargetBlob, rec) == 8);
    CHECK(mrnv::kTargetSlots == 32);
    CHECK(mrnv::kTargetHopMax == 3);
    CHECK(mrnv::kTargetFlagOccupied == 0x01);
    CHECK(mrnv::kTargetMagic == 0x4D525442u);       // 'MRTB'
    CHECK(mrnv::kTargetVersion == 1);
    CHECK(mrnv::kTargetMagic != mrnv::kMgmtKeyMagic);
    CHECK(mrnv::kTargetMagic != mrnv::kPeersMagic);
    CHECK(mrnv::kTargetMagic != mrnv::kAclMagic);
    CHECK(mrnv::kTargetMagic != mrnv::kAdminIdMagic);
    // ⛔ NOT `/mrpeers`' capacity: that book EVICTS and this one refuses `full`.
    CHECK(mrnv::kTargetSlots != (sizeof(mrnv::PeerBlob::rec) / sizeof(mrnv::PeerRec)));
}

TEST_CASE("radmin4/targets: the STORAGE classifier keeps its four arms apart") {
    mrnv::TargetBlob b{};
    mrnv::target_blob_init(b);
    const int n = static_cast<int>(sizeof b);
    CHECK(mrnv::target_blob_state(b, n) == mrnv::TargetRead::ok);
    CHECK(mrnv::target_blob_state(b, mrnv::kSlotAbsent) == mrnv::TargetRead::absent);
    mrnv::SlotIo dead; dead.backend_failed = true;
    CHECK(mrnv::target_blob_state(b, mrnv::kSlotAbsent, dead) == mrnv::TargetRead::io_failed);
    mrnv::SlotIo big;  big.oversize = true;
    CHECK(mrnv::target_blob_state(b, n, big) == mrnv::TargetRead::invalid);
    CHECK(mrnv::target_blob_state(b, n - 1) == mrnv::TargetRead::invalid);
    mrnv::TargetBlob w = b; w.magic = mrnv::kMgmtKeyMagic;
    CHECK(mrnv::target_blob_state(w, n) == mrnv::TargetRead::invalid);
    w = b; w.version = 2;
    CHECK(mrnv::target_blob_state(w, n) == mrnv::TargetRead::invalid);
}

TEST_CASE("radmin4/targets: the CONTENT policy refuses every non-canonical book") {
    mrnv::TargetBlob b{};
    mrnv::target_blob_init(b);
    CHECK(mrfw::target_content_valid(b));

    // one canonical occupied row to perturb
    mrnv::TargetRow good{};
    std::memset(good.admin_pub, 0x11, 32);
    good.key_hash32 = 0xAABBCCDDu;
    good.label[0] = 'a'; good.label[1] = '1'; good.label_len = 2;
    good.hops[0] = 5; good.hop_count = 1;
    good.flags = mrnv::kTargetFlagOccupied;
    b.rec[0] = good; b.count = 1;
    CHECK(mrfw::target_content_valid(b));

    SUBCASE("an EMPTY row that kept ANY byte is not empty")        { b.rec[9].label[3] = 'x'; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("count must equal the population")                      { b.count = 2; CHECK_FALSE(mrfw::target_content_valid(b));
                                                                      b.count = mrnv::kTargetSlots + 1; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("an unknown flags bit is CORRUPT, never ignored")       { b.rec[0].flags = 0x03; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("a stray reserved byte")                                { b.rec[0].reserved[4] = 1; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("an all-zero administration key grants nothing")        { std::memset(b.rec[0].admin_pub, 0, 32); CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("a zero routing hash is 'unset', not a hint")           { b.rec[0].key_hash32 = 0; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("label_len 0 and label_len 17")                         { b.rec[0].label_len = 0; CHECK_FALSE(mrfw::target_content_valid(b));
                                                                      b.rec[0].label_len = 17; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("a label byte outside the alphabet")                    { b.rec[0].label[1] = ' '; CHECK_FALSE(mrfw::target_content_valid(b));
                                                                      b.rec[0].label[1] = '/'; CHECK_FALSE(mrfw::target_content_valid(b));
                                                                      b.rec[0].label[1] = '\x7f'; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("a non-zero label tail")                                { b.rec[0].label[5] = 'z'; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("hop_count 4 is refused — there is no fourth destination") {
        b.rec[0].hops[1] = 6; b.rec[0].hops[2] = 7; b.rec[0].hops[3] = 8; b.rec[0].hop_count = 4;
        CHECK_FALSE(mrfw::target_content_valid(b));
    }
    SUBCASE("a ZERO id inside the used prefix")                     { b.rec[0].hop_count = 2; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("a NON-ZERO byte past the used prefix")                 { b.rec[0].hops[2] = 9; CHECK_FALSE(mrfw::target_content_valid(b)); }
    SUBCASE("hop_count 0 requires every hop byte zero")             { b.rec[0].hop_count = 0; CHECK_FALSE(mrfw::target_content_valid(b));
                                                                      b.rec[0].hops[0] = 0; CHECK(mrfw::target_content_valid(b)); }
    SUBCASE("hop_count 3 is legal")                                 { b.rec[0].hops[1] = 6; b.rec[0].hops[2] = 7; b.rec[0].hop_count = 3;
                                                                      CHECK(mrfw::target_content_valid(b)); }
    SUBCASE("duplicate administration keys are ONE principal in two rows") {
        b.rec[4] = good; b.rec[4].label[0] = 'b'; b.count = 2;
        CHECK_FALSE(mrfw::target_content_valid(b));
    }
    SUBCASE("duplicate labels break the selector") {
        b.rec[4] = good; std::memset(b.rec[4].admin_pub, 0x22, 32); b.count = 2;
        CHECK_FALSE(mrfw::target_content_valid(b));
    }
    SUBCASE("★ the SAME routing hash on DIFFERENT keys stays TWO principals — legal, never merged") {
        b.rec[4] = good;
        std::memset(b.rec[4].admin_pub, 0x22, 32);
        b.rec[4].label[0] = 'b';
        b.count = 2;
        CHECK(mrfw::target_content_valid(b));
        CHECK(b.rec[0].key_hash32 == b.rec[4].key_hash32);
    }
    SUBCASE("labels are CASE-SENSITIVE, so `A1` and `a1` are two labels") {
        b.rec[4] = good; std::memset(b.rec[4].admin_pub, 0x22, 32); b.rec[4].label[0] = 'A'; b.count = 2;
        CHECK(mrfw::target_content_valid(b));
    }
}

// ================================================================================================================
// SELECTORS
// ================================================================================================================

TEST_CASE("radmin4/targets: the mask->verdict step answers none / one / many") {
    // ⚠⚠ THE `many` ARM IS DRIVEN FROM A **SYNTHETIC MASK**, AND IT IS LABELLED AS SUCH: two occupied rows whose
    //    BLAKE2b-512 fingerprints agree would be a real digest collision, which cannot be constructed. ⛔ NO CLAIM
    //    IS MADE ANYWHERE IN THIS SUITE THAT A COLLISION WAS PRODUCED. Splitting the resolver in two is exactly
    //    what makes the refusal arm reachable at all instead of being an untested branch.
    uint8_t slot = 0xFF;
    CHECK(mrfw::target_pick_from_mask(0u, slot) == mrfw::TargetPick::none);
    slot = 0xFF;
    CHECK(mrfw::target_pick_from_mask(1u << 7, slot) == mrfw::TargetPick::one);
    CHECK(slot == 7);
    slot = 0xFF;
    CHECK(mrfw::target_pick_from_mask((1u << 3) | (1u << 20), slot) == mrfw::TargetPick::many);
    CHECK(slot == 0xFF);                       // ⛔ an ambiguous mask writes NO slot — never a first match
    slot = 0xFF;
    CHECK(mrfw::target_pick_from_mask(0xFFFFFFFFu, slot) == mrfw::TargetPick::many);
    slot = 0xFF;
    CHECK(mrfw::target_pick_from_mask(1u << 31, slot) == mrfw::TargetPick::one);
    CHECK(slot == 31);
}

TEST_CASE("radmin4/targets: the fingerprint mask uses R-RA-29's INDEPENDENT reference values") {
    mrnv::TargetBlob b{};
    mrnv::target_blob_init(b);
    uint8_t idx = 0;
    for (const FpVec& v : kFpRef) {
        mrnv::TargetRow& r = b.rec[idx];
        unhex32(v.pub_hex, r.admin_pub);
        r.key_hash32 = 0x1000u + idx;
        r.label[0] = static_cast<char>('a' + idx);
        r.label_len = 1;
        r.flags = mrnv::kTargetFlagOccupied;
        ++idx;
    }
    b.count = idx;
    CHECK(mrfw::target_content_valid(b));
    uint8_t k = 0;
    for (const FpVec& v : kFpRef) {
        uint8_t slot = 0xFF;
        CHECK(mrfw::target_pick_from_mask(mrfw::target_mask_by_fp(b, v.fp_hex), slot) == mrfw::TargetPick::one);
        CHECK(slot == k);
        ++k;
    }
    // ★ THE CONTROL: a single corrupted character in the expected value must stop matching, or the comparison
    //   above would also be true of a resolver that compares nothing.
    for (const FpVec& v : kFpRef) {
        char bad[mrfw::kAdminFpHex + 1];
        std::memcpy(bad, v.fp_hex, mrfw::kAdminFpHex);
        bad[mrfw::kAdminFpHex] = '\0';
        bad[5] = (bad[5] == '0') ? '1' : '0';
        uint8_t slot = 0xFF;
        CHECK(mrfw::target_pick_from_mask(mrfw::target_mask_by_fp(b, bad), slot) == mrfw::TargetPick::none);
    }
    // labels resolve too, and ⛔ never by prefix
    uint8_t slot = 0xFF;
    CHECK(mrfw::target_pick_from_mask(mrfw::target_mask_by_label(b, "b", 1), slot) == mrfw::TargetPick::one);
    CHECK(slot == 1);
    CHECK(mrfw::target_mask_by_label(b, "", 0) == 0);
    CHECK(mrfw::target_mask_by_label(b, "ab", 2) == 0);
    // ⛔ AND NOT BY PREFIX IN THE OTHER DIRECTION EITHER — a query SHORTER than a stored label must not match.
    //   Found by `--target=radmin4targets` T13: with `label_len != len` relaxed to `<`, `label=al` resolved to
    //   `alpha`, and every row above still passed because they all carry ONE-character labels.
    mrnv::TargetRow& longrow = b.rec[7];
    std::memset(longrow.admin_pub, 0x5D, 32);
    longrow.key_hash32 = 0x2000u;
    std::memcpy(longrow.label, "alpha", 5);
    longrow.label_len = 5;
    longrow.flags = mrnv::kTargetFlagOccupied;
    b.count = static_cast<uint16_t>(b.count + 1);
    CHECK(mrfw::target_content_valid(b));
    uint8_t lslot = 0xFF;
    CHECK(mrfw::target_pick_from_mask(mrfw::target_mask_by_label(b, "alpha", 5), lslot) == mrfw::TargetPick::one);
    CHECK(lslot == 7);
    CHECK(mrfw::target_mask_by_label(b, "al", 2) == 0);      // ⛔ a PREFIX of a stored label is NOT that label
    CHECK(mrfw::target_mask_by_label(b, "a", 1) == (1u << 0));  // …and `a` still resolves to the row labelled `a`
    CHECK(mrfw::target_mask_by_label(b, "alphax", 6) == 0);
}

// ================================================================================================================
// THE SERVICE
// ================================================================================================================

TEST_CASE("radmin4/targets: `read` leaves a VALID EMPTY book on every non-ok arm and writes nothing") {
    for (mrnv::TargetRead st : { mrnv::TargetRead::absent, mrnv::TargetRead::invalid, mrnv::TargetRead::io_failed }) {
        FakeTargetStore store; FakeTargetUse use;
        store.state = st;
        mrfw::TargetService svc(store, use);
        mrnv::TargetBlob book{};
        const TargetState s = svc.read(book);
        CHECK(s != TargetState::ok);
        CHECK(mrfw::target_content_valid(book));
        CHECK(book.magic == mrnv::kTargetMagic);
        CHECK(mrfw::target_population(book) == 0);
        CHECK(store.saves == 0);
    }
}

TEST_CASE("radmin4/targets: boot reports four states read-only and never counts on a non-ok one") {
    FakeTargetStore store; FakeTargetUse use;
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};
    CHECK(svc.boot_report(book).state == TargetState::absent);
    CHECK(svc.boot_report(book).count == 0);
    store.seed_empty();
    CHECK(svc.add(book, spec_of("alpha", 0x11, 0xAAu)).ok);
    const mrfw::TargetBoot ok = svc.boot_report(book);
    CHECK(ok.state == TargetState::ok);
    CHECK(ok.count == 1);
    // ⛔ AND THE SCRATCH IS INVALIDATED: nothing survives the call for a later reader to mistake for a cache.
    CHECK(mrfw::target_population(book) == 0);
    store.state = mrnv::TargetRead::io_failed;
    const mrfw::TargetBoot bad = svc.boot_report(book);
    CHECK(bad.state == TargetState::io_failed);
    CHECK(bad.count == 0);
}

TEST_CASE("radmin4/targets: `add` fills the LOWEST free row, reuses holes and refuses duplicates") {
    FakeTargetStore store; FakeTargetUse use;
    store.seed_empty();
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};

    const mrfw::TargetResult a = svc.add(book, spec_of("alpha", 0x11, 0x11111111u));
    CHECK(a.ok);
    CHECK(a.slot == 0);
    CHECK(store.saves == 1);
    const mrfw::TargetResult b = svc.add(book, spec_of("beta", 0x22, 0x22222222u, 3));
    CHECK(b.ok);
    CHECK(b.slot == 1);
    CHECK(store.rec.count == 2);

    SUBCASE("a duplicate administration key refuses, at zero writes") {
        const int saves = store.saves;
        CHECK(svc.add(book, spec_of("gamma", 0x11, 0x33333333u)).err == TargetErr::duplicate);
        CHECK(store.saves == saves);
    }
    SUBCASE("a duplicate label refuses, at zero writes") {
        const int saves = store.saves;
        CHECK(svc.add(book, spec_of("beta", 0x33, 0x33333333u)).err == TargetErr::duplicate);
        CHECK(store.saves == saves);
    }
    SUBCASE("a HOLE is reused before a fresh slot, and slot numbers never shift") {
        CHECK(svc.remove(book, 0).ok);
        const mrfw::TargetResult c = svc.add(book, spec_of("gamma", 0x33, 0x33333333u));
        CHECK(c.ok);
        CHECK(c.slot == 0);
        CHECK(store.rec.rec[1].label[0] == 'b');       // ⛔ beta did NOT move
    }
    SUBCASE("an invalid spec never reaches the record") {
        const int saves = store.saves;
        mrfw::TargetSpec bad = spec_of("gamma", 0x33, 0u);          // zero hash
        CHECK(svc.add(book, bad).err == TargetErr::bad_args);
        bad = spec_of("gamma", 0x00, 0x44444444u);                  // zero key
        CHECK(svc.add(book, bad).err == TargetErr::bad_args);
        bad = spec_of("", 0x33, 0x44444444u);                       // empty label
        CHECK(svc.add(book, bad).err == TargetErr::bad_args);
        bad = spec_of("gamma", 0x33, 0x44444444u); bad.hop_count = 4;
        CHECK(svc.add(book, bad).err == TargetErr::bad_args);
        CHECK(store.saves == saves);
    }
}

TEST_CASE("radmin4/targets: all THIRTY-TWO rows fill, the 33rd refuses `full`, and NOTHING is evicted") {
    FakeTargetStore store; FakeTargetUse use;
    store.seed_empty();
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
        char lab[8];
        std::snprintf(lab, sizeof lab, "t%u", (unsigned)i);
        const mrfw::TargetResult r = svc.add(book, spec_of(lab, static_cast<uint8_t>(0x40 + i), 0x1000u + i));
        CHECK(r.ok);
        CHECK(r.slot == i);
    }
    CHECK(store.rec.count == mrnv::kTargetSlots);
    const int saves = store.saves;
    CHECK(svc.add(book, spec_of("over", 0xEE, 0x9999u)).err == TargetErr::full);
    CHECK(store.saves == saves);                      // ⛔ evicts NOTHING
    CHECK(store.rec.rec[0].label[0] == 't');
    CHECK(mrfw::target_content_valid(store.rec));
}

TEST_CASE("radmin4/targets: `set` changes the three mutable fields and NEVER the trust key or the slot") {
    FakeTargetStore store; FakeTargetUse use;
    store.seed_empty();
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};
    CHECK(svc.add(book, spec_of("alpha", 0x11, 0x11111111u, 3)).ok);
    CHECK(svc.add(book, spec_of("beta", 0x22, 0x22222222u)).ok);

    SUBCASE("relabel + rehash + a 3-hop path") {
        const mrfw::TargetResult r = svc.set(book, 0, spec_of("alpha2", 0x99, 0x55555555u, 4, 5, 6));
        CHECK(r.ok);
        CHECK(r.slot == 0);
        const mrnv::TargetRow& row = store.rec.rec[0];
        uint8_t want_key[32];
        std::memset(want_key, 0x11, 32);
        CHECK(std::memcmp(row.admin_pub, want_key, 32) == 0);   // ⛔ THE KEY IS IMMUTABLE — 0x99 was ignored
        CHECK(row.key_hash32 == 0x55555555u);
        CHECK(row.label_len == 6);
        CHECK(row.hop_count == 3);
        CHECK(row.hops[0] == 4);
        CHECK(mrfw::target_content_valid(store.rec));
    }
    SUBCASE("`layer=none` clears the path, and the cleared tail is ZERO") {
        CHECK(svc.set(book, 0, spec_of("alpha", 0x11, 0x11111111u)).ok);
        CHECK(store.rec.rec[0].hop_count == 0);
        for (int i = 0; i < 4; ++i) CHECK(store.rec.rec[0].hops[i] == 0);
    }
    SUBCASE("an IDENTICAL set is `unchanged` and costs ZERO writes") {
        const int saves = store.saves;
        CHECK(svc.set(book, 0, spec_of("alpha", 0x11, 0x11111111u, 3)).err == TargetErr::unchanged);
        CHECK(store.saves == saves);
    }
    SUBCASE("a label that collides with ANOTHER row refuses") {
        const int saves = store.saves;
        CHECK(svc.set(book, 0, spec_of("beta", 0x11, 0x11111111u)).err == TargetErr::duplicate);
        CHECK(store.saves == saves);
    }
    SUBCASE("an EMPTY slot is not_found; an out-of-range one is bad_args") {
        CHECK(svc.set(book, 5, spec_of("zulu", 0x11, 0x11u)).err == TargetErr::not_found);
        CHECK(svc.set(book, 99, spec_of("zulu", 0x11, 0x11u)).err == TargetErr::bad_args);
    }
    SUBCASE("an IN-USE row refuses — a FUTURE-CALLER service test") {
        FakeTargetUse busy;
        busy.busy_slot = 0;
        mrfw::TargetService svc2(store, busy);
        const int saves = store.saves, loads = store.loads;
        CHECK(svc2.set(book, 0, spec_of("zulu", 0x11, 0x11u)).err == TargetErr::in_use);
        CHECK(store.saves == saves);
        CHECK(store.loads == loads);
    }
}

TEST_CASE("radmin4/targets: `remove` zeroes the row WHOLE, leaves a hole and decrements the count") {
    FakeTargetStore store; FakeTargetUse use;
    store.seed_empty();
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};
    CHECK(svc.add(book, spec_of("alpha", 0x11, 0x11111111u, 3)).ok);
    CHECK(svc.add(book, spec_of("beta", 0x22, 0x22222222u)).ok);
    const mrfw::TargetResult r = svc.remove(book, 0);
    CHECK(r.ok);
    CHECK(store.rec.count == 1);
    CHECK(mrfw::admin_buf_all_zero(reinterpret_cast<const uint8_t*>(&store.rec.rec[0]), sizeof(mrnv::TargetRow)));
    CHECK(mrfw::target_row_occupied(store.rec.rec[1]));
    CHECK(svc.remove(book, 0).err == TargetErr::not_found);
    CHECK(svc.remove(book, 99).err == TargetErr::bad_args);
}

TEST_CASE("radmin4/targets: `reset` recovers ONLY a corrupt book and is not a bulk-delete escape") {
    SUBCASE("invalid -> empty, one write") {
        FakeTargetStore store; FakeTargetUse use;
        store.seed_empty();
        store.rec.count = 4;                                  // content broken
        mrfw::TargetService svc(store, use);
        mrnv::TargetBlob book{};
        CHECK(svc.recover(book).ok);
        CHECK(store.saves == 1);
        CHECK(store.rec.count == 0);
        CHECK(mrfw::target_content_valid(store.rec));
    }
    SUBCASE("absent and ok are not_invalid, at zero writes") {
        FakeTargetStore store; FakeTargetUse use;
        mrfw::TargetService svc(store, use);
        mrnv::TargetBlob book{};
        CHECK(svc.recover(book).err == TargetErr::not_invalid);
        store.seed_empty();
        CHECK(svc.recover(book).err == TargetErr::not_invalid);
        CHECK(store.saves == 0);
    }
    SUBCASE("io_failed refuses even the recovery verb") {
        FakeTargetStore store; FakeTargetUse use;
        store.state = mrnv::TargetRead::io_failed;
        mrfw::TargetService svc(store, use);
        mrnv::TargetBlob book{};
        CHECK(svc.recover(book).err == TargetErr::store_io_failed);
        CHECK(store.saves == 0);
    }
    SUBCASE("ANY in-use target refuses — a FUTURE-CALLER service test") {
        FakeTargetStore store; FakeTargetUse use;
        store.seed_empty();
        store.rec.count = 4;
        use.busy_any = true;
        mrfw::TargetService svc(store, use);
        mrnv::TargetBlob book{};
        CHECK(svc.recover(book).err == TargetErr::in_use);
        CHECK(store.saves == 0);
        CHECK(store.loads == 0);
    }
}

TEST_CASE("radmin4/targets: EVERY ordinary write is refused over an unreadable or corrupt book") {
    for (mrnv::TargetRead st : { mrnv::TargetRead::invalid, mrnv::TargetRead::io_failed }) {
        FakeTargetStore store; FakeTargetUse use;
        store.state = st;
        mrfw::TargetService svc(store, use);
        mrnv::TargetBlob book{};
        CHECK_FALSE(svc.add(book, spec_of("alpha", 0x11, 0x11u)).ok);
        CHECK_FALSE(svc.set(book, 0, spec_of("alpha", 0x11, 0x11u)).ok);
        CHECK_FALSE(svc.remove(book, 0).ok);
        CHECK(store.saves == 0);
    }
}

TEST_CASE("radmin4/targets: a FAILED save publishes no success and RESTORES the candidate row") {
    FakeTargetStore store; FakeTargetUse use;
    store.seed_empty();
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};
    CHECK(svc.add(book, spec_of("alpha", 0x11, 0x11111111u)).ok);
    store.save_ok = false;
    const mrfw::TargetResult r = svc.add(book, spec_of("beta", 0x22, 0x22222222u));
    CHECK(r.err == TargetErr::nv_save_failed);
    CHECK(store.rec.count == 1);                                    // the stored record did not move
    CHECK_FALSE(mrfw::target_row_occupied(book.rec[1]));            // ⛔ the failed candidate is INVALIDATED
    CHECK(book.count == 1);
    CHECK(mrfw::admin_buf_all_zero(r.admin_pub, 32));
    // ⛔ and the failure does NOT claim the old bytes survived — `written` shows what was attempted
    CHECK(store.written.count == 2);
}

TEST_CASE("radmin4/targets: EVERY entry point RELOADS — a stale scratch can never be adopted") {
    FakeTargetStore store; FakeTargetUse use;
    store.seed_empty();
    mrfw::TargetService svc(store, use);
    mrnv::TargetBlob book{};
    CHECK(svc.add(book, spec_of("alpha", 0x11, 0x11111111u)).ok);
    // Poison the caller's buffer between calls: a service that trusted it would save the poison.
    std::memset(&book, 0x5A, sizeof book);
    const int saves = store.saves;
    const mrfw::TargetResult r = svc.add(book, spec_of("beta", 0x22, 0x22222222u));
    CHECK(r.ok);
    CHECK(r.slot == 1);
    CHECK(store.saves == saves + 1);
    CHECK(mrfw::target_content_valid(store.rec));
    CHECK(store.rec.count == 2);
    CHECK(store.rec.rec[0].label[0] == 'a');
}
