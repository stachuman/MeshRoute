// MeshRoute — test/test_firmware_admin_identity.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// §RADMIN slice 3 — the `/mradmid` administration-identity service (`src/firmware_admin_identity.h` + the record
// in `src/device_nv.h`). Design §§6.2-6.4; rulings R-RA-6 / R-RA-29; register [[B312]] / [[B317]].
//
// ★★★ WHY EVERY CASE COUNTS WRITES AND DRAWS INSTEAD OF ASSERTING A VERDICT — the `/mrteams` suite's rule: the
//     verdict is a value the implementation CHOOSES, so a wrong implementation can choose the right value while
//     doing the wrong thing. A WRITE COUNT and a DRAW COUNT are CONSEQUENCES. ⇒ "an unreadable store costs zero
//     writes" is measured as `saves == 0`, and "a missing confirmation costs no entropy" is measured as
//     `draws == 0` — ⛔ never as "it returned refused".
//
// ★★★ THE FINGERPRINT EXPECTATIONS ARE INDEPENDENT AND WERE FROZEN BEFORE THE HELPER EXISTED. They come from
//     `/home/staszek/mr-slice3-ref/gen_fp_reference.py`, run OUTSIDE both repositories with CPython 3.11.2's
//     `hashlib.blake2b(digest_size=64)` — a NON-PRODUCTION BLAKE2, ⛔ not lib/monocypher — and that generator
//     validates ITSELF against the RFC 7693 Appendix A known-answer vector for BLAKE2b-512("abc") before emitting
//     a single line. ⛔ NO expected fingerprint in this file was produced by the code under test.
//
// ⛔ THE LIMIT OF THE CLAIM: the store is a FAKE. No NVS/LittleFS write, no flash WEAR, no reset-during-write.
//    `/mradmid`'s power-cut behaviour is METAL-ONLY and is bench Part 55a's ([[B193]] qualified `/mrcfg` and
//    `/mrjoin`, ⛔ NOT this record). The entropy source is a FAKE too: a counted draw stream is ⛔ NOT a hardware
//    or RF entropy proof, and [[B312]] stays OPEN.
// ⛔⛔ AND ONE HONEST GAP, MARKED RATHER THAN GLOSSED: the service's transient blob is a STACK frame, and reading
//    it after the call returns is undefined behaviour — so the wipe cannot be observed "in situ" by any host test.
//    That is exactly why `SecretWipeGuard` is a NAMED type: the case below drives the guard itself over a carrier
//    that OUTLIVES it, which is fully-defined and mutation-visible. The service's USE of that guard is verified by
//    inspection, ⛔ and this file does not claim otherwise.
#include "doctest.h"

#include <cstring>
#include <initializer_list>

#include "firmware_admin_identity.h"
#include "identity.h"     // meshroute::identity_from_seed — the REAL derivation the service calls

using mrfw::AdminIdErr;
using mrfw::AdminIdState;

namespace {

// ---- the COUNTING store ---------------------------------------------------------------------------------
// ★ `state` is the FOUR-valued answer, so a case can put the fake in `absent`, `invalid` or `io_failed` without
//   forging bytes; `rec` moves only on a successful save, so "the stored record did not move" is measurable.
struct FakeAdminStore : mrfw::IAdminIdStore {
    mrnv::AdminIdBlob rec{};
    mrnv::AdminIdBlob written{};
    mrnv::AdminIdRead state = mrnv::AdminIdRead::absent;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    // ★ On a NON-ok read the fake deposits GARBAGE, deliberately: the real `read_slot` may leave a PARTIAL record
    //   behind, and the service must re-init rather than trust it (device_nv.h's warning).
    bool deposit_garbage = true;

