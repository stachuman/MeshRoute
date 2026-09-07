// MeshRoute — test_node_remote_session.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 5 — the NODE-LEVEL suite: the REAL RTS -> DATA -> post-ACK ACCEPT path, the on-air
//     bootstrap answer (R-RA-31) and the ONE shared expiry timer.
//
// ⛔⛔ WHAT MAKES THIS A WIRING PROOF RATHER THAN A UNIT TEST: every request below is delivered as ACTUAL FRAMES
//     through `Node::on_recv` (a real RTS, a real `pack_data` DATA carrying a real `pack_unicast_inner` inner with
//     `SOURCE_HASH`, then the post-ACK timer), and every response is read back out of the HAL's CAPTURED TX BYTES
//     and DECODED with the controller's own key. ⛔ No production function is called directly to stand in for the
//     receive path, and ⛔ no emitted frame is asserted by name alone.
//
// ⚠ THERE IS NO CONTROLLER IMPLEMENTATION IN THE TREE (Slice 8a owns it). The fixture derives the controller-side
//   base key itself from the two identities, exactly as a controller will — so the two sides agree because they
//   compute the same function of the same ordered inputs, not because they share a call.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "frame_codec.h"
#include "identity.h"
#include "monocypher.h"
#include "node.h"
#include "protocol_constants.h"
#include "remote_codec.h"
#include "remote_session.h"
#include "support/test_hal.h"
#include "timer_wheel.h"   // §radmin-5: the kCap boundary is asserted against the REAL wheel

using namespace meshroute;

namespace {

struct TxFrame { std::string label; std::vector<uint8_t> bytes; };

class RsHal : public mrtest::TestHalBase {
  public:
    std::vector<TxFrame> tx_frames;
    std::vector<std::string> ev;
    std::vector<std::pair<uint32_t, uint32_t>> arms;   // (timer_id, delay)
    std::vector<uint32_t> cancels;
    // ★ A NON-DEGENERATE, DETERMINISTIC entropy stream. The base HAL writes ZEROS by design, and an all-zero
    //   64-bit draw is exactly what `Node::admin_draw_epoch` REFUSES — which is a case of its own below, driven
    //   by flipping this flag rather than by calling a different function.
    bool dead_rng = false;
    uint8_t rng_seed = 0x5A;
    void rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) o[i] = dead_rng ? 0 : static_cast<uint8_t>(rng_seed++ * 31 + 7);
    }
    TxResult tx_answer = TxResult::ok;
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        if (tx_answer != TxResult::ok) return tx_answer;
        tx_frames.push_back(TxFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n) });
        return TxResult::ok;
    }
    bool after(uint32_t delay, uint32_t id) override { arms.emplace_back(id, delay); return true; }
    void cancel(uint32_t id) override { cancels.push_back(id); }
    // ★ THE FIELDS ARE CAPTURED, not only the names: `xl_send_no_gateway`'s `target_layer` IS `hops[0]` — the
    //   first REVERSED destination hop `originate_layer_path` was handed — so recording it is the only way a
    //   case can prove the reversal produced the right path rather than merely that a reversal happened.
    std::vector<std::pair<std::string, int64_t>> ev_fields;   // (type.key, value) in emit order
    void emit(const char* type, const EventField* f, size_t n) override {
        const std::string t = type ? type : "";
        ev.push_back(t);
        for (size_t i = 0; i < n; ++i) ev_fields.emplace_back(t + "." + (f[i].key ? f[i].key : ""), f[i].i);
    }
    // The value of `type.key` on the LAST emit of `type`; `-1` when it never appeared.
    int64_t field(const char* type, const char* key) const {
        const std::string want = std::string(type) + "." + key;
        int64_t v = -1;
        for (const auto& kv : ev_fields) if (kv.first == want) v = kv.second;
        return v;
    }
    int count(const char* t) const { int n = 0; for (auto& e : ev) if (e == t) ++n; return n; }
    int armed(uint32_t id) const { int n = 0; for (auto& a : arms) if (a.first == id) ++n; return n; }
};

Identity make_identity(uint8_t tag) {
    uint8_t seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(tag * 11 + i * 5 + 3);
    Identity id{};
    identity_from_seed(id, seed);
    return id;
}
bool controller_base_key(const Identity& controller, const uint8_t target_ed[32], uint8_t out[32]) {
    uint8_t tx[32];
    ed_pub_to_x25519(tx, target_ed);
    uint8_t sh[32];
    if (remote_ecdh_shared(sh, controller, tx) != RemoteStatus::ok) return false;
    const bool ok = remote_kdf_base(out, sh, controller.ed_pub, target_ed) == RemoteStatus::ok;
    crypto_wipe(sh, sizeof sh);
    return ok;
}

