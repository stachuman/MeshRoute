// MeshRoute — test_data_type_namespace.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-A — THE DATA-NAMESPACE TRANSITION (design
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` §5, §6, §18.1).
// This TU is the executable half of the namespace contract:
//   · §18.1.1  static assertions pin every new numeric value AND both range boundaries;
//   · §18.1.2  boundary cases at 0x00, 0x01, 0x7F, 0x80, 0xBF, 0xC0, 0xFD, 0xFE, 0xFF;
//   · §18.1.3  the range predicate is the EXACT bounded form — the `t & 0x80` degradation is a live mutation;
//   · §6       the ONE trait authority, checked against an INDEPENDENTLY WRITTEN expectation over all 256 values;
//   · §18.1.5  every live type round-trips pack_data/parse_data AT ITS NEW VALUE;
//   · §18.1.6  `protocol::wire_version` is unchanged, with a control that fails if it is bumped.
//
// ⛔⛔ THE TRAIT AUTHORITY LANDS WITHOUT A CONSUMER, ON PURPOSE (design §17-A / C1). Nothing in production calls
//     `data_type_traits()` yet; replacing the duplicated DM-floor lists and adding the fail-closed unknown-internal
//     arm is Slice B's BEHAVIOUR change, deliberately kept out of this slice so the corpus movement here is
//     attributable to the renumbering alone. ⇒ these cases are the authority's only current consumer, and that is
//     why the expectation below is written out longhand rather than by calling the function under test.
//
// ⚠ THE EXPECTED TRAIT TABLE IS A SECOND, INDEPENDENT IMPLEMENTATION. `expected_traits()` re-derives every row
//   from the DESIGN's rules (§5.1 ranges, §6.2, §6.4, §7.1) instead of re-using `data_type_traits()`'s switch. If it
//   simply mirrored the production switch it would agree with any mutation of it and prove nothing.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "frame_codec.h"
#include "inbox.h"                 // inbox_rec_type_tombstone — the 0xFE NON-WIRE store marker
#include "protocol_constants.h"    // protocol::wire_version — the §18.1.6 control

#include <cstdint>
#include <span>

using namespace meshroute;

// =====================================================================================================
// §18.1.1 — STATIC ASSERTIONS: every new value + both range boundaries
// =====================================================================================================
// These are COMPILE-TIME, deliberately: a value that moved must break the BUILD, not merely a run. The
// design calls the assignments wire contract (§5.2), so they get the strongest available pin.

// the range boundaries themselves (§5.1)
static_assert(data_type_app_lo      == 0x01, "application range base moved");
static_assert(data_type_app_hi      == 0x7F, "application range top moved");
static_assert(data_type_internal_lo == 0x80, "protocol-internal range base moved");
static_assert(data_type_internal_hi == 0xBF, "protocol-internal range top moved");

// ⛔⛔ THE PREDICATE'S OWN BOUNDARIES ARE DELIBERATELY **RUNTIME** CHECKS (§CUSTODY-A/1 below), NOT
//     static_asserts, AND THE REASON IS THE INSTRUMENT: §18.1.3 requires the `t & 0x80` degradation to be a RED
//     mutation, and a `static_assert` turns it into a COMPILE failure instead — which the mutation harness
//     classifies as "unusable" (the suite never ran), i.e. an entry that measures nothing. The bounds
//     THEMSELVES stay static above, where nothing can degrade them silently; the predicate's behaviour over all
//     256 values is asserted at runtime, where the battery can prove the assertion bites.
//     ⓘ Same trade, opposite direction, for the VALUES: they ARE static_asserted (§18.1.1 requires it), so
//       reverting one to its old ordinal is caught by the COMPILER rather than by the battery — the stronger
//       outcome, and the reason no `sliceA*` entry reverts an enum member (see the harness's own note).

// the application block (§5.2)
static_assert(DATA_TYPE_INTRO                          == 0x01, "INTRO");
static_assert(DATA_TYPE_MOBILE_SEND                    == 0x02, "MOBILE_SEND");
static_assert(DATA_TYPE_SEALED_RELAY                   == 0x03, "SEALED_RELAY");
static_assert(DATA_TYPE_CHANNEL_POST                   == 0x04, "CHANNEL_POST");
static_assert(DATA_TYPE_APP_MESSAGE                    == 0x05, "APP_MESSAGE (reserved, unimplemented)");
// the internal block (§5.2)
static_assert(DATA_TYPE_E2E_ACK                        == 0x80, "E2E_ACK");
static_assert(DATA_TYPE_H_ANSWER                       == 0x88, "H_ANSWER");
static_assert(DATA_TYPE_AUTHORITATIVE_H_ANSWER         == 0x89, "AUTHORITATIVE_H_ANSWER");
static_assert(DATA_TYPE_H_ANSWER_PUBKEY                == 0x8A, "H_ANSWER_PUBKEY");
static_assert(DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY  == 0x8B, "AUTHORITATIVE_H_ANSWER_PUBKEY (the B59 type)");
static_assert(DATA_TYPE_MOBILE_H_ANSWER                == 0x90, "MOBILE_H_ANSWER");
static_assert(DATA_TYPE_MOBILE_BREADCRUMB              == 0x91, "MOBILE_BREADCRUMB");
static_assert(DATA_TYPE_MOBILE_LAYER_QUERY             == 0x92, "MOBILE_LAYER_QUERY");
static_assert(DATA_TYPE_MOBILE_LAYER_ANSWER            == 0x93, "MOBILE_LAYER_ANSWER");
static_assert(DATA_TYPE_MOBILE_PUBKEY_PUSH             == 0x94, "MOBILE_PUBKEY_PUSH (retired)");
static_assert(DATA_TYPE_MOBILE_H_ANSWER_PUBKEY         == 0x95, "MOBILE_H_ANSWER_PUBKEY");
static_assert(DATA_TYPE_MOBILE_KEY_FORWARD             == 0x96, "MOBILE_KEY_FORWARD");
static_assert(DATA_TYPE_REMOTE_CMD                     == 0xA0, "REMOTE_CMD");
static_assert(DATA_TYPE_REMOTE_RESP                    == 0xA1, "REMOTE_RESP");
static_assert(DATA_TYPE_TEAM_KEY_GRANT                 == 0xA2, "TEAM_KEY_GRANT");

// ⛔ THE TOMBSTONE COLLIDES WITH NOTHING, and this is now a STRUCTURAL fact rather than luck: 0xFE is above the
//    internal range's top, so no DataType can ever reach it without first leaving the namespace.
static_assert(inbox_rec_type_tombstone > data_type_internal_hi,
              "the inbox tombstone must sit ABOVE the whole DataType namespace");

namespace {

// Every LIVE (produced and/or consumed) type, at its new value. ⛔ Deliberately NOT the whole enum: the two
// allocated-but-dead members and the reservation are listed separately below, because "allocated" and "live"
// are different claims and conflating them is what let a retired type keep a present-tense comment for months.
struct Row { uint8_t value; const char* name; };
const Row kLiveTypes[] = {
    { DATA_TYPE_INTRO,                          "INTRO" },
    { DATA_TYPE_MOBILE_SEND,                    "MOBILE_SEND" },
    { DATA_TYPE_SEALED_RELAY,                   "SEALED_RELAY" },
    { DATA_TYPE_CHANNEL_POST,                   "CHANNEL_POST (enclosed marker)" },
    { DATA_TYPE_E2E_ACK,                        "E2E_ACK" },
    { DATA_TYPE_CUSTODY_FAILURE,                "CUSTODY_FAILURE" },   // ★ live as of §CUSTODY-F
    { DATA_TYPE_H_ANSWER,                       "H_ANSWER" },
    { DATA_TYPE_AUTHORITATIVE_H_ANSWER,         "AUTHORITATIVE_H_ANSWER" },
    { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY,  "AUTHORITATIVE_H_ANSWER_PUBKEY" },
    { DATA_TYPE_MOBILE_H_ANSWER,                "MOBILE_H_ANSWER" },
    { DATA_TYPE_MOBILE_BREADCRUMB,              "MOBILE_BREADCRUMB" },
    { DATA_TYPE_MOBILE_LAYER_QUERY,             "MOBILE_LAYER_QUERY" },
    { DATA_TYPE_MOBILE_LAYER_ANSWER,            "MOBILE_LAYER_ANSWER" },
    { DATA_TYPE_MOBILE_H_ANSWER_PUBKEY,         "MOBILE_H_ANSWER_PUBKEY" },
    { DATA_TYPE_MOBILE_KEY_FORWARD,             "MOBILE_KEY_FORWARD" },
    { DATA_TYPE_REMOTE_CMD,                     "REMOTE_CMD" },
    { DATA_TYPE_REMOTE_RESP,                    "REMOTE_RESP" },
    { DATA_TYPE_TEAM_KEY_GRANT,                 "TEAM_KEY_GRANT" },
};

// The KNOWN internal set (design §6, the Slice-A truth table's "known internal" row): allocated, internal, and
// understood — so `known = true`. ⛔ 0x8A and 0x94 are NOT here: a reserved number and a retired one are not
// knowledge, and the design makes them take the internal range's UNKNOWN behaviour.
bool is_known_internal(uint8_t t) {
    switch (t) {
        case DATA_TYPE_E2E_ACK:
        // ★ ADDED 2026-08-31 BY §CUSTODY-F: 0x81 gained a PRODUCER and §9's defined meaning, so it is now a
        //   KNOWN internal type. ⛔ It is deliberately NOT added to `persistent_outcome` below — see there.
        case DATA_TYPE_CUSTODY_FAILURE:
        case DATA_TYPE_H_ANSWER:
        case DATA_TYPE_AUTHORITATIVE_H_ANSWER:
        case DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY:
        case DATA_TYPE_MOBILE_H_ANSWER:
        case DATA_TYPE_MOBILE_BREADCRUMB:
        case DATA_TYPE_MOBILE_LAYER_QUERY:
        case DATA_TYPE_MOBILE_LAYER_ANSWER:
        case DATA_TYPE_MOBILE_H_ANSWER_PUBKEY:
        case DATA_TYPE_MOBILE_KEY_FORWARD:
        case DATA_TYPE_REMOTE_CMD:
        case DATA_TYPE_REMOTE_RESP:
        case DATA_TYPE_TEAM_KEY_GRANT:
            return true;
        default:
            return false;
    }
}

// The KNOWN application set: 0x00 (the untyped DM) and the four real envelopes/markers. ⛔ 0x05 APP_MESSAGE is
// NOT here — reserving a number is not knowing the type (design §5.2 "not implemented here").
bool is_known_application(uint8_t t) {
    return t == 0x00 || t == DATA_TYPE_INTRO || t == DATA_TYPE_MOBILE_SEND
        || t == DATA_TYPE_SEALED_RELAY || t == DATA_TYPE_CHANNEL_POST;
}

// ★★ THE INDEPENDENT EXPECTATION. Written from the DESIGN's rules, not from `data_type_traits()`'s switch:
//    range first, then the two known-sets, then the single persistent-outcome membership. A mutation of the
//    production switch cannot be absorbed here, because nothing here reads it.
DataTypeTraits expected_traits(uint8_t t) {
    DataTypeTraits e{};
    e.internal = (t >= 0x80 && t <= 0xBF);                       // §5.1, spelled out again on purpose
    const bool application_range = (t >= 0x01 && t <= 0x7F);
    const bool untyped_dm        = (t == 0x00);
    e.known = is_known_internal(t) || is_known_application(t);
    // §6.4: the application range (and the untyped DM) carries logical user intent and keeps the user's send
    // lifecycle, whether or not this build knows the specific type. §6.2(5): internal never emits the generic
    // family. Everything outside both ranges is not valid for origination at all (§5.1) — it bears nothing.
    e.application_bearing    = untyped_dm || application_range;
    e.generic_send_lifecycle = untyped_dm || application_range;
    // §7.1: the opt-in persistent set is EXACTLY {E2E_ACK} — STILL, at Slice F.
    // ⛔⛔ CORRECTED IN PLACE 2026-08-31. It read *"CUSTODY_FAILURE joins it when the custody-codec slice
    //    allocates 0x81 — it must NOT be anticipated here"*. §CUSTODY-F allocated 0x81 and DID NOT join the set:
    //    F adds the producer and the codec, G adds the storing consumer, and the trait describes actual durable
    //    writing rather than an intention to write. ⇒ **SLICE G is the row that moves.**
    e.persistent_outcome = (t == DATA_TYPE_E2E_ACK);
    return e;
}

bool same(const DataTypeTraits& a, const DataTypeTraits& b) {
    return a.known == b.known && a.internal == b.internal
        && a.application_bearing == b.application_bearing
        && a.generic_send_lifecycle == b.generic_send_lifecycle
        && a.persistent_outcome == b.persistent_outcome;
}

}  // namespace

// =====================================================================================================
// §18.1.2/§18.1.3 — THE RANGE PREDICATE AT ITS BOUNDARIES
// =====================================================================================================

TEST_CASE("§CUSTODY-A/1 the internal-range predicate is exactly [0x80,0xBF] at every named boundary") {
    // The spec's boundary list, verbatim (§18.1.2), each with its intended verdict.
    struct B { uint8_t v; bool internal; bool application; const char* what; };
    const B cases[] = {
        { 0x00, false, false, "the ordinary untyped DM — outside BOTH ranges" },
        { 0x01, false, true,  "application range base" },
        { 0x7F, false, true,  "application range top" },
        { 0x80, true,  false, "internal range base" },
        { 0xBF, true,  false, "internal range top" },
        { 0xC0, false, false, "reserved range base — ⛔ `t & 0x80` would call this internal" },
        { 0xFD, false, false, "reserved range top — ⛔ `t & 0x80` would call this internal" },
        { 0xFE, false, false, "the inbox-store tombstone — NEVER a wire DataType" },
        { 0xFF, false, false, "invalid/reserved" },
    };
    for (const auto& c : cases) {
        CAPTURE(c.what);
        CHECK(data_type_is_internal(c.v)    == c.internal);
        CHECK(data_type_is_application(c.v) == c.application);
    }
    // ★ AND THE WHOLE 256-VALUE SPACE AGREES WITH THE CLOSED BOUNDS — the boundary list above is a summary,
    //   this is the exhaustive statement. It is also what makes the `t & 0x80` mutation unmissable: that form
    //   turns 64 of these into false positives (0xC0..0xFF), not one.
    for (unsigned v = 0; v <= 0xFF; ++v) {
        CAPTURE(v);
        const uint8_t t   = static_cast<uint8_t>(v);
        const bool want_i = (t >= 0x80 && t <= 0xBF);
        const bool want_a = (t >= 0x01 && t <= 0x7F);
        const bool got_i  = data_type_is_internal(t);
        const bool got_a  = data_type_is_application(t);
        const bool both   = got_i && got_a;
        CHECK(got_i == want_i);
        CHECK(got_a == want_a);
        CHECK_FALSE(both);                                                     // never both
    }
}

// =====================================================================================================
// §6 — THE ONE TRAIT AUTHORITY, EXHAUSTIVELY
// =====================================================================================================

TEST_CASE("§CUSTODY-A/2 data_type_traits matches the design's table for all 256 values") {
    for (unsigned v = 0; v <= 0xFF; ++v) {
        const uint8_t t = static_cast<uint8_t>(v);
        CAPTURE(v);
        CHECK(same(data_type_traits(t), expected_traits(t)));
    }
}

// The brief's truth table, row by row, spelled out so a reader can audit the authority WITHOUT running the
// exhaustive comparison above — and so a failure names WHICH row moved instead of "value 0x8A".
TEST_CASE("§CUSTODY-A/3 the Slice-A trait truth table, row by row") {
    struct Row5 { uint8_t v; bool known, internal, app_bearing, generic, persistent; const char* label; };
    const Row5 rows[] = {
        { 0x00,                                   true,  false, true,  true,  false, "ordinary DM" },
        { DATA_TYPE_INTRO,                        true,  false, true,  true,  false, "0x01 INTRO" },
        { DATA_TYPE_MOBILE_SEND,                  true,  false, true,  true,  false, "0x02 MOBILE_SEND" },
        { DATA_TYPE_SEALED_RELAY,                 true,  false, true,  true,  false, "0x03 SEALED_RELAY" },
        { DATA_TYPE_CHANNEL_POST,                 true,  false, true,  true,  false, "0x04 CHANNEL_POST" },
        // ★ the reservation is NOT knowledge — it takes the application range's unknown behaviour
        { DATA_TYPE_APP_MESSAGE,                  false, false, true,  true,  false, "0x05 APP_MESSAGE (reserved)" },
        // ★ the only persistent_outcome at Slice A
        { DATA_TYPE_E2E_ACK,                      true,  true,  false, false, true,  "0x80 E2E_ACK" },
        { DATA_TYPE_H_ANSWER,                     true,  true,  false, false, false, "0x88 H_ANSWER" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER,       true,  true,  false, false, false, "0x89 AUTH_H_ANSWER" },
        // ★ reserved, never emitted -> known = false, internal = true
        { DATA_TYPE_H_ANSWER_PUBKEY,              false, true,  false, false, false, "0x8A (type 4's heir, reserved)" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY,true,  true,  false, false, false, "0x8B AUTH_H_ANSWER_PUBKEY" },
        { DATA_TYPE_MOBILE_H_ANSWER,              true,  true,  false, false, false, "0x90 MOBILE_H_ANSWER" },
        { DATA_TYPE_MOBILE_BREADCRUMB,            true,  true,  false, false, false, "0x91 BREADCRUMB" },
        { DATA_TYPE_MOBILE_LAYER_QUERY,           true,  true,  false, false, false, "0x92 LAYER_QUERY" },
        { DATA_TYPE_MOBILE_LAYER_ANSWER,          true,  true,  false, false, false, "0x93 LAYER_ANSWER" },
        // ★ RETIRED, zero producers/consumers -> known = false
        { DATA_TYPE_MOBILE_PUBKEY_PUSH,           false, true,  false, false, false, "0x94 (type 12's heir, RETIRED)" },
        { DATA_TYPE_MOBILE_H_ANSWER_PUBKEY,       true,  true,  false, false, false, "0x95 MOBILE_H_ANSWER_PUBKEY" },
        { DATA_TYPE_MOBILE_KEY_FORWARD,           true,  true,  false, false, false, "0x96 KEY_FORWARD" },
        { DATA_TYPE_REMOTE_CMD,                   true,  true,  false, false, false, "0xA0 REMOTE_CMD" },
        { DATA_TYPE_REMOTE_RESP,                  true,  true,  false, false, false, "0xA1 REMOTE_RESP" },
        { DATA_TYPE_TEAM_KEY_GRANT,               true,  true,  false, false, false, "0xA2 TEAM_KEY_GRANT" },
        // unknown application range
        { 0x06,                                   false, false, true,  true,  false, "0x06 unallocated application" },
        { 0x0F,                                   false, false, true,  true,  false, "0x0F (ordinal 15's old slot)" },
        { 0x7F,                                   false, false, true,  true,  false, "0x7F application top" },
        // ★ 0x81 is ALLOCATED as of §CUSTODY-F — known, internal, NOT application-bearing, NO generic
        //   lifecycle, and ⛔ `persistent_outcome` STILL FALSE (Slice G flips it).
        { DATA_TYPE_CUSTODY_FAILURE,              true,  true,  false, false, false, "0x81 CUSTODY_FAILURE" },
        // unknown internal range
        { 0x82,                                   false, true,  false, false, false, "0x82 unallocated internal" },
        { 0x87,                                   false, true,  false, false, false, "0x87 unallocated internal" },
        { 0xBF,                                   false, true,  false, false, false, "0xBF internal top" },
        // outside both ranges
        { 0xC0,                                   false, false, false, false, false, "0xC0 reserved" },
        { 0xFD,                                   false, false, false, false, false, "0xFD reserved" },
        { 0xFE,                                   false, false, false, false, false, "0xFE tombstone (never wire)" },
        { 0xFF,                                   false, false, false, false, false, "0xFF invalid" },
    };
    for (const auto& r : rows) {
        CAPTURE(r.label);
        const DataTypeTraits tr = data_type_traits(r.v);
        CHECK(tr.known                  == r.known);
        CHECK(tr.internal               == r.internal);
        CHECK(tr.application_bearing    == r.app_bearing);
        CHECK(tr.generic_send_lifecycle == r.generic);
        CHECK(tr.persistent_outcome     == r.persistent);
    }
}

TEST_CASE("§CUSTODY-A/4 persistent_outcome membership is EXACTLY {E2E_ACK} at Slice A") {
    unsigned n = 0;
    for (unsigned v = 0; v <= 0xFF; ++v) {
        const uint8_t t = static_cast<uint8_t>(v);
        if (data_type_traits(t).persistent_outcome) { ++n; CHECK(t == DATA_TYPE_E2E_ACK); }
    }
    CHECK(n == 1);
    // ★★ THIS IS THE CASE §CUSTODY-A SAID WOULD MOVE, AND IT MOVED HALFWAY — which is the whole ruling.
    //    0x81 is now ALLOCATED and KNOWN (F gave it a producer), and it is STILL NOT PERSISTENT: F writes
    //    nothing durable, so the trait would be describing a store that does not exist. ⇒ Slice G flips the
    //    persistence half and this assertion is what will redden when it does.
    CHECK_FALSE(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).persistent_outcome);
    CHECK(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).known);
    CHECK(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).internal);
    CHECK_FALSE(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).generic_send_lifecycle);
    CHECK_FALSE(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).application_bearing);
}

// =====================================================================================================
// §18.1.5 — EVERY LIVE TYPE ROUND-TRIPS pack_data/parse_data AT ITS NEW VALUE
// =====================================================================================================

// ⓘ THIS IS THE ONLY PROOF FOR FOUR OF THEM ([[B267]]/[[A0-F15]]): H_ANSWER (0x88), MOBILE_H_ANSWER_PUBKEY
//   (0x95), MOBILE_KEY_FORWARD (0x96) and TEAM_KEY_GRANT (0xA2) have ZERO corpus reach, so no scenario can
//   witness their renumbering. Neither can the app-range reservation 0x05. Native is their whole gate.
TEST_CASE("§CUSTODY-A/5 every live DATA type round-trips pack_data/parse_data at its NEW value") {
    const uint8_t body[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    for (const auto& r : kLiveTypes) {
        CAPTURE(r.name);
        data_in in{};
        in.addr_len = 0; in.flags = 0; in.type = r.value; in.next = 2; in.dst = 3;
        in.hops_remaining = 12; in.committed_hops = 1; in.prev_fwd_rt_hops = 4; in.ctr = 0xBEEF;
        in.inner = std::span<const uint8_t>(body, sizeof body);
        uint8_t buf[64];
        const size_t n = pack_data(in, std::span<uint8_t>(buf, sizeof buf));
        CHECK(n == DATA_HDR_LEN + 1 + sizeof body + DATA_MAC_LEN);
        if (!n) continue;
        CHECK((buf[1] & DATA_FLAG_APP) != 0);          // APP is DERIVED from a non-zero type
        CHECK(buf[DATA_HDR_LEN] == r.value);           // ★ the NEW value really is the byte at offset 8
        const auto out = parse_data(std::span<const uint8_t>(buf, n));
        CHECK(out.has_value());
        if (!out) continue;
        CHECK(out->app);
        CHECK(out->type == r.value);
        CHECK(out->inner_off == DATA_HDR_LEN + 1);
        CHECK(out->inner_len == sizeof body);
        // the one derived convenience the codec computes from the type — it followed the value, not a literal
        const bool want_ack = (r.value == static_cast<uint8_t>(DATA_TYPE_E2E_ACK));
        CHECK(out->e2e_is_ack == want_ack);
    }
    // the two allocated-but-not-emitted members and the reservation pack and parse identically: they are
    // numbers in the same space, and nothing in the codec knows or cares that no producer exists.
    for (uint8_t t : { uint8_t{DATA_TYPE_H_ANSWER_PUBKEY}, uint8_t{DATA_TYPE_MOBILE_PUBKEY_PUSH},
                       uint8_t{DATA_TYPE_APP_MESSAGE} }) {
        CAPTURE(static_cast<int>(t));
        data_in in{};
        in.addr_len = 0; in.flags = 0; in.type = t; in.next = 2; in.dst = 3;
        in.hops_remaining = 12; in.ctr = 1;
        in.inner = std::span<const uint8_t>(body, sizeof body);
        uint8_t buf[64];
        const size_t n = pack_data(in, std::span<uint8_t>(buf, sizeof buf));
        CHECK(n > 0);
        if (!n) continue;
        CHECK(buf[DATA_HDR_LEN] == t);
        const auto out = parse_data(std::span<const uint8_t>(buf, n));
        CHECK(out.has_value());
        if (out) CHECK(out->type == t);
    }
}

// =====================================================================================================
// §18.1.6 — THE wire_version CONTROL
// =====================================================================================================

// ★★★ THE OWNER RULING, PINNED IN CODE (design §5.3). The DATA-namespace transition changes what the TYPE
//     byte MEANS on every typed frame, and the reflex is to bump `wire_version` to fence it off. The ruling is
//     that it stays EXACTLY where it is, and the reason is not compatibility — it is ATTRIBUTION: a bump
//     re-anchors all 36 corpus streams at once (C4/M3), so bundling one with this renumbering would make both
//     unmeasurable. The fleet reflashes together instead (M3: MeshRoute is unshipped, so that is free).
// ⛔ IF YOU ARE HERE BECAUSE THIS FAILED: do NOT re-anchor it to make a build pass. A `wire_version` bump gets
//    its OWN slice and its OWN commit, and it needs a fresh owner ruling — this control existing IS that rule.
static_assert(protocol::wire_version == 1,
              "§CUSTODY-A: protocol::wire_version must NOT be bumped by the DATA-namespace transition "
              "(design §5.3, owner ruling). A bump re-anchors all 36 corpus streams and destroys this "
              "slice's attribution — give it its own slice.");

TEST_CASE("§CUSTODY-A/6 protocol::wire_version is UNCHANGED by the namespace transition") {
    CHECK(protocol::wire_version == 1);
    // ★ the runtime half of the same claim, so the control is visible in a test run and not only at compile
    //   time — the static_assert above is what actually stops a bump from ever linking.
    CHECK(static_cast<unsigned>(protocol::wire_version) <= 15);   // it is a 4-bit beacon field (protocol_constants.h)
}
