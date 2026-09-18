// Shared real-MAC pair lifted from test_custody_receive_g.cpp for Slice 8b.
#pragma once
#include "doctest.h"
#include "node.h"
#include "ram_inbox_store.h"
#include "test_hal.h"
#include <string>
#include <vector>
namespace {

// Timer ids, mirrored TU-locally exactly as `test_custody_relay_f.cpp:46` does.
constexpr uint32_t kRtsTimeoutTimerId   = 4;
constexpr uint32_t kCtsToDataGapTimerId = 7;
constexpr uint32_t kQueueWakeupTimerId  = 8;
constexpr uint32_t kPostAckTimerId      = 9;
constexpr uint32_t kRetryBackoffTimerId = 10;

const RxMeta kRx{10.0f, -75.0f, 0, static_cast<int8_t>(-1)};

struct GFrame { std::string label; std::vector<uint8_t> bytes; };

class GHal : public mrtest::TestHalBase {
public:
    // ★ §B278 S3: the recorder gained the FIELDS beside the name — additively, so every pre-existing
    //   `count(...)` reader is unchanged. S3's four events are pinned by field NAME, ORDER and integer TYPE,
    //   and a recorder that kept only the event name could not say any of that.
    struct EmitRec { std::string kind; std::vector<std::string> keys; std::vector<int> types;
                     std::vector<int64_t> ivals; };
    std::vector<std::string> emits;
    std::vector<EmitRec>     emit_recs;
    std::vector<GFrame>      tx_frames;
    bool dead_rng = false; // labelled target entropy fault; custody fixtures keep the original stream
    void emit(const char* kind, const EventField* f, size_t n) override {
        emits.push_back(kind ? kind : "");
        EmitRec r; r.kind = kind ? kind : "";
        for (size_t i = 0; i < n; ++i) {
            r.keys.push_back(f[i].key ? f[i].key : "");
            r.types.push_back(static_cast<int>(f[i].type));
            r.ivals.push_back(f[i].type == EventField::T::i64 ? f[i].i : -1);
        }
        emit_recs.push_back(r);
    }
    int  count(const char* k) const { int c = 0; for (const auto& e : emits) if (e == k) ++c; return c; }
    const EmitRec* first_emit(const char* k) const {
        for (const auto& r : emit_recs) if (r.kind == k) return &r;
        return nullptr;
    }
    void clear_emits() { emits.clear(); emit_recs.clear(); }
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        tx_frames.push_back(GFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n) });
        return TxResult::ok;
    }
    void rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) o[i] = dead_rng ? 0 : static_cast<uint8_t>(0x5Au ^ (i * 17u));
    }
    size_t label_count(const char* label) const {
        size_t c = 0; for (const auto& f : tx_frames) if (f.label == label) ++c; return c;
    }
    std::vector<uint8_t> last(const char* label) const {
        for (auto it = tx_frames.rbegin(); it != tx_frames.rend(); ++it) if (it->label == label) return it->bytes;
        return {};
    }
};

NodeConfig g_cfg() {
    NodeConfig cfg; cfg.n_layers = 1;
    cfg.layers[0].layer_id = 1; cfg.layers[0].routing_sf = 8;
    cfg.layers[0].allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
    cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
    // ⓘ A NONZERO leaf id, for `test_custody_relay_f.cpp`'s reason: `on_init` sets `layers[0].layer_id = leaf_id`
    //   on a single-layer node, so leaving the default 0 would make §9.2's `reporter_layer` — and therefore
    //   §13.15's whole falsifier — indistinguishable from an unwritten byte.
    cfg.leaf_id = 2;
    return cfg;
}

void drain(Node& n) { Push d{}; while (n.next_push(d)) {} }

