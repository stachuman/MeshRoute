// MeshRoute — test_custody_terminal_e.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-E — THE TYPED TERMINAL CONTEXT, and [[B263]]'s closure (spec
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md` §9.3/§9.4/§11/§17-E, §18.4.7).
//
// THE THREE CLAIMS THIS FILE MEASURES:
//   (1) [[B263]] — a TRANSIT terminal give-up emits NO generic `send_failed` under the original sender's
//       `{dst, ctr}`, while a LOCAL application send's `send_failed` keeps its ordering AND its values
//       byte-identically. Both sides are pinned; either alone is compatible with a wrong fix.
//   (2) The typed context — root STAGE (CTS vs hop-ACK) and terminal CAUSE (§9.4's five values, with the
//       `cascade_count -> cascade_age -> queue_full` precedence and `load_shed`/`one_way_throttled` separate),
//       plus `repair_attempted` set by INVOKING the repair logic. §18.4.7: one case per cause and per stage.
//   (3) The step-7 seam is INERT — Slice E constructs no custody notice, enqueues nothing, and allocates no
//       `0x81`. A behavioural negative arm plus a grep-backed structural arm, because either alone is weak.
//
// ⛔ PRODUCTION-SHAPED WHEREVER THE PATH ALLOWS: the transit carrier is a REAL forward installed by a real
//    RTS/CTS/DATA/ACK exchange on a 3-node chain, not a seeded `PendingTx`. That is the [[B268]] lesson — a
//    fabricated carrier proves the assertion, not the code.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "support/test_hal.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// Timer ids, mirrored TU-locally exactly as `test_node_r3.cpp:128` and `test_custody_internal_b.cpp:33` do.
constexpr uint32_t kRtsTimeoutTimerId   = 4;
constexpr uint32_t kAckTimeoutTimerId   = 5;
constexpr uint32_t kCtsToDataGapTimerId = 7;
constexpr uint32_t kQueueWakeupTimerId  = 8;
constexpr uint32_t kPostAckTimerId      = 9;
constexpr uint32_t kRetryBackoffTimerId = 10;

struct ETxFrame { std::string label; std::vector<uint8_t> bytes; };

class EHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    std::vector<ETxFrame>    tx_frames;
    void emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind ? kind : ""); }
    int  count(const char* k) const { int c = 0; for (const auto& e : emits) if (e == k) ++c; return c; }
    void clear_emits() { emits.clear(); }
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        tx_frames.push_back(ETxFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n) });
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
};

LayerConfig e_layer(uint8_t layer_id, uint8_t sf) {
    LayerConfig L; L.layer_id = layer_id; L.routing_sf = sf;
    L.allowed_sf_bitmap = static_cast<uint16_t>(1u << sf);
    return L;
}
NodeConfig e_cfg() {
    NodeConfig cfg; cfg.n_layers = 1; cfg.layers[0] = e_layer(/*layer_id=*/1, /*sf=*/8);
    cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
    return cfg;
}
void drain(Node& n) { Push d{}; while (n.next_push(d)) {} }

// ---- a 3-node chain 1 -> 2 -> 3 (the §CUSTODY-B `Chain`, kept TU-local so neither file's fixture drifts under
//      the other's cases). Node 2 is the RELAY: a DM from 1 to 3 installs a TRANSIT carrier there, which is the
//      exact shape [[B263]] is about.
struct EChain {
    EHal h1, h2, h3;
    Node n1{h1, /*id=*/1, 0x11111111u};
    Node n2{h2, /*id=*/2, 0x22222222u};
    Node n3{h3, /*id=*/3, 0x33333333u};
    uint64_t now = 100000;
    EChain() {
        NodeConfig cfg = e_cfg();
        CHECK(n1.on_init(cfg)); CHECK(n2.on_init(cfg)); CHECK(n3.on_init(cfg));
        n1.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        n1.test_learn_route(/*dest=*/3, /*via=*/2, 2, 40, false);
        n2.test_learn_route(/*dest=*/1, /*via=*/1, 1, 40, false);
        n2.test_learn_route(/*dest=*/3, /*via=*/3, 1, 40, false);   // 2's ONLY path to 3 -> exhaustion is terminal
        n3.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        n3.test_learn_route(/*dest=*/1, /*via=*/2, 2, 40, false);
        h1._now = h2._now = h3._now = now;
        drain(n1); drain(n2); drain(n3);
        h1.clear_emits(); h2.clear_emits(); h3.clear_emits();
    }
    void step() { h1._now = h2._now = h3._now = ++now; }
};

// One COMPLETE hop over the real MAC (RTS -> CTS -> DATA -> ACK -> post-ACK), verbatim in shape from
// `test_custody_internal_b.cpp`'s `hop`. The ACK feed-back is load-bearing: without it the sender stays
// `awaiting_ack` and refuses the next exchange.
void e_hop(EChain& c, Node& src, EHal& shal, Node& dst, EHal& dhal) {
    const std::vector<uint8_t> rts = shal.last("RTS");
    CHECK_FALSE(rts.empty());
    if (rts.empty()) return;
    c.step();
    dst.on_recv(rts.data(), rts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    const std::vector<uint8_t> cts = dhal.last("CTS");
    CHECK_FALSE(cts.empty());
    if (cts.empty()) return;
    c.step();
    src.on_recv(cts.data(), cts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    c.step();
    src.on_timer(kCtsToDataGapTimerId);
    const std::vector<uint8_t> data = shal.last("DATA");
    CHECK_FALSE(data.empty());
    if (data.empty()) return;
    c.step();
    dst.on_recv(data.data(), data.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    const std::vector<uint8_t> ack = dhal.last("ACK");
    if (!ack.empty()) { c.step(); src.on_recv(ack.data(), ack.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)}); }
    c.step();
    dst.on_timer(kPostAckTimerId);
}

// Drive a live flight all the way to a CASCADE-COUNT terminal: burn the same-hop retries, let the cascade
// requeue, and advance the clock by just each backoff so the AGE cap never fires first. Returns true if a
// terminal actually happened — ⛔ every case CHECKs it, so a fixture that silently stops driving is a failure,
// never a vacuous pass.
bool run_to_cascade_terminal(Node& n, EHal& hal, int max_rounds = 12) {
    for (int round = 0; round < max_rounds && hal.count("path_cascade_exhausted") == 0; ++round) {
        for (int i = 0; i < 4; ++i) { n.on_timer(kRtsTimeoutTimerId); n.on_timer(kRetryBackoffTimerId); }
        hal._now += 21000;                    // > the largest requeue backoff, < the 60 s age cap
        n.on_timer(kQueueWakeupTimerId);
    }
    return hal.count("path_cascade_exhausted") > 0;
}

// Count the generic user-send failures in a node's push ring, capturing the FIRST (the ring is FIFO, so the
// first is the one the terminal under test minted — a later flight's own death must not overwrite it).
int count_send_failed(Node& n, Push* out = nullptr) {
    int c = 0; Push p{};
    while (n.next_push(p)) if (p.kind == PushKind::send_failed) { if (out && c == 0) *out = p; ++c; }
    return c;
}

// ---- the grep-backed structural arm's file reader -----------------------------------------------------------
// ⛔ IT CANNOT BE VACUOUS: the caller CHECKs that the file opened AND that a known marker is present, so a
//    wrong working directory is a LOUD failure rather than an empty search returning "no violations found".
//    ([[B82]]: a relative path in a cwd-resetting context silently measured nothing once already.)
std::string read_repo_file(const char* rel) {
    // `__FILE__` is this file's path as the build saw it; strip the trailing "test/<name>" to get the root.
    std::string self = __FILE__;
    const std::string tail = "test/test_custody_terminal_e.cpp";
    std::string root;
    if (self.size() >= tail.size() && self.compare(self.size() - tail.size(), tail.size(), tail) == 0)
        root = self.substr(0, self.size() - tail.size());
    for (const std::string& base : { root, std::string(""), std::string("../") }) {
        std::ifstream f(base + rel);
        if (!f.good()) continue;
        std::ostringstream ss; ss << f.rdbuf();
        std::string s = ss.str();
        if (!s.empty()) return s;
    }
    return {};
}

}  // namespace

// =====================================================================================================
// §CUSTODY-E/1 — [[B263]]: THE TRANSIT TERMINAL GIVE-UP STOPS LYING, AND THE LOCAL ONE DOES NOT CHANGE
// =====================================================================================================

// AUTHORITY: `Node::terminal_carrier_outcome` (node.cpp) — the central gate's new `own_origination` term —
// reached through `giveup_flight` (node_cascade.cpp).
// ★★★★ THE REGISTER ROW'S OWN GATE, VERBATIM: "a native case proving a transit terminal give-up emits NO
//      generic `send_failed` + a mutation restoring the unconditional push (RED)". This is that case, and the
//      mutation is battery `sliceEnode` E01.
// ⛔ THE CARRIER IS REAL: node 1 sends a DM addressed to node 3, which routes THROUGH node 2. Node 2 accepts
//    custody (it ACKs), installs a forward with `has_previous_hop = true`, and then cannot reach 3 — the exact
//    B59 topology. Nothing is seeded.
TEST_CASE("§CUSTODY-E/1 [[B263]] a TRANSIT terminal give-up emits NO generic send_failed under the foreign {dst,ctr}") {
    EChain c;
    const uint8_t body[] = { 'v', 'i', 'a' };
    const uint16_t ctr = c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off,
                                                 /*dst_hash=*/0, /*type=*/0);
    CHECK(ctr != 0);
    e_hop(c, c.n1, c.h1, c.n2, c.h2);         // 1 -> 2: node 2 now holds the transit carrier for dst 3
    drain(c.n2);                               // start the relay's ring empty — only the terminal may fill it
    c.h2.clear_emits();
    CHECK(c.h2.label_count("RTS") > 0);        // non-vacuous: the relay really did try to reach 3

    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(c.h2.count("rts_giveup") >= 1);      // ⛔ the TELEMETRY is untouched — this slice suppresses a PUSH,
    CHECK(c.h2.count("path_cascade_exhausted") >= 1);   //    never an emit (the corpus obligation, in miniature)

    // ★★★★ THE CLOSURE: nothing at all in the relay's app ring.
    Push last{};
    CHECK(count_send_failed(c.n2, &last) == 0);
}

// ⛔ THE OTHER SIDE OF THE SAME GATE, and it is what stops the fix being an over-correction: our OWN send's
//    generic failure is untouched — same push, same reason, same `{dst, ctr}`.
TEST_CASE("§CUSTODY-E/1b a LOCAL application send's terminal give-up still reports, with its EXACT values") {
    EChain c;
    const uint8_t body[] = { 'm', 'i', 'n', 'e' };
    const uint16_t ctr = c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off,
                                                 /*dst_hash=*/0, /*type=*/0);
    CHECK(ctr != 0);
    drain(c.n1);
    // Node 2 never answers: the RTS times out, the cascade exhausts, the flight dies.
    CHECK(run_to_cascade_terminal(c.n1, c.h1));

    Push p{};
    CHECK(count_send_failed(c.n1, &p) == 1);              // ★ EXACTLY ONE
    CHECK(p.reason == SendFailReason::no_cts);            // ⛔ the value, not just the presence
    CHECK(p.dst == 2);
    CHECK(p.ctr == ctr);
}

// ★★★ THE ORDERING HALF of §17-E bullet 2, which a presence/value check alone cannot see: the report is made
//     while flight A is still the live carrier — BEFORE the reset and BEFORE `become_free()` installs the
//     queued flight B. If the ownership decision were made after the reset (battery E02), the values would come
//     from a dead carrier or from B.
TEST_CASE("§CUSTODY-E/1c the local generic failure is minted from the DYING flight, before become_free installs the next") {
    EChain c;
    const uint8_t body[] = { 'a' };
    // ⓘ THE QUEUE IS FILLED DELIBERATELY, AND THE REASON IS MEASURED: with only ONE item behind it, flight A
    //   REQUEUES (its backoff parks it) and the queued flight B becomes current first, so "which flight died"
    //   stops being a controlled variable. A FULL queue makes A's very first cascade exhaustion terminal, so the
    //   push under test is unambiguously A's — and B is still there to be installed by `become_free()`.
    const uint16_t ctr_a = c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr_a != 0);
    for (int i = 0; i < 8; ++i)
        CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    CHECK(c.n1.test_tx_queue_n() == 8);                   // the queued flights sit behind the live flight A
    drain(c.n1);
    for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);

    Push p{};
    const int n = count_send_failed(c.n1, &p);
    CHECK(n >= 1);
    CHECK(p.ctr == ctr_a);                                // ★ A's counter — read while A was alive
    CHECK(p.dst == 2);                                    // ★ A's destination, NOT a queued flight's (3)
    // ★★ AND THE CARRIER IS GONE while its values are in the ring — which is the ORDERING claim itself: the
    //    report can only have been minted BEFORE `_pending_tx.reset()`, because after it there is no `{dst, ctr}`
    //    left to read. ⓘ MEASURED: the eight queued flights are still held at this instant (their admission is
    //    gated by the MAC state the give-up left behind), so no later flight can be the source of that pair.
    CHECK_FALSE(c.n1.has_pending_tx());
    CHECK(c.n1.test_tx_queue_n() == 8);
}

// =====================================================================================================
// §CUSTODY-E/2 — THE ROOT STAGE (§9.3 bits 1/2), BOTH ARMS, FROM THE TIMER THAT FIRED
// =====================================================================================================

// ★★★ §18.4.7's "both stages". The stage is production-observable TWICE over: through the `SendFailReason` it
//     maps to (no_cts / no_ack — the app-visible half) and through the typed context itself. Both are asserted,
//     because the mapping and the context are two different mistakes.
TEST_CASE("§CUSTODY-E/2 an RTS root is the CTS stage -> no_cts") {
    EChain c;
    const uint8_t body[] = { 'r' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n1);
    Node::test_reset_terminal_custody_ctx();
    CHECK(run_to_cascade_terminal(c.n1, c.h1));

    Push p{};
    CHECK(count_send_failed(c.n1, &p) == 1);
    CHECK(p.reason == SendFailReason::no_cts);
    CHECK(Node::test_last_terminal_custody_ctx().stage == CustodyRootStage::cts);
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);   // ⛔ nor the cause sentinel
}

TEST_CASE("§CUSTODY-E/2b a DATA-ACK root is the hop-ACK stage -> no_ack") {
    EChain c;
    const uint8_t body[] = { 'd' };
    // ⓘ THE SHAPE IS MEASURED, NOT CHOSEN FOR CONVENIENCE. A plain missed ACK does NOT reach a terminal at the
    //   DATA-ACK root: `ack_timeout_fire` spends a same-hop retry, which re-RTSes, so the flight dies at the CTS
    //   root every time. Two production facts make the ACK root terminal in one pass:
    //     · a liveness-SILENT next-hop makes `ack_timeout_fire` cascade IMMEDIATELY (§P3, "data_ack_silent_cascade");
    //     · a FULL tx queue makes that first cascade exhaustion terminal instead of a requeue.
    //   Both are real production conditions driven through their real seams.
    const uint16_t ctr = c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr != 0);
    for (int i = 0; i < 8; ++i)
        CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    CHECK(c.n1.test_tx_queue_n() == 8);
    // Take the exchange as far as the DATA, then WITHHOLD the hop ACK.
    const std::vector<uint8_t> rts = c.h1.last("RTS");
    CHECK_FALSE(rts.empty());
    c.step();
    c.n2.on_recv(rts.data(), rts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    const std::vector<uint8_t> cts = c.h2.last("CTS");
    CHECK_FALSE(cts.empty());
    c.step();
    c.n1.on_recv(cts.data(), cts.size(), RxMeta{10.0f, -75.0f, 0, static_cast<int8_t>(-1)});
    c.step();
    c.n1.on_timer(kCtsToDataGapTimerId);
    CHECK(c.h1.label_count("DATA") == 1);                 // the DATA aired; no ACK is ever fed back
    // ⛔ THE SILENCE IS ESTABLISHED **AFTER** THE CTS, AND THAT ORDERING IS MEASURED, NOT STYLISTIC: receiving a
    //    frame from a peer runs `clear_peer_suspect`, so a next-hop marked silent before the exchange is healthy
    //    again by the time the DATA-ACK wait begins — which is precisely the MF4 observation one plane over.
    c.n1.record_peer_rts_timeout(2, 9); c.n1.record_peer_rts_timeout(2, 9); c.n1.record_peer_rts_timeout(2, 9);
    CHECK(c.n1.peer_penalty_q4(2) >= protocol::peer_silent_penalty_q4);
    drain(c.n1);
    c.h1.clear_emits();
    Node::test_reset_terminal_custody_ctx();

    c.step();
    c.n1.on_timer(kAckTimeoutTimerId);                    // ⛔ the DATA-ACK root, and only it
    CHECK(c.h1.count("data_ack_silent_cascade") >= 1);    // the DATA-ACK root really is the one that fired
    CHECK(c.h1.count("rts_giveup") == 0);                 // ⛔ non-vacuous: NOT the CTS root
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);
    Push p{};
    CHECK(count_send_failed(c.n1, &p) == 1);
    CHECK(p.reason == SendFailReason::no_ack);            // ★ the app-visible half of the stage mapping
    CHECK(p.ctr == ctr);
    CHECK(Node::test_last_terminal_custody_ctx().stage == CustodyRootStage::hop_ack);
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);   // ⛔ nor the cause sentinel
}

// =====================================================================================================
// §CUSTODY-E/3 — THE CAUSE AUTHORITY (§9.4): the PURE unit, one arm per value, plus the precedence
// =====================================================================================================

// AUTHORITY: `Node::cascade_terminal_cause` (node_cascade.cpp) — the one place a §9.4 value is chosen, and
// `try_cascade_requeue`'s branch predicate as well.
TEST_CASE("§CUSTODY-E/3 the cause authority answers one value per §9.4 branch") {
    // NOT terminal: a fresh flight on an idle queue requeues.
    CHECK(Node::cascade_terminal_cause(/*requeue_count=*/0, /*age_ms=*/0, /*queue_depth=*/0)
          == CustodyFailureReason::invalid);
    // cascade_count — the requeue budget is spent.
    CHECK(Node::cascade_terminal_cause(protocol::cascade_requeue_max, 0, 0) == CustodyFailureReason::cascade_count);
    // cascade_age — the total-age cap.
    CHECK(Node::cascade_terminal_cause(0, protocol::cascade_requeue_total_max_ms, 0) == CustodyFailureReason::cascade_age);
    CHECK(Node::cascade_terminal_cause(0, protocol::cascade_requeue_total_max_ms - 1, 0) != CustodyFailureReason::cascade_age);
    // queue_full — no requeue slot. (Depth 8 == kTxQueueCap; the load budget is also exhausted there, which is
    // exactly why the PRECEDENCE case below matters.)
    CHECK(Node::cascade_terminal_cause(0, 0, /*queue_depth=*/8) == CustodyFailureReason::queue_full);
    // load_shed — the load-adaptive budget refuses it while the queue still has room.
    // At depth 5: effective max = 3 - (5 - 2) = 0, so even a never-requeued flight is shed.
    CHECK(Node::cascade_effective_max(5) == 0);
    CHECK(Node::cascade_terminal_cause(0, 0, /*queue_depth=*/5) == CustodyFailureReason::load_shed);
    // ...and at a depth the budget still covers, the same flight is NOT terminal — the two-sided check that
    // separates "load_shed is chosen" from "load_shed is always chosen".
    CHECK(Node::cascade_effective_max(2) == protocol::cascade_requeue_max);
    CHECK(Node::cascade_terminal_cause(0, 0, /*queue_depth=*/2) == CustodyFailureReason::invalid);
}

// ★★★ THE PRECEDENCE, §9.4: `cascade_count` -> `cascade_age` -> `queue_full`, then `load_shed`. It is asserted
//     with EVERY pair simultaneously true, because a precedence bug is invisible when only one input is set.
TEST_CASE("§CUSTODY-E/3b the §9.4 precedence is cascade_count -> cascade_age -> queue_full -> load_shed") {
    const uint8_t cnt = protocol::cascade_requeue_max;
    const uint64_t age = protocol::cascade_requeue_total_max_ms;
    CHECK(Node::cascade_terminal_cause(cnt, age, 0)  == CustodyFailureReason::cascade_count);   // count beats age
    CHECK(Node::cascade_terminal_cause(cnt, 0,   8)  == CustodyFailureReason::cascade_count);   // count beats queue_full
    CHECK(Node::cascade_terminal_cause(cnt, age, 8)  == CustodyFailureReason::cascade_count);   // count beats both
    CHECK(Node::cascade_terminal_cause(0,   age, 8)  == CustodyFailureReason::cascade_age);     // age beats queue_full
    CHECK(Node::cascade_terminal_cause(0,   age, 5)  == CustodyFailureReason::cascade_age);     // age beats load_shed
    CHECK(Node::cascade_terminal_cause(0,   0,   8)  == CustodyFailureReason::queue_full);      // queue_full beats load_shed
}

// ⛔ THE WIRE VALUES ARE §9.4's, PINNED. Slice F serializes THESE; a renumbering here is a silent wire change.
TEST_CASE("§CUSTODY-E/3c the §9.4 / §9.3 numeric values are the approved ones") {
    CHECK(static_cast<uint8_t>(CustodyFailureReason::invalid)           == 0);
    CHECK(static_cast<uint8_t>(CustodyFailureReason::one_way_throttled) == 1);
    CHECK(static_cast<uint8_t>(CustodyFailureReason::cascade_count)     == 2);
    CHECK(static_cast<uint8_t>(CustodyFailureReason::cascade_age)       == 3);
    CHECK(static_cast<uint8_t>(CustodyFailureReason::queue_full)        == 4);
    CHECK(static_cast<uint8_t>(CustodyFailureReason::load_shed)         == 5);
    // §9.3's stage values are the NOTICE-FLAG BIT NUMBERS, so Slice F's flags half is `1u << stage`.
    CHECK(static_cast<uint8_t>(CustodyRootStage::invalid) == 0);
    CHECK(static_cast<uint8_t>(CustodyRootStage::cts)     == 1);   // bit 1 = failed_at_cts
    CHECK(static_cast<uint8_t>(CustodyRootStage::hop_ack) == 2);   // bit 2 = failed_at_ack
    // ⛔ AND THEY ARE INDEPENDENT OF `SendFailReason` (§9.4: "wire values are explicit and independent"): the
    //    two enums are separate types, and the stage -> SendFailReason map is the only bridge.
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::cts)     == SendFailReason::no_cts);
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::hop_ack) == SendFailReason::no_ack);
}

// ★★★★ §CUSTODY-E/3d — THE SENTINEL IS FAIL-CLOSED, and this case exists because the first cut of this slice was
//      NOT. `custody_stage_fail_reason` shipped as `stage == hop_ack ? no_ack : no_cts`, so `CustodyRootStage::
//      invalid` — the enum's own explicit non-stage value, the one a default-constructed context carries —
//      silently became `no_cts`, A MEANINGFUL DIAGNOSIS. §9.3 requires exactly one of failed_at_cts/failed_at_ack
//      to be set, and that ternary would have satisfied it with a lie.
// ⛔ THE PROPERTY, stated in the form the mutation attacks: `invalid` can become NEITHER meaningful diagnosis.
//    Asserting `== none` alone is NOT enough — it would still pass if `none` were quietly redefined — so both
//    negative arms are pinned beside it. (The launder-the-sentinel class: [[B134]]'s `has_key`, Slice A's
//    `known = false`.)
TEST_CASE("§CUSTODY-E/3d the invalid stage is FAIL-CLOSED — it can never become no_cts or no_ack") {
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::invalid) == SendFailReason::none);
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::invalid) != SendFailReason::no_cts);
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::invalid) != SendFailReason::no_ack);
    // ⛔ AND THE TWO REAL STAGES ARE UNAFFECTED — a fail-closed sentinel must not be bought by weakening the
    //    answers that carry information (the over-correction direction).
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::cts)     == SendFailReason::no_cts);
    CHECK(Node::custody_stage_fail_reason(CustodyRootStage::hop_ack) == SendFailReason::no_ack);
    // ⓘ THE DEFAULT-CONSTRUCTED CONTEXT IS THE PRODUCER THAT MATTERS: it is what the observation slot resets to,
    //   and what any future zero-initialised carrier would hold. It must claim no stage and no cause.
    const TerminalCustodyContext blank{};
    CHECK(blank.stage == CustodyRootStage::invalid);
    CHECK(blank.cause == CustodyFailureReason::invalid);
    CHECK(blank.repair_attempted == false);
    CHECK(Node::custody_stage_fail_reason(blank.stage) == SendFailReason::none);
}

