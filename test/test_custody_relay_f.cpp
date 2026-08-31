// MeshRoute — test_custody_relay_f.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-F — THE v1 CUSTODY CODEC AND RELAY GENERATION (spec
// `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`
// §9 the wire record · §10 eligibility · §11 step 7 the snapshot lifetime · §12 transmission · §18.3/§18.4).
//
// THE SIX CLAIMS THIS FILE MEASURES:
//   (1) THE CODEC — §9.2's 24 offsets, both directions, against a hand-written GOLDEN byte vector; the
//       little-endian 16/32-bit fields; §9.3's exactly-one-stage rule and its `1u << stage` derivation;
//       §9.4/§9.5's numeric values; tail acceptance (`record_len > 24`); and §18.3.6's rejection list.
//   (2) ELIGIBILITY — §10.1's twelve conditions, EACH tested BOTH WAYS off a REAL production transit carrier,
//       including the two plane controls (AUTO-resolving-to-TEAM ineligible · GLOBAL with a colliding team id
//       eligible) and the exact [[B59]] case (`AUTHORITATIVE_H_ANSWER_PUBKEY` is POSITIVELY eligible).
//   (3) THE POSITIVE PATH — an eligible transit terminal originates EXACTLY ONE `0x81`, addressed to the failed
//       ORIGIN, carrying the record the dead carrier justifies.
//   (4) THE SNAPSHOT LIFETIME — §11's order: capture while alive -> reset -> `become_free()` (a queued flight B
//       becomes current) -> enqueue; and the §11 not-inherited list.
//   (5) §12's BEHAVIOUR SET — plane GLOBAL not AUTO, fresh reporter counter, no E2E ack request, no armed user
//       deadline, no generic lifecycle push, DM-floor exempt — plus THE RECURSION GATE, both halves.
//   (6) THE GENERATOR-TO-RECEIVER JOIN — a REAL `0x81`, built by the real generator off a real terminal transit
//       carrier, flown over the real MAC to its real addressee and CONSUMED there. ⚠ RE-AIMED 2026-08-31: this
//       claim used to be *"the intermediate F-before-G state — it drops at Slice B's fail-closed tail guard with
//       exactly one bounded `unsupported_internal`"*, the ruled behaviour until G landed. G landed; see §F/7.
//
// ⛔ PRODUCTION-SHAPED WHEREVER THE PATH ALLOWS. The transit carrier is a REAL forward installed by a real
//    RTS/CTS/DATA/ACK exchange on a 3-node chain (the §CUSTODY-B/E fixture). The negative matrix takes THAT
//    carrier, copies it, and changes EXACTLY ONE field per arm — a one-variable experiment against a real
//    baseline, never a fabricated `PendingTx` ([[B268]]'s lesson, honoured rather than argued around).
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include "node.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "support/test_hal.h"

#include <cstring>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

// Timer ids, mirrored TU-locally exactly as `test_custody_terminal_e.cpp:38` does.
constexpr uint32_t kRtsTimeoutTimerId   = 4;
constexpr uint32_t kCtsToDataGapTimerId = 7;
constexpr uint32_t kQueueWakeupTimerId  = 8;
constexpr uint32_t kPostAckTimerId      = 9;
constexpr uint32_t kRetryBackoffTimerId = 10;

struct FTxFrame { std::string label; std::vector<uint8_t> bytes; };

class FHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    std::vector<FTxFrame>    tx_frames;
    void emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind ? kind : ""); }
    int  count(const char* k) const { int c = 0; for (const auto& e : emits) if (e == k) ++c; return c; }
    void clear_emits() { emits.clear(); }
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        tx_frames.push_back(FTxFrame{ p.label ? p.label : "", std::vector<uint8_t>(b, b + n) });
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

LayerConfig f_layer(uint8_t layer_id, uint8_t sf) {
    LayerConfig L; L.layer_id = layer_id; L.routing_sf = sf;
    L.allowed_sf_bitmap = static_cast<uint16_t>(1u << sf);
    return L;
}
NodeConfig f_cfg() {
    NodeConfig cfg; cfg.n_layers = 1; cfg.layers[0] = f_layer(/*layer_id=*/1, /*sf=*/8);
    cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8);
    // ⓘ A NONZERO leaf id, DELIBERATELY: `on_init` sets `layers[0].layer_id = leaf_id` on a single-layer node,
    //   so leaving the default 0 would make §9.2's `reporter_layer` field indistinguishable from an unwritten
    //   byte. With 2 the assertion is a real discriminator. (All three nodes share this cfg, so the byte-0
    //   leaf gate is unaffected.)
    cfg.leaf_id = 2;
    return cfg;
}
void drain(Node& n) { Push d{}; while (n.next_push(d)) {} }

// ---- the 3-node chain 1 -> 2 -> 3. Node 2 is the RELAY: a DM from 1 to 3 installs a TRANSIT carrier there,
//      which is [[B59]]'s exact topology and the only carrier shape §10.1 admits.
struct FChain {
    FHal h1, h2, h3;
    Node n1{h1, /*id=*/1, 0x11111111u};
    Node n2{h2, /*id=*/2, 0x22222222u};
    Node n3{h3, /*id=*/3, 0x33333333u};
    uint64_t now = 100000;
    FChain() {
        NodeConfig cfg = f_cfg();
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

const RxMeta kRx{10.0f, -75.0f, 0, static_cast<int8_t>(-1)};

// One COMPLETE hop over the real MAC (RTS -> CTS -> DATA -> ACK -> post-ACK), verbatim in shape from
// `test_custody_terminal_e.cpp`'s `e_hop`.
void f_hop(FChain& c, Node& src, FHal& shal, Node& dst, FHal& dhal) {
    const std::vector<uint8_t> rts = shal.last("RTS");
    CHECK_FALSE(rts.empty());
    if (rts.empty()) return;
    c.step(); dst.on_recv(rts.data(), rts.size(), kRx);
    const std::vector<uint8_t> cts = dhal.last("CTS");
    CHECK_FALSE(cts.empty());
    if (cts.empty()) return;
    c.step(); src.on_recv(cts.data(), cts.size(), kRx);
    c.step(); src.on_timer(kCtsToDataGapTimerId);
    const std::vector<uint8_t> data = shal.last("DATA");
    CHECK_FALSE(data.empty());
    if (data.empty()) return;
    c.step(); dst.on_recv(data.data(), data.size(), kRx);
    const std::vector<uint8_t> ack = dhal.last("ACK");
    if (!ack.empty()) { c.step(); src.on_recv(ack.data(), ack.size(), kRx); }
    c.step(); dst.on_timer(kPostAckTimerId);
}

bool run_to_cascade_terminal(Node& n, FHal& hal, int max_rounds = 12) {
    for (int round = 0; round < max_rounds && hal.count("path_cascade_exhausted") == 0; ++round) {
        for (int i = 0; i < 4; ++i) { n.on_timer(kRtsTimeoutTimerId); n.on_timer(kRetryBackoffTimerId); }
        hal._now += 21000;                    // > the largest requeue backoff, < the 60 s age cap
        n.on_timer(kQueueWakeupTimerId);
    }
    return hal.count("path_cascade_exhausted") > 0;
}

// ---- THE SHARED FIXTURE FOR THE ELIGIBILITY MATRIX --------------------------------------------------------
// Install a REAL transit carrier at node 2 (a DM 1 -> 3 relayed through it) and hand back a COPY of the live
// `PendingTx` plus the typed context a selected terminal would carry. ⛔ NON-VACUOUS: the caller CHECKs that a
// carrier actually existed, so a fixture that silently stopped driving is a failure and never a green pass.
struct LiveTransit {
    PendingTx pt{};
    TerminalCustodyContext ctx{ CustodyRootStage::cts, CustodyFailureReason::cascade_count, false };
    bool ok = false;
};
LiveTransit capture_live_transit(FChain& c, uint8_t type = 0) {
    LiveTransit out{};
    const uint8_t body[] = { 'v', 'i', 'a' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, /*dst_hash=*/0, type) != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);          // 1 -> 2: node 2 now holds the transit carrier for dst 3
    const PendingTx* live = c.n2.test_live_pending_tx();
    CHECK(live != nullptr);
    if (!live) return out;
    out.pt = *live;                            // a TEST-side copy of a PRODUCTION-INSTALLED carrier
    out.ok = true;
    return out;
}

// The §9.2 GOLDEN record, chosen so every field is distinct and no byte is 0 by accident.
CustodyFailureRecord golden_record() {
    CustodyFailureRecord r{};
    r.version           = 1;
    r.record_len        = 24;
    r.notice_flags      = static_cast<uint8_t>(CUSTODY_FLAG_FORWARDED | CUSTODY_FLAG_FAILED_AT_ACK
                                               | CUSTODY_FLAG_REPAIR_ATTEMPTED | CUSTODY_FLAG_NEXT_WAS_ONE_WAY
                                               | CUSTODY_FLAG_HAS_DST_HASH);            // 0x3D
    r.terminal_reason   = CustodyFailureReason::cascade_age;                             // 3
    r.failed_origin     = 0x11;
    r.failed_dst        = 0x22;
    r.failed_ctr        = 0x3344;
    r.failed_type       = 0x8B;
    r.failed_data_flags = 0x06;
    r.failed_plane      = CustodyFailurePlane::static_same_layer;                        // 0
    r.reporter_layer    = 0x55;
    r.previous_hop      = 0x66;
    r.failed_next_hop   = 0x77;
    r.requeue_count     = 0x02;
    r.alternatives_tried = 0x03;
    r.committed_hops    = 0x04;
    r.remaining_hops    = 0x1D;
    r.dst_hash32        = 0xA1B2C3D4u;
    r.reserved          = 0;
    return r;
}

// The SAME record as 24 literal bytes, written out by hand from §9.2's offset table. ⛔ It is NOT produced by
// the encoder — that would make the golden a tautology; it is the SPEC transcribed, and the encoder is judged
// against it.
const uint8_t kGoldenBytes[24] = {
    /*0  version        */ 0x01,
    /*1  record_len     */ 0x18,          // 24
    /*2  notice_flags   */ 0x3D,
    /*3  terminal_reason*/ 0x03,          // cascade_age
    /*4  failed_origin  */ 0x11,
    /*5  failed_dst     */ 0x22,
    /*6  failed_ctr LE  */ 0x44, 0x33,    // 0x3344
    /*8  failed_type    */ 0x8B,
    /*9  failed_flags   */ 0x06,
    /*10 failed_plane   */ 0x00,          // static_same_layer
    /*11 reporter_layer */ 0x55,
    /*12 previous_hop   */ 0x66,
    /*13 failed_next_hop*/ 0x77,
    /*14 requeue_count  */ 0x02,
    /*15 alts_tried     */ 0x03,
    /*16 committed_hops */ 0x04,
    /*17 remaining_hops */ 0x1D,
    /*18 dst_hash32 LE  */ 0xD4, 0xC3, 0xB2, 0xA1,
    /*22 reserved       */ 0x00, 0x00,
};

bool same_record(const CustodyFailureRecord& a, const CustodyFailureRecord& b) {
    return a.version == b.version && a.record_len == b.record_len && a.notice_flags == b.notice_flags
        && a.terminal_reason == b.terminal_reason && a.failed_origin == b.failed_origin
        && a.failed_dst == b.failed_dst && a.failed_ctr == b.failed_ctr && a.failed_type == b.failed_type
        && a.failed_data_flags == b.failed_data_flags && a.failed_plane == b.failed_plane
        && a.reporter_layer == b.reporter_layer && a.previous_hop == b.previous_hop
        && a.failed_next_hop == b.failed_next_hop && a.requeue_count == b.requeue_count
        && a.alternatives_tried == b.alternatives_tried && a.committed_hops == b.committed_hops
        && a.remaining_hops == b.remaining_hops && a.dst_hash32 == b.dst_hash32 && a.reserved == b.reserved;
}

// Pull the LAST DATA frame a hal aired that carries `type`, returning its parsed header + body span.
struct TypedData { bool found = false; data_out d{}; std::vector<uint8_t> frame; std::vector<uint8_t> body; };
TypedData last_typed_data(const FHal& hal, uint8_t type) {
    TypedData out{};
    for (auto it = hal.tx_frames.rbegin(); it != hal.tx_frames.rend(); ++it) {
        if (it->label != "DATA") continue;
        const std::optional<data_out> d = parse_data(std::span<const uint8_t>(it->bytes));
        if (!d || d->type != type) continue;
        out.found = true; out.d = *d; out.frame = it->bytes;
        const std::span<const uint8_t> inner = data_inner(std::span<const uint8_t>(out.frame), out.d);
        const std::optional<data_unicast_inner> ui = parse_unicast_inner(inner, out.d.flags);
        if (ui) out.body.assign(ui->body.begin(), ui->body.end());
        return out;
    }
    return out;
}

// Drive node 2's freshly-queued notice all the way onto the air (RTS -> CTS from node 1 -> DATA), so the frame
// itself can be parsed. ⛔ Every step is a real MAC exchange; nothing is hand-assembled.
bool air_the_notice(FChain& c) {
    const std::vector<uint8_t> rts = c.h2.last("RTS");
    if (rts.empty()) return false;
    c.step(); c.n1.on_recv(rts.data(), rts.size(), kRx);
    const std::vector<uint8_t> cts = c.h1.last("CTS");
    if (cts.empty()) return false;
    c.step(); c.n2.on_recv(cts.data(), cts.size(), kRx);
    c.step(); c.n2.on_timer(kCtsToDataGapTimerId);
    return true;
}

}  // namespace

