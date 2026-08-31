// MeshRoute — test_custody_receive_g.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-G — THE RECEIVER, PERSISTENCE AND FACTUAL OUTPUT (spec
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`
// §13 the eighteen validations · §7.2 the record mapping · §7.3 the five-step order · §14 PushKind/JSON/USB ·
// §18.5 receipt, storage and presentation).
//
// THE SEVEN CLAIMS THIS FILE MEASURES:
//   (1) THE STATE TRANSITION, AS A PAIR — an addressed `0x81` is CONSUMED by the wired handler, and another
//       unhandled internal type is STILL dropped at Slice B's fail-closed tail guard. One case, both halves:
//       a transition stated as "the guard weakened" would be false, and only the pair can say so.
//   (2) §13's EIGHTEEN VALIDATIONS — one independent falsifier per term, each AT THE LAYER THAT OWNS IT:
//       record-byte breaks for the codec's eleven, frame-level arms for plaintext/unicast parsing, an
//       addressed-elsewhere fixture for `failed_origin == self`, a layer fixture for the receiving-layer
//       match, and the four protocol-domain overruns.
//   (3) §7.3's FIVE STEPS, OBSERVABLE — the Push carries the sequence the STORE assigned (record BEFORE push),
//       storage-disabled yields `seq = 0`, and ★ THE APPEND-FAILURE ARM: the model is GAP-TOLERANT, so a failed
//       append still advances the sequence, still pushes, stores nothing, and never retries.
//   (4) THE FORWARDING ROLES — a relay forwards a transit notice; a home forwards a hosted-mobile-addressed one.
//       Only `failed_origin == self` consumes.
//   (5) §14.2's JSON, LIVE AND PULLED — both produce the semantic event `custody_failure` with the same
//       identity and fields, against a golden transcribed from the spec rather than from the encoder.
//       ⛔ §14.3's USB LINE IS **NOT** COVERED HERE and no case in this file claims it is: `src/fw_main.cpp`
//       is outside the native build (§B115), so its arm is BOARD-COMPILED (`-Wswitch` proves the kind is
//       handled) and its CONTENT is metal-pending — bench Part 53. The JSON is the host-proven surface.
//   (6) THE [[B59]] END-TO-END CASE — a `0x8B` authoritative pubkey answer dies in transit at a relay, the
//       relay reports, and the ORIGINAL SENDER ends up with a stored record and a live push. This is the
//       founding scenario of the whole arc and the first time it closes.
//   (7) §13.18's DOMAIN CONSTANT, pinned against the WIRE FIELD it describes, not against itself.
//
// ⛔ PRODUCTION-SHAPED WHEREVER THE PATH ALLOWS. Every arm that can be a real frame IS one: node 2 originates a
//    typed `0x81` over the real MAC and node 1 answers with the production `do_post_ack` dispatch. The two
//    contexts a static pair's MAC cannot install — a CRYPTED carrier and an inner that fails the unicast parse —
//    are driven through the `MESHROUTE_NATIVE` seam, off a COPY of a REAL `PostAck` with EXACTLY ONE field
//    changed ([[B268]]'s lesson: a fabricated frame proves the assertion, not the code).
//
// ⛔ THE VISIBILITY HALF IS NOT HERE. §7.4/§18.5.5-6-10 belong to Slice C's machinery and are re-run in
//    `test/test_custody_internal_c.cpp` (§CUSTODY-C/2e and §CUSTODY-C/5b) using C's own OLED mirror, budget and
//    unread router — re-proved, ⛔ never re-implemented.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"
#include "inbox.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "console_json.h"      // §14.2: write_push / write_inbox_dm — the two semantic emitters
#include "ram_inbox_store.h"
#include "support/test_hal.h"

#include <cstring>
#include <optional>
#include <string>
#include <vector>

using namespace meshroute;

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
    std::vector<std::string> emits;
    std::vector<GFrame>      tx_frames;
    void emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind ? kind : ""); }
    int  count(const char* k) const { int c = 0; for (const auto& e : emits) if (e == k) ++c; return c; }
    void clear_emits() { emits.clear(); }
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        tx_frames.push_back(GFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n) });
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
    explicit GPair(bool wire_inbox = true) {
        const NodeConfig cfg = g_cfg();
        CHECK(n1.on_init(cfg)); CHECK(n2.on_init(cfg));
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

// ---- §9.2's record, as a VALUE, with every field distinct so no assertion can pass on a zero -----------------
// ⛔ NOT the §9.2 GOLDEN BYTE VECTOR — that lives in `test_custody_relay_f.cpp` and is the wire authority. This
//    is a well-formed record built through the PRODUCTION encoder so the RECEIVER has something real to judge.
CustodyFailureRecord g_base_record(uint8_t failed_origin, uint8_t reporter_layer) {
    CustodyFailureRecord r{};
    r.notice_flags      = custody_notice_flags(CustodyRootStage::cts, /*repair_attempted=*/true,
                                               /*next_was_one_way=*/false, /*has_dst_hash=*/false);
    r.terminal_reason   = CustodyFailureReason::cascade_count;
    r.failed_origin     = failed_origin;
    r.failed_dst        = 9;
    r.failed_ctr        = 0x0BEE;
    r.failed_type       = DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY;   // [[B59]]'s own type
    r.failed_data_flags = 0;
    r.failed_plane      = CustodyFailurePlane::static_same_layer;
    r.reporter_layer    = reporter_layer;
    r.previous_hop      = 1;
    r.failed_next_hop   = 9;
    r.requeue_count     = protocol::cascade_requeue_max;   // AT the domain edge — the boundary must be INSIDE
    r.alternatives_tried = 1;
    r.committed_hops    = 1;
    r.remaining_hops    = 4;
    r.dst_hash32        = 0;
    r.reserved          = 0;
    return r;
}

// Pack it. ⛔ `pack_custody_failure` REFUSES a record that violates §9.2/§9.3, which is exactly why the arms
// below mutate the PACKED BYTES rather than the struct: a transmitter cannot air most of the malformed shapes
// §13 must refuse, and asking the receiver only about records its own packer would accept would test nothing.
uint8_t g_pack(const CustodyFailureRecord& r, uint8_t out[custody_record_v1_len]) {
    const size_t n = pack_custody_failure(r, std::span<uint8_t>(out, custody_record_v1_len));
    CHECK(n == custody_record_v1_len);   // non-vacuous: a refusal must not silently yield an empty body
    return static_cast<uint8_t>(n);
}

// §9.2's offsets, for the BYTE-BREAK arms. ⛔ These are NOT a second reader — nothing here decodes a record;
//    they name WHICH BYTE an arm corrupts, which is the one thing a falsifier has to be able to say.
enum RecOff : uint8_t {
    kOffVersion = 0, kOffRecordLen = 1, kOffFlags = 2, kOffReason = 3, kOffFailedOrigin = 4,
    kOffFailedDst = 5, kOffCtrLo = 6, kOffCtrHi = 7, kOffFailedType = 8, kOffPlane = 10,
    kOffReporterLayer = 11, kOffPrevHop = 12, kOffNextHop = 13, kOffRequeues = 14, kOffAlts = 15,
    kOffCommitted = 16, kOffRemaining = 17, kOffReserved = 22,
};

// ---- ONE ARM = ONE FRESH PAIR. The outcome of delivering `len` bytes of `body` as an 0x81 to node 1. --------
struct ArmOut { int accepted = 0; int rejected = 0; int pushes = 0; int delivered = 0; int unsupported = 0;
                uint32_t seq = 0; int stored = 0; bool flew = false; };

struct StoreSink { int custody = 0; uint32_t seq = 0; uint8_t body_len = 0; std::vector<uint8_t> body; };
bool store_cb(void* ctx, const InboxEntry& e) {
    auto* s = static_cast<StoreSink*>(ctx);
    if (e.type == DATA_TYPE_CUSTODY_FAILURE) {
        ++s->custody; s->seq = e.seq; s->body_len = e.body_len;
        s->body.assign(e.body, e.body + e.body_len);
    }
    return true;
}

ArmOut run_arm(const uint8_t* body, uint8_t len) {
    ArmOut o{};
    GPair p;
    o.flew = p.send_typed(body, len);
    o.accepted    = p.h1.count("custody_failure_rx");
    o.rejected    = p.h1.count("custody_failure_reject");
    o.delivered   = p.h1.count("delivered");
    o.unsupported = p.h1.count("unsupported_internal");
    Push pu{};
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) { ++o.pushes; o.seq = pu.seq; }
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    o.stored = s.custody;
    return o;
}

// A one-line falsifier: mutate exactly ONE byte of a valid record and require the receiver to REJECT it —
// no acceptance, no push, no storage, no delivery, and exactly one bounded reject event.
void expect_rejected_byte(const char* term, uint8_t off, uint8_t value) {
    CAPTURE(term); CAPTURE(off); CAPTURE(value);
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);
    rec[off] = value;
    const ArmOut o = run_arm(rec, n);
    CHECK(o.flew);              // ⛔ the frame really reached node 1 — a stalled chain is not a rejection
    CHECK(o.accepted == 0);
    CHECK(o.rejected == 1);     // exactly one bounded scalar event per rejected flight
    CHECK(o.pushes == 0);
    CHECK(o.stored == 0);       // §17-G/6: invalid reports stay OUT of storage
    CHECK(o.delivered == 0);    // ⛔ and never fall through to ordinary DM delivery
    CHECK(o.unsupported == 0);  // ⛔ NOT `unsupported_internal` — 0x81 is supported now
}

}  // namespace

// =====================================================================================================
// §CUSTODY-G/1 — THE STATE TRANSITION, AS A PAIR
// =====================================================================================================

// ★★★★ THE SLICE'S HEADLINE, AND IT IS DELIBERATELY ONE CASE WITH TWO HALVES. §17-G ends the ratified
//      F-before-G intermediate state in which an addressed `0x81` died at Slice B's fail-closed tail guard.
//      Proving only the first half ("0x81 is consumed now") is compatible with having BROKEN the guard; proving
//      only the second is compatible with not having built the receiver. Measured together, they say exactly
//      what changed: ONE type acquired a handler, and the guard is untouched.
// ⓘ `0x87` is the control: an UNALLOCATED value inside the internal range `0x80..0xBF` (frame_codec.h pins it
//   as unknown-internal), so it has no handler on any build and must still take the guard's drop.
TEST_CASE("§CUSTODY-G/1 the transition, both halves: an addressed 0x81 is CONSUMED, another internal type is STILL guard-dropped") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);

    // ---- half one: the 0x81 is consumed by the wired handler
    {
        GPair p;
        CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE));
        CHECK(p.h1.count("custody_failure_rx") == 1);
        CHECK(p.h1.count("unsupported_internal") == 0);   // ⛔ it no longer reaches the tail guard at all
        CHECK(p.h1.count("delivered") == 0);
        int pushes = 0; Push pu{};
        while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) ++pushes;
        CHECK(pushes == 1);
    }
    // ---- half two: another unhandled internal type still dies at the SAME guard, with its own telemetry
    {
        GPair p;
        const uint8_t junk[] = { 'x', 'y', 'z' };
        CHECK(p.send_typed(junk, sizeof junk, /*type=*/0x87));
        CHECK(p.h1.count("unsupported_internal") == 1);   // ★ the guard is intact for everything else
        CHECK(p.h1.count("custody_failure_rx") == 0);
        CHECK(p.h1.count("custody_failure_reject") == 0); // ⛔ and the custody arm did not eat it either
        CHECK(p.h1.count("delivered") == 0);
        int pushes = 0; Push pu{};
        while (p.n1.next_push(pu)) ++pushes;
        CHECK(pushes == 0);
    }
    // ⓘ 0x87 really is an unknown INTERNAL value, so half two is not accidentally testing an application type.
    CHECK(data_type_is_internal(0x87));
    CHECK_FALSE(data_type_traits(0x87).known);
}

// =====================================================================================================
// §CUSTODY-G/2 — §13's EIGHTEEN VALIDATIONS, ONE FALSIFIER EACH, AT THE OWNING LAYER
// =====================================================================================================

// ★★★★ THE POSITIVE BASELINE. Every falsifier below is this record with EXACTLY ONE thing changed, so a
//      rejection can only be attributed to that change. ⛔ Without this case the whole matrix could pass
//      vacuously on a receiver that rejects everything.
TEST_CASE("§CUSTODY-G/2 the POSITIVE baseline: a valid addressed record is accepted, stored and pushed") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);
    const ArmOut o = run_arm(rec, n);
    CHECK(o.flew);
    CHECK(o.accepted == 1);
    CHECK(o.rejected == 0);
    CHECK(o.pushes == 1);
    CHECK(o.stored == 1);
    CHECK(o.seq != 0u);          // the store is wired in this fixture
    CHECK(o.delivered == 0);     // ⛔ §7.3(5): never ordinary DM delivery
    CHECK(o.unsupported == 0);
}

// ---- §13.1 / §13.2 — THE FRAME-LEVEL TERMS, through the seam (a static pair's MAC cannot install either) ----
// ★★ EACH IS ONE FIELD CHANGED ON A **COPY OF A REAL, PRODUCTION-INSTALLED `PostAck`** — the frame really flew,
//    node 1 really accepted the hop, and only then is one variable moved. See node.h's seam banner.
// ★★★★ §13.1, AND THE ARM IS BUILT THE HARD WAY ON PURPOSE — the obvious version DOES NOT MEASURE THE TERM.
//      Setting `DATA_FLAG_CRYPTED` on an ORDINARY notice looks like a falsifier and is not: under CRYPTED the
//      shared codec deliberately does NOT consume the origin byte (`frame_codec.cpp`, *"a relay must NOT learn
//      who originated a CRYPTED DM"*), so `ui->body` shifts by one and `parse_custody_failure` refuses it at
//      §13.4 anyway. The mutation battery measured exactly that: the first version of this case SURVIVED a
//      mutant that deleted the plaintext term.
// ⇒ THE ARM CONSTRUCTS THE ONE CIRCUMSTANCE IN WHICH §13.1 IS THE ONLY THING STANDING THERE. The reporter is
//   NODE 1, so the origin byte a CRYPTED parse leaves in front of the body is `1` — which is exactly
//   `custody_record_version_v1`. The wire body is the valid record MINUS its version byte. Then:
//     · PLAINTEXT (production): `ui->body` = those 23 bytes -> too short -> refused at the codec;
//     · CRYPTED with §13.1 dropped: `ui->body` = [1][the 23 bytes] = a COMPLETE, VALID v1 record that satisfies
//       every remaining term -> STORED AND REPORTED, out of bytes that were never plaintext.
//   That is the hazard in one sentence: ciphertext can ALIGN into a valid-looking record, and only "the DATA is
//   plaintext" refuses it.
TEST_CASE("§CUSTODY-G/2.1 §13.1 a CRYPTED carrier is REFUSED — and it is the ONLY term stopping ciphertext aligning into a record") {
    // ---- part A: the fixture's positive control, so the arm below is not measured against a broken path.
    {
        GPair p;
        uint8_t rec[custody_record_v1_len];
        const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);
        CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/0, /*fire_post_ack=*/false));
        const PostAck* live = p.n1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        p.h1.clear_emits();
        PostAck base = *live;
        auto ui = parse_unicast_inner(std::span<const uint8_t>(base.inner, base.inner_len), base.flags);
        CHECK(ui.has_value());
        p.n1.test_custody_failure_receive(base, ui ? &*ui : nullptr);
        CHECK(p.h1.count("custody_failure_rx") == 1);
        CHECK(p.h1.count("custody_failure_reject") == 0);
    }
    // ---- part B: the arm, and its construction is spelled out because it has to be exact.
    // A PLAINTEXT unicast inner is `[origin][source_hash 4][body]`; a CRYPTED one is handed to the open step
    // WHOLE — the codec consumes neither the origin nor the source hash, because a relay must not learn who
    // originated a sealed DM. So the CRYPTED view of THIS frame is
    //     [origin=1][key_hash32 LE = 18 03 02 02][the 24 crafted body bytes]     (29 bytes)
    // and the record a v1 parse reads out of it is built from the FIVE PREFIX BYTES plus the body:
    //     version = origin = 1 · record_len = 24 · notice_flags = 0x03 (forwarded|cts) · reason = 2 · and
    //     failed_origin = 2 — node 2's own id, which is what makes it ADDRESSED TO THE RECEIVER.
    // ⇒ node 1's key_hash32 is CHOSEN, not arbitrary: its four little-endian bytes ARE those four fields.
    GHal bh1, bh2;
    Node bn1{bh1, /*id=*/1, 0x02020318u};      // LE: 18 03 02 02 = record_len 24 · flags 0x03 · reason 2 · origin 2
    Node bn2{bh2, /*id=*/2, 0x22222222u};
    const NodeConfig cfg = g_cfg();
    CHECK(bn1.on_init(cfg)); CHECK(bn2.on_init(cfg));
    bn1.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, false);
    bn2.test_learn_route(/*dest=*/1, /*via=*/1, 1, 40, false);
    uint64_t bnow = 100000; bh1._now = bh2._now = bnow;
    drain(bn1); drain(bn2);
    bh1.clear_emits(); bh2.clear_emits();
    CHECK(bn1.node_id() == custody_record_version_v1);       // ⛔ the whole construction rests on this
    CHECK(bn2.active_layer_id() == 2);

    // The 24 crafted body bytes — every one of them a §9.2 field READ FROM ITS SHIFTED POSITION.
    const uint8_t crafted[24] = {
        /* failed_dst      */ 9,
        /* failed_ctr LE   */ 0xEE, 0x0B,
        /* failed_type     */ DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY,
        /* failed_flags    */ 0,
        /* failed_plane    */ static_cast<uint8_t>(CustodyFailurePlane::static_same_layer),
        /* reporter_layer  */ 2,
        /* previous_hop    */ 1,
        /* failed_next_hop */ 9,
        /* requeues        */ 0,
        /* alternatives    */ 0,
        /* committed_hops  */ 0,
        /* remaining_hops  */ 0,
        /* dst_hash32      */ 0, 0, 0, 0,
        /* reserved        */ 0, 0,
        /* the 5 surplus bytes are the accepted TAIL of a record_len-24 record inside a 29-byte view */
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    };
    // Fly it 1 -> 2 as a real 0x81 over the real MAC, and stop before the post-ACK pass.
    CHECK(bn1.test_do_send_typed(/*dst=*/2, crafted, sizeof crafted, CryptIntent::off, /*dst_hash=*/0,
                                 DATA_TYPE_CUSTODY_FAILURE) != 0);
    {
        const std::vector<uint8_t> rts = bh1.last("RTS");
        CHECK_FALSE(rts.empty()); if (rts.empty()) return;
        ++bnow; bh1._now = bh2._now = bnow; bn2.on_recv(rts.data(), rts.size(), kRx);
        const std::vector<uint8_t> cts = bh2.last("CTS");
        CHECK_FALSE(cts.empty()); if (cts.empty()) return;
        ++bnow; bh1._now = bh2._now = bnow; bn1.on_recv(cts.data(), cts.size(), kRx);
        ++bnow; bh1._now = bh2._now = bnow; bn1.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = bh1.last("DATA");
        CHECK_FALSE(data.empty()); if (data.empty()) return;
        ++bnow; bh1._now = bh2._now = bnow; bn2.on_recv(data.data(), data.size(), kRx);
    }
    const PostAck* live = bn2.test_pending_post_ack();
    CHECK(live != nullptr);
    if (!live) return;
    bh2.clear_emits();
    // ① THE CONTROL — as it really arrived (PLAINTEXT) the body is the 24 crafted bytes, whose first byte is a
    //   node id and not a version. The codec refuses it at §13.4, so nothing is stored.
    {
        PostAck base = *live;
        auto ui = parse_unicast_inner(std::span<const uint8_t>(base.inner, base.inner_len), base.flags);
        CHECK(ui.has_value());
        if (ui) {
            CHECK(ui->body.size() == sizeof crafted);
            CHECK_FALSE(parse_custody_failure(ui->body).has_value());
        }
        bn2.test_custody_failure_receive(base, ui ? &*ui : nullptr);
        CHECK(bh2.count("custody_failure_rx") == 0);
        CHECK(bh2.count("custody_failure_reject") == 1);
    }
    bh2.clear_emits();
    // ② THE ARM — ONE variable, the CRYPTED flag. The bytes the receiver would hand the codec now ALIGN into a
    //   complete, valid, correctly-addressed v1 record, and ⛔ §13.1 is the only term that refuses it.
    {
        PostAck crypted = *live;
        crypted.flags |= DATA_FLAG_CRYPTED;                       // ← THE ONE VARIABLE
        auto ui = parse_unicast_inner(std::span<const uint8_t>(crypted.inner, crypted.inner_len), crypted.flags);
        CHECK(ui.has_value());
        // ★★ NON-VACUOUS, AND THIS IS WHAT MAKES THE ARM A FALSIFIER RATHER THAN A HOPE: the shifted view really
        //    does parse, and it really is addressed to this node.
        if (ui) {
            CHECK(ui->body.size() == sizeof crafted + 5u);        // origin + the 4 source-hash bytes
            const std::optional<CustodyFailureRecord> would = parse_custody_failure(ui->body);
            CHECK(would.has_value());
            if (would) {
                CHECK(would->version        == custody_record_version_v1);
                CHECK(would->record_len     == custody_record_v1_len);
                CHECK(would->failed_origin  == bn2.node_id());     // ⇒ §13.11 would ACCEPT it
                CHECK(would->failed_dst     == 9);
                CHECK(would->failed_ctr     == 0x0BEE);
                CHECK(would->reporter_layer == bn2.active_layer_id());   // ⇒ §13.15 would too
                CHECK(would->failed_plane   == CustodyFailurePlane::static_same_layer);
                CHECK(would->terminal_reason == CustodyFailureReason::cascade_count);
            }
        }
        // ...and it is refused anyway. ⛔ Delete `is_plaintext` and this becomes a STORED, REPORTED custody
        //    failure assembled out of bytes that were never plaintext (battery arm G10).
        bn2.test_custody_failure_receive(crypted, ui ? &*ui : nullptr);
        CHECK(bh2.count("custody_failure_rx") == 0);
        CHECK(bh2.count("custody_failure_reject") == 1);
    }
}

TEST_CASE("§CUSTODY-G/2.2 §13.2 an inner that fails the standard unicast parse is REFUSED") {
    GPair p;
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/0, /*fire_post_ack=*/false));
    const PostAck* live = p.n1.test_pending_post_ack();
    CHECK(live != nullptr);
    if (!live) return;
    p.h1.clear_emits();
    PostAck empty = *live;
    empty.inner_len = 0;                                          // ← THE ONE VARIABLE
    // ★ THE `nullopt` IS PRODUCED BY THE PRODUCTION PARSER, not asserted by the test: a plaintext inner with no
    //   room for the origin byte is exactly `parse_unicast_inner`'s `inner.size() < off + 1` refusal.
    auto ui = parse_unicast_inner(std::span<const uint8_t>(empty.inner, empty.inner_len), empty.flags);
    CHECK_FALSE(ui.has_value());
    p.n1.test_custody_failure_receive(empty, ui ? &*ui : nullptr);
    CHECK(p.h1.count("custody_failure_rx") == 0);
    CHECK(p.h1.count("custody_failure_reject") == 1);
}

// ---- §13.3 - §13.9, §13.12, §13.13, §13.16, §13.17 — THE CODEC'S ELEVEN, driven THROUGH THE RECEIVER --------
// ★★ F already tested `parse_custody_failure` directly against each of these. What THESE arms add is different
//    and is the reason they exist: they prove the RECEIVER actually CONSULTS the codec on the real path. A
//    receiver that imported the header and then hand-checked a few fields would pass F's arms and fail these.
TEST_CASE("§CUSTODY-G/2.3 §13.3 a body shorter than the 24-byte floor is REFUSED") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    const ArmOut o = run_arm(rec, static_cast<uint8_t>(n - 1));    // 23 bytes: one short of the floor
    CHECK(o.flew); CHECK(o.accepted == 0); CHECK(o.rejected == 1);
    CHECK(o.pushes == 0); CHECK(o.stored == 0); CHECK(o.delivered == 0); CHECK(o.unsupported == 0);
}
TEST_CASE("§CUSTODY-G/2.4 §13.4 an unknown record version is REFUSED") {
    expect_rejected_byte("13.4 version", kOffVersion, 2);
}
TEST_CASE("§CUSTODY-G/2.5 §13.5 a record_len beyond the available body is REFUSED") {
    expect_rejected_byte("13.5 record_len over", kOffRecordLen, custody_record_v1_len + 6);
}
TEST_CASE("§CUSTODY-G/2.5b §13.5 a record_len below the 24-byte floor is REFUSED") {
    expect_rejected_byte("13.5 record_len under", kOffRecordLen, custody_record_v1_len - 1);
}
TEST_CASE("§CUSTODY-G/2.6 §13.6 a set reserved flag bit (6-7) is REFUSED") {
    uint8_t rec[custody_record_v1_len];
    const CustodyFailureRecord base = g_base_record(1, 2);
    const uint8_t n = g_pack(base, rec);
    rec[kOffFlags] = static_cast<uint8_t>(base.notice_flags | 0x40);   // bit 6 — §9.3 says zero in v1
    const ArmOut o = run_arm(rec, n);
    CHECK(o.flew); CHECK(o.accepted == 0); CHECK(o.rejected == 1); CHECK(o.stored == 0);
}
TEST_CASE("§CUSTODY-G/2.7 §13.7 a record with `forwarded` CLEAR is REFUSED") {
    uint8_t rec[custody_record_v1_len];
    const CustodyFailureRecord base = g_base_record(1, 2);
    const uint8_t n = g_pack(base, rec);
    rec[kOffFlags] = static_cast<uint8_t>(base.notice_flags & ~CUSTODY_FLAG_FORWARDED);
    const ArmOut o = run_arm(rec, n);
    CHECK(o.flew); CHECK(o.accepted == 0); CHECK(o.rejected == 1); CHECK(o.stored == 0);
}
TEST_CASE("§CUSTODY-G/2.8 §13.8 BOTH stage bits set is REFUSED, and NEITHER set is REFUSED") {
    const CustodyFailureRecord base = g_base_record(1, 2);
    {   // both
        uint8_t rec[custody_record_v1_len]; const uint8_t n = g_pack(base, rec);
        rec[kOffFlags] = static_cast<uint8_t>(base.notice_flags | CUSTODY_FLAG_FAILED_AT_ACK);
        const ArmOut o = run_arm(rec, n);
        CHECK(o.flew); CHECK(o.rejected == 1); CHECK(o.accepted == 0); CHECK(o.stored == 0);
    }
    {   // neither
        uint8_t rec[custody_record_v1_len]; const uint8_t n = g_pack(base, rec);
        rec[kOffFlags] = static_cast<uint8_t>(base.notice_flags & ~custody_flags_stage_mask);
        const ArmOut o = run_arm(rec, n);
        CHECK(o.flew); CHECK(o.rejected == 1); CHECK(o.accepted == 0); CHECK(o.stored == 0);
    }
}
TEST_CASE("§CUSTODY-G/2.9 §13.9 an unknown reason, and the never-transmitted `invalid`, are REFUSED") {
    expect_rejected_byte("13.9 unknown reason", kOffReason, 6);
    expect_rejected_byte("13.9 reason invalid", kOffReason,
                         static_cast<uint8_t>(CustodyFailureReason::invalid));
}
TEST_CASE("§CUSTODY-G/2.12 §13.12 an out-of-domain node id in ANY of the four identity fields is REFUSED") {
    // ⛔ ALL FOUR are exercised — a receiver that checked only `failed_origin` would pass a single-field arm.
    expect_rejected_byte("13.12 failed_dst 0",     kOffFailedDst, 0);
    expect_rejected_byte("13.12 failed_dst 255",   kOffFailedDst, 0xFF);
    expect_rejected_byte("13.12 previous_hop 0",   kOffPrevHop, 0);
    expect_rejected_byte("13.12 failed_next_hop 255", kOffNextHop, 0xFF);
    // `failed_origin` is covered by §13.11's own arm below (0 and 255 are also not this node's id).
}
TEST_CASE("§CUSTODY-G/2.13 §13.13 a zero failed_ctr is REFUSED") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    rec[kOffCtrLo] = 0; rec[kOffCtrHi] = 0;
    const ArmOut o = run_arm(rec, n);
    CHECK(o.flew); CHECK(o.rejected == 1); CHECK(o.accepted == 0); CHECK(o.stored == 0);
}
TEST_CASE("§CUSTODY-G/2.16 §13.16 the hash flag and the hash must agree in BOTH directions") {
    {   // flag SET over a zero hash
        uint8_t rec[custody_record_v1_len];
        const CustodyFailureRecord base = g_base_record(1, 2);
        const uint8_t n = g_pack(base, rec);
        rec[kOffFlags] = static_cast<uint8_t>(base.notice_flags | CUSTODY_FLAG_HAS_DST_HASH);
        const ArmOut o = run_arm(rec, n);
        CHECK(o.flew); CHECK(o.rejected == 1); CHECK(o.accepted == 0);
    }
    {   // hash PRESENT with the flag clear — the packer refuses this shape, so the bytes are broken directly
        uint8_t rec[custody_record_v1_len];
        const uint8_t n = g_pack(g_base_record(1, 2), rec);
        rec[18] = 0xAA;                                   // §9.2 offset 18: dst_hash32 low byte
        const ArmOut o = run_arm(rec, n);
        CHECK(o.flew); CHECK(o.rejected == 1); CHECK(o.accepted == 0);
    }
}
TEST_CASE("§CUSTODY-G/2.17 §13.17 a nonzero reserved byte is REFUSED") {
    expect_rejected_byte("13.17 reserved", kOffReserved, 1);
}

// ---- §13.10 / §13.11 / §13.14 / §13.15 / §13.18 — THE RECEIVER-CONTEXT TERMS ------------------------------
TEST_CASE("§CUSTODY-G/2.10 §13.10 a RESERVED plane value parses but is REFUSED as unsupported in v1") {
    // ★ THE TWO QUESTIONS ARE SEPARATE, AND THIS ARM IS WHY THE CODEC LEAVES THE SECOND ALONE: the reserved
    //   planes are WELL-FORMED (the codec accepts them) and UNSUPPORTED (the receiver refuses them). An
    //   UNDEFINED plane is a codec rejection instead, and both must refuse.
    expect_rejected_byte("13.10 team",          kOffPlane, static_cast<uint8_t>(CustodyFailurePlane::team));
    expect_rejected_byte("13.10 hosted_mobile", kOffPlane, static_cast<uint8_t>(CustodyFailurePlane::hosted_mobile));
    expect_rejected_byte("13.10 cross_layer",   kOffPlane, static_cast<uint8_t>(CustodyFailurePlane::cross_layer));
    expect_rejected_byte("13.10 unknown",       kOffPlane, static_cast<uint8_t>(CustodyFailurePlane::unknown));
    expect_rejected_byte("13.10 undefined 7",   kOffPlane, 7);   // not a defined value at all -> the codec refuses
}

TEST_CASE("§CUSTODY-G/2.10b §13.10 a record claiming `static_same_layer` that ARRIVED on the TEAM plane is REFUSED") {
    // ★★ THE RECEIVER-CONTEXT HALF of the plane term, and it is §13's own closing rule applied to the one body
    //    field that HAS an outer counterpart: *"A mismatch between outer context and body invariants is
    //    malformed, not evidence."* §9.1 forces `Plane::GLOBAL` on every v1 notice precisely so a team-local-id
    //    collision cannot route a static-plane diagnostic onto the team plane, so a team-plane arrival
    //    contradicts the record's own byte. ⛔ It is NOT a reporter-identity check — the record carries no
    //    reporter field and none is invented (see the receiver's banner).
    // Driven through the seam because a static pair's MAC cannot install a team-plane arrival at all.
    GPair p;
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/0, /*fire_post_ack=*/false));
    const PostAck* live = p.n1.test_pending_post_ack();
    CHECK(live != nullptr);
    if (!live) return;
    p.h1.clear_emits();
    PostAck team = *live;
    team.team_plane = true;                                       // ← THE ONE VARIABLE
    auto ui = parse_unicast_inner(std::span<const uint8_t>(team.inner, team.inner_len), team.flags);
    CHECK(ui.has_value());
    p.n1.test_custody_failure_receive(team, ui ? &*ui : nullptr);
    CHECK(p.h1.count("custody_failure_rx") == 0);
    CHECK(p.h1.count("custody_failure_reject") == 1);
}

TEST_CASE("§CUSTODY-G/2.11 §13.11 a report addressed to a DIFFERENT failed origin is REFUSED — only the sender consumes") {
    // ★★ THE ADDRESSEE TEST, and the falsifier the brief names: an ADDRESSED-ELSEWHERE fixture. The frame is
    //    genuinely addressed to node 1 at the WIRE level (node 1 ACKed the hop), and the RECORD names someone
    //    else as the failed origin. Consuming it would store a diagnostic about a message this node never sent.
    expect_rejected_byte("13.11 someone else", kOffFailedOrigin, 7);
    expect_rejected_byte("13.11 zero",         kOffFailedOrigin, 0);
    expect_rejected_byte("13.11 broadcast",    kOffFailedOrigin, 0xFF);
}

TEST_CASE("§CUSTODY-G/2.14 §13.14 a report ABOUT an E2E ACK or ABOUT another custody notice is REFUSED") {
    expect_rejected_byte("13.14 about an ack",    kOffFailedType, DATA_TYPE_E2E_ACK);
    expect_rejected_byte("13.14 about a notice",  kOffFailedType, DATA_TYPE_CUSTODY_FAILURE);
    // ...and the CONTROL, so the arm is about the two excluded types and not about `failed_type` at all: an
    // ordinary untyped DM (0) and [[B59]]'s 0x8B are both perfectly reportable.
    {
        uint8_t rec[custody_record_v1_len];
        const uint8_t n = g_pack(g_base_record(1, 2), rec);
        rec[kOffFailedType] = 0;                                   // an ordinary DM
        const ArmOut o = run_arm(rec, n);
        CHECK(o.accepted == 1); CHECK(o.rejected == 0); CHECK(o.stored == 1);
    }
}

TEST_CASE("§CUSTODY-G/2.15 §13.15 a reporter_layer that is not the ACTIVE receiving layer is REFUSED") {
    // ★ THE LAYER FIXTURE: `g_cfg()` sets `leaf_id = 2`, so node 1's active layer is 2 and 0/1/3 are all wrong.
    //   ⓘ Testing 0 as well as a nonzero wrong value is deliberate — a receiver that forgot the check entirely
    //     would also accept an UNWRITTEN byte, and 0 is what an unwritten byte looks like.
    CHECK(g_cfg().leaf_id == 2);
    expect_rejected_byte("13.15 layer 0", kOffReporterLayer, 0);
    expect_rejected_byte("13.15 layer 1", kOffReporterLayer, 1);
    expect_rejected_byte("13.15 layer 3", kOffReporterLayer, 3);
}

TEST_CASE("§CUSTODY-G/2.18 §13.18 each of the four count/hop fields must fit its PROTOCOL domain") {
    // ⛔ FOUR INDEPENDENT SUB-ARMS, because they are four different bounds from four different authorities and
    //    a fused check could not say which one refused the record.
    expect_rejected_byte("13.18 requeues",       kOffRequeues,  protocol::cascade_requeue_max + 1);
    expect_rejected_byte("13.18 alternatives",   kOffAlts,      protocol::max_rt_candidates + 1);
    expect_rejected_byte("13.18 committed_hops", kOffCommitted, custody_committed_hops_max + 1);
    expect_rejected_byte("13.18 remaining_hops", kOffRemaining, protocol::hop_budget_max_initial + 1);
    // ...and the BOUNDARY is INSIDE the domain, so the check is `<=` and not `<`. Without this the four arms
    // above would also pass on a receiver that rejected every nonzero count.
    {
        uint8_t rec[custody_record_v1_len];
        CustodyFailureRecord r = g_base_record(1, 2);
        r.requeue_count      = protocol::cascade_requeue_max;
        r.alternatives_tried = protocol::max_rt_candidates;
        r.committed_hops     = custody_committed_hops_max;
        r.remaining_hops     = protocol::hop_budget_max_initial;
        const uint8_t n = g_pack(r, rec);
        const ArmOut o = run_arm(rec, n);
        CHECK(o.accepted == 1); CHECK(o.rejected == 0); CHECK(o.stored == 1);
    }
}

// ★★★★ §13's CLOSING RULE, MADE MEASURABLE — and it is a NEGATIVE requirement, which is why it needs a case:
//      the record carries NO reporter-ID field, so there is nothing to cross-check the outer origin against and
//      ⛔ no outer/body reporter-equality check may be invented. A receiver that grew one would refuse every
//      report whose reporting relay is not also named in the body — i.e. every real report, since no field
//      names it. This case pins that the SAME record is accepted regardless of who relayed it.
TEST_CASE("§CUSTODY-G/2.19 the reporter is UNAUTHENTICATED and unchecked — the same record is accepted from any relay") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);
    // ⓘ `previous_hop` (12) and `failed_next_hop` (13) name 1 and 9; the RELAY here is node 2, which appears in
    //   NO field of the record. Acceptance therefore proves no equality was demanded of it.
    const ArmOut o = run_arm(rec, n);
    CHECK(o.accepted == 1);
    CHECK(o.stored == 1);
    // ...and the stored `origin` is the outer relay, recorded as the CLAIM it is (§7.2).
    GPair p;
    CHECK(p.send_typed(rec, n));
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 1);
    CHECK(p.h1.count("custody_failure_rx") == 1);
}

// ★★★ §13's TAIL RULE (§9.2): a record_len ABOVE 24 that fits the body is VALID, the v1 decoder interprets the
//     first 24 bytes, and the surplus is RETAINED DURABLY. ⛔ Dropping the tail would silently truncate a newer
//     reporter's record on the one node that still had it.
TEST_CASE("§CUSTODY-G/2.20 a VALID unknown tail is accepted, ignored by the v1 decoder, and RETAINED in storage") {
    uint8_t body[custody_record_v1_len + 4];
    const uint8_t n = g_pack(g_base_record(1, 2), body);
    CHECK(n == custody_record_v1_len);
    body[kOffRecordLen] = custody_record_v1_len + 4;              // a v2 reporter appended 4 bytes
    body[custody_record_v1_len + 0] = 0xDE; body[custody_record_v1_len + 1] = 0xAD;
    body[custody_record_v1_len + 2] = 0xBE; body[custody_record_v1_len + 3] = 0xEF;

    GPair p;
    CHECK(p.send_typed(body, custody_record_v1_len + 4));
    CHECK(p.h1.count("custody_failure_rx") == 1);
    CHECK(p.h1.count("custody_failure_reject") == 0);
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 1);
    CHECK(s.body_len == custody_record_v1_len + 4);               // ★ THE PIN: `record_len` bytes, not 24
    if (s.body_len == custody_record_v1_len + 4) {
        CHECK(s.body[custody_record_v1_len + 0] == 0xDE);
        CHECK(s.body[custody_record_v1_len + 1] == 0xAD);
        CHECK(s.body[custody_record_v1_len + 2] == 0xBE);
        CHECK(s.body[custody_record_v1_len + 3] == 0xEF);
    }
    // ...and the LIVE push carries the same `record_len` bytes (§14.1: `body_len = record_len`).
    Push pu{}; int found = 0;
    while (p.n1.next_push(pu))
        if (pu.kind == PushKind::custody_failure) { ++found; CHECK(pu.body_len == custody_record_v1_len + 4); }
    CHECK(found == 1);
    // ⓘ AND THE v1 DECODER IGNORES IT: the same bytes parse to a record whose semantic fields are unchanged.
    const std::optional<CustodyFailureRecord> rec =
        parse_custody_failure(std::span<const uint8_t>(s.body.data(), s.body.size()));
    CHECK(rec.has_value());
    if (rec) {
        CHECK(rec->record_len == custody_record_v1_len + 4);
        CHECK(rec->failed_ctr == 0x0BEE);
        CHECK(custody_record_tail(std::span<const uint8_t>(s.body.data(), s.body.size()), *rec).size() == 4u);
    }
}

// =====================================================================================================
// §CUSTODY-G/3 — §7.3's FIVE STEPS, AND THE APPEND-FAILURE ARM
// =====================================================================================================

// ★★★★ §18.5.1 VERBATIM: *"A valid addressed report is stored before its live Push; both carry the same
//      assigned sequence."* The ORDER is not directly observable, so what is measured is the CONSEQUENCE that
//      only the correct order can produce: the Push carries a sequence that the store had already assigned.
//      Pushing first would have nothing to carry.
TEST_CASE("§CUSTODY-G/3 record BEFORE push: the live push carries the sequence the STORE assigned") {
    GPair p;
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    CHECK(p.send_typed(rec, n));
    uint32_t push_seq = 0; int pushes = 0; Push pu{};
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) { ++pushes; push_seq = pu.seq; }
    CHECK(pushes == 1);
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 1);
    CHECK(push_seq != 0u);
    CHECK(push_seq == s.seq);          // ★ THE PIN
    CHECK(s.seq == p.n1.inbox().dm_newest_seq());
}

// §7.3, verbatim: *"When storage is disabled, the live push carries `seq = 0`."* ⓘ THIS IS ALSO THE
// SIMULATOR'S SITUATION — the corpus wires no inbox stores ([[B134]]: `Inbox::on_init` has exactly one
// production caller, `src/fw_main.cpp`), so every custody push in a corpus stream carries `seq = 0`.
TEST_CASE("§CUSTODY-G/3b storage DISABLED: one live push still fires, carrying seq 0") {
    GPair p(/*wire_inbox=*/false);
    CHECK_FALSE(p.n1.inbox().enabled());
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    CHECK(p.send_typed(rec, n));
    CHECK(p.h1.count("custody_failure_rx") == 1);
    int pushes = 0; Push pu{};
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) { ++pushes; CHECK(pu.seq == 0u); }
    CHECK(pushes == 1);                // ★ the diagnostic is NOT suppressed by the absence of a store
}

// ★★★★ THE APPEND-FAILURE ARM (§7.3: *"Ordinary append failure and drop-oldest behavior remain unchanged;
//      there is no retry or protected slot"*), and it exists because the approved model is GAP-TOLERANT:
//      `Inbox::record()` assigns AND ADVANCES the sequence even when the store's append fails (inbox.cpp).
// ⛔⛔ THE CONCLUSION THIS CASE FORBIDS: **a nonzero `seq` is NOT proof of persistence.** `seq == 0` means
//     exactly one thing — storage is disabled. Reading a nonzero value as "it reached the medium" is the
//     "a success that isn't" shape this arc has corrected twice, and the four assertions below are what make
//     the distinction measurable rather than a comment.
TEST_CASE("§CUSTODY-G/3c APPEND FAILURE: the append is attempted, one push still fires with a NONZERO seq, nothing is stored") {
    GPair p;
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(1, 2), rec);
    p.dm1.fail_append = true;                          // model a dead/full flash
    CHECK(p.send_typed(rec, n));

    // ① the append was ATTEMPTED — and it was attempted BEFORE the push, which is what the store knows.
    CHECK(p.dm1.failed_append_calls == 1);
    // ② ONE live push, carrying the ASSIGNED (nonzero) sequence.
    uint32_t failed_seq = 0; int pushes = 0; Push pu{};
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) { ++pushes; failed_seq = pu.seq; }
    CHECK(pushes == 1);
    CHECK(failed_seq != 0u);                           // ⛔ NOT a persistence proof — the gap-tolerant assignment
    // ③ ...and the pull contains NO corresponding record.
    {
        StoreSink s{};
        p.n1.inbox().pull(0, 0, store_cb, &s);
        CHECK(s.custody == 0);
    }
    // ④ the NEXT successful record takes a HIGHER sequence — the failed one consumed its own, and there was
    //    no retry and no protected slot.
    p.dm1.fail_append = false;
    p.dm1.failed_append_calls = 0;
    {
        GPair q;                                        // a second pair is not usable here — same node, same store
        (void)q;
    }
    const uint32_t next_seq = p.n1.inbox().record_dm(/*origin=*/7, 0, 42, 0,
                                                     reinterpret_cast<const uint8_t*>("x"), 1, p.now);
    CHECK(next_seq > failed_seq);
    CHECK(p.dm1.failed_append_calls == 0);              // ⛔ no retry of the lost record was attempted
    StoreSink s2{};
    p.n1.inbox().pull(0, 0, store_cb, &s2);
    CHECK(s2.custody == 0);                             // ⛔ and it never reappeared
}

// =====================================================================================================
// §CUSTODY-G/4 — THE FORWARDING ROLES: ONLY `failed_origin == self` CONSUMES
// =====================================================================================================

// ★★★★ A RELAY IS NOT THE ADDRESSEE. A notice routes to `failed_origin`; the intermediate hops forward it as
//      ORDINARY TRANSIT DATA and take no semantic view of it at all. Placing the consumer inside the
//      destination branch — after `if (!pa.is_forward)` — is what makes that structural rather than a rule
//      somebody has to remember.
TEST_CASE("§CUSTODY-G/4 a RELAY forwards a transit 0x81 — it does not consume, validate or store it") {
    GHal h1, h2, h3;
    Node n1{h1, 1, 0x11111111u}, n2{h2, 2, 0x22222222u}, n3{h3, 3, 0x33333333u};
    RamInboxStore dm2{protocol::inbox_dm_store_bytes}, ch2{protocol::inbox_chan_store_bytes};
    const NodeConfig cfg = g_cfg();
    CHECK(n1.on_init(cfg)); CHECK(n2.on_init(cfg)); CHECK(n3.on_init(cfg));
    n2.inbox().on_init(&dm2, &ch2);                    // ★ the RELAY has a store, so "nothing stored" is a measurement
    n1.test_learn_route(2, 2, 1, 40, false); n1.test_learn_route(3, 2, 2, 40, false);
    n2.test_learn_route(1, 1, 1, 40, false); n2.test_learn_route(3, 3, 1, 40, false);
    uint64_t now = 100000; h1._now = h2._now = h3._now = now;
    drain(n1); drain(n2); drain(n3);
    h2.clear_emits();

    // ⚠ THE RECORD NAMES **NODE 2** AS THE FAILED ORIGIN, deliberately: if the relay validated by anything other
    //   than "am I the wire destination", this is the record that would tempt it to consume. It must not.
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/2, /*reporter_layer=*/2), rec);
    CHECK(n1.test_do_send_typed(/*dst=*/3, rec, n, CryptIntent::off, 0, DATA_TYPE_CUSTODY_FAILURE) != 0);
    // one full hop 1 -> 2
    const std::vector<uint8_t> rts = h1.last("RTS");
    CHECK_FALSE(rts.empty()); if (rts.empty()) return;
    ++now; h1._now = h2._now = h3._now = now; n2.on_recv(rts.data(), rts.size(), kRx);
    const std::vector<uint8_t> cts = h2.last("CTS");
    CHECK_FALSE(cts.empty()); if (cts.empty()) return;
    ++now; h1._now = h2._now = h3._now = now; n1.on_recv(cts.data(), cts.size(), kRx);
    ++now; h1._now = h2._now = h3._now = now; n1.on_timer(kCtsToDataGapTimerId);
    const std::vector<uint8_t> data = h1.last("DATA");
    CHECK_FALSE(data.empty()); if (data.empty()) return;
    ++now; h1._now = h2._now = h3._now = now; n2.on_recv(data.data(), data.size(), kRx);
    ++now; h1._now = h2._now = h3._now = now; n2.on_timer(kPostAckTimerId);

    CHECK(h2.count("custody_failure_rx") == 0);        // ⛔ not consumed...
    CHECK(h2.count("custody_failure_reject") == 0);    // ⛔ ...and not rejected either — the relay took NO view
    CHECK(h2.count("unsupported_internal") == 0);      // ⛔ nor did it reach the tail guard
    CHECK(h2.count("delivered") == 0);
    StoreSink s{};
    n2.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 0);                             // ⛔ nothing stored at the relay
    int pushes = 0; Push pu{};
    while (n2.next_push(pu)) ++pushes;
    CHECK(pushes == 0);
    // ★ AND IT WAS ACTUALLY FORWARDED — the positive half, without which "not consumed" could just mean
    //   "silently dropped". `become_free()` promotes the forward straight to the LIVE flight (so the tx QUEUE is
    //   empty by now, which is why the carrier is read through F's `test_live_pending_tx` seam rather than the
    //   queue accessors), and the relay has already aired its RTS for it.
    CHECK(h2.label_count("RTS") >= 1);
    const PendingTx* fwd = n2.test_live_pending_tx();
    CHECK(fwd != nullptr);
    if (fwd) {
        CHECK(fwd->type == DATA_TYPE_CUSTODY_FAILURE);   // forwarded VERBATIM as ordinary transit DATA
        CHECK(fwd->dst  == 3);                           // ...toward the failed origin the record names
        CHECK(fwd->has_previous_hop);                    // ...as a TRANSIT carrier, not a re-origination
    }
}

// ★★★★ THE THIRD FORWARDING ROLE, and the one a guard placed "at the top of the destination branch" would eat:
//      a HOME is the outer wire destination of a DST_HASH-addressed frame but only a PROXY for its hosted
//      mobile. The MOBILE — not the home — is the node entitled to decide whether it is the failed origin.
TEST_CASE("§CUSTODY-G/4b a HOME forwards a hosted-mobile-addressed 0x81 at the last mile — it does not consume it") {
    GPair p;
    const uint32_t mobile_hash = 0xC0FFEE01u;
    uint8_t ed[32]; for (int i = 0; i < 32; ++i) ed[i] = uint8_t(i);
    p.n1.test_add_host_mobile(mobile_hash, /*local_id=*/40, ed);   // node 1 now HOSTS a mobile
    p.h1.clear_emits();

    // ⚠ The record names NODE 1 as the failed origin — the shape that WOULD be consumed if the last-mile fork
    //   did not run first. The DST_HASH says the frame is for the mobile, and that must win.
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);
    CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/mobile_hash));

    CHECK(p.h1.count("mobile_lastmile_fwd") == 1);     // ★ forwarded to the mobile's local id
    CHECK(p.h1.count("custody_failure_rx") == 0);      // ⛔ the home did NOT consume it
    CHECK(p.h1.count("custody_failure_reject") == 0);  // ⛔ and did not reject it either
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 0);
    int pushes = 0; Push pu{};
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) ++pushes;
    CHECK(pushes == 0);
}

// =====================================================================================================
// §CUSTODY-G/5 — §14.2's JSON, LIVE AND PULLED
// =====================================================================================================

namespace {
// The §14.2 EXAMPLE, as a record. ⛔ Every value is transcribed from the design's own JSON block, so the
// golden below is the SPEC and the encoder is judged against it — never the other way round.
CustodyFailureRecord spec_example_record() {
    CustodyFailureRecord r{};
    r.notice_flags      = custody_notice_flags(CustodyRootStage::cts, /*repair_attempted=*/true,
                                               /*next_was_one_way=*/true, /*has_dst_hash=*/false);
    r.terminal_reason   = CustodyFailureReason::one_way_throttled;
    r.failed_origin     = 42;
    r.failed_dst        = 48;
    r.failed_ctr        = 3598;
    r.failed_type       = 139;      // 0x8B
    r.failed_plane      = CustodyFailurePlane::static_same_layer;
    r.reporter_layer    = 1;
    r.previous_hop      = 42;
    r.failed_next_hop   = 48;
    r.requeue_count     = 0;
    r.alternatives_tried = 1;
    r.committed_hops    = 0;
    r.remaining_hops    = 0;
    return r;
}
// §14.2's field list, in §14.2's order, as one string. Shared by the live and the pulled golden because
// §18.5.3 requires *"the same semantic report identity and fields"* on both.
const char* kSpecFields =
    ",\"reporter\":186,\"reporter_layer\":1,\"failed_origin\":42,\"dst\":48,\"ctr\":3598,\"failed_type\":139"
    ",\"stage\":\"cts\",\"reason\":\"one_way_throttled\",\"previous_hop\":42,\"next_hop\":48"
    ",\"requeues\":0,\"alternatives\":1,\"committed_hops\":0,\"remaining_hops\":0"
    ",\"repair_attempted\":true,\"one_way\":true";
}  // namespace

TEST_CASE("§CUSTODY-G/5 the LIVE push renders §14.2's example EXACTLY") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(spec_example_record(), rec);
    Push p{};
    p.kind = PushKind::custody_failure;
    p.origin = 186;                       // the reporting relay, §14.2's `"reporter": 186`
    p.dst = 48; p.ctr = 3598; p.layer_id = 1; p.seq = 17;
    p.body_len = n; std::memcpy(p.body, rec, n);
    char buf[1700];
    const size_t m = meshroute::console::write_push(buf, sizeof buf, p, nullptr);
    CHECK(m > 0);
    const std::string got(buf, m);
    // ⓘ THE TRAILING NEWLINE IS PART OF THE CONTRACT, not test noise: `JsonBuf::finish()` terminates every
    //   line with `\n` (this surface is NDJSON), and a golden that stripped it would stop pinning that.
    const std::string want = std::string("{\"ev\":\"custody_failure\",\"seq\":17") + kSpecFields + "}\n";
    CHECK(got == want);
}

TEST_CASE("§CUSTODY-G/5b storage disabled: the live event OMITS `seq`, per the existing push convention") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(spec_example_record(), rec);
    Push p{};
    p.kind = PushKind::custody_failure; p.origin = 186; p.dst = 48; p.ctr = 3598; p.seq = 0;
    p.body_len = n; std::memcpy(p.body, rec, n);
    char buf[1700];
    const size_t m = meshroute::console::write_push(buf, sizeof buf, p, nullptr);
    const std::string got(buf, m);
    const std::string want = std::string("{\"ev\":\"custody_failure\"") + kSpecFields + "}\n";
    CHECK(got == want);
}

TEST_CASE("§CUSTODY-G/5c the PULLED record renders the SAME semantic event, plus its receive timestamp") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(spec_example_record(), rec);
    const uint32_t seq = ib.record_custody_failure(/*reporter=*/186, /*failed_ctr=*/3598,
                                                   /*layer_id=*/1, rec, n, /*now_ms=*/77000);
    CHECK(seq == 1u);
    // ⚠ MIRRORS `src/firmware_inbox.cpp`'s pull callback, and is labelled a MIRROR: `src/*.cpp` is outside the
    //   native build (§B115). ⛔ The DECISION it mirrors is deliberately NOT in that file — the custody fork
    //   lives inside `write_inbox_dm`, which IS natively compiled, so what this mirror reproduces is only the
    //   argument passing.
    struct Sink { std::string out; };
    Sink sink;
    ib.pull(0, 0, [](void* ctx, const InboxEntry& e) {
        char b[1700];
        const size_t m = meshroute::console::write_inbox_dm(
            b, sizeof b, e.seq, e.origin, e.layer_id, uint16_t(e.msg_id), e.sender_hash, e.rx_time_ms,
            reinterpret_cast<const char*>(e.body), e.body_len, e.enc != 0, e.type, e.origin_layer);
        static_cast<Sink*>(ctx)->out.assign(b, m);
        return true;
    }, &sink);
    const std::string want = std::string("{\"ev\":\"custody_failure\",\"seq\":1,\"rx_ms\":77000") + kSpecFields + "}\n";
    CHECK(sink.out == want);
    // ★★ THE TWO SURFACES AGREE ON EVERY SEMANTIC FIELD (§18.5.3), asserted rather than eyeballed: the shared
    //    substring is byte-identical, so an app can ship ONE decoder.
    CHECK(sink.out.find(kSpecFields) != std::string::npos);
}

TEST_CASE("§CUSTODY-G/5d `dst_hash` is emitted through the hash helper ONLY when its flag is valid") {
    CustodyFailureRecord r = spec_example_record();
    r.notice_flags = custody_notice_flags(CustodyRootStage::hop_ack, /*repair_attempted=*/false,
                                          /*next_was_one_way=*/false, /*has_dst_hash=*/true);
    r.dst_hash32 = 0xA1B2C3D4u;
    // ★★ A DELIBERATELY **NON-PALINDROMIC** COUNTER, and it is not decoration: §14.2's own example counter is
    //    3598 = `0x0E0E`, whose two wire bytes are IDENTICAL — so a consumer that re-read `failed_ctr` BIG-endian
    //    would render it correctly and the endianness defect would hide inside the spec's own golden. The
    //    mutation battery measured exactly that (the "second offset-reader" arm SURVIVED against 0x0E0E).
    //    `0x1234` renders as 4660 little-endian and 13330 byte-swapped.
    r.failed_ctr = 0x1234;
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(r, rec);
    Push p{};
    p.kind = PushKind::custody_failure; p.origin = 186; p.dst = 48; p.ctr = 3598; p.seq = 0;
    p.body_len = n; std::memcpy(p.body, rec, n);
    char buf[1700];
    const size_t m = meshroute::console::write_push(buf, sizeof buf, p, nullptr);
    const std::string got(buf, m);
    CHECK(got.find("\"dst_hash\":\"a1b2c3d4\"") != std::string::npos);   // key_hex32's lower-case 8-digit form
    CHECK(got.find("\"stage\":\"ack\"") != std::string::npos);           // the OTHER stage word, so both are pinned
    CHECK(got.find("\"repair_attempted\":false") != std::string::npos);
    CHECK(got.find("\"one_way\":false") != std::string::npos);
    // ★ THE LITTLE-ENDIAN PIN (see the record's own note): 0x1234 == 4660, and 13330 would be the byte-swapped
    //   reading a second, hand-rolled offset reader produces.
    CHECK(got.find("\"ctr\":4660") != std::string::npos);
    CHECK(got.find("\"ctr\":13330") == std::string::npos);
    // ...and the negative: the spec example has the flag CLEAR, so it must carry no `dst_hash` at all.
    uint8_t rec2[custody_record_v1_len];
    const uint8_t n2 = g_pack(spec_example_record(), rec2);
    Push q{}; q.kind = PushKind::custody_failure; q.origin = 186; q.body_len = n2;
    std::memcpy(q.body, rec2, n2);
    const size_t m2 = meshroute::console::write_push(buf, sizeof buf, q, nullptr);
    CHECK(std::string(buf, m2).find("dst_hash") == std::string::npos);
}

