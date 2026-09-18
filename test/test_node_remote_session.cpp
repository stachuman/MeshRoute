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
// The historical target fixtures derive their own controller key. The 8ac loopback at the end instead
// drives the production controller state inside a second Node, including its real response intake.
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
#include "../src/firmware_remote_executor.h"
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
    bool fixed_rng = false;
    uint64_t fixed_epoch = 0;
    unsigned epoch_draws = 0;
    uint8_t rng_seed = 0x5A;
    void rand_bytes(uint8_t* o, size_t n) override {
        if (n == sizeof(uint64_t)) ++epoch_draws;
        if (fixed_rng && n == sizeof(uint64_t)) {
            for (size_t i = 0; i < n; ++i) o[i] = static_cast<uint8_t>(fixed_epoch >> (8 * i));
            return;
        }
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
                   uint32_t dst_hash = 0, uint8_t extra_flags = 0) {
    uint8_t flags = DATA_FLAG_SOURCE_HASH | extra_flags;
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
struct TargetNode : mrfw::IRadminTarget {
    RsHal    hal;
    Identity self  = make_identity(2);
    Identity admin = make_identity(3);
    Node     node{hal, /*id=*/5, make_identity(2).key_hash32};
    std::vector<Identity> ctrl;

    bool next_admitted(RadminIngressView& v) override { return node.radmin_next_admitted(v); }
    bool reserve_transcript(uint8_t si, uint16_t n) override { return node.radmin_reserve_transcript(si, n); }
    void transcript_append(uint8_t si, const uint8_t* p, size_t n) override { node.radmin_transcript_append(si, p, n); }
    void transcript_complete(uint8_t si, RemoteTerminal r) override { node.radmin_transcript_complete(si, r); }
    bool tx_queue_full() override { return node.tx_queue_full(); }
    RadminSend send_next_frame() override { return node.radmin_send_frame(); }
    void expire() override { node.radmin_service_expire(); }
    bool service_control() override { return node.radmin_service_control(); }
    bool next_open(RadminOpenView& v) override { return node.radmin_next_open(v); }
    bool reserve_open(uint8_t i, uint16_t cap) override { return node.radmin_reserve_open(i, cap); }
    void open_append(uint8_t i, const uint8_t* p, size_t n) override { node.radmin_open_append(i, p, n); }
    void open_complete(uint8_t i, RemoteTerminal r) override { node.radmin_open_complete(i, r); }
    RadminSend send_open_frame() override { return node.radmin_send_open_frame(); }

    // B375 SYNTHETIC fault only: a real epoch installation wipes pending work. Keep the
    // mismatch scoped and restore the exact old epoch before any recovery/replay flight.
    struct EpochMismatch {
        uint64_t& field;
        const uint64_t saved;
        EpochMismatch(TargetNode& t, uint8_t slot)
            : field(const_cast<RemoteSessionState&>(t.node.admin_session_state()).epoch[slot]), saved(field) {
            field ^= 1;
        }
        ~EpochMismatch() { field = saved; }
    };

    void controller_session_key(uint8_t slot, uint8_t (&session)[32]) {
        uint8_t base[32] = {};
        CHECK(controller_base_key(ctrl[slot], admin.ed_pub, base));
        CHECK(remote_kdf_session(session, base, node.admin_session_state().epoch[slot]) == RemoteStatus::ok);
        crypto_wipe(base, sizeof base);
    }
    std::vector<uint8_t> auth_execute_request(uint8_t slot, uint64_t id, const RemoteCarrier& carrier,
                                             bool ack = false) {
        return session_request(slot, id, carrier, ack ? RemoteCmdOpcode::response_ack : RemoteCmdOpcode::auth_execute,
                               ctrl[slot].key_hash32);
    }
    std::vector<uint8_t> session_request(uint8_t slot, uint64_t id, const RemoteCarrier& carrier,
                                         RemoteCmdOpcode opcode, uint32_t source) {
        uint8_t session[32]; controller_session_key(slot, session);
        RemoteMessage m{}; m.outer_type = DATA_TYPE_REMOTE_CMD; m.slot = slot; m.request_id = id;
        m.opcode = static_cast<uint8_t>(opcode);
        const uint8_t command[] = {'s','t','a','t','u','s'};
        const std::span<const uint8_t> body = opcode == RemoteCmdOpcode::auth_execute
            ? std::span<const uint8_t>(command) : std::span<const uint8_t>{};
        std::vector<uint8_t> out(kRadminBodyBytes); size_t n = 0;
        CHECK(remote_body_encode(out, n, m, body, RemoteKeys{{}, session},
                                 RemoteSource{true, source}, carrier) == RemoteStatus::ok);
        crypto_wipe(session, sizeof session); out.resize(n); return out;
    }

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
                uint32_t dst_hash = 0, uint8_t extra_flags = 0) {
        RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
        std::array<uint8_t, 16> rb{};
        hal._now = t;
        node.on_recv(rb.data(), mk_rts(/*src=*/from_id, /*next=*/5, /*dst=*/5,
                                       static_cast<uint8_t>(ctr & 0x0F), /*plen=*/60, rb, from_id, ctr), meta);
        std::array<uint8_t, 300> db{};
        const size_t dn = mk_data_rpc(/*dst=*/5, ctr, from_id, source_hash, body, db, type,
                                      layer_ids, n_layers, cur, dst_hash, extra_flags);
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

namespace {
struct RadminOpenExec final : mrfw::IRadminExec {
    unsigned calls = 0;
    std::string output = "open output\n";
    std::string input;
    mrfw::CommandContext context{};
    mrfw::RadminExecResult result{mrfw::DispatchOutcome::completed, mrfw::RefuseReason::none};
    mrfw::RadminExecResult run(const char* p, size_t n, const mrfw::CommandContext& ctx,
                              mrfw::IRadminTranscriptSink& sink) override {
        ++calls; input.assign(p, n); context = ctx;
        sink.append(reinterpret_cast<const uint8_t*>(output.data()), output.size());
        return result;
    }
};
RemoteCarrier radmin_command_carrier() {
    RemoteCarrier c{}; c.outer_data_type = DATA_TYPE_REMOTE_CMD;
    c.source_hash_on_wire = true; c.dst_hash_on_wire = true; return c;
}
std::vector<uint8_t> radmin_open_request(uint64_t id, const std::string& line, const RemoteCarrier& carrier) {
    RemoteMessage m{}; m.outer_type = DATA_TYPE_REMOTE_CMD; m.slot = kRemoteSlotSentinel;
    m.opcode = static_cast<uint8_t>(RemoteCmdOpcode::open_execute); m.request_id = id;
    std::vector<uint8_t> bytes(kRadminBodyBytes); size_t n = 0;
    CHECK(remote_body_encode(bytes, n, m, {reinterpret_cast<const uint8_t*>(line.data()), line.size()}, {},
                             {true, 0}, carrier) == RemoteStatus::ok);
    bytes.resize(n); return bytes;
}
}

// Fixture entry points for test_node_remote_exec.cpp. Keep the original two-endpoint fixture in
// this translation unit (no fixture fork/file move); the new file owns the separately named cases.
void radmin7_node_exchange(size_t output_bytes) {
    struct CountingExec final : mrfw::IRadminExec {
        unsigned calls = 0; std::string output;
        mrfw::RadminExecResult run(const char* p, size_t n, const mrfw::CommandContext& ctx,
                                  mrfw::IRadminTranscriptSink& sink) override {
            ++calls; CHECK(std::string(p, n) == "status"); CHECK(ctx.acl_slot == 1);
            sink.append(reinterpret_cast<const uint8_t*>(output.data()), output.size());
            return {mrfw::DispatchOutcome::completed, mrfw::RefuseReason::none};
        }
    } exec;
    exec.output.assign(output_bytes, 'Q');
    TargetNode t; t.provision(2); t.learn_peer(7, t.ctrl[1]);
    RemoteCarrier cmd{}; cmd.outer_data_type = DATA_TYPE_REMOTE_CMD; cmd.dst_hash_on_wire = true;
    const auto request = t.auth_execute_request(1, 99, cmd);
    t.flight(7, 0xB1, t.ctrl[1].key_hash32, request, 10000);
    CHECK(exec.calls == 0); CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    mrfw::radmin_service_once(t, exec);
    CHECK(exec.calls == 1); CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 0);
    const auto drain = [&](std::string& text) {
        std::vector<std::vector<uint8_t>> frames;
        uint8_t session[32]; t.controller_session_key(1, session);
        for (unsigned i = 0; i < 9; ++i) {
            const uint8_t si = remote_transcript_next(t.node.admin_session_state());
            if (si == kRadminNoSlot) break;
            const auto& s = t.node.admin_session_state();
            const auto ti = s.seen[si].record.transcript_slot;
            const uint8_t before = s.transcripts[ti].next_seq_to_send;
            mrfw::radmin_service_once(t, exec);
            CHECK(s.transcripts[ti].next_seq_to_send == before + 1); // exactly ONE per service call
            t.pump_tx(7);
            uint32_t sender = 0;
            const auto frame = t.last_rpc_body(DATA_TYPE_REMOTE_RESP, &sender);
            CHECK_FALSE(frame.empty()); CHECK(sender == t.self.key_hash32);
            if (frame.empty()) break;
            std::array<uint8_t, kRadminBodyBytes> plain{}; RemoteDecoded d{};
            RemoteCarrier response = cmd; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
            CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, frame, RemoteKeys{{}, session},
                                     {true, t.ctrl[1].key_hash32}, response, plain) == RemoteStatus::ok);
            CHECK(d.msg.response_seq == i); CHECK(d.msg.request_id == 99); CHECK(d.msg.slot == 1);
            if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output))
                text.append(reinterpret_cast<const char*>(d.body.data()), d.body.size());
            else {
                CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::terminal));
                CHECK(d.terminal == (output_bytes > 1648 ? RemoteTerminal::output_truncated : RemoteTerminal::completed));
                CHECK(d.msg.response_seq == (output_bytes ? (std::min(output_bytes, size_t{1648}) + 205) / 206 : 0));
            }
            frames.push_back(frame);
        }
        crypto_wipe(session, sizeof session);
        CHECK(remote_transcript_next(t.node.admin_session_state()) == kRadminNoSlot);
        return frames;
    };
    std::string text; const auto first = drain(text);
    CHECK(text == exec.output.substr(0, 1648));
    CHECK(first.size() == (output_bytes ? (std::min(output_bytes, size_t{1648}) + 205) / 206 + 1 : 1));
    t.flight(7, 0xB2, t.ctrl[1].key_hash32, request, 20000);
    std::string replay; CHECK(drain(replay) == first); CHECK(replay == text); CHECK(exec.calls == 1);
    const auto ack = t.auth_execute_request(1, 99, cmd, true);
    t.flight(7, 0xB3, t.ctrl[1].key_hash32, ack, 30000);
    const auto& s = t.node.admin_session_state();
    const uint8_t si = remote_session_seen_find(s, 1, 99); CHECK(si < kRadminSeenSlots);
    if (si < kRadminSeenSlots) {
        CHECK(s.seen[si].record.state == static_cast<uint8_t>(SeenState::acknowledged));
        CHECK(s.seen[si].record.transcript_slot == kRadminNoTranscript);
    }
    t.flight(7, 0xB4, t.ctrl[1].key_hash32, ack, 40000);
    CHECK(remote_session_ingress_used(s) == 0); CHECK(remote_session_seen_used(s) == 1);
    t.flight(7, 0xB5, t.ctrl[1].key_hash32, request, 50000);
    mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 1);
    CHECK(remote_transcript_next(s) == kRadminNoSlot);
}