// =====================================================================================================
// §CUSTODY-E/4 — THE CAUSE, AS THE PRODUCTION PATH ACTUALLY BUILDS IT (§18.4.7: one case per cause)
// =====================================================================================================

// ★★ These drive the REAL cascade and read the typed context the terminal produced. The unit cases above prove
//    the authority; these prove the WIRING — that each production branch reaches it with its own signals.
TEST_CASE("§CUSTODY-E/4 a spent requeue budget builds cause = cascade_count") {
    EChain c;
    const uint8_t body[] = { 'c' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n1);
    Node::test_reset_terminal_custody_ctx();
    CHECK(run_to_cascade_terminal(c.n1, c.h1));
    CHECK(c.h1.count("cascade_load_skip") == 0);          // ⛔ NOT the shed arm — the hard cap
    CHECK(Node::test_last_terminal_custody_ctx().cause == CustodyFailureReason::cascade_count);
    CHECK(Node::test_last_terminal_custody_ctx().stage != CustodyRootStage::invalid);       // ⛔ the seam never sees the sentinel
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);
}

TEST_CASE("§CUSTODY-E/4b an aged-out flight builds cause = cascade_age") {
    EChain c;
    const uint8_t body[] = { 'a' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n1);
    Node::test_reset_terminal_custody_ctx();
    // ⛔ The clock jump is what separates this from /4: ONE requeue, then the 60 s total-age cap fires while the
    //    requeue COUNT is still 1 of 3. Same code path, different §9.4 answer.
    for (int round = 0; round < 12 && c.h1.count("path_cascade_exhausted") == 0; ++round) {
        for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
        c.h1._now += protocol::cascade_requeue_total_max_ms + 1000;
        c.n1.on_timer(kQueueWakeupTimerId);
    }
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);
    CHECK(Node::test_last_terminal_custody_ctx().cause == CustodyFailureReason::cascade_age);
    CHECK(Node::test_last_terminal_custody_ctx().stage != CustodyRootStage::invalid);
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);
}

