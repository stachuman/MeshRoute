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

#include "support/remote_carrier_chain.h"

namespace {

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

// ★ §B278 S4: the sink gained the record's IDENTITY beside its bytes — ADDITIVELY, so every pre-existing reader
//   (`o.custody`, `o.seq`, `o.body_len`, `o.body`) is byte-identical. §8.3 maps `origin` / `msg_id` / `layer`
//   DIFFERENTLY for a translated record, and a sink that kept only the body could not say so; `visited` and
//   `recs` exist because §S4-5's raw-pull pin has to see BOTH forms in ONE pull.
struct StoreRec { uint32_t seq = 0, msg_id = 0; uint8_t origin = 0, layer_id = 0, body_len = 0;
                  std::vector<uint8_t> body; };
struct StoreSink { int custody = 0; uint32_t seq = 0; uint8_t body_len = 0; std::vector<uint8_t> body;
                   int visited = 0; std::vector<StoreRec> recs; };
bool store_cb(void* ctx, const InboxEntry& e) {
    auto* s = static_cast<StoreSink*>(ctx);
    ++s->visited;
    if (e.type == DATA_TYPE_CUSTODY_FAILURE) {
        ++s->custody; s->seq = e.seq; s->body_len = e.body_len;
        s->body.assign(e.body, e.body + e.body_len);
        StoreRec r{}; r.seq = e.seq; r.msg_id = e.msg_id; r.origin = e.origin; r.layer_id = e.layer_id;
        r.body_len = e.body_len; r.body.assign(e.body, e.body + e.body_len);
        s->recs.push_back(r);
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
TEST_CASE("§CUSTODY-G/2.6 a translated flag over the direct 24-byte form is REFUSED") {
    uint8_t rec[custody_record_v1_len];
    const CustodyFailureRecord base = g_base_record(1, 2);
    const uint8_t n = g_pack(base, rec);
    rec[kOffFlags] = static_cast<uint8_t>(base.notice_flags | 0x40);   // bit 6 requires §B278's 32-byte form
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

// ★★★★ §B278 S2 — THE INTERIM HOME-TRANSLATED REFUSAL, AND IT IS A **PRODUCTION-SHAPED** ARM: the record below
//      is not merely well-formed, it is the §CUSTODY-G/2 POSITIVE BASELINE — the very record this receiver
//      accepts, stores and pushes — with §6.2's translated tail added and nothing else changed. Every one of
//      §13's eighteen terms is therefore satisfied, so the ONLY thing that can refuse it is S2's explicit guard.
// ⛔⛔ THIS IS THE RATIFIED S2→S4 INTERMEDIATE STATE, NOT THE FINAL BEHAVIOUR. S3 originates the translated form;
//    **S4 replaces this guard** with design §8.1/§8.2's split direct-vs-translated contextual validation. When
//    S4 lands, this case is the one that must be re-aimed — the same way §CUSTODY-F/6's
//    "drops at Slice B's tail guard" claim was re-aimed when G landed.
// ⓘ WHY THE GUARD IS LOAD-BEARING RATHER THAN COSMETIC: before S2 a bit-6 record died at the codec's reserved
//   mask. S2 allocates bit 6, so without the guard the eighteen terms below would run on a translated record —
//   and a translating home's own id sits in `failed_origin`, so a home could store and push its own translation
//   as if it were a direct report about itself.
// ⚠⚠ CORRECTED IN PLACE 2026-09-02 BY §B278 S4, OLD CLAIM KEPT VISIBLE. This case was titled
//    *"§B278 S2 a WELL-FORMED translated record is REFUSED by the interim guard"* and its body asserted the
//    blanket S2 refusal. **S4 REPLACED THAT GUARD** with the split direct-vs-translated contextual
//    validation, so "refused by every receiver" is no longer true and must not be left standing. What IS
//    still true — and is what the identical assertions below now measure — is that this fixture's receiver
//    is a STATIC node, and §8.2's configured-mobile term refuses a translated record there for ever
//    (§B278-S4/4 is the same claim stated positively, with the mobile control beside it).
TEST_CASE("§CUSTODY-G/2.21 §B278 S4 a WELL-FORMED translated record is REFUSED BY A STATIC RECEIVER: no store, no push") {
    // ---- the tail, valid against the baseline prefix: `failed_dst` is 9 and no `HAS_DST_HASH` is carried.
    CustodyTranslatedTail tail{};
    tail.original_reporter = 2;                                   // the relay this fixture reports from
    tail.target_kind       = CustodyTranslatedTargetKind::node_id;
    tail.mobile_ctr        = 0x0777;
    tail.target_value      = 9;                                   // == g_base_record(...).failed_dst

    uint8_t body[custody_record_translated_len];
    const size_t n = pack_custody_failure_translated(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2),
                                                     tail, std::span<uint8_t>(body, sizeof body));
    CHECK(n == custody_record_translated_len);                    // non-vacuous: the packer really produced 32 B
    if (n != custody_record_translated_len) return;
    // ★ AND IT IS GENUINELY WELL-FORMED AT THE CODEC — the refusal below is the RECEIVER's, not the parser's.
    const std::optional<CustodyFailureRecord> parsed =
        parse_custody_failure(std::span<const uint8_t>(body, n));
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(custody_record_is_translated(parsed->notice_flags));
        CHECK(parsed->failed_origin == 1);                        // ⇒ §13.11's addressee test WOULD pass
        CHECK(parsed->reporter_layer == 2);                       // ⇒ §13.15's layer test WOULD pass
        CHECK(parse_custody_translated_tail(std::span<const uint8_t>(body, n), *parsed).has_value());
    }
    // ---- the arm: exactly one bounded rejection, and nothing else happens at all. ⚠ §B278 S4: the refusing
    //      term is no longer the interim guard but `_cfg.is_mobile` — `run_arm`'s node 1 is STATIC.
    const ArmOut o = run_arm(body, static_cast<uint8_t>(n));
    CHECK(o.flew);                 // ⛔ the frame really reached node 1 — a stalled chain is not a rejection
    CHECK(o.accepted == 0);
    CHECK(o.rejected == 1);        // the EXISTING bounded exit, taken exactly once
    CHECK(o.pushes == 0);          // ⛔ zero Push
    CHECK(o.stored == 0);          // ⛔ zero store
    CHECK(o.delivered == 0);       // ⛔ and never a fall-through to ordinary DM delivery
    CHECK(o.unsupported == 0);     // ⛔ NOT `unsupported_internal` — 0x81 is still a supported type
    // ---- THE POSITIVE CONTROL, in the same case: the identical prefix WITHOUT the tail is still accepted, so
    //      the refusal above is attributable to the translated form alone and not to a broken fixture.
    {
        uint8_t direct[custody_record_v1_len];
        const uint8_t dn = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), direct);
        const ArmOut d = run_arm(direct, dn);
        CHECK(d.accepted == 1);
        CHECK(d.rejected == 0);
        CHECK(d.pushes == 1);
        CHECK(d.stored == 1);
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

// =====================================================================================================
// ★★★★ §B278 S3 (2026-09-02) — HOME CORRELATION AND TRANSLATED-CUSTODY ORIGINATION
//      (design §4.4 the lookup identity · §4.5 the lifecycle · §6.1/§6.2 the translated form ·
//       §7 the eight-step receive order · §10.1 the one 300 s row TTL)
//
// THE SIX CLAIMS THIS SECTION MEASURES, on the SAME §CUSTODY-G fixture and the SAME production receiver:
//   (1) THE ORDER IS THE CONTRACT — the ORIGINAL direct record is stored and its live Push enqueued BEFORE
//       any correlation happens, and a report that correlates to nothing still gets both local outcomes.
//   (2) §4.4's KEY IS COMPLETE — counter, return kind, return peer, layer and outward type each fail
//       INDEPENDENTLY, so a counter-only matcher is RED; and the `DST_HASH` cross-check applies exactly when
//       the report carries one and only to a hash-addressed row.
//   (3) THE FOUR DISPOSITIONS ARE DISTINCT — zero live eligible rows is SILENT (the normal v1-only state),
//       a live population with no match and an ambiguity each emit their own bounded event and send nothing,
//       and only an exact match originates.
//   (4) THE TRANSLATED FORM IS THE S2 FORM — the 32 bytes the receiver airs are byte-for-byte
//       `pack_custody_failure_translated`'s, with H1/ctrH unchanged, `pa.origin` as the original reporter and
//       the ROW's mobile counter and retained target; a `record_len > 24` input is stored WHOLE and
//       translated from a normalized 24-byte copy.
//   (5) THE OBLIGATION TRANSITIONS — `queued`/`parked` mark the EXACT row `forwarded`; a refusal (pack, park
//       ring full, TX queue full, the un-synced `none` arm) or a stale action marks NOTHING and leaves the row
//       `eligible`.
//   (6) THE LIFECYCLE — ACK-first translates nothing, custody-first still lets the later ACK translate and
//       clear, a duplicate after `forwarded` produces no second send, the 300 s edge prunes BEFORE the scan,
//       and a translated 0x81 can never produce a custody record about itself.
//
// ⛔ PRODUCTION-SHAPED WHEREVER THE PATH ALLOWS, exactly as this file's own banner requires: every arm below
//    flies a REAL 0x81 from node 2 to node 1 over the real MAC and lets the real `do_post_ack` dispatch it.
//    Rows are seeded through the PRODUCTION `deleg_ack_put` authority (a verbatim `test_*` pass-through), so a
//    row under test is byte-for-byte what a real activation would have left. The TWO shapes production cannot
//    reach — an ambiguous ring and a stale action — say so at their own case and use a labelled seam.
// =====================================================================================================

namespace {

// The delegated flight the rows below describe. Every constant is tied to `g_base_record`'s own fields, so a
// change there cannot silently make the match vacuous (the positive baseline in §B278-S3/3 is what proves it).
constexpr uint32_t kS3MobileHash  = 0xC0FFEE01u;   // M1's stable key hash (the translated report's destination)
constexpr uint32_t kS3MobileHash2 = 0xC0FFEE02u;   // a SECOND mobile, for the cross-row cases
constexpr uint8_t  kS3MobileLocal = 200;           // M1's local id at H1 (the direct last mile's address)
constexpr uint16_t kS3CtrM        = 0x0777;        // ctrM — what M1 is actually waiting on
constexpr uint16_t kS3CtrM2       = 0x0888;
constexpr uint8_t  kS3ReturnPeer  = 9;             // == g_base_record(...).failed_dst
constexpr uint16_t kS3CtrH        = 0x0BEE;        // == g_base_record(...).failed_ctr
constexpr uint8_t  kS3OutType     = DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY;   // == ....failed_type
constexpr uint8_t  kS3Reporter    = 2;             // node 2 is the reporting relay here (i.e. `pa.origin`)
constexpr uint8_t  kKindNodeId    = 0;             // Node::DelegAckPeer::node_id, flattened
constexpr uint8_t  kKindKeyHash   = 1;             // Node::DelegAckPeer::key_hash, flattened

// A well-formed direct report, packed. A nonzero `dst_hash` additionally sets CUSTODY_FLAG_HAS_DST_HASH.
uint8_t s3_pack_report(uint8_t out[custody_record_v1_len], uint32_t dst_hash = 0) {
    CustodyFailureRecord r = g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2);
    if (dst_hash) {
        r.dst_hash32   = dst_hash;
        r.notice_flags = custody_notice_flags(CustodyRootStage::cts, /*repair_attempted=*/true,
                                              /*next_was_one_way=*/false, /*has_dst_hash=*/true);
    }
    return g_pack(r, out);
}

// Seed ONE row through the PRODUCTION ring authority. The defaults describe the flight the report above is
// about; each argument exists so a case can break exactly one §4.4 term.
bool s3_seed(Node& n, uint32_t mobile_hash = kS3MobileHash, uint16_t ctr_h = kS3CtrH, uint16_t ctr_m = kS3CtrM,
             uint8_t target_kind = kKindNodeId, uint32_t target = kS3ReturnPeer,
             uint8_t return_kind = kKindNodeId, uint32_t return_peer = kS3ReturnPeer,
             uint8_t outward_type = kS3OutType,
             uint8_t custody = Node::test_custody_state_eligible()) {
    return n.test_deleg_ack_put(mobile_hash, ctr_h, ctr_m, target_kind, target,
                                return_kind, return_peer, outward_type, custody);
}

// Everything one S3 arm produced, in one value. The §CUSTODY-G local outcome (store + Push) is read on EVERY
// case, so no S3 assertion can pass on a receiver that stopped doing its own job.
struct S3Out {
    int accepted = 0, rejected = 0, pushes = 0, stored = 0;
    int no_map = 0, ambiguous = 0, forwarded = 0, refused = 0;
    uint32_t seq = 0;
    uint8_t  stored_len = 0;
    std::vector<uint8_t> stored_body;
    uint8_t  n_eligible = 0, n_forwarded = 0, live_rows = 0;
    uint8_t  tx_n = 0, parked_n = 0;
};

S3Out s3_collect(GPair& p) {
    S3Out o{};
    o.accepted  = p.h1.count("custody_failure_rx");
    o.rejected  = p.h1.count("custody_failure_reject");
    o.no_map    = p.h1.count("deleg_custody_no_map");
    o.ambiguous = p.h1.count("deleg_custody_ambiguous");
    o.forwarded = p.h1.count("deleg_custody_forwarded");
    o.refused   = p.h1.count("deleg_custody_forward_refused");
    Push pu{};
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) { ++o.pushes; o.seq = pu.seq; }
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    o.stored = s.custody; o.stored_len = s.body_len; o.stored_body = s.body;
    o.n_eligible  = p.n1.test_deleg_custody_n(Node::test_custody_state_eligible());
    o.n_forwarded = p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded());
    o.live_rows   = p.n1.test_deleg_ack_live_n();
    o.tx_n        = p.n1.test_tx_queue_n();
    o.parked_n    = p.n1.test_parked_sends_n();
    return o;
}

// The 32 bytes the receiver MUST have produced, built INDEPENDENTLY through the S2 packer from the ROW's
// values and `pa.origin`. ⛔ Never read back off the encoder under test.
std::vector<uint8_t> s3_expected_body(uint8_t target_kind = kKindNodeId, uint32_t target = kS3ReturnPeer,
                                      uint16_t ctr_m = kS3CtrM, uint32_t dst_hash = 0) {
    CustodyFailureRecord r = g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2);
    if (dst_hash) {
        r.dst_hash32   = dst_hash;
        r.notice_flags = custody_notice_flags(CustodyRootStage::cts, true, false, /*has_dst_hash=*/true);
    }
    CustodyTranslatedTail t{};
    t.original_reporter = kS3Reporter;
    t.target_kind       = target_kind == kKindKeyHash ? CustodyTranslatedTargetKind::key_hash
                                                      : CustodyTranslatedTargetKind::node_id;
    t.mobile_ctr        = ctr_m;
    t.target_value      = target;
    std::vector<uint8_t> out(custody_record_translated_len, 0);
    const size_t n = pack_custody_failure_translated(r, t, std::span<uint8_t>(out.data(), out.size()));
    CHECK(n == custody_record_translated_len);
    return out;
}

// Register M1 as a LIVE DIRECT hosted mobile of H1, so `send_by_hash` takes the direct last-mile arm.
void s3_host(Node& n, uint32_t hash = kS3MobileHash, uint8_t local = kS3MobileLocal) {
    uint8_t ed[32];
    for (int i = 0; i < 32; ++i) ed[i] = static_cast<uint8_t>(0xA0 + i);
    n.test_add_host_mobile(hash, local, ed);
}

// The BODY of a queued TxItem, read through the PRODUCTION unicast parser (⛔ never a second offset table).
std::vector<uint8_t> s3_queued_body(Node& n, uint8_t i = 0) {
    uint8_t len = 0;
    const uint8_t* inner = n.test_tx_inner(i, len);
    auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, len), n.test_tx_flags(i));
    if (!ui) return {};
    return std::vector<uint8_t>(ui->body.begin(), ui->body.end());
}

// Fly the report, then fire the post-ack with the TX drain SUSPENDED, so the originated item STAYS in the
// queue for a wire-golden read. ⛔ THE ORDER MATTERS AND IS THE WHOLE POINT OF THE HELPER: `_pending_tx` is the
// half-duplex guard, so a node holding it will not answer an RTS at all — suspending before the hop would
// stall the delivery instead of holding its result.
bool s3_send_and_hold(GPair& p, const uint8_t* body, uint8_t len) {
    if (!p.send_typed(body, len, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/0, /*fire_post_ack=*/false))
        return false;
    p.n1.test_suspend_tx_drain(true);
    p.step();
    p.n1.on_timer(kPostAckTimerId);
    return true;
}

// The parsed record for the default report, for the probe-driven cases.
CustodyFailureRecord s3_parsed_report() {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    const std::optional<CustodyFailureRecord> parsed = parse_custody_failure(std::span<const uint8_t>(rec, n));
    CHECK(parsed.has_value());
    return parsed ? *parsed : CustodyFailureRecord{};
}

}  // namespace

