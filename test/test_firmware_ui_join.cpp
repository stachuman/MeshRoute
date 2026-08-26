// MeshRoute — test_firmware_ui_join.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK, never
//     REQUIRE — doctest implements REQUIRE's abort with a throw, so it does not compile in this build.
//
// §UI-15 slice 6 — the native suite for the STATIC-JOIN pure unit (`src/firmware_ui_join.h`): plan §2.3's FOUR-TERM
// CORRELATION RULE, the §3 store matrix as PANEL TEXT, the select/confirm/waiting screens' strings and the 60 s word.
//
// ★★★ WHY IT EXISTS AS ITS OWN SUITE: the correlation rule is the decision this slice is built around, and the two
//     traps it defends against are both INVISIBLE to every other gate — `src/firmware_ui.cpp` (which supplies the
//     two device facts) is compiled by neither the native suite nor the simulator, and no corpus scenario runs a UI.
//     A rule proved only through the model would also share `model`'s mutation battery; here each of the four terms
//     gets its own entry in `--target=uijoin`, which is what makes *"each term was mutated SEPARATELY"* a
//     measurement.
//
// ★★ THE SHAPES ARE NAMED, NOT NUMBERED. Plan §2.3 says `join_adopted` fires for *"verb join/create, boot DAD, OR
//    the heal re-adopt"*, so the negative cases below are written as those events rather than as "term N is false":
//    a boot DAD has no session behind it, a heal re-adopt lands on the layer the record CURRENTLY holds, a foreign
//    adopt names somebody else's id. Each still isolates exactly one term, so a single-term drop reddens exactly
//    the case that describes the event it would then accept.
#include "doctest.h"
#include "firmware_ui_join.h"
#include <cstdint>
#include <cstring>

using mrui::JoinSelList;
using mrui::JoinSelRow;
using mrui::UiJoinList;
using mrui::UiJoinSession;
using mrui::join_push_correlates;
using PK = MESHROUTE_NS::PushKind;

namespace {

// A stored profile, as `joinprofile set` would have written it. ⚠ The frequency is the build's own default carrier:
// 869.4625 MHz is 869462500 Hz EXACTLY and is ⛔ not representable in integral kHz — the value every unit in this
// feature is measured against.
mrnv::JoinProfile prof(uint8_t layer, uint8_t sf, uint32_t freq_hz, uint32_t bw_hz, const char* name = nullptr) {
    mrnv::JoinProfile p{};
    p.present    = 1;
    p.layer      = layer;
    p.routing_sf = sf;
    p.freq_hz    = freq_hz;
    p.bw_hz      = bw_hz;
    if (name) {
        const size_t n = strlen(name) > sizeof p.name ? sizeof p.name : strlen(name);
        memcpy(p.name, name, n);
        p.name_len = uint8_t(n);
    }
    return p;
}

// A list in the ORDINARY state: served, `ok`, with the named slots filled.
UiJoinList ok_list() {
    UiJoinList l{};
    l.served = true;
    l.res.verdict = mrfw::ProfileVerdict::ok;
    mrnv::join_blob_init(l.rec);
    return l;
}
UiJoinList refused_list(mrfw::ProfileErr e) {
    UiJoinList l{};
    l.served = true;
    l.res.verdict = mrfw::ProfileVerdict::refused;
    l.res.err     = e;
    mrnv::join_blob_init(l.rec);
    return l;
}
UiJoinList absent_list() {
    UiJoinList l{};
    l.served = true;
    l.res.verdict = mrfw::ProfileVerdict::empty;
    mrnv::join_blob_init(l.rec);
    return l;
}

// The ORDINARY correlated session: the operator asked for layer 4, the record still holds 4, and the node adopted
// id 42 on leaf 4. Every negative case below is this one with EXACTLY ONE fact moved.
struct Corr {
    UiJoinSession sess{};
    PK      kind      = PK::join_adopted;
    uint8_t push_leaf = 4;
    uint8_t push_dst  = 42;
    uint8_t persisted = 4;
    uint8_t canonical = 42;
    Corr() { sess.active = true; sess.requested_layer = 4; }
    bool run() const {
        return join_push_correlates(sess, kind, push_leaf, push_dst, persisted, canonical);
    }
};

}  // namespace