// ---- REAL frame construction -------------------------------------------------------------------------------
size_t mk_rts(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo, uint8_t plen,
              std::array<uint8_t, 16>& b, uint8_t origin, uint16_t ctr) {
    rts_in in{};
    in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = 0; in.payload_len = plen;
    in.id = rts_flight_identity_plain(origin, ctr);
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}

// A DATA whose inner is the REAL `pack_unicast_inner` layout, carrying SOURCE_HASH (and optionally a
// cross-layer path). ⛔ Nothing is hand-assembled: the inner comes from the ONE byte-order authority.
size_t mk_data_rpc(uint8_t dst, uint16_t ctr, uint8_t origin, uint32_t source_hash,
                   std::span<const uint8_t> body, std::array<uint8_t, 300>& out, uint8_t type,
                   const uint8_t* layer_ids = nullptr, uint8_t n_layers = 0, uint8_t cur = 0,
                   uint32_t dst_hash = 0) {
    uint8_t flags = DATA_FLAG_SOURCE_HASH;
    // ★ A CROSS-LAYER DM MUST CARRY `DST_HASH == the recipient's key` OR THE RECEIVER BRIDGES IT ONWARD
    //   (node_mac_rx.cpp's Slice-4c.1 keystone). That is production behaviour this fixture must honour, not a
    //   convenience: a cross-layer request without it never reaches ANY consumer, remote-admin included.
    if (dst_hash) flags = static_cast<uint8_t>(flags | DATA_FLAG_DST_HASH);
    if (n_layers) flags = static_cast<uint8_t>(flags | DATA_FLAG_CROSS_LAYER);
    std::array<uint8_t, 280> inner{};
    const size_t in_len = pack_unicast_inner(std::span<uint8_t>(inner.data(), inner.size()), flags,
                                             dst_hash, layer_ids, n_layers, cur, origin,
                                             source_hash, body.data(), static_cast<uint8_t>(body.size()), 0, 0);
    if (in_len == 0) return 0;
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{};
    in.addr_len = 0; in.flags = flags; in.type = type; in.next = dst; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), in_len);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(out.data(), out.size()));
}

// ---- the target endpoint ------------------------------------------------------------------------------------
struct TargetNode {
    RsHal    hal;
    Identity self  = make_identity(2);
    Identity admin = make_identity(3);
    Node     node{hal, /*id=*/5, make_identity(2).key_hash32};
    std::vector<Identity> ctrl;

    TargetNode() {
        NodeConfig cfg;
        cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
        node.on_init(cfg);
        node.set_crypto_identity(self.x_secret, self.ed_pub);
    }

    // Install `n` OWNER controllers, through the SAME `RemoteSessionInstall` conversion path the firmware uses.
    void provision(uint8_t n) {
        RemoteSessionInstall p{};
        p.set_root = true;
        memcpy(p.x_secret, admin.x_secret, 32);
        memcpy(p.ed_pub,   admin.ed_pub,   32);
        p.set_acl = true;
        ctrl.clear();
        for (uint8_t i = 0; i < n; ++i) {
            ctrl.push_back(make_identity(static_cast<uint8_t>(30 + i)));
            memcpy(p.acl[i].ed_pub, ctrl.back().ed_pub, 32);
            p.acl[i].role  = kRadminRoleOwner;
            uint64_t e = 0;
            CHECK(node.admin_draw_epoch(e));          // ★ the REAL checked HAL draw
            p.epoch[i]     = e;
            p.epoch_set[i] = true;
        }
        node.admin_session_commit(p);
        CHECK(node.admin_session_readiness() == AdminReadiness::ready);
        CHECK(node.admin_session_slots() == n);
    }

    // Teach the node an AUTHORITATIVE id_bind for `who`, so a same-layer by-hash reply RESOLVES instead of parking.
    void learn_peer(uint8_t peer_id, const Identity& who) {
        std::array<uint8_t, 64> bb{};
        beacon_entry be{}; be.dest = peer_id; be.next = peer_id; be.score_bucket = 14; be.hops = 1;
        beacon_in bin{}; bin.leaf_id = 0; bin.src = peer_id; bin.key_hash32 = who.key_hash32;
        bin.entries = std::span<const beacon_entry>(&be, 1);
        const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
        RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
        node.on_recv(bb.data(), bn, bm);
    }

    std::vector<uint8_t> bootstrap_request(const Identity& who, uint64_t rid, uint32_t src,
                                            const RemoteCarrier& carrier) {
        uint8_t base[32] = {};
        CHECK(controller_base_key(who, admin.ed_pub, base));
        RemoteMessage m{};
        m.outer_type     = DATA_TYPE_REMOTE_CMD;
        m.opcode         = static_cast<uint8_t>(RemoteCmdOpcode::bootstrap);
        m.slot           = kRemoteSlotSentinel;
        m.request_id     = rid;
        m.controller_pub = std::span<const uint8_t>(who.ed_pub, 32);
        const RemoteKeys keys{ std::span<const uint8_t>(base, 32), {} };
        std::vector<uint8_t> out(260);
        size_t n = 0;
        const RemoteStatus s = remote_body_encode(std::span<uint8_t>(out.data(), out.size()), n, m,
                                                  std::span<const uint8_t>{}, keys, RemoteSource{true, src}, carrier);
        crypto_wipe(base, sizeof base);
        CHECK(s == RemoteStatus::ok);
        out.resize(n);
        return out;
    }

