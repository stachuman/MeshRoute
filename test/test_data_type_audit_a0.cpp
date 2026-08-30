// MeshRoute — test_data_type_audit_a0.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §A0 — DATA-path CHARACTERIZATION (spec `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`
// §17 "Slice A0" + §18.0). These cases assert the DATA surface **AS IT IS TODAY**, before the namespace transition
// renumbers it. They are deliberately NOT aspirational: several of them pin behaviour the design intends to CHANGE
// (Slice B's fail-closed unknown-internal handling, Slice B's trait-driven DM floor). Their purpose is that the
// change becomes a MEASURED delta rather than an unnoticed one — and that a later review does not reopen any of
// these from intuition. Every claim names its authority in-line.
//
// ⛔ A0 CHANGES NO PRODUCTION BEHAVIOUR (C1). Nothing here is reachable from firmware; it only observes. ★ AND THAT
//    CONSTRAINT SHAPED THE FILE: this TU uses the PUBLIC `Node` API ONLY — no `friend` declaration was added to
//    `node.h`, because that would have been a production edit. The receive-path cases therefore drive the REAL
//    RTS/CTS/DATA/ACK wire exchange between two real nodes (`on_recv` + `on_timer`, both public) rather than poking
//    a `PostAck` in through a white-box seam. That is slower to set up and strictly more faithful.
//
// ⚠ THE CASES MARKED `[CHANGES IN SLICE x]` ARE EXPECTED TO GO RED WHEN THAT SLICE LANDS. That is their job: they
//   are the tripwire that makes the transition attributable. Re-anchor them IN the slice that changes the
//   behaviour, with the movement stated — never by deleting them.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only,
//     never REQUIRE.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"
#include "inbox.h"              // inbox_rec_type_tombstone — the 0xFE NON-WIRE store marker
#include "protocol_constants.h"
#include "support/test_hal.h"

#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// Timer ids, mirrored TU-locally exactly as `test_node_r3.cpp:131` does (they are private constants on Node).
constexpr uint32_t kQueueWakeupTimerId  = 8;    // -> become_free()          (node.cpp:1322)
constexpr uint32_t kPostAckTimerId      = 9;    // -> do_post_ack()          (node.cpp:1323)
constexpr uint32_t kCtsToDataGapTimerId = 7;    // -> do_data_tx()          (node.h:1337)

struct TxFrame { std::string label; std::vector<uint8_t> bytes; };

class A0Hal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    std::vector<TxFrame>     tx_frames;
    void emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind ? kind : ""); }
    int  count(const char* k) const { int c = 0; for (const auto& e : emits) if (e == k) ++c; return c; }
    void clear_emits() { emits.clear(); }
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        TxFrame f; f.label = p.label ? p.label : ""; f.bytes.assign(b, b + n);
        tx_frames.push_back(std::move(f));
        return TxResult::ok;
    }
    // A non-degenerate entropy stream: the all-zero default makes e2e_seal_inner refuse (R7 bad-RNG guard).
    void rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(0xA5u ^ (i * 31u));
    }
    size_t label_count(const char* label) const {
        size_t c = 0; for (const auto& f : tx_frames) if (f.label == label) ++c; return c;
    }
    std::vector<uint8_t> last(const char* label) const {
        for (auto it = tx_frames.rbegin(); it != tx_frames.rend(); ++it) if (it->label == label) return it->bytes;
        return {};
    }
};

LayerConfig a0_layer(uint8_t layer_id, uint8_t sf) {
    LayerConfig L; L.layer_id = layer_id; L.routing_sf = sf;
    L.allowed_sf_bitmap = static_cast<uint16_t>(1u << sf);
    return L;
}

// Two real nodes with mutual 1-hop routes, wired only through the public API.
struct Pair {
    A0Hal shal, rhal;
    Node  sender{shal,  /*id=*/1, /*key=*/0x11111111u};
    Node  receiver{rhal, /*id=*/2, /*key=*/0x22222222u};
    uint64_t now = 100000;
    Pair() {
        NodeConfig cfg; cfg.n_layers = 1; cfg.layers[0] = a0_layer(/*layer_id=*/1, /*sf=*/8);
        cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
        CHECK(sender.on_init(cfg));
        CHECK(receiver.on_init(cfg));
        sender.test_learn_route(/*dest=*/2, /*via=*/2, /*hops=*/1, /*snr_q4=*/40, /*team_plane=*/false);
        receiver.test_learn_route(/*dest=*/1, /*via=*/1, /*hops=*/1, /*snr_q4=*/40, /*team_plane=*/false);
        shal._now = rhal._now = now;
        drain_pushes(sender); drain_pushes(receiver);
        shal.clear_emits(); rhal.clear_emits();
    }
    static void drain_pushes(Node& n) { Push d{}; while (n.next_push(d)) {} }
    void step() { shal._now = rhal._now = ++now; }
};