// ============================================================================== the FOUR-TERM RULE (plan §2.3.7)
TEST_CASE("ui15-join-corr control — the ordinary correlated adopt is ACCEPTED, so every rejection below is evidence") {
    Corr c;
    CHECK(c.run() == true);
    // ...and it is not accepted by accident: the four terms really are all satisfied here.
    CHECK(c.sess.active == true);
    CHECK(c.sess.requested_layer == c.persisted);
    CHECK(c.push_leaf == uint8_t(c.sess.requested_layer & 0x0F));
    CHECK(c.push_dst == c.canonical);
    CHECK(c.push_dst != 0);
}

TEST_CASE("ui15-join-corr TERM 1 — a BOOT DAD completes NOTHING: `join_adopted` fires at every boot") {
    // ★★★ THE NAMED SHAPE (plan §2.3, `command.h:223`): a provisioned node emits `join_adopted` on its own boot DAD,
    //     with its OWN layer and its OWN id — i.e. with terms 2, 3 and 4 all TRUE. The only thing that separates it
    //     from the operator's join is that nobody started one.
    Corr c;
    c.sess.active = false;
    CHECK(c.run() == false);
    // ⛔ AND IT IS TERM 1 ALONE THAT REFUSES IT: everything else about this push is indistinguishable from a real one.
    CHECK(c.sess.requested_layer == c.persisted);
    CHECK(c.push_leaf == uint8_t(c.sess.requested_layer & 0x0F));
    CHECK(c.push_dst == c.canonical);
}

TEST_CASE("ui15-join-corr TERM 2 — a HEAL RE-ADOPT on the record's CURRENT layer completes nothing (persisted<->persisted)") {
    // ★★★ THE SECOND NAMED SHAPE: the node re-adopts its claim while a UI join for a DIFFERENT layer is in flight
    //     (or a serial `join` / `cfg set layer0_id` moved the record underneath). The adopt is REAL — it just belongs
    //     to the operation the record now describes, not to the one the screen is waiting for.
    // ⛔⛔ THE SHAPE IS CHOSEN SO THAT **ONLY TERM 2** CAN REFUSE IT, and that is not a detail: 20 and 4 share the
    //     NIBBLE 4, so the push this operation would produce and the push the OTHER one produces are IDENTICAL ON
    //     THE WIRE. Term 3 cannot tell them apart and term 4 cannot either — the record's FULL byte is the only
    //     thing that can. (A first version of this case used layers 9 and 4, which term 3 already separated, so the
    //     term-2 mutation stayed GREEN: the case described term 2 and measured term 3.)
    Corr c;
    c.sess.requested_layer = 20;     // the screen asked for 20 (nibble 4)...
    c.persisted            = 4;      // ...but `/mrcfg` now holds 4 (nibble 4), so this adopt is not ours
    c.push_leaf            = 4;      // ⚠ the SAME nibble either way
    CHECK(c.push_leaf == uint8_t(c.sess.requested_layer & 0x0F));   // term 3 is SATISFIED — and it must still refuse
    CHECK(c.push_dst == c.canonical);                               // ...as is term 4
    CHECK(c.run() == false);
    // ★ THE COMPARISON IS PERSISTED <-> PERSISTED, and it is the FULL byte on both sides: the same request against a
    //   record that DOES hold it is accepted.
    c.persisted = 20;
    CHECK(c.run() == true);
}

TEST_CASE("ui15-join-corr TERM 3 — a push on the WRONG LEAF NIBBLE completes nothing (nibble<->nibble)") {
    Corr c;
    c.push_leaf = 5;                 // the record says layer 4, so leaf 4 — this adopt is on another leaf
    CHECK(c.run() == false);
    c.push_leaf = 4;
    CHECK(c.run() == true);
}