// ★★★★ §7.2/§14.2's HARD RULE, MADE STRUCTURAL: *"The ordinary `inbox_dm` text encoder must not stringify the
//      binary record."* A custody record NEVER produces an `inbox_dm` event and its bytes never reach `j.str()`.
//      ⓘ Asserted on a record whose bytes CONTAIN a JSON metacharacter and a control byte, so a leak would be
//        visible in the output rather than merely present.
TEST_CASE("§CUSTODY-G/5e a stored custody record NEVER renders as `inbox_dm` and its bytes never reach the text encoder") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(spec_example_record(), rec);
    ib.record_custody_failure(186, 3598, 1, rec, n, 77000);
    ib.record_dm(/*origin=*/7, 0, 5, 0, reinterpret_cast<const uint8_t*>("plain"), 5, 78000);   // the CONTROL
    struct Sink { std::vector<std::string> lines; };
    Sink sink;
    ib.pull(0, 0, [](void* ctx, const InboxEntry& e) {
        char b[1700];
        const size_t m = meshroute::console::write_inbox_dm(
            b, sizeof b, e.seq, e.origin, e.layer_id, uint16_t(e.msg_id), e.sender_hash, e.rx_time_ms,
            reinterpret_cast<const char*>(e.body), e.body_len, e.enc != 0, e.type, e.origin_layer);
        static_cast<Sink*>(ctx)->lines.emplace_back(b, m);
        return true;
    }, &sink);
    CHECK(sink.lines.size() == 2u);
    if (sink.lines.size() != 2u) return;
    CHECK(sink.lines[0].find("{\"ev\":\"custody_failure\"") == 0u);
    CHECK(sink.lines[0].find("\"ev\":\"inbox_dm\"") == std::string::npos);
    CHECK(sink.lines[0].find("\"body\":") == std::string::npos);   // ★ no text field at all for the record
    CHECK(sink.lines[1].find("\"ev\":\"inbox_dm\"") != std::string::npos);   // ...and an ordinary DM is unchanged
    CHECK(sink.lines[1].find("\"body\":\"plain\"") != std::string::npos);
}