void radmin7_node_cross_layer(uint8_t depth) {
    struct PathExec final : mrfw::IRadminExec {
        unsigned calls = 0;
        std::string output = std::string(415, 'X');
        mrfw::RadminExecResult run(const char*, size_t, const mrfw::CommandContext&,
                                  mrfw::IRadminTranscriptSink& sink) override {
            ++calls; sink.append(reinterpret_cast<const uint8_t*>(output.data()), output.size());
            return {mrfw::DispatchOutcome::completed, mrfw::RefuseReason::none};
        }
    } exec;
    TargetNode t; t.provision(2); t.learn_peer(7, t.ctrl[1]);
    const uint8_t all[4] = {0x31, 0x21, 0x11, 0};
    const uint8_t* path = all + 4 - depth;
    RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_CMD;
    carrier.dst_hash_on_wire = true; carrier.cross_layer = true;
    carrier.path_depth = depth; carrier.path_cursor = depth - 1;
    const auto req = t.auth_execute_request(1, 99, carrier);
    t.flight(7, 0xE0, t.ctrl[1].key_hash32, req, 10000, DATA_TYPE_REMOTE_CMD,
             path, depth, depth - 1, t.self.key_hash32);
    mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 1);
    const auto& s = t.node.admin_session_state();
    const uint8_t si = remote_session_seen_find(s, 1, 99);
    CHECK(si < kRadminSeenSlots); if (si >= kRadminSeenSlots) return;
    const uint8_t ti = s.seen[si].record.transcript_slot;
    CHECK(ti < kRadminTranscriptSlots); if (ti >= kRadminTranscriptSlots) return;
    // A real, valid return path but no known gateway: loud synchronous enqueue refusal,
    // no cursor advance and no same-layer substitute. Learning a route then makes progress.
    mrfw::radmin_service_once(t, exec);
    CHECK(s.response_enqueue_failure == 1); CHECK(s.transcripts[ti].next_seq_to_send == 0);
    CHECK(t.hal.count("xl_send_no_gateway") == 1);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    CHECK(s.response_seal_failure == 0);
    CHECK(t.hal.field("radmin_response_enqueue_failure", "count") == 1);
    // SYNTHETIC counter seeding only; the no-gateway refusal itself is the real sender.
    auto& writable = const_cast<RemoteSessionState&>(s);
    writable.response_enqueue_failure = UINT16_MAX - 1;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        mrfw::radmin_service_once(t, exec);
        CHECK(s.response_enqueue_failure == UINT16_MAX);
        CHECK(s.response_seal_failure == 0);
        CHECK(s.transcripts[ti].next_seq_to_send == 0);
        CHECK(t.hal.field("radmin_response_enqueue_failure", "count") == UINT16_MAX);
    }
    writable.response_enqueue_failure = 1; // restore fixture counter, not transcript state
    schedule_record schedule[2] = {{0, 7, false, 250, 0, 50}, {1, 7, false, 250, 250, 50}};
    beacon_entry entry{}; entry.dest = entry.next = 9; entry.score_bucket = 14; entry.hops = 1;
    beacon_in b{}; b.src = 9; b.key_hash32 = 0x9595; b.self_gateway = true;
    b.schedule = schedule; b.entries = {&entry, 1};
    std::array<uint8_t, 100> beacon{}; const size_t bn = pack_beacon(b, beacon);
    CHECK(bn > 0); t.node.on_recv(beacon.data(), bn, RxMeta{12, -70, 0, 9});
    CHECK(t.node.rt_gateway_schedule(9) != nullptr);
    uint8_t session[32]; t.controller_session_key(1, session);
    std::string text; unsigned frames = 0, terminals = 0;
    size_t cap = 0;
    carrier.outer_data_type = DATA_TYPE_REMOTE_RESP; carrier.path_cursor = 1;
    CHECK(remote_body_cap(carrier, cap) == RemoteStatus::ok);
    CHECK(s.transcripts[ti].chunk_bytes == cap - kRemoteOverheadAuthResponse);
    for (unsigned i = 0; i < 9 && remote_transcript_next(s) != kRadminNoSlot; ++i) {
        const auto before = t.hal.tx_frames.size();
        mrfw::radmin_service_once(t, exec); t.pump_tx(9);
        bool found = false;
        for (size_t j = before; j < t.hal.tx_frames.size(); ++j) {
            const auto& raw = t.hal.tx_frames[j].bytes;
            const auto data = parse_data(raw);
            if (!data || data->type != DATA_TYPE_REMOTE_RESP) continue;
            const auto in = parse_unicast_inner(data_inner(raw, *data), data->flags);
            CHECK(in.has_value()); if (!in) continue;
            found = true;
            CHECK(data->next == 9); CHECK(in->has_cross_layer); CHECK(in->n_layers == depth); CHECK(in->cur == 1);
            for (uint8_t k = 0; k < depth; ++k) CHECK(in->layer_ids[k] == path[depth - 1 - k]);
            CHECK(in->has_source_hash); CHECK(in->source_hash == t.self.key_hash32);
            CHECK(in->has_dst_hash); CHECK(in->dst_key_hash32 == t.ctrl[1].key_hash32);
            CHECK(in->body.size() <= cap);
            std::array<uint8_t, kRadminBodyBytes> plain{}; RemoteDecoded d{};
            CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, in->body, RemoteKeys{{}, session},
                {true, t.ctrl[1].key_hash32}, carrier, plain) == RemoteStatus::ok);
            CHECK(d.msg.response_seq == frames++);
            if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output))
                text.append(reinterpret_cast<const char*>(d.body.data()), d.body.size());
            else { ++terminals; CHECK(d.terminal == RemoteTerminal::completed); }
        }
        CHECK(found); CHECK(exec.calls == 1);
    }
    crypto_wipe(session, sizeof session);
    CHECK(text == exec.output); CHECK(terminals == 1); CHECK(frames == 4);
    CHECK(s.response_enqueue_failure == 1);
}

void radmin7_node_pressure(bool operator_waiter) {
    struct WaiterExec final : mrfw::IRadminExec {
        unsigned calls = 0;
        mrfw::RadminExecResult run(const char* p, size_t n, const mrfw::CommandContext& ctx,
                                  mrfw::IRadminTranscriptSink&) override {
            ++calls;
            CHECK(std::string(p, n) == "status");
            CHECK(ctx.transport == mrfw::CommandTransport::remote);
            return {mrfw::DispatchOutcome::completed, mrfw::RefuseReason::none};
        }
    } exec;
    TargetNode t; t.provision(3); t.learn_peer(7, t.ctrl[1]);
    RemoteSessionInstall plan{}; plan.set_acl = true;
    memcpy(plan.acl, t.node.admin_session_state().acl, sizeof plan.acl);
    plan.acl[2].role = kRadminRoleOperator;
    CHECK(t.node.admin_draw_epoch(plan.epoch[2])); plan.epoch_set[2] = true;
    t.node.admin_session_commit(plan);
    RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_CMD; carrier.dst_hash_on_wire = true;
    uint16_t ctr = 0xC0;
    for (uint64_t id = 1; id <= kRadminTranscriptSlots; ++id) {
        const auto req = t.auth_execute_request(1, id, carrier);
        t.flight(7, ++ctr, t.ctrl[1].key_hash32, req, id * 10000);
        mrfw::radmin_service_once(t, exec);
        CHECK(exec.calls == id);
        mrfw::radmin_service_once(t, exec); t.pump_tx(7);
        CHECK(remote_transcript_next(t.node.admin_session_state()) == kRadminNoSlot);
        const auto body = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
        CHECK(body.size() == kRemoteOverheadAuthResponse + 1); // retained silent terminal, not ACKed
    }
    const uint8_t slot = operator_waiter ? 2 : 1;
    const uint8_t sender = operator_waiter ? 8 : 7;
    t.learn_peer(sender, t.ctrl[slot]);
    const auto req = t.auth_execute_request(slot, 5, carrier);
    t.flight(sender, ++ctr, t.ctrl[slot].key_hash32, req, 50000);
    auto& s = t.node.admin_session_state();
    const auto si = remote_session_seen_find(s, slot, 5);
    CHECK(si < kRadminSeenSlots); if (si >= kRadminSeenSlots) return;
    RadminIngressView before{};
    const bool admitted = t.node.radmin_next_admitted(before);
    CHECK(admitted); if (!admitted) return;
    const std::vector<uint8_t> held(before.body.begin(), before.body.end());
    mrfw::radmin_service_once(t, exec);
    CHECK(exec.calls == 4); CHECK(s.transcript_exhaustion == 1);
    CHECK(s.seen[si].record.state == static_cast<uint8_t>(SeenState::admitted));
    CHECK(remote_session_ingress_used(s) == 1);
    // ACK authenticates in the real RTS/DATA/post-ACK path even with CONTROL occupied by
    // an owner waiter. It reserves no ingress and does not overwrite the waiter's body.
    const auto ack = t.auth_execute_request(1, 1, carrier, true);
    t.flight(7, ++ctr, t.ctrl[1].key_hash32, ack, 60000);
    RadminIngressView after{};
    const bool still_admitted = t.node.radmin_next_admitted(after);
    CHECK(still_admitted); if (!still_admitted) return;
    CHECK(after.seen_index == before.seen_index);
    CHECK(std::vector<uint8_t>(after.body.begin(), after.body.end()) == held);
    CHECK(remote_session_ingress_used(s) == 1);
    mrfw::radmin_service_once(t, exec);
    CHECK(exec.calls == 5); CHECK(remote_session_ingress_used(s) == 0);
    CHECK(s.seen[si].record.state == static_cast<uint8_t>(SeenState::completed));

    // A real full Node TX queue prevents even an enqueue attempt. Suspend only the TX
    // drain AFTER all receive/ACK proof above, then fill through the existing send seam.
    t.node.test_suspend_tx_drain(true);
    const uint8_t noise = 0;
    for (unsigned i = 0; i < 64 && !t.node.tx_queue_full(); ++i)
        (void)t.node.test_do_send_typed(7, &noise, 1, CryptIntent::off, 0, DATA_TYPE_REMOTE_RESP);
    CHECK(t.node.tx_queue_full()); if (!t.node.tx_queue_full()) return;
    const auto ti = s.seen[si].record.transcript_slot;
    const uint8_t seq = s.transcripts[ti].next_seq_to_send;
    const auto aired = t.hal.tx_frames.size();
    const auto failures = s.response_enqueue_failure;
    const auto seal_failures = s.response_seal_failure;
    mrfw::radmin_service_once(t, exec);
    CHECK(s.transcripts[ti].next_seq_to_send == seq);
    CHECK(s.response_enqueue_failure == failures); // no attempted send to misclassify as refusal
    CHECK(s.response_seal_failure == seal_failures);
    CHECK(t.node.radmin_send_frame() == RadminSend::none); // real sender has its own full-queue guard
    CHECK(s.response_enqueue_failure == failures);
    CHECK(s.response_seal_failure == seal_failures);
    CHECK(t.hal.tx_frames.size() == aired); CHECK(exec.calls == 5);
}