TEST_CASE("ui15-join-corr TERM 4 — a FOREIGN or ZERO dst completes nothing (id<->id, and non-zero)") {
    // ⛔ TWO CLAUSES, BECAUSE THEY ARE TWO FACTS. `dst == 0` is the UNPROVISIONED value (`blob_put_static_join`
    //    writes `node_id = 0` to mean exactly that), so a 0 on both sides would let a node that adopted NOTHING
    //    complete the screen; a non-zero mismatch is somebody else's adoption.
    Corr foreign;
    foreign.push_dst = 43;                       // adopted, but not by us
    CHECK(foreign.run() == false);
    Corr zero;
    zero.push_dst = 0; zero.canonical = 0;       // ⛔ EQUAL, and still refused
    CHECK(zero.run() == false);
    Corr live;
    CHECK(live.run() == true);                   // the positive arm, in the same case
}

TEST_CASE("ui15-join-corr the KIND GATE — ⛔ NO `join_refused` REASON FAILS THE SCREEN, and no other kind completes it") {
    // ★★★★ PLAN §2.3 RULE 6, and it is the OTHER half of the shared-channel problem: `join_refused` carries
    //      wire-version OBSERVATIONS ABOUT OTHER PEERS (`command.h:204`) and mobile-home PHY refusals ride the same
    //      kind, so failing on one would fail the operator's join because somebody else's node is on an old build.
    //      ⇒ every reason is IGNORED FOR COMPLETION. The rule answers "does this COMPLETE the join": for a refusal
    //      the answer is `false`, and `UiModel::on_join_push` treats `false` as "change nothing".
    const MESHROUTE_NS::JoinRefuseReason reasons[] = {
        MESHROUTE_NS::JoinRefuseReason::wire_version, MESHROUTE_NS::JoinRefuseReason::leaf_full,
        MESHROUTE_NS::JoinRefuseReason::phy_mismatch, MESHROUTE_NS::JoinRefuseReason::sf_list_mismatch,
    };
    for (MESHROUTE_NS::JoinRefuseReason r : reasons) {
        (void)r;                                  // the rule never reads it — that IS the property
        Corr c;
        c.kind = PK::join_refused;                // ...and everything else about the push is perfectly correlated
        CHECK(c.run() == false);
    }
    // ⛔ AND NO OTHER PUSH KIND COMPLETES IT EITHER — walked exhaustively rather than sampled, because the gate is a
    //    single `!=` and a mutation widening it must have somewhere to be caught.
    for (int k = 0; k <= int(PK::send_aired); ++k) {
        Corr c;
        c.kind = PK(k);
        CHECK(c.run() == (PK(k) == PK::join_adopted));
    }
}

TEST_CASE("ui15-join-corr ★★ TRAP 2 — a join above LAYER 15 correlates through the NIBBLE and COMPLETES") {
    // ★★★★ THE v3 -> v4 CORRECTION, TESTED AT A VALUE ABOVE 15 EXPLICITLY. `blob_put_static_join` persists the FULL
    //      byte in `layer0_id` and the NIBBLE in `leaf_id`, and the live apply mirrors the NIBBLE into
    //      `layers[0].layer_id` — so a join requested at 17 persists 17, lives as 1 and PUSHES leaf 1. The rule v3
    //      shipped compared the full byte against the live layer and was UNSATISFIABLE here: OLED join would have
    //      failed for ever on every layer above 15.
    Corr c;
    c.sess.requested_layer = 17;
    c.persisted            = 17;     // persisted <-> persisted: the FULL byte
    c.push_leaf            = 1;      // nibble <-> nibble: 17 & 0x0F
    CHECK(mrfw::join_leaf_of_layer(17) == 1);
    CHECK(c.run() == true);
    // ⛔ AND THE COLLAPSED COMPARISON IS THE DEFECT: a push carrying the FULL byte as its leaf is NOT ours (no leaf
    //    nibble can be 17), so the rule must reject it.
    c.push_leaf = 17;
    CHECK(c.run() == false);
    // ★ Several layers above 15, so the case cannot pass on a single lucky value.
    for (uint8_t layer = 16; layer < 200; layer = uint8_t(layer + 17)) {
        Corr x;
        x.sess.requested_layer = layer;
        x.persisted            = layer;
        x.push_leaf            = uint8_t(layer & 0x0F);
        CHECK(x.run() == true);
    }
}

