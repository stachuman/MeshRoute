// MeshRoute — test_radmin_characterization_0e.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ REMOTE-ADMIN v2 SLICE 0e — THE NATIVE HALF OF THE CHARACTERIZATION (0e-B compile, 0e-C caps, 0e-D budget).
//
// Authority: `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md` §8, §12, §13, §15 and the
// owner rulings R-RA-2 (candidate ABI), R-RA-3 (packer-derived carrier caps) and R-RA-20 (the first-hop budget).
//
// ⛔⛔ THIS SLICE LANDS NO PRODUCTION CODE, AND THIS FILE IS THE PROOF THAT IT DOES NOT NEED TO. Every number below
//     is asked of an authority that ALREADY EXISTS — `data_inner_cap()` / `data_frame_len()` / `pack_unicast_inner()`
//     / `pack_data()` / `airtime_ms()` / `protocol_constants.h` — and NOTHING here is a second wire authority a
//     later slice could accidentally consume. Slice 2 owns the production `remote_body_cap(RemoteCarrier)`; Slice 7a
//     owns `remote_scheduled_reply_path_budget_ms(cfg)`. What 0e produces is their KAT INPUT, not their replacement.
//
// ⛔ NO CAPACITY, TIMER OR CONFIGURATION IS DECIDED HERE. The candidate records in
//    `test/radmin_0e_candidate_types.h` are measured, not adopted.
//
// ---- WHY THE CAPS ARE PACKED AND NOT COMPUTED -------------------------------------------------------------------
// R-RA-3, verbatim: "one `remote_body_cap(carrier)` function in the codec, computed from the packers, with a test
// that packs at the cap and refuses at cap plus one for every carrier. The 214/213 figures in the draft stay as
// examples only." So every row below is proved by RUNNING `pack_unicast_inner` and `pack_data` at the cap and at cap
// plus one. ⛔ A cap that only ever appeared in arithmetic would be the copied-literal defect the ruling names.
//
// ---- THE TWO BOUNDS, AND WHY BOTH ARE ASKED ---------------------------------------------------------------------
// `data_inner_cap(flags, type, 255)` is the AIR-FIT bound (255 − 8 header − 1 TYPE − `data_mac_len(flags)`), and
// `protocol::max_payload_bytes_hard_cap` (241) is the `TxItem.inner[]` STORAGE bound. §B20's banner in
// `frame_codec.h:730-737` is explicit that these are DIFFERENT QUESTIONS and the stricter one governs:
//     plaintext typed -> air 242, storage 241  =>  STORAGE governs
//     CRYPTED   typed -> air 238, storage 241  =>  AIR     governs
// The cap+1 probe below reports WHICH one refused, so the two can never be conflated again.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "airtime.h"
#include "frame_codec.h"
#include "protocol_constants.h"
#include "timer_wheel.h"                 // the REAL wheel, so 0e-B's mirror can be proved faithful rather than assumed
#include "radmin_0e_candidate_types.h"

using namespace MESHROUTE_NS;
namespace P = MESHROUTE_NS::protocol;