// =====================================================================================================
// §CUSTODY-F/1 — THE CODEC (§9.2/§9.3/§9.5, §18.3)
// =====================================================================================================

// ★★★★ §18.3.1/§18.3.2/§18.3.3: the 24-byte prefix, EVERY offset, and the little-endian 16/32-bit fields —
//      against a golden vector transcribed from the spec table rather than produced by the encoder.
TEST_CASE("§CUSTODY-F/1 pack_custody_failure reproduces §9.2's 24 bytes EXACTLY") {
    uint8_t out[64];
    std::memset(out, 0xEE, sizeof out);
    const size_t n = pack_custody_failure(golden_record(), std::span<uint8_t>(out, sizeof out));
    CHECK(n == 24);
    CHECK(n == custody_record_v1_len);
    for (size_t i = 0; i < 24; ++i) { CAPTURE(i); CHECK(out[i] == kGoldenBytes[i]); }
    CHECK(out[24] == 0xEE);                     // ⛔ it wrote 24 and not one byte more
    // ★ THE ENDIANNESS, ASSERTED AS SUCH and not merely as "byte 6 is 0x44": low byte first, both fields.
    CHECK(out[6] == 0x44); CHECK(out[7] == 0x33);                                     // failed_ctr  0x3344 LE
    CHECK(out[18] == 0xD4); CHECK(out[19] == 0xC3); CHECK(out[20] == 0xB2); CHECK(out[21] == 0xA1);  // hash LE
    // §18.3.4: version, length and zeroed reserved bytes are written by the ENCODER, never left to the caller.
    CHECK(out[0] == custody_record_version_v1);
    CHECK(out[1] == custody_record_v1_len);
    CHECK(out[22] == 0); CHECK(out[23] == 0);
}

TEST_CASE("§CUSTODY-F/1b parse_custody_failure reads §9.2's 24 bytes back into the same record") {
    const std::optional<CustodyFailureRecord> r = parse_custody_failure(std::span<const uint8_t>(kGoldenBytes));
    CHECK(r.has_value());
    if (!r) return;
    CHECK(same_record(*r, golden_record()));
    // ...and the round trip closes: encode(decode(golden)) == golden bytes.
    uint8_t out[24];
    CHECK(pack_custody_failure(*r, std::span<uint8_t>(out, sizeof out)) == 24);
    CHECK(std::memcmp(out, kGoldenBytes, 24) == 0);
    // A plain v1 record has NO tail.
    CHECK(custody_record_tail(std::span<const uint8_t>(kGoldenBytes), *r).empty());
}

// ★★★★ §9.2's TAIL RULE and §18.3.5: `record_len > 24` is VALID — a later version appended a tail — and a v1
//      reader interprets the first 24 bytes while telling a storing consumer how many bytes to retain.
TEST_CASE("§CUSTODY-F/1c a valid UNKNOWN TAIL is accepted, and the v1 semantics are unchanged") {
    std::vector<uint8_t> body(kGoldenBytes, kGoldenBytes + 24);
    body[1] = 28;                                        // record_len = 24 + a 4-byte tail
    body.insert(body.end(), { 0xDE, 0xAD, 0xBE, 0xEF });
    const std::optional<CustodyFailureRecord> r = parse_custody_failure(std::span<const uint8_t>(body));
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->record_len == 28);
    // Every v1 field is read from the first 24 bytes and is identical to the plain record's.
    CustodyFailureRecord g = golden_record(); g.record_len = 28;
    CHECK(same_record(*r, g));
    const std::span<const uint8_t> tail = custody_record_tail(std::span<const uint8_t>(body), *r);
    CHECK(tail.size() == 4);
    if (tail.size() == 4) {
        CHECK(tail[0] == 0xDE); CHECK(tail[1] == 0xAD); CHECK(tail[2] == 0xBE); CHECK(tail[3] == 0xEF);
    }
    // ⛔ AND THE TRANSMITTER STILL REFUSES TO PRODUCE ONE: a v1 encoder appends no tail (§9.2), so a record
    //    claiming 28 cannot be packed. Acceptance and origination are deliberately asymmetric.
    uint8_t out[64];
    CHECK(pack_custody_failure(*r, std::span<uint8_t>(out, sizeof out)) == 0);
    // ⛔ AND A LENGTH BEYOND THE BODY IS REJECTED — the other side of the same field.
    std::vector<uint8_t> lying(kGoldenBytes, kGoldenBytes + 24);
    lying[1] = 25;                                       // claims a tail the body does not have
    CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(lying)).has_value());
}

// ★★★★ §18.3.6, one arm per rejection, EACH from the golden record so exactly one byte differs from a record
//      that is known to parse. A rejection list tested from a fresh buffer proves nothing about the field.
TEST_CASE("§CUSTODY-F/1d the parser rejects every §18.3.6 malformation, one byte at a time") {
    auto rejects = [](int off, uint8_t val) {
        std::vector<uint8_t> b(kGoldenBytes, kGoldenBytes + 24);
        b[static_cast<size_t>(off)] = val;
        return !parse_custody_failure(std::span<const uint8_t>(b)).has_value();
    };
    // The control FIRST: the unmutated golden PARSES, so every rejection below is attributable to its byte.
    CHECK(parse_custody_failure(std::span<const uint8_t>(kGoldenBytes)).has_value());
    // short body (§13.3)
    CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(kGoldenBytes, 23)).has_value());
    CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(kGoldenBytes, 0)).has_value());
    CHECK(rejects(0, 0));                       // version 0        (§13.4)
    CHECK(rejects(0, 2));                       // version 2, unknown
    CHECK(rejects(1, 23));                      // record_len < 24  (§13.5)
    CHECK(rejects(2, 0x7D));                    // flags bit 6 set  (§13.6)
    CHECK(rejects(2, 0xBD));                    // flags bit 7 set
    CHECK(rejects(2, 0x3C));                    // `forwarded` clear (§13.7)
    CHECK(rejects(2, 0x3F));                    // BOTH stage bits  (§13.8)
    CHECK(rejects(2, 0x39));                    // NEITHER stage bit
    CHECK(rejects(3, 0));                       // reason `invalid` (§13.9) — never transmitted
    CHECK(rejects(3, 6));                       // reason above the v1 range
    CHECK(rejects(4, 0));                       // failed_origin 0    (§13.12)
    CHECK(rejects(4, 255));                     // failed_origin 0xFF
    CHECK(rejects(5, 0));                       // failed_dst 0
    CHECK(rejects(5, 255));                     // failed_dst 0xFF
    CHECK(rejects(12, 0));                      // previous_hop 0
    CHECK(rejects(12, 255));                    // previous_hop 0xFF
    CHECK(rejects(13, 0));                      // failed_next_hop 0
    CHECK(rejects(13, 255));                    // failed_next_hop 0xFF
    CHECK(rejects(10, 4));                      // an UNDEFINED plane value (§9.5 defines 0..3 and 255)
    CHECK(rejects(10, 254));
    CHECK(rejects(22, 1));                      // reserved byte nonzero (§13.17)
    CHECK(rejects(23, 1));
    {   // failed_ctr == 0 (§13.13) — two bytes, so it needs its own edit
        std::vector<uint8_t> b(kGoldenBytes, kGoldenBytes + 24);
        b[6] = 0; b[7] = 0;
        CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(b)).has_value());
    }
    {   // §13.16 the hash flag and the hash must agree — BOTH directions
        std::vector<uint8_t> b(kGoldenBytes, kGoldenBytes + 24);
        b[2] = static_cast<uint8_t>(b[2] & ~CUSTODY_FLAG_HAS_DST_HASH);     // flag clear, hash nonzero
        CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(b)).has_value());
        std::vector<uint8_t> b2(kGoldenBytes, kGoldenBytes + 24);
        b2[18] = b2[19] = b2[20] = b2[21] = 0;                              // flag set, hash zero
        CHECK_FALSE(parse_custody_failure(std::span<const uint8_t>(b2)).has_value());
    }
    // ⛔ AND THE RESERVED-BUT-DEFINED PLANES PARSE, because well-formedness and v1 SUPPORT are two questions:
    //    §13.10's "must be static_same_layer" is the RECEIVER's rule and belongs to Slice G.
    for (uint8_t p : { uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(255) }) {
        std::vector<uint8_t> b(kGoldenBytes, kGoldenBytes + 24);
        b[10] = p;
        CAPTURE(p);
        CHECK(parse_custody_failure(std::span<const uint8_t>(b)).has_value());
    }
}