// Result of driving one complete hop across the real MAC.
struct Hop { bool got_data = false; uint8_t data_type = 0; bool e2e_ack_req = false; };

// Did this node ORIGINATE an E2E ACK? `send_e2e_ack` reaches `enqueue_data` with the tx_event string
// `"e2e_ack_tx"` (node_mac.cpp:775), and `enqueue_data` emits that name at :406. ⓘ We assert the ORIGINATION,
// not a returning DATA frame: the ack is a new flight, so only its RTS has aired by the time `do_post_ack`
// returns — scanning for a `DATA_TYPE_E2E_ACK` frame here would be a false negative (measured: it was).
int e2e_acks_originated(const A0Hal& hal) { return hal.count("e2e_ack_tx"); }

// Drive the RTS -> CTS -> DATA -> ACK exchange, then run the receiver's post-ACK deliver. Entirely public API:
// `on_recv` and `on_timer` are both part of Node's public surface (node.h). Modelled on the established driver in
// `test_node_r3.cpp:371` (U1 — same sequence, same frame labels).
//
// `or_data_flags` ORs bits into the DATA frame's byte-1 flags AFTER the sender aired it and BEFORE the receiver
// sees it. ★ THIS IS NOT A BACK DOOR AND IT IS NOT A PRODUCTION SEAM — it is a test-side edit of bytes already
// captured off the HAL, i.e. exactly what a foreign or hostile node can put on the air. It is the only way to
// reach the type × flag combination `Node`'s public surface cannot originate (`test_do_send_typed` hard-codes
// `flags = 0`; the `on_command` path that carries flags hard-codes `type = 0`), and adding a seam that could
// would be the production edit C1 forbids.
// ⓘ THE FORGERY IS ACCEPTED BY THE RECEIVER, and that is a measured property of the identity design rather than
//   an accident: a PLAINTEXT flight's identity is `rts_flight_identity_plain(origin, ctr)` (dm_crypto.cpp:57) —
//   origin and ctr only. `E2E_ACK_REQ` does not touch `d.crypted`, so the domain stays `plaintext`, the width is
//   unchanged, `data_mac_len` stays 4 so no offset moves, and `payload_len` is explicitly NOT part of the
//   identity (node_mac_rx.cpp:1109). ⇒ the frame still matches `_pending_rx->id` at :1116-1117.
Hop drive_hop(Pair& p, uint8_t or_data_flags = 0) {
    Hop out{};
    const std::vector<uint8_t> rts_wire = p.shal.last("RTS");
    CHECK_FALSE(rts_wire.empty());
    if (rts_wire.empty()) return out;

    const size_t cts0 = p.rhal.label_count("CTS");
    p.step();
    p.receiver.on_recv(rts_wire.data(), rts_wire.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    CHECK(p.rhal.label_count("CTS") == cts0 + 1);
    const std::vector<uint8_t> cts_wire = p.rhal.last("CTS");
    if (cts_wire.empty()) return out;

    p.step();
    p.sender.on_recv(cts_wire.data(), cts_wire.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});

    const size_t data0 = p.shal.label_count("DATA");
    p.step();
    p.sender.on_timer(kCtsToDataGapTimerId);
    CHECK(p.shal.label_count("DATA") == data0 + 1);
    std::vector<uint8_t> data_wire = p.shal.last("DATA");
    if (data_wire.empty()) return out;
    if (or_data_flags) data_wire[1] = static_cast<uint8_t>(data_wire[1] | or_data_flags);   // byte 1 = the flags
    const auto parsed = parse_data(std::span<const uint8_t>(data_wire.data(), data_wire.size()));
    CHECK(parsed.has_value());
    if (parsed) { out.got_data = true; out.data_type = parsed->type; out.e2e_ack_req = parsed->e2e_ack_req; }

    p.step();
    p.receiver.on_recv(data_wire.data(), data_wire.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    p.step();
    p.receiver.on_timer(kPostAckTimerId);        // -> do_post_ack: the addressed-consumer if-chain
    return out;
}

// Did this node hand the app an ordinary inbound MESSAGE?
bool delivered_as_message(Node& n, Push& out) {
    Push pu{};
    while (n.next_push(pu)) if (pu.kind == PushKind::msg_recv) { out = pu; return true; }
    return false;
}

// Every DataType the enum allocates today, in numeric order.
// ⓘ RE-ANCHORED 2026-08-29 (§CUSTODY-A): re-sorted into the NEW numeric order and DATA_TYPE_APP_MESSAGE
//   (0x05) added — it is an allocated member now, even though it is a pure reservation with no behaviour.
struct TypeRow { uint8_t value; const char* name; };
const TypeRow kAllocatedTypes[] = {
    { DATA_TYPE_INTRO,                          "INTRO" },
    { DATA_TYPE_MOBILE_SEND,                    "MOBILE_SEND" },
    { DATA_TYPE_SEALED_RELAY,                   "SEALED_RELAY" },
    { DATA_TYPE_CHANNEL_POST,                   "CHANNEL_POST" },
    { DATA_TYPE_APP_MESSAGE,                    "APP_MESSAGE (reserved)" },
    { DATA_TYPE_E2E_ACK,                        "E2E_ACK" },
    { DATA_TYPE_H_ANSWER,                       "H_ANSWER" },
    { DATA_TYPE_AUTHORITATIVE_H_ANSWER,         "AUTHORITATIVE_H_ANSWER" },
    { DATA_TYPE_H_ANSWER_PUBKEY,                "H_ANSWER_PUBKEY (reserved)" },
    { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY,  "AUTHORITATIVE_H_ANSWER_PUBKEY" },
    { DATA_TYPE_MOBILE_H_ANSWER,                "MOBILE_H_ANSWER" },
    { DATA_TYPE_MOBILE_BREADCRUMB,              "MOBILE_BREADCRUMB" },
    { DATA_TYPE_MOBILE_LAYER_QUERY,             "MOBILE_LAYER_QUERY" },
    { DATA_TYPE_MOBILE_LAYER_ANSWER,            "MOBILE_LAYER_ANSWER" },
    { DATA_TYPE_MOBILE_PUBKEY_PUSH,             "MOBILE_PUBKEY_PUSH (retired)" },
    { DATA_TYPE_MOBILE_H_ANSWER_PUBKEY,         "MOBILE_H_ANSWER_PUBKEY" },
    { DATA_TYPE_MOBILE_KEY_FORWARD,             "MOBILE_KEY_FORWARD" },
    { DATA_TYPE_REMOTE_CMD,                     "REMOTE_CMD" },
    { DATA_TYPE_REMOTE_RESP,                    "REMOTE_RESP" },
    { DATA_TYPE_TEAM_KEY_GRANT,                 "TEAM_KEY_GRANT" },
};
constexpr size_t kAllocatedTypeCount = sizeof(kAllocatedTypes) / sizeof(kAllocatedTypes[0]);

}  // namespace