TEST_CASE("§CUSTODY-E/4c a FULL tx queue builds cause = queue_full, and it outranks the shed") {
    EChain c;
    const uint8_t body[] = { 'q' };
    // One live flight + kTxQueueCap staged behind it: the requeue has nowhere to go.
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    for (int i = 0; i < 8; ++i)
        CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    CHECK(c.n1.test_tx_queue_n() == 8);
    drain(c.n1);
    Node::test_reset_terminal_custody_ctx();
    for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);
    CHECK(c.h1.count("cascade_load_skip") == 0);          // ⛔ the queue-full arm, NOT the shed arm
    CHECK(Node::test_last_terminal_custody_ctx().cause == CustodyFailureReason::queue_full);
    CHECK(Node::test_last_terminal_custody_ctx().stage != CustodyRootStage::invalid);
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);
}

TEST_CASE("§CUSTODY-E/4d a congested-but-not-full queue builds cause = load_shed, with its own telemetry") {
    EChain c;
    const uint8_t body[] = { 's' };
    // One live flight + 5 staged: depth 5 < kTxQueueCap(8), but the load-adaptive budget is already 0.
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    for (int i = 0; i < 5; ++i)
        CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    CHECK(c.n1.test_tx_queue_n() == 5);
    drain(c.n1);
    Node::test_reset_terminal_custody_ctx();
    for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h1.count("cascade_load_skip") >= 1);          // ★ the shed arm's OWN marker — the branches are distinct
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);
    CHECK(Node::test_last_terminal_custody_ctx().cause == CustodyFailureReason::load_shed);
    CHECK(Node::test_last_terminal_custody_ctx().stage != CustodyRootStage::invalid);
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);
}

