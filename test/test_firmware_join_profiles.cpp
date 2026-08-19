// MeshRoute — test/test_firmware_join_profiles.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-15 slice 2 — the `/mrjoin` profile store (`src/firmware_join_profiles.h` + the record in `src/device_nv.h`).
//
// ★★★ WHY EVERY CASE COUNTS WRITES INSTEAD OF ASSERTING A VERDICT. Seventeen instruments in this arc were GREEN
//     against the very defect they were written to catch, and the shape of that failure is always the same: the
//     verdict is a value the implementation chooses, so a wrong implementation can choose the right value while
//     doing the wrong thing. A WRITE COUNT is not a value the implementation chooses — it is a consequence. ⇒ the
//     absent/corrupt matrix is pinned as EIGHT CELLS × (verdict, loads, saves), and "MUST NOT recover" is measured
//     as `saves == 0`, never as "it returned refused".
//
// ⛔ THE LIMIT OF THE CLAIM, unchanged from slice 1 and §PROV-TX: the store is a FAKE. No NVS/LittleFS write, no
//    wear, no reset-during-write ([[B193]]); §UI-15 slice 7 owns the power-cut question.
#include <doctest.h>
#include <cmath>
#include <cstddef>            // offsetof — the layout pins
#include <initializer_list>   // the braced range-for lists (slot indices) — required, not implied by <doctest.h>
#include <cstring>
#include "firmware_join_profiles.h"
#include "firmware_config_parse.h"   // mrfw::parse_index_strict — the console's index gate (blocker 3); pulled in
                                     // transitively too, named here because a case below drives it directly

namespace {

// The COUNTING store. ★ `state` is the THREE-valued answer, so a test can put the fake in `absent` or `invalid`
// without having to forge bytes — and `rec` is only assigned on a successful save, so "the stored record did not
// move" is measurable too.
struct FakeProfileStore : mrfw::IJoinStore {
    mrnv::JoinBlob   rec{};
    mrnv::JoinBlob   written{};
    mrnv::JoinRead   state = mrnv::JoinRead::ok;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    // ★ On a NON-ok read, deposit `rec` instead of garbage. ⛔ NOT a convenience: it is the ONLY way to build the
    //   case where a CORRUPT record's bytes happen to equal a fresh empty one, which is what makes "reset must not
    //   coalesce a corrupt store away" an exercised property rather than an argued one.
    bool deposit_rec = false;

    mrnv::JoinRead load(mrnv::JoinBlob& out) override {
        ++loads;
        // ⓘ On a NON-ok answer the fake deposits GARBAGE, deliberately: the real `read_slot` may leave a partial
        //   record behind, and the service is required to re-init rather than trust it. A fake that left `out`
        //   pristine would make that requirement untestable.
        if (state != mrnv::JoinRead::ok) {
            if (deposit_rec) out = rec; else std::memset(&out, 0xA5, sizeof out);
            return state;
        }
        out = rec;
        return mrnv::JoinRead::ok;
    }
    bool save(const mrnv::JoinBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        written = b; rec = b; state = mrnv::JoinRead::ok;
        return true;
    }
};

struct Fix {
    FakeProfileStore store;
    mrfw::JoinProfileService svc{store};
    Fix() { mrnv::join_blob_init(store.rec); }      // a VALID, EMPTY record is the ordinary starting point
};

mrfw::JoinRequest req(uint8_t layer, double freq_mhz, double bw_khz, uint8_t sf) {
    mrfw::JoinRequest r{};
    r.layer = layer; r.freq_mhz = freq_mhz; r.bw_khz = bw_khz; r.routing_sf = sf;
    return r;
}

}  // namespace

// ================================================================================ the RECORD itself
// ★ The two `static_assert`s live in device_nv.h (they must hold on ARM and Xtensa too, where no test runs); these
//   cases pin what a HOST can additionally see — that the layout has no hidden slack and the header owns the pad.
TEST_CASE("ui15-prof: the /mrjoin record is 24 B per profile and 8 + 4x24 overall, with the pad in the HEADER") {
    CHECK(sizeof(mrnv::JoinProfile) == 24u);
    CHECK(sizeof(mrnv::JoinBlob)    == 8u + 4u * 24u);
    CHECK(mrnv::kJoinProfiles == 4);
    // the four one-byte fields pack ahead of the two uint32s — no per-profile padding was invented (spec §3)
    CHECK(offsetof(mrnv::JoinProfile, freq_hz) == 4u);
    CHECK(offsetof(mrnv::JoinProfile, bw_hz)   == 8u);
    CHECK(offsetof(mrnv::JoinProfile, name)    == 12u);
    CHECK(sizeof(mrnv::JoinProfile::name)      == 12u);
    CHECK(offsetof(mrnv::JoinBlob, prof)       == 8u);   // magic(4) + version(2) + reserved(2)
}