// =====================================================================================================
// §A0-1 — THE NUMERIC CONTRACT (RE-ANCHORED BY §CUSTODY-A, 2026-08-29)
// =====================================================================================================

// AUTHORITY: lib/core/frame_codec.h (the `DataType` enum itself).
// ⛔⛔ THIS CASE WAS MARKED `[CHANGES IN SLICE A]` AND IT DID — it is RE-ANCHORED here, not deleted, and the
//     movement is recorded so the transition stays attributable. It used to assert "contiguous 1..19";
//     numbers are now a RANGE contract (design §5.1/§5.2), so contiguity is deliberately GONE and the gaps
//     inside 0x80..0xBF are the point. The complete old -> new landing, every member accounted:
//         INTRO                        15 -> 0x01     E2E_ACK                        3 -> 0x80
//         MOBILE_SEND                  14 -> 0x02     H_ANSWER                       1 -> 0x88
//         SEALED_RELAY                 17 -> 0x03     AUTHORITATIVE_H_ANSWER         2 -> 0x89
//         CHANNEL_POST                 18 -> 0x04     H_ANSWER_PUBKEY                4 -> 0x8A
//         APP_MESSAGE              (new) -> 0x05     AUTH_H_ANSWER_PUBKEY           5 -> 0x8B
//                                                     MOBILE_H_ANSWER                8 -> 0x90
//                                                     MOBILE_BREADCRUMB              9 -> 0x91
//                                                     MOBILE_LAYER_QUERY            10 -> 0x92
//                                                     MOBILE_LAYER_ANSWER           11 -> 0x93
//                                                     MOBILE_PUBKEY_PUSH            12 -> 0x94
//                                                     MOBILE_H_ANSWER_PUBKEY        13 -> 0x95
//                                                     MOBILE_KEY_FORWARD            16 -> 0x96
//                                                     REMOTE_CMD                     6 -> 0xA0
//                                                     REMOTE_RESP                    7 -> 0xA1
//                                                     TEAM_KEY_GRANT                19 -> 0xA2
//     ⛔ 0x81 (CUSTODY_FAILURE) is NOT allocated here — the custody-codec slice owns its addition.
// ⓘ The exhaustive value/boundary/trait pinning lives in `test/test_data_type_namespace.cpp`; what THIS case
//   keeps is the A0-side claim it always made — no duplicate assignment, no member outside its declared range,
//   and a per-member pin — so a member silently sliding into the wrong block is caught from both files.
TEST_CASE("§A0-1 every allocated DataType sits in its declared range, with no duplicate assignment") {
    CHECK(kAllocatedTypeCount == 20);          // 19 pre-transition members + the APP_MESSAGE reservation
    bool seen[256] = {};
    for (const auto& r : kAllocatedTypes) {
        CAPTURE(r.name);
        CHECK_FALSE(seen[r.value]);            // no duplicate numeric assignment
        seen[r.value] = true;
        // every member is in EXACTLY one of the two live ranges — never 0x00, never 0xC0..0xFF
        const bool app = data_type_is_application(r.value);
        const bool internal = data_type_is_internal(r.value);
        CHECK((app != internal));              // exactly one
        CHECK((app || internal));
    }
    // the application block, in order
    CHECK(static_cast<uint8_t>(DATA_TYPE_INTRO)                          == 0x01);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_SEND)                    == 0x02);
    CHECK(static_cast<uint8_t>(DATA_TYPE_SEALED_RELAY)                   == 0x03);
    CHECK(static_cast<uint8_t>(DATA_TYPE_CHANNEL_POST)                   == 0x04);
    CHECK(static_cast<uint8_t>(DATA_TYPE_APP_MESSAGE)                    == 0x05);
    // the internal block, in order
    CHECK(static_cast<uint8_t>(DATA_TYPE_E2E_ACK)                        == 0x80);
    CHECK(static_cast<uint8_t>(DATA_TYPE_H_ANSWER)                       == 0x88);
    CHECK(static_cast<uint8_t>(DATA_TYPE_AUTHORITATIVE_H_ANSWER)         == 0x89);
    CHECK(static_cast<uint8_t>(DATA_TYPE_H_ANSWER_PUBKEY)                == 0x8A);
    CHECK(static_cast<uint8_t>(DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY)  == 0x8B);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_H_ANSWER)                == 0x90);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_BREADCRUMB)              == 0x91);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_LAYER_QUERY)             == 0x92);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_LAYER_ANSWER)            == 0x93);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_PUBKEY_PUSH)             == 0x94);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_H_ANSWER_PUBKEY)         == 0x95);
    CHECK(static_cast<uint8_t>(DATA_TYPE_MOBILE_KEY_FORWARD)             == 0x96);
    CHECK(static_cast<uint8_t>(DATA_TYPE_REMOTE_CMD)                     == 0xA0);
    CHECK(static_cast<uint8_t>(DATA_TYPE_REMOTE_RESP)                    == 0xA1);
    CHECK(static_cast<uint8_t>(DATA_TYPE_TEAM_KEY_GRANT)                 == 0xA2);
}