TEST_CASE("§CUSTODY-E/4e a shut MF4 reprobe window builds cause = one_way_throttled") {
    EChain c;
    const uint8_t body[] = { 'o' };
    c.n1.test_set_link_one_way(/*next_hop=*/2);           // the sole route to 2 is one-way (MF4's subject)
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n1);
    // FIRST exhaustion: the reprobe window is OPEN (never probed), so the single probe flies and NOTHING dies.
    for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h1.count("link_reprobe") == 1);
    CHECK(c.h1.count("path_cascade_exhausted") == 0);     // ⛔ non-vacuous: the first pass is a PROBE, not a death
    Node::test_reset_terminal_custody_ctx();
    // SECOND exhaustion, still INSIDE `link_reprobe_ttl_ms`: the window is shut -> the selected terminal.
    for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h1.count("link_reprobe") == 1);               // still one — no second probe
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);
    CHECK(Node::test_last_terminal_custody_ctx().cause == CustodyFailureReason::one_way_throttled);
    // ⛔ AND IT IS NOT ONE OF THE CASCADE CAUSES: the carrier was not spent, the queue was empty and the flight
    //    was young. A cause merged into `cascade_count` would be indistinguishable without this.
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::cascade_count);
    CHECK(Node::test_last_terminal_custody_ctx().stage != CustodyRootStage::invalid);
    CHECK(Node::test_last_terminal_custody_ctx().cause != CustodyFailureReason::invalid);
}

