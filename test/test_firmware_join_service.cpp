// MeshRoute — test/test_firmware_join_service.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-15 slice 1 — the typed STATIC-JOIN transaction (`src/firmware_join_service.h`).
//
// ★★★ WHAT THESE CASES ARE FOR, AND WHY THE FAKES ARE THE INSTRUMENT RATHER THAN A CONVENIENCE. Before this slice
//     every one of the properties below lived inside `handle_join`, in `src/firmware_config.cpp` — a file compiled by
//     NEITHER this suite (`test_build_src = no`) NOR the simulator, and reached by no corpus scenario. ⇒ "exactly one
//     write", "zero live calls on a failure" and "save BEFORE live" were unmeasurable by construction. A COUNTING
//     store and a COUNTING live seam are what make them measurable at all — and they are also the LIMIT of the claim:
//     ⛔ no NVS/LittleFS write, no wear, and no reset-during-write behaviour is exercised here ([[B193]]).
//
// ⓘ Counted discriminators are preferred to state assertions throughout (the dispatch's standing warning: sixteen
//   instruments in this arc were GREEN against the very defect they were written to catch). Every ordering claim is
//   made against a monotonic sequence stamp, not against "the flag ended up true".
#include <doctest.h>
#include <cstring>
#include "firmware_join_service.h"

namespace {

// A monotonic clock shared by BOTH fakes — this is what turns "save before live" into a MEASUREMENT (two stamps
// compared) instead of an assertion about a boolean that some other path could also have set.
static int g_seq = 0;

struct FakeJoinStore : mrfw::ICfgStore {
    mrnv::Blob rec{};          // what "NV" holds
    mrnv::Blob written{};      // the last candidate handed to save()
    int  loads = 0, saves = 0;
    int  last_save_seq = -1;
    bool load_ok = true, save_ok = true;

    bool load(mrnv::Blob& out) override {
        ++loads;
        if (!load_ok) return false;   // ⓘ deliberately leaves `out` untouched — the §nv-ritual's documented warning
        out = rec;
        return true;
    }
    bool save(const mrnv::Blob& b) override {
        ++saves; last_save_seq = ++g_seq;
        if (!save_ok) return false;
        written = b; rec = b;
        return true;
    }
};

struct FakeJoinLive : mrfw::IJoinLive {
    int calls = 0;
    int last_call_seq = -1;
    mrnv::Blob seen{};
    void apply_and_start(const mrnv::Blob& b) override { ++calls; last_call_seq = ++g_seq; seen = b; }
};

struct Fix {
    FakeJoinStore store;
    FakeJoinLive  live;
    mrfw::JoinService svc{store, live};
    Fix() {
        g_seq = 0;
        // A NON-BLANK starting record, so "the non-provisioning fields are PRESERVED" is measurable rather than
        // vacuously true over a zeroed blob.
        std::memset(&store.rec, 0, sizeof store.rec);
        store.rec.magic = mrnv::kMagic; store.rec.version = mrnv::kVersion;
        store.rec.node_id = 42; store.rec.joined = 1; store.rec.lineage_id = 0x1234; store.rec.config_epoch = 7;
        store.rec.leaf_name_len = 3; store.rec.leaf_name[0] = 'a'; store.rec.leaf_name[1] = 'b'; store.rec.leaf_name[2] = 'c';
        store.rec.team_id = 0xDEADBEEF; store.rec.tx_power = 22; store.rec.allowed_sf_bitmap = 0x0280;
        store.rec.freq_mhz = 868.0; store.rec.bw_hz = 250000; store.rec.routing_sf = 7;
        store.rec.leaf_id = 9; store.rec.layer0_id = 9;
    }
};

mrfw::JoinRequest req(uint8_t layer, double freq, double bw_khz, uint8_t sf) {
    mrfw::JoinRequest r{}; r.layer = layer; r.freq_mhz = freq; r.bw_khz = bw_khz; r.routing_sf = sf; return r;
}

}  // namespace

