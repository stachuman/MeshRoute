// MeshRoute — test/test_firmware_admin_verbs.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// §RADMIN slice 3 — the `admin-id …` / `acl …` USB verb family (`src/firmware_admin_verbs.h`).
//
// ★★★ IT PINS EVERY EMITTED BYTE. `test_build_src = no` keeps `firmware_commands.cpp` out of every host build, so
//     a response composed inside a handler would be a byte no automated gate could fail. Composed here, the whole
//     line is comparable — and `--target=radmin3verbs` can attack every token boundary and every lexeme.
//
// ★★★ THE SPANS ARE DELIBERATELY NOT NUL-TERMINATED. `dispatch()` hands a (pointer, length) pair out of a byte
//     stage, so every case below runs the verb over a buffer whose bytes AFTER `len` are poison (0x7F filler that
//     would parse as junk). A parser that reached past `len` reads that poison and fails visibly.
//
// ⛔ NOTHING HERE IS A TRANSPORT TEST. The BLE refusal is `ble_dispatch_line`'s and is executed by
//    `tools/probe_console_sink/ble_guard.py` against the REAL extracted guard; what this file owns is
//    `admin_verb_owns()` — the predicate that guard's rows are measured against.
#include "doctest.h"

#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "firmware_admin_verbs.h"

using mrfw::AclRole;
using mrfw::AclState;
using mrfw::AdminIdState;

namespace {

// ---- the capture sink ------------------------------------------------------------------------------------
// ★ It records lines SEPARATELY as well as concatenated, so "one LF-terminated line per record" is measurable
//   rather than assumed — a formatter that emitted two records in one `line()` call would pass a naive compare.
struct CaptureLines : mrfw::IAdminLines {
    std::vector<std::string> lines;
    std::string all;
    void line(const char* s, size_t n) override {
        lines.emplace_back(s, n);
        all.append(s, n);
    }
};

struct FakeAdminStore : mrfw::IAdminIdStore {
    mrnv::AdminIdBlob rec{};
    mrnv::AdminIdRead state = mrnv::AdminIdRead::absent;
    int loads = 0, saves = 0;
    bool save_ok = true;
    mrnv::AdminIdRead load(mrnv::AdminIdBlob& out) override {
        ++loads;
        if (state != mrnv::AdminIdRead::ok) { std::memset(&out, 0xA5, sizeof out); return state; }
        out = rec;
        return mrnv::AdminIdRead::ok;
    }
    bool save(const mrnv::AdminIdBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::AdminIdRead::ok; return true;
    }
    void seed_valid(uint8_t f) { mrnv::admin_id_blob_init(rec); std::memset(rec.seed, f, 32); state = mrnv::AdminIdRead::ok; }
};
struct FakeSeed : mrfw::IAdminSeedSource {
    int draws = 0; bool answer = true; uint8_t byte = 0x24;
    bool fill(uint8_t out[32]) override { ++draws; std::memset(out, byte, 32); return answer; }
};
struct FakeAclStore : mrfw::IAclStore {
    mrnv::AclBlob rec{};
    mrnv::AclRead state = mrnv::AclRead::absent;
    int loads = 0, saves = 0;
    bool save_ok = true;
    mrnv::AclRead load(mrnv::AclBlob& out) override {
        ++loads;
        if (state != mrnv::AclRead::ok) { std::memset(&out, 0xA5, sizeof out); return state; }
        out = rec;
        return mrnv::AclRead::ok;
    }
    bool save(const mrnv::AclBlob& b) override {
        ++saves;
        if (!save_ok) return false;
        rec = b; state = mrnv::AclRead::ok; return true;
    }
    void empty_valid() { mrnv::acl_blob_init(rec); state = mrnv::AclRead::ok; }
};

// ★★ THE POISONED SPAN. `s` is copied into the MIDDLE of a buffer whose tail is 0x7F, and the verb is handed a
//    length that stops before the poison. Anything that runs off the end sees it.
struct Span {
    std::vector<char> buf;
    const char* p = nullptr;
    size_t n = 0;
    explicit Span(const std::string& s) : buf(s.size() + 64, '\x7f') {
        std::memcpy(buf.data(), s.data(), s.size());
        p = buf.data();
        n = s.size();
    }
};

// Run `admin-id <tail>` / `acl <tail>` over a poisoned span and return the whole emitted text.
std::string run_id(mrfw::AdminIdService& svc, const std::string& tail) {
    Span sp(tail);
    CaptureLines out;
    mrfw::admin_id_verb(svc, sp.p, sp.n, out);
    return out.all;
}
std::string run_acl(mrfw::AclService& acl, mrfw::AdminIdService& id, const std::string& tail) {
    Span sp(tail);
    CaptureLines out;
    mrfw::acl_verb(acl, id, sp.p, sp.n, out);
    return out.all;
}

const char* kKeyA = "1111111111111111111111111111111111111111111111111111111111111111";
const char* kKeyB = "2222222222222222222222222222222222222222222222222222222222222222";
// The independent reference's `seq` vector, so the emitted `fp=` is checkable against a frozen literal.
const char* kKeySeq = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
const char* kFpSeq  = "5c52920a7263e39d";

std::string fp_of_hexkey(const char* hex) {
    uint8_t k[32];
    for (int i = 0; i < 32; ++i) {
        auto nib = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            return static_cast<uint8_t>(c - 'A' + 10);
        };
        k[i] = static_cast<uint8_t>((nib(hex[2 * i]) << 4) | nib(hex[2 * i + 1]));
    }
    char fp[mrfw::kAdminFpHex + 1];
    mrfw::admin_fp_hex(k, fp);
    return std::string(fp);
}

}  // namespace