// =====================================================================================================
// §CUSTODY-E/5 — `repair_attempted` COMES FROM INVOKING THE REPAIR LOGIC, NEVER FROM AN EVENT NAME
// =====================================================================================================

// ★★★★ THE PAIR IS THE MEASUREMENT: two terminals reached through the SAME give-up event name ("rts_giveup"),
//      one of which invoked `emit_route_request` and one of which did not. A string-derived answer cannot tell
//      them apart, so this case is precisely what battery `sliceEcascade` E07 fails against.
TEST_CASE("§CUSTODY-E/5 repair_attempted is FALSE when the terminal pass invoked no route request") {
    EChain c;
    const uint8_t body[] = { 'n' };
    // ⓘ MEASURED: a LONG cascade eventually drives its own next-hop to liveness-SILENT (every `rts_timeout_fire`
    //   give-up is evidence), and §P3 then fires a rediscovery — so `repair_attempted` legitimately becomes true.
    //   The FALSE arm therefore needs a terminal reached on the FIRST exhaustion, which a full queue gives.
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    for (int i = 0; i < 8; ++i)
        CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n1);
    c.h1.clear_emits();
    Node::test_reset_terminal_custody_ctx();
    for (int i = 0; i < 4; ++i) { c.n1.on_timer(kRtsTimeoutTimerId); c.n1.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h1.count("path_cascade_exhausted") >= 1);
    CHECK(c.h1.count("rts_giveup") >= 1);                 // the SAME event name as the case below
    CHECK(c.h1.count("r_tx") == 0);                       // ...and no repair was invoked
    CHECK(Node::test_last_terminal_custody_ctx().repair_attempted == false);
}