// ★★★★ §9.3's DERIVATION and its exactly-one-stage rule — the half a golden vector cannot see.
TEST_CASE("§CUSTODY-F/1e notice_flags derives the stage bit from CustodyRootStage — and REFUSES the sentinel") {
    // §9.3 bit 1 / bit 2 ARE the enum's values, which is why `1u << stage` is legitimate and a second table is not.
    CHECK(custody_notice_flags(CustodyRootStage::cts, false, false, false)
          == (CUSTODY_FLAG_FORWARDED | CUSTODY_FLAG_FAILED_AT_CTS));
    CHECK(custody_notice_flags(CustodyRootStage::hop_ack, false, false, false)
          == (CUSTODY_FLAG_FORWARDED | CUSTODY_FLAG_FAILED_AT_ACK));
    CHECK((1u << static_cast<uint8_t>(CustodyRootStage::cts))     == CUSTODY_FLAG_FAILED_AT_CTS);
    CHECK((1u << static_cast<uint8_t>(CustodyRootStage::hop_ack)) == CUSTODY_FLAG_FAILED_AT_ACK);
    // the three optional bits, independently
    CHECK((custody_notice_flags(CustodyRootStage::cts, true,  false, false) & CUSTODY_FLAG_REPAIR_ATTEMPTED) != 0);
    CHECK((custody_notice_flags(CustodyRootStage::cts, false, true,  false) & CUSTODY_FLAG_NEXT_WAS_ONE_WAY) != 0);
    CHECK((custody_notice_flags(CustodyRootStage::cts, false, false, true ) & CUSTODY_FLAG_HAS_DST_HASH)     != 0);
    CHECK((custody_notice_flags(CustodyRootStage::cts, false, false, false) & 0xC0) == 0);   // bits 6-7 zero in v1
    // ★★★★ THE SENTINEL. `1u << 0` IS `CUSTODY_FLAG_FORWARDED`, so a naive derivation would turn "no stage" into
    //      a record that sets bit 0 twice and no stage bit at all — §9.3's rule satisfied by a lie, exactly the
    //      launder-the-sentinel class §CUSTODY-E/3d closed one layer down. It returns an IMPOSSIBLE flags byte...
    CHECK(custody_notice_flags(CustodyRootStage::invalid, false, false, false) == 0);
    CHECK(custody_notice_flags(CustodyRootStage::invalid, true, true, true)    == 0);
    // ...and the PACKER refuses it, so the two layers are independent and both are measured.
    CustodyFailureRecord r = golden_record();
    r.notice_flags = custody_notice_flags(CustodyRootStage::invalid, false, false, false);
    uint8_t out[24];
    CHECK(pack_custody_failure(r, std::span<uint8_t>(out, sizeof out)) == 0);
    // the exactly-one-stage predicate itself, over the whole byte
    CHECK(custody_flags_exactly_one_stage(CUSTODY_FLAG_FORWARDED | CUSTODY_FLAG_FAILED_AT_CTS));
    CHECK(custody_flags_exactly_one_stage(CUSTODY_FLAG_FORWARDED | CUSTODY_FLAG_FAILED_AT_ACK));
    CHECK_FALSE(custody_flags_exactly_one_stage(CUSTODY_FLAG_FORWARDED));
    CHECK_FALSE(custody_flags_exactly_one_stage(CUSTODY_FLAG_FORWARDED | custody_flags_stage_mask));
}

// ★★ §18.3.3 — the reason and plane NUMERIC values, pinned on the WIRE side. (Slice E pins the enums; this pins
//    that the codec serializes THOSE values and invents no second mapping — §17-F's ruling.)
TEST_CASE("§CUSTODY-F/1f the wire reason/plane values are §9.4's and §9.5's") {
    CHECK(static_cast<uint8_t>(CustodyFailurePlane::static_same_layer) == 0);
    CHECK(static_cast<uint8_t>(CustodyFailurePlane::team)              == 1);
    CHECK(static_cast<uint8_t>(CustodyFailurePlane::hosted_mobile)     == 2);
    CHECK(static_cast<uint8_t>(CustodyFailurePlane::cross_layer)       == 3);
    CHECK(static_cast<uint8_t>(CustodyFailurePlane::unknown)           == 255);
    CHECK(custody_record_version_v1 == 1);
    CHECK(custody_record_v1_len     == 24);
    // Each §9.4 value round-trips through byte 3 at its own number — the serialization, not the enum.
    struct { CustodyFailureReason r; uint8_t wire; } rows[] = {
        { CustodyFailureReason::one_way_throttled, 1 },
        { CustodyFailureReason::cascade_count,     2 },
        { CustodyFailureReason::cascade_age,       3 },
        { CustodyFailureReason::queue_full,        4 },
        { CustodyFailureReason::load_shed,         5 },
    };
    for (const auto& row : rows) {
        CAPTURE(row.wire);
        CustodyFailureRecord g = golden_record(); g.terminal_reason = row.r;
        uint8_t out[24];
        CHECK(pack_custody_failure(g, std::span<uint8_t>(out, sizeof out)) == 24);
        CHECK(out[3] == row.wire);
        const std::optional<CustodyFailureRecord> back = parse_custody_failure(std::span<const uint8_t>(out));
        CHECK(back.has_value());
        if (back) CHECK(back->terminal_reason == row.r);
    }
    // ⛔ `invalid` (0) is never transmittable, in either direction.
    CHECK_FALSE(custody_reason_is_transmittable(0));
    CHECK_FALSE(custody_reason_is_transmittable(6));
    CustodyFailureRecord bad = golden_record(); bad.terminal_reason = CustodyFailureReason::invalid;
    uint8_t out[24];
    CHECK(pack_custody_failure(bad, std::span<uint8_t>(out, sizeof out)) == 0);
    // ⛔ v1 transmits ONLY static_same_layer (§9.5), even though the parser accepts the reserved values.
    for (CustodyFailurePlane p : { CustodyFailurePlane::team, CustodyFailurePlane::hosted_mobile,
                                   CustodyFailurePlane::cross_layer, CustodyFailurePlane::unknown }) {
        CustodyFailureRecord g = golden_record(); g.failed_plane = p;
        CHECK(pack_custody_failure(g, std::span<uint8_t>(out, sizeof out)) == 0);
    }
}

TEST_CASE("§CUSTODY-F/1g the packer refuses a short buffer and every transmitter invariant") {
    uint8_t small[23];
    CHECK(pack_custody_failure(golden_record(), std::span<uint8_t>(small, sizeof small)) == 0);
    uint8_t out[24];
    auto refuses = [&](CustodyFailureRecord r) { return pack_custody_failure(r, std::span<uint8_t>(out, sizeof out)) == 0; };
    { CustodyFailureRecord r = golden_record(); r.version = 2;            CHECK(refuses(r)); }
    { CustodyFailureRecord r = golden_record(); r.notice_flags &= static_cast<uint8_t>(~CUSTODY_FLAG_FORWARDED); CHECK(refuses(r)); }
    { CustodyFailureRecord r = golden_record(); r.notice_flags |= 0x40;   CHECK(refuses(r)); }
    // ★★ §9.3's EXACTLY-ONE-STAGE RULE, AT THE PACKER, BOTH VIOLATIONS. ⛔ Neither is covered by the
    //    `forwarded` check above, which is why the packer needs its own stage test and why a battery arm that
    //    deleted it SURVIVED until these two lines existed.
    { CustodyFailureRecord r = golden_record();
      r.notice_flags = static_cast<uint8_t>(r.notice_flags | custody_flags_stage_mask); CHECK(refuses(r)); }  // BOTH
    { CustodyFailureRecord r = golden_record();
      r.notice_flags = static_cast<uint8_t>(r.notice_flags & ~custody_flags_stage_mask); CHECK(refuses(r)); }  // NEITHER
    { CustodyFailureRecord r = golden_record(); r.failed_ctr = 0;         CHECK(refuses(r)); }
    { CustodyFailureRecord r = golden_record(); r.failed_origin = 0;      CHECK(refuses(r)); }
    { CustodyFailureRecord r = golden_record(); r.failed_next_hop = 255;  CHECK(refuses(r)); }
    { CustodyFailureRecord r = golden_record(); r.reserved = 1;           CHECK(refuses(r)); }
    // §10.1's "never invent a hash": a set flag over a zero hash, and a carried hash with the flag clear.
    { CustodyFailureRecord r = golden_record(); r.dst_hash32 = 0;         CHECK(refuses(r)); }
    { CustodyFailureRecord r = golden_record();
      r.notice_flags = static_cast<uint8_t>(r.notice_flags & ~CUSTODY_FLAG_HAS_DST_HASH); CHECK(refuses(r)); }
    // ...and the control: the unmutated record still packs, so every refusal above is attributable.
    CHECK(pack_custody_failure(golden_record(), std::span<uint8_t>(out, sizeof out)) == 24);
}