namespace {

// ---------------------------------------------------------------------------------------------------------------
// The RPC DATA type byte.
// ⛔ The VALUE is Slice 2's allocation, not 0e's, and nothing here depends on it: `pack_data` derives APP from
//    `type != 0`, so every non-zero type costs exactly the same ONE byte. A placeholder in the reserved-for-
//    protocol-internal range keeps the arithmetic honest without pretending a codepoint has been allocated.
// ⚠ CORRECTED 2026-09-06 BY REMOTE-ADMIN v2 SLICE 2, AND THE OLD CLAIM IS KEPT VISIBLE ABOVE (V1). The sentence
//    "without pretending a codepoint has been allocated" was already wrong when it was written: `0xA0` is
//    `DATA_TYPE_REMOTE_CMD` and `0xA1` is `DATA_TYPE_REMOTE_RESP` in `frame_codec.h:802-803`, both ALLOCATED and
//    live on the legacy remote path since 2026-06-24. This constant is therefore not a placeholder for an
//    unallocated number — it is the REAL RPC type byte, and Slice 2 reuses it rather than allocating one
//    (no `wire_version` change, design §8.1). ⛔ The NAME and every measurement below are deliberately left
//    UNCHANGED: this is a comment-only correction and 0e's numbers are its own.
// ---------------------------------------------------------------------------------------------------------------
constexpr uint8_t kPlaceholderRemoteType = 0xA0;

// §8.11 fixed RPC envelope overheads. Quoted from the design's own table, not re-derived by guesswork.
constexpr uint16_t kAuthRequestEnvelope  = 25;   // ctl 1 + request_id 8 + tag 16
constexpr uint16_t kAuthResponseEnvelope = 26;   // ctl 1 + request_id 8 + response_seq 1 + tag 16
constexpr uint16_t kOpenRequestEnvelope  = 9;    // ctl 1 + request_id 8
constexpr uint16_t kOpenResponseEnvelope = 10;   // ctl 1 + request_id 8 + response_seq 1

// §8.9: the terminal's authenticated plaintext is one compact result code, and `scheduled` is the one result that
// must also carry its bounded activation delay. The delay's ceiling is `e2e_ack_deadline_xl_ms - 1` = 299 999 ms,
// which needs 4 bytes. ⇒ the largest TERMINAL{scheduled} payload is 5 bytes.
constexpr uint16_t kTerminalScheduledPayload = 1 + 4;

// What refused, when something did. ⛔ `structural` is NOT a length verdict and must never be reported as one:
// it is `pack_data` rejecting the frame SHAPE (see the CRYPTED ⇒ DST_HASH rule at `frame_codec.cpp:900`).
enum class Bound { none, storage, air, structural };

struct Carrier {
    const char* name;
    const char* direction;
    uint8_t     flags;        // the DATA byte-1 flags that decide the inner layout
    uint8_t     n_layers;     // CROSS_LAYER path depth (0 when same-layer)
    bool        wrapper_type_byte;   // the registered-mobile wrapper prefixes its BODY with the enclosed TYPE
};

// ★ THE CARRIER VOCABULARY (design §8.11 + §14 + the pre-check's §3 table). Every request and response carrier the
//   design names, plus EVERY legal cross-layer depth: `pack_unicast_inner` accepts 1..`gw_env_max_hops` and refuses
//   0 or more, so 1..4 is the whole legal domain — not a sample of it.
const Carrier kCarriers[] = {
    // registered mobile -> its OWN home. `node_hashlocate.cpp:1785-1791`: DST_HASH = the target key hash,
    // SOURCE_HASH, and a wrapper body whose first byte is the enclosed TYPE (DATA_FLAG_MS_ENCLOSED_TYPE).
    {"mobile -> own home wrapper", "request",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | DATA_FLAG_MS_ENCLOSED_TYPE, 0, true},
    {"home -> target by node id", "request", DATA_FLAG_SOURCE_HASH, 0, false},
    {"home -> target by key hash", "request", DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH, 0, false},
    {"cross-layer by node id, depth 1", "request",
     DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 1, false},
    {"cross-layer by node id, depth 2", "request",
     DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 2, false},
    {"cross-layer by node id, depth 3", "request",
     DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 3, false},
    {"cross-layer by node id, depth 4", "request",
     DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 4, false},
    {"cross-layer by key hash, depth 1", "request",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 1, false},
    {"cross-layer by key hash, depth 2", "request",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 2, false},
    {"cross-layer by key hash, depth 3", "request",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 3, false},
    {"cross-layer by key hash, depth 4", "request",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 4, false},
    {"target -> home/static by node id", "response", DATA_FLAG_SOURCE_HASH, 0, false},
    {"target -> home/static by key hash", "response",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH, 0, false},
    // `node_hashlocate.cpp:1818-1821`: the hosted-mobile last mile is `enqueue_data(..., addr_len=1, ...)`.
    // addr_len is a HEADER field, so the last mile adds NO inner bytes — the inner is the by-hash shape.
    // ⚠ CLARIFIED 2026-09-06 (Slice 2): this ROW IS THE HASH-PRESENT SHAPE, and that is a choice of fixture, not
    //    a description of the current producer. The live last-mile call passes `override_dst_hash = 0`
    //    (`node_hashlocate.cpp:1820`) and R-RA-25's narrow addendum keeps it that way, so a real last-mile inner
    //    today carries NO `DST_HASH`; attaching one is the parked [[B310]] proposal, not present behaviour.
    //    Under R-RA-28 the distinction costs nothing at the admission boundary — both forms admit 232 — but the
    //    RAW fit measured here is the hash-present one and must not be read as the hash-less leg's.
    {"hosted-mobile last mile (addr_len=1)", "response",
     DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH, 0, false},
};
constexpr size_t kCarrierCount = sizeof(kCarriers) / sizeof(kCarriers[0]);

// The immutable inner bytes a carrier spends before its RPC body, derived FIELD BY FIELD from the locked order in
// `frame_codec.h:1330-1334` — ⛔ never copied from the design's prose.
size_t immutable_bytes(const Carrier& c) {
    size_t n = 1;                                              // origin, always present
    if (c.flags & DATA_FLAG_DST_HASH)    n += 4;               // dst_key_hash32, 4 B LE
    if (c.flags & DATA_FLAG_CROSS_LAYER) n += 2u + c.n_layers; // n_layers | cur | layer_ids[n]
    if (c.flags & DATA_FLAG_SOURCE_HASH) n += 4;               // source_hash, 4 B LE, AFTER origin
    if (c.wrapper_type_byte)             n += 1;               // the wrapper's enclosed-TYPE body prefix
    return n;
}

// The two bounds, asked of the real authorities.
size_t air_fit_inner_cap(uint8_t flags) {
    return data_inner_cap(flags, kPlaceholderRemoteType, P::lora_max_frame_bytes);
}
constexpr size_t storage_inner_cap() { return P::max_payload_bytes_hard_cap; }

size_t governing_inner_cap(uint8_t flags) {
    const size_t air = air_fit_inner_cap(flags);
    return air < storage_inner_cap() ? air : storage_inner_cap();
}

size_t rpc_body_cap(const Carrier& c, bool crypted) {
    const uint8_t flags = static_cast<uint8_t>(crypted ? (c.flags | DATA_FLAG_CRYPTED) : c.flags);
    return governing_inner_cap(flags) - immutable_bytes(c);
}

// ★ THE BOUNDARY PROBE, AND IT RUNS THE REAL PACKERS. `body_len` is the RPC body length under test. The inner is
//   built into the FULL `TxItem.inner[]`-sized storage buffer (241) rather than a cap-sized one, deliberately: that
//   is the real sender's buffer, so the STORAGE refusal and the AIR refusal are distinguishable instead of both
//   collapsing into "the span was too small".
Bound pack_probe(const Carrier& c, bool crypted, size_t body_len, size_t* out_inner_len) {
    const uint8_t flags = static_cast<uint8_t>(crypted ? (c.flags | DATA_FLAG_CRYPTED) : c.flags);
    std::vector<uint8_t> body(body_len + (c.wrapper_type_byte ? 1u : 0u), 0x5A);
    if (c.wrapper_type_byte && !body.empty()) body[0] = kPlaceholderRemoteType;   // the enclosed TYPE prefix

    uint8_t layer_ids[P::gw_env_max_hops] = {1, 2, 3, 4};
    uint8_t inner[P::max_payload_bytes_hard_cap];
    const size_t written = pack_unicast_inner(
        std::span<uint8_t>(inner, sizeof inner), flags, /*dst_key_hash32=*/0xDEADBEEFu,
        layer_ids, c.n_layers, /*cur=*/0, /*origin=*/7, /*source_hash=*/0xC0FFEEu,
        body.data(), static_cast<uint8_t>(body.size()), 0, 0);
    if (out_inner_len) *out_inner_len = written;
    if (written == 0) return Bound::storage;                  // the packer itself refused: no truncation, ever
    if (data_frame_len(flags, kPlaceholderRemoteType, written) > P::lora_max_frame_bytes) return Bound::air;

    // …and the whole-frame authority too, on the sender's real 255-byte on-air buffer.
    uint8_t mac[8] = {};
    data_in di{};
    di.addr_len = 0;
    di.flags = flags;
    di.type = kPlaceholderRemoteType;
    di.next = 3; di.dst = 9; di.ctr = 0x1234;
    di.inner = std::span<const uint8_t>(inner, written);
    di.mac = std::span<const uint8_t>(mac, data_mac_len(flags));
    uint8_t frame[P::lora_max_frame_bytes];
    const size_t flen = pack_data(di, std::span<uint8_t>(frame, sizeof frame));
    if (flen == 0) return Bound::structural;   // the length already fits, so this is a SHAPE refusal
    return Bound::none;
}

// ⚠⚠ SUPERSEDED AS AN ADMISSION AUTHORITY 2026-09-06 BY **R-RA-28** — AND THE MEASUREMENTS THEMSELVES STAND.
//    Every cap this file computes (`rpc_body_cap` above, and the 226..236 span the boundary probes below pin) is a
//    RAW PACKING measurement: what `pack_unicast_inner` / `pack_data` physically accept for a given flag set. The
//    LARGER rows — the six by-node-id carriers whose inner spends no `DST_HASH`, reaching 236 — are NOT the body
//    an RPC carrier may admit. R-RA-28 rules that the capacity authority ALWAYS reserves the four `DST_HASH`
//    bytes alongside the mandatory origin and `SOURCE_HASH`, whether or not a legal leg transmits the field, so
//    the live admission caps are same-layer 232, typed same-layer wrapper 231, full cross-layer depth 1..4
//    229/228/227/226, and the typed cross-layer wrapper's destination depth 1..3 228/227/226 (its depth 4 is an
//    INVALID carrier, not a 225-byte one — [[B309]]). ⇒ where a reserved field is absent the two authorities
//    DIFFER ON PURPOSE: a raw 233-byte body still packs into a hash-less same-layer inner while admission refuses
//    it, and that is a measurement rather than a packer refusal ([[B308]]). The production authority is
//    `remote_body_cap(RemoteCarrier)` in `lib/core/remote_codec.cpp`, exercised in `test/test_remote_codec.cpp`;
//    this file remains its KAT INPUT and its historical record, exactly as its banner says. ⛔ Comment-only:
//    no expected value, fixture row or executable token below is changed.
//
// ★★ A MEASURED PRODUCTION INVARIANT, not a policy invented here: `pack_data` (frame_codec.cpp:900) REFUSES a
//    CRYPTED frame that carries no DST_HASH, because the per-DM nonce derives from the cleartext
//    `dst_key_hash32`. ⇒ outer `CRYPTED` is structurally unavailable to a carrier addressed BY NODE ID; for those
//    rows the mutation §8.11 asks for does not merely lower the cap, it is refused outright.
bool outer_crypted_is_available(const Carrier& c) { return (c.flags & DATA_FLAG_DST_HASH) != 0; }

// ---------------------------------------------------------------------------------------------------------------
// 0e-D — the first-hop budget, every term read from its NAMED production authority.
// ---------------------------------------------------------------------------------------------------------------
// The ruled default static-plane PHY: the native build's own `-DLORA_SF=8 -DLORA_BW=125.0 -DLORA_CR=5`
// (platformio.ini:59-61). ⛔ Not a literal chosen here — a control below proves the budget moves when it changes.
constexpr uint8_t  kCfgSf   = 8;
constexpr uint32_t kCfgBwHz = 125000;
constexpr uint8_t  kCfgCr   = 5;
// `IHal::rx_window_slop_ms` is ZERO on host and on the simulator (the HAL default). On metal it is ~53 ms per radio
// turnaround. ⛔ 0e publishes the HOST figure and NAMES the metal input; it does not describe a bench estimate as a
// compile constant (0e-D, verbatim).
constexpr uint32_t kHostSlopMs = 0;

uint32_t air(uint16_t len) { return airtime_ms(kCfgSf, kCfgBwHz, kCfgCr, P::preamble_sym, len); }

// The largest authenticated TERMINAL{scheduled} DATA frame, through the SAME framing authority 0e-C uses.
size_t max_terminal_inner_len() {
    size_t worst = 0;
    for (const Carrier& c : kCarriers) {
        if (std::strcmp(c.direction, "response") != 0 && !(c.flags & DATA_FLAG_CROSS_LAYER)) continue;
        const size_t inner = immutable_bytes(c) + kAuthResponseEnvelope + kTerminalScheduledPayload;
        if (inner > worst) worst = inner;
    }
    return worst;
}

struct Budget {
    uint32_t rts_air, cts_air, terminal_air, ack_air;
    uint32_t cts_gap, busy_retries, cts_wait, ack_wait, requeue;
    uint32_t total;
};

// ★ CTS-WAIT: `Node::start_rts_timeout()` (lib/core/node_mac.cpp:2356-2407), reproduced EXACTLY:
//       base  = airtime_routing_ms(unicast_rts_wire_len(crypted)) + airtime_routing_ms(terminal_cts_wire_len(crypted))
//       shift = min(attempt, 2)                 attempt = rts_max_retries - retries_left
//       delay = (base << shift) + 2*slop + 1
uint32_t cts_wait_ms(bool crypted, uint8_t attempt) {
    const uint32_t base = air(static_cast<uint16_t>(unicast_rts_wire_len(crypted)))
                        + air(static_cast<uint16_t>(terminal_cts_wire_len(crypted)));
    const uint32_t shift = attempt < 2 ? attempt : 2;
    return (base << shift) + 2u * kHostSlopMs + 1u;
}

// ★ ACK-WAIT: `Node::start_ack_timeout()` (lib/core/node_mac.cpp:2408-2429), reproduced EXACTLY:
//       base = airtime_ms(data_sf, bw, cr, preamble, 18 + inner_len) + airtime_routing_ms(3)
//            + rx_window_slop_ms(data_sf) + rx_window_slop_ms(routing_sf)
//       armed at base + 2
// ⛔ The `18 + inner_len` length model is the PRODUCTION authority's own and is NOT replaced by `data_frame_len`
//    here: reproducing the timer means reproducing the number the timer uses. `data_frame_len` is what 0e-C asks
//    for the frame that actually flies, and the two are reported side by side rather than silently reconciled.
uint32_t ack_wait_ms(size_t inner_len) {
    const uint32_t base = air(static_cast<uint16_t>(18 + inner_len))
                        + air(3)
                        + kHostSlopMs + kHostSlopMs;
    return base + 2u;
}

Budget compute_budget(uint8_t attempt) {
    Budget b{};
    const bool crypted = false;                 // §8.11: the outer RPC DATA carrier stays plaintext
    const size_t inner = max_terminal_inner_len();
    b.rts_air      = air(static_cast<uint16_t>(unicast_rts_wire_len(crypted)));
    b.cts_air      = air(3);                    // the ordinary CTS (node_mac.cpp:1402 `airtime_routing_ms(3)`)
    b.terminal_air = air(static_cast<uint16_t>(
                          data_frame_len(DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH | DATA_FLAG_CROSS_LAYER,
                                         kPlaceholderRemoteType, inner)));
    b.ack_air      = air(3);                    // the ACK is 3 B on the routing SF (start_ack_timeout)
    b.cts_gap      = P::cts_to_data_gap_ms;                             // :133 = 5
    b.busy_retries = static_cast<uint32_t>(P::rts_max_retries) * P::rts_busy_retry_ms;   // :135 x :134 = 2 x 30
    // B352/R-RA-23: withdrawn single-attempt budget 6506 / default 13012. Sum every attempt, not just attempt 2.
    for (uint16_t a = 0; a <= attempt; ++a) b.cts_wait += cts_wait_ms(crypted, static_cast<uint8_t>(a));
    b.ack_wait     = ack_wait_ms(inner);
    b.requeue      = P::cascade_requeue_base_ms;                        // :273 = 5000, ONE requeue at the FIRST price
    b.total = b.rts_air + b.cts_air + b.terminal_air + b.ack_air
            + b.cts_gap + b.busy_retries + b.cts_wait + b.ack_wait + b.requeue;
    return b;
}

bool want_table() { return std::getenv("MR_RADMIN0E_TABLE") != nullptr; }

}  // namespace