void radmin7_node_seal_failure() {
    struct SealExec final : mrfw::IRadminExec {
        unsigned calls = 0;
        const std::string output = std::string(415, 'S');
        mrfw::RadminExecResult run(const char*, size_t, const mrfw::CommandContext&,
                                  mrfw::IRadminTranscriptSink& sink) override {
            ++calls;
            sink.append(reinterpret_cast<const uint8_t*>(output.data()), output.size());
            return {mrfw::DispatchOutcome::completed, mrfw::RefuseReason::none};
        }
    } exec;
    TargetNode t; t.provision(2); t.learn_peer(7, t.ctrl[1]);
    RemoteCarrier cmd{}; cmd.outer_data_type = DATA_TYPE_REMOTE_CMD; cmd.dst_hash_on_wire = true;
    const auto request = t.auth_execute_request(1, 99, cmd);
    t.flight(7, 0xF1, t.ctrl[1].key_hash32, request, 10000);
    mrfw::radmin_service_once(t, exec);
    CHECK(exec.calls == 1);
    const auto& s = t.node.admin_session_state();
    const auto si = remote_session_seen_find(s, 1, 99);
    CHECK(si < kRadminSeenSlots); if (si >= kRadminSeenSlots) return;
    const auto ti = s.seen[si].record.transcript_slot;
    CHECK(ti < kRadminTranscriptSlots); if (ti >= kRadminTranscriptSlots) return;
    CHECK(s.transcripts[ti].frames == 4);
    uint8_t session[32]; t.controller_session_key(1, session);
    RemoteCarrier response = cmd; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
    const auto send_one = [&](uint8_t seq) {
        const auto tx_before = t.hal.tx_frames.size();
        CHECK(s.transcripts[ti].next_seq_to_send == seq);
        mrfw::radmin_service_once(t, exec);
        CHECK(s.transcripts[ti].next_seq_to_send == seq + 1);
        t.pump_tx(7);
        CHECK(t.hal.tx_frames.size() > tx_before);
        const auto frame = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
        CHECK_FALSE(frame.empty());
        RemoteDecoded d{}; std::array<uint8_t, kRadminBodyBytes> plain{};
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, frame, RemoteKeys{{}, session},
                                 {true, t.ctrl[1].key_hash32}, response, plain) == RemoteStatus::ok);
        CHECK(d.msg.response_seq == seq); CHECK(d.msg.request_id == 99); CHECK(d.msg.slot == 1);
        if (seq == 3) CHECK(d.terminal == RemoteTerminal::completed);
        else CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output));
        CHECK(exec.calls == 1);
        return frame;
    };
    std::vector<std::vector<uint8_t>> original{send_one(0)};
    CHECK(s.transcripts[ti].next_seq_to_send == 1); // reset-to-zero must not pass the failure test
    {
        TargetNode::EpochMismatch fault(t, 1);
        auto& writable = const_cast<RemoteSessionState&>(s);
        const auto failed_attempt = [&](uint16_t expected_count) {
            std::array<uint8_t, sizeof(RemoteSessionState)> expected{};
            memcpy(expected.data(), &s, expected.size());
            memcpy(expected.data() + offsetof(RemoteSessionState, response_seal_failure),
                   &expected_count, sizeof expected_count);
            const auto queued = t.node.test_tx_queue_n();
            const auto aired = t.hal.tx_frames.size();
            const auto emits = t.hal.ev.size();
            const auto fields = t.hal.ev_fields.size();
            mrfw::radmin_service_once(t, exec); // REAL sender/encoder, synthetic epoch guard refusal
            CHECK(memcmp(expected.data(), &s, expected.size()) == 0);
            CHECK(s.transcripts[ti].next_seq_to_send == 1);
            CHECK(s.response_seal_failure == expected_count);
            CHECK(s.response_enqueue_failure == 0);
            CHECK(t.node.test_tx_queue_n() == queued);
            CHECK(t.hal.tx_frames.size() == aired);
            CHECK(t.hal.ev.size() == emits + 1); // no send_by_hash/transport event
            CHECK(t.hal.ev_fields.size() == fields + 1); // scalar count only; no result bytes
            CHECK(t.hal.ev.back() == "radmin_response_seal_failure");
            CHECK(t.hal.field("radmin_response_seal_failure", "count") == expected_count);
            CHECK(exec.calls == 1);
        };
        failed_attempt(1);
        writable.response_seal_failure = UINT16_MAX - 1; // labelled synthetic saturation boundary
        failed_attempt(UINT16_MAX);
        failed_attempt(UINT16_MAX);
    } // restore the exact epoch BEFORE recovery; normal invalidation was never invoked
    for (uint8_t seq = 1; seq < 4; ++seq) original.push_back(send_one(seq));
    CHECK(s.response_seal_failure == UINT16_MAX); CHECK(s.response_enqueue_failure == 0);
    CHECK(remote_transcript_next(s) == kRadminNoSlot);
    const auto before_idle = t.hal.ev.size();
    mrfw::radmin_service_once(t, exec);
    CHECK(t.hal.ev.size() == before_idle); CHECK(exec.calls == 1);
    CHECK(s.response_seal_failure == UINT16_MAX); CHECK(s.response_enqueue_failure == 0);
    // Above resumed seq1 on the next eligible pass without a request. THIS is the separate
    // authenticated exact request retry, which restarts at zero and replays all original bodies.
    t.flight(7, 0xF2, t.ctrl[1].key_hash32, request, 20000);
    CHECK(s.transcripts[ti].next_seq_to_send == 0);
    for (uint8_t seq = 0; seq < 4; ++seq) CHECK(send_one(seq) == original[seq]);
    CHECK(exec.calls == 1);
    crypto_wipe(session, sizeof session);
}

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

    // B369 withdraws "scratch goes, seen stays" for never-executed work. Both go at the deadline.
    const size_t cancels_before = t.hal.cancels.size();
    t.hal._now = 50100 + radmin_staging_lifetime_ms;
    t.node.on_timer(91);
    CHECK(t.hal.count("radmin_expired") == 1);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 0);
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 0);   // never executed, no tombstone to protect
    bool cancelled_91 = false;
    for (size_t i = cancels_before; i < t.hal.cancels.size(); ++i) if (t.hal.cancels[i] == 91) cancelled_91 = true;
    CHECK(cancelled_91);
    // Old assertion "retry is a replay rather than re-admitting" is superseded: it re-admits fresh.
    t.flight(7, 0x92, t.ctrl[0].key_hash32, req, 50100 + radmin_staging_lifetime_ms + 10);
    CHECK(remote_session_seen_used(t.node.admin_session_state()) == 1);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
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
    const auto counters = t.node.radmin_counters();
    CHECK(counters.response_enqueue_failure == 1);
    CHECK(counters.inbound_refusal == 0); CHECK(counters.open_rate_refusal == 0); CHECK(counters.response_seal_failure == 0);
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

TEST_CASE("§radmin-7b2/Node safe rollover prepares a base-key result then rejects old-session traffic") {
    TargetNode t; t.provision(2); t.learn_peer(7, t.ctrl[0]); RadminOpenExec exec;
    const auto carrier = radmin_command_carrier();
    const auto old = t.auth_execute_request(0, 55, carrier);
    const auto req = t.session_request(0, 44, carrier, RemoteCmdOpcode::safe_rollover, t.ctrl[0].key_hash32);
    const auto before = t.node.admin_session_state();
    t.flight(7, 0x300, t.ctrl[0].key_hash32, req, 10000);
    CHECK(t.node.admin_session_state().epoch[0] == before.epoch[0]);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    const unsigned draws = t.hal.epoch_draws;
    mrfw::radmin_service_once(t, exec);
    const auto& s = t.node.admin_session_state();
    CHECK(exec.calls == 0); CHECK(t.hal.epoch_draws == draws + 1);
    CHECK(s.epoch[0] != before.epoch[0]); CHECK(s.epoch[1] == before.epoch[1]);
    CHECK(remote_session_seen_used(s) == 0); CHECK(remote_session_ingress_used(s) == 0);
    t.pump_tx(7);
    const auto wire = t.last_rpc_body(DATA_TYPE_REMOTE_RESP); CHECK(wire.size() == 34);
    uint8_t base[32] = {}, pt[32] = {}; RemoteDecoded d{};
    CHECK(controller_base_key(t.ctrl[0], t.admin.ed_pub, base));
    auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
    CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, wire, {base, {}}, {true, t.ctrl[0].key_hash32}, response, pt)
          == RemoteStatus::ok);
    CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::rollover_result));
    CHECK(d.msg.request_id == 44); CHECK(d.msg.slot == 0); CHECK(d.msg.admin_epoch == s.epoch[0]);
    CHECK(d.msg.abandoned_count == 0); crypto_wipe(base, sizeof base);
    t.flight(7, 0x301, t.ctrl[0].key_hash32, old, 20000);
    CHECK(s.inbound_refusal == 1); CHECK(remote_session_seen_used(s) == 0);
    mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 0);
    const auto fresh = t.auth_execute_request(0, 55, carrier);
    t.flight(7, 0x302, t.ctrl[0].key_hash32, fresh, 30000);
    mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 1);
    CHECK(s.response_seal_failure == 0); CHECK(s.response_enqueue_failure == 0);
}

