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
#include <initializer_list>   // §UI-16 K5: the range-for sweeps over the four-valued read and the six-armed verdict
#include "firmware_team_keyring.h"
#include "identity.h"    // meshroute::team_channel_key_derive — the REAL derivation the live seam's fake mirrors

namespace {

using mrfw::KeyringVerdict;
using mrfw::KeyringErr;
using mrfw::KeyringRestore;

// ★★★ §UI-16 K3 — THE SHARED ORDER LOG, and it is what turns *"the key is persisted BEFORE the activation"* from a
//     claim about source order into a MEASUREMENT. Both durable seams stamp one character; the assertion is on the
//     resulting word. ⛔ Asserting two call COUNTS cannot see an order, which is exactly how an ordering rule gets
//     "refactored" into its inverse while every count stays right.
struct OrderLog {
    char ev[8] = {};
    int  n     = 0;
    void put(char c) { if (n < 7) ev[n++] = c; }
    const char* str() const { return ev; }
};

// The COUNTING store. ★ `state` is the FOUR-valued answer, so a case can put the fake in `absent`, `invalid` or
// `io_failed` without forging bytes; `rec` is assigned only on a successful save, so "the stored record did not
// move" is measurable too.
struct FakeKeyStore : mrfw::ITeamKeyStore {
    OrderLog* log = nullptr;   // §UI-16 K3: stamps 'K' on a `/mrteams` write attempt (see OrderLog)
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
        if (log) log->put('K');
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

// ==================================================== §UI-16 K3 — THE GRANT RECEIVE: PERSISTENCE **FIRST**
// ★★★ WHAT THESE CASES MEASURE, and it is the same discipline the write-policy cases above are held to: a
//     CONSEQUENCE, never a returned value on its own. *"Nothing was written"* is `saves == 0 && commits == 0`;
//     *"the key landed before the activation"* is the ORDER LOG reading `"KC"`; *"the UI was not told"* is a sink
//     that recorded nothing. ⛔ A verdict is a value the implementation CHOOSES — a wrong implementation can choose
//     the right one while doing the wrong thing ([[B240]]'s receive half was exactly that shape: adopt, push, and
//     persist nothing).
// ⛔ THE LIMIT OF EVERY CLAIM HERE is K1's, unchanged: both stores are FAKES. ⛔ No NVS/LittleFS write, no flash
//    WEAR and no reset-during-write ([[B193]]). The power-cut half of a RECEIVED grant is METAL-ONLY (M2, spec §7.5).
namespace {

using mrfw::GrantSave;

// The `/mrcfg` half's counting fake. ★ It MODELS the real binding rather than merely recording: `commit_active`
// updates the record it will hand back on the next `read`, which is what makes the ZERO-WRITE idempotence case a
// measurement of the second receipt rather than of a stub that always looks stale.
struct FakeBinding : mrfw::ITeamKeyBinding {
    uint32_t membership   = 0;        // `/mrcfg` team_id      — re-check (4)'s authority
    uint32_t binding_team = 0;        // `/mrcfg` team_key_team_id
    bool     active       = false;    // `/mrcfg` team_key_active
    bool     has_pub      = false;    // `/mrcfg` team_ch_key_present
    uint8_t  pub[32]      = {};       // `/mrcfg` team_ch_pub — the COMMITTED witness (PUBLIC half only)
    bool     read_ok = true, commit_ok = true;
    int      reads = 0, commits = 0;
    OrderLog* log = nullptr;
    uint32_t committed_team = 0;
    uint8_t  committed_priv[32] = {};

    // ★★ ON A FAILED READ THE FAKE MAY STILL DEPOSIT A PLAUSIBLE RECORD, DELIBERATELY, and it is the same discipline
    //    `FakeKeyStore::deposit_rec` carries one store over: the REAL read is `nv_load_stamped` over `mrnv::load`,
    //    which may leave a PARTIAL record in `out` before answering false (device_nv.h's §nv-ritual warning). ⇒ a
    //    caller that IGNORES the `false` does not get zeroes — it gets bytes that LOOK right, which is exactly why
    //    "fails closed" has to be measured against a deposit rather than against an empty struct. ⛔ Without this the
    //    fail-open mutation is masked by re-check (4) and the control measures nothing (MEASURED 2026-08-24).
    bool deposit_on_fail = false;

    bool read(mrfw::TeamKeyBinding& out) override {
        ++reads;
        if (!read_ok && !deposit_on_fail) return false;
        out = mrfw::TeamKeyBinding{};
        out.membership_team_id = membership;
        out.binding_team_id    = binding_team;
        out.key_active         = active;
        out.committed_present  = has_pub;
        out.committed_pub      = has_pub ? pub : nullptr;
        return read_ok;              // ★ the deposit above happened EITHER WAY — see `deposit_on_fail`
    }
    bool commit_active(uint32_t team_id, const uint8_t p[32], const uint8_t s[32]) override {
        ++commits;
        if (log) log->put('C');
        // ★★★★ THE FAKE ENFORCES WHAT THE REAL WRITER MUST — ADDED 2026-08-25 (QG blocker 1), and it is the
        //      `deposit_on_fail` lesson one seam over: a fake that is MORE PERMISSIVE than the device it stands for
        //      makes every case above it pass for a reason the device does not have. `DeviceTeamKeyBinding` refuses
        //      to write an ACTIVE BINDING into a `/mrcfg` record whose MEMBERSHIP names another team (a binding
        //      that lies, which the five-term boot restore then rejects), and it refuses through the SAME pure
        //      predicate this line calls (U1 — one authority, and `--target=teamkeyring` can attack it).
        if (!mrfw::commit_membership_ok(membership, team_id)) return false;
        if (!commit_ok) return false;
        committed_team = team_id;
        std::memcpy(committed_priv, s, 32);
        binding_team = team_id; active = true; has_pub = true;
        std::memcpy(pub, p, 32);
        return true;
    }
};

// One receipt, everything wired. ⓘ The keyring service is the SAME `TeamKeyringService` the console and the boot
// restore use — ⛔ never a second service over one record, which is the rule `prov_service()` is built on.
struct GrantFix {
    OrderLog                 log;
    FakeKeyStore             store;
    FakeBinding              binding;
    mrfw::TeamKeyringService keyring{store};
    mrfw::TeamKeyGrantService svc{keyring, binding};
    uint8_t pub[32] = {}, priv[32] = {};