    mrnv::AdminIdRead load(mrnv::AdminIdBlob& out) override {
        ++loads;
        if (state != mrnv::AdminIdRead::ok) {
            if (deposit_garbage) std::memset(&out, 0xA5, sizeof out);
            else                 out = rec;
            return state;
        }
        out = rec;
        return mrnv::AdminIdRead::ok;
    }
    bool save(const mrnv::AdminIdBlob& b) override {
        ++saves;
        written = b;
        if (!save_ok) return false;
        rec = b;
        state = mrnv::AdminIdRead::ok;
        return true;
    }
    // Put the fake into a VALID stored state holding `fill` in every seed byte.
    void seed_valid(uint8_t fill) {
        mrnv::admin_id_blob_init(rec);
        std::memset(rec.seed, fill, sizeof rec.seed);
        state = mrnv::AdminIdRead::ok;
    }
};

// ---- the COUNTING entropy source ------------------------------------------------------------------------
// ⛔ IT IS A FAKE DRAW STREAM AND ⛔ NOT A HARDWARE PROOF. What it measures is the SERVICE's discipline: how many
//    times it asks, and what it does with each answer.
struct FakeSeed : mrfw::IAdminSeedSource {
    int     draws  = 0;
    bool    answer = true;
    uint8_t byte   = 0x11;              // ⓘ `byte`, ⛔ not `fill` — that name is the OVERRIDE below
    bool    zero_result = false;        // a "successful" draw that is all zeros — the dead-RNG shape
    bool    partial_then_false = false; // writes half the buffer, THEN says no