// ================================================================== §3's STORE MATRIX, AS THE PANEL SEES IT (pin 1)
TEST_CASE("ui15-join-store the FOUR store states are FOUR different panel texts — `io_failed` never reads as absent") {
    // ★★★★ THE DISTINCTION [[B218]] BOUGHT: `absent` is an ordinary fresh device, `invalid` means the RECORD is
    //      wrong and `joinprofile reset confirm` repairs it, `io_failed` means the STORE WOULD NOT OPEN and the
    //      remedy is a DEVICE one. ⛔ Collapsing any two tells the operator to retype four presets the flash still
    //      holds, or to discard four intact ones because a mount failed.
    const UiJoinList absent = absent_list();
    const UiJoinList inval  = refused_list(mrfw::ProfileErr::store_invalid);
    const UiJoinList iofail = refused_list(mrfw::ProfileErr::store_io_failed);
    UiJoinList empty_ok = ok_list();                    // a VALID record with four empty slots
    UiJoinList full     = ok_list();
    full.rec.prof[0] = prof(4, 9, 869462500u, 125000u, "hut");
    UiJoinList unserved{};                              // ⛔ no seam at all — a partially-wired build

    CHECK(strcmp(mrui::join_store_head(absent), "NO PROFILES") == 0);
    CHECK(strcmp(mrui::join_store_detail(absent), "") == 0);
    CHECK(strcmp(mrui::join_store_head(inval), "PROFILE STORE") == 0);
    CHECK(strcmp(mrui::join_store_detail(inval), "INVALID") == 0);
    CHECK(strcmp(mrui::join_store_head(iofail), "STORAGE FAILURE") == 0);
    CHECK(strcmp(mrui::join_store_detail(iofail), "CHECK faults") == 0);
    CHECK(strcmp(mrui::join_store_head(unserved), "NO JOIN SERVICE") == 0);
    // ★ A VALID-BUT-EMPTY record is `NO PROFILES` too — the same answer `joinprofile list` gives, and ⛔ NOT the
    //   corrupt one: two different facts, one honest operator-facing word.
    CHECK(strcmp(mrui::join_store_head(empty_ok), "NO PROFILES") == 0);
    CHECK(mrui::join_list_count(empty_ok) == 0);
    // ...and an ordinary populated store says NOTHING, so the note rows belong to the list.
    CHECK(strcmp(mrui::join_store_head(full), "") == 0);
    CHECK(strcmp(mrui::join_store_detail(full), "") == 0);
    CHECK(mrui::join_list_count(full) == 1);

    // ⛔ THE FOUR FAULT TEXTS ARE FOUR DIFFERENT STRINGS — walked pairwise, because a collapse is exactly what a
    //    reader tidying "two unreadable states" would produce.
    const UiJoinList all[] = { absent, inval, iofail, unserved };
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            char a[48], b[48];
            snprintf(a, sizeof a, "%s|%s", mrui::join_store_head(all[i]), mrui::join_store_detail(all[i]));
            snprintf(b, sizeof b, "%s|%s", mrui::join_store_head(all[j]), mrui::join_store_detail(all[j]));
            CHECK((strcmp(a, b) == 0) == (i == j));
        }
        // ...and every one of them fits the rail's 19-column body.
        CHECK(strlen(mrui::join_store_head(all[i])) <= 19u);
        CHECK(strlen(mrui::join_store_detail(all[i])) <= 19u);
    }
    // ★ THE RULED SENTENCE IS INTACT ACROSS ITS TWO ROWS: plan §3's `PROFILE STORE INVALID` is 21 columns against a
    //   19-column body, so §7.1 rule 5 splits it — ⛔ neither half may be reworded.
    char joined[48];
    snprintf(joined, sizeof joined, "%s %s", mrui::join_store_head(inval), mrui::join_store_detail(inval));
    CHECK(strcmp(joined, "PROFILE STORE INVALID") == 0);
}