    explicit GrantFix(uint32_t team = 0xAA11u, uint8_t seed = 7) {
        mrnv::team_key_blob_init(store.rec);
        store.log = &log; binding.log = &log;
        binding.membership = team;
        CHECK(make_pair(seed, pub, priv));
    }
    mrfw::TeamKeyGrant grant(uint32_t push_team, uint32_t live_team) const {
        mrfw::TeamKeyGrant g{};
        g.push_team_id = push_team; g.live_team_id = live_team;
        g.live_pub = pub; g.live_priv = priv;
        return g;
    }
    int writes() const { return store.saves + binding.commits; }
};

// ★★★★ THE DRAIN LOOP'S GATE, REPRODUCED EXACTLY, so the F-10 ORDER is proved on the shape `src/fw_main.cpp`
//      actually carries and not only on the service in isolation. ⛔ `fw_main.cpp` is compiled by NEITHER the native
//      suite NOR the simulator (§B115), so this is the closest a host gate can stand to it — and the SOURCE half
//      (that the real loop carries this and only this) is pinned by `tools/probe_firmware_ui/run.sh`'s K3 checks.
// ⓘ The real lines read
//      `const mrfw::GrantUiRoute ui_route = (pu.kind != PushKind::team_key_received)`
//      `                                    ? mrfw::GrantUiRoute::received`
//      `                                    : mrfw::team_key_grant_persist(pu.team_id);`
//      `switch (ui_route) { received -> mr_ui_on_push(pu); active_unsaved -> mr_ui_on_team_key_unsaved(); `
//      `                    suppressed -> ; count -> ; }`
//   — a switch over a RETURNED classification, ⛔ no decision (U3). `calls` = `mr_ui_on_push`,
//   `unsaved` = `mr_ui_on_team_key_unsaved`, `silent` = neither door was opened.
// ★★★★ [[B243]] CLOSED 2026-08-25 — the second door made the failure SAYABLE.
// ⛔⛔ **AND THE FIRST CUT OF IT WAS WRONG, WHICH IS WHY THIS FIXTURE HAS THREE COUNTERS AND NOT TWO (QG blocker,
//    same day).** ⛔ WITHDRAWN, KEPT VISIBLE: `const bool ok = … outcome == saved; if (ok) ++ui.calls; else
//    ++ui.unsaved;` — a BOOLEAN, so EVERY refusal counted as the failed-save door and **these cases pinned that as
//    correct**. It is not: `TEAM KEY ACTIVE` is FALSE when the live pair was wiped (`no_live_key`), when we have
//    LEFT the team (`not_our_team`) and when the receipt named none (`zero_team`). ⇒ the cases below are RE-PINNED
//    on the three-valued route, and `silent` exists so *"the panel says nothing"* is a COUNTED fact rather than the
//    absence of one — an assertion that only ever reads `unsaved == 0` cannot tell silence from a door that fired.
// ⓘ The bool return is kept ("was the push FORWARDED") because that is what the ~20 existing call sites ask; the
//   two failure routes are told apart by the counters, ⛔ never by the return.
struct UiSink { int calls = 0; int unsaved = 0; int silent = 0; uint32_t last_team = 0; };
bool drain_one(mrfw::TeamKeyGrantService& svc, const mrfw::TeamKeyGrant& g, bool is_grant_push, UiSink& ui) {
    const mrfw::GrantUiRoute r = is_grant_push ? mrfw::grant_ui_route_of(svc.receive(g).outcome)
                                               : mrfw::GrantUiRoute::received;
    switch (r) {
        case mrfw::GrantUiRoute::received:       ++ui.calls; ui.last_team = g.push_team_id; break;
        case mrfw::GrantUiRoute::active_unsaved: ++ui.unsaved; break;
        case mrfw::GrantUiRoute::suppressed:     ++ui.silent;  break;
        case mrfw::GrantUiRoute::count:          break;        // ⛔ not a route; unreachable
    }
    return r == mrfw::GrantUiRoute::received;
}

// ★★★★ THE PRECONDITION OF THE WORD `ACTIVE`, ASSERTED RATHER THAN ASSUMED (QG, 2026-08-25). `TEAM KEY ACTIVE` is
//      true exactly when the three terms re-checks (1)-(3) establish are all in force: a non-zero team, the LIVE
//      membership equal to it, and the key pair actually present. Every `active_unsaved` arm is reached only past
//      those three by control flow — so this is the fixture's independent restatement of that fact, and an arm
//      wrongly classified `active_unsaved` would have to satisfy it to be believed.
// ⓘ It is the fixture's equivalent of the device's `team_channel_pub()/priv()` non-nullness: `GrantFix` hands the
//   service the same two pointers, so "the key is live" means the same thing on both sides of the seam.
bool live_key_really_active(const GrantFix& f, const mrfw::TeamKeyGrant& g) {
    (void)f;
    return g.push_team_id != 0 && g.push_team_id == g.live_team_id
           && g.live_pub != nullptr && g.live_priv != nullptr;
}

}  // namespace

// ---- PIN 1 + the ORDER -------------------------------------------------------------------------------------------
TEST_CASE("ui16-K3: a successful persist writes the KEY FIRST, the ACTIVATION SECOND, and only then forwards") {
    GrantFix f;
    UiSink ui;
    const bool fwd = drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), /*is_grant_push=*/true, ui);

    CHECK(fwd);
    CHECK(ui.calls == 1);                      // ★ PIN 1 — a `saved` verdict forwards the push…
    CHECK(ui.unsaved == 0);                    // …and ⛔ NEVER also raises [[B243]]'s failure note (one door per receipt)
    CHECK(ui.silent == 0);                     // …⛔ nor suppresses it: EXACTLY one of the three routes is taken
    // ★★★ THE F-10 ORDER, AS A MEASUREMENT: `/mrteams` then `/mrcfg`. A reboot landing between the two finds a
    //     RETAINED record with no active binding ⇒ KEYLESS, which is honest. The inverse finds an ACTIVATION with
    //     no key behind it — a binding that lies, and QG blocker 2 arriving by push.
    CHECK(std::strcmp(f.log.str(), "KC") == 0);
    CHECK(f.store.saves == 1);
    CHECK(f.binding.commits == 1);
    // The material really landed, in both records, and it is the LIVE pair — ⛔ not something derived here.
    const int idx = mrfw::team_key_find(f.store.rec, 0xAA11u);
    // ⚠ `-fno-exceptions` ⇒ doctest's `REQUIRE` is unavailable in this build (the note `test_node_query.cpp:331`
    //   carries), so a guard that must stop the case is written as `CHECK` + an explicit `if`.
    CHECK(idx >= 0);
    if (idx >= 0) {
        CHECK(std::memcmp(f.store.rec.rec[idx].team_ch_pub,  f.pub,  32) == 0);
        CHECK(std::memcmp(f.store.rec.rec[idx].team_ch_priv, f.priv, 32) == 0);
    }
    CHECK(f.binding.committed_team == 0xAA11u);
    CHECK(f.binding.active);
    CHECK(f.binding.has_pub);
    CHECK(std::memcmp(f.binding.pub, f.pub, 32) == 0);      // the COMMITTED witness == the keyring's pub (term iv)
    // ⓘ A push of ANY OTHER KIND is untouched by the gate — the drain loop forwards it with no persistence at all.
    UiSink other; CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), /*is_grant_push=*/false, other));
    CHECK(other.calls == 1);
    CHECK(f.writes() == 2);                                  // ⛔ and it spent no further write
}