// ===============================================================================================================
TEST_CASE("radmin 0e-B: the candidate value types compile and their host layout is measured") {
    // ⛔ NOT a capacity ruling. This case exists so the header the ABI probe compiles for ARM and Xtensa is also
    //    compiled by the native gate — a candidate that stopped compiling would otherwise only redden in a probe
    //    nobody runs per-commit.
    CHECK(sizeof(radmin0e::PendingRequestCore) == 104);
    CHECK(sizeof(radmin0e::PendingRequestInline) == 336);
    CHECK(sizeof(radmin0e::PendingRequestRef) == 112);
    CHECK(sizeof(radmin0e::SealedRequestSlot) == 236);
    CHECK(sizeof(radmin0e::SessionCacheEntry) == 144);
    CHECK(sizeof(radmin0e::ResponseAssemblyHeader) == 24);
    CHECK(sizeof(radmin0e::ResponseChunk) == 210);
    CHECK(sizeof(radmin0e::RetainedResultHeader) == 24);
    CHECK(sizeof(radmin0e::AckDebtEntry) == 72);
    CHECK(sizeof(radmin0e::SeenRequestRecord) == 32);
    CHECK(sizeof(radmin0e::TranscriptHeader) == 24);
    CHECK(sizeof(radmin0e::TranscriptChunk) == 210);
    CHECK(sizeof(radmin0e::IngressOperationHeader) == 32);
    CHECK(sizeof(radmin0e::IngressBodySlot) == 236);
    CHECK(sizeof(radmin0e::OpenStagingSlot) == 24);
    CHECK(sizeof(radmin0e::DeferredActionRecord) == 24);

    // ★ THE ONE CONSTANT THE HEADER CLAIMS IS DERIVED. If the carrier derivation below ever moves,
    //   `kCandidateMaxCarrierBodyBytes` must move with it — this is where the two are tied together.
    const Carrier& mobile = kCarriers[0];
    CHECK(rpc_body_cap(mobile, /*crypted=*/false) == radmin0e::kCandidateMaxCarrierBodyBytes);
    CHECK(radmin0e::kCandidateSealedAckBytes == kAuthRequestEnvelope);   // §8.11: the sealed RESPONSE_ACK is 25 B
    CHECK(radmin0e::kCandidateResponseChunkBytes
          == radmin0e::kCandidateMaxCarrierBodyBytes - kAuthResponseEnvelope);
}