// AUTHORITY: lib/core/inbox.h:64 — `inbox_rec_type_tombstone = 0xFE` is a STORE marker, explicitly "NOT a
// DataType" (inbox.h:35). The matrix carries it as a NON-WIRE exclusion; this is the executable half.
TEST_CASE("§A0-1b the 0xFE tombstone is a store marker that collides with no allocated DataType") {
    for (const auto& r : kAllocatedTypes) CHECK(r.value != 0xFE);
    CHECK(static_cast<uint8_t>(inbox_rec_type_tombstone) == 0xFE);
    // ⓘ It is NOT protected on the wire: §A0-3 shows 0xFE packs and parses like any other type byte.
}

// =====================================================================================================
// §A0-2 — THE OWN-DM BURST FLOOR: THE EXEMPT SET IS EXACTLY THREE TYPES
// =====================================================================================================

// AUTHORITY: lib/core/node_mac.cpp:918-919 (`become_free`, the CHECK half) and :1147-1148 (`issue_send`, the
// STAMP half). Both spell the identical disjunction
//     (type == DATA_TYPE_E2E_ACK) || (type == DATA_TYPE_REMOTE_CMD) || (type == DATA_TYPE_REMOTE_RESP)
// by hand, with no shared symbol, helper or table binding them.
//
// ★★ THIS IS THE SPEC'S §6.2(4) TARGET. Slice B replaces both lists with the trait authority, after which the
//    exempt set becomes the whole 0x80..0xBF internal range. Until then the exempt set is THESE THREE and no
//    others — notably NOT the hash/key answers, and NOT the exact B59 pubkey-answer type.
// [CHANGES IN SLICE B for every currently-NON-exempt internal-to-be type.]
TEST_CASE("§A0-2 the own-DM burst floor exempts exactly {E2E_ACK, REMOTE_CMD, REMOTE_RESP} — nothing else") {
    struct Case { uint8_t type; bool exempt; const char* label; };
    const Case cases[] = {
        { DATA_TYPE_E2E_ACK,                       true,  "E2E_ACK" },
        { DATA_TYPE_REMOTE_CMD,                    true,  "REMOTE_CMD" },
        { DATA_TYPE_REMOTE_RESP,                   true,  "REMOTE_RESP" },
        { 0,                                       false, "untyped DM (the carrier the floor exists for)" },
        { DATA_TYPE_H_ANSWER,                      false, "H_ANSWER" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER,        false, "AUTHORITATIVE_H_ANSWER" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY, false, "AUTHORITATIVE_H_ANSWER_PUBKEY (the B59 type)" },
        { DATA_TYPE_MOBILE_H_ANSWER,               false, "MOBILE_H_ANSWER" },
        { DATA_TYPE_MOBILE_H_ANSWER_PUBKEY,        false, "MOBILE_H_ANSWER_PUBKEY" },
        { DATA_TYPE_MOBILE_KEY_FORWARD,            false, "MOBILE_KEY_FORWARD" },
        { DATA_TYPE_MOBILE_BREADCRUMB,             false, "MOBILE_BREADCRUMB" },
        { DATA_TYPE_MOBILE_LAYER_QUERY,            false, "MOBILE_LAYER_QUERY" },
        { DATA_TYPE_MOBILE_LAYER_ANSWER,           false, "MOBILE_LAYER_ANSWER" },
        { DATA_TYPE_INTRO,                         false, "INTRO" },
        { 200,                                     false, "unknown type 200" },
    };
    const uint8_t body[] = { 'a', 'b' };
    for (const auto& c : cases) {
        CAPTURE(c.label);
        Pair p;
        // Send #1: with the floor disarmed this is admitted and (if non-exempt) lays the _last_dm_origin_ms stamp
        // in issue_send (node_mac.cpp:1147-1150).
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, c.type) != 0);
        // Drop the live flight so become_free is free to pick again (public seam: resets _pending_tx).
        p.sender.test_suspend_tx_drain(false);
        p.shal.clear_emits();
        // Send #2, at the SAME instant: strictly inside dm_min_interval_ms of the stamp, if one was laid.
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, c.type) != 0);
        p.sender.on_timer(kQueueWakeupTimerId);        // -> become_free -> the floor CHECK
        if (c.exempt) {
            // No stamp was ever laid and the type is exempt anyway: nothing is deferred.
            CHECK(p.shal.count("send_blocked") == 0);
        } else {
            CHECK(p.shal.count("send_blocked") == 1);  // deferred in place (node_mac.cpp:923-928)
        }
    }
}