    bool fill(uint8_t out[32]) override {
        ++draws;
        if (partial_then_false) { std::memset(out, 0x7E, 16); return false; }
        if (zero_result)        { std::memset(out, 0x00, 32); return true; }
        std::memset(out, byte, 32);
        return answer;
    }
};

// The FULL 64-hex reference digests' first eight bytes, from the independent generator (see the head note).
struct FpVec { const char* name; const char* pub_hex; const char* fp_hex; };
const FpVec kFpRef[] = {
    { "zeros",      "0000000000000000000000000000000000000000000000000000000000000000", "9ab7a73a97a1a303" },
    { "ones",       "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", "83b5ade6991342ed" },
    { "seq",        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "5c52920a7263e39d" },
    { "seq_b0",     "010102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "06b824f0d590f1e6" },
    { "seq_swap01", "010002030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", "f808c6cbb729c99b" },
    { "seq_b31",    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e20", "59a25fa5ddc30b96" },
    { "hi",         "e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", "d70a8754d94c8f21" },
    { "mixed",      "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08", "00fbe28e23ddf762" },
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

}  // namespace

// ================================================================================================================
// THE RECORD: layout, storage classification and content policy
// ================================================================================================================

TEST_CASE("radmin3/id: the /mradmid record layout is the frozen v1 contract on THIS ABI") {
    CHECK(sizeof(mrnv::AdminIdBlob) == 40);
    CHECK(alignof(mrnv::AdminIdBlob) == 4);
    CHECK(offsetof(mrnv::AdminIdBlob, magic) == 0);
    CHECK(offsetof(mrnv::AdminIdBlob, version) == 4);
    CHECK(offsetof(mrnv::AdminIdBlob, reserved) == 6);
    CHECK(offsetof(mrnv::AdminIdBlob, seed) == 8);
    CHECK(sizeof(mrnv::AdminIdBlob{}.seed) == 32);
    // ★ THE MAGIC IS 'MRA1' AND IT IS ITS OWN — a collision with another record's magic would let a `/mrcfg` or a
    //   `/mrteams` blob be adopted as an administration root of the right length.
    CHECK(mrnv::kAdminIdMagic == 0x4D524131u);
    CHECK(mrnv::kAdminIdVersion == 1);
    for (uint32_t other : { mrnv::kMagic, mrnv::kIdMagic, mrnv::kPeersMagic,
                            mrnv::kJoinMagic, mrnv::kTeamKeyMagic, mrnv::kUiPresetMagic })
        CHECK(mrnv::kAdminIdMagic != other);
}

TEST_CASE("radmin3/id: admin_id_blob_init stamps a VALID header and leaves the seed ALL-ZERO (never a usable root)") {
    mrnv::AdminIdBlob b;
    std::memset(&b, 0x5A, sizeof b);
    mrnv::admin_id_blob_init(b);
    CHECK(b.magic == mrnv::kAdminIdMagic);
    CHECK(b.version == mrnv::kAdminIdVersion);
    CHECK(b.reserved == 0);
    CHECK(mrfw::admin_buf_all_zero(b.seed, sizeof b.seed));
    // ★★ THE POINT: the storage header alone can NEVER publish a usable identity. Only the checked entropy path can.
    CHECK_FALSE(mrfw::admin_id_content_valid(b));
    // …and the composed state agrees, so a record stamped but never seeded reads as INVALID rather than as ok.
    CHECK(mrfw::admin_id_state_of(mrnv::AdminIdRead::ok, b) == AdminIdState::invalid);
}

TEST_CASE("radmin3/id: the four-state classifier keeps absent, corrupt and unreadable APART") {
    mrnv::AdminIdBlob b{};
    mrnv::admin_id_blob_init(b);
    std::memset(b.seed, 0x33, sizeof b.seed);
    const int N = static_cast<int>(sizeof b);

    SUBCASE("an exactly-sized, correctly-stamped record is ok") {
        CHECK(mrnv::admin_id_blob_state(b, N) == mrnv::AdminIdRead::ok);
    }
    SUBCASE("absent is the -1 sentinel and NOTHING else") {
        CHECK(mrnv::admin_id_blob_state(b, mrnv::kSlotAbsent) == mrnv::AdminIdRead::absent);
        // ⚠ a DIFFERENT negative is a corrupt-CTZ read, ⛔ never a fresh device
        CHECK(mrnv::admin_id_blob_state(b, -84) == mrnv::AdminIdRead::invalid);
        CHECK(mrnv::admin_id_blob_state(b, -5) == mrnv::AdminIdRead::invalid);
    }
    SUBCASE("short and over-long lengths are invalid") {
        CHECK(mrnv::admin_id_blob_state(b, N - 1) == mrnv::AdminIdRead::invalid);
        CHECK(mrnv::admin_id_blob_state(b, N + 1) == mrnv::AdminIdRead::invalid);
        CHECK(mrnv::admin_id_blob_state(b, 0) == mrnv::AdminIdRead::invalid);
    }
    SUBCASE("wrong magic and wrong version are invalid — EQUALITY, no range") {
        mrnv::AdminIdBlob x = b; x.magic = mrnv::kTeamKeyMagic;
        CHECK(mrnv::admin_id_blob_state(x, N) == mrnv::AdminIdRead::invalid);
        x = b; x.version = 0;  CHECK(mrnv::admin_id_blob_state(x, N) == mrnv::AdminIdRead::invalid);
        x = b; x.version = 2;  CHECK(mrnv::admin_id_blob_state(x, N) == mrnv::AdminIdRead::invalid);
    }
    SUBCASE("★ a backend that would not open is io_failed and OUTRANKS the absent sentinel") {
        mrnv::SlotIo io; io.backend_failed = true;
        CHECK(mrnv::admin_id_blob_state(b, mrnv::kSlotAbsent, io) == mrnv::AdminIdRead::io_failed);
        CHECK(mrnv::admin_id_blob_state(b, N, io) == mrnv::AdminIdRead::io_failed);
    }
    SUBCASE("★ an OVER-LENGTH file is invalid even when the read returned exactly sizeof — the PREFIX hazard") {
        mrnv::SlotIo io; io.oversize = true;
        CHECK(mrnv::admin_id_blob_state(b, N, io) == mrnv::AdminIdRead::invalid);
    }
}

TEST_CASE("radmin3/id: the CONTENT policy refuses an all-zero seed and a stray reserved byte") {
    mrnv::AdminIdBlob b{};
    mrnv::admin_id_blob_init(b);
    CHECK_FALSE(mrfw::admin_id_content_valid(b));            // all-zero seed — the DEAD-RNG record
    b.seed[31] = 1;
    CHECK(mrfw::admin_id_content_valid(b));                   // ★ ONE non-zero byte anywhere is enough
    b.seed[31] = 0; b.seed[0] = 1;
    CHECK(mrfw::admin_id_content_valid(b));
    b.reserved = 1;
    CHECK_FALSE(mrfw::admin_id_content_valid(b));             // ⛔ non-canonical padding breaks the write guard
    b.reserved = 0;
    CHECK(mrfw::admin_id_content_valid(b));
    // …and the COMPOSED state maps a content failure onto `invalid`, ⛔ never onto `ok`.
    b.seed[0] = 0;
    CHECK(mrfw::admin_id_state_of(mrnv::AdminIdRead::ok, b) == AdminIdState::invalid);
    CHECK(mrfw::admin_id_state_of(mrnv::AdminIdRead::absent, b) == AdminIdState::absent);
    CHECK(mrfw::admin_id_state_of(mrnv::AdminIdRead::io_failed, b) == AdminIdState::io_failed);
    CHECK(mrfw::admin_id_state_of(mrnv::AdminIdRead::invalid, b) == AdminIdState::invalid);
}

TEST_CASE("radmin3/id: admin_buf_all_zero answers the whole buffer, not a prefix") {
    uint8_t z[32] = {};
    CHECK(mrfw::admin_buf_all_zero(z, 32));
    for (int i = 0; i < 32; ++i) {
        uint8_t b[32] = {};
        b[i] = 1;
        CHECK_FALSE(mrfw::admin_buf_all_zero(b, 32));   // ⛔ a non-zero byte at ANY index must be seen
    }
    CHECK(mrfw::admin_buf_all_zero(z, 0));              // an empty span is vacuously zero
}

// ================================================================================================================
// R-RA-29: THE FINGERPRINT, against FROZEN INDEPENDENT REFERENCE LITERALS
// ================================================================================================================

TEST_CASE("radmin3/id: the fingerprint reproduces the INDEPENDENT BLAKE2b-512 reference on every vector") {
    for (const FpVec& v : kFpRef) {
        CAPTURE(v.name);
        uint8_t pub[32];
        unhex32(v.pub_hex, pub);
        char fp[mrfw::kAdminFpHex + 1];
        mrfw::admin_fp_hex(pub, fp);
        CHECK(std::strlen(fp) == mrfw::kAdminFpHex);
        CHECK(std::strcmp(fp, v.fp_hex) == 0);
        // ⛔ LOWERCASE, always: the ruling says so and a selector that changes case is a different string.
        for (size_t i = 0; i < mrfw::kAdminFpHex; ++i)
            CHECK(((fp[i] >= '0' && fp[i] <= '9') || (fp[i] >= 'a' && fp[i] <= 'f')));
    }
}

TEST_CASE("radmin3/id: the comparison DISCRIMINATES — an altered expected byte must NOT match (the control)") {
    // ★★★ THE CONTROL FOR THE CASE ABOVE. Without it, "every vector matches" would also be true of a comparison
    //     that compares nothing. Each reference value is corrupted in ONE character and must then disagree.
    for (const FpVec& v : kFpRef) {
        CAPTURE(v.name);
        uint8_t pub[32];
        unhex32(v.pub_hex, pub);
        char fp[mrfw::kAdminFpHex + 1];
        mrfw::admin_fp_hex(pub, fp);
        for (size_t i = 0; i < mrfw::kAdminFpHex; ++i) {
            char corrupted[mrfw::kAdminFpHex + 1];
            std::memcpy(corrupted, v.fp_hex, mrfw::kAdminFpHex + 1);
            corrupted[i] = (corrupted[i] == '0') ? '1' : '0';
            CHECK(std::strcmp(fp, corrupted) != 0);
        }
    }
}

TEST_CASE("radmin3/id: the fingerprint is the FIRST eight digest bytes — ⛔ not bytes 8..15, ⛔ not a reordering") {
    // The independent generator also published the FULL 64-hex digest of the `seq` key:
    //   5c52920a7263e39d 57920ca0cb752ac6 …
    // ⇒ bytes 8..15 render as "57920ca0cb752ac6" and must NEVER be what this helper produces (an offset error),
    //   and the byte-reversed prefix must never appear either (a little-endian integer conversion).
    uint8_t pub[32];
    unhex32("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", pub);
    char fp[mrfw::kAdminFpHex + 1];
    mrfw::admin_fp_hex(pub, fp);
    CHECK(std::strcmp(fp, "5c52920a7263e39d") == 0);
    CHECK(std::strcmp(fp, "57920ca0cb752ac6") != 0);   // ⛔ the WRONG OFFSET
    CHECK(std::strcmp(fp, "9de363720a92525c") != 0);   // ⛔ a LITTLE-ENDIAN integer rendering of the same 8 bytes
}

TEST_CASE("radmin3/id: a TRUNCATED, CHANGED or REORDERED key produces a different fingerprint") {
    uint8_t base[32], b0[32], swapped[32], last[32];
    unhex32("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", base);
    unhex32("010102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", b0);
    unhex32("010002030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", swapped);
    unhex32("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e20", last);
    char f[4][mrfw::kAdminFpHex + 1];
    mrfw::admin_fp_hex(base, f[0]);
    mrfw::admin_fp_hex(b0, f[1]);
    mrfw::admin_fp_hex(swapped, f[2]);
    mrfw::admin_fp_hex(last, f[3]);
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) CHECK(std::strcmp(f[i], f[j]) != 0);
    // ★ AND EVERY ONE MATCHES ITS INDEPENDENT REFERENCE, so "they differ" is not merely "the function is chaotic".
    CHECK(std::strcmp(f[1], "06b824f0d590f1e6") == 0);
    CHECK(std::strcmp(f[2], "f808c6cbb729c99b") == 0);
    CHECK(std::strcmp(f[3], "59a25fa5ddc30b96") == 0);
}

TEST_CASE("radmin3/id: the full-key renderer is 64 lowercase hex and round-trips every reference vector") {
    for (const FpVec& v : kFpRef) {
        CAPTURE(v.name);
        uint8_t pub[32];
        unhex32(v.pub_hex, pub);
        char out[mrfw::kAdminKeyHex + 1];
        mrfw::admin_key_hex(pub, out);
        CHECK(std::strlen(out) == mrfw::kAdminKeyHex);
        CHECK(std::strcmp(out, v.pub_hex) == 0);
    }
}

// ================================================================================================================
// THE SERVICE
// ================================================================================================================

TEST_CASE("radmin3/id: generate creates ONLY an ABSENT identity, in EXACTLY ONE write") {
    FakeAdminStore st; FakeSeed sd;
    st.state = mrnv::AdminIdRead::absent;
    mrfw::AdminIdService svc(st, sd);

    const mrfw::AdminIdResult r = svc.generate();
    CHECK(r.ok);
    CHECK(r.err == AdminIdErr::none);
    CHECK(st.saves == 1);                      // ★ EXACTLY ONE — never two, never zero
    CHECK(sd.draws == 1);
    CHECK(st.rec.magic == mrnv::kAdminIdMagic);
    CHECK(st.rec.version == mrnv::kAdminIdVersion);
    CHECK(st.rec.reserved == 0);
    CHECK_FALSE(mrfw::admin_buf_all_zero(st.rec.seed, 32));

    // ★★ THE PUBLISHED KEY IS THE ONE THE STORED SEED DERIVES — measured against the REAL derivation, so a service
    //    that reported a key from a different seed (or from a stale buffer) is caught.
    meshroute::Identity id{};
    meshroute::identity_from_seed(id, st.rec.seed);
    CHECK(std::memcmp(r.ed_pub, id.ed_pub, 32) == 0);
    // ⛔ AND THE SEED IS NOT THE PUBLIC KEY: a service that published seed bytes would pass a lazy equality test.
    CHECK(std::memcmp(r.ed_pub, st.rec.seed, 32) != 0);
}

TEST_CASE("radmin3/id: generate REFUSES an existing, invalid or unreadable record — ZERO writes, ZERO draws") {
    struct Row { mrnv::AdminIdRead read; bool valid_content; AdminIdErr err; };
    for (Row row : { Row{ mrnv::AdminIdRead::ok,        true,  AdminIdErr::already_present },
                     Row{ mrnv::AdminIdRead::invalid,   false, AdminIdErr::store_invalid },
                     Row{ mrnv::AdminIdRead::io_failed, false, AdminIdErr::store_io_failed },
                     // ★ a STORAGE-ok record whose CONTENT is a dead-RNG seed is `invalid`, ⛔ not already_present
                     Row{ mrnv::AdminIdRead::ok,        false, AdminIdErr::store_invalid } }) {
        FakeAdminStore st; FakeSeed sd;
        st.state = row.read;
        if (row.read == mrnv::AdminIdRead::ok) {
            mrnv::admin_id_blob_init(st.rec);
            if (row.valid_content) std::memset(st.rec.seed, 0x44, sizeof st.rec.seed);
        }
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.generate();
        CHECK_FALSE(r.ok);
        CHECK(r.err == row.err);
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);                                        // ⛔ a refusal costs NO entropy
        CHECK(mrfw::admin_buf_all_zero(r.ed_pub, 32));               // ⛔ a refusal publishes NO key
    }
}

TEST_CASE("radmin3/id: rotate replaces ONLY an OK identity and REFUSES the other three states") {
    SUBCASE("ok -> rotated, exactly one write, a DIFFERENT seed") {
        FakeAdminStore st; FakeSeed sd;
        st.seed_valid(0x22);
        mrnv::AdminIdBlob before = st.rec;
        sd.byte = 0x9C;
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.rotate();
        CHECK(r.ok);
        CHECK(st.saves == 1);
        CHECK(sd.draws == 1);
        CHECK(std::memcmp(&st.rec, &before, sizeof before) != 0);   // ★ the record MOVED
        CHECK(st.rec.magic == mrnv::kAdminIdMagic);
    }
    struct Row { mrnv::AdminIdRead read; AdminIdErr err; };
    for (Row row : { Row{ mrnv::AdminIdRead::absent,    AdminIdErr::absent },
                     Row{ mrnv::AdminIdRead::invalid,   AdminIdErr::store_invalid },
                     Row{ mrnv::AdminIdRead::io_failed, AdminIdErr::store_io_failed } }) {
        FakeAdminStore st; FakeSeed sd;
        st.state = row.read;
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.rotate();
        CHECK_FALSE(r.ok);
        CHECK(r.err == row.err);
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);
    }
}

