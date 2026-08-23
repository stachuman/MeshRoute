// MeshRoute — test/test_firmware_team_keyring.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-16 K1 — the `/mrteams` team-key keyring (`src/firmware_team_keyring.h` + the record in `src/device_nv.h`).
// Register row [[B240]]: a team CONTENT key that arrived over the air was adopted into RAM and never persisted.
//
// ★★★ WHY EVERY CASE COUNTS WRITES INSTEAD OF ASSERTING A VERDICT — the `/mrjoin` suite's rule, and it matters more
//     here: the verdict is a value the implementation CHOOSES, so a wrong implementation can choose the right value
//     while doing the wrong thing. A WRITE COUNT is a CONSEQUENCE. ⇒ "identical material writes nothing" is measured
//     as `saves == 0`, and "a full keyring never evicts" is measured as the four stored records being BYTE-IDENTICAL
//     afterwards — ⛔ never as "it returned refused".
//
// ⛔ THE LIMIT OF THE CLAIM, unchanged from the `/mrjoin` store and §PROV-TX: the store is a FAKE. No NVS/LittleFS
//    write, no flash WEAR, no reset-during-write ([[B193]]). The keyring's power-cut behaviour is METAL-ONLY (M2).
//
// ⛔⛔ AND ONE HONEST GAP, MARKED RATHER THAN GLOSSED: the service's transient blob is a STACK frame, and reading it
//    after the call returns is undefined behaviour — so the wipe cannot be observed "in situ" by any host test. That
//    is exactly why `SecretWipeGuard` is a NAMED type: the case below drives the guard itself over a carrier that
//    OUTLIVES it, which is fully-defined and mutation-visible. The service's USE of that guard is verified by
//    inspection, ⛔ and this file does not claim otherwise.
#include <doctest.h>
#include <cstddef>
#include <cstring>
#include "firmware_team_keyring.h"
#include "identity.h"    // meshroute::team_channel_key_derive — the REAL derivation the live seam's fake mirrors

namespace {

using mrfw::KeyringVerdict;
using mrfw::KeyringErr;
using mrfw::KeyringRestore;

// The COUNTING store. ★ `state` is the FOUR-valued answer, so a case can put the fake in `absent`, `invalid` or
// `io_failed` without forging bytes; `rec` is assigned only on a successful save, so "the stored record did not
// move" is measurable too.
struct FakeKeyStore : mrfw::ITeamKeyStore {
    mrnv::TeamKeyBlob rec{};
    mrnv::TeamKeyBlob written{};
    mrnv::TeamKeyRead state = mrnv::TeamKeyRead::ok;
    int  loads = 0, saves = 0;
    bool save_ok = true;
    // ★ On a NON-ok read the fake deposits GARBAGE, deliberately: the real `read_slot` may leave a PARTIAL record
    //   behind, and the service is required to re-init rather than trust it (device_nv.h's §nv-ritual warning).
    bool deposit_rec = false;

    mrnv::TeamKeyRead load(mrnv::TeamKeyBlob& out) override {
        ++loads;
        if (state != mrnv::TeamKeyRead::ok) {
            if (deposit_rec) out = rec; else std::memset(&out, 0xA5, sizeof out);
            return state;
        }
        out = rec;
        return mrnv::TeamKeyRead::ok;
    }
    bool save(const mrnv::TeamKeyBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        written = b; rec = b; state = mrnv::TeamKeyRead::ok;
        return true;
    }
};

// The LIVE seam's fake. ★★ IT IS A 1:1 MIRROR OF `Node::team_channel_key_adopt` (`lib/core/node.cpp:112-121`) OVER
//    THE **SAME** `team_channel_key_derive`, ⛔ not a re-invention of the rule: derive the public half from the
//    private one, cross-check the supplied public half, refuse on mismatch or on a degenerate scalar. That is what
//    makes the corruption-rejection case a measurement of the REAL rule rather than of a stub that says "no".
struct FakeKeyLive : mrfw::ITeamKeyLive {
    int  calls = 0, clear_calls = 0;
    bool installed = false;
    uint8_t pub[32] = {}, priv[32] = {};
    // ★ THE GOVERNANCE HALF (QG blocker 1), mirroring `Node::team_channel_key_clear`: the pair is destroyed and the
    //   node answers "no key". Counted, so "the keyring's verdict was APPLIED" is a measurement, not an inference.
    void clear_key() override {
        ++clear_calls;
        installed = false;
        std::memset(pub, 0, sizeof pub);
        std::memset(priv, 0, sizeof priv);
    }
    bool adopt_key(const uint8_t in_pub[32], const uint8_t in_priv[32]) override {
        ++calls;
        uint8_t derived_pub[32], canon_priv[32];
        if (!meshroute::team_channel_key_derive(derived_pub, canon_priv, in_priv)) return false;
        uint8_t diff = 0;
        for (int i = 0; i < 32; ++i) diff = static_cast<uint8_t>(diff | (derived_pub[i] ^ in_pub[i]));
        if (diff != 0) return false;
        std::memcpy(pub, derived_pub, 32);
        std::memcpy(priv, canon_priv, 32);
        installed = true;
        return true;
    }
};

struct Fix {
    FakeKeyStore store;
    mrfw::TeamKeyringService svc{store};
    Fix() { mrnv::team_key_blob_init(store.rec); }   // a VALID, EMPTY keyring is the ordinary starting point
};

// The FIVE-TERM binding a HEALTHY node presents at boot: in team `id`, the binding active and naming that same team,
// and `/mrcfg` witnessing exactly the public half the keyring holds. ★ Every case that must NOT install starts from
// this and breaks EXACTLY ONE term, so no assertion below can pass for a second reason.
mrfw::TeamKeyBinding healthy(uint32_t id, const uint8_t* committed_pub) {
    mrfw::TeamKeyBinding b{};
    b.membership_team_id = id;
    b.binding_team_id    = id;
    b.key_active         = true;
    b.committed_present  = true;
    b.committed_pub      = committed_pub;
    return b;
}

bool make_pair(uint8_t seed_byte, uint8_t pub[32], uint8_t priv[32]) {
    uint8_t scalar[32];
    for (int i = 0; i < 32; ++i) scalar[i] = static_cast<uint8_t>(seed_byte + i);
    return meshroute::team_channel_key_derive(pub, priv, scalar);
}
bool all_zero(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) if (b[i]) return false;
    return true;
}

}  // namespace