// ★★★ §14.1, verbatim: *"Do not place the custody reason in `Push::reason`."* The two enums are deliberately
//     independent, and this pins that the JSON never grows a `reason` field from `SendFailReason`'s vocabulary.
TEST_CASE("§CUSTODY-G/5f the custody reason is the WIRE enum's word, never a SendFailReason spelling") {
    uint8_t rec[custody_record_v1_len];
    CustodyFailureRecord r = spec_example_record();
    r.terminal_reason = CustodyFailureReason::queue_full;   // ⚠ a word `SendFailReason` ALSO has
    const uint8_t n = g_pack(r, rec);
    Push p{};
    p.kind = PushKind::custody_failure; p.origin = 186; p.body_len = n;
    p.reason = SendFailReason::none;                        // ⛔ and it STAYS none
    std::memcpy(p.body, rec, n);
    char buf[1700];
    const size_t m = meshroute::console::write_push(buf, sizeof buf, p, nullptr);
    const std::string got(buf, m);
    CHECK(got.find("\"reason\":\"queue_full\"") != std::string::npos);
    // ⓘ THE COLLISION IS THE POINT: `queue_full` exists in BOTH vocabularies with DIFFERENT meanings (§9.4's is
    //   "the TX queue had no requeue slot at the reporting relay"; `SendFailReason`'s is "the no-route defer
    //   queue refused a NEW local send"). They must never be produced by the same table — which is why the
    //   receiver leaves `Push::reason` at `none` and the JSON reads the wire enum out of the body.
    CHECK(meshroute::console::custodyreason_name(CustodyFailureReason::queue_full) == std::string("queue_full"));
    CHECK(meshroute::console::sendfailreason_name(SendFailReason::queue_full) == std::string("queue_full"));
    CHECK(p.reason == SendFailReason::none);
}