// ---- PIN 2 — a FAILED persist ⇒ ⛔ NOT forwarded, and the failure is surfaced ------------------------------------
TEST_CASE("ui16-K3: a FAILED persist is NOT forwarded — the panel can never say RECEIVED for a RAM-only key") {
    SUBCASE("the /mrteams save fails: the ACTIVATION is never attempted") {
        GrantFix f;
        f.store.save_ok = false;
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
        CHECK(ui.calls == 0);                                 // ★ PIN 2 — ⛔ the UI is not told it was RECEIVED…
        // ★★★★ [[B243]] — …**AND IT IS TOLD THE TRUE THING INSTEAD**, which is the half that was missing until
        //      2026-08-25: the second door fires EXACTLY ONCE, and ⛔ the two doors are mutually exclusive (a
        //      receipt that raised both would put two verdicts in one result slot and the panel would show the
        //      last writer). The WORDS the note then carries — `TEAM KEY ACTIVE` / `NOT SAVED` / `LOST ON REBOOT`
        //      — are `test/test_firmware_ui_send.cpp`'s; what this pins is that the DEVICE PATH reaches them.
        CHECK(ui.unsaved == 1);
        CHECK(ui.silent == 0);                                // ⛔ and it is ⛔ NOT suppressed — see the note above
        // ★★ AND THE WORDING IS TRUE HERE, WHICH IS THE WHOLE REASON THIS ARM GETS A DOOR AT ALL (QG, 2026-08-25):
        //    `keyring_failed` is reached only PAST re-check (3), so all three preconditions of `TEAM KEY ACTIVE`
        //    are established BY CONTROL FLOW — asserted, ⛔ not assumed.
        CHECK(live_key_really_active(f, f.grant(0xAA11u, 0xAA11u)));
        CHECK(f.store.saves == 1);                            // ONE attempt…
        CHECK(f.binding.commits == 0);                        // …and ⛔ NO activation behind a key that did not land
        CHECK(std::strcmp(f.log.str(), "K") == 0);
        const mrfw::GrantSaveResult r = mrfw::TeamKeyGrantService(f.keyring, f.binding).receive(f.grant(0xAA11u, 0xAA11u));
        CHECK(r.outcome == GrantSave::keyring_failed);
        CHECK(r.err == mrfw::KeyringErr::nv_save_failed);     // the failure is NAMED, ⛔ never material
    }
    SUBCASE("the /mrcfg activation fails AFTER the key landed: still not forwarded") {
        GrantFix f;
        f.binding.commit_ok = false;
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
        CHECK(ui.calls == 0);
        CHECK(ui.unsaved == 1);                               // ★ past re-check (3) ⇒ the note is TRUE and IS shown
        CHECK(ui.silent == 0);
        CHECK(live_key_really_active(f, f.grant(0xAA11u, 0xAA11u)));
        CHECK(f.store.saves == 1);                            // the key IS durable…
        CHECK(f.binding.commits == 1);                        // …the binding is not, so the next boot comes up KEYLESS
        CHECK(std::strcmp(f.log.str(), "KC") == 0);
    }
    SUBCASE("a FULL keyring refuses LOUDLY (P-15) — zero writes, nothing evicted, not forwarded") {
        GrantFix f;
        for (uint8_t i = 0; i < mrnv::kTeamKeyRecs; ++i) {
            uint8_t p[32], s[32];
            CHECK(make_pair(static_cast<uint8_t>(30 + i), p, s));
            CHECK(f.keyring.put(0xB000u + i, p, s).verdict == KeyringVerdict::ok);
        }
        const mrnv::TeamKeyBlob before = f.store.rec;
        const int saves_before = f.store.saves;
        f.binding.membership = 0xC0DEu;
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0xC0DEu, 0xC0DEu), true, ui));
        CHECK(ui.calls == 0);
        // ★ P-15's loud refusal is a `keyring_failed`, i.e. an AFTER-re-check-(3) arm: the key really is live, so
        //   the operator is told so and told it will not survive — ⛔ not left in silence.
        CHECK(ui.unsaved == 1);
        CHECK(ui.silent == 0);
        CHECK(live_key_really_active(f, f.grant(0xC0DEu, 0xC0DEu)));
        CHECK(f.store.saves == saves_before);                 // ⛔ ZERO further writes
        CHECK(f.binding.commits == 0);
        CHECK(std::memcmp(&before, &f.store.rec, sizeof before) == 0);   // ⛔ NOTHING evicted
    }
    SUBCASE("an UNREADABLE /mrcfg record FAILS CLOSED — zero writes on both records") {
        GrantFix f;
        f.binding.read_ok = false;
        // ★★ AND THE RECORD IT DEPOSITED WOULD HAVE PASSED re-check (4) — that is the whole point. A fake that
        //    returns `false` and leaves ZEROES lets re-check (4) refuse for the WRONG reason, so a caller that
        //    ignored the `false` would still be caught and the control would measure nothing (MEASURED).
        f.binding.deposit_on_fail = true;
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
        // ★ `record_unreadable` sits IMMEDIATELY past re-check (3) — the `/mrcfg` read is the first thing after it
        //   — so the live pair is present and it is this team's: the note is true and the door is the right one.
        CHECK(ui.unsaved == 1);
        CHECK(ui.silent == 0);
        CHECK(f.writes() == 0);
        CHECK(f.store.loads == 0);                            // ⛔ the keyring is not even opened
    }
}

// ---- PIN 3 — THE FOUR HANDLING-TIME RE-CHECKS, each failing the write ON ITS OWN --------------------------------
// ★★ EVERY CASE STARTS FROM THE HEALTHY RECEIPT AND BREAKS EXACTLY ONE TERM, so no assertion below can pass for a
//    second reason — the same construction the FIVE-TERM boot restore's cases use.
TEST_CASE("ui16-K3: each of the four handling-time re-checks fails the write on its own, with ZERO writes") {
    SUBCASE("(1) the push names team 0 — ⛔ 0 reads and 0 writes, refused before anything is opened") {
        GrantFix f;
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0u, 0u), true, ui));
        // ⛔⛔ **SUPPRESSED — NEITHER DOOR** (QG, 2026-08-25). The receipt named no team, so there is no team whose
        //    key could be called ACTIVE and nothing true to put on the panel. ⛔ These three lines used to read
        //    `ui.unsaved == 1` by omission, which pinned the defect.
        CHECK(ui.calls == 0);
        CHECK(ui.unsaved == 0);
        CHECK(ui.silent == 1);
        CHECK(f.writes() == 0);
        CHECK(f.binding.reads == 0);
        CHECK(f.store.loads == 0);
        CHECK(f.svc.receive(f.grant(0u, 0u)).outcome == GrantSave::zero_team);
    }
    SUBCASE("(2) the LIVE membership moved between RX and drain (`team 0`, or a switch)") {
        GrantFix f;
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0xAA11u, /*live=*/0u), true, ui));      // we have LEFT the team
        // ⛔⛔ **SUPPRESSED — NEITHER DOOR.** We are no longer in that team, so its key is not this node's active
        //    key and `TEAM KEY ACTIVE` would be a false statement about a membership we have already dropped.
        CHECK(ui.calls == 0);
        CHECK(ui.unsaved == 0);
        CHECK(ui.silent == 1);
        CHECK(f.writes() == 0);
        CHECK(f.svc.receive(f.grant(0xAA11u, 0u)).outcome == GrantSave::not_our_team);
        CHECK(f.svc.receive(f.grant(0xAA11u, 0xBEEFu)).outcome == GrantSave::not_our_team);   // …or moved to another
    }
    SUBCASE("(3) the LIVE key is gone — ⛔ 64 zero bytes are never persisted as a team key") {
        GrantFix f;
        mrfw::TeamKeyGrant g = f.grant(0xAA11u, 0xAA11u);
        g.live_pub = nullptr;                                  // `team_channel_pub()` answers null while keyless
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, g, true, ui));
        // ⛔⛔ **SUPPRESSED — NEITHER DOOR, AND THIS IS THE ARM QG's BLOCKER NAMED FIRST.** The pair was WIPED
        //    between RX and drain: there is no live key, so `TEAM KEY ACTIVE` is not merely unhelpful, it is FALSE.
        CHECK(ui.calls == 0);
        CHECK(ui.unsaved == 0);
        CHECK(ui.silent == 1);
        CHECK(g.live_pub == nullptr);                          // the fixture really is keyless (⛔ not vacuous)
        CHECK(f.writes() == 0);
        // ★★ AND IT REFUSES BEFORE ANY I/O AT ALL — ⛔ zero `/mrcfg` reads and zero keyring loads. A refusal that
        //    still paid a flash read would be a receipt-shaped cost for a receipt that was always going to be
        //    dropped, and it is the PLACEMENT half of this clause (its own mutation, T37).
        CHECK(f.binding.reads == 0);
        CHECK(f.store.loads == 0);
        CHECK(f.svc.receive(g).outcome == GrantSave::no_live_key);
        mrfw::TeamKeyGrant h = f.grant(0xAA11u, 0xAA11u);
        h.live_priv = nullptr;                                 // …and the PRIVATE half alone is enough to refuse
        CHECK(f.svc.receive(h).outcome == GrantSave::no_live_key);
        CHECK(f.writes() == 0);
    }
    SUBCASE("(4) the PERSISTED record names ANOTHER team — the second authority, and it disagrees") {
        // ★★★ THIS IS THE ONE A LIVE-ONLY CHECK CANNOT SEE: `/mrcfg` legitimately lags the running config between a
        //     live change and its save, so (2) passing says nothing about what a REBOOT would read. Marking a key
        //     ACTIVE against a record that names another team is QG blocker 3 arriving by push.
        GrantFix f;
        f.binding.membership = 0xBEEFu;                        // the RECORD still says we are in the old team
        UiSink ui;
        CHECK_FALSE(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
        CHECK(ui.calls == 0);
        // ★ ⛔ NOT suppressed: re-checks (1)-(3) all PASSED, so the live key is present and it is THIS team's. The
        //   key really is active; only the durable side refused. The three ruled rows are true here.
        CHECK(ui.unsaved == 1);
        CHECK(ui.silent == 0);
        CHECK(live_key_really_active(f, f.grant(0xAA11u, 0xAA11u)));
        CHECK(f.binding.reads == 1);                           // it WAS asked…
        CHECK(f.writes() == 0);                                // …and answered with zero writes
        CHECK(f.store.loads == 0);                             // ⛔ and the keyring was never opened
        CHECK(f.svc.receive(f.grant(0xAA11u, 0xAA11u)).outcome == GrantSave::record_mismatch);
    }
}