TEST_CASE("§CUSTODY-E/5b repair_attempted is TRUE when the §P3 rediscovery really fired") {
    EChain c;
    const uint8_t body[] = { 'y' };
    // Drive next-hop 2 to liveness-SILENT through the real path (3 same-hop give-ups), exactly as
    // `test_node_r3.cpp:1224` does — that is what arms §P3's active rediscovery at the cascade exhaustion.
    c.n1.record_peer_rts_timeout(2, 9); c.n1.record_peer_rts_timeout(2, 9); c.n1.record_peer_rts_timeout(2, 9);
    CHECK(c.n1.peer_penalty_q4(2) >= protocol::peer_silent_penalty_q4);
    CHECK(c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n1);
    c.h1.clear_emits();
    Node::test_reset_terminal_custody_ctx();
    CHECK(run_to_cascade_terminal(c.n1, c.h1));
    CHECK(c.h1.count("rts_giveup") >= 1);                 // ★ THE SAME EVENT NAME as /5
    CHECK(c.h1.count("r_tx") >= 1);                       // ...but a repair WAS invoked (the RREQ emit)
    CHECK(Node::test_last_terminal_custody_ctx().repair_attempted == true);
}

// =====================================================================================================
// §CUSTODY-E/6 — RETIRED IN PLACE BY §CUSTODY-F (2026-08-31). ⛔ NOT DELETED: the record of what Slice E
//               guaranteed, and of the slice that legitimately ended it, is the point.
// =====================================================================================================
//
// ⛔⛔ WHAT STOOD HERE AND WHY IT IS GONE. Two cases asserted §17-E bullet 4 ("do not emit custody traffic
//     yet") — a BEHAVIOURAL arm (an eligible transit terminal enqueues nothing, installs no flight, pushes
//     nothing, and no frame byte is `0x81`) and a GREP-BACKED structural arm (`DATA_TYPE_CUSTODY_FAILURE =`
//     does not exist in `frame_codec.h`; `enqueue_data` does not appear in `node_cascade.cpp`). **§CUSTODY-F
//     is the slice those assertions named as their terminator, and it landed: 0x81 is allocated, the codec
//     exists, and the selected transit terminal now originates exactly one notice.** Keeping either case
//     would be asserting the absence of the feature the next slice shipped.
//
// ★ THEY ARE REPLACED, NOT DROPPED, AND THE REPLACEMENTS ARE STRICTLY STRONGER — `test/test_custody_relay_f.cpp`:
//     · §CUSTODY-F/4  — the eligible transit terminal enqueues EXACTLY ONE 0x81, to the failed ORIGIN, on the
//                       global plane, with the §9.2 record the failed carrier justifies (the positive form of
//                       the behavioural arm: "nothing" became "exactly this");
//     · §CUSTODY-F/5  — the ordering: snapshot -> reset -> become_free -> enqueue, with a queued flight B
//                       becoming current BEFORE the notice is queued;
//     · §CUSTODY-F/3x — the twelve-condition negative matrix: every INELIGIBLE terminal still enqueues nothing,
//                       which is exactly §CUSTODY-E/6's claim, retained for every carrier shape but the one
//                       Slice F was built to report on.
// ⓘ THE THIRD CASE OF THIS GROUP SURVIVES UNCHANGED below (§CUSTODY-E/6c, the string-inference ban): §11's
//   prohibition on deriving a wire enum from an event name is not slice-scoped and no later slice retires it.