// ================================================================================================================
// OWNERSHIP: the two primary tokens, with EXACT boundaries
// ================================================================================================================

TEST_CASE("radmin3/verbs: the family owns EXACTLY `acl` and `admin-id`, with a space/tab/end boundary") {
    struct Row { const char* line; bool owned; };
    for (Row r : {
            // ★ the owned forms, INCLUDING malformed subforms — a malformed line of an owned family is still ours
            Row{ "acl", true }, Row{ "acl ", true }, Row{ "acl\t", true }, Row{ "acl list", true },
            Row{ "acl  list", true }, Row{ "acl bogus", true }, Row{ "acl add", true },
            Row{ "acl remove 0", true }, Row{ "acl reset confirm", true },
            Row{ "admin-id", true }, Row{ "admin-id ", true }, Row{ "admin-id\tshow", true },
            Row{ "admin-id show", true }, Row{ "admin-id bogus x y", true }, Row{ "admin-id rotate confirm", true },
            // ⛔ the near misses — every one must fall through to the router's ordinary unknown-verb answer
            Row{ "acls", false }, Row{ "aclx", false }, Row{ "acl_list", false }, Row{ "ac", false },
            Row{ "a", false }, Row{ "ACL", false }, Row{ "ACL list", false },
            Row{ "admin-identity", false }, Row{ "admin-identity show", false },
            Row{ "admin-key", false }, Row{ "admin-key show self", false },   // ★ Slice 4's CONTROLLER verb
            Row{ "admin", false }, Row{ "admin ", false }, Row{ "admin-i", false },
            Row{ "admin-idx", false }, Row{ "ADMIN-ID", false }, Row{ "", false },
            Row{ " acl", false },        // ⛔ the router matches at offset 0; a leading space is a different line
            Row{ "xacl", false }, Row{ "help", false }, Row{ "status", false }, Row{ "ui preset list", false } }) {
        CAPTURE(r.line);
        const size_t n = std::strlen(r.line);
        CHECK(mrfw::admin_verb_owns(r.line, n) == r.owned);
    }
    // …and the two halves are disjoint and complete — measured on the SAME predicate the router's arm evaluates.
    CHECK(mrfw::admin_primary_is("acl list", 8, "acl"));
    CHECK_FALSE(mrfw::admin_primary_is("acl list", 8, "admin-id"));
    CHECK(mrfw::admin_primary_is("admin-id show", 13, "admin-id"));
    CHECK_FALSE(mrfw::admin_primary_is("admin-id show", 13, "acl"));
}

TEST_CASE("radmin3/verbs: ownership respects the LENGTH, never a NUL — a truncated span is a different line") {
    const char* line = "admin-idXshow";
    CHECK_FALSE(mrfw::admin_verb_owns(line, 13));   // `admin-idX…` is not the token
    CHECK(mrfw::admin_verb_owns(line, 8));          // …but the first 8 bytes ARE exactly `admin-id`
    const char* acl = "aclx";
    CHECK_FALSE(mrfw::admin_verb_owns(acl, 4));
    CHECK(mrfw::admin_verb_owns(acl, 3));
}

// ================================================================================================================
// `admin-id` — grammar and exact bytes
// ================================================================================================================