TEST_CASE("ui15-join-store only PRESENT slots are rows, and a non-`ok` store offers BACK and nothing else") {
    UiJoinList l = ok_list();
    l.rec.prof[0] = prof(4, 9, 869462500u, 125000u, "hut");
    l.rec.prof[2] = prof(17, 7, 868000000u, 62500u);      // no name -> the `PROFILE 3` default
    JoinSelList rows = mrui::join_sel_rows(l);
    CHECK(rows.n == 3);
    JoinSelRow r{};
    CHECK(rows.at(0, r)); CHECK(r.back == false); CHECK(r.slot1 == 1);
    CHECK(rows.at(1, r)); CHECK(r.back == false); CHECK(r.slot1 == 3);   // ★ SLOT 3, not row index 1 (§B66)
    CHECK(rows.at(2, r)); CHECK(r.back == true);
    CHECK(rows.at(3, r) == false);                                       // ⛔ fails closed past the end
    // ⛔⛔ A STORE THAT COULD NOT BE READ OFFERS NO SLOT TO JOIN FROM — on all three fault states — and BACK survives
    //    every one of them, because leaving must never depend on a store.
    // ★★ AND THE RECORD IS DELIBERATELY LEFT HOLDING PRESETS IN EVERY ONE OF THEM. That is not a contrived fixture:
    //    device_nv.h's §nv-ritual warning is that a FAILED READ MAY STILL HAVE WRITTEN INTO ITS OUT PARAMETER, so
    //    the bytes behind a refused read can look like four perfectly good profiles. ⇒ the row list must be gated on
    //    the READ's answer and ⛔ never on what the buffer happens to contain. (A first version of this case left the
    //    fault records EMPTY, so the "gate on the verdict" mutation stayed GREEN — the loop found nothing anyway.)
    UiJoinList faults[] = { refused_list(mrfw::ProfileErr::store_invalid),
                            refused_list(mrfw::ProfileErr::store_io_failed),
                            absent_list(), UiJoinList{} };
    for (UiJoinList& f : faults) {
        for (uint8_t i = 0; i < mrnv::kJoinProfiles; ++i) f.rec.prof[i] = prof(4, 9, 869462500u, 125000u, "junk");
        CHECK(join_list_count(f) == mrnv::kJoinProfiles);            // ...the leftovers really are there
        const JoinSelList fr = mrui::join_sel_rows(f);
        CHECK(fr.n == 1);
        JoinSelRow b{};
        CHECK(fr.at(0, b));
        CHECK(b.back == true);
    }
    // ⛔ AND A `present == 0` SLOT IS NEVER A ROW, so a `double` can never land on an empty one.
    UiJoinList none = ok_list();
    CHECK(mrui::join_sel_rows(none).n == 1);
}

// ============================================================================================ the SCREENS' STRINGS
TEST_CASE("ui15-join-text the slot LABEL is the operator's name, or plan §11's `PROFILE n` default") {
    char out[mrui::kJoinLabelCap];
    mrui::join_row_label(out, sizeof out, prof(4, 9, 869462500u, 125000u, "hut"), 1);
    CHECK(strcmp(out, "hut") == 0);
    // ★ AN EMPTY NAME RENDERS THE SLOT'S OWN NUMBER — and the number is the SLOT (1-based), not the row index.
    mrui::join_row_label(out, sizeof out, prof(4, 9, 869462500u, 125000u), 3);
    CHECK(strcmp(out, "PROFILE 3") == 0);
    // ⛔⛔ THE STORED NAME IS NOT NUL-TERMINATED — `name_len` bounds it (device_nv.h says so at the field). A `%s`
    //    over `prof.name` would run straight into `freq_hz`; a FULL 12-byte label must come back as exactly 12 bytes.
    mrnv::JoinProfile p = prof(4, 9, 869462500u, 125000u, "ABCDEFGHIJKL");
    CHECK(p.name_len == 12);
    mrui::join_row_label(out, sizeof out, p, 2);
    CHECK(strcmp(out, "ABCDEFGHIJKL") == 0);
    CHECK(strlen(out) == 12u);
    // ⛔⛔ AND THE BYTES PAST `name_len` ARE NOT PART OF THE LABEL. `join_profile_put` zeroes the slot, but a record
    //    this build did not write — an older one, or a valid-magic record with wrong contents — need not have, and a
    //    `%s` would then print the garbage AND run past the field. ⇒ the copy is bounded by `name_len` and by
    //    nothing else. (A first version of this case used a FULL 12-byte name, where a `%s` truncated to the same
    //    answer and the mutation stayed GREEN.)
    mrnv::JoinProfile dirty = prof(4, 9, 869462500u, 125000u, "hut");
    for (size_t k = dirty.name_len; k < sizeof dirty.name; ++k) dirty.name[k] = 'X';
    mrui::join_row_label(out, sizeof out, dirty, 1);
    CHECK(strcmp(out, "hut") == 0);
    CHECK(strlen(out) == 3u);
    // ⛔ AND A RECORD CAN HOLD ANYTHING: a `name_len` past the field is CLAMPED, never trusted.
    p.name_len = 200;
    mrui::join_row_label(out, sizeof out, p, 2);
    CHECK(strlen(out) <= 12u);
    // ...every label fits the rail's 19-column body, marker included.
    CHECK(1u + strlen(out) <= 19u);
}