    // ONE real flight: RTS, then the DATA, then the post-ACK timer that runs `do_post_ack`.
    void flight(uint8_t from_id, uint16_t ctr, uint32_t source_hash, std::span<const uint8_t> body,
                uint64_t t, uint8_t type = DATA_TYPE_REMOTE_CMD,
                const uint8_t* layer_ids = nullptr, uint8_t n_layers = 0, uint8_t cur = 0,
                uint32_t dst_hash = 0) {
        RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
        std::array<uint8_t, 16> rb{};
        hal._now = t;
        node.on_recv(rb.data(), mk_rts(/*src=*/from_id, /*next=*/5, /*dst=*/5,
                                       static_cast<uint8_t>(ctr & 0x0F), /*plen=*/60, rb, from_id, ctr), meta);
        std::array<uint8_t, 300> db{};
        const size_t dn = mk_data_rpc(/*dst=*/5, ctr, from_id, source_hash, body, db, type,
                                      layer_ids, n_layers, cur, dst_hash);
        CHECK(dn > 0);
        hal._now = t + 100;
        node.on_recv(db.data(), dn, meta);
        node.on_timer(/*kPostAckTimerId=*/9);
    }

    // ★ DRIVE THE REAL MAC HAND-OFF. `enqueue_data` queues a TxItem; the node then airs an RTS and waits for a
    //   CTS before the DATA leaves. ⛔ Reading the queue instead would be exactly the "a success that isn't"
    //   shape — the whole point is to observe the BYTES that actually aired.
    void pump_tx(uint8_t next_hop) {
        // ★ `src_hint` MUST BE THE REAL NEXT HOP: `handle_ack` cross-checks it against `_pending_tx->next` and
        //   DROPS a mismatch (node_mac_rx.cpp). A wrong one here would leave the flight un-acked and the node
        //   BUSY — and the retry below would then never reach the accept arm at all, which is a false green.
        RxMeta m{ 12.0f, -70.0f, 0, static_cast<int8_t>(next_hop) };
        std::array<uint8_t, 8> cb{};
        cts_in ci{}; ci.chosen_data_sf = 7; ci.already_received = false; ci.tx_id = next_hop; ci.rx_id = 5;
        const size_t cn = pack_cts(ci, std::span<uint8_t>(cb.data(), cb.size()));
        hal._now += 10;
        node.on_recv(cb.data(), cn, m);
        hal._now += 10;
        node.on_timer(/*kCtsToDataGapTimerId=*/7);
        // …and COMPLETE the flight, so the node is free for the next one. Without this the second request of a
        // retry case would meet a still-pending TX and never reach the accept arm at all — a false green.
        std::array<uint8_t, 8> ab{};
        ack_in ai{}; ai.ctr_lo = static_cast<uint8_t>(last_tx_ctr_lo()); ai.budget_hint = 0; ai.snr_bucket = 0; ai.to = 5;
        const size_t an = pack_ack(ai, std::span<uint8_t>(ab.data(), ab.size()));
        hal._now += 10;
        node.on_recv(ab.data(), an, m);
    }
    // The low nibble of the ctr on the most recently aired DATA frame — the ACK's match key.
    uint8_t last_tx_ctr_lo() const {
        for (auto it = tx_frames_rbegin(); it != hal.tx_frames.rend(); ++it) {
            const std::span<const uint8_t> f(it->bytes.data(), it->bytes.size());
            auto d = parse_data(f);
            if (d) return static_cast<uint8_t>(d->ctr & 0x0F);
        }
        return 0;
    }
    std::vector<TxFrame>::const_reverse_iterator tx_frames_rbegin() const { return hal.tx_frames.rbegin(); }

    // The LAST captured TX frame that decodes as a DATA of `type`, unpacked to its RPC body. Empty when none.
    std::vector<uint8_t> last_rpc_body(uint8_t type, uint32_t* out_source_hash = nullptr) {
        for (auto it = hal.tx_frames.rbegin(); it != hal.tx_frames.rend(); ++it) {
            const std::span<const uint8_t> frame(it->bytes.data(), it->bytes.size());
            auto d = parse_data(frame);
            if (!d || d->type != type) continue;
            auto ui = parse_unicast_inner(data_inner(frame, *d), d->flags);
            if (!ui) continue;
            if (out_source_hash) *out_source_hash = ui->has_source_hash ? ui->source_hash : 0;
            return std::vector<uint8_t>(ui->body.begin(), ui->body.end());
        }
        return {};
    }
};

}  // namespace

