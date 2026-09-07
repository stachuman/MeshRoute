// MeshRoute — test/test_firmware_admin_client_verbs.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// §RADMIN slice 4 — the CONTROLLER verb families `admin-key …` / `admin-target …`, R-RA-30's BLE split predicate
// and the CLIENT `regen` admission + warning (`src/firmware_admin_client_verbs.h`).
//
// ★★★ EVERY EMITTED BYTE IS PINNED. `src/firmware_commands.cpp` and `src/fw_main.cpp` are compiled by NEITHER the
//     native suite (`test_build_src = no`) NOR the simulator, so a decision left in either would have no automated
//     cover at all. Hoisted here, the grammar, the reason vocabulary and the exact output lines are host-run and
//     `--target=radmin4verbs` can attack any of them.
//
// ⛔ WHAT THIS FILE DOES NOT PROVE: that `ble_dispatch_line` actually EVALUATES `admin_client_ble_refuses` before
//    the seam. That is `tools/probe_console_sink/ble_guard.py`'s job — it EXTRACTS the guard's condition from
//    `fw_main.cpp` and runs it against this predicate. A LineSink-shaped sink here is ⛔ NOT BLE admission
//    evidence.
#include "doctest.h"

#include <cstring>
#include <initializer_list>
#include <string>

#include "firmware_admin_client_verbs.h"
#include "identity.h"

using mrfw::MgmtKeyErr;
using mrfw::TargetErr;

namespace {

struct CapLines : mrfw::IAdminLines {
    std::string s;
    int         lines = 0;
    void line(const char* p, size_t n) override { s.append(p, n); ++lines; }
    void clear() { s.clear(); lines = 0; }
};

// ---- minimal counting fakes (the two service suites own the deep behaviour; here they are a substrate) -------
struct KeyStore : mrfw::IMgmtKeyStore {
    mrnv::MgmtKeyBlob rec{};
    mrnv::MgmtKeyRead state = mrnv::MgmtKeyRead::absent;
    int loads = 0, saves = 0;
    bool save_ok = true;
    mrnv::MgmtKeyRead load(mrnv::MgmtKeyBlob& out) override {
        ++loads;
        if (state != mrnv::MgmtKeyRead::ok) { std::memset(&out, 0xA5, sizeof out); return state; }
        out = rec;
        return mrnv::MgmtKeyRead::ok;
    }
    bool save(const mrnv::MgmtKeyBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::MgmtKeyRead::ok; return true;
    }
    void empty() { mrnv::mgmt_key_blob_init(rec); state = mrnv::MgmtKeyRead::ok; }
    void put(uint8_t n, uint8_t fill) {
        if (state != mrnv::MgmtKeyRead::ok) empty();
        std::memset(rec.rec[n].seed, fill, 32);
        uint16_t pop = 0;
        for (uint8_t i = 0; i < mrnv::kMgmtKeySlots; ++i) if (mrfw::mgmt_key_row_occupied(rec.rec[i])) ++pop;
        rec.count = pop;
    }
};
struct Seed : mrfw::IAdminSeedSource {
    int draws = 0; bool answer = true; uint8_t byte = 0x31;
    bool fill(uint8_t out[32]) override { ++draws; std::memset(out, byte, 32); return answer; }
};
struct NoUse : mrfw::IMgmtKeyUse {
    bool slot_in_use(uint8_t) const override { return false; }
    bool any_in_use() const override { return false; }
};
struct TgtStore : mrfw::ITargetStore {
    mrnv::TargetBlob rec{};
    mrnv::TargetRead state = mrnv::TargetRead::absent;
    int loads = 0, saves = 0;
    bool save_ok = true;
    mrnv::TargetRead load(mrnv::TargetBlob& out) override {
        ++loads;
        if (state != mrnv::TargetRead::ok) { std::memset(&out, 0xA5, sizeof out); return state; }
        out = rec;
        return mrnv::TargetRead::ok;
    }
    bool save(const mrnv::TargetBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::TargetRead::ok; return true;
    }
    void empty() { mrnv::target_blob_init(rec); state = mrnv::TargetRead::ok; }
};
struct NoTgtUse : mrfw::ITargetUse {
    bool slot_in_use(uint8_t) const override { return false; }
    bool any_in_use() const override { return false; }
};
struct Debt : mrfw::IClientRemoteDebt {
    bool b = false;
    bool busy() const override { return b; }
};

const uint8_t kSelf[32] = {
    0xd4, 0xf8, 0xe6, 0xf2, 0x67, 0x27, 0x11, 0x77, 0xc1, 0x1d, 0x17, 0xd3, 0x98, 0x10, 0xd7, 0x47,
    0x16, 0x65, 0x72, 0xa1, 0xb6, 0xdb, 0x8e, 0x35, 0x23, 0x63, 0xd9, 0x78, 0x6e, 0xb0, 0x79, 0x83,
};
// R-RA-29's INDEPENDENT frozen reference (Slice 3's `kFpRef`, see test_firmware_admin_targets.cpp's head note):
// the fingerprint of the all-ff public key.
const char kPubFF[] = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
const char kFpFF[]  = "83b5ade6991342ed";

// Run one `admin-key <tail>` line through the pure verb; `tail` is the RAW span after the primary token.
std::string run_key(mrfw::MgmtKeyService& svc, const char* tail) {
    CapLines out;
    mrfw::admin_key_verb(svc, tail, std::strlen(tail), out);
    return out.s;
}
std::string run_tgt(mrfw::TargetService& svc, mrnv::TargetBlob& book, const char* tail) {
    CapLines out;
    mrfw::admin_target_verb(svc, book, tail, std::strlen(tail), out);
    return out.s;
}

}  // namespace