TEST_CASE("radmin3/id: recovery is the ONLY write over an INVALID record — and io_failed refuses even IT") {
    SUBCASE("★ invalid -> recovered, exactly one write") {
        FakeAdminStore st; FakeSeed sd;
        st.state = mrnv::AdminIdRead::invalid;
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.recover();
        CHECK(r.ok);
        CHECK(st.saves == 1);
        CHECK(sd.draws == 1);
        CHECK(mrfw::admin_id_content_valid(st.rec));
    }
    SUBCASE("★★ a STORAGE-ok record with dead-RNG content is INVALID and IS recoverable") {
        FakeAdminStore st; FakeSeed sd;
        mrnv::admin_id_blob_init(st.rec);          // an all-zero seed
        st.state = mrnv::AdminIdRead::ok;
        mrfw::AdminIdService svc(st, sd);
        CHECK(svc.state() == AdminIdState::invalid);
        const mrfw::AdminIdResult r = svc.recover();
        CHECK(r.ok);
        CHECK(st.saves == 1);
    }
    struct Row { mrnv::AdminIdRead read; bool ok_content; AdminIdErr err; };
    for (Row row : { Row{ mrnv::AdminIdRead::absent,    false, AdminIdErr::not_invalid },
                     Row{ mrnv::AdminIdRead::ok,        true,  AdminIdErr::not_invalid },
                     // ⛔⛔ THE ONE THAT MATTERS: nothing is known about the record, so NOTHING may be written —
                     //     not even the recovery verb. A re-mint here would destroy an intact root because a
                     //     mount failed transiently.
                     Row{ mrnv::AdminIdRead::io_failed, false, AdminIdErr::store_io_failed } }) {
        FakeAdminStore st; FakeSeed sd;
        st.state = row.read;
        if (row.ok_content) { mrnv::admin_id_blob_init(st.rec); std::memset(st.rec.seed, 7, 32); }
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.recover();
        CHECK_FALSE(r.ok);
        CHECK(r.err == row.err);
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);
    }
}