TEST_CASE("radmin3/verbs: admin-id show/generate emit the EXACT contract lines") {
    FakeAdminStore st; FakeSeed sd;
    st.state = mrnv::AdminIdRead::absent;
    mrfw::AdminIdService svc(st, sd);

    const std::string gen = run_id(svc, " generate");
    // ⓘ THE GUARD IS EXPLICIT, ⛔ not a `REQUIRE`: this project compiles with EXCEPTIONS DISABLED
    //   (`-fno-exceptions`), so doctest's aborting `REQUIRE` family is unavailable here — every existing suite in
    //   `test/` uses `CHECK` for the same reason. A failed shape must therefore not be followed by an index.
    CHECK(gen.rfind("> admin-id generated fp=", 0) == 0);
    CHECK(gen.size() == 24 + mrfw::kAdminFpHex + 5 + mrfw::kAdminKeyHex + 1);
    std::string pub, fp;
    if (gen.size() == 24 + mrfw::kAdminFpHex + 5 + mrfw::kAdminKeyHex + 1) {
        CHECK(gen.back() == '\n');
        // ★ THE FINGERPRINT IS THE ONE THE PRINTED KEY HASHES TO — parsed back out of the line, so a formatter
        //   that printed a fingerprint of something else is caught.
        pub = gen.substr(gen.find(" pub=") + 5, mrfw::kAdminKeyHex);
        fp  = gen.substr(gen.find(" fp=") + 4, mrfw::kAdminFpHex);
        CHECK(fp_of_hexkey(pub.c_str()) == fp);
    }
    // ⛔ NO SEED BYTE ANYWHERE IN THE LINE.
    char seed_hex[mrfw::kAdminKeyHex + 1];
    mrfw::admin_key_hex(st.rec.seed, seed_hex);
    CHECK(gen.find(seed_hex) == std::string::npos);

    const std::string show = run_id(svc, " show");
    CHECK(show.rfind("> admin-id ok fp=", 0) == 0);
    if (!pub.empty()) {
        CHECK(show.find(" pub=" + pub) != std::string::npos);
        CHECK(show.find(" fp=" + fp) != std::string::npos);
    }
}

TEST_CASE("radmin3/verbs: admin-id rotate/reset REQUIRE an exact confirm and emit their own past-tense word") {
    { FakeAdminStore st; FakeSeed sd; st.seed_valid(0x31);
      mrfw::AdminIdService svc(st, sd);
      const std::string r = run_id(svc, " rotate confirm");
      CHECK(r.rfind("> admin-id rotated fp=", 0) == 0);
      CHECK(st.saves == 1); }
    { FakeAdminStore st; FakeSeed sd; st.state = mrnv::AdminIdRead::invalid;
      mrfw::AdminIdService svc(st, sd);
      const std::string r = run_id(svc, " reset confirm");
      CHECK(r.rfind("> admin-id recovered fp=", 0) == 0);
      CHECK(st.saves == 1); }
}

TEST_CASE("radmin3/verbs: ⛔ a missing, wrong or trailing-junk confirm costs ZERO writes and ZERO draws") {
    for (const char* tail : { " rotate", " rotate ", " rotate confirmm", " rotate CONFIRM", " rotate confirm ",
                              " rotate confirm x", " rotate yes", " rotate  confirm x", " rotate confir" }) {
        CAPTURE(tail);
        FakeAdminStore st; FakeSeed sd; st.seed_valid(0x31);
        mrfw::AdminIdService svc(st, sd);
        CHECK(run_id(svc, tail) == "> admin-id err bad_args\n");
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);
        CHECK(st.loads == 0);        // ⛔ the grammar refuses BEFORE the service is reached at all
    }
    // ★ `rotate  confirm` (two spaces) IS accepted: parse_confirm_token skips the dispatcher's separating spaces.
    FakeAdminStore ok; FakeSeed sd2; ok.seed_valid(0x31);
    mrfw::AdminIdService svc(ok, sd2);
    CHECK(run_id(svc, " rotate  confirm").rfind("> admin-id rotated", 0) == 0);
}

TEST_CASE("radmin3/verbs: admin-id refuses a bare verb, an unknown subcommand and every extra token") {
    for (const char* tail : { "", " ", "  ", "\t", " bogus", " showx", " Show", " SHOW", " sho",
                              " show x", " show  x", " generate now", " generate 1", " list", " reset",
                              " generate confirm" }) {
        CAPTURE(tail);
        FakeAdminStore st; FakeSeed sd; st.seed_valid(0x31);
        mrfw::AdminIdService svc(st, sd);
        CHECK(run_id(svc, tail) == "> admin-id err bad_args\n");
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);
    }
}