TEST_CASE("§radmin-7b2/Node zero and current-epoch draws refuse exactly once without replacing the epoch") {
    for (const bool equal : {false, true}) {
        TargetNode t; t.provision(1); t.learn_peer(7, t.ctrl[0]); RadminOpenExec exec;
        const auto carrier = radmin_command_carrier();
        const uint64_t epoch = t.node.admin_session_state().epoch[0];
        const auto req = t.session_request(0, 9, carrier, RemoteCmdOpcode::safe_rollover, t.ctrl[0].key_hash32);
        t.flight(7, 0x310, t.ctrl[0].key_hash32, req, 10000);
        t.hal.fixed_rng = true; t.hal.fixed_epoch = equal ? epoch : 0;
        const unsigned draws = t.hal.epoch_draws;
        mrfw::radmin_service_once(t, exec);
        CHECK(t.hal.epoch_draws == draws + 1); CHECK(t.node.admin_session_state().epoch[0] == epoch);
        CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 0); CHECK(exec.calls == 0);
        t.hal.fixed_rng = false; t.pump_tx(7);
        const auto wire = t.last_rpc_body(DATA_TYPE_REMOTE_RESP); CHECK(wire.size() == kRemoteOverheadAdmissionResult);
        uint8_t key[32], pt[1]; t.controller_session_key(0, key); RemoteDecoded d{};
        auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, wire, {{}, key}, {true, t.ctrl[0].key_hash32}, response, pt)
              == RemoteStatus::ok);
        CHECK(d.msg.admission_code == RemoteAdmission::preparation_failed); CHECK(d.msg.admission_detail == 0);
        const auto counters = t.node.radmin_counters();
        CHECK(counters.inbound_refusal == 0); CHECK(counters.response_seal_failure == 0);
        CHECK(counters.response_enqueue_failure == 0); crypto_wipe(key, sizeof key);
    }
}

TEST_CASE("§radmin-7b2/Node present-zero source preserves a committed rollover and bootstrap recovers its epoch") {
    TargetNode t; t.provision(1); RadminOpenExec exec; const auto carrier = radmin_command_carrier();
    const auto req = t.session_request(0, 9, carrier, RemoteCmdOpcode::force_rollover, 0);
    const auto old_epoch = t.node.admin_session_state().epoch[0];
    t.flight(7, 0x320, 0, req, 10000);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    mrfw::radmin_service_once(t, exec);
    const auto& s = t.node.admin_session_state();
    CHECK(s.epoch[0] != old_epoch); CHECK(s.response_enqueue_failure == 1);
    CHECK(s.inbound_refusal == 0); CHECK(s.response_seal_failure == 0); CHECK(exec.calls == 0);
    CHECK(t.hal.count("radmin_reply_no_dst") == 1); CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    CHECK(remote_session_ingress_used(s) == 0);
    const auto epoch = s.epoch[0];
    t.learn_peer(7, t.ctrl[0]);
    const auto bootstrap = t.bootstrap_request(t.ctrl[0], 10, t.ctrl[0].key_hash32, carrier);
    t.flight(7, 0x321, t.ctrl[0].key_hash32, bootstrap, 20000); t.pump_tx(7);
    const auto response = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
    uint8_t base[32] = {}, pt[1] = {}; CHECK(controller_base_key(t.ctrl[0], t.admin.ed_pub, base));
    auto response_carrier = carrier; response_carrier.outer_data_type = DATA_TYPE_REMOTE_RESP; RemoteDecoded d{};
    CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, response, {base, {}}, {true, t.ctrl[0].key_hash32},
                             response_carrier, pt) == RemoteStatus::ok);
    CHECK(d.msg.admin_epoch == epoch); CHECK(s.response_enqueue_failure == 1);
    crypto_wipe(base, sizeof base);
}

TEST_CASE("§radmin-7b2/Node open output owns input streams clear frames and cannot be dispatched twice") {
    TargetNode t; t.provision(1); t.learn_peer(7, t.ctrl[0]); RadminOpenExec exec;
    exec.output.assign(500, 'Q'); exec.output[19] = '\0'; exec.output[220] = '\n';
    const auto carrier = radmin_command_carrier(); auto req = radmin_open_request(17, "status", carrier);
    t.flight(7, 0x330, t.ctrl[0].key_hash32, req, 10000);
    const auto& s = t.node.admin_session_state(); CHECK(remote_session_staging_used(s) == 1);
    const auto deadline = s.staging[0].expires_at_ms;
    std::fill(req.begin(), req.end(), 0xCC);
    mrfw::radmin_service_once(t, exec);
    CHECK(exec.calls == 1); CHECK(exec.input == "status");
    CHECK(exec.context.authority == mrfw::CommandAuthority::remote_open);
    CHECK(exec.context.transport == mrfw::CommandTransport::remote); CHECK_FALSE(exec.context.physical_presence);
    CHECK(exec.context.acl_slot == kRadminNoSlot); CHECK(exec.context.request_id == 17);
    CHECK(exec.context.line_max_bytes == console::remote_command_max_bytes);
    CHECK(remote_session_seen_used(s) == 0); CHECK(remote_session_ingress_used(s) == 0);
    std::string output; unsigned frames = 0, terminals = 0;
    while (remote_open_next(s) != kRadminNoSlot && frames < 10) {
        mrfw::radmin_service_once(t, exec); t.pump_tx(7);
        const auto bytes = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
        RemoteDecoded d{}; uint8_t pt[kRadminBodyBytes] = {};
        auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, bytes, {}, {true, t.ctrl[0].key_hash32}, response, pt)
              == RemoteStatus::ok);
        CHECK(d.msg.response_seq == frames++); CHECK(d.msg.slot == kRemoteSlotSentinel); CHECK(d.msg.request_id == 17);
        if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output))
            output.append(reinterpret_cast<const char*>(d.body.data()), d.body.size());
        else { ++terminals; CHECK(d.terminal == RemoteTerminal::completed); }
    }
    CHECK(output == exec.output); CHECK(frames == 4); CHECK(terminals == 1); CHECK(exec.calls == 1);
    CHECK(s.staging[0].kind == static_cast<uint8_t>(OpenStagingKind::open_cooldown));
    const OpenCapture empty{}; CHECK(std::memcmp(&s.open[0], &empty, sizeof empty) == 0);
    CHECK(s.staging[0].expires_at_ms == deadline);
    const auto retry = radmin_open_request(18, "routes", carrier);
    t.flight(7, 0x331, t.ctrl[0].key_hash32, retry, 20000);
    CHECK(s.open_rate_refusal == 1); CHECK(s.inbound_refusal == 0); mrfw::radmin_service_once(t, exec);
    CHECK(exec.calls == 1); CHECK(s.staging[0].expires_at_ms == deadline);
    t.hal._now = deadline; mrfw::radmin_service_once(t, exec); CHECK(remote_session_staging_used(s) == 0);
}

TEST_CASE("§radmin-7b2/Node present-zero open sender refuses retains frozen capture and expires without a terminal") {
    TargetNode t; t.provision(1); RadminOpenExec exec; const auto carrier = radmin_command_carrier();
    const auto req = radmin_open_request(19, "status", carrier);
    t.flight(7, 0x340, 0, req, 10000); mrfw::radmin_service_once(t, exec);
    const auto& s = t.node.admin_session_state(); const auto capture = s.open[0];
    CHECK(exec.calls == 1); CHECK(s.inbound_refusal == 0); CHECK(s.response_enqueue_failure == 0);
    for (unsigned attempt = 1; attempt <= 2; ++attempt) {
        mrfw::radmin_service_once(t, exec);
        CHECK(s.response_enqueue_failure == attempt); CHECK(t.hal.count("radmin_reply_no_dst") == attempt);
        CHECK(s.inbound_refusal == 0); CHECK(s.open_rate_refusal == 0); CHECK(s.response_seal_failure == 0);
        CHECK(std::memcmp(&s.open[0], &capture, sizeof capture) == 0);
        CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty()); CHECK(exec.calls == 1);
    }
    t.hal._now = s.staging[0].expires_at_ms;
    mrfw::radmin_service_once(t, exec);
    const OpenCapture empty{}; CHECK(std::memcmp(&s.open[0], &empty, sizeof empty) == 0);
    CHECK(remote_session_staging_used(s) == 0); CHECK(s.response_enqueue_failure == 2);
}