// ---- PIN 4 — a RE-GRANT replaces that team's record ATOMICALLY and IDEMPOTENTLY ---------------------------------
TEST_CASE("ui16-K3: a re-key REPLACES this team's record in place — one row, one write, both records agree") {
    GrantFix f;
    UiSink ui;
    CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
    CHECK(f.store.rec.count == 1);

    uint8_t pub2[32], priv2[32];
    CHECK(make_pair(99, pub2, priv2));
    mrfw::TeamKeyGrant g2 = f.grant(0xAA11u, 0xAA11u);
    g2.live_pub = pub2; g2.live_priv = priv2;                  // the teammate re-granted with NEW material
    CHECK(drain_one(f.svc, g2, true, ui));

    CHECK(ui.calls == 2);
    CHECK(f.store.rec.count == 1);                             // ★ ONE row for one team_id — ⛔ never a second
    const int idx = mrfw::team_key_find(f.store.rec, 0xAA11u);
    CHECK(idx >= 0);
    if (idx >= 0) CHECK(std::memcmp(f.store.rec.rec[idx].team_ch_priv, priv2, 32) == 0);
    CHECK(std::memcmp(f.binding.pub, pub2, 32) == 0);          // and the WITNESS moved with it, or the boot rejects
    CHECK(f.store.saves == 2);
    CHECK(f.binding.commits == 2);
}

// ---- PIN 5 — a DIFFERENT team's grant is refused UPSTREAM, and would write nothing here either ------------------
TEST_CASE("ui16-K3: a foreign team's grant never gets here — and if it did it would write NOTHING") {
    // ★★ THE UPSTREAM REFUSAL IS `lib/core`'s AND IS PINNED THERE, ⛔ not re-implemented: `team_key_grant_receive`
    //    answers `TeamKeyGrantRx::team_mismatch` for `their_team != _cfg.team_id` (`lib/core/node.cpp:286`) and
    //    ⛔ enqueues NO push at all — `test/test_node_channel.cpp` drives both that verdict and the
    //    "no team_key_received push" assertion. ⇒ this handler is a SECOND line, and the case exists because a
    //    handler that relies on an upstream guard is a handler that breaks when the guard moves (C2).
    GrantFix f;
    UiSink ui;
    CHECK_FALSE(drain_one(f.svc, f.grant(/*push=*/0xF00Du, /*live=*/0xAA11u), true, ui));
    CHECK(ui.calls == 0);
    CHECK(f.writes() == 0);
    CHECK(f.store.loads == 0);
}

// ---- PIN 6 — IDENTICAL MATERIAL ⇒ **ZERO WRITES**, counted on BOTH records --------------------------------------
TEST_CASE("ui16-K3: a re-grant of IDENTICAL material writes NOTHING — on the keyring AND on /mrcfg") {
    // ⓘ Re-granting the SAME key is the COMMON case, not an edge one: a teammate re-sends on every join, and the
    //   flash this receipt would otherwise burn is a SECRET's flash. K1 measured the keyring half; this measures
    //   the half K3 adds, because a coalescing guard installed on one of two records is not a coalescing guard.
    GrantFix f;
    UiSink ui;
    CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
    const int saves = f.store.saves, commits = f.binding.commits;
    CHECK(saves == 1);
    CHECK(commits == 1);

    for (int i = 0; i < 5; ++i) CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));

    CHECK(ui.calls == 6);                                      // ★ every one of them is still FORWARDED…
    CHECK(f.store.saves   == saves);                           // …at a cost of ⛔ ZERO further writes
    CHECK(f.binding.commits == commits);
    // ★ AND THE ACTIVATION IS SKIPPED ONLY WHEN THE RECORD REALLY WITNESSES **THIS** KEY: break the witness alone
    //   and the very next receipt rewrites it, so the guard is "already correct", ⛔ never "already present".
    f.binding.has_pub = false;
    CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
    CHECK(f.binding.commits == commits + 1);
    CHECK(f.store.saves == saves);                             // ⛔ and the keyring still wrote nothing
    f.binding.active = false;                                  // the binding was cleared (`team 0` then re-join)
    CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
    CHECK(f.binding.commits == commits + 2);
    uint8_t stranger[32]; std::memset(stranger, 0x5A, sizeof stranger);
    std::memcpy(f.binding.pub, stranger, 32);                  // …or witnesses a DIFFERENT public half
    CHECK(drain_one(f.svc, f.grant(0xAA11u, 0xAA11u), true, ui));
    CHECK(f.binding.commits == commits + 3);
}

