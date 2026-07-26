// MeshRoute — test_node_e2e_ack.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Shelf item (i) — the E2E-ack DEADLINE. A -a DM (DATA_FLAG_E2E_ACK_REQ) is a POSITIVE-ONLY receipt: send_e2e_acked
// fires when the DATA_TYPE_E2E_ACK returns, and NOTHING fired when it never did. These cases exercise the new
// pending-ack ring: arm->ack->cleared (no timeout), arm->expiry->send_failed{e2e_ack_timeout} + a LATE ack is a
// harmless no-op, the ring-full refusal, the per-path ctr-pairing match rule, and the deadline derivation math.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"        // DATA_FLAG_E2E_ACK_REQ
#include "protocol_constants.h"
#include "support/test_hal.h"

#include <vector>
#include <string>

using namespace meshroute;

namespace meshroute {
// White-box seam (native only; friend of Node) — reach the private pending-e2e-ack ring + arm/clear/fire helpers.
struct E2eAckTestAccess {
    static uint8_t used(Node& n) { uint8_t c = 0; for (auto& e : n._pending_e2e_acks) if (e.used) ++c; return c; }
    static void    arm(Node& n, uint32_t key, bool is_xl, uint8_t dst, uint16_t ctr, uint32_t budget) { n.e2e_ack_arm(key, is_xl, dst, ctr, budget); }
    static void    clear(Node& n, uint8_t origin, uint16_t ctr, uint32_t sh) { n.e2e_ack_clear(origin, ctr, sh); }
    static bool    full(Node& n) { return n.e2e_ack_ring_full(); }
    static void    fire(Node& n) { n.e2e_ack_deadline_fire(); }
    static Node::PendingE2eAck& entry(Node& n, uint8_t i) { return n._pending_e2e_acks[i]; }
    static uint32_t timer_id() { return Node::kE2eAckDeadlineTimerId; }
};
}  // namespace meshroute

namespace {

class E2eHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    std::vector<std::pair<uint32_t, uint32_t>> armed;   // (delay, id)
    std::vector<uint32_t> cancelled;

    bool     after(uint32_t d, uint32_t id) override { armed.emplace_back(d, id); return true; }
    void     cancel(uint32_t id) override { cancelled.push_back(id); }
    void     emit(const char* k, const EventField*, size_t) override { emits.push_back(k); }
    bool     saw(const char* k) const { for (auto& e : emits) if (e == k) return true; return false; }
};

// A provisioned single-layer static node that can originate DMs (node_id set, data SF configured).
Node make_static(E2eHal& hal) {
    Node n(hal, /*id=*/1, /*key=*/0x11111111u);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
    n.on_init(cfg);
    return n;
}

Command send_cmd(uint8_t dst_id, uint8_t flags) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst_id; c.u.send.flags = flags; c.u.send.plane = 0;
    static const char body[] = "hi"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
    return c;
}

bool drained_e2e_timeout(Node& n, uint16_t ctr) {
    Push p; bool hit = false;
    while (n.next_push(p))
        if (p.kind == PushKind::send_failed && p.reason == SendFailReason::e2e_ack_timeout && p.ctr == ctr) hit = true;
    return hit;
}

}  // namespace

TEST_CASE("e2e-ack deadline — derivation math (round trip = 2x the one-way delivery budget; no magic numbers)") {
    CHECK(protocol::e2e_ack_deadline_ms    == 2 * protocol::send_defer_ttl_ms);        // same-layer
    CHECK(protocol::e2e_ack_deadline_xl_ms == 2 * protocol::gateway_send_giveup_ms);   // cross-layer / delegated tier
    CHECK(protocol::e2e_ack_deadline_xl_ms >= protocol::e2e_ack_deadline_ms);          // the delegated tier is the LARGER one
    CHECK(protocol::e2e_ack_deadline_xl_ms <= protocol::mobile_home_cache_ttl_ms);     // bounded so a re-home invalidates first
    CHECK(protocol::cap_pending_e2e_acks == 8);
}