TEST_CASE("§radmin-7b2/Node full notice then other-slot rotation allows same-key execute without terminal nonce reuse") {
    TargetNode t; t.provision(2); t.learn_peer(7, t.ctrl[1]); t.learn_peer(8, t.ctrl[0]);
    RadminOpenExec exec; exec.output.clear(); const auto carrier = radmin_command_carrier();
    uint16_t ctr = 0x400; uint64_t now = 10000;
    for (uint64_t id = 1; id <= kRadminSeenSlots; ++id) {
        t.flight(7, ++ctr, t.ctrl[1].key_hash32, t.auth_execute_request(1, id, carrier), now += 1000);
        mrfw::radmin_service_once(t, exec); mrfw::radmin_service_once(t, exec); t.pump_tx(7);
        t.flight(7, ++ctr, t.ctrl[1].key_hash32, t.auth_execute_request(1, id, carrier, true), now += 1000);
        CHECK(remote_session_seen_used(t.node.admin_session_state()) == id);
    }
    const auto& s = t.node.admin_session_state(); const auto epoch = s.epoch[0];
    const auto retry = t.auth_execute_request(0, 100, carrier);
    t.flight(8, ++ctr, t.ctrl[0].key_hash32, retry, now += 1000); t.pump_tx(8);
    const auto full = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
    uint8_t key[32], pt[kRadminBodyBytes]; t.controller_session_key(0, key);
    auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP; RemoteDecoded d{};
    CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, full, {{}, key}, {true, t.ctrl[0].key_hash32}, response, pt)
          == RemoteStatus::ok);
    CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::admission_result));
    CHECK(d.msg.admission_code == RemoteAdmission::session_full); CHECK(s.inbound_refusal == 1);
    CHECK(remote_session_seen_used(s) == 16); CHECK(remote_session_ingress_used(s) == 0);
    const auto rotate = t.session_request(1, 101, carrier, RemoteCmdOpcode::safe_rollover, t.ctrl[1].key_hash32);
    t.flight(7, ++ctr, t.ctrl[1].key_hash32, rotate, now += 1000);
    mrfw::radmin_service_once(t, exec); t.pump_tx(7);
    CHECK(remote_session_seen_used(s) == 0); CHECK(s.epoch[0] == epoch);
    t.flight(8, ++ctr, t.ctrl[0].key_hash32, retry, now += 1000);
    mrfw::radmin_service_once(t, exec); mrfw::radmin_service_once(t, exec); t.pump_tx(8);
    const auto terminal = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
    CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, terminal, {{}, key}, {true, t.ctrl[0].key_hash32}, response, pt)
          == RemoteStatus::ok);
    CHECK(d.msg.request_id == 100); CHECK(d.msg.response_seq == 0); CHECK(d.terminal == RemoteTerminal::completed);
    CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::terminal));
    CHECK(full[0] != terminal[0]); CHECK(exec.calls == 17); CHECK(s.inbound_refusal == 1);
    t.flight(8, ++ctr, t.ctrl[0].key_hash32, t.auth_execute_request(0, 100, carrier, true), now += 1000);
    std::vector<uint8_t> acknowledged;
    for (unsigned i = 0; i < 2; ++i) {
        t.flight(8, ++ctr, t.ctrl[0].key_hash32, retry, now += 1000); t.pump_tx(8);
        const auto bytes = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, bytes, {{}, key}, {true, t.ctrl[0].key_hash32}, response, pt)
              == RemoteStatus::ok);
        CHECK(d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::protocol_error));
        CHECK(d.msg.response_seq == 0); CHECK(d.body.size() == 1); CHECK(d.body[0] == 0);
        if (i) CHECK(bytes == acknowledged); else acknowledged = bytes;
        mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 17);
        CHECK(remote_session_seen_used(s) == 1); CHECK(remote_session_ingress_used(s) == 0);
    }
    CHECK(s.inbound_refusal == 3); crypto_wipe(key, sizeof key);
}

TEST_CASE("§radmin-7b2/Node paused-executor synthetic guard refuses both controls target-wide") {
    for (const auto opcode : {RemoteCmdOpcode::safe_rollover, RemoteCmdOpcode::force_rollover}) {
        TargetNode t; t.provision(2); t.learn_peer(7, t.ctrl[0]); RadminOpenExec exec;
        RemoteSessionInstall role{}; role.set_acl = true;
        std::memcpy(role.acl, t.node.admin_session_state().acl, sizeof role.acl);
        role.acl[1].role = kRadminRoleOperator; t.node.admin_session_commit(role);
        const auto carrier = radmin_command_carrier();
        t.flight(8, 0x470, t.ctrl[1].key_hash32, t.auth_execute_request(1, 1, carrier), 10000);
        const auto& s = t.node.admin_session_state(); const auto si = remote_session_seen_find(s, 1, 1);
        CHECK(si < kRadminSeenSlots); if (si >= kRadminSeenSlots) return;
        CHECK(t.node.radmin_reserve_transcript(si, kRadminChunkBytes)); // explicitly paused synthetic executor
        const auto protected_seen = s.seen[si]; const auto protected_header = s.transcripts[s.seen[si].record.transcript_slot];
        const auto req = t.session_request(0, 2, carrier, opcode, t.ctrl[0].key_hash32);
        t.flight(7, 0x471, t.ctrl[0].key_hash32, req, 20000);
        const auto epoch = s.epoch[0]; const unsigned draws = t.hal.epoch_draws;
        mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 0); CHECK(t.hal.epoch_draws == draws);
        CHECK(s.epoch[0] == epoch); CHECK(std::memcmp(&s.seen[si], &protected_seen, sizeof protected_seen) == 0);
        CHECK(std::memcmp(&s.transcripts[s.seen[si].record.transcript_slot], &protected_header, sizeof protected_header) == 0);
        t.pump_tx(7); const auto wire = t.last_rpc_body(DATA_TYPE_REMOTE_RESP);
        uint8_t key[32], pt[1]; t.controller_session_key(0, key); RemoteDecoded d{};
        auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, wire, {{}, key}, {true, t.ctrl[0].key_hash32}, response, pt)
              == RemoteStatus::ok);
        CHECK(d.msg.admission_code == RemoteAdmission::executing); CHECK(d.msg.admission_detail == 0);
        CHECK(s.inbound_refusal == 0); CHECK(s.response_enqueue_failure == 0); CHECK(s.response_seal_failure == 0);
        crypto_wipe(key, sizeof key);
    }
}

TEST_CASE("§radmin-7b2/Node full TX defers control without draw and expires the original reservation") {
    TargetNode t; t.provision(2); RadminOpenExec exec; const auto carrier = radmin_command_carrier();
    const auto req = t.session_request(0, 1, carrier, RemoteCmdOpcode::safe_rollover, 0);
    t.flight(7, 0x480, 0, req, 10000);
    const auto before = t.node.admin_session_state();
    t.node.test_suspend_tx_drain(true); const uint8_t noise = 0;
    for (unsigned i = 0; i < 64 && !t.node.tx_queue_full(); ++i)
        (void)t.node.test_do_send_typed(7, &noise, 1, CryptIntent::off, 0, DATA_TYPE_REMOTE_RESP);
    CHECK(t.node.tx_queue_full()); if (!t.node.tx_queue_full()) return;
    const auto draws = t.hal.epoch_draws; const auto aired = t.hal.tx_frames.size();
    mrfw::radmin_service_once(t, exec);
    CHECK(std::memcmp(&before, &t.node.admin_session_state(), sizeof before) == 0);
    CHECK(t.hal.epoch_draws == draws); CHECK(t.hal.tx_frames.size() == aired); CHECK(exec.calls == 0);
    CHECK_FALSE(t.node.radmin_service_control()); CHECK(t.hal.epoch_draws == draws);
    t.hal._now = before.ingress[kRadminIngressControl].expires_at_ms - 1;
    mrfw::radmin_service_once(t, exec); CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 1);
    ++t.hal._now; mrfw::radmin_service_once(t, exec);
    CHECK(remote_session_ingress_used(t.node.admin_session_state()) == 0);
    CHECK(t.hal.epoch_draws == draws); CHECK(t.node.admin_session_state().epoch[0] == before.epoch[0]);
    CHECK(t.node.radmin_counters().response_enqueue_failure == 0);
}

TEST_CASE("§radmin-7b2/Node busy two to one uses distinct nonces and force preserves later other-slot wire state") {
    TargetNode t; t.provision(3); RadminOpenExec exec;
    for (uint8_t slot = 0; slot < 3; ++slot) t.learn_peer(7 + slot, t.ctrl[slot]);
    const auto carrier = radmin_command_carrier(); uint64_t now = 10000; uint16_t ctr = 0x500;
    for (const auto pair : {std::pair<uint8_t,uint64_t>{0,1}, {1,2}, {2,3}, {0,4}}) {
        const auto [slot, id] = pair;
        t.flight(7 + slot, ++ctr, t.ctrl[slot].key_hash32, t.auth_execute_request(slot, id, carrier), now += 1000);
        mrfw::radmin_service_once(t, exec);
        mrfw::radmin_service_once(t, exec); t.pump_tx(7 + slot);
        mrfw::radmin_service_once(t, exec); t.pump_tx(7 + slot);
    }
    CHECK(exec.calls == 4);
    const auto safe = t.session_request(0, 10, carrier, RemoteCmdOpcode::safe_rollover, t.ctrl[0].key_hash32);
    uint8_t key[32]; t.controller_session_key(0, key);
    uint8_t nonces[2][24] = {}; auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
    const auto& s = t.node.admin_session_state(); const uint64_t epoch = s.epoch[0];
    for (uint8_t i = 0; i < 2; ++i) {
        t.flight(7, ++ctr, t.ctrl[0].key_hash32, safe, now += 1000);
        const unsigned draws = t.hal.epoch_draws;
        mrfw::radmin_service_once(t, exec); CHECK(t.hal.epoch_draws == draws); t.pump_tx(7);
        const auto bytes = t.last_rpc_body(DATA_TYPE_REMOTE_RESP); RemoteDecoded d{}; uint8_t pt[1] = {};
        CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, bytes, {{}, key}, {true, t.ctrl[0].key_hash32}, response, pt)
              == RemoteStatus::ok);
        CHECK(d.msg.admission_code == RemoteAdmission::session_busy); CHECK(d.msg.admission_detail == 2 - i);
        RemoteLayout layout{}; CHECK(remote_layout(DATA_TYPE_REMOTE_RESP, bytes[0], layout) == RemoteStatus::ok);
        CHECK(remote_nonce(nonces[i], layout, d.msg, key, {true, t.ctrl[0].key_hash32}) == RemoteStatus::ok);
        CHECK(s.epoch[0] == epoch); CHECK(exec.calls == 4);
        if (!i) t.flight(7, ++ctr, t.ctrl[0].key_hash32, t.auth_execute_request(0, 4, carrier, true), now += 1000);
    }
    CHECK(std::memcmp(nonces[0], nonces[1], 24) != 0); crypto_wipe(key, sizeof key);
    // A real exact retry and one real send leave the first survivor's terminal pending at seq 1.
    t.flight(8, ++ctr, t.ctrl[1].key_hash32, t.auth_execute_request(1, 2, carrier), now += 1000);
    mrfw::radmin_service_once(t, exec); t.pump_tx(8);
    const uint8_t survivors[2] = {remote_session_seen_find(s, 1, 2), remote_session_seen_find(s, 2, 3)};
    CHECK(s.transcripts[s.seen[survivors[0]].record.transcript_slot].next_seq_to_send == 1);
    const auto force = t.session_request(0, 11, carrier, RemoteCmdOpcode::force_rollover, t.ctrl[0].key_hash32);
    t.flight(7, ++ctr, t.ctrl[0].key_hash32, force, now += 1000);
    const auto before = s;
    uint8_t pending[kRadminBodyBytes] = {}; size_t pn = 0;
    CHECK(remote_transcript_encode(s, survivors[0], pending, pn) == RemoteStatus::ok);
    mrfw::radmin_service_once(t, exec); t.pump_tx(7);
    const auto wire = t.last_rpc_body(DATA_TYPE_REMOTE_RESP); uint8_t base[32], pt[1]; RemoteDecoded result{};
    CHECK(controller_base_key(t.ctrl[0], t.admin.ed_pub, base));
    CHECK(remote_body_decode(result, DATA_TYPE_REMOTE_RESP, wire, {base, {}}, {true, t.ctrl[0].key_hash32}, response, pt)
          == RemoteStatus::ok);
    CHECK(result.msg.abandoned_count == 1); CHECK(result.msg.admin_epoch == s.epoch[0]); CHECK(s.epoch[0] != epoch);
    CHECK(remote_session_seen_find(s, 0, 1) == kRadminNoSlot); CHECK(remote_session_seen_find(s, 0, 4) == kRadminNoSlot);
    for (const auto si : survivors) {
        CHECK(std::memcmp(&s.seen[si], &before.seen[si], sizeof(SeenEntry)) == 0);
        const auto ti = s.seen[si].record.transcript_slot; auto expected = before.transcripts[ti]; --expected.order;
        CHECK(std::memcmp(&s.transcripts[ti], &expected, sizeof expected) == 0);
        for (uint16_t ci = expected.first_chunk; ci < kRadminChunkSlots; ci = s.chunks[ci].next)
            CHECK(std::memcmp(&s.chunks[ci], &before.chunks[ci], sizeof(TranscriptChunk)) == 0);
    }
    CHECK(s.epoch[1] == before.epoch[1]); CHECK(s.epoch[2] == before.epoch[2]);
    uint8_t after[kRadminBodyBytes] = {}; size_t an = 0;
    CHECK(remote_transcript_encode(s, survivors[0], after, an) == RemoteStatus::ok);
    CHECK(an == pn); CHECK(std::memcmp(after, pending, pn) == 0);
    CHECK(remote_transcript_next(s) == survivors[0]); CHECK(exec.calls == 4);
    CHECK(s.inbound_refusal == 0); CHECK(s.response_seal_failure == 0); CHECK(s.response_enqueue_failure == 0);
    crypto_wipe(base, sizeof base);
}