TEST_CASE("ui16-K3: the verdict enum renders in full, and ⛔ no arm carries material") {
    // ⛔⛔⛔ **THE INVENTORY IS THE ENUM's, ⛔ NOT A BOUND RE-TYPED HERE** (2026-08-25, the §UI-16 N6b precedent
    //      applied a second time). ⚠ THE HISTORY IS THE ARGUMENT: this loop used to read
    //      `i <= uint8_t(GrantSave::binding_failed)` — a HAND-WRITTEN last enumerator — so a ninth outcome appended
    //      after `binding_failed` would have been left unswept while the case still called itself exhaustive. That
    //      is the exact shape that already cost this arc one silently-short sweep (`InviteGrantState`'s array).
    // ★★★★ THE COUPLING IS MECHANICAL NOW, ON TWO INDEPENDENT AXES, AND NEITHER IS A LITERAL:
    //      (1) the sweep walks `0 .. GrantSave::count - 1`, so an outcome added to the enum is visited BY
    //          CONSTRUCTION — there is no bound to forget; and
    //      (2) `grant_save_name`'s switch has ⛔ no `default:`, so an outcome added and NOT worded is a
    //          **BUILD FAILURE** (the blanket `-Werror=switch`), not a green test.
    //      ⇒ adding an outcome without extending this case's coverage is impossible: it either compiles and is
    //      swept, or it does not compile.
    for (uint8_t i = 0; i < static_cast<uint8_t>(GrantSave::count); ++i)
        CHECK(std::strcmp(mrfw::grant_save_name(static_cast<GrantSave>(i)), "?") != 0);
    CHECK(std::strcmp(mrfw::grant_save_name(GrantSave::saved), "saved") == 0);
    CHECK(std::strcmp(mrfw::grant_save_name(GrantSave::record_mismatch), "record_mismatch") == 0);
    // ⓘ THE SENTINEL IS NOT SWEPT AND MUST NOT BE: it is not an outcome, so its honest answer is the SAME `?`
    //   refusal an out-of-range cast gets. Pinned in both directions so it cannot quietly become a state — and so
    //   that the `<` above cannot be "fixed" back to a `<=` without this line reddening.
    CHECK(std::strcmp(mrfw::grant_save_name(GrantSave::count), "?") == 0);
    CHECK(static_cast<uint8_t>(GrantSave::count) > static_cast<uint8_t>(GrantSave::binding_failed));
    // ★ `saved` IS ZERO deliberately: it is the only value the drain loop's gate lets through, and a default-
    //   constructed `GrantSaveResult` must ⛔ NOT be it — the fail-closed direction (C2).
    CHECK(static_cast<uint8_t>(GrantSave::saved) == 0);
    CHECK(mrfw::GrantSaveResult{}.outcome != GrantSave::saved);
}

// ---- [[B243]] / QG 2026-08-25 — THE UI ROUTING CLASSIFICATION, ARM BY ARM AND OVER THE WHOLE ENUM ----------------
TEST_CASE("ui16-K3: every GrantSave arm is classified, and ⛔ no arm claims a key that is not live") {
    using R = mrfw::GrantUiRoute;
    // ★★★ THE SWEEP IS THE ENUM's (the same fence the word sweep uses): `0 .. count-1`, so an outcome added to
    //     `GrantSave` is classified here BY CONSTRUCTION, and one added and NOT classified is a `-Werror=switch`
    //     BUILD FAILURE in `grant_ui_route_of`. ⛔ There is no list beside the enum to keep in sync.
    int received = 0, unsaved = 0, silent = 0;
    for (uint8_t i = 0; i < static_cast<uint8_t>(GrantSave::count); ++i) {
        const R r = mrfw::grant_ui_route_of(static_cast<GrantSave>(i));
        CHECK(r != R::count);                                  // ⛔ the sentinel is never a classification
        if      (r == R::received)       ++received;
        else if (r == R::active_unsaved) ++unsaved;
        else                             ++silent;
    }
    CHECK(received == 1);                                      // ⛔ EXACTLY ONE outcome forwards the push
    CHECK(received + unsaved + silent == int(GrantSave::count));
    // ★★★★ **THE THREE SUPPRESSED ARMS, NAMED — THIS IS QG's 2026-08-25 BLOCKER TURNED INTO A PIN.** Each of them
    //      is refused BEFORE re-check (3), so at that point there is ⛔ NO live key, ⛔ no established membership,
    //      or ⛔ no team at all — and `TEAM KEY ACTIVE` would be a FALSE statement, not merely an unhelpful one.
    CHECK(mrfw::grant_ui_route_of(GrantSave::zero_team)    == R::suppressed);
    CHECK(mrfw::grant_ui_route_of(GrantSave::not_our_team) == R::suppressed);
    CHECK(mrfw::grant_ui_route_of(GrantSave::no_live_key)  == R::suppressed);
    CHECK(silent == 3);                                        // …and ⛔ NO OTHER arm is silenced
    // ★ THE FOUR `active_unsaved` ARMS, ALSO NAMED: every one sits PAST re-check (3), so the key genuinely is live
    //   and the failure is durability alone. ⛔ None of them may be collapsed into silence — that loses the honest
    //   note [[B243]] exists to deliver.
    CHECK(mrfw::grant_ui_route_of(GrantSave::record_unreadable) == R::active_unsaved);
    CHECK(mrfw::grant_ui_route_of(GrantSave::record_mismatch)   == R::active_unsaved);
    CHECK(mrfw::grant_ui_route_of(GrantSave::keyring_failed)    == R::active_unsaved);
    CHECK(mrfw::grant_ui_route_of(GrantSave::binding_failed)    == R::active_unsaved);
    CHECK(unsaved == 4);
    CHECK(mrfw::grant_ui_route_of(GrantSave::saved) == R::received);
    // ⛔ AND THE SENTINEL FAILS CLOSED: a value that should be impossible may never talk its way onto the panel.
    CHECK(mrfw::grant_ui_route_of(GrantSave::count) == R::suppressed);
    CHECK(static_cast<uint8_t>(R::count) > static_cast<uint8_t>(R::suppressed));
}

// =====================================================================================================================
// §UI-16 K5 — THE PRESENCE QUESTION AND THE **EXPLICIT** ACTIVATION (spec §4-K5, P-2b)
// =====================================================================================================================
// ★★★ WHAT THIS BLOCK MEASURES: the two decisions `SAVED KEY FOUND` / `USE SAVED KEY` rest on — *"is a key for this
//     exact team RETAINED?"* (a BOOLEAN, zero writes, fails closed) and *"install it, durably, or leave the node
//     KEYLESS"*. ⛔ WHERE THE OFFER SITS IN THE FLOW is the MODEL's (`test/test_firmware_ui_model.cpp`) and the
//     MAPPING onto panel words is the ADAPTER's (`test/test_firmware_ui_prov.cpp`); neither is re-measured here.
// ⛔ AND NOTHING HERE MAY BE DESCRIBED AS PROVING ANYTHING ABOUT FLASH — the store is a fake, and [[B193]]'s
//    power-cut half is a bench check (this file's standing boundary, unchanged).

TEST_CASE("ui16-k5: `has_record` answers a BOOLEAN about ONE exact id, spends ZERO writes and FAILS CLOSED") {
    Fix f;
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x41, pub, priv));
    CHECK(f.svc.put(0x51CE0004u, pub, priv).verdict == mrfw::KeyringVerdict::ok);
    const int saves_after_put = f.store.saves;
    const mrnv::TeamKeyBlob before = f.store.rec;

    CHECK(f.svc.has_record(0x51CE0004u) == true);           // the team whose key we hold
    CHECK(f.svc.has_record(0x51CE0005u) == false);          // ⛔ a NEIGHBOURING id — one bit apart, no record
    CHECK(f.svc.has_record(0u) == false);                   // ⛔ 0 is never stored, so it can never be FOUND
    // ★★ ZERO WRITES ON EVERY ANSWER, and the record did not move one byte: a QUESTION may not touch the store.
    CHECK(f.store.saves == saves_after_put);
    CHECK(std::memcmp(&f.store.rec, &before, sizeof before) == 0);

    // ★★★ AND IT FAILS CLOSED (C2) ON EVERY UNREADABLE STATE: an offer made against a store nobody could read
    //     would walk the operator into a refusal. ⓘ The three are one answer HERE and stay distinguishable in
    //     `use_saved`, where he has ASKED.
    // ⚠⚠ THE STORE **DEPOSITS A PLAUSIBLE RECORD ON EVERY FAILING READ**, AND WITHOUT IT THIS LOOP MEASURED
    //    NOTHING — ★ MEASURED 2026-08-25, ⛔ not reasoned: the first cut left `deposit_rec` false, so a failing read
    //    handed back 0xA5 filler in which no lookup could succeed, and `--target=teamkeyring` T42 (the FAIL-OPEN
    //    mutation) stayed GREEN. That is the [[B217]] shape exactly — a case whose zero came from the fixture rather
    //    than from the rule. ⓘ The real `read_slot` may leave a PARTIAL record behind before answering false
    //    (device_nv.h's §nv-ritual warning), so a reader that IGNORES the answer does ⛔ not get zeroes — it gets
    //    bytes that LOOK right, and here they are THIS TEAM's own record. ⇒ `false` below is now a decision about
    //    the READ ANSWER and about nothing else.
    f.store.deposit_rec = true;
    for (mrnv::TeamKeyRead st : { mrnv::TeamKeyRead::absent, mrnv::TeamKeyRead::invalid,
                                  mrnv::TeamKeyRead::io_failed }) {
        f.store.state = st;
        CHECK(mrfw::team_key_find(f.store.rec, 0x51CE0004u) >= 0);   // the deposit REALLY holds the team (⛔ vacuity)
        CHECK(f.svc.has_record(0x51CE0004u) == false);
        CHECK(f.store.saves == saves_after_put);            // ⛔ still zero writes
    }
    f.store.state = mrnv::TeamKeyRead::ok;
    CHECK(f.svc.has_record(0x51CE0004u) == true);           // ★ the positive arm again: the zeros above are evidence
}