TEST_CASE("radmin3/verbs: every admin-id refusal reason reaches the console with its literal lexeme") {
    struct Row { mrnv::AdminIdRead read; bool seed_ok; const char* verb; const char* want; };
    for (Row r : {
            Row{ mrnv::AdminIdRead::absent,    false, " show",           "> admin-id err absent\n" },
            Row{ mrnv::AdminIdRead::invalid,   false, " show",           "> admin-id err store_invalid\n" },
            Row{ mrnv::AdminIdRead::io_failed, false, " show",           "> admin-id err store_io_failed\n" },
            Row{ mrnv::AdminIdRead::ok,        true,  " generate",       "> admin-id err already_present\n" },
            Row{ mrnv::AdminIdRead::invalid,   false, " generate",       "> admin-id err store_invalid\n" },
            Row{ mrnv::AdminIdRead::io_failed, false, " generate",       "> admin-id err store_io_failed\n" },
            Row{ mrnv::AdminIdRead::absent,    false, " rotate confirm", "> admin-id err absent\n" },
            Row{ mrnv::AdminIdRead::io_failed, false, " rotate confirm", "> admin-id err store_io_failed\n" },
            Row{ mrnv::AdminIdRead::absent,    false, " reset confirm",  "> admin-id err not_invalid\n" },
            Row{ mrnv::AdminIdRead::ok,        true,  " reset confirm",  "> admin-id err not_invalid\n" },
            // ⛔⛔ THE ONE THAT MATTERS MOST: an unreadable store refuses even the recovery verb.
            Row{ mrnv::AdminIdRead::io_failed, false, " reset confirm",  "> admin-id err store_io_failed\n" } }) {
        CAPTURE(r.verb);
        FakeAdminStore st; FakeSeed sd;
        st.state = r.read;
        if (r.seed_ok) { mrnv::admin_id_blob_init(st.rec); std::memset(st.rec.seed, 0x77, 32); }
        mrfw::AdminIdService svc(st, sd);
        CHECK(run_id(svc, r.verb) == r.want);
        CHECK(st.saves == 0);
    }
    // …and the two service-side failures the store itself produces.
    { FakeAdminStore st; FakeSeed sd; st.state = mrnv::AdminIdRead::absent; sd.answer = false;
      mrfw::AdminIdService svc(st, sd);
      CHECK(run_id(svc, " generate") == "> admin-id err entropy_failed\n"); }
    { FakeAdminStore st; FakeSeed sd; st.state = mrnv::AdminIdRead::absent; st.save_ok = false;
      mrfw::AdminIdService svc(st, sd);
      CHECK(run_id(svc, " generate") == "> admin-id err nv_save_failed\n"); }
}

TEST_CASE("radmin3/verbs: the reason lexeme table is EXHAUSTIVE and every name is distinct") {
    const char* names[] = {
        mrfw::admin_id_err_name(mrfw::AdminIdErr::none),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::bad_args),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::absent),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::store_invalid),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::store_io_failed),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::already_present),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::not_invalid),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::entropy_failed),
        mrfw::admin_id_err_name(mrfw::AdminIdErr::nv_save_failed) };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        CHECK(names[i][0] != '\0');
        for (size_t j = i + 1; j < sizeof names / sizeof names[0]; ++j)
            CHECK(std::strcmp(names[i], names[j]) != 0);
    }
    CHECK(std::strcmp(mrfw::admin_id_state_name(AdminIdState::ok), "ok") == 0);
    CHECK(std::strcmp(mrfw::admin_id_state_name(AdminIdState::absent), "absent") == 0);
    CHECK(std::strcmp(mrfw::admin_id_state_name(AdminIdState::invalid), "invalid") == 0);
    CHECK(std::strcmp(mrfw::admin_id_state_name(AdminIdState::io_failed), "io_failed") == 0);
    CHECK(std::strcmp(mrfw::acl_state_name(AclState::ok), "ok") == 0);
    CHECK(std::strcmp(mrfw::acl_state_name(AclState::absent), "absent") == 0);
    CHECK(std::strcmp(mrfw::acl_state_name(AclState::invalid), "invalid") == 0);
    CHECK(std::strcmp(mrfw::acl_state_name(AclState::io_failed), "io_failed") == 0);
    const char* an[] = {
        mrfw::acl_err_name(mrfw::AclErr::none),                 mrfw::acl_err_name(mrfw::AclErr::bad_args),
        mrfw::acl_err_name(mrfw::AclErr::store_invalid),        mrfw::acl_err_name(mrfw::AclErr::store_io_failed),
        mrfw::acl_err_name(mrfw::AclErr::not_invalid),          mrfw::acl_err_name(mrfw::AclErr::identity_absent),
        mrfw::acl_err_name(mrfw::AclErr::identity_invalid),     mrfw::acl_err_name(mrfw::AclErr::identity_io_failed),
        mrfw::acl_err_name(mrfw::AclErr::duplicate_key),        mrfw::acl_err_name(mrfw::AclErr::zero_key),
        mrfw::acl_err_name(mrfw::AclErr::acl_full),             mrfw::acl_err_name(mrfw::AclErr::slot_empty),
        mrfw::acl_err_name(mrfw::AclErr::first_owner_required), mrfw::acl_err_name(mrfw::AclErr::last_owner),
        mrfw::acl_err_name(mrfw::AclErr::self_slot),            mrfw::acl_err_name(mrfw::AclErr::nv_save_failed) };
    for (size_t i = 0; i < sizeof an / sizeof an[0]; ++i) {
        CHECK(an[i][0] != '\0');
        for (size_t j = i + 1; j < sizeof an / sizeof an[0]; ++j) CHECK(std::strcmp(an[i], an[j]) != 0);
    }
}