// ⛔ AND THE REASON-INFERENCE-BY-STRING IS GONE FOR GOOD (design §11: "string prefixes such as `rts_*` and
//    `data_*` are not an authority for a wire enum"). The former `giveup_fail_reason` prefix matcher must not
//    exist anywhere in the tree's production sources.
TEST_CASE("§CUSTODY-E/6c grep-backed: the give-up event name is no longer parsed for a reason") {
    const std::string cascade = read_repo_file("lib/core/node_cascade.cpp");
    const std::string mac_rx  = read_repo_file("lib/core/node_mac_rx.cpp");
    CHECK_FALSE(cascade.empty());
    CHECK_FALSE(mac_rx.empty());
    CHECK(mac_rx.find("handle_nack") != std::string::npos);           // the reader read the right file
    // ⛔ THE FUNCTION AND ITS CALLS ARE GONE. Its NAME survives only inside the banner recording what was
    //    removed and why, which is the correction idiom — so the searches are for the DEFINITION and for the
    //    call-with-a-literal form, never for the bare word.
    CHECK(cascade.find("SendFailReason Node::giveup_fail_reason") == std::string::npos);
    CHECK(cascade.find("giveup_fail_reason(\"")                   == std::string::npos);
    CHECK(mac_rx.find("giveup_fail_reason")                       == std::string::npos);
}