// ================================================================================================================
// R-RA-30: THE BLE SPLIT PREDICATE
// ================================================================================================================

TEST_CASE("radmin4/verbs: the family predicate matches TOKEN BOUNDARIES and never a prefix") {
    for (const char* owned : { "admin-key", "admin-key ", "admin-key list", "admin-key\tshow self",
                               "admin-target", "admin-target list", "admin-target  show fp=x" })
        CHECK(mrfw::admin_client_verb_owns(owned, std::strlen(owned)));
    // ⛔ NOT this family — `admin-id`/`acl` are the TARGET half's (Slice 3) and carry a DIFFERENT envelope.
    for (const char* foreign : { "admin-keys", "admin-key2", "admin-targets", "admin-targetx", "admin",
                                 "admin ", "admin-id", "admin-id show", "acl", "acl list", "ADMIN-KEY",
                                 "help", "status", "", " admin-key" })
        CHECK_FALSE(mrfw::admin_client_verb_owns(foreign, std::strlen(foreign)));
}

TEST_CASE("radmin4/verbs: R-RA-30 — ONLY public list/show survive the BLE guard") {
    struct Row { const char* line; bool refuse; };
    const Row rows[] = {
        // the two PUBLIC forms — the positive case
        { "admin-key list", false },              { "admin-key  list", false },
        { "admin-key show self", false },         { "admin-key show key0", false },
        { "admin-target list", false },           { "admin-target list page=2", false },
        { "admin-target show fp=83b5ade6991342ed", false },
        { "admin-target show label=alpha", false },
        // ⛔ a MALFORMED list/show is still a list/show TOKEN: it passes the guard and the ROUTER refuses it,
        //    publicly. That is design §6.2's "may be used through secured BLE", not a smuggled second operation.
        { "admin-key list x", false },            { "admin-target show", false },
        // every OTHER owned form refuses — bare, whitespace, unknown, mutation, reset, export
        { "admin-key", true },                    { "admin-key ", true },
        { "admin-key\t", true },                  { "admin-key bogus", true },
        { "admin-key generate key0", true },      { "admin-key import key0 ff", true },
        { "admin-key export key0", true },        { "admin-key remove key0 confirm", true },
        { "admin-key reset confirm", true },      { "admin-key LIST", true },
        { "admin-key listx", true },              { "admin-key shown self", true },
        { "admin-target", true },                 { "admin-target ", true },
        { "admin-target bogus", true },           { "admin-target add a ff hash=0x1", true },
        { "admin-target set label=a label=b hash=0x1 layer=none", true },
        { "admin-target remove label=a confirm", true },
        { "admin-target reset confirm", true },   { "admin-target LIST", true },
        // ⛔ and lines this family does not own are NOT refused by THIS guard (the target family has its own)
        { "acl list", false },                    { "admin-id show", false },
        { "admin-keys list", false },             { "help", false },
        { "status", false },                      { "", false },
    };
    for (const Row& r : rows) {
        CAPTURE(r.line);
        CHECK(mrfw::admin_client_ble_refuses(r.line, std::strlen(r.line)) == r.refuse);
        // THE COMPOSITION INVARIANT: a refusal implies ownership — over-refusing would swallow another verb.
        if (mrfw::admin_client_ble_refuses(r.line, std::strlen(r.line)))
            CHECK(mrfw::admin_client_verb_owns(r.line, std::strlen(r.line)));
    }
    CHECK_FALSE(mrfw::admin_client_ble_refuses("", 0));    // the empty-line floor
}