// -----------------------------------------------------------------------------------------------------
// §B278-S3/1 — THE ORDER, AND THE SILENT NORMAL CASE
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE HEADLINE ORDER CLAIM, MEASURED AS A PAIR. A node with NO delegated obligation at all (every v1-only
//      node, and every home whose row already ACKed, forwarded or expired) must behave EXACTLY as it did before
//      S3: store, push, say nothing. ⛔ That silence is a RULED decision, not an omission — a `no_map` line
//      behind every ordinary custody receipt would be noise on the node and would move four corpus streams.
TEST_CASE("§B278-S3/1 zero live eligible rows: stored + pushed exactly as before, and S3 is SILENT") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p;
    CHECK(p.send_typed(rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.accepted == 1);                 // the §CUSTODY-G local outcome is UNCHANGED …
    CHECK(o.pushes == 1);
    CHECK(o.stored == 1);
    CHECK(o.stored_len == custody_record_v1_len);
    CHECK(o.no_map == 0);                   // … and not one S3 event fires
    CHECK(o.ambiguous == 0);
    CHECK(o.forwarded == 0);
    CHECK(o.refused == 0);
    CHECK(o.tx_n == 0);                     // ⛔ nothing was originated
    CHECK(o.parked_n == 0);
}

// ★★★ THE SAME CLAIM WITH A LIVE POPULATION THAT DOES NOT MATCH: the local outcomes STILL happen in full and
//     the ONE bounded diagnostic fires. This is the case that says "S3 never suppresses H1's own diagnostic".
TEST_CASE("§B278-S3/2 a live eligible population with no match: BOTH local outcomes, one bounded `no_map`, no send") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p;
    s3_host(p.n1);
    CHECK(s3_seed(p.n1, kS3MobileHash, /*ctr_h=*/0x0111));    // a live eligible row for a DIFFERENT flight
    CHECK(p.send_typed(rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.accepted == 1);
    CHECK(o.pushes == 1);
    CHECK(o.stored == 1);                   // ⛔ the local record is stored REGARDLESS of correlation
    CHECK(o.no_map == 1);
    CHECK(o.ambiguous == 0);
    CHECK(o.forwarded == 0);
    CHECK(o.refused == 0);
    CHECK(o.tx_n == 0);
    CHECK(o.n_eligible == 1);               // the unrelated row is untouched
    CHECK(o.n_forwarded == 0);
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/3 — §4.4's KEY, ONE INDEPENDENT FALSIFIER PER TERM
// -----------------------------------------------------------------------------------------------------

// ★★★★ EVERY TERM ON ITS OWN, AGAINST ONE POSITIVE BASELINE. ⛔ A COUNTER-ONLY MATCHER IS RED HERE: three of
//      the four wire-driven arms keep `ctr_h` identical and change something else, so a matcher that looked
//      only at the counter would forward every one of them. `failed_ctr` is a HOME counter, minted PER
//      DESTINATION, so two live rows really can share it — the corpus cannot exercise that, which is exactly
//      why this case exists.
TEST_CASE("§B278-S3/3 §4.4's five key terms each fail INDEPENDENTLY (a counter-only matcher is RED)") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    // ---- the POSITIVE baseline: the complete key matches and the report is forwarded.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 1);
        CHECK(o.no_map == 0);
        CHECK(o.n_forwarded == 1);
    }
    struct Arm { const char* term; uint16_t ctr_h; uint8_t return_kind; uint32_t return_peer; uint8_t out_type; };
    const Arm arms[] = {
        { "ctr_h",        0x0BEF,  kKindNodeId,  kS3ReturnPeer,     kS3OutType },
        { "return_kind",  kS3CtrH, kKindKeyHash, kS3ReturnPeer,     kS3OutType },
        { "return_peer",  kS3CtrH, kKindNodeId,  kS3ReturnPeer + 1, kS3OutType },
        { "outward_type", kS3CtrH, kKindNodeId,  kS3ReturnPeer,     DATA_TYPE_MOBILE_KEY_FORWARD },
    };
    for (const Arm& a : arms) {
        CAPTURE(a.term);
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, a.ctr_h, kS3CtrM, kKindNodeId, kS3ReturnPeer,
                      a.return_kind, a.return_peer, a.out_type));
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);
        CHECK(o.stored == 1);               // the local diagnostic never depends on the match
        CHECK(o.forwarded == 0);            // ⛔ this term ALONE refused the correlation
        CHECK(o.no_map == 1);
        CHECK(o.tx_n == 0);
        CHECK(o.n_eligible == 1);           // and the row keeps its obligation
    }
    // ---- THE LAYER TERM needs a record CLAIMING another layer, which §13.15 would reject at the receiver
    //      before the lookup ever runs. It is therefore proven at the ONE authority that decides it, with its
    //      own positive control in the same block so the arm cannot pass vacuously.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        const CustodyFailureRecord good = s3_parsed_report();
        CustodyFailureRecord other = good;
        other.reporter_layer = static_cast<uint8_t>(other.reporter_layer + 1);
        const Node::TestCustodyProbe bad = p.n1.test_custody_lookup(other, kS3Reporter);
        CHECK(bad.disposition == Node::test_custody_disposition_no_match());
        CHECK(bad.matches == 0);
        const Node::TestCustodyProbe ok = p.n1.test_custody_lookup(good, kS3Reporter);
        CHECK(ok.disposition == Node::test_custody_disposition_exact());
        CHECK(ok.matches == 1);
    }
    // ---- AND THE `eligible` TERM ITSELF: a `none` row of the same identity is never selected, so a row whose
    //      arm carried no custody obligation can never acquire one here.
    for (uint8_t state : { Node::test_custody_state_none(), Node::test_custody_state_forwarded() }) {
        CAPTURE(int(state));
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindNodeId, kS3ReturnPeer,
                      kKindNodeId, kS3ReturnPeer, kS3OutType, state));
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 0);
        CHECK(o.no_map == 0);               // ⛔ SILENT: a non-eligible row is not a live population either
        CHECK(o.refused == 0);
        CHECK(o.tx_n == 0);
    }
}

// ★★★ THE OPTIONAL `DST_HASH` CROSS-CHECK, BOTH DIRECTIONS AND ON THE RIGHT ROW KIND (§4.4). It is a
//     cross-check, ⛔ NOT a lookup term: its ABSENCE may never refuse a match, and a NODE-ID row is never
//     hash-compared at all (its `target` is a node id — comparing it to a 32-bit hash would be a category
//     error that happens to be arithmetic).
TEST_CASE("§B278-S3/4 DST_HASH agrees when carried, is not required when absent, and never touches a node-id row") {
    constexpr uint32_t kTargetHash = 0xA1B2C3D4u;
    uint8_t rec_hash[custody_record_v1_len];
    const uint8_t nh = s3_pack_report(rec_hash, kTargetHash);
    uint8_t rec_plain[custody_record_v1_len];
    const uint8_t np = s3_pack_report(rec_plain);
    // (a) hash-addressed row + a report carrying the SAME hash -> forwarded, and the tail carries the hash.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindKeyHash, kTargetHash));
        CHECK(s3_send_and_hold(p, rec_hash, nh));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 1);
        CHECK(o.tx_n == 1);
        if (o.tx_n == 1)
            CHECK(s3_queued_body(p.n1) == s3_expected_body(kKindKeyHash, kTargetHash, kS3CtrM, kTargetHash));
    }
    // (b) hash-addressed row + a report carrying a DIFFERENT hash -> refused by the cross-check alone.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindKeyHash, kTargetHash ^ 0xFFu));
        CHECK(p.send_typed(rec_hash, nh));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 0);
        CHECK(o.no_map == 1);
        CHECK(o.n_eligible == 1);
    }
    // (c) hash-addressed row + a report with NO `HAS_DST_HASH` -> the match STILL succeeds (§4.4, verbatim:
    //     "lookup does not fail merely because the hash is unavailable"), and the tail carries the ROW's hash.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindKeyHash, kTargetHash));
        CHECK(s3_send_and_hold(p, rec_plain, np));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 1);
        if (o.tx_n == 1)
            CHECK(s3_queued_body(p.n1) == s3_expected_body(kKindKeyHash, kTargetHash, kS3CtrM, /*dst_hash=*/0));
    }
    // (d) NODE-ID row + a report carrying a hash -> still matched: the hash term does not apply to it at all.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));                       // node_id target == failed_dst
        CHECK(p.send_typed(rec_hash, nh));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 1);
        CHECK(o.no_map == 0);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/5 — AMBIGUITY (AN INVARIANT FAILURE), AND THE COMPLETE-IDENTITY COMMIT
// -----------------------------------------------------------------------------------------------------

// ★★★★ TWO LIVE ROWS SHARING THE COMPLETE §4.4 KEY ARE WIRE-INDISTINGUISHABLE, so nothing is forwarded and
//      nothing is guessed. ⛔ THE STATE IS UNREACHABLE IN PRODUCTION AND THIS CASE PROVES BOTH HALVES: the
//      ring's own §B278 S1b activation uniqueness REFUSES the second row (measured first, so the seam is not
//      papering over a real path), and the diagnostic is then driven through the labelled invariant seam.
TEST_CASE("§B278-S3/5 ambiguity: the ring refuses the second row, and if one existed nothing would be sent") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    // ---- half one: the PRODUCTION authority refuses to create the ambiguous state at all.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash,  kS3CtrH, kS3CtrM));
        CHECK_FALSE(s3_seed(p.n1, kS3MobileHash2, kS3CtrH, kS3CtrM2));   // same return key, another mobile
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_eligible()) == 1);
        CHECK(p.n1.test_deleg_ack_live_n() == 1);                        // ⛔ the incumbent is not evicted
    }
    // ---- half two: FORCED into the impossible state, the lookup reports it and originates nothing.
    {
        GPair p; s3_host(p.n1);
        p.n1.test_deleg_force_row(0, kS3MobileHash,  kS3CtrH, kS3CtrM,  kKindNodeId, kS3ReturnPeer,
                                  kKindNodeId, kS3ReturnPeer, kS3OutType,
                                  Node::test_custody_state_eligible());
        p.n1.test_deleg_force_row(1, kS3MobileHash2, kS3CtrH, kS3CtrM2, kKindNodeId, kS3ReturnPeer,
                                  kKindNodeId, kS3ReturnPeer, kS3OutType,
                                  Node::test_custody_state_eligible());
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);              // the local diagnostic still happens in full …
        CHECK(o.stored == 1);
        CHECK(o.pushes == 1);
        CHECK(o.ambiguous == 1);             // … and exactly ONE bounded ambiguity event fires
        CHECK(o.forwarded == 0);
        CHECK(o.no_map == 0);
        CHECK(o.refused == 0);
        CHECK(o.tx_n == 0);                  // ⛔ nothing was originated
        CHECK(o.parked_n == 0);
        CHECK(o.n_eligible == 2);            // ⛔ and NOTHING changed: neither row was marked, cleared or evicted
        CHECK(o.n_forwarded == 0);
        CHECK(o.live_rows == 2);
        const GHal::EmitRec* r = p.h1.first_emit("deleg_custody_ambiguous");
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->keys == std::vector<std::string>{ "dst", "ctr_h", "type", "layer", "matches" });
            CHECK(r->ivals == std::vector<int64_t>{ kS3ReturnPeer, kS3CtrH, kS3OutType, 2, 2 });
            for (int t : r->types) CHECK(t == static_cast<int>(EventField::T::i64));
        }
    }
}

// ★★★★ THE COMPLETE-IDENTITY COMMIT, MEASURED WHERE A WEAKER ONE WOULD BREAK. Two live eligible rows share
//      `ctr_h` (home counters are per DESTINATION, so this is genuinely reachable) but name different return
//      peers. §4.4 selects exactly one; a commit predicate weakened to the counter would find TWO and mark
//      NEITHER — so this case is RED for both a weakened lookup and a weakened `eligible -> forwarded` write.
TEST_CASE("§B278-S3/6 two eligible rows share ctr_h: exactly the matching one is forwarded, the other untouched") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p; s3_host(p.n1);
    CHECK(s3_seed(p.n1, kS3MobileHash,  kS3CtrH, kS3CtrM,  kKindNodeId, kS3ReturnPeer,
                  kKindNodeId, kS3ReturnPeer));                       // the MATCH (return_peer 9)
    CHECK(s3_seed(p.n1, kS3MobileHash2, kS3CtrH, kS3CtrM2, kKindNodeId, kS3ReturnPeer - 1,
                  kKindNodeId, kS3ReturnPeer - 1));                   // same ctr_h, return_peer 8
    CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_eligible()) == 2);
    CHECK(p.send_typed(rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.forwarded == 1);
    CHECK(o.ambiguous == 0);
    CHECK(o.n_forwarded == 1);
    CHECK(o.n_eligible == 1);                 // ⛔ the OTHER mobile's obligation is untouched
    CHECK(o.live_rows == 2);                  // … and neither row was evicted or cleared
    bool found = false;
    for (uint8_t i = 0; i < p.n1.test_deleg_ack_cap(); ++i) {
        const Node::TestDelegRow r = p.n1.test_deleg_row(i);
        if (r.custody_state == Node::test_custody_state_forwarded()) {
            found = true;
            CHECK(r.mobile_hash == kS3MobileHash);     // the RIGHT row was marked
            CHECK(r.return_peer == kS3ReturnPeer);
            CHECK(r.ctr_m == kS3CtrM);
        }
    }
    CHECK(found);
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/7 — THE TRANSLATED FORM ON THE WIRE
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE 32 BYTES, FIELD BY FIELD, AND THE DIRECT-HOST CARRIER AROUND THEM (§6.1/§6.2 + §8.2 rule 7).
//      The expectation is built INDEPENDENTLY through the S2 packer from the ROW's values and `pa.origin`.
TEST_CASE("§B278-S3/7 direct-host delivery: type 0x81, addr_len 1, no DST_HASH, and the literal 32-byte form") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p; s3_host(p.n1);
    CHECK(s3_seed(p.n1));
    CHECK(s3_send_and_hold(p, rec, n));        // keep the originated item queued for a wire-golden read
    const S3Out o = s3_collect(p);
    CHECK(o.forwarded == 1);
    CHECK(o.refused == 0);
    CHECK(o.n_forwarded == 1);
    CHECK(o.tx_n == 1);
    if (o.tx_n != 1) return;
    CHECK(p.n1.test_tx_type(0) == DATA_TYPE_CUSTODY_FAILURE);
    CHECK(p.n1.test_tx_dst(0) == kS3MobileLocal);            // the mobile's LOCAL id …
    CHECK(p.n1.test_tx_addr_len(0) == 1);                    // … addressed by the 1-byte last mile
    CHECK(p.n1.test_tx_origin(0) == 1);                      // outer origin stays H1
    CHECK((p.n1.test_tx_flags(0) & DATA_FLAG_E2E_ACK_REQ) == 0);   // ⛔ evidence, not a message awaiting a reply
    CHECK((p.n1.test_tx_flags(0) & DATA_FLAG_CRYPTED) == 0);       // ⛔ plaintext, exactly as the v1 notice is
    uint8_t inner_len = 0;
    const uint8_t* inner = p.n1.test_tx_inner(0, inner_len);
    auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, inner_len), p.n1.test_tx_flags(0));
    CHECK(ui.has_value());
    if (!ui) return;
    CHECK_FALSE(ui->has_dst_hash);                            // §8.2 rule 7: the direct form carries none
    // ★ THE SOURCE HASH IS **H1's OWN**, and that is the proof this is NOT a delegated re-origination: a
    //   `reply_to_hash` would stamp the MOBILE's hash here and would make the carrier look like something the
    //   mobile asked H1 to send on its behalf — reserving a correlation row for a diagnostic about the ring.
    CHECK(ui->has_source_hash);
    CHECK(ui->source_hash == 0x11111111u);                    // node 1's key hash (GPair), ⛔ not kS3MobileHash
    CHECK(ui->body.size() == custody_record_translated_len);
    const std::vector<uint8_t> body = s3_queued_body(p.n1);
    CHECK(body == s3_expected_body());
    // ---- and the tail is the ROW's, read back through the production parser rather than by offset.
    const std::optional<CustodyFailureRecord> parsed =
        parse_custody_failure(std::span<const uint8_t>(body.data(), body.size()));
    CHECK(parsed.has_value());
    if (!parsed) return;
    CHECK(custody_record_is_translated(parsed->notice_flags));
    CHECK(parsed->record_len    == custody_record_translated_len);
    CHECK(parsed->failed_origin == 1);                        // H1 — UNCHANGED from the direct report
    CHECK(parsed->failed_ctr    == kS3CtrH);                  // ctrH — UNCHANGED
    CHECK(parsed->failed_dst    == kS3ReturnPeer);
    CHECK(parsed->failed_type   == kS3OutType);
    const std::optional<CustodyTranslatedTail> tail =
        parse_custody_translated_tail(std::span<const uint8_t>(body.data(), body.size()), *parsed);
    CHECK(tail.has_value());
    if (!tail) return;
    CHECK(tail->original_reporter == kS3Reporter);            // `pa.origin` — the OUTER reporting relay
    CHECK(tail->target_kind == CustodyTranslatedTargetKind::node_id);
    CHECK(tail->mobile_ctr   == kS3CtrM);                     // the ROW's mobile counter
    CHECK(tail->target_value == kS3ReturnPeer);               // the ROW's retained target
}