TEST_CASE("§radmin-7b2/Node open nonzero cursor survives synthetic encode and send faults then resumes") {
    TargetNode t; t.provision(1); t.learn_peer(7, t.ctrl[0]); RadminOpenExec exec; exec.output.assign(500, 'X');
    const auto carrier = radmin_command_carrier();
    t.flight(7, 0x580, t.ctrl[0].key_hash32, radmin_open_request(1, "status", carrier), 10000);
    mrfw::radmin_service_once(t, exec); mrfw::radmin_service_once(t, exec); t.pump_tx(7);
    auto& s = const_cast<RemoteSessionState&>(t.node.admin_session_state());
    CHECK(s.open[0].next_seq == 1); CHECK(s.open[0].next_offset == 222);
    const auto frozen = s.open[0]; const auto route = s.staging[0].route;
    s.staging[0].route.carrier = static_cast<uint8_t>(RadminCarrierKind::none); // labelled synthetic encoder fault
    mrfw::radmin_service_once(t, exec);
    CHECK(s.response_seal_failure == 1); CHECK(s.response_enqueue_failure == 0);
    CHECK(t.hal.field("radmin_response_seal_failure", "count") == 1);
    CHECK(std::memcmp(&s.open[0], &frozen, sizeof frozen) == 0);
    s.staging[0].route = route;
    s.staging[0].peer_source_hash = 0; // labelled synthetic sender fault AFTER seq0 was really sent
    mrfw::radmin_service_once(t, exec);
    CHECK(s.response_seal_failure == 1); CHECK(s.response_enqueue_failure == 1);
    CHECK(t.hal.field("radmin_response_enqueue_failure", "count") == 1);
    CHECK(std::memcmp(&s.open[0], &frozen, sizeof frozen) == 0);
    s.staging[0].peer_source_hash = t.ctrl[0].key_hash32;
    mrfw::radmin_service_once(t, exec); t.pump_tx(7);
    CHECK(s.open[0].next_seq == 2); CHECK(s.open[0].next_offset == 444); CHECK(exec.calls == 1);
    const auto wire = t.last_rpc_body(DATA_TYPE_REMOTE_RESP); RemoteDecoded d{}; uint8_t pt[kRadminBodyBytes] = {};
    auto response = carrier; response.outer_data_type = DATA_TYPE_REMOTE_RESP;
    CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, wire, {}, {true, t.ctrl[0].key_hash32}, response, pt) == RemoteStatus::ok);
    CHECK(d.msg.response_seq == 1); CHECK(d.body.size() == 222);
    CHECK(std::memcmp(d.body.data(), exec.output.data() + 222, 222) == 0);
    t.node.test_suspend_tx_drain(true); const uint8_t noise = 0;
    for (unsigned i = 0; i < 64 && !t.node.tx_queue_full(); ++i)
        (void)t.node.test_do_send_typed(7, &noise, 1, CryptIntent::off, 0, DATA_TYPE_REMOTE_RESP);
    CHECK(t.node.tx_queue_full()); if (!t.node.tx_queue_full()) return;
    const auto full_capture = s.open[0];
    CHECK(t.node.radmin_send_open_frame() == RadminSend::none); mrfw::radmin_service_once(t, exec);
    CHECK(std::memcmp(&s.open[0], &full_capture, sizeof full_capture) == 0);
    CHECK(s.response_seal_failure == 1); CHECK(s.response_enqueue_failure == 1);
}

TEST_CASE("§radmin-7b2/Node parked clear terminal transfers ownership even with raw counter zero") {
    TargetNode t; t.provision(1); RadminOpenExec exec; exec.output.clear();
    const auto carrier = radmin_command_carrier();
    // No peer route is taught: the actual by-hash sender parks the encoded terminal.
    t.flight(7, 0x590, t.ctrl[0].key_hash32, radmin_open_request(1, "status", carrier), 10000);
    mrfw::radmin_service_once(t, exec);
    CHECK(t.node.radmin_send_open_frame() == RadminSend::parked);
    const auto& s = t.node.admin_session_state();
    CHECK(s.staging[0].kind == static_cast<uint8_t>(OpenStagingKind::open_cooldown));
    const OpenCapture empty{}; CHECK(std::memcmp(&s.open[0], &empty, sizeof empty) == 0);
    CHECK(s.response_enqueue_failure == 0); CHECK(s.response_seal_failure == 0);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty()); CHECK(exec.calls == 1);
}

TEST_CASE("§radmin-7b2/Node clear returns reverse every real depth and price payload from the open overhead") {
    for (const uint8_t depth : {uint8_t(2), uint8_t(3), uint8_t(4)}) {
        TargetNode t; t.provision(1); RadminOpenExec exec; exec.output.assign(500, 'X');
        const uint8_t all[4] = {0x31, 0x21, 0x11, 0}; const uint8_t* path = all + 4 - depth;
        auto carrier = radmin_command_carrier(); carrier.cross_layer = true;
        carrier.path_depth = depth; carrier.path_cursor = depth - 1;
        const auto req = radmin_open_request(30, "routes", carrier);
        t.flight(7, 0x600, t.ctrl[0].key_hash32, req, 10000, DATA_TYPE_REMOTE_CMD, path, depth, depth - 1, t.self.key_hash32);
        mrfw::radmin_service_once(t, exec); CHECK(exec.calls == 1);
        const auto& s = t.node.admin_session_state();
        mrfw::radmin_service_once(t, exec);
        CHECK(s.response_enqueue_failure == 1); CHECK(s.open[0].next_seq == 0);
        CHECK(t.hal.count("xl_send_no_gateway") == 1); CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
        // Existing two-endpoint fixture: teach a real gateway schedule, then use its RTS/CTS/DATA/ACK pump.
        schedule_record schedule[2] = {{0, 7, false, 250, 0, 50}, {1, 7, false, 250, 250, 50}};
        beacon_entry entry{}; entry.dest = entry.next = 9; entry.score_bucket = 14; entry.hops = 1;
        beacon_in b{}; b.src = 9; b.key_hash32 = 0x9595; b.self_gateway = true;
        b.schedule = schedule; b.entries = {&entry, 1};
        std::array<uint8_t, 100> beacon{}; const size_t bn = pack_beacon(b, beacon);
        CHECK(bn > 0); t.node.on_recv(beacon.data(), bn, RxMeta{12, -70, 0, 9});
        CHECK(t.node.rt_gateway_schedule(9) != nullptr);
        carrier.outer_data_type = DATA_TYPE_REMOTE_RESP; carrier.path_cursor = 1; size_t cap = 0;
        CHECK(remote_body_cap(carrier, cap) == RemoteStatus::ok); CHECK(s.open[0].frame_cap == cap - 10);
        std::string assembled; unsigned frames = 0, terminals = 0;
        while (remote_open_next(s) != kRadminNoSlot && frames < 10) {
            const auto first = t.hal.tx_frames.size();
            mrfw::radmin_service_once(t, exec); t.pump_tx(9); unsigned emitted = 0;
            for (size_t j = first; j < t.hal.tx_frames.size(); ++j) {
                const auto& raw = t.hal.tx_frames[j].bytes; const auto data = parse_data(raw);
                if (!data || data->type != DATA_TYPE_REMOTE_RESP) continue;
                ++emitted; const auto inner = parse_unicast_inner(data_inner(raw, *data), data->flags);
                CHECK(inner.has_value()); if (!inner) continue;
                CHECK(data->next == 9); CHECK(inner->n_layers == depth); CHECK(inner->cur == 1);
                for (uint8_t i = 0; i < depth; ++i) CHECK(inner->layer_ids[i] == path[depth - 1 - i]);
                CHECK(inner->has_source_hash); CHECK(inner->source_hash == t.self.key_hash32);
                CHECK(inner->has_dst_hash); CHECK(inner->dst_key_hash32 == t.ctrl[0].key_hash32);
                RemoteDecoded d{}; uint8_t pt[kRadminBodyBytes] = {};
                CHECK(remote_body_decode(d, DATA_TYPE_REMOTE_RESP, inner->body, {}, {true, t.ctrl[0].key_hash32}, carrier, pt)
                      == RemoteStatus::ok);
                CHECK(d.msg.response_seq == frames++); CHECK(d.msg.request_id == 30); CHECK(d.msg.slot == kRemoteSlotSentinel);
                if (d.msg.opcode == static_cast<uint8_t>(RemoteRespOpcode::output))
                    assembled.append(reinterpret_cast<const char*>(d.body.data()), d.body.size());
                else { ++terminals; CHECK(d.terminal == RemoteTerminal::completed); }
            }
            CHECK(emitted == 1); CHECK(exec.calls == 1);
        }
        CHECK(assembled == exec.output); CHECK(terminals == 1); CHECK(frames == 4);
        CHECK(s.response_enqueue_failure == 1); CHECK(s.response_seal_failure == 0);
    }
}