// =============================================================================================================
// R-RA-31 — the on-air bootstrap answer, END TO END through real frames
// =============================================================================================================
TEST_CASE("§radmin-5/N1 a REAL bootstrap flight is authenticated and ANSWERED on air, and the answer decodes") {
    TargetNode t;
    t.provision(3);
    t.learn_peer(/*peer_id=*/7, t.ctrl[1]);
    const size_t tx_before = t.hal.tx_frames.size();

    RemoteCarrier req_carrier{};
    req_carrier.outer_data_type = DATA_TYPE_REMOTE_CMD;
    req_carrier.source_hash_on_wire = true;
    const auto req = t.bootstrap_request(t.ctrl[1], /*rid=*/0xC0FFEE, t.ctrl[1].key_hash32, req_carrier);
    t.flight(/*from_id=*/7, /*ctr=*/0x0021, t.ctrl[1].key_hash32, req, /*t=*/10000);
    t.pump_tx(/*next_hop=*/7);

    CHECK(t.hal.count("radmin_rx") == 1);
    CHECK(t.hal.count("radmin_bootstrap_tx") == 1);
    CHECK(t.hal.tx_frames.size() > tx_before);

    // ★★ THE ANSWER IS READ OUT OF THE AIRED BYTES, not out of a return value.
    uint32_t resp_src = 0;
    const auto body = t.last_rpc_body(DATA_TYPE_REMOTE_RESP, &resp_src);
    CHECK(body.size() == kRemoteOverheadBootstrapResponse);      // 33 B
    // ★ THE RESPONSE CARRIER'S OWN `SOURCE_HASH` IS THIS TARGET'S ROUTING IDENTITY — a DIFFERENT fact from the
    //   controller-domain source the crypto binds. ⛔ The physical source is never fed back as the codec's input.
    CHECK(resp_src == t.self.key_hash32);
    CHECK(resp_src != t.ctrl[1].key_hash32);

    // …and it opens under the CONTROLLER's base key with the ORIGINAL controller source, at the matched slot.
    uint8_t base[32] = {};
    CHECK(controller_base_key(t.ctrl[1], t.admin.ed_pub, base));
    RemoteCarrier resp_carrier{};
    resp_carrier.outer_data_type = DATA_TYPE_REMOTE_RESP;
    resp_carrier.dst_hash_on_wire = true;
    resp_carrier.source_hash_on_wire = true;
    RemoteDecoded d{};
    uint8_t pt[8] = {};
    const RemoteKeys keys{ std::span<const uint8_t>(base, 32), {} };
    const RemoteStatus st = remote_body_decode(d, DATA_TYPE_REMOTE_RESP,
                                               std::span<const uint8_t>(body.data(), body.size()), keys,
                                               RemoteSource{true, t.ctrl[1].key_hash32}, resp_carrier,
                                               std::span<uint8_t>(pt, sizeof pt));
    crypto_wipe(base, sizeof base);
    CHECK(st == RemoteStatus::ok);
    CHECK(d.authenticated);
    CHECK(d.layout.domain == RemoteDomainId::resp_bootstrap);
    CHECK(d.msg.slot == 1);
    CHECK(d.msg.request_id == 0xC0FFEEu);
    CHECK(d.msg.admin_epoch == t.node.admin_session_state().epoch[1]);
    CHECK(d.msg.admin_epoch != 0);

    // ⛔ READ-ONLY: no seen row, no epoch change, and the reserved staging row was RELEASED on the send outcome.
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 0);
    CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
}

TEST_CASE("§radmin-5/N2 a LOST response is recovered by an exact READ-ONLY retry — same epoch, same answer") {
    TargetNode t;
    t.provision(1);
    t.learn_peer(7, t.ctrl[0]);
    RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
    const auto req = t.bootstrap_request(t.ctrl[0], 0x1234, t.ctrl[0].key_hash32, rc);

    t.flight(7, 0x0031, t.ctrl[0].key_hash32, req, 10000);
    t.pump_tx(7);
    const auto first = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
    CHECK(first.size() == kRemoteOverheadBootstrapResponse);
    const uint64_t epoch_after_first = t.node.admin_session_state().epoch[0];

    // "The response was lost." The controller simply repeats the request — §7.2's whole point: a replayed
    // bootstrap cannot rotate or disrupt anything, because it is READ-ONLY.
    t.flight(7, 0x0032, t.ctrl[0].key_hash32, req, 20000);
    t.pump_tx(7);
    const auto second = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
    CHECK(second.size() == kRemoteOverheadBootstrapResponse);
    CHECK(t.node.admin_session_state().epoch[0] == epoch_after_first);   // ⛔ the epoch did NOT rotate
    CHECK(second == first);                                             // ★ byte-identical: same key, id and epoch
    CHECK(t.hal.count("radmin_bootstrap_tx") == 2);
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 0);  // ⛔ still no seen row consumed
}