// ============================================================================ THE RECORD AND ITS FOUR-STATE READ
TEST_CASE("ui16-keyring: the /mrteams record's ABI is what the size check guards, and `reserved` is NAMED") {
    // `sizeof` IS the migration policy (load_team_keys' exact size check), so a silent layout move must be visible.
    // ⚠ The header's static_asserts pin this PER-ABI (they compile on ARM and Xtensa too); these CHECKs are the
    //   host half, plus the property the asserts cannot state: that a value-initialised record is ALL ZERO, which
    //   is what makes the whole-record byte compare a valid "nothing changed".
    CHECK(sizeof(mrnv::TeamKeyRecord) == 72);
    CHECK(sizeof(mrnv::TeamKeyBlob) == 296);
    CHECK(offsetof(mrnv::TeamKeyRecord, team_id) == 0);
    CHECK(offsetof(mrnv::TeamKeyRecord, team_ch_pub) == 4);
    CHECK(offsetof(mrnv::TeamKeyRecord, team_ch_priv) == 36);
    CHECK(offsetof(mrnv::TeamKeyRecord, reserved) == 68);
    CHECK(sizeof(mrnv::TeamKeyRecord::reserved) == 4);
    // ★ NO IMPLICIT TAIL PADDING: 4 + 32 + 32 + 4 accounts for every byte. If `reserved` were dropped the record
    //   would still be 72 — but those 4 bytes would be INDETERMINATE after value-initialisation, and the coalescing
    //   compare would then answer differently for identical material. This is the [[AB1]]/`PeerRec::_pad` rule.
    CHECK(4u + 32u + 32u + 4u == sizeof(mrnv::TeamKeyRecord));
    const mrnv::TeamKeyRecord z{};
    CHECK(all_zero(&z, sizeof z));
    const mrnv::TeamKeyBlob zb{};
    CHECK(all_zero(&zb, sizeof zb));
}

TEST_CASE("ui16-keyring: the keyring has its OWN magic, its OWN slot and the factory-reset namespace") {
    CHECK(mrnv::kTeamKeyMagic == 0x4D524B31u);          // 'MRK1'
    CHECK(mrnv::kTeamKeyMagic != mrnv::kMagic);         // ⛔ never /mrcfg's
    CHECK(mrnv::kTeamKeyMagic != mrnv::kJoinMagic);     // ⛔ never /mrjoin's
    CHECK(mrnv::kTeamKeyVersion == 1);
    CHECK(mrnv::kTeamKeyRecs == 4);                     // FOUR entries, matching the four join profiles
    CHECK(std::strcmp(mrnv::kSlotTeams.path, "/mrteams") == 0);
    CHECK(std::strcmp(mrnv::kSlotTeams.key,  "teams") == 0);
    // ★★ FACTORY RESET ERASES IT, AND THE RULING IS EXPRESSED AS **DATA**: the ESP32 `factory_erase()` clears the
    //    whole `"mr"` namespace and the nRF52 arm formats every file, so being in `"mr"` IS the erasure — with ⛔ not
    //    one line of new code. `/mrfault` is the deliberate exception that opts out by living elsewhere; a store of
    //    team SECRETS must not join it.
    CHECK(std::strcmp(mrnv::kSlotTeams.ns, "mr") == 0);
    CHECK(std::strcmp(mrnv::kSlotTeams.ns, mrnv::kSlotCfg.ns) == 0);
    CHECK(std::strcmp(mrnv::kSlotTeams.ns, mrnv::kSlotFault.ns) != 0);
}