// ⓘ THE SNAPSHOT'S SIZE, MEASURED RATHER THAN CLAIMED (§11: "do not copy the approximately 352-byte PendingTx
//   onto a firmware stack merely to retain 24 diagnostic bytes"). The record is a VALUE whose C++ size exceeds
//   its 24-byte wire length because `dst_hash32` forces 4-byte alignment — stated so a reader is not surprised.
TEST_CASE("§CUSTODY-F/1h the snapshot is BOUNDED and is nowhere near a PendingTx copy") {
    CHECK(sizeof(CustodyFailureRecord) == 28);
    CHECK(sizeof(CustodyNoticeSnapshot) == 32);
    CHECK(sizeof(CustodyNoticeSnapshot) * 10 < sizeof(PendingTx));   // ★ an order of magnitude, not a saving
    CHECK(sizeof(PendingTx) == 352);
}

// =====================================================================================================
// §CUSTODY-F/2 — THE POSITIVE PATH: ONE NOTICE, TO THE FAILED ORIGIN, WITH THE RIGHT RECORD (§18.4.2)
// =====================================================================================================

// ★★★★ [[B59]]'s EXACT TOPOLOGY: node 1 sends to node 3 through node 2; node 2 ACKs custody and then cannot
//      reach 3. This is the case the whole arc exists for, and it is driven end to end through the real MAC.
TEST_CASE("§CUSTODY-F/2 an eligible transit terminal emits EXACTLY ONE 0x81 to the failed origin") {
    FChain c;
    const uint8_t body[] = { 'v', 'i', 'a' };
    // ⓘ PRE-BURN node 2's OWN counter for dst 1 with a completed DM, so "a FRESH reporter counter" is a
    //   MEASURABLE claim below rather than a coincidence: without this, node 1's first ctr to dst 3 and node 2's
    //   first ctr to dst 1 are both 1 and `ctr != ctr` could never fail. (Counters are per-destination.)
    CHECK(c.n2.test_do_send_typed(/*dst=*/1, body, sizeof body, CryptIntent::off, 0, 0) == 1);
    f_hop(c, c.n2, c.h2, c.n1, c.h1);
    drain(c.n1); drain(c.n2);
    const uint16_t ctr = c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr == 1);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    drain(c.n2);
    c.h2.clear_emits();
    CHECK(c.h2.label_count("RTS") > 0);                   // non-vacuous: the relay really did try to reach 3
    CHECK(run_to_cascade_terminal(c.n2, c.h2));

    // ★ THE NOTICE WAS ORIGINATED — and it did NOT preempt anything, because the queue was otherwise empty.
    CHECK(c.h2.count("custody_notice_tx") == 1);          // ⛔ EXACTLY one (§10.3: one per terminal carrier)
    CHECK(c.h2.count("custody_notice_refused") == 0);
    CHECK(c.n2.has_pending_tx());                         // `become_free` installed it as the current flight

    // ★ AND IT IS ON THE AIR, PARSED FROM THE REAL FRAME.
    CHECK(air_the_notice(c));
    const TypedData td = last_typed_data(c.h2, DATA_TYPE_CUSTODY_FAILURE);
    CHECK(td.found);
    if (!td.found) return;
    CHECK(td.d.type == DATA_TYPE_CUSTODY_FAILURE);
    // ★ AND THE NUMBER ITSELF, pinned symbolically so the wire value cannot drift silently. (Written through a
    //   cast rather than as `type == 0x81`, because `tools/check_data_type_literals.py` correctly refuses a bare
    //   numeric DataType comparison anywhere in the tree — including in a test.)
    CHECK(static_cast<uint8_t>(DATA_TYPE_CUSTODY_FAILURE) == 0x81);
    CHECK(td.d.dst == 1);                                 // ★ addressed to the FAILED ORIGIN (§9.1)
    CHECK(td.d.next == 1);
    CHECK_FALSE(td.d.crypted);                            // §9.1 CRYPTED clear
    CHECK_FALSE(td.d.e2e_ack_req);                        // §9.1 E2E_ACK_REQ clear
    CHECK_FALSE(td.d.dst_hash);                           // app_dm=false -> no DST_HASH prefixing
    CHECK_FALSE(td.d.source_hash);
    CHECK(td.d.ctr == 2);                                 // ★ node 2's OWN next counter for dst 1 (1 was burnt)
    CHECK(td.d.ctr != ctr);                               // ★ a FRESH reporter counter, NOT the failed one (1)
    CHECK(td.body.size() == 24);

    // ★★ THE RECORD ITSELF, DECODED THROUGH THE SHARED PARSER (§9.2's "do not re-read byte offsets separately").
    const std::optional<CustodyFailureRecord> r = parse_custody_failure(std::span<const uint8_t>(td.body));
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->version == 1);
    CHECK(r->record_len == 24);
    CHECK(r->failed_origin == 1);                         // the original sender
    CHECK(r->failed_dst    == 3);                         // the original destination
    CHECK(r->failed_ctr    == ctr);                       // ★ the FAILED flight's counter, in the BODY
    CHECK(r->failed_type   == 0);                         // an ordinary untyped DM
    CHECK(r->previous_hop  == 1);                         // the upstream custody source
    CHECK(r->failed_next_hop == 3);                       // the hop it could not reach
    CHECK(r->failed_plane  == CustodyFailurePlane::static_same_layer);
    CHECK(r->reporter_layer == 2);                        // the relay's ACTIVE layer id (f_cfg's leaf_id)
    CHECK(r->reporter_layer == c.n2.active_layer_id());   // ...and it IS that accessor, not a constant
    CHECK(r->terminal_reason != CustodyFailureReason::invalid);
    CHECK((r->notice_flags & CUSTODY_FLAG_FORWARDED) != 0);
    CHECK(custody_flags_exactly_one_stage(r->notice_flags));
    CHECK(r->reserved == 0);
    // ⛔ §18.4.12: NO ORIGINAL PAYLOAD BYTES ARE RETAINED. The dead DM's body was "via"; a 24-byte record whose
    //    every field is an identity or a count cannot contain it, and the frame is searched to prove it.
    CHECK(td.body.size() == custody_record_v1_len);
    bool carries_payload = false;
    for (size_t i = 0; i + 2 < td.frame.size(); ++i)
        if (td.frame[i] == 'v' && td.frame[i+1] == 'i' && td.frame[i+2] == 'a') carries_payload = true;
    CHECK_FALSE(carries_payload);
}

// ★★★ §18.4.4 — the notice may select the FORMER UPSTREAM as its own next hop, because it is a NEW flight with
//     no inherited previous-hop exclusion. Here that is not merely allowed but necessary: node 1 IS the origin.
TEST_CASE("§CUSTODY-F/2b the notice routes back through the former upstream — no inherited hop exclusion") {
    FChain c;
    const uint8_t body[] = { 'u' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    // The dying carrier's previous_hop is 1 — the very node the notice must now address.
    const PendingTx* live = c.n2.test_live_pending_tx();
    CHECK(live != nullptr);
    if (live) { CHECK(live->has_previous_hop); CHECK(live->previous_hop == 1); }
    drain(c.n2); c.h2.clear_emits();
    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(c.h2.count("custody_notice_tx") == 1);
    CHECK(air_the_notice(c));
    const TypedData td = last_typed_data(c.h2, DATA_TYPE_CUSTODY_FAILURE);
    CHECK(td.found);
    if (td.found) CHECK(td.d.next == 1);                  // ★ the former upstream, chosen freely
}

// =====================================================================================================
// §CUSTODY-F/3 — §10.1's TWELVE CONDITIONS, EACH BOTH WAYS, OFF A REAL PRODUCTION CARRIER
// =====================================================================================================

// ★★★★ THE POSITIVE CONTROL FOR THE WHOLE MATRIX. Everything below mutates ONE field of this exact carrier, so
//      a negative arm that goes ineligible is attributable to its own term and nothing else.
TEST_CASE("§CUSTODY-F/3 the live production transit carrier is ELIGIBLE (the matrix's control)") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    const CustodyNoticeSnapshot s = c.n2.test_custody_notice_snapshot(t.pt, t.ctx);
    CHECK(s.eligible);
    CHECK(s.rec.failed_origin == 1);
    CHECK(s.rec.failed_dst == 3);
    CHECK(s.rec.previous_hop == 1);
    CHECK(s.rec.failed_next_hop == 3);
    // ⛔ AND THE CARRIER REALLY IS THE PRODUCTION SHAPE, not a default-constructed struct that happens to pass.
    CHECK(t.pt.has_previous_hop);
    CHECK(t.pt.ctr != 0);
    CHECK(t.pt.inner_len > 0);
}