TEST_CASE("ui15-prof: /mrjoin has its OWN magic and version — ⛔ never /mrcfg's") {
    CHECK(mrnv::kJoinMagic != mrnv::kMagic);
    CHECK(mrnv::kJoinMagic == 0x4D524A31u);              // 'MRJ1'
    mrnv::JoinBlob b{};
    mrnv::join_blob_init(b);
    CHECK(b.magic    == mrnv::kJoinMagic);
    CHECK(b.version  == mrnv::kJoinVersion);
    CHECK(b.reserved == 0u);                             // ★ zeroed => part of the byte-identical compare
    CHECK(mrfw::join_profile_count(b) == 0);
}

// ★ PIN 3 — WRONG MAGIC · WRONG VERSION · WRONG SIZE ⇒ THE WHOLE RECORD IS REJECTED. ⛔ There is no partial parse:
//   a record that fails any one of the three is `invalid`, never "three good slots and one bad".
TEST_CASE("ui15-prof: magic / version / size each reject the WHOLE record, and only -1 reads as ABSENT") {
    mrnv::JoinBlob b{};
    mrnv::join_blob_init(b);
    const int full = (int)sizeof b;

    CHECK(mrnv::join_blob_state(b, full) == mrnv::JoinRead::ok);

    SUBCASE("wrong magic")   { b.magic   = mrnv::kMagic;               CHECK(mrnv::join_blob_state(b, full) == mrnv::JoinRead::invalid); }
    SUBCASE("magic zeroed")  { b.magic   = 0;                          CHECK(mrnv::join_blob_state(b, full) == mrnv::JoinRead::invalid); }
    SUBCASE("version high")  { b.version = (uint16_t)(mrnv::kJoinVersion + 1); CHECK(mrnv::join_blob_state(b, full) == mrnv::JoinRead::invalid); }
    SUBCASE("version zero")  { b.version = 0;                          CHECK(mrnv::join_blob_state(b, full) == mrnv::JoinRead::invalid); }
    SUBCASE("short read")    { CHECK(mrnv::join_blob_state(b, full - 1) == mrnv::JoinRead::invalid); }
    SUBCASE("zero length")   { CHECK(mrnv::join_blob_state(b, 0)        == mrnv::JoinRead::invalid); }
    // ⛔⛔ THE `full + 1` SUBCASE THAT USED TO SIT HERE IS DELETED, NOT MOVED, AND THE DELETION IS THE POINT: NO
    //     BACKEND CAN EVER RETURN IT. Both arms read at most `len` bytes, so an over-length record comes back as a
    //     full-length read of a VALID PREFIX — `full`, not `full + 1` — and this predicate answered `ok` to it. An
    //     instrument that asserts an unreachable input is an instrument that cannot fail; it is replaced by
    //     test_device_nv.cpp's "a file LONGER than the record is REJECTED", which drives the real sequence with a
    //     real over-length file. What survives HERE is the fact that predicate carries: the SIZE, not the length.
    SUBCASE("over-long, AS THE BACKEND REALLY REPORTS IT: a full-length read of a valid prefix + the file's size") {
        mrnv::SlotIo io; io.oversize = true;
        CHECK(mrnv::join_blob_state(b, full, io) == mrnv::JoinRead::invalid);
        CHECK(mrnv::join_blob_state(b, full)     == mrnv::JoinRead::ok);   // ⛔ the control: the length alone is blind
    }
    // ★★★ A BACKEND THAT WOULD NOT OPEN IS **NOT** A FRESH DEVICE. -1 is the same code either way, so only `io` can
    //     tell them apart — and the arm that could not tell them apart printed `NO PROFILES` over a broken flash.
    SUBCASE("backend would not open") {
        mrnv::SlotIo io; io.backend_failed = true;
        CHECK(mrnv::join_blob_state(b, mrnv::kSlotAbsent, io) == mrnv::JoinRead::io_failed);
        CHECK(mrnv::join_blob_state(b, mrnv::kSlotAbsent)     == mrnv::JoinRead::absent);   // ⛔ the control
        // ⛔ and it wins over EVERY other reading: a storage failure that also looks like a good record is still a
        //    storage failure, because the bytes in `out` were never actually read.
        CHECK(mrnv::join_blob_state(b, full, io) == mrnv::JoinRead::io_failed);
    }
    // ★★ THE HONESTY LINE, AND IT IS THE REASON THIS IS `== -1` RATHER THAN `< 0`: a nRF52 corrupt-CTZ read returns
    //    an LFS error code (e.g. -84 / -5), and reading THAT as "absent" would show a corrupted store as a fresh
    //    device. -1 is the primitive's "no such slot" and nothing else.
    SUBCASE("absent")        { CHECK(mrnv::join_blob_state(b, -1)  == mrnv::JoinRead::absent);  }
    SUBCASE("read error -5") { CHECK(mrnv::join_blob_state(b, -5)  == mrnv::JoinRead::invalid); }
    SUBCASE("read error -84"){ CHECK(mrnv::join_blob_state(b, -84) == mrnv::JoinRead::invalid); }
}