TEST_CASE("ui16-keyring: the four read states, and the ORDER that keeps a dead store from reading as a fresh one") {
    mrnv::TeamKeyBlob b{};
    mrnv::team_key_blob_init(b);
    const int n = static_cast<int>(sizeof b);
    CHECK(mrnv::team_key_blob_state(b, n) == mrnv::TeamKeyRead::ok);
    // ABSENT — an ordinary fresh device, ⛔ never an error.
    CHECK(mrnv::team_key_blob_state(b, mrnv::kSlotAbsent) == mrnv::TeamKeyRead::absent);
    // ★★ `backend_failed` WINS OVER THE ABSENT SENTINEL, and the order is the whole point: both live read arms
    //    return `kSlotAbsent` when the STORE would not open, so any later ordering would report a dead flash as
    //    "no team keys stored" — the same laundering [[B218]] found for `/mrjoin`.
    mrnv::SlotIo dead; dead.backend_failed = true;
    CHECK(mrnv::team_key_blob_state(b, mrnv::kSlotAbsent, dead) == mrnv::TeamKeyRead::io_failed);
    CHECK(mrnv::team_key_blob_state(b, n, dead) == mrnv::TeamKeyRead::io_failed);
    // ★ An OVER-LENGTH record is `invalid` and ⛔ never `ok`: nRF52 reads `len` bytes out of a longer file and
    //   returns EXACTLY `len`, so a valid PREFIX would pass every other check.
    mrnv::SlotIo big; big.oversize = true;
    CHECK(mrnv::team_key_blob_state(b, n, big) == mrnv::TeamKeyRead::invalid);
    // the BYTE matrix: short, over-long, wrong magic, wrong version
    CHECK(mrnv::team_key_blob_state(b, n - 1) == mrnv::TeamKeyRead::invalid);
    CHECK(mrnv::team_key_blob_state(b, n + 1) == mrnv::TeamKeyRead::invalid);
    { mrnv::TeamKeyBlob x = b; x.magic = mrnv::kMagic;   CHECK(mrnv::team_key_blob_state(x, n) == mrnv::TeamKeyRead::invalid); }
    { mrnv::TeamKeyBlob x = b; x.magic = mrnv::kJoinMagic; CHECK(mrnv::team_key_blob_state(x, n) == mrnv::TeamKeyRead::invalid); }
    // EQUALITY on the version, ⛔ not a range: a rejected record leaves the node KEYLESS, which is safe and visible.
    { mrnv::TeamKeyBlob x = b; x.version = 0; CHECK(mrnv::team_key_blob_state(x, n) == mrnv::TeamKeyRead::invalid); }
    { mrnv::TeamKeyBlob x = b; x.version = static_cast<uint16_t>(mrnv::kTeamKeyVersion + 1);
      CHECK(mrnv::team_key_blob_state(x, n) == mrnv::TeamKeyRead::invalid); }
    // the two unreadable answers are NAMED once and ⛔ never collapsed (different operator remedies)
    CHECK(mrfw::team_key_read_unreadable(mrnv::TeamKeyRead::invalid));
    CHECK(mrfw::team_key_read_unreadable(mrnv::TeamKeyRead::io_failed));
    CHECK_FALSE(mrfw::team_key_read_unreadable(mrnv::TeamKeyRead::absent));   // ⛔ a fresh device is not a fault
    CHECK_FALSE(mrfw::team_key_read_unreadable(mrnv::TeamKeyRead::ok));
    CHECK(mrfw::keyring_err_of_unreadable(mrnv::TeamKeyRead::invalid)   == KeyringErr::store_invalid);
    CHECK(mrfw::keyring_err_of_unreadable(mrnv::TeamKeyRead::io_failed) == KeyringErr::store_io_failed);
}

TEST_CASE("ui16-keyring: `team_key_blob_init` stamps ONE valid empty record and zeroes every secret byte") {
    mrnv::TeamKeyBlob b;
    std::memset(&b, 0x5A, sizeof b);
    mrnv::team_key_blob_init(b);
    CHECK(b.magic == mrnv::kTeamKeyMagic);
    CHECK(b.version == mrnv::kTeamKeyVersion);
    CHECK(b.count == 0);
    for (uint8_t i = 0; i < mrnv::kTeamKeyRecs; ++i) CHECK(all_zero(&b.rec[i], sizeof b.rec[i]));
    CHECK(mrnv::team_key_blob_state(b, static_cast<int>(sizeof b)) == mrnv::TeamKeyRead::ok);
}

// ============================================================================ THE SECRET WIPE
TEST_CASE("ui16-keyring: the secret wipe guard ZEROES its carrier on scope exit — both carriers") {
    // ★★ THE ONLY DEFINED WAY TO MEASURE THE WIPE: the carrier OUTLIVES the guard. (Inspecting the service's own
    //    stack frame after the call would be undefined behaviour — see this file's header note.)
    mrnv::TeamKeyBlob b;
    std::memset(&b, 0xC3, sizeof b);
    CHECK_FALSE(all_zero(&b, sizeof b));                       // non-vacuity: it really was full of "secret"
    { mrfw::SecretWipeGuard<mrnv::TeamKeyBlob> g{b}; (void)g; }
    CHECK(all_zero(&b, sizeof b));

    mrnv::TeamKeyRecord r;
    std::memset(&r, 0x7F, sizeof r);
    CHECK_FALSE(all_zero(&r, sizeof r));
    { mrfw::SecretWipeGuard<mrnv::TeamKeyRecord> g{r}; (void)g; }
    CHECK(all_zero(&r, sizeof r));
}

TEST_CASE("ui16-keyring: the composition path zeroes the WHOLE record first — no stale tail, no live padding") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x21, pub, priv));
    mrnv::TeamKeyRecord r;
    std::memset(&r, 0x99, sizeof r);                           // a record full of a PREVIOUS team's bytes
    mrfw::team_key_rec_put(r, 0xABCD1234u, pub, priv);
    CHECK(r.team_id == 0xABCD1234u);
    CHECK(std::memcmp(r.team_ch_pub, pub, 32) == 0);
    CHECK(std::memcmp(r.team_ch_priv, priv, 32) == 0);
    // ★ `reserved` is ZERO, not 0x99: without this the coalescing compare would fire on residue and rewrite flash.
    CHECK(all_zero(r.reserved, sizeof r.reserved));
    // ⛔ NO LABEL FIELD EXISTS TO FILL — the record is exactly id + the two halves + named padding (F-3, and the
    //    keyring ruling repeats it). Asserted as a SIZE identity so adding a label breaks this case.
    CHECK(sizeof r == sizeof r.team_id + sizeof r.team_ch_pub + sizeof r.team_ch_priv + sizeof r.reserved);
}

// ============================================================================ THE WRITE POLICY (K1 pins 1-5)
TEST_CASE("ui16-keyring: `team_id == 0` is NEVER stored — zero LOADS and zero writes") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x31, pub, priv));
    Fix f;
    const mrfw::KeyringResult r = f.svc.put(0, pub, priv);
    CHECK(r.verdict == KeyringVerdict::refused);
    CHECK(r.err == KeyringErr::zero_team);
    CHECK(f.store.loads == 0);                                 // ⛔ refused before the store is even opened
    CHECK(f.store.saves == 0);
    CHECK(f.store.rec.count == 0);
    // …and it can never be FOUND either, so a corrupt "team 0" row can never satisfy a binding.
    mrnv::TeamKeyBlob b{}; mrnv::team_key_blob_init(b);
    b.count = 1;                                               // a row whose id rotted to 0
    CHECK(mrfw::team_key_find(b, 0) == -1);
}