TEST_CASE("§radmin-5/N3 SILENCE on air: an unknown key, a bad tag and a missing SOURCE_HASH all emit NOTHING") {
    TargetNode t;
    t.provision(2);
    t.learn_peer(7, t.ctrl[0]);
    RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;

    // (a) a stranger's key: authenticated shape, no ACL row.
    Identity stranger = make_identity(200);
    const auto s_req = t.bootstrap_request(stranger, 1, stranger.key_hash32, rc);
    t.flight(7, 0x41, stranger.key_hash32, s_req, 10000);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    CHECK(t.hal.count("radmin_bootstrap_tx") == 0);

    // (b) a corrupted tag on an otherwise valid request.
    auto bad = t.bootstrap_request(t.ctrl[0], 2, t.ctrl[0].key_hash32, rc);
    bad[bad.size() - 1] ^= 0x40;
    t.flight(7, 0x42, t.ctrl[0].key_hash32, bad, 20000);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    CHECK(t.hal.count("radmin_bootstrap_tx") == 0);

    // (c) NO SOURCE_HASH at all — R-RA-13. The arm refuses before the carrier is described.
    {
        RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
        std::array<uint8_t, 16> rb{};
        t.hal._now = 30000;
        t.node.on_recv(rb.data(), mk_rts(7, 5, 5, 3, 60, rb, 7, 0x43), meta);
        std::array<uint8_t, 300> db{};
        std::array<uint8_t, 280> inner{};
        const auto good = t.bootstrap_request(t.ctrl[0], 3, t.ctrl[0].key_hash32, rc);
        const size_t in_len = pack_unicast_inner(std::span<uint8_t>(inner.data(), inner.size()), /*flags=*/0,
                                                 0, nullptr, 0, 0, /*origin=*/7, /*source_hash=*/0,
                                                 good.data(), static_cast<uint8_t>(good.size()), 0, 0);
        const uint8_t mac[4] = {0,0,0,0};
        data_in in{}; in.addr_len = 0; in.flags = 0; in.type = DATA_TYPE_REMOTE_CMD; in.next = 5; in.dst = 5;
        in.hops_remaining = 31; in.ctr = 0x43;
        in.inner = std::span<const uint8_t>(inner.data(), in_len);
        in.mac = std::span<const uint8_t>(mac, 4);
        const size_t dn = pack_data(in, std::span<uint8_t>(db.data(), db.size()));
        t.hal._now = 30100;
        t.node.on_recv(db.data(), dn, meta);
        t.node.on_timer(9);
        CHECK(t.hal.count("radmin_rx_refused") >= 1);
        CHECK(t.hal.count("radmin_bootstrap_tx") == 0);
        CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    }
    // ⛔ AND ALL THREE ARE INDISTINGUISHABLE FROM OUTSIDE: not one of them aired a REMOTE_RESP.
    CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
}

TEST_CASE("§radmin-5/N4 the send ADMISSION is the SendDispatch, not the counter: parked and refused are distinguished") {
    // ---- PARKED: the reply destination is unresolved, so the transport stores it behind an H lookup. -------
    {
        TargetNode t;
        t.provision(1);
        // ⛔ NO `learn_peer`: the controller's hash has no binding, so `send_by_hash` PARKS.
        RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
        const auto req = t.bootstrap_request(t.ctrl[0], 1, t.ctrl[0].key_hash32, rc);
        t.flight(7, 0x51, t.ctrl[0].key_hash32, req, 10000);
        t.pump_tx(7);
        CHECK(t.hal.count("radmin_bootstrap_tx") == 1);        // the attempt HAPPENED and was classified
        // ★ A PARKED COPY IS TRANSPORT-OWNED AND IS NOT AIRED TX YET — no REMOTE_RESP DATA left the node.
        CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
        // ⛔ …and the staging row was still RELEASED on that checked outcome, not leaked.
        CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
    }
    // ---- QUEUED: with the binding learned, the same request really airs. ----------------------------------
    {
        TargetNode t;
        t.provision(1);
        t.learn_peer(7, t.ctrl[0]);
        RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
        const auto req = t.bootstrap_request(t.ctrl[0], 1, t.ctrl[0].key_hash32, rc);
        t.flight(7, 0x52, t.ctrl[0].key_hash32, req, 10000);
        t.pump_tx(7);
        CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).size() == kRemoteOverheadBootstrapResponse);
        CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
    }
}