TEST_CASE("ui16-k5: ★★★ `USE SAVED KEY` installs the RETAINED pair and makes it BOOT-DURABLE (the five terms hold)") {
    // ★★★★ THE SLICE'S POSITIVE ARM, AND IT IS PROVED BY **DRIVING THE REAL RESTORE PATH** against the state this
    //      activation wrote — ⛔ not by reading the source and agreeing that the terms look satisfied.
    Fix f;
    FakeKeyLive live;
    FakeBinding bind;
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x77, pub, priv));
    CHECK(f.svc.put(0x77D9348Au, pub, priv).verdict == mrfw::KeyringVerdict::ok);
    const mrnv::TeamKeyBlob before = f.store.rec;
    const int saves_before = f.store.saves;
    bind.membership = 0x77D9348Au;              // we JOINED that team first — the membership write already happened
    CHECK(live.installed == false);             // ...and the join left us KEYLESS (the precondition, asserted)

    CHECK(f.svc.use_saved(0x77D9348Au, live, bind) == mrfw::SavedKeyUse::installed);
    // ★★ THE LIVE HALF: adopted EXACTLY ONCE, through the accessor that RE-DERIVES the public half — and ⛔ the
    //    governance never fired, because this is the one arm that does not refuse.
    CHECK(live.installed == true);
    CHECK(live.calls == 1);
    CHECK(live.clear_calls == 0);
    CHECK(std::memcmp(live.pub, pub, 32) == 0);
    // ★★ THE DURABLE HALF: exactly ONE `/mrcfg` activation, for exactly THIS team...
    CHECK(bind.commits == 1);
    CHECK(bind.committed_team == 0x77D9348Au);
    CHECK(bind.active == true);
    // ...and ⛔ ZERO `/mrteams` writes: the key was ALREADY durable, so an activation may not spend flash on it.
    CHECK(f.store.saves == saves_before);
    CHECK(std::memcmp(&f.store.rec, &before, sizeof before) == 0);

    // ★★★★ THE BOOT-DURABILITY PROOF: a FRESH live seam (i.e. a power cycle — nothing is installed) plus the FIVE
    //      TERMS read off the record this activation actually wrote. ⛔ The binding is not hand-built: `bind.pub`
    //      and `bind.binding_team` are what `commit_active` stored.
    FakeKeyLive rebooted;
    mrfw::TeamKeyBinding after{};
    after.membership_team_id = bind.membership;
    after.binding_team_id    = bind.binding_team;
    after.key_active         = bind.active;
    after.committed_present  = bind.has_pub;
    after.committed_pub      = bind.pub;
    CHECK(f.svc.restore(after, rebooted) == mrfw::KeyringRestore::installed);
    CHECK(rebooted.installed == true);
    CHECK(std::memcmp(rebooted.pub, pub, 32) == 0);         // ★ the SAME key, after the reboot
    CHECK(f.store.saves == saves_before);                   // ⛔ and a restore still writes nothing
}