// ================================================================================================================
// `acl` — grammar and exact bytes
// ================================================================================================================

TEST_CASE("radmin3/verbs: acl list emits occupied rows in SLOT ORDER, then the end line") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x0C);
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);

    CHECK(run_acl(acl, id, " list") == "> acl end count=0 owners=0 operators=0\n");

    CHECK(run_acl(acl, id, std::string(" add owner ") + kKeySeq).rfind("> acl added slot=0", 0) == 0);
    CHECK(run_acl(acl, id, std::string(" add operator ") + kKeyA).rfind("> acl added slot=1", 0) == 0);
    CHECK(run_acl(acl, id, std::string(" add operator ") + kKeyB).rfind("> acl added slot=2", 0) == 0);
    CHECK(run_acl(acl, id, " remove 1 confirm") == "> acl removed slot=1\n");

    const std::string got = run_acl(acl, id, " list");
    const std::string want =
        std::string("> acl slot=0 role=owner fp=") + kFpSeq + " pub=" + kKeySeq + "\n"
        "> acl slot=2 role=operator fp=" + fp_of_hexkey(kKeyB) + " pub=" + kKeyB + "\n"
        "> acl end count=2 owners=1 operators=1\n";
    CHECK(got == want);          // ⛔ the HOLE at slot 1 is skipped and slot 2 KEEPS ITS NUMBER
}

TEST_CASE("radmin3/verbs: a FULL ten-row listing is one line per record and fits the console stage") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x0D);
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);
    // The widest shape: ten rows, nine of them `operator` (the longer token).
    for (int i = 0; i < 10; ++i) {
        std::string key(64, '0');
        for (int j = 0; j < 64; ++j) key[j] = "0123456789abcdef"[(i * 7 + j) % 16];
        const std::string role = (i == 0) ? "owner" : "operator";
        CHECK(run_acl(acl, id, " add " + role + " " + key).rfind("> acl added", 0) == 0);
    }
    Span sp(std::string(" list"));
    CaptureLines out;
    mrfw::acl_verb(acl, id, sp.p, sp.n, out);
    CHECK(out.lines.size() == 11);                       // ★ ten rows + one end line, ONE record per call
    for (const std::string& l : out.lines) {
        CHECK(l.back() == '\n');
        CHECK(l.find('\n') == l.size() - 1);             // ⛔ exactly ONE newline per emitted record
        CHECK(l.size() < mrfw::kAdminLineMax);
    }
    CHECK(out.all.size() <= mrfw::kAdminListMaxBytes);
    CHECK(out.all.size() < 2048);                        // ⛔ ZERO CONSOLE_DROP on the 2048-byte stage
    if (!out.lines.empty()) CHECK(out.lines.back() == "> acl end count=10 owners=1 operators=9\n");
}

TEST_CASE("radmin3/verbs: acl add — the exact success line, and the fp matches the printed key") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x0E);
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.state = mrnv::AclRead::absent;
    mrfw::AclService acl(st);
    const std::string got = run_acl(acl, id, std::string(" add owner ") + kKeySeq);
    CHECK(got == std::string("> acl added slot=0 role=owner fp=") + kFpSeq + " pub=" + kKeySeq + "\n");
    CHECK(st.saves == 1);
    // ★ AN UPPERCASE key token is accepted by the decoder and RENDERED BACK IN LOWERCASE — one canonical form.
    FakeAclStore st2; st2.state = mrnv::AclRead::absent;
    mrfw::AclService acl2(st2);
    std::string upper(kKeyA);
    for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'f' ? c - 32 : c);
    const std::string up = run_acl(acl2, id, " add owner " + upper);
    CHECK(up.find(std::string(" pub=") + kKeyA) != std::string::npos);
}