TEST_CASE("ui16-keyring: an ABSENT store is SEEDED and the record lands in EXACTLY ONE write") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x41, pub, priv));
    Fix f;
    f.store.state = mrnv::TeamKeyRead::absent;                 // a fresh device: ⛔ not an error
    const mrfw::KeyringResult r = f.svc.put(0x0A0B0C0Du, pub, priv);
    CHECK(r.verdict == KeyringVerdict::ok);
    CHECK(r.err == KeyringErr::none);
    CHECK(f.store.saves == 1);                                 // ★ ONE — the seed and the record land together
    CHECK(f.store.written.magic == mrnv::kTeamKeyMagic);       // …and what was written is a VALID record
    CHECK(f.store.written.version == mrnv::kTeamKeyVersion);
    CHECK(f.store.written.count == 1);
    CHECK(f.store.written.rec[0].team_id == 0x0A0B0C0Du);
    CHECK(std::memcmp(f.store.written.rec[0].team_ch_priv, priv, 32) == 0);
    // ⛔ THE PARTIAL-READ RESIDUE IS NOT TRUSTED: the fake deposited 0xA5 garbage on the failed read, and the
    //    written record is a clean seed rather than that garbage with one row edited.
    for (uint8_t i = 1; i < mrnv::kTeamKeyRecs; ++i) CHECK(all_zero(&f.store.written.rec[i], sizeof f.store.written.rec[i]));
}

TEST_CASE("ui16-keyring: ★ IDENTICAL MATERIAL WRITES NOTHING — the flash-wear guard, COUNTED") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x51, pub, priv));
    Fix f;
    CHECK(f.svc.put(0x1111u, pub, priv).verdict == KeyringVerdict::ok);
    CHECK(f.store.saves == 1);
    const mrnv::TeamKeyBlob before = f.store.rec;
    // ⓘ A re-grant of the SAME key is the COMMON case, not an edge one: a teammate re-sends on every join.
    for (int i = 0; i < 5; ++i) {
        const mrfw::KeyringResult r = f.svc.put(0x1111u, pub, priv);
        CHECK(r.verdict == KeyringVerdict::unchanged);
        CHECK(r.err == KeyringErr::none);
    }
    CHECK(f.store.saves == 1);                                 // ★ STILL ONE — five re-puts, ZERO extra writes
    CHECK(std::memcmp(&before, &f.store.rec, sizeof before) == 0);
    CHECK(f.store.loads == 6);                                 // it did LOOK each time; it just did not WRITE
}

TEST_CASE("ui16-keyring: EXACTLY ONE record per team_id — a re-key REPLACES it in place, atomically") {
    uint8_t pub[32], priv[32], pub2[32], priv2[32], pub3[32], priv3[32];
    CHECK(make_pair(0x61, pub,  priv));
    CHECK(make_pair(0x8D, pub2, priv2));
    CHECK(make_pair(0xB5, pub3, priv3));                       // THREE distinct pairs, so no assertion below is vacuous
    CHECK(std::memcmp(priv, priv2, 32) != 0);
    CHECK(std::memcmp(priv2, priv3, 32) != 0);
    Fix f;
    CHECK(f.svc.put(0x2222u, pub, priv).verdict == KeyringVerdict::ok);
    CHECK(f.svc.put(0x3333u, pub2, priv2).verdict == KeyringVerdict::ok);
    CHECK(f.store.rec.count == 2);
    // the RE-KEY of the FIRST team, to a THIRD pair
    const mrfw::KeyringResult r = f.svc.put(0x2222u, pub3, priv3);
    CHECK(r.verdict == KeyringVerdict::ok);
    CHECK(f.store.saves == 3);                                 // one write, ⛔ not a delete-then-insert pair
    CHECK(f.store.rec.count == 2);                             // ★ still TWO rows — ⛔ never a second row for one team
    CHECK(f.store.rec.rec[0].team_id == 0x2222u);              // …and it stayed in ITS slot (order is insertion order)
    CHECK(std::memcmp(f.store.rec.rec[0].team_ch_priv, priv3, 32) == 0);   // replaced, in place
    CHECK(f.store.rec.rec[1].team_id == 0x3333u);              // ⛔ the OTHER team's record did not move…
    CHECK(std::memcmp(f.store.rec.rec[1].team_ch_priv, priv2, 32) == 0);   // …and still holds ITS OWN key
    CHECK(mrfw::team_key_find(f.store.rec, 0x2222u) == 0);
    CHECK(mrfw::team_key_find(f.store.rec, 0x3333u) == 1);
    CHECK(mrfw::team_key_find(f.store.rec, 0x4444u) == -1);
}