TEST_CASE("ui16-k5: ★★★ EVERY failing arm installs NOTHING, keeps the record INTACT and clears SURGICALLY") {
    // ★★★★ ⛔ TITLE AND SHAPE CORRECTED IN PLACE 2026-08-25 (QG blocker 1), AND THE WITHDRAWN CONTRACT IS KEPT
    //      VISIBLE: this case read *"EVERY failing arm leaves the node KEYLESS…"* and asserted `clear_calls == 1` on
    //      every one of them, because the first cut routed all refusals through the boot restore's clearing funnel.
    //      ⛔ **THAT WAS WRONG, AND IT WAS WRONG IN THE DANGEROUS DIRECTION**: the live key at this moment belongs to
    //      whatever team `/mrcfg` currently NAMES, so a refusal that clears destroys an INNOCENT key — the key of
    //      the team the operator is actually in. ⇒ the refusals are now SURGICAL, and the assertions below flip with
    //      them: `clear_calls == 0` on every arm, and ⛔ EXACTLY ONE arm wipes — `binding_failed`, which is UNDOING
    //      ITS OWN INSTALL and not applying a verdict to somebody else's key.
    // ★★ EVERY ARM SEEDS THE MEMBERSHIP IT MEANS TO MEASURE (the §7.1 rule-1 discipline): without it each one would
    //    refuse at the NEW re-check and the case would pass for the wrong reason — which is exactly what the first
    //    run of this correction did, and why the seeds below are asserted rather than assumed.
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x31, pub, priv));

    {   // (a) `zero_team` — ⛔ 0 READS and 0 writes: the floor is asked before any authority is consulted.
        Fix f; FakeKeyLive live; FakeBinding bind;
        CHECK(f.svc.put(0x51CE0004u, pub, priv).verdict == mrfw::KeyringVerdict::ok);
        const int loads = f.store.loads, saves = f.store.saves;
        CHECK(f.svc.use_saved(0u, live, bind) == mrfw::SavedKeyUse::zero_team);
        CHECK(f.store.loads == loads);                      // ⛔ ZERO keyring loads — the floor is FIRST
        CHECK(f.store.saves == saves);
        CHECK(bind.reads == 0);                             // ⛔ ...and ⛔ not even the /mrcfg record was read
        CHECK(live.calls == 0);
        CHECK(bind.commits == 0);
        CHECK(live.clear_calls == 0);                       // ⛔ SURGICAL: nothing of ours, nothing cleared
    }
    {   // ★★★★ (b) **THE A -> B RACE — QG BLOCKER 1's OWN CASE.** The offer was built for team A; a `team <id>` over
        //     serial moves MEMBERSHIP to B before the operator's `double` lands. Installing A's key then would put
        //     A's secret live under a `/mrcfg` that says B — a binding the five-term boot restore REJECTS, under a
        //     panel that just claimed `TEAM KEY ACTIVE`. ⇒ REFUSED, and ⛔ B's live key, B's binding and A's record
        //     are all left EXACTLY as they were.
        Fix f; FakeKeyLive live; FakeBinding bind;
        const uint32_t A = 0x51CE0004u, B = 0x77D9348Au;
        CHECK(f.svc.put(A, pub, priv).verdict == mrfw::KeyringVerdict::ok);
        // ...and B is a REAL team with a REAL live key and its OWN active binding — without that, "B's key survived"
        // would be a statement about a node that never had one (the §W10b lesson).
        uint8_t bpub[32], bpriv[32];
        CHECK(make_pair(0x62, bpub, bpriv));
        CHECK(live.adopt_key(bpub, bpriv));
        bind.membership   = B;                              // ★ THE RACE: /mrcfg now names B
        bind.binding_team = B;
        bind.active       = true;
        bind.has_pub      = true;
        std::memcpy(bind.pub, bpub, 32);
        const mrnv::TeamKeyBlob before = f.store.rec;
        const int saves = f.store.saves, loads = f.store.loads;
        const int adopts = live.calls;

        CHECK(f.svc.use_saved(A, live, bind) == mrfw::SavedKeyUse::not_our_team);
        // ★★★ B's LIVE KEY IS UNTOUCHED — ⛔ not cleared, ⛔ not overwritten by A's.
        CHECK(live.installed == true);
        CHECK(live.clear_calls == 0);
        CHECK(live.calls == adopts);                        // ⛔ A's pair was never even offered to the accessor
        CHECK(std::memcmp(live.pub, bpub, 32) == 0);        // ...and it is still B's
        // ★★★ B's ACTIVE BINDING IS UNTOUCHED, and ⛔ NOTHING was written on either record.
        CHECK(bind.commits == 0);
        CHECK(bind.binding_team == B);
        CHECK(bind.active == true);
        CHECK(f.store.saves == saves);
        CHECK(f.store.loads == loads);                      // ⛔ the SECRET store was not even opened
        // ★★★ AND A's RETAINED RECORD IS INTACT — the operator can still be offered it after re-joining A.
        CHECK(std::memcmp(&f.store.rec, &before, sizeof before) == 0);
        CHECK(mrfw::team_key_find(f.store.rec, A) >= 0);
        // ★★ THE POSITIVE ARM IN THE SAME CASE: the identical call with the race removed INSTALLS — so the refusal
        //    above is a measurement of the re-check and ⛔ not of a fixture that cannot succeed.
        bind.membership = A;
        CHECK(f.svc.use_saved(A, live, bind) == mrfw::SavedKeyUse::installed);
        CHECK(std::memcmp(live.pub, pub, 32) == 0);
        CHECK(bind.commits == 1);
    }
    {   // (c) `record_unreadable` — the `/mrcfg` record could not be read ⇒ FAIL CLOSED (C2): an unestablished term
        //     is ⛔ never treated as satisfied, so ⛔ nothing is adopted, written or cleared.
        Fix f; FakeKeyLive live; FakeBinding bind;
        CHECK(f.svc.put(0x51CE0004u, pub, priv).verdict == mrfw::KeyringVerdict::ok);
        bind.membership = 0x51CE0004u;                      // the term WOULD have passed — the read is what fails
        bind.read_ok    = false;
        const int saves = f.store.saves, loads = f.store.loads;
        CHECK(f.svc.use_saved(0x51CE0004u, live, bind) == mrfw::SavedKeyUse::record_unreadable);
        CHECK(live.calls == 0);
        CHECK(live.clear_calls == 0);
        CHECK(bind.commits == 0);
        CHECK(f.store.saves == saves);
        CHECK(f.store.loads == loads);
    }
    {   // (d) `no_record` — an ABSENT store, and a store that simply holds ANOTHER team's key. ⛔ Never a substitute.
        Fix f; FakeKeyLive live; FakeBinding bind;
        bind.membership = 0x51CE0004u;
        f.store.state = mrnv::TeamKeyRead::absent;
        CHECK(f.svc.use_saved(0x51CE0004u, live, bind) == mrfw::SavedKeyUse::no_record);
        CHECK(live.clear_calls == 0);
        CHECK(bind.commits == 0);
        CHECK(f.store.saves == 0);

        Fix g; FakeKeyLive live2; FakeBinding bind2;
        CHECK(g.svc.put(0x51CE0004u, pub, priv).verdict == mrfw::KeyringVerdict::ok);
        const mrnv::TeamKeyBlob before = g.store.rec;
        const int saves = g.store.saves;
        bind2.membership = 0x77D9348Au;                     // we ARE in the team we are asking about...
        CHECK(g.svc.use_saved(0x77D9348Au, live2, bind2) == mrfw::SavedKeyUse::no_record);   // ...it just has no key
        CHECK(live2.installed == false);                    // ⛔ the OTHER team's key was NOT installed
        CHECK(live2.calls == 0);
        CHECK(live2.clear_calls == 0);
        CHECK(g.store.saves == saves);
        CHECK(std::memcmp(&g.store.rec, &before, sizeof before) == 0);
    }
    {   // (e) `store_failed` — ⛔ NOT collapsed with `no_record`: "the record is not there" and "the store could not
        //     be read" take different operator actions, and he has explicitly asked.
        for (mrnv::TeamKeyRead st : { mrnv::TeamKeyRead::invalid, mrnv::TeamKeyRead::io_failed }) {
            Fix f; FakeKeyLive live; FakeBinding bind;
            bind.membership = 0x51CE0004u;
            f.store.state = st;
            CHECK(f.svc.use_saved(0x51CE0004u, live, bind) == mrfw::SavedKeyUse::store_failed);
            CHECK(live.clear_calls == 0);
            CHECK(live.installed == false);
            CHECK(bind.commits == 0);
            CHECK(f.store.saves == 0);                      // ⛔ a blind rewrite would destroy four intact keys
        }
    }
    {   // (f) `rejected` — THE CORRUPTION ARM, and it is measured against the REAL rule: the fake's `adopt_key` is a
        //     1:1 mirror of `Node::team_channel_key_adopt`, so a record whose pub does not derive from its priv is
        //     refused BY THE DERIVATION and ⛔ never installed verbatim.
        // ★★ AND THE **INNOCENT LIVE KEY SURVIVES IT**: the node holds this same team's key (a serial import a
        //    moment ago), the saved record is corrupt, and the refusal may ⛔ not take the working key with it.
        Fix f; FakeKeyLive live; FakeBinding bind;
        uint8_t bad_pub[32];
        std::memcpy(bad_pub, pub, 32);
        bad_pub[0] = static_cast<uint8_t>(bad_pub[0] ^ 0x01);          // one bit of rot in the PUBLIC half
        CHECK(f.svc.put(0x51CE0004u, bad_pub, priv).verdict == mrfw::KeyringVerdict::ok);
        uint8_t ipub[32], ipriv[32];
        CHECK(make_pair(0x55, ipub, ipriv));
        CHECK(live.adopt_key(ipub, ipriv));                 // ...the innocent key, live before the act
        const mrnv::TeamKeyBlob before = f.store.rec;
        const int saves = f.store.saves, adopts = live.calls;
        bind.membership = 0x51CE0004u;
        CHECK(f.svc.use_saved(0x51CE0004u, live, bind) == mrfw::SavedKeyUse::rejected);
        CHECK(live.calls == adopts + 1);                    // ...it WAS offered to the accessor...
        CHECK(live.clear_calls == 0);                       // ⛔ ...and the refusal cleared NOTHING (surgical)
        CHECK(live.installed == true);                      // ★ the innocent key is still live...
        CHECK(std::memcmp(live.pub, ipub, 32) == 0);        // ...and it is still the SAME key
        CHECK(bind.commits == 0);                           // ⛔ and ⛔ no activation was written for a bad record
        CHECK(f.store.saves == saves);
        CHECK(std::memcmp(&f.store.rec, &before, sizeof before) == 0);
    }
    {   // (g) `binding_failed` — ★ THE ONE ARM THAT WIPES, AND IT IS AN **UNDO OF ITS OWN INSTALL**: the key was
        //     adopted, the `/mrcfg` write failed, so the node is put back rather than left reading a channel it will
        //     lose at the next boot. ⓘ The record stays RETAINED — the operator can try again.
        Fix f; FakeKeyLive live; FakeBinding bind;
        CHECK(f.svc.put(0x51CE0004u, pub, priv).verdict == mrfw::KeyringVerdict::ok);
        const mrnv::TeamKeyBlob before = f.store.rec;
        const int saves = f.store.saves;
        bind.membership = 0x51CE0004u;
        bind.commit_ok  = false;
        CHECK(f.svc.use_saved(0x51CE0004u, live, bind) == mrfw::SavedKeyUse::binding_failed);
        CHECK(live.calls == 1);                             // it really was adopted first...
        CHECK(bind.commits == 1);                           // ...the ONE attempt was made and reported false...
        CHECK(live.clear_calls == 1);                       // ...and THIS VERB'S OWN install was UNDONE
        CHECK(live.installed == false);
        CHECK(f.store.saves == saves);
        CHECK(std::memcmp(&f.store.rec, &before, sizeof before) == 0);
        CHECK(mrfw::team_key_find(f.store.rec, 0x51CE0004u) >= 0);      // ★ still RETAINED — a retry is possible
        // ★★ AND THE NEXT BOOT COMES UP KEYLESS, driven rather than argued: no active binding was committed.
        FakeKeyLive rebooted;
        mrfw::TeamKeyBinding after{};
        after.membership_team_id = 0x51CE0004u;
        after.binding_team_id    = bind.binding_team;       // ⛔ still 0 — the commit failed
        after.key_active         = bind.active;
        CHECK(f.svc.restore(after, rebooted) == mrfw::KeyringRestore::no_binding);
        CHECK(rebooted.installed == false);
    }
}