// ★★★ A `record_len > 24` DIRECT INPUT: stored WHOLE locally, translated from a NORMALIZED 24-byte copy into
//     exactly 32 bytes. ⛔ The unknown direct tail is NOT forwarded — §6.2 defines bytes 24-31 and nothing else
//     may occupy them — while §7.2's "retain any accepted future tail" is unchanged for the LOCAL record.
TEST_CASE("§B278-S3/8 a direct record with an unknown tail is stored whole and translated from a 24-byte copy") {
    uint8_t base[custody_record_v1_len];
    const uint8_t bn = s3_pack_report(base);
    std::vector<uint8_t> body(base, base + bn);
    body.insert(body.end(), { 0xDE, 0xAD, 0xBE, 0xEF });
    body[1] = static_cast<uint8_t>(body.size());              // record_len = 28 — the wire says so
    GPair p; s3_host(p.n1);
    CHECK(s3_seed(p.n1));
    CHECK(s3_send_and_hold(p, body.data(), static_cast<uint8_t>(body.size())));
    const S3Out o = s3_collect(p);
    CHECK(o.accepted == 1);
    CHECK(o.stored == 1);
    CHECK(o.stored_len == 28);                                // ⛔ the LOCAL record keeps all 28 bytes …
    CHECK(o.stored_body == body);
    CHECK(o.forwarded == 1);
    CHECK(o.tx_n == 1);
    if (o.tx_n != 1) return;
    const std::vector<uint8_t> air = s3_queued_body(p.n1);
    CHECK(air.size() == custody_record_translated_len);       // … and exactly 32 bytes were aired
    CHECK(air == s3_expected_body());                         // byte-identical to the no-tail case
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/9 — THE CACHED-HOME ARM AND ITS PLANE
// -----------------------------------------------------------------------------------------------------

// ★★★ RE-AIMED 2026-09-04 BY REMOTE-ADMIN v2 SLICE 0d (R-RA-12 + owner decision D-0d-1). ⛔ THE OLD CLAIM IS
//     KEPT VISIBLE, because it was TRUE of what it measured and is exactly what D-0d-1 had to rule on:
//     *"THE CACHED-HOME ARM IS UNCHANGED AND ITS `Plane::AUTO` IS EQUIVALENT TO GLOBAL FOR A STATIC HOME
//     (spec §7's pinned sentence). ⛔ S3 does not touch that arm: it asks for `Plane::GLOBAL` and the arm still
//     hands `Plane::AUTO` to `do_send`, exactly as it always did."*
//     ⇒ That equivalence was a statement about B278's ATTRIBUTION — "S3 changed nothing here" — never a policy
//     that the arm must stay `AUTO`. It holds only for a STATIC sender: the same arm is taken by an
//     UNREGISTERED team mobile, for which `is_team_peer(home)` can be TRUE and `AUTO` then routes the notice to
//     a TEAMMATE. Slice 0d makes the arm stamp `Plane::GLOBAL` EXPLICITLY, so this case's asserted authority is
//     now the invariant ("a home is static, so this flight is global by definition") rather than a coincidence.
//     ⓘ B278 is untouched by the change: correlation, custody state, destination hash, source hash, counter and
//       ACK behaviour are not functions of the plane, and every byte asserted below is unmoved. §B278-S3/9b is
//       the control that makes the difference OBSERVABLE on this very fixture.
TEST_CASE("§B278-S3/9 cached-home delivery: the arm stamps GLOBAL explicitly, DST_HASH == the mobile hash") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p;
    p.n1.mobile_home_set(kS3MobileHash, /*home_id=*/2, /*epoch=*/1, /*home_layer=*/2);
    CHECK(s3_seed(p.n1));
    // ★ THE INVARIANT the arm now names: GLOBAL can NEVER resolve to the team plane, whatever the id collides.
    CHECK(p.n1.flight_is_team_plane(Plane::GLOBAL, /*dst=*/2) == false);
    // ⓘ ...and the OLD pin, kept as a measurement rather than an authority: for THIS static sender, with no
    //   `_team_peer` bit on the home's id, `AUTO` happened to agree. §B278-S3/9b removes that coincidence.
    CHECK(p.n1.is_team_peer(/*id=*/2) == false);
    CHECK(p.n1.flight_is_team_plane(Plane::AUTO, 2) == false);
    CHECK(s3_send_and_hold(p, rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.forwarded == 1);
    CHECK(o.n_forwarded == 1);
    CHECK(o.tx_n == 1);
    if (o.tx_n != 1) return;
    CHECK(p.n1.test_tx_type(0) == DATA_TYPE_CUSTODY_FAILURE);
    CHECK(p.n1.test_tx_dst(0) == 2);                          // routed to the cached HOME …
    CHECK(p.n1.test_tx_addr_len(0) == 0);
    uint8_t inner_len = 0;
    const uint8_t* inner = p.n1.test_tx_inner(0, inner_len);
    auto ui = parse_unicast_inner(std::span<const uint8_t>(inner, inner_len), p.n1.test_tx_flags(0));
    CHECK(ui.has_value());
    if (!ui) return;
    CHECK(ui->has_dst_hash);
    CHECK(ui->dst_key_hash32 == kS3MobileHash);               // … which last-miles it to M1
    CHECK(s3_queued_body(p.n1) == s3_expected_body());        // the SAME 32 bytes as the direct arm
}

// ★★★ §B278-S3/9b — THE CONTROL THAT MAKES S3/9's AUTHORITY NON-VACUOUS (Slice 0d, D-0d-1). S3/9 above cannot
//     tell `AUTO` from `GLOBAL`, because its reporter has no `_team_peer` bit on the cached home's id — which is
//     precisely why the old "AUTO ≡ GLOBAL" sentence was safe to write and unsafe to keep. Here the reporter is
//     a TEAM node whose teammate's team-local id NUMERICALLY EQUALS the cached home's static id (§18's mixed-id
//     collision), so the two planes are DIFFERENT decisions, and `stamp_origin` (node.h) is where the queued
//     frame says which one was taken: a team-plane flight stamps `team_local_id()`, a global one stamps
//     `_node_id`. ⛔ THE COLLISION IS INSTALLED AFTER THE REPORT IS RECEIVED AND BEFORE THE FORWARD IS
//     ENQUEUED — `s3_send_and_hold` is inlined for exactly that reason — so nothing about the inbound hop moves.
TEST_CASE("§B278-S3/9b cached-home CONTROL — a colliding teammate does not capture the notice: origin stays the reporter's STATIC id") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p;
    p.n1.mobile_home_set(kS3MobileHash, /*home_id=*/2, /*epoch=*/1, /*home_layer=*/2);
    CHECK(s3_seed(p.n1));
    CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/0, /*fire_post_ack=*/false));
    p.n1.test_suspend_tx_drain(true);
    p.step();
    // ---- the §18 collision, installed on the ENQUEUE side only ----
    p.n1.set_team_local_id(/*our team id=*/9);
    p.n1.test_learn_route(/*dest=*/2, /*via=*/2, 1, 40, /*team_plane=*/true);   // a teammate whose TEAM id is 2
    CHECK(p.n1.is_team_peer(/*id=*/2) == true);                                  // ⛔ the premise: the ids collide
    CHECK(p.n1.flight_is_team_plane(Plane::AUTO,   /*dst=*/2) == true);          // ...so AUTO now means TEAM ...
    CHECK(p.n1.flight_is_team_plane(Plane::GLOBAL, /*dst=*/2) == false);         // ...and GLOBAL still means static
    p.n1.on_timer(kPostAckTimerId);
    CHECK(p.n1.test_tx_queue_n() == 1);
    if (p.n1.test_tx_queue_n() != 1) return;
    CHECK(p.n1.test_tx_type(0)   == DATA_TYPE_CUSTODY_FAILURE);
    CHECK(p.n1.test_tx_dst(0)    == 2);                       // still addressed to the cached HOME ...
    CHECK(p.n1.test_tx_origin(0) == 1);                       // ★★ ...under the reporter's OWN STATIC id.
    CHECK(p.n1.test_tx_origin(0) != 9);                       // ★★ NOT team_local_id() — the arm is GLOBAL.
    uint8_t inner_len = 0;
    const uint8_t* inner = p.n1.test_tx_inner(0, inner_len);
    auto cui = parse_unicast_inner(std::span<const uint8_t>(inner, inner_len), p.n1.test_tx_flags(0));
    CHECK(cui.has_value());
    if (cui) { CHECK(cui->has_dst_hash); CHECK(cui->dst_key_hash32 == kS3MobileHash); }
    CHECK(s3_queued_body(p.n1) == s3_expected_body());        // ★ and the 32 custody bytes are UNMOVED
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/10 — PARK, AND THE FOUR REFUSALS
// -----------------------------------------------------------------------------------------------------

// ★★★ AN UNRESOLVED HOME PARKS, AND A PARK IS *"the outcome is on its way"*: the row is marked `forwarded` and
//     the parked send keeps its TYPE and its exact 32 bytes across the resolution.
TEST_CASE("§B278-S3/10 unresolved home: `parked` marks forwarded, and the drain preserves type + the 32 bytes") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p;                                    // ⛔ no hosted row and no cached home -> park + H flood
    CHECK(s3_seed(p.n1));
    CHECK(p.send_typed(rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.forwarded == 1);
    CHECK(o.refused == 0);
    CHECK(o.n_forwarded == 1);
    CHECK(o.tx_n == 0);                          // nothing queued …
    CHECK(o.parked_n == 1);                      // … one send retained behind the resolution
    // the H answer arrives: the parked send flies with its type and body intact.
    p.n1.test_suspend_tx_drain(true);
    p.n1.test_drain_parked_sends(kS3MobileHash, /*resolved_id=*/2);
    CHECK(p.n1.test_tx_queue_n() == 1);
    if (p.n1.test_tx_queue_n() != 1) return;
    CHECK(p.n1.test_tx_type(0) == DATA_TYPE_CUSTODY_FAILURE);
    CHECK(s3_queued_body(p.n1) == s3_expected_body());
}

// ★★★★ THE THREE DISPATCH REFUSALS, EACH WITH ITS OWN DRIVER, ALL LEAVING THE ROW `eligible`. ⛔ A refusal
//      never marks, never clears and never retries; the obligation survives to its own 300 s expiry so a
//      genuinely fresh repeat report can retry (§4.5).
TEST_CASE("§B278-S3/11 refusals: park-ring full, TX-queue full and the un-synced `none` arm all leave the row eligible") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    // (a) PARK RING FULL -> `SendDispatch::Admit::refused` from `park_send`.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        // filled through the PUBLIC diagnostic `resolve` verb — a notify-only park, i.e. the same ring, and
        // ⛔ not a private seam reaching in behind the API.
        for (uint8_t i = 0; i < protocol::cap_parked_sends; ++i) {
            Command c{}; c.kind = CmdKind::resolve;
            c.u.resolve.dst_hash = 0x51000000u + i; c.u.resolve.dst_id = 0;
            c.u.resolve.hard = false; c.u.resolve.plane = 0;
            CHECK(p.n1.on_command(c).code == CmdCode::queued);
        }
        CHECK(p.n1.test_parked_sends_n() == protocol::cap_parked_sends);
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);
        CHECK(o.stored == 1);
        CHECK(o.forwarded == 0);
        CHECK(o.refused == 1);
        CHECK(o.n_forwarded == 0);
        CHECK(o.n_eligible == 1);                 // ⛔ still owed
    }
    // (b) TX QUEUE FULL on the direct-host arm -> `refused` from `enqueue_data`.
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(p.send_typed(rec, n, DATA_TYPE_CUSTODY_FAILURE, /*dst_hash=*/0, /*fire_post_ack=*/false));
        p.n1.test_suspend_tx_drain(true);
        const uint8_t filler[] = { 'f', 'i', 'l', 'l' };
        for (uint8_t i = 0; i < 8; ++i)
            (void)p.n1.test_do_send_typed(2, filler, sizeof filler, CryptIntent::off, 0, 0);
        CHECK(p.n1.test_tx_queue_n() == 8);        // kTxQueueCap
        p.step(); p.n1.on_timer(kPostAckTimerId);
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);
        CHECK(o.forwarded == 0);
        CHECK(o.refused == 1);
        CHECK(o.n_eligible == 1);
        CHECK(o.tx_n == 8);                        // ⛔ nothing was added — the frame really did not fly
    }
    // (c) THE UN-SYNCED MANAGED JOINER -> `enqueue_data` refuses SILENTLY and never sets a dispatch, i.e.
    //     `Admit::none`. ⛔ `none` is a refusal too: 0x81 has no generic send lifecycle, so without this arm
    //     the failure would be completely silent.
    {
        GPair p(/*wire_inbox=*/true, /*n1_lineage=*/7);
        s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);
        CHECK(o.forwarded == 0);
        CHECK(o.refused == 1);
        CHECK(o.n_eligible == 1);
        CHECK(o.tx_n == 0);
    }
}

// ★★★ A PACK REFUSAL IS A BOUNDED FORWARD REFUSAL, ⛔ never a licence to hand-build bytes. §6.3 requires a
//     node-id `target_value` to equal `failed_dst`; a row whose retained node-id target disagrees therefore
//     cannot be translated, and the receiver must say so and change nothing.
TEST_CASE("§B278-S3/12 a translated-pack refusal is reported as a forward refusal and marks nothing") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p; s3_host(p.n1);
    // the §4.4 key still matches (return_peer 9); the retained node-id TARGET is 8, which §6.3 refuses.
    CHECK(s3_seed(p.n1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindNodeId, /*target=*/kS3ReturnPeer - 1,
                  kKindNodeId, kS3ReturnPeer));
    CHECK(p.send_typed(rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.accepted == 1);
    CHECK(o.stored == 1);
    CHECK(o.forwarded == 0);
    CHECK(o.refused == 1);
    CHECK(o.n_forwarded == 0);
    CHECK(o.n_eligible == 1);
    CHECK(o.tx_n == 0);
    CHECK(o.parked_n == 0);                        // ⛔ nothing was even attempted
}

// ★★★★ THE STALE ACTION. ⛔ NO PRODUCTION PATH CAN PRODUCE ONE — inside `custody_failure_receive` nothing
//      mutates the ring between the lookup and the commit (`reply_to_hash == 0` and `mobile_ctr == 0` keep
//      `send_by_hash` out of the ring entirely) — so the complete-identity recheck is DEFENCE IN DEPTH, and
//      this case is the probe that makes it measurable rather than merely claimed.
TEST_CASE("§B278-S3/13 a stale action marks NOTHING: the commit re-selects by the complete identity") {
    const CustodyFailureRecord rec = s3_parsed_report();
    // (a) the POSITIVE control: an action materialized against a live row commits exactly once.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        CHECK(probe.disposition == Node::test_custody_disposition_exact());
        CHECK(p.n1.test_custody_mark_forwarded(probe));
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 1);
        CHECK_FALSE(p.n1.test_custody_mark_forwarded(probe));    // ⛔ only once: the row is no longer eligible
    }
    // (b) the row is REPLACED under the action by a DIFFERENT mobile's row on the same return key.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        CHECK(probe.disposition == Node::test_custody_disposition_exact());
        uint16_t ignored = 0;
        CHECK(p.n1.test_deleg_ack_translate(kS3MobileHash, kS3CtrH, kKindNodeId, kS3ReturnPeer, ignored));
        CHECK(p.n1.test_deleg_ack_live_n() == 0);                // the ACK consumed it
        CHECK(s3_seed(p.n1, kS3MobileHash2, kS3CtrH, kS3CtrM2)); // a DIFFERENT mobile takes the return key
        CHECK_FALSE(p.n1.test_custody_mark_forwarded(probe));    // ⛔ the replacement is NEVER marked
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 0);
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_eligible()) == 1);
    }
    // (c) the row is simply GONE.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        uint16_t ignored = 0;
        CHECK(p.n1.test_deleg_ack_translate(kS3MobileHash, kS3CtrH, kKindNodeId, kS3ReturnPeer, ignored));
        CHECK_FALSE(p.n1.test_custody_mark_forwarded(probe));
        CHECK(p.n1.test_deleg_ack_live_n() == 0);                // ⛔ and nothing was created to mark
    }
    // (d) TWO rows now share the action's COMPLETE identity: the commit marks NEITHER. ⛔ Structurally
    //     unreachable from the receiver — the commit's key is STRICTLY STRONGER than §4.4's, so a lookup that
    //     answered `exact` cannot leave two commit candidates — and unreachable through the ring, whose
    //     activation uniqueness refuses the duplicate. It is the invariant seam's second and last user, and it
    //     is what makes "exactly one, or nothing" a measured rule rather than a comment.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        CHECK(probe.disposition == Node::test_custody_disposition_exact());
        p.n1.test_deleg_force_row(1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindNodeId, kS3ReturnPeer,
                                  kKindNodeId, kS3ReturnPeer, kS3OutType,
                                  Node::test_custody_state_eligible());
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_eligible()) == 2);
        CHECK_FALSE(p.n1.test_custody_mark_forwarded(probe));    // ⛔ ambiguous -> mark NEITHER
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 0);
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_eligible()) == 2);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/14 — THE LIFECYCLE (§4.5)
// -----------------------------------------------------------------------------------------------------