// ================================================================================ PIN 1 + PIN 5 — the round trip
// ★★ 869.4625 MHz IS 869462.5 kHz — NOT AN INTEGER — BUT EXACTLY 869462500 Hz. This case is the whole reason the
//    record stores Hz, and it is also the units pin: a kHz<->Hz mutation moves these numbers by 1000x.
TEST_CASE("ui15-prof: set -> load round-trips EXACTLY, including 869.4625 MHz => freq_hz 869462500") {
    Fix f;
    const mrfw::ProfileResult r = f.svc.set(2, req(4, 869.4625, 125.0, 9), "hut", 3);
    CHECK(r.verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.saves == 1);

    mrnv::JoinBlob got{};
    CHECK(f.svc.list(got).verdict == mrfw::ProfileVerdict::ok);
    CHECK(std::memcmp(&got, &f.store.written, sizeof got) == 0);        // byte-identical round trip

    const mrnv::JoinProfile& p = got.prof[1];
    CHECK(p.present    == 1);
    CHECK(p.layer      == 4);
    CHECK(p.routing_sf == 9);
    CHECK(p.freq_hz    == 869462500u);      // ★ EXACT — a uint32 kHz field would have stored 869462 and moved the radio
    CHECK(p.bw_hz      == 125000u);         // ★ kHz in, Hz stored: 125 -> 125000, ⛔ not 125 and ⛔ not 125000000
    CHECK(p.name_len   == 3);
    CHECK(std::memcmp(p.name, "hut", 3) == 0);
    CHECK(mrfw::join_profile_count(got) == 1);
    // the other three slots are untouched and empty
    CHECK(got.prof[0].present == 0);
    CHECK(got.prof[2].present == 0);
    CHECK(got.prof[3].present == 0);
}

// ★ PIN 5, STATED AS A TABLE so a units mutation cannot be green on one row and red on none. Each row is a value an
//   operator really types; the expected integers are the ONLY ones that are right.
TEST_CASE("ui15-prof: the units are MHz in / Hz stored and kHz in / Hz stored — ⛔ never swapped") {
    struct Row { double mhz; double khz; uint32_t freq_hz; uint32_t bw_hz; } rows[] = {
        { 869.4625, 125.0,  869462500u, 125000u },
        { 868.0,    250.0,  868000000u, 250000u },
        { 869.525,   62.5,  869525000u,  62500u },
        { 915.0,     41.67, 915000000u,  41670u },
        { 433.175,   31.25, 433175000u,  31250u },
        { 100.0,      7.0,  100000000u,   7000u },   // both domain floors
        { 1000.0,   500.0, 1000000000u, 500000u },   // both domain ceilings — and 1e9 fits a uint32 comfortably
    };
    for (const Row& row : rows) {
        CAPTURE(row.mhz);
        Fix f;
        CHECK(f.svc.set(1, req(4, row.mhz, row.khz, 9), nullptr, 0).verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.written.prof[0].freq_hz == row.freq_hz);
        CHECK(f.store.written.prof[0].bw_hz   == row.bw_hz);
        // ★★ AND THE REVERSE CONVERSION IS THE SAME AUTHORITY: what slice 6 hands back to `JoinService` must be the
        //    number the operator typed, or a profile would join a different carrier than it displays.
        const mrfw::JoinRequest back = mrfw::join_request_from_profile(f.store.written.prof[0]);
        CHECK(back.freq_mhz == row.mhz);   // ⛔ EXACT, not near: whole Hz / 1e6 is exact in a double at these
        CHECK(back.bw_khz   == row.khz);   //    magnitudes, so an approximate compare would hide a real drift
        CHECK(back.layer      == 4);
        CHECK(back.routing_sf == 9);
    }
}

// ⓘ The FULL layer byte is stored, exactly as `/mrcfg`'s `layer0_id` does — the wire leaf nibble is DERIVED, and
//   storing it twice is what would let the two drift (slice 1's correlation rule depends on the distinction).
TEST_CASE("ui15-prof: a profile stores the FULL layer byte; the leaf nibble stays derived") {
    Fix f;
    CHECK(f.svc.set(1, req(200, 868.0, 125.0, 8), nullptr, 0).verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.written.prof[0].layer == 200);
    CHECK(mrfw::join_leaf_of_layer(f.store.written.prof[0].layer) == 8);
}