TEST_CASE("radmin3/verbs: acl add refuses every malformed role or key — ZERO writes, one bad_args line") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x0F);
    mrfw::AdminIdService id(ist, sd);
    const std::string k(kKeyA);
    for (std::string tail : {
            std::string(" add"), std::string(" add owner"), std::string(" add owner "),
            std::string(" add  owner  "),
            " add ownerx " + k, " add OWNER " + k, " add own " + k, " add operators " + k,
            " add empty " + k, " add 2 " + k, " add owner " + k.substr(0, 63),
            " add owner " + k + "0", " add owner " + k + "00", " add owner " + k.substr(0, 62) + "gg",
            " add owner 0x" + k.substr(0, 62), " add owner " + k + " extra",
            " add owner " + k + " " + k, " add owner " + std::string(64, 'z') }) {
        CAPTURE(tail);
        FakeAclStore st; st.empty_valid();
        mrfw::AclService acl(st);
        CHECK(run_acl(acl, id, tail) == "> acl err bad_args\n");
        CHECK(st.saves == 0);
    }
    // ★ AN ALL-ZERO key is syntactically fine and is refused by the SERVICE, with its own reason.
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);
    CHECK(run_acl(acl, id, " add owner " + std::string(64, '0')) == "> acl err zero_key\n");
    CHECK(st.saves == 0);
}

TEST_CASE("radmin3/verbs: acl set — updated / unchanged, and every malformed slot is bad_args") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x1A);
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);
    CHECK(run_acl(acl, id, std::string(" add owner ") + kKeyA).rfind("> acl added slot=0", 0) == 0);
    CHECK(run_acl(acl, id, std::string(" add operator ") + kKeyB).rfind("> acl added slot=1", 0) == 0);
    const int saves = st.saves;

    CHECK(run_acl(acl, id, " set 1 operator") == "> acl unchanged slot=1 role=operator\n");
    CHECK(st.saves == saves);                     // ⛔ a NO-OP writes nothing
    CHECK(run_acl(acl, id, " set 1 owner") == "> acl updated slot=1 role=owner\n");
    CHECK(st.saves == saves + 1);

    // ★ THE STRICT INDEX: `atol`'s prefix parse, the sign, the overflow and the trailing junk all refuse.
    for (const char* tail : { " set", " set 1", " set  1 ", " set 1junk owner", " set +1 owner", " set -1 owner",
                              " set 01x owner", " set 1.0 owner", " set  owner", " set 1 owner extra",
                              " set 99999999999999999999 owner", " set 0x1 owner", " set 1 ownerx",
                              " set 1 OWNER", " set 1 empty", " set '' owner" }) {
        CAPTURE(tail);
        CHECK(run_acl(acl, id, tail) == "> acl err bad_args\n");
    }
    // ★ `01` IS legal decimal — the strict parser rejects junk, ⛔ not leading zeros.
    CHECK(run_acl(acl, id, " set 01 owner") == "> acl unchanged slot=1 role=owner\n");
    // …and a slot outside 0..9 is bad_args BEFORE any narrowing (⛔ never wrapped into slot 0).
    for (const char* tail : { " set 10 owner", " set 11 owner", " set 256 owner", " set 300 owner" }) {
        CAPTURE(tail);
        CHECK(run_acl(acl, id, tail) == "> acl err bad_args\n");
    }
    CHECK(st.rec.rec[0].role == mrnv::kAclRoleOwner);
}

TEST_CASE("radmin3/verbs: acl remove/reset gates and lines") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x1C);
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);
    CHECK(run_acl(acl, id, std::string(" add owner ") + kKeyA).rfind("> acl added", 0) == 0);
    CHECK(run_acl(acl, id, std::string(" add operator ") + kKeyB).rfind("> acl added", 0) == 0);
    const int saves = st.saves;

    for (const char* tail : { " remove 1", " remove 1 ", " remove 1 confirmm", " remove 1 CONFIRM",
                              " remove 1 confirm ", " remove 1 confirm x", " remove confirm",
                              " remove 1junk confirm", " remove 10 confirm", " remove" }) {
        CAPTURE(tail);
        CHECK(run_acl(acl, id, tail) == "> acl err bad_args\n");
        CHECK(st.saves == saves);
    }
    CHECK(run_acl(acl, id, " remove 1 confirm") == "> acl removed slot=1\n");
    CHECK(st.saves == saves + 1);
    CHECK(run_acl(acl, id, " remove 1 confirm") == "> acl err slot_empty\n");
    // ⛔ THE LAST OWNER
    CHECK(run_acl(acl, id, " remove 0 confirm") == "> acl err last_owner\n");
    CHECK(run_acl(acl, id, " set 0 operator") == "> acl err last_owner\n");

    // `acl reset confirm` — INVALID only, and the counters are literal zeroes.
    for (const char* tail : { " reset", " reset ", " reset confirmm", " reset confirm x", " reset yes" }) {
        CAPTURE(tail);
        CHECK(run_acl(acl, id, tail) == "> acl err bad_args\n");
    }
    CHECK(run_acl(acl, id, " reset confirm") == "> acl err not_invalid\n");
    FakeAclStore bad; bad.state = mrnv::AclRead::invalid;
    mrfw::AclService acl2(bad);
    CHECK(run_acl(acl2, id, " reset confirm") == "> acl recovered count=0 owners=0 operators=0\n");
    CHECK(bad.saves == 1);
    FakeAclStore dead; dead.state = mrnv::AclRead::io_failed;
    mrfw::AclService acl3(dead);
    CHECK(run_acl(acl3, id, " reset confirm") == "> acl err store_io_failed\n");
    CHECK(dead.saves == 0);
}