TEST_CASE("e2e-ack deadline — arm on a -a send, CLEAR on the ack, NO timeout fires") {
    E2eHal hal; Node n = make_static(hal);
    const CmdResult r = n.on_command(send_cmd(/*dst*/ 2, DATA_FLAG_E2E_ACK_REQ));
    CHECK(r.code == CmdCode::queued);
    CHECK(E2eAckTestAccess::used(n) == 1);                       // armed

    // The same-layer ack: sender_hash==0, acker origin == the dst we sent to (2), ctr == the minted ctr.
    E2eAckTestAccess::clear(n, /*origin*/ 2, r.ctr, /*sender_hash*/ 0);
    CHECK(E2eAckTestAccess::used(n) == 0);                       // cleared

    hal._now = protocol::e2e_ack_deadline_ms + 10 * 60 * 1000;   // well past any deadline
    E2eAckTestAccess::fire(n);
    CHECK_FALSE(drained_e2e_timeout(n, r.ctr));                  // nothing to expire -> no push
}

TEST_CASE("e2e-ack deadline — no ack -> send_failed{e2e_ack_timeout}; a LATE ack is a harmless no-op") {
    E2eHal hal; Node n = make_static(hal);
    const CmdResult r = n.on_command(send_cmd(/*dst*/ 2, DATA_FLAG_E2E_ACK_REQ));
    CHECK(E2eAckTestAccess::used(n) == 1);

    hal._now = protocol::e2e_ack_deadline_ms + 1;               // past the same-layer deadline
    E2eAckTestAccess::fire(n);
    CHECK(E2eAckTestAccess::used(n) == 0);                       // expired
    CHECK(hal.saw("send_failed"));                               // telemetry
    Push p; bool got = false;
    while (n.next_push(p))
        if (p.kind == PushKind::send_failed && p.reason == SendFailReason::e2e_ack_timeout) {
            got = true; CHECK(p.ctr == r.ctr); CHECK(p.dst == 2);
        }
    CHECK(got);

    // A LATE ack after expiry: the entry is already gone -> a no-op (no crash, no double-free, still empty).
    E2eAckTestAccess::clear(n, /*origin*/ 2, r.ctr, /*sender_hash*/ 0);
    CHECK(E2eAckTestAccess::used(n) == 0);
}

TEST_CASE("e2e-ack deadline — a full ring REFUSES a new -a send LOUD (err_ack_ring_full), never evict-oldest") {
    E2eHal hal; Node n = make_static(hal);
    for (uint8_t i = 0; i < protocol::cap_pending_e2e_acks; ++i) {   // fill the ring (8 distinct -a sends)
        const CmdResult r = n.on_command(send_cmd(static_cast<uint8_t>(2 + i), DATA_FLAG_E2E_ACK_REQ));
        CHECK(r.code == CmdCode::queued);
    }
    CHECK(E2eAckTestAccess::full(n));
    const CmdResult r9 = n.on_command(send_cmd(/*dst*/ 99, DATA_FLAG_E2E_ACK_REQ));   // the 9th
    CHECK(r9.code == CmdCode::err_ack_ring_full);
    CHECK(E2eAckTestAccess::used(n) == protocol::cap_pending_e2e_acks);               // NOT evicted

    // A NON-ack send is never gated by the ring.
    const CmdResult plain = n.on_command(send_cmd(/*dst*/ 99, /*flags*/ 0));
    CHECK(plain.code == CmdCode::queued);
}