TEST_CASE("radmin 0e-B: the timer-wheel mirror is FAITHFUL, and one extra id is priced") {
    // ★ Without this the mirror would be pricing a struct that is not the one in the image. It is asserted, not
    //   assumed: the mirror at the REAL `kCap` must reproduce the REAL wheel byte for byte.
    // ⛔⛔ THE PRODUCTION WHEEL MOVED 2026-09-07 (§remote-admin v2 SLICE 5 spent the id 0e priced), and the two
    //    historical mirrors are KEPT — that is what makes 0e's pricing checkable AFTER the fact instead of
    //    merely remembered. ⚠ `AtCapP1` is STILL "kCap + 1 relative to the 0e baseline of 91", i.e. **92**; it is
    //    ⛔ NOT re-pointed at 93, because renaming 0e's +1 OPTION into a SECOND increase would erase the very
    //    measurement this case exists to preserve.
    CHECK(meshroute::TimerWheel::kCap == 92);
    // ★★ THE DIRECT PROOF THE SLICE OWED: the SHIPPED wheel is now byte-identical to the 92-id mirror 0e priced,
    //    and exactly +8 versus the 91-id one. ⇒ "one shared scan costs 8 bytes" is no longer a projection.
    CHECK(sizeof(radmin0e::TimerWheelStorageAtCapP1) == sizeof(meshroute::TimerWheel));
    CHECK(alignof(radmin0e::TimerWheelStorageAtCapP1) == alignof(meshroute::TimerWheel));
    CHECK(sizeof(meshroute::TimerWheel) - sizeof(radmin0e::TimerWheelStorageAtCap) == 8);
    // ⛔ AND THE 91-ID MIRROR IS NO LONGER THE IMAGE — asserted, so nobody re-reads it as current.
    CHECK(sizeof(radmin0e::TimerWheelStorageAtCap) != sizeof(meshroute::TimerWheel));

    // ⛔ THERE WAS NO FREE ID, AND THERE IS NONE NOW EITHER — at a different number. `node.h`'s
    //    `kRadminExpiryTimerId = 91` is the last one taken and it is the ONE the shared scan spent; the option
    //    priced at +8 below is the one that shipped, and option 1's eight per-class ids were NOT taken.
    //    ⓘ That exhaustion is a documented fact of those files, not a value this fixture can assert; what it
    //      CAN assert is the price of growing the wheel, which is what the three sizes below are.
    const size_t at_cap = sizeof(radmin0e::TimerWheelStorageAtCap);
    const size_t plus_1 = sizeof(radmin0e::TimerWheelStorageAtCapP1);   // option 2: one shared expiry scan
    const size_t plus_8 = sizeof(radmin0e::TimerWheelStorageAtCapP8);   // option 1 sketch: eight per-class ids
    CHECK(plus_1 > at_cap);
    CHECK(plus_8 > plus_1);
    CHECK(plus_1 - at_cap == 8);      // one id: one bool + one u64, less the byte alignment absorbs
    CHECK(plus_8 - at_cap == 72);     // eight ids: 9 B each
}