// ★★★★ THE NEGATIVE MATRIX, §18.4.9 — one arm per §10.1 term, each a SINGLE field off the control above.
TEST_CASE("§CUSTODY-F/3b every §10.1 condition, negated one at a time, refuses the carrier") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    CHECK(c.n2.test_custody_notice_snapshot(t.pt, t.ctx).eligible);      // the control, restated locally

    auto ineligible = [&](PendingTx pt) { return !c.n2.test_custody_notice_snapshot(pt, t.ctx).eligible; };

    { PendingTx p = t.pt; p.has_previous_hop = false;                   CHECK(ineligible(p)); }  // (2) not transit
    { PendingTx p = t.pt; p.m_broadcast = true;                         CHECK(ineligible(p)); }  // (3) channel M
    { PendingTx p = t.pt; p.flood = true;                               CHECK(ineligible(p)); }  // (3) FLOOD
    // (4) NOT PLAINTEXT. ⓘ MEASURED SUBSUMPTION, stated so nobody reads this arm as stronger than it is: under
    //     `DATA_FLAG_CRYPTED` the shared codec deliberately leaves `u.origin = 0` (frame_codec.cpp:1028-1033 —
    //     "a relay must NOT learn who originated a CRYPTED DM"), so §10.1(8) and (9) refuse the carrier even if
    //     (4) were deleted. The term is KEPT because §10.1 lists it and a future codec change could unmask it;
    //     its battery arm is a documented deliberate absence rather than a silent gap.
    { PendingTx p = t.pt; p.flags |= DATA_FLAG_CRYPTED;                 CHECK(ineligible(p)); }
    { PendingTx p = t.pt; p.plane = Plane::TEAM;                        CHECK(ineligible(p)); }  // (5) team plane
    { PendingTx p = t.pt; p.is_gw_relay = true;                         CHECK(ineligible(p)); }  // (6) gw re-inject
    { PendingTx p = t.pt; p.flags |= DATA_FLAG_CROSS_LAYER;             CHECK(ineligible(p)); }  // (6) cross-layer
    { PendingTx p = t.pt; p.addr_len = 1;                               CHECK(ineligible(p)); }  // (6) mobile last mile
    { PendingTx p = t.pt; p.mobile_src = true;                          CHECK(ineligible(p)); }  // (6) mobile delegation
    // (7) THE INNER DOES NOT PARSE. ⓘ The same measured subsumption: `origin_agrees` conjoins `inner_parses`,
    //     and an unparsed inner yields `inner_origin = 0`, which §10.1(9) refuses. Kept for spec fidelity.
    { PendingTx p = t.pt; p.inner_len = 0;                              CHECK(ineligible(p)); }
    { PendingTx p = t.pt; p.origin = static_cast<uint8_t>(p.origin + 1); CHECK(ineligible(p)); } // (8) origin disagrees
    { PendingTx p = t.pt; p.dst = 0;                                    CHECK(ineligible(p)); }  // (9) invalid dst
    { PendingTx p = t.pt; p.dst = 255;                                  CHECK(ineligible(p)); }  // (9) reserved dst
    { PendingTx p = t.pt; p.next = 0;                                   CHECK(ineligible(p)); }  // (9) invalid next hop
    { PendingTx p = t.pt; p.previous_hop = 255;                         CHECK(ineligible(p)); }  // (9) reserved prev hop
    { PendingTx p = t.pt; p.ctr = 0;                                    CHECK(ineligible(p)); }  // (10) zero counter
    { PendingTx p = t.pt; p.type = DATA_TYPE_CUSTODY_FAILURE;           CHECK(ineligible(p)); }  // (11) never about itself
    { PendingTx p = t.pt; p.type = DATA_TYPE_E2E_ACK;                   CHECK(ineligible(p)); }  // (11) never about an ack
    // (12) the terminal branch: `invalid` is `cascade_terminal_cause`'s NOT-TERMINAL answer and must never
    //      produce a notice, whatever the carrier looks like.
    { TerminalCustodyContext bad{ CustodyRootStage::cts, CustodyFailureReason::invalid, false };
      CHECK_FALSE(c.n2.test_custody_notice_snapshot(t.pt, bad).eligible); }
    // ...and the stage sentinel is refused too, through the flags helper's fail-closed answer.
    { TerminalCustodyContext bad{ CustodyRootStage::invalid, CustodyFailureReason::cascade_count, false };
      const CustodyNoticeSnapshot s = c.n2.test_custody_notice_snapshot(t.pt, bad);
      // Eligibility does not test the STAGE, so the snapshot is built — and its flags byte is the impossible 0
      // that `pack_custody_failure` refuses, which is where the sentinel dies. Both halves asserted.
      CHECK(s.eligible);
      CHECK(s.rec.notice_flags == 0);
      uint8_t out[24];
      CHECK(pack_custody_failure(s.rec, std::span<uint8_t>(out, sizeof out)) == 0); }
}

// ★★★ §9.3 bit 3 — `repair_attempted` IS THE TYPED CONTEXT'S ANSWER AND NOTHING ELSE. Slice E establishes the
//     FACT (set at `emit_route_request`'s invocation, never inferred); this pins that the CODEC carries it
//     faithfully in both directions. A hard-coded bit is invisible to every other case in this file.
TEST_CASE("§CUSTODY-F/3ba the repair_attempted bit mirrors the terminal context, both ways") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    for (bool repair : { false, true }) {
        CAPTURE(repair);
        TerminalCustodyContext ctx{ CustodyRootStage::cts, CustodyFailureReason::cascade_count, repair };
        const CustodyNoticeSnapshot s = c.n2.test_custody_notice_snapshot(t.pt, ctx);
        CHECK(s.eligible);
        CHECK(((s.rec.notice_flags & CUSTODY_FLAG_REPAIR_ATTEMPTED) != 0) == repair);
        // ...and it survives the wire round trip, which is the half a snapshot check alone cannot see.
        uint8_t out[24];
        CHECK(pack_custody_failure(s.rec, std::span<uint8_t>(out, sizeof out)) == 24);
        const std::optional<CustodyFailureRecord> back = parse_custody_failure(std::span<const uint8_t>(out));
        CHECK(back.has_value());
        if (back) CHECK(((back->notice_flags & CUSTODY_FLAG_REPAIR_ATTEMPTED) != 0) == repair);
    }
    // ⛔ AND THE STAGE BIT IS THE CONTEXT'S TOO — asserted here beside it so "the flags byte tracks the context"
    //    is one claim rather than two half-claims.
    { TerminalCustodyContext ctx{ CustodyRootStage::hop_ack, CustodyFailureReason::cascade_age, false };
      const CustodyNoticeSnapshot s = c.n2.test_custody_notice_snapshot(t.pt, ctx);
      CHECK((s.rec.notice_flags & CUSTODY_FLAG_FAILED_AT_ACK) != 0);
      CHECK((s.rec.notice_flags & CUSTODY_FLAG_FAILED_AT_CTS) == 0);
      CHECK(s.rec.terminal_reason == CustodyFailureReason::cascade_age); }
}

// ★★★★ §10.1(6b) THE CROSS-LAYER EXCLUSION, ON A CARRIER THAT REALLY IS ONE. ⛔ Flipping the flag on an
//      ordinary inner is NOT a test of this term: `parse_unicast_inner` then fails and §10.1(7) refuses the
//      carrier first, so the arm passes for the wrong reason (measured — the battery arm survived). The inner
//      here is PACKED as a genuine cross-layer inner through the shared codec, so it parses, its origin agrees,
//      and the ONLY condition left to refuse it is the cross-layer exclusion itself.
TEST_CASE("§CUSTODY-F/3bb a GENUINE cross-layer carrier is refused by §10.1(6b) alone") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    PendingTx p = t.pt;
    const uint8_t body[] = { 'x', 'l' };
    const uint8_t layer_ids[2] = { 1, 2 };
    uint8_t inner[64];
    const size_t n = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), DATA_FLAG_CROSS_LAYER,
                                        /*dst_key_hash32=*/0, layer_ids, /*n_layers=*/2, /*cur=*/0,
                                        /*origin=*/t.pt.origin, /*source_hash=*/0, body, sizeof body, 0, 0);
    CHECK(n > 0);
    if (!n) return;
    p.flags = DATA_FLAG_CROSS_LAYER;
    p.inner_len = static_cast<uint8_t>(n);
    for (size_t i = 0; i < n; ++i) p.inner[i] = inner[i];
    // ★ THE CONTROL THAT MAKES THIS ARM ATTRIBUTABLE: with the SAME inner and the flag CLEARED it does not
    //   parse as cross-layer, so the carrier is refused for a different reason — which is exactly the masking
    //   this case exists to escape. With the flag SET the inner parses and the origin agrees...
    { const std::optional<data_unicast_inner> ui =
          parse_unicast_inner(std::span<const uint8_t>(p.inner, p.inner_len), p.flags);
      CHECK(ui.has_value());
      if (ui) { CHECK(ui->has_cross_layer); CHECK(ui->origin == t.pt.origin); } }
    // ...and eligibility STILL refuses it, which can only be §10.1(6b).
    CHECK_FALSE(c.n2.test_custody_notice_snapshot(p, t.ctx).eligible);
}

// ★★★★ §12's C2 FAIL-LOUD ARM: a snapshot that is eligible yet UNPACKABLE is dropped with bounded telemetry and
//      never aired. ⛔ It is unreachable from the production cascade by construction (Slice E's seam guarantees
//      the stage sentinel cannot arrive), which is precisely why it is driven through the seam here — a refusal
//      nothing can reach is a refusal no mutation can redden, and the arm that deletes its emit SURVIVED until
//      this case existed.
TEST_CASE("§CUSTODY-F/3bc an ELIGIBLE but UNPACKABLE snapshot is refused LOUDLY and airs nothing") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    // The one shape that is eligible and cannot pack: a real terminal cause with the stage SENTINEL, whose
    // `custody_notice_flags` answer is the impossible 0 byte.
    TerminalCustodyContext ctx{ CustodyRootStage::invalid, CustodyFailureReason::cascade_count, false };
    const CustodyNoticeSnapshot s = c.n2.test_custody_notice_snapshot(t.pt, ctx);
    CHECK(s.eligible);
    CHECK(s.rec.notice_flags == 0);
    const uint8_t queue_before = c.n2.test_tx_queue_n();
    c.h2.clear_emits();
    c.n2.test_custody_notice_enqueue(s);
    CHECK(c.h2.count("custody_notice_refused") == 1);     // ★ LOUD (C2) — the ONE signal a notice was owed
    CHECK(c.h2.count("custody_notice_tx") == 0);          // ⛔ nothing was even offered to the queue
    CHECK(c.n2.test_tx_queue_n() == queue_before);
    Push p{}; int pushes = 0; while (c.n2.next_push(p)) ++pushes;
    CHECK(pushes == 0);                                   // ⛔ telemetry only — never an app-facing result
    // ...and the control: the SAME snapshot with a real stage packs and IS offered, so the refusal above is
    // attributable to the flags byte and not to the seam being inert.
    TerminalCustodyContext ok{ CustodyRootStage::cts, CustodyFailureReason::cascade_count, false };
    const CustodyNoticeSnapshot s2 = c.n2.test_custody_notice_snapshot(t.pt, ok);
    c.h2.clear_emits();
    c.n2.test_custody_notice_enqueue(s2);
    CHECK(c.h2.count("custody_notice_refused") == 0);
    CHECK(c.h2.count("custody_notice_tx") == 1);
}