TEST_CASE("ui15-join-text the CONFIRM screen shows all four values, and 869.4625 MHz renders EXACTLY") {
    // ★★★ THE VALUE NO INTEGRAL kHz CAN HOLD, which is the whole reason the record is in Hz (plan §3): 869.4625 MHz
    //     is 869462.5 kHz, and a `uint32_t` kHz field would render — and fly — 869.462.
    char frq[mrui::kJoinFreqLineCap], phy[mrui::kJoinPhyLineCap];
    mrui::join_fmt_freq(frq, sizeof frq, prof(4, 9, 869462500u, 125000u));
    CHECK(strcmp(frq, "869.4625 MHz") == 0);
    mrui::join_fmt_phy(phy, sizeof phy, prof(4, 9, 869462500u, 125000u));
    CHECK(strcmp(phy, "L4 SF9 BW125.00") == 0);
    // ★ THE FULL LAYER BYTE IS SHOWN, ⛔ never the nibble: what the operator confirms is what the record will hold.
    mrui::join_fmt_phy(phy, sizeof phy, prof(17, 12, 868000000u, 62500u));
    CHECK(strcmp(phy, "L17 SF12 BW62.50") == 0);          // ...and a FRACTIONAL bandwidth is a real LoRa one
    mrui::join_fmt_freq(frq, sizeof frq, prof(17, 12, 868000000u, 62500u));
    CHECK(strcmp(frq, "868.0000 MHz") == 0);
    // ⛔ THE WIDEST REACHABLE EXPANSION STILL FITS THE 19-COLUMN BODY (the domains are `firmware_config_parse.h`'s:
    //    layer 1..255, SF 5..12, BW 7..500 kHz, freq 100..1000 MHz).
    mrui::join_fmt_phy(phy, sizeof phy, prof(255, 12, 1000000000u, 500000u));
    CHECK(strcmp(phy, "L255 SF12 BW500.00") == 0);
    CHECK(strlen(phy) <= 19u);
    mrui::join_fmt_freq(frq, sizeof frq, prof(255, 12, 1000000000u, 500000u));
    CHECK(strcmp(frq, "1000.0000 MHz") == 0);
    CHECK(strlen(frq) <= 19u);
    // The confirmation's two actions, by IDENTITY, with the safe one first.
    CHECK(strcmp(mrui::join_confirm_label(false), "BACK") == 0);
    CHECK(strcmp(mrui::join_confirm_label(true), "JOIN") == 0);
    CHECK(1u + strlen(mrui::join_confirm_label(true)) <= 19u);
}