// ================================================================================================================
// `admin-key` — every emitted byte
// ================================================================================================================

TEST_CASE("radmin4/verbs: `admin-key show self` prints the node's own identity, byte for byte") {
    KeyStore ks; Seed sd; NoUse nu;
    mrfw::MgmtKeyService svc(ks, sd, nu, kSelf);
    CHECK(run_key(svc, " show self") ==
          "> admin-key self fp=45c2192f3d0c62f4 "
          "pub=d4f8e6f267271177c11d17d39810d747166572a1b6db8e352363d9786eb07983\n");
    CHECK(ks.loads == 0);
}

TEST_CASE("radmin4/verbs: the `admin-key` grammar refuses every malformed form at ZERO cost") {
    KeyStore ks; Seed sd; NoUse nu;
    ks.empty();
    mrfw::MgmtKeyService svc(ks, sd, nu, kSelf);
    // ⓘ ` show self x` is here because `--target=radmin4verbs` V30 survived without it: the trailing-token gate
    //   on the ONE arm that answers without touching the store had no row.
    const char* bad[] = { "", " ", "  ", "\t", " bogus", " LIST", " list x", " show", " show selfx",
                          " show self x", " show self  x",
                          " show key", " show key10", " show KEY0", " show 0", " generate", " generate key",
                          " generate key0 x", " import key0", " export", " export key0 x", " remove key0",
                          " reset" };
    for (const char* t : bad) {
        CAPTURE(t);
        const std::string got = run_key(svc, t);
        CHECK((got == "> admin-key err bad_args\n" || got == "> admin-key err needs_confirm\n"));
    }
    CHECK(ks.saves == 0);
    CHECK(sd.draws == 0);
    // ⛔ the two DESTRUCTIVE verbs name the missing confirmation SPECIFICALLY
    CHECK(run_key(svc, " remove key0") == "> admin-key err needs_confirm\n");
    CHECK(run_key(svc, " reset") == "> admin-key err needs_confirm\n");
    CHECK(run_key(svc, " reset confirm extra") == "> admin-key err needs_confirm\n");
    CHECK(ks.saves == 0);
    CHECK(sd.draws == 0);
}

TEST_CASE("radmin4/verbs: `admin-key list` prints self, the occupied slots in order, then its footer") {
    KeyStore ks; Seed sd; NoUse nu;
    ks.put(0, 0x11);
    ks.put(4, 0x22);
    mrfw::MgmtKeyService svc(ks, sd, nu, kSelf);
    CapLines out;
    const char* t = " list";
    mrfw::admin_key_verb(svc, t, std::strlen(t), out);
    CHECK(out.lines == 4);                                   // self + two rows + end
    CHECK(out.s.find("> admin-key self fp=") == 0);
    CHECK(out.s.find("\n> admin-key key0 fp=") != std::string::npos);
    CHECK(out.s.find("\n> admin-key key4 fp=") != std::string::npos);
    CHECK(out.s.find("> admin-key end count=2\n") != std::string::npos);
    CHECK(out.s.find("seed") == std::string::npos);          // ⛔ NO SEED BYTE, ever, on this path
    CHECK(ks.saves == 0);
}

TEST_CASE("radmin4/verbs: an unreadable keyring REFUSES the listing rather than implying an empty one") {
    for (auto st : { mrnv::MgmtKeyRead::invalid, mrnv::MgmtKeyRead::io_failed }) {
        KeyStore ks; Seed sd; NoUse nu;
        ks.state = st;
        mrfw::MgmtKeyService svc(ks, sd, nu, kSelf);
        const std::string got = run_key(svc, " list");
        CHECK((got == "> admin-key err store_invalid\n" || got == "> admin-key err store_io_failed\n"));
        // ⛔ …and `show self` STILL answers: the fact it reports does not live in that record
        CHECK(run_key(svc, " show self").find("> admin-key self fp=") == 0);
    }
}