// ---- THE PAIR: node 2 (the REPORTER) -> node 1 (the FAILED ORIGIN, i.e. the only legitimate consumer) -------
// Node 1's inbox is wired by default, because §7.3's whole point is the sequence the store assigns.
struct GPair {
    GHal h1, h2;
    Node n1{h1, /*id=*/1, 0x11111111u};
    Node n2{h2, /*id=*/2, 0x22222222u};
    RamInboxStore dm1{protocol::inbox_dm_store_bytes}, ch1{protocol::inbox_chan_store_bytes};
    uint64_t now = 100000;
    // ★ §B278 S3: `n1_lineage` is APPENDED with a default, so every pre-existing construction is byte-identical.
    //   A nonzero lineage with `config_epoch == 0` is the ONE production shape that makes `leaf_config_synced()`
    //   false, i.e. the un-synced managed joiner whose `enqueue_data` refuses an `app_dm` origination SILENTLY —
    //   the `SendDispatch::Admit::none` arm §B278 S3 must treat as a forward refusal (U1: the existing config
    //   fields, not a new seam).
    explicit GPair(bool wire_inbox = true, uint16_t n1_lineage = 0) {
        const NodeConfig cfg = g_cfg();
        NodeConfig cfg1 = cfg; cfg1.lineage_id = n1_lineage;
        CHECK(n1.on_init(cfg1)); CHECK(n2.on_init(cfg));
        if (wire_inbox) n1.inbox().on_init(&dm1, &ch1);   // ⛔ AFTER Node::on_init (node.h's contract)
        n1.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
        n2.test_learn_route(/*dest=*/1, /*via=*/1, 1, 40, false);
        h1._now = h2._now = now;
        drain(n1); drain(n2);
        h1.clear_emits(); h2.clear_emits();
    }
    void step() { h1._now = h2._now = ++now; }

    // One COMPLETE hop 2 -> 1 over the real MAC. `fire_post_ack = false` leaves the `PostAck` PENDING, which is
    // the window the §CUSTODY-G seam reads a REAL one out of.
    bool hop_2_to_1(bool fire_post_ack = true) {
        const std::vector<uint8_t> rts = h2.last("RTS");
        if (rts.empty()) return false;
        step(); n1.on_recv(rts.data(), rts.size(), kRx);
        const std::vector<uint8_t> cts = h1.last("CTS");
        if (cts.empty()) return false;
        step(); n2.on_recv(cts.data(), cts.size(), kRx);
        step(); n2.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = h2.last("DATA");
        if (data.empty()) return false;
        step(); n1.on_recv(data.data(), data.size(), kRx);
        const std::vector<uint8_t> ack = h1.last("ACK");
        if (!ack.empty()) { step(); n2.on_recv(ack.data(), ack.size(), kRx); }
        if (fire_post_ack) { step(); n1.on_timer(kPostAckTimerId); }
        return true;
    }
    // Originate a typed DATA at node 2 addressed to node 1 and fly it. Returns false if the chain stalled, so a
    // fixture that silently stopped driving is a FAILURE and never a green pass.
    bool send_typed(const uint8_t* body, uint8_t len, uint8_t type = DATA_TYPE_CUSTODY_FAILURE,
                    uint32_t dst_hash = 0, bool fire_post_ack = true) {
        if (n2.test_do_send_typed(/*dst=*/1, body, len, CryptIntent::off, dst_hash, type) == 0) return false;
        return hop_2_to_1(fire_post_ack);
    }
    // The REVERSE direction, needed by exactly one arm: §13.1's falsifier requires the REPORTER's node id to be
    // 1, because that is the byte a CRYPTED parse leaves in front of the record (see §CUSTODY-G/2.1).
    bool hop_1_to_2(bool fire_post_ack = true) {
        const std::vector<uint8_t> rts = h1.last("RTS");
        if (rts.empty()) return false;
        step(); n2.on_recv(rts.data(), rts.size(), kRx);
        const std::vector<uint8_t> cts = h2.last("CTS");
        if (cts.empty()) return false;
        step(); n1.on_recv(cts.data(), cts.size(), kRx);
        step(); n1.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = h1.last("DATA");
        if (data.empty()) return false;
        step(); n2.on_recv(data.data(), data.size(), kRx);
        const std::vector<uint8_t> ack = h2.last("ACK");
        if (!ack.empty()) { step(); n1.on_recv(ack.data(), ack.size(), kRx); }
        if (fire_post_ack) { step(); n2.on_timer(kPostAckTimerId); }
        return true;
    }
    bool send_typed_from_1(const uint8_t* body, uint8_t len, bool fire_post_ack = true) {
        if (n1.test_do_send_typed(/*dst=*/2, body, len, CryptIntent::off, /*dst_hash=*/0,
                                  DATA_TYPE_CUSTODY_FAILURE) == 0) return false;
        return hop_1_to_2(fire_post_ack);
    }
};


} // namespace