namespace {
// This executor only prepares an opaque plan. Real firmware applies are separately driven by the action probe.
struct Radmin73Prepare final : mrfw::IRadminExec {
    TargetNode& t;unsigned calls=0;bool output=false;
    explicit Radmin73Prepare(TargetNode& target):t(target){}
    mrfw::RadminExecResult run(const char*,size_t,const mrfw::CommandContext& c,mrfw::IRadminTranscriptSink& sink) override {
        ++calls;
        CHECK(t.node.radmin_prepare_action(c.acl_slot,c.request_id,2,0,7006)==RemoteTerminal::scheduled);
        if(output) {const uint8_t b='X';sink.append(&b,1);}
        return {mrfw::DispatchOutcome::scheduled,mrfw::RefuseReason::none};
    }
};
}
TEST_CASE("§radmin-73/Node queued terminal owns clock once; OUTPUT and failed real sends never do") {
    TargetNode t;t.provision(2);t.learn_peer(7,t.ctrl[1]);Radmin73Prepare exec(t);exec.output=true;
    const auto carrier=radmin_command_carrier();const auto request=t.auth_execute_request(1,0,carrier);
    t.flight(7,0x701,t.ctrl[1].key_hash32,request,10000);mrfw::radmin_service_once(t,exec);
    const auto& s=t.node.admin_session_state();const auto si=remote_session_seen_find(s,1,0);
    CHECK(si<kRadminSeenSlots);if(si>=kRadminSeenSlots)return;const auto ti=s.seen[si].record.transcript_slot;
    CHECK(ti<kRadminTranscriptSlots);if(ti>=kRadminTranscriptSlots)return;CHECK(s.action.phase==RemoteActionPhase::prepared);
    const auto frozen=s.action;
    CHECK(t.node.radmin_send_frame()==RadminSend::queued);t.pump_tx(7);
    CHECK(s.action.phase==RemoteActionPhase::prepared);CHECK(s.action.activate_at_ms==0);
    CHECK(s.transcripts[ti].next_seq_to_send==1);
    {
        TargetNode::EpochMismatch fault(t,1); // labelled synthetic seal refusal, no production fault hook
        const auto queued=t.node.test_tx_queue_n();const auto aired=t.hal.tx_frames.size();
        CHECK(t.node.radmin_send_frame()==RadminSend::refused);
        CHECK(s.response_seal_failure==1);CHECK(s.response_enqueue_failure==0);
        CHECK(s.transcripts[ti].next_seq_to_send==1);CHECK(memcmp(&s.action,&frozen,sizeof frozen)==0);
        CHECK(t.node.test_tx_queue_n()==queued);CHECK(t.hal.tx_frames.size()==aired);
    }
    auto& writable=const_cast<RemoteSessionState&>(s);
    const auto source=writable.seen[si].record.source_hash;
    writable.seen[si].record.source_hash=0; // labelled synthetic checked-sender refusal after real OUTPUT
    CHECK(t.node.radmin_send_frame()==RadminSend::refused);
    CHECK(s.response_enqueue_failure==1);CHECK(s.response_seal_failure==1);
    CHECK(s.transcripts[ti].next_seq_to_send==1);CHECK(memcmp(&s.action,&frozen,sizeof frozen)==0);
    writable.seen[si].record.source_hash=source;
    const auto owned_at=t.hal.now();CHECK(t.node.radmin_send_frame()==RadminSend::queued);
    CHECK(s.action.phase==RemoteActionPhase::armed);CHECK(s.action.activate_at_ms==owned_at+7006);
    CHECK(s.transcripts[ti].next_seq_to_send==2);t.pump_tx(7);
    const auto terminal=t.last_rpc_body(DATA_TYPE_REMOTE_RESP);CHECK(terminal.size()==kRemoteOverheadAuthResponse+5);
    const auto deadline=s.action.activate_at_ms;
    t.flight(7,0x702,t.ctrl[1].key_hash32,request,t.hal.now()+1000);
    CHECK(s.transcripts[ti].next_seq_to_send==0);CHECK(exec.calls==1);
    CHECK(t.node.radmin_send_frame()==RadminSend::queued);t.pump_tx(7);
    CHECK(t.node.radmin_send_frame()==RadminSend::queued);t.pump_tx(7);
    CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP)==terminal);CHECK(s.action.activate_at_ms==deadline);
    t.hal._now=deadline-1;t.node.on_timer(Node::test_last_allocated_timer_id());CHECK(s.action.phase==RemoteActionPhase::armed);
    CHECK(t.hal.arms.back()==std::pair<uint32_t,uint32_t>{Node::test_last_allocated_timer_id(),1});
    DeferredActionRecord transfer{};CHECK_FALSE(t.node.radmin_take_action(transfer));
    t.hal._now=deadline;t.node.on_timer(Node::test_last_allocated_timer_id());CHECK(s.action.phase==RemoteActionPhase::due);
    CHECK(t.hal.cancels.back()==Node::test_last_allocated_timer_id()); // no due-row zero-delay livelock
    CHECK(t.node.radmin_take_action(transfer));CHECK(transfer.request_id==0);CHECK(transfer.source_hash==source);
    CHECK(transfer.trigger==RemoteActionTrigger::deadline);CHECK(transfer.authority==kRadminRoleOwner);
    CHECK_FALSE(t.node.radmin_take_action(transfer));const DeferredActionRecord zero{};CHECK(memcmp(&transfer,&zero,sizeof zero)==0);
    CHECK(exec.calls==1);
}
TEST_CASE("§radmin-73/Node parked scheduled ownership arms with zero counter and wakes exactly at deadline") {
    TargetNode t;t.provision(1);Radmin73Prepare exec(t);const auto carrier=radmin_command_carrier();
    t.flight(7,0x711,t.ctrl[0].key_hash32,t.auth_execute_request(0,17,carrier),10000);
    mrfw::radmin_service_once(t,exec);const auto now=t.hal.now();
    CHECK(t.node.radmin_send_frame()==RadminSend::parked);
    const auto& s=t.node.admin_session_state();CHECK(s.action.phase==RemoteActionPhase::armed);
    CHECK(s.action.activate_at_ms==now+7006);CHECK(t.last_rpc_body(DATA_TYPE_REMOTE_RESP).empty());
    CHECK(t.hal.arms.back()==std::pair<uint32_t,uint32_t>{Node::test_last_allocated_timer_id(),7006});
    CHECK(s.response_enqueue_failure==0);CHECK(s.response_seal_failure==0);
    t.hal._now=now+7006;t.node.radmin_service_expire();CHECK(s.action.phase==RemoteActionPhase::due);
    DeferredActionRecord action{};CHECK(t.node.radmin_take_action(action));CHECK(action.kind==2);
}
TEST_CASE("§radmin-73/Node full TX paces unowned work and cannot starve an already owned promise") {
    for(bool owned:{false,true}) {
        TargetNode t;t.provision(1);t.learn_peer(7,t.ctrl[0]);Radmin73Prepare exec(t);
        t.flight(7,0x721,t.ctrl[0].key_hash32,t.auth_execute_request(0,19,radmin_command_carrier()),10000);
        mrfw::radmin_service_once(t,exec);if(owned){CHECK(t.node.radmin_send_frame()==RadminSend::queued);t.pump_tx(7);}
        t.node.test_suspend_tx_drain(true);const uint8_t noise=0;
        for(unsigned i=0;i<64&&!t.node.tx_queue_full();++i)
            (void)t.node.test_do_send_typed(7,&noise,1,CryptIntent::off,0,DATA_TYPE_REMOTE_RESP);
        CHECK(t.node.tx_queue_full());if(!t.node.tx_queue_full())return;const auto before=t.node.admin_session_state();
        CHECK(t.node.radmin_send_frame()==RadminSend::none);
        CHECK(memcmp(&before,&t.node.admin_session_state(),sizeof before)==0);
        t.hal._now+=100000;DeferredActionRecord action{};CHECK(t.node.radmin_take_action(action)==owned);
        CHECK(t.node.tx_queue_full());CHECK(exec.calls==1);
        CHECK(t.node.radmin_counters().response_enqueue_failure==0);CHECK(t.node.radmin_counters().response_seal_failure==0);
    }
}

TEST_CASE("§radmin-73/Node saturated deadline is a real wake and diagnostics use one scalar snapshot") {
    TargetNode t;t.provision(1);Radmin73Prepare exec(t);
    t.flight(7,0x731,t.ctrl[0].key_hash32,t.auth_execute_request(0,23,radmin_command_carrier()),10000);
    mrfw::radmin_service_once(t,exec);t.hal._now=UINT64_MAX-5;
    CHECK(t.node.radmin_send_frame()==RadminSend::parked);
    const auto& state=t.node.admin_session_state();CHECK(state.action.activate_at_ms==UINT64_MAX);
    CHECK(t.hal.arms.back()==std::pair<uint32_t,uint32_t>{Node::test_last_allocated_timer_id(),5});
    const auto before=state;t.node.radmin_action_result(3,6);
    const auto status=t.node.radmin_action_status();CHECK(status.remaining_ms==5);
    CHECK(status.phase==RemoteActionPhase::armed);CHECK(status.request_id==23);
    CHECK(status.last_kind==3);CHECK(status.last_outcome==6);
    CHECK(memcmp(&state.action,&before.action,sizeof state.action)==0);
    DeferredActionRecord out{};CHECK_FALSE(t.node.radmin_take_action(out));
    t.hal._now=UINT64_MAX;const auto cancels=t.hal.cancels.size();CHECK(t.node.radmin_take_action(out));
    CHECK(t.hal.cancels.size()==cancels+1);CHECK(t.hal.cancels.back()==Node::test_last_allocated_timer_id());
    CHECK(out.activate_at_ms==UINT64_MAX);CHECK(out.trigger==RemoteActionTrigger::deadline);
    CHECK_FALSE(t.node.radmin_take_action(out));
}