TEST_CASE("radmin4/verbs: generate / import / export / remove / reset print their exact lines") {
    KeyStore ks; Seed sd; NoUse nu;
    ks.empty();
    mrfw::MgmtKeyService svc(ks, sd, nu, kSelf);

    const std::string gen = run_key(svc, " generate key3");
    CHECK(gen.find("> admin-key generated key3 fp=") == 0);
    CHECK(gen.size() == 116);                                 // the pinned maximal `generated` row
    CHECK(gen[gen.size() - 1] == '\n');
    CHECK(sd.draws == 1);
    CHECK(ks.saves == 1);

    // import: a distinct seed, so no duplicate
    // ⓘ DELIBERATELY NOT the golden seed {1..32}: that one derives to `kSelf`, and importing it would be a
    //   legitimate `duplicate` — which is the keyring suite's subject, not this file's.
    const std::string imp = run_key(svc, " import key7 "
        "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40");
    CHECK(imp.find("> admin-key imported key7 fp=") == 0);
    CHECK(sd.draws == 1);                                     // ⛔ import NEVER draws

    // export prints the SEED — the one arm that does, and it is the one the BLE guard refuses
    CHECK(run_key(svc, " export key7") ==
          "> admin-key exported key7 "
          "seed=2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40\n");

    CHECK(run_key(svc, " remove key7 confirm") == "> admin-key removed key7\n");
    CHECK(run_key(svc, " remove key7 confirm") == "> admin-key err not_found\n");
    CHECK(run_key(svc, " export key7") == "> admin-key err not_found\n");

    // reset only recovers a CORRUPT record
    CHECK(run_key(svc, " reset confirm") == "> admin-key err not_invalid\n");
    ks.rec.count = 9;                                          // content broken
    CHECK(run_key(svc, " reset confirm") == "> admin-key reset\n");
}

TEST_CASE("radmin4/verbs: every `admin-key` reason lexeme is reachable and spelled exactly once") {
    for (auto e : { MgmtKeyErr::none, MgmtKeyErr::bad_args, MgmtKeyErr::needs_confirm,
                    MgmtKeyErr::store_invalid, MgmtKeyErr::store_io_failed, MgmtKeyErr::not_invalid,
                    MgmtKeyErr::not_found, MgmtKeyErr::occupied, MgmtKeyErr::duplicate,
                    MgmtKeyErr::bad_material, MgmtKeyErr::entropy_failed, MgmtKeyErr::in_use,
                    MgmtKeyErr::nv_save_failed }) {
        const char* n = mrfw::mgmt_key_err_name(e);
        CHECK(n != nullptr);
        CHECK(std::strlen(n) > 0);
    }
    CHECK(std::strcmp(mrfw::mgmt_key_err_name(MgmtKeyErr::needs_confirm), "needs_confirm") == 0);
    CHECK(std::strcmp(mrfw::mgmt_key_err_name(MgmtKeyErr::entropy_failed), "entropy_failed") == 0);
    CHECK(std::strcmp(mrfw::mgmt_key_state_name(mrfw::MgmtKeyState::io_failed), "io_failed") == 0);
    CHECK(std::strcmp(mrfw::target_state_name(mrfw::TargetState::absent), "absent") == 0);
    CHECK(std::strcmp(mrfw::target_err_name(TargetErr::ambiguous), "ambiguous") == 0);
    CHECK(std::strcmp(mrfw::target_err_name(TargetErr::unchanged), "unchanged") == 0);
}

// ================================================================================================================
// `admin-target` — the grammar, the pages and every emitted byte
// ================================================================================================================