// AUTHORITY: lib/core/node_mac.cpp:1147-1150 — the STAMP half of the same policy, written out a SECOND time
// with no symbol shared with the check half. §A0-2 above cannot see this list on its own (it sends the same type
// twice, so a stamp-side-only error is masked by the matching check-side exemption). This case separates them:
// an EXEMPT origination must leave the floor UNARMED, so an ordinary DM issued immediately afterwards flies.
// ⇒ this is what makes a stamp-list mutation measurable rather than silent.
TEST_CASE("§A0-2b an EXEMPT origination lays no floor stamp — the next ordinary DM is not deferred by it") {
    const uint8_t body[] = { 'a', 'b' };
    {   // EXEMPT first: no stamp is laid, so the following untyped DM must NOT be blocked.
        Pair p;
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, DATA_TYPE_E2E_ACK) != 0);
        p.sender.test_suspend_tx_drain(false);
        p.shal.clear_emits();
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, /*type=*/0) != 0);
        p.sender.on_timer(kQueueWakeupTimerId);
        CHECK(p.shal.count("send_blocked") == 0);
    }
    {   // Positive control — a NON-exempt first send DOES lay the stamp, and the same second DM is deferred.
        Pair p;
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, /*type=*/0) != 0);
        p.sender.test_suspend_tx_drain(false);
        p.shal.clear_emits();
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, /*type=*/0) != 0);
        p.sender.on_timer(kQueueWakeupTimerId);
        CHECK(p.shal.count("send_blocked") == 1);
    }
}