// ================================================================================ PIN 2 — the EIGHT-CELL MATRIX
// ★★★ THE NORMATIVE TABLE (spec §3), EVERY CELL WITH ITS WRITE COUNT. This is the case that makes "clear must not
//     recover" and "an absent store is not an error" facts rather than intentions.
TEST_CASE("ui15-prof: the absent/corrupt matrix — all EIGHT cells, with WRITE COUNTS") {
    // ---- ABSENT ----------------------------------------------------------------------------------------------
    SUBCASE("ABSENT + set: seeds a valid empty record, applies the slot, EXACTLY ONE write") {
        Fix f; f.store.state = mrnv::JoinRead::absent;
        const mrfw::ProfileResult r = f.svc.set(3, req(4, 868.0, 125.0, 9), nullptr, 0);
        CHECK(r.verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 1);                      // ★ ONE — ⛔ not two (seed + edit share the write)
        CHECK(f.store.written.magic   == mrnv::kJoinMagic);
        CHECK(f.store.written.version == mrnv::kJoinVersion);
        CHECK(f.store.written.prof[2].present == 1);
        CHECK(mrfw::join_profile_count(f.store.written) == 1);
        // ⛔ the seed must be a CLEAN record, not the garbage the failed read deposited
        CHECK(f.store.written.reserved == 0u);
        CHECK(f.store.written.prof[0].present == 0);
        CHECK(f.store.written.prof[1].present == 0);
        CHECK(f.store.written.prof[3].present == 0);
    }
    SUBCASE("ABSENT + list: NO PROFILES, an ordinary state — ZERO writes") {
        Fix f; f.store.state = mrnv::JoinRead::absent;
        mrnv::JoinBlob out{};
        const mrfw::ProfileResult r = f.svc.list(out);
        CHECK(r.verdict == mrfw::ProfileVerdict::empty);
        CHECK(r.err     == mrfw::ProfileErr::none);     // ⛔ NOT a failure
        CHECK(f.store.saves == 0);
        CHECK(mrfw::join_profile_count(out) == 0);
        CHECK(out.magic == mrnv::kJoinMagic);           // ⛔ the caller never sees the failed read's garbage
    }
    SUBCASE("ABSENT + clear: NO CHANGE, ★ ZERO WRITES, and NOT a failure") {
        Fix f; f.store.state = mrnv::JoinRead::absent;
        const mrfw::ProfileResult r = f.svc.clear(2);
        CHECK(r.verdict == mrfw::ProfileVerdict::empty);
        CHECK(r.err     == mrfw::ProfileErr::none);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 0);                      // ★★ THE PIN — a "helpful" seed here would be wear AND a lie
    }
    SUBCASE("ABSENT + reset confirm: already empty ⇒ ★ ZERO WRITES, and NOT a failure") {
        Fix f; f.store.state = mrnv::JoinRead::absent;
        const mrfw::ProfileResult r = f.svc.reset(/*confirmed=*/true);
        CHECK(r.verdict == mrfw::ProfileVerdict::empty);
        CHECK(r.err     == mrfw::ProfileErr::none);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 0);                      // ★ THE PIN
    }
    // ---- CORRUPT ---------------------------------------------------------------------------------------------
    SUBCASE("CORRUPT + set: refuses PROFILE STORE INVALID — ZERO writes") {
        Fix f; f.store.state = mrnv::JoinRead::invalid;
        const mrfw::ProfileResult r = f.svc.set(1, req(4, 868.0, 125.0, 9), nullptr, 0);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::store_invalid);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 0);                      // ⛔ a set must not silently reseed over three unread slots
    }
    SUBCASE("CORRUPT + list: PROFILE STORE INVALID — ZERO writes, ⛔ no repair on a read") {
        Fix f; f.store.state = mrnv::JoinRead::invalid;
        mrnv::JoinBlob out{};
        const mrfw::ProfileResult r = f.svc.list(out);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::store_invalid);
        CHECK(f.store.saves == 0);
    }
    SUBCASE("CORRUPT + clear: ⛔⛔ MUST NOT RECOVER — refuses, ZERO writes") {
        Fix f; f.store.state = mrnv::JoinRead::invalid;
        const mrfw::ProfileResult r = f.svc.clear(2);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::store_invalid);
        CHECK(f.store.saves == 0);                      // ★★★ PIN 8 — a backdoor repair would rewrite 3 unread slots
        CHECK(f.store.state == mrnv::JoinRead::invalid);// ...and the store is STILL corrupt afterwards
    }
    SUBCASE("CORRUPT + reset confirm: ★ the ONLY recovery path — EXACTLY ONE write, a valid empty record") {
        Fix f; f.store.state = mrnv::JoinRead::invalid;
        const mrfw::ProfileResult r = f.svc.reset(/*confirmed=*/true);
        CHECK(r.verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 1);
        CHECK(f.store.written.magic   == mrnv::kJoinMagic);
        CHECK(f.store.written.version == mrnv::kJoinVersion);
        CHECK(mrfw::join_profile_count(f.store.written) == 0);
        CHECK(f.store.state == mrnv::JoinRead::ok);     // ...and the store is repaired
    }
}

// ⛔ THE CORRUPT-RESET CANNOT BE COALESCED AWAY. If the corrupt bytes happened to compare equal to a fresh empty
//    record, a byte-compare-first implementation would report `unchanged`, write nothing, and leave the store
//    corrupt — the one path where a zero-write answer is the DISHONEST one. Driven with a fake whose garbage IS a
//    valid empty record, so the property is exercised rather than argued.
TEST_CASE("ui15-prof: reset on a corrupt store writes even when the bytes already look empty") {
    Fix f;
    mrnv::join_blob_init(f.store.rec);          // the bytes a successful read WOULD have produced…
    f.store.deposit_rec = true;                 // …and the read DOES hand them over…
    f.store.state = mrnv::JoinRead::invalid;    // …but reports the record as unreadable
    const mrfw::ProfileResult r = f.svc.reset(/*confirmed=*/true);
    CHECK(r.verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.saves == 1);                  // ⛔ never `unchanged`
}