TEST_CASE("e2e-ack deadline — the per-origination-path ctr-pairing match rule") {
    E2eHal hal; Node n = make_static(hal);

    // (A) same-layer id send: entry{key=dst, is_xl=false}. Clears ONLY on a same-layer ack (sender_hash==0)
    //     whose acker origin == the dst. A stray XL ack (sender_hash!=0) with the same ctr must NOT clear it.
    E2eAckTestAccess::arm(n, /*key=*/7, /*is_xl=*/false, /*dst=*/7, /*ctr=*/100, protocol::e2e_ack_deadline_ms);
    E2eAckTestAccess::clear(n, /*origin*/ 7, /*ctr*/ 100, /*sender_hash*/ 0xDEAD);   // XL-shaped ack -> no match
    CHECK(E2eAckTestAccess::used(n) == 1);
    E2eAckTestAccess::clear(n, /*origin*/ 9, /*ctr*/ 100, /*sender_hash*/ 0);        // wrong origin -> no match
    CHECK(E2eAckTestAccess::used(n) == 1);
    E2eAckTestAccess::clear(n, /*origin*/ 7, /*ctr*/ 100, /*sender_hash*/ 0);        // right origin + same-layer -> clears
    CHECK(E2eAckTestAccess::used(n) == 0);

    // (B) cross-layer send: entry{key=far_hash, is_xl=true}. Clears on the CROSS_LAYER ack whose source_hash == far_hash.
    E2eAckTestAccess::arm(n, /*key=*/0xABCDEF01u, /*is_xl=*/true, /*dst=*/0, /*ctr=*/200, protocol::e2e_ack_deadline_xl_ms);
    E2eAckTestAccess::clear(n, /*origin*/ 42, /*ctr*/ 200, /*sender_hash*/ 0);           // same-layer ack -> no match
    CHECK(E2eAckTestAccess::used(n) == 1);
    E2eAckTestAccess::clear(n, /*origin*/ 42, /*ctr*/ 200, /*sender_hash*/ 0x00000002u); // wrong hash -> no match
    CHECK(E2eAckTestAccess::used(n) == 1);
    E2eAckTestAccess::clear(n, /*origin*/ 42, /*ctr*/ 200, /*sender_hash*/ 0xABCDEF01u); // matching hash -> clears
    CHECK(E2eAckTestAccess::used(n) == 0);

    // (C) DELEGATED wrapper (mobile's own ctr, key=0 wildcard): the reverse-ack can arrive same-layer OR XL after a
    //     home/gateway hop -> matches on ctr alone, whatever sender_hash the ack-unification reverse-ack carries.
    E2eAckTestAccess::arm(n, /*key=*/0, /*is_xl=*/false, /*dst=*/0, /*ctr=*/300, protocol::e2e_ack_deadline_xl_ms);
    E2eAckTestAccess::clear(n, /*origin*/ 55, /*ctr*/ 300, /*sender_hash*/ 0x99887766u); // XL-shaped reverse-ack -> clears (ctr)
    CHECK(E2eAckTestAccess::used(n) == 0);
    E2eAckTestAccess::arm(n, /*key=*/0, /*is_xl=*/false, /*dst=*/0, /*ctr=*/301, protocol::e2e_ack_deadline_xl_ms);
    E2eAckTestAccess::clear(n, /*origin*/ 55, /*ctr*/ 301, /*sender_hash*/ 0);            // same-layer reverse-ack -> clears (ctr)
    CHECK(E2eAckTestAccess::used(n) == 0);
}

TEST_CASE("e2e-ack deadline — the one-shot timer re-arms to the EARLIEST pending deadline (park_reflood idiom)") {
    E2eHal hal; Node n = make_static(hal);
    hal.armed.clear();
    E2eAckTestAccess::arm(n, /*key=*/5, /*is_xl=*/false, /*dst=*/5, /*ctr=*/1, /*budget*/ 100000);
    E2eAckTestAccess::arm(n, /*key=*/6, /*is_xl=*/false, /*dst=*/6, /*ctr=*/2, /*budget*/ 40000);   // earlier
    // The LAST after() for our timer id must reflect the EARLIEST (40 s) deadline.
    uint32_t last_delay = 0; bool seen = false;
    for (auto& a : hal.armed) if (a.second == E2eAckTestAccess::timer_id()) { last_delay = a.first; seen = true; }
    CHECK(seen);
    CHECK(last_delay == 40000);
}