TEST_CASE("§radmin-5/N5 CROSS-LAYER: the captured path is REVERSED once, at every full depth 2..4") {
    // The reversal is observed through `xl_send_no_gateway`'s own `target_layer` field — which IS `hops[0]`, the
    // first reversed destination hop `originate_layer_path` was handed. ⛔ No same-layer reply is substituted.
    for (uint8_t depth = 2; depth <= 4; ++depth) {
        TargetNode t;
        t.provision(1);
        t.learn_peer(7, t.ctrl[0]);
        // The received path ends on OUR layer (layer 0 here), so the reversal starts there.
        uint8_t ids[4] = { 0x31, 0x21, 0x11, 0x00 };
        uint8_t path[4] = {};
        for (uint8_t i = 0; i < depth; ++i) path[i] = ids[4 - depth + i];
        RemoteCarrier rc{};
        rc.outer_data_type = DATA_TYPE_REMOTE_CMD;
        rc.source_hash_on_wire = true;
        rc.cross_layer = true;
        rc.path_depth  = depth;
        rc.path_cursor = static_cast<uint8_t>(depth - 1);
        rc.dst_hash_on_wire = true;
        const auto req = t.bootstrap_request(t.ctrl[0], 0x700 + depth, t.ctrl[0].key_hash32, rc);
        t.flight(7, static_cast<uint16_t>(0x60 + depth), t.ctrl[0].key_hash32, req, 10000,
                 DATA_TYPE_REMOTE_CMD, path, depth, static_cast<uint8_t>(depth - 1), t.self.key_hash32);
        CHECK(t.hal.count("radmin_rx") == 1);
        CHECK(t.hal.count("radmin_bootstrap_tx") == 1);
        CHECK(t.hal.count("xl_send_no_gateway") == 1);          // no gateway is configured -> fail LOUD
        // ★★ THE REVERSAL ITSELF, MEASURED: `rev = [path[depth-1] .. path[0]]`, `rev[0]` is OUR layer, and the
        //    hops handed to `originate_layer_path` start at `rev[1] == path[depth-2]`. ⛔ Handing it `rev`
        //    whole (prepending our layer twice) or failing to reverse at all would land a DIFFERENT value here.
        CHECK(t.hal.field("xl_send_no_gateway", "target_layer") == path[depth - 2]);
        // ⛔ …and it is NOT the un-reversed first hop. ⓘ Only asserted from depth 3: at depth 2 `path[depth-2]`
        //   IS `path[0]`, so the discrimination is vacuous there and stating it would be decoration.
        if (depth >= 3) CHECK(t.hal.field("xl_send_no_gateway", "target_layer") != path[0]);
        CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());  // ⛔ and NO same-layer substitute was aired
        CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
    }
}

TEST_CASE("§radmin-5/N6 an UNREVERSIBLE cross-layer path is refused loudly, never answered same-layer") {
    TargetNode t;
    t.provision(1);
    t.learn_peer(7, t.ctrl[0]);
    // A two-hop path that does NOT terminate on our layer: the reversal cannot produce a valid return leg.
    uint8_t path[2] = { 0x21, 0x31 };            // our layer is 0x00; neither end is ours
    RemoteCarrier rc{};
    rc.outer_data_type = DATA_TYPE_REMOTE_CMD;
    rc.source_hash_on_wire = true;
    rc.cross_layer = true; rc.path_depth = 2; rc.path_cursor = 1; rc.dst_hash_on_wire = true;
    const auto req = t.bootstrap_request(t.ctrl[0], 0x800, t.ctrl[0].key_hash32, rc);
    t.flight(7, 0x70, t.ctrl[0].key_hash32, req, 10000, DATA_TYPE_REMOTE_CMD, path, 2, 1, t.self.key_hash32);
    CHECK(t.hal.count("radmin_reply_bad_path") == 1);
    CHECK(t.hal.count("radmin_bootstrap_tx") == 0);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());   // ⛔ NO same-layer fallback
    CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
}