TEST_CASE("B247 a corrupt stored PHY renders one bounded invalid-profile state, never truncated raw fields") {
    // ★ `/mrjoin`'s record header can be valid while one slot's contents are not. Each corrupt fixture moves ONE
    //   field outside the transaction's own domain; every other field remains the ordinary known-good profile.
    mrnv::JoinProfile bad[] = {
        prof(0,   9, 869462500u, 125000u),       // layer below 1 (uint8_t cannot represent above 255)
        prof(4,   4, 869462500u, 125000u),       // SF below 5
        prof(4,  13, 869462500u, 125000u),       // SF above 12
        prof(4,   9,  99999999u, 125000u),       // frequency below 100 MHz
        prof(4,   9, 1000000001u, 125000u),      // frequency above 1000 MHz
        prof(4,   9, 869462500u, 6999u),         // bandwidth below 7 kHz
        prof(4,   9, 869462500u, 500001u),       // bandwidth above 500 kHz
        prof(4,   9, 869462500u, UINT32_MAX),    // the exact raw-type width that provoked B247's warning
    };
    for (const mrnv::JoinProfile& p : bad) {
        CHECK(mrui::join_profile_phy_valid(p) == false);
        char phy[mrui::kJoinPhyLineCap];
        char frq[mrui::kJoinFreqLineCap];
        memset(phy, 'X', sizeof phy);
        memset(frq, 'X', sizeof frq);
        mrui::join_fmt_phy(phy, sizeof phy, p);
        mrui::join_fmt_freq(frq, sizeof frq, p);
        CHECK(strcmp(phy, mrui::kJoinInvalidProfile) == 0);
        CHECK(strlen(phy) == 15u);
        CHECK(frq[0] == '\0');                         // one explicit row, not the same warning twice
    }

    // The copy itself remains safe for any caller capacity: terminator inside the cap, sentinel beyond untouched.
    char tiny[5] = {'X', 'X', 'X', 'X', 'S'};
    mrui::join_fmt_phy(tiny, 4, bad[0]);
    CHECK(strcmp(tiny, "PRO") == 0);
    CHECK(tiny[3] == '\0');
    CHECK(tiny[4] == 'S');
}

TEST_CASE("B247 the exact lower and upper stored-PHY boundaries remain valid and byte-exact") {
    const mrnv::JoinProfile edge[] = {
        prof(1,   5,  100000000u,   7000u),
        prof(255, 12, 1000000000u, 500000u),
    };
    const char* const want_phy[] = {"L1 SF5 BW7.00", "L255 SF12 BW500.00"};
    const char* const want_frq[] = {"100.0000 MHz",  "1000.0000 MHz"};
    for (size_t i = 0; i < 2; ++i) {
        CHECK(mrui::join_profile_phy_valid(edge[i]) == true);
        char phy[mrui::kJoinPhyLineCap], frq[mrui::kJoinFreqLineCap];
        mrui::join_fmt_phy(phy, sizeof phy, edge[i]);
        mrui::join_fmt_freq(frq, sizeof frq, edge[i]);
        CHECK(strcmp(phy, want_phy[i]) == 0);
        CHECK(strcmp(frq, want_frq[i]) == 0);
        CHECK(strlen(phy) <= 19u);
        CHECK(strlen(frq) <= 19u);
    }
}

TEST_CASE("ui15-join-text 60 s is a WORD CHANGE — `STILL JOINING`, ⛔ never a failure, and never `JOINED`") {
    // ★★★ PLAN §2.3 RULE 5: normal adoption is ~23 s, one conflict/retry reaches ~53 s, and retries are NOT finitely
    //     bounded — so a deadline that declared failure would LIE about an operation still in progress.
    CHECK(strcmp(mrui::join_wait_head(false), "JOINING") == 0);
    CHECK(strcmp(mrui::join_wait_head(true), "STILL JOINING") == 0);
    CHECK(mrui::kJoinStillMs == 60000u);
    // ⛔⛔ NEITHER WORD IS A CLAIM OF SUCCESS: `JOINED` is not a prefix, a suffix or a substring of either, so no
    //    panel on this path can read as "you are in" before a correlated adopt.
    CHECK(strstr(mrui::join_wait_head(false), "JOINED") == nullptr);
    CHECK(strstr(mrui::join_wait_head(true), "JOINED") == nullptr);
    CHECK(strlen(mrui::join_wait_head(true)) <= 19u);
    // The result's second row once an adopt HAS landed: plan §2.3 rule 2's *"the resulting node id"*.
    char n[mrui::kJoinNodeLineCap];
    mrui::join_fmt_node(n, sizeof n, 42);
    CHECK(strcmp(n, "node 42") == 0);
    mrui::join_fmt_node(n, sizeof n, 255);
    CHECK(strcmp(n, "node 255") == 0);
    CHECK(strlen(n) <= 19u);
}