TEST_CASE("radmin3/id: EVERY ENTROPY FAILURE refuses BEFORE the save and publishes no key") {
    struct Row { const char* name; bool answer; bool zero; bool partial; };
    for (Row row : { Row{ "provider says no",          false, false, false },
                     Row{ "all-zero draw (dead RNG)",  true,  true,  false },
                     Row{ "partial write THEN false",  false, false, true  } }) {
        CAPTURE(row.name);
        for (int op = 0; op < 3; ++op) {                     // generate / rotate / recover share ONE mint path
            FakeAdminStore st; FakeSeed sd;
            sd.answer = row.answer; sd.zero_result = row.zero; sd.partial_then_false = row.partial;
            if (op == 0)      st.state = mrnv::AdminIdRead::absent;
            else if (op == 1) st.seed_valid(0x31);
            else              st.state = mrnv::AdminIdRead::invalid;
            mrnv::AdminIdBlob before = st.rec;
            mrfw::AdminIdService svc(st, sd);
            const mrfw::AdminIdResult r = (op == 0) ? svc.generate() : (op == 1) ? svc.rotate() : svc.recover();
            CHECK_FALSE(r.ok);
            CHECK(r.err == AdminIdErr::entropy_failed);
            CHECK(sd.draws == 1);                            // it ASKED exactly once …
            CHECK(st.saves == 0);                            // … and wrote NOTHING
            CHECK(std::memcmp(&st.rec, &before, sizeof before) == 0);
            CHECK(mrfw::admin_buf_all_zero(r.ed_pub, 32));   // ⛔ no key escaped
        }
    }
}