TEST_CASE("ui16-k5: ★★★ the WRITER refuses an ACTIVE BINDING for a team its own record does not name") {
    // ★★★★ QG blocker 1's SECOND AUTHORITY, and it is pure so it can be attacked: `commit_active` writes the binding
    //      into the `/mrcfg` record it has just loaded, and that record carries the MEMBERSHIP. A binding for a team
    //      the record does not name is a binding that LIES — the five-term boot restore compares the two (term (ii))
    //      and comes up KEYLESS while the panel claimed a key was active. ⇒ the writer refuses, ⛔ without writing.
    // ⓘ THE REAL DEVICE WRITER (`DeviceTeamKeyBinding`, `src/firmware_config.cpp`), THIS SUITE'S FAKE, the adapter
    //   suite's fake and the UI probe's binding all call THIS ONE predicate (U1) — the device TU is compiled by no
    //   gate (§B115), so the decision has to live where a mutation can reach it.
    CHECK(mrfw::commit_membership_ok(0x51CE0004u, 0x51CE0004u) == true);
    CHECK(mrfw::commit_membership_ok(0x77D9348Au, 0x51CE0004u) == false);   // ★ the A -> B race, at the write
    CHECK(mrfw::commit_membership_ok(0u, 0x51CE0004u) == false);            // an unprovisioned record names no team
    CHECK(mrfw::commit_membership_ok(0x51CE0004u, 0u) == false);            // ⛔ and 0 is never a target
    CHECK(mrfw::commit_membership_ok(0u, 0u) == false);
    // ★★ AND THE FAKE ENFORCES IT, so no case above can pass against a writer more permissive than the device's.
    FakeBinding bind;
    bind.membership = 0x77D9348Au;                                        // /mrcfg says B...
    uint8_t pub[32], priv[32];
    CHECK(make_pair(0x44, pub, priv));
    CHECK(bind.commit_active(0x51CE0004u, pub, priv) == false);            // ...so a binding for A is REFUSED
    CHECK(bind.commits == 1);                                             // the attempt is counted ([[B193]]'s rule)
    CHECK(bind.binding_team == 0u);                                       // ⛔ and nothing was written
    CHECK(bind.active == false);
    CHECK(bind.commit_active(0x77D9348Au, pub, priv) == true);            // ★ the positive arm: B's own binding lands
    CHECK(bind.binding_team == 0x77D9348Au);
    CHECK(bind.active == true);
}

TEST_CASE("ui16-k5: the `SavedKeyUse` inventory is a PROPERTY OF THE ENUM — ⛔ not a hand-typed list") {
    // ★★★★ ⛔ REWRITTEN IN PLACE 2026-08-25 (QG blocker 2), AND THE WITHDRAWN SHAPE IS KEPT VISIBLE: this case built
    //      `const mrfw::SavedKeyUse every[] = { installed, zero_team, no_record, store_failed, rejected,
    //      binding_failed };` and asserted `sizeof(every)/sizeof(every[0]) == 6u` — a HAND-MAINTAINED inventory whose
    //      literal and whose array could only agree by somebody remembering to edit both. ⛔ This arc has already
    //      failed exactly that way once (`mrui::InviteGrantState`: an array stayed short while its hand-typed
    //      literal stayed right, and the sweep went on calling itself exhaustive). ⇒ THE THIRD INSTANCE OF THE
    //      FENCE, matching `GrantSave::count` and `InviteGrantState::count`:
    //        (1) the sweep IS `0 .. count-1`, so an arm added to the enum is visited BY CONSTRUCTION;
    //        (2) `saved_key_use_name` has ⛔ NO `default:`, so an arm added and NOT worded is a BUILD FAILURE;
    //        (3) the sentinel itself is pinned LAST and answers `?`, so it cannot quietly become an outcome.
    //      ⇒ there is no pair left to keep in sync.
    const int n = static_cast<int>(mrfw::SavedKeyUse::count);
    CHECK(n >= 6);                                          // a floor, ⛔ not an equality: arms may be ADDED
    int worded = 0;
    for (int i = 0; i < n; ++i) {
        const mrfw::SavedKeyUse x = static_cast<mrfw::SavedKeyUse>(i);
        const char* wx = mrfw::saved_key_use_name(x);
        CHECK(wx[0] != '\0');
        CHECK(std::strcmp(wx, "?") != 0);                   // ⛔ no arm falls through to the refusal token
        CHECK(std::strlen(wx) <= 19u);                      // ...and the panel's 19-column body still holds it
        ++worded;
        // ⛔ EVERY WORD IS DISTINCT: a collapse would make two outcomes indistinguishable on the operator's second
        //    row, which is the "success that isn't" shape at the diagnostic end.
        for (int j = 0; j < n; ++j)
            CHECK((std::strcmp(wx, mrfw::saved_key_use_name(static_cast<mrfw::SavedKeyUse>(j))) == 0) == (i == j));
    }
    CHECK(worded == n);                                     // the sweep really visited every arm
    // ★ THE SENTINEL IS ⛔ NOT AN OUTCOME: it is LAST, and it answers the refusal token rather than a plausible word.
    CHECK(std::strcmp(mrfw::saved_key_use_name(mrfw::SavedKeyUse::count), "?") == 0);
    CHECK(static_cast<int>(mrfw::SavedKeyUse::binding_failed) < n);
    // ★ AND THE NAMED ARMS ARE STILL THE ARMS THE SERVICE RETURNS — spelled once, so a RENAME is visible here.
    CHECK(std::strcmp(mrfw::saved_key_use_name(mrfw::SavedKeyUse::installed), "installed") == 0);
    CHECK(std::strcmp(mrfw::saved_key_use_name(mrfw::SavedKeyUse::not_our_team), "not_our_team") == 0);
    CHECK(std::strcmp(mrfw::saved_key_use_name(mrfw::SavedKeyUse::record_unreadable), "record_unreadable") == 0);
}