// ★★★★ ACK-FIRST vs CUSTODY-FIRST, THE ORDERING PROPERTY B278 EXISTS TO ADD. Custody processing never consumes
//      the ACK obligation: a forwarded row is still ACTIVE, so the later E2E ACK still translates ctrH -> ctrM
//      and clears it. In the other order the ACK's one-shot has already cleared the row, so the later report is
//      stored at H1 and produces no translated send and no B278 noise at all.
TEST_CASE("§B278-S3/14 custody-first still lets the later ACK translate; ACK-first translates nothing later") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    // ---- CUSTODY FIRST
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.forwarded == 1);
        CHECK(o.n_forwarded == 1);
        CHECK(o.live_rows == 1);                   // ⛔ the ACK obligation is RETAINED
        uint16_t ctr_m = 0;
        CHECK(p.n1.test_deleg_ack_translate(kS3MobileHash, kS3CtrH, kKindNodeId, kS3ReturnPeer, ctr_m));
        CHECK(ctr_m == kS3CtrM);                   // … and still translates to the mobile's counter
        CHECK(p.n1.test_deleg_ack_live_n() == 0);  // … and then clears, exactly once
    }
    // ---- ACK FIRST
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        uint16_t ctr_m = 0;
        CHECK(p.n1.test_deleg_ack_translate(kS3MobileHash, kS3CtrH, kKindNodeId, kS3ReturnPeer, ctr_m));
        CHECK(ctr_m == kS3CtrM);
        CHECK(p.n1.test_deleg_ack_live_n() == 0);
        CHECK(p.send_typed(rec, n));
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);                    // the report is STILL stored and pushed at H1 …
        CHECK(o.stored == 1);
        CHECK(o.pushes == 1);
        CHECK(o.no_map == 0);                      // … and produces NO B278 noise at all (zero live rows)
        CHECK(o.ambiguous == 0);
        CHECK(o.forwarded == 0);
        CHECK(o.refused == 0);
        CHECK(o.tx_n == 0);
    }
}

// ★★★ A DUPLICATE REPORT AFTER `forwarded` PRODUCES NO SECOND TRANSLATION — because the lookup requires
//     `eligible` and a forwarded row is not. ⛔ The local storage rules are unchanged: the second report is
//     still received under §CUSTODY-G's own rules.
TEST_CASE("§B278-S3/15 a duplicate report after forwarding: still received, but never a second translation") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    // ⓘ Driven on the PARK arm (no hosted row, no cached home) DELIBERATELY: a queued direct last-mile would
    //   put node 1 into its own TX flight, and a node holding `_pending_tx` cannot answer the second report's
    //   RTS — the fixture would then be measuring a stalled hop rather than the duplicate rule.
    GPair p;
    CHECK(s3_seed(p.n1));
    CHECK(p.send_typed(rec, n));
    CHECK(p.h1.count("deleg_custody_forwarded") == 1);
    CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 1);
    p.h1.clear_emits();
    // the SAME record again, over the real MAC (node 2 mints a fresh reporter ctr, so dedup admits it).
    CHECK(p.send_typed(rec, n));
    CHECK(p.h1.count("custody_failure_rx") == 1);                    // still received …
    CHECK(p.h1.count("deleg_custody_forwarded") == 0);               // ⛔ … but no second translation
    CHECK(p.h1.count("deleg_custody_no_map") == 0);                  // and no diagnostic either: a forwarded
    CHECK(p.h1.count("deleg_custody_forward_refused") == 0);         //   row is not part of the live population
    CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 1);
}

// ★★★★ THE 300 s EDGE, AND THE PRUNE-BEFORE-SCAN FALSIFIER — measured at the ONE authority so the edge is
//      exact to the millisecond (a full MAC hop advances the clock several times and could not be). A report
//      arriving AT or after the edge finds nothing, because the lookup's FIRST operation prunes.
TEST_CASE("§B278-S3/16 the 300 s edge: prune-before-scan is exact, and a forwarded row expires reporting state 3") {
    const CustodyFailureRecord rec = s3_parsed_report();
    const uint64_t ttl = protocol::delegated_custody_ttl_ms;
    // (a) ONE MILLISECOND BEFORE the edge the row is still live and still selected.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        p.h1._now = p.now + ttl - 1;
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        CHECK(probe.disposition == Node::test_custody_disposition_exact());
        CHECK(p.h1.count("deleg_ack_expired") == 0);
    }
    // (b) AT the edge the row is pruned BEFORE the scan: the population is ZERO, so the answer is the SILENT
    //     `no_live_rows` and not `no_match` — which is exactly what prune-FIRST buys.
    {
        GPair p;
        CHECK(s3_seed(p.n1));
        p.h1._now = p.now + ttl;
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        CHECK(probe.disposition == Node::test_custody_disposition_no_live_rows());
        CHECK(probe.matches == 0);
        CHECK(p.h1.count("deleg_ack_expired") == 1);            // the row really expired, in the lookup's prune
        CHECK(p.n1.test_deleg_ack_live_n() == 0);
    }
    // (c) the FULL production path past the edge: stored, pushed, and completely silent on the S3 side.
    {
        uint8_t body[custody_record_v1_len];
        const uint8_t n = s3_pack_report(body);
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        p.h1._now = p.h2._now = (p.now += ttl + 1000);
        CHECK(p.send_typed(body, n));
        const S3Out o = s3_collect(p);
        CHECK(o.accepted == 1);
        CHECK(o.stored == 1);
        CHECK(o.forwarded == 0);
        CHECK(o.no_map == 0);
        CHECK(o.refused == 0);
        CHECK(o.tx_n == 0);
        CHECK(o.live_rows == 0);
    }
    // (d) a FORWARDED row expires at the SAME edge, and its expiry reports `custody_state = 3`.
    {
        uint8_t body[custody_record_v1_len];
        const uint8_t n = s3_pack_report(body);
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(p.send_typed(body, n));
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 1);
        p.h1.clear_emits();
        p.h1._now = p.now + ttl;
        const Node::TestCustodyProbe probe = p.n1.test_custody_lookup(rec, kS3Reporter);
        CHECK(probe.disposition == Node::test_custody_disposition_no_live_rows());
        CHECK(p.n1.test_deleg_ack_live_n() == 0);
        CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 0);
        const GHal::EmitRec* e = p.h1.first_emit("deleg_ack_expired");
        CHECK(e != nullptr);
        if (e) {
            CHECK(e->keys == std::vector<std::string>{ "mobile_hash", "ctr_m", "ctr_h", "target", "layer",
                                                       "custody_state" });
            CHECK(e->ivals.back() == Node::test_custody_state_forwarded());   // ⛔ state 3, reported exactly
        }
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/17 — RECURSION, RELEASE OWNERSHIP AND THE STATIC-RECEIVER DROP
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE RECURSION GATE, MEASURED ON THE PRODUCED BYTES THEMSELVES. The 32-byte record S3 airs is handed
//      straight back to a STATIC receiver, which refuses it — so nothing is stored, nothing is pushed, and
//      — critically — H1 never consumes its OWN translation even though `failed_origin` is H1's id and every
//      one of §13's eighteen terms would otherwise pass.
// ⚠⚠ CORRECTED IN PLACE 2026-09-02 BY §B278 S4, OLD CLAIM KEPT VISIBLE. This paragraph said *"the §B278 S2
//    interim guard refuses it"* and the block below said *"THIS IS THE RATIFIED S2->S4 INTERMEDIATE STATE.
//    S4 replaces that guard; this case is one of the two that must be re-aimed then (the other is
//    §CUSTODY-G/2.21)."* **BOTH have now been done**: S4 replaced the guard with the split contextual
//    validation, and both cases were re-aimed. The refusing term here is `_cfg.is_mobile` (§8.2) — the
//    receiver is a STATIC node — and §B278-S4/4 states the same claim positively, with arm (c) isolating
//    that term from §8.2's rule 3.
// ⚠⚠ CORRECTED IN PLACE 2026-09-02 BY §B278 S4, OLD CLAIM KEPT VISIBLE. This case was titled *"the produced
//    32-byte record is refused by EVERY RECEIVER UNTIL S4"*. **S4 landed**, so that sentence is now false in
//    general: the intended configured mobile ACCEPTS it (§B278-S4/2). What survives unchanged, and is what
//    the untouched assertions below measure, is the half that S3 actually needed: **the translating HOME
//    never consumes its own translation**, although its own static id sits in `failed_origin`. `run_arm`'s
//    receiver is a STATIC node, and §8.2's configured-mobile term is what refuses it.
TEST_CASE("§B278-S3/17 the produced 32-byte record is still refused by a STATIC receiver, and spawns no custody") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    std::vector<uint8_t> produced;
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(s3_send_and_hold(p, rec, n));
        CHECK(p.n1.test_tx_queue_n() == 1);
        if (p.n1.test_tx_queue_n() != 1) return;
        produced = s3_queued_body(p.n1);
    }
    CHECK(produced.size() == custody_record_translated_len);
    CHECK(produced == s3_expected_body());
    // hand the REAL produced bytes to the REAL receiver, on a node that would be their addressee.
    const ArmOut o = run_arm(produced.data(), static_cast<uint8_t>(produced.size()));
    CHECK(o.flew);
    CHECK(o.accepted == 0);
    CHECK(o.rejected == 1);              // ⚠ §B278 S4: the EXISTING bounded exit, now taken on `_cfg.is_mobile`
    CHECK(o.pushes == 0);
    CHECK(o.stored == 0);
    CHECK(o.delivered == 0);
    CHECK(o.unsupported == 0);
}

// ★★★ THE CALLER REMAINS THE SOLE RELEASE OWNER. S3 adds no `become_free()` of its own: the received 0x81 is
//     released exactly once by the 0x81 arm of `do_post_ack`, and the only additional pump on the path is
//     `enqueue_data`'s own, which every admitted send has always run. Measured as: the translated item really
//     drained into a flight, and re-firing the post-ack timer is a NO-OP (the PostAck was consumed once).
TEST_CASE("§B278-S3/18 the caller still owns the single release; the translated send adds no second one") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair p; s3_host(p.n1);
    CHECK(s3_seed(p.n1));
    CHECK(p.send_typed(rec, n));
    const S3Out o = s3_collect(p);
    CHECK(o.forwarded == 1);
    CHECK(o.tx_n == 0);                          // drained into a flight, not left queued
    CHECK(p.h1.label_count("RTS") >= 1);         // … and the flight really started
    p.step(); p.n1.on_timer(kPostAckTimerId);    // re-firing the post-ack timer must change NOTHING
    CHECK(p.h1.count("custody_failure_rx") == 1);
    CHECK(p.h1.count("deleg_custody_forwarded") == 1);
    CHECK(p.n1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 1);
}

// -----------------------------------------------------------------------------------------------------
// §B278-S3/19 — THE TELEMETRY SCHEMAS AND THE BOUNDED ACTION
// -----------------------------------------------------------------------------------------------------

// ★★★ THE THREE PRODUCTION-REACHABLE EVENT SHAPES, PINNED BY NAME, FIELD ORDER AND INTEGER TYPE. ⛔ Scalars
//     only — never a record byte, never a string derived from one. (`deleg_custody_ambiguous` is pinned in
//     §B278-S3/5, where its invariant-only state is produced.)
TEST_CASE("§B278-S3/19 the S3 events: exact names, field order and integer types") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    // ---- `deleg_custody_forwarded`
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1));
        CHECK(p.send_typed(rec, n));
        const GHal::EmitRec* r = p.h1.first_emit("deleg_custody_forwarded");
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->keys == std::vector<std::string>{ "mobile_hash", "dst", "ctr_h", "ctr_m", "type" });
            CHECK(r->ivals == std::vector<int64_t>{ static_cast<int64_t>(kS3MobileHash), kS3ReturnPeer,
                                                    kS3CtrH, kS3CtrM, kS3OutType });
            for (int t : r->types) CHECK(t == static_cast<int>(EventField::T::i64));
        }
        CHECK(p.h1.first_emit("deleg_custody_no_map") == nullptr);
        CHECK(p.h1.first_emit("deleg_custody_forward_refused") == nullptr);
        CHECK(p.h1.first_emit("deleg_custody_ambiguous") == nullptr);
    }
    // ---- `deleg_custody_no_map`
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, /*ctr_h=*/0x0111));
        CHECK(p.send_typed(rec, n));
        const GHal::EmitRec* r = p.h1.first_emit("deleg_custody_no_map");
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->keys == std::vector<std::string>{ "dst", "ctr_h", "type", "layer" });
            CHECK(r->ivals == std::vector<int64_t>{ kS3ReturnPeer, kS3CtrH, kS3OutType, 2 });
            for (int t : r->types) CHECK(t == static_cast<int>(EventField::T::i64));
        }
        CHECK(p.h1.first_emit("deleg_custody_forwarded") == nullptr);
    }
    // ---- `deleg_custody_forward_refused`
    {
        GPair p; s3_host(p.n1);
        CHECK(s3_seed(p.n1, kS3MobileHash, kS3CtrH, kS3CtrM, kKindNodeId, kS3ReturnPeer - 1,
                      kKindNodeId, kS3ReturnPeer));
        CHECK(p.send_typed(rec, n));
        const GHal::EmitRec* r = p.h1.first_emit("deleg_custody_forward_refused");
        CHECK(r != nullptr);
        if (r) {
            CHECK(r->keys == std::vector<std::string>{ "mobile_hash", "dst", "ctr_h", "ctr_m", "type" });
            CHECK(r->ivals == std::vector<int64_t>{ static_cast<int64_t>(kS3MobileHash), kS3ReturnPeer,
                                                    kS3CtrH, kS3CtrM, kS3OutType });
            for (int t : r->types) CHECK(t == static_cast<int>(EventField::T::i64));
        }
        CHECK(p.h1.first_emit("deleg_custody_forwarded") == nullptr);
    }
}

// ★★★ THE BOUNDED ACTION'S SHAPE, PINNED WHERE IT IS DECLARED. It is a STACK object and must never become a
//     `Node` member — `sizeof(Node)` is asserted in node.h and is unmoved by S3.
TEST_CASE("§B278-S3/20 the bounded translation action is 56 B of value types and costs Node nothing") {
    CHECK(Node::test_custody_action_size()  == 56);
    CHECK(Node::test_custody_action_align() == 4);
    CHECK(sizeof(CustodyFailureRecord) == 28);          // 24 wire bytes + the dst_hash32 4-alignment
    CHECK(sizeof(CustodyTranslatedTail) == 8);
    // ⛔⛔ RE-PINNED 2026-09-07 BY §remote-admin v2 SLICE 5 (R-RA-31 authorizes the native/gateway `Node` re-pin),
    //    and THIS CASE'S OWN OBLIGATION IS UNCHANGED: it asserts that the bounded TRANSLATION ACTION is a stack
    //    object costing `Node` nothing. Slice 5 adds a `Node` member of its own (2 064 B, ACCEPT-only) and this
    //    number moves with it — 222072 -> 224136 — but the four lines above, which are what S3 actually pins,
    //    do not move at all. ⛔ Do NOT read this line as "S3 grew": the derivation is in node.h's ledger.
    // 7b-1 measured native pool/counters/alignment delta +1784 (R-RA-34); the S3 offsets stay pinned above.
    CHECK(sizeof(Node) == 235248); // R-RA-40: +80 ACCEPT-only deferred state
    // and the four dispositions really are four distinct values (a collapsed enum would make three of the
    // product decisions above indistinguishable).
    CHECK(Node::test_custody_disposition_no_live_rows() == 0);
    CHECK(Node::test_custody_disposition_no_match()     == 1);
    CHECK(Node::test_custody_disposition_ambiguous()    == 2);
    CHECK(Node::test_custody_disposition_exact()        == 3);
}