// ================================================================================ PIN 4 — the index, both ends
TEST_CASE("ui15-prof: slot 0 and slot 5 refuse and write NOTHING; 1 and 4 work (off-by-one, both ends)") {
    for (long bad : { -1L, 0L, 5L, 6L, 255L }) {
        CAPTURE(bad);
        Fix f;
        const mrfw::ProfileResult s = f.svc.set(bad, req(4, 868.0, 125.0, 9), nullptr, 0);
        CHECK(s.verdict == mrfw::ProfileVerdict::refused);
        CHECK(s.err     == mrfw::ProfileErr::bad_index);
        CHECK(f.store.loads == 0);                       // ⛔ a bad index does not even READ the record
        CHECK(f.store.saves == 0);
        const mrfw::ProfileResult c = f.svc.clear(bad);
        CHECK(c.verdict == mrfw::ProfileVerdict::refused);
        CHECK(c.err     == mrfw::ProfileErr::bad_index);
        CHECK(f.store.loads == 0);
        CHECK(f.store.saves == 0);
    }
    for (long good : { 1L, 2L, 3L, 4L }) {
        CAPTURE(good);
        Fix f;
        CHECK(f.svc.set(good, req(4, 868.0, 125.0, 9), nullptr, 0).verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.saves == 1);
        CHECK(f.store.written.prof[good - 1].present == 1);   // ★ the 1-based slot lands in the 0-based cell
        CHECK(mrfw::join_profile_count(f.store.written) == 1);
        CHECK(f.svc.clear(good).verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.saves == 2);
        CHECK(mrfw::join_profile_count(f.store.written) == 0);
    }
}

// ================================================================================ PIN 6 — the NaN boundary
// ★★★ `validate_join` ACCEPTS a non-finite (that is [[B216]], deliberate, and ⛔ NOT fixed here). This record is
//     INTEGRAL, so `static_cast<uint32_t>(NaN)` would be UNDEFINED BEHAVIOUR ⇒ the refusal lives at the PROFILE
//     boundary. Both halves are pinned in one case: the shared validator still accepts, and the profile verb still
//     refuses. A "fix" moved into the shared validator turns the FIRST half red — which is the point.
TEST_CASE("ui15-prof: a non-finite freq/bw is REFUSED with ZERO writes — while validate_join still accepts it") {
    const double nan_v = std::nan("");
    const double inf_v = HUGE_VAL;
    struct Row { const char* what; mrfw::JoinRequest rq; } rows[] = {
        { "freq NaN",  req(4, nan_v,   125.0,  9) },
        { "bw NaN",    req(4, 869.525, nan_v,  9) },
        { "freq +inf", req(4, inf_v,   125.0,  9) },
        { "bw -inf",   req(4, 869.525, -inf_v, 9) },
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        Fix f;
        const mrfw::ProfileResult r = f.svc.set(1, row.rq, nullptr, 0);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::not_finite);   // ⛔ NOT invalid_freq — a NaN is not an out-of-range number
        CHECK(f.store.loads == 0);                          // ⛔ 0 loads
        CHECK(f.store.saves == 0);                          // ★ 0 writes
    }
    // ⛔⛔ THE OTHER HALF, AND IT IS A PIN AGAINST A "FIX" AS MUCH AS AGAINST A REGRESSION: the SHARED validator's
    //    behaviour is untouched, so the existing `join` verb still does exactly what it did (C1).
    CHECK(mrfw::validate_join(req(4, nan_v,   125.0, 9)) == mrfw::JoinErr::none);
    CHECK(mrfw::validate_join(req(4, 869.525, nan_v, 9)) == mrfw::JoinErr::none);
}

// ================================================================================ the DOMAIN arms
// ★ ONE AUTHORITY: every arm below is `validate_join`'s, relabelled. Each row has the other three fields valid, so
//   each is individually load-bearing.
TEST_CASE("ui15-prof: every domain refusal costs ZERO loads and ZERO writes, and names its own field") {
    struct Row { const char* what; mrfw::JoinRequest rq; mrfw::ProfileErr err; } rows[] = {
        { "layer 0",   req(0, 869.525, 125.0,  9), mrfw::ProfileErr::invalid_layer },
        { "freq low",  req(4,  99.999, 125.0,  9), mrfw::ProfileErr::invalid_freq  },
        { "freq high", req(4, 1000.01, 125.0,  9), mrfw::ProfileErr::invalid_freq  },
        { "bw low",    req(4, 869.525,   6.99, 9), mrfw::ProfileErr::invalid_bw    },
        { "bw high",   req(4, 869.525, 500.01, 9), mrfw::ProfileErr::invalid_bw    },
        { "sf low",    req(4, 869.525, 125.0,  4), mrfw::ProfileErr::invalid_sf    },
        { "sf high",   req(4, 869.525, 125.0, 13), mrfw::ProfileErr::invalid_sf    },
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        Fix f;
        const mrfw::ProfileResult r = f.svc.set(1, row.rq, nullptr, 0);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == row.err);
        CHECK(f.store.loads == 0);
        CHECK(f.store.saves == 0);
    }
}