TEST_CASE("radmin3/verbs: acl refuses a bare verb and every unknown subcommand") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x1D);
    mrfw::AdminIdService id(ist, sd);
    for (const char* tail : { "", " ", "  ", "\t", " bogus", " lists", " List", " LIST", " li",
                              " list x", " list  x", " show", " generate", " rotate confirm", " del 0 confirm" }) {
        CAPTURE(tail);
        FakeAclStore st; st.empty_valid();
        mrfw::AclService acl(st);
        CHECK(run_acl(acl, id, tail) == "> acl err bad_args\n");
        CHECK(st.saves == 0);
    }
}

TEST_CASE("radmin3/verbs: the identity gate reaches the console with its OWN three reasons") {
    struct Row { mrnv::AdminIdRead read; bool seed_ok; const char* want; };
    for (Row r : { Row{ mrnv::AdminIdRead::absent,    false, "> acl err identity_absent\n" },
                   Row{ mrnv::AdminIdRead::invalid,   false, "> acl err identity_invalid\n" },
                   Row{ mrnv::AdminIdRead::io_failed, false, "> acl err identity_io_failed\n" },
                   // ★ a STORAGE-ok root with a dead-RNG seed is INVALID, ⛔ not usable
                   Row{ mrnv::AdminIdRead::ok,        false, "> acl err identity_invalid\n" } }) {
        FakeAdminStore ist; FakeSeed sd;
        ist.state = r.read;
        if (r.read == mrnv::AdminIdRead::ok) mrnv::admin_id_blob_init(ist.rec);   // all-zero seed
        mrfw::AdminIdService id(ist, sd);
        FakeAclStore st; st.empty_valid();
        mrfw::AclService acl(st);
        CHECK(run_acl(acl, id, std::string(" add owner ") + kKeyA) == r.want);
        CHECK(st.saves == 0);
    }
    // ⛔ AND ONLY `add` PAYS FOR THAT READ: `list`/`set`/`remove` never consult the identity store.
    FakeAdminStore ist; FakeSeed sd; ist.state = mrnv::AdminIdRead::io_failed;
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);
    const int before = ist.loads;
    CHECK(run_acl(acl, id, " list") == "> acl end count=0 owners=0 operators=0\n");
    CHECK(run_acl(acl, id, " set 0 owner") == "> acl err slot_empty\n");
    CHECK(run_acl(acl, id, " remove 0 confirm") == "> acl err slot_empty\n");
    CHECK(ist.loads == before);
}

TEST_CASE("radmin3/verbs: every ACL service refusal reaches the console with its literal lexeme") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x2E);
    mrfw::AdminIdService id(ist, sd);
    { FakeAclStore st; st.state = mrnv::AclRead::invalid; mrfw::AclService a(st);
      CHECK(run_acl(a, id, " list") == "> acl err store_invalid\n"); }
    { FakeAclStore st; st.state = mrnv::AclRead::io_failed; mrfw::AclService a(st);
      CHECK(run_acl(a, id, " list") == "> acl err store_io_failed\n"); }
    { FakeAclStore st; st.empty_valid(); mrfw::AclService a(st);
      CHECK(run_acl(a, id, std::string(" add operator ") + kKeyA) == "> acl err first_owner_required\n"); }
    { FakeAclStore st; st.empty_valid(); mrfw::AclService a(st);
      CHECK(run_acl(a, id, std::string(" add owner ") + kKeyA).rfind("> acl added", 0) == 0);
      CHECK(run_acl(a, id, std::string(" add owner ") + kKeyA) == "> acl err duplicate_key\n"); }
    { FakeAclStore st; st.empty_valid(); mrfw::AclService a(st);
      for (int i = 0; i < 10; ++i) {
          std::string key(64, '0');
          for (int j = 0; j < 64; ++j) key[j] = "0123456789abcdef"[(i * 3 + j) % 16];
          CHECK(run_acl(a, id, std::string(i == 0 ? " add owner " : " add operator ") + key)
                      .rfind("> acl added", 0) == 0);
      }
      CHECK(run_acl(a, id, std::string(" add operator ") + kKeyA) == "> acl err acl_full\n"); }
    { FakeAclStore st; st.empty_valid(); st.save_ok = false; mrfw::AclService a(st);
      CHECK(run_acl(a, id, std::string(" add owner ") + kKeyA) == "> acl err nv_save_failed\n"); }
}