// AUTHORITY: lib/core/node_mac.cpp:918-921 — the CHECK half's `!exempt_type` term, reached only once the floor
// is ARMED. ⛔ THIS CASE EXISTS BECAUSE §A0-2 CANNOT SEE THAT TERM AT ALL, and that was MEASURED, not foreseen:
// mutation A01 (drop REMOTE_CMD from the CHECK list) left the whole suite GREEN on the first battery run. The
// reason is structural — for an exempt type the STAMP half never arms the floor, so the CHECK half is never
// reached and its membership test is unobservable from a same-type pair.
// ⇒ the observable arrangement is a MIXED pair: an ordinary DM arms the floor, and the exempt type must then
//   still fly immediately. That is also the real-world shape the MF9 exemption exists for (a user DM followed by
//   this node's own e2e-ack, which must not wait 3 s behind it).
// [CHANGES IN SLICE B — the exempt set widens to the whole internal range.]
TEST_CASE("§A0-2c with the floor ARMED by an ordinary DM, an exempt type still flies — the CHECK-half term") {
    struct Case { uint8_t type; bool exempt; const char* label; };
    const Case cases[] = {
        { DATA_TYPE_E2E_ACK,                       true,  "E2E_ACK after an ordinary DM" },
        { DATA_TYPE_REMOTE_CMD,                    true,  "REMOTE_CMD after an ordinary DM" },
        { DATA_TYPE_REMOTE_RESP,                   true,  "REMOTE_RESP after an ordinary DM" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY, false, "the B59 pubkey answer after an ordinary DM" },
        { DATA_TYPE_H_ANSWER,                      false, "H_ANSWER after an ordinary DM" },
        { 0,                                       false, "a second ordinary DM (the control)" },
    };
    const uint8_t body[] = { 'a', 'b' };
    for (const auto& c : cases) {
        CAPTURE(c.label);
        Pair p;
        // Arm the floor with an ordinary user DM (non-exempt => issue_send lays the stamp).
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, /*type=*/0) != 0);
        p.sender.test_suspend_tx_drain(false);
        p.shal.clear_emits();
        // Now the type under test, at the SAME instant — strictly inside dm_min_interval_ms of the stamp.
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, c.type) != 0);
        p.sender.on_timer(kQueueWakeupTimerId);
        CHECK(p.shal.count("send_blocked") == (c.exempt ? 0 : 1));
    }
}

// =====================================================================================================
// §A0-3 — THE CODEC ACCEPTS EVERY TYPE BYTE
// =====================================================================================================

// AUTHORITY: lib/core/frame_codec.cpp:955-961 — parse_data's ONLY type-adjacent code copies frame[8] verbatim.
// There is NO range check, NO enum-membership check and NO reserved-value rejection anywhere in the codec. This
// is the structural reason §A0-4's fall-through is reachable at all.
TEST_CASE("§A0-3 pack/parse round-trip every allocated type AND every reserved/unknown boundary value") {
    const uint8_t body[] = { 0xAA, 0xBB, 0xCC };
    // ⓘ RE-ANCHORED 2026-08-29 (§CUSTODY-A): the probe VALUES are unchanged on purpose — what changed is what
    //   they mean, and the case's claim (the codec validates NOTHING) is exactly as true of the new namespace.
    //   The `e2e_is_ack` assertion below is symbolic, so it followed the value from 3 to 0x80 by itself.
    const uint8_t probes[] = { 1, 2, 3, 19,          // now UNALLOCATED application-range values (the old ordinals)
                               20, 100, 0x7F,        // unallocated, inside the application range
                               0x80, 0xA0, 0xBF,     // inside the protocol-internal range (0x80/0xA0 allocated, 0xBF not)
                               0xC0, 0xFD,           // inside the reserved range
                               0xFE,                 // the inbox tombstone value — NOT a DataType, yet it flies
                               0xFF };               // invalid/reserved — yet it flies
    for (uint8_t t : probes) {
        CAPTURE(static_cast<int>(t));
        data_in in{};
        in.addr_len = 0; in.flags = 0; in.type = t; in.next = 2; in.dst = 2;
        in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = 0x1234;
        in.inner = std::span<const uint8_t>(body, sizeof body);   // the body rides `data_in::inner`
        uint8_t buf[255];
        const size_t n = pack_data(in, std::span<uint8_t>(buf, sizeof buf));
        CHECK(n > 0);
        if (!n) continue;
        // pack_data DERIVES the APP bit from `type != 0` (frame_codec.cpp:913-915) and emits TYPE at byte 8.
        CHECK((buf[1] & DATA_FLAG_APP) != 0);
        CHECK(buf[DATA_HDR_LEN] == t);
        const auto out = parse_data(std::span<const uint8_t>(buf, n));
        CHECK(out.has_value());
        if (!out) continue;
        CHECK(out->app);
        CHECK(out->type == t);                    // accepted verbatim — no validation of any kind
        CHECK(out->e2e_is_ack == (t == DATA_TYPE_E2E_ACK));
    }
}

// AUTHORITY: frame_codec.h:741-742 asserts "0 is reserved/invalid (never on the wire — APP=0 means no TYPE
// byte)". That is true of OUR PACKER and is NOT enforced on receipt: parse_data trusts the APP bit and reads
// byte 8 unconditionally. Pinned as CURRENT behaviour; see the matrix findings ledger (A0-F6).
TEST_CASE("§A0-3b APP=1 with a 0x00 TYPE byte parses as type 0 — the packer's invariant is not a receive guard") {
    const uint8_t body[] = { 0x11, 0x22, 0x33 };
    data_in in{};
    in.addr_len = 0; in.flags = 0; in.type = DATA_TYPE_E2E_ACK; in.next = 2; in.dst = 2;
    in.hops_remaining = 31; in.ctr = 0x1234;
    in.inner = std::span<const uint8_t>(body, sizeof body);
    uint8_t buf[255];
    const size_t n = pack_data(in, std::span<uint8_t>(buf, sizeof buf));
    CHECK(n > 0);
    if (!n) return;
    buf[DATA_HDR_LEN] = 0x00;                    // forge: keep APP set, zero the TYPE byte
    const auto out = parse_data(std::span<const uint8_t>(buf, n));
    CHECK(out.has_value());
    if (!out) return;
    CHECK(out->app);                             // the flag still says "a TYPE byte is present"
    CHECK(out->type == 0);                       // ...and the value read is the reserved 0
    CHECK(out->inner_off == DATA_HDR_LEN + 1);   // ★ the inner still starts PAST it -> one body byte is eaten
}

