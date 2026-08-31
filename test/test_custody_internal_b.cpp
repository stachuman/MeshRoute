// MeshRoute — test_custody_internal_b.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-B — COMMON INTERNAL BEHAVIOUR (spec
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` §6.2/§6.3/§6.4, §18.2).
// The A0 file (`test_data_type_audit_a0.cpp`) holds the BEFORE record and its re-anchored AFTER; this file holds
// the behaviour A0 had no arm for at all: the fail-closed guard's BOUND, the three FORWARDING roles it must sit
// behind, the generic-lifecycle suppression and the things §6.3/§6.4 say must NOT move.
//
// ⛔ PUBLIC-API ONLY, deliberately, for the same reason A0 was: every receive case drives the REAL
//    RTS/CTS/DATA/ACK exchange between real nodes rather than poking a `PostAck` through a white-box seam.
//    ⇒ what is asserted here is what a peer can actually cause, not what a test can construct.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "support/test_hal.h"
#include "identity.h"
#include "firmware_ui_invite.h"

#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// Timer ids, mirrored TU-locally exactly as `test_node_r3.cpp:131` and `test_data_type_audit_a0.cpp:41` do.
constexpr uint32_t kQueueWakeupTimerId  = 8;
constexpr uint32_t kPostAckTimerId      = 9;
constexpr uint32_t kCtsToDataGapTimerId = 7;
constexpr uint32_t kRtsTimeoutTimerId   = 4;    // sender: CTS-wait  (node.h)
constexpr uint32_t kRetryBackoffTimerId = 10;   // sender: jittered RTS retry (node.h:1343)

// ★ `seq`/`tag` are captured because the [[B268]] cases drive a REAL TxDone: `DeviceHal` echoes exactly the
//   `TxParams` the sending site stamped, and a test that invented a `seq` would be testing its own arithmetic
//   rather than the node's flight identity.
struct BTxFrame { std::string label; std::vector<uint8_t> bytes; uint32_t seq = 0; uint16_t tag = 0; };

class BHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    std::vector<BTxFrame>    tx_frames;
    // ★ THE FIELD KEYS OF EVERY EMIT, kept so the guard's FIELD BOUND is testable. The ruled bound is not only
    //   "one event per flight" but "SCALAR-ONLY, never the body or anything derived from it" — and a test that
    //   only counts events cannot see a payload field being added.
    std::vector<std::pair<std::string, std::vector<std::string>>> emit_fields;
    void emit(const char* kind, const EventField* f, size_t n) override {
        emits.push_back(kind ? kind : "");
        std::vector<std::string> keys;
        for (size_t i = 0; i < n; ++i) keys.push_back(f[i].key ? f[i].key : "");
        emit_fields.push_back({ kind ? kind : "", keys });
    }
    std::vector<std::string> fields_of(const char* k) const {
        for (const auto& e : emit_fields) if (e.first == k) return e.second;
        return {};
    }
    int  count(const char* k) const { int c = 0; for (const auto& e : emits) if (e == k) ++c; return c; }
    void clear_emits() { emits.clear(); emit_fields.clear(); }
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        tx_frames.push_back(BTxFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n), p.seq, p.tag });
        return TxResult::ok;
    }
    void rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(0x5Au ^ (i * 17u));
    }
    size_t label_count(const char* label) const {
        size_t c = 0; for (const auto& f : tx_frames) if (f.label == label) ++c; return c;
    }
    std::vector<uint8_t> last(const char* label) const {
        for (auto it = tx_frames.rbegin(); it != tx_frames.rend(); ++it) if (it->label == label) return it->bytes;
        return {};
    }
    const BTxFrame* last_frame(const char* label) const {
        for (auto it = tx_frames.rbegin(); it != tx_frames.rend(); ++it) if (it->label == label) return &*it;
        return nullptr;
    }
};

LayerConfig b_layer(uint8_t layer_id, uint8_t sf) {
    LayerConfig L; L.layer_id = layer_id; L.routing_sf = sf;
    L.allowed_sf_bitmap = static_cast<uint16_t>(1u << sf);
    return L;
}
NodeConfig b_cfg() {
    NodeConfig cfg; cfg.n_layers = 1; cfg.layers[0] = b_layer(/*layer_id=*/1, /*sf=*/8);
    cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
    return cfg;
}
void drain(Node& n) { Push d{}; while (n.next_push(d)) {} }

// ---- a 3-node chain 1 -> 2 -> 3, so the SAME frame can be driven at a RELAY and at a DESTINATION -------------
// ⓘ The relay role and the destination role are the two arms of `do_post_ack`'s `if (!pa.is_forward)`; putting
//   them on one chain is what makes "the guard is on the destination arm only" a measurement rather than a claim.
struct Chain {
    BHal h1, h2, h3;
    Node n1{h1, /*id=*/1, 0x11111111u};
    Node n2{h2, /*id=*/2, 0x22222222u};
    Node n3{h3, /*id=*/3, 0x33333333u};
    uint64_t now = 100000;
    Chain() {
        NodeConfig cfg = b_cfg();
        CHECK(n1.on_init(cfg)); CHECK(n2.on_init(cfg)); CHECK(n3.on_init(cfg));
        n1.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        n1.test_learn_route(/*dest=*/3, /*via=*/2, 2, 40, false);   // 3 is reached THROUGH 2 -> 2 is a relay
        n2.test_learn_route(/*dest=*/1, /*via=*/1, 1, 40, false);
        n2.test_learn_route(/*dest=*/3, /*via=*/3, 1, 40, false);
        n3.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        n3.test_learn_route(/*dest=*/1, /*via=*/2, 2, 40, false);
        h1._now = h2._now = h3._now = now;
        drain(n1); drain(n2); drain(n3);
        h1.clear_emits(); h2.clear_emits(); h3.clear_emits();
    }
    void step() { h1._now = h2._now = h3._now = ++now; }
};

// One COMPLETE hop from `src` to `dst` over the real MAC: RTS -> CTS -> DATA -> ACK, then the receiver's
// post-ACK deliver. Returns the DATA bytes that aired.
// ⛔ THE ACK IS FED BACK, and that is load-bearing rather than tidiness: without it the sender stays
//    `awaiting_ack` with `_pending_tx` live, so it refuses the NEXT hop's RTS (half-duplex) and the following
//    exchange fails for a reason that has nothing to do with what is under test. Measured, not foreseen.
// `hold_dst_tx` suspends the RECEIVER's drain in the window between the ACK and `do_post_ack`, so a forward the
// deliver path enqueues STAYS in the queue where the case can read its TYPE. Suspending it any earlier makes the
// receiver refuse the incoming RTS (the same half-duplex guard) — also measured.
std::vector<uint8_t> hop(Chain& c, Node& src, BHal& shal, Node& dst, BHal& dhal, bool hold_dst_tx = false) {
    const std::vector<uint8_t> rts = shal.last("RTS");
    CHECK_FALSE(rts.empty());
    if (rts.empty()) return {};
    const size_t cts0 = dhal.label_count("CTS");
    c.step();
    dst.on_recv(rts.data(), rts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    CHECK(dhal.label_count("CTS") == cts0 + 1);
    const std::vector<uint8_t> cts = dhal.last("CTS");
    if (cts.empty()) return {};
    c.step();
    src.on_recv(cts.data(), cts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    const size_t data0 = shal.label_count("DATA");
    c.step();
    src.on_timer(kCtsToDataGapTimerId);
    CHECK(shal.label_count("DATA") == data0 + 1);
    const std::vector<uint8_t> data = shal.last("DATA");
    if (data.empty()) return {};
    c.step();
    dst.on_recv(data.data(), data.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    const std::vector<uint8_t> ack = dhal.last("ACK");        // the hop ACK the receiver just sent
    if (!ack.empty()) { c.step(); src.on_recv(ack.data(), ack.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)}); }
    if (hold_dst_tx) dst.test_suspend_tx_drain(true);
    c.step();
    dst.on_timer(kPostAckTimerId);
    return data;
}

bool got_msg(Node& n) { Push pu{}; while (n.next_push(pu)) if (pu.kind == PushKind::msg_recv) return true; return false; }

// ⓘ The representative unknown-internal value. `0x94` is the RETIRED `MOBILE_PUBKEY_PUSH` — A0-F10b's "harmless"
//   stray, whose body used to become 32 raw ed25519 key bytes of inbox TEXT. It is the slice's named case.
constexpr uint8_t kStray = DATA_TYPE_MOBILE_PUBKEY_PUSH;   // 0x94

}  // namespace

// =====================================================================================================
// §CUSTODY-B/1 — THE GUARD'S BOUND, FROM BOTH SIDES (the S0 ruling, 2026-08-30)
// =====================================================================================================

// AUTHORITY: lib/core/node_mac_rx.cpp — the fail-closed guard at the head of the delivery tail.
// ★★ THE OWNER'S RULING, VERBATIM, IS WHAT THIS CASE MEASURES:
//      "«Bounded» means fixed-size and non-amplifying — one scalar-only emit per dedup-admitted distinct flight
//       — not a wall-clock rate limit. A sender varying flight identity can still produce one event per
//       physically accepted exchange."
// ⇒ BOTH SIDES ARE PINNED, deliberately, because either alone is compatible with a wrong bound: a case that
//   only showed "N frames -> 1 event" is equally satisfied by a wall-clock limiter (which is NOT what was ruled
//   and would hide occurrences), and a case that only showed "N flights -> N events" cannot distinguish the
//   ruled bound from no bound at all.
TEST_CASE("§CUSTODY-B/1 the bound, side A: DISTINCT flights each earn exactly ONE unsupported_internal") {
    Chain c;
    const uint8_t body[] = { 'k', 'e', 'y' };
    for (int i = 1; i <= 4; ++i) {
        CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, kStray) != 0);
        (void)hop(c, c.n1, c.h1, c.n2, c.h2);
        c.n1.test_suspend_tx_drain(false);        // free the flight so the next origination can fly
        CHECK(c.h2.count("unsupported_internal") == i);   // ★ one per dedup-admitted distinct flight — no more, no less
        CHECK(c.h2.count("delivered") == 0);              // ⛔ and never the payload-carrying deliver emit
    }
    CHECK_FALSE(got_msg(c.n2));                            // ⛔ nothing reached the app across all four
}

TEST_CASE("§CUSTODY-B/1b the bound, side B: the SAME flight replayed is dedup-dropped, so it earns only ONE") {
    Chain c;
    const uint8_t body[] = { 'k', 'e', 'y' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, kStray) != 0);
    const std::vector<uint8_t> rts  = c.h1.last("RTS");
    const std::vector<uint8_t> data = hop(c, c.n1, c.h1, c.n2, c.h2);
    CHECK_FALSE(data.empty());
    CHECK(c.h2.count("unsupported_internal") == 1);
    // ★ REPLAY: the identical RTS + the identical DATA, exactly as a retransmitting (or hostile) sender would.
    // The per-frame dedup (`dup_drop`) is what bounds this — ⛔ NOT a counter, and ⛔ NOT the originator throttle
    // (which a hostile sender simply ignores). This is the arm that proves the bound is the RADIO plus dedup.
    for (int r = 0; r < 3; ++r) {
        c.step();
        c.n2.on_recv(rts.data(), rts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
        c.step();
        c.n2.on_recv(data.data(), data.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
        c.step();
        c.n2.on_timer(kPostAckTimerId);
    }
    CHECK(c.h2.count("unsupported_internal") == 1);        // ★ STILL one — the replays never reached the tail
    CHECK(c.h2.count("delivered") == 0);
    CHECK_FALSE(got_msg(c.n2));
}

// ★★★ THE PREDICATE CONTROL (spec §18.2 / the QG correction). `known` proves ALLOCATION, not that a handler RAN.
// `MOBILE_KEY_FORWARD` (0x96) is `known = true` AND its consuming fork is gated on `is_mobile` (node_mac_rx.cpp)
// — so on a STATIC receiver it arrives with nobody having handled it. A guard written `internal && !known` would
// wave it through and put 32 raw requester-key bytes in the inbox as text (A0-F11). This case is what makes that
// mutation RED, and it is the reason the predicate is "internal reached the tail", full stop.
TEST_CASE("§CUSTODY-B/1c a KNOWN-but-unwired internal type is dropped too — `internal && !known` would be wrong") {
    Chain c;
    CHECK(data_type_traits(DATA_TYPE_MOBILE_KEY_FORWARD).known);       // the premise, stated rather than assumed
    CHECK(data_type_traits(DATA_TYPE_MOBILE_KEY_FORWARD).internal);
    uint8_t key_body[33] = { 0 };
    for (int i = 0; i < 32; ++i) key_body[i] = static_cast<uint8_t>(0xE0 + i);   // stands in for the requester key
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, key_body, sizeof key_body, CryptIntent::off, /*dst_hash=*/0,
                                  DATA_TYPE_MOBILE_KEY_FORWARD) != 0);
    (void)hop(c, c.n1, c.h1, c.n2, c.h2);
    CHECK(c.h2.count("unsupported_internal") == 1);   // the STATIC receiver has no handler -> fail closed
    CHECK(c.h2.count("delivered") == 0);
    CHECK_FALSE(got_msg(c.n2));                        // ⛔ the key bytes did NOT become message text
}

// =====================================================================================================
// §CUSTODY-B/2 — THE THREE FORWARDING ROLES THE GUARD MUST SIT BEHIND (§6.2(3))
// =====================================================================================================

// ROLE (a): ORDINARY RELAY. Node 2 is not the destination, so `pa.is_forward` is true and the guard's whole
// branch is never entered. A relay is content-blind by design — it must move an unknown-internal frame on.
TEST_CASE("§CUSTODY-B/2a a RELAY forwards an unknown-internal frame — it never reaches the guard") {
    Chain c;
    const uint8_t body[] = { 'x', 'y' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, kStray) != 0);
    (void)hop(c, c.n1, c.h1, c.n2, c.h2, /*hold_dst_tx=*/true);
    CHECK(c.n2.test_tx_queue_n() == 1);                       // ★ FORWARDED, not eaten
    CHECK(c.n2.test_tx_type(0) == kStray);                    // ...with its TYPE byte intact
    CHECK(c.n2.test_tx_dst(0) == 3);
    CHECK(c.h2.count("unsupported_internal") == 0);           // ⛔ the relay passed no verdict on it
    CHECK_FALSE(got_msg(c.n2));
}

// ROLE (c): the HOSTED-MOBILE LAST MILE. ★ THIS IS THE ONE A GUARD PLACED AT THE TOP OF THE DESTINATION BRANCH
// WOULD EAT. The home IS the outer wire destination, but only as a PROXY: the inner DST_HASH names its hosted
// mobile, so the frame belongs to the mobile and the home must re-address and forward it. The mobile — not the
// home — is the node entitled to decide it has no handler.
TEST_CASE("§CUSTODY-B/2c a HOME last-miles an unknown-internal frame to its hosted mobile, never eats it") {
    Chain c;
    const uint32_t m_hash = 0xABCD1234u;
    uint8_t ed[32] = { 0 }; for (int i = 0; i < 32; ++i) ed[i] = static_cast<uint8_t>(i + 1);
    c.n2.test_add_host_mobile(m_hash, /*local_id=*/9, ed);    // node 2 hosts a mobile
    const uint8_t body[] = { 'q' };
    // Addressed to the HOME (dst=2) with the inner DST_HASH naming the MOBILE — the production last-mile shape.
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*override_dst_hash=*/m_hash,
                                  kStray) != 0);
    (void)hop(c, c.n1, c.h1, c.n2, c.h2, /*hold_dst_tx=*/true);
    CHECK(c.h2.count("mobile_lastmile_fwd") == 1);            // ★ the last-mile arm ran...
    CHECK(c.n2.test_tx_queue_n() == 1);                       // ...and produced a real forward
    CHECK(c.n2.test_tx_type(0) == kStray);                    // with the TYPE preserved for the mobile to judge
    CHECK(c.n2.test_tx_addr_len(0) == 1);                     // a 1-hop last-mile to the mobile's LOCAL id
    CHECK(c.h2.count("unsupported_internal") == 0);           // ⛔⛔ THE HOME DID NOT EAT ITS MOBILE'S FRAME
    CHECK(c.h2.count("delivered") == 0);
    CHECK_FALSE(got_msg(c.n2));
}

// =====================================================================================================
// §CUSTODY-B/3 — §6.2(5) SUPPRESSION, AND ITS BOUNDS (§6.2(6), §6.3, §6.4, and the [[B263]] fence)
// =====================================================================================================

// AUTHORITY: lib/core/node_cascade.cpp `giveup_flight` + `defer_send`.
// ★ THE PAIR IS THE POINT: the same terminal path, driven once with an INTERNAL carrier and once with an
//   APPLICATION carrier. Suppressing both would be an over-correction that deletes real user feedback; the
//   application arm is ALSO the [[B263]] fence.
// ⓘ CORRECTED IN PLACE 2026-08-31 BY §CUSTODY-E, not deleted: this line used to end "— it stays reproducibly as
//   it is until Slice E", and Slice E has now landed. What it fenced is UNCHANGED and this case still passes
//   untouched, because every carrier here is a LOCAL origination (`test_defer_send` builds `is_forward = false`).
//   What Slice E changed is the TRANSIT arm, which this file never drove — see `test_custody_terminal_e.cpp`
//   §CUSTODY-E/1, which drives a REAL relayed carrier and pins the suppression.
TEST_CASE("§CUSTODY-B/3 a deferred-send giveup reports send_failed for an APPLICATION carrier and not for an INTERNAL one") {
    struct Case { uint8_t type; bool expect_push; const char* label; };
    const Case cases[] = {
        { 0,                             true,  "untyped DM — the user's own send" },
        { DATA_TYPE_INTRO,               true,  "INTRO (0x01) — an application ENVELOPE keeps user semantics (§6.4)" },
        { DATA_TYPE_SEALED_RELAY,        true,  "SEALED_RELAY (0x03) — likewise" },
        // ⓘ MEASURED, AND STATED RATHER THAN HIDDEN: the RESERVED range answers `generic_send_lifecycle = false`
        //   too, because Slice A's landed table gives 0xC0..0xFF `{false,false,false,false,false}` — it is neither
        //   internal NOR application-bearing, and only application-bearing types own a user-send lifecycle. ⛔ This
        //   is UNREACHABLE in production (design §5.1: "0xC0..0xFD reserved; not valid for origination", and no
        //   origination path builds one), so it changes no shipped behaviour — but it is a real consequence of
        //   consuming the trait rather than testing `data_type_is_internal` directly, and it is recorded here.
        { 0xC0,                          false, "0xC0 — RESERVED, outside BOTH ranges (see the note)" },
        { DATA_TYPE_E2E_ACK,             false, "E2E_ACK (0x80)" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY, false, "the B59 pubkey answer (0x8B)" },
        { DATA_TYPE_MOBILE_LAYER_ANSWER, false, "MOBILE_LAYER_ANSWER (0x93)" },
        { DATA_TYPE_TEAM_KEY_GRANT,      false, "TEAM_KEY_GRANT (0xA2)" },
    };
    for (const auto& c : cases) {
        CAPTURE(c.label);
        BHal hal; Node n(hal, /*id=*/1, 0x11111111u);
        NodeConfig cfg = b_cfg();
        CHECK(n.on_init(cfg));
        drain(n);
        // Drive the defer-loop giveup directly (the redrain cap), which is `defer_send`'s own terminal arm.
        n.test_defer_send(/*dst=*/9, /*ctr=*/77, /*redrain_count=*/protocol::send_defer_max_redrains, c.type);
        CHECK(hal.count("send_deferred_giveup") == 1);       // ⛔ the LOUD refusal fires either way — §6.2(5) suppresses
                                                             //    the generic USER outcome, never the diagnosis
        bool pushed = false;
        Push pu{}; while (n.next_push(pu)) if (pu.kind == PushKind::send_failed) pushed = true;
        CHECK(pushed == c.expect_push);
    }
}

// §6.2(6) — the PROTOCOL-SPECIFIC results survive. This is the over-correction control in the other direction:
// a slice that suppressed these would have deleted the only outcome an internal exchange has.
TEST_CASE("§CUSTODY-B/3b receiving an E2E ACK still produces send_e2e_acked (§6.2(6) — nothing protocol-specific moved)") {
    Chain c;
    const uint8_t body[] = { 'h', 'i' };
    // A real -a send through the PUBLIC command surface (the only one that carries flags): node 1 asks for an
    // end-to-end ack, node 2 delivers the DM and originates the ack.
    Command cmd{}; cmd.kind = CmdKind::send; cmd.u.send.dst_id = 2; cmd.u.send.flags = DATA_FLAG_E2E_ACK_REQ;
    cmd.body = body; cmd.body_len = sizeof body;
    const CmdResult res = c.n1.on_command(cmd);
    CHECK(res.code == CmdCode::queued);
    const std::vector<uint8_t> data = hop(c, c.n1, c.h1, c.n2, c.h2);
    CHECK_FALSE(data.empty());
    CHECK(c.h2.count("e2e_ack_tx") == 1);                // node 2 ORIGINATED the ack (node_mac.cpp)
    // Fly the ack back to node 1 and prove the app-facing protocol-specific push still lands.
    // ⛔ NO drain poke here: `send_e2e_ack` -> `enqueue_data` -> `become_free` has ALREADY put the ack's RTS on
    //    the air by the time `do_post_ack` returns, so resetting `_pending_tx` would DESTROY the live flight and
    //    the DATA would never follow. (Measured: that is exactly what the first cut of this case did.)
    (void)hop(c, c.n2, c.h2, c.n1, c.h1);
    bool e2e_acked = false;
    Push pu{}; while (c.n1.next_push(pu)) if (pu.kind == PushKind::send_e2e_acked) e2e_acked = true;
    CHECK(e2e_acked);                                    // ★ the internal frame's OWN result is intact
    CHECK(c.h1.count("e2e_ack_rx") == 1);
    CHECK(c.h1.count("unsupported_internal") == 0);      // ⛔ and a WIRED internal type never meets the guard
}

// §6.2(8) — persistence is EXACT. A sweep, not a spot check: `persistent_outcome` must still name exactly one
// type at this slice, so nothing acquired durable storage as a side effect of the trait becoming load-bearing.
TEST_CASE("§CUSTODY-B/3c persistence stayed exact: E2E_ACK is the ONLY persistent_outcome type in 0..255") {
    int n = 0;
    for (int t = 0; t <= 255; ++t) {
        const uint8_t v = static_cast<uint8_t>(t);
        if (data_type_traits(v).persistent_outcome) { ++n; CHECK(v == DATA_TYPE_E2E_ACK); }
    }
    CHECK(n == 1);
    // ⛔ And the forward reservation has NOT quietly opted in ahead of the custody-codec slice.
    CHECK_FALSE(data_type_traits(0x81).persistent_outcome);
}

// §6.3 — the internal range buys the DM-floor exemption and NOTHING ELSE. The RTS backstop hint is the named
// example: only E2E ACK has it, and it must not have widened with the floor.
TEST_CASE("§CUSTODY-B/3d §6.3: only E2E_ACK marks its RTS with the backstop hint — the widening stopped at the floor") {
    struct Case { uint8_t type; bool hinted; };
    const Case cases[] = {
        { DATA_TYPE_E2E_ACK,                       true  },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY, false },
        { DATA_TYPE_MOBILE_LAYER_ANSWER,           false },
        { DATA_TYPE_H_ANSWER,                      false },
        { 0,                                       false },
    };
    for (const auto& c : cases) {
        CAPTURE(static_cast<int>(c.type));
        Chain ch;
        const uint8_t body[] = { 'a' };
        CHECK(ch.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, c.type) != 0);
        const std::vector<uint8_t> rts = ch.h1.last("RTS");
        CHECK_FALSE(rts.empty());
        if (rts.empty()) continue;
        const auto r = parse_rts(std::span<const uint8_t>(rts.data(), rts.size()));
        CHECK(r.has_value());
        if (r) CHECK(((r->rts_flags & RTS_FLAG_E2E_ACK) != 0) == c.hinted);
    }
}

// ★★★ THE FIELD BOUND, RULED 2026-08-30 AND CLOSED HERE. The guard's whole reason for existing is that raw key
//     material must not reach a text sink; an event that echoed the body would re-open exactly that on the
//     console and in the corpus NDJSON. ⇒ the field set is CLOSED at `{type, origin, dst, ctr}` and this case is
//     what makes "the telemetry bound removed" a mutation that can go RED.
TEST_CASE("§CUSTODY-B/1d the guard's telemetry is SCALAR-ONLY: exactly {type, origin, dst, ctr}, never the body") {
    Chain c;
    // A body of recognisable "key material", so a leak would be unmistakable in the failure output.
    uint8_t secret[32]; for (int i = 0; i < 32; ++i) secret[i] = static_cast<uint8_t>(0xD0 + i);
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, secret, sizeof secret, CryptIntent::off, /*dst_hash=*/0, kStray) != 0);
    (void)hop(c, c.n1, c.h1, c.n2, c.h2);
    CHECK(c.h2.count("unsupported_internal") == 1);
    const std::vector<std::string> f = c.h2.fields_of("unsupported_internal");
    CHECK(f.size() == 4);
    if (f.size() == 4) {
        CHECK(f[0] == "type"); CHECK(f[1] == "origin"); CHECK(f[2] == "dst"); CHECK(f[3] == "ctr");
    }
    // ⛔ AND THE CLOSED-SET CHECK, which is the half a positional test alone would miss: no field may be named
    //    anything body-shaped, whatever position it is added at.
    for (const auto& k : f) {
        CHECK(k != "payload"); CHECK(k != "body"); CHECK(k != "text"); CHECK(k != "len"); CHECK(k != "hex");
    }
}

// ★★★ THE TERMINAL-GIVEUP ARM of §6.2(5), driven through the REAL cascade rather than the defer queue: an
//     internal flight that exhausts its retries must NOT hand the app a `send_failed` under a ctr the user never
//     minted. ⓘ This is the `giveup_flight` path — a DIFFERENT production site from §CUSTODY-B/3's `defer_send`,
//     and the one that carries the [[B59]] failure shape. The application control on the same path is what keeps
//     the [[B263]] boundary honest: an APPLICATION carrier still reports.
// ⛔⛔ CORRECTED IN PLACE 2026-08-31 BY §CUSTODY-E: the words "transit-or-not, until Slice E" are WITHDRAWN and
//     the reason is worth keeping. They were the deliberate pin for the arm this file DID exercise (a LOCAL
//     origination, which is unchanged and still passes here verbatim) — but they also asserted, in a comment,
//     a TRANSIT behaviour no assertion in this file ever drove. Slice E closes [[B263]]: a transit terminal
//     give-up now reports NOTHING. The transit arm has a real, production-shaped case of its own in
//     `test_custody_terminal_e.cpp` §CUSTODY-E/1. ⇒ a claim that lived only in prose has become a measurement.
TEST_CASE("§CUSTODY-B/3e a cascade-exhausted INTERNAL flight reports no send_failed; an APPLICATION one still does") {
    struct Case { uint8_t type; bool expect_push; const char* label; };
    const Case cases[] = {
        { 0,                                       true,  "untyped DM — the user's OWN send (local; the transit twin is §CUSTODY-E/1)" },
        { DATA_TYPE_INTRO,                         true,  "INTRO (0x01) — application envelope (§6.4)" },
        { DATA_TYPE_E2E_ACK,                       false, "E2E_ACK (0x80)" },
        { DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY, false, "the B59 pubkey answer (0x8B)" },
    };
    for (const auto& c : cases) {
        CAPTURE(c.label);
        BHal hal; Node n(hal, /*id=*/1, 0x11111111u);
        NodeConfig cfg = b_cfg();
        CHECK(n.on_init(cfg));
        n.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);   // the ONLY next-hop -> exhaustion is terminal
        drain(n);
        const uint8_t body[] = { 'z' };
        CHECK(n.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, c.type) != 0);
        // Exhaust the primary hop (rts_max_retries = 3), then cascade. With no ALTERNATE next-hop the cascade
        // REQUEUES with backoff rather than giving up, so the whole cycle repeats until the requeue budget is
        // spent and `try_cascade_requeue` finally calls `giveup_flight`. ⓘ MEASURED, not assumed: a single
        // exhaustion ends at `cascade_requeue`, which is why this loop exists rather than four timer pokes.
        for (int round = 0; round < 24 && hal.count("path_cascade_exhausted") == 0; ++round) {
            for (int i = 0; i < 3; ++i) { n.on_timer(kRtsTimeoutTimerId); n.on_timer(kRetryBackoffTimerId); }
            n.on_timer(kRtsTimeoutTimerId);
            hal._now += 60000;                      // let the requeue backoff elapse
            n.on_timer(kQueueWakeupTimerId);        // -> become_free re-issues the requeued flight
        }
        bool pushed = false;
        Push pu{}; while (n.next_push(pu)) if (pu.kind == PushKind::send_failed) pushed = true;
        CHECK(pushed == c.expect_push);
    }
}

// ★★★ THE POSITIVE ARM OF THE GENERIC FAMILY, and it exists because a MUTATION FOUND IT MISSING: the first cut
//     of this slice's battery gated `send_acked` off entirely and the whole suite stayed GREEN — nothing pinned
//     that push at all. ⇒ the §6.2(5) gate now has a control in BOTH directions: internal must NOT raise it
//     (§CUSTODY-B/3, /3e) and an application send MUST still raise it (here). A suppression with only a
//     suppressing control is one edit away from deleting a shipped signal in silence.
TEST_CASE("§CUSTODY-B/3f an APPLICATION send still earns its generic send_acked — the over-correction control") {
    Chain c;
    const uint8_t body[] = { 'h', 'i' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, /*type=*/0) != 0);
    (void)hop(c, c.n1, c.h1, c.n2, c.h2);
    bool acked = false;
    Push pu{}; while (c.n1.next_push(pu)) if (pu.kind == PushKind::send_acked) acked = true;
    CHECK(acked);                                     // the user's own DM: the hop ACK is still reported
    // ...and the INTERNAL twin on the identical path is not (the pair is the measurement).
    Chain d;
    CHECK(d.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, /*dst_hash=*/0,
                                  DATA_TYPE_E2E_ACK) != 0);
    (void)hop(d, d.n1, d.h1, d.n2, d.h2);
    bool acked_i = false;
    Push pi{}; while (d.n1.next_push(pi)) if (pi.kind == PushKind::send_acked) acked_i = true;
    CHECK_FALSE(acked_i);
}

// =====================================================================================================
// [[B268]] — THE GRANT'S OWN OUTCOMES, DRIVEN THROUGH PRODUCTION (owner ruling (b), 2026-08-30)
// ⛔⛔ THE RULING'S EXPLICIT TEST BAR, AND THE REASON IT EXISTS: every §UI-16 grant case FABRICATES its pushes,
//     so the whole suite stayed green while the core stopped minting them — that structural blindness IS [[B268]].
//     ⇒ these two cases drive a REAL node: a real grant origination, a real `TxParams::seq` echoed back through
//     `on_tx_complete` exactly as `DeviceHal` echoes it, and a real cascade give-up. The fabricated cases in
//     `test_firmware_ui_invite.cpp` are kept — they test the pure MODEL (correlation, monotonicity, terminal
//     refusal) — but they can never prove what these two prove.
// =====================================================================================================
namespace {
struct GrantNode {
    BHal hal, phal;               // `phal` = the TEAM PEER's radio — a real node is needed to CTS, so the grant's
    Identity A{}, B{};            // DATA frame actually airs. Only a DATA frame carries a flight identity that
    Node* n = nullptr;            // `push_send_aired_if_owned` will accept (`frame_tag_of(tag) == FrameTag::data`),
    Node* peer = nullptr;         // which is a production property, not a test detail — measured, not assumed.
    uint64_t now = 100000;
    GrantNode() {
        uint8_t sa[32], sb[32];
        for (int i = 0; i < 32; ++i) { sa[i] = uint8_t(i + 5); sb[i] = uint8_t(80 - i); }
        identity_from_seed(A, sa); identity_from_seed(B, sb);
        n = new Node(hal, /*id=*/2, A.key_hash32);
        NodeConfig cfg; cfg.routing_sf = 8; cfg.leaf_id = 0;
        cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
        cfg.is_mobile = true; cfg.team_id = 0xAAAAAAAAu;
        CHECK(n->on_init(cfg));
        n->set_crypto_identity(A.x_secret, A.ed_pub);
        n->team_channel_key_mint();
        n->set_team_local_id(40);
        n->peer_key_set(B.key_hash32, B.ed_pub, Node::PeerKeyConf::authoritative);
        peer = new Node(phal, /*id=*/86, B.key_hash32);
        NodeConfig pcfg; pcfg.routing_sf = 8; pcfg.leaf_id = 0;
        pcfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
        pcfg.team_id = 0xAAAAAAAAu;                 // the grant flies on the TEAM plane; the peer must be on it
        CHECK(peer->on_init(pcfg));
        peer->set_team_local_id(86);
        hal._now = phal._now = now;
        n->test_learn_route(/*dest=*/86, /*via=*/86, 1, 40, /*team_plane=*/true);
        n->team_key_set(86, B.key_hash32, Node::IdBindSource::bcn, Node::IdBindConf::authoritative);
        peer->test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        drain(*n); drain(*peer);
        hal.clear_emits(); phal.clear_emits();
    }
    ~GrantNode() { delete peer; delete n; }
    void step() { hal._now = phal._now = ++now; }
    // Drive RTS -> CTS -> DATA so the grant's DATA frame (the only one carrying a flight identity the aired push
    // will accept) actually airs. ⛔ The ACK is deliberately NOT fed back: 4a needs the flight STILL LIVE when the
    // TxDone edge arrives, which is exactly the ordering `DeviceHal` produces on metal.
    const BTxFrame* air_data() {
        const std::vector<uint8_t> rts = hal.last("RTS");
        if (rts.empty()) return nullptr;
        step();
        peer->on_recv(rts.data(), rts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
        const std::vector<uint8_t> cts = phal.last("CTS");
        if (cts.empty()) return nullptr;
        step();
        n->on_recv(cts.data(), cts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
        step();
        n->on_timer(kCtsToDataGapTimerId);
        return hal.last_frame("DATA");
    }
};
}  // namespace

TEST_CASE("[[B268]]/4a PRODUCTION: a real correlated TxDone mints team_key_grant_aired -> the UI says KEY SENT") {
    GrantNode g;
    uint16_t ctr = 0; uint8_t dst = 0;
    const Node::TeamKeyGrantTx tx = g.n->team_key_grant_send(g.B.key_hash32, /*name=*/nullptr, /*name_len=*/0,
                                                            Plane::TEAM, &ctr, &dst);
    CHECK(tx == Node::TeamKeyGrantTx::queued);        // a REAL admission, not a stub
    CHECK(ctr != 0);
    // The UI's verdict carrier, built from the core's own answer exactly as `invite_grant_perform` builds it.
    mrui::InviteGrantResult r{};
    r.st = mrui::InviteGrantState::queued; r.ctr = ctr; r.dst = dst;
    // ★★★ THE REAL TxDone EDGE. `seq`/`tag` are read off the HAL — they are what the SENDING SITE stamped into
    //     `TxParams`, which is precisely what `DeviceHal` echoes back. ⛔ Not invented, not `_flight_gen` peeked at.
    const BTxFrame* data = g.air_data();          // a REAL RTS/CTS/DATA exchange with a real peer
    CHECK(data != nullptr);
    if (!data) return;
    CHECK(data->seq != 0);                        // a live flight always carries an identity                            // a live flight always carries an identity
    g.n->on_tx_complete(TxOutcome{ TxOutcomeKind::aired, BusyReason::none, TxResult::ok,
                                   data->tag, data->seq, /*sf=*/8, /*busy_until_ms=*/0 });
    // ★ THE CORE REALLY MINTED THE GRANT'S OWN KIND — and ⛔ no generic push came with it.
    bool grant_aired = false, generic = false;
    MESHROUTE_NS::Push pu{}, grant_push{};
    while (g.n->next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_aired) { grant_aired = true; grant_push = pu; }
        if (pu.kind == PushKind::send_aired || pu.kind == PushKind::send_acked
            || pu.kind == PushKind::send_failed || pu.kind == PushKind::send_blocked) generic = true;
    }
    CHECK(grant_aired);
    CHECK_FALSE(generic);                             // §6.2(5): never a generic lifecycle push for the grant
    CHECK(grant_push.dst == dst);                     // the correlation the UI keys on, minted by the core
    CHECK(grant_push.ctr == ctr);
    // ★★★★ AND THE UI REACHES `KEY SENT` FROM THE **REAL** PUSH — the transition [[B268]] had made unreachable.
    CHECK(mrui::invite_grant_apply_push(r, grant_push) == true);
    CHECK(r.st == mrui::InviteGrantState::sent);
    CHECK(strcmp(mrui::invite_grant_word(r.st), "KEY SENT") == 0);
}

TEST_CASE("[[B268]]/4b PRODUCTION: a real terminal give-up mints team_key_grant_failed -> the UI says GRANT FAILED") {
    GrantNode g;
    uint16_t ctr = 0; uint8_t dst = 0;
    const Node::TeamKeyGrantTx tx = g.n->team_key_grant_send(g.B.key_hash32, /*name=*/nullptr, /*name_len=*/0,
                                                            Plane::TEAM, &ctr, &dst);
    CHECK(tx == Node::TeamKeyGrantTx::queued);        // ⛔ POST-ADMISSION: the synchronous refusals are a different
    CHECK(ctr != 0);                                  //    contract and are deliberately untouched by this slice
    mrui::InviteGrantResult r{};
    r.st = mrui::InviteGrantState::queued; r.ctr = ctr; r.dst = dst;
    // ★ A REAL terminal give-up: exhaust the primary hop, then keep cycling until the cascade's requeue budget is
    //   spent and `try_cascade_requeue` calls `giveup_flight`. (Same shape as §CUSTODY-B/3e, measured not assumed.)
    for (int round = 0; round < 24 && g.hal.count("path_cascade_exhausted") == 0; ++round) {
        for (int i = 0; i < 3; ++i) { g.n->on_timer(kRtsTimeoutTimerId); g.n->on_timer(kRetryBackoffTimerId); }
        g.n->on_timer(kRtsTimeoutTimerId);
        g.hal._now += 60000;
        g.n->on_timer(kQueueWakeupTimerId);
    }
    CHECK(g.hal.count("path_cascade_exhausted") >= 1);   // the flight really did terminate
    bool grant_failed = false, generic = false;
    MESHROUTE_NS::Push pu{}, grant_push{};
    while (g.n->next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_failed) { grant_failed = true; grant_push = pu; }
        if (pu.kind == PushKind::send_failed || pu.kind == PushKind::send_aired
            || pu.kind == PushKind::send_acked || pu.kind == PushKind::send_blocked) generic = true;
    }
    CHECK(grant_failed);
    CHECK_FALSE(generic);
    CHECK(grant_push.ctr == ctr);
    CHECK(grant_push.reason != SendFailReason::none);    // the ruling: the FAILED kind also carries the reason
    CHECK(mrui::invite_grant_apply_push(r, grant_push) == true);
    CHECK(r.st == mrui::InviteGrantState::failed);
    CHECK(strcmp(mrui::invite_grant_word(r.st), "GRANT FAILED") == 0);
}

// =====================================================================================================
// [[B268]] BLOCKER-1 — THE OTHER POST-ADMISSION TERMINAL PATHS (QG 2026-08-30)
// ⛔⛔ THE DEFECT THESE CLOSE: `team_key_grant_failed` was minted only by `giveup_flight`. An ADMITTED grant can
//     die at SIX other places, and at every one of them §6.2(5) correctly suppressed the generic `send_failed`
//     while nothing replaced it ⇒ the panel sat at `GRANT QUEUED` for ever. All eleven carrier deaths now route
//     through ONE helper (`Node::terminal_carrier_outcome`), and these cases drive the real ones.
// =====================================================================================================

// ⓘ A MEASURED CORRECTION TO THIS CASE'S FIRST SHAPE, recorded because the fact is worth keeping: a grant with
//   no route does NOT defer — `send_by_hash`'s TEAM arm PARKS it (`TeamKeyGrantTx::parked`, a PRE-admission
//   answer the synchronous contract already carries), so that route never reaches a post-admission terminal path
//   at all. The deferred path is entered from `issue_send` when a route vanishes AFTER admission, which the
//   public API cannot stage. ⇒ the carrier is built through the existing `test_defer_send` seam while BOTH
//   functions under test — `defer_send`'s give-up arms and `try_drain_deferred`'s TTL arm — are the real
//   production code. ⛔ Stated rather than glossed: this one is seam-fed, 5b/5d below are fully production-shaped.
TEST_CASE("[[B268]]/5a a DEFERRED grant that ages out mints team_key_grant_failed (real defer + real TTL drain)") {
    constexpr uint32_t kDeferredDrainTimerId = 11;   // node.h:1345
    BHal hal; Node n(hal, /*id=*/2, 0x11111111u);
    NodeConfig cfg = b_cfg();
    CHECK(n.on_init(cfg));
    hal._now = 100000; drain(n); hal.clear_emits();
    n.test_defer_send(/*dst=*/86, /*ctr=*/4242, /*redrain_count=*/0, DATA_TYPE_TEAM_KEY_GRANT);
    CHECK(n.test_deferred_count() == 1);
    drain(n);
    hal._now += protocol::send_defer_ttl_ms + 1000;       // age it past the TTL
    n.on_timer(kDeferredDrainTimerId);                    // -> try_drain_deferred's give-up arm
    CHECK(hal.count("send_deferred_giveup") == 1);
    CHECK(n.test_deferred_count() == 0);                  // the carrier is DESTROYED at the reporting site
    int n_grant = 0; bool generic = false; Push pu{}, g{};
    while (n.next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_failed) { ++n_grant; g = pu; }
        if (pu.kind == PushKind::send_failed) generic = true;
    }
    CHECK(n_grant == 1);                                  // ★ EXACTLY ONE
    CHECK_FALSE(generic);                                 // ⛔ and never the generic twin
    CHECK(g.ctr == 4242);
    CHECK(g.reason == SendFailReason::no_route);
    // ⛔ THE APPLICATION CONTROL ON THE IDENTICAL PATH: an ordinary DM still reports generically, byte-for-byte
    //    as before this slice — and STILL DOES after §CUSTODY-E, because `test_defer_send` stages a LOCAL
    //    origination (`is_forward = false`), which the [[B263]] ownership term leaves untouched.
    BHal h2; Node a(h2, /*id=*/2, 0x11111111u);
    CHECK(a.on_init(cfg)); h2._now = 100000; drain(a);
    a.test_defer_send(/*dst=*/86, /*ctr=*/7, /*redrain_count=*/0, /*type=*/0);
    h2._now += protocol::send_defer_ttl_ms + 1000;
    a.on_timer(kDeferredDrainTimerId);
    bool app_generic = false; Push p2{};
    while (a.next_push(p2)) if (p2.kind == PushKind::send_failed) app_generic = true;
    CHECK(app_generic);
}

// The REPROVISION purge of a grant that is the LIVE FLIGHT — fully production-shaped: a real admission, a real
// flight, and `clear_learned_state()`, the public prep-restart seam every reprovision verb funnels through.
TEST_CASE("[[B268]]/5b PRODUCTION: a reprovision purge of a live grant flight mints team_key_grant_failed") {
    GrantNode gn;
    uint16_t ctr = 0; uint8_t dst = 0;
    CHECK(gn.n->team_key_grant_send(gn.B.key_hash32, nullptr, 0, Plane::TEAM, &ctr, &dst)
          == Node::TeamKeyGrantTx::queued);
    CHECK(gn.hal.last_frame("RTS") != nullptr);           // it really is the live flight
    drain(*gn.n);
    gn.n->clear_learned_state();                          // the real reprovision wipe
    int n_grant = 0; bool generic = false; Push pu{}, g{};
    while (gn.n->next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_failed) { ++n_grant; g = pu; }
        if (pu.kind == PushKind::send_failed) generic = true;
    }
    CHECK(n_grant == 1);                                  // ★ EXACTLY ONE, from the in-flight purge
    CHECK_FALSE(generic);
    CHECK(g.ctr == ctr);
    CHECK(g.reason == SendFailReason::reprovisioned);
}

// ★★★★ THE NASTIEST SEQUENCE, the one QG asked for by name: a grant DEFERRED (carrier #1) and then
//      REPROVISION-PURGED (carrier #2) — two of the eleven terminal paths in one lifetime. The single-emission
//      guarantee is by DESTRUCTION, not by a flag: whichever path runs first reports it AND destroys it, so the
//      second finds nothing. ⛔ If that were wrong the panel would take a `GRANT FAILED` it had already taken.
TEST_CASE("[[B268]]/5c deferred THEN purged — exactly ONE team_key_grant_failed across both paths, never two") {
    constexpr uint32_t kDeferredDrainTimerId = 11;
    BHal hal; Node n(hal, /*id=*/2, 0x11111111u);
    NodeConfig cfg = b_cfg();
    CHECK(n.on_init(cfg));
    hal._now = 100000; drain(n);
    n.test_defer_send(/*dst=*/86, /*ctr=*/4242, /*redrain_count=*/0, DATA_TYPE_TEAM_KEY_GRANT);
    CHECK(n.test_deferred_count() == 1);
    drain(n);
    hal._now += protocol::send_defer_ttl_ms + 1000;
    n.on_timer(kDeferredDrainTimerId);                    // (1) the TTL drain reports AND destroys it
    CHECK(n.test_deferred_count() == 0);                  // ★ the destruction that makes "exactly one" structural
    n.clear_learned_state();                              // (2) the purge finds nothing left to report
    int n_grant = 0; Push pu{};
    while (n.next_push(pu)) if (pu.kind == PushKind::team_key_grant_failed) ++n_grant;
    CHECK(n_grant == 1);                                  // ⛔ ONE across BOTH terminal paths, not two
    // ...and the MIRROR ORDER is equally single: purge first, so the TTL drain that follows has nothing.
    BHal h2; Node m(h2, /*id=*/2, 0x11111111u);
    CHECK(m.on_init(cfg)); h2._now = 100000; drain(m);
    m.test_defer_send(/*dst=*/86, /*ctr=*/99, /*redrain_count=*/0, DATA_TYPE_TEAM_KEY_GRANT);
    drain(m);
    m.clear_learned_state();                              // (1') the purge reports AND wipes _deferred_n
    CHECK(m.test_deferred_count() == 0);
    h2._now += protocol::send_defer_ttl_ms + 1000;
    m.on_timer(kDeferredDrainTimerId);                    // (2') nothing left
    int n2 = 0; Push p2{};
    while (m.next_push(p2)) if (p2.kind == PushKind::team_key_grant_failed) ++n2;
    CHECK(n2 == 1);
}

// The NACK / full-queue sibling: a real BUSY_RX NACK arrives for a live grant flight whose same-hop requeue has
// nowhere to go, so `handle_nack` takes ITS OWN give-up arm — ⛔ NOT `giveup_flight`, a genuinely different site,
// which is exactly why it needed its own helper call and its own per-site control.
TEST_CASE("[[B268]]/5d PRODUCTION: a NACK requeue with a FULL queue mints team_key_grant_failed") {
    GrantNode gn;
    uint16_t ctr = 0; uint8_t dst = 0;
    CHECK(gn.n->team_key_grant_send(gn.B.key_hash32, nullptr, 0, Plane::TEAM, &ctr, &dst)
          == Node::TeamKeyGrantTx::queued);
    CHECK(gn.hal.last_frame("RTS") != nullptr);           // the grant is the LIVE flight
    // Fill every queue slot so the NACK's same-hop requeue cannot be admitted.
    const uint8_t body[] = { 'x' };
    for (int i = 0; i < 16 && gn.n->test_tx_queue_n() < 8; ++i)
        (void)gn.n->test_do_send_typed(/*dst=*/86, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, /*type=*/0);
    CHECK(gn.n->test_tx_queue_n() == 8);                  // kTxQueueCap
    drain(*gn.n);
    gn.hal.clear_emits();
    // A LONG-busy BUSY_RX NACK for the live flight -> requeue attempt -> queue full -> the give-up arm.
    // ⓘ `mobile_to` + the TEAM-plane local id, not the static id: `handle_nack`'s §6.4 gate refuses a NACK whose
    //   plane does not match the node's (`(mobile_to == 1) != _cfg.is_mobile`), and this granter is a mobile team
    //   member. MEASURED — a plain static-addressed NACK is silently ignored, emitting nothing at all.
    nack_in ni{}; ni.reason = protocol::nack_reason_busy_rx; ni.mobile_to = true;
    ni.ctr_lo = static_cast<uint8_t>(ctr & 0x0F); ni.payload = 250; ni.to = 40;
    uint8_t nb[4]; const size_t nn = pack_nack(ni, std::span<uint8_t>(nb, sizeof nb));
    gn.step();
    gn.n->on_recv(nb, nn, RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(86)});
    CHECK(gn.hal.count("rts_giveup") == 1);               // the give-up arm really ran
    int n_grant = 0; bool generic = false; Push pu{}, g{};
    while (gn.n->next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_failed) { ++n_grant; g = pu; }
        if (pu.kind == PushKind::send_failed) generic = true;
    }
    CHECK(n_grant == 1);
    CHECK_FALSE(generic);
    CHECK(g.ctr == ctr);
}

// The defer-queue CAP refusal — `defer_send`'s other terminal arm, a different site from the redrain give-up and
// from the TTL drain, so it needs its own case or its own helper call has no control.
TEST_CASE("[[B268]]/5e the DEFER-QUEUE CAP refusal mints team_key_grant_failed") {
    BHal hal; Node n(hal, /*id=*/2, 0x11111111u);
    CHECK(n.on_init(b_cfg()));
    hal._now = 100000; drain(n);
    for (uint8_t i = 0; i < protocol::cap_deferred_sends; ++i)      // fill the defer ring
        n.test_defer_send(/*dst=*/86, /*ctr=*/static_cast<uint16_t>(100 + i), /*redrain_count=*/0, /*type=*/0);
    CHECK(n.test_deferred_count() == protocol::cap_deferred_sends);
    drain(n);
    n.test_defer_send(/*dst=*/86, /*ctr=*/4242, /*redrain_count=*/0, DATA_TYPE_TEAM_KEY_GRANT);   // REFUSED (full)
    CHECK(hal.count("send_deferred_refused") == 1);
    int n_grant = 0; bool generic = false; Push pu{}, g{};
    while (n.next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_failed) { ++n_grant; g = pu; }
        if (pu.kind == PushKind::send_failed) generic = true;
    }
    CHECK(n_grant == 1);
    CHECK_FALSE(generic);
    CHECK(g.reason == SendFailReason::queue_full);
}

// The reprovision purge of a QUEUED (not in-flight) grant — `node_channel.cpp`'s queue sweep, a different site
// from 5b's in-flight sweep.
// ⓘ `test_suspend_tx_drain(true)` installs a DEFAULT-CONSTRUCTED `PendingTx` (type 0) as the half-duplex guard,
//   so the in-flight sweep also fires its ordinary generic `send_failed` for that phantom. That is a property of
//   the TEST SEAM, not of production, and it is why this case counts the GRANT push rather than asserting the
//   absence of every generic one (5b, which uses a real flight, asserts that).
TEST_CASE("[[B268]]/5f the reprovision purge of a QUEUED grant mints team_key_grant_failed") {
    GrantNode gn;
    gn.n->test_suspend_tx_drain(true);
    uint16_t ctr = 0; uint8_t dst = 0;
    CHECK(gn.n->team_key_grant_send(gn.B.key_hash32, nullptr, 0, Plane::TEAM, &ctr, &dst)
          == Node::TeamKeyGrantTx::queued);
    CHECK(gn.n->test_tx_queue_n() == 1);                  // it is in the QUEUE, not the air
    drain(*gn.n);
    gn.n->clear_learned_state();
    int n_grant = 0; Push pu{}, g{};
    while (gn.n->next_push(pu)) if (pu.kind == PushKind::team_key_grant_failed) { ++n_grant; g = pu; }
    CHECK(n_grant == 1);
    CHECK(g.ctr == ctr);
    CHECK(g.reason == SendFailReason::reprovisioned);
}

// The LBT-STASH give-up: a DATA frame blocked by a busy channel is stashed and re-issued up to
// `tx_defer_max_retries`; on exhaustion the flight is RELEASED. ⛔ That release reported NOTHING to anybody
// before this slice — it is the SIXTH carrier death the blocker-1 sweep found — and it is reachable through the
// public `on_radio_busy` seam the simulator itself uses.
TEST_CASE("[[B268]]/5g the LBT-STASH DATA give-up mints team_key_grant_failed (the sixth site)") {
    GrantNode gn;
    uint16_t ctr = 0; uint8_t dst = 0;
    CHECK(gn.n->team_key_grant_send(gn.B.key_hash32, nullptr, 0, Plane::TEAM, &ctr, &dst)
          == Node::TeamKeyGrantTx::queued);
    const BTxFrame* data = gn.air_data();                 // the grant's DATA is on the air and STASHED
    CHECK(data != nullptr);
    if (!data) return;
    drain(*gn.n);
    gn.hal.clear_emits();
    // Exhaust the stash: each busy re-issues, the last one gives up and releases the flight.
    for (int i = 0; i <= protocol::tx_defer_max_retries + 1; ++i) {
        gn.step();
        gn.n->on_radio_busy(BusyInfo{ BusyReason::channel_busy, data->tag, /*sf=*/8, /*busy_until_ms=*/gn.now + 10 });
    }
    CHECK(gn.hal.count("tx_giveup") >= 1);                // the give-up arm really ran
    int n_grant = 0; bool generic = false; Push pu{}, g{};
    while (gn.n->next_push(pu)) {
        if (pu.kind == PushKind::team_key_grant_failed) { ++n_grant; g = pu; }
        if (pu.kind == PushKind::send_failed) generic = true;
    }
    CHECK(n_grant == 1);
    CHECK_FALSE(generic);                                 // ⛔ the site's PRE-EXISTING silence for the generic
    CHECK(g.ctr == ctr);                                  //    family is preserved byte-for-byte
}