// =============================================================================================================
// The entropy seam and the cold-boot refusal
// =============================================================================================================
TEST_CASE("§radmin-5/N7 a DEAD RNG refuses the epoch draw, and the node stays disabled with nothing installed") {
    TargetNode t;
    t.hal.dead_rng = true;
    uint64_t e = 0xFFFF;
    CHECK_FALSE(t.node.admin_draw_epoch(e));                 // ⛔ an all-zero draw REFUSES
    CHECK(e == 0xFFFFu);                                     // ⛔ and leaves the caller's value untouched
    t.node.admin_session_entropy_failed();
    CHECK(t.node.admin_session_readiness() == AdminReadiness::entropy_failed);
    CHECK(t.node.admin_session_slots() == 0);
    // ⛔ NOTHING IS ACCEPTED while it is in that state — a request is refused before the carrier is described.
    RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
    Identity anyone = make_identity(40);
    uint8_t base[32] = {};
    CHECK(controller_base_key(anyone, t.admin.ed_pub, base));
    RemoteMessage m{};
    m.outer_type = DATA_TYPE_REMOTE_CMD;
    m.opcode = static_cast<uint8_t>(RemoteCmdOpcode::bootstrap);
    m.slot = kRemoteSlotSentinel; m.request_id = 1;
    m.controller_pub = std::span<const uint8_t>(anyone.ed_pub, 32);
    std::vector<uint8_t> req(260); size_t n = 0;
    const RemoteKeys keys{ std::span<const uint8_t>(base, 32), {} };
    CHECK(remote_body_encode(std::span<uint8_t>(req.data(), req.size()), n, m, std::span<const uint8_t>{},
                             keys, RemoteSource{true, anyone.key_hash32}, rc) == RemoteStatus::ok);
    crypto_wipe(base, sizeof base);
    req.resize(n);
    t.flight(7, 0x81, anyone.key_hash32, req, 10000);
    CHECK(t.hal.count("radmin_bootstrap_tx") == 0);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    // ★ A LIVE RNG then produces a usable epoch through the SAME seam — so the refusal was the entropy, not the API.
    t.hal.dead_rng = false;
    CHECK(t.node.admin_draw_epoch(e));
    CHECK(e != 0);
}

// =============================================================================================================
// §4.5 — the ONE shared expiry timer, at the Node level
// =============================================================================================================
TEST_CASE("§radmin-5/N8 the wheel grew by exactly ONE id, and it is the ONE this slice allocated") {
    CHECK(TimerWheel::kCap == 92);
    CHECK(Node::test_last_allocated_timer_id() == 91);
    // ★ THE WHEEL ADMITS IT AND REFUSES THE NEXT — the boundary, not merely the constant.
    TimerWheel w;
    CHECK(w.after(10, Node::test_last_allocated_timer_id(), 0));
    CHECK_FALSE(w.after(10, Node::test_last_allocated_timer_id() + 1, 0));
}

TEST_CASE("§radmin-5/N9 an admitted request ARMS the shared scan; firing it releases the row and CANCELS") {
    TargetNode t;
    t.provision(1);
    t.learn_peer(7, t.ctrl[0]);
    // An authenticated EXECUTE reserves an ingress row (the owner's control partition) and arms the scan.
    uint8_t base[32] = {}, session[32] = {};
    CHECK(controller_base_key(t.ctrl[0], t.admin.ed_pub, base));
    CHECK(remote_kdf_session(session, base, t.node.admin_session_state().epoch[0]) == RemoteStatus::ok);
    RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
    RemoteMessage m{};
    m.outer_type = DATA_TYPE_REMOTE_CMD;
    m.opcode = static_cast<uint8_t>(RemoteCmdOpcode::auth_execute);
    m.slot = 0; m.request_id = 0xA5A5;
    const uint8_t cmdbody[6] = { 's','t','a','t','u','s' };
    std::vector<uint8_t> req(260); size_t n = 0;
    const RemoteKeys keys{ std::span<const uint8_t>(base, 32), std::span<const uint8_t>(session, 32) };
    CHECK(remote_body_encode(std::span<uint8_t>(req.data(), req.size()), n, m,
                             std::span<const uint8_t>(cmdbody, 6), keys,
                             RemoteSource{true, t.ctrl[0].key_hash32}, rc) == RemoteStatus::ok);
    crypto_wipe(base, sizeof base); crypto_wipe(session, sizeof session);
    req.resize(n);

    const int armed_before = t.hal.armed(91);
    t.flight(7, 0x91, t.ctrl[0].key_hash32, req, 50000);
    CHECK(t.hal.count("radmin_rx") == 1);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 1);
    CHECK(t.hal.armed(91) > armed_before);                    // ★ the shared scan was armed on id 91
    // ⛔ AND NOTHING WAS ANSWERED — an execute has no reply in this slice.
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());

    // Fire the scan BEFORE the deadline: nothing is released and it re-arms.
    t.hal._now = 50100 + radmin_staging_lifetime_ms - 1;
    t.node.on_timer(91);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    CHECK(t.hal.count("radmin_expired") == 0);

    // Fire it AT the deadline: the scratch goes, the SEEN row stays, and the timer is CANCELLED (nothing pends).
    const size_t cancels_before = t.hal.cancels.size();
    t.hal._now = 50100 + radmin_staging_lifetime_ms;
    t.node.on_timer(91);
    CHECK(t.hal.count("radmin_expired") == 1);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 0);
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 1);   // ⛔ the fingerprint is NOT time-evicted
    bool cancelled_91 = false;
    for (size_t i = cancels_before; i < t.hal.cancels.size(); ++i) if (t.hal.cancels[i] == 91) cancelled_91 = true;
    CHECK(cancelled_91);
    // ⇒ and the identical retry still classifies as a REPLAY rather than re-admitting.
    t.flight(7, 0x92, t.ctrl[0].key_hash32, req, 50100 + radmin_staging_lifetime_ms + 10);
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 1);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 0);   // ⛔ a replay reserves NOTHING
}