// ===============================================================================================================
TEST_CASE("radmin 0e-C: every carrier's cap is derived through the real DATA packers") {
    CHECK(kCarrierCount == 14);           // ⛔ an emptied table must redden, never report a vacuous PASS
    CHECK(kCarrierCount > 0);

    if (want_table()) {
        std::printf("\n[radmin-0e] CARRIER CAP TABLE (derived through pack_unicast_inner + pack_data)\n");
        std::printf("[radmin-0e] %-38s %-8s %5s %5s %5s %5s %5s %5s %5s\n",
                    "carrier", "dir", "immut", "air", "store", "gov", "rpc", "cmd", "out");
    }

    for (size_t i = 0; i < kCarrierCount; ++i) {
        const Carrier& c = kCarriers[i];
        CAPTURE(c.name);

        // ---- control: SOURCE_HASH is present on EVERY v2 carrier (§8.11, verbatim) -----------------------------
        CHECK((c.flags & DATA_FLAG_SOURCE_HASH) != 0);

        const size_t immut = immutable_bytes(c);
        const size_t air_cap = air_fit_inner_cap(c.flags);
        const size_t gov = governing_inner_cap(c.flags);
        const size_t cap = rpc_body_cap(c, /*crypted=*/false);

        // plaintext typed: air 242, storage 241 => STORAGE governs
        CHECK(air_cap == 242);
        CHECK(storage_inner_cap() == 241);
        CHECK(gov == 241);
        CHECK(cap == gov - immut);

        // ---- control: no copied universal literal may be the answer (R-RA-3, verbatim) --------------------------
        // ⛔⛔ RE-AIMED 2026-09-05 (Slice 0h / R-RA-25), OLD FORM KEPT VISIBLE. The first line here read
        //     `CHECK(cap != P::dm_max_body_bytes);   // 239`. That inequality was never the claim — it was a PROXY
        //     for *"this cap was derived through the real packers, not copied from an unrelated DM literal"*, and it
        //     only worked while the DM literal happened to be 239. R-RA-25 re-derives the application-DM cap as
        //     `241 - 4 (DST_HASH) - 1 (origin) - 4 (SOURCE_HASH)` = **232**, which is EXACTLY the arithmetic the
        //     same-layer by-key-hash carriers perform, so three rows of this table now legitimately equal it.
        //     Keeping the old line would have kept a FALSE inequality green by shrinking the table or by pinning a
        //     number nobody re-derives — the failure this whole fixture exists to prevent.
        // ⇒ The claim is asserted DIRECTLY instead: a coincidence with the DM cap is permitted if and ONLY IF this
        //   carrier spends the very same immutable terms the DM cap reserves. A carrier that spent a DIFFERENT
        //   prefix and still landed on 232 would be a copied literal, and this reddens. (The executed cap /
        //   cap+1 packing probe below is the second, independent half of the same proof.)
        CHECK((cap == P::dm_max_body_bytes)
              == (immut == P::dm_inner_origin_bytes + P::dm_inner_dst_hash_bytes + P::dm_inner_source_hash_bytes));
        CHECK(P::dm_max_body_bytes == P::max_payload_bytes_hard_cap
                                      - (P::dm_inner_origin_bytes + P::dm_inner_dst_hash_bytes
                                         + P::dm_inner_source_hash_bytes));   // the DM cap's own derivation, named
        CHECK(cap != P::max_payload_bytes_hard_cap);           // 241
        CHECK(cap != 214);
        CHECK(cap != 213);

        // ---- at cap: the REAL packers accept -------------------------------------------------------------------
        size_t inner_len = 0;
        CHECK(pack_probe(c, /*crypted=*/false, cap, &inner_len) == Bound::none);
        CHECK(inner_len == gov);
        CHECK(data_frame_len(c.flags, kPlaceholderRemoteType, inner_len) <= P::lora_max_frame_bytes);

        // ---- at cap + 1: the SAME real packer refuses, and STORAGE is what refuses ------------------------------
        CHECK(pack_probe(c, /*crypted=*/false, cap + 1, nullptr) == Bound::storage);

        // ---- outer CRYPTED recomputes against the LOWER air-fit answer and therefore lowers the cap -------------
        const uint8_t cflags = static_cast<uint8_t>(c.flags | DATA_FLAG_CRYPTED);
        const size_t ccap = rpc_body_cap(c, /*crypted=*/true);
        CHECK(air_fit_inner_cap(cflags) == 238);
        CHECK(governing_inner_cap(cflags) == 238);             // AIR governs under CRYPTED
        CHECK(ccap == cap - 3);
        CHECK(ccap < cap);
        if (outer_crypted_is_available(c)) {
            CHECK(pack_probe(c, /*crypted=*/true, ccap, nullptr) == Bound::none);
            CHECK(pack_probe(c, /*crypted=*/true, ccap + 1, nullptr) == Bound::air);   // AIR is what refuses here
        } else {
            // A by-node-id carrier cannot be sealed at all — the SHAPE is refused before any length question.
            CHECK(pack_probe(c, /*crypted=*/true, ccap, nullptr) == Bound::structural);
            CHECK(pack_probe(c, /*crypted=*/true, ccap + 1, nullptr) == Bound::air);   // length refuses first here
        }

        if (want_table()) {
            std::printf("[radmin-0e] %-38s %-8s %5zu %5zu %5zu %5zu %5zu %5zu %5zu\n",
                        c.name, c.direction, immut, air_cap, storage_inner_cap(), gov, cap,
                        cap - kAuthRequestEnvelope, cap - kAuthResponseEnvelope);
        }
    }
}