namespace {
// 8ac's loopback remains a control beside 8b's real mobile/home carrier chain.
// Requests enter a real target Node through RTS/DATA; target MAC responses enter the real controller Node.
struct ControllerLoopCarrier : IRadminCarrier {
    std::vector<std::vector<uint8_t>> commands, acks;
    bool tx_queue_full() const override { return false; }
    RemoteClientSend submit_request(const RemoteClientRoute&, RemoteSource, const RemoteCarrier&,
                                    std::span<const uint8_t> b, bool) override {
        commands.emplace_back(b.begin(), b.end()); return RemoteClientSend::queued;
    }
    RemoteClientSend submit_ack(const RemoteClientRoute&, RemoteSource, const RemoteCarrier&,
                                std::span<const uint8_t> b) override {
        acks.emplace_back(b.begin(), b.end()); return RemoteClientSend::queued;
    }
};
struct ControllerLoopSink : IRemoteLocalDelivery {
    std::string text, result;
    uint32_t detail=0;
    bool connected(RemoteLocalTransport) const override { return true; }
    bool output(RemoteLocalTransport, uint64_t, uint16_t& seq, std::span<const uint8_t> b) override {
        text.append(reinterpret_cast<const char*>(b.data()),b.size()); ++seq; return true;
    }
    bool terminal(RemoteLocalTransport, uint64_t, const char* name, uint32_t d, bool) override {
        result=name; detail=d; return true;
    }
    void retained(RemoteLocalTransport, uint64_t) override {}
};
bool controller_loop_entropy(void* ctx,uint8_t* out,size_t n) {
    auto& counter=*static_cast<uint64_t*>(ctx);
    ++counter; for (size_t i=0;i<n;++i) out[i]=static_cast<uint8_t>(counter>>(8*i));
    return true;
}
}
TEST_CASE("8ac two Node loopback owns response bytes and all terminal meanings through real MAC intake") {
    const char* names[]={"completed","scheduled","unknown_command","refused","output_truncated",
                         "internal_error","session_full","session_busy","action_busy"};
    for (uint8_t code=0;code<15;++code) {
        TargetNode target; target.provision(4);
        const auto credential=target.ctrl[3];
        const auto identity=make_identity(88); target.learn_peer(4,identity);
        CHECK(std::memcmp(credential.ed_pub,identity.ed_pub,32)!=0); // key4 differs from the messaging identity
        RsHal hal; Node controller{hal,4,identity.key_hash32}; NodeConfig cfg{};
        cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<12); controller.on_init(cfg);
        controller.set_crypto_identity(identity.x_secret,identity.ed_pub);
        ControllerLoopCarrier carrier; ControllerLoopSink sink; uint64_t entropy=0x800;
        RemoteClientRequest req{}; req.identity=&credential; req.target_pub=target.admin.ed_pub;
        req.source_hash=identity.key_hash32; req.route.target_hash=target.self.key_hash32;
        req.transport=RemoteLocalTransport::ble; req.credential_slot=4;
        const uint8_t text[]={'s','t','a','t','u','s'};req.command=text;
        const auto begin=remote_client_start(controller.remote_client(),req,0,controller_loop_entropy,&entropy,carrier);
        CHECK(begin.error==RemoteClientError::none); CHECK(carrier.commands.size()==1);
        if(carrier.commands.empty())return;
        auto receive_reply=[&] {
            const auto before=target.hal.tx_frames.size(); target.pump_tx(4);
            bool found=false;
            for(size_t j=before;j<target.hal.tx_frames.size();++j) {
                const auto& frame=target.hal.tx_frames[j].bytes;
                const auto d=parse_data(frame); if(!d || d->type!=DATA_TYPE_REMOTE_RESP)continue;
                auto ui=parse_unicast_inner(data_inner(frame,*d),d->flags);
                CHECK(ui.has_value()); if(!ui)continue;
                CHECK(ui->source_hash==target.self.key_hash32); CHECK(d->dst==4);
                std::array<uint8_t,16> rts{};
                hal._now=target.hal._now+500;
                const auto n=mk_rts(5,4,4,static_cast<uint8_t>(d->ctr&15),60,rts,5,d->ctr);
                controller.on_recv(rts.data(),n,RxMeta{8,-80,0,5});hal._now+=100;
                controller.on_recv(frame.data(),frame.size(),RxMeta{8,-80,0,5});controller.on_timer(9);
                found=true;
            }
            CHECK(found);
        };
        target.flight(4,0x810,identity.key_hash32,carrier.commands.back(),1000);
        receive_reply();
        remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
        CHECK(carrier.commands.size()==2); if(carrier.commands.size()!=2)return;
        // Labelled synthetic pressure/tombstone fixtures exercise real target decisions,
        // wire encoding, target MAC TX, and controller MAC RX. No reply is forged here.
        auto& state=const_cast<RemoteSessionState&>(target.node.admin_session_state());
        if(code==9) {
            state.ingress[kRadminIngressControl].state=static_cast<uint8_t>(IngressState::reserved);
            state.ingress[kRadminIngressControl].expires_at_ms=UINT64_MAX;
        }
        if(code>=11) for(unsigned i=0;i<kRadminSeenSlots;++i) {
            auto& r=state.seen[i].record;r.state=static_cast<uint8_t>(SeenState::acknowledged);
            r.controller_slot=3;r.request_id=100+i;r.admin_epoch=state.epoch[3];r.transcript_slot=kRadminNoTranscript;
        }
        target.flight(4,0x811,identity.key_hash32,carrier.commands.back(),hal._now+1000);
        if(code==9 || code>=11) {
            receive_reply();
            remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
            if(code==9) { CHECK(sink.result=="ingress_full");CHECK(carrier.commands.size()==2);continue; }
            CHECK(carrier.commands.size()==3); // session_full led to exactly one SAFE_ROLLOVER
            if(carrier.commands.size()!=3)continue;
            CHECK((carrier.commands.back()[0]>>4)==static_cast<uint8_t>(RemoteCmdOpcode::safe_rollover));
            if(code==12) { state.transcripts[0].state=static_cast<uint8_t>(TranscriptState::ready);state.transcripts[0].controller_slot=3; }
            if(code==13) state.seen[0].record.state=static_cast<uint8_t>(SeenState::executing);
            if(code==14) { target.hal.fixed_rng=true;target.hal.fixed_epoch=0; }
            target.flight(4,0x813,identity.key_hash32,carrier.commands.back(),hal._now+1000);
            RadminOpenExec exec;mrfw::radmin_service_once(target,exec);CHECK(exec.calls==0);receive_reply();
            remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
            if(code>=12) {
                CHECK(sink.result==(code==12?"session_busy":code==13?"executing":"preparation_failed"));
                CHECK(carrier.commands.size()==3);CHECK(carrier.acks.empty());continue;
            }
            CHECK(carrier.commands.size()==4);if(carrier.commands.size()!=4)continue;
            CHECK(controller.remote_client().pending[0].core.request_id!=begin.request_id);
            target.flight(4,0x814,identity.key_hash32,carrier.commands.back(),hal._now+1000);
            RadminIngressView fresh{};CHECK(target.node.radmin_next_admitted(fresh));
            CHECK(fresh.request_id==controller.remote_client().pending[0].core.request_id);
            CHECK(target.node.radmin_reserve_transcript(fresh.seen_index,kRadminChunkBytes));
            target.node.radmin_transcript_complete(fresh.seen_index,RemoteTerminal::completed);
            CHECK(target.node.radmin_send_frame()==RadminSend::queued);receive_reply();
            remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
            CHECK(sink.result=="completed");CHECK(carrier.commands.size()==4);continue;
        }
        RadminIngressView admitted{};CHECK(target.node.radmin_next_admitted(admitted));
        CHECK(admitted.slot==3);CHECK(admitted.request_id==begin.request_id);
        if(code==10) {
            state.seen[admitted.seen_index].record.state=static_cast<uint8_t>(SeenState::acknowledged);
            state.ingress[kRadminIngressControl]={};
            target.flight(4,0x815,identity.key_hash32,carrier.commands.back(),hal._now+1000);receive_reply();
            remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
            CHECK(sink.result=="already_acknowledged");CHECK(carrier.acks.empty());continue;
        }
        size_t cap=0; CHECK(remote_body_cap(admitted.reply_carrier,cap)==RemoteStatus::ok);
        CHECK(target.node.radmin_reserve_transcript(admitted.seen_index,static_cast<uint16_t>(cap-kRemoteOverheadAuthResponse)));
        const uint8_t output[]={'o','w','n','e','d'};
        target.node.radmin_transcript_append(admitted.seen_index,output,sizeof output);
        if(code==1)CHECK(target.node.radmin_prepare_action(3,begin.request_id,2,0,7006)==RemoteTerminal::scheduled);
        target.node.radmin_transcript_complete(admitted.seen_index,static_cast<RemoteTerminal>(code));
        for(unsigned i=0;i<2;++i){CHECK(target.node.radmin_send_frame()==RadminSend::queued);receive_reply();}
        CHECK(controller.remote_client().counters.auth_failure==0);
        CHECK(controller.remote_client().counters.assembly_failure==0);
        remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
        CHECK(sink.text=="owned");CHECK(sink.result==names[code]);if(code==1)CHECK(sink.detail==7006);CHECK(carrier.acks.empty());
        CHECK(remote_client_ack(controller.remote_client(),begin.request_id,static_cast<uint32_t>(hal._now))==RemoteClientError::none);
        remote_client_service(controller.remote_client(),static_cast<uint32_t>(hal._now),controller_loop_entropy,&entropy,carrier,sink);
        CHECK(carrier.acks.size()==1);
        if(!carrier.acks.empty())target.flight(4,0x812,identity.key_hash32,carrier.acks.back(),hal._now+1000);
        CHECK(controller.remote_client().ack_debt[0].in_use==1); // submission is never receipt
    }
}

TEST_CASE("8b authenticated cross-layer admission uses the existing reversed-path ACK sender") {
    TargetNode t;t.provision(1);t.learn_peer(7,t.ctrl[0]);
    const uint8_t path[]={1,0};auto carrier=radmin_command_carrier();
    carrier.cross_layer=true;carrier.path_depth=2;carrier.path_cursor=1;carrier.dst_hash_on_wire=true;
    const auto request=t.auth_execute_request(0,901,carrier);
    t.flight(7,0x890,t.ctrl[0].key_hash32,request,10000,DATA_TYPE_REMOTE_CMD,path,2,1,t.self.key_hash32,DATA_FLAG_E2E_ACK_REQ);
    RadminIngressView admitted{};CHECK(t.node.radmin_next_admitted(admitted));
    CHECK(t.hal.count("xl_ack_no_gateway")==1);CHECK(t.hal.field("xl_ack_no_gateway","acked_ctr")==0x890);
    CHECK(t.hal.field("xl_ack_no_gateway","target_leaf")==1);CHECK(t.hal.count("e2e_ack_tx")==0);
}