TEST_CASE("ui16-keyring: ★★★ P-15 — a FULL keyring FAILS LOUDLY and evicts NOTHING") {
    Fix f;
    uint8_t pub[4][32], priv[4][32];
    for (int i = 0; i < 4; ++i) {
        CHECK(make_pair(static_cast<uint8_t>(0x11 + 0x20 * i), pub[i], priv[i]));
        CHECK(f.svc.put(static_cast<uint32_t>(0x1000u + i), pub[i], priv[i]).verdict == KeyringVerdict::ok);
    }
    CHECK(f.store.rec.count == mrnv::kTeamKeyRecs);
    CHECK(f.store.saves == 4);
    const mrnv::TeamKeyBlob before = f.store.rec;

    uint8_t pub5[32], priv5[32];
    CHECK(make_pair(0xA3, pub5, priv5));
    const mrfw::KeyringResult r = f.svc.put(0x5555u, pub5, priv5);
    CHECK(r.verdict == KeyringVerdict::refused);
    CHECK(r.err == KeyringErr::keyring_full);
    CHECK(f.store.saves == 4);                                 // ⛔ ZERO writes
    // ★★ THE MEASUREMENT THAT MATTERS: not the verdict, but that all four SECRETS are byte-identical afterwards.
    //    "Evict the oldest" — the idiom used everywhere else in this tree — would pass a verdict check and destroy
    //    an UNRECOVERABLE key here.
    CHECK(std::memcmp(&before, &f.store.rec, sizeof before) == 0);
    for (int i = 0; i < 4; ++i) {
        CHECK(f.store.rec.rec[i].team_id == static_cast<uint32_t>(0x1000u + i));
        CHECK(std::memcmp(f.store.rec.rec[i].team_ch_priv, priv[i], 32) == 0);
    }
    CHECK(mrfw::team_key_find(f.store.rec, 0x5555u) == -1);    // …and the fifth team was NOT stored
    // ⓘ …while a re-key of a team ALREADY in the full keyring still works: `full` is about ADMITTING a new team.
    CHECK(f.svc.put(0x1000u, pub5, priv5).verdict == KeyringVerdict::ok);
    CHECK(f.store.rec.count == mrnv::kTeamKeyRecs);
    // ★ THE RULED LEXEME (spec string S-30), spelled ONCE in the pure unit.
    CHECK(std::strcmp(mrfw::kKeyringFullText, "KEYRING FULL") == 0);
    CHECK(std::strlen(mrfw::kKeyringFullText) == 12);          // fits the panel's 19 columns when K3/K5 render it
}

TEST_CASE("ui16-keyring: an UNREADABLE store refuses with ZERO writes — and the two answers stay apart") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x71, pub, priv));
    for (int arm = 0; arm < 2; ++arm) {
        Fix f;
        f.store.state = arm ? mrnv::TeamKeyRead::io_failed : mrnv::TeamKeyRead::invalid;
        const mrfw::KeyringResult r = f.svc.put(0x6666u, pub, priv);
        CHECK(r.verdict == KeyringVerdict::refused);
        CHECK(r.err == (arm ? KeyringErr::store_io_failed : KeyringErr::store_invalid));
        CHECK(f.store.saves == 0);                             // ⛔⛔ NEVER a blind rewrite: up to four intact keys
        CHECK(f.store.loads == 1);
    }
}

TEST_CASE("ui16-keyring: the ONE save attempt failing is reported, ⛔ never laundered into a success") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x81, pub, priv));
    Fix f;
    f.store.save_ok = false;
    const mrfw::KeyringResult r = f.svc.put(0x7777u, pub, priv);
    CHECK(r.verdict == KeyringVerdict::nv_failed);
    CHECK(r.err == KeyringErr::nv_save_failed);
    CHECK(f.store.saves == 1);                                 // ★ ONE attempt, ⛔ no retry loop
    CHECK(f.store.rec.count == 0);                             // the fake's record did not move
}

TEST_CASE("ui16-keyring: a bit-rotted count can never index past rec[], and is REPAIRED by the next write") {
    uint8_t pub[32], priv[32], pub2[32], priv2[32];
    CHECK(make_pair(0x91, pub,  priv));
    CHECK(make_pair(0xA9, pub2, priv2));
    {   // (a) a corrupt count over an EMPTY record: clamped to the cap ⇒ the keyring reads as FULL and refuses
        //     loudly. ⛔ The one thing it must never do is scan or write past `rec[]`.
        Fix f;
        f.store.rec.count = 0xFFFFu;                           // corruption INSIDE an otherwise valid record
        const mrfw::KeyringResult r = f.svc.put(0x8888u, pub, priv);
        CHECK(r.verdict == KeyringVerdict::refused);
        CHECK(r.err == KeyringErr::keyring_full);
        CHECK(f.store.saves == 0);
        CHECK(mrfw::team_key_find(f.store.rec, 0x8888u) == -1);
    }
    {   // (b) ★ a corrupt count over a POPULATED row: the re-key still finds its row, and the record that goes back
        //     to flash carries a SANE count — ⛔ the corruption is not persisted forward for the next boot to read.
        Fix f;
        CHECK(f.svc.put(0x8888u, pub, priv).verdict == KeyringVerdict::ok);
        f.store.rec.count = 0xFFFFu;
        const mrfw::KeyringResult r = f.svc.put(0x8888u, pub2, priv2);
        CHECK(r.verdict == KeyringVerdict::ok);
        CHECK(f.store.written.count == mrnv::kTeamKeyRecs);    // ★ clamped, ⛔ never 0xFFFF
        CHECK(f.store.written.rec[0].team_id == 0x8888u);
        CHECK(memcmp(f.store.written.rec[0].team_ch_priv, priv2, 32) == 0);
    }
}