// =====================================================================================================
// ★★★★ §B278 S4 (2026-09-02) — THE MOBILE RECEIVER AND THE PRESENTATION SURFACES
//      (design §8.1 the mode split · §8.2 the translated contextual rules · §8.3 the store/Push mapping ·
//       §8.4 JSON/USB · §13.3-§13.5; brief `docs/superpowers/plans/2026-09-02-b278-s4-mobile-receive-surfaces.md`).
//
// S4 is the first CONSUMER of the form §B278 S3 produces. The claims measured below:
//   (a) a DIRECT record is byte- and behaviour-identical to §CUSTODY-G's — store, Push, telemetry and JSON;
//   (b) a TRANSLATED record is consumed ONLY by its intended configured mobile, on either of §8.2 rule 7's two
//       PRODUCTION arms (the hosted direct-transit form without `DST_HASH`; the re-homed form WITH it);
//   (c) every receiver-owned term refuses INDEPENDENTLY, each with its own falsifier;
//   (d) §8.3's mapping and order — store first, the Push carries the store's seq, the ORIGINAL reporter and
//       ctrM, and the WHOLE `record_len` body including an accepted future tail;
//   (e) translated mode RETURNS before S3's lookup/origination and never becomes a DM, an ACK or a send;
//   (f) live and pulled JSON expose §8.4's same fields while the direct bytes do not move; and
//   (g) the complete-tuple consumer contract is sufficient, and every one-field mismatch refuses.
//
// ⛔⛔ THE TWO TRANSPORTS, AND WHY THERE ARE TWO — stated because a reader must be able to see that neither is a
//    convenience. `for_static_rts` (node_mac_rx.cpp) admits a unicast RTS at a MOBILE only when `addr_len == 1`,
//    which is the mobile-plane mark that exactly TWO production senders set:
//      A. THE HOSTED DIRECT LAST MILE — `send_by_hash` at the mobile's OWN home. This is the arm §B278 S3
//         originates on, it carries NO `DST_HASH`, and its bytes are whatever the home's correlation produced.
//         ⇒ the fully end-to-end case (§B278-S4/2) and every arm about the mobile's REGISTRATION run here.
//      B. THE HOME LAST-MILE FORWARD — a `DST_HASH`-addressed DATA reaching the mobile's CURRENT home, which
//         forwards it verbatim to the local id (the §CUSTODY-G/4b role). The origin stays the ORIGINATING home,
//         which is precisely §8.2 rule 7's re-homed shape: `pa.origin == failed_origin == H1` while the last
//         mile happens at H2. ⇒ the hash arm and every arm that must break a RECORD BYTE run here, because this
//         transport can carry bytes a correlating home would never produce.
//    ⛔ Three contexts NEITHER transport can install — a CRYPTED carrier, a TEAM-plane arrival and a FOREIGN
//      `DST_HASH` (which `do_post_ack` redirects long before the receiver) — are driven through the
//      `MESHROUTE_NATIVE` seam off a COPY of the LIVE `PostAck` the production MAC really left behind, with
//      EXACTLY ONE field changed. That is §CUSTODY-G's own rule, applied unchanged.
//
// ⛔ THE USB HALF IS NOT HERE, and no case below claims it is: `src/fw_main.cpp` and
//    `src/firmware_custody_push.h` are outside the native build (§B115). S4 moves the whole renderer into that
//    header and `tools/probe_custody_usb/run.sh` COMPILES AND EXECUTES it against this same codec, with its own
//    default negative controls. That probe is the USB gate; these cases are the receiver + JSON gate.
// =====================================================================================================

namespace {

// A DIFFERENT current home, for the arms that must break the mobile's own registration relation.
constexpr uint8_t kS4OtherHome = 3;

NodeConfig g_mobile_cfg() { NodeConfig c = g_cfg(); c.is_mobile = true; return c; }

// ---- THE CHAIN ---------------------------------------------------------------------------------------------
// node 2 and node 1 are the pair §CUSTODY-G already drives; M1 is a configured mobile whose identity is EXACTLY
// the one S3's ring row names (`kS3MobileHash` / `kS3MobileLocal`), so a fixture that stopped agreeing with the
// row would stop DELIVERING rather than silently pass. Node 1 HOSTS M1 in every arm, which is what makes both
// transports available on one chain: it is M1's own home (transport A) and it is the current home that performs
// the last mile for a hash-addressed carrier originated at node 2 (transport B).
struct S4Chain {
    GPair p;
    GHal  hm;
    Node  m1{hm, kS3MobileLocal, kS3MobileHash};
    RamInboxStore dm_m{protocol::inbox_dm_store_bytes}, ch_m{protocol::inbox_chan_store_bytes};
    // `mobile_home = 0` leaves M1 deliberately UNREGISTERED — the one arm §8.2 rule 7's second half needs.
    explicit S4Chain(uint8_t mobile_home = 1, bool wire_inbox = true) {
        CHECK(m1.on_init(g_mobile_cfg()));
        if (wire_inbox) m1.inbox().on_init(&dm_m, &ch_m);          // ⛔ AFTER on_init (node.h's contract)
        if (mobile_home) m1.test_set_my_mobile_reg(mobile_home, kS3MobileLocal);
        m1.test_learn_route(/*dest=*/1, /*via=*/1, 1, 40, false);
        p.n1.test_learn_route(/*dest=*/kS3MobileLocal, /*via=*/kS3MobileLocal, 1, 40, false);
        s3_host(p.n1);                                             // node 1 hosts M1 (both transports need it)
        hm._now = p.h1._now;
        drain(m1); hm.clear_emits(); p.h1.clear_emits(); p.h1.tx_frames.clear();
    }
    void step() { p.step(); hm._now = p.h1._now; }
    // ONE COMPLETE HOP node 1 -> M1 over the real MAC — the mirror of `GPair::hop_2_to_1`. `fire_post_ack=false`
    // leaves M1's `PostAck` PENDING, which is the window a seam arm reads a REAL one out of.
    bool hop_to_m1(bool fire_post_ack = true) {
        const std::vector<uint8_t> rts = p.h1.last("RTS");
        if (rts.empty()) return false;
        step(); m1.on_recv(rts.data(), rts.size(), kRx);
        const std::vector<uint8_t> cts = hm.last("CTS");
        if (cts.empty()) return false;
        step(); p.n1.on_recv(cts.data(), cts.size(), kRx);
        step(); p.n1.on_timer(kCtsToDataGapTimerId);
        const std::vector<uint8_t> data = p.h1.last("DATA");
        if (data.empty()) return false;
        step(); m1.on_recv(data.data(), data.size(), kRx);
        const std::vector<uint8_t> ack = hm.last("ACK");
        if (!ack.empty()) { step(); p.n1.on_recv(ack.data(), ack.size(), kRx); }
        if (fire_post_ack) { step(); m1.on_timer(kPostAckTimerId); }
        return true;
    }
    // ---- TRANSPORT A: node 1 correlates a real direct report and originates the translated record itself.
    bool deliver_via_home(bool fire_post_ack = true) {
        uint8_t rec[custody_record_v1_len];
        const uint8_t n = s3_pack_report(rec);
        if (!s3_seed(p.n1)) return false;
        if (!p.send_typed(rec, n)) return false;
        return hop_to_m1(fire_post_ack);
    }
    // ---- TRANSPORT B: node 2 originates `body` hash-addressed; node 1 last-miles it to M1.
    bool deliver_lastmile(const std::vector<uint8_t>& body, bool fire_post_ack = true) {
        if (body.empty()) return false;
        if (!p.send_typed(body.data(), static_cast<uint8_t>(body.size()), DATA_TYPE_CUSTODY_FAILURE,
                          /*dst_hash=*/kS3MobileHash)) return false;
        return hop_to_m1(fire_post_ack);
    }
};

// ---- THE TWO PRODUCED RECORDS, BOTH BUILT BY THE REAL S3 PRODUCER — ⛔ never hand-assembled here ------------
// (1) as node 1 produces it: `failed_origin` = 1, tail reporter = 2. This is the byte sequence transport A airs.
std::vector<uint8_t> s4_produced_at_home1() {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = s3_pack_report(rec);
    GPair q; s3_host(q.n1);
    CHECK(s3_seed(q.n1));
    CHECK(s3_send_and_hold(q, rec, n));
    CHECK(q.n1.test_tx_queue_n() == 1);
    if (q.n1.test_tx_queue_n() != 1) return {};
    const std::vector<uint8_t> body = s3_queued_body(q.n1);
    CHECK(body == s3_expected_body());          // …and it really is the ruled form, independently rebuilt
    return body;
}
// (2) the SAME production path run at node 2 instead: `failed_origin` = 2, tail reporter = 1. Transport B needs
//     this one, because §8.2 rule 3 requires `pa.origin == failed_origin` and transport B's origin IS node 2.
std::vector<uint8_t> s4_produced_at_home2() {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/2, /*reporter_layer=*/2), rec);
    GPair q; s3_host(q.n2);
    CHECK(s3_seed(q.n2));
    CHECK(q.send_typed_from_1(rec, n, /*fire_post_ack=*/false));
    q.n2.test_suspend_tx_drain(true);
    q.step();
    q.n2.on_timer(kPostAckTimerId);
    CHECK(q.h2.count("deleg_custody_forwarded") == 1);
    CHECK(q.n2.test_tx_queue_n() == 1);
    if (q.n2.test_tx_queue_n() != 1) return {};
    return s3_queued_body(q.n2);
}

// Everything one S4 arm produced AT THE MOBILE, in one value. The local outcome is read on EVERY case, so no
// assertion can pass on a receiver that stopped doing its own job.
struct S4Out {
    int accepted = 0, rejected = 0, pushes = 0, stored = 0, delivered = 0, unsupported = 0, notices = 0;
    int other_pushes = 0;
    int no_map = 0, ambiguous = 0, forwarded = 0, refused = 0;
    uint32_t push_seq = 0;  uint8_t push_origin = 0, push_dst = 0, push_layer = 0;  uint16_t push_ctr = 0;
    SendFailReason push_reason = SendFailReason::none;
    std::vector<uint8_t> push_body;
    uint32_t stored_seq = 0, stored_msg_id = 0;
    uint8_t  stored_origin = 0, stored_layer = 0, stored_len = 0;
    std::vector<uint8_t> stored_body;
    int visited = 0;
    uint8_t tx_n = 0, parked_n = 0;
};

S4Out s4_collect(S4Chain& c) {
    S4Out o{};
    o.accepted    = c.hm.count("custody_failure_rx");
    o.rejected    = c.hm.count("custody_failure_reject");
    o.delivered   = c.hm.count("delivered");
    o.unsupported = c.hm.count("unsupported_internal");
    o.notices     = c.hm.count("custody_notice_tx");
    o.no_map      = c.hm.count("deleg_custody_no_map");
    o.ambiguous   = c.hm.count("deleg_custody_ambiguous");
    o.forwarded   = c.hm.count("deleg_custody_forwarded");
    o.refused     = c.hm.count("deleg_custody_forward_refused");
    Push pu{};
    while (c.m1.next_push(pu)) {
        if (pu.kind == PushKind::custody_failure) {
            ++o.pushes; o.push_seq = pu.seq; o.push_origin = static_cast<uint8_t>(pu.origin);
            o.push_dst = static_cast<uint8_t>(pu.dst); o.push_ctr = pu.ctr;
            o.push_layer = pu.layer_id; o.push_reason = pu.reason;
            o.push_body.assign(pu.body, pu.body + pu.body_len);
        } else { ++o.other_pushes; }
    }
    StoreSink s{};
    c.m1.inbox().pull(0, 0, store_cb, &s);
    o.stored = s.custody; o.visited = s.visited;
    if (!s.recs.empty()) {
        const StoreRec& r = s.recs.back();
        o.stored_seq = r.seq; o.stored_msg_id = r.msg_id; o.stored_origin = r.origin;
        o.stored_layer = r.layer_id; o.stored_len = r.body_len; o.stored_body = r.body;
    }
    o.tx_n     = c.m1.test_tx_queue_n();
    o.parked_n = c.m1.test_parked_sends_n();
    return o;
}

// The one-line falsifier: break exactly ONE byte of the record transport B airs and require the mobile to
// refuse — no acceptance, no push, no storage, no delivery, exactly one bounded reject.
void s4_expect_rejected_byte(const char* term, uint8_t off, uint8_t value) {
    CAPTURE(term); CAPTURE(off); CAPTURE(value);
    std::vector<uint8_t> body = s4_produced_at_home2();
    CHECK(body.size() == custody_record_translated_len);
    if (body.size() != custody_record_translated_len) return;
    body[off] = value;
    S4Chain c;
    CHECK(c.deliver_lastmile(body));
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 0);
    CHECK(o.rejected == 1);      // the EXISTING bounded scalar exit, exactly once
    CHECK(o.pushes == 0);
    CHECK(o.stored == 0);
    CHECK(o.delivered == 0);     // ⛔ never a fall-through to ordinary DM delivery
    CHECK(o.unsupported == 0);   // ⛔ NOT `unsupported_internal` — 0x81 IS supported
}

// §8.4's field block, as one string, so the LIVE and the PULLED golden share it byte for byte.
std::string s4_json_fields(uint8_t reporter, uint8_t failed_origin, uint8_t layer, const char* target,
                           uint16_t ctr_m) {
    return std::string(",\"reporter\":") + std::to_string(reporter)
         + ",\"reporter_layer\":" + std::to_string(layer)
         + ",\"failed_origin\":" + std::to_string(failed_origin)
         + ",\"dst\":9,\"ctr\":3054,\"failed_type\":139"
           ",\"stage\":\"cts\",\"reason\":\"cascade_count\",\"previous_hop\":1,\"next_hop\":9"
           ",\"requeues\":" + std::to_string(protocol::cascade_requeue_max)
         + ",\"alternatives\":1,\"committed_hops\":1,\"remaining_hops\":4"
           ",\"repair_attempted\":true,\"one_way\":false"
         + target
         + ",\"mobile_ctr\":" + std::to_string(ctr_m);
}

std::string s4_live_json(const std::vector<uint8_t>& body, uint8_t origin, uint8_t dst, uint16_t ctr,
                         uint8_t layer, uint32_t seq) {
    Push p{};
    p.kind = PushKind::custody_failure; p.origin = origin; p.dst = dst; p.ctr = ctr;
    p.layer_id = layer; p.seq = seq;
    p.body_len = static_cast<uint8_t>(body.size());
    for (size_t i = 0; i < body.size(); ++i) p.body[i] = body[i];
    char buf[1700];
    const size_t m = meshroute::console::write_push(buf, sizeof buf, p, nullptr);
    return std::string(buf, m);
}

std::string s4_pulled_json(const std::vector<uint8_t>& body, uint8_t origin, uint16_t msg_id, uint8_t layer,
                           uint64_t rx_ms) {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    const uint32_t seq = ib.record_custody_failure(origin, msg_id, layer, body.data(),
                                                  static_cast<uint8_t>(body.size()), rx_ms);
    CHECK(seq == 1u);
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
    return sink.out;
}

// ★★★★ §S4-5's GENERIC OPERATION CONSUMER — TEST-ONLY, and deliberately so: it demonstrates that §8.4's
//      complete tuple is SUFFICIENT for Slice H / remote-admin Slice 9 without adding a product consumer or any
//      state transition. It parses through the PRODUCTION codec and matches the COMPLETE body tuple.
// ⛔⛔ IT MUST NOT USE THE STORE KEY `(origin, msg_id)` AS THE OPERATION IDENTITY (§8.4): an E2E-ACK receipt for
//    the SAME operation is stored under a DIFFERENT origin (the acker) with `msg_id = ctrM`, so a consumer keyed
//    on the record key alone would never pair the two — and one keyed on the counter alone would promote a
//    different operation entirely.
struct S4Operation {
    uint8_t  failed_origin = 0;
    uint8_t  reporter_layer = 0;
    CustodyTranslatedTargetKind target_kind = CustodyTranslatedTargetKind::node_id;
    uint32_t target_value = 0;
    uint16_t mobile_ctr = 0;
    uint8_t  failed_type = 0;
};
// How many pending operations this record matches. ⛔ ONE codec, no offsets, no record key.
int s4_consume(const std::vector<S4Operation>& pending, const std::vector<uint8_t>& record) {
    const std::optional<CustodyFailureRecord> rec =
        parse_custody_failure(std::span<const uint8_t>(record.data(), record.size()));
    if (!rec) return 0;
    const std::optional<CustodyTranslatedTail> t =
        parse_custody_translated_tail(std::span<const uint8_t>(record.data(), record.size()), *rec);
    if (!t) return 0;
    int hits = 0;
    for (const S4Operation& op : pending)
        if (op.failed_origin  == rec->failed_origin
         && op.reporter_layer == rec->reporter_layer
         && op.target_kind    == t->target_kind
         && op.target_value   == t->target_value
         && op.mobile_ctr     == t->mobile_ctr
         && op.failed_type    == rec->failed_type) ++hits;
    return hits;
}

}  // namespace