TEST_CASE("ui15-prof: a name longer than the slot is REFUSED, ⛔ never truncated (C2)") {
    Fix f;
    const char* thirteen = "0123456789abc";
    const mrfw::ProfileResult r = f.svc.set(1, req(4, 868.0, 125.0, 9), thirteen, 13);
    CHECK(r.verdict == mrfw::ProfileVerdict::refused);
    CHECK(r.err     == mrfw::ProfileErr::name_too_long);
    CHECK(f.store.loads == 0);
    CHECK(f.store.saves == 0);
    // ...and EXACTLY twelve is accepted (the other end of the same boundary)
    Fix g;
    CHECK(g.svc.set(1, req(4, 868.0, 125.0, 9), "0123456789ab", 12).verdict == mrfw::ProfileVerdict::ok);
    CHECK(g.store.written.prof[0].name_len == 12);
    CHECK(std::memcmp(g.store.written.prof[0].name, "0123456789ab", 12) == 0);
}

// ★ THE NAME TAIL IS ZEROED, AND IT IS NOT COSMETIC: a stale tail byte would make two logically identical profiles
//   compare different, so every re-set would write flash. Determinism is what makes coalescing meaningful.
TEST_CASE("ui15-prof: the unused name bytes are ZERO, so a shorter name leaves no tail behind") {
    Fix f;
    CHECK(f.svc.set(1, req(4, 868.0, 125.0, 9), "LONGERNAME", 10).verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.svc.set(1, req(4, 868.0, 125.0, 9), "ab",         2).verdict == mrfw::ProfileVerdict::ok);
    const mrnv::JoinProfile& p = f.store.written.prof[0];
    CHECK(p.name_len == 2);
    for (size_t i = 2; i < sizeof p.name; ++i) { CAPTURE(i); CHECK(p.name[i] == '\0'); }
}

// ================================================================================ PIN 7 — write coalescing
// ★★ COUNTED, NOT ASSERTED: the second call must leave `saves` at 1. A verdict-only test would stay green against an
//    implementation that wrote every time and reported `ok`.
TEST_CASE("ui15-prof: re-setting IDENTICAL values performs ZERO further writes") {
    Fix f;
    CHECK(f.svc.set(2, req(4, 869.4625, 62.5, 9), "hut", 3).verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.saves == 1);
    for (int again = 0; again < 3; ++again) {
        CAPTURE(again);
        const mrfw::ProfileResult r = f.svc.set(2, req(4, 869.4625, 62.5, 9), "hut", 3);
        CHECK(r.verdict == mrfw::ProfileVerdict::unchanged);
        CHECK(r.err     == mrfw::ProfileErr::none);
        CHECK(f.store.loads == 2 + again);      // it still READS (it must, to compare)…
        CHECK(f.store.saves == 1);              // ★ …and it still does not WRITE
    }
    // ⓘ …and a genuinely different value DOES write, so the guard is not simply "never writes"
    CHECK(f.svc.set(2, req(4, 869.4625, 62.5, 10), "hut", 3).verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.saves == 2);
}

TEST_CASE("ui15-prof: clearing an ALREADY-empty slot of a valid record performs ZERO writes") {
    Fix f;
    const mrfw::ProfileResult r = f.svc.clear(4);
    CHECK(r.verdict == mrfw::ProfileVerdict::unchanged);
    CHECK(f.store.loads == 1);
    CHECK(f.store.saves == 0);
}

TEST_CASE("ui15-prof: reset confirm on an already-empty VALID record performs ZERO writes") {
    Fix f;
    const mrfw::ProfileResult r = f.svc.reset(/*confirmed=*/true);
    CHECK(r.verdict == mrfw::ProfileVerdict::unchanged);
    CHECK(f.store.loads == 1);
    CHECK(f.store.saves == 0);
}

// ⛔ A MISSING `confirm` REFUSES WITHOUT LOADING AND WITHOUT WRITING.
TEST_CASE("ui15-prof: reset without confirm refuses — ZERO loads, ZERO writes") {
    Fix f;
    CHECK(f.svc.set(1, req(4, 868.0, 125.0, 9), nullptr, 0).verdict == mrfw::ProfileVerdict::ok);
    const int saves_before = f.store.saves, loads_before = f.store.loads;
    const mrfw::ProfileResult r = f.svc.reset(/*confirmed=*/false);
    CHECK(r.verdict == mrfw::ProfileVerdict::refused);
    CHECK(r.err     == mrfw::ProfileErr::needs_confirm);
    CHECK(f.store.loads == loads_before);
    CHECK(f.store.saves == saves_before);
    CHECK(mrfw::join_profile_count(f.store.rec) == 1);   // ...and the profile survives
}

// ★ `reset confirm` DISCARDS ALL FOUR — the reason it needs a confirm at all.
TEST_CASE("ui15-prof: reset confirm empties every slot in ONE write") {
    Fix f;
    for (long i = 1; i <= 4; ++i) CHECK(f.svc.set(i, req((uint8_t)i, 868.0, 125.0, 9), nullptr, 0).verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.saves == 4);
    CHECK(mrfw::join_profile_count(f.store.rec) == 4);
    CHECK(f.svc.reset(/*confirmed=*/true).verdict == mrfw::ProfileVerdict::ok);
    CHECK(f.store.saves == 5);                           // ★ ONE more, not four
    CHECK(mrfw::join_profile_count(f.store.written) == 0);
}