TEST_CASE("ui16-keyring: the coalescing compare is WHOLE-RECORD — a dirty `reserved` is repaired, not read as equal") {
    // ★★ THIS IS WHY `reserved[4]` IS A NAMED MEMBER. A key-only compare would answer "unchanged" here and leave
    //    four indeterminate bytes on flash forever; the whole-record compare notices, rewrites ONCE, and the stored
    //    record becomes byte-deterministic — which is what makes every LATER "identical material writes nothing"
    //    answer trustworthy.
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0xF1, pub, priv));
    Fix f;
    CHECK(f.svc.put(0x4242u, pub, priv).verdict == KeyringVerdict::ok);
    CHECK(f.store.saves == 1);
    std::memset(f.store.rec.rec[0].reserved, 0xB7, sizeof f.store.rec.rec[0].reserved);   // residue on flash
    const mrfw::KeyringResult r = f.svc.put(0x4242u, pub, priv);                          // the SAME key material
    CHECK(r.verdict == KeyringVerdict::ok);                    // ⛔ NOT `unchanged`: the RECORD is not identical
    CHECK(f.store.saves == 2);
    CHECK(all_zero(f.store.rec.rec[0].reserved, sizeof f.store.rec.rec[0].reserved));
    // …and now that it is clean, the next identical put really does write nothing.
    CHECK(f.svc.put(0x4242u, pub, priv).verdict == KeyringVerdict::unchanged);
    CHECK(f.store.saves == 2);
}

// ============================================================================ THE BOOT RESTORE — THE FIVE TERMS
// ★★★ THE EXACT MATCH IS FIVE TERMS, AND EACH GETS ITS OWN CASE THAT BREAKS **EXACTLY ONE** OF THEM (the
//     four-term-correlation precedent). ⛔ A case that broke two could pass for the wrong reason, and the two states
//     QG found — a stale binding, and a re-key whose `/mrcfg` write failed — are precisely single-term breaks.
// ★★ AND EVERY NON-INSTALLING ARM ASSERTS THE **GOVERNANCE**: `clear_calls == 1` and the live seam left keyless. The
//    earlier version of this file asserted only `installed == false` over a seam that STARTED empty, which is true
//    for a service that does nothing at all — the fixture-in-the-middle-of-the-domain shape.

TEST_CASE("ui16-keyring: the restore installs when ALL FIVE terms hold — and writes nothing") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0xB1, pub, priv));
    Fix f;
    CHECK(f.svc.put(0x9999u, pub, priv).verdict == KeyringVerdict::ok);
    const int saves_before = f.store.saves;

    FakeKeyLive live;
    CHECK(f.svc.restore(healthy(0x9999u, pub), live) == KeyringRestore::installed);
    CHECK(live.calls == 1);
    CHECK(live.clear_calls == 0);                              // ⛔ the installing arm never clears
    CHECK(live.installed);
    CHECK(std::memcmp(live.priv, priv, 32) == 0);
    CHECK(std::memcmp(live.pub,  pub,  32) == 0);              // ★ DERIVED from priv, and it matches what was stored
    CHECK(f.store.saves == saves_before);                      // ⛔ a restore NEVER writes
}

TEST_CASE("ui16-keyring: term (i) — ★★★ P-2b: an INACTIVE binding installs nothing and reads nothing") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0xC1, pub, priv));
    Fix f;
    CHECK(f.svc.put(0x9999u, pub, priv).verdict == KeyringVerdict::ok);
    {   // the binding was CLEARED (`team 0`) while the record is RETAINED: the store is ⛔ NOT EVEN READ — the
        // ruling expressed as a read count rather than as an argument.
        FakeKeyLive live;
        CHECK(live.adopt_key(pub, priv));                      // the seam arrives POPULATED (see QG-B1 below)
        const int loads_before = f.store.loads;
        mrfw::TeamKeyBinding b = healthy(0x9999u, pub);
        b.key_active = false;                                  // ← the ONE broken term
        CHECK(f.svc.restore(b, live) == KeyringRestore::no_binding);
        CHECK(live.calls == 1);                                // (only the test's own adopt above)
        CHECK(live.clear_calls == 1);                          // ★ the verdict was APPLIED
        CHECK_FALSE(live.installed);
        CHECK(f.store.loads == loads_before);                  // ★ ZERO loads
        CHECK(f.store.saves == 1);                             // …and the record is STILL THERE
        CHECK(mrfw::team_key_find(f.store.rec, 0x9999u) == 0);
    }
    {   // a corrupt binding that claims active over team 0 installs nothing either
        FakeKeyLive live;
        mrfw::TeamKeyBinding b = healthy(0, pub);
        b.membership_team_id = 0;
        CHECK(f.svc.restore(b, live) == KeyringRestore::no_binding);
        CHECK(live.calls == 0);
        CHECK(live.clear_calls == 1);
    }
}

TEST_CASE("ui16-keyring: term (ii) — ★★★ the binding must name the team we are IN (a stale binding installs nothing)") {
    // QG blocker 3: the binding is TWO facts — a key is active, and for whom — and neither establishes MEMBERSHIP.
    // A binding that has gone stale against `/mrcfg`'s `team_id` must not install ANOTHER team's key.
    uint8_t pubA[32], privA[32], pubB[32], privB[32];
    CHECK(make_pair(0x1A, pubA, privA));
    CHECK(make_pair(0x7B, pubB, privB));
    Fix f;
    CHECK(f.svc.put(0xAAAAu, pubA, privA).verdict == KeyringVerdict::ok);   // team A's key is stored…
    CHECK(f.svc.put(0xBBBBu, pubB, privB).verdict == KeyringVerdict::ok);   // …and so is team B's

    FakeKeyLive live;
    CHECK(live.adopt_key(pubA, privA));
    const int loads_before = f.store.loads;                    // (the two `put`s above each read once)
    mrfw::TeamKeyBinding b = healthy(0xBBBBu, pubB);
    b.membership_team_id = 0xAAAAu;                            // ← the ONE broken term: we are in A, the binding says B
    CHECK(f.svc.restore(b, live) == KeyringRestore::team_mismatch);
    CHECK_FALSE(live.installed);                               // ⛔ B's key is NOT installed on a node in team A…
    CHECK(live.clear_calls == 1);                              // …and A's stale live key is gone too
    CHECK(f.store.loads == loads_before);                      // ⛔ the store is not even read for this decision
    CHECK(mrfw::team_key_find(f.store.rec, 0xBBBBu) == 1);     // both records RETAINED
    CHECK(mrfw::team_key_find(f.store.rec, 0xAAAAu) == 0);
}