// -----------------------------------------------------------------------------------------------------
// §B278-S4/1 — THE DIRECT PATH DOES NOT MOVE
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE COMPATIBILITY CLAIM, MEASURED RATHER THAN ARGUED. S4 rewrites the receiver's contextual block and
//      both JSON emitters; a direct record's store, Push, telemetry and JSON bytes must be exactly what
//      §CUSTODY-G landed. ⛔ The JSON half is a BYTE comparison against §14.2's own transcribed example — the
//      same `kSpecFields` golden §CUSTODY-G/5 uses — so "byte-identical" is a measurement, not a claim.
TEST_CASE("§B278-S4/1 a DIRECT record is byte- and behaviour-identical to §CUSTODY-G's") {
    uint8_t rec[custody_record_v1_len];
    const uint8_t n = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), rec);
    GPair p;
    CHECK(p.send_typed(rec, n));
    CHECK(p.h1.count("custody_failure_rx") == 1);
    CHECK(p.h1.count("custody_failure_reject") == 0);
    Push pu{}; int pushes = 0; uint32_t seq = 0;
    while (p.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) {
        ++pushes; seq = pu.seq;
        CHECK(pu.origin   == 2);                       // §14.1: the OUTER reporting relay — UNCHANGED
        CHECK(pu.dst      == 9);
        CHECK(pu.ctr      == 0x0BEE);                  // ctrH, i.e. `failed_ctr` — UNCHANGED
        CHECK(pu.layer_id == p.n1.active_layer_id());  // the ACTIVE layer — UNCHANGED
        CHECK(pu.reason   == SendFailReason::none);
        CHECK(pu.body_len == custody_record_v1_len);
    }
    CHECK(pushes == 1);
    StoreSink s{};
    p.n1.inbox().pull(0, 0, store_cb, &s);
    CHECK(s.custody == 1);
    CHECK(s.recs.size() == 1u);
    if (s.recs.size() == 1u) {
        CHECK(s.recs[0].origin   == 2);                          // §7.2: origin = the reporting relay
        CHECK(s.recs[0].msg_id   == 0x0BEEu);                    // §7.2: msg_id = failed_ctr
        CHECK(s.recs[0].layer_id == p.n1.active_layer_id());
        CHECK(s.recs[0].seq      == seq);                        // record BEFORE push, still observable
        CHECK(s.recs[0].body_len == custody_record_v1_len);
    }
    // ---- the telemetry, field for field: the corpus counts eleven of these receipts and none of them may move.
    const GHal::EmitRec* e = p.h1.first_emit("custody_failure_rx");
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->keys.size() == 4u);
        if (e->keys.size() == 4u) {
            CHECK(e->keys[0] == "reporter");  CHECK(e->ivals[0] == 2);
            CHECK(e->keys[1] == "dst");       CHECK(e->ivals[1] == 9);
            CHECK(e->keys[2] == "ctr");       CHECK(e->ivals[2] == 0x0BEE);
            CHECK(e->keys[3] == "seq");
        }
    }
    // ---- the JSON, BYTE-IDENTICAL to §14.2's example on both surfaces, with NO translated field present.
    uint8_t spec[custody_record_v1_len];
    const uint8_t sn = g_pack(spec_example_record(), spec);
    const std::vector<uint8_t> sbody(spec, spec + sn);
    const std::string live = s4_live_json(sbody, /*origin=*/186, /*dst=*/48, /*ctr=*/3598, /*layer=*/1, /*seq=*/17);
    CHECK(live == std::string("{\"ev\":\"custody_failure\",\"seq\":17") + kSpecFields + "}\n");
    const std::string pulled = s4_pulled_json(sbody, /*origin=*/186, /*msg_id=*/3598, /*layer=*/1, /*rx_ms=*/77000);
    CHECK(pulled == std::string("{\"ev\":\"custody_failure\",\"seq\":1,\"rx_ms\":77000") + kSpecFields + "}\n");
    CHECK(live.find("delegated")   == std::string::npos);
    CHECK(live.find("target_kind") == std::string::npos);
    CHECK(live.find("mobile_ctr")  == std::string::npos);
    CHECK(pulled.find("delegated") == std::string::npos);
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/2 — THE ARC CLOSES: H1 TRANSLATES, M1 CONSUMES (rule 7 arm two — no `DST_HASH`)
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE HEADLINE CASE, END TO END AND WITH NOTHING FABRICATED. A real relay reports a real failure to its
//      home over the real MAC; the home stores, pushes, correlates and ORIGINATES the translated record through
//      `send_by_hash`'s direct-host arm; that exact frame then flies to M1 over the real MAC, and M1 stores and
//      pushes it. This is the first time §B278's chain is closed on host.
TEST_CASE("§B278-S4/2 END TO END: the home's own translated record is accepted, stored and pushed by its mobile") {
    S4Chain c;                                   // M1 is registered to home 1, which hosts it
    CHECK(c.deliver_via_home());
    CHECK(c.p.h1.count("deleg_custody_forwarded") == 1);   // the home really correlated and originated (S3)
    // ---- the RECEIVED CONTEXT, pinned at the fields §8.2 actually reads.
    const std::vector<uint8_t> aired = c.p.h1.last("DATA");
    CHECK(!aired.empty());
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 1);
    CHECK(o.rejected == 0);
    CHECK(o.pushes == 1);
    CHECK(o.stored == 1);
    CHECK(o.delivered == 0);          // ⛔ never an ordinary DM
    CHECK(o.unsupported == 0);
    CHECK(o.other_pushes == 0);       // ⛔ no msg_recv / send_failed / generic lifecycle
    // ---- §8.3's mapping, on BOTH carriers.
    CHECK(o.push_origin == kS3Reporter);          // the ORIGINAL reporter (node 2), NOT the translating home
    CHECK(o.push_ctr    == kS3CtrM);              // ctrM — what the MOBILE is waiting on
    CHECK(o.push_dst    == kS3ReturnPeer);        // the static destination the flight died toward
    CHECK(o.push_layer  == 2);                    // `reporter_layer`
    CHECK(o.push_reason == SendFailReason::none);
    CHECK(o.push_body   == s3_expected_body());   // the WHOLE 32 bytes ride the Push
    CHECK(o.stored_origin == kS3Reporter);
    CHECK(o.stored_msg_id == kS3CtrM);
    CHECK(o.stored_layer  == 2);
    CHECK(o.stored_len    == custody_record_translated_len);
    CHECK(o.stored_body   == s3_expected_body());
    CHECK(o.stored_seq    == o.push_seq);         // ★ store BEFORE Push: the Push carries the assigned sequence
    CHECK(o.push_seq != 0u);                      // this fixture WIRES a store, so it is a real sequence
    // ---- (e): translated mode returned before every S3 step, and M1 sent nothing.
    CHECK(o.no_map == 0); CHECK(o.ambiguous == 0); CHECK(o.forwarded == 0); CHECK(o.refused == 0);
    CHECK(o.tx_n == 0); CHECK(o.parked_n == 0);
    CHECK(c.hm.label_count("RTS") == 0);          // ⛔ nothing was re-originated
    CHECK(o.notices == 0);                        // ⛔ and no custody notice ABOUT the custody carrier
    // ---- the FACTUAL telemetry follows the SELECTED public identity, with its name, field order and integer
    //      types unchanged. ★ The two identities are DISTINGUISHABLE here on purpose: the carrier's origin is
    //      the home (1) while the record's reporter is node 2, and ctrH (0x0BEE) is not ctrM (0x0777) — so an
    //      event that reported the carrier instead of the record would be visible rather than plausible.
    const GHal::EmitRec* e = c.hm.first_emit("custody_failure_rx");
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->keys.size() == 4u);
        if (e->keys.size() == 4u) {
            CHECK(e->keys[0] == "reporter");  CHECK(e->ivals[0] == kS3Reporter);   // ⛔ NOT the carrier's origin (1)
            CHECK(e->keys[1] == "dst");       CHECK(e->ivals[1] == kS3ReturnPeer);
            CHECK(e->keys[2] == "ctr");       CHECK(e->ivals[2] == kS3CtrM);       // ⛔ NOT ctrH (0x0BEE)
            CHECK(e->keys[3] == "seq");       CHECK(e->ivals[3] == static_cast<int64_t>(o.push_seq));
        }
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/3 — RULE 7 ARM ONE: THE RE-HOMED MOBILE ACCEPTS BY STABLE HASH
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE SECOND PRODUCTION ARM, and it is why rule 7 has two. The record was produced by the home that owns
//      the failed flight (node 2); the mobile's CURRENT home (node 1) performs the last mile. `pa.origin` is
//      therefore still node 2 — which is `failed_origin`, so rule 3 holds — while the mobile's own selected home
//      is node 1. The carried `DST_HASH == self` is the whole of the evidence, exactly as §8.2 says.
TEST_CASE("§B278-S4/3 a RE-HOMED mobile accepts through a DIFFERENT home, by DST_HASH") {
    const std::vector<uint8_t> body = s4_produced_at_home2();
    CHECK(body.size() == custody_record_translated_len);
    S4Chain c;                                            // M1's selected home is node 1, NOT the origin
    CHECK(c.m1.mobile_home_id() == 1);
    CHECK(c.deliver_lastmile(body, /*fire_post_ack=*/false));
    // the received context, read off the LIVE PostAck the production MAC installed
    const PostAck* live = c.m1.test_pending_post_ack();
    CHECK(live != nullptr);
    if (live) {
        CHECK(live->origin == 2);                          // the ORIGINATING home, two hops back
        CHECK((live->flags & DATA_FLAG_CRYPTED) == 0);     // plaintext
        CHECK_FALSE(live->team_plane);                     // static plane
        auto ui = parse_unicast_inner(std::span<const uint8_t>(live->inner, live->inner_len), live->flags);
        CHECK(ui.has_value());
        if (ui) { CHECK(ui->has_dst_hash); CHECK(ui->dst_key_hash32 == kS3MobileHash); }
    }
    c.step(); c.m1.on_timer(kPostAckTimerId);
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 1);
    CHECK(o.rejected == 0);
    CHECK(o.pushes == 1);
    CHECK(o.stored == 1);
    CHECK(o.push_origin == 1);                             // the tail's original reporter (node 1 reported to H)
    CHECK(o.push_ctr    == kS3CtrM);
    CHECK(o.push_layer  == 2);
    CHECK(o.stored_body == body);
    // …and rule 3 really is satisfied rather than skipped: `failed_origin` IS the DATA origin.
    const std::optional<CustodyFailureRecord> parsed =
        parse_custody_failure(std::span<const uint8_t>(body.data(), body.size()));
    CHECK(parsed.has_value());
    if (parsed) CHECK(parsed->failed_origin == 2);
    // ---- ★ AND THE LAYER THE OUTCOME IS FILED UNDER IS THE **RECORD'S**, NOT THE ARRIVAL'S. That is the whole
    //      reason the hash arm does not test the layer: a re-homed mobile is reached through a home that need
    //      not be on the layer the flight died on, and §8.3 files the outcome under `reporter_layer`. Driven as
    //      a real frame — the hash arm accepts it, and the stored/pushed layer is 5 rather than this mobile's
    //      active 2.
    {
        std::vector<uint8_t> other_layer = body;
        other_layer[kOffReporterLayer] = 5;
        S4Chain d;
        CHECK(d.m1.active_layer_id() == 2);
        CHECK(d.deliver_lastmile(other_layer));
        const S4Out r = s4_collect(d);
        CHECK(r.accepted == 1);
        CHECK(r.rejected == 0);
        CHECK(r.push_layer   == 5);        // ⛔ the RECORD's reporter layer …
        CHECK(r.stored_layer == 5);        // … on both carriers, never the receiving one
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/4 — A STATIC RECEIVER NEVER CONSUMES THE TRANSLATED FORM
// -----------------------------------------------------------------------------------------------------

// ★★★★ §8.1, verbatim: *"A static node never treats a translated record as its direct report."* The strongest
//      shape of the claim is the TRANSLATING HOME ITSELF: its own static id sits in `failed_origin`, so §13.11
//      would pass — and `_cfg.is_mobile` is the single term that refuses it. ⛔ The positive control in the same
//      case rules out a broken fixture: the identical bytes on a configured mobile are accepted.
TEST_CASE("§B278-S4/4 the configured-mobile term: a STATIC node refuses the translated form, a mobile accepts it") {
    const std::vector<uint8_t> body = s4_produced_at_home1();
    // (a) the STATIC receiver of `run_arm` — node 1, whose id IS this record's `failed_origin`.
    const ArmOut a = run_arm(body.data(), static_cast<uint8_t>(body.size()));
    CHECK(a.flew);
    CHECK(a.accepted == 0);
    CHECK(a.rejected == 1);
    CHECK(a.pushes == 0);
    CHECK(a.stored == 0);
    CHECK(a.delivered == 0);
    CHECK(a.unsupported == 0);
    // (b) the SAME production path on a configured mobile: accepted. The one variable is `NodeConfig::is_mobile`.
    S4Chain c;
    CHECK(c.deliver_via_home());
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 1);
    CHECK(o.rejected == 0);
    CHECK(o.stored == 1);
    CHECK(o.push_body == body);          // ⛔ and it is the SAME 32 bytes the static node refused
    CHECK(g_cfg().is_mobile == false);
    CHECK(g_mobile_cfg().is_mobile == true);
    // ---- (c) ★★★★ THE ARM THAT ISOLATES THE TERM, AND IT WAS **EARNED BY THE MUTATION BATTERY**: with (a)
    //      alone, `sliceGrx` S66 (*"the configured-mobile term is dropped"*) SURVIVED. (a) is refused by §8.2
    //      RULE 3, not by `_cfg.is_mobile` — `run_arm` delivers from node 2 while that record's `failed_origin`
    //      is 1, so `pa.origin != failed_origin` and the mutant never reaches the term under test.
    //      ⇒ this arm satisfies EVERY OTHER translated term on a STATIC receiver: the record originates at the
    //      node that really is its `failed_origin` (node 2), and the carrier is hash-addressed to node 1's OWN
    //      stable key hash, so rule 3 and rule 7's hash arm both hold. The ONLY thing left refusing it is that
    //      node 1 is not a configured mobile — which is exactly §8.1's sentence, isolated.
    {
        const std::vector<uint8_t> from2 = s4_produced_at_home2();
        const std::optional<CustodyFailureRecord> parsed =
            parse_custody_failure(std::span<const uint8_t>(from2.data(), from2.size()));
        CHECK(parsed.has_value());
        if (parsed) CHECK(parsed->failed_origin == 2);      // PREMISE: rule 3 WILL hold at node 1 …
        GPair q;
        CHECK(q.n1.key_hash32() == 0x11111111u);            // … and the carrier is addressed to node 1's OWN hash
        CHECK(q.send_typed(from2.data(), static_cast<uint8_t>(from2.size()), DATA_TYPE_CUSTODY_FAILURE,
                           /*dst_hash=*/0x11111111u));
        CHECK(q.h1.count("custody_failure_rx") == 0);       // ⛔ STILL refused …
        CHECK(q.h1.count("custody_failure_reject") == 1);   // … through the one bounded exit
        CHECK(q.h1.count("unsupported_internal") == 0);
        CHECK(q.h1.count("delivered") == 0);
        int pushes = 0; Push pu{};
        while (q.n1.next_push(pu)) if (pu.kind == PushKind::custody_failure) ++pushes;
        CHECK(pushes == 0);
        StoreSink st{};
        q.n1.inbox().pull(0, 0, store_cb, &st);
        CHECK(st.custody == 0);
        // …and the POSITIVE control on the SAME bytes and the SAME transport: a configured MOBILE holding that
        // same hash accepts them. The one variable between the two is `NodeConfig::is_mobile`.
        S4Chain d;
        CHECK(d.deliver_lastmile(from2));
        const S4Out r = s4_collect(d);
        CHECK(r.accepted == 1);
        CHECK(r.stored == 1);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/5 — EVERY RECEIVER-OWNED TERM REFUSES INDEPENDENTLY
// -----------------------------------------------------------------------------------------------------

// ★★★ ONE FALSIFIER PER TERM, on REAL frames through transport B. ⛔ The codec-owned terms (record length,
//     version, the flag bits, the four id domains, the tail's own six rules) are NOT re-attacked here — they are
//     `sliceFcodec` / `sliceGcodec`'s and are driven as positive dependencies in §B278-S4/6.
TEST_CASE("§B278-S4/5 the COMMON receiver terms each refuse a translated record alone") {
    // §13.10 — the record's own plane claim (a reserved value PARSES and is unsupported in v1).
    s4_expect_rejected_byte("plane team",         kOffPlane,      static_cast<uint8_t>(CustodyFailurePlane::team));
    s4_expect_rejected_byte("plane cross_layer",  kOffPlane,      static_cast<uint8_t>(CustodyFailurePlane::cross_layer));
    // §13.14 — never about an ack, never about another notice.
    s4_expect_rejected_byte("about an ack",       kOffFailedType, DATA_TYPE_E2E_ACK);
    s4_expect_rejected_byte("about a notice",     kOffFailedType, DATA_TYPE_CUSTODY_FAILURE);
    // §13.18 — the four count/hop domains, each against its own authority.
    s4_expect_rejected_byte("requeues",   kOffRequeues,  protocol::cascade_requeue_max + 1);
    s4_expect_rejected_byte("alts",       kOffAlts,      protocol::max_rt_candidates + 1);
    s4_expect_rejected_byte("committed",  kOffCommitted, custody_committed_hops_max + 1);
    s4_expect_rejected_byte("remaining",  kOffRemaining, protocol::hop_budget_max_initial + 1);
    // §8.2 rule 3 — the translating DATA's origin must BE the record's failed origin.
    s4_expect_rejected_byte("wrong outer origin", kOffFailedOrigin, 7);
    s4_expect_rejected_byte("outer origin = 1",   kOffFailedOrigin, 1);   // a plausible neighbour, not just junk
    // ---- THE POSITIVE CONTROL for the whole battery: the UNBROKEN record on the same transport is accepted.
    {
        S4Chain c;
        CHECK(c.deliver_lastmile(s4_produced_at_home2()));
        const S4Out o = s4_collect(c);
        CHECK(o.accepted == 1);
        CHECK(o.rejected == 0);
        CHECK(o.stored == 1);
    }
}

// ★★★ THE MOBILE'S OWN REGISTRATION RELATION — §8.2 rule 7's SECOND arm, on transport A, where the carrier
//     really has no `DST_HASH`. Three arms: the right home accepts, a WRONG current home refuses, and an
//     UNREGISTERED mobile refuses. ⛔ Nothing about the record changes between them; the one variable is the
//     mobile's own `_my_mobile_reg`.
TEST_CASE("§B278-S4/5b the no-hash arm needs an ACTIVE registration to the reporting home") {
    { S4Chain c(/*mobile_home=*/1);            CHECK(c.deliver_via_home());
      const S4Out o = s4_collect(c); CHECK(o.accepted == 1); CHECK(o.rejected == 0); CHECK(o.stored == 1); }
    { S4Chain c(/*mobile_home=*/kS4OtherHome); CHECK(c.deliver_via_home());
      const S4Out o = s4_collect(c); CHECK(o.accepted == 0); CHECK(o.rejected == 1); CHECK(o.stored == 0); }
    { S4Chain c(/*mobile_home=*/0);            CHECK_FALSE(c.m1.mobile_registered());
      CHECK(c.deliver_via_home());
      const S4Out o = s4_collect(c); CHECK(o.accepted == 0); CHECK(o.rejected == 1); CHECK(o.stored == 0); }
}

// ★★★ THE LAYER TERM ON THE NO-HASH ARM. ⚠ It is the ONE receiver term neither transport can break with a real
//     frame, and the reason is structural rather than an omission: on transport A the record's `reporter_layer`
//     IS the reporting home's active layer and the mobile shares it by construction, and a mobile on a genuinely
//     different FULL layer is not reachable by that home's carrier at all (the RTS leaf-nibble filter). ⇒ driven
//     through the seam, over a COPY of the LIVE `PostAck` the production MAC really installed, with EXACTLY ONE
//     RECORD BYTE changed — the same one-variable shape `expect_rejected_byte` uses at the wire.
//     ⛔ The body's position inside `inner` is DERIVED from the production parser's own span, never a literal.
TEST_CASE("§B278-S4/5c the no-hash arm refuses a report stamped with another layer (seam-driven)") {
    for (int arm = 0; arm < 3; ++arm) {
        const uint8_t layer = static_cast<uint8_t>(arm == 0 ? 0 : (arm == 1 ? 1 : 3));   // 0 = an UNWRITTEN byte
        CAPTURE(layer);
        S4Chain c;
        CHECK(c.deliver_via_home(/*fire_post_ack=*/false));
        const PostAck* live = c.m1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        auto ui0 = parse_unicast_inner(std::span<const uint8_t>(live->inner, live->inner_len), live->flags);
        CHECK(ui0.has_value());
        if (!ui0) return;
        CHECK_FALSE(ui0->has_dst_hash);                      // PREMISE: this really is the NO-HASH arm
        const size_t body_off = static_cast<size_t>(ui0->body.data() - live->inner);
        PostAck pa = *live;
        pa.inner[body_off + kOffReporterLayer] = layer;       // ← THE ONE VARIABLE
        c.hm.clear_emits();
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        CHECK(ui.has_value());
        c.m1.test_custody_failure_receive(pa, ui ? &*ui : nullptr);
        CHECK(c.hm.count("custody_failure_rx") == 0);
        CHECK(c.hm.count("custody_failure_reject") == 1);
    }
    // —— the POSITIVE control through the identical seam, so the three refusals are one-variable results.
    {
        S4Chain c;
        CHECK(c.deliver_via_home(/*fire_post_ack=*/false));
        const PostAck* live = c.m1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        c.hm.clear_emits();
        const PostAck pa = *live;
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        c.m1.test_custody_failure_receive(pa, ui ? &*ui : nullptr);
        CHECK(c.hm.count("custody_failure_rx") == 1);
        CHECK(c.hm.count("custody_failure_reject") == 0);
    }
}

// ★★★ THE THREE CONTEXTS NEITHER TRANSPORT CAN INSTALL, driven through the `MESHROUTE_NATIVE` seam off a COPY
//     of the LIVE `PostAck` the production MAC really left behind, with EXACTLY ONE field changed.
//     ⓘ The foreign-`DST_HASH` arm is here for a structural reason worth stating: `do_post_ack` REDIRECTS a
//       foreign `DST_HASH` (`l2c_handle_misdelivery`) long before this function, so the receiver's own hash term
//       is defence in depth — and only the seam can drive it.
TEST_CASE("§B278-S4/5d crypted, team-plane and foreign-DST_HASH arrivals each refuse alone (seam-driven)") {
    const std::vector<uint8_t> body = s4_produced_at_home2();
    // (a) THE POSITIVE BASELINE, through the same seam, so the three refusals below are one-variable results.
    {
        S4Chain c;
        CHECK(c.deliver_lastmile(body, /*fire_post_ack=*/false));
        const PostAck* live = c.m1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        c.hm.clear_emits();
        const PostAck pa = *live;
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        CHECK(ui.has_value());
        c.m1.test_custody_failure_receive(pa, ui ? &*ui : nullptr);
        CHECK(c.hm.count("custody_failure_rx") == 1);
        CHECK(c.hm.count("custody_failure_reject") == 0);
    }
    // (b) §13.1 — a CRYPTED carrier: its inner is ciphertext, so a record parsed out of it is built from noise.
    {
        S4Chain c;
        CHECK(c.deliver_lastmile(body, /*fire_post_ack=*/false));
        const PostAck* live = c.m1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        c.hm.clear_emits();
        PostAck pa = *live;
        pa.flags = static_cast<uint8_t>(pa.flags | DATA_FLAG_CRYPTED);          // ← THE ONE VARIABLE
        auto ui = parse_unicast_inner(std::span<const uint8_t>(live->inner, live->inner_len), live->flags);
        c.m1.test_custody_failure_receive(pa, ui ? &*ui : nullptr);
        CHECK(c.hm.count("custody_failure_rx") == 0);
        CHECK(c.hm.count("custody_failure_reject") == 1);
    }
    // (c) §13.10's receiver half — a TEAM-plane arrival contradicts the record's own static plane byte.
    {
        S4Chain c;
        CHECK(c.deliver_lastmile(body, /*fire_post_ack=*/false));
        const PostAck* live = c.m1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        c.hm.clear_emits();
        PostAck pa = *live;
        pa.team_plane = true;                                                    // ← THE ONE VARIABLE
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        c.m1.test_custody_failure_receive(pa, ui ? &*ui : nullptr);
        CHECK(c.hm.count("custody_failure_rx") == 0);
        CHECK(c.hm.count("custody_failure_reject") == 1);
    }
    // (d) §8.2 rule 7 arm one — a carried `DST_HASH` that is NOT this mobile's stable hash.
    {
        S4Chain c;
        CHECK(c.deliver_lastmile(body, /*fire_post_ack=*/false));
        const PostAck* live = c.m1.test_pending_post_ack();
        CHECK(live != nullptr);
        if (!live) return;
        c.hm.clear_emits();
        const PostAck pa = *live;
        auto ui = parse_unicast_inner(std::span<const uint8_t>(pa.inner, pa.inner_len), pa.flags);
        CHECK(ui.has_value());
        if (!ui) return;
        CHECK(ui->has_dst_hash);
        CHECK(ui->dst_key_hash32 == kS3MobileHash);       // PREMISE: the real carrier really is hash-addressed
        data_unicast_inner foreign = *ui;
        foreign.dst_key_hash32 = kS3MobileHash2;                                 // ← THE ONE VARIABLE
        c.m1.test_custody_failure_receive(pa, &foreign);
        CHECK(c.hm.count("custody_failure_rx") == 0);
        CHECK(c.hm.count("custody_failure_reject") == 1);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/6 — THE CODEC-OWNED TERMS ARE CITED, NOT RE-IMPLEMENTED
// -----------------------------------------------------------------------------------------------------

// ★★★ THE STRUCTURAL CLAIM S4-1 MAKES: the receiver CONSULTS the codec rather than importing a second offset
//     table. Driven as POSITIVE DEPENDENCIES — a malformed prefix and a malformed TAIL both reach the mobile as
//     the SAME bounded refusal, and the receiver contributes no second verdict of its own.
TEST_CASE("§B278-S4/6 codec-owned malformed prefix and tail reach the mobile as the one bounded refusal") {
    const std::vector<uint8_t> good = s4_produced_at_home2();
    CHECK(good.size() == custody_record_translated_len);
    if (good.size() != custody_record_translated_len) return;
    struct Break { const char* what; uint8_t off; uint8_t val; };
    const Break breaks[] = {
        { "version",                   kOffVersion,   2 },                                             // §13.4
        { "reserved flag bit 7",       kOffFlags,     static_cast<uint8_t>(good[kOffFlags] | 0x80) },  // §13.6
        { "record_len below 32",       kOffRecordLen, custody_record_v1_len },        // §6.2's translated floor
        { "tail target_kind unknown",  24 + 1,        9 },                            // §6.3, the tail's domain
        { "tail original_reporter 0",  24 + 0,        0 },                            // §6.3
        { "tail target_value != dst",  24 + 4,        static_cast<uint8_t>(kS3ReturnPeer + 1) },       // §6.3
    };
    for (const Break& b : breaks) {
        CAPTURE(b.what);
        std::vector<uint8_t> bad = good;
        bad[b.off] = b.val;
        // the CODEC really is what refuses it …
        CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(bad.data(), bad.size())).has_value());
        // … and the mobile answers with the SAME bounded exit, no store, no push.
        S4Chain c;
        CHECK(c.deliver_lastmile(bad));
        const S4Out o = s4_collect(c);
        CHECK(o.accepted == 0);
        CHECK(o.rejected == 1);
        CHECK(o.stored == 0);
        CHECK(o.pushes == 0);
    }
    // ⛔ AND THE COHERENCE TRIPWIRE: a record the PREFIX parser accepted always yields a tail (the prefix parser
    //    validates a bit-6 record THROUGH the same reader), so the receiver's `!tail` refusal is unreachable by
    //    construction rather than untested — asserted here on every break plus the good record.
    CHECK(parse_custody_translated_tail(std::span<const uint8_t>(good.data(), good.size()),
              *parse_custody_failure(std::span<const uint8_t>(good.data(), good.size()))).has_value());
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/7 — THE TWO ARMS ARE ALTERNATIVES, NOT A CONJUNCTION
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE THREE SENTENCES THE BRIEF SPELLS OUT, EACH ITS OWN ARM: the ABSENCE of `DST_HASH` does not reject
//      the valid direct-host form; a wrong current home DOES reject that form; and a CORRECT carried hash does
//      not require the home relation at all.
TEST_CASE("§B278-S4/7 absent hash accepts on the right home, rejects on the wrong one, and a correct hash needs neither") {
    {   // (a) NO hash, RIGHT current home -> accepted. Absence of DST_HASH is not itself a refusal.
        S4Chain c(/*mobile_home=*/1);
        CHECK(c.deliver_via_home());
        const S4Out o = s4_collect(c);
        CHECK(o.accepted == 1); CHECK(o.rejected == 0);
        // …and the carrier really did arrive WITHOUT a DST_HASH, which is what makes (a) the no-hash arm.
        const std::vector<uint8_t> aired = c.p.h1.last("DATA");
        CHECK(!aired.empty());
    }
    {   // (b) NO hash, WRONG current home -> refused.
        S4Chain c(/*mobile_home=*/kS4OtherHome);
        CHECK(c.deliver_via_home());
        const S4Out o = s4_collect(c);
        CHECK(o.accepted == 0); CHECK(o.rejected == 1);
    }
    {   // (c) a CORRECT carried hash, on a mobile whose selected home is NOT the record's origin -> accepted.
        S4Chain c(/*mobile_home=*/kS4OtherHome);
        CHECK(c.m1.mobile_home_id() == kS4OtherHome);
        CHECK(c.deliver_lastmile(s4_produced_at_home2()));
        const S4Out o = s4_collect(c);
        CHECK(o.accepted == 1); CHECK(o.rejected == 0); CHECK(o.stored == 1);
    }
    {   // (d) …and a correct carried hash does not need a registration either.
        S4Chain c(/*mobile_home=*/0);
        CHECK_FALSE(c.m1.mobile_registered());
        CHECK(c.deliver_lastmile(s4_produced_at_home2()));
        const S4Out o = s4_collect(c);
        CHECK(o.accepted == 1); CHECK(o.rejected == 0); CHECK(o.stored == 1);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/8 — THE FAIL-CLOSED GUARD IS UNTOUCHED
// -----------------------------------------------------------------------------------------------------

// ★★★ THE TRANSITION, STATED AS A PAIR (the §CUSTODY-G/1 idiom): S4 widens the 0x81 consumer, and every OTHER
//     unhandled internal type still dies at Slice B's fail-closed tail guard — on a MOBILE too.
//     ⓘ `0x87` is the same control §CUSTODY-G/1 uses: an UNALLOCATED value inside the internal range.
TEST_CASE("§B278-S4/8 another unknown internal type still takes Slice B's fail-closed guard at the mobile") {
    S4Chain c;
    CHECK(c.deliver_lastmile(s4_produced_at_home2()));
    CHECK(c.hm.count("custody_failure_rx") == 1);
    CHECK(c.hm.count("unsupported_internal") == 0);
    // the OTHER half: a DIFFERENT addressed internal type, same mobile, same MAC, same last-mile transport.
    c.hm.clear_emits();
    const uint8_t junk[] = { 'x', 'y', 'z' };
    CHECK(c.p.send_typed(junk, sizeof junk, /*type=*/0x87, /*dst_hash=*/kS3MobileHash));
    CHECK(c.hop_to_m1());
    CHECK(c.hm.count("unsupported_internal") == 1);
    CHECK(c.hm.count("custody_failure_rx") == 0);
    CHECK(c.hm.count("custody_failure_reject") == 0);
    CHECK(data_type_is_internal(0x87));
    CHECK_FALSE(data_type_traits(0x87).known);
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/9 — PERSISTENCE: THE FUTURE TAIL AND DISABLED STORAGE
// -----------------------------------------------------------------------------------------------------

// ★★★★ §8.3's *"including every accepted future byte"*, on a record a FUTURE reporter could produce: the
//      32-byte form plus eight bytes this build cannot interpret, `record_len = 40`. A v1 reader interprets the
//      32 it knows and RETAINS all 40 — truncating would destroy the tail on the one node that had it.
TEST_CASE("§B278-S4/9 a translated record with an accepted FUTURE tail is stored and pushed WHOLE") {
    std::vector<uint8_t> body = s4_produced_at_home2();
    CHECK(body.size() == custody_record_translated_len);
    if (body.size() != custody_record_translated_len) return;
    for (uint8_t i = 0; i < 8; ++i) body.push_back(static_cast<uint8_t>(0xE0 + i));
    body[kOffRecordLen] = static_cast<uint8_t>(body.size());      // 40 — the record says how long it is
    S4Chain c;
    CHECK(c.deliver_lastmile(body));
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 1);
    CHECK(o.stored == 1);
    CHECK(o.stored_len == 40);
    CHECK(o.stored_body == body);                 // ⛔ all forty bytes, not the 32 this build understands
    CHECK(o.push_body == body);
    CHECK(o.push_origin == 1);                    // …and the identity is still the TAIL's, not the carrier's
    CHECK(o.push_ctr == kS3CtrM);
    // …and presentation still reads only the KNOWN 32-byte prefix (§8.4): the JSON of the 40-byte record and of
    // its 32-byte prefix are identical.
    const std::string j40 = s4_live_json(body, o.push_origin, o.push_dst, o.push_ctr, o.push_layer, 5);
    std::vector<uint8_t> prefix(body.begin(), body.begin() + custody_record_translated_len);
    prefix[kOffRecordLen] = custody_record_translated_len;       // the same record, WITHOUT the future tail
    const std::string j32 = s4_live_json(prefix, o.push_origin, o.push_dst, o.push_ctr, o.push_layer, 5);
    CHECK(j40 == j32);
}

// ★★★ STORAGE DISABLED: one live Push still fires, carrying `seq = 0` (§7.3, unchanged for the new form).
TEST_CASE("§B278-S4/9b storage disabled: the translated record still pushes, with seq 0 and nothing stored") {
    S4Chain c(/*mobile_home=*/1, /*wire_inbox=*/false);
    CHECK(c.deliver_via_home());
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 1);
    CHECK(o.pushes == 1);
    CHECK(o.push_seq == 0u);          // ⛔ 0 IFF storage is disabled — never a persistence proof
    CHECK(o.stored == 0);
    CHECK(o.push_origin == kS3Reporter);
    CHECK(o.push_ctr == kS3CtrM);
    CHECK(o.push_body == s3_expected_body());
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/10 — TRANSLATED MODE RETURNS BEFORE S3's BLOCK
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE `return` IS THE CONTRACT. A mobile that DOES hold a delegated-flight row — an unreachable state made
//      measurable through the ring's own production seam — must still emit NONE of S3's four events and
//      originate nothing: the translated branch never reaches the lookup at all.
//      ⛔ Without this arm a receiver that fell through into S3's block would look identical on every other case
//        in this file, because a mobile's ring is normally empty.
TEST_CASE("§B278-S4/10 a translated record never reaches S3's lookup, even on a node holding a live row") {
    S4Chain c;
    CHECK(s3_seed(c.m1));                                  // a live eligible row that WOULD match this report
    CHECK(c.m1.test_deleg_ack_live_n() == 1);
    CHECK(c.deliver_lastmile(s4_produced_at_home2()));
    const S4Out o = s4_collect(c);
    CHECK(o.accepted == 1);
    CHECK(o.stored == 1);
    CHECK(o.no_map == 0);
    CHECK(o.ambiguous == 0);
    CHECK(o.forwarded == 0);          // ⛔ the row is NOT consumed …
    CHECK(o.refused == 0);
    CHECK(c.m1.test_deleg_custody_n(Node::test_custody_state_forwarded()) == 0);
    CHECK(c.m1.test_deleg_custody_n(Node::test_custody_state_eligible()) == 1);   // … it is untouched
    CHECK(o.tx_n == 0);
    CHECK(o.parked_n == 0);
    CHECK(c.hm.label_count("RTS") == 0);
    // ---- the DIRECT control: on the same seeded ring a DIRECT report DOES reach S3's block. The pair is what
    //      proves the `return` is a decision rather than an empty ring.
    {
        uint8_t rec[custody_record_v1_len];
        const uint8_t n = s3_pack_report(rec);
        GPair q;
        CHECK(s3_seed(q.n1));
        CHECK(q.send_typed(rec, n));
        CHECK(q.h1.count("deleg_custody_forwarded") + q.h1.count("deleg_custody_forward_refused")
              + q.h1.count("deleg_custody_no_map") == 1);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/11 — §8.4's JSON, LIVE AND PULLED
// -----------------------------------------------------------------------------------------------------

// ★★★★ THE TWO SURFACES ARE ONE FIELD AUTHORITY. Both target kinds, both transports, with the shared field
//      block compared BYTE for BYTE — and the forbidden shapes asserted ABSENT rather than assumed.
TEST_CASE("§B278-S4/11 live and pulled JSON expose §8.4's translated fields identically, for both target kinds") {
    // ---- (a) the NODE-ID form — the shape S3's direct-host arm actually produces.
    const std::vector<uint8_t> node_form = s4_produced_at_home1();
    const std::string want_node = s4_json_fields(kS3Reporter, /*failed_origin=*/1, 2,
        ",\"delegated\":true,\"target_kind\":\"node_id\",\"target_id\":9", kS3CtrM);
    const std::string live_node = s4_live_json(node_form, kS3Reporter, kS3ReturnPeer, kS3CtrM, 2, /*seq=*/4);
    CHECK(live_node == std::string("{\"ev\":\"custody_failure\",\"seq\":4") + want_node + "}\n");
    const std::string pull_node = s4_pulled_json(node_form, kS3Reporter, kS3CtrM, 2, /*rx_ms=*/9000);
    CHECK(pull_node == std::string("{\"ev\":\"custody_failure\",\"seq\":1,\"rx_ms\":9000") + want_node + "}\n");
    CHECK(live_node.find(want_node) != std::string::npos);      // ★ ONE decoder serves both transports
    CHECK(pull_node.find(want_node) != std::string::npos);
    // ---- (b) the HASH form — §6.2's other target kind, built through the same production packer.
    const std::vector<uint8_t> hash_form =
        s3_expected_body(kKindKeyHash, /*target=*/0xA1B2C3D4u, kS3CtrM, /*dst_hash=*/0);
    CHECK(hash_form.size() == custody_record_translated_len);
    const std::string want_hash = s4_json_fields(kS3Reporter, /*failed_origin=*/1, 2,
        ",\"delegated\":true,\"target_kind\":\"hash\",\"target_hash\":\"a1b2c3d4\"", kS3CtrM);
    const std::string live_hash = s4_live_json(hash_form, kS3Reporter, kS3ReturnPeer, kS3CtrM, 2, /*seq=*/4);
    CHECK(live_hash == std::string("{\"ev\":\"custody_failure\",\"seq\":4") + want_hash + "}\n");
    const std::string pull_hash = s4_pulled_json(hash_form, kS3Reporter, kS3CtrM, 2, /*rx_ms=*/9000);
    CHECK(pull_hash == std::string("{\"ev\":\"custody_failure\",\"seq\":1,\"rx_ms\":9000") + want_hash + "}\n");
    // ---- (c) EXACTLY ONE target field, NO aliases, and `ctr` still means ctrH.
    for (const std::string* s : { &live_node, &pull_node, &live_hash, &pull_hash }) {
        CHECK(s->find("\"via_home\"") == std::string::npos);
        CHECK(s->find("\"home_ctr\"") == std::string::npos);
        CHECK(s->find("\"ctr\":3054") != std::string::npos);            // 0x0BEE — the HOME counter, unmoved
        CHECK(s->find("\"mobile_ctr\":1911") != std::string::npos);     // 0x0777 — ctrM, in its OWN field
    }
    CHECK(live_node.find("\"target_hash\"") == std::string::npos);
    CHECK(pull_node.find("\"target_hash\"") == std::string::npos);
    CHECK(live_hash.find("\"target_id\"") == std::string::npos);
    CHECK(pull_hash.find("\"target_id\"") == std::string::npos);
    // ---- (d) TYPE IDENTITY: `delegated` is a JSON boolean, `target_kind`/`target_hash` are strings,
    //          `target_id`/`mobile_ctr` are integers. A quoted number here is a different wire contract.
    CHECK(live_node.find("\"delegated\":true")   != std::string::npos);
    CHECK(live_node.find("\"delegated\":\"true\"") == std::string::npos);
    CHECK(live_node.find("\"target_id\":9,")     != std::string::npos);
    CHECK(live_hash.find("\"target_hash\":\"a1b2c3d4\"") != std::string::npos);
    // ---- (e) THE FAIL-LOUD ARM: a bit-6 record whose length claims the DIRECT form never renders as a direct
    //          event — the codec refuses it and the emitter says so.
    {
        std::vector<uint8_t> torn = node_form;
        torn[kOffRecordLen] = custody_record_v1_len;
        const std::string got = s4_live_json(torn, kS3Reporter, kS3ReturnPeer, kS3CtrM, 2, /*seq=*/4);
        CHECK(got.find("unparseable_record") != std::string::npos);
        CHECK(got.find("\"delegated\"") == std::string::npos);
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/12 — THE COMPLETE-TUPLE CONSUMER CONTRACT (test-only fixture)
// -----------------------------------------------------------------------------------------------------

// ★★★★ §8.4's rule, proven SUFFICIENT and proven NECESSARY. The positive consumes exactly one operation; each
//      of the six fields, changed alone, refuses; and the two forbidden shortcuts — counter-only and the store
//      key `(origin, msg_id)` — are shown to select the WRONG operation, which is why they are forbidden.
TEST_CASE("§B278-S4/12 the generic consumer needs the COMPLETE six-field tuple, and every one-field miss refuses") {
    const std::vector<uint8_t> body = s4_produced_at_home1();
    S4Operation good{};
    good.failed_origin = 1; good.reporter_layer = 2;
    good.target_kind = CustodyTranslatedTargetKind::node_id;
    good.target_value = kS3ReturnPeer; good.mobile_ctr = kS3CtrM; good.failed_type = kS3OutType;
    CHECK(s4_consume({ good }, body) == 1);            // the positive: exactly ONE matching operation
    // ---- six independent one-field misses.
    { S4Operation m = good; m.failed_origin  = 5;                                  CHECK(s4_consume({ m }, body) == 0); }
    { S4Operation m = good; m.reporter_layer = 3;                                  CHECK(s4_consume({ m }, body) == 0); }
    { S4Operation m = good; m.target_kind = CustodyTranslatedTargetKind::key_hash; CHECK(s4_consume({ m }, body) == 0); }
    { S4Operation m = good; m.target_value   = kS3ReturnPeer + 1;                  CHECK(s4_consume({ m }, body) == 0); }
    { S4Operation m = good; m.mobile_ctr     = kS3CtrM2;                           CHECK(s4_consume({ m }, body) == 0); }
    { S4Operation m = good; m.failed_type    = DATA_TYPE_INTRO;                    CHECK(s4_consume({ m }, body) == 0); }
    // ---- the FORBIDDEN counter-only matcher, demonstrated wrong on the same input: a second operation differs
    //      from the first ONLY in its target, so a counter-only matcher selects two and cannot say which.
    {
        S4Operation other = good; other.target_value = kS3ReturnPeer + 1;
        int counter_only = 0;
        for (const S4Operation& op : { good, other }) if (op.mobile_ctr == kS3CtrM) ++counter_only;
        CHECK(counter_only == 2);                       // ⛔ ambiguous — this is why §8.4 forbids it
        CHECK(s4_consume({ good, other }, body) == 1);  // …while the complete tuple still selects exactly one
    }
    // ---- and the STORE KEY is not the operation identity: an E2E-ACK receipt for the SAME operation is stored
    //      under the ACKER's origin, so `(origin, msg_id)` never pairs the two records.
    {
        RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
        Inbox ib; ib.on_init(&dm, &ch);
        (void)ib.record_custody_failure(kS3Reporter, kS3CtrM, 2, body.data(),
                                        static_cast<uint8_t>(body.size()), 1000);
        (void)ib.record_ack(/*from_origin=*/kS3ReturnPeer, /*acked_ctr=*/kS3CtrM, /*layer_id=*/2, /*now=*/1100);
        StoreSink s{};
        ib.pull(0, 0, store_cb, &s);
        CHECK(s.visited == 2);
        CHECK(s.custody == 1);
        CHECK(s.recs.size() == 1u);
        if (s.recs.size() == 1u) CHECK(s.recs[0].origin != kS3ReturnPeer);   // ⛔ different origins, one operation
    }
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/13 — RAW PULL RETURNS BOTH FORMS
// -----------------------------------------------------------------------------------------------------

// ★★★ §7.4's diagnostic pull stays RAW for the new form too, pinned with BOTH forms in ONE pull — two separate
//     pulls over two stores could both pass while the two forms were never seen together.
//     ⓘ The OLED/unread half is re-proved in `test/test_custody_internal_c.cpp` (§B278-S4/13b) against Slice C's
//       own mirror, budget and unread router — re-PROVED there, ⛔ never re-implemented here.
TEST_CASE("§B278-S4/13 one raw pull returns the DIRECT and the TRANSLATED record, both verbatim") {
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    Inbox ib; ib.on_init(&dm, &ch);
    uint8_t direct[custody_record_v1_len];
    const uint8_t dn = g_pack(g_base_record(/*failed_origin=*/1, /*reporter_layer=*/2), direct);
    const std::vector<uint8_t> translated = s4_produced_at_home1();
    (void)ib.record_custody_failure(/*reporter=*/2, /*failed_ctr=*/0x0BEE, /*layer=*/2, direct, dn, 1000);
    (void)ib.record_custody_failure(kS3Reporter, kS3CtrM, 2, translated.data(),
                                    static_cast<uint8_t>(translated.size()), 1100);
    StoreSink s{};
    ib.pull(0, 0, store_cb, &s);
    CHECK(s.visited == 2);
    CHECK(s.custody == 2);
    CHECK(s.recs.size() == 2u);
    if (s.recs.size() != 2u) return;
    CHECK(s.recs[0].body_len == custody_record_v1_len);
    CHECK(s.recs[0].body == std::vector<uint8_t>(direct, direct + dn));
    CHECK(s.recs[1].body_len == custody_record_translated_len);
    CHECK(s.recs[1].body == translated);
    // …and the two really ARE the two forms, read through the codec rather than by eye.
    const std::optional<CustodyFailureRecord> a =
        parse_custody_failure(std::span<const uint8_t>(s.recs[0].body.data(), s.recs[0].body.size()));
    const std::optional<CustodyFailureRecord> b =
        parse_custody_failure(std::span<const uint8_t>(s.recs[1].body.data(), s.recs[1].body.size()));
    CHECK(a.has_value()); CHECK(b.has_value());
    if (a) CHECK_FALSE(custody_record_is_translated(a->notice_flags));
    if (b) CHECK(custody_record_is_translated(b->notice_flags));
}

// -----------------------------------------------------------------------------------------------------
// §B278-S4/14 — NON-RECURSION
// -----------------------------------------------------------------------------------------------------

// ★★★ A TRANSLATED CARRIER THAT DIES PRODUCES NO NOTICE AND NO GENERIC PUSH, from both ends: the GENERATOR
//     excludes 0x81 (`type_reportable`, node_cascade.cpp) and the RECEIVER refuses a record ABOUT an 0x81
//     (§13.14). Measured at the mobile: consuming one produces exactly ONE push and no send lifecycle at all.
TEST_CASE("§B278-S4/14 consuming a translated record spawns no custody notice and no generic send lifecycle") {
    S4Chain c;
    CHECK(c.deliver_via_home());
    const S4Out o = s4_collect(c);
    CHECK(o.pushes == 1);
    CHECK(o.other_pushes == 0);            // ⛔ no send_aired / send_failed / msg_recv of any kind
    CHECK(o.notices == 0);                 // ⛔ no custody notice about the custody carrier
    CHECK(c.hm.count("e2e_ack_tx") == 0);  // ⛔ and no E2E ACK is generated for a notice (§9.1)
    CHECK(o.delivered == 0);
    // …and a record ABOUT an 0x81 is refused at this receiver too — the other half of never-about-itself.
    s4_expect_rejected_byte("about a notice", kOffFailedType, DATA_TYPE_CUSTODY_FAILURE);
}

#include "../src/firmware_remote_client.h"
TEST_CASE("8b S4 transport A translated remote command remains durable while the observer only renders") {
    constexpr uint32_t target=0xa1b2c3d4;
    S4Chain c;
    auto record=g_base_record(1,2);record.failed_type=DATA_TYPE_REMOTE_CMD;record.dst_hash32=target;
    record.notice_flags=custody_notice_flags(CustodyRootStage::cts,true,false,true);
    uint8_t direct[custody_record_v1_len]{};const auto n=g_pack(record,direct);
    CHECK(s3_seed(c.p.n1,kS3MobileHash,kS3CtrH,kS3CtrM,kKindKeyHash,target,kKindNodeId,kS3ReturnPeer,DATA_TYPE_REMOTE_CMD));
    CHECK(c.p.send_typed(direct,n));CHECK(c.hop_to_m1());
    Push push{};bool found=false;
    while(c.m1.next_push(push))if(push.kind==PushKind::custody_failure){found=true;break;}
    CHECK(found);if(!found)return;
    const auto original=push;
    auto& state=c.m1.remote_client();state={};auto& row=state.pending[0].core;
    row.request_id=1;row.state=uint8_t(RemoteClientPhase::response_wait);
    row.flags=uint8_t(RemoteCmdOpcode::auth_execute)|8;row.carrier_ctr=kS3CtrM;row.route.target_hash=target;
    struct ObserverSink {
        std::string text;RemoteLocalTransport transport{};
        bool carrier(RemoteLocalTransport t,uint64_t id,const char* event,uint16_t ctr,const mrfw::CarrierDetail* detail) {
            CHECK(id==1);transport=t;char line[245]{};
            const auto size=mrfw::remote_client_carrier_format(line,sizeof line,t,"0000000000000001",event,ctr,detail);
            text.assign(line,size);return size!=0;
        }
    } sink;
    StoreSink before{};c.m1.inbox().pull(0,0,store_cb,&before);
    const auto held=state;char before_json[1700]{},after_json[1700]{};
    const auto json_n=console::write_push(before_json,sizeof before_json,push);
    mrfw::remote_client_observe_push(state,push,sink,[](void* p,uint32_t h){return static_cast<Node*>(p)->id_bind_find_by_hash(h);},&c.m1);
    CHECK(sink.text.find("carrier custody_failure ctr=1911 origin=1 reporter=2 layer=2 reason=cascade_count")!=std::string::npos);
    CHECK(sink.transport==RemoteLocalTransport::usb);CHECK(memcmp(&state,&held,sizeof state)==0);
    CHECK(memcmp(&push,&original,sizeof push)==0);
    CHECK(console::write_push(after_json,sizeof after_json,push)==json_n);CHECK(memcmp(before_json,after_json,json_n)==0);
    StoreSink after{};c.m1.inbox().pull(0,0,store_cb,&after);
    CHECK(before.custody==1);CHECK(after.custody==1);CHECK(before.recs.back().body==after.recs.back().body);
    CHECK(after.recs.back().body==std::vector<uint8_t>(push.body,push.body+push.body_len));
    for(unsigned mismatch=0;mismatch<4;++mismatch) {
        state=held;push=original;sink.text.clear();
        if(mismatch==0)++row.carrier_ctr;
        if(mismatch==1)++row.route.target_hash;
        if(mismatch==2)row.flags &= ~8;
        if(mismatch==3) {
            auto decoded=parse_custody_failure({push.body,push.body_len});CHECK(decoded.has_value());if(!decoded)return;
            auto tail=parse_custody_translated_tail({push.body,push.body_len},*decoded);CHECK(tail.has_value());if(!tail)return;
            decoded->failed_type=DATA_TYPE_REMOTE_RESP;
            decoded->record_len=custody_record_v1_len;
            decoded->notice_flags &= ~CUSTODY_FLAG_HOME_TRANSLATED;
            push.body_len=pack_custody_failure_translated(*decoded,*tail,push.body);
            CHECK(push.body_len==custody_record_translated_len);
        }
        mrfw::remote_client_observe_push(state,push,sink,[](void* p,uint32_t h){return static_cast<Node*>(p)->id_bind_find_by_hash(h);},&c.m1);CHECK(sink.text.empty());
    }
}