// ================================================================================ the SUCCESS path
TEST_CASE("ui15-join: a valid request writes ONCE, then applies live — and in that order") {
    Fix f;
    const mrfw::JoinResult r = f.svc.apply_join(req(4, 869.525, 125.0, 9));

    CHECK(r.verdict == mrfw::JoinVerdict::started);   // ⛔ `started`, never `joined` — DAD has only BEGUN (spec §2.3)
    CHECK(r.err     == mrfw::JoinErr::none);
    CHECK(f.store.loads == 1);
    CHECK(f.store.saves == 1);                        // ★ EXACTLY ONE write attempt
    CHECK(f.live.calls  == 1);                        // ★ EXACTLY ONE live apply
    // ★★ THE ORDER, AS A COMPARISON OF TWO COUNTERS. A test that only checked `calls == 1` would stay GREEN against
    //    an implementation that applied live and THEN saved — the very defect [[B207]] found in the team verb.
    CHECK(f.store.last_save_seq < f.live.last_call_seq);

    // the record the live seam was handed is the record that was SAVED, byte for byte — not a re-derived copy
    CHECK(std::memcmp(&f.live.seen, &f.store.written, sizeof(mrnv::Blob)) == 0);
}

TEST_CASE("ui15-join: the candidate carries the PHY and clears exactly the provisioning fields") {
    Fix f;
    (void)f.svc.apply_join(req(4, 869.525, 125.0, 9));
    const mrnv::Blob& b = f.store.written;

    CHECK(b.freq_mhz   == 869.525);
    CHECK(b.bw_hz      == 125000u);          // 125 kHz -> Hz, converted ONCE, inside the transaction
    CHECK(b.routing_sf == 9);
    CHECK(b.node_id      == 0);              // unprovisioned -> DAD
    CHECK(b.joined       == 0);
    CHECK(b.lineage_id   == 0);
    CHECK(b.config_epoch == 0);
    CHECK(b.leaf_name_len == 0);             // §clean-join
    // ⛔ THE NAME BYTES ARE NOT ZEROED, ONLY THE LENGTH — verbatim pre-slice behaviour, and it is asserted rather
    //    than left implicit because "tidying" it would silently move the persisted record's bytes.
    CHECK(b.leaf_name[0] == 'a');
    // ...and everything the join does not name survives the round trip (U2: the carrier is LOADED, not rebuilt)
    CHECK(b.team_id  == 0xDEADBEEFu);
    CHECK(b.tx_power == 22);
    CHECK(b.allowed_sf_bitmap == 0x0280);
    CHECK(b.magic   == mrnv::kMagic);
    CHECK(b.version == mrnv::kVersion);
}

// ★★★ THE PIN THE §UI-15 SLICE-6 CORRELATION RULE RESTS ON (spec §2.3.7), AND IT IS WHY THE TWO FIELDS MUST NOT BE
//     COLLAPSED: `layer0_id` takes the FULL byte and `leaf_id` the NIBBLE. A request for layer 17 persists 17 and
//     lives/pushes as leaf 1 — v3 of the plan required "stored layer == live layer", which is UNSATISFIABLE above 15
//     and would have made OLED join fail permanently on every layer over 15.
TEST_CASE("ui15-join: layer0_id keeps the FULL byte while leaf_id keeps the nibble") {
    struct Row { uint8_t layer, leaf; } rows[] = { {1,1}, {15,15}, {16,0}, {17,1}, {200,8}, {255,15} };
    for (const Row& row : rows) {
        Fix f;
        const mrfw::JoinResult r = f.svc.apply_join(req(row.layer, 868.1, 125.0, 8));
        CHECK(r.verdict == mrfw::JoinVerdict::started);
        CHECK(f.store.written.layer0_id == row.layer);   // the FULL byte
        CHECK(f.store.written.leaf_id   == row.leaf);    // the wire nibble
        CHECK(r.layer == row.layer);                     // ...and the RESULT reports both, so the JSON ack does not
        CHECK(r.leaf  == row.leaf);                      //    have to re-derive either one
    }
}

// ★★ THE ANTI-INTEGER-kHz PIN (plan v3's correction, and this build's own default carrier). 869.4625 MHz is
//    869462.5 kHz: a `uint32_t freq_khz` request field would round it to 869462 and CHANGE THE RADIO. The stored
//    value must be the exact double the parser produced.
TEST_CASE("ui15-join: a fractional carrier survives the transaction EXACTLY (no integer-kHz rounding)") {
    Fix f;
    (void)f.svc.apply_join(req(3, 869.4625, 62.5, 7));
    CHECK(f.store.written.freq_mhz == 869.4625);   // ⛔ exact, not near
    CHECK(f.store.written.bw_hz    == 62500u);     // and a FRACTIONAL bandwidth is a real LoRa bandwidth
}