// =====================================================================================================
// §CUSTODY-G/6 — THE [[B59]] END-TO-END CASE: THE ARC'S FOUNDING SCENARIO, CLOSED
// =====================================================================================================

// ★★★★ THE WHOLE DESIGN EXISTS FOR THIS ONE SEQUENCE, and until now no test could state it end to end:
//        node 1 originates an AUTHORITATIVE pubkey answer (`0x8B`) to node 3 through relay node 2;
//        node 2 ACKs custody, then its ONLY path to 3 dies and its cascade terminates;
//        node 2 originates ONE `0x81` back to node 1 (§CUSTODY-F);
//        node 1 VALIDATES it, STORES it and PUSHES it (§CUSTODY-G).
//      ⛔ EVERY STEP IS PRODUCTION: a real typed origination, a real MAC hop, the real cascade terminal, the
//      real generator, and the real receiver. Nothing is injected and no record is fabricated.
// ⓘ WHY `0x8B` SPECIFICALLY: [[B59]] was a pubkey answer that vanished in transit and produced no evidence at
//   all — the metal-confirmed failure this arc was opened to make visible (design §1.1). It is also the case
//   §18.4.10 requires to remain POSITIVELY eligible for a notice.
TEST_CASE("§CUSTODY-G/6 [[B59]] END TO END: a 0x8B dies in transit and the ORIGINAL SENDER ends up with the evidence") {
    GHal h1, h2, h3;
    Node n1{h1, 1, 0x11111111u}, n2{h2, 2, 0x22222222u}, n3{h3, 3, 0x33333333u};
    RamInboxStore dm1{protocol::inbox_dm_store_bytes}, ch1{protocol::inbox_chan_store_bytes};
    const NodeConfig cfg = g_cfg();
    CHECK(n1.on_init(cfg)); CHECK(n2.on_init(cfg)); CHECK(n3.on_init(cfg));
    n1.inbox().on_init(&dm1, &ch1);                    // ★ node 1 is the SENDER — the evidence lands here
    n1.test_learn_route(2, 2, 1, 40, false); n1.test_learn_route(3, 2, 2, 40, false);
    n2.test_learn_route(1, 1, 1, 40, false); n2.test_learn_route(3, 3, 1, 40, false);   // 2's ONLY path to 3
    uint64_t now = 100000; h1._now = h2._now = h3._now = now;
    drain(n1); drain(n2); drain(n3);
    h1.clear_emits(); h2.clear_emits();
    auto tick = [&]() { ++now; h1._now = h2._now = h3._now = now; };

    // ---- (1) node 1 originates the pubkey answer to node 3; node 2 takes custody of it.
    const uint8_t answer[] = { 2, 3, 0xAA, 0xBB };     // [target_layer][node_id][key bytes…] — shape only
    const uint16_t sent_ctr = n1.test_do_send_typed(/*dst=*/3, answer, sizeof answer, CryptIntent::off,
                                                    /*dst_hash=*/0, DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY);
    CHECK(sent_ctr != 0);
    {
        const std::vector<uint8_t> rts = h1.last("RTS");
        CHECK_FALSE(rts.empty()); if (rts.empty()) return;
        tick(); n2.on_recv(rts.data(), rts.size(), kRx);
        const std::vector<uint8_t> cts = h2.last("CTS");
        CHECK_FALSE(cts.empty()); if (cts.empty()) return;
        tick(); n1.on_recv(cts.data(), cts.size(), kRx);
        tick(); n1.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = h1.last("DATA");
        CHECK_FALSE(data.empty()); if (data.empty()) return;
        tick(); n2.on_recv(data.data(), data.size(), kRx);
        const std::vector<uint8_t> ack = h2.last("ACK");
        CHECK_FALSE(ack.empty());                       // ★ node 2 ACKED CUSTODY — the precondition of §8
        if (!ack.empty()) { tick(); n1.on_recv(ack.data(), ack.size(), kRx); }
        tick(); n2.on_timer(kPostAckTimerId);
    }
    drain(n1); drain(n2);
    h1.clear_emits(); h2.clear_emits();

    // ---- (2) node 3 never answers: node 2's cascade exhausts and terminates.
    for (int round = 0; round < 12 && h2.count("path_cascade_exhausted") == 0; ++round) {
        for (int i = 0; i < 4; ++i) { n2.on_timer(kRtsTimeoutTimerId); n2.on_timer(kRetryBackoffTimerId); }
        h2._now += 21000; h1._now = h2._now; h3._now = h2._now; now = h2._now;
        n2.on_timer(kQueueWakeupTimerId);
    }
    CHECK(h2.count("path_cascade_exhausted") > 0);
    CHECK(h2.count("custody_notice_tx") == 1);          // ★ EXACTLY ONE notice, addressed to the failed origin

    // ---- (3) the notice flies 2 -> 1 over the real MAC.
    h1.clear_emits();
    {
        const std::vector<uint8_t> rts = h2.last("RTS");
        CHECK_FALSE(rts.empty()); if (rts.empty()) return;
        tick(); n1.on_recv(rts.data(), rts.size(), kRx);
        const std::vector<uint8_t> cts = h1.last("CTS");
        CHECK_FALSE(cts.empty()); if (cts.empty()) return;
        tick(); n2.on_recv(cts.data(), cts.size(), kRx);
        tick(); n2.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = h2.last("DATA");
        CHECK_FALSE(data.empty()); if (data.empty()) return;
        { const std::optional<data_out> d = parse_data(std::span<const uint8_t>(data));
          CHECK(d.has_value());
          if (d) CHECK(d->type == DATA_TYPE_CUSTODY_FAILURE); }   // non-vacuous: it really is an 0x81
        tick(); n1.on_recv(data.data(), data.size(), kRx);
        const std::vector<uint8_t> ack = h1.last("ACK");
        if (!ack.empty()) { tick(); n2.on_recv(ack.data(), ack.size(), kRx); }
        tick(); n1.on_timer(kPostAckTimerId);
    }

    // ---- (4) THE CLOSING PROOF: the ORIGINAL SENDER holds the evidence, live and durable.
    CHECK(h1.count("custody_failure_rx") == 1);
    CHECK(h1.count("custody_failure_reject") == 0);
    CHECK(h1.count("unsupported_internal") == 0);
    CHECK(h1.count("delivered") == 0);                  // ⛔ never an ordinary message

    Push pu{}; int n_cust = 0; Push cust{};
    while (n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) { cust = pu; ++n_cust; }
    CHECK(n_cust == 1);
    CHECK(cust.origin == 2);                            // the reporting relay
    CHECK(cust.dst    == 3);                            // the pubkey answer's destination
    CHECK(cust.ctr    == sent_ctr);                     // ★ §15.2's correlation PAIR, both halves, on OUR send
    CHECK(cust.reason == SendFailReason::none);

    StoreSink s{};
    n1.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 1);
    CHECK(s.seq == cust.seq);                           // record BEFORE push, on the real path
    const std::optional<CustodyFailureRecord> rec =
        parse_custody_failure(std::span<const uint8_t>(s.body.data(), s.body.size()));
    CHECK(rec.has_value());
    if (rec) {
        CHECK(rec->failed_origin == 1);                                          // us
        CHECK(rec->failed_dst    == 3);
        CHECK(rec->failed_ctr    == sent_ctr);
        CHECK(rec->failed_type   == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY);    // ★ [[B59]]'s own frame type
        CHECK(rec->previous_hop  == 1);                                          // the relay took custody FROM us
        CHECK(rec->failed_next_hop == 3);
        CHECK(rec->reporter_layer == 2);
        CHECK(custody_reason_is_transmittable(static_cast<uint8_t>(rec->terminal_reason)));
    }
    // ★★ AND IT IS RENDERABLE AS FACT, which is what [[B59]] actually lacked: the COMPANION gets a complete
    //    semantic line naming the relay, the failed frame and why custody ended.
    // ⛔⛔ SCOPE, STATED EXACTLY: what follows exercises `write_push` — **the JSON surface**. It does NOT
    //    execute `src/fw_main.cpp`'s USB formatting, which is outside the native build (§B115) and is only
    //    BOARD-COMPILED here; its `-Wswitch`-guarded arm proves the kind is HANDLED, not what it prints.
    //    ⇒ the JSON is the host-proven surface; the USB LINE's content is METAL-PENDING (bench Part 53), and
    //    the `NACK` assertion below is therefore about THIS JSON and nothing else. §14.3's rule binds both
    //    surfaces; only one of them is proven here.
    char buf[1700];
    const size_t m = meshroute::console::write_push(buf, sizeof buf, cust, nullptr);
    const std::string js(buf, m);
    CHECK(js.find("\"ev\":\"custody_failure\"") != std::string::npos);
    CHECK(js.find("\"reporter\":2") != std::string::npos);
    CHECK(js.find("\"failed_origin\":1") != std::string::npos);
    CHECK(js.find("\"failed_type\":139") != std::string::npos);
    CHECK(js.find("NACK") == std::string::npos);        // ⛔ §14.3, IN THIS JSON — see the scope note above
}