// ★★★★ THE PLANE TERM'S TWO CONTROLS (§10.1(5) + §9.1), and they are the reason the term is BOUND to
//      `flight_is_team_plane` rather than re-derived as `plane != TEAM`.
TEST_CASE("§CUSTODY-F/3c the plane authority: AUTO-resolving-to-TEAM is INELIGIBLE, GLOBAL-with-a-colliding-team-id is not") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    // Make the carrier's destination (3) a TEAM PEER through the real learn path, so `is_team_peer(3)` is true
    // and the numeric id 3 exists on BOTH planes — the exact collision §18 carved `GLOBAL` out for. Node 2 keeps
    // its static route to 3 as well, which is what makes this a COLLISION rather than a re-plumbing.
    c.n2.test_learn_route(/*dest=*/3, /*via=*/3, /*hops=*/1, /*snr_q4=*/40, /*team_plane=*/true);
    CHECK(c.n2.is_team_peer(3));

    // (a) AUTO + a team peer RESOLVES to the team plane -> INELIGIBLE. ⛔ A `plane != TEAM` re-derivation would
    //     have called this eligible and reported a TEAM-plane loss as a static one.
    { PendingTx p = t.pt; p.plane = Plane::AUTO;
      CHECK(c.n2.flight_is_team_plane(Plane::AUTO, 3));                          // the authority agrees...
      CHECK_FALSE(c.n2.test_custody_notice_snapshot(p, t.ctx).eligible); }       // ...and so does eligibility
    // (b) GLOBAL forces the STATIC plane even under that collision -> ELIGIBLE. ⛔ A `plane == GLOBAL`-only
    //     reading would be right here and wrong in (a); a `!= TEAM` reading is right here and wrong in (a) too.
    { PendingTx p = t.pt; p.plane = Plane::GLOBAL;
      CHECK_FALSE(c.n2.flight_is_team_plane(Plane::GLOBAL, 3));
      CHECK(c.n2.test_custody_notice_snapshot(p, t.ctx).eligible); }
    // (c) the explicit TEAM plane is of course still refused.
    { PendingTx p = t.pt; p.plane = Plane::TEAM;
      CHECK_FALSE(c.n2.test_custody_notice_snapshot(p, t.ctx).eligible); }
}

// ★★★★ §10.1's closing sentence and §18.4.10 — THE [[B59]] CASE, POSITIVELY. An internal type is eligible;
//      `AUTHORITATIVE_H_ANSWER_PUBKEY` (0x8B) is the exact payload whose transit death started this arc.
TEST_CASE("§CUSTODY-F/3d the B59 case: an AUTHORITATIVE_H_ANSWER_PUBKEY carrier in transit IS eligible") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    PendingTx p = t.pt;
    p.type = DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY;
    const CustodyNoticeSnapshot s = c.n2.test_custody_notice_snapshot(p, t.ctx);
    CHECK(s.eligible);                                    // ★ INTERNAL TYPES ARE OTHERWISE ELIGIBLE
    CHECK(s.rec.failed_type == DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY);   // ...and the type travels in the record
    CHECK(static_cast<uint8_t>(DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY) == 0x8B);
    // ⛔ AND IT IS NOT A BLANKET "internal is fine": the two excluded internal types are still refused, which
    //    is what makes this row an inclusion rather than an absence of a check.
    for (uint8_t excluded : { uint8_t(DATA_TYPE_CUSTODY_FAILURE), uint8_t(DATA_TYPE_E2E_ACK) }) {
        PendingTx q = t.pt; q.type = excluded;
        CAPTURE(excluded);
        CHECK_FALSE(c.n2.test_custody_notice_snapshot(q, t.ctx).eligible);
    }
    // ⓘ ...and the other internal types this relay may carry are eligible too, so §10.1's "internal types are
    //   otherwise eligible" is measured as a rule and not as one lucky value.
    for (uint8_t ok_type : { uint8_t(DATA_TYPE_H_ANSWER), uint8_t(DATA_TYPE_AUTHORITATIVE_H_ANSWER),
                             uint8_t(DATA_TYPE_REMOTE_RESP), uint8_t(DATA_TYPE_TEAM_KEY_GRANT) }) {
        PendingTx q = t.pt; q.type = ok_type;
        CAPTURE(ok_type);
        CHECK(c.n2.test_custody_notice_snapshot(q, t.ctx).eligible);
    }
}

// ★★★ §10.1's "do not invent or reconstruct a hash from a node ID" — the record's hash comes from the FAILED
//     FRAME's inner or it is zero, and the flag agrees in both directions.
TEST_CASE("§CUSTODY-F/3e dst_hash32 is COPIED from the parsed inner, never invented") {
    FChain c;
    // (a) the plain DM the fixture sends carries NO dst hash -> the record carries zero and the flag is clear.
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    const CustodyNoticeSnapshot plain = c.n2.test_custody_notice_snapshot(t.pt, t.ctx);
    CHECK(plain.eligible);
    CHECK(plain.rec.dst_hash32 == 0);
    CHECK((plain.rec.notice_flags & CUSTODY_FLAG_HAS_DST_HASH) == 0);
    // ⛔ AND THE RELAY DOES KNOW A HASH FOR THE DESTINATION — it simply must not use it. This is the whole
    //    defect the rule forbids: a locally-believed binding presented as something the failed frame carried.
    CHECK(c.n2.test_id_bind_set(/*id=*/3, /*key_hash32=*/0xDEADBEEFu, /*authoritative=*/true));
    const CustodyNoticeSnapshot still_plain = c.n2.test_custody_notice_snapshot(t.pt, t.ctx);
    CHECK(still_plain.rec.dst_hash32 == 0);
    CHECK((still_plain.rec.notice_flags & CUSTODY_FLAG_HAS_DST_HASH) == 0);

    // (b) a carrier whose inner REALLY carries a validated hash -> it is copied and the flag is set.
    FChain c2;
    const uint8_t body[] = { 'h' };
    CHECK(c2.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off,
                                   /*dst_hash=*/0x0BADF00Du, /*type=*/0) != 0);
    f_hop(c2, c2.n1, c2.h1, c2.n2, c2.h2);
    const PendingTx* live = c2.n2.test_live_pending_tx();
    CHECK(live != nullptr);
    if (!live) return;
    CHECK((live->flags & DATA_FLAG_DST_HASH) != 0);       // non-vacuous: the frame really carries one
    const CustodyNoticeSnapshot hashed =
        c2.n2.test_custody_notice_snapshot(*live, LiveTransit{}.ctx);
    CHECK(hashed.eligible);
    CHECK(hashed.rec.dst_hash32 == 0x0BADF00Du);          // ★ the FRAME's hash, byte for byte
    CHECK((hashed.rec.notice_flags & CUSTODY_FLAG_HAS_DST_HASH) != 0);
}

// =====================================================================================================
// §CUSTODY-F/4 — §11's ORDER AND THE SNAPSHOT LIFETIME (§18.4.6)
// =====================================================================================================

// ★★★★ THE ORDERING CLAIM, in the form §12 states it: the notice is queued AFTER the failed flight is closed
//      and does NOT preempt the flight `become_free()` just installed. With a queued flight B waiting, B must
//      become the current flight and the notice must go BEHIND it.
TEST_CASE("§CUSTODY-F/4 snapshot -> reset -> become_free installs B -> the notice queues BEHIND it") {
    FChain c;
    const uint8_t body[] = { 'o' };
    const uint16_t ctr_a = c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr_a != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);                     // node 2 holds transit carrier A (origin 1, dst 3)
    CHECK(c.n2.has_pending_tx());
    // Node 2 stages its OWN flight B behind A, FIRST in the queue. When A dies, `become_free()` must install B.
    const uint16_t ctr_b = c.n2.test_do_send_typed(/*dst=*/1, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr_b != 0);
    // ⓘ FOUR FILLERS, AND THE REASON IS MEASURED (the §CUSTODY-E/1c idiom): at queue depth 5 the load-adaptive
    //   budget is already 0, so A's very FIRST cascade exhaustion is terminal. With only B behind it, A would
    //   REQUEUE instead — B would become current, cascade on its own and be BACKING OFF by the time A finally
    //   died, so "was B ready when become_free ran" would stop being a controlled variable.
    for (int i = 0; i < 4; ++i)
        CHECK(c.n2.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    CHECK(c.n2.test_tx_queue_n() == 5);                   // B (index 0) + 4 fillers, behind the live A
    CHECK(Node::cascade_effective_max(5) == 0);           // ...so the first exhaustion IS terminal
    drain(c.n2); c.h2.clear_emits();
    // ⛔ THE VOLLEY STOPS AT THE TERMINAL, deliberately: a further `rts_timeout_fire` would land on whatever
    //    `become_free()` installed and start cascading THAT, which is a different experiment.
    for (int i = 0; i < 4 && c.h2.count("path_cascade_exhausted") == 0; ++i) {
        c.n2.on_timer(kRtsTimeoutTimerId); c.n2.on_timer(kRetryBackoffTimerId);
    }
    CHECK(c.h2.count("path_cascade_exhausted") == 1);

    // ★ B IS THE CURRENT FLIGHT (become_free ran BEFORE the notice was queued) and the notice sits in the queue.
    CHECK(c.n2.has_pending_tx());
    const PendingTx* cur = c.n2.test_live_pending_tx();
    CHECK(cur != nullptr);
    if (cur) {
        CHECK(cur->type == 0);                            // ⛔ B is an ordinary DM — NOT the 0x81 notice
        CHECK(cur->type != DATA_TYPE_CUSTODY_FAILURE);
        CHECK(cur->ctr == ctr_b);
        CHECK(cur->dst == 1);
        CHECK(cur->origin == 2);                          // node 2's own flight, not the relayed one
    }
    CHECK(c.h2.count("custody_notice_tx") == 1);
    CHECK(c.n2.test_tx_queue_n() == 5);                   // 4 fillers + the notice, queued BEHIND B
}