// ================================================================================================================
// THE BOOT REPORT
// ================================================================================================================

TEST_CASE("radmin3/verbs: the boot report is TWO read-only lines and names the state on every arm") {
    struct Row { mrnv::AdminIdRead idr; bool seed_ok; mrnv::AclRead aclr; bool build;
                 const char* want_id; const char* want_acl; };
    for (Row r : {
            Row{ mrnv::AdminIdRead::absent,    false, mrnv::AclRead::absent,    false,
                 "> admin-id boot state=absent\n",    "> acl boot state=absent count=0 owners=0 operators=0\n" },
            Row{ mrnv::AdminIdRead::invalid,   false, mrnv::AclRead::invalid,   false,
                 "> admin-id boot state=invalid\n",   "> acl boot state=invalid count=0 owners=0 operators=0\n" },
            Row{ mrnv::AdminIdRead::io_failed, false, mrnv::AclRead::io_failed, false,
                 "> admin-id boot state=io_failed\n", "> acl boot state=io_failed count=0 owners=0 operators=0\n" },
            Row{ mrnv::AdminIdRead::ok,        true,  mrnv::AclRead::ok,        true,
                 "> admin-id boot state=ok\n",        "> acl boot state=ok count=2 owners=1 operators=1\n" },
            // ★ a STORAGE-ok record with dead-RNG content reports `invalid` — the honest answer.
            Row{ mrnv::AdminIdRead::ok,        false, mrnv::AclRead::ok,        false,
                 "> admin-id boot state=invalid\n",   "> acl boot state=ok count=0 owners=0 operators=0\n" } }) {
        CAPTURE(r.want_id);
        FakeAdminStore ist; FakeSeed sd;
        ist.state = r.idr;
        if (r.idr == mrnv::AdminIdRead::ok) {
            mrnv::admin_id_blob_init(ist.rec);
            if (r.seed_ok) std::memset(ist.rec.seed, 0x66, 32);
        }
        FakeAclStore st;
        st.state = r.aclr;
        if (r.aclr == mrnv::AclRead::ok) {
            mrnv::acl_blob_init(st.rec);
            if (r.build) {
                std::memset(st.rec.rec[0].ed_pub, 0xA1, 32); st.rec.rec[0].role = mrnv::kAclRoleOwner;
                std::memset(st.rec.rec[6].ed_pub, 0xA2, 32); st.rec.rec[6].role = mrnv::kAclRoleOperator;
                st.rec.count = 2;
            }
        }
        mrfw::AdminIdService id(ist, sd);
        mrfw::AclService acl(st);
        CaptureLines out;
        mrfw::admin_boot_report(id, acl, out);
        CHECK(out.lines.size() == 2);
        if (out.lines.size() == 2) {
            CHECK(out.lines[0] == r.want_id);
            CHECK(out.lines[1] == r.want_acl);
        }
        CHECK(ist.saves == 0);
        CHECK(st.saves == 0);
        CHECK(sd.draws == 0);
        // ⛔ NO KEY BYTE, NO FINGERPRINT, ⛔ ever, in a boot line.
        CHECK(out.all.find("pub=") == std::string::npos);
        CHECK(out.all.find("fp=") == std::string::npos);
    }
}

TEST_CASE("radmin3/verbs: every response goes to the SUPPLIED sink and nowhere else") {
    FakeAdminStore ist; FakeSeed sd; ist.seed_valid(0x3F);
    mrfw::AdminIdService id(ist, sd);
    FakeAclStore st; st.empty_valid();
    mrfw::AclService acl(st);
    CaptureLines a, b;
    Span sp(std::string(" list"));
    mrfw::acl_verb(acl, id, sp.p, sp.n, a);
    CHECK(a.lines.size() == 1);
    CHECK(b.lines.empty());                       // ⛔ a second sink saw NOTHING — there is no global sink
    mrfw::acl_verb(acl, id, sp.p, sp.n, b);
    CHECK(b.all == a.all);                        // …and the same call to a different sink is byte-identical
}