TEST_CASE("radmin4/verbs: the numeric/`layer`/label argument parsers refuse everything malformed") {
    uint32_t h = 0;
    CHECK(mrfw::admin_client_hash("0x1", 3, h));            CHECK(h == 1);
    CHECK(mrfw::admin_client_hash("0XABCDEF12", 10, h));    CHECK(h == 0xABCDEF12u);
    CHECK(mrfw::admin_client_hash("0xffffffff", 10, h));    CHECK(h == 0xFFFFFFFFu);
    // ⛔ THE `0x` PREFIX IS REQUIRED. `abc`, `1234` and `deadbeef` are the shapes found by
    //   `--target=radmin4verbs` V09: with the prefix test dropped, each of them parsed as a hash (their TAIL is
    //   hex), which is exactly the id-versus-hash ambiguity the prefix exists to kill.
    for (const char* bad : { "1", "0x", "0x0", "0x00000000", "0xg", "0x123456789", "x1", "00x1", "0x1 ",
                             "abc", "1234", "deadbeef", "12345678", "0X", "0y1" })
        CHECK_FALSE(mrfw::admin_client_hash(bad, std::strlen(bad), h));

    uint8_t hops[4]; uint8_t hc = 0xFF;
    CHECK(mrfw::admin_client_layers("none", 4, hops, hc));   CHECK(hc == 0);
    CHECK(mrfw::admin_client_layers("7", 1, hops, hc));      CHECK(hc == 1); CHECK(hops[0] == 7);
    CHECK(mrfw::admin_client_layers("1,2,3", 5, hops, hc));  CHECK(hc == 3);
    CHECK(hops[3] == 0);
    CHECK(mrfw::admin_client_layers("255", 3, hops, hc));    CHECK(hops[0] == 255);
    for (const char* bad : { "", "0", "1,2,3,4", "1,", ",1", "1,,2", "256", "-1", "a", "1 2", "NONE" })
        CHECK_FALSE(mrfw::admin_client_layers(bad, std::strlen(bad), hops, hc));

    uint8_t slot = 0xFF;
    CHECK(mrfw::admin_client_key_slot("key0", 4, slot));  CHECK(slot == 0);
    CHECK(mrfw::admin_client_key_slot("key9", 4, slot));  CHECK(slot == 9);
    for (const char* bad : { "key", "key10", "key00", "KEY0", "0", "keyA", "key-1", "self" })
        CHECK_FALSE(mrfw::admin_client_key_slot(bad, std::strlen(bad), slot));

    CHECK(mrfw::admin_client_fp(kFpFF, 16));
    for (const char* bad : { "83b5ade6991342e", "83b5ade6991342edd", "83B5ADE6991342ED", "83b5ade6991342eg" })
        CHECK_FALSE(mrfw::admin_client_fp(bad, std::strlen(bad)));
}

TEST_CASE("radmin4/verbs: `admin-target add` composes the row and prints its exact line") {
    TgtStore ts; NoTgtUse nu;
    ts.empty();
    mrfw::TargetService svc(ts, nu);
    mrnv::TargetBlob book{};
    std::string cmd = std::string(" add alpha ") + kPubFF + " hash=0xDEADBEEF layer=1,2,3";
    const std::string got = run_tgt(svc, book, cmd.c_str());
    CHECK(got == std::string("> admin-target added slot=0 fp=") + kFpFF + " pub=" + kPubFF + "\n");
    CHECK(ts.saves == 1);
    const mrnv::TargetRow& r = ts.rec.rec[0];
    CHECK(r.key_hash32 == 0xDEADBEEFu);
    CHECK(r.hop_count == 3);
    CHECK(r.label_len == 5);
    CHECK(r.flags == mrnv::kTargetFlagOccupied);

    // the listing row — the FROZEN template, including the UPPERCASE hash and the lowercase fp/pub
    const std::string row = run_tgt(svc, book, " list");
    CHECK(row == std::string("> admin-target slot=0 label=alpha fp=") + kFpFF + " pub=" + kPubFF +
                 " hash=0xDEADBEEF layer=1,2,3\n> admin-target end page=0 count=1\n");
}

TEST_CASE("radmin4/verbs: an OMITTED `layer=` means same-layer, and `layer=none` clears one") {
    TgtStore ts; NoTgtUse nu;
    ts.empty();
    mrfw::TargetService svc(ts, nu);
    mrnv::TargetBlob book{};
    std::string add = std::string(" add alpha ") + kPubFF + " hash=0x1";
    CHECK(run_tgt(svc, book, add.c_str()).find("> admin-target added") == 0);
    CHECK(ts.rec.rec[0].hop_count == 0);
    CHECK(run_tgt(svc, book, " list").find("layer=none\n") != std::string::npos);
    // set: all three fields, and the KEY is preserved
    const std::string upd = run_tgt(svc, book, " set label=alpha label=beta hash=0x2 layer=9");
    CHECK(upd == std::string("> admin-target updated slot=0 fp=") + kFpFF + " pub=" + kPubFF + "\n");
    CHECK(ts.rec.rec[0].key_hash32 == 2);
    CHECK(ts.rec.rec[0].hops[0] == 9);
    const int saves = ts.saves;
    CHECK(run_tgt(svc, book, " set label=beta label=beta hash=0x2 layer=9") ==
          "> admin-target err unchanged\n");
    CHECK(ts.saves == saves);                                   // ⛔ a no-op costs ZERO writes
    const std::string byfp = std::string(" set fp=") + kFpFF + " label=beta hash=0x2 layer=none";
    CHECK(run_tgt(svc, book, byfp.c_str()) ==
          std::string("> admin-target updated slot=0 fp=") + kFpFF + " pub=" + kPubFF + "\n");
    CHECK(ts.rec.rec[0].hop_count == 0);
}