// ⓘ …and `clear` touches ONLY its own slot. Counted against the other three, because "it cleared the right one" and
//   "it cleared only one" are different claims.
TEST_CASE("ui15-prof: clear empties exactly one slot and leaves the other three byte-identical") {
    Fix f;
    for (long i = 1; i <= 4; ++i) CHECK(f.svc.set(i, req((uint8_t)(i * 3), 868.0 + i, 125.0, 9), "n", 1).verdict == mrfw::ProfileVerdict::ok);
    const mrnv::JoinBlob before = f.store.rec;
    CHECK(f.svc.clear(2).verdict == mrfw::ProfileVerdict::ok);
    const mrnv::JoinBlob& after = f.store.written;
    CHECK(after.prof[1].present == 0);
    const mrnv::JoinProfile zero{};
    CHECK(std::memcmp(&after.prof[1], &zero, sizeof(mrnv::JoinProfile)) == 0);                      // fully zeroed
    CHECK(std::memcmp(&after.prof[0], &before.prof[0], sizeof(mrnv::JoinProfile)) == 0);
    CHECK(std::memcmp(&after.prof[2], &before.prof[2], sizeof(mrnv::JoinProfile)) == 0);
    CHECK(std::memcmp(&after.prof[3], &before.prof[3], sizeof(mrnv::JoinProfile)) == 0);
    CHECK(mrfw::join_profile_count(after) == 3);
}

// ================================================================================ the STORE failure arm
TEST_CASE("ui15-prof: a failed save reports nv_failed after EXACTLY ONE attempt — no retry") {
    for (int verb = 0; verb < 3; ++verb) {
        CAPTURE(verb);
        Fix f;
        f.store.save_ok = false;
        mrfw::ProfileResult r{};
        if      (verb == 0) r = f.svc.set(1, req(4, 868.0, 125.0, 9), nullptr, 0);
        else if (verb == 1) { f.store.rec.prof[0].present = 1; r = f.svc.clear(1); }
        else                { f.store.state = mrnv::JoinRead::invalid; r = f.svc.reset(true); }
        CHECK(r.verdict == mrfw::ProfileVerdict::nv_failed);
        CHECK(r.err     == mrfw::ProfileErr::nv_save_failed);
        CHECK(f.store.saves == 1);      // ★ ONE attempt — not zero (it must try), not two (no retry)
    }
}

// ================================================================================ the enum voices
TEST_CASE("ui15-prof: every verdict and every error has its own name (the -Wswitch discipline's other half)") {
    CHECK(std::strcmp(mrfw::profile_verdict_name(mrfw::ProfileVerdict::ok),        "ok")        == 0);
    CHECK(std::strcmp(mrfw::profile_verdict_name(mrfw::ProfileVerdict::unchanged), "unchanged") == 0);
    CHECK(std::strcmp(mrfw::profile_verdict_name(mrfw::ProfileVerdict::empty),     "empty")     == 0);
    CHECK(std::strcmp(mrfw::profile_verdict_name(mrfw::ProfileVerdict::refused),   "refused")   == 0);
    CHECK(std::strcmp(mrfw::profile_verdict_name(mrfw::ProfileVerdict::nv_failed), "nv_failed") == 0);
    const mrfw::ProfileErr errs[] = {
        mrfw::ProfileErr::none, mrfw::ProfileErr::bad_index, mrfw::ProfileErr::invalid_layer,
        mrfw::ProfileErr::invalid_freq, mrfw::ProfileErr::invalid_bw, mrfw::ProfileErr::invalid_sf,
        mrfw::ProfileErr::not_finite, mrfw::ProfileErr::name_too_long, mrfw::ProfileErr::store_invalid,
        mrfw::ProfileErr::store_io_failed, mrfw::ProfileErr::needs_confirm, mrfw::ProfileErr::nv_save_failed,
    };
    for (mrfw::ProfileErr e : errs) {
        CAPTURE((int)e);
        CHECK(std::strcmp(mrfw::profile_err_name(e), "?") != 0);   // every arm is written
    }
    CHECK(std::strcmp(mrfw::profile_err_name(mrfw::ProfileErr::not_finite),    "not_finite")    == 0);
    CHECK(std::strcmp(mrfw::profile_err_name(mrfw::ProfileErr::store_invalid), "store_invalid") == 0);
    CHECK(std::strcmp(mrfw::profile_err_name(mrfw::ProfileErr::needs_confirm), "needs_confirm") == 0);
    // ⛔ THE TWO UNREADABLE ANSWERS MUST NOT SHARE A NAME either — the console prints a DIFFERENT remedy for each.
    CHECK(std::strcmp(mrfw::profile_err_name(mrfw::ProfileErr::store_io_failed), "store_io_failed") == 0);
    CHECK(std::strcmp(mrfw::profile_err_name(mrfw::ProfileErr::store_io_failed),
                      mrfw::profile_err_name(mrfw::ProfileErr::store_invalid)) != 0);
}