// ================================================================================ the REFUSAL arms
// ★ EACH ARM SEPARATELY, EACH WITH THE OTHER THREE FIELDS VALID — which is what makes every arm individually
//   load-bearing. A case that left two fields bad could not say which predicate did the refusing.
TEST_CASE("ui15-join: every domain refusal is DISTINCT and costs zero loads, zero writes, zero live calls") {
    struct Row { const char* what; mrfw::JoinRequest rq; mrfw::JoinErr err; } rows[] = {
        { "layer 0",   req(0,   869.525, 125.0, 9),  mrfw::JoinErr::invalid_layer },
        { "freq low",  req(4,    99.0,   125.0, 9),  mrfw::JoinErr::invalid_freq  },
        { "freq high", req(4,  1001.0,   125.0, 9),  mrfw::JoinErr::invalid_freq  },
        { "bw low",    req(4,   869.525,   6.9, 9),  mrfw::JoinErr::invalid_bw    },
        { "bw high",   req(4,   869.525, 501.0, 9),  mrfw::JoinErr::invalid_bw    },
        { "sf low",    req(4,   869.525, 125.0, 4),  mrfw::JoinErr::invalid_sf    },
        { "sf high",   req(4,   869.525, 125.0, 13), mrfw::JoinErr::invalid_sf    },
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        Fix f;
        const mrfw::JoinResult r = f.svc.apply_join(row.rq);
        CHECK(r.verdict == mrfw::JoinVerdict::refused);
        CHECK(r.err     == row.err);
        CHECK(f.store.loads == 0);    // ⛔ a refusal does not even READ the record
        CHECK(f.store.saves == 0);
        CHECK(f.live.calls  == 0);    // ⛔ no retune, no DAD, no airtime
    }
}

// ⓘ THE FOUR ARMS ARE DISTINCT VALUES, not four spellings of one refusal — asserted directly, because the console
//   renders all four with the SAME usage line and a collapsed enum would be invisible there.
TEST_CASE("ui15-join: the four domain arms are four distinct enumerators with distinct names") {
    CHECK(mrfw::JoinErr::invalid_layer != mrfw::JoinErr::invalid_freq);
    CHECK(mrfw::JoinErr::invalid_freq  != mrfw::JoinErr::invalid_bw);
    CHECK(mrfw::JoinErr::invalid_bw    != mrfw::JoinErr::invalid_sf);
    CHECK(std::strcmp(mrfw::join_err_name(mrfw::JoinErr::invalid_layer), "invalid_layer") == 0);
    CHECK(std::strcmp(mrfw::join_err_name(mrfw::JoinErr::invalid_freq),  "invalid_freq")  == 0);
    CHECK(std::strcmp(mrfw::join_err_name(mrfw::JoinErr::invalid_bw),    "invalid_bw")    == 0);
    CHECK(std::strcmp(mrfw::join_err_name(mrfw::JoinErr::invalid_sf),    "invalid_sf")    == 0);
    CHECK(std::strcmp(mrfw::join_err_name(mrfw::JoinErr::nv_load_failed), "nv_load_failed") == 0);
    CHECK(std::strcmp(mrfw::join_err_name(mrfw::JoinErr::nv_save_failed), "nv_save_failed") == 0);
}

// ⛔⛔ THE DELIBERATELY-PRESERVED NaN, AND IT IS A PIN AGAINST A FIX AS MUCH AS AGAINST A REGRESSION.
//     `firmware_config_parse.h` states the standing rule: every one of these call sites has always ACCEPTED a NaN,
//     because the predicates are the NEGATION of the reject-conditions. Starting to reject one here would be a
//     behaviour change smuggled into a refactor (C1) — and it is EXACTLY what a `uint32_t bw_hz` request field did
//     when this slice's byte-identity harness measured it. ⇒ [[JOIN-BW-NAN]] is registered; ⛔ NOT fixed here.
TEST_CASE("ui15-join: a NaN freq/bw is ACCEPTED, verbatim as the pre-slice verb accepted it (C1, [[JOIN-BW-NAN]])") {
    const double nan_v = 0.0 / 0.0;
    {
        Fix f;
        const mrfw::JoinResult r = f.svc.apply_join(req(4, nan_v, 125.0, 9));
        CHECK(r.verdict == mrfw::JoinVerdict::started);   // ⛔ NOT refused — this is the preserved behaviour
        CHECK(f.store.saves == 1);
    }
    {
        Fix f;
        const mrfw::JoinResult r = f.svc.apply_join(req(4, 869.525, nan_v, 9));
        CHECK(r.verdict == mrfw::JoinVerdict::started);
        CHECK(f.store.saves == 1);
    }
}