TEST_CASE("ui16-keyring: term (iii) — no record for the bound team ⇒ KEYLESS, and other teams are retained") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x2C, pub, priv));
    Fix f;
    CHECK(f.svc.put(0x9999u, pub, priv).verdict == KeyringVerdict::ok);
    FakeKeyLive live;
    CHECK(live.adopt_key(pub, priv));
    // in team 0xAAAA with an active binding for it — the keyring holds 0x9999 only
    CHECK(f.svc.restore(healthy(0xAAAAu, pub), live) == KeyringRestore::no_record);
    CHECK_FALSE(live.installed);                               // ⛔ never a substitute record
    CHECK(live.clear_calls == 1);
    CHECK(f.store.saves == 1);                                 // ⛔ nothing written
    CHECK(mrfw::team_key_find(f.store.rec, 0x9999u) == 0);     // RETAINED
}

TEST_CASE("ui16-keyring: term (iv) — ★★★ the stored key must be the one /mrcfg COMMITTED") {
    // QG blocker 2, at the unit: the keyring is written FIRST and `/mrcfg` LAST, so a transaction that failed in
    // between leaves a keyring record the committed record never witnessed. The reboot must not adopt it.
    uint8_t pub1[32], priv1[32], pub2[32], priv2[32];
    CHECK(make_pair(0x3D, pub1, priv1));
    CHECK(make_pair(0x8E, pub2, priv2));
    CHECK(std::memcmp(pub1, pub2, 32) != 0);                   // non-vacuity
    {   // the keyring holds the NEW key; `/mrcfg` still witnesses the OLD one
        Fix f;
        CHECK(f.svc.put(0x4444u, pub2, priv2).verdict == KeyringVerdict::ok);
        FakeKeyLive live;
        CHECK(live.adopt_key(pub1, priv1));
        mrfw::TeamKeyBinding b = healthy(0x4444u, pub1);       // ← the ONE broken term: the witness is the OLD pub
        CHECK(f.svc.restore(b, live) == KeyringRestore::not_committed);
        CHECK_FALSE(live.installed);                           // ⛔ the FAILED request's key is not adopted…
        CHECK(live.clear_calls == 1);                          // …and the node is keyless, not left on the old key
        CHECK(f.store.saves == 1);                             // ⛔ and the restore repairs nothing
    }
    {   // `/mrcfg` carries NO key at all ⇒ it witnesses nothing
        Fix f;
        CHECK(f.svc.put(0x4444u, pub2, priv2).verdict == KeyringVerdict::ok);
        FakeKeyLive live;
        mrfw::TeamKeyBinding b = healthy(0x4444u, pub2);
        b.committed_present = false;                           // ← the ONE broken term
        CHECK(f.svc.restore(b, live) == KeyringRestore::not_committed);
        CHECK_FALSE(live.installed);
        CHECK(live.clear_calls == 1);
    }
    {   // ★ THE POSITIVE CONTROL: the SAME fixture with the matching witness installs, so neither arm above is
        //   passing because the restore is broken for everything.
        Fix f;
        CHECK(f.svc.put(0x4444u, pub2, priv2).verdict == KeyringVerdict::ok);
        FakeKeyLive live;
        CHECK(f.svc.restore(healthy(0x4444u, pub2), live) == KeyringRestore::installed);
        CHECK(live.installed);
        CHECK(live.clear_calls == 0);
    }
}

TEST_CASE("ui16-keyring: term (v) — a record whose pub does not verify is REJECTED, and the node is KEYLESS") {
    uint8_t pub[32], priv[32], pub2[32], priv2[32];
    CHECK(make_pair(0xD1, pub,  priv));
    CHECK(make_pair(0xE7, pub2, priv2));
    CHECK(std::memcmp(pub, pub2, 32) != 0);                    // non-vacuity
    // ARM 0: the stored PUBLIC half belongs to a different scalar (a partial write / a bit flip).
    // ARM 1: the stored PRIVATE half is all-zero — `team_channel_key_derive` refuses a degenerate scalar.
    for (int arm = 0; arm < 2; ++arm) {
        Fix f;
        CHECK(f.svc.put(0xBEEFu, pub, priv).verdict == KeyringVerdict::ok);
        // ⚠ the witness follows the STORED pub, so term (iv) holds and (v) is the only broken term
        if (arm == 0) std::memcpy(f.store.rec.rec[0].team_ch_pub, pub2, 32);
        else          std::memset(f.store.rec.rec[0].team_ch_priv, 0, 32);
        const uint8_t* witness = (arm == 0) ? pub2 : pub;
        FakeKeyLive live;
        CHECK(f.svc.restore(healthy(0xBEEFu, witness), live) == KeyringRestore::rejected);
        CHECK(live.calls == 1);                                // it WAS offered…
        CHECK_FALSE(live.installed);                           // …★ and REFUSED: ⛔ never a wrong key
        CHECK(live.clear_calls == 1);                          // ★ and the verdict was APPLIED
        CHECK(f.store.saves == 1);                             // ⛔ the restore repaired nothing
    }
}