// ============================================================ THE FOURTH STORE STATE (2026-08-19 QG correction)
// ★★★ THE BUG THIS PINS: the primitive could not report "the backend would not open", so `/mrjoin` announced NO
//     PROFILES over a filesystem that would not mount. With the state reachable, the matrix grows a THIRD column —
//     and every cell of it is ZERO WRITES, ★ INCLUDING `reset confirm`, which is the one that differs from CORRUPT.
TEST_CASE("ui15-prof: a STORAGE FAILURE refuses on all four verbs with ZERO writes — ⛔ never 'no profiles'") {
    SUBCASE("IO-FAILED + list: ⛔ NOT `empty` — the operator must not read this as a fresh device") {
        Fix f; f.store.state = mrnv::JoinRead::io_failed;
        mrnv::JoinBlob out{};
        const mrfw::ProfileResult r = f.svc.list(out);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);      // ⛔⛔ NOT ProfileVerdict::empty
        CHECK(r.err     == mrfw::ProfileErr::store_io_failed);  // ⛔ and NOT store_invalid — a different remedy
        CHECK(f.store.saves == 0);
    }
    SUBCASE("IO-FAILED + set: refused, ZERO writes") {
        Fix f; f.store.state = mrnv::JoinRead::io_failed;
        const mrfw::ProfileResult r = f.svc.set(1, req(4, 868.0, 125.0, 9), nullptr, 0);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::store_io_failed);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 0);
    }
    SUBCASE("IO-FAILED + clear: refused, ZERO writes") {
        Fix f; f.store.state = mrnv::JoinRead::io_failed;
        const mrfw::ProfileResult r = f.svc.clear(2);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::store_io_failed);
        CHECK(f.store.saves == 0);
    }
    // ★★★ THE CELL THAT SEPARATES THE TWO UNREADABLE STATES. On CORRUPT, `reset confirm` writes — that is its whole
    //     purpose. On IO-FAILED it MUST NOT: nothing was read, so the four profiles may be perfectly intact behind a
    //     transient mount failure, and a blind rewrite would destroy them to "repair" a fault it cannot repair.
    SUBCASE("IO-FAILED + reset confirm: ⛔⛔ REFUSED, ZERO WRITES — ⛔ NOT the corrupt-store recovery") {
        Fix f; f.store.state = mrnv::JoinRead::io_failed;
        const mrfw::ProfileResult r = f.svc.reset(/*confirmed=*/true);
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::store_io_failed);
        CHECK(f.store.loads == 1);
        CHECK(f.store.saves == 0);                              // ★★★ THE PIN
        CHECK(f.store.state == mrnv::JoinRead::io_failed);      // ...and nothing about the store was touched
    }
    // ⛔ the CORRUPT column is unchanged by all of the above — the two states do not bleed into each other.
    SUBCASE("CORRUPT + reset confirm still writes (the positive control for the cell above)") {
        Fix f; f.store.state = mrnv::JoinRead::invalid;
        CHECK(f.svc.reset(true).verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.saves == 1);
    }
}

// ============================================================ THE CONSOLE'S INDEX GATE (blocker 3, 2026-08-19)
// ★★ `src/firmware_config.cpp` is outside the native build, so what is driven here is the PAIR the verb is now built
//    from — `mrfw::parse_index_strict` (native-tested in test_firmware_config_parse.cpp) followed by the service —
//    composed exactly as `handle_joinprofile` composes them. ⛔ It is NOT a claim about the TU; it is the claim that
//    the composition refuses and writes nothing, which is the part a structural probe cannot show.
TEST_CASE("ui15-prof: a malformed slot token refuses and performs ZERO loads and ZERO writes") {
    // `atol` answered 2 for "2junk" and 1 for "1x" — both of which WROTE. Every token below must reach no verb.
    for (const char* tok : { "2junk", "1x", "", " ", "-1", "+2", "0x2", "2.0", "junk", "99999999999999999999" }) {
        CAPTURE(tok);
        Fix f;
        long slot = 0;
        const bool ok = mrfw::parse_index_strict(tok, slot);
        CHECK_FALSE(ok);                                  // ⛔ the token never becomes an index
        if (!ok) continue;                                // (the verb is not called at all — as the console does)
        f.svc.clear(slot);
    }
    // ★ and the out-of-range-but-numeric ends still refuse in the SERVICE, with zero loads (the existing pin)
    for (const char* tok : { "0", "5" }) {
        CAPTURE(tok);
        Fix f;
        long slot = -1;
        CHECK(mrfw::parse_index_strict(tok, slot));       // it IS a number…
        const mrfw::ProfileResult r = f.svc.clear(slot);  // …and the SERVICE is the one authority on the range
        CHECK(r.verdict == mrfw::ProfileVerdict::refused);
        CHECK(r.err     == mrfw::ProfileErr::bad_index);
        CHECK(f.store.loads == 0);
        CHECK(f.store.saves == 0);
    }
    // ★ the positive control: the four real slots still parse and still work
    for (long want = 1; want <= 4; ++want) {
        const char tok[2] = { (char)('0' + want), '\0' };
        CAPTURE(tok);
        long slot = 0;
        CHECK(mrfw::parse_index_strict(tok, slot));
        CHECK(slot == want);
        Fix f;
        CHECK(f.svc.set(slot, req(4, 868.0, 125.0, 9), nullptr, 0).verdict == mrfw::ProfileVerdict::ok);
        CHECK(f.store.written.prof[slot - 1].present == 1);
    }
}