TEST_CASE("radmin 0e-C: outer CRYPTED is structurally unavailable to a by-node-id carrier") {
    // ★★★ A 0e MEASUREMENT THAT THE DESIGN DOES NOT YET STATE, recorded here because it changes what §8.11's
    //     CRYPTED mutation means. §8.11 says setting `DATA_FLAG_CRYPTED` "makes the real air-fit authority return
    //     238 before carrier fields, so the cap battery must include that mutation". TRUE — but only for a
    //     HASH-ADDRESSED carrier. `pack_data` refuses CRYPTED without DST_HASH outright
    //     (`lib/core/frame_codec.cpp:900`: "the per-DM nonce derives from the CLEARTEXT dst_key_hash32, so it MUST
    //     be present"), so for a by-node-id carrier the sealed variant has no cap at all rather than a lower one.
    // ⛔ 0e reports this; it does not decide what the design should say about it.
    size_t by_id = 0, by_hash = 0;
    for (const Carrier& c : kCarriers) {
        if (outer_crypted_is_available(c)) ++by_hash; else ++by_id;
    }
    CHECK(by_id == 6);        // home->target by id, cross-layer by id x4, target->home by id
    CHECK(by_hash == 8);
    CHECK(by_id + by_hash == kCarrierCount);

    // The rule itself, asked of the packer rather than quoted from the comment.
    uint8_t inner[16] = {};
    uint8_t mac8[8] = {};
    uint8_t frame[P::lora_max_frame_bytes];
    data_in di{};
    di.type = kPlaceholderRemoteType;
    di.inner = std::span<const uint8_t>(inner, sizeof inner);
    di.mac = std::span<const uint8_t>(mac8, 8);

    di.flags = DATA_FLAG_CRYPTED | DATA_FLAG_SOURCE_HASH;                        // no DST_HASH
    CHECK(pack_data(di, std::span<uint8_t>(frame, sizeof frame)) == 0);
    di.flags = DATA_FLAG_CRYPTED | DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH;   // with DST_HASH
    CHECK(pack_data(di, std::span<uint8_t>(frame, sizeof frame)) > 0);
}

TEST_CASE("radmin 0e-C: the registered-mobile example reproduces its own field accounting") {
    // ⛔ The design's "231 / 206" is NOT copied: the ten immutable bytes are counted field by field from the
    //    locked inner order, then the cap is asked of the codec.
    const Carrier& m = kCarriers[0];
    CHECK(std::strcmp(m.name, "mobile -> own home wrapper") == 0);
    CHECK((m.flags & DATA_FLAG_DST_HASH) != 0);
    CHECK((m.flags & DATA_FLAG_SOURCE_HASH) != 0);
    CHECK((m.flags & DATA_FLAG_MS_ENCLOSED_TYPE) != 0);
    CHECK((m.flags & DATA_FLAG_CROSS_LAYER) == 0);
    CHECK(immutable_bytes(m) == 4u /*dst_hash*/ + 1u /*origin*/ + 4u /*source_hash*/ + 1u /*enclosed type*/);
    CHECK(immutable_bytes(m) == 10);
    CHECK(rpc_body_cap(m, false) == 231);
    CHECK(rpc_body_cap(m, false) - kAuthRequestEnvelope == 206);     // §8.11's authenticated command cap
    CHECK(rpc_body_cap(m, false) - kAuthResponseEnvelope == 205);
    CHECK(rpc_body_cap(m, false) - kOpenRequestEnvelope == 222);
    CHECK(rpc_body_cap(m, false) - kOpenResponseEnvelope == 221);
}

TEST_CASE("radmin 0e-C: hash and node-id forms differ ONLY by the 4-byte DST_HASH") {
    const Carrier& by_id = kCarriers[1];
    const Carrier& by_hash = kCarriers[2];
    CHECK(std::strcmp(by_id.name, "home -> target by node id") == 0);
    CHECK(std::strcmp(by_hash.name, "home -> target by key hash") == 0);
    CHECK(by_id.flags == (by_hash.flags & ~DATA_FLAG_DST_HASH));
    CHECK(immutable_bytes(by_hash) - immutable_bytes(by_id) == 4);
    CHECK(rpc_body_cap(by_id, false) - rpc_body_cap(by_hash, false) == 4);
    // the hosted-mobile last mile adds NO inner bytes over the by-hash shape (addr_len is a HEADER field)
    const Carrier& last_mile = kCarriers[13];
    CHECK(immutable_bytes(last_mile) == immutable_bytes(by_hash));
}