// =====================================================================================================
// §A0-4 — THE ADDRESSED UNKNOWN-TYPE FALL-THROUGH (driven over the real wire)
// =====================================================================================================

// AUTHORITY: lib/core/node_mac_rx.cpp:1551-1934 — the addressed-consumer dispatch inside `do_post_ack` is a flat
// IF-CHAIN of early-returning guards with **no `else` / `default` arm**. "Unknown type" is not a decision
// anywhere in the code; it is the ABSENCE of a guard, so execution reaches the generic deliver tail at :1936
// (record_dm :1988, msg_recv push :2003).
//
// ⚠ -Wswitch IS STRUCTURALLY BLIND TO THIS: an if-chain has no exhaustiveness diagnostic, so a new DataType
//   member produces no warning anywhere. Registered as a quality-gate gap (A0-F2), not a claim about intent.
//
// ★★ THE SPEC'S §6.2(2) TARGET: Slice B makes an addressed unknown INTERNAL type fail closed. Today EVERY
//    unknown type — the internal range, the reserved range and the 0xFE tombstone value alike — is
//    delivered to the user as an ordinary DM. ⓘ The §CUSTODY-A namespace transition did NOT change that:
//    the ranges now exist as a contract, but no runtime arm reads them yet. [CHANGES IN SLICE B.]
TEST_CASE("§A0-4 an addressed UNKNOWN DATA type is delivered as an ordinary DM (msg_recv), not dropped") {
    struct Case { uint8_t type; const char* label; };
    const Case cases[] = {
        { 20,   "20 — the next unallocated value" },
        { 100,  "100 — mid application range" },
        // ⛔ RE-ANCHORED 2026-08-29 (§CUSTODY-A): `0x80` LEFT this list — it is DATA_TYPE_E2E_ACK now, so it is
        //    consumed by the ack arm and is covered by the §A0-4b control instead. `0x81` replaces it as the
        //    unallocated internal-range value nearest the base (it is the forward reservation for
        //    CUSTODY_FAILURE, which the custody-codec slice adds; until then it is genuinely unknown).
        { 0x81, "0x81 — the protocol-internal range, unallocated (CUSTODY_FAILURE's reservation)" },
        { 0xBF, "0xBF — the protocol-internal range top, unallocated" },
        { 0xC0, "0xC0 — the reserved range" },
        { 0xFE, "0xFE — the inbox tombstone value, on the wire" },
        { 0xFF, "0xFF — the invalid/reserved value" },
        // ★ Three ALLOCATED types with no addressed consumer take the SAME path (ledger A0-F7/A0-F8/A0-F4):
        { DATA_TYPE_H_ANSWER_PUBKEY,    "0x8A — allocated, RESERVED, never emitted, and never CONSUMED" },
        { DATA_TYPE_MOBILE_PUBKEY_PUSH, "0x94 — allocated but RETIRED; its handler was deleted" },
        { DATA_TYPE_APP_MESSAGE,        "0x05 — the application-range RESERVATION; no producer, no consumer" },
        // ⛔ ADDED 2026-08-29 (QG A0 review, matrix correction). CHANNEL_POST is the ENCLOSED marker read out of
        //    a MOBILE_SEND wrapper's BODY (node_channel.cpp:806 writes it as a body byte; node_mac_rx.cpp:1581
        //    reads it there). There is NO `pa.type == DATA_TYPE_CHANNEL_POST` arm anywhere, so an OUTER CHANNEL_POST DATA — which the enum
        //    calls "never a wire frame type" — has no consumer and lands here with the rest. ⇒ the enum's claim
        //    describes the origination set, NOT an enforced invariant. This row is the evidence.
        { DATA_TYPE_CHANNEL_POST,       "0x04 — the ENCLOSED-only marker, arriving as an OUTER type" },
    };
    const uint8_t body[] = { 'h', 'e', 'l', 'l', 'o' };
    for (const auto& c : cases) {
        CAPTURE(c.label);
        Pair p;
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, c.type) != 0);
        const Hop h = drive_hop(p);
        CHECK(h.got_data);
        CHECK(h.data_type == c.type);            // the unknown byte really did reach the air unchanged
        Push pu{};
        const bool as_msg = delivered_as_message(p.receiver, pu);
        CHECK(as_msg);                                    // reaches the app as a MESSAGE
        CHECK(p.rhal.count("delivered") == 1);            // and fires the generic deliver telemetry
        if (as_msg) {
            CHECK(pu.origin == 1);
            CHECK(pu.body_len == sizeof body);            // the raw body becomes the message text verbatim
        }
    }
}