// ================================================================================ the STORE failure arms
TEST_CASE("ui15-join: a LOAD failure writes nothing and applies nothing") {
    Fix f; f.store.load_ok = false;
    const mrfw::JoinResult r = f.svc.apply_join(req(4, 869.525, 125.0, 9));
    CHECK(r.verdict == mrfw::JoinVerdict::refused);
    CHECK(r.err     == mrfw::JoinErr::nv_load_failed);
    CHECK(f.store.loads == 1);
    CHECK(f.store.saves == 0);
    CHECK(f.live.calls  == 0);
}

// ★★★ THE ATOMICITY PROPERTY, AND THE ONE WORTH THE WHOLE SEAM: the ONE write is attempted, it fails, and ⛔ NOTHING
//     IS APPLIED — no retune, no membership reset, no DAD, so no airtime is spent on a join that will not survive a
//     reboot. ⛔ It is NOT a claim that the failed write left the stored record byte-intact; no host fake can say so.
TEST_CASE("ui15-join: a SAVE failure attempts exactly one write and makes ZERO live calls") {
    Fix f; f.store.save_ok = false;
    const mrfw::JoinResult r = f.svc.apply_join(req(4, 869.525, 125.0, 9));
    CHECK(r.verdict == mrfw::JoinVerdict::nv_failed);
    CHECK(r.err     == mrfw::JoinErr::nv_save_failed);
    CHECK(f.store.loads == 1);
    CHECK(f.store.saves == 1);          // ★ EXACTLY ONE attempt — not zero (it must try), not two (no retry)
    CHECK(f.live.calls  == 0);          // ★ THE PIN
    CHECK(f.live.last_call_seq == -1);  // ...measured as "never stamped", not merely "count is 0"
}

// ⓘ The load/save failures are the only two paths on which the record is READ but not written, so the pre-existing
//   record must be observably untouched by the transaction itself (the fake's `rec` is only assigned on a successful
//   save). Counted rather than asserted structurally: a future `save` that mutated `rec` before failing would show.
TEST_CASE("ui15-join: neither store failure moves the fake's stored record") {
    for (int arm = 0; arm < 2; ++arm) {
        CAPTURE(arm);
        Fix f;
        mrnv::Blob before = f.store.rec;
        if (arm == 0) f.store.load_ok = false; else f.store.save_ok = false;
        (void)f.svc.apply_join(req(4, 869.525, 125.0, 9));
        CHECK(std::memcmp(&before, &f.store.rec, sizeof(mrnv::Blob)) == 0);
    }
}

// ================================================================================ the pure helpers
TEST_CASE("ui15-join: validate_join and join_leaf_of_layer are usable on their own (the OLED path's floor)") {
    CHECK(mrfw::validate_join(req(4, 869.525, 125.0, 9)) == mrfw::JoinErr::none);
    CHECK(mrfw::validate_join(req(0, 869.525, 125.0, 9)) == mrfw::JoinErr::invalid_layer);
    // ★ the ORDER of the four checks is itself observable, and it is pinned so a reordering cannot silently change
    //   WHICH refusal an all-bad request reports to a future screen.
    CHECK(mrfw::validate_join(req(0, 0.0, 0.0, 0)) == mrfw::JoinErr::invalid_layer);
    CHECK(mrfw::validate_join(req(4, 0.0, 0.0, 0)) == mrfw::JoinErr::invalid_freq);
    CHECK(mrfw::validate_join(req(4, 869.525, 0.0, 0)) == mrfw::JoinErr::invalid_bw);
    CHECK(mrfw::validate_join(req(4, 869.525, 125.0, 0)) == mrfw::JoinErr::invalid_sf);
    CHECK(mrfw::join_leaf_of_layer(0)   == 0);
    CHECK(mrfw::join_leaf_of_layer(17)  == 1);
    CHECK(mrfw::join_leaf_of_layer(255) == 15);
}