TEST_CASE("radmin 0e-C: every legal cross-layer depth is exercised and the minimum carrier is identified") {
    // `pack_unicast_inner` (frame_codec.cpp:1089) accepts n_layers 1..gw_env_max_hops and refuses anything else,
    // so 1..4 IS the legal domain rather than a sample of it.
    CHECK(P::gw_env_max_hops == 4);
    size_t seen_by_id = 0, seen_by_hash = 0;
    size_t min_cap = 1024;
    const char* min_name = nullptr;
    for (const Carrier& c : kCarriers) {
        if (c.flags & DATA_FLAG_CROSS_LAYER) {
            CHECK(c.n_layers >= 1);
            CHECK(c.n_layers <= P::gw_env_max_hops);
            if (c.flags & DATA_FLAG_DST_HASH) ++seen_by_hash; else ++seen_by_id;
            CHECK(immutable_bytes(c) == (c.flags & DATA_FLAG_DST_HASH ? 9u : 5u) + 2u + c.n_layers);
        }
        const size_t cap = rpc_body_cap(c, false);
        if (cap < min_cap) { min_cap = cap; min_name = c.name; }
    }
    CHECK(seen_by_id == P::gw_env_max_hops);
    CHECK(seen_by_hash == P::gw_env_max_hops);
    CHECK(min_name != nullptr);
    CHECK(std::strcmp(min_name, "cross-layer by key hash, depth 4") == 0);
    CHECK(min_cap == 226);
    // …and the minimum-cap carrier packs at its cap and refuses one past it, like every other row.
    const Carrier deepest{"x", "request",
                          DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | DATA_FLAG_CROSS_LAYER, 4, false};
    CHECK(pack_probe(deepest, false, min_cap, nullptr) == Bound::none);
    CHECK(pack_probe(deepest, false, min_cap + 1, nullptr) == Bound::storage);
}

TEST_CASE("radmin 0e-C: dropping SOURCE_HASH, DST_HASH or a path byte to recover room is REFUSED") {
    // ★ The temptation is REAL and measurable — each drop really does return bytes — which is exactly why the
    //   refusal has to be a check rather than a comment. §8.11: "Every request and response carrier also requires
    //   SOURCE_HASH; no implementation may drop it to recover body capacity."
    const Carrier& c = kCarriers[8];   // cross-layer by key hash, depth 2
    const size_t honest = rpc_body_cap(c, false);

    Carrier no_source = c; no_source.flags &= static_cast<uint8_t>(~DATA_FLAG_SOURCE_HASH);
    Carrier no_dst    = c; no_dst.flags    &= static_cast<uint8_t>(~DATA_FLAG_DST_HASH);
    Carrier short_path = c; short_path.n_layers = static_cast<uint8_t>(c.n_layers - 1);

    CHECK(rpc_body_cap(no_source, false) == honest + 4);       // the room a dropped SOURCE_HASH would return
    CHECK(rpc_body_cap(no_dst, false) == honest + 4);
    CHECK(rpc_body_cap(short_path, false) == honest + 1);

    // …and none of those three shapes is a v2 carrier. A carrier without SOURCE_HASH is not merely smaller; it is
    // not a carrier, and this predicate is what a later slice must consult before believing a cap.
    auto is_v2_carrier = [](const Carrier& x) { return (x.flags & DATA_FLAG_SOURCE_HASH) != 0; };
    CHECK_FALSE(is_v2_carrier(no_source));
    CHECK(is_v2_carrier(no_dst));            // by-id addressing is legal; DROPPING the hash from a BY-HASH carrier
    CHECK(is_v2_carrier(short_path));        //   or shortening its path changes WHICH carrier it is, not its cap
    for (const Carrier& row : kCarriers) CHECK(is_v2_carrier(row));
    // the shortened path is a DIFFERENT carrier that is already in the table under its own name
    CHECK(rpc_body_cap(short_path, false) == rpc_body_cap(kCarriers[7], false));
}

// ===============================================================================================================
TEST_CASE("radmin 0e-D: the first-hop budget, term by term from the named production authorities") {
    // The ruled default PHY is the native build's own.
    CHECK(P::preamble_sym == 16);
    CHECK(P::cts_to_data_gap_ms == 5);
    CHECK(P::rts_busy_retry_ms == 30);
    CHECK(P::rts_max_retries == 2);
    CHECK(P::cascade_requeue_base_ms == 5000);
    // ⛔ The two constants the formula must NEVER substitute (R-RA-20, verbatim).
    CHECK(P::cascade_requeue_backoff_cap_ms == 30000);
    CHECK(P::send_defer_ttl_ms != P::cascade_requeue_base_ms);
    CHECK(P::cascade_requeue_backoff_cap_ms != P::cascade_requeue_base_ms);

    const size_t inner = max_terminal_inner_len();
    CHECK(inner == 15u + kAuthResponseEnvelope + kTerminalScheduledPayload);   // deepest by-hash cross-layer
    CHECK(inner == 46);

    const Budget b = compute_budget(/*attempt=*/P::rts_max_retries);
    const uint32_t floor_ms = b.total;
    const uint32_t default_ms = 2u * b.total;                          // R-RA-20: the owner's explicit 2x
    const uint32_t ceiling_ms = P::e2e_ack_deadline_xl_ms - 1u;

    CHECK(ceiling_ms == 299999);
    CHECK(floor_ms == 7006);
    CHECK(default_ms == 14012);
    CHECK(floor_ms != 6506);
    CHECK(default_ms != 13012);
    CHECK(P::e2e_ack_deadline_xl_ms == 300000);

    // Every term is strictly positive — a silently-zero term would make the budget quietly smaller.
    CHECK(b.rts_air > 0);
    CHECK(b.cts_air > 0);
    CHECK(b.terminal_air > 0);
    CHECK(b.ack_air > 0);
    CHECK(b.cts_wait > 0);
    CHECK(b.ack_wait > 0);
    CHECK(b.cts_gap == 5);
    CHECK(b.busy_retries == 60);
    CHECK(b.requeue == 5000);

    // The sum is the sum — no term may be dropped.
    CHECK(b.total == b.rts_air + b.cts_air + b.terminal_air + b.ack_air
                   + b.cts_gap + b.busy_retries + b.cts_wait + b.ack_wait + b.requeue);

    // ⛔ STOP condition 9: the budget and its 2x default MUST fit strictly below the 300 s ceiling.
    CHECK(floor_ms < ceiling_ms);
    CHECK(default_ms < ceiling_ms);
    CHECK(default_ms == 2u * floor_ms);

    // ⛔ No bare 3 / 15 / 30-second literal is the answer (the legacy path's 3 s at firmware_remote.cpp:148 is
    //    exactly what R-RA-20 replaces, and the retired draft's "30 s" is what it deletes).
    CHECK(floor_ms != 3000);
    CHECK(floor_ms != 15000);
    CHECK(floor_ms != 30000);
    CHECK(default_ms != 3000);
    CHECK(default_ms != 15000);
    CHECK(default_ms != 30000);

    if (want_table()) {
        std::printf("\n[radmin-0e] FIRST-HOP BUDGET at SF%u/BW%u/CR%u, host slop=%u\n",
                    kCfgSf, kCfgBwHz, kCfgCr, kHostSlopMs);
        std::printf("[radmin-0e]   RTS airtime (%zu B)                    %6u ms\n",
                    unicast_rts_wire_len(false), b.rts_air);
        std::printf("[radmin-0e]   CTS airtime (3 B)                      %6u ms\n", b.cts_air);
        std::printf("[radmin-0e]   TERMINAL{scheduled} DATA (%zu B frame) %6u ms\n",
                    data_frame_len(DATA_FLAG_SOURCE_HASH | DATA_FLAG_DST_HASH | DATA_FLAG_CROSS_LAYER,
                                   kPlaceholderRemoteType, inner), b.terminal_air);
        std::printf("[radmin-0e]   ACK airtime (3 B)                      %6u ms\n", b.ack_air);
        std::printf("[radmin-0e]   cts_to_data_gap_ms                     %6u ms\n", b.cts_gap);
        std::printf("[radmin-0e]   rts_max_retries * rts_busy_retry_ms    %6u ms\n", b.busy_retries);
        std::printf("[radmin-0e]   start_rts_timeout CTS-wait SUM 0/1/2   %6u ms\n", b.cts_wait);
        std::printf("[radmin-0e]     attempt 0 / 1 / 2                    %6u / %u / %u ms\n",
                    cts_wait_ms(false, 0), cts_wait_ms(false, 1), cts_wait_ms(false, 2));
        std::printf("[radmin-0e]   start_ack_timeout ACK-wait             %6u ms\n", b.ack_wait);
        std::printf("[radmin-0e]   cascade_requeue_base_ms                %6u ms\n", b.requeue);
        std::printf("[radmin-0e]   -------------------------------------- %6u ms  = budget(cfg)\n", b.total);
        std::printf("[radmin-0e]   floor = 1x                             %6u ms\n", floor_ms);
        std::printf("[radmin-0e]   default = 2x (R-RA-20)                 %6u ms\n", default_ms);
        std::printf("[radmin-0e]   ceiling = e2e_ack_deadline_xl_ms - 1   %6u ms\n", ceiling_ms);
        std::printf("[radmin-0e]   remaining interval headroom            %6u ms\n", ceiling_ms - default_ms);
    }
}