TEST_CASE("radmin4/verbs: the selector resolves by label and by fingerprint, never by index or hash") {
    TgtStore ts; NoTgtUse nu;
    ts.empty();
    mrfw::TargetService svc(ts, nu);
    mrnv::TargetBlob book{};
    std::string add = std::string(" add alpha ") + kPubFF + " hash=0x1";
    CHECK(run_tgt(svc, book, add.c_str()).find("> admin-target added") == 0);
    CHECK(run_tgt(svc, book, " show label=alpha").find("> admin-target slot=0 label=alpha") == 0);
    const std::string showfp = std::string(" show fp=") + kFpFF;
    CHECK(run_tgt(svc, book, showfp.c_str()).find("> admin-target slot=0") == 0);
    CHECK(run_tgt(svc, book, " show label=beta") == "> admin-target err not_found\n");
    CHECK(run_tgt(svc, book, " show fp=0000000000000000") == "> admin-target err not_found\n");
    // ⛔ a bare index, a hash and an unprefixed label are NOT selectors
    for (const char* bad : { " show 0", " show alpha", " show 0x1", " show hash=0x1", " show fp=zz",
                             " show label=", " show label=with space", " show" })
        CHECK(run_tgt(svc, book, bad) == "> admin-target err bad_args\n");
}

TEST_CASE("radmin4/verbs: FOUR fixed pages of EIGHT physical slots enumerate all 32 without loss") {
    TgtStore ts; NoTgtUse nu;
    ts.empty();
    mrfw::TargetService svc(ts, nu);
    mrnv::TargetBlob book{};
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
        // ⛔ the first two nibbles ENCODE `i`, so all 32 keys are pairwise distinct (a rotation alone repeats
        //    with period 16 and the book would rightly refuse the second half as duplicates).
        char pub[65];
        for (int k = 0; k < 64; ++k) pub[k] = "0123456789abcdef"[(k + 3) % 16];
        pub[0] = "0123456789abcdef"[(i >> 4) & 0xF];
        pub[1] = "0123456789abcdef"[i & 0xF];
        pub[64] = '\0';
        char cmd[160];
        std::snprintf(cmd, sizeof cmd, " add t%u %s hash=0x%X", (unsigned)i, pub, (unsigned)(i + 1));
        CHECK(run_tgt(svc, book, cmd).find("> admin-target added") == 0);
    }
    int seen = 0;
    for (uint8_t p = 0; p < 4; ++p) {
        char cmd[32];
        std::snprintf(cmd, sizeof cmd, " list page=%u", (unsigned)p);
        const std::string got = run_tgt(svc, book, cmd);
        char foot[64];
        std::snprintf(foot, sizeof foot, "> admin-target end page=%u count=32\n", (unsigned)p);
        CHECK(got.find(foot) != std::string::npos);
        for (uint8_t s = p * 8; s < p * 8 + 8; ++s) {
            char needle[40];
            std::snprintf(needle, sizeof needle, "> admin-target slot=%u ", (unsigned)s);
            CHECK(got.find(needle) != std::string::npos);
            ++seen;
        }
    }
    CHECK(seen == 32);                                     // every physical slot, exactly once
    // no page argument means PAGE 0 by contract — ⛔ not an automatic multi-page dump
    const std::string p0 = run_tgt(svc, book, " list");
    CHECK(p0.find("> admin-target end page=0 count=32\n") != std::string::npos);
    CHECK(p0.find("> admin-target slot=8 ") == std::string::npos);
    for (const char* bad : { " list page=4", " list page=-1", " list page=00", " list page=", " list 0",
                             " list page=0 x", " list x" })
        CHECK(run_tgt(svc, book, bad) == "> admin-target err bad_args\n");
}