// =====================================================================================================
// §CUSTODY-G/7 — §13.18's DOMAIN CONSTANT, PINNED AGAINST THE WIRE FIELD
// =====================================================================================================

// ★★★ A DOMAIN CONSTANT THAT ONLY AGREES WITH ITSELF IS NOT A DOMAIN. `custody_committed_hops_max` describes
//     DATA byte 4 bits 2..0 — a 3-BIT field — and the only honest way to pin it is against the codec that
//     masks that byte. ⓘ This is also the reason the constant was introduced rather than a literal `7`: it
//     names a bound that already existed in two places (`pack_data`'s saturation and `hb_new_committed`), and
//     ⛔ this slice deliberately did NOT rewrite those two sites (C1 — a literal→constant sweep is a refactor).
TEST_CASE("§CUSTODY-G/7 custody_committed_hops_max IS the DATA header's 3-bit committed_hops field") {
    // ⓘ A RUNTIME `CHECK`, ⛔ NOT a `static_assert`, and the reason is measured rather than stylistic: a
    //   `static_assert` makes a widened-constant mutant fail to COMPILE, which the mutation battery scores
    //   `UNUSABLE` — i.e. the property would be defended by something that can never be shown to fail. The
    //   compile-time form was tried first and produced exactly that verdict.
    CHECK(custody_committed_hops_max == 7);
    // The wire proof: parse_data masks byte 4 with 0x07, so no received frame can EVER exceed the constant.
    // Driven over every value a byte could hold, through the real codec.
    data_in in{};
    in.addr_len = 0; in.flags = 0; in.next = 2; in.dst = 3; in.ctr = 7;
    in.hops_remaining = protocol::hop_budget_max_initial; in.prev_fwd_rt_hops = 0;
    const uint8_t payload[] = { 1, 2, 3 };
    in.inner = std::span<const uint8_t>(payload, sizeof payload);
    for (unsigned v = 0; v <= 255; ++v) {
        in.committed_hops = uint8_t(v);
        uint8_t frame[64];
        const size_t n = pack_data(in, std::span<uint8_t>(frame, sizeof frame));
        if (n == 0) continue;
        const std::optional<data_out> d = parse_data(std::span<const uint8_t>(frame, n));
        CHECK(d.has_value());
        if (d) CHECK(d->committed_hops <= custody_committed_hops_max);
    }
    // ⛔ AND THE BOUND IS NOT MERELY SAFE, IT IS TIGHT: value 7 must actually be REACHABLE on the wire, or the
    //    constant would be an arbitrary over-approximation that happens to hold.
    in.committed_hops = custody_committed_hops_max;
    uint8_t tight[64];
    const size_t tn = pack_data(in, std::span<uint8_t>(tight, sizeof tight));
    CHECK(tn > 0);
    if (tn) {
        const std::optional<data_out> d = parse_data(std::span<const uint8_t>(tight, tn));
        CHECK(d.has_value());
        if (d) CHECK(d->committed_hops == custody_committed_hops_max);
    }
    // ...and the three sibling bounds are the EXISTING authorities, not re-typed numbers.
    CHECK(protocol::hop_budget_max_initial == 31);
    CHECK(protocol::cascade_requeue_max == 3);
    CHECK(protocol::max_rt_candidates == 3);
}