TEST_CASE("ui16-keyring: an ABSENT / UNREADABLE store leaves the node keyless, with ZERO writes") {
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x5F, pub, priv));
    {   // ABSENT — a fresh device with an active binding (flashed over a provisioned /mrcfg): keyless, no error
        Fix f;
        FakeKeyLive live;
        f.store.state = mrnv::TeamKeyRead::absent;
        CHECK(f.svc.restore(healthy(0x1234u, pub), live) == KeyringRestore::no_record);
        CHECK(live.calls == 0);
        CHECK(live.clear_calls == 1);
        CHECK(f.store.saves == 0);
    }
    for (int arm = 0; arm < 2; ++arm) {   // INVALID / IO_FAILED — ⛔ distinct from "there is no key", ⛔ never rewritten
        Fix g;
        FakeKeyLive live;
        g.store.state = arm ? mrnv::TeamKeyRead::io_failed : mrnv::TeamKeyRead::invalid;
        CHECK(g.svc.restore(healthy(0x1234u, pub), live) == KeyringRestore::store_failed);
        CHECK(live.calls == 0);
        CHECK(live.clear_calls == 1);
        CHECK(g.store.saves == 0);
    }
}

// ==== QG BLOCKER 1 — written FIRST against the UNFIXED code, where it FAILED; kept as the regression ==============
TEST_CASE("ui16-keyring: QG-B1 ★★★ a key ALREADY LIVE does not survive a restore that could not install") {
    // ⛔⛔ THE STATE MY FIRST CUT COULD NOT PRODUCE: `/mrcfg`'s v22 copy used to be installed by `fw_main` BEFORE the
    //     keyring was consulted, so the live seam reached `restore()` ALREADY POPULATED — and every one of my arms
    //     started it EMPTY, which is why they all passed against a service that simply declined to act. The fix is
    //     two-sided: `fw_main` no longer installs that key at all (the keyring is the one authority), AND every
    //     non-installing arm here CLEARS. This case drives the second half, over EVERY refusing arm.
    uint8_t oldpub[32], oldpriv[32], other[32], otherpriv[32];
    CHECK(make_pair(0x33, oldpub, oldpriv));
    CHECK(make_pair(0x64, other, otherpriv));
    struct Arm { const char* what; int kind; KeyringRestore want; };
    const Arm arms[] = {
        { "the store is ABSENT",         0, KeyringRestore::no_record     },
        { "the record is CORRUPT",       1, KeyringRestore::store_failed  },
        { "the store would not OPEN",    2, KeyringRestore::store_failed  },
        { "the binding is INACTIVE",     3, KeyringRestore::no_binding    },
        { "the binding is for ANOTHER",  4, KeyringRestore::team_mismatch },
        { "no record for the team",      5, KeyringRestore::no_record     },
        { "the key was never COMMITTED", 6, KeyringRestore::not_committed },
    };
    for (const Arm& a : arms) {
        Fix f;
        CHECK(f.svc.put(0x1234u, other, otherpriv).verdict == KeyringVerdict::ok);
        mrfw::TeamKeyBinding b = healthy(0x1234u, other);
        switch (a.kind) {
            case 0: f.store.state = mrnv::TeamKeyRead::absent;    break;
            case 1: f.store.state = mrnv::TeamKeyRead::invalid;   break;
            case 2: f.store.state = mrnv::TeamKeyRead::io_failed; break;
            case 3: b.key_active = false;                          break;
            case 4: b.membership_team_id = 0x5678u;                break;
            case 5: b = healthy(0x5678u, other);                   break;
            case 6: b.committed_pub = oldpub;                      break;
            default: break;
        }
        FakeKeyLive live;
        CHECK(live.adopt_key(oldpub, oldpriv));                 // ← the legacy path got there first
        CHECK(live.installed);
        CHECK(f.svc.restore(b, live) == a.want);
        // ★ THE KEYRING'S VERDICT GOVERNS: it could not restore, so the node IS keyless.
        CHECK_FALSE(live.installed);
        CHECK(live.clear_calls == 1);
        CHECK(all_zero(live.priv, sizeof live.priv));           // …and the material is gone, not merely flagged
    }
}

TEST_CASE("ui16-keyring: every enum renders, and the name functions are TOTAL") {
    // The `-Werror=switch` discipline's other half: this project has shipped THREE enum->string defects the
    // byte-identity gate was structurally blind to. Drive the FULL enum, ⛔ not a sample.
    for (uint8_t i = 0; i <= static_cast<uint8_t>(KeyringVerdict::nv_failed); ++i)
        CHECK(std::strcmp(mrfw::keyring_verdict_name(static_cast<KeyringVerdict>(i)), "?") != 0);
    for (uint8_t i = 0; i <= static_cast<uint8_t>(KeyringErr::nv_save_failed); ++i)
        CHECK(std::strcmp(mrfw::keyring_err_name(static_cast<KeyringErr>(i)), "?") != 0);
    for (uint8_t i = 0; i <= static_cast<uint8_t>(KeyringRestore::store_failed); ++i)
        CHECK(std::strcmp(mrfw::keyring_restore_name(static_cast<KeyringRestore>(i)), "?") != 0);
    CHECK(std::strcmp(mrfw::keyring_verdict_name(KeyringVerdict::unchanged), "unchanged") == 0);
    CHECK(std::strcmp(mrfw::keyring_err_name(KeyringErr::keyring_full), "keyring_full") == 0);
    CHECK(std::strcmp(mrfw::keyring_restore_name(KeyringRestore::rejected), "rejected") == 0);
    // ★ THE VERDICT ENUM HAS **FOUR** ARMS AND `empty` IS NOT ONE OF THEM (QG, 2026-08-22): it had no producer —
    //   an absent store is SEEDED by `put` (one write) and is `no_record` on `restore`, a different enum. Pinned as
    //   a COUNT so re-adding a producerless arm fails here rather than looking like coverage.
    CHECK(static_cast<uint8_t>(KeyringVerdict::nv_failed) == 3);
    // ⛔ NO NAME FUNCTION CARRIES MATERIAL: every string is a FACT about the store (P-8).
    CHECK(std::strlen(mrfw::keyring_err_name(KeyringErr::store_io_failed)) == 15);
}