// ★★★★ §11's NOT-INHERITED LIST, field by field where it is observable, and by construction where it is not.
TEST_CASE("§CUSTODY-F/4b the notice inherits NOTHING from the failed carrier") {
    FChain c;
    const uint8_t body[] = { 'i' };
    // Pre-burn node 2's own counter for dst 1 (see §CUSTODY-F/2) so `ctr != ctr_a` is a real discriminator.
    CHECK(c.n2.test_do_send_typed(/*dst=*/1, body, sizeof body, CryptIntent::off, 0, 0) == 1);
    f_hop(c, c.n2, c.h2, c.n1, c.h1);
    drain(c.n1); drain(c.n2);
    const uint16_t ctr_a = c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr_a == 1);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    uint32_t dead_gen = 0;
    { const PendingTx* live = c.n2.test_live_pending_tx();
      CHECK(live != nullptr);
      if (live) dead_gen = live->flight_gen; }
    drain(c.n2); c.h2.clear_emits();
    // ⛔ THE VOLLEY STOPS AT THE TERMINAL (see §CUSTODY-F/4): the notice becomes the current flight here, so one
    //    extra `rts_timeout_fire` would start cascading THE NOTICE and its counters would no longer be the fresh
    //    ones this case is about. Measured: without the guard `requeue_count` reads 1.
    uint8_t dead_requeue = 0, dead_alts = 0;
    for (int round = 0; round < 12 && c.h2.count("path_cascade_exhausted") == 0; ++round) {
        for (int i = 0; i < 4 && c.h2.count("path_cascade_exhausted") == 0; ++i) {
            if (const PendingTx* live = c.n2.test_live_pending_tx()) {
                dead_requeue = live->requeue_count; dead_alts = live->alts_tried_n;   // A's state, read while alive
            }
            c.n2.on_timer(kRtsTimeoutTimerId); c.n2.on_timer(kRetryBackoffTimerId);
        }
        if (c.h2.count("path_cascade_exhausted") == 0) {
            c.h2._now += 21000;
            c.n2.on_timer(kQueueWakeupTimerId);
        }
    }
    CHECK(c.h2.count("path_cascade_exhausted") == 1);
    CHECK(c.h2.count("custody_notice_tx") == 1);
    const PendingTx* notice = c.n2.test_live_pending_tx();
    CHECK(notice != nullptr);
    if (!notice) return;
    CHECK(notice->type == DATA_TYPE_CUSTODY_FAILURE);     // it IS the notice we are inspecting
    CHECK(notice->ctr != ctr_a);                          // ⛔ NOT the failed counter
    CHECK(notice->origin == 2);                           // ★ a NEW OWN-ORIGIN internal DATA (§11)
    CHECK_FALSE(notice->has_previous_hop);                // ⛔ no inherited previous-hop exclusion
    CHECK(notice->previous_hop == 0);
    CHECK(notice->alts_tried_n == 0);                     // ⛔ no inherited alternatives
    CHECK(notice->requeue_count == 0);                    // ⛔ no inherited retry counters
    CHECK(notice->flight_gen != dead_gen);                // ⛔ a new flight generation
    CHECK(notice->plane == Plane::GLOBAL);                // §9.1: explicit, never AUTO
    CHECK((notice->flags & DATA_FLAG_CRYPTED) == 0);      // ⛔ no inherited nonce seed can even apply...
    bool seed_zero = true;
    for (int i = 0; i < 8; ++i) if (notice->nonce_seed[i] != 0) seed_zero = false;
    CHECK(seed_zero);                                     // ...and it is zero, not the dead carrier's
    CHECK(notice->addr_len == 0);
    CHECK_FALSE(notice->mobile_src);
    CHECK_FALSE(notice->is_gw_relay);
    CHECK_FALSE(notice->m_broadcast);
    CHECK_FALSE(notice->flood);
    // ⓘ The dying carrier had really moved: a cascade terminal implies requeues and tried alternatives, so the
    //   zeroes above are a RESET and not a coincidence of two fresh carriers.
    CHECK((dead_requeue > 0 || dead_alts > 0));
}

// ★★★ THE READ-ORDER CLAIM ITSELF: the record's fields could only have been captured while the carrier existed.
//     After the terminal the carrier is GONE, and the notice still carries its `{origin, dst, ctr, prev, next}`.
TEST_CASE("§CUSTODY-F/4c the record's values can only have been read BEFORE the reset") {
    FChain c;
    const uint8_t body[] = { 'p' };
    const uint16_t ctr_a = c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    drain(c.n2); c.h2.clear_emits();
    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(air_the_notice(c));
    const TypedData td = last_typed_data(c.h2, DATA_TYPE_CUSTODY_FAILURE);
    CHECK(td.found);
    if (!td.found) return;
    const std::optional<CustodyFailureRecord> r = parse_custody_failure(std::span<const uint8_t>(td.body));
    CHECK(r.has_value());
    if (!r) return;
    // ★ The current flight is the NOTICE; carrier A no longer exists anywhere in the node — yet these five
    //   values are A's. There is no path by which they could be re-derived after `_pending_tx.reset()`.
    const PendingTx* cur = c.n2.test_live_pending_tx();
    CHECK(cur != nullptr);
    if (cur) CHECK(cur->type == DATA_TYPE_CUSTODY_FAILURE);
    CHECK(r->failed_origin == 1);
    CHECK(r->failed_dst == 3);
    CHECK(r->failed_ctr == ctr_a);
    CHECK(r->previous_hop == 1);
    CHECK(r->failed_next_hop == 3);
}

// =====================================================================================================
// §CUSTODY-F/5 — §12's BEHAVIOUR SET, AND THE RECURSION GATE
// =====================================================================================================

// ★★★★ §12's list of things the notice NEVER has. Slice B's machinery already suppresses the generic family for
//      an internal type — this VERIFIES that it applies to `0x81`, it does not re-implement it.
TEST_CASE("§CUSTODY-F/5 the notice raises NO generic user-send lifecycle and arms no user deadline") {
    FChain c;
    const uint8_t body[] = { 'g' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    drain(c.n2); c.h2.clear_emits();
    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(c.h2.count("custody_notice_tx") == 1);
    // ⛔ NOT ONE app-facing push, of any kind — neither for the transit carrier that died ([[B263]]) nor for the
    //    notice this node just originated (§6.2(5) through the trait).
    Push p{}; int pushes = 0; while (c.n2.next_push(p)) ++pushes;
    CHECK(pushes == 0);
    // ⛔ NOR the generic emits that carry the same names.
    CHECK(c.h2.count("send_failed") == 0);
    CHECK(c.h2.count("send_blocked") == 0);
    // ★ THE TRAIT IS THE AUTHORITY, and it is asserted directly so a future re-wiring cannot quietly re-open it.
    CHECK_FALSE(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).generic_send_lifecycle);
    CHECK(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).internal);          // ...which is also the DM-floor exemption
    CHECK_FALSE(data_type_traits(DATA_TYPE_CUSTODY_FAILURE).application_bearing);

    // Now air it and let the whole exchange complete, so a `send_aired`/`send_acked` would have had its chance.
    CHECK(air_the_notice(c));
    const std::vector<uint8_t> data = c.h2.last("DATA");
    CHECK_FALSE(data.empty());
    c.step(); c.n1.on_recv(data.data(), data.size(), kRx);
    const std::vector<uint8_t> ack = c.h1.last("ACK");
    CHECK_FALSE(ack.empty());
    if (!ack.empty()) { c.step(); c.n2.on_recv(ack.data(), ack.size(), kRx); }
    int after = 0; while (c.n2.next_push(p)) ++after;
    CHECK(after == 0);                                    // ⛔ still nothing, through a COMPLETE hop exchange
    CHECK(c.h2.count("send_acked") == 0);
}