// ★★★ §9.3's STAGE INVERSE, PINNED DIRECTLY — and it needs its own case for a reason worth stating: a PARSED
//     record can never carry a malformed stage (`parse_custody_failure` enforces §13.8 first), so
//     `custody_stage_of_flags`' FAIL-CLOSED arm is unreachable from every other test in this file. A refusal
//     nothing can drive is a refusal no mutation can redden — the same gap Slice F's own seam was added to
//     close, and the mutation battery measured it here before this case existed.
TEST_CASE("§CUSTODY-G/7b custody_stage_of_flags: both directions, and FAIL-CLOSED on an impossible stage") {
    const uint8_t base = CUSTODY_FLAG_FORWARDED;
    // the two real answers, each derived from §9.3's bit — and the round trip through the FORWARD derivation,
    // so the inverse is pinned against the function it inverts rather than against a copied bit number.
    CHECK(custody_stage_of_flags(uint8_t(base | CUSTODY_FLAG_FAILED_AT_CTS)) == CustodyRootStage::cts);
    CHECK(custody_stage_of_flags(uint8_t(base | CUSTODY_FLAG_FAILED_AT_ACK)) == CustodyRootStage::hop_ack);
    for (CustodyRootStage s : { CustodyRootStage::cts, CustodyRootStage::hop_ack }) {
        const uint8_t f = custody_notice_flags(s, /*repair=*/false, /*one_way=*/false, /*has_hash=*/false);
        CAPTURE(int(f));
        CHECK(custody_stage_of_flags(f) == s);                  // forward ∘ inverse == identity
        CHECK(custody_flags_exactly_one_stage(f));
    }
    // ⛔ FAIL-CLOSED, BOTH IMPOSSIBLE SHAPES: neither stage bit, and both at once. §9.3 admits exactly one, so
    //    either answer must be the NEVER-TRANSMITTED sentinel — ⛔ never a plausible-looking `cts`.
    CHECK(custody_stage_of_flags(base) == CustodyRootStage::invalid);
    CHECK(custody_stage_of_flags(uint8_t(base | custody_flags_stage_mask)) == CustodyRootStage::invalid);
    CHECK(custody_stage_of_flags(0) == CustodyRootStage::invalid);
    // ...and the sentinel RENDERS as the sentinel, so an unreadable stage looks unreadable on every surface.
    CHECK(meshroute::console::custodystage_name(CustodyRootStage::invalid) == std::string("invalid"));
    CHECK(meshroute::console::custodystage_name(CustodyRootStage::cts) == std::string("cts"));
    CHECK(meshroute::console::custodystage_name(CustodyRootStage::hop_ack) == std::string("ack"));
}