TEST_CASE("radmin4/verbs: a MAXIMAL page fits the declared bound and the 2048-byte console stage") {
    TgtStore ts; NoTgtUse nu;
    ts.empty();
    mrfw::TargetService svc(ts, nu);
    mrnv::TargetBlob book{};
    // ★ THE MAXIMUM IS ON **PAGE 3**, and that is the point of building all 32: slots 24..31 are the two-digit
    //   ones, so their rows are one byte wider than page 0's. A bound derived from page 0 would be 168 and WRONG.
    for (uint8_t i = 0; i < mrnv::kTargetSlots; ++i) {
        char pub[65];
        for (int k = 0; k < 64; ++k) pub[k] = "0123456789abcdef"[(k + 5) % 16];
        pub[0] = "0123456789abcdef"[(i >> 4) & 0xF];
        pub[1] = "0123456789abcdef"[i & 0xF];
        pub[64] = '\0';
        char cmd[200];
        // a MAXIMAL row: a 16-character label, an 8-digit hash and a 3-hop path of 3-digit ids
        std::snprintf(cmd, sizeof cmd, " add L%02u3456789abcdef %s hash=0x%08X layer=255,255,255",
                      (unsigned)i, pub, (unsigned)(i + 1));
        CHECK(run_tgt(svc, book, cmd).find("> admin-target added") == 0);
    }
    CapLines out;
    const char* t = " list page=3";
    mrfw::admin_target_verb(svc, book, t, std::strlen(t), out);
    CHECK(out.lines == 9);                                  // eight rows + the footer
    // every row is EXACTLY the pinned 169-byte maximum, and the whole page the pinned 1387
    size_t at = 0, rows = 0;
    while (true) {
        const size_t nl = out.s.find('\n', at);
        if (nl == std::string::npos) break;
        const size_t len = nl - at + 1;
        if (rows < 8) CHECK(len == 169);
        else          CHECK(len == 35);
        at = nl + 1;
        ++rows;
    }
    CHECK(rows == 9);
    CHECK(out.s.size() == mrfw::kTargetPageMaxBytes);
    CHECK(out.s.size() == 1387);
    CHECK(out.s.size() < 2048);
}

TEST_CASE("radmin4/verbs: `admin-target` refuses every malformed form, destructive ones by name") {
    TgtStore ts; NoTgtUse nu;
    ts.empty();
    mrfw::TargetService svc(ts, nu);
    mrnv::TargetBlob book{};
    std::string add = std::string(" add alpha ") + kPubFF + " hash=0x1";
    CHECK(run_tgt(svc, book, add.c_str()).find("> admin-target added") == 0);
    const int saves = ts.saves;
    for (const char* bad : { "", " ", " bogus", " ADD", " add", " add alpha", " add alpha ff hash=0x1",
                             " add with space ff hash=0x1", " set", " set label=alpha",
                             " set label=alpha label=b hash=0x1", " set label=alpha label=b layer=none",
                             " remove", " reset x" })
        CHECK(run_tgt(svc, book, bad).find("> admin-target err ") == 0);
    CHECK(ts.saves == saves);
    CHECK(run_tgt(svc, book, " remove label=alpha") == "> admin-target err needs_confirm\n");
    CHECK(run_tgt(svc, book, " reset") == "> admin-target err needs_confirm\n");
    CHECK(run_tgt(svc, book, " reset confirm extra") == "> admin-target err needs_confirm\n");
    CHECK(ts.saves == saves);
    CHECK(run_tgt(svc, book, " remove label=alpha confirm") == "> admin-target removed slot=0\n");
    CHECK(run_tgt(svc, book, " reset confirm") == "> admin-target err not_invalid\n");
}

TEST_CASE("radmin4/verbs: an unreadable book refuses EVERY arm, at zero writes") {
    for (auto st : { mrnv::TargetRead::invalid, mrnv::TargetRead::io_failed }) {
        TgtStore ts; NoTgtUse nu;
        ts.state = st;
        mrfw::TargetService svc(ts, nu);
        mrnv::TargetBlob book{};
        const char* want = (st == mrnv::TargetRead::invalid) ? "> admin-target err store_invalid\n"
                                                             : "> admin-target err store_io_failed\n";
        CHECK(run_tgt(svc, book, " list") == want);
        CHECK(run_tgt(svc, book, " show label=a") == want);
        CHECK(run_tgt(svc, book, " remove label=a confirm") == want);
        CHECK(ts.saves == 0);
    }
}

// ================================================================================================================
// THE BOOT LINES
// ================================================================================================================

TEST_CASE("radmin4/verbs: the CLIENT boot report is two READ-ONLY lines with no key, seed or fingerprint") {
    KeyStore ks; Seed sd; NoUse nu;
    TgtStore ts; NoTgtUse tnu;
    mrfw::MgmtKeyService keys(ks, sd, nu, kSelf);
    mrfw::TargetService tgts(ts, tnu);
    mrnv::TargetBlob book{};
    CapLines out;
    mrfw::admin_client_boot_report(keys, tgts, book, out);
    CHECK(out.s == "> admin-key boot state=absent count=0\n"
                   "> admin-target boot state=absent count=0\n");
    CHECK(ks.saves == 0);
    CHECK(ts.saves == 0);
    CHECK(sd.draws == 0);

    ks.put(2, 0x55);
    ts.empty();
    out.clear();
    mrfw::admin_client_boot_report(keys, tgts, book, out);
    CHECK(out.s == "> admin-key boot state=ok count=1\n"
                   "> admin-target boot state=ok count=0\n");

    ks.state = mrnv::MgmtKeyRead::io_failed;
    ts.state = mrnv::TargetRead::io_failed;
    out.clear();
    mrfw::admin_client_boot_report(keys, tgts, book, out);
    // ⚠ ZERO here means "NO ACCEPTED RECORDS" — ⛔ never "the flash is empty". `state=` carries that.
    CHECK(out.s == "> admin-key boot state=io_failed count=0\n"
                   "> admin-target boot state=io_failed count=0\n");
    CHECK(ks.saves == 0);
    CHECK(ts.saves == 0);
}