TEST_CASE("radmin3/id: a FAILED SAVE publishes no success and derives no key — and claims nothing about the flash") {
    for (int op = 0; op < 3; ++op) {
        FakeAdminStore st; FakeSeed sd;
        st.save_ok = false;
        if (op == 0)      st.state = mrnv::AdminIdRead::absent;
        else if (op == 1) st.seed_valid(0x55);
        else              st.state = mrnv::AdminIdRead::invalid;
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = (op == 0) ? svc.generate() : (op == 1) ? svc.rotate() : svc.recover();
        CHECK_FALSE(r.ok);
        CHECK(r.err == AdminIdErr::nv_save_failed);
        CHECK(st.saves == 1);                                // it TRIED exactly once
        CHECK(mrfw::admin_buf_all_zero(r.ed_pub, 32));       // ⛔ no key published
        // ⚠⚠ AND THE MEDIUM MAY HAVE MOVED ANYWAY. The fake records what was handed to it: the bytes reached the
        //    seam even though the write reported failure, which is exactly the nRF52 remove-then-write exposure
        //    ([[B317]]) the console text must never contradict with "nothing was written".
        CHECK(st.written.magic == mrnv::kAdminIdMagic);
        CHECK_FALSE(mrfw::admin_buf_all_zero(st.written.seed, 32));
    }
}