// ★★★★ THE RECURSION GATE (§12's last bullet), BOTH HALVES — and they are two different mechanisms, which is
//      why one arm would not do.
TEST_CASE("§CUSTODY-F/5b the recursion gate: a notice's OWN terminal death generates nothing") {
    FChain c;
    const uint8_t body[] = { 'r' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    drain(c.n2); c.h2.clear_emits();
    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(c.h2.count("custody_notice_tx") == 1);          // notice #1 exists and is the current flight
    const PendingTx* notice = c.n2.test_live_pending_tx();
    CHECK(notice != nullptr);
    if (notice) CHECK(notice->type == DATA_TYPE_CUSTODY_FAILURE);
    // ★ NOW KILL THE NOTICE ITSELF: node 1 answers nothing, so the notice cascades to its own terminal.
    c.h2.clear_emits();
    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(c.h2.count("path_cascade_exhausted") >= 1);     // non-vacuous: the notice REALLY died terminally
    CHECK(c.h2.count("custody_notice_tx") == 0);          // ⛔⛔ AND NOTHING WAS GENERATED — no notice #2
    Push p{}; int pushes = 0; while (c.n2.next_push(p)) ++pushes;
    CHECK(pushes == 0);                                   // ⛔ telemetry-only: no generic user-send result either
    CHECK_FALSE(c.n2.has_pending_tx());
    CHECK(c.n2.test_tx_queue_n() == 0);
}

TEST_CASE("§CUSTODY-F/5c the recursion gate, half two: a RELAYED 0x81 dying in transit generates nothing") {
    FChain c;
    const LiveTransit t = capture_live_transit(c);
    CHECK(t.ok);
    if (!t.ok) return;
    // The SAME production transit carrier, carrying someone else's custody notice. Term (11) refuses it, and
    // that is the arm term (2) cannot cover: this carrier IS transit, so only the type rule can stop it.
    PendingTx p = t.pt;
    p.type = DATA_TYPE_CUSTODY_FAILURE;
    CHECK(p.has_previous_hop);                            // ⛔ non-vacuous: it is genuinely a transit carrier
    CHECK_FALSE(c.n2.test_custody_notice_snapshot(p, t.ctx).eligible);
    // ...and the E2E ACK exclusion is the sibling rule, equally load-bearing.
    p.type = DATA_TYPE_E2E_ACK;
    CHECK_FALSE(c.n2.test_custody_notice_snapshot(p, t.ctx).eligible);
}

// ★★★ §12's best-effort tail: a FULL TX queue loses the notice with bounded telemetry and no recursion.
TEST_CASE("§CUSTODY-F/5d a full TX queue drops the notice with bounded telemetry only") {
    FChain c;
    const uint8_t body[] = { 'f' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    // Fill node 2's TX queue behind the live transit carrier, so the terminal's own requeue has no slot AND the
    // notice cannot be admitted either. (The queue-full arm is also §9.4's `queue_full` cause — one fixture,
    // two facts, both asserted.)
    // ⛔⛔ THE NAV JITTER IS LOAD-BEARING AND IT IS THE MEASURED REASON THIS ARM IS REACHABLE AT ALL: without it
    //    every queued item is ready at once, so `become_free()` inside `giveup_flight` INSTALLS one, frees a
    //    slot, and the notice always fits — the refusal could never fire from a full queue alone. Forcing the
    //    real `nav_enabled` origination jitter (`_rand_ret`, the pre-existing herd-spread idiom) parks all eight
    //    behind a backoff, so `become_free` legitimately installs nothing and the queue is STILL full at step 7.
    //    That is a genuine production state (a congested relay whose queue is entirely in backoff), not a poke.
    c.h2._rand_ret = 400;                                 // every own-DM origination backs off ~400 ms
    for (int i = 0; i < 8; ++i)
        CHECK(c.n2.test_do_send_typed(/*dst=*/1, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    CHECK(c.n2.test_tx_queue_n() == 8);
    drain(c.n2); c.h2.clear_emits();
    for (int i = 0; i < 4; ++i) { c.n2.on_timer(kRtsTimeoutTimerId); c.n2.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h2.count("path_cascade_exhausted") >= 1);
    // ⛔ NO custody DATA WAS ADMITTED, and the loss is REPORTED as bounded scalar telemetry — never a Push.
    CHECK(c.h2.count("custody_notice_refused") == 1);
    // ⓘ `custody_notice_tx` STILL FIRES ONCE, and that is `enqueue_data`'s pre-existing contract rather than a
    //   defect: the `tx_event` emit is UNCONDITIONAL (node_mac.cpp), exactly as `e2e_ack_tx` and every other
    //   internal origination behaves. ★ IT IS AN ATTEMPT MARKER, NOT AN ADMISSION — the same [[B251]]/§UI-16-N6b
    //   distinction the minted counter carries. The ADMISSION is the `SendDispatch`, and its refusal is what
    //   `custody_notice_refused` reports. Asserted here so no reader takes the emit for a delivery.
    CHECK(c.h2.count("custody_notice_tx") == 1);
    CHECK(c.n2.test_tx_queue_n() == 8);                   // ⛔ the queue is UNCHANGED: nothing was stored
    for (const auto& f : c.h2.tx_frames) {                // ⛔ and no 0x81 ever reached the air
        if (f.label != "DATA") continue;
        const std::optional<data_out> d = parse_data(std::span<const uint8_t>(f.bytes));
        if (d) CHECK(d->type != DATA_TYPE_CUSTODY_FAILURE);
    }
    Push p{}; int pushes = 0; while (c.n2.next_push(p)) ++pushes;
    CHECK(pushes == 0);
}

// =====================================================================================================
// §CUSTODY-F/6 — LOCAL ORIGINATIONS AND EXCLUDED DEATHS ARE UNCHANGED (§18.4.8, §10.2)
// =====================================================================================================

// ★★★ §18.4.8: a LOCAL application send's terminal give-up keeps its generic result and emits NO report. This
//     is §CUSTODY-E/1b's case with Slice F's extra claim bolted on, and it is the over-correction guard.
TEST_CASE("§CUSTODY-F/6 a LOCAL send's terminal keeps send_failed and generates NO notice") {
    FChain c;
    const uint8_t body[] = { 'l' };
    const uint16_t ctr = c.n1.test_do_send_typed(/*dst=*/2, body, sizeof body, CryptIntent::off, 0, 0);
    CHECK(ctr != 0);
    drain(c.n1); c.h1.clear_emits();
    CHECK(run_to_cascade_terminal(c.n1, c.h1));
    Push p{}; int failed = 0; Push first{};
    while (c.n1.next_push(p)) if (p.kind == PushKind::send_failed) { if (failed == 0) first = p; ++failed; }
    CHECK(failed == 1);                                   // ★ the app STILL learns its own send died
    CHECK(first.reason == SendFailReason::no_cts);
    CHECK(first.dst == 2);
    CHECK(first.ctr == ctr);
    CHECK(c.h1.count("custody_notice_tx") == 0);          // ⛔ and no custody traffic at all
    CHECK(c.h1.count("custody_notice_refused") == 0);
    for (const auto& f : c.h1.tx_frames) {
        if (f.label != "DATA") continue;
        const std::optional<data_out> d = parse_data(std::span<const uint8_t>(f.bytes));
        if (d) CHECK(d->type != DATA_TYPE_CUSTODY_FAILURE);
    }
}

// ★★★ §10.2 — THE DEFERRED CUSTODY-LOSS SITES ARE UNTOUCHED. The `no_route` defer/drain give-up is one of them,
//     and it is the one this fixture can drive: it is a post-custody loss that generates NOTHING in v1.
TEST_CASE("§CUSTODY-F/6b a §10.2 DEFERRED loss site (no-route defer TTL) still generates nothing") {
    FChain c;
    const uint8_t body[] = { 'd' };
    // Node 2 has no route to 9 at all: the send is DEFERRED, and its TTL give-up is a §10.2 site.
    CHECK(c.n2.test_do_send_typed(/*dst=*/9, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    drain(c.n2); c.h2.clear_emits();
    c.h2._now += protocol::send_defer_ttl_ms + 1000;
    for (int i = 0; i < 4; ++i) { c.n2.on_timer(kQueueWakeupTimerId); c.n2.on_timer(kRetryBackoffTimerId); }
    CHECK(c.h2.count("custody_notice_tx") == 0);
    CHECK(c.h2.count("custody_notice_refused") == 0);
}

// =====================================================================================================
// §CUSTODY-F/7 — THE INTERMEDIATE F-BEFORE-G STATE, RATIFIED AND MEASURED
// =====================================================================================================

// ⚠⚠ WITHDRAWN AND RE-ANCHORED 2026-08-31 BY §CUSTODY-G — **BY THE SLICE IT NAMED, WHICH IS THE POINT OF
//    HAVING PINNED IT.** THE CASE READ: *"§CUSTODY-F/7 a REAL 0x81 arriving at its addressee drops at the
//    Slice-B tail guard, exactly once"*, and asserted `unsupported_internal == 1` with `pushes == 0`. Its own
//    banner said: *"F makes 0x81 an EMITTED type while Slice G's receiver does not exist … this case is what
//    will have to be re-anchored when Slice G lands"*. Slice G landed `Node::custody_failure_receive`, so an
//    addressed 0x81 is now CONSUMED before ordinary DM delivery (§13) and never reaches the tail guard.
// ⛔ THE CASE IS RE-AIMED, NOT DELETED, AND IT KEEPS THE PROPERTY THAT MADE IT VALUABLE: it is the ONLY arm in
//    the tree where the notice is produced by the REAL generator off a REAL terminal transit carrier and then
//    flown to its REAL addressee over the REAL MAC. Everything downstream of it — the eighteen validations, the
//    five-step order, the JSON — is measured in `test/test_custody_receive_g.cpp` against synthesized records;
//    THIS is the case that proves the two halves of the arc actually meet.
// ⓘ The inbox is UNWIRED in this fixture (no `Inbox::on_init`), so the push carries `seq == 0` — §7.3's
//   storage-disabled receipt, which is also exactly the simulator's situation (B134). The STORED half is
//   proven in the G file, where a `RamInboxStore` is wired.
TEST_CASE("§CUSTODY-F/7 a REAL 0x81 arriving at its addressee is CONSUMED by the §CUSTODY-G receiver, not guard-dropped") {
    FChain c;
    const uint8_t body[] = { 'w' };
    CHECK(c.n1.test_do_send_typed(/*dst=*/3, body, sizeof body, CryptIntent::off, 0, 0) != 0);
    f_hop(c, c.n1, c.h1, c.n2, c.h2);
    drain(c.n2); drain(c.n1);
    c.h2.clear_emits();
    CHECK(run_to_cascade_terminal(c.n2, c.h2));
    CHECK(c.h2.count("custody_notice_tx") == 1);
    c.h1.clear_emits();
    CHECK(air_the_notice(c));
    const std::vector<uint8_t> data = c.h2.last("DATA");
    CHECK_FALSE(data.empty());
    if (data.empty()) return;
    { const std::optional<data_out> d = parse_data(std::span<const uint8_t>(data));
      CHECK(d.has_value());
      if (d) CHECK(d->type == DATA_TYPE_CUSTODY_FAILURE); }   // non-vacuous: it really is an 0x81
    c.step(); c.n1.on_recv(data.data(), data.size(), kRx);
    c.step(); c.n1.on_timer(kPostAckTimerId);             // the deferred delivery pass — where the tail guard runs

    // ★ THE HOP COMPLETED — the notice was ACKed like any DATA (§12: ordinary hop ACKs) ...
    CHECK(c.h1.label_count("ACK") >= 1);
    // ... and the SEMANTIC verdict is now CONSUMPTION by the §CUSTODY-G receiver.
    CHECK(c.h1.count("unsupported_internal") == 0);       // ⛔ the tail guard is no longer where an 0x81 dies
    CHECK(c.h1.count("custody_failure_reject") == 0);     // ⛔ and the REAL generator's record passes all eighteen
    CHECK(c.h1.count("custody_failure_rx") == 1);         // ★ exactly one validated receipt
    CHECK(c.h1.count("delivered") == 0);                  // ⛔ still never delivered as a message ...
    CHECK(c.h1.count("msg_recv") == 0);                   // ⛔ ... and still not an ordinary DM
    // ★★ ONE push, and it is `custody_failure` carrying §14.1's mapping off the record the RELAY built.
    Push p{}; int pushes = 0; Push cust{}; int n_cust = 0;
    while (c.n1.next_push(p)) { ++pushes; if (p.kind == PushKind::custody_failure) { cust = p; ++n_cust; } }
    CHECK(pushes == 1);
    CHECK(n_cust == 1);
    CHECK(cust.origin == 2);                              // the OUTER reporting relay = node 2
    CHECK(cust.dst    == 3);                              // failed_dst = the DM's original destination
    CHECK(cust.seq    == 0u);                             // §7.3: storage disabled in this fixture -> 0
    CHECK(cust.reason == SendFailReason::none);           // ⛔ §14.1: the custody reason is NEVER in Push::reason
    CHECK(cust.body_len == custody_record_v1_len);
    // ⛔ THE PUSH CARRIES THE **RELAY'S OWN** RECORD, decoded through the ONE codec — not a re-derivation.
    const std::optional<CustodyFailureRecord> got =
        parse_custody_failure(std::span<const uint8_t>(cust.body, cust.body_len));
    CHECK(got.has_value());
    if (got) {
        CHECK(got->failed_origin == 1);                   // §13.11: addressed to US, which is why it was consumed
        CHECK(got->failed_dst    == 3);
        CHECK(got->failed_ctr    == cust.ctr);
        CHECK(got->reporter_layer == 2);                  // f_cfg()'s deliberately-nonzero leaf id
        CHECK(custody_reason_is_transmittable(static_cast<uint8_t>(got->terminal_reason)));
        CHECK(custody_flags_exactly_one_stage(got->notice_flags));
    }
    // ⓘ THE GUARD ASKS THE RANGE, NOT THE ALLOCATION — which is why allocating 0x81 did not change the guard,
    //   and why Slice G's consumer sits IN FRONT of it rather than replacing it. The paired case that proves
    //   the guard still eats OTHER internal types lives in `test/test_custody_receive_g.cpp`.
    CHECK(data_type_is_internal(DATA_TYPE_CUSTODY_FAILURE));
}