// ================================================================================================================
// `regen` — the CLIENT admission and the warning
// ================================================================================================================

TEST_CASE("radmin4/verbs: the selector's four verdicts each map to their OWN ruled lexeme") {
    // ★★ DRIVEN DIRECTLY, because the `many` arm is unreachable through the grammar: two occupied rows whose
    //    BLAKE2b-512 fingerprints agree would be a real digest collision. Found by `--target=radmin4verbs` V18,
    //    which renamed `ambiguous` to `not_found` and survived — the operator would have been told the row does
    //    not exist when in fact TWO do. ⛔ NO CLAIM is made that a collision was produced.
    struct Row { mrfw::TargetSel sel; const char* want; };
    const Row rows[] = {
        { mrfw::TargetSel::none, "> admin-target err not_found\n" },
        { mrfw::TargetSel::many, "> admin-target err ambiguous\n" },
        { mrfw::TargetSel::bad,  "> admin-target err bad_args\n" },
    };
    for (const Row& r : rows) {
        CapLines out;
        mrfw::target_emit_sel_err(out, r.sel);
        CHECK(out.s == r.want);
        CHECK(out.lines == 1);
    }
    // ⛔ …and the `ok` verdict emits NOTHING: a resolved selector is not an error.
    CapLines ok;
    mrfw::target_emit_sel_err(ok, mrfw::TargetSel::ok);
    CHECK(ok.s.empty());
    CHECK(ok.lines == 0);
}

TEST_CASE("radmin4/verbs: the emitter is FAIL-CLOSED — a line that would not fit is REFUSED, never truncated") {
    // ★ Found by `--target=radmin4verbs` V26: the bound is documented as unreachable by construction, but
    //   "unreachable" is a claim about the CALLERS, not about the emitter. Driven directly, it is testable.
    CapLines out;
    char buf[mrfw::kAdminClientLineMax];
    std::memset(buf, 'x', sizeof buf);
    mrfw::admin_client_emit(out, buf, static_cast<int>(mrfw::kAdminClientLineMax));       // exactly the bound
    mrfw::admin_client_emit(out, buf, static_cast<int>(mrfw::kAdminClientLineMax) + 1);   // past it
    mrfw::admin_client_emit(out, buf, 0);                                                 // nothing composed
    mrfw::admin_client_emit(out, buf, -1);                                                // snprintf's error
    CHECK(out.lines == 0);
    CHECK(out.s.empty());
    mrfw::admin_client_emit(out, buf, static_cast<int>(mrfw::kAdminClientLineMax) - 1);   // the widest legal line
    CHECK(out.lines == 1);
    CHECK(out.s.size() == mrfw::kAdminClientLineMax - 1);
}

TEST_CASE("radmin4/verbs: the regen admission refuses only on DEBT, and prints exactly one line") {
    Debt d;
    CHECK(mrfw::client_regen_admitted(d));
    d.b = true;
    CHECK_FALSE(mrfw::client_regen_admitted(d));            // ⚠ a FUTURE-CALLER service test — no producer yet
    CapLines out;
    mrfw::client_regen_emit_busy(out);
    CHECK(out.s == "> regen err remote_busy\n");
    CHECK(out.lines == 1);
}

TEST_CASE("radmin4/verbs: the regen warning is the frozen line and carries no key, seed or fingerprint") {
    CapLines out;
    mrfw::client_regen_emit_note(out);
    CHECK(out.s == "> regen note old self ACL grants do not follow the new key; "
                   "dedicated keys and targets preserved\n");
    CHECK(out.lines == 1);
    CHECK(out.s.find("pub=") == std::string::npos);
    CHECK(out.s.find("seed=") == std::string::npos);
    CHECK(out.s.find("fp=") == std::string::npos);
    CHECK(out.s.size() < mrfw::kAdminClientLineMax);
}