TEST_CASE("radmin3/id: show returns PUBLIC material from a valid record and refuses the other three states") {
    SUBCASE("ok -> the derived public key, ZERO writes, ⛔ no seed byte") {
        FakeAdminStore st; FakeSeed sd;
        st.seed_valid(0x6D);
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.show();
        CHECK(r.ok);
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);
        meshroute::Identity id{};
        meshroute::identity_from_seed(id, st.rec.seed);
        CHECK(std::memcmp(r.ed_pub, id.ed_pub, 32) == 0);
        CHECK(std::memcmp(r.ed_pub, st.rec.seed, 32) != 0);
    }
    struct Row { mrnv::AdminIdRead read; AdminIdErr err; };
    for (Row row : { Row{ mrnv::AdminIdRead::absent,    AdminIdErr::absent },
                     Row{ mrnv::AdminIdRead::invalid,   AdminIdErr::store_invalid },
                     Row{ mrnv::AdminIdRead::io_failed, AdminIdErr::store_io_failed } }) {
        FakeAdminStore st; FakeSeed sd;
        st.state = row.read;
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdResult r = svc.show();
        CHECK_FALSE(r.ok);
        CHECK(r.err == row.err);
        CHECK(st.saves == 0);
        CHECK(mrfw::admin_buf_all_zero(r.ed_pub, 32));
    }
}