TEST_CASE("radmin 0e-D: every budget input moves the result in the correct direction") {
    const Budget b = compute_budget(P::rts_max_retries);

    // ---- omitting any airtime term LOWERS the budget: none of them is decorative --------------------------------
    CHECK(b.total - b.rts_air < b.total);
    CHECK(b.total - b.cts_air < b.total);
    CHECK(b.total - b.terminal_air < b.total);
    CHECK(b.total - b.ack_air < b.total);
    CHECK(b.total - b.cts_gap < b.total);
    CHECK(b.total - b.busy_retries < b.total);
    CHECK(b.total - b.cts_wait < b.total);
    CHECK(b.total - b.ack_wait < b.total);
    CHECK(b.total - b.requeue < b.total);

    // ---- a SLOWER PHY must cost more, and the budget must not be hard-coded to SF8 ------------------------------
    CHECK(airtime_ms(10, kCfgBwHz, kCfgCr, P::preamble_sym, 59) > airtime_ms(8, kCfgBwHz, kCfgCr, P::preamble_sym, 59));
    CHECK(airtime_ms(kCfgSf, 62500, kCfgCr, P::preamble_sym, 59) > airtime_ms(kCfgSf, kCfgBwHz, kCfgCr, P::preamble_sym, 59));
    CHECK(airtime_ms(kCfgSf, kCfgBwHz, 8, P::preamble_sym, 59) > airtime_ms(kCfgSf, kCfgBwHz, kCfgCr, P::preamble_sym, 59));

    // ---- a LONGER terminal must cost more: a stale terminal length cannot pass ----------------------------------
    CHECK(air(static_cast<uint16_t>(
              data_frame_len(DATA_FLAG_SOURCE_HASH, kPlaceholderRemoteType, max_terminal_inner_len() + 32)))
          >= b.terminal_air);

    // ---- the CTS-wait really is attempt-dependent (base << shift), capped at shift 2 ----------------------------
    CHECK(cts_wait_ms(false, 1) > cts_wait_ms(false, 0));
    CHECK(cts_wait_ms(false, 2) > cts_wait_ms(false, 1));
    CHECK(cts_wait_ms(false, 3) == cts_wait_ms(false, 2));      // the shift caps at 2
    CHECK(cts_wait_ms(false, 7) == cts_wait_ms(false, 2));
    // …and a CRYPTED flight prices the wider RTS/CTS identities (10/6 -> 11/7)
    CHECK(unicast_rts_wire_len(true) == unicast_rts_wire_len(false) + 1);
    CHECK(terminal_cts_wire_len(true) == terminal_cts_wire_len(false) + 1);
    CHECK(cts_wait_ms(true, 2) >= cts_wait_ms(false, 2));

    // ---- the ACK-wait grows with the DATA it is waiting behind --------------------------------------------------
    CHECK(ack_wait_ms(max_terminal_inner_len() + 64) > ack_wait_ms(max_terminal_inner_len()));

    // ---- ⛔ the two forbidden substitutions would BOTH change the answer, so neither is silently equivalent ------
    const uint32_t with_cap = b.total - b.requeue + P::cascade_requeue_backoff_cap_ms;
    const uint32_t with_ttl = b.total - b.requeue + P::send_defer_ttl_ms;
    CHECK(with_cap != b.total);
    CHECK(with_ttl != b.total);
    CHECK(with_cap > b.total);       // the 30 s backoff CAP prices later requeues, not the first
    CHECK(with_ttl > b.total);       // send_defer_ttl_ms is the no-route ring, an unrelated clock

    // ---- the default is EXACTLY 2x, never an approximation --------------------------------------------------
    CHECK(2u * b.total == b.total + b.total);
    CHECK(2u * b.total != b.total * 3u / 2u);
}