// The contrasting positive control: a type that DOES have an addressed consumer returns early and is never
// delivered as a DM. Without this, §A0-4 could pass on a fixture that simply delivers everything.
// AUTHORITY: node_mac_rx.cpp:1830-1848 (E2E_ACK) and :1812-1828 (REMOTE_CMD/RESP), both `become_free(); return;`.
TEST_CASE("§A0-4b control — a CONSUMED type is NOT delivered as a DM (the fixture distinguishes the two)") {
    struct Case { uint8_t type; const char* label; };
    const Case cases[] = {
        { DATA_TYPE_E2E_ACK,     "0x80 — consumed by the E2E-ack arm, recorded as a receipt" },
        { DATA_TYPE_REMOTE_CMD,  "0xA0 — consumed by the remote-admin staging arm" },
        { DATA_TYPE_REMOTE_RESP, "0xA1 — consumed by the remote-admin staging arm" },
    };
    const uint8_t body[] = { 0x34, 0x12 };       // a 2-B LE ctr, valid for the E2E-ack arm
    for (const auto& c : cases) {
        CAPTURE(c.label);
        Pair p;
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, c.type) != 0);
        const Hop h = drive_hop(p);
        CHECK(h.got_data);
        Push pu{};
        CHECK_FALSE(delivered_as_message(p.receiver, pu));   // consumed, never a message
        CHECK(p.rhal.count("delivered") == 0);               // and the generic deliver never ran
    }
}

// AUTHORITY: node_mac_rx.cpp:1999-2002 — the E2E-ack reply is keyed on `DATA_FLAG_E2E_ACK_REQ` ALONE, downstream
// of the whole type dispatch. So an addressed UNKNOWN type that sets the flag is BOTH delivered as a message AND
// acknowledged: the fall-through is an amplification surface, not merely a mis-delivery. That is finding A0-F3,
// and this case is its executable evidence.
//
// ⛔⛔ CORRECTED 2026-08-29 (QG A0 review, blocker 1), AND THE CORRECTION IS THE POINT OF THE ENTRY:
//     this case previously carried THIS NAME while its fixture sent WITHOUT the flag and asserted only that no
//     ack appeared. It therefore proved the CONVERSE of its title and could not support A0-F3 at all — a name
//     writing a cheque the body did not cash. The positive arm below is now the real one; the old flagless arm is
//     RETAINED, correctly labelled, as the negative control that makes the FLAG (not the type) the deciding input.
// ⓘ The flag is forged into the DATA in flight (see `drive_hop`'s note) because no public seam can originate an
//   unknown TYPE together with `E2E_ACK_REQ`. That is a faithful model of the threat, not a weaker one: an
//   attacker does not use our `on_command` surface either.
TEST_CASE("§A0-4c an addressed UNKNOWN type carrying E2E_ACK_REQ is delivered AND earns an E2E-ack reply") {
    const uint8_t body[] = { 'x', 'y' };
    {   // ★ POSITIVE ARM — the amplification A0-F3 claims.
        Pair p;
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, /*type=*/200) != 0);
        const Hop h = drive_hop(p, /*or_data_flags=*/DATA_FLAG_E2E_ACK_REQ);
        CHECK(h.got_data);
        CHECK(h.data_type == 200);
        CHECK(h.e2e_ack_req);                                // the forged bit really is on the received frame
        Push pu{};
        CHECK(delivered_as_message(p.receiver, pu));         // (a) still delivered as an ordinary DM
        CHECK(p.rhal.count("delivered") == 1);
        CHECK(e2e_acks_originated(p.rhal) == 1);             // (b) AND the receiver originated an E2E ACK for it
    }
    {   // NEGATIVE CONTROL — the identical unknown type with the flag CLEAR earns no ack.
        Pair p;
        CHECK(p.sender.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                          /*dst_hash=*/0, /*type=*/200) != 0);
        const Hop h = drive_hop(p);                          // no forged flag
        CHECK(h.got_data);
        CHECK_FALSE(h.e2e_ack_req);
        Push pu{};
        CHECK(delivered_as_message(p.receiver, pu));         // delivered either way...
        CHECK(e2e_acks_originated(p.rhal) == 0);             // ...but NO ack: the flag is what decides
    }
}