TEST_CASE("radmin3/id: a stored seed RELOADS to the same public key and the same fingerprint") {
    FakeAdminStore st; FakeSeed sd;
    st.state = mrnv::AdminIdRead::absent;
    sd.byte = 0xC7;
    mrfw::AdminIdService svc(st, sd);
    const mrfw::AdminIdResult made = svc.generate();
    CHECK(made.ok);
    char fp1[mrfw::kAdminFpHex + 1], fp2[mrfw::kAdminFpHex + 1];
    mrfw::admin_fp_hex(made.ed_pub, fp1);

    // A SECOND service over the SAME store — the reboot shape.
    FakeSeed sd2;
    mrfw::AdminIdService svc2(st, sd2);
    const mrfw::AdminIdResult back = svc2.show();
    CHECK(back.ok);
    mrfw::admin_fp_hex(back.ed_pub, fp2);
    CHECK(std::memcmp(made.ed_pub, back.ed_pub, 32) == 0);
    CHECK(std::strcmp(fp1, fp2) == 0);
    CHECK(sd2.draws == 0);                                   // ⛔ a reload draws NOTHING
}

TEST_CASE("radmin3/id: the BOOT report VALIDATES and REPORTS — zero writes, zero draws, on all four states") {
    struct Row { mrnv::AdminIdRead read; bool seed_ok; AdminIdState want; };
    for (Row row : { Row{ mrnv::AdminIdRead::ok,        true,  AdminIdState::ok },
                     Row{ mrnv::AdminIdRead::ok,        false, AdminIdState::invalid },
                     Row{ mrnv::AdminIdRead::absent,    false, AdminIdState::absent },
                     Row{ mrnv::AdminIdRead::invalid,   false, AdminIdState::invalid },
                     Row{ mrnv::AdminIdRead::io_failed, false, AdminIdState::io_failed } }) {
        FakeAdminStore st; FakeSeed sd;
        st.state = row.read;
        if (row.read == mrnv::AdminIdRead::ok) {
            mrnv::admin_id_blob_init(st.rec);
            if (row.seed_ok) std::memset(st.rec.seed, 0x18, 32);
        }
        mrfw::AdminIdService svc(st, sd);
        const mrfw::AdminIdBoot b = svc.boot_report();
        CHECK(b.state == row.want);
        CHECK(st.saves == 0);      // ⛔ BOOT WRITES NOTHING …
        CHECK(sd.draws == 0);      // ⛔ … and auto-generates nothing
        CHECK(st.loads == 1);      // exactly one read
    }
}

TEST_CASE("radmin3/id: the SecretWipeGuard actually zeroes a carrier that OUTLIVES it") {
    // ★★★ THE ONLY FULLY-DEFINED WAY TO OBSERVE THE WIPE (see the head note): the carrier lives HERE, the guard
    //     lives in an inner scope. Reading a wiped stack frame after a call returns would be UB.
    mrnv::AdminIdBlob carrier{};
    mrnv::admin_id_blob_init(carrier);
    std::memset(carrier.seed, 0xEE, sizeof carrier.seed);
    CHECK_FALSE(mrfw::admin_buf_all_zero(carrier.seed, sizeof carrier.seed));
    CHECK(carrier.magic == mrnv::kAdminIdMagic);
    {
        mrfw::SecretWipeGuard<mrnv::AdminIdBlob> g{carrier};
        CHECK_FALSE(mrfw::admin_buf_all_zero(carrier.seed, sizeof carrier.seed));   // still live INSIDE the scope
    }
    CHECK(mrfw::admin_buf_all_zero(carrier.seed, sizeof carrier.seed));
    CHECK(carrier.magic == 0);      // ⛔ the WHOLE carrier, not only the seed member

    // …and over the expanded Identity, which is the other secret-bearing transient the service creates.
    meshroute::Identity id{};
    const uint8_t seed[32] = { 1, 2, 3, 4, 5 };
    meshroute::identity_from_seed(id, seed);
    CHECK_FALSE(mrfw::admin_buf_all_zero(id.ed_secret, sizeof id.ed_secret));
    { mrfw::SecretWipeGuard<meshroute::Identity> g{id}; }
    CHECK(mrfw::admin_buf_all_zero(id.ed_secret, sizeof id.ed_secret));
    CHECK(mrfw::admin_buf_all_zero(id.x_secret, sizeof id.x_secret));
    CHECK(mrfw::admin_buf_all_zero(id.seed, sizeof id.seed));
}