TEST_CASE("§radmin-5/N11 a ZERO reply destination is an explicit SEND FAILURE, never a fallback to the origin") {
    // ★★ THE SOURCE HASH IS PRESENT AND ITS VALUE IS 0 — a legal value (§radmin-5/G1 proves the classifier
    //    accepts it), so the request AUTHENTICATES and a reply is built. It is the TRANSMITTER that must refuse:
    //    0 is not a routable destination, and ⛔ falling back to the 8-bit `pa.origin` would answer a node that
    //    merely relayed the frame.
    TargetNode t;
    t.provision(1);
    t.learn_peer(7, t.ctrl[0]);
    RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
    const auto req = t.bootstrap_request(t.ctrl[0], 0x900, /*src=*/0, rc);
    t.flight(7, 0xA1, /*source_hash=*/0, req, 10000);
    CHECK(t.hal.count("radmin_rx") == 1);                    // the arm ran and CLASSIFIED it
    CHECK(t.hal.count("radmin_reply_no_dst") == 1);          // ★ and refused the SEND, loudly
    CHECK(t.hal.count("radmin_bootstrap_tx") == 0);          // ⛔ nothing was handed to the transport
    t.pump_tx(7);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());   // ⛔ and nothing aired
    // ⛔ …and the reserved staging row was still RELEASED on that checked outcome, not leaked.
    CHECK(remote_session_staging_used(t.node.admin_session_state()) == 0);
}

TEST_CASE("§radmin-5/N12 the reservation's deadline is stamped from the CURRENT time, not from zero") {
    TargetNode t;
    t.provision(1);
    t.learn_peer(7, t.ctrl[0]);
    uint8_t base[32] = {}, session[32] = {};
    CHECK(controller_base_key(t.ctrl[0], t.admin.ed_pub, base));
    CHECK(remote_kdf_session(session, base, t.node.admin_session_state().epoch[0]) == RemoteStatus::ok);
    RemoteCarrier rc{}; rc.outer_data_type = DATA_TYPE_REMOTE_CMD; rc.source_hash_on_wire = true;
    RemoteMessage m{};
    m.outer_type = DATA_TYPE_REMOTE_CMD;
    m.opcode = static_cast<uint8_t>(RemoteCmdOpcode::auth_execute);
    m.slot = 0; m.request_id = 0xB00B;
    const uint8_t body[2] = { 'h','i' };
    std::vector<uint8_t> req(260); size_t n = 0;
    const RemoteKeys keys{ std::span<const uint8_t>(base, 32), std::span<const uint8_t>(session, 32) };
    CHECK(remote_body_encode(std::span<uint8_t>(req.data(), req.size()), n, m,
                             std::span<const uint8_t>(body, 2), keys,
                             RemoteSource{true, t.ctrl[0].key_hash32}, rc) == RemoteStatus::ok);
    crypto_wipe(base, sizeof base); crypto_wipe(session, sizeof session);
    req.resize(n);
    const uint64_t kT = 400000;
    t.flight(7, 0xB1, t.ctrl[0].key_hash32, req, kT);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    // ★★ THE DEADLINE IS `receive time + the named lifetime`. A reservation stamped from ZERO would be born
    //    already expired at any realistic clock, and the very next scan would release work that just arrived.
    CHECK(remote_session_earliest_deadline(t.node.admin_session_state())
          == kT + 100 + radmin_staging_lifetime_ms);
    t.hal._now = kT + 100 + radmin_staging_lifetime_ms - 1;
    t.node.on_timer(91);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);   // ⛔ NOT released a millisecond early
}

TEST_CASE("§radmin-5/N10 an unprovisioned node draws nothing, arms nothing and stages nothing on init") {
    RsHal hal;
    Identity id = make_identity(9);
    Node node(hal, /*id=*/3, id.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    const size_t arms_before = hal.arms.size();
    CHECK(node.on_init(cfg));
    // ★★ THE RNG-ISOLATION CONTRACT, ASSERTED: `on_init` performs ZERO remote-admin draws, arms the shared scan
    //    ZERO times and publishes no readiness. This is what keeps all 36 deterministic corpus streams
    //    byte-identical BY CONSTRUCTION — every simulator node is exactly this node.
    CHECK(node.admin_session_readiness() == AdminReadiness::disabled);
    CHECK(node.admin_session_slots() == 0);
    CHECK(hal.armed(91) == 0);
    CHECK(hal.count("radmin_rx") == 0);
    CHECK(hal.count("radmin_expired") == 0);
    for (size_t i = arms_before; i < hal.arms.size(); ++i) CHECK(hal.arms[i].first != 91);
}
