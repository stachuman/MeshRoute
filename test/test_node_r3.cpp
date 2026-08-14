// MeshRoute — test_node_r3.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// R3 data plane: the RECEIVER flight (RTS->CTS->DATA->delivered) and the
// last_acked dedup TTL gate — paths the idle/lossless scenario gates (t86/t87)
// do not isolate. Driven through on_recv/on_timer with an in-memory Hal.
//
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main());
// -fno-exceptions => CHECK only; guard optional derefs with `if`.
#include "doctest.h"

#include "node.h"
#include "identity.h"
#include "frame_codec.h"
#include "airtime.h"    // §hybrid-rts S1 item 8: the CTS-wait is asserted against the real airtime model
#include "leaf_config.h"   // R6.1: real config_hash for the peering-filter test
#include "ram_inbox_store.h"
#include "support/test_hal.h"

#include <array>
#include <cstring>
#include <functional>   // §hybrid-rts S4: the per-arm mismatch lambdas
#include <span>
#include <string>
#include <vector>

using namespace meshroute;

namespace {

struct Ev { std::string type; int to = -1; int dst = -1; bool dup = false;
            bool has_payload = false; std::string payload; int depth = -1; int ctr = -1;
            int next = -1; int requeue_count = -1; int reason = -1; int from = -1;
            int tag = -1; uint32_t seq = 0; int sf = -1; int result = -1; std::string label;
            int rt_total = -1;                                    // §B4: the sync-response plane's route count
            bool healed = false; bool has_healed = false;
            // ★ §hybrid-rts S4: the implicit-forward credit's own fields. `basis` is the NAMED observation
            // (`local_admitted` / `alternate_path` — S4 emitted the first as `local_data`, renamed by S4c because
            // NEITHER basis proves a DATA of ours aired) and the two bools are the pending state, so the credit
            // census can be split by state from ONE instrument instead of two.
            std::string basis; bool awaiting_cts = false; bool awaiting_ack = false;
            int forward_next = -1; };

// §T1/T2: `seq` is the flight identity `Node::tx_params_of` stamped. The production DeviceHal now carries it through
// queue/in-flight/outcome; native TestHal reads it solely for direct regression evidence at the hand-off boundary.
struct TxFrame { std::string label; std::vector<uint8_t> bytes; uint32_t seq = 0; };

class TestHal : public mrtest::TestHalBase {
public:
    std::vector<Ev> events;
    std::vector<TxFrame> tx_frames;   // captured TX bytes (to parse DATA hop-budget fields)
    // NB `rand_calls` (the base's) guards the cascade #1 determinism risk: no EXTRA draws. rand_bytes is
    // overridden below and does NOT route through rand_range, so it does not perturb those deltas.

    // ★★★★ §hybrid-rts S4b (2026-08-09) — `tx_answer`, COPIED (U1) from `test_node_join.cpp`'s TestHal rather than
    // invented, because §HYBRID-RTS-S4's own mutation M3 was nearly INERT for want of it: this HAL always handed
    // off, so `TxHandOff::rejected` was UNREACHABLE in this TU and a "HAL rejection" case here would have been an
    // inert green. ⛔ THE LESSON, and it is the same one join's copy records: before declaring a disposition
    // unreachable, ask what ONE field would make it reachable. `DeviceHal::tx` answers `busy` on a full 8-entry
    // outbound ring and RETAINS NOTHING, which is exactly what this reproduces.
    // ⓘ Defaults to `ok` and a refused frame is NOT recorded in `tx_frames`, so every pre-existing case in this TU
    //   is byte-identical — none of them sets it.
    TxResult tx_answer = TxResult::ok;
    int      tx_calls  = 0;                       // ATTEMPTS, refused or not (a refusal is still an attempt)
    TxResult tx(const uint8_t* b, size_t n, const TxParams& p) override {
        ++tx_calls;
        if (tx_answer != TxResult::ok) return tx_answer;   // refused: the HAL keeps nothing, so neither do we
        TxFrame f; f.label = p.label ? p.label : ""; f.seq = p.seq;   // §T1: the identity the SENDING SITE supplied
        f.bytes.assign(b, b + n); tx_frames.push_back(std::move(f));
        return TxResult::ok;
    }
    uint64_t _channel_busy_until = 0;   // R4.5: scriptable LBT busy horizon
    uint64_t channel_busy_until() override { return _channel_busy_until; }
    uint64_t _airtime_used = 0;   // R4.0: scriptable rolling-window airtime for compute_budget_tier
    uint64_t airtime_used_ms(uint64_t) override { return _airtime_used; }
    uint64_t _oldest_tx_end = 0;  // scriptable oldest in-window TX-end (duty_status recovery calc)
    uint64_t oldest_tx_end_ms() override { return _oldest_tx_end; }
    uint32_t _slop = 0;                                                   // §CTS-wait: settable metal turnaround slop (rx_window_slop_ms)
    uint32_t rx_window_slop_ms(int) const override { return _slop; }
    uint32_t last_after_delay[16] = {};                                   // §CTS-wait: last after() delay per timer id (id<16)
    std::vector<std::pair<uint32_t, uint32_t>> armed;                     // §F-XL-2: (delay_ms, timer_id) captured from after() (any id)
    bool     after(uint32_t d, uint32_t id) override { if (id < 16) last_after_delay[id] = d; armed.emplace_back(d, id); return true; }
    // Crypto RNG (DISTINCT from the base fixture's weak rand_range, whose default returns `lo`=0). A real HW RNG never
    // returns an all-zero seed; emulate a non-degenerate deterministic stream so the e2e nonce-seed is realistic.
    // zero_rng=true forces all-zero (to exercise the R7 bad-RNG fail-loud guard in e2e_seal_inner).
    bool     zero_rng = false;
    uint8_t  _rb = 0x11;
    void     rand_bytes(uint8_t* o, size_t n) override {
        for (size_t i = 0; i < n; ++i) { _rb = static_cast<uint8_t>(_rb * 31 + 7); o[i] = zero_rng ? 0 : (_rb == 0 ? 0xA5 : _rb); }
    }
    void     emit(const char* type, const EventField* f, size_t n) override {
        Ev e; e.type = type;
        for (size_t i = 0; i < n; ++i) {
            if      (std::strcmp(f[i].key, "to") == 0)  e.to  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dst") == 0) e.dst = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "dest") == 0) e.dst = static_cast<int>(f[i].i);  // rt_update uses "dest"
            else if (std::strcmp(f[i].key, "dup") == 0) e.dup = f[i].b;
            else if (std::strcmp(f[i].key, "healed") == 0) { e.healed = f[i].b; e.has_healed = true; }
            else if (std::strcmp(f[i].key, "depth") == 0) e.depth = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "ctr") == 0)   e.ctr   = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "next") == 0)  e.next  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "from") == 0)  e.from  = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "reason") == 0) e.reason = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "tag") == 0) e.tag = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "seq") == 0) e.seq = static_cast<uint32_t>(f[i].i);
            else if (std::strcmp(f[i].key, "sf") == 0) e.sf = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "result") == 0) e.result = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "label") == 0 && f[i].s) e.label = f[i].s;
            else if (std::strcmp(f[i].key, "requeue_count") == 0) e.requeue_count = static_cast<int>(f[i].i);
            else if (std::strcmp(f[i].key, "rt_total") == 0) e.rt_total = static_cast<int>(f[i].i);   // §B4
            else if (std::strcmp(f[i].key, "payload") == 0 && f[i].s) { e.has_payload = true; e.payload = f[i].s; }
            else if (std::strcmp(f[i].key, "basis") == 0 && f[i].s) e.basis = f[i].s;              // §hybrid-rts S4
            else if (std::strcmp(f[i].key, "forward_next") == 0) e.forward_next = static_cast<int>(f[i].i);  // §hybrid-rts S4
            else if (std::strcmp(f[i].key, "awaiting_cts") == 0) e.awaiting_cts = f[i].b;          // §hybrid-rts S4
            else if (std::strcmp(f[i].key, "awaiting_ack") == 0) e.awaiting_ack = f[i].b;          // §hybrid-rts S4
        }
        events.push_back(e);
    }

    int count(const char* t) const { int n = 0; for (const auto& e : events) if (e.type == t) ++n; return n; }
    const Ev* last(const char* t) const { const Ev* r = nullptr; for (const auto& e : events) if (e.type == t) r = &e; return r; }
    const TxFrame* last_tx(const char* label) const {
        const TxFrame* r = nullptr; for (const auto& f : tx_frames) if (f.label == label) r = &f; return r;
    }
};

constexpr uint32_t kRtsTimeoutTimerId    = 4;   // mirror node.h's private constants
constexpr uint32_t kAckTimeoutTimerId    = 5;
constexpr uint32_t kCtsToDataGapTimerId  = 7;
constexpr uint32_t kQueueWakeupTimerId   = 8;   // become_free re-drain (NAV origination-jitter wake)
constexpr uint32_t kPostAckTimerId       = 9;
constexpr uint32_t kRetryBackoffTimerId  = 10;
constexpr uint32_t kDeferredDrainTimerId = 11;
constexpr uint32_t kCascadeRequeueTimerId = 12;
constexpr uint32_t kNackWaitTimerId      = 13;
constexpr uint32_t kTriggeredBeaconTimerId = 3;   // R4.2: rerank re-advertises via a triggered beacon
constexpr uint32_t kBeaconTimerId        = 1;     // R4.3 periodic beacon fire
constexpr uint32_t kAgingTimerId         = 2;     // periodic route/ledger age-out sweep (§team-parity T2 ratchet test)
constexpr uint32_t kBeaconJitterTimerId  = 27;    // R4.3 silence-jitter deferred beacon (#D ring base [27..30])
constexpr uint32_t kLbtDeferTimerId      = 15;    // R4.5 LBT busy-channel deferred TX
constexpr uint32_t kRadioBusyRetryTimerId = 19;   // R4.5b on_radio_busy stash-retry (slot base)
constexpr uint32_t kDutyDeferTimerId      = 23;   // #2 tx_with_retry duty-defer re-run (slot base)
constexpr uint32_t kRtsDutyDeferTimerId   = 31;   // #A redo: over-budget RTS duty-defer re-check/hand
constexpr uint32_t kRreqForwardTimerBase  = 85;   // §F-XL-2: rreq_forward de-storm ring [85..88]
constexpr uint32_t kRreqForwardSlots      = 4;
constexpr uint32_t kSyncResponseTimerId   = 32;   // §B4: jittered sync-response ring base [32..47]

// §F-XL-2: an RREQ relay no longer re-broadcasts immediately — it stashes the built frame + arms a jittered timer
// (kRreqForwardTimerBase+slot). TestHal never auto-fires, so a test expecting the re-broadcast on-air must drive the
// ring (a spent/never-armed slot is a no-op). Mirrors fire_h_forwards in test_node_hashlocate.cpp.
static void fire_rreq_forwards(Node& node) {
    for (uint32_t id = kRreqForwardTimerBase; id < kRreqForwardTimerBase + kRreqForwardSlots; ++id) node.on_timer(id);
}

static size_t mk_nack(uint8_t to, uint8_t ctr_lo, uint8_t reason, uint8_t payload,
                      std::array<uint8_t, 8>& b) {
    nack_in in{}; in.reason = reason; in.ctr_lo = ctr_lo; in.payload = payload; in.to = to;
    return pack_nack(in, std::span<uint8_t>(b.data(), b.size()));
}

// Pack a 1-entry beacon from `src` advertising route {dest via next, hops} (so a
// receiver installs a candidate to `dest` whose next-hop is `src`). Distinct
// scores let the test pin candidate ordering.
static size_t mk_beacon_route(uint8_t src, uint8_t dest, uint8_t next, uint8_t hops,
                              uint8_t score_bucket, std::array<uint8_t, 64>& b) {
    beacon_entry e{}; e.dest = dest; e.next = next; e.score_bucket = score_bucket;
    e.is_gateway = false; e.hops = hops;
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x2000u + src;
    in.entries = std::span<const beacon_entry>(&e, 1);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}

// Drive a sender's primary next-hop to exhaustion (rts_max_retries=3): each
// RTS-timeout that still has budget decrements + arms the backoff; firing the
// backoff re-sends. After the budget is spent the next RTS-timeout cascades.
static void exhaust_rts_same_hop(Node& node) {
    for (int i = 0; i < 3; ++i) {
        node.on_timer(kRtsTimeoutTimerId);   // retries_left-- (arms kRetryBackoffTimerId)
        node.on_timer(kRetryBackoffTimerId); // re-tx_rts_retry on the SAME hop
    }
    node.on_timer(kRtsTimeoutTimerId);        // retries_left==0 -> cascade_to_alt
}

// ★ §hybrid-rts S1 (2026-08-08): a UNICAST rts_in without a flight identity no longer packs at all
// (`pack_rts` returns 0 — C2 fail-loud), so every injected DM RTS in this file names one.
// ★★★ §hybrid-rts S2 (2026-08-08) — S1's note said *"⚠ S2 MUST revisit every caller: once DATA validation
// lands, a placeholder that disagrees with the DATA will (correctly) be refused."* IT DID, and it was measured
// rather than reasoned: landing the validation turned **21 pre-existing test cases RED** across three files —
// every one that injects an RTS and then the DATA it authorises. ⇒ the two OPTIONAL trailing parameters below
// let a caller name the flight the DATA will actually carry; the default keeps the old well-formed PLACEHOLDER
// for the many cases that never send a DATA at all (RTS-only anti-spam, NACK, busy, cascade...).
// ⓘ THAT SPLIT IS ITSELF THE COVERAGE STATEMENT: a case that passes `origin`/`ctr` is exercising the MATCH path;
//   a case that sends a DATA WITHOUT passing them is exercising the MISMATCH path, and there are dedicated §S2
//   cases for that below rather than accidental ones.
static RtsFlightIdentity mk_rts_id(uint8_t src, uint8_t ctr_lo) {
    return rts_flight_identity_plain(src, ctr_lo);
}
// ★★ §hybrid-rts S2 — THE GENERAL TOOL: derive a DATA frame's OWN flight identity from the frame, i.e. compute
// exactly what `handle_data` will recompute. Any case that already holds a packed DATA frame (a sealed DM, a
// location DM, an `e2e_seal_AtoB` product) should pass this to `mk_rts` rather than restate origin/ctr/seed by hand
// — restating them is how a test drifts from the wire it claims to exercise.
static RtsFlightIdentity id_of_data_frame(const uint8_t* f, size_t n) {
    auto d = parse_data(std::span<const uint8_t>(f, n));
    if (!d) return RtsFlightIdentity{};
    auto inner = data_inner(std::span<const uint8_t>(f, n), *d);
    auto ui    = parse_unicast_inner(inner, d->flags);
    uint8_t seed[8] = {0};
    if (d->crypted) { auto sd = data_nonce_seed(std::span<const uint8_t>(f, n), *d);
                      for (uint8_t i = 0; i < 8 && i < sd.size(); ++i) seed[i] = sd[i]; }
    return rts_flight_identity(d->crypted, ui ? ui->origin : 0, d->ctr, d->dst, seed);
}
// A unicast RTS whose identity is taken from an already-built DATA frame (the shape above).
static size_t mk_rts_for_frame(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo, uint8_t plen,
                               std::array<uint8_t, 16>& b, const uint8_t* f, size_t n) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = 0; in.payload_len = plen; in.id = id_of_data_frame(f, n);
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_rts(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo,
                     uint8_t plen, std::array<uint8_t, 16>& b, uint8_t rts_flags = 0,
                     int origin = -1, int ctr = -1, const uint8_t* seed = nullptr) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = rts_flags; in.payload_len = plen; in.m_payload_id_lo16 = 0;
    in.id = (seed != nullptr && ctr >= 0)                                  // CRYPTED flight: seed | ctr | dst
              ? rts_flight_identity_crypted(seed, static_cast<uint16_t>(ctr), dst)
          : (origin >= 0 && ctr >= 0)                                      // PLAINTEXT flight: origin | ctr
              ? rts_flight_identity_plain(static_cast<uint8_t>(origin), static_cast<uint16_t>(ctr))
              : mk_rts_id(src, ctr_lo);   // placeholder: fine for an RTS whose DATA never arrives
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_data(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                      const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;   // [origin][body] — no payload-flags byte
    uint8_t bl = 0; while (body[bl]) { inner[1 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA with explicit hop-budget fields (for the HOP_BUDGET enforcement tests).
static size_t mk_data_hb(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                         uint8_t hops_remaining, uint8_t committed,
                         const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;   // [origin][body] — no payload-flags byte
    uint8_t bl = 0; while (body[bl]) { inner[1 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.hops_remaining = hops_remaining; in.committed_hops = committed; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA with explicit flags + TYPE + a raw body (may contain 0 bytes) — for the E2E ACK tests. The inner is
// the normal-unicast shape [origin][body] (no payload-flags byte); `type` rides the byte-8 TYPE byte (APP).
static size_t mk_data_e2e(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin, uint8_t flags,
                          const uint8_t* body, uint8_t body_len, std::array<uint8_t, 64>& b, uint8_t type = 0) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;
    for (uint8_t i = 0; i < body_len; ++i) inner[1 + i] = body[i];
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = flags; in.type = type; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + body_len);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA carrying a DST_HASH inner ([dst_key_hash32 LE 4B][origin][body]) with the DST_HASH header flag set —
// L2c verify-on-delivery. No payload-flags byte; presence is signalled by the byte-1 flag.
static size_t mk_data_dsthash(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                              uint32_t dst_hash, const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 40> inner{};
    inner[0] = static_cast<uint8_t>(dst_hash);        inner[1] = static_cast<uint8_t>(dst_hash >> 8);
    inner[2] = static_cast<uint8_t>(dst_hash >> 16);  inner[3] = static_cast<uint8_t>(dst_hash >> 24);
    inner[4] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[5 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = DATA_FLAG_DST_HASH; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 5 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// DATA carrying an H_ANSWER (hash-bind) inner: resolves hb_key -> hb_node (authoritative=owner). Routed to
// `dst`; do_post_ack consumes it via on_hash_bind_response (drains a parked redirect/send for hb_key).
static size_t mk_data_hashbind(uint8_t next, uint8_t dst, uint16_t ctr,
                               uint8_t hb_node, uint32_t hb_key, bool authoritative,
                               std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 16> inner{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = hb_node; hb.key_hash32 = hb_key;   // 6-B inner; authoritative via TYPE
    const size_t il = pack_hash_bind_inner(hb, std::span<uint8_t>(inner.data(), inner.size()));
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = 0; in.next = next; in.dst = dst;
    in.type = authoritative ? DATA_TYPE_AUTHORITATIVE_H_ANSWER : DATA_TYPE_H_ANSWER;   // H_ANSWER rides the frame TYPE
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), il);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// §1b CRYPTED DATA: [dst_hash LE 4][origin][body] inner + the CRYPTED flag + an 8-B nonce-seed TRAILER (which
// IS the dedup key after §1b). The inner body is a stand-in: the dedup runs in handle_data BEFORE any open, and
// a forwarder (dst != self) re-tx's a sealed frame verbatim without ever opening it — so no valid seal is needed
// to exercise the dedup/loop path. CRYPTED requires DST_HASH (pack_data rejects CRYPTED && !DST_HASH).
static size_t mk_data_crypted(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin, uint32_t dst_hash,
                              const uint8_t seed8[8], const char* body, std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 40> inner{};
    inner[0] = uint8_t(dst_hash);       inner[1] = uint8_t(dst_hash >> 8);
    inner[2] = uint8_t(dst_hash >> 16); inner[3] = uint8_t(dst_hash >> 24);
    inner[4] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[5 + bl] = uint8_t(body[bl]); ++bl; }
    data_in in{}; in.addr_len = 0; in.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 5 + bl);
    in.mac = std::span<const uint8_t>(seed8, 8);                  // the 8-B nonce-seed (conditional MAC trailer)
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// Minimal beacon FROM `src` (one throwaway entry) — installs a DIRECT hops=1
// route to `src` on the receiver, so a send to `src` has a usable next hop.
static size_t mk_beacon(uint8_t src, std::array<uint8_t, 64>& b) {
    beacon_entry e{}; e.dest = 200; e.next = 201; e.score_bucket = 12; e.is_gateway = false; e.hops = 2;
    beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x1234;
    in.entries = std::span<const beacon_entry>(&e, 1);
    return pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_cts(uint8_t rx_id, uint8_t tx_id, uint8_t data_sf, std::array<uint8_t, 8>& b) {
    cts_in in{}; in.chosen_data_sf = data_sf; in.already_received = false; in.tx_id = tx_id; in.rx_id = rx_id;
    return pack_cts(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_ack_hint(uint8_t to, uint8_t ctr_lo, uint8_t budget_hint, std::array<uint8_t, 8>& b) {
    ack_in in{}; in.ctr_lo = ctr_lo; in.budget_hint = budget_hint; in.snr_bucket = 0; in.to = to;
    return pack_ack(in, std::span<uint8_t>(b.data(), b.size()));
}
static size_t mk_ack(uint8_t to, uint8_t ctr_lo, std::array<uint8_t, 8>& b) {
    ack_in in{}; in.ctr_lo = ctr_lo; in.budget_hint = 0; in.snr_bucket = 0; in.to = to;
    return pack_ack(in, std::span<uint8_t>(b.data(), b.size()));
}
static CmdResult send_cmd(Node& node, uint8_t dst, const char* body) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.flags = 0;
    c.body = reinterpret_cast<const uint8_t*>(body);
    c.body_len = static_cast<uint8_t>(std::strlen(body));
    return node.on_command(c);
}

}  // namespace

TEST_CASE("R3 dedup — seen-origins ROLLS (evict oldest) at the 256 cap instead of refusing the new key") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);

    // Fill to the cap with DISTINCT keys recorded at INCREASING times -> key `base+0` is the oldest (min expiry).
    const uint32_t base = 0x01020300u;
    for (uint16_t i = 0; i < protocol::cap_seen_origins; ++i)
        node.record_seen_origin(base + i, /*from=*/2, /*now=*/uint64_t(1000 + i));
    CHECK(node.seen_origin_count() == protocol::cap_seen_origins);   // 256, all live (TTL 30s)
    CHECK(node.seen_origin_live(base + 0, /*now=*/2000));            // the oldest is present (not expired)

    // One more NEW key past the cap -> ROLL: the oldest (base+0) is evicted, the new key stored, count stays 256.
    const uint32_t fresh = base + protocol::cap_seen_origins;
    node.record_seen_origin(fresh, /*from=*/3, /*now=*/uint64_t(1000 + protocol::cap_seen_origins));
    CHECK(node.seen_origin_count() == protocol::cap_seen_origins);   // STILL 256 — rolled, not grown, not refused
    CHECK_FALSE(node.seen_origin_live(base + 0, /*now=*/2000));      // the oldest was evicted...
    CHECK(node.seen_origin_live(fresh, /*now=*/2000));              // ...and the NEW key IS recorded (the fix)
    CHECK(node.seen_origin_live(base + 1, /*now=*/2000));          // only ONE evicted — the 2nd-oldest survived
}

TEST_CASE("R3 receiver — RTS -> CTS -> DATA -> delivered (we are the destination)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };   // immediate sender = bob(1)

    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb, 0, /*origin=*/0, /*ctr=*/0x0005);
    CHECK(rn > 0);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    CHECK(hal.count("rts_rx") == 1);
    const Ev* cts = hal.last("cts_tx");
    CHECK(cts != nullptr);
    if (cts) { CHECK(cts->to == 1); CHECK_FALSE(cts->dup); }

    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/0, "hi", db);
    CHECK(dn > 0);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    CHECK(hal.count("data_rx") == 1);
    CHECK(hal.count("ack_tx") == 1);
    node.on_timer(kPostAckTimerId);                          // deliver is deferred by the ACK airtime
    const Ev* dlv = hal.last("delivered");
    CHECK(dlv != nullptr);
    if (dlv) { CHECK(dlv->has_payload); CHECK(dlv->payload == "hi"); }
}

// Collect every `delivered` payload in order, so a duplicate can be distinguished from a second delivery
// rather than merely counted. ⛔ §B153's regressions assert THESE, never a counter or a flag.
static std::vector<std::string> delivered_payloads(const TestHal& hal) {
    std::vector<std::string> v;
    for (const auto& e : hal.events) if (e.type == "delivered" && e.has_payload) v.push_back(e.payload);
    return v;
}

TEST_CASE("inbox integration — a delivered DM is recorded durably + pushed, fields consistent") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    node.inbox().on_init(&dm, &ch);                          // a backend installs durable stores
    CHECK(node.inbox().enabled());
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };  // immediate sender = bob(1)

    std::array<uint8_t, 16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb, 0, /*origin=*/0, /*ctr=*/0x0005), meta);
    std::array<uint8_t, 64> db{};
    hal._now = 2000; node.on_recv(db.data(), mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/0, "hi", db), meta);
    node.on_timer(kPostAckTimerId);                          // do_post_ack: msg_recv push + record_dm together

    // 1) the live push ring received the DM
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);

    // 2) the durable inbox recorded exactly one DM, consistent with that push
    CHECK(dm.count() == 1);
    CHECK(ch.count() == 0);                                  // a DM does not touch the channel store
    struct Got { bool seen; InboxKind kind; uint8_t origin; uint32_t msg_id; std::string body; } g{ false, InboxKind::channel, 0, 0, "" };
    node.inbox().pull(0, 0, [](void* c, const InboxEntry& e) -> bool {
        auto* x = static_cast<Got*>(c);
        x->seen = true; x->kind = e.kind; x->origin = e.origin; x->msg_id = e.msg_id;
        x->body.assign(reinterpret_cast<const char*>(e.body ? e.body : reinterpret_cast<const uint8_t*>("")), e.body_len);
        return true;
    }, &g);
    CHECK(g.seen);
    CHECK(g.kind == InboxKind::dm);
    CHECK(g.body == "hi");                                   // the delivered content
    if (got) { CHECK(g.origin == pu.origin); CHECK(g.msg_id == pu.ctr);   // DM msg_id == the ctr; same source (do_post_ack)
               CHECK(g.body == std::string(reinterpret_cast<const char*>(pu.body), pu.body_len)); }
}

// =============================================================================
// ★★★★ [[B153]] — THE RTS-TIME `already_received` GATE IS RETIRED. Six regressions (QA-specified).
//
// ★★★ THE ARGUMENT, because these tests only make sense against it: **a 7-byte RTS cannot distinguish a RETRY
// of message A from the FIRST ATTEMPT of message B sharing the same `(hop src, dst, ctr_lo, payload_len)` —
// those frames are BYTE-IDENTICAL.** No receiver algorithm can return a safe TERMINAL `already_received` from
// one. ⇒ **RTS AUTHORIZES RECEPTION; ONLY DATA PROVES MESSAGE IDENTITY.** A free receiver always CTSes and
// waits for the DATA; `handle_data`'s `_seen_origins` (full `(origin,dst,ctr)` / the whole 8-B nonce-seed) is
// the sole authority.
//
// ⓘ REPLACES the former case "R3 dedup — retried RTS within last_acked TTL -> already_received CTS; past TTL
// -> fresh CTS", which PINNED THE RETIRED BEHAVIOUR. It is not deleted quietly: test 2 below is the same
// scenario asserting the NEW contract (normal CTS, duplicate DATA, ONE delivery).
// ⛔ Assertions are on OBSERVABLE side effects — delivered payloads, parsed ACK/NACK frames on the wire —
// never a bare flag. Test 6 is the anti-vacuity control and proves 2 and 3 can fail.
// =============================================================================

// The ACKs this node actually put on the air, parsed back off the wire (never a counter).
static std::vector<ack_out> acks_on_wire(const TestHal& hal) {
    std::vector<ack_out> v;
    for (const auto& f : hal.tx_frames)
        if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x4)
            if (auto a = parse_ack(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) v.push_back(*a);
    return v;
}
// ★★ THE CTSes THIS NODE ACTUALLY PUT ON THE AIR, parsed back off the wire.
// ⛔ DO NOT assert `Ev::dup` for this: the fresh-CTS site emits only ("to","sf") — it carries NO `dup` field —
// so `CHECK_FALSE(ev->dup)` is VACUOUSLY TRUE and stays true even if the wire bit is set. That is not a
// hypothetical: mutation M-B153B-5 (re-emitting `already_received = true` on the normal CTS) left every one of
// these cases GREEN until they were re-pointed at the wire. Assert the BIT, never the telemetry field.
static std::vector<cts_out> ctses_on_wire(const TestHal& hal) {
    std::vector<cts_out> v;
    for (const auto& f : hal.tx_frames)
        if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x2)
            if (auto c = parse_cts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) v.push_back(*c);
    return v;
}
static std::vector<nack_out> nacks_on_wire(const TestHal& hal) {
    std::vector<nack_out> v;
    for (const auto& f : hal.tx_frames)
        if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x5)
            if (auto n = parse_nack(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) v.push_back(*n);
    return v;
}

// ---- 1. THE EXACT s27 [[B153]] COLLISION: two ORIGINS, one relay, identical old-RTS tuple -> BOTH deliver ---
TEST_CASE("[[B153]]/1 the s27 collision — two DIFFERENT ORIGINS relayed by one node with an identical "
          "(src,dst,ctr_lo,payload_len) both DELIVER, and no CTS claims already_received") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };          // the RELAY (gateway) is node 1

    // s27 measured *"two XL handoffs with identical (dst=101, ctr=2) and DIFFERENT origins (114 and 111)"* — so
    // the counters are IDENTICAL, not merely congruent mod 16, and the ORIGIN is the only separating field.
    // Two hosted mobiles' independent counters colliding is the ordinary case: every peer counter starts alike.
    struct F { uint8_t origin; uint16_t ctr; const char* body; };
    const F f1{114, 0x0022, "re-m1"}, f2{111, 0x0022, "re-m3"};
    CHECK(f1.ctr == f2.ctr);                                         // premises asserted, not assumed
    CHECK(f1.origin != f2.origin);

    uint64_t t = 1000;
    for (const F& f : {f1, f2}) {
        std::array<uint8_t, 16> rb{};                                // src=1 (the RELAY), same dst/ctr_lo/plen
        const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, uint8_t(f.ctr & 0x0F), /*plen=*/15, rb, 0, /*origin=*/f.origin, /*ctr=*/int(f.ctr));
        CHECK(rn == 10);                                             // ⛔ CORRECTED 2026-08-08 (§hybrid-rts S1): the
                                                                     // RTS is now TEN bytes and CARRIES the identity.
                                                                     // ⚠ THIS CASE STILL MEANS WHAT IT MEANT: the two
                                                                     // injected RTS frames remain identical to each other
                                                                     // (`mk_rts` derives its placeholder from src/ctr_lo,
                                                                     // which ARE equal here), so the relay still cannot
                                                                     // separate them from the RTS in S1 — the DATA-level
                                                                     // dedup is still the sole authority. S2 is what makes
                                                                     // the two tails differ.
        hal._now = t; node.on_recv(rb.data(), rn, meta);
        std::array<uint8_t, 64> db{};
        const size_t dn = mk_data(/*next=*/2, /*dst=*/2, f.ctr, f.origin, f.body, db);
        hal._now = t + 500; node.on_recv(db.data(), dn, meta);
        node.on_timer(kPostAckTimerId);
        t += 2000;                                                   // inside the OLD 10 s last_acked window
    }
    CHECK(hal._now < 1000 + protocol::last_acked_ttl_ms);            // the retired gate WOULD have been live here

    // ⛔ ON THE WIRE: two CTSes, and NEITHER carries `already_received`. Read the BIT (see ctses_on_wire).
    const std::vector<cts_out> ctses = ctses_on_wire(hal);
    CHECK(ctses.size() == 2);
    for (const auto& c : ctses) CHECK_FALSE(c.already_received);
    const std::vector<std::string> got = delivered_payloads(hal);
    CHECK(got.size() == 2);
    if (got.size() == 2) { CHECK(got[0] == "re-m1"); CHECK(got[1] == "re-m3"); }
}

// ---- 2. PLAINTEXT ACK LOSS: DATA twice at MAC, delivery exactly once, the sender is ACKed --------------------
TEST_CASE("[[B153]]/2 plaintext lost-ACK recovery — the DATA arrives TWICE, the application is delivered "
          "EXACTLY ONCE, and the retry is ACKed on the wire so the sender completes") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    const size_t rn = mk_rts(1, 2, 2, /*ctr_lo=*/5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    const size_t dn = mk_data(2, 2, /*ctr=*/0x0005, /*origin=*/1, "hi", db);

    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    hal._now = 1500; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(delivered_payloads(hal).size() == 1);

    // Our ACK was lost -> the sender retries the WHOLE exchange 5 s later.
    // ★★★ §hybrid-rts S2 (2026-08-08) — THE EXPECTATION IS DELIBERATELY FLIPPED HERE TOO, and this case is the
    // headline of the restored optimisation: the retry is answered with the canonical 6-B TERMINAL CTS carrying
    // the exact plaintext identity `origin|ctr_hi|ctr_lo`, so the sender completes WITHOUT re-flying the DATA.
    // ⛔ WHAT MUST STILL HOLD, AND IS ASSERTED BELOW: the application is delivered EXACTLY ONCE. That was this
    //    case's reason for existing under B153 and it does not change — only the mechanism that achieves it does.
    hal._now = 6500; node.on_recv(rb.data(), rn, meta);
    { const std::vector<cts_out> c = ctses_on_wire(hal);
      CHECK(c.size() == 2);                      // a SECOND CTS went out...
      CHECK_FALSE(c[0].already_received);        // the first exchange was ordinary
      CHECK(c[1].already_received);              // ★ ...and it claims prior receipt, on EVIDENCE
      CHECK(c[1].id.domain == RtsIdDomain::plaintext);
      CHECK(c[1].id.width == 3);                 // a 6-B terminal frame
      CHECK(rts_flight_identity_equal(c[1].id, rts_flight_identity_plain(1, 0x0005)));
      CHECK_FALSE(c[1].team_plane); }
    hal._now = 7000; node.on_recv(db.data(), dn, meta);   // if the sender re-flies it anyway, it is simply refused
    node.on_timer(kPostAckTimerId);

    CHECK(hal.count("data_rx") == 1);            // ★ the redundant DATA never gets a reservation
    // ★ ...and delivered ONCE. This is the assertion the whole design rests on.
    const std::vector<std::string> got = delivered_payloads(hal);
    CHECK(got.size() == 1);
    if (got.size() == 1) CHECK(got[0] == "hi");
    const std::vector<ack_out> acks = acks_on_wire(hal);
    CHECK(acks.size() == 1);                     // one real ACK; the retry completed on the TERMINAL CTS instead
    for (const auto& a : acks) { CHECK(a.to == 1); CHECK(a.ctr_lo == 5); }
    CHECK(nacks_on_wire(hal).empty());           // a same-prev-hop duplicate is NOT a loop
}

// ---- 3. ENCRYPTED ACK LOSS: the duplicate is recognised by the FULL NONCE-SEED identity ----------------------
TEST_CASE("[[B153]]/3 CRYPTED lost-ACK recovery — the duplicate is recognised by the FULL 8-B NONCE-SEED, so "
          "the post-ACK open runs EXACTLY ONCE; a different seed is a different message") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    const uint8_t S1[8] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88 };
    const uint8_t S2[8] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x89 };   // ONE bit different, in the last byte
    // ★ §hybrid-rts S2: the RTS that authorises a CRYPTED flight carries THAT flight's identity
    // (`BLAKE2b-512(0xE1|seed|ctr_hi|ctr_lo|dst)[:4]`), so the S1-seed and S2-seed messages need DIFFERENT RTSes —
    // which is the point: they ARE different messages, and the wire now says so before the DATA arrives.
    std::array<uint8_t, 16> rb{};  const size_t rn  = mk_rts(1, 2, 2, /*ctr_lo=*/5, 20, rb,  0, -1, /*ctr=*/0x0005, S1);
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(1, 2, 2, /*ctr_lo=*/5, 20, rb2, 0, -1, /*ctr=*/0x0005, S2);
    std::array<uint8_t, 64> d1{}, d2{};
    // dst_hash = OUR OWN hash, so the frame is for us and the post-ACK path attempts the open. ⓘ A hand-built
    // seal cannot decrypt, so the post-ACK ACTION observable is `e2e_open_no_key` — which is reached ONLY
    // through `_post_ack`, i.e. only when the dedup did NOT short-circuit. That is exactly what must happen once.
    const size_t n1 = mk_data_crypted(2, 2, /*ctr=*/0x0005, /*origin=*/1, node.key_hash32(), S1, "hi", d1);
    const size_t n2 = mk_data_crypted(2, 2, /*ctr=*/0x0005, /*origin=*/1, node.key_hash32(), S2, "hi", d2);

    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    hal._now = 1500; node.on_recv(d1.data(), n1, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("e2e_open_no_key") == 1);                  // the post-ACK open ran once

    // ACK lost -> the sender re-flies the SAME sealed message: same ctr AND the same nonce-seed (preserved
    // verbatim across a requeue — that is what makes the seed a message identity).
    // ★★★ §hybrid-rts S2 (2026-08-08) — THIS EXPECTATION IS DELIBERATELY FLIPPED, AND THE FLIP IS THE SLICE.
    // Under B153 this retry got an ORDINARY CTS and the sender had to re-fly the whole DATA; the assertion here
    // used to be `CHECK_FALSE(already_received)` on both CTSes. The completed-flight cache now answers the SECOND
    // RTS with the canonical TERMINAL CTS — 7 B for a crypted flight — carrying the full identity echo. That is
    // legitimate now and was not legitimate then, for one reason only: the RTS carries the identity, so the
    // receiver is answering *this* flight rather than guessing from `(src, dst, ctr_lo, len)`.
    hal._now = 6500; node.on_recv(rb.data(), rn, meta);
    { const std::vector<cts_out> c = ctses_on_wire(hal);
      CHECK(c.size() == 2);
      CHECK_FALSE(c[0].already_received);                      // the FIRST exchange is ordinary
      CHECK(c[1].already_received);                            // ★ the retry is answered TERMINALLY
      CHECK(c[1].id.domain == RtsIdDomain::crypted);           // ...at the CRYPTED width (7-B frame)
      CHECK(c[1].id.width == 4);
      CHECK(rts_flight_identity_equal(c[1].id, rts_flight_identity_crypted(S1, 0x0005, 2)));   // the EXACT echo
      CHECK_FALSE(c[1].team_plane); }                          // a static flight echoes plane 0
    // ⓘ AND THE REDUNDANT DATA NO LONGER HAS TO FLY. The terminal CTS allocated NO `PendingRx`, so if the sender
    // ignored it and re-flew the DATA anyway that frame is refused at the reservation gate — no second `data_rx`,
    // no second ACK, and above all NO second delivery. The safety property this case was written for
    // ("the application is delivered EXACTLY ONCE") is asserted three ways below.
    hal._now = 7000; node.on_recv(d1.data(), n1, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("data_rx") == 1);                          // ★ the redundant DATA is not even admitted
    CHECK(hal.count("e2e_open_no_key") == 1);                  // ★ opened ONCE. No second delivery attempt.
    { const std::vector<ack_out> acks = acks_on_wire(hal);
      CHECK(acks.size() == 1);                                 // the TERMINAL CTS, not an ACK, is what completes the retry
      for (const auto& a : acks) { CHECK(a.to == 1); CHECK(a.ctr_lo == 5); } }

    // ★★ THE DIFFERENTIAL CONTROL that makes the above mean "the SEED is the identity" rather than "anything
    // repeated is suppressed": same origin, same dst, same ctr — ONE different seed byte -> a DIFFERENT message,
    // a DIFFERENT RTS identity, therefore a cache MISS, an ORDINARY CTS and a real second delivery.
    hal._now = 8000; node.on_recv(rb2.data(), rn2, meta);
    { const std::vector<cts_out> c = ctses_on_wire(hal);
      CHECK(c.size() == 3);
      CHECK_FALSE(c[2].already_received); }                    // ★ one seed bit ⇒ NO terminal answer
    hal._now = 8500; node.on_recv(d2.data(), n2, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("e2e_open_no_key") == 2);                  // it is NOT deduped -> the open runs again
    CHECK(hal.count("data_rx") == 2);
}

// ---- 4. TWO DIFFERENT MESSAGES WITH IDENTICAL OLD RTS TUPLES: the ctr_lo-WRAP shape ------------------------
TEST_CASE("[[B153]]/4 one origin whose 4-bit ctr_lo has WRAPPED — two different messages with an IDENTICAL old "
          "RTS tuple both deliver (the half 'just add the origin to the key' would still have lost)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    const uint8_t kOrigin = 114;
    const uint16_t c1 = 0x0002, c2 = 0x0012;                   // 2 and 18 -> the SAME 4-bit ctr_lo
    CHECK((c1 & 0x0F) == (c2 & 0x0F));
    CHECK(c1 != c2);

    uint64_t t = 1000;
    const char* bodies[2] = { "wrap-a", "wrap-b" };             // equal length -> equal payload_len
    for (int k = 0; k < 2; ++k) {
        const uint16_t c = k ? c2 : c1;
        std::array<uint8_t, 16> rb{};
        const size_t rn = mk_rts(1, 2, 2, uint8_t(c & 0x0F), /*plen=*/16, rb, 0, /*origin=*/kOrigin, /*ctr=*/int(c));
        hal._now = t; node.on_recv(rb.data(), rn, meta);
        std::array<uint8_t, 64> db{};
        const size_t dn = mk_data(2, 2, c, kOrigin, bodies[k], db);
        hal._now = t + 500; node.on_recv(db.data(), dn, meta);
        node.on_timer(kPostAckTimerId);
        t += 2000;
    }
    CHECK(hal._now < 1000 + protocol::last_acked_ttl_ms);
    const std::vector<cts_out> ctses = ctses_on_wire(hal);           // the BIT, not the emit field
    CHECK(ctses.size() == 2);
    for (const auto& c : ctses) CHECK_FALSE(c.already_received);
    const std::vector<std::string> got = delivered_payloads(hal);
    CHECK(got.size() == 2);
    if (got.size() == 2) { CHECK(got[0] == "wrap-a"); CHECK(got[1] == "wrap-b"); }
}

// ---- 5. A DIFFERENT-PREV-HOP duplicate is still a LOOP, and still NACKed rather than ACKed ------------------
TEST_CASE("[[B153]]/5 the SAME message arriving via a DIFFERENT prev-hop still produces a LOOP_DUP NACK on the "
          "wire — not an ACK, and not a delivery") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    RxMeta from4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data(2, 2, /*ctr=*/0x0005, /*origin=*/9, "hi", db);   // ONE message, origin 9

    { std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(/*src=*/1, 2, 2, 5, 15, rb, 0, /*origin=*/9, /*ctr=*/0x0005);
      hal._now = 1000; node.on_recv(rb.data(), rn, from1); }
    hal._now = 1500; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(delivered_payloads(hal).size() == 1);
    const size_t acks_after_first = acks_on_wire(hal).size();
    CHECK(acks_after_first == 1);

    // the identical flight arrives again, relayed by a DIFFERENT neighbour (4) -> a mesh loop
    { std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(/*src=*/4, 2, 2, 5, 15, rb, 0, /*origin=*/9, /*ctr=*/0x0005);
      hal._now = 2000; node.on_recv(rb.data(), rn, from4); }
    hal._now = 2500; node.on_recv(db.data(), dn, from4);
    node.on_timer(kPostAckTimerId);

    const std::vector<nack_out> nacks = nacks_on_wire(hal);
    CHECK(nacks.size() == 1);
    if (nacks.size() == 1) { CHECK(nacks[0].reason == protocol::nack_reason_loop_dup);
                             CHECK(nacks[0].to == 4); CHECK(nacks[0].ctr_lo == 5); }
    CHECK(acks_on_wire(hal).size() == acks_after_first);        // ⛔ NO ACK for the looped copy
    CHECK(delivered_payloads(hal).size() == 1);                 // ...and no second delivery
}

// WI-4 (R3.x) concurrency micro-gate. The half-duplex single-flight invariant:
// a 2nd same-priority send enqueued WHILE a flight is in progress must NOT issue
// its RTS until the first flight completes and become_free re-drains the queue.
// This is a pure single-node _pending_tx-gate property — t86/t87 keep depth<=1,
// so only a hand-fed mid-flight ordering exercises it.
TEST_CASE("R3.x concurrency — 2nd send waits behind the in-flight one until become_free re-drains") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);   // self = alice(1)
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);

    // Seed a direct route to bob(2) so issue_send has a next hop.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    { bool saw_route_to_bob = false;      // the direct route to bob(2) the send needs
      for (const auto& ev : hal.events) if (ev.type == "rt_update" && ev.dst == 2) saw_route_to_bob = true;
      CHECK(saw_route_to_bob); }

    // Send #1 -> drains immediately (queue was idle) -> exactly one RTS.
    hal._now = 2000; send_cmd(node, /*dst=*/2, "msg-a");
    CHECK(hal.count("tx_enqueue") == 1);
    CHECK(hal.count("rts_tx")     == 1);

    // Send #2 mid-flight (pending_tx set) -> enqueued, but NO new RTS issues.
    hal._now = 2001; send_cmd(node, /*dst=*/2, "msg-b");
    CHECK(hal.count("tx_enqueue") == 2);
    CHECK(hal.count("rts_tx")     == 1);    // <-- the invariant: still 1, msg-b is queued
    const Ev* enq2 = hal.last("tx_enqueue");
    CHECK(enq2 != nullptr);
    if (enq2) CHECK(enq2->depth == 1);      // one item waiting behind the flight

    // Complete the first flight: CTS -> (gap) -> DATA -> ACK. Still no 2nd RTS
    // until the ACK lands and become_free re-drains.
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    CHECK(cn > 0);
    hal._now = 2100; node.on_recv(cb.data(), cn, bob);
    CHECK(hal.count("cts_rx") == 1);
    node.on_timer(kCtsToDataGapTimerId);                 // CTS->DATA gap fires -> DATA tx
    CHECK(hal.count("data_tx") == 1);
    CHECK(hal.count("rts_tx")  == 1);                    // msg-b STILL not issued

    std::array<uint8_t, 8> ab{};
    const size_t an = mk_ack(/*to=*/1, /*ctr_lo=*/1, ab);
    CHECK(an > 0);
    // ACK completes flight #1 -> become_free re-drains. Advance past dm_min_interval_ms from msg-a (now=2000)
    // so msg-b (an own DM) clears the Slice 3 burst floor and its RTS issues.
    hal._now = 5200; node.on_recv(ab.data(), an, bob);
    CHECK(hal.count("ack_rx") == 1);
    // ACK completes flight #1 -> become_free re-drains -> msg-b's RTS issues now.
    CHECK(hal.count("rts_tx") == 2);
    const Ev* rts2 = hal.last("rts_tx");
    CHECK(rts2 != nullptr);
    if (rts2) CHECK(rts2->ctr == 2);                     // the 2nd RTS is msg-b (ctr=2)
}

// P6 (R3.x) determinism golden: the retry-jitter range is identical-by-
// construction with the Lua. retry_jitter_ms = 3*airtime_routing(RTS_LEN=8).
// At routing SF8/BW125/CR5 that is 3*88 = 264 (Lua-verified). Pins node.cpp's
// use of the literal 8 + the x3 so a future wire shortening can't silently
// de-align the forced-retry mt19937 streams.
TEST_CASE("R3.x golden — retry_jitter_ms == 3*airtime_routing(RTS_LEN=8)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.radio_bw_hz = 125000; cfg.radio_cr = 5;
    node.on_init(cfg);
    CHECK(node.retry_jitter_ms() == 264);   // SF8/BW125/CR5

    TestHal hal7; Node node7(hal7, 1, 0);
    NodeConfig cfg7; cfg7.routing_sf = 7; cfg7.radio_bw_hz = 125000; cfg7.radio_cr = 5;
    node7.on_init(cfg7);
    CHECK(node7.retry_jitter_ms() == 132);  // SF7/BW125/CR5
}

TEST_CASE("§CTS-wait metal slop: start_rts_timeout adds 2*rx_window_slop_ms (metal turnaround); inert at slop=0") {
    // The CTS round-trip crosses TWO radio turnarounds (sender TX->RX + gateway RX->TX). start_rts_timeout must add
    // 2*rx_window_slop_ms — 0 on the sim/native HAL so the delay is UNCHANGED (native + s18 byte-identical), ~53ms/turnaround on metal.
    auto arm_cts_wait = [](uint32_t slop) -> uint32_t {
        TestHal hal; hal._slop = slop;
        Node node(hal, /*id=*/1, /*key=*/0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 7); cfg.leaf_id = 0;
        node.on_init(cfg);
        node.route_inject(/*dest*/ 20, /*next_hop*/ 20, /*hops*/ 1, /*score*/ 100);   // a direct route -> the send RTSes
        send_cmd(node, /*dst*/ 20, "hi");                                             // originate -> RTS -> start_rts_timeout arms kRtsTimeoutTimerId (4)
        return hal.last_after_delay[kRtsTimeoutTimerId];
    };
    const uint32_t d0 = arm_cts_wait(0);
    const uint32_t dK = arm_cts_wait(37);
    CHECK(d0 > 0);                          // the CTS-wait IS armed (base<<shift + 1)
    CHECK(dK == d0 + 2u * 37u);             // ★ the fix: +2 turnarounds of slop; slop==0 -> inert -> native + s18 unchanged
}

// ★★★ §hybrid-rts S1 item 8 / [[B158]] — THE CTS-WAIT PRICES THE FRAMES THAT ACTUALLY FLY.
// The wait used to be `airtime_routing_ms(8) + airtime_routing_ms(4)` — a PHANTOM 8-byte RTS for a 7-byte wire.
// S1 makes the unicast RTS 10 B (plaintext) / 11 B (crypted) and the possible terminal CTS 6/7 B, so the wait
// must price 10+6 / 11+7 or the protocol is UNDER-TIMED the moment the wire grows.
// ★ THE PHY IS CHOSEN, NOT ARBITRARY, AND THIS IS THE WHOLE POINT OF THE CASE. At the corpus's dominant
//   SF8/BW125k/CR5 the growth is +0 ms for a plaintext flight (a(10)==a(8)==88, a(6)==a(4)==78) — so a test
//   written there would PASS against the old formula and prove nothing. SF11/BW62.5k/CR5 separates them:
//   a(8)+a(4) = 1253+1089 = 2342 but a(10)+a(6) = 1417+1253 = 2670. Both values are asserted below, so the
//   case fails if either the new formula or the old one is used at the wrong place.
TEST_CASE("§hybrid-rts S1 / [[B158]] — start_rts_timeout prices the ACTUAL 10-B RTS + 6-B terminal CTS, not a(8)+a(4)") {
    constexpr uint8_t  kSf = 11;
    constexpr uint32_t kBw = 62500;
    constexpr uint8_t  kCr = 5;
    auto air = [](uint16_t len) { return airtime_ms(kSf, kBw, kCr, protocol::preamble_sym, len); };
    // premises asserted, not assumed — if the airtime model moves, this case says so instead of drifting
    CHECK(air(4)  == 1089); CHECK(air(8)  == 1253);
    CHECK(air(6)  == 1253); CHECK(air(10) == 1417);
    CHECK(air(7)  == 1253); CHECK(air(11) == 1417);
    const uint32_t old_base = air(8)  + air(4);                    // 2342 — the phantom-8 formula
    const uint32_t new_base = air(10) + air(6);                    // 2670 — plaintext 10-B RTS + 6-B terminal CTS
    CHECK(new_base != old_base);                                    // ★ the PHY really does discriminate

    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = kSf; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << kSf);
    cfg.leaf_id = 0; cfg.radio_bw_hz = kBw; cfg.radio_cr = kCr;
    node.on_init(cfg);
    node.route_inject(/*dest*/ 20, /*next_hop*/ 20, /*hops*/ 1, /*score*/ 100);
    send_cmd(node, /*dst=*/20, "hi");                               // a PLAINTEXT flight
    const uint32_t armed = hal.last_after_delay[kRtsTimeoutTimerId];
    CHECK(armed == new_base + 1u);                                  // shift 0, slop 0 -> (base << 0) + 2*0 + 1
    CHECK(armed != old_base + 1u);                                  // ⛔ and it is NOT the retired formula
    // the semantic helpers the site must be expressed through (a raw literal 10/6 would drift)
    CHECK(unicast_rts_wire_len(false)  == 10);
    CHECK(terminal_cts_wire_len(false) == 6);
    // ★ AND THE RTS THAT WENT OUT REALLY IS 10 BYTES — the timing and the wire agree, asserted on the frame
    //   itself rather than on the formula (an observable side effect, not a telemetry field).
    const auto* rts = hal.last_tx("RTS");
    CHECK(rts != nullptr);
    if (rts) { CHECK(rts->bytes.size() == 10);
               auto pr = parse_rts(std::span<const uint8_t>(rts->bytes.data(), rts->bytes.size()));
               CHECK(pr.has_value());
               if (pr) { CHECK(pr->id.domain == RtsIdDomain::plaintext);
                         // the identity the producer emitted IS the flight's canonical (origin, ctr)
                         CHECK(pr->id.bytes[0] == 1); } }
}

// The CRYPTED arm of the same site: an 11-B RTS + a 7-B terminal CTS. At SF11/BW62.5k/CR5 the plaintext and
// crypted bases coincide (a(11)==a(10) and a(7)==a(6)), so this case uses SF8/BW125k/CR5 where they do NOT:
// plaintext 88+78 = 166 but crypted 98+88 = 186 (§HYBRID-RTS-S0's +20 ms on s18's PHY).
TEST_CASE("§hybrid-rts S1 item 8 — a CRYPTED flight prices the 11-B RTS + 7-B terminal CTS (+20 ms on s18's PHY)") {
    constexpr uint8_t  kSf = 8;
    constexpr uint32_t kBw = 125000;
    constexpr uint8_t  kCr = 5;
    auto air = [](uint16_t len) { return airtime_ms(kSf, kBw, kCr, protocol::preamble_sym, len); };
    CHECK(air(4) == 78); CHECK(air(6) == 78); CHECK(air(8) == 88); CHECK(air(10) == 88);
    CHECK(air(7) == 88); CHECK(air(11) == 98);
    const uint32_t plain_base   = air(10) + air(6);                 // 166
    const uint32_t crypted_base = air(11) + air(7);                 // 186
    CHECK(crypted_base == plain_base + 20u);                        // ★ the PHY discriminates the two domains

    uint8_t seedA[32], seedB[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = static_cast<uint8_t>(i + 1); seedB[i] = static_cast<uint8_t>(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    TestHal hal; Node node(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = kSf; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << kSf);
    cfg.leaf_id = 0; cfg.radio_bw_hz = kBw; cfg.radio_cr = kCr;
    node.on_init(cfg);
    node.set_crypto_identity(idA.x_secret, idA.ed_pub);
    node.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    node.route_inject(/*dest*/ 20, /*next_hop*/ 20, /*hops*/ 1, /*score*/ 100);
    const uint8_t body[2] = { 'h', 'i' };
    CHECK(node.test_do_send_typed(/*dst=*/20, body, sizeof body, CryptIntent::on,
                                  /*override_dst_hash=*/idB.key_hash32, /*type=*/0) != 0);
    // ★ ASSERT THE FRAME, not the intent: an 11-B RTS is the only proof the flight really is CRYPTED here.
    const auto* rts = hal.last_tx("RTS");
    CHECK(rts != nullptr);
    if (rts) {
        CHECK(rts->bytes.size() == 11);
        auto pr = parse_rts(std::span<const uint8_t>(rts->bytes.data(), rts->bytes.size()));
        CHECK(pr.has_value());
        if (pr) CHECK(pr->id.domain == RtsIdDomain::crypted);
    }
    CHECK(hal.last_after_delay[kRtsTimeoutTimerId] == crypted_base + 1u);
    CHECK(hal.last_after_delay[kRtsTimeoutTimerId] != plain_base + 1u);          // ⛔ not the plaintext price
    CHECK(hal.last_after_delay[kRtsTimeoutTimerId] != air(8) + air(4) + 1u);     // ⛔ not the retired phantom-8 price
}

TEST_CASE("§per-layer-id: single-layer canonical_node_id() == node_id() (persist unchanged)") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    CHECK_FALSE(node.config().is_gateway);                 // single-layer node (not a gateway)
    CHECK(node.canonical_node_id() == node.node_id());     // single-layer persists _node_id (the current/DAD-adopted id) — UNCHANGED
    CHECK(node.canonical_node_id() == 7);
}

// ---- Cascade-to-alt walk + no-route defer+Q (the cascade milestone) --------
// Seed a sender (alice=1) with K candidates to dest=5 via distinct next-hops,
// ordered by hops so the candidate order is unambiguous (no score tie).
static Node* mk_sender_with_routes(TestHal& hal, std::vector<std::array<uint8_t,3>> vias) {
    // each via = {next_hop_src, hops_advertised, score_bucket}
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node->on_init(cfg);
    std::array<uint8_t,64> bb{};
    for (auto& v : vias) {
        RxMeta m{12.0f, -70.0f, 0, static_cast<int8_t>(v[0])};
        const size_t n = mk_beacon_route(/*src=*/v[0], /*dest=*/5, /*next=*/9, /*hops=*/v[1], /*score=*/v[2], bb);
        node->on_recv(bb.data(), n, m);
    }
    return node;
}

TEST_CASE("cascade — primary RTS exhausts -> walk to the alternate candidate") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // via2 (h2) primary, via3 (h3) alt
    send_cmd(*node, /*dst=*/5, "hi");
    const Ev* r1 = hal.last("rts_tx"); CHECK(r1 != nullptr);
    if (r1) CHECK(r1->next == 2);                       // first RTS to the primary (via 2)
    const int rand_before = hal.rand_calls;
    exhaust_rts_same_hop(*node);                        // primary fails -> cascade
    CHECK(hal.count("path_cascade") == 1);
    const Ev* pc = hal.last("path_cascade"); CHECK(pc != nullptr);
    if (pc) { CHECK(pc->next == 3); CHECK(pc->dst == 5); }   // walked to the alternate (via 3)
    const Ev* r2 = hal.last("rts_tx"); CHECK(r2 != nullptr);
    if (r2) CHECK(r2->next == 3);                       // re-RTS on the alt
    CHECK(hal.count("rts_giveup") == 0);                // walked, did not give up
    // DETERMINISM (spec risk #1): exactly rts_max_retries(3) same-hop retry-jitter
    // draws on the primary, and ZERO on the cascade switch. An extra draw here would
    // de-align the lua/meshroute mt19937 streams.
    CHECK(hal.rand_calls - rand_before == 3);
    delete node;
}

TEST_CASE("cascade — full K=3 walk: primary -> alt1 -> alt2, then exhaustion requeues") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14},{4,3,14}});  // via2,via3,via4
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);                        // via2 -> via3
    exhaust_rts_same_hop(*node);                        // via3 -> via4
    CHECK(hal.count("path_cascade") == 2);
    const Ev* pc = hal.last("path_cascade"); if (pc) CHECK(pc->next == 4);
    exhaust_rts_same_hop(*node);                        // via4 fails, no untried candidate -> requeue
    CHECK(hal.count("cascade_requeue") == 1);
    const Ev* rq = hal.last("cascade_requeue"); if (rq) CHECK(rq->requeue_count == 1);
    CHECK(hal.count("rts_giveup") == 0);                // requeued, not yet given up
    delete node;
}

TEST_CASE("cascade — single candidate: requeue caps then a true giveup") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2
    hal._now = 1000;                                    // enqueue_time; age cap is 60000 from here
    send_cmd(*node, 5, "hi");
    // Each requeue holds the flight by next_attempt_ms (backoff 5000/10000/20000);
    // advance the clock past each before re-draining (must stay < 61000 so the COUNT
    // cap — not the age cap — is what finally gives up).
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 1);
    hal._now = 6000;  node->on_timer(kCascadeRequeueTimerId);
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 2);
    hal._now = 16000; node->on_timer(kCascadeRequeueTimerId);
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 3);
    hal._now = 36000; node->on_timer(kCascadeRequeueTimerId);
    CHECK(hal.count("rts_giveup") == 0);                // still no giveup after 3 requeues
    exhaust_rts_same_hop(*node);                        // requeue_count==3 -> count cap -> giveup
    CHECK(hal.count("path_cascade_exhausted") == 1);
    CHECK(hal.count("rts_giveup") == 1);
    delete node;
}

TEST_CASE("cascade — single candidate: the total-AGE cap gives up before the count cap") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);  CHECK(hal.count("cascade_requeue") == 1);   // requeue_count=1 (< max 3)
    // Jump past the original enqueue + total-age cap, then re-fly: the age cap fires
    // (now - enqueue_time_ms(1000) >= 60000) even though requeue_count is only 1.
    hal._now = 1000 + protocol::cascade_requeue_total_max_ms + 1;
    node->on_timer(kCascadeRequeueTimerId);            // re-drain -> re-fly
    exhaust_rts_same_hop(*node);                       // exhausts -> try_cascade_requeue -> age cap -> giveup
    CHECK(hal.count("cascade_requeue") == 1);          // NOT a 2nd requeue
    CHECK(hal.count("path_cascade_exhausted") == 1);
    CHECK(hal.count("rts_giveup") == 1);
    delete node;
}

// ===== Slice 6: slow-reprobe interception on a one-way sole route =====
// A one-way next-hop stays liveness-HEALTHY (its beacons keep arriving) so §P3 never
// fires on it; without the bidi interception the no-alt giveup would burst into the
// 9-80-RTS try_cascade_requeue. The interception throttles to ONE RTS per
// link_reprobe_ttl_ms while still flying the single sole-route probe.
TEST_CASE("bidi reprobe — a one-way sole route fires its FIRST probe immediately (clock starts at 0)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to dst 5 via next-hop 2
    // Mark next-hop 2 one-way: advertiser 2's complete heard-set OMITS self(1) -> Slice 3 detection sets one_way.
    node->test_update_link_bidi_from_beacon(/*advertiser=*/2, /*entries=*/nullptr, /*n=*/0, /*complete=*/true);
    hal._now = 5000;
    send_cmd(*node, /*dst=*/5, "hi");
    const int rts_before = hal.count("rts_tx");
    exhaust_rts_same_hop(*node);                           // no alt -> one-way interception
    CHECK(hal.count("link_reprobe") == 1);                 // the single throttled probe fired
    // exhaust_rts_same_hop fires 3 same-hop retry RTSs before the cascade; the interception then adds
    // exactly ONE probe RTS (+4 total). The load-bearing point: ONE probe, no try_cascade_requeue burst.
    CHECK(hal.count("rts_tx") == rts_before + 4);          // 3 retries + the single probe, no burst
    CHECK(hal.count("cascade_requeue") == 0);              // the burst requeue was suppressed
    delete node;
}

TEST_CASE("bidi reprobe — one probe per link_reprobe_ttl_ms; non-one-way keeps the legacy requeue burst") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2
    node->test_update_link_bidi_from_beacon(/*advertiser=*/2, nullptr, 0, /*complete=*/true);  // 2 -> one_way
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);                           // probe #1
    CHECK(hal.count("link_reprobe") == 1);
    CHECK(hal.count("cascade_requeue") == 0);              // NO burst
    // A second giveup WITHIN the TTL window must NOT re-probe (throttled, clean giveup, no burst).
    hal._now = 1000 + protocol::link_reprobe_ttl_ms - 1;
    send_cmd(*node, 5, "hi2");
    exhaust_rts_same_hop(*node);
    CHECK(hal.count("link_reprobe") == 1);                 // STILL 1 -> throttled
    CHECK(hal.count("cascade_requeue") == 0);              // still no burst
    // A giveup AFTER the TTL probes again.
    hal._now = 1000 + protocol::link_reprobe_ttl_ms + 1;
    send_cmd(*node, 5, "hi3");
    exhaust_rts_same_hop(*node);
    CHECK(hal.count("link_reprobe") == 2);                 // window elapsed -> a fresh probe

    // CONTROL: a sole route whose next-hop is NOT one_way still takes the legacy requeue burst (no regression).
    TestHal hal2;
    Node* n2 = mk_sender_with_routes(hal2, {{2,1,14}});    // 2 left unknown (never marked one_way)
    hal2._now = 1000;
    send_cmd(*n2, 5, "hi");
    exhaust_rts_same_hop(*n2);
    CHECK(hal2.count("link_reprobe") == 0);                // bidi plane not engaged
    CHECK(hal2.count("cascade_requeue") == 1);             // legacy burst path intact
    delete node; delete n2;
}

TEST_CASE("bidi reprobe — the single probe flies, a CTS recovers (confirmed + degraded cleared + link_recover)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2
    node->test_update_link_bidi_from_beacon(/*advertiser=*/2, nullptr, 0, /*complete=*/true);  // 2 -> one_way
    // Sanity: the sole candidate to dst 5 reads degraded while 2 is one_way.
    // rt_find is private — locate the entry via the public rt_count()/rt_at() seams.
    auto find_rt = [&](uint8_t dest) -> const RtEntry* {
        for (uint8_t i = 0; i < node->rt_count(); ++i) if (node->rt_at(i).dest == dest) return &node->rt_at(i);
        return nullptr;
    };
    const RtEntry* e = find_rt(5);
    CHECK(e != nullptr);
    if (e) {
        CHECK(e->n == 1);
        if (e->n == 1) CHECK(node->candidate_degraded(e->candidates[0]) == true);
    }
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    const int rts_before = hal.count("rts_tx");
    exhaust_rts_same_hop(*node);                           // one-way interception -> ONE probe RTS to 2
    // +4 = 3 same-hop retries (exhaust_rts_same_hop) + the single lucky-marginal probe (no burst).
    CHECK(hal.count("rts_tx") == rts_before + 4);          // the probe actually flew (its rts_tx is the +1 over the retries)
    const Ev* probe = hal.last("rts_tx");
    CHECK(probe != nullptr);
    if (probe) CHECK(probe->next == 2);
    // The probe gets a real CTS from next-hop 2 -> recovery.
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);                      // CTS matched -> note_link_confirmed(2)
    CHECK(hal.count("link_recover") == 1);                 // it WAS one_way -> recovery emitted
    const RtEntry* e2 = find_rt(5);
    CHECK(e2 != nullptr);
    if (e2) {
        CHECK(e2->n == 1);
        if (e2->n == 1) CHECK(node->candidate_degraded(e2->candidates[0]) == false);   // recompute is live -> degraded cleared
    }
    delete node;
}

TEST_CASE("bidi reprobe — §P3 liveness-silent path is orthogonal (RREQ + requeue, no link_reprobe)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2; 2 stays _link_bidi=unknown
    send_cmd(*node, 5, "hi");
    // Drive next-hop 2 to liveness-SILENT (>= peer_silent_penalty_q4) WITHOUT touching the bidi plane.
    // mark_neighbor_silent_for_test does not exist; use the real liveness path (3 same-hop giveups -> SILENT,
    // the proven §P3 idiom). peer_penalty_q4 is the public read accessor for the private liveness_penalty_q4.
    node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9);
    CHECK(node->peer_penalty_q4(2) >= protocol::peer_silent_penalty_q4);   // 2 is now SILENT
    const int rreq_before = hal.count("r_tx");             // §P3 RREQ event is r_tx (emit_route_request)
    node->on_timer(kRtsTimeoutTimerId);                    // silent + no alt -> §P3 RREQ + legacy requeue
    CHECK(hal.count("link_reprobe") == 0);                 // bidi plane NOT engaged (2 is unknown, not one_way)
    CHECK(hal.count("r_tx") > rreq_before);                // §P3 RREQ fired (orthogonal, unaffected)
    CHECK(hal.count("cascade_requeue") == 1);              // legacy requeue path intact
    delete node;
}

TEST_CASE("cascade — ACK-timeout resets the flight (re-RTS) before walking") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});
    send_cmd(*node, 5, "hi");                           // RTS to via 2
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);                   // CTS -> gap timer
    node->on_timer(kCtsToDataGapTimerId);               // -> DATA tx (awaiting_ack)
    CHECK(hal.count("data_tx") == 1);
    const int rts_before = hal.count("rts_tx");
    node->on_timer(kAckTimeoutTimerId);                 // ACK lost: reset awaiting flags, arm backoff
    node->on_timer(kRetryBackoffTimerId);               // -> re-RTS (NOT stuck in awaiting_ack)
    CHECK(hal.count("rts_tx") == rts_before + 1);       // the flight re-RTS'd on the same hop (via 2)
    const Ev* rr = hal.last("rts_tx"); if (rr) CHECK(rr->next == 2);
    delete node;
}

TEST_CASE("cascade — ACK-timeout exhaustion walks to the alternate (Risk #5 full path)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});
    send_cmd(*node, 5, "hi");
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);               // -> DATA (awaiting_ack, retries_left=3)
    CHECK(hal.count("data_tx") == 1);
    // First ACK-timeout resets to a re-RTS (retries 3->2); the remaining failures are
    // RTS-timeouts (the flight is back in awaiting_cts) until retries_left==0 -> cascade.
    node->on_timer(kAckTimeoutTimerId);  node->on_timer(kRetryBackoffTimerId);   // 3->2, re-RTS
    node->on_timer(kRtsTimeoutTimerId);  node->on_timer(kRetryBackoffTimerId);   // 2->1, re-RTS
    node->on_timer(kRtsTimeoutTimerId);  node->on_timer(kRetryBackoffTimerId);   // 1->0, re-RTS
    node->on_timer(kRtsTimeoutTimerId);                                          // 0 -> cascade
    CHECK(hal.count("path_cascade") == 1);
    const Ev* pc = hal.last("path_cascade"); if (pc) CHECK(pc->next == 3);       // walked to the alt
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 3);                // fresh RTS on the alt (awaiting_cts)
    delete node;
}

TEST_CASE("defer — originator send with no route is held, then drained when a route appears") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    send_cmd(node, /*dst=*/5, "later");                 // no route -> DEFER (not drop)
    CHECK(hal.count("send_deferred") == 1);
    CHECK(hal.count("send_no_route") == 0);             // originator defers, never drops
    CHECK(hal.count("rts_tx") == 0);
    // a beacon installs a route to 5 -> drain-on-rt_changed -> the held send flies
    std::array<uint8_t,64> bb{}; RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    const size_t n = mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), n, m2);
    CHECK(hal.count("rts_tx") >= 1);
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 2);
}

TEST_CASE("defer — TTL-first giveup: a held send with no route ages out on the periodic drain") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    send_cmd(node, 5, "lost");
    CHECK(hal.count("send_deferred") == 1);
    hal._now = 1000 + protocol::send_defer_ttl_ms + 1;  // past the defer TTL, still no route
    node.on_timer(kDeferredDrainTimerId);               // periodic drain -> TTL giveup (checked BEFORE route-exists)
    CHECK(hal.count("send_deferred_giveup") == 1);
    CHECK(hal.count("rts_tx") == 0);
}

// ★ The defer queue FULL path (node_cascade.cpp defer_send, `send_deferred_refused`). It is exercised by NO
// simulation scenario in the corpus, and on a board the MR_TELEMETRY emit beside it is STRIPPED
// (-DMESHROUTE_NO_TELEMETRY), so the send_failed Push is the ONLY signal the companion ever sees — which is why
// it shipping without a reason (rendering as a `send_failed` with the "reason" key absent) was invisible.
TEST_CASE("defer — a FULL defer queue REFUSES the new send and reports reason queue_full (not a bare send_failed)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    // Fill the queue with cap_deferred_sends unrouted originator sends. Nothing ever transmits (no route), so
    // _last_dm_origin_ms is never stamped and the DM burst floor never arms -> every send reaches defer_send.
    for (unsigned i = 0; i < protocol::cap_deferred_sends; ++i)
        send_cmd(node, static_cast<uint8_t>(20 + i), "held");
    CHECK(hal.count("send_deferred") == static_cast<int>(protocol::cap_deferred_sends));
    CHECK(hal.count("send_deferred_refused") == 0);                 // not yet: the queue is exactly full
    { Push drain{}; while (node.next_push(drain)) {} }               // clear the ring so the next push is ours
    send_cmd(node, /*dst=*/200, "one too many");                     // full -> REFUSE the NEW send (never drop-oldest)
    CHECK(hal.count("send_deferred_refused") == 1);
    CHECK(hal.count("send_deferred") == static_cast<int>(protocol::cap_deferred_sends));   // the refused one is NOT held
    Push p{}; bool failed = false; SendFailReason reason = SendFailReason::none;
    while (node.next_push(p)) if (p.kind == PushKind::send_failed) { failed = true; reason = p.reason; break; }
    CHECK(failed);
    CHECK(reason == SendFailReason::queue_full);                     // was SendFailReason::none -> a reason-less push
    CHECK(p.dst == 200);
}

TEST_CASE("defer — TTL-FIRST beats route-exists: past-TTL held send gives up even when a route arrives") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    hal._now = 1000;
    send_cmd(node, 5, "trapped");                       // deferred (no route)
    CHECK(hal.count("send_deferred") == 1);
    // Age PAST the TTL, THEN install a route. The beacon triggers try_drain_deferred,
    // which checks TTL BEFORE route-exists -> the send gives up, it does NOT fly. This
    // is the s12 defer_ttl_route_exists_trap guard: a route arriving past-TTL must not
    // resurrect a stale held send.
    hal._now = 1000 + protocol::send_defer_ttl_ms + 1;
    std::array<uint8_t,64> bb{}; RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    const size_t n = mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), n, m2);                     // route to 5 now exists, but TTL already passed
    CHECK(hal.count("send_deferred_giveup") == 1);      // TTL-first -> giveup
    CHECK(hal.count("rts_tx") == 0);                    // did NOT fly despite the fresh route
}

// ---- NACK plane (BUSY_RX + LOOP_DUP) ---------------------------------------
TEST_CASE("nack — BUSY_RX emit: a 2nd-flight RTS into a busy receiver gets a NACK") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    const size_t rn = mk_rts(/*src=*/2, /*next=*/1, /*dst=*/9, /*ctr_lo=*/5, /*plen=*/10, rb);
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; node.on_recv(rb.data(), rn, m2);   // flight A -> CTS + pending_rx
    CHECK(hal.count("cts_tx") >= 1);
    const size_t rn2 = mk_rts(/*src=*/3, /*next=*/1, /*dst=*/8, /*ctr_lo=*/7, /*plen=*/10, rb);
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 1100; node.on_recv(rb.data(), rn2, m3);  // flight B (different) -> BUSY_RX NACK to 3
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == 0); }
}

TEST_CASE("nack — a busy SENDER (pending_tx) stays SILENT (no NACK)") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                            // pending_tx via 2
    std::array<uint8_t,16> rb{};
    const size_t rn = mk_rts(3, 1, 8, 7, 10, rb); RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    node->on_recv(rb.data(), rn, m3);
    CHECK(hal.count("nack_tx") == 0);                   // SILENT while sending (busy_for would lie)
    CHECK(hal.count("rts_drop_pending_tx") == 1);
    delete node;
}

TEST_CASE("nack — BUSY_RX recovery (short busy): mark blind + nack_wait re-RTS SAME hop, ONE new draw") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                            // pending_tx via 2, ctr_lo=1
    const int rts_before = hal.count("rts_tx");
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/1, /*reason=*/0, /*payload=*/10, nb);  // busy 160ms <= 2000
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("nack_rx") == 1);
    CHECK(hal.count("blind_observed") == 1);            // via 2 marked blind
    CHECK(hal.count("tx_requeued") == 0);               // short busy -> wait, NOT requeue
    CHECK(hal.rand_calls - rand_before == 1);           // N1: exactly ONE new draw
    node->on_timer(kNackWaitTimerId);                   // wait elapsed -> re-RTS SAME hop
    CHECK(hal.count("rts_tx") == rts_before + 1);
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 2);   // same hop (BUSY_RX never path-switches)
    delete node;
}

TEST_CASE("nack — BUSY_RX recovery (long busy): blind + requeue, the re-issue SKIPS the blind hop") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});  // via2 primary, via3 alt
    send_cmd(*node, 5, "hi");                            // pending_tx via 2
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, 0, /*payload=*/200, nb);   // busy 3200ms > 2000 -> requeue
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("blind_observed") == 1);            // via 2 blind
    CHECK(hal.count("tx_requeued") == 1);               // long busy -> requeue (next_attempt=0 -> re-issues now)
    const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
    if (r) CHECK(r->next == 3);                          // is_blind(2) -> the re-issue picks via 3, not the blind via 2
    delete node;
}

TEST_CASE("nack — LOOP_DUP recovery: cascade to the alternate (NO new draw)") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{4,2,14}});  // via2 primary, via4 alt
    send_cmd(*node, 5, "hi");                            // pending_tx via 2
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, /*reason=*/3, /*payload=*/9, nb);   // LOOP_DUP
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("path_cascade") == 1);
    CHECK(hal.count("tx_loop_alt") == 1);
    const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 4);   // cascaded to via 4
    CHECK(hal.rand_calls - rand_before == 0);           // the LOOP_DUP re-RTS draws NO jitter
    delete node;
}

TEST_CASE("nack — LOOP_DUP miss: DIRECT giveup, NOT requeue (Lua dv:10588)") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2
    send_cmd(*node, 5, "hi");
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, 3, 9, nb); RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("path_cascade_exhausted") == 1);
    CHECK(hal.count("rts_giveup") == 1);
    CHECK(hal.count("cascade_requeue") == 0);           // DIRECT giveup, NOT a requeue
    delete node;
}

TEST_CASE("nack — LOOP_DUP emit: same flight via a different prev-hop NACKs the sender") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};                        // merge(1) needs a route to dst=5 to forward
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // copy 1 via b1=2: RTS+DATA (origin0,dst5,ctr10) -> ACK + record seen_origin_from=2 + (forward pending)
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,10,rb,0,/*origin=*/0,/*ctr=*/10); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data(1,5,10,0,"x",db); node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("ack_tx") >= 1);
    // copy 2 via b2=3: SAME (origin,dst,ctr) -> prev-hop 3 != recorded 2 -> LOOP_DUP NACK to 3, NO ACK
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    const int ack_before = hal.count("ack_tx");
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,10,10,rb,0,/*origin=*/0,/*ctr=*/10); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data(1,5,10,0,"x",db); node.on_recv(db.data(), dn, m3); }
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == 3); }
    CHECK(hal.count("dup_drop") >= 1);
    CHECK(hal.count("ack_tx") == ack_before);           // the looped dup was NOT re-ACKed
}

// =============================================================================
// §1b sealed-sender — the CRYPTED dedup key is the 8-B nonce-seed, NOT the
// cleartext (origin,dst,ctr). After §1c seals `origin` the relay can no longer
// read it; the seed (globally unique per message, preserved verbatim on
// forward) is the flight id. These pin that BEFORE 1c so the wire change is a
// one-liner. The DISTINGUISHING test (same header, different seed) is the RED
// driver: under the old origin-keyed dedup copy-2 would false-LOOP_DUP.
// =============================================================================
TEST_CASE("§1b CRYPTED dedup keys on the seed — same (origin,dst,ctr) but a DIFFERENT seed is a DISTINCT flight") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};                         // a route to dst=5 so the forwarder forwards (dedup is pre-forward anyway)
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    const uint8_t S1[8] = { 0xAA,0x01,0x02,0x03, 0x04,0x05,0x06,0x07 };
    const uint8_t S2[8] = { 0xBB,0x11,0x12,0x13, 0x14,0x15,0x16,0x17 };   // DIFFERENT seed, SAME (origin,dst,ctr)
    // copy 1 via prev-hop 2: a CRYPTED forward (origin 0, dst 5, ctr 10), seed S1 -> accepted + records seen(seed1, from=2)
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,18,rb,0,-1,/*ctr=*/10,S1); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_crypted(1,5,10,/*origin=*/0,/*dst_hash=*/0xDEADBEEFu, S1, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("ack_tx") >= 1);
    CHECK(node.seen_origin_count() == 1);                // one flight recorded so far
    const int nack_before = hal.count("nack_tx");
    const int dup_before  = hal.count("dup_drop");
    // copy 2 via prev-hop 3: SAME (origin,dst,ctr) but a DIFFERENT seed S2 -> a SEPARATE message -> NOT a loop-dup.
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,10,18,rb,0,-1,/*ctr=*/10,S2); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S2, "x", db); node.on_recv(db.data(), dn, m3); }
    CHECK(hal.count("nack_tx") == nack_before);          // NO LOOP_DUP — the seed differs (RED before 1b: origin-key would loop)
    CHECK(hal.count("dup_drop") == dup_before);          // not dropped as a dup
    CHECK(node.seen_origin_count() == 2);                // a SECOND distinct flight recorded (vs the retransmit's 1) — the seed IS the key
}

TEST_CASE("§1b CRYPTED loop detection: the SAME seed via a DIFFERENT prev-hop -> LOOP_DUP NACK") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    const uint8_t S[8] = { 0xC0,0xC1,0xC2,0xC3, 0xC4,0xC5,0xC6,0xC7 };
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};                      // copy 1 via prev-hop 2 -> accepted, records seen(seed, from=2)
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,18,rb,0,-1,/*ctr=*/10,S); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("ack_tx") >= 1);
    const int ack_before = hal.count("ack_tx");
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};                      // copy 2: the SAME seed (a forwarded loop) via prev-hop 3 -> LOOP_DUP
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,10,18,rb,0,-1,/*ctr=*/10,S); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m3); }
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == 3); }               // nack_reason_loop_dup
    CHECK(hal.count("dup_drop") >= 1);
    CHECK(hal.count("ack_tx") == ack_before);                            // the loop was NOT re-ACKed
}

TEST_CASE("§1b CRYPTED retransmit: the SAME seed is a live dup (ONE flight entry), NOT a new flight or a LOOP_DUP") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    const uint8_t S[8] = { 0xD0,0xD1,0xD2,0xD3, 0xD4,0xD5,0xD6,0xD7 };
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    // copy 1 (seed S via prev-hop 2, pl=18) -> accepted, records exactly ONE seen-origin entry (the seed-key).
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,18,rb,0,-1,/*ctr=*/10,S); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(node.seen_origin_count() == 1);
    const int nack_before = hal.count("nack_tx");
    // copy 2: the SAME seed via the SAME prev-hop 2. The RTS uses a DIFFERENT payload_len (20) so the last-acked cache
    // MISSES (its key includes payload_len) and the DATA actually REACHES the seed-dedup — otherwise the RTS layer
    // absorbs the retry and the dedup never runs (the vacuity the review caught). SAME prev-hop => benign, not a loop.
    hal._now = 2000; { const size_t rn = mk_rts(2,1,5,10,20,rb,0,-1,/*ctr=*/10,S); node.on_recv(rb.data(), rn, m2); }
    hal._now = 2100; { const size_t dn = mk_data_crypted(1,5,10,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m2); }
    CHECK(node.seen_origin_count() == 1);               // DEDUPED on the seed: NO new flight entry (a broken seed-key => 2)
    CHECK(hal.count("nack_tx") == nack_before);         // same prev-hop => benign dup, NOT a LOOP_DUP
}

// The CRYPTED seed key and the plaintext (origin,dst,ctr) key share ONE _seen_origins map. They must NEVER alias:
// CRYPTED keys are namespaced into [2^63, 2^64) (top bit forced) and plaintext into [0, 2^32). A crafted seed whose
// low bytes equal a live plaintext key must NOT be mistaken for that flight.
TEST_CASE("§1b cross-type non-collision: a CRYPTED seed aliasing a plaintext (origin,dst,ctr) is a DISTINCT flight") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // a PLAINTEXT flight: origin=0x12, dst=5, ctr=0x5678 -> plaintext sokey = (0x12<<24)|(5<<16)|0x5678 = 0x12055678.
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,/*ctr_lo=*/8,18,rb,0,/*origin=*/0x12,/*ctr=*/0x5678); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data(1,5,/*ctr=*/0x5678,/*origin=*/0x12,"x",db); node.on_recv(db.data(), dn, m2); }
    CHECK(node.seen_origin_count() == 1);
    const int nack_before = hal.count("nack_tx");
    // a CRYPTED frame whose 8-B seed's LOW 4 bytes (LE) == 0x12055678 and high 4 == 0 -> the OLD 32-bit fold collides
    // with the plaintext key; the 64-bit type-namespaced key does NOT.
    const uint8_t S[8] = { 0x78,0x56,0x05,0x12, 0x00,0x00,0x00,0x00 };
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 2000; { const size_t rn = mk_rts(3,1,5,/*ctr_lo=*/8,18,rb,0,-1,/*ctr=*/0x5678,S); node.on_recv(rb.data(), rn, m3); }
    hal._now = 2100; { const size_t dn = mk_data_crypted(1,5,/*ctr=*/0x5678,0,0xDEADBEEFu, S, "x", db); node.on_recv(db.data(), dn, m3); }
    CHECK(hal.count("nack_tx") == nack_before);         // NO false LOOP_DUP — disjoint namespaces (RED under the 32-bit fold)
    CHECK(node.seen_origin_count() == 2);               // recorded as a DISTINCT flight, not aliased onto the plaintext one
}

// =============================================================================
// S1 (2026-07-04) — txitem_from_pending: the ONE place a TxItem is rebuilt from
// an in-flight PendingTx on requeue. These lock the field-drop class shut: the
// H4/M7 bugs were requeue sites that FORGOT `type` (a typed frame re-flown as a
// junk plain DM) or the 8-B CRYPTED `nonce_seed` (a sealed DM re-flown with a
// zero seed -> recipient Poly1305 tag-fail -> hard delivery loss under exactly
// the congestion that triggers a requeue). RED before the helper existed.
// =============================================================================
TEST_CASE("S1 helper — txitem_from_pending preserves a CRYPTED nonce_seed (not zeroed) across a requeue") {
    PendingTx pt{};
    pt.origin = 0x12; pt.dst = 5; pt.ctr = 0x5678; pt.ctr_lo = 8;
    pt.flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH;
    const uint8_t seed[8] = { 0xDE,0xAD,0xBE,0xEF, 0x01,0x02,0x03,0x04 };   // non-zero: a real XChaCha nonce seed
    for (int i = 0; i < 8; ++i) pt.nonce_seed[i] = seed[i];
    pt.inner[0] = 0xAA; pt.inner[1] = 0xBB; pt.inner_len = 2;

    const TxItem it = txitem_from_pending(pt);

    bool seed_ok = true;
    for (int i = 0; i < 8; ++i) if (it.nonce_seed[i] != seed[i]) seed_ok = false;
    CHECK(seed_ok);                                     // the H4 drop: seed survives -> recipient can still open the DM
    CHECK(it.flags == (DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH));
    // sanity: the seed is genuinely non-zero (guards against a test that would pass on an all-zero copy)
    bool any_nonzero = false;
    for (int i = 0; i < 8; ++i) if (it.nonce_seed[i] != 0) any_nonzero = true;
    CHECK(any_nonzero);
}

TEST_CASE("S1 helper — txitem_from_pending preserves a typed frame's DataType across a requeue") {
    PendingTx pt{};
    pt.origin = 3; pt.dst = 9; pt.ctr = 0x1111; pt.ctr_lo = 4;
    pt.type = DATA_TYPE_E2E_ACK;                        // a typed frame (ack/response) — NOT a plain DM
    pt.flags = DATA_FLAG_APP;
    pt.inner[0] = 0x11; pt.inner[1] = 0x00; pt.inner_len = 2;

    const TxItem it = txitem_from_pending(pt);

    CHECK(it.type == DATA_TYPE_E2E_ACK);                // the M7 drop: NOT downgraded to 0 (a junk plain DM, ack lost)
    CHECK(it.type != 0);
}

TEST_CASE("S1 helper — txitem_from_pending copies the full identity + hop-budget core") {
    PendingTx pt{};
    pt.origin = 7; pt.dst = 21; pt.ctr = 0x9ABC; pt.ctr_lo = 12; pt.flags = 0x40; pt.type = 5;
    pt.has_previous_hop = true; pt.previous_hop = 42;   // a relayed item -> is_forward + previous_hop
    pt.is_gw_relay = true;                              // a cross-layer relay keeps RTS_FLAG_RELAY on the requeue
    pt.fwd_remaining = 6; pt.fwd_committed = 2;         // the carried hop budget
    pt.inner[0] = 1; pt.inner[1] = 2; pt.inner[2] = 3; pt.inner_len = 3;
    const uint8_t seed[8] = { 9,8,7,6,5,4,3,2 };
    for (int i = 0; i < 8; ++i) pt.nonce_seed[i] = seed[i];

    const TxItem it = txitem_from_pending(pt);

    CHECK(it.origin == 7); CHECK(it.dst == 21); CHECK(it.ctr == 0x9ABC); CHECK(it.ctr_lo == 12);
    CHECK(it.flags == 0x40); CHECK(it.type == 5);
    CHECK(it.is_forward == true); CHECK(it.previous_hop == 42);   // has_previous_hop -> is_forward
    CHECK(it.is_gw_relay == true);
    CHECK(it.fwd_remaining == 6); CHECK(it.fwd_committed == 2);
    CHECK(it.inner_len == 3);
    bool inner_ok = it.inner[0] == 1 && it.inner[1] == 2 && it.inner[2] == 3;
    CHECK(inner_ok);
    bool seed_ok = true; for (int i = 0; i < 8; ++i) if (it.nonce_seed[i] != seed[i]) seed_ok = false;
    CHECK(seed_ok);
    // the site meta is NOT copied by the helper (the caller applies it) -> defaults hold here
    CHECK(it.requeue_count == 0); CHECK(it.enqueue_time_ms == 0); CHECK(it.next_attempt_ms == 0);
}

// =============================================================================
// §B160 (2026-08-08) — THE PLANE MUST SURVIVE THE REQUEUE DOOR.
// `txitem_from_pending` did not copy `Plane`, so every requeue resurrected the
// flight as `Plane::AUTO`, which dispatches by `is_team_peer(dst)`. A GLOBAL
// flight to a dst that numerically COLLIDES a teammate's team id therefore came
// back a TEAM flight: routed on `_rt_team` and aired with `addr_len=1 /
// mobile_src=1 / src=team_local_id()` — the §team-parity T8 class (a static
// flight wearing team-plane wire marks, breaching A2/I2) re-entering after T8
// closed the origination door.
//
// ★ THESE ASSERT THE WIRE, NOT THE CARRIER. `CHECK(it.plane == GLOBAL)` would
// only prove the assignment was typed; the load-bearing observables are the
// RTS's `next` (which route table was consulted) and its `src`/`addr_len`/
// `mobile_src` triple (which plane the frame CLAIMS on air).
// The §18 collision setup is lifted verbatim from the "plane hard split" case
// below (U1): dst 50 is BOTH a static route via 60 and a direct team peer.
// =============================================================================
namespace {
// A team-capable mobile with the §18 numeric collision on dst 50:
//   _rt      : 50 -> next 60   (route_inject)
//   _rt_team : 50 -> next 50   (learned from a same-team beacon)
// node_id 30 != team_local_id 93, so the RTS `src` alone discriminates the plane.
void b160_collision_node(Node& n, TestHal& hal) {
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(n.on_init(cfg));
    n.set_team_local_id(93);
    CHECK(n.node_id() == 30); CHECK(n.team_local_id() == 93);
    CHECK(n.route_inject(/*dest=*/50, /*next=*/60, /*hops=*/1, /*score=*/160));   // STATIC plane: 50 via 60
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(50)};
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=50; tb.key_hash32=0x5050u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    n.on_recv(b.data(), bn, meta);
    CHECK(n.is_team_peer(50));                    // TEAM plane knows 50 as a direct peer -> AUTO would dispatch team
    (void)hal;
}
CmdResult b160_send(Node& n, uint8_t dst, Plane plane) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.plane = static_cast<uint8_t>(plane);
    c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
    return n.on_command(c);
}
// The LAST unicast RTS on the wire, decoded. Returns false when there is none —
// so a case can prove its discriminator saw a frame at all (a parser that
// matches zero rows must itself be controlled).
bool b160_last_rts(const TestHal& hal, rts_out& out) {
    bool got = false;
    for (const auto& f : hal.tx_frames) {
        auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pr && !pr->m_broadcast) { out = *pr; got = true; }
    }
    return got;
}
}  // namespace

TEST_CASE("§B160 — ★★ a GLOBAL flight requeued (cascade-exhaustion) to a team-colliding dst STAYS GLOBAL on the wire") {
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    b160_collision_node(node, hal);
    hal._now = 1000;
    CHECK(b160_send(node, /*dst=*/50, Plane::GLOBAL).code == CmdCode::queued);
    // The FIRST flight is the control: GLOBAL routes on _rt (next 60) and wears NO team marks.
    { rts_out r{}; CHECK(b160_last_rts(hal, r));
      CHECK(r.next == 60); CHECK(r.src == 30); CHECK(r.addr_len == 0); CHECK_FALSE(r.mobile_src); }
    // Exhaust the single candidate -> cascade_to_alt finds no alt -> try_cascade_requeue (node_cascade.cpp:240).
    exhaust_rts_same_hop(node);
    CHECK(hal.count("cascade_requeue") == 1);            // the requeue actually happened (else the case is vacuous)
    hal.tx_frames.clear();
    hal._now = 6000; node.on_timer(kCascadeRequeueTimerId);   // past the backoff -> re-drain -> issue_send
    rts_out r{};
    CHECK(b160_last_rts(hal, r));                        // ★ the re-issue DID air an RTS (discriminator control)
    CHECK(r.next == 60);                                 // ★★ still the STATIC route — AUTO would have picked 50 via _rt_team
    CHECK(r.src == 30);                                  // ★★ still our node_id — AUTO+team would air team_local_id 93
    CHECK(r.addr_len == 0);                              // ★★ no team-plane addressing mark (T8's A2/I2 breach)
    CHECK_FALSE(r.mobile_src);                           // ★★ no LOCAL-id mark
}

TEST_CASE("§B160 — the converse: a genuine TEAM flight requeued STAYS TEAM (the fix is not a blanket GLOBAL-ification)") {
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    b160_collision_node(node, hal);
    hal._now = 1000;
    CHECK(b160_send(node, /*dst=*/50, Plane::TEAM).code == CmdCode::queued);
    { rts_out r{}; CHECK(b160_last_rts(hal, r));
      CHECK(r.next == 50); CHECK(r.src == 93); CHECK(r.addr_len == 1); CHECK(r.mobile_src); }
    exhaust_rts_same_hop(node);
    CHECK(hal.count("cascade_requeue") == 1);
    hal.tx_frames.clear();
    hal._now = 6000; node.on_timer(kCascadeRequeueTimerId);
    rts_out r{}; CHECK(b160_last_rts(hal, r));
    CHECK(r.next == 50);                                 // the TEAM route, preserved
    CHECK(r.src == 93); CHECK(r.addr_len == 1); CHECK(r.mobile_src);   // team-plane wire marks, preserved
}

TEST_CASE("§B160 — an AUTO flight requeued still dispatches by is_team_peer (unchanged: AUTO in, AUTO out)") {
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    b160_collision_node(node, hal);
    hal._now = 1000;
    CHECK(b160_send(node, /*dst=*/50, Plane::AUTO).code == CmdCode::queued);
    { rts_out r{}; CHECK(b160_last_rts(hal, r));
      CHECK(r.next == 50); CHECK(r.src == 93); }          // AUTO + is_team_peer(50) -> the team plane, as always
    exhaust_rts_same_hop(node);
    CHECK(hal.count("cascade_requeue") == 1);
    hal.tx_frames.clear();
    hal._now = 6000; node.on_timer(kCascadeRequeueTimerId);
    rts_out r{}; CHECK(b160_last_rts(hal, r));
    CHECK(r.next == 50); CHECK(r.src == 93);              // identical before and after the fix (AUTO copies as AUTO)
    CHECK(r.addr_len == 1); CHECK(r.mobile_src);
}

TEST_CASE("§B160 — the SECOND requeue site: the long-busy BUSY_RX NACK arm also preserves GLOBAL") {
    // node_mac_rx.cpp:1610 — a distinct call site with its own meta handling (verbatim requeue, next_attempt_ms=0
    // -> re-issues immediately, no timer). Covered separately because "three sites route through one helper" is a
    // claim about the source, not a measurement of any one site.
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    b160_collision_node(node, hal);
    // A SECOND static candidate: this arm BLINDS the current next-hop before requeuing, so with only one
    // candidate the re-issue would find no selectable static route and defer silently — no RTS to read, and
    // the case would prove nothing. 61 is the static fallback the fixed code must land on.
    // ⚠ test_learn_route, NOT route_inject: route_inject verifies its own write with the PLANE-BLIND
    // `rt_find(dest)` (node.h:699), which for a dst that is now a team peer reads `_rt_team` and so reports
    // false for a perfectly good `_rt` insert. The write happened; only the confirmation read was on the wrong
    // plane. Assert the STATIC table directly instead.
    node.test_learn_route(/*dest=*/50, /*via=*/61, /*hops=*/2, /*snr_q4=*/140, /*team_plane=*/false);
    { bool alt = false;
      for (uint8_t i = 0; i < node.rt_count(); ++i)
          if (node.rt_at(i).dest == 50)
              for (uint8_t c = 0; c < node.rt_at(i).n; ++c) if (node.rt_at(i).candidates[c].next_hop == 61) alt = true;
      CHECK(alt); }                                      // the static alt really is installed (control on the setup)
    hal._now = 1000;
    CHECK(b160_send(node, /*dst=*/50, Plane::GLOBAL).code == CmdCode::queued);
    rts_out first{}; CHECK(b160_last_rts(hal, first)); CHECK(first.next == 60);
    hal.tx_frames.clear();
    // BUSY_RX with payload 200 => 3200 ms > nack_wait_threshold_ms (2000) -> the REQUEUE arm, not the wait arm.
    // mobile_to=1 is required by handle_nack's gate on an is_mobile node (node_mac_rx.cpp:1537) — that gate is
    // orthogonal to this slice; it is set so the requeue arm is reachable at all.
    nack_in ni{}; ni.reason = protocol::nack_reason_busy_rx; ni.ctr_lo = first.ctr_lo; ni.payload = 200;
    ni.to = 30; ni.mobile_to = true;
    uint8_t nb[4]; const size_t nn = pack_nack(ni, std::span<uint8_t>(nb, sizeof nb));
    node.on_recv(nb, nn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(60)});   // src_hint = our next-hop 60
    CHECK(hal.count("tx_requeued") == 1);                // the long-busy arm ran (else vacuous)
    CHECK(hal.count("blind_observed") == 1);             // ...and it blinded via 60 first
    // The requeue sets next_attempt_ms=0, but become_free's OWN-DM floor (dm_min_interval_ms=3000, stamped at the
    // first origination at t=1000) defers it in place and arms kQueueWakeupTimerId — send_blocked, no RTS yet. Wake
    // at t=4100: past the 4000 floor, still inside the 4200 blind horizon for via 60, so the STATIC fallback 61 is
    // the pick. (Both bounds are load-bearing; move either and the case stops discriminating.)
    CHECK(hal.count("send_blocked") == 1);
    hal._now = 4100; node.on_timer(kQueueWakeupTimerId);
    rts_out r{}; CHECK(b160_last_rts(hal, r));
    CHECK(r.next == 61);                                 // ★★ the STATIC fallback — AUTO would have picked 50 via _rt_team
    CHECK(r.src == 30); CHECK(r.addr_len == 0); CHECK_FALSE(r.mobile_src);   // ★★ no team-plane wire marks
}

// =============================================================================
// Phase 0 (routing-liveness-plane port) — id==0 / 0-sentinel hardening. An
// UNPROVISIONED node (id 0) and the reserved id 0 must NEVER enter routing:
// id-0 nodes don't beacon, src-0 beacons are dropped, dest-0/via-0 candidates
// are rejected. (s18 has no id 0 -> these guards are inert -> byte-identical.)
// =============================================================================
TEST_CASE("§P0 — an id==0 (unprovisioned) node emits NO beacon (the broad emit_beacon guard)") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0x0000ABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; cfg.lbt_enabled = false;
    node.on_init(cfg);
    // learn a route from a VALID sender -> schedules a triggered beacon -> firing it reaches emit_beacon directly
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(7)});
    hal.tx_frames.clear();
    node.on_timer(kTriggeredBeaconTimerId);              // the triggered path bypasses periodic_beacon_fire's join guard
    node.on_timer(kBeaconTimerId);
    int n_bcn = 0; for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x0) ++n_bcn;
    CHECK(n_bcn == 0);                                   // an id==0 node never advertises routes
}
TEST_CASE("§P0 — a received BCN with src==0 is DROPPED (no route learned from the sentinel)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.rt_count() == 0);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(/*src=*/0, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(0)});
    CHECK(node.rt_count() == 0);                         // src==0 dropped: no direct route to 0, no DV route via next_hop 0
}
TEST_CASE("§P0 — a beacon ROUTE-ENTRY with dest==0 is rejected (never a route to the sentinel)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x0000BBBB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/0, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    node.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(7)});
    bool route_to_0 = false, route_to_7 = false;
    for (uint8_t i = 0; i < node.rt_count(); ++i) { if (node.rt_at(i).dest == 0) route_to_0 = true; if (node.rt_at(i).dest == 7) route_to_7 = true; }
    CHECK_FALSE(route_to_0);                             // the dest==0 entry was skipped
    CHECK(route_to_7);                                   // sanity: the DIRECT route to the sender(7) still learned (dest-specific guard, not a blanket drop)
}

// =============================================================================
// Phase 1 (routing-liveness port) — local liveness STATE. RTS/ACK-timeout
// giveups accumulate into suspect(1)/silent(3)/dead(6-over-15min) tiers; a frame
// heard from a peer clears it; dest_seen drives is_next_hop_fresh. DETECTION
// ONLY — not yet applied to routing (Phase 2). (state-only + new telemetry.)
// =============================================================================
TEST_CASE("§P1 peer-liveness STATE — timeout tiers (suspect/silent/dead) + clear + freshness") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    hal._now = 1000;
    CHECK(node.peer_suspect_level(9) == 0);                              // unknown -> healthy
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 1);   // 1 timeout -> SUSPECT (a full RTS-giveup is enough evidence to deprioritise)
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 1);   // 2 -> still SUSPECT (SILENT only at 3)
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 2);   // 3 -> SILENT
    node.clear_peer_suspect(9, "rx_frame"); CHECK(node.peer_suspect_level(9) == 0);   // heard from 9 -> cleared
    for (int i = 0; i < 6; ++i) node.record_peer_rts_timeout(9, 0);      // 6 timeouts, all at t=1000 -> evidence window NOT elapsed
    CHECK(node.peer_suspect_level(9) == 2);                              // -> SILENT, not yet DEAD
    hal._now = 1000 + protocol::peer_dead_evidence_window_ms + 1;        // past the 15-min evidence window
    node.record_peer_rts_timeout(9, 0); CHECK(node.peer_suspect_level(9) == 3);   // -> DEAD
    node.clear_peer_suspect(9, "rx_frame"); CHECK(node.peer_suspect_level(9) == 0);   // a clear resets even DEAD
    node.record_peer_rts_timeout(5, 0); CHECK(node.peer_suspect_level(5) == 0);   // self is never tiered
    node.record_peer_rts_timeout(0, 0); CHECK(node.peer_suspect_level(0) == 0);   // the 0 sentinel is never tiered
    // FRESHNESS (is_next_hop_fresh — defined, NOT consulted by routing in P1)
    hal._now = 5000000; node.mark_dest_seen(11);
    CHECK(node.is_next_hop_fresh(11));                                   // just seen -> fresh
    CHECK_FALSE(node.is_next_hop_fresh(99));                             // never seen -> not fresh
    CHECK(node.is_next_hop_fresh(5));                                    // self -> always fresh
    hal._now = 5000000 + protocol::next_hop_live_ttl_ms + 1;            // >20 min unseen
    CHECK_FALSE(node.is_next_hop_fresh(11));                             // gone stale
}
TEST_CASE("§seen-bitmap re-port — dest_seen freshness survives for >cap_peer_liveness distinct dests (dedicated map, no LRU loss)") {
    // The Lua dest_seen_ms is an unbounded node_id->ms map; the C++ used to piggyback it on the bounded
    // 64-entry PeerLiveness LRU table, so freshness for the (cap+1)-th..Nth distinct dest was EVICTED — which
    // starved the seen-bitmap benefit (gossip-only peers couldn't stay fresh). The dedicated _dest_seen_ms[256]
    // map has NO eviction: every marked dest stays fresh.
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    hal._now = 1000000;
    constexpr int kN = 100;                                  // > cap_peer_liveness (64)
    for (int id = 10; id < 10 + kN; ++id) node.mark_dest_seen(static_cast<uint8_t>(id));
    // ALL must read fresh — the bounded PeerLiveness LRU would have lost the first (kN - cap) of them.
    for (int id = 10; id < 10 + kN; ++id)
        CHECK(node.is_next_hop_fresh(static_cast<uint8_t>(id)));
    hal._now = 1000000 + protocol::next_hop_live_ttl_ms + 1;  // all age out together past the TTL
    for (int id = 10; id < 10 + kN; ++id)
        CHECK_FALSE(node.is_next_hop_fresh(static_cast<uint8_t>(id)));
}
TEST_CASE("§P1 liveness LRU — eviction keeps a DEAD peer over a healthy one (asymmetric-link safety)") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    // fill the table to cap with healthy peers (ids 10.., node 10 stamped FIRST -> oldest dest_seen -> the OLD policy's first victim)
    for (int i = 0; i < protocol::cap_peer_liveness; ++i) { hal._now = 1000 + i; node.mark_dest_seen(static_cast<uint8_t>(10 + i)); }
    // make node 10 DEAD (6 timeouts spanning the evidence window)
    hal._now = 2000; for (int i = 0; i < 6; ++i) node.record_peer_rts_timeout(10, 0);
    hal._now = 2000 + protocol::peer_dead_evidence_window_ms + 1; node.record_peer_rts_timeout(10, 0);
    CHECK(node.peer_suspect_level(10) == 3);
    // overflow: a NEW peer arrives -> table full -> eviction. The fix evicts a HEALTHY slot, NOT the dead one.
    node.mark_dest_seen(200);
    CHECK(node.peer_suspect_level(10) == 3);   // the DEAD peer SURVIVED (old min-dest_seen policy would have evicted it -> 0)
    CHECK(node.is_next_hop_fresh(200));        // the new peer got in (a healthy slot made room)
}

// =============================================================================
// Phase 2 (routing-liveness port) — APPLY the liveness penalty + freshness gate.
// A demoted (suspect/silent/dead) next-hop loses effective_score; a stale one is
// non-viable; on a tier change the routes via it are re-ranked -> traffic reroutes
// to a fresh alt. THIS IS BEHAVIORAL (the byte-identical gate gives way to the
// delivery suite). Recovery (a frame heard) restores the route.
// =============================================================================
namespace { int rt_primary_for(Node& n, uint8_t dest) {   // the current primary next-hop for `dest`, or -1
    for (uint8_t i = 0; i < n.rt_count(); ++i) if (n.rt_at(i).dest == dest) return n.rt_at(i).candidates[0].next_hop;
    return -1; } }
TEST_CASE("§P2 — a DEMOTED (silent) next-hop loses primacy to a fresh alt, and RECOVERS on a heard frame") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    hal._now = 1000; { const size_t n = mk_beacon_route(/*src=*/2, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb); node.on_recv(bb.data(), n, RxMeta{22.0f,-60.0f,0,static_cast<int8_t>(2)}); }
    hal._now = 1001; { const size_t n = mk_beacon_route(/*src=*/3, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }
    CHECK(rt_primary_for(node, 5) == 2);                    // strong link via 2 wins initially
    hal._now = 2000;                                        // node 2 goes SILENT (3 RTS-timeout giveups -> 40 dB penalty)
    node.record_peer_rts_timeout(2, 0); node.record_peer_rts_timeout(2, 0); node.record_peer_rts_timeout(2, 0);
    CHECK(node.peer_suspect_level(2) == 2);
    CHECK(rt_primary_for(node, 5) == 3);                    // §P2: the tier-promotion resort rerouted to the fresh alt via 3
    hal._now = 3000; node.clear_peer_suspect(2, "rx_frame");   // a frame heard from 2 -> it's alive -> clear
    CHECK(node.peer_suspect_level(2) == 0);
    CHECK(rt_primary_for(node, 5) == 2);                    // recovered -> the strong link regains primacy (clear re-ranked)
}
TEST_CASE("§P2 — a STALE next-hop (unseen > next_hop_live_ttl) is non-viable, loses to a fresh alt") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    hal._now = 1000; { const size_t n = mk_beacon_route(2, 5, 9, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{22.0f,-60.0f,0,static_cast<int8_t>(2)}); }   // strong via 2
    hal._now = 1001; { const size_t n = mk_beacon_route(3, 5, 9, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }    // weak via 3
    CHECK(rt_primary_for(node, 5) == 2);
    // advance past next_hop_live_ttl with NO frame from node 2 (-> stale); node 3 re-beacons (stays fresh) -> re-sort
    hal._now = 1000 + protocol::next_hop_live_ttl_ms + 1;
    { const size_t n = mk_beacon_route(3, 5, 9, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }   // keeps node 3 fresh
    CHECK_FALSE(node.is_next_hop_fresh(2));                 // node 2 went stale
    node.rt_resort_for_pick(5);                             // freshness is a PICK-TIME gate (Lua) -> force the re-sort a send would do
    CHECK(rt_primary_for(node, 5) == 3);                    // §P2: stale via-2 non-viable -> fresh via-3 wins (pure freshness, no penalty)
}

// =============================================================================
// Phase 3 (routing-liveness port) — the silent-next CASCADE reaction at the sender.
// A flight whose primary next-hop is ALREADY known silent/dead (prior-flight
// evidence) does NOT burn same-hop retries on the dead path — it cascades to a
// viable alt on the FIRST timeout (or, with no alt, fires an RREQ to actively
// rediscover, closing the user's no-alt dead-relay bug). Reads the persisted
// liveness tier; NO per-timeout counting (that churned the suite +17.8% events).
// DRIFT from the spec's literal per-failure/suspect trigger — see the phase report.
// =============================================================================
TEST_CASE("§P3 — a flight on an ALREADY-silent primary cascades to the alt on the FIRST timeout (no dead-path retries)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // via2 (h2) primary, via3 (h3) alt
    send_cmd(*node, /*dst=*/5, "hi");
    const Ev* r1 = hal.last("rts_tx"); CHECK(r1 != nullptr);
    if (r1) CHECK(r1->next == 2);                       // first RTS to the primary (via 2)
    // node 2 goes SILENT from OTHER evidence (3 prior giveups) WHILE this flight is in-air on it
    node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9);
    CHECK(node->peer_suspect_level(2) == 2);            // SILENT
    const int rand_before = hal.rand_calls;
    node->on_timer(kRtsTimeoutTimerId);                // FIRST timeout: primary is silent -> cascade NOW (don't retry)
    CHECK(hal.count("path_cascade") == 1);             // cascaded on the FIRST timeout (vs after 3 retries)
    const Ev* pc = hal.last("path_cascade"); if (pc) { CHECK(pc->next == 3); CHECK(pc->dst == 5); }
    const Ev* r2 = hal.last("rts_tx"); if (r2) CHECK(r2->next == 3);   // re-RTS on the fresh alt (via 3)
    CHECK(hal.count("rts_giveup") == 0);               // walked, did not give up
    CHECK(hal.rand_calls - rand_before == 0);          // NO same-hop retry-jitter draw (didn't retry the dead path)
    delete node;
}
TEST_CASE("§P3 — a silent SINGLE-candidate primary fires an RREQ on exhaustion (active no-alt rediscovery)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2 — no alt
    send_cmd(*node, /*dst=*/5, "hi");
    const Ev* r1 = hal.last("rts_tx"); if (r1) CHECK(r1->next == 2);
    node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9); node->record_peer_rts_timeout(2, 9);
    CHECK(node->peer_suspect_level(2) == 2);            // the sole next-hop is SILENT
    const int r_tx_before = hal.count("r_tx");
    node->on_timer(kRtsTimeoutTimerId);                // FIRST timeout: silent + no alt -> RREQ + requeue
    CHECK(hal.count("r_tx") == r_tx_before + 1);       // active rediscovery: an RREQ for the unreachable dst
    const Ev* rq = hal.last("r_tx"); if (rq) CHECK(rq->dst == 5);
    CHECK(hal.count("cascade_requeue") == 1);          // and the flight is requeued (not a hard giveup yet)
    delete node;
}
TEST_CASE("§P3 — a HEALTHY single-candidate primary does NOT fire an RREQ on a normal (congested) giveup") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // only via 2, healthy
    send_cmd(*node, /*dst=*/5, "hi");
    const int r_tx_before = hal.count("r_tx");
    exhaust_rts_same_hop(*node);                        // normal congested giveup (primary not silent)
    CHECK(hal.count("cascade_requeue") == 1);           // requeued as before
    CHECK(hal.count("r_tx") == r_tx_before);            // NO RREQ — the RREQ-on-silent is gated on the silent tier
    delete node;
}

// =============================================================================
// Phase 4 (routing-liveness port) — distributed liveness GOSSIP (BCN wire change).
// A node advertises its LOCALLY-observed silent/dead peers in a BCN suspect-TLV
// (type 1 ids-only / type 2 [id,state]); a receiver applies them as a REMOTE
// observation -> the mesh converges, not just the failing node. Anti-storm: a
// gossip-learned tier is applied but NEVER re-advertised (only local rts_timeout
// evidence populates the advertise window).
// =============================================================================
TEST_CASE("§P4 gossip ENCODE — a locally-SILENT peer -> type-1 id TLV; a locally-DEAD peer -> type-2 [id,state] TLV") {
    using meshroute::SuspectEntry;
    uint8_t buf[32]; SuspectEntry got[8];
    {   // SILENT-only -> type-1 SUSPECT_NODES (bare id list)
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
        hal._now = 1000;
        CHECK(node.test_build_suspect_ext(buf, sizeof(buf)) == 0);                 // nothing observed -> no TLV
        node.record_peer_rts_timeout(5, 0); node.record_peer_rts_timeout(5, 0); node.record_peer_rts_timeout(5, 0);
        CHECK(node.peer_suspect_level(5) == 2);
        const size_t n = node.test_build_suspect_ext(buf, sizeof(buf));
        CHECK(n == 1 + 1);
        CHECK((buf[0] >> 4) == protocol::bcn_ext_type_suspect_nodes);
        CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), got, 8) == 1);
        CHECK(got[0].node_id == 5); CHECK(got[0].state == 1);                      // type-1 applies as SUSPECT
    }
    {   // a DEAD peer present -> type-2 LIVENESS_STATE carrying the state byte
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
        hal._now = 1000;
        for (int i = 0; i < 6; ++i) node.record_peer_rts_timeout(6, 0);            // 6 at t=1000 -> SILENT (window not elapsed)
        hal._now = 1000 + protocol::peer_dead_evidence_window_ms + 1;
        node.record_peer_rts_timeout(6, 0);                                        // 7th past the window -> DEAD
        CHECK(node.peer_suspect_level(6) == 3);
        const size_t n = node.test_build_suspect_ext(buf, sizeof(buf));
        CHECK((buf[0] >> 4) == protocol::bcn_ext_type_liveness_state);
        CHECK(meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), got, 8) == 1);
        CHECK(got[0].node_id == 6); CHECK(got[0].state == 3);
    }
}
TEST_CASE("§P4 gossip APPLY — a remote DEAD demotes the local route to a fresh alt, WITHOUT first-hand evidence") {
    using meshroute::SuspectEntry;
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    hal._now = 1000; { const size_t n = mk_beacon_route(2, 9, 7, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{22.0f,-60.0f,0,static_cast<int8_t>(2)}); }  // strong via 2
    hal._now = 1001; { const size_t n = mk_beacon_route(3, 9, 7, 1, 14, bb); node.on_recv(bb.data(), n, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(3)}); }   // weak via 3
    CHECK(rt_primary_for(node, 9) == 2);
    SuspectEntry g[1] = { { 2, 3 } };                                              // GOSSIP from node 8: "node 2 is DEAD"
    hal._now = 2000; node.test_apply_suspect_gossip(g, 1, /*bcn_src=*/8);
    CHECK(node.peer_suspect_level(2) == 3);                                        // remote DEAD applied (no own timeout)
    CHECK(rt_primary_for(node, 9) == 3);                                          // -> route reroutes to the fresh alt via 3
}
TEST_CASE("§P4 anti-storm — a gossip-LEARNED tier is NOT re-advertised; only LOCAL evidence is; self/gossiper skipped") {
    using meshroute::SuspectEntry;
    uint8_t buf[32];
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    hal._now = 1000;
    // learn "2 dead" + "9 silent" via gossip from node 8
    SuspectEntry g[2] = { { 2, 3 }, { 9, 2 } };
    node.test_apply_suspect_gossip(g, 2, /*bcn_src=*/8);
    CHECK(node.peer_suspect_level(2) == 3);
    CHECK(node.test_build_suspect_ext(buf, sizeof(buf)) == 0);                     // ANTI-STORM: learned marks are NOT re-gossiped
    // a LOCAL observation IS advertised (proves the encoder works + isolates the anti-storm above)
    node.record_peer_rts_timeout(4, 0); node.record_peer_rts_timeout(4, 0); node.record_peer_rts_timeout(4, 0);   // 4 SILENT locally
    const size_t n = node.test_build_suspect_ext(buf, sizeof(buf));
    CHECK(n > 0);
    SuspectEntry got[8]; const uint8_t c = meshroute::parse_suspect_tlv(std::span<const uint8_t>(buf, n), got, 8);
    CHECK(c == 1); CHECK(got[0].node_id == 4);                                     // ONLY the locally-observed 4 — NOT the gossip-learned 2/9
    // self + gossiper are never marked
    SuspectEntry s[2] = { { 1, 3 }, { 7, 3 } };                                    // id 1 = self, src 7
    node.test_apply_suspect_gossip(s, 2, /*bcn_src=*/7);
    CHECK(node.peer_suspect_level(1) == 0);                                        // never self-mark
    CHECK(node.peer_suspect_level(7) == 0);                                        // never mark the gossiper
}

TEST_CASE("cascade — equal-score candidates keep INSERTION order (Lua-faithful, NO id tie-break)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    RxMeta m9{12.0f, -70.0f, 0, static_cast<int8_t>(9)};   // via 9 arrives FIRST
    RxMeta m4{12.0f, -70.0f, 0, static_cast<int8_t>(4)};   // via 4 arrives second
    size_t n;
    // advertised next=7 (a third party — not self=1, not the beacon senders 9/4,
    // else split-horizon would drop the route). Equal hops(1)+score(14) => a true tie.
    n = mk_beacon_route(9, 5, 7, 1, 14, bb); node.on_recv(bb.data(), n, m9);
    n = mk_beacon_route(4, 5, 7, 1, 14, bb); node.on_recv(bb.data(), n, m4);
    send_cmd(node, 5, "tie");
    const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
    // via 9 arrived first -> stays primary (insertion order), exactly like the Lua
    // (route_strictly_better returns false on a tie). An id tie-break would wrongly
    // pick via 4 and DIVERGE from the Lua reference.
    if (r) CHECK(r->next == 9);
}

// ---- HOP_BUDGET enforcement ------------------------------------------------
static std::optional<data_out> parse_tx_data(const TxFrame* d) {
    if (!d) return std::nullopt;
    return parse_data(std::span<const uint8_t>(d->bytes.data(), d->bytes.size()));
}

TEST_CASE("hop_budget — originator initial budget = min(31, rt_hops + slack)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,2,14}});   // candidate to 5 via 2: hops = 2+1 = 3
    send_cmd(*node, 5, "hi");                               // pending_tx via 2, ctr_lo=1
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    const int rand_before = hal.rand_calls;                // the budget block is pure arithmetic ...
    node->on_timer(kCtsToDataGapTimerId);                  // -> DATA tx
    CHECK(hal.rand_calls - rand_before == 0);              // ... no new draw (determinism golden, review #16)
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) {
        CHECK(pd->hops_remaining   == 6);                  // min(31, 3 + slack(3))
        CHECK(pd->committed_hops   == 0);
        CHECK(pd->prev_fwd_rt_hops == 3);                  // self's rt[5].hops, re-stamped
    }
    delete node;
}

TEST_CASE("hop_budget — forwarder with hops_remaining==0 NACKs HOP_BUDGET (no ACK, no forward)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(7, 5, 9, 1, 14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);   // route to 5
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2, 1, 5, 3, 10, rb, 0, /*origin=*/7, /*ctr=*/3); node.on_recv(rb.data(), rn, m2);       // CTS + pending_rx
    const int ack_before = hal.count("ack_tx"), data_before = hal.count("data_tx");
    const size_t dn = mk_data_hb(/*next=*/1, /*dst=*/5, /*ctr=*/3, /*origin=*/7,
                                 /*hops_remaining=*/0, /*committed=*/2, "x", db);
    node.on_recv(db.data(), dn, m2);
    CHECK(hal.count("hop_budget_exceeded") == 1);
    const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
    if (nk) { CHECK(nk->to == 2); CHECK(nk->reason == 2); }
    CHECK(hal.count("ack_tx") == ack_before);              // NO ACK (NACK in lieu of)
    node.on_timer(kPostAckTimerId);                        // (nothing armed)
    CHECK(hal.count("data_tx") == data_before);            // NO forward
}

TEST_CASE("hop_budget — destination is EXEMPT: hops_remaining==0 AT the dst delivers, no NACK") {
    TestHal hal; Node node(hal, 5, 0xABCD);                // self == the destination
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2, 5, 5, 3, 10, rb, 0, /*origin=*/7, /*ctr=*/3); node.on_recv(rb.data(), rn, m2);
    const size_t dn = mk_data_hb(/*next=*/5, /*dst=*/5, /*ctr=*/3, /*origin=*/7, 0, 0, "hi", db);
    node.on_recv(db.data(), dn, m2);
    CHECK(hal.count("ack_tx")  == 1);
    CHECK(hal.count("nack_tx") == 0);                      // dest exempt
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 1);
}

TEST_CASE("hop_budget — forwarder decrements: arriving remaining=2 forwards with remaining=1") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(7, 5, 9, 1, 14, bb);   // route to 5 via 7
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2, 1, 5, 3, 10, rb, 0, /*origin=*/7, /*ctr=*/3); node.on_recv(rb.data(), rn, m2);
    const size_t dn = mk_data_hb(1, 5, 3, 7, /*hops_remaining=*/2, /*committed=*/1, "x", db);
    node.on_recv(db.data(), dn, m2);                       // pass -> ACK + forward queued
    CHECK(hal.count("ack_tx") == 1);
    node.on_timer(kPostAckTimerId);                        // do_post_ack -> forward TxItem -> issue_send -> RTS to 7
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(1, 7, 7, cb);                 // CTS from 7 for the forward (tx_id=7)
    node.on_recv(cb.data(), cn, m7);
    node.on_timer(kCtsToDataGapTimerId);                   // -> forwarded DATA tx
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) { CHECK(pd->hops_remaining == 1); CHECK(pd->committed_hops == 2);   // decremented (2->1, committed 1->2)
              CHECK(pd->prev_fwd_rt_hops == 2); }   // re-stamped to self's rt[5].hops (beacon hops 1 + 1)
}

TEST_CASE("hop_budget — sender NACK recovery: terminal giveup + rt.hops bump feeds the NEXT budget") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,2,14}});   // candidate to 5 via 2, hops = 3
    send_cmd(*node, 5, "hi");                               // flight 1 (ctr_lo=1) via 2
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/1, /*reason=*/2, /*payload=*/(4 << 4), nb);  // committed=4
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("rts_giveup")   == 1);                  // TERMINAL giveup
    CHECK(hal.count("path_cascade") == 0);                  // NO cascade
    CHECK(hal.rand_calls - rand_before == 0);               // NO rand on the HOP_BUDGET path
    // the rt.hops bump (3 -> max(3, committed+1=5) = 5) feeds the NEXT send: min(31, 5+3) = 8.
    send_cmd(*node, 5, "hi2");                              // flight 2 (ctr_lo=2)
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(1, 2, 7, cb); node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) CHECK(pd->hops_remaining == 8);                // reflects the bumped rt.hops = 5
    delete node;
}

// Locks the handle_data REORDER + the exhaustion-path seen_origins write (review #04/#05/#10):
// Lua runs HOP_BUDGET ABOVE the loop-dup dedup AND records (origin,dst,ctr) on exhaustion, so a
// LATER non-exhausted arrival of the SAME flight via a DIFFERENT prev-hop is caught as LOOP_DUP
// (not accepted+forwarded). Before the fix the C++ ran dedup first and skipped the write.
TEST_CASE("hop_budget — an exhausted frame records seen_origins so a later diff-prev-hop arrival is LOOP_DUP") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{}; const size_t bn = mk_beacon_route(7, 5, 9, 1, 14, bb);   // route to 5 (could forward)
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // copy 1 via prev-hop 2: arrives EXHAUSTED (hops_remaining==0) -> HOP_BUDGET NACK + records
    // seen_origin_from[(origin0,dst5,ctr3)] = 2.
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,3,10,rb,0,/*origin=*/0,/*ctr=*/3); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data_hb(1,5,3,0,/*hops_remaining=*/0,/*committed=*/2,"x",db);
                       node.on_recv(db.data(), dn, m2); }
    CHECK(hal.count("hop_budget_exceeded") == 1);
    {   // the REAL emitted NACK bytes carry reason=hop_budget + committed in the HIGH nibble (review #03)
        auto pn = parse_nack(std::span<const uint8_t>(hal.last_tx("NACK")->bytes.data(),
                                                      hal.last_tx("NACK")->bytes.size()));
        CHECK(pn.has_value());
        if (pn) { CHECK(pn->reason == protocol::nack_reason_hop_budget);
                  CHECK(((pn->payload >> 4) & 0x0f) == 3); }   // committed 2 -> +1 -> 3
    }
    const int ack_after_copy1 = hal.count("ack_tx");
    // copy 2 via prev-hop 3: SAME (origin,dst,ctr) but NOT exhausted -> HOP_BUDGET passes, then the
    // dedup finds prior_from 2 != 3 -> LOOP_DUP NACK to 3, NO ACK.
    RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)};
    hal._now = 1200; { const size_t rn = mk_rts(3,1,5,3,10,rb,0,/*origin=*/0,/*ctr=*/3); node.on_recv(rb.data(), rn, m3); }
    hal._now = 1300; { const size_t dn = mk_data_hb(1,5,3,0,/*hops_remaining=*/5,/*committed=*/1,"x",db);
                       node.on_recv(db.data(), dn, m3); }
    const Ev* nk2 = hal.last("nack_tx"); CHECK(nk2 != nullptr);
    if (nk2) { CHECK(nk2->to == 3); CHECK(nk2->reason == protocol::nack_reason_loop_dup); }   // LOOP_DUP, not HOP_BUDGET
    CHECK(hal.count("dup_drop") >= 1);
    CHECK(hal.count("ack_tx") == ack_after_copy1);            // the looped dup was NOT ACKed
}

// Locks the requeue budget-threading fix (review #00): a FORWARDED flight that hits a long-busy
// BUSY_RX NACK must keep its inherited hop budget across the requeue. Before the fix the rebuilt
// TxItem zeroed fwd_remaining, so the re-issued DATA carried hops_remaining=0 and the NEXT hop
// terminally HOP_BUDGET-killed an in-transit message that had ample budget.
TEST_CASE("hop_budget — a forwarded flight keeps its budget across a BUSY_RX long-busy requeue") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};
    { const size_t n = mk_beacon_route(7, 5, 9, 1, 14, bb);     // route to 5 via 7 (primary, score 14)
      RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), n, m7); }
    { const size_t n = mk_beacon_route(8, 5, 9, 1, 13, bb);     // route to 5 via 8 (alt, score 13)
      RxMeta m8{12.0f,-70.0f,0,static_cast<int8_t>(8)}; node.on_recv(bb.data(), n, m8); }
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // become a forwarder: DATA from prev-hop 2 (origin0,dst5,ctr4) arriving remaining=4 -> decrement
    // to fwd_remaining=3, ACK, forward queued.
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    const size_t rn = mk_rts(2,1,5,4,10,rb,0,/*origin=*/0,/*ctr=*/4); node.on_recv(rb.data(), rn, m2);
    const size_t dn = mk_data_hb(1,5,4,0,/*hops_remaining=*/4,/*committed=*/1,"x",db);
    node.on_recv(db.data(), dn, m2);
    node.on_timer(kPostAckTimerId);                            // do_post_ack -> forward via 7 (RTS to 7)
    { const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr); if (r) CHECK(r->next == 7); }
    // long-busy BUSY_RX NACK from 7 (busy 3200ms > 2000) -> mark 7 blind + requeue the forward.
    std::array<uint8_t,8> nb{}; const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/4, /*reason=*/0, /*payload=*/200, nb);
    RxMeta m7n{8.0f,-80.0f,0,static_cast<int8_t>(7)}; node.on_recv(nb.data(), nn, m7n);
    CHECK(hal.count("tx_requeued") == 1);
    { const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr); if (r) CHECK(r->next == 8); }   // re-issued via the alt
    // CTS from 8 -> forwarded DATA must STILL carry the inherited budget (3), not 0.
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/8, /*data_sf=*/7, cb);
    RxMeta m8c{12.0f,-70.0f,0,static_cast<int8_t>(8)}; node.on_recv(cb.data(), cn, m8c);
    node.on_timer(kCtsToDataGapTimerId);                       // -> forwarded DATA tx
    auto pd = parse_tx_data(hal.last_tx("DATA")); CHECK(pd.has_value());
    if (pd) { CHECK(pd->hops_remaining == 3); CHECK(pd->committed_hops == 2); }   // budget survived the requeue
}

// ---- R4.0 + R4.1 — duty-cycle budget tier + BUDGET NACK (reason 1) ----------
static Node* mk_budget_node(TestHal& hal, double duty_cycle, uint32_t window_ms) {
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = duty_cycle; cfg.duty_cycle_window_ms = window_ms;
    node->on_init(cfg);
    return node;
}

TEST_CASE("duty_status — pct/avail/enabled surface the rolling-window budget") {
    TestHal hal;
    Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/3600000);   // budget = 360,000 ms
    hal._airtime_used = 0;                                                 // 0% -> headroom
    auto d0 = node->duty_status();
    CHECK(d0.enabled); CHECK(d0.pct == 0); CHECK(d0.avail_ms == 0);
    hal._airtime_used = 180000;                                           // half budget -> 50%, still headroom
    auto d50 = node->duty_status();
    CHECK(d50.pct == 50); CHECK(d50.avail_ms == 0);
    hal._airtime_used = 360000; hal._oldest_tx_end = 0; hal._now = 0;     // at budget -> 100% (silent)
    auto d100 = node->duty_status();
    CHECK(d100.pct == 100); CHECK(d100.avail_ms > 0);                     // oldest=0 -> full-window fallback
    hal._airtime_used = 400000; hal._oldest_tx_end = 1000; hal._now = 600000;   // recovery = oldest + window - now
    auto dr = node->duty_status();
    CHECK(dr.pct == 100); CHECK(dr.avail_ms == (1000u + 3600000u - 600000u));
    delete node;
    TestHal hal2; Node* off = mk_budget_node(hal2, /*duty=*/0.0, /*window=*/3600000);   // duty<=0 -> disabled (no limit)
    hal2._airtime_used = 9999999;
    auto doff = off->duty_status();
    CHECK_FALSE(doff.enabled); CHECK(doff.pct == 0); CHECK(doff.avail_ms == 0);
    delete off;
}

TEST_CASE("① mobile-as-transit — learn is_mobile, exclude as transit; §Fix 3 NO dest route from a beacon (dv:1325-1334)") {
    TestHal hal;
    Node* node = mk_budget_node(hal, /*duty=*/0.0, /*window=*/3600000);   // id=1, leaf 0
    RxMeta meta{8.0f, -80.0f, 0, -1};
    CHECK_FALSE(node->is_mobile_peer(5));
    // a MOBILE neighbour 5 beacons, carrying a route to dest 9 (=> a 9-via-5 candidate at our node)
    beacon_entry e{}; e.dest = 9; e.next = 7; e.score_bucket = 12; e.hops = 2;
    beacon_in in{}; in.leaf_id = 0; in.src = 5; in.key_hash32 = 0x2005; in.is_mobile = true;
    in.entries = std::span<const beacon_entry>(&e, 1);
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(in, std::span<uint8_t>(bb.data(), bb.size()));
    node->on_recv(bb.data(), bn, meta);

    CHECK(node->is_mobile_peer(5));                          // learned the bit
    CHECK(node->route_uses_mobile_as_transit(9, 5));         // dest 9 via mobile 5 = transit -> excluded
    CHECK_FALSE(node->route_uses_mobile_as_transit(5, 5));   // 5 IS the dest -> deliver TO a mobile is fine
    CHECK_FALSE(node->route_uses_mobile_as_transit(9, 3));   // 3 not mobile -> fine
    CHECK_FALSE(node->route_uses_mobile_as_transit(9, 0));   // next 0 -> not a transit

    auto has = [&](uint8_t d){ for (uint8_t i = 0; i < node->rt_count(); ++i) if (node->rt_at(i).dest == d) return true; return false; };
    CHECK_FALSE(has(9));                                     // rt_merge SKIPPED the 9-via-mobile-5 candidate (dv:4583)
    CHECK_FALSE(has(5));                                     // §mobile Fix 3: a mobile's LOCAL id NEVER enters the static rt (supersedes ①'s allow-as-dest) — a static node reaches a mobile ONLY as home_id+dst_hash; the home last-miles via a DIRECT addr_len=1 send
    CHECK(hal.count("rt_skip_mobile_transit") >= 1);
    delete node;
}

TEST_CASE("§mobile Fix 3 — a mobile's beacon mints NO static route to its local id; a non-mobile beacon still does") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto has_dest = [](Node* n, uint8_t d){ for (uint8_t i = 0; i < n->rt_count(); ++i) if (n->rt_at(i).dest == d) return true; return false; };
    // (a) MOBILE beacon (src=17) -> NO rt[17] (the local id stays off the static plane; reached only as home_id+dst_hash)
    { TestHal hal; Node* node = mk_budget_node(hal, /*duty=*/0.0, /*window=*/3600000);
      beacon_in in{}; in.leaf_id = 0; in.src = 17; in.key_hash32 = 0xB0B1u; in.is_mobile = true;
      std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(in, std::span<uint8_t>(bb.data(), bb.size()));
      node->on_recv(bb.data(), bn, meta);
      CHECK_FALSE(has_dest(node, 17));                      // ★ Fix 3: learn_direct_neighbor SKIPPED for a mobile beacon
      CHECK(node->is_mobile_peer(17));                      // but the mobility bit IS still learned (line 634 unchanged)
      delete node; }
    // (b) NON-mobile beacon (src=17) -> rt[17] as before (proves the guard is is_mobile-specific, not a blanket drop)
    { TestHal hal; Node* node = mk_budget_node(hal, 0.0, 3600000);
      beacon_in in{}; in.leaf_id = 0; in.src = 17; in.key_hash32 = 0xB0B1u; in.is_mobile = false;
      std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(in, std::span<uint8_t>(bb.data(), bb.size()));
      node->on_recv(bb.data(), bn, meta);
      CHECK(has_dest(node, 17));                            // ★ a static neighbour -> a normal direct route
      delete node; }
}

TEST_CASE("§mobile 6.2 — team-id TLV round-trips (type 5, 5 B); a non-team ext -> 0") {
    uint8_t buf[8];
    const size_t n = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(buf, sizeof buf));
    CHECK(n == 5);   // [ (5<<4)|4 ][ team_id 4 B LE ]
    CHECK(parse_team_id_tlv(std::span<const uint8_t>(buf, n)) == 0xABCD1234u);
    uint32_t ids[1] = { 0x11223344u };
    uint8_t other[8]; const size_t on = pack_channel_digest_tlv(ids, 1, std::span<uint8_t>(other, sizeof other));
    CHECK(parse_team_id_tlv(std::span<const uint8_t>(other, on)) == 0);   // a type-3 TLV carries no type-5 -> absent
}

TEST_CASE("§mobile 6.2 — a same-team peer routes via _rt_team (not _rt); §18 collision avoided; a different team is ignored") {
    TestHal hal;
    Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    RxMeta meta{8.0f,-80.0f,0,-1};
    auto in_team = [&](uint8_t d){ for (uint8_t i=0;i<node.rt_team_count();++i) if (node.rt_team_at(i).dest==d) return true; return false; };
    auto in_stat = [&](uint8_t d){ for (uint8_t i=0;i<node.rt_count();++i)      if (node.rt_at(i).dest==d)      return true; return false; };
    // (a) a SAME-TEAM peer (local id 20) beacons with the type-5 team TLV -> learned as a team peer + a route in _rt_team, NOT _rt
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=20; tb.key_hash32=0x9999u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(20));
    CHECK(in_team(20)); CHECK_FALSE(in_stat(20));           // ★ the team plane has 20; the static plane does not
    // (b) §18 collision: a STATIC node ALSO has global id 20 -> its route lands in _rt and COEXISTS with team-20 in _rt_team
    beacon_in sb{}; sb.leaf_id=0; sb.src=20; sb.key_hash32=0x2020u; sb.is_mobile=false;
    std::array<uint8_t,64> sbb{}; size_t sbn = pack_beacon(sb, std::span<uint8_t>(sbb.data(), sbb.size()));
    node.on_recv(sbb.data(), sbn, meta);
    CHECK(in_stat(20));                                     // ★ static-20 in _rt
    CHECK(in_team(20));                                     // ★ team-20 STILL in _rt_team (NOT evicted — the whole point of the separate table)
    // (c) a DIFFERENT-team peer (21) -> not a team peer, no _rt_team route
    uint8_t ext2[8]; size_t en2 = pack_team_id_tlv(0x99999999u, std::span<uint8_t>(ext2, sizeof ext2));
    beacon_in ob{}; ob.leaf_id=0; ob.src=21; ob.key_hash32=0x2121u; ob.is_mobile=true; ob.ext=std::span<const uint8_t>(ext2, en2);
    std::array<uint8_t,64> obb{}; size_t obn = pack_beacon(ob, std::span<uint8_t>(obb.data(), obb.size()));
    node.on_recv(obb.data(), obn, meta);
    CHECK_FALSE(node.is_team_peer(21));
    CHECK_FALSE(in_team(21));
}

TEST_CASE("§mobile 6.2 — a same-team peer is a LEGAL transit (multi-hop A->B->C); a multi-hop teammate gets a dispatch bit") {
    TestHal hal;
    Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    RxMeta meta{8.0f,-80.0f,0,-1};
    // teammate B (id 20) beacons WITH a carried route to teammate C (id 25) -> at us, "25 via B(20)", 2 hops
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_entry ce{}; ce.dest=25; ce.next=26; ce.score_bucket=12; ce.hops=1;
    beacon_in tb{}; tb.leaf_id=0; tb.src=20; tb.key_hash32=0x9999u; tb.is_mobile=true;
    tb.ext=std::span<const uint8_t>(ext, en); tb.entries=std::span<const beacon_entry>(&ce, 1);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(20));                            // B (direct teammate)
    CHECK(node.is_team_peer(25));                            // ★ C (MULTI-HOP) also gets a dispatch bit -> rt_find(25) -> _rt_team
    // the _rt_team route to 25 is via B(20)
    bool team25=false; uint8_t via25=0;
    for (uint8_t i=0;i<node.rt_team_count();++i) if (node.rt_team_at(i).dest==25){ team25=true; via25=node.rt_team_at(i).candidates[0].next_hop; }
    CHECK(team25); CHECK(via25 == 20);
    // ★ THE FIX: B (a same-team peer) is a LEGAL transit — route_uses_mobile_as_transit(C, B) must be FALSE (else the
    // send-time next_hop_selectable rejects B and multi-hop team routing is defeated).
    CHECK_FALSE(node.route_uses_mobile_as_transit(25, 20));  // C via teammate B -> allowed
    CHECK(node.is_mobile_peer(20));                          // (B is still a mobile peer — the carve-out is is_team_peer, not is_mobile_peer)
}

// Wave-2 ruling 2.3: a TEAM liveness rerank must ADVERTISE. team_resort_routes_through now mirrors the static
// resort_routes_for_neighbor_penalty — a primary change dirty-marks the moved _rt_team entry + arms ONE triggered
// beacon (emit_beacon's dirty pass selects from _rt_team for a team member), so the rerank rides the member's cadence.
TEST_CASE("§2.3 team liveness rerank — a demoted team relay loses primacy AND the moved _rt_team entry dirties + arms a triggered beacon") {
    TestHal hal;
    Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.set_team_local_id(30);                              // adopt our team id -> emit_beacon runs the team plane (clears _rt_team dirty on flush)
    RxMeta meta{8.0f,-80.0f,0,-1};
    uint8_t ext[8]; const size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    // two teammates (20, 21) EACH carry a route to teammate C (25) -> two EQUAL candidates for 25; insertion order -> via 20 primary
    for (uint8_t relay : {20, 21}) {
        beacon_entry ce{}; ce.dest=25; ce.next=26; ce.score_bucket=14; ce.hops=1;
        beacon_in tb{}; tb.leaf_id=0; tb.src=relay; tb.key_hash32=0x9000u+relay; tb.is_mobile=true;
        tb.ext=std::span<const uint8_t>(ext, en); tb.entries=std::span<const beacon_entry>(&ce, 1);
        std::array<uint8_t,64> b{}; const size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
        node.on_recv(b.data(), bn, meta);
    }
    auto idx25 = [&]() -> int { for (uint8_t i=0;i<node.rt_team_count();++i) if (node.rt_team_at(i).dest==25) return i; return -1; };
    const int i25 = idx25();
    CHECK(i25 >= 0);
    CHECK(node.rt_team_at(i25).candidates[0].next_hop == 20);   // via 20 primary (insertion order, equal score)
    node.on_timer(kTriggeredBeaconTimerId);                     // flush the setup's pending triggered beacon (team emit clears the setup dirty)
    CHECK_FALSE(node.rt_team_at(i25).dirty);                    // setup dirty cleared by the flush
    hal.armed.clear();
    const int rb = hal.rand_calls;
    // demote relay 20 on the TEAM plane (local RTS-timeout evidence) -> team_resort_routes_through(20)
    node.record_peer_rts_timeout(20, 0, /*team_plane=*/true);
    node.record_peer_rts_timeout(20, 0, /*team_plane=*/true);
    node.record_peer_rts_timeout(20, 0, /*team_plane=*/true);   // 3 -> SILENT tier (penalty demotes via-20)
    CHECK(node.rt_team_at(i25).candidates[0].next_hop == 21);   // §2.3: reranked -> via 21 now primary
    CHECK(node.rt_team_at(i25).dirty);                          // §2.3 FIX: the moved entry is dirty-marked
    CHECK(hal.count("rt_penalty_rerank") >= 1);                 // telemetry mirrors the static path
    CHECK(hal.rand_calls - rb >= 1);                            // §2.3 FIX: a triggered beacon was armed (>=1 trigger-jitter draw)
    bool armed_trigger = false; for (auto& a : hal.armed) if (a.second == kTriggeredBeaconTimerId) armed_trigger = true;
    CHECK(armed_trigger);                                       // ...on kTriggeredBeaconTimerId
}

TEST_CASE("§P2-1 — a cross-NIBBLE same-team beacon is ACCEPTED by a team member (leaf-exempt); DROPPED by a static + an other-team member") {
    RxMeta meta{8.0f,-80.0f,0,-1};
    const uint32_t TEAM = 0xABCD1234u;
    // A teammate on leaf 4 — a DIFFERENT nibble from the leaf-6 receivers below (the mixed-leaf case the pre-2026-07-20 code
    // partitioned: the pre-parse nibble gate dropped this beacon so no cross-leaf _rt_team ever formed).
    uint8_t ext[8]; const size_t en = pack_team_id_tlv(TEAM, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=4; tb.src=20; tb.key_hash32=0x9999u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext,en);
    std::array<uint8_t,64> b{}; const size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    auto in_team=[&](Node& n,uint8_t d){ for(uint8_t i=0;i<n.rt_team_count();++i) if(n.rt_team_at(i).dest==d) return true; return false; };
    auto in_stat=[&](Node& n,uint8_t d){ for(uint8_t i=0;i<n.rt_count();++i)      if(n.rt_at(i).dest==d)      return true; return false; };
    // (a) SAME-TEAM MEMBER on leaf 6 -> ACCEPTS the leaf-4 teammate beacon (leaf-exempt): team peer + _rt_team route, NOT _rt.
    { TestHal hal; Node m(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=6; cfg.is_mobile=true; cfg.team_id=TEAM;
      CHECK(m.on_init(cfg));
      m.on_recv(b.data(), bn, meta);
      CHECK(m.is_team_peer(20));                              // ★ accepted despite the foreign nibble
      CHECK(in_team(m,20)); CHECK_FALSE(in_stat(m,20)); }    // ...into the TEAM plane only
    // (b) a STATIC on leaf 6 (team_id=0) -> DROPS the leaf-4 beacon pre-parse (UNCHANGED behavior — the containment axis).
    { TestHal hal; Node s(hal, /*id=*/31, /*key=*/0x3131u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=6;
      CHECK(s.on_init(cfg));
      s.on_recv(b.data(), bn, meta);
      CHECK_FALSE(s.is_team_peer(20)); CHECK_FALSE(in_team(s,20)); CHECK_FALSE(in_stat(s,20)); }
    // (c) an OTHER-team member on leaf 6 -> parses (it defers the nibble drop) but is NOT same-team -> no peer, no route.
    { TestHal hal; Node o(hal, /*id=*/32, /*key=*/0x3232u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=6; cfg.is_mobile=true; cfg.team_id=0x99999999u;
      CHECK(o.on_init(cfg));
      o.on_recv(b.data(), bn, meta);
      CHECK_FALSE(o.is_team_peer(20)); CHECK_FALSE(in_team(o,20)); CHECK_FALSE(in_stat(o,20)); }
}

TEST_CASE("§P2-1 — a cross-NIBBLE team-scoped H is HANDLED leaf-agnostically by a same-team member; a static/foreign-leaf static-H is dropped at the leaf gate") {
    RxMeta meta{8.0f,-80.0f,0,-1};
    const uint32_t TEAM = 0xABCD1234u;
    auto mk_h=[&](bool team_scoped, uint32_t key, std::array<uint8_t,64>& buf)->size_t{
        h_in in{}; in.leaf_id=4; in.origin=20; in.query_key32=key; in.ttl=3; in.team_scoped=team_scoped; in.team_id=team_scoped?TEAM:0;
        return pack_h(in, std::span<uint8_t>(buf.data(), buf.size())); };
    // (a) a same-team member on leaf 6 receives a leaf-4 TEAM-scoped H for ITS OWN hash -> handled + resolved (leaf-exempt).
    { TestHal hal; Node m(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=6; cfg.is_mobile=true; cfg.team_id=TEAM;
      CHECK(m.on_init(cfg));
      std::array<uint8_t,64> hb{}; const size_t hn = mk_h(/*team_scoped=*/true, /*key=*/0x3030u, hb);
      m.on_recv(hb.data(), hn, meta);
      CHECK(hal.count("h_rx") >= 1);                          // ★ NOT leaf-dropped — the team-scoped H was processed
      CHECK(hal.count("h_resolved") >= 1); }                 // ...and resolved own-hash across the nibble
    // (b) a STATIC on leaf 6 gets the SAME leaf-4 team-scoped H -> dropped at the leaf gate BEFORE h_rx (not same-team).
    { TestHal hal; Node s(hal, /*id=*/31, /*key=*/0x3131u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=6;
      CHECK(s.on_init(cfg));
      std::array<uint8_t,64> hb{}; const size_t hn = mk_h(/*team_scoped=*/true, /*key=*/0x3131u, hb);
      s.on_recv(hb.data(), hn, meta);
      CHECK(hal.count("h_rx") == 0); CHECK(hal.count("h_resolved") == 0); }
    // (c) a same-team member gets a leaf-4 STATIC (non-team-scoped) H -> a static H stays LEAF-scoped -> dropped, no h_rx.
    { TestHal hal; Node m(hal, /*id=*/33, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=6; cfg.is_mobile=true; cfg.team_id=TEAM;
      CHECK(m.on_init(cfg));
      std::array<uint8_t,64> hb{}; const size_t hn = mk_h(/*team_scoped=*/false, /*key=*/0x3030u, hb);
      m.on_recv(hb.data(), hn, meta);
      CHECK(hal.count("h_rx") == 0); }
}

TEST_CASE("§mobile 6.4 — a team mobile emits its team-id TLV ONLY after adopting a team_local_id (pre-DAD -> identity-only, keeps its static node_id OUT of peers' _rt_team)") {
    TestHal hal; Node node(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.quiet_threshold_ms=0;
    cfg.is_mobile=true; cfg.team_id=0xA930CA2Du;
    CHECK(node.on_init(cfg));
    auto emit_team_tlv = [&]() -> uint32_t {                  // force a beacon out; return the type-5 team_id it carries (0 = absent)
        const size_t before = hal.tx_frames.size();
        node.test_emit_beacon("periodic");
        if (hal.tx_frames.size() != before + 1) return 0xDEADu;   // no beacon aired -> sentinel (fails either assertion)
        const auto& f = hal.tx_frames.back();
        auto pb = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (!pb) return 0xDEADu;
        auto ext = beacon_ext(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()), *pb);
        return parse_team_id_tlv(ext);
    };
    // (a) team_local_id == 0 (pre-DAD / not yet adopted): NO team TLV — else a peer learns our STATIC node_id 17 into _rt_team.
    CHECK(node.team_local_id() == 0);
    CHECK(emit_team_tlv() == 0u);                             // ★ THE FIX: no type-5 TLV until the team id is adopted
    // (b) adopt a team_local_id -> the beacon now carries the team TLV (the team plane comes alive).
    node.set_team_local_id(196);
    CHECK(emit_team_tlv() == 0xA930CA2Du);                    // now present
}

TEST_CASE("§mobile 6.4 — a team member never learns a _rt_team route to its OWN team-local id (a teammate re-advertising us -> team-plane split-horizon)") {
    TestHal hal; Node node(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xA930CA2Du;
    CHECK(node.on_init(cfg));
    node.set_team_local_id(196);                             // OUR team-plane id
    RxMeta meta{8.0f,-80.0f,0,-1};
    // teammate B (id 131) beacons a carried route whose dest == OUR team id 196 (B reflecting a route back to us).
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xA930CA2Du, std::span<uint8_t>(ext, sizeof ext));
    beacon_entry ce{}; ce.dest=196; ce.next=200; ce.score_bucket=12; ce.hops=1;   // "196 via 200" — dest is us
    beacon_in tb{}; tb.leaf_id=0; tb.src=131; tb.key_hash32=0x8888u; tb.is_mobile=true;
    tb.ext=std::span<const uint8_t>(ext, en); tb.entries=std::span<const beacon_entry>(&ce, 1);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(131));                            // B itself is a legit direct teammate
    // ★ THE FIX: no self-route to our OWN team id 196 (and no dispatch bit for it).
    bool self_route=false;
    for (uint8_t i=0;i<node.rt_team_count();++i) if (node.rt_team_at(i).dest==196) self_route=true;
    CHECK_FALSE(self_route);
    CHECK_FALSE(node.is_team_peer(196));
}

TEST_CASE("§mobile 6.4 — team UNICAST DM: the sender routes via _rt_team + marks the RTS addr_len=1 (team-plane next); a member's own team id is a delivery dst") {
    TestHal hal;
    Node node(hal, /*id=*/0, /*key=*/0x3030u);              // OFF-GRID (node_id 0) team member — no static host
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.team_dad_fire();                                    // §6.4 Option X: self-DAD provisions node_id = _team_local_id
    const uint8_t a_tid = node.team_local_id();
    CHECK(a_tid >= 17);
    CHECK(node.node_id() == a_tid);                          // ★ off-grid: the team-DAD'd id IS the node's link-layer id
    // learn a DIRECT teammate C (id 25) via a same-team beacon -> a route in _rt_team
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(25)};
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=25; tb.key_hash32=0x2525u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(25));
    // (B) originate a DM to the teammate -> the RTS routes via _rt_team (NOT dropped, NOT via a static home) and carries
    // addr_len=1 so the teammate's mark-accept treats `next` as its team-plane id. src = our team id (a_tid).
    hal.tx_frames.clear();
    send_cmd(node, /*dst=*/25, "hi");
    bool got_rts=false;
    for (auto& f : hal.tx_frames) { auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pr && pr->next == 25) { got_rts=true; CHECK(pr->addr_len == 1); CHECK(pr->src == a_tid); CHECK(pr->mobile_src); } }
    CHECK(got_rts);                                          // ★ B: the team-plane last-mile is addressed (addr_len=1) AND its src is marked a LOCAL id (mobile_src) -> kept out of every static _rt/ledger
    // (D) a DM addressed to our team-plane id is FOR US (delivered, not forwarded). Off-grid a_tid==node_id so the static
    // term already covers it; for_me_dst also carries the DUAL case (node_id=static id, dst=team id).
    CHECK(node.for_me_dst(a_tid));
    CHECK_FALSE(node.for_me_dst(static_cast<uint8_t>(a_tid ^ 0x0F)));
}

TEST_CASE("§mobile 6.4 — PLAINTEXT delivery-BY-HASH within a team: send 0x<teammate-hash> resolves to its team_local_id + routes via _rt_team (no H-flood)") {
    TestHal hal;
    Node node(hal, /*id=*/0, /*key=*/0x3030u);              // OFF-GRID team member (no static home)
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.team_dad_fire();                                    // adopt our team_local_id
    // learn a DIRECT teammate C (id 25, key_hash 0x2525) via a same-team beacon -> is_team_peer + team_key cache (id->hash)
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(25)};
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=25; tb.key_hash32=0x2525u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(25));
    // send BY HASH (plaintext, crypt=def) to the teammate's key_hash -> must resolve to id 25 + route via _rt_team (an RTS
    // to next==25), NOT flood an H-query. = the proven `send 25` path, reached from a 0x-hash target.
    hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 0; c.u.send.dst_hash = 0x2525u; c.u.send.flags = 0;
    c.crypt = CryptIntent::def; c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
    node.on_command(c);
    bool got_rts=false;
    for (auto& f : hal.tx_frames) {
        auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pr && pr->next == 25) { got_rts=true; CHECK(pr->addr_len == 1); CHECK(pr->mobile_src); }   // team-plane last-mile, marked LOCAL id
    }
    CHECK(got_rts);   // ★ THE FIX: 0x<hash> resolved to team id 25 (team_id_of_key) + routed via _rt_team — no H-flood storm
}

TEST_CASE("§mobile 6.4 / §18 — a DUAL team member (node_id != team_local_id) stamps the team-plane RTS src with team_local_id, NOT node_id") {
    TestHal hal;
    Node node(hal, /*id=*/17, /*key=*/0x1717u);              // static node_id 17 (can collide a teammate's node_id)
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.set_team_local_id(93);                              // DUAL: node_id=17, team_local_id=93 (distinct)
    CHECK(node.node_id() == 17); CHECK(node.team_local_id() == 93);
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(238)};    // learn teammate C (team id 238)
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=238; tb.key_hash32=0xEE38u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(238));
    hal.tx_frames.clear();
    send_cmd(node, /*dst=*/238, "hi");
    bool got_rts=false;
    for (auto& f : hal.tx_frames) { auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pr && pr->next == 238) { got_rts=true; CHECK(pr->src == 93); CHECK(pr->addr_len == 1); CHECK(pr->mobile_src); } }  // ★ src is OUR team id 93, not node_id 17
    CHECK(got_rts);
}

TEST_CASE("§mobile 6.4 / §18 — a DUAL team member answers a team RTS with a CTS whose tx_id is its team_local_id (not node_id)") {
    TestHal hal;
    Node node(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.set_team_local_id(238);                             // our team id 238, node_id 17
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(93)};
    rts_in r{}; r.leaf_id=0; r.src=93; r.next=238; r.ctr_lo=5; r.dst=238; r.sf_index=3; r.payload_len=7; r.mobile_src=true; r.addr_len=1;  // team RTS to our team id
    r.id = mk_rts_id(93, 5);   // §hybrid-rts S1: a unicast RTS carries the flight identity
    uint8_t rb[11]; size_t rn = pack_rts(r, std::span<uint8_t>(rb, sizeof rb));
    hal.tx_frames.clear();
    node.on_recv(rb, rn, meta);
    bool got_cts=false;
    for (auto& f : hal.tx_frames) { auto pc = parse_cts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pc) { got_cts=true; CHECK(pc->tx_id == 238); CHECK(pc->rx_id == 93); } }   // ★ CTS from OUR team id 238 back to the sender's team id 93
    CHECK(got_cts);
}

TEST_CASE("§mobile 6.4 / §18 — end-to-end: two team mobiles sharing node_id=17 complete a team DM (CTS accepted -> DATA; team ACK to team_local_id accepted -> no storm)") {
    TestHal hal;
    Node node(hal, /*id=*/17, /*key=*/0x1717u);              // sender: node_id=17, team id 93 (peer ALSO has node_id 17)
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.set_team_local_id(93);
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(238)};
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=238; tb.key_hash32=0xEE38u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    hal.tx_frames.clear();
    send_cmd(node, /*dst=*/238, "hi");
    uint8_t fctr=0; for (auto& f : hal.tx_frames) { auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size())); if (pr && pr->next==238) fctr=pr->ctr_lo; }
    // teammate 238 answers our RTS: CTS rx_id=93 (our team id, echoing our RTS src), tx_id=238. Must clear OUR flight -> DATA.
    RxMeta m238{12.0f,-70.0f,0,static_cast<int8_t>(238)};
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(/*rx_id=*/93, /*tx_id=*/238, /*data_sf=*/7, cb);
    node.on_recv(cb.data(), cn, m238);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);                         // ★ S0: the teammate's CTS (rx_id=our team id) cleared the flight -> DATA sent (was 0 pre-fix: dropped as self/overheard)
    // teammate ACKs to our TEAM id 93 with mobile_to=1 -> the ACK gate must accept it (S1).
    ack_in ai{}; ai.ctr_lo = fctr; ai.to = 93; ai.mobile_to = true;
    uint8_t ab[8]; size_t an = pack_ack(ai, std::span<uint8_t>(ab, sizeof ab));
    node.on_recv(ab, an, m238);
    const int rts_before = hal.count("rts_tx");
    node.on_timer(kAckTimeoutTimerId); node.on_timer(kRetryBackoffTimerId);
    CHECK(hal.count("rts_tx") == rts_before);                // ★ S1: the team ACK (to=team_local_id) was accepted -> flight complete -> NO re-RTS storm
}

TEST_CASE("§mobile 6.4 / Wave 2 — the plane hard split: GLOBAL routes via _rt even for an id colliding a team peer; TEAM via _rt_team; TEAM on a node with no team plane fails loud") {
    // §18 collision: id 50 lives on BOTH planes — a STATIC route via 60 (_rt) AND a team peer via 50 (_rt_team). A fresh node
    // per send (a pending flight blocks a second origination) so the two plane routings are observed independently.
    auto setup = [](Node& n) {
        NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
        CHECK(n.on_init(cfg));
        n.set_team_local_id(93);
        CHECK(n.route_inject(/*dest=*/50, /*next=*/60, /*hops=*/1, /*score=*/160));
        RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(50)};
        uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
        beacon_in tb{}; tb.leaf_id=0; tb.src=50; tb.key_hash32=0x5050u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
        std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
        n.on_recv(b.data(), bn, meta);
        CHECK(n.is_team_peer(50));
    };
    auto rts_next = [&](uint8_t plane) -> int {          // fresh node; issue `send 50 (plane)`; return the RTS next-hop
        TestHal hal; Node n(hal, /*id=*/30, /*key=*/0x3030u); setup(n);
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 50; c.u.send.plane = plane;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        n.on_command(c);
        for (auto& f : hal.tx_frames) { auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size())); if (pr) return pr->next; }
        return -1;
    };
    CHECK(rts_next(2 /*GLOBAL*/) == 60);                 // ★ GLOBAL -> the STATIC route (via 60), never the colliding team peer
    CHECK(rts_next(1 /*TEAM*/)   == 50);                 // ★ TEAM -> the team plane (_rt_team, direct to 50)
    // §team-parity T1 CHANGED THIS ASSERTION, deliberately. It used to read "TEAM to a non-teammate (id 77) -> fail
    // loud (err_no_binding), no storm" — that guard WAS the reported bench bug (spec §0: 213 could not `send 174 -t`),
    // because every team-RREQ entry point sits downstream of do_send. An unknown teammate id is now QUEUED and
    // discovered; see the T1 bench-case test below. What still fails loud is the CONFIGURATION case: no team plane.
    auto team_send = [](Node& n, uint8_t dst) {
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.plane = 1;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        return n.on_command(c);
    };
    { TestHal hal; Node n(hal, /*id=*/30, /*key=*/0x3030u); setup(n);
      const CmdResult r = team_send(n, /*dst=*/77);                       // unknown teammate id
      CHECK(r.code == CmdCode::queued);                                   // ★ T1: no longer refused — discovery is the answer
      CHECK(r.ctr != 0); }                                                // ★ a counter IS minted (the bench saw ctr=0)
    // (a) team_id == 0 -> refuse: the node is not on a team at all.
    { TestHal hal; Node n(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true;
      CHECK(n.on_init(cfg));
      CHECK(team_send(n, /*dst=*/77).code == CmdCode::err_no_binding); }
    // (b) team_id set but team-DAD not complete (team_local_id == 0) -> refuse. Without a DAD'd id
    //     emit_route_request's team arm returns silently, so the send would otherwise fail only 30 s later by TTL.
    { TestHal hal; Node n(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
      CHECK(n.on_init(cfg));
      n.set_team_local_id(0);
      CHECK(team_send(n, /*dst=*/77).code == CmdCode::err_no_binding); }
    // (c) a NON-mobile node carrying a team_id -> refuse. `team <id>` on the console sets team_id without is_mobile
    //     (firmware_config.cpp:659 gates team_dad_fire on is_mobile), and handle_f_team requires is_mobile on the
    //     RECEIVER — so such a node could flood team RREQs that teammates answer and then drop its OWN replies.
    //     Same membership predicate the sibling `send_channel -t` verb uses (node.cpp:1130).
    { TestHal hal; Node n(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.team_id=0xABCD1234u;
      CHECK(n.on_init(cfg));
      n.set_team_local_id(93);
      CHECK(team_send(n, /*dst=*/77).code == CmdCode::err_no_binding); }
}

// ============================ §team-parity T1 (spec 2026-07-27 §3/T1) ==========================================
// The reported bench failure and the team hop-cap adoption. ★ THESE TESTS ARE THE PRIMARY GATE FOR T1: all 32 corpus
// scenarios are byte-identical through this slice (measured), and two of the four adopted sites are corpus-DARK, so
// byte-identity cannot see a mistake in any of it. See the coverage matrix in the slice report.

namespace {
// An OFF-GRID team member — the bench configuration: is_mobile, no static host, node_id == team_local_id.
void t1_offgrid(Node& n, uint8_t id, uint32_t team) {
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=team;
    CHECK(n.on_init(cfg));
    n.set_team_local_id(id);
}
// A teammate's beacon (src = its team_local_id) — installs the _rt_team hops=1 route + the _team_peer dispatch bit.
size_t t1_team_beacon(uint8_t src, uint32_t team, std::array<uint8_t,64>& b) {
    uint8_t ext[8]; const size_t en = pack_team_id_tlv(team, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=src; tb.key_hash32=0x7000u+src; tb.is_mobile=true;
    tb.ext=std::span<const uint8_t>(ext, en);
    return pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
}
// The last F frame this node put on the air, parsed (parse_f itself rejects any non-F cmd nibble).
std::optional<f_out> t1_last_f(const TestHal& hal) {
    std::optional<f_out> r;
    for (const auto& f : hal.tx_frames)
        if (auto p = parse_f(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) r = *p;
    return r;
}
// The FIRST F frame's raw bytes (empty if none) — for feeding one node's flood into another's on_recv.
std::vector<uint8_t> t1_first_f_bytes(const TestHal& hal) {
    for (const auto& f : hal.tx_frames)
        if (parse_f(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) return f.bytes;
    return {};
}
bool t1_in_team_rt(Node& n, uint8_t d) { for (uint8_t i=0;i<n.rt_team_count();++i) if (n.rt_team_at(i).dest==d) return true; return false; }
bool t1_in_static_rt(Node& n, uint8_t d) { for (uint8_t i=0;i<n.rt_count();++i) if (n.rt_at(i).dest==d) return true; return false; }
}  // namespace

TEST_CASE("§team-parity T1 — THE BENCH CASE: `send -t` to a NEVER-HEARD teammate discovers it through the middle relay (was `err ctr=0 depth=0`)") {
    // Spec §0/§5: three off-grid members on one PHY, the middle one the only mutual neighbour.
    //   A(213) <-> R(234) <-> B(174);  A has NEVER heard B and holds no route to it.
    // Pre-T1 this died in on_command with err_no_binding before a counter was minted. Post-T1 it must queue, flood a
    // TEAM-scoped RREQ, accept the relay's RREP, and fly the DM to the relay.
    const uint32_t TEAM = 0x06EF37AEu;                       // the bench's real team_id
    TestHal ha, hr;
    Node A(ha, /*id=*/213, /*key=*/0xA213u);  t1_offgrid(A, 213, TEAM);
    Node R(hr, /*id=*/234, /*key=*/0xA234u);  t1_offgrid(R, 234, TEAM);
    RxMeta from_r{12.0f,-70.0f,0,static_cast<int8_t>(234)};
    RxMeta from_a{12.0f,-70.0f,0,static_cast<int8_t>(213)};
    RxMeta from_b{12.0f,-70.0f,0,static_cast<int8_t>(174)};
    std::array<uint8_t,64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); A.on_recv(bb.data(), n, from_r); }   // A hears the relay
    { const size_t n = t1_team_beacon(/*src=*/213, TEAM, bb); R.on_recv(bb.data(), n, from_a); }   // R hears A
    { const size_t n = t1_team_beacon(/*src=*/174, TEAM, bb); R.on_recv(bb.data(), n, from_b); }   // R hears B
    CHECK(A.is_team_peer(234));
    CHECK_FALSE(A.is_team_peer(174));                        // ★ the precondition: 174 has NEVER been heard by A
    CHECK_FALSE(t1_in_team_rt(A, 174));

    ha.events.clear(); ha.tx_frames.clear();
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 174; c.u.send.plane = 1 /*TEAM*/;
    c.body = reinterpret_cast<const uint8_t*>("Test next"); c.body_len = 9;
    const CmdResult res = A.on_command(c);
    CHECK(res.code == CmdCode::queued);                      // ★★ THE FIX — pre-T1: err_no_binding
    CHECK(res.ctr != 0);                                     // ★★ a counter is minted — the bench printed ctr=0
    CHECK(ha.count("send_deferred") == 1);                   // no route -> parked, not dropped

    // The discovery it fired must be TEAM-scoped (never a static F): the isolation assumption T1 rests on.
    auto rq = t1_last_f(ha);
    CHECK(rq.has_value());
    if (rq) {
        CHECK(rq->team_scoped);                              // ★ team-private plane
        CHECK(rq->team_id  == TEAM);
        CHECK(rq->origin   == 213);                          // our TEAM id, not a static node_id
        CHECK(rq->dst_id   == 174);
        CHECK_FALSE(rq->is_reply);
        CHECK(rq->ttl_or_next_hop == 1);                     // the cheap expanding-ring probe (spec §3/T1 t=0)
    }

    // The relay answers from its cached _rt_team route to 174.
    const std::vector<uint8_t> rqb = t1_first_f_bytes(ha);
    CHECK(!rqb.empty());
    hr.tx_frames.clear();
    if (!rqb.empty()) R.on_recv(rqb.data(), rqb.size(), from_a);
    CHECK(hr.count("rreq_resolved_cached") == 1);
    auto rp = t1_last_f(hr);
    CHECK(rp.has_value());
    if (rp) {
        CHECK(rp->is_reply);
        CHECK(rp->team_scoped);
        CHECK(rp->ttl_or_next_hop == 213);                   // unicast back to the asker
    }

    // A ingests the RREP -> the route to the never-heard teammate installs on the TEAM plane only.
    const std::vector<uint8_t> rpb = t1_first_f_bytes(hr);
    CHECK(!rpb.empty());
    if (!rpb.empty()) A.on_recv(rpb.data(), rpb.size(), from_r);
    CHECK(t1_in_team_rt(A, 174));                            // ★ discovered
    CHECK_FALSE(t1_in_static_rt(A, 174));                    // ★ R2 isolation: nothing entered the static plane
    CHECK(A.is_team_peer(174));

    // ...and the parked DM now flies, to the relay.
    ha.tx_frames.clear();
    A.on_timer(kDeferredDrainTimerId);
    CHECK(ha.count("send_drained") == 1);
    int rts_next = -1;
    for (const auto& f : ha.tx_frames) if (auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) rts_next = pr->next;
    CHECK(rts_next == 234);                                  // ★★ the DM takes the discovered 2-hop path via the relay
}

TEST_CASE("§team-parity T1 — the deferred-drain requery escalates to team_hop_cap on a team item, dv_hop_cap on a static one") {
    // node_cascade.cpp:317. Corpus reach at T0: 783 executions, team_rreq==false in ALL of them -> the team half is
    // corpus-dark and this test is its only detector.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(A, 213, TEAM);
    std::array<uint8_t,64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); A.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,static_cast<int8_t>(234)}); }
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 174; c.u.send.plane = 1 /*TEAM*/;
    c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
    CHECK(A.on_command(c).code == CmdCode::queued);
    { auto probe = t1_last_f(hal); CHECK(probe.has_value()); if (probe) CHECK(probe->ttl_or_next_hop == 1); }   // t=0: the ttl=1 probe
    hal.tx_frames.clear();
    hal._now += 1000;                                        // +1 s (send_defer_drain_period_ms)
    A.on_timer(kDeferredDrainTimerId);                       // still no route -> requery at full PLANE radius
    auto esc = t1_last_f(hal);
    CHECK(esc.has_value());
    if (esc) {
        CHECK(esc->team_scoped);
        CHECK(esc->ttl_or_next_hop == protocol::team_hop_cap);   // ★★ 8, not 16 — pre-T1 this was dv_hop_cap
    }
    CHECK(protocol::team_hop_cap != protocol::dv_hop_cap);   // the assertion above is non-vacuous by construction

    // Control, same site: a STATIC deferred item still escalates to dv_hop_cap.
    TestHal hs; Node S(hs, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; CHECK(S.on_init(cfg));
    std::array<uint8_t,64> sb{}; { const size_t n = mk_beacon(/*src=*/20, sb); S.on_recv(sb.data(), n, RxMeta{8.0f,-80.0f,0,20}); }
    send_cmd(S, /*dst=*/99, "hi");                           // unknown static dst -> defer + ttl=1 probe
    hs.tx_frames.clear();
    hs._now += 1000;
    S.on_timer(kDeferredDrainTimerId);
    auto sesc = t1_last_f(hs);
    CHECK(sesc.has_value());
    if (sesc) {
        CHECK_FALSE(sesc->team_scoped);
        CHECK(sesc->ttl_or_next_hop == protocol::dv_hop_cap);    // ★ static plane unchanged at 16
    }
}

TEST_CASE("§team-parity T1 — team cascade exhaustion re-floods at team_hop_cap, not dv_hop_cap") {
    // node_cascade.cpp:145 — ★★ THE GATE-BLIND SITE. 0/32 corpus executions at T0 AND at T1, with a same-site control
    // (cascade_to_alt is entered 1148 times, pt.plane == AUTO every time) proving it is genuinely dark. This test is
    // its ONLY detector.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(A, 213, TEAM);
    std::array<uint8_t,64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); A.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,static_cast<int8_t>(234)}); }
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 234; c.u.send.plane = 1 /*TEAM*/;
    c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
    CHECK(A.on_command(c).code == CmdCode::queued);          // a KNOWN teammate -> RTS flies immediately
    hal.tx_frames.clear();
    exhaust_rts_same_hop(A);                                 // the sole team candidate goes silent -> cascade_to_alt, no alt
    CHECK(hal.count("path_cascade") == 0);                   // there IS no alternate -> the team-exhaustion branch ran
    auto f = t1_last_f(hal);
    CHECK(f.has_value());
    if (f) {
        CHECK(f->team_scoped);                               // still team-scoped (isolation preserved)
        CHECK_FALSE(f->is_reply);
        CHECK(f->dst_id == 234);
        CHECK(f->ttl_or_next_hop == protocol::team_hop_cap); // ★★ 8 — pre-T1 this site flooded at the static 16
    }
}

TEST_CASE("§team-parity T1 — the team RREQ hop-cap and the team RREP backstop ride team_hop_cap (2x for the reply)") {
    // node_route_discovery.cpp:224 feeding :233 and :283. Measured with a direct counter: 3 team-plane executions in
    // the whole 32-scenario corpus, all in s28, at hops 0 (RREQ) and 1 (RREP ×2) — far below BOTH the old bound
    // (16 / 32) and the new one (8 / 16), so the corpus cannot observe the cap VALUE even where it executes the line.
    // Bounds under test: RREQ drops at hops >= 8 (was 16); RREP drops at hops > 16 (was 32).
    const uint32_t TEAM = 0x06EF37AEu;
    auto mk_team_f = [&](bool reply, uint8_t hops, uint8_t ttl_or_next, std::array<uint8_t,16>& buf) -> size_t {
        f_in in{}; in.leaf_id=0; in.origin=213; in.is_reply=reply; in.dst_id=174;
        in.ttl_or_next_hop=ttl_or_next; in.hops=hops; in.relay=200; in.config_hash=0;
        in.team_scoped=true; in.team_id=TEAM;
        return pack_f(in, std::span<uint8_t>(buf.data(), buf.size()));
    };
    RxMeta m{12.0f,-70.0f,0,static_cast<int8_t>(200)};
    // ---- RREQ guard: `f.hops >= hop_cap_for(team)`.
    auto rreq_dropped = [&](uint8_t hops) {
        TestHal hal; Node N(hal, /*id=*/234, /*key=*/0xA234u); t1_offgrid(N, 234, TEAM);
        std::array<uint8_t,16> fb{}; const size_t n = mk_team_f(/*reply=*/false, hops, /*ttl=*/4, fb);
        N.on_recv(fb.data(), n, m);
        return hal.count("rreq_drop_hop_cap") == 1;
    };
    CHECK_FALSE(rreq_dropped(protocol::team_hop_cap - 1));   // 7 hops: the deepest legal team RREQ, accepted
    CHECK(rreq_dropped(protocol::team_hop_cap));             // ★★ 8: dropped — pre-T1 the bound was 16, so this PASSED
    CHECK(rreq_dropped(protocol::dv_hop_cap));               // 16 still dropped (the static bound is a superset)
    // ---- RREP backstop: `f.hops > 2 * hop_cap_for(team)`. Addressed to us so the unicast gate lets it through.
    auto rrep_dropped = [&](uint8_t hops) {
        TestHal hal; Node N(hal, /*id=*/234, /*key=*/0xA234u); t1_offgrid(N, 234, TEAM);
        std::array<uint8_t,16> fb{}; const size_t n = mk_team_f(/*reply=*/true, hops, /*next_hop=*/234, fb);
        N.on_recv(fb.data(), n, m);
        return hal.count("rrep_drop_hop_cap") == 1;
    };
    CHECK_FALSE(rrep_dropped(2 * protocol::team_hop_cap));   // 16 = the legal worst case (8-hop cacher + 8-hop reverse)
    CHECK(rrep_dropped(2 * protocol::team_hop_cap + 1));     // ★★ 17: dropped — pre-T1 the bound was 32, so this PASSED
    // ---- Control, SAME code path with team==false: the static bounds are untouched.
    auto static_f = [&](bool reply, uint8_t hops, uint8_t ttl_or_next, std::array<uint8_t,16>& buf) -> size_t {
        f_in in{}; in.leaf_id=0; in.origin=99; in.is_reply=reply; in.dst_id=98;
        in.ttl_or_next_hop=ttl_or_next; in.hops=hops; in.relay=200; in.config_hash=0;
        return pack_f(in, std::span<uint8_t>(buf.data(), buf.size()));
    };
    auto static_node = [&](bool reply, uint8_t hops, uint8_t tn, const char* ev) {
        TestHal hal; Node N(hal, /*id=*/234, /*key=*/0xA234u);
        NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; CHECK(N.on_init(cfg));
        std::array<uint8_t,16> fb{}; const size_t n = static_f(reply, hops, tn, fb);
        N.on_recv(fb.data(), n, m);
        return hal.count(ev) == 1;
    };
    CHECK_FALSE(static_node(false, protocol::team_hop_cap,        4,   "rreq_drop_hop_cap"));   // ★ 8 hops still fine on the static plane
    CHECK      (static_node(false, protocol::dv_hop_cap,          4,   "rreq_drop_hop_cap"));   // 16 = the static bound
    CHECK_FALSE(static_node(true,  2 * protocol::team_hop_cap + 1, 234, "rrep_drop_hop_cap"));  // ★ 17 still fine on the static plane
    CHECK      (static_node(true,  2 * protocol::dv_hop_cap + 1,   234, "rrep_drop_hop_cap"));  // 33 = the static bound
}

TEST_CASE("§team-parity T1 — an off-grid member may `send -t <unknown id> -a`: the E2E-ACK gate is plane-aware, not is_team_peer-only") {
    // node_mac.cpp:68. The mobile_no_home refusal exists because a NON-team reply leg needs a routable home to stamp
    // as origin. A TEAM send has no such need — it stamps the team_local_id and the reverse ack rides _rt_team (the
    // RREQ laid the reverse path at every relay). Pre-T1 the gate keyed on is_team_peer alone, so `-t -a` to an
    // unheard teammate was refused mobile_no_home — the same chicken-and-egg as the send guard, one layer down.
    const uint32_t TEAM = 0x06EF37AEu;
    auto ack_send = [&](Node& n, uint8_t dst, uint8_t plane) {
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.plane = plane;
        c.u.send.flags = DATA_FLAG_E2E_ACK_REQ;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        return n.on_command(c);
    };
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(A, 213, TEAM);   // off-grid: NO home at all
      const CmdResult r = ack_send(A, /*dst=*/174, /*plane=*/1 /*TEAM*/);
      CHECK(r.code == CmdCode::queued);
      CHECK(r.ctr != 0);                                     // ★★ enqueue_data minted a ctr (0 = it refused)
      CHECK(hal.count("send_failed") == 0); }                // ★★ no mobile_no_home
    // Control: the SAME homeless mobile sending -a on the GLOBAL plane is still refused — the guard is intact.
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(A, 213, TEAM);
      const CmdResult r = ack_send(A, /*dst=*/174, /*plane=*/2 /*GLOBAL*/);
      CHECK(r.ctr == 0);                                     // ★ still refused
      CHECK(hal.count("send_failed") == 1);
      const Ev* e = hal.last("send_failed"); CHECK(e != nullptr); if (e) CHECK(e->dst == 174); }
}

TEST_CASE("§ack-gate-plane — a GLOBAL `-a` DM to an id that COLLIDES a teammate's team id is refused loud, not admitted as team") {
    // node_mac.cpp:89. THE ONE INPUT CELL the T1 case above cannot reach: its GLOBAL control uses dst=174, which is
    // NOT a _team_peer, so `is_team_peer(dst)` was false and both the old and the new expression gate it. The gap was
    // GLOBAL + is_team_peer(dst) TRUE — the §18 numeric collision. The pre-fix arm `plane == Plane::TEAM ||
    // is_team_peer(dst)` was plane-BLIND, so it admitted that flight as "team" and the mobile then stamped an
    // unroutable origin (stamp_origin's flight_is_team_plane() is false for GLOBAL -> `mob ? home_id : _node_id`, and
    // this branch is only reached when there is NO routable home) while rt_find(dst, GLOBAL) routed it on the STATIC
    // _rt. The ack could never come back: the send looked accepted and then silently timed out.
    // ★ WHY THIS IS THE DEFAULT PATH ON METAL, not an exotic case: lib/console/console_parse.cpp:259 is the ONLY site
    // in lib/ or src/ that assigns a DM's plane, and it emits `team ? TEAM : GLOBAL` — never AUTO. The companion/BLE
    // transports share that dispatch() and parser. So EVERY plain `send <id> -a` from a phone is GLOBAL, and the
    // colliding-id case needs nothing more than a teammate whose team-DAD id happens to equal the static id typed.
    // ⚠ CORPUS-DARK BY CONSTRUCTION: no scenario contains a static id that collides a live teammate's team id while
    // the sender is a homeless mobile, so this test is the only detector. (The sim could not even express the GLOBAL
    // half until §sim-plane-parity made the plain DM verbs emit GLOBAL.)
    const uint32_t TEAM = 0x06EF37AEu;
    const uint8_t  MATE = 234;                                 // the teammate's team_local_id == the id we address
    auto ack_send = [](Node& n, uint8_t dst, uint8_t plane) {
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.plane = plane;
        c.u.send.flags = DATA_FLAG_E2E_ACK_REQ;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        return n.on_command(c);
    };
    auto with_teammate = [&](Node& n, TestHal& hal, uint8_t id) {
        t1_offgrid(n, id, TEAM);                               // off-grid: is_mobile, NO home at all
        std::array<uint8_t,64> bb{};
        const size_t bn = t1_team_beacon(MATE, TEAM, bb);      // sets the _team_peer bit + the hops=1 _rt_team route
        n.on_recv(bb.data(), bn, RxMeta{12.0f,-70.0f,0,static_cast<int8_t>(MATE)});
        CHECK(n.is_team_peer(MATE));                           // ★ non-vacuity: the collision precondition really holds
        hal.events.clear();
    };
    // ★★ THE FIX: GLOBAL + a colliding teammate id + no routable home -> REFUSED LOUD.
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); with_teammate(A, hal, 213);
      const CmdResult r = ack_send(A, MATE, /*plane=*/2 /*GLOBAL*/);
      CHECK(r.ctr == 0);                                       // ★★ pre-fix this minted a ctr and "succeeded"
      CHECK(hal.count("send_failed") == 1);
      const Ev* e = hal.last("send_failed"); CHECK(e != nullptr); if (e) CHECK(e->dst == MATE); }
    // Control 1 (same site, same node, same dst): `-t` on the TEAM plane is STILL admitted — the narrowing touched
    // exactly one cell, and this is the cell T1 opened. Without this the test could not distinguish the fix from a
    // blanket re-tightening back to pre-T1.
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); with_teammate(A, hal, 213);
      const CmdResult r = ack_send(A, MATE, /*plane=*/1 /*TEAM*/);
      CHECK(r.code == CmdCode::queued);
      CHECK(r.ctr != 0);
      CHECK(hal.count("send_failed") == 0); }
    // Control 2 (same site, same dst): AUTO + is_team_peer is UNCHANGED by the fix — it is still admitted. This is the
    // arm that proves the new predicate is flight_is_team_plane() and not the blunter `plane != Plane::GLOBAL`.
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); with_teammate(A, hal, 213);
      const CmdResult r = ack_send(A, MATE, /*plane=*/0 /*AUTO*/);
      CHECK(r.code == CmdCode::queued);
      CHECK(r.ctr != 0);
      CHECK(hal.count("send_failed") == 0); }
    // Control 3: GLOBAL to a NON-colliding id is refused before and after — proves the refusal above is not simply
    // "GLOBAL is always refused for this node" but is the guard's ordinary no-home behaviour.
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); with_teammate(A, hal, 213);
      CHECK_FALSE(A.is_team_peer(/*dst=*/174));
      const CmdResult r = ack_send(A, /*dst=*/174, /*plane=*/2 /*GLOBAL*/);
      CHECK(r.ctr == 0);
      CHECK(hal.count("send_failed") == 1); }
    // Control 4: the guard's SECOND conjunct still saves a REGISTERED mobile — a routable home makes the GLOBAL
    // colliding-id send legitimate again, so the narrowing costs nothing to a homed member.
    { TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); with_teammate(A, hal, 213);
      A.test_set_my_mobile_reg(/*home_id=*/101, /*local_id=*/213);
      const CmdResult r = ack_send(A, MATE, /*plane=*/2 /*GLOBAL*/);
      CHECK(r.code == CmdCode::queued);
      CHECK(r.ctr != 0);
      CHECK(hal.count("send_failed") == 0); }
}

// ============================ §team-parity T2 (spec 2026-07-27 §3/T2) =========================================
// Neighbour-learning parity: the team plane gains the RX-event coverage the static plane has (2 learn sites -> 7),
// plus DATA-origin learning (owner-ruled IN, reversing the spec's own exclusion note — QA review §10.1).
// ★ EVERY case below asserts BOTH halves: the team route IS installed AND the static _rt/_id_bind are untouched
// (invariant I2), and the converse A2 (no STATIC id enters _rt_team) is the subject of the origin-gate cases.
// Corpus coverage of these sites is partial and the NACK row is corpus-DARK (0 executions in all 32 scenarios), so
// these tests are the only detector there — see the coverage matrix in the slice report.

namespace {
// A team RTS: mobile_src + addr_len=1, src = the sender's team_local_id, next = the addressed team id.
size_t t2_team_rts(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo, uint8_t plen, std::array<uint8_t, 16>& b,
                   int origin = -1, int ctr = -1) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = 0; in.payload_len = plen; in.addr_len = 1; in.mobile_src = true;
    // §hybrid-rts S2: name the flight the DATA will carry, else `handle_data` correctly refuses it.
    in.id = (origin >= 0 && ctr >= 0)
              ? rts_flight_identity_plain(static_cast<uint8_t>(origin), static_cast<uint16_t>(ctr))
              : mk_rts_id(src, ctr_lo);
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
// A team DATA: addr_len=1 so team_addr_for_us(next) holds; committed_hops is the from-origin hop count.
size_t t2_team_data(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin, uint8_t committed,
                    const char* body, std::array<uint8_t, 64>& b, uint8_t type = 0) {
    std::array<uint8_t, 32> inner{}; inner[0] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[1 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 1; in.flags = 0; in.type = type; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = committed; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 1 + bl);
    in.mac   = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
// The _rt_team entry for `dest` (nullptr if absent), so a test can pin next-hop AND hops.
const RtEntry* t2_team_rt(Node& n, uint8_t dest) {
    for (uint8_t i = 0; i < n.rt_team_count(); ++i) if (n.rt_team_at(i).dest == dest) return &n.rt_team_at(i);
    return nullptr;
}
// Drive an inbound team flight up to the DATA: RTS addressed to our team id (we CTS -> _pending_rx), then the DATA.
void t2_inbound_team_flight(Node& n, uint8_t me_team, uint8_t prev, uint8_t dst, uint8_t origin,
                            uint8_t committed, uint16_t ctr, uint8_t type = 0) {
    const RxMeta m{12.0f, -70.0f, 0, static_cast<int16_t>(prev)};   // int16_t, NOT int8_t: an id > 127 would go negative and silently disable src_hint
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    const size_t rn = t2_team_rts(prev, me_team, dst, static_cast<uint8_t>(ctr & 0x0F), /*plen=*/8, rb, origin, int(ctr));
    n.on_recv(rb.data(), rn, m);
    const size_t dn = t2_team_data(me_team, dst, ctr, origin, committed, "hi", db, type);
    n.on_recv(db.data(), dn, m);
}
}  // namespace

TEST_CASE("§team-parity T2 row 4b — DATA-ORIGIN learning installs a REAL multi-hop team route: hops = committed_hops + 1") {
    // The owner's ruling (QA §10.1) reversing the spec's exclusion note. Re-verified at source: frame_codec.h:595
    // carries committed_hops, incremented at every forward (node_mac_rx.cpp hb_new_committed).
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/60, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,60}); }
    { const size_t n = t1_team_beacon(/*src=*/70, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,70}); }
    CHECK(X.is_team_peer(70));
    { const RtEntry* e = t2_team_rt(X, 70); CHECK(e != nullptr); if (e) CHECK(e->candidates[0].hops == 1); }

    // A DM originated by 70, relayed by 60 (committed_hops=1 ⇒ 70 is 2 hops away), delivered to us (50).
    t2_inbound_team_flight(X, /*me_team=*/50, /*prev=*/60, /*dst=*/50, /*origin=*/70, /*committed=*/1, /*ctr=*/0x0021);
    const RtEntry* e = t2_team_rt(X, 70);
    CHECK(e != nullptr);
    if (e) {
        bool via60 = false;
        for (uint8_t i = 0; i < e->n; ++i) if (e->candidates[i].next_hop == 60 && e->candidates[i].hops == 2) via60 = true;
        CHECK(via60);                                       // ★★ hops == committed_hops + 1, next == the PREV-HOP
    }
    // ★ ISOLATION (I2): nothing about 70 or 60 entered the static plane.
    CHECK_FALSE(t1_in_static_rt(X, 70));
    CHECK_FALSE(t1_in_static_rt(X, 60));
}

// ===================== §team-parity T7 (2026-07-28) — the is_team_peer(origin) fence removed ===================
// T2 fenced the DATA-origin learn on is_team_peer(origin) because a HOMED member stamped its HOME's STATIC node id
// (measured: s28/s29 origin=101), so a never-heard origin could not be told from a static id. T6 made a team-plane
// flight stamp team_local_id() — the same two scenarios re-anchored on exactly that byte (origin 101 -> 233/196) —
// which retires the premise. The case below REPLACES the T2 test that pinned the fence: the behaviour it asserted is
// what T7 deliberately changes, so keeping it would pin the bug.
TEST_CASE("§team-parity T7 — THE BENCH CASE: a NEVER-HEARD teammate's id is learned from a relayed team DM (spec §0's smoking gun)") {
    // Spec §0: "213 *received* a message from 174 and the immediately following `routes` still showed n=1."
    // Post-T7 the receiver installs a REAL multi-hop team route to an origin it has never heard and has no DV route to.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(X, 213, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,234}); }
    CHECK(X.is_team_peer(234));                              // the relay: heard, 1 hop
    CHECK_FALSE(X.is_team_peer(174));                         // ★ the originator: NEVER heard, no beacon, no F, no route
    CHECK(t2_team_rt(X, 174) == nullptr);

    // 174 originates, 234 relays (committed_hops=1 => 174 is 2 hops out), addressed to our team id 213.
    t2_inbound_team_flight(X, /*me_team=*/213, /*prev=*/234, /*dst=*/213, /*origin=*/174, /*committed=*/1, /*ctr=*/0x0051);

    const RtEntry* e = t2_team_rt(X, 174);
    CHECK(e != nullptr);                                     // ★★ THE PAYOFF — pre-T7 this is nullptr
    if (e) {
        bool via234 = false;
        for (uint8_t i = 0; i < e->n; ++i) if (e->candidates[i].next_hop == 234 && e->candidates[i].hops == 2) via234 = true;
        CHECK(via234);                                       // hops = committed_hops + 1, next = the prev-hop
    }
    CHECK(X.is_team_peer(174));                              // ★★ the dispatch bit too, so rt_find(174, AUTO) reads _rt_team
    // ★ ISOLATION (I2): the team plane learned it; the static plane learned nothing about either id.
    CHECK_FALSE(t1_in_static_rt(X, 174));
    CHECK_FALSE(t1_in_static_rt(X, 234));
}

TEST_CASE("§team-parity T7 — and the reverse send then routes on the TEAM plane: rt_find(never-heard origin) resolves to _rt_team") {
    // The bench's other half: 213 could not `send 174 "..." -t` at all. The route installed above is what the reply uses;
    // this pins that it is the TEAM table that answers, since rt_find(dst, TEAM) hard-forces _rt_team with no fallback.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(X, 213, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,234}); }
    t2_inbound_team_flight(X, /*me_team=*/213, /*prev=*/234, /*dst=*/213, /*origin=*/174, /*committed=*/1, /*ctr=*/0x0052);
    CHECK(t2_team_rt(X, 174) != nullptr);

    hal.events.clear(); hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 174; c.u.send.plane = 1 /*TEAM*/;
    c.body = reinterpret_cast<const uint8_t*>("reply"); c.body_len = 5;
    const CmdResult r = X.on_command(c);
    CHECK(r.code == CmdCode::queued);                        // ★ not err_no_binding, and not deferred-with-no-route
    CHECK(r.ctr != 0);
    X.on_timer(0); X.on_timer(0);
    // ★★ THE R3 HALF: the flight goes out addressed to the TEAM next-hop, and NO static RREQ is raised for 174.
    CHECK(hal.count("r_tx") == 0);                           // no route-request flood at all
    const Ev* rts = hal.last("rts_tx");
    CHECK(rts != nullptr);
    if (rts) { CHECK(rts->dst == 174); CHECK(rts->next == 234); }
}

TEST_CASE("§team-parity T7 — the T6 FALLBACK ARM cannot install a static origin: a not-yet-DAD'd sender airs src=0, and learn_route_via refuses via==0") {
    // stamp_origin takes the team branch only when team_local_id() != 0 (node.h:865). `send -t` is refused before DAD
    // (node.cpp:1141) but an AUTO send to a team peer is not, and _team_peer bits are set before our own DAD completes
    // (node_beacon.cpp:776). Such a sender's RTS airs src = team_local_id() = 0 (node_mac.cpp:874) => _pending_rx->from
    // == 0 => learn_route_via returns on the via==0 sentinel (node_beacon.cpp:65).
    // ⚠ THE ORIGINAL CLAIM HERE ("the ONE reachable path", "provably cannot install") WAS OVER-BROAD, and the actual
    // bound is narrower — corrected 2026-07-28 (§team-parity T8) after measuring rather than reasoning:
    //   (a) `via == 0` is a bound on THIS producer at ONE hop only. The receive-side learn has NO namespace check on
    //       `origin` whatsoever: hand it the same frame with any valid non-zero `from` and it installs a STATIC id into
    //       `_rt_team` — pinned by the T8 case below, which is the honest statement of the residual.
    //   (b) it was NOT the only producer. `team_next` (node_mac.cpp:867) decided the WIRE plane from the NEXT HOP while
    //       stamp_origin decided the origin namespace from the PLANE, so a flight routed on the STATIC plane through a
    //       team-peer next hop aired as a team-plane frame with a static origin AND a valid non-zero src — no
    //       DAD-pending needed. T8 closes that at the sender; the two T8 cases below pin both halves.
    //   (c) whether a relay FORWARDS a src=0 frame (which would give this producer a valid `from` at hop 2) is still
    //       UNMEASURED: the flight is accepted and CTS'd (measured), but neither the src=0 arm nor a valid-src control
    //       relayed inside this single-node harness, so the control was vacuous and no conclusion is claimed.
    // This case remains the control for T7's removed fence: origin 101 with via==0 STILL does not install.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(X, 213, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,234}); }

    t2_inbound_team_flight(X, /*me_team=*/213, /*prev=*/0, /*dst=*/213, /*origin=*/101, /*committed=*/1, /*ctr=*/0x0053);
    CHECK(t2_team_rt(X, 101) == nullptr);                    // ★★ no static id in the team plane (s35's A2)
    CHECK_FALSE(X.is_team_peer(101));                        // ★★ and no dispatch bit, so rt_find(101) still reads _rt
    CHECK_FALSE(t1_in_static_rt(X, 101));
    CHECK_FALSE(X.is_team_peer(0));                          // the src=0 sentinel never enters the peer set either
}

// ===================== §team-parity T8 (2026-07-28) — the wire plane must be the routed plane =====================
// T6 made stamp_origin agree with rt_find ("the identity a flight CLAIMS cannot drift from the table it is ROUTED on").
// T8 is that same statement one level down, on the AIR: `team_next` (node_mac.cpp:867) decided whether the RTS/DATA go
// out as a TEAM-plane frame (addr_len=1, src=team_local_id(), mobile_src=1) from `is_team_peer(pt.next)` ALONE — the
// next hop — while stamp_origin decided the origin namespace from `flight_is_team_plane(plane, dst)` — the plane. The
// two diverge for any flight routed on the STATIC plane whose next hop happens to be a team peer, and the receiver's
// DATA-origin learn (T7, node_mac_rx.cpp:694) then installs `_rt_team[<static id>]`.
// ★ THE FIX IS AT THE SENDER BECAUSE IT CANNOT BE AT THE RECEIVER: team-DAD draws a local id from 17..254
// (node_mobile.cpp:191) and static node_ids are 1..254 — one numeric space, no discriminator, and neither RTS nor plain
// DATA carries a team_id (invariant I9). "Is this origin a team-namespace id?" is UNDECIDABLE at the receiver.
TEST_CASE("§team-parity T8 — THE RESIDUAL, stated honestly: the RECEIVE-side learn has NO namespace check on `origin`") {
    // Hand the unfenced learn a team-plane DATA whose origin is a STATIC node id and whose `from` is a VALID non-zero
    // teammate id, at ONE hop, and it installs. This is not a fix, it is the bound: T7's guard note claimed the T6
    // fallback arm "provably cannot install (via == 0)", which covers only via==0 — NOT a valid via with a static
    // origin. Every real producer of this frame is closed at the sender (the two cases below); this pins WHY that has
    // to be where it is closed, and it must fail loudly if anyone ever weakens the sender-side agreement again.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node R(hal, /*id=*/234, /*key=*/0xA234u); t1_offgrid(R, 234, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/174, TEAM, bb); R.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,174}); }
    CHECK(R.is_team_peer(174));
    // origin 17 = a STATIC node id; prev 93 = a valid non-zero team local id; committed 0 = one hop.
    t2_inbound_team_flight(R, /*me_team=*/234, /*prev=*/93, /*dst=*/174, /*origin=*/17, /*committed=*/0, /*ctr=*/0x0061);
    CHECK(t2_team_rt(R, 17) != nullptr);                     // ★★ IT INSTALLS — measured, not argued
    CHECK(R.is_team_peer(17));                               // ★★ and the dispatch bit too (learn_route_via :73)
    // Control: the SAME frame with via == 0 (T7's documented bound) is refused, so this case is not simply "any origin".
    { TestHal h2; Node R2(h2, /*id=*/234, /*key=*/0xA234u); t1_offgrid(R2, 234, TEAM);
      std::array<uint8_t, 64> b2{}; const size_t n = t1_team_beacon(174, TEAM, b2);
      R2.on_recv(b2.data(), n, RxMeta{12.0f,-70.0f,0,174});
      t2_inbound_team_flight(R2, 234, /*prev=*/0, 174, /*origin=*/17, /*committed=*/0, 0x0062);
      CHECK(t2_team_rt(R2, 17) == nullptr);
      CHECK_FALSE(R2.is_team_peer(17)); }
}

TEST_CASE("§team-parity T8 — THE FIX: a STATIC-plane flight through a team-peer next hop is aired STATIC, not as a team frame") {
    // ★★ THE MEASURED, SEAM-FREE REACHABILITY (no test_learn_route): two real beacons put `_rt[55].next = 238` in the
    // STATIC table and `_team_peer[238]` in the team plane for the same numeric id — the §18/I9 numeric collision (two
    // physical nodes sharing id 238: a static advertiser and an off-grid teammate whose node_id IS its team_local_id),
    // or one node whose is_mobile/team_id config changed between beacons (set_team_id never touches is_mobile,
    // node.cpp:413). Then a GLOBAL `send 55` — which is what `console_parse.cpp:259` emits for EVERY plain `send`, so
    // this is the default metal verb, not an exotic plane.
    // PRE-FIX (measured): RTS src=93 next=238 dst=55 addr_len=1 mobile_src=1, inner origin 17 => a team-plane frame
    // carrying a STATIC origin with a valid non-zero src => the receiver installs _rt_team[17]. POST-FIX: src=17,
    // addr_len=0 — a static frame, matching the table the flight was routed on and the origin stamp_origin chose.
    const uint32_t TEAM = 0x06EF37AEu;
    const uint8_t  MATE = 238;                               // the id that is BOTH a static next hop and a team peer
    TestHal hal; Node A(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(A.on_init(cfg));
    A.set_team_local_id(93);                                 // DUAL: node_id 17, team id 93 — so the two ids are tellable apart
    // (1) a plain NON-mobile beacon from 238 advertising a route to the static dest 55 -> the STATIC table
    { const beacon_entry ents[1] = { { /*dest=*/55, /*next=*/MATE, /*score_bucket=*/12, /*is_gateway=*/false, /*hops=*/1, /*degraded=*/false } };
      beacon_in tb{}; tb.leaf_id=0; tb.src=MATE; tb.key_hash32=0x7000u+MATE; tb.is_mobile=false;
      tb.entries = std::span<const beacon_entry>(ents, 1);
      std::array<uint8_t, 96> b{}; const size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
      CHECK(bn > 0);
      A.on_recv(b.data(), bn, RxMeta{12.0f,-70.0f,0,-1}); }
    CHECK(t1_in_static_rt(A, 55));                           // non-vacuity: the static route really exists...
    CHECK_FALSE(A.is_team_peer(MATE));                       // ...and 238 is not yet a team peer
    // (2) NOW the same numeric id turns up as a teammate -> the _team_peer dispatch bit
    { std::array<uint8_t, 64> bb{}; const size_t n = t1_team_beacon(MATE, TEAM, bb);
      A.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,-1}); }
    CHECK(A.is_team_peer(MATE));                             // ★ the divergence precondition holds
    CHECK(t1_in_static_rt(A, 55));                           // ★ and the static route survived it
    hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 55; c.u.send.plane = 2 /*GLOBAL*/;
    c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
    CHECK(A.on_command(c).code == CmdCode::queued);
    const uint8_t stamped = A.test_tx_origin(0);
    CHECK(stamped == 17);                                    // ★ a STATIC id (no mobile reg -> _node_id), never the team id 93
    A.on_timer(0); A.on_timer(0);
    bool saw = false;
    for (auto& f : hal.tx_frames) {
        auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (!pr || pr->next != MATE) continue;
        saw = true;
        CHECK(pr->addr_len == 0);                            // ★★ NOT a team-plane frame (pre-fix: 1)
        CHECK(pr->src == 17);                                // ★★ our STATIC node_id (pre-fix: the team id 93)
        CHECK_FALSE(pr->mobile_src);                         // ★★ not marked as a local-id src (pre-fix: true)
    }
    CHECK(saw);                                              // non-vacuity: the flight really went out via 238
}

TEST_CASE("§team-parity T8 — CONTROL: a genuine TEAM-plane flight to the same next hop is UNCHANGED (the narrowing is one cell)") {
    // Without this the fix could not be told from a blanket "never air a team frame". Same node, same next hop, same
    // teammate — only the flight's PLANE differs, which is exactly the cell T8 narrows.
    const uint32_t TEAM = 0x06EF37AEu;
    const uint8_t  MATE = 238;
    TestHal hal; Node A(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(A.on_init(cfg));
    A.set_team_local_id(93);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(MATE, TEAM, bb); A.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,-1}); }
    CHECK(A.is_team_peer(MATE));
    // one FRESH node per plane: a flight left awaiting its CTS blocks the next drain, which would make `saw` vacuous.
    for (uint8_t plane : { uint8_t(1) /*TEAM*/, uint8_t(0) /*AUTO -> is_team_peer(dst) true*/ }) {
        TestHal hal2; Node N(hal2, /*id=*/17, /*key=*/0x1717u);
        CHECK(N.on_init(cfg));
        N.set_team_local_id(93);
        { const size_t n = t1_team_beacon(MATE, TEAM, bb); N.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,-1}); }
        CHECK(N.is_team_peer(MATE));
        hal2.tx_frames.clear();
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = MATE; c.u.send.plane = plane;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        CHECK(N.on_command(c).code == CmdCode::queued);
        CHECK(N.test_tx_origin(0) == 93);                    // T6: a team-plane flight stamps our TEAM id
        N.on_timer(0); N.on_timer(0);
        bool saw = false;
        for (auto& f : hal2.tx_frames) {
            auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
            if (!pr || pr->next != MATE) continue;
            saw = true;
            CHECK(pr->addr_len == 1);                        // ★ still a team-plane frame
            CHECK(pr->src == 93);                            // ★ still our team local id
            CHECK(pr->mobile_src);
        }
        CHECK(saw);
    }
}

TEST_CASE("§team-parity T7 — a TYPED (APP) DATA still teaches nothing, and this now matters MORE: its ui->origin is a payload byte") {
    // !d.app was load-bearing under T2 and is more so under T7: with the origin fence gone it is the only thing between a
    // payload byte and _rt_team. Measured over the 35-scenario corpus: 14 gate entries carry app=1, and s28's four read
    // origin=4 — an id that would now install if !d.app were dropped.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(X, 213, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/234, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,234}); }
    t2_inbound_team_flight(X, /*me_team=*/213, /*prev=*/234, /*dst=*/213, /*origin=*/174, /*committed=*/1, /*ctr=*/0x0054,
                           /*type=*/DATA_TYPE_REMOTE_CMD);
    CHECK(t2_team_rt(X, 174) == nullptr);                    // ★ a never-heard id from a TYPED frame is still refused
    CHECK_FALSE(X.is_team_peer(174));
}

TEST_CASE("§team-parity T7 — a node with NO team plane (team_id == 0) is still inert at the unfenced site") {
    // Static reduction: for_team_data is false whenever team_id == 0 or _team_local_id == 0 (team_addr_for_us,
    // node.h:174), so dropping the origin fence cannot change one byte of static-plane behaviour.
    TestHal hal; Node S(hal, /*id=*/50, /*key=*/0xA050u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    CHECK(S.on_init(cfg));
    // The same frames, at a node whose team_id is 0: the addr_len=1 DATA is not "for us" on either plane.
    t2_inbound_team_flight(S, /*me_team=*/50, /*prev=*/234, /*dst=*/50, /*origin=*/174, /*committed=*/1, /*ctr=*/0x0055);
    CHECK_FALSE(t1_in_static_rt(S, 174));                    // ★ no static install from a team-shaped frame either
}

TEST_CASE("§team-parity T2 row 4b — a TYPED DATA (APP) teaches nothing: its inner is NOT the unicast layout, so ui->origin is a payload byte") {
    // !d.app is load-bearing, not caution. Measured in the corpus: 19 of 64 team DATA receptions are
    // AUTHORITATIVE_H_ANSWER (type 2) frames whose parse_unicast_inner "origin" reads 0 — a hash-bind payload byte.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
    std::array<uint8_t, 64> bb{};
    { const size_t n = t1_team_beacon(/*src=*/60, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,60}); }
    { const size_t n = t1_team_beacon(/*src=*/70, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,70}); }
    const RtEntry* before = t2_team_rt(X, 70); CHECK(before != nullptr);
    const uint8_t n_before = before ? before->n : 0;

    // Same shape as the passing case, but APP/type=REMOTE_CMD: byte 8 becomes TYPE and the "origin" byte moves.
    t2_inbound_team_flight(X, /*me_team=*/50, /*prev=*/60, /*dst=*/50, /*origin=*/70, /*committed=*/1, /*ctr=*/0x0041,
                           /*type=*/DATA_TYPE_REMOTE_CMD);
    const RtEntry* after = t2_team_rt(X, 70);
    CHECK(after != nullptr);
    if (after) {
        CHECK(after->n == n_before);                         // ★ no 2-hop candidate added
        for (uint8_t i = 0; i < after->n; ++i) CHECK(after->candidates[i].hops == 1);
    }
}

TEST_CASE("§team-parity T2 row 4 — a team DATA's PREV-HOP re-scores the TEAM route from the DATA-time SNR (the RTS arm already learned it: row 4 adds the SECOND sample)") {
    // ★ HONEST SCOPE, found by the non-vacuity run: row 4 is LARGELY SUBSUMED by the shipped RTS-addressed-to-us arm
    // (node_mac_rx.cpp:92). To hold a _pending_rx for a team DATA at all you must first have CTS'd its team RTS, and
    // that RTS is addressed to your team id — so the prev-hop is ALREADY learned by the time the DATA lands. What row 4
    // genuinely adds is a second, DATA-time sample: the DATA flies at the chosen data SF at a different SNR, and its
    // score/last_seen refresh the candidate. That is what this test pins; the pre-T2 build keeps the RTS-time score.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
    std::array<uint8_t, 64> bb{}; std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    { const size_t n = t1_team_beacon(/*src=*/60, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{2.0f,-110.0f,0,60}); }
    CHECK(X.is_team_peer(60));
    const RtEntry* e0 = t2_team_rt(X, 60); CHECK(e0 != nullptr);
    // RTS at the SAME poor SNR as the beacon (so the shipped arm cannot improve the score), DATA at a much better one.
    { const size_t n = t2_team_rts(/*src=*/60, /*next=*/50, /*dst=*/50, /*ctr_lo=*/1, /*plen=*/8, rb);
      X.on_recv(rb.data(), n, RxMeta{2.0f,-110.0f,0,60}); }
    const int16_t after_rts = t2_team_rt(X, 60) ? t2_team_rt(X, 60)->candidates[0].score : 0;
    { const size_t n = t2_team_data(/*next=*/50, /*dst=*/50, /*ctr=*/0x0001, /*origin=*/60, /*committed=*/0, "hi", db);
      X.on_recv(db.data(), n, RxMeta{20.0f,-60.0f,0,60}); }
    const RtEntry* e1 = t2_team_rt(X, 60);
    CHECK(e1 != nullptr);
    if (e1) CHECK(e1->candidates[0].score > after_rts);      // ★★ the DATA-time sample landed (pre-T2: unchanged)
    CHECK_FALSE(t1_in_static_rt(X, 60));                     // ★ I2
}

TEST_CASE("§team-parity T2 row 1 — an OVERHEARD team RTS from a KNOWN teammate refreshes the team plane; an UNKNOWN mobile src is admitted to NEITHER plane") {
    const uint32_t TEAM = 0x06EF37AEu;
    std::array<uint8_t, 64> bb{}; std::array<uint8_t, 16> rb{};
    // (a) known teammate 60, reachable only 2 hops via 70 -> an overheard RTS (addressed to 70, NOT to us) shortens it.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
      { const size_t n = t1_team_beacon(/*src=*/70, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,70}); }
      { uint8_t ext[8]; const size_t en = pack_team_id_tlv(TEAM, std::span<uint8_t>(ext, sizeof ext));
        beacon_entry e{}; e.dest = 60; e.next = 70; e.score_bucket = 12; e.hops = 1;
        beacon_in tb{}; tb.leaf_id=0; tb.src=70; tb.key_hash32=0x7046u; tb.is_mobile=true;
        tb.entries = std::span<const beacon_entry>(&e, 1); tb.ext = std::span<const uint8_t>(ext, en);
        const size_t n = pack_beacon(tb, std::span<uint8_t>(bb.data(), bb.size()));
        X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,70}); }
      { const RtEntry* e = t2_team_rt(X, 60); CHECK(e != nullptr); if (e) CHECK(e->candidates[0].hops == 2); }
      const size_t rn = t2_team_rts(/*src=*/60, /*next=*/70, /*dst=*/70, /*ctr_lo=*/3, /*plen=*/8, rb);
      X.on_recv(rb.data(), rn, RxMeta{12.0f,-70.0f,0,60});   // overheard: next is 70, not our 50
      { const RtEntry* e = t2_team_rt(X, 60); CHECK(e != nullptr); if (e) CHECK(e->candidates[0].hops == 1); }  // ★
      CHECK_FALSE(t1_in_static_rt(X, 60)); }                 // ★ I2
    // (b) an UNKNOWN mobile src (could be a foreign team, or a plain mobile's home last-mile) — the RTS carries no
    //     team id (frame_codec.h:289), so it must enter NEITHER plane. This is the narrowing the row-1 comment states.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
      const size_t rn = t2_team_rts(/*src=*/99, /*next=*/70, /*dst=*/70, /*ctr_lo=*/4, /*plen=*/8, rb);
      X.on_recv(rb.data(), rn, RxMeta{12.0f,-70.0f,0,99});
      CHECK(t2_team_rt(X, 99) == nullptr);                   // ★★ A2: no unknown id admitted to _rt_team
      CHECK_FALSE(X.is_team_peer(99));
      CHECK_FALSE(t1_in_static_rt(X, 99)); }                 // ★★ I2: nor to the static plane (unchanged pre-T2 guard)
}

TEST_CASE("§team-parity T2 rows 3/5/6 — CTS, ACK and NACK from our team next-hop each RE-SCORE the team route (and only it); ★ the NACK row is corpus-DARK, so this is its only detector") {
    // A team member holding a POOR-SNR direct route to teammate 60, with a live team flight whose next-hop is 60.
    // Each control frame arrives at a much better SNR: post-T2 the team candidate's score improves, pre-T2 it does not.
    // Corpus reach (measured over all 32 scenarios): CTS 69 executions, ACK 61, ★ NACK **0** — no team NACK ever
    // happens in the corpus, so byte-identity cannot see row 6 at all.
    const uint32_t TEAM = 0x06EF37AEu;
    auto build = [&](Node& X, int16_t& score_before) {
        t1_offgrid(X, 50, TEAM);
        std::array<uint8_t, 64> bb{};
        { const size_t n = t1_team_beacon(/*src=*/60, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{2.0f,-110.0f,0,60}); }
        CHECK(X.is_team_peer(60));
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 60; c.u.send.plane = 1 /*TEAM*/;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        CHECK(X.on_command(c).code == CmdCode::queued);
        const RtEntry* e = t2_team_rt(X, 60); CHECK(e != nullptr);
        score_before = e ? e->candidates[0].score : 0;
    };
    auto score_now = [](Node& X) { const RtEntry* e = t2_team_rt(X, 60); return e ? e->candidates[0].score : 0; };
    std::array<uint8_t, 8> fb{};
    const RxMeta good{20.0f, -60.0f, 0, 60};
    // (a) row 3 — CTS from the team next-hop.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); int16_t s0 = 0; build(X, s0);
      const size_t n = mk_cts(/*rx_id=*/50, /*tx_id=*/60, /*data_sf=*/7, fb);
      hal.events.clear();
      X.on_recv(fb.data(), n, good);
      CHECK(hal.count("cts_rx") == 1);                       // the CTS was accepted (the arm's precondition)
      CHECK(score_now(X) > s0);                              // ★★ row 3 fired (pre-T2: unchanged)
      CHECK_FALSE(t1_in_static_rt(X, 60)); }                 // ★ I2: the team next-hop stays out of _rt
    // (b) row 5 — ACK from the team next-hop, after the flight reaches awaiting_ack.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); int16_t s0 = 0; build(X, s0);
      { const size_t n = mk_cts(50, 60, 7, fb); X.on_recv(fb.data(), n, RxMeta{2.0f,-110.0f,0,60}); }
      X.on_timer(kCtsToDataGapTimerId);                      // fire the DATA -> the flight awaits an ACK
      const int16_t s1 = score_now(X);
      ack_in ai{}; ai.ctr_lo = 1; ai.budget_hint = 0; ai.snr_bucket = 0; ai.to = 50; ai.mobile_to = 1;
      const size_t n = pack_ack(ai, std::span<uint8_t>(fb.data(), fb.size()));
      hal.events.clear();
      X.on_recv(fb.data(), n, good);
      CHECK(hal.count("ack_rx") == 1);                       // the ACK was accepted (the arm's precondition)
      CHECK(score_now(X) > s1);                              // ★★ row 5 fired (pre-T2: unchanged)
      CHECK_FALSE(t1_in_static_rt(X, 60)); }                 // ★ I2
    // (c) row 6 — NACK from the team next-hop. ★★ 0 corpus executions: THE ONLY DETECTOR.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); int16_t s0 = 0; build(X, s0);
      nack_in ni{}; ni.reason = protocol::nack_reason_busy_rx; ni.ctr_lo = 1; ni.payload = 1; ni.to = 50; ni.mobile_to = 1;
      const size_t n = pack_nack(ni, std::span<uint8_t>(fb.data(), fb.size()));
      X.on_recv(fb.data(), n, good);
      CHECK(score_now(X) > s0);                              // ★★ row 6 fired (pre-T2: unchanged)
      CHECK(t2_team_rt(X, 60) != nullptr);
      CHECK_FALSE(t1_in_static_rt(X, 60)); }                 // ★★ I2 — the NACK arm never writes the static plane
}

TEST_CASE("§team-parity T2 row 7 — a mobile-marked Q from a KNOWN teammate refreshes the TEAM plane; from an UNKNOWN local id, neither plane") {
    // ★ The spec says this row "pairs with T4". It does not: node_channel.cpp:524/1181 already send a channel_pull Q
    // with mobile = is_mobile, and s28 carries 17 such receptions today.
    const uint32_t TEAM = 0x06EF37AEu;
    auto send_q = [](Node& n, uint8_t src, bool mobile){
        q_in qi{}; qi.leaf_id=0; qi.src=src; qi.dest=0xFF; qi.opcode=q_opcode::req_sync; qi.mobile=mobile;
        uint8_t qb[8]; const size_t qn = pack_q(qi, std::span<uint8_t>(qb, sizeof qb));
        n.on_recv(qb, qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(src)});
    };
    std::array<uint8_t, 64> bb{};
    // (a) a known teammate reachable 2 hops via 70 -> the Q shortens it to a direct team route.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
      { const size_t n = t1_team_beacon(/*src=*/70, TEAM, bb); X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,70}); }
      { uint8_t ext[8]; const size_t en = pack_team_id_tlv(TEAM, std::span<uint8_t>(ext, sizeof ext));
        beacon_entry e{}; e.dest = 60; e.next = 70; e.score_bucket = 12; e.hops = 1;
        beacon_in tb{}; tb.leaf_id=0; tb.src=70; tb.key_hash32=0x7046u; tb.is_mobile=true;
        tb.entries = std::span<const beacon_entry>(&e, 1); tb.ext = std::span<const uint8_t>(ext, en);
        const size_t n = pack_beacon(tb, std::span<uint8_t>(bb.data(), bb.size()));
        X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,70}); }
      { const RtEntry* e = t2_team_rt(X, 60); CHECK(e != nullptr); if (e) CHECK(e->candidates[0].hops == 2); }
      send_q(X, /*src=*/60, /*mobile=*/true);
      { const RtEntry* e = t2_team_rt(X, 60); CHECK(e != nullptr); if (e) CHECK(e->candidates[0].hops == 1); }  // ★
      CHECK_FALSE(t1_in_static_rt(X, 60)); }                 // ★ I2 (the pre-T2 guard, unchanged)
    // (b) an UNKNOWN mobile-marked src enters neither plane.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t1_offgrid(X, 50, TEAM);
      send_q(X, /*src=*/99, /*mobile=*/true);
      CHECK(t2_team_rt(X, 99) == nullptr);                   // ★★ A2
      CHECK_FALSE(t1_in_static_rt(X, 99)); }                 // ★★ I2
}

TEST_CASE("§team-parity T2 — a node with NO team plane (team_id == 0) is INERT at every activated site: static behaviour byte-for-byte") {
    // The runtime-inertness rule (spec §4): MR_FEAT_TEAM is compiled IN on native, so every new arm must be
    // is_team_peer/for_team_data-false when team_id == 0. This is the algebraic complement of the s18 tripwire.
    TestHal hal; Node S(hal, /*id=*/50, /*key=*/0xA050u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    CHECK(S.on_init(cfg));
    CHECK(S.team_local_id() == 0);
    std::array<uint8_t, 16> rb{};
    const size_t rn = t2_team_rts(/*src=*/60, /*next=*/70, /*dst=*/70, /*ctr_lo=*/3, /*plen=*/8, rb);
    S.on_recv(rb.data(), rn, RxMeta{12.0f,-70.0f,0,60});
    CHECK(S.rt_team_count() == 0);                           // ★ nothing on the team plane
    CHECK_FALSE(t1_in_static_rt(S, 60));                     // ★ and the pre-T2 mobile_src guard still holds
    q_in qi{}; qi.leaf_id=0; qi.src=60; qi.dest=0xFF; qi.opcode=q_opcode::req_sync; qi.mobile=true;
    uint8_t qb[8]; const size_t qn = pack_q(qi, std::span<uint8_t>(qb, sizeof qb));
    S.on_recv(qb, qn, RxMeta{8.0f,-80.0f,0,60});
    CHECK(S.rt_team_count() == 0);
    CHECK_FALSE(t1_in_static_rt(S, 60));
}

TEST_CASE("§team-parity T2 — THE RATCHET FIX: live team traffic refreshes _rt_team last_seen, so a busy teammate no longer ages out and loses its dispatch bit") {
    // Spec §0's "Aggravating" bullet: node_routing.cpp:489 clears the _team_peer bit when the last team route ages out,
    // "so every team route is a one-way ratchet toward permanently-unsendable". Pre-T2 only a same-team BEACON (15-min
    // periodic, dirty-only) could refresh a team route — DM traffic taught the plane nothing. T2's five learn rows all
    // run rt_merge, whose metadata-only path (node_routing.cpp:398) refreshes last_seen_ms even when the candidate is not
    // strictly better — which is exactly what age_out_stale_routes reads. ★ INVISIBLE TO THE CORPUS: no scenario runs
    // past rt_aging_ttl_neighbor_ms (45 min) with team traffic in flight, so this test is the only detector.
    const uint32_t TEAM = 0x06EF37AEu;
    const uint64_t TTL  = protocol::rt_aging_ttl_neighbor_ms;
    auto seed = [&](Node& X) {
        t1_offgrid(X, 50, TEAM);
        std::array<uint8_t, 64> bb{};
        const size_t n = t1_team_beacon(/*src=*/60, TEAM, bb);
        X.on_recv(bb.data(), n, RxMeta{12.0f,-70.0f,0,60});
        CHECK(X.is_team_peer(60));
    };
    // (a) SILENCE — the route ages out and the dispatch bit goes with it (the ratchet, unchanged by T2).
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); seed(X);
      hal._now += TTL + 1000;
      X.on_timer(kAgingTimerId);
      CHECK(t2_team_rt(X, 60) == nullptr);
      CHECK_FALSE(X.is_team_peer(60)); }                     // permanently-unsendable, pre- and post-T2
    // (b) TRAFFIC — an overheard team RTS at the TTL midpoint keeps it alive past the deadline. Pre-T2 this frame
    //     taught the team plane nothing and the route died exactly as in (a).
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); seed(X);
      std::array<uint8_t, 16> rb{};
      hal._now += TTL / 2;
      const size_t rn = t2_team_rts(/*src=*/60, /*next=*/70, /*dst=*/70, /*ctr_lo=*/3, /*plen=*/8, rb);
      X.on_recv(rb.data(), rn, RxMeta{12.0f,-70.0f,0,60});   // overheard, NOT addressed to us (row 1)
      hal._now += TTL / 2 + 1000;                            // now past the ORIGINAL deadline
      X.on_timer(kAgingTimerId);
      CHECK(t2_team_rt(X, 60) != nullptr);                   // ★★ the ratchet is broken
      CHECK(X.is_team_peer(60));                             // ★★ and the dispatch bit survives
      CHECK_FALSE(t1_in_static_rt(X, 60)); }                 // ★ I2
}

// ============================ §team-parity T4 (spec 2026-07-27 §3/T4) =========================================
// On-demand full-table pull for the team plane: a new `team_sync` Q opcode (0) + a 4-B team_id tail, the mobile
// refusal at the originator lifted for a team-scoped pull, and the originator antidote made plane-aware.
// ★ COVERAGE NOTE, measured not assumed: of the whole slice only ONE arm is corpus-visible — s28 fires exactly one
// team_sync (XH2 -> XO5 at t=660475) and that single frame is the sole source of all 18 of s28's delta events. The
// I7 refusal halves for a FOREIGN team and a not-yet-DAD'd member, the mixed-leaf exemption, and the loop-guard plane
// correction are corpus-DARK — these tests are their only detectors. (The static-node refusal half IS corpus-visible:
// s28's S3 receives the same frame at 660660 and emits nothing.)
namespace {
// A same-team TEAM_SYNC Q frame, as send_req_sync_q would air it.
size_t t4_team_sync_q(uint8_t leaf, uint8_t src, uint32_t team, std::array<uint8_t,16>& b) {
    q_in qi{}; qi.leaf_id = leaf; qi.src = src; qi.dest = 0xFF;
    qi.opcode = q_opcode::team_sync; qi.mobile = true; qi.team_id = team;
    return pack_q(qi, std::span<uint8_t>(b.data(), b.size()));
}
// A team member on an arbitrary leaf nibble (t1_offgrid pins leaf 0; the mixed-leaf cases need a choice).
void t4_member(Node& n, uint8_t leaf, uint8_t tlid, uint32_t team) {
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=leaf;
    cfg.is_mobile=true; cfg.team_id=team; cfg.sync_response_min_routes=0;
    CHECK(n.on_init(cfg));
    n.set_team_local_id(tlid);
}
}  // namespace

TEST_CASE("§team-parity T4 — ★ INVARIANT I7: a TEAM_SYNC is answered ONLY by a same-team member; everyone else spends NO state on it") {
    const uint32_t TEAM  = 0x33330001u;
    const uint32_t OTHER = 0x77770002u;
    std::array<uint8_t,16> qb{};
    const size_t qn = t4_team_sync_q(/*leaf=*/0, /*src=*/33, TEAM, qb);
    CHECK(qn == 8);
    const RxMeta from33{12.0f,-70.0f,0,static_cast<int8_t>(33)};

    // (a) ★ HALF ONE — a same-team, DAD'd member ANSWERS. The reply is emit_beacon("sync"), which self-selects the
    //     team plane, so no plane argument travels with the request.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t4_member(X, /*leaf=*/0, /*tlid=*/50, TEAM);
      X.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 1);
      CHECK(hal.count("sync_response_scheduled") == 1);
      CHECK(hal.count("sync_response_tx") == 0);             // scheduled, not fired — the jittered backoff still applies
      CHECK_FALSE(t1_in_static_rt(X, 33)); }                 // ★ I2: nothing on the static plane, ever

    // (b) ★★ HALF TWO — a STATIC node hears the identical frame and does NOTHING. No q_rx (so the responder dedup
    //     ring is never even consulted, let alone written), no response, no route on either plane. This is the half
    //     s28 measures live: its static S3 receives this very frame at t=660660 and emits nothing.
    { TestHal hal; Node S(hal, /*id=*/50, /*key=*/0xA050u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.sync_response_min_routes=0;
      CHECK(S.on_init(cfg));
      S.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 0);                         // ★★ dropped BEFORE the dedup ring and the emit
      CHECK(hal.count("sync_response_scheduled") == 0);
      CHECK(S.rt_team_count() == 0);
      CHECK_FALSE(t1_in_static_rt(S, 33)); }

    // (c) ★ a member of a DIFFERENT team ignores it — corpus-DARK (s28's Y team is out of range of the X team's
    //     one team_sync), so this is the only detector for the same_team half of the predicate.
    { TestHal hal; Node Y(hal, /*id=*/50, /*key=*/0xA050u); t4_member(Y, /*leaf=*/0, /*tlid=*/50, OTHER);
      Y.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 0);
      CHECK(hal.count("sync_response_scheduled") == 0);
      CHECK(Y.rt_team_count() == 0); }

    // (d) ★ a same-team member that has not finished team-DAD ignores it. Its own beacon would carry no team id, so
    //     answering would air an identity-only page that teaches the puller nothing.
    { TestHal hal; Node M(hal, /*id=*/50, /*key=*/0xA050u); t4_member(M, /*leaf=*/0, /*tlid=*/0, TEAM);
      M.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 0);
      CHECK(hal.count("sync_response_scheduled") == 0); }

    // (e) ★ a NON-MOBILE node carrying our team_id ignores it. `team <id>` on the console sets team_id without
    //     is_mobile; such a node's emit_beacon would air its STATIC table under a team-tagged src.
    { TestHal hal; Node N(hal, /*id=*/50, /*key=*/0xA050u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.team_id=TEAM; cfg.sync_response_min_routes=0;
      CHECK(N.on_init(cfg));
      N.set_team_local_id(50);
      N.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 0);
      CHECK(hal.count("sync_response_scheduled") == 0); }
}

TEST_CASE("§team-parity T4 — the MIXED-LEAF exemption: a same-team TEAM_SYNC crosses nibbles; every other Q kind still drops") {
    // Mixed-leaf teams are supported by design (node_beacon.cpp:491-515), s29 runs one, and the bench config that
    // shipped to metal was leaf 4 vs leaf 7. Without the exemption a mixed-leaf member's pull is dropped at handle_q's
    // cross-network filter BEFORE the handler, so it is answered only by teammates sharing its nibble.
    // ★ CORPUS-DARK: every corpus team_sync is same-leaf, so this case is its only detector.
    const uint32_t TEAM = 0x33330001u;
    std::array<uint8_t,16> qb{};
    const size_t qn = t4_team_sync_q(/*leaf=*/4, /*src=*/33, TEAM, qb);      // sender on nibble 4
    const RxMeta from33{12.0f,-70.0f,0,static_cast<int8_t>(33)};

    // (a) ★ a same-team member on nibble 7 ANSWERS a nibble-4 pull.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t4_member(X, /*leaf=*/7, /*tlid=*/50, TEAM);
      X.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("sync_response_scheduled") == 1); }

    // (b) ★ THE CONTROL that keeps the exemption honest — the SAME node, the SAME foreign nibble, a REQ_SYNC instead
    //     of a TEAM_SYNC: still dropped. The exemption is opcode-scoped, not a general leaf relaxation.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); t4_member(X, /*leaf=*/7, /*tlid=*/50, TEAM);
      q_in ri{}; ri.leaf_id = 4; ri.src = 33; ri.dest = 0xFF; ri.opcode = q_opcode::req_sync; ri.mobile = true;
      uint8_t rb[8]; const size_t rn = pack_q(ri, std::span<uint8_t>(rb, sizeof rb));
      X.on_recv(rb, rn, from33);
      CHECK(hal.count("q_rx") == 0);
      CHECK(hal.count("sync_response_scheduled") == 0); }

    // (c) ★ THE SECOND CONTROL — a STATIC node on nibble 7 drops the nibble-4 TEAM_SYNC. The exemption never widens
    //     the cross-network filter for anyone who is not on the team.
    { TestHal hal; Node S(hal, /*id=*/50, /*key=*/0xA050u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=7; cfg.sync_response_min_routes=0;
      CHECK(S.on_init(cfg));
      S.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 0);
      CHECK(hal.count("sync_response_scheduled") == 0); }
}

TEST_CASE("§team-parity T4 — the loop guard is plane-correct: a HOMED member whose node_id collides the requester's TEAM id still answers") {
    // §18: a homed member's _node_id is a home-assigned STATIC-plane local id and can be numerically equal to a
    // teammate's team_local_id. Comparing `q.src == _node_id` on a team_sync would make exactly that responder go
    // silent — a suppress-direction plane collision. ★ CORPUS-DARK (no corpus team_sync collides), only detector.
    const uint32_t TEAM = 0x33330001u;
    std::array<uint8_t,16> qb{};
    const size_t qn = t4_team_sync_q(/*leaf=*/0, /*src=*/33, TEAM, qb);      // requester's TEAM id is 33
    const RxMeta from33{12.0f,-70.0f,0,static_cast<int8_t>(33)};

    // (a) ★ node_id 33 (its static-plane local id) but team_local_id 50 — a DIFFERENT node from the requester.
    { TestHal hal; Node X(hal, /*id=*/33, /*key=*/0xA033u); t4_member(X, /*leaf=*/0, /*tlid=*/50, TEAM);
      X.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("sync_response_scheduled") == 1); }                    // ★ answers, despite the id collision

    // (b) the guard still WORKS on the plane it belongs to: our own team id echoed back is ignored.
    { TestHal hal; Node X(hal, /*id=*/17, /*key=*/0xA017u); t4_member(X, /*leaf=*/0, /*tlid=*/33, TEAM);
      X.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("q_rx") == 0);
      CHECK(hal.count("sync_response_scheduled") == 0); }
}

TEST_CASE("§team-parity T4 — the originator antidote is PLANE-AWARE: a team send with no route fires a TEAM_SYNC alongside the RREQ; a static send fires a static REQ_SYNC") {
    // node_mac.cpp's Wave-4 antidote reuses the flight's own `team_route` decision, so the pull and the route lookup
    // can never disagree about which plane is missing a route. This is the end-to-end shape s28 exhibits at t=660475
    // (q_tx opcode 0 and r_tx reason team_no_route in the same instant).
    const uint32_t TEAM = 0x33330001u;
    auto last_q = [](const TestHal& h) {
        std::optional<q_out> r;
        for (const auto& f : h.tx_frames)
            if (auto p = parse_q(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) r = *p;
        return r;
    };
    auto send = [](Node& n, uint8_t dst, uint8_t plane) {
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst; c.u.send.plane = plane;
        c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
        return n.on_command(c);
    };
    // (a) ★ TEAM plane, no route -> a TEAM_SYNC under our team id, scoped, plus the team RREQ (T1's F).
    { TestHal hal; Node X(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(X, 213, TEAM);
      CHECK(send(X, /*dst=*/174, /*plane=*/1).code == CmdCode::queued);
      auto q = last_q(hal);
      CHECK(q.has_value());
      if (q) { CHECK(q->opcode == static_cast<uint8_t>(q_opcode::team_sync));   // ★★ the plane-aware antidote
               CHECK(q->src == 213);                                            // our team_local_id
               CHECK(q->team_id == TEAM); }
      CHECK(t1_last_f(hal).has_value()); }                                      // ★ the RREQ still flies (they compose)

    // (b) ★ THE STATIC CONTROL — the identical no-route shape on a static node still fires a plain REQ_SYNC (opcode 1,
    //     4 bytes, no tail). This is what s18's byte-identity rests on, pinned locally so a mistake cannot hide.
    { TestHal hal; Node S(hal, /*id=*/5, /*key=*/0xA005u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.req_sync_on_boot=false;
      CHECK(S.on_init(cfg));
      CHECK(send(S, /*dst=*/99, /*plane=*/2).code == CmdCode::queued);
      auto q = last_q(hal);
      CHECK(q.has_value());
      if (q) { CHECK(q->opcode == static_cast<uint8_t>(q_opcode::req_sync));    // ★ NOT team_sync
               CHECK(q->src == 5); CHECK(q->team_id == 0); } }
}

// ============================ §B4 (register B4) — the sync-response route count is PLANE-SCOPED ==================
// `schedule_sync_response` read the STATIC `_rt_count` for BOTH its route-starved skip AND its `rt_total`, and
// `sync_response_fire` held a THIRD copy of the same read. T4 marked it ⚠ MISSING and declined the plane parameter.
// ★★ THE SKIP HALF IS CORPUS-DARK BY CONSTRUCTION, measured not assumed: `sync_response_min_routes` defaults to 0,
// `route_n < 0` is never true for an unsigned, and NOTHING in either repo raises the knob (it has no console / NV /
// remote-admin surface at all) ⇒ `sync_response_skip` fires in 0 of 36 corpus scenarios. **These cases are its only
// detector.** The `rt_total` half IS corpus-visible — 2 movers (s35a, s38), 2 field values each.
namespace {
// An off-grid team member with the route-starved knob RAISED — the configuration the corpus never runs.
void b4_member(Node& n, uint32_t team, uint8_t min_routes) {
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0;
    cfg.is_mobile=true; cfg.team_id=team; cfg.sync_response_min_routes=min_routes;
    CHECK(n.on_init(cfg));
    n.set_team_local_id(50);
}
}  // namespace

TEST_CASE("§B4 — the route-starved skip is PLANE-SCOPED: an OFF-GRID member holding team routes is no longer MUTED on a team pull") {
    const uint32_t TEAM = 0x33330001u;
    std::array<uint8_t,16> qb{};
    const size_t qn = t4_team_sync_q(/*leaf=*/0, /*src=*/33, TEAM, qb);
    const RxMeta from33{12.0f,-70.0f,0,static_cast<int8_t>(33)};
    std::array<uint8_t,64> bb{};
    auto hear_team = [&](Node& n, uint8_t src) {
        const size_t bn = t1_team_beacon(src, TEAM, bb);
        n.on_recv(bb.data(), bn, RxMeta{12.0f,-70.0f,0,static_cast<int8_t>(src)});
    };

    // (a) ★★ THE BUG. Two TEAM routes, ZERO static routes — an off-grid member never registers with a static host, so
    //     its `_rt_count` is 0 FOREVER. Pre-fix the gate read 0 < 2 and emitted `sync_response_skip`: the member went
    //     silent on the one plane it is a responder for, no matter how full its `_rt_team` was.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); b4_member(X, TEAM, /*min_routes=*/2);
      hear_team(X, 60); hear_team(X, 61);
      CHECK(X.rt_team_count() == 2);
      CHECK(X.rt_count() == 0);                              // ★ the whole point: the static plane is empty, forever
      X.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("sync_response_skip") == 0);           // ★★ NOT muted
      CHECK(hal.count("sync_response_scheduled") == 1);
      const Ev* s = hal.last("sync_response_scheduled");
      CHECK(s); if (s) CHECK(s->rt_total == 2);              // ★ the TEAM count, not the static 0
      // ...and the THIRD reader agrees: the fire reports the same plane the scheduler gated on.
      X.on_timer(kSyncResponseTimerId);
      CHECK(hal.count("sync_response_tx") == 1);
      const Ev* t = hal.last("sync_response_tx");
      CHECK(t); if (t) CHECK(t->rt_total == 2); }            // ★ pre-fix: 0

    // (b) ★ THE SAME-SITE CONTROL — one team route, still below the two the knob demands: the gate STILL BITES. Without
    //     this, (a) would pass just as well if the fix had simply deleted the skip.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); b4_member(X, TEAM, /*min_routes=*/2);
      hear_team(X, 60);
      CHECK(X.rt_team_count() == 1);
      X.on_recv(qb.data(), qn, from33);
      CHECK(hal.count("sync_response_skip") == 1);
      CHECK(hal.count("sync_response_scheduled") == 0);
      const Ev* s = hal.last("sync_response_skip");
      CHECK(s); if (s) CHECK(s->rt_total == 1); }            // ★ it reports the TEAM count it actually gated on

    // (c) ★★★ THE PLANE-DERIVATION TRAP, and the reason `team_plane` is a CALLER argument. A homed/team-active member
    //     is team-active WHILE answering a STATIC REQ_SYNC, so deriving the plane inside the callee from
    //     team_active()/is_mobile/_cfg.team_id would answer this static pull with the TEAM count — one plane breach
    //     turned into two (C3). Not hypothetical: 12 corpus events in 5 scenarios (s22 3, s28 4, s29 2, s35a 2, s38 1)
    //     are static-plane sync responses from a node that holds team routes, 9 of them with a differing team count.
    { TestHal hal; Node X(hal, /*id=*/50, /*key=*/0xA050u); b4_member(X, TEAM, /*min_routes=*/3);
      hear_team(X, 60); hear_team(X, 61); hear_team(X, 62);
      CHECK(X.rt_team_count() == 3);                         // team plane is AT the threshold -> a derived plane passes
      q_in ri{}; ri.leaf_id=0; ri.src=33; ri.dest=0xFF; ri.opcode=q_opcode::req_sync; ri.mobile=false;
      uint8_t rb[8]; const size_t rn = pack_q(ri, std::span<uint8_t>(rb, sizeof rb));
      X.on_recv(rb, rn, from33);
      CHECK(hal.count("sync_response_skip") == 1);           // ★★ the STATIC count gated it, as it must
      CHECK(hal.count("sync_response_scheduled") == 0);
      const Ev* s = hal.last("sync_response_skip");
      CHECK(s); if (s) { CHECK(s->rt_total == static_cast<int>(X.rt_count()));      // the STATIC table...
                         CHECK(s->rt_total < static_cast<int>(X.rt_team_count())); } }   // ...and it is NOT the team one
}

// ✖ NO "the _rt_team-route ⇒ _team_peer-bit invariant holds" TEST HERE, deliberately. One was written and DELETED as
// vacuous: it could not be falsified by any of the three T2 mutants (pre-T2, over-broad, and "learn_route_via drops the
// bit"), because every T2 site is gated on is_team_peer already being true, so the bit is never newly set on this path.
// The invariant IS pinned — mutant 3 fails two PRE-EXISTING cases ("§team-multihop Plane 2" and the T1 bench case) —
// and the asymmetry that makes a future caller vulnerable is recorded in-source at node_beacon.cpp's learn_direct_neighbor.

TEST_CASE("§mobile — a MOBILE-marked Q's src (a home-assigned LOCAL id) is NEVER learned into the static _rt; a static Q IS (the dest=17 bench leak)") {
    auto learned = [](Node& n, uint8_t d){ for (uint8_t i=0;i<n.rt_count();++i) if (n.rt_at(i).dest==d) return true; return false; };
    auto send_q = [](Node& n, uint8_t src, bool mobile){
        q_in qi{}; qi.leaf_id=5; qi.src=src; qi.dest=0xFF; qi.opcode=q_opcode::req_sync; qi.mobile=mobile;
        uint8_t qb[8]; size_t qn = pack_q(qi, std::span<uint8_t>(qb, sizeof qb));
        n.on_recv(qb, qn, RxMeta{8.0f,-80.0f,0,static_cast<int8_t>(src)});
    };
    // (a) a MOBILE-marked REQ_SYNC (the mobile's src=17 is a home-assigned local id) must NOT leak into the static plane
    { TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=5; CHECK(node.on_init(cfg));
      send_q(node, /*src=*/17, /*mobile=*/true);
      CHECK_FALSE(learned(node, 17)); }                       // ★ no dest=17 in the static _rt (the fixed leak)
    // (b) a NORMAL static REQ_SYNC from the same id IS learned (behaviour unchanged for static peers)
    { TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
      NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=5; CHECK(node.on_init(cfg));
      send_q(node, /*src=*/17, /*mobile=*/false);
      CHECK(learned(node, 17)); }                             // a static Q sender is a routable neighbour
}

// §mobile — the ACK to a mobile/team ORIGINATOR must carry mobile_to=1 (else the originator's gate rejects it, so EVERY
// mobile/team-originated DM fails at the ACK step). The receiver sets mobile_to from the mobile_src RTS (via PendingRx.mobile_from).
TEST_CASE("§mobile — a receiver ACKs a mobile_src originator with mobile_to=1 (so a mobile/team-originated DM completes)") {
    TestHal hal;
    Node R(hal, /*id=*/2, /*key=*/0x0002u);                  // the next-hop / dest that ACKs the originator
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; CHECK(R.on_init(cfg));
    RxMeta meta{8.0f,-80.0f,0,static_cast<int8_t>(20)};      // originator = a mobile/team member with LOCAL id 20
    rts_in r{}; r.leaf_id=0; r.src=20; r.next=2; r.ctr_lo=5; r.dst=2; r.sf_index=0; r.payload_len=6; r.mobile_src=true;  // ★ mobile_src RTS
    r.id = rts_flight_identity_plain(20, 0x0005);   // §hybrid-rts S1: matches the mk_data below (origin 20, ctr 5)
    uint8_t rb[11]; size_t rn = pack_rts(r, std::span<uint8_t>(rb, sizeof rb));
    hal._now=1000; R.on_recv(rb, rn, meta);
    CHECK(hal.count("rts_rx") == 1);
    std::array<uint8_t,64> db{}; size_t dn = mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/20, "hi", db);
    hal._now=2000; R.on_recv(db.data(), dn, meta);
    CHECK(hal.count("ack_tx") == 1);
    bool got_ack=false;
    for (auto& f : hal.tx_frames) { auto pa = parse_ack(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pa && pa->to == 20) { got_ack=true; CHECK(pa->mobile_to); } }   // ★ mobile_to set -> the mobile originator accepts the ACK
    CHECK(got_ack);
}

TEST_CASE("§mobile — H mobile_req + NACK mobile_to wire bits round-trip (backward-compat rsv bits; plain frame -> 0)") {
    // H mobile_req (byte-7 b3) — the requester's origin is a LOCAL id -> owner skips id_bind
    { h_in in{}; in.leaf_id=4; in.origin=17; in.query_key32=0xABCDu; in.ttl=3; in.mobile_req=true;
      uint8_t buf[8]; size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof buf));
      auto o = parse_h(std::span<const uint8_t>(buf, n)); CHECK(o.has_value());
      if (o) { CHECK(o->mobile_req); CHECK_FALSE(o->team_scoped); CHECK_FALSE(o->hard); CHECK_FALSE(o->want_pubkey); } }
    { h_in in{}; in.leaf_id=4; in.origin=17; in.query_key32=0xABCDu; in.ttl=3;   // plain H -> mobile_req false
      uint8_t buf[8]; size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof buf));
      auto o = parse_h(std::span<const uint8_t>(buf, n)); CHECK(o.has_value());
      if (o) CHECK_FALSE(o->mobile_req); }
    // NACK mobile_to (byte-1 b0) — the `to` is a LOCAL id; a colliding static ignores it
    { nack_in in{}; in.reason=3; in.ctr_lo=0x0A; in.payload=42; in.to=17; in.mobile_to=true;
      uint8_t buf[4]; size_t n = pack_nack(in, std::span<uint8_t>(buf, sizeof buf));
      auto o = parse_nack(std::span<const uint8_t>(buf, n)); CHECK(o.has_value());
      if (o) { CHECK(o->mobile_to); CHECK(o->ctr_lo == 0x0A); CHECK(o->to == 17); CHECK(o->payload == 42); } }
    { nack_in in{}; in.reason=3; in.ctr_lo=0x0A; in.payload=42; in.to=17;   // plain NACK -> mobile_to false + ctr_lo intact
      uint8_t buf[4]; size_t n = pack_nack(in, std::span<uint8_t>(buf, sizeof buf));
      auto o = parse_nack(std::span<const uint8_t>(buf, n)); CHECK(o.has_value());
      if (o) { CHECK_FALSE(o->mobile_to); CHECK(o->ctr_lo == 0x0A); } }
}

// §18 plane-separation re-audit (spec §9.5): a mobile/team flight whose next-hop is a LOCAL id must not pollute the STATIC
// liveness plane when it TIMES OUT (the class the RX-only audit missed — the write is in the timer handler, node_cascade.cpp).
// This is the test that would have caught it. FALSIFIABILITY control below.
TEST_CASE("§18 — a TEAM flight's LOCAL-id next-hop timing out does NOT suspect the static _peer_liveness plane; a static flight DOES (control)") {
    // (leak path) a team node with a TEAM peer at LOCAL id 40; a team DM to 40 that gives up must NOT record liveness on 40
    TestHal hal;
    Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u; CHECK(node.on_init(cfg));
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=40; tb.key_hash32=0x4040u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, RxMeta{12.0f,-70.0f,0,static_cast<int8_t>(40)});
    CHECK(node.is_team_peer(40));
    CHECK(node.peer_suspect_level(40) == 0);
    send_cmd(node, /*dst=*/40, "hi");                 // team DM -> RTS to 40 (addr_len=1, is_team_peer)
    exhaust_rts_same_hop(node);                       // same-hop retries exhausted -> the giveup path (guarded record_peer_rts_timeout)
    // ★ 40 (a team LOCAL id) is NOT suspected. FALSIFIABILITY: revert the node_cascade.cpp guard -> record_peer_rts_timeout(40)
    //   fires -> peer_suspect_level(40) >= 1 -> this CHECK FAILS. (Same shape guards _link_bidi / _blind_until / budget.)
    CHECK(node.peer_suspect_level(40) == 0);
    // (positive control) a STATIC flight's GLOBAL next-hop IS suspected -> the guard is scoped to local-id flights, not a blanket skip
    TestHal h2; Node* stat = mk_sender_with_routes(h2, {{2,1,14}});   // static node, dest 5 via global next 2
    send_cmd(*stat, /*dst=*/5, "hi");
    exhaust_rts_same_hop(*stat);
    CHECK(stat->peer_suspect_level(2) >= 1);          // a static next-hop giveup DOES record liveness evidence (proves teeth)
    delete stat;
}

// =============================================================================
// ★★★★★ §hybrid-rts S4 (2026-08-09) — THE SENDER-SIDE IMPLICIT-ACK, RESTORED AND KEYED ON THE S1 IDENTITY.
// Design: docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md §5.2 · plan §S4 ·
// owner ruling `docs/2026-08-05-owner-rulings-ledger.md` §1.10.
//
// ⛔⛔ WHAT WAS HERE, AND WHY IT IS REPLACED RATHER THAN DELETED QUIETLY. A single [[B157]] case asserted that
// an overheard forward-RTS matching `next/dst/ctr_lo + payload_len` must NOT cancel our pending flight, and it
// stayed GREEN through S4 for the wrong reason: `mk_rts` gave that frame a PLACEHOLDER identity, so it was
// exercising the mismatch path while claiming to prove the mechanism was absent. ⇒ **a case whose green could
// not distinguish "the feature is gone" from "this particular frame did not match" is not a detector.** The
// contract it protected is preserved BELOW, as an explicit one-bit / same-old-tuple refusal with a positive
// control beside it, so a build that lost the identity comparison goes red rather than green.
//
// ⚠ ⛔ NEITHER CREDIT BASIS IS DELIVERY EVIDENCE (ruling §1.10, verbatim: *"A mismatch may be billed as physical
//   airtime, but must not refresh liveness or alter timers, routing, pending state, or application outcomes."*).
//   Every case below asserts the ABSENCE of `send_acked` / `send_e2e_acked` / `send_failed`, because the one way
//   this restoration could be wrong is by looking like a success.
// =============================================================================
namespace {
// ★ §hybrid-rts S4 — a unicast RTS carrying an EXPLICIT identity and EXPLICIT wire marks. The credit cases need
// both: the identity so a forward can be made to match (or to miss by one bit), and the marks so the wire-declared
// plane can be varied WITHOUT changing any numeric field.
static size_t mk_rts_wire(uint8_t src, uint8_t next, uint8_t dst, uint8_t ctr_lo, uint8_t plen,
                          std::array<uint8_t, 16>& b, const RtsFlightIdentity& id,
                          uint8_t addr_len = 0, bool mobile_src = false, uint8_t rts_flags = 0) {
    rts_in in{}; in.leaf_id = 0; in.src = src; in.next = next; in.ctr_lo = ctr_lo; in.dst = dst;
    in.sf_index = 3; in.rts_flags = rts_flags; in.payload_len = plen;
    in.addr_len = addr_len; in.mobile_src = mobile_src; in.id = id;
    return pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
}
// The shape of the unicast RTS WE most recently aired, read OFF THE WIRE. ⛔ A case that restated the identity
// derivation would be asserting its own arithmetic; this asserts the frame the receiver would actually see, which
// is also what makes the "same identity" arms honest.
struct OurRts { bool got = false; uint8_t src = 0, next = 0, dst = 0, ctr_lo = 0, plen = 0, addr_len = 0;
                bool mobile_src = false; RtsFlightIdentity id{}; };
static OurRts our_last_rts(const TestHal& hal) {
    OurRts o{};
    for (const auto& f : hal.tx_frames) {
        auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (!pr || pr->m_broadcast) continue;                       // unicast only (an M/FLOOD RTS carries no identity)
        o.got = true; o.src = pr->src; o.next = pr->next; o.dst = pr->dst; o.ctr_lo = pr->ctr_lo;
        o.plen = pr->payload_len; o.addr_len = pr->addr_len; o.mobile_src = pr->mobile_src; o.id = pr->id;
    }
    return o;
}
// "No app-facing outcome was invented." Drains the push ring and reports whether ANY terminal send outcome appeared.
static bool any_send_outcome_push(Node& n) {
    Push p{}; bool any = false;
    while (n.next_push(p))
        if (p.kind == PushKind::send_acked || p.kind == PushKind::send_failed
            || p.kind == PushKind::send_e2e_acked) any = true;
    return any;
}
// A sender at id 1 with a TWO-HOP route to `dst` via `next`, so an overheard forward from `next` to a third node
// is a genuine downstream forward rather than a frame addressed back at the destination.
static Node* s4_sender(TestHal& hal, uint8_t dst, uint8_t next) {
    Node* n = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    n->on_init(cfg);
    n->route_inject(dst, next, /*hops=*/2, /*score=*/100);
    return n;
}
}  // namespace

TEST_CASE("§hybrid-rts S4 — `alternate_path`: an exact downstream forward while we still AWAIT CTS clears the "
          "redundant local copy, sends NO DATA, and invents NO app outcome") {
    // ⛔⛔ §hybrid-rts S4b (2026-08-09) — **THIS CASE'S HEADING USED TO READ "THE MAJORITY SHAPE … 46 of the 61
    // historical credits". BOTH HALVES ARE WITHDRAWN.** The 46/61 split was UNMEASURABLE when written (the deciding
    // field, `data_ever_admitted` — S4 called it `data_ever_transmitted` — is introduced by S4) and is FALSE on the
    // S4 wire: measured over all 36 scenarios, **49 credits — 36 local / 13 `alternate_path`** (the 36 were emitted
    // as `local_data`, renamed `local_admitted` by S4c with the counts unmoved), so this shape is the MINORITY (~27 %).
    // The historical inference "`awaiting_cts` ⇒ DATA never transmitted" fails because `awaiting_cts` is NOT a
    // monotone phase — a flight that ADMITS a DATA, loses the ACK and re-RTSes is back in it (32 of the 45).
    // ★ WHAT THIS CASE PINS IS UNCHANGED, and it is the justification, not the frequency: **"the flight is
    // progressing and this local copy is redundant"** — ⛔ NOT "our DATA crossed the hop", because no DATA of ours
    // exists. Figures: `simulation/BASELINE.md` §HYBRID-RTS-S4 (3) / §HYBRID-RTS-S4b.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    const uint8_t body[2] = { 'h', 'i' };
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, body, sizeof body, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    CHECK(o.next == 20); CHECK(o.dst == 50);
    CHECK(rts_flight_identity_valid(o.id));
    CHECK(hal.count("data_tx") == 0);                       // precondition: awaiting_cts, nothing aired
    CHECK(node->has_pending_tx());
    { Push d{}; while (node->next_push(d)) {} }              // drain whatever the origination pushed
    // 20 (our next hop) forwards THE SAME FLIGHT onward to 9 — same identity bytes, same dst, wire plane STATIC.
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, o.id);
    CHECK(fn == 10);
    hal._now = 1500; node->on_recv(fb.data(), fn, m20);
    // ★★ THE CREDIT, AND ITS BASIS
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) { CHECK(ia->basis == "alternate_path");          // ★ we admitted no DATA — the OTHER label
              CHECK(ia->awaiting_cts); CHECK_FALSE(ia->awaiting_ack);
              CHECK(ia->from == 20); CHECK(ia->dst == 50); CHECK(ia->forward_next == 9); }
    CHECK_FALSE(node->has_pending_tx());                    // the redundant copy is gone...
    CHECK(hal.count("data_tx") == 0);                       // ★ ...and NO DATA was ever sent for it
    // ⛔ AND NOTHING SUCCESS- OR FAILURE-SHAPED WAS INVENTED (ruling §1.10 second half)
    CHECK(hal.count("send_failed") == 0);
    CHECK(hal.count("delivered") == 0);
    CHECK(hal.count("send_e2e_acked") == 0);
    CHECK_FALSE(any_send_outcome_push(*node));
    delete node;
}

TEST_CASE("§hybrid-rts S4 — `local_admitted`: an exact forward after the DATA was ADMITTED to the radio clears the "
          "copy, emits only the named diagnostic, and does not re-send the DATA") {
    // ⛔ §hybrid-rts S4c (2026-08-10): this case's basis used to be called `local_data` and this heading used to say
    // *"AFTER a real DATA transmission"*. **BOTH ARE WITHDRAWN NAMES FOR THE SAME OBSERVATION** — `TestHal::tx`
    // returning `ok` is an ADMISSION, and on metal `DeviceHal::tx` is an enqueue whose frame can still be refused
    // (`on_radio_busy`) or dropped (`pump_tx`). What the case pins is unchanged: the OTHER of the two labels fires,
    // and no second DATA is sent.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    const uint8_t body[2] = { 'h', 'i' };
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, body, sizeof body, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/20, /*data_sf=*/7, cb);
    hal._now = 1100; node->on_recv(cb.data(), cn, m20);
    node->on_timer(kCtsToDataGapTimerId);                   // -> the DATA is ADMITTED to the HAL (awaiting_ack)
    CHECK(hal.count("data_tx") == 1);                       // ★ the precondition this case is ABOUT
    CHECK(node->has_pending_tx());
    { Push d{}; while (node->next_push(d)) {} }
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, o.id);
    hal._now = 1600; node->on_recv(fb.data(), fn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) { CHECK(ia->basis == "local_admitted");          // ★ THE OTHER BASIS — the HAL ADMITTED our DATA
              CHECK(ia->awaiting_ack); CHECK_FALSE(ia->awaiting_cts); }
    CHECK_FALSE(node->has_pending_tx());
    CHECK(hal.count("data_tx") == 1);                       // ★ no SECOND DATA — that is the airtime saving
    CHECK(hal.count("send_failed") == 0);
    CHECK(hal.count("delivered") == 0);
    CHECK_FALSE(any_send_outcome_push(*node));
    // ...and the recovery path is not needed, because the flight is gone: the ACK timeout it was waiting on can
    // fire harmlessly (the timers were cancelled, and there is no flight left to retry).
    const int rts_before = hal.count("rts_tx");
    node->on_timer(kAckTimeoutTimerId); node->on_timer(kRetryBackoffTimerId);
    CHECK(hal.count("rts_tx") == rts_before);
    CHECK_FALSE(any_send_outcome_push(*node));
    delete node;
}

TEST_CASE("§hybrid-rts S4 — an INDEPENDENTLY ARMED end-to-end ACK wait SURVIVES the credit (the credit is "
          "progress evidence, never a delivery receipt)") {
    // ★★ Design §5.2: *"Preserve any independently armed end-to-end ACK wait."* The `-a` deadline ring is the ONLY
    // thing that can report a real end-to-end outcome; if the credit had cleared it, an undelivered `-a` DM would
    // go silent forever — the "a success that isn't" class this whole arc exists to remove.
    // ⓘ The wait is PRIVATE state, so it is proven BEHAVIOURALLY: the deadline still fires its send_failed.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    const uint8_t body[2] = { 'h', 'i' };
    hal._now = 1000;
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 50; c.u.send.flags = DATA_FLAG_E2E_ACK_REQ;
    c.body = body; c.body_len = sizeof body;
    const CmdResult r = node->on_command(c);
    CHECK(r.code == CmdCode::queued);
    const uint16_t ctr = r.ctr;
    CHECK(ctr != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    { Push d{}; while (node->next_push(d)) {} }
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, o.id);
    hal._now = 1500; node->on_recv(fb.data(), fn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    CHECK_FALSE(node->has_pending_tx());
    CHECK_FALSE(any_send_outcome_push(*node));              // ⛔ the credit itself pushed NOTHING
    // ★★ THE ASSERTION: the E2E wait is STILL ARMED — its deadline still fires, and it names OUR ctr.
    hal._now = 1000 + protocol::e2e_ack_deadline_ms + 1;
    node->on_timer(/*kE2eAckDeadlineTimerId=*/90);
    bool timed_out = false; Push p{};
    while (node->next_push(p))
        if (p.kind == PushKind::send_failed && p.reason == SendFailReason::e2e_ack_timeout && p.ctr == ctr)
            timed_out = true;
    CHECK(timed_out);                                       // ★ the app still learns the truth
    delete node;
}

TEST_CASE("§hybrid-rts S4 — the `s27` collision frame (TWO ORIGINS, SAME `ctr_lo`, SAME length) does NOT clear the "
          "other flight, and the flight survives to be retried") {
    // ★★★ THE EXACT FRAME THAT FORCED [[B157]]'s DELETION, rebuilt. On the 7-byte wire the two frames were
    // BYTE-IDENTICAL: `s27` t=363718, gateway G1 overheard 103 forwarding origin **114**'s message and credited it
    // to the still-unsent origin **111** flight (both `ctr` 2, both `payload_len` 57). ⇒ `re-m3` lost in silence.
    // On the S1 wire the identity tail differs, so the credit REFUSES.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    const uint8_t body[2] = { 'h', 'i' };
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, body, sizeof body, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    const Ev* rt = hal.last("rts_tx"); CHECK(rt != nullptr);
    const uint16_t our_ctr = rt ? static_cast<uint16_t>(rt->ctr) : 0;
    // A DIFFERENT ORIGIN, the SAME full ctr — i.e. the same `ctr_lo`, the same dst, the same next hop, the same
    // payload_len. EVERY field the retired match compared is equal; only the identity's origin byte differs.
    const RtsFlightIdentity other = rts_flight_identity_plain(/*origin=*/114, our_ctr);
    CHECK_FALSE(rts_flight_identity_equal(other, o.id));    // ⚠ stated up front: the frames DO differ now
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, other);
    hal._now = 1500; node->on_recv(fb.data(), fn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 0);      // ★★ REFUSED
    CHECK(node->has_pending_tx());                          // ★ our message is STILL OURS
    CHECK_FALSE(any_send_outcome_push(*node));              // no outcome either way — a mismatch changes nothing
    // ...and the recovery path still works: the CTS timeout drives a real retry rather than a silent drop.
    const int rts_before = hal.count("rts_tx");
    node->on_timer(kRtsTimeoutTimerId); node->on_timer(kRetryBackoffTimerId);
    CHECK(hal.count("rts_tx") > rts_before);
    // ★ THE POSITIVE CONTROL, in the same case: the EXACT identity on the SAME endpoints DOES credit — so the
    //   refusal above is about the identity, not about an inert code path.
    const OurRts o2 = our_last_rts(hal); CHECK(o2.got);
    std::array<uint8_t, 16> gb{};
    const size_t gn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o2.ctr_lo, o2.plen, gb, o2.id);
    hal._now = 2500; node->on_recv(gb.data(), gn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    CHECK_FALSE(node->has_pending_tx());
    delete node;
}

TEST_CASE("§hybrid-rts S4 — a ONE-BIT mismatch in identity, dst, next-hop, wire PLANE or identity DOMAIN leaves "
          "the flight pending (each arm on its own node, each with the exact frame as its control)") {
    const uint8_t body[2] = { 'h', 'i' };
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    // Each arm: build a fresh sender, perturb ONE thing, assert no credit + flight intact, then feed the EXACT
    // frame and assert the credit fires. Without that second half every arm would pass on a build with the
    // mechanism removed entirely — which is exactly how the retired [[B157]] case stayed green.
    auto arm = [&](const char* what,
                   std::function<void(const OurRts&, std::array<uint8_t,16>&, size_t&)> make_bad) {
        TestHal hal;
        Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
        hal._now = 1000;
        CHECK(node->test_do_send_typed(/*dst=*/50, body, sizeof body, CryptIntent::def, 0, 0) != 0);
        const OurRts o = our_last_rts(hal); CHECK(o.got);
        { Push d{}; while (node->next_push(d)) {} }
        std::array<uint8_t, 16> bad{}; size_t bn = 0;
        make_bad(o, bad, bn);
        CHECK_MESSAGE(bn > 0, what);                        // the frame must actually pack, or the arm is vacuous
        hal._now = 1500; node->on_recv(bad.data(), bn, m20);
        CHECK_MESSAGE(hal.count("implicit_ack_from_forward") == 0, what);   // ⛔ REFUSED
        CHECK_MESSAGE(node->has_pending_tx(), what);                        // ★ flight intact
        CHECK_FALSE(any_send_outcome_push(*node));
        // the positive control on the SAME node
        std::array<uint8_t, 16> good{};
        const size_t gn = mk_rts_wire(20, 9, o.dst, o.ctr_lo, o.plen, good, o.id);
        hal._now = 1600; node->on_recv(good.data(), gn, m20);
        CHECK_MESSAGE(hal.count("implicit_ack_from_forward") == 1, what);
        CHECK_MESSAGE(!node->has_pending_tx(), what);
        delete node;
    };
    // ⛔ ONE IDENTITY BIT
    arm("one identity bit", [](const OurRts& o, std::array<uint8_t,16>& b, size_t& n) {
        RtsFlightIdentity x = o.id; x.bytes[o.id.width - 1] ^= 0x01;
        n = mk_rts_wire(20, 9, o.dst, o.ctr_lo, o.plen, b, x);
    });
    // ⛔ DST
    arm("dst", [](const OurRts& o, std::array<uint8_t,16>& b, size_t& n) {
        n = mk_rts_wire(20, 9, static_cast<uint8_t>(o.dst ^ 0x01), o.ctr_lo, o.plen, b, o.id);
    });
    // ⛔ NEXT-HOP RELATIONSHIP: the forward comes from a node that is NOT the hop we RTS'd
    arm("next-hop", [](const OurRts& o, std::array<uint8_t,16>& b, size_t& n) {
        n = mk_rts_wire(/*src=*/21, 9, o.dst, o.ctr_lo, o.plen, b, o.id);
    });
    // ⛔ WIRE PLANE: identical identity and endpoints, but the frame DECLARES the TEAM plane (1,1) while our own
    //    flight declared STATIC. Decoded with the shared wire helper on BOTH sides — never a receiver predicate.
    arm("wire plane", [](const OurRts& o, std::array<uint8_t,16>& b, size_t& n) {
        n = mk_rts_wire(20, 9, o.dst, o.ctr_lo, o.plen, b, o.id, /*addr_len=*/1, /*mobile_src=*/true);
    });
    // ⛔ IDENTITY DOMAIN/WIDTH: a CRYPTED 4-byte tail where our flight is PLAINTEXT. `rts_flight_identity_equal`
    //    compares domain and width first, so a numeric coincidence can never cross the domains.
    arm("identity domain", [](const OurRts& o, std::array<uint8_t,16>& b, size_t& n) {
        const uint8_t seed[8] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88 };
        RtsFlightIdentity cid = rts_flight_identity_crypted(seed, 0x0001, o.dst);
        for (uint8_t i = 0; i < o.id.width; ++i) cid.bytes[i] = o.id.bytes[i];   // ★ numerically IDENTICAL prefix
        n = mk_rts_wire(20, 9, o.dst, o.ctr_lo, o.plen, b, cid);
    });
}

TEST_CASE("§hybrid-rts S4 — a TEAM-plane flight and a STATIC forward with otherwise IDENTICAL numeric fields do "
          "not cross-match, in BOTH directions") {
    // ★★ The mirror of the STATIC-flight/TEAM-forward arm above, and the direction the corpus cannot reach
    // ([[B160-COV]]: 12 scenarios have a team plane, 12 produce a requeue, and the intersection is empty).
    // Our own flight declares wire TEAM (`addr_len=1, mobile_src=1` — `rts_wire_marks`), so a forward that
    // declares STATIC with the SAME identity bytes and the SAME endpoints must be refused.
    const uint32_t TEAM = 0xABCD1234u;
    TestHal hal;
    Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.is_mobile = true; cfg.team_id = TEAM; CHECK(node.on_init(cfg));
    uint8_t ext[8]; const size_t en = pack_team_id_tlv(TEAM, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id = 0; tb.src = 40; tb.key_hash32 = 0x4040u; tb.is_mobile = true;
    tb.ext = std::span<const uint8_t>(ext, en);
    std::array<uint8_t, 64> b{};
    const size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    hal._now = 500; node.on_recv(b.data(), bn, RxMeta{ 12.0f, -70.0f, 0, static_cast<int8_t>(40) });
    CHECK(node.is_team_peer(40));
    hal._now = 1000;
    send_cmd(node, /*dst=*/40, "hi");                       // a TEAM DM -> RTS with (addr_len=1, mobile_src=1)
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    CHECK(o.addr_len == 1); CHECK(o.mobile_src);            // ★ precondition: OUR flight declares wire TEAM
    CHECK(rts_wire_team_plane(o.addr_len, o.mobile_src));
    CHECK(node.has_pending_tx());
    RxMeta m40{ 12.0f, -70.0f, 0, static_cast<int8_t>(40) };
    // ⛔ ARM 1 — the SAME identity, SAME endpoints, but the forward declares STATIC (0,0).
    { std::array<uint8_t, 16> fb{};
      const size_t fn = mk_rts_wire(/*src=*/40, /*next=*/9, o.dst, o.ctr_lo, o.plen, fb, o.id,
                                    /*addr_len=*/0, /*mobile_src=*/false);
      hal._now = 1500; node.on_recv(fb.data(), fn, m40); }
    CHECK(hal.count("implicit_ack_from_forward") == 0);      // ★★ REFUSED — the planes disagree
    CHECK(node.has_pending_tx());
    // ★ ARM 2 (the positive control) — the SAME frame declaring TEAM (1,1) DOES credit.
    { std::array<uint8_t, 16> fb{};
      const size_t fn = mk_rts_wire(/*src=*/40, /*next=*/9, o.dst, o.ctr_lo, o.plen, fb, o.id,
                                    /*addr_len=*/1, /*mobile_src=*/true);
      hal._now = 1600; node.on_recv(fb.data(), fn, m40); }
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) CHECK(ia->basis == "alternate_path");
    CHECK_FALSE(node.has_pending_tx());
}

TEST_CASE("§hybrid-rts S4 — a DUTY-DEFERRED DATA was never even ADMITTED, so the basis stays `alternate_path` EVEN "
          "THOUGH the `data_tx` telemetry fired (the boundary is the admission, not the emit)") {
    // ★★★ THIS IS THE CASE THAT MAKES `data_ever_admitted`'s PLACEMENT FALSIFIABLE, and it is the only one that
    // can: `TestHal::tx` answers `ok` unless `tx_answer` says otherwise, so on every other path `handed` is true and
    // moving the assignment above `tx_with_retry` would be INVISIBLE. Here the duty pre-check returns
    // `deferred_retry_armed` — the frame is stashed, NOT offered to the radio at all — while `MR_EMIT("data_tx", ...)`
    // fires REGARDLESS, a few lines later.
    // ⇒ ⚠ THE TELEMETRY IS NOT EVEN THE ADMISSION, let alone the transmission. If the field were set before the
    //   hand-off, this flight would report the local basis for a DATA that never left the node. The gate mutates that.
    // ⓘ It also reproduces the corpus's rarest pending state: `awaiting_cts` false (the CTS arrived) AND
    //   `awaiting_ack` false (nothing was handed, so no ACK wait was armed) — the "neither" class.
    TestHal hal;
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10;                                  // ⚠ a real budget: with 0 there is no pre-check at all
    CHECK(node->on_init(cfg));
    node->route_inject(/*dest=*/50, /*next_hop=*/20, /*hops=*/2, /*score=*/100);
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, (const uint8_t*)"hi", 2, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);       // the RTS is slot<0 -> never duty-deferred, so it flew
    hal._airtime_used = 999999999ull;                       // ★ now the DATA cannot be handed to the radio
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/20, /*data_sf=*/7, cb);
    hal._now = 1100; node->on_recv(cb.data(), cn, m20);
    node->on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("duty_cycle_blocked") == 1);            // ★ the frame was DEFERRED, not sent
    CHECK(hal.count("data_tx") == 1);                       // ⚠ ...and the telemetry fired anyway
    CHECK(node->has_pending_tx());
    { Push d{}; while (node->next_push(d)) {} }
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, o.id);
    hal._now = 1600; node->on_recv(fb.data(), fn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) { CHECK(ia->basis == "alternate_path");           // ★★ THE ASSERTION — no DATA of ours was ever admitted
              CHECK_FALSE(ia->awaiting_cts); CHECK_FALSE(ia->awaiting_ack); }   // the "neither" pending state
    CHECK_FALSE(node->has_pending_tx());
    CHECK_FALSE(any_send_outcome_push(*node));
    delete node;
}

// =============================================================================
// ★★★★★ §hybrid-rts S4b (2026-08-09) — THE TWO CORPUS-DARK CORRECTNESS HOLES S4 LEFT, EACH WITH ITS CONTROL.
// Both were found by review, NOT by the corpus: the credit census and all 36 stream md5s are unchanged by either
// fix, so ⛔ **these native cases are the ONLY detectors either fix has** and each is paired with a positive
// control + a mutation applied at match count == 1 (recorded in `simulation/BASELINE.md` §HYBRID-RTS-S4b).
// =============================================================================

TEST_CASE("★★★ §hybrid-rts S4b — a HAL-REJECTED DATA is NOT an admission: the basis stays `alternate_path` while "
          "the ACK wait it legitimately arms stays armed (the two dispositions are ASYMMETRIC for the flag)") {
    // ⛔⛔ THE DEFECT: `do_data_tx` computed `handed = tx_with_retry(...) != deferred_retry_armed`, so
    // `TxHandOff::rejected` — the HAL REFUSING the frame (`DeviceHal::tx`: full ring, `busy`, `txq_drops`, nothing
    // retained) — satisfied it, and the flag was set although the radio never accepted the DATA. A later exact
    // downstream forward was then credited on the LOCAL basis, i.e. it asserted something that never happened.
    // §hybrid-rts S4b restricts the flag to `disp == TxHandOff::handed`.
    // ⛔ §hybrid-rts S4c (2026-08-10) — **THIS CASE'S SCOPE IS NARROWER THAN S4b's HEADING CLAIMED, AND THE HEADING
    //   IS CORRECTED ABOVE.** It pins that a SYNCHRONOUS refusal is not an admission. ⚠ It does NOT — and cannot —
    //   pin that the DATA aired: `on_radio_busy` and `pump_tx()`'s failed arm reject a frame AFTER a successful
    //   admission, and this flag has no writer that can take it back. That residue is [[B164]], both directions.
    // ★★★ WHY THIS CASE CAN EXIST AT ALL, and it is the method point: `TestHal::tx` used to ALWAYS hand off, so
    //   `rejected` was unreachable in this TU and any "rejection" case here would have been an INERT GREEN. The
    //   capability was ONE FIELD away (`tx_answer`), the same field `test_node_join.cpp` already had. ⇒ a rejection
    //   test on a HAL that cannot reject proves nothing.
    // ★★ THE ASYMMETRY IS PINNED IN ONE EMIT, deliberately, because that is the half that is easy to break while
    //   "fixing" this: the SAME credit must report `basis == alternate_path` (no DATA admitted) AND
    //   `awaiting_ack == true` (the MAC ack-timeout IS a rejected frame's only recovery — §tx-admission TX1 —
    //   so it must still have been armed). A two-way `false-on-rejection` would flip the second one.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, (const uint8_t*)"hi", 2, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);        // the RTS flew BEFORE the HAL starts refusing
    CHECK(o.next == 20); CHECK(o.dst == 50);
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/20, /*data_sf=*/7, cb);
    hal._now = 1100; node->on_recv(cb.data(), cn, m20);      // CTS -> the DATA is due after the gap
    hal.tx_answer = TxResult::busy;                          // ⛔ NOW the radio refuses: a DEFINITIVE drop
    const int calls_before = hal.tx_calls;
    node->on_timer(kCtsToDataGapTimerId);                    // -> do_data_tx -> tx_with_retry -> TxHandOff::rejected
    // ★ PRECONDITIONS, asserted so this case cannot pass for the wrong reason
    CHECK(hal.tx_calls == calls_before + 1);                 // the HAL WAS asked (a refusal is still an attempt)
    CHECK(hal.count("tx_hal_rejected") == 1);                // ★★ and it REFUSED — this is the whole premise
    CHECK(hal.count("duty_cycle_blocked") == 0);             // ⚠ NOT the duty path: this is the OTHER disposition
    CHECK(hal.count("data_tx") == 1);                        // the telemetry fired regardless (as at the duty twin)
    CHECK(node->has_pending_tx());
    { Push d{}; while (node->next_push(d)) {} }
    hal.tx_answer = TxResult::ok;                            // let the rest of the exchange behave normally
    // 20 forwards THE EXACT flight onward to 9 — the credit's endpoints, identity and plane all match.
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, o.id);
    hal._now = 1600; node->on_recv(fb.data(), fn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) {
        CHECK(ia->basis == "alternate_path");                // ★★★ THE ASSERTION: a rejection is NOT an admission
        CHECK(ia->awaiting_ack);                             // ★★★ ...and the legacy recovery IS still armed
        CHECK_FALSE(ia->awaiting_cts);
    }
    CHECK_FALSE(node->has_pending_tx());
    CHECK_FALSE(any_send_outcome_push(*node));               // ⛔ and still no invented app outcome
    delete node;
}

TEST_CASE("★★★ §hybrid-rts S4b — a LOOPED RTS (our own next hop forwards the EXACT flight BACK to us) earns NO "
          "credit, KEEPS our copy, and is still handled normally") {
    // ⛔⛔ THE DEFECT: the credit gate tested expected neighbour, destination, identity and plane — every one of
    // which answers *"which flight?"* — and NOTHING tested whether the RTS was addressed BACK to this node. In a
    // route loop B forwards our exact flight to A, and A then cancelled its timers, dropped its `_pending_tx` and
    // RETURNED: it discarded THE ONLY VIABLE COPY on evidence that showed the opposite of progress, and it never
    // reached the ordinary addressed-RTS handling that frame was owed.
    // ★★ THE PRINCIPLE: **an exact identity match proves WHICH FLIGHT, never WHICH DIRECTION.**
    // ★★★ THIS CASE HAS TWO HALVES AND THE SECOND IS NOT OPTIONAL: "no credit" is NOT the contract — the frame must
    //   FALL THROUGH to normal processing with the flight intact. `rts_rx` is emitted only AFTER the
    //   `!addressed_for_us` overhear branch returns, so its presence is proof the addressed path was reached; and
    //   with a `_pending_tx` and no `_pending_rx` the normal path answers `rts_drop_pending_tx` (silent, by design).
    //   A guard that merely skipped the credit and swallowed the frame would fail on those two.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);    // us = id 1, route to 50 via 20
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, (const uint8_t*)"hi", 2, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    CHECK(o.next == 20); CHECK(o.dst == 50);
    CHECK(rts_flight_identity_valid(o.id));
    CHECK(node->has_pending_tx());
    { Push d{}; while (node->next_push(d)) {} }
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    // ⛔ THE LOOP FRAME: from 20 (our chosen next hop), same dst 50, THE SAME IDENTITY BYTES, wire plane STATIC —
    //    but `next = 1`, i.e. addressed straight BACK at us. Every S4 term matches; only the direction is wrong.
    std::array<uint8_t, 16> lb{};
    const size_t ln = mk_rts_wire(/*src=*/20, /*next=*/1, /*dst=*/50, o.ctr_lo, o.plen, lb, o.id);
    CHECK(ln == 10);                                         // a real unicast RTS with a real identity tail
    hal._now = 1500; node->on_recv(lb.data(), ln, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 0);      // ★★★ NO CREDIT — a loop is not progress
    CHECK(node->has_pending_tx());                           // ★★★ ...and OUR COPY SURVIVES
    CHECK(hal.count("rts_rx") == 1);                         // ★★★ ...and the frame WAS handled (addressed path)
    CHECK(hal.count("rts_drop_pending_tx") == 1);            // ★★★ ...by the normal busy-with-own-TX answer
    CHECK_FALSE(any_send_outcome_push(*node));
    // ...and the flight still recovers on its own timers, so nothing was quietly wedged either.
    const int rts_before = hal.count("rts_tx");
    node->on_timer(kRtsTimeoutTimerId); node->on_timer(kRetryBackoffTimerId);
    CHECK(hal.count("rts_tx") > rts_before);
    // ★★★★ THE POSITIVE CONTROL, ON THE SAME NODE: the SAME frame differing ONLY in `next` (9, a third node =
    //   a GENUINE downstream forward) DOES credit. ⇒ the refusal above is about DIRECTION and nothing else, and a
    //   build with the guard removed cannot pass both halves.
    const OurRts o2 = our_last_rts(hal); CHECK(o2.got);
    std::array<uint8_t, 16> gb{};
    const size_t gn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o2.ctr_lo, o2.plen, gb, o2.id);
    hal._now = 2500; node->on_recv(gb.data(), gn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);      // ★ the genuine forward credits
    CHECK_FALSE(node->has_pending_tx());
    CHECK(hal.count("rts_rx") == 1);                         // ⓘ unchanged: an OVERHEARD frame never emits rts_rx
    delete node;
}

TEST_CASE("★★★ §hybrid-rts S4b — the loop guard uses the CANONICAL address admission: a TEAM-plane loop is refused "
          "on `team_addr_for_us`, and the mark must MATCH OUR KIND (a bare `next == id` would be wrong)") {
    // ★★ WHY THIS SECOND LOOP CASE EXISTS: the guard is `wire_team ? for_team_rts : for_static_rts`, not
    // `r.next == _node_id`. Two properties follow that a bare numeric compare would get wrong, and both are pinned:
    //   ① on the TEAM plane the loop is detected against `_team_local_id` (`team_addr_for_us`), NOT `_node_id`;
    //   ② the ADDRESS MARK must match our kind — a `(addr_len=0)` STATIC frame naming our numeric id is NOT
    //      addressed to a mobile, so it is a genuine overhear and MUST still be able to credit.
    // ⛔ A second, hand-rolled derivation of either would be the §hybrid-rts S2 defect again (40 false refusals);
    //   the gate reuses `for_static_rts` / `team_addr_for_us` / `rts_wire_team_plane` and this case is what proves
    //   the reuse is the right one rather than merely tidy.
    const uint32_t TEAM = 0xABCD1234u;
    TestHal hal;
    Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.is_mobile = true; cfg.team_id = TEAM; CHECK(node.on_init(cfg));
    node.set_team_local_id(93);                              // a CONFIRMED team-plane id (the persisted-at-boot API,
                                                             // the same idiom `b160_collision_node` uses) — 93 != 30
    uint8_t ext[8]; const size_t en = pack_team_id_tlv(TEAM, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id = 0; tb.src = 40; tb.key_hash32 = 0x4040u; tb.is_mobile = true;
    tb.ext = std::span<const uint8_t>(ext, en);
    std::array<uint8_t, 64> b{};
    const size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    hal._now = 500; node.on_recv(b.data(), bn, RxMeta{ 12.0f, -70.0f, 0, static_cast<int8_t>(40) });
    CHECK(node.is_team_peer(40));
    const uint8_t my_team_id = node.team_local_id();
    CHECK(my_team_id != 0);                                  // precondition: we HAVE a team-plane id to loop back to
    CHECK(my_team_id != 30);                                 // ★ and it DIFFERS from our numeric node id, or ARM 2 below
                                                             //   would be the same frame as ARM 1 and prove nothing
    hal._now = 1000;
    send_cmd(node, /*dst=*/40, "hi");                        // a TEAM DM -> RTS with (addr_len=1, mobile_src=1)
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    CHECK(o.addr_len == 1); CHECK(o.mobile_src);
    CHECK(node.has_pending_tx());
    RxMeta m40{ 12.0f, -70.0f, 0, static_cast<int8_t>(40) };
    // ⛔ ARM 1 — a TEAM-plane loop: same identity, same dst, wire TEAM, `next = OUR TEAM-PLANE id`.
    { std::array<uint8_t, 16> fb{};
      const size_t fn = mk_rts_wire(/*src=*/40, /*next=*/my_team_id, o.dst, o.ctr_lo, o.plen, fb, o.id,
                                    /*addr_len=*/1, /*mobile_src=*/true);
      hal._now = 1500; node.on_recv(fb.data(), fn, m40); }
    CHECK(hal.count("implicit_ack_from_forward") == 0);      // ★★ REFUSED on `team_addr_for_us`
    CHECK(node.has_pending_tx());                            // ★★ our copy survives
    CHECK(hal.count("rts_rx") == 1);                         // ★★ and it reached the addressed path
    // ★ ARM 2 (the MARK control, property ②) — `next` is our NUMERIC node id (30) but the frame declares TEAM, so
    //   the wire-team reading applies and `team_addr_for_us` is FALSE for id 30 ⇒ this is an OVERHEAR and it MUST
    //   credit. A guard written as `r.next == _node_id` would wrongly refuse here and this arm would go RED.
    { std::array<uint8_t, 16> fb{};
      const size_t fn = mk_rts_wire(/*src=*/40, /*next=*/30, o.dst, o.ctr_lo, o.plen, fb, o.id,
                                    /*addr_len=*/1, /*mobile_src=*/true);
      hal._now = 1600; node.on_recv(fb.data(), fn, m40); }
    CHECK(hal.count("implicit_ack_from_forward") == 1);      // ★★★ the overhear DOES credit
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) CHECK(ia->basis == "alternate_path");
    CHECK_FALSE(node.has_pending_tx());
}

// =============================================================================
// ★★★★★ §hybrid-rts S4d (2026-08-10) — **THE FLAG'S `false` SIDE WAS NOT HONEST, AND THIS IS ITS ONLY DETECTOR.**
// S4c made `true` honest by renaming the flag to what it witnesses (an ADMISSION). It left `false` claiming more
// than it could: the only writer sat in `do_data_tx` (the INITIAL send path), while `duty_defer_fire` re-runs
// `tx_with_retry` from the stash and can obtain an admission there with NO write — yet the consumer maps `false`
// **CATEGORICALLY** onto `basis=alternate_path` = *"no DATA has been admitted locally"* (design §5.2 item 2).
// ★★ S4d's FIX IS STRUCTURAL, NOT ADDITIVE: the single write moves to the ONE point every admission crosses
// (`tx_with_retry`, immediately after `_hal.tx()` answers `ok`). ⛔ NOT a copy at each retry site — that is the
// same defect in a new shape, and it is what [[B164]] explicitly forbade.
// ⚠ THE CORPUS CANNOT SEE THIS: `duty_cycle_blocked` with label `DATA` is **ZERO across all 36 scenarios**
// (measured, `BASELINE.md` §HYBRID-RTS-S4c/S4d), so the DATA duty-defer path is CORPUS-DARK and the case below is
// the only thing that can fail. ⇒ it carries its own vacuity controls: the deferral itself is asserted, and the
// mutation that restores the pre-S4d control flow must turn it RED.
// ⚠ HONEST COVERAGE GAP, STATED RATHER THAN IMPLIED: the crossing point's `!m_broadcast` term has **NO native
// detector here**. It is load-bearing — `do_data_tx`'s M-broadcast arm reaches `tx_with_retry` with
// `FrameTag::data` (`node_mac.cpp:1788`) — but the flag has no accessor and an M flight can never earn the credit,
// so there is no observable this TU can assert on. A case was DRAFTED and REMOVED rather than left as an inert
// green. Its only evidence is the structural argument in-source plus the mutation recorded in `BASELINE.md`
// §HYBRID-RTS-S4d; ⛔ do not read the absence of a case here as coverage.
// =============================================================================

TEST_CASE("★★★★ §hybrid-rts S4d — a DUTY-DEFERRED DATA THAT THE TIMER LATER ADMITS reports `basis=local_admitted`: "
          "the fact is established at the ONE crossing point every admission passes, not at the initial send") {
    // ⛔⛔ THE DEFECT THIS CASE PINS: pre-S4d the assignment lived in `do_data_tx`'s `disp == TxHandOff::handed`
    // arm. A DATA that the duty pre-check DEFERRED never reached it; when `duty_defer_fire` later re-ran
    // `tx_with_retry` and the radio ACCEPTED the frame, nothing was recorded. The flight then earned the credit
    // labelled `alternate_path` — *"we admitted NO DATA, the next hop got this flight through another branch"* —
    // on a flight whose DATA this node had itself handed to its own radio. **A CATEGORICAL LABEL ON A FACT THE
    // FLAG COULD NOT ESTABLISH.**
    // ★★ WHY THE FIX IS A CROSSING POINT AND NOT TWO ASSIGNMENTS: every admission — initial AND deferred-retry —
    // must pass `_hal.tx()` inside `tx_with_retry`, so establishing the fact THERE makes it exact **and makes a
    // future path unable to bypass it**. Same structural lesson as [[B162]]'s refusal banner: *a guarantee made at
    // one of several exits is not made at all.*
    // ★★★ HARNESS VACUITY IS THE FIRST THING PROVEN, because S4's own M3 mutation was only ever falsifiable
    // BECAUSE someone added a duty-deferred case: `TestHal::tx` answers `ok` unless told otherwise, so on every
    // other path `handed` is true and a mis-placed write is INVISIBLE. ⇒ the deferral is ASSERTED here
    // (`duty_cycle_blocked == 1`, **zero** DATA frames offered, and NOT the rejection disposition) before the
    // admission is arranged. A "duty-defer" test on a HAL that never defers is an inert green.
    TestHal hal;
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10;                                  // ⚠ a real budget: with 0 there is no pre-check at all
    CHECK(node->on_init(cfg));
    node->route_inject(/*dest=*/50, /*next_hop=*/20, /*hops=*/2, /*score=*/100);
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, (const uint8_t*)"hi", 2, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);       // the RTS is slot<0 -> never duty-deferred, so it flew
    CHECK(o.next == 20); CHECK(o.dst == 50);
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/20, /*data_sf=*/7, cb);
    hal._now = 1100; node->on_recv(cb.data(), cn, m20);     // CTS -> the DATA is due after the gap
    // ---- (1) the FIRST attempt is DUTY-DEFERRED: the radio is never even asked ----
    hal._airtime_used = 999999999ull;                       // ★ over budget -> tx_with_retry defers, no _hal.tx
    const int tx_calls_before = hal.tx_calls;
    node->on_timer(kCtsToDataGapTimerId);                   // do_data_tx -> tx_with_retry -> deferred_retry_armed
    CHECK(hal.count("duty_cycle_blocked") == 1);            // ★ VACUITY CONTROL: the deferral REALLY happened
    CHECK(hal.tx_calls == tx_calls_before);                 // ★ VACUITY CONTROL: the HAL was NOT asked at all
    CHECK(hal.count("tx_hal_rejected") == 0);               // ⚠ and it is the DUTY path, not the rejection twin
    int data_deferred = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data_deferred;
    CHECK(data_deferred == 0);                              // ★ no DATA frame exists yet
    CHECK(node->has_pending_tx());
    // ---- (2) the duty timer fires and the SAME flight's DATA is ADMITTED — the write pre-S4d never made ----
    hal._airtime_used = 0;                                  // budget frees
    node->on_timer(kDutyDeferTimerId + 1);                  // duty_defer_fire(DATA slot) -> tx_with_retry -> ok
    int data_admitted = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data_admitted;
    CHECK(data_admitted == 1);                              // ★ THE PREMISE: the DATA WAS admitted, on the re-run
    CHECK(hal.tx_calls == tx_calls_before + 1);             // ★ ...and exactly one HAL admission happened
    CHECK(hal.count("duty_cycle_blocked") == 1);            // ⚠ it did NOT re-defer (else the premise is gone)
    { Push d{}; while (node->next_push(d)) {} }
    // ---- (3) 20 forwards THE EXACT flight onward to 9: the credit must rest on the LOCAL admission ----
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_wire(/*src=*/20, /*next=*/9, /*dst=*/50, o.ctr_lo, o.plen, fb, o.id);
    hal._now = 1600; node->on_recv(fb.data(), fn, m20);
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    const Ev* ia = hal.last("implicit_ack_from_forward"); CHECK(ia != nullptr);
    if (ia) {
        CHECK(ia->basis == "local_admitted");               // ★★★★ THE ASSERTION — pre-S4d this read alternate_path
        CHECK(ia->awaiting_ack);                            // the re-hand re-armed the ACK wait (duty_defer_fire)
        CHECK_FALSE(ia->awaiting_cts);                      // the CTS had arrived before the defer
    }
    CHECK_FALSE(node->has_pending_tx());                    // the redundant local copy is still cleared
    CHECK_FALSE(any_send_outcome_push(*node));              // ⛔ and still NO invented app outcome
    delete node;
}

TEST_CASE("§hybrid-rts S4 — an M/FLOOD RTS carries NO identity tail and can therefore never earn the credit") {
    // ⚠ HONEST COVERAGE STATEMENT, because this case does NOT test what its neighbour in the source does.
    //   WHAT IS TESTED HERE: the OVERHEARD-FRAME half — an M_BROADCAST RTS has no identity tail, so
    //   `rts_flight_identity_equal` (absent-vs-absent is NOT a match) refuses it. That is the live discriminator.
    //   ⛔ WHAT IS **NOT** TESTED: the `!pt.m_broadcast` guard on OUR OWN pending flight. That guard is
    //   DEFENSIVE-AND-EXPLICIT, not load-bearing: for it to matter, a channel M flight would have to be pending
    //   AND an overheard unicast RTS would have to reproduce that flight's identity — and the M flight's identity
    //   is derived from a channel inner that no unicast DATA carrier can reproduce. It is written down (and this
    //   sentence says so) rather than claimed to be covered.
    TestHal hal;
    Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    hal._now = 1000;
    CHECK(node->test_do_send_typed(/*dst=*/50, (const uint8_t*)"hi", 2, CryptIntent::def, 0, 0) != 0);
    const OurRts o = our_last_rts(hal); CHECK(o.got);
    RxMeta m20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    // An M_BROADCAST RTS from our next hop, on our dst, cannot carry an identity at all — so it cannot credit.
    { std::array<uint8_t, 16> fb{};
      rts_in in{}; in.leaf_id = 0; in.src = 20; in.next = 9; in.ctr_lo = o.ctr_lo; in.dst = o.dst;
      in.sf_index = 3; in.rts_flags = RTS_FLAG_M_BROADCAST; in.payload_len = o.plen; in.m_payload_id_lo16 = 0x1234;
      const size_t fn = pack_rts(in, std::span<uint8_t>(fb.data(), fb.size()));
      CHECK(fn == 9);                                       // the M shape — 9 B, no identity tail
      hal._now = 1500; node->on_recv(fb.data(), fn, m20); }
    CHECK(hal.count("implicit_ack_from_forward") == 0);
    CHECK(node->has_pending_tx());
    // ★ the control: the unicast twin of the same frame DOES credit, so "no credit" above is about the shape
    { std::array<uint8_t, 16> gb{};
      const size_t gn = mk_rts_wire(20, 9, o.dst, o.ctr_lo, o.plen, gb, o.id);
      hal._now = 1600; node->on_recv(gb.data(), gn, m20); }
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    delete node;
}

TEST_CASE("§hybrid-rts S4 — telemetry `dup` is NOT the wire bit: the re-CTS branch emits dup:true on a frame "
          "whose `already_received` bit is CLEAR, and only the frame may be believed") {
    // ⚠⚠ THE INSTRUMENT TRAP, pinned so no future case reaches for `dup` as a shorthand for the terminal bit.
    // `handle_rts`'s pending-RX re-CTS branch emits `cts_tx{dup:true}` while packing `already_received = false`;
    // the completed-flight branch emits `cts_tx{already_received:true}` and packs the bit. ⇒ the telemetry field
    // and the wire bit are DIFFERENT FACTS, and only the second one is a protocol statement.
    // ⓘ STRUCTURAL NOTE: `Hal::emit` returns void and lib/core has no telemetry READER, so no live decision can
    //   consume `dup` even in principle — the risk is entirely in TESTS, which is what this case fences.
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    CHECK(node.on_init(cfg));
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    const RtsFlightIdentity id = rts_flight_identity_plain(/*origin=*/1, /*ctr=*/0x0005);
    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts_wire(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb, id);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);    // fresh admission -> ordinary CTS + PendingRx
    { const Ev* e = hal.last("cts_tx"); CHECK(e != nullptr); if (e) CHECK_FALSE(e->dup); }
    // ⇒ the RETRIED RTS for the SAME flight while the DATA is still awaited: the re-CTS branch.
    hal._now = 1100; node.on_recv(rb.data(), rn, from1);
    { const Ev* e = hal.last("cts_tx"); CHECK(e != nullptr);
      if (e) CHECK(e->dup); }                               // ⚠ telemetry says "dup"...
    { const auto* c = hal.last_tx("CTS"); CHECK(c != nullptr);
      if (c) {
        // ★★ ...AND THE FRAME SAYS OTHERWISE. Parsed AND asserted at the raw bit, both ways.
        CHECK((c->bytes.size() == 3 || c->bytes.size() == 4));      // an ORDINARY shape, never 6/7
        CHECK((c->bytes[0] & 0x01) == 0);                           // ★ bit 0 of byte 0 = already_received: CLEAR
        auto pc = parse_cts(std::span<const uint8_t>(c->bytes.data(), c->bytes.size()));
        CHECK(pc.has_value()); if (pc) CHECK_FALSE(pc->already_received);
      } }
    // ★ THE POSITIVE CONTROL — a genuinely TERMINAL CTS, so "bit clear" above is not a build that never sets it.
    node.completed_flight_store(/*from=*/1, /*dst=*/2, /*team=*/false, id, hal._now);
    { std::array<uint8_t, 16> r2{};
      const size_t n2 = mk_rts_wire(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/9, /*plen=*/15, r2, id);
      hal._now = 2000; node.on_recv(r2.data(), n2, from1); }
    { const auto* c = hal.last_tx("CTS"); CHECK(c != nullptr);
      if (c) {
        CHECK(c->bytes.size() == 6);                               // the plaintext TERMINAL wire
        CHECK((c->bytes[0] & 0x01) == 1);                          // ★ the bit IS set on the real thing
        auto pc = parse_cts(std::span<const uint8_t>(c->bytes.data(), c->bytes.size()));
        CHECK(pc.has_value()); if (pc) CHECK(pc->already_received);
      } }
    { const Ev* e = hal.last("cts_tx"); CHECK(e != nullptr);
      if (e) CHECK_FALSE(e->dup); }                        // ⚠ ...and THIS one does not say "dup" at all
}

TEST_CASE("④ cascade_effective_max — full budget at/below threshold, shrinks 1:1 above, int-clamp no wrap (dv:6275)") {
    const int thr = protocol::cascade_requeue_load_threshold;
    const int mx  = protocol::cascade_requeue_max;
    CHECK(Node::cascade_effective_max(0) == mx);                       // empty queue -> full budget
    CHECK(Node::cascade_effective_max(static_cast<uint8_t>(thr)) == mx);   // at threshold -> still full
    CHECK(Node::cascade_effective_max(static_cast<uint8_t>(thr + 1)) == (mx - 1 > 0 ? mx - 1 : 0));   // one over -> shrink 1
    CHECK(Node::cascade_effective_max(static_cast<uint8_t>(thr + mx)) == 0);   // budget fully gated
    CHECK(Node::cascade_effective_max(255) == 0);                      // deep backlog -> 0, NO uint8 underflow wrap
}

TEST_CASE("R4.0 budget tier — thresholds 50/80/95 + disabled = HEALTHY (Lua dv:3560-3571)") {
    using BT = Node::BudgetTier;
    TestHal hal;
    // window 1000ms, duty 0.10 -> budget = floor(0.10*1000) = 100ms. pct = 100*used/100 = used.
    Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/1000);
    const int rand0 = hal.rand_calls;
    hal._airtime_used = 0;   CHECK(node->compute_budget_tier() == BT::healthy);    // 0%
    hal._airtime_used = 49;  CHECK(node->compute_budget_tier() == BT::healthy);    // 49% < 50
    hal._airtime_used = 50;  CHECK(node->compute_budget_tier() == BT::strained);   // 50% -> STRAINED
    hal._airtime_used = 79;  CHECK(node->compute_budget_tier() == BT::strained);   // 79% < 80
    hal._airtime_used = 80;  CHECK(node->compute_budget_tier() == BT::critical);   // 80% -> CRITICAL
    hal._airtime_used = 94;  CHECK(node->compute_budget_tier() == BT::critical);   // 94% < 95
    hal._airtime_used = 95;  CHECK(node->compute_budget_tier() == BT::exhausted);  // 95% -> EXHAUSTED
    hal._airtime_used = 200; CHECK(node->compute_budget_tier() == BT::exhausted);  // >100%
    CHECK(hal.rand_calls - rand0 == 0);   // pure, no draws
    delete node;
    // duty_cycle <= 0 -> disabled -> always HEALTHY even at saturation
    TestHal hal2; Node* off = mk_budget_node(hal2, /*duty=*/0.0, /*window=*/1000);
    hal2._airtime_used = 1000000; CHECK(off->compute_budget_tier() == BT::healthy);
    delete off;
    // plumb-proof (review #12): the r6 gate values (0.1, 1h) derive a NON-ZERO budget (360000ms),
    // so the tier crosses HEALTHY->STRAINED at 50% (180000ms) — not a silent disabled no-op.
    TestHal hal3; Node* r6 = mk_budget_node(hal3, /*duty=*/0.1, /*window=*/3600000);
    hal3._airtime_used = 179999; CHECK(r6->compute_budget_tier() == BT::healthy);    // 49.99% < 50
    hal3._airtime_used = 180000; CHECK(r6->compute_budget_tier() == BT::strained);   // 50% of 360000ms
    delete r6;
}

TEST_CASE("R4.1 budget NACK emit — receiver >=CRITICAL refuses an RTS with reason=1 (tier in high nibble)") {
    std::array<uint8_t,16> rb{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    // HEALTHY (10%) -> normal CTS, NO budget NACK
    {
        TestHal hal; Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/1000);   // budget 100ms
        hal._airtime_used = 10;
        const size_t rn = mk_rts(/*src=*/2,/*next=*/1,/*dst=*/9,/*ctr_lo=*/5,/*plen=*/10, rb);
        node->on_recv(rb.data(), rn, m2);
        CHECK(hal.count("cts_tx") == 1);
        CHECK(hal.count("nack_tx") == 0);
        delete node;
    }
    // CRITICAL (85%) on a FRESH node (no stale pending_rx that would BUSY_RX first) -> BUDGET NACK
    // reason=1, tier=2 in the high nibble, NO CTS.
    {
        // Budget 10000ms (window 100000 @ 10%): pct still 85% -> CRITICAL, but realistic enough that the ~36ms NACK
        // FITS the budget (8500+36 <= 10000) so tx_with_retry's duty pre-check (#2) doesn't defer it. A tiny 100ms
        // budget would faithfully duty-DEFER the NACK (the Lua does too) — production 1%/1h budgets fit it easily.
        TestHal hal; Node* node = mk_budget_node(hal, /*duty=*/0.10, /*window=*/100000);
        hal._airtime_used = 8500;
        const size_t rn = mk_rts(/*src=*/3,/*next=*/1,/*dst=*/8,/*ctr_lo=*/6,/*plen=*/10, rb);
        RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)}; node->on_recv(rb.data(), rn, m3);
        const Ev* nk = hal.last("nack_tx"); CHECK(nk != nullptr);
        if (nk) { CHECK(nk->to == 3); CHECK(nk->reason == protocol::nack_reason_budget); }
        CHECK(hal.count("cts_tx") == 0);   // NO CTS on the refused RTS
        {   // the REAL emitted NACK bytes carry tier=CRITICAL(2) in the high nibble
            auto pn = parse_nack(std::span<const uint8_t>(hal.last_tx("NACK")->bytes.data(),
                                                          hal.last_tx("NACK")->bytes.size()));
            CHECK(pn.has_value());
            if (pn) { CHECK(pn->reason == protocol::nack_reason_budget);
                      CHECK(((pn->payload >> 4) & 0x0f) == 2); }
        }
        delete node;
    }
}

TEST_CASE("R4.1 budget NACK react — sender blinds the next hop (tier-scaled) + requeues, no draws") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,2,14}});   // via2 primary, via3 alt
    hal._now = 1000;
    send_cmd(*node, 5, "hi");                                       // pending_tx via 2, ctr_lo=1
    CHECK(!node->is_blind(2));
    const int rand_before = hal.rand_calls;
    std::array<uint8_t,8> nb{};
    // BUDGET NACK reason=1, tier=CRITICAL(2) -> blind via2 for budget_blind_critical_ms, requeue.
    const size_t nn = mk_nack(/*to=*/1, /*ctr_lo=*/1, /*reason=*/protocol::nack_reason_budget,
                              /*payload=*/(2 << 4), nb);
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("blind_observed") == 1);
    CHECK(node->is_blind(2));                                       // via2 now blind
    { const Ev* nr = hal.last("nack_rx"); CHECK(nr != nullptr);
      if (nr) { CHECK(nr->reason == protocol::nack_reason_budget); } }
    CHECK(hal.count("cascade_requeue") == 1);                       // requeued via the helper (caps not hit)
    CHECK(hal.rand_calls - rand_before == 0);                      // DRAW-FREE
    // drain the backoff (requeue_count=1 -> backoff 5000): the re-issue skips the blind via2 -> via3
    hal._now = 6000; node->on_timer(kCascadeRequeueTimerId);
    CHECK(node->is_blind(2));                                       // still blind at 6000 (window 180000)
    { const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
      if (r) CHECK(r->next == 3); }
    delete node;
}

TEST_CASE("R4.1 budget NACK react — tier-scaled blind window (STRAINED < CRITICAL < EXHAUSTED)") {
    // EXHAUSTED(3) gets the longest window; probe is_blind just past the strained window to show
    // EXHAUSTED still blind there while a STRAINED-tier blind would have lapsed.
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        send_cmd(*node, 5, "hi");
        std::array<uint8_t,8> nb{};
        const size_t nn = mk_nack(1, 1, protocol::nack_reason_budget, (3 << 4), nb);   // EXHAUSTED
        RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
        hal._now = protocol::budget_blind_strained_ms + 1;        // past the STRAINED window
        CHECK(node->is_blind(2));                                 // EXHAUSTED window is longer -> still blind
        hal._now = protocol::budget_blind_exhausted_ms + 1;       // past the EXHAUSTED window
        CHECK(!node->is_blind(2));
        delete node;
    }
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        send_cmd(*node, 5, "hi");
        std::array<uint8_t,8> nb{};
        const size_t nn = mk_nack(1, 1, protocol::nack_reason_budget, (1 << 4), nb);   // STRAINED
        RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
        hal._now = protocol::budget_blind_strained_ms + 1;        // past the STRAINED window
        CHECK(!node->is_blind(2));                                // STRAINED window already lapsed
        delete node;
    }
}

// ---- R4.2 — persistent neighbor tier mark + route penalty + ACK budget_hint ----
TEST_CASE("R4.2 tier mark — max-merge (no downgrade) + tier-0 no-op + TTL lazy-prune") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    hal._now = 1000;
    CHECK(node->get_neighbor_tier(2) == 0);                       // unmarked
    node->mark_neighbor_budget_tier(2, /*CRITICAL*/2, "test", true);
    CHECK(node->get_neighbor_tier(2) == 2);
    node->mark_neighbor_budget_tier(2, /*STRAINED*/1, "test", true);   // lower -> max-merge keeps CRITICAL (dv:4323)
    CHECK(node->get_neighbor_tier(2) == 2);
    node->mark_neighbor_budget_tier(2, /*EXHAUSTED*/3, "test", true);  // higher -> upgrades
    CHECK(node->get_neighbor_tier(2) == 3);
    node->mark_neighbor_budget_tier(2, /*HEALTHY*/0, "test", true);    // tier 0 -> no-op
    CHECK(node->get_neighbor_tier(2) == 3);
    hal._now = 1000 + protocol::neighbor_budget_tier_ttl_ms;      // >= TTL from the last set -> lazy prune on read
    CHECK(node->get_neighbor_tier(2) == 0);
    delete node;
}

TEST_CASE("R4.2 route demotion — marking a CRITICAL primary reranks it below the viable alt") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,1,14}});  // via2,via3 EQUAL -> via2 primary
    node->on_timer(kTriggeredBeaconTimerId);                      // flush any pending triggered beacon
    const int rb = hal.rand_calls;
    const int reranked = node->mark_neighbor_budget_tier(2, /*CRITICAL*/2, "nack_budget", /*local_only=*/false);
    CHECK(reranked == 1);                                         // the primary moved
    CHECK(hal.rand_calls - rb == 1);                             // !local_only + primary moved -> ONE triggered-beacon draw
    CHECK(node->get_neighbor_tier(2) == 2);
    CHECK(hal.count("rt_penalty_rerank") == 1);
    CHECK(hal.count("neighbor_budget_mark") == 1);
    send_cmd(*node, 5, "x");                                      // now routes via the alt (via2 demoted, NOT blind)
    const Ev* r = hal.last("rts_tx"); CHECK(r != nullptr);
    if (r) CHECK(r->next == 3);
    delete node;
}

TEST_CASE("R4.2 BUDGET NACK reaction reranks the route; demotion OUTLIVES the blind window") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14},{3,1,14}});  // equal -> via2 primary
    node->on_timer(kTriggeredBeaconTimerId);
    hal._now = 1000;
    send_cmd(*node, 5, "hi");                                     // RTS to via2
    { const Ev* r = hal.last("rts_tx"); if (r) CHECK(r->next == 2); }
    std::array<uint8_t,8> nb{};
    const size_t nn = mk_nack(1, 1, protocol::nack_reason_budget, (2 << 4), nb);   // CRITICAL from via2
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node->on_recv(nb.data(), nn, m2);
    CHECK(hal.count("neighbor_budget_mark") == 1);               // the react marked via2 ...
    CHECK(hal.count("rt_penalty_rerank") == 1);                 // ... and reranked the route
    CHECK(node->get_neighbor_tier(2) == 2);
    // route demotion (tier TTL 300000) OUTLIVES the short blind window (CRITICAL 180000): past the
    // blind but within the TTL, via2 is no longer blind yet still tier-marked -> route stays demoted.
    hal._now = 1000 + protocol::budget_blind_critical_ms + 1;
    CHECK(!node->is_blind(2));
    CHECK(node->get_neighbor_tier(2) == 2);
    delete node;
}

TEST_CASE("R4.2 ACK budget_hint — STRAINED forwarder's ACK carries the tier; sender marks it (local_only, no draw)") {
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    // EMIT: a STRAINED forwarder (60%) still CTSes (only >=CRITICAL refuses) and ACKs with budget_hint=STRAINED.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
        cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 1000;   // budget 100ms
        node.on_init(cfg);
        hal._airtime_used = 60;                                   // 60% -> STRAINED (CTSes, doesn't refuse)
        RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
        const size_t rn = mk_rts(/*src=*/2,/*next=*/1,/*dst=*/1,/*ctr_lo=*/3,/*plen=*/10, rb, 0, /*origin=*/0, /*ctr=*/3);  // dst=self -> deliver path
        node.on_recv(rb.data(), rn, m2);
        CHECK(hal.count("cts_tx") == 1);                          // STRAINED still CTSes
        const size_t dn = mk_data_hb(1, 1, 3, 0, /*hops_remaining=*/5, /*committed=*/0, "x", db);
        node.on_recv(db.data(), dn, m2);
        auto pk = parse_ack(std::span<const uint8_t>(hal.last_tx("ACK")->bytes.data(),
                                                     hal.last_tx("ACK")->bytes.size()));
        CHECK(pk.has_value());
        if (pk) CHECK(pk->budget_hint == 1);                     // STRAINED in the ACK (min(CRITICAL, tier))
    }
    // CONSUME: a sender receiving an ACK with budget_hint=STRAINED marks the next-hop (local_only -> NO beacon/draw).
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        node->on_timer(kTriggeredBeaconTimerId);
        send_cmd(*node, 5, "hi");                                 // RTS to via2
        std::array<uint8_t,8> cb{};
        const size_t cn = mk_cts(1, 2, 7, cb);
        RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
        node->on_timer(kCtsToDataGapTimerId);                    // DATA tx -> awaiting_ack
        const int rb2 = hal.rand_calls;
        std::array<uint8_t,8> ab{};
        const size_t an = mk_ack_hint(/*to=*/1, /*ctr_lo=*/1, /*budget_hint=*/1, ab);   // ACK from via2, STRAINED
        node->on_recv(ab.data(), an, m2);
        CHECK(node->get_neighbor_tier(2) == 1);                  // marked from the ACK
        CHECK(hal.rand_calls - rb2 == 0);                        // local_only -> NO triggered-beacon draw
        delete node;
    }
}

// review #07: a node STRAINED at RTS-time (so it CTSes, doesn't refuse) that climbs to EXHAUSTED by
// DATA-time -> the forward ACK hint must CAP at CRITICAL(2), not carry EXHAUSTED(3). Drive the mid-flight
// tier climb by bumping the scripted airtime between the RTS and the DATA.
TEST_CASE("R4.2 ACK budget_hint — EXHAUSTED at DATA-time caps the forward hint at CRITICAL") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000;     // budget 10000ms (realistic — the CTS/ACK fit it)
    node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._airtime_used = 6000;                                     // 60% STRAINED at RTS -> CTSes (no refuse)
    const size_t rn = mk_rts(/*src=*/2,/*next=*/1,/*dst=*/1,/*ctr_lo=*/3,/*plen=*/10, rb, 0, /*origin=*/0, /*ctr=*/3);
    node.on_recv(rb.data(), rn, m2);
    CHECK(hal.count("cts_tx") == 1);
    hal._airtime_used = 9600;                                     // 96% EXHAUSTED by DATA-time (ACK still fits: 9600+air <= 10000)
    const size_t dn = mk_data_hb(1, 1, 3, 0, /*hops_remaining=*/5, /*committed=*/0, "x", db);
    node.on_recv(db.data(), dn, m2);
    auto pk = parse_ack(std::span<const uint8_t>(hal.last_tx("ACK")->bytes.data(),
                                                 hal.last_tx("ACK")->bytes.size()));
    CHECK(pk.has_value());
    if (pk) CHECK(pk->budget_hint == 2);                         // min(CRITICAL, EXHAUSTED) = CRITICAL(2)
}

// ---- R4.4 — originator anti-spam (1st-hop statistical rate-limit) ----
TEST_CASE("R4.4 compute_originator_metric — distinct ctr_lo, 10s dedup, window prune, apparent=max(0,rts-cts)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    int app; uint32_t air; uint8_t rts, cts;
    // 3 distinct-ctr_lo RTSes from sender 9 (air 10 each) -> rts=3
    hal._now = 1000;
    node.track_originator_observation(9, /*rts*/0, 1, 10);
    node.track_originator_observation(9, /*rts*/0, 2, 10);
    node.track_originator_observation(9, /*rts*/0, 3, 10);
    // a RETRY of ctr_lo=1 within the 10s dedup window -> NOT a new event, air NOT re-added
    hal._now = 5000;
    node.track_originator_observation(9, /*rts*/0, 1, 10);
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 3); CHECK(cts == 0); CHECK(app == 3); CHECK(air == 30);   // dedup: still 3 events, 30ms
    // one CTS from 9 -> cts=1 -> apparent = 3-1 = 2
    node.track_originator_observation(9, /*cts*/1, 1, 10);
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 3); CHECK(cts == 1); CHECK(app == 2); CHECK(air == 40);
    // advance past the window (300000) from the ctr_lo=2/3 events (t=1000) but the ctr_lo=1 rts was
    // refreshed to t=5000 and the cts to now; prune drops the t=1000 events.
    hal._now = 1000 + protocol::originator_window_ms + 1;   // 301001: t=1000 events pruned, t=5000 kept
    node.track_originator_observation(9, /*rts*/0, 4, 10);  // triggers a prune + adds ctr_lo=4
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 2);   // ctr_lo=1 (refreshed to 5000) + ctr_lo=4; the t=1000 ctr_lo=2,3 pruned
    CHECK(app == 1);   // rts 2 - cts 1 (the cts at now is kept) ... apparent = max(0, 2-1) = 1
}

TEST_CASE("R4.4 originator ledger — fixed ring caps at cap_originator_events, evicts oldest, metric stays correct") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    // Overflow the heap-free ring: cap+8 RTSes from sender 9, ctr_lo cycling 0..15, 1s apart. Same ctr_lo
    // recurs every 16s (> the 10s dedup window) so NONE dedup -> cap+8 distinct events, all inside the
    // 5-min window (no prune). The ring must cap at N and evict the oldest 8, not grow unboundedly.
    const int N = protocol::cap_originator_events;
    const int over = 8;
    for (int i = 0; i < N + over; ++i) {
        hal._now = 1000 + (uint64_t)i * 1000;
        node.track_originator_observation(9, /*rts*/0, (uint8_t)(i % 16), 10);
    }
    int app; uint32_t air; uint8_t rts, cts;
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(rts == 16);                     // the retained recent N events still cover all 16 distinct ctr_lo
    CHECK(cts == 0);
    CHECK(app == 16);
    CHECK(air == (uint32_t)(N * 10));     // capped at N (oldest `over` evicted) — NOT (N+over)*10 = no unbounded growth
}

TEST_CASE("R4.4 airtime backstop — a sender UNDER the airtime cap is NOT throttled (no false-positive)") {
    // Post-Inc-1 the throttle is airtime-only: the R-C apparent-origination COUNT clause was removed (a
    // missed CTS made a forwarder look like an originator -> false-drops, 168 on s18). The backstop is
    // honesty-independent — it caps a heavy NEIGHBOUR's airtime regardless of originate-vs-forward — so
    // the old "balanced forwarder (rts~=cts) is exempt" distinction no longer applies (a heavy forwarder
    // IS capped). What MUST hold instead, and is the no-false-positive guarantee: a sender whose overheard
    // airtime stays under the cap is never throttled (mirrors s18 dormancy — heaviest legit << cap).
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.04; cfg.duty_cycle_window_ms = 300000;   // budget 12000ms -> airtime cap 3000ms
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    for (uint8_t i = 0; i < 5; ++i) {                           // 5 overheard RTSes from 9 (each ~tens of ms)
        const size_t n = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/i,/*plen=*/10, rb);
        node.on_recv(rb.data(), n, m9);
    }
    const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/5,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m9);                            // addressed to us — total airtime << 3000ms cap
    CHECK(hal.count("rts_drop_originator_throttle") == 0);      // under cap -> NOT throttled
    CHECK(hal.count("cts_tx") == 1);                            // CTSed normally
}

TEST_CASE("R4.4 airtime backstop — over the 25%-budget airtime cap drops even when apparent <= max") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> airtime cap floor(0.35*10)=3ms
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    const size_t ov = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb);
    node.on_recv(rb.data(), ov, m9);                            // 1 overheard RTS (airtime >> 2ms cap)
    const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/1,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m9);                            // apparent=2 (<6) but total_air > 2ms cap
    CHECK(hal.count("rts_drop_originator_throttle") == 1);      // dropped via the airtime backstop
    CHECK(hal.count("cts_tx") == 0);
}

// review #00/#01: with duty DISABLED (budget 0) the airtime backstop must be OFF (no airtime share to
// enforce) — NOT a 0 cap that drops every RTS. Post-Inc-1 there is NO count fallback, so budget 0 means
// NO throttle at all. Guard fixed in BOTH engines.
TEST_CASE("R4.4 airtime backstop OFF when duty disabled (budget 0) -> no throttle (no count fallback)") {
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,9};
    // duty=0: an overheard + an addressed RTS (airtime >> any cap) -> NOT dropped (backstop skipped), CTSed.
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;   // duty_cycle = 0 (disabled)
    node.on_init(cfg);
    const size_t ov = mk_rts(9,99,8,0,10,rb); node.on_recv(rb.data(), ov, m9);
    const size_t rn = mk_rts(9, 1,8,1,10,rb); node.on_recv(rb.data(), rn, m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 0);  // budget 0 -> backstop SKIPPED, no count fallback
    CHECK(hal.count("cts_tx") == 1);
}

TEST_CASE("R4.4 Inc 2 — DATA airtime feeds the ledger (kind=data) + warn band fires below the drop cap") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.02; cfg.duty_cycle_window_ms = 300000;   // budget 6000ms -> cap 2100ms, warn 1680ms (share 0.35)
    node.on_init(cfg);
    // DATA airtime (kind=2) feeds total_air just like RTS/CTS — that's what gives the backstop teeth
    // (RTS-only airtime never approached the cap). 1800ms lands in the [1680,2100) warn band.
    node.track_originator_observation(9, /*kind=data*/2, /*ctr_lo=*/0, /*air=*/1800);
    int app; uint32_t air; uint8_t rts, cts;
    node.compute_originator_metric(9, app, air, rts, cts);
    CHECK(air == 1800);   // DATA airtime counted
    CHECK(rts == 0);      // kind=data is neither rts...
    CHECK(cts == 0);      // ...nor cts (no false apparent-origination)
    // An addressed RTS from 9: total_air (1300 + the RTS's own airtime, still < 1500) is in the warn band
    // -> rts_originator_airtime_warn, but NOT over the cap -> CTSed, not dropped.
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    const size_t rn = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/1,/*plen=*/10, rb);
    node.on_recv(rb.data(), rn, m9);
    CHECK(hal.count("rts_originator_airtime_warn") == 1);   // warn band hit
    CHECK(hal.count("rts_drop_originator_throttle") == 0);  // under cap -> not dropped
    CHECK(hal.count("cts_tx") == 1);                        // warn does not block the CTS
}

// ---- e2e-ack backstop exemption + anti-spoof (2026-07-02) ------------------
// (c) codec round-trip: RTS_FLAG_E2E_ACK survives pack -> parse (it's the 4th free bit of the rts_flags nibble).
TEST_CASE("e2e-ack exemption — RTS_FLAG_E2E_ACK survives the RTS codec round-trip") {
    rts_in in{}; in.leaf_id = 0; in.src = 7; in.next = 3; in.ctr_lo = 5; in.dst = 9;
    in.sf_index = 3; in.rts_flags = RTS_FLAG_E2E_ACK; in.payload_len = 12; in.m_payload_id_lo16 = 0;
    in.id = rts_flight_identity_plain(7, 0x0105);
    std::array<uint8_t, 16> b{};
    const size_t n = pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
    CHECK(n == 10);                                                 // §hybrid-rts S1: plaintext DM RTS = 7-B base + 3-B identity
    auto out = parse_rts(std::span<const uint8_t>(b.data(), n));
    CHECK(out.has_value());
    const rts_out o = out.value_or(rts_out{});
    CHECK((o.rts_flags & RTS_FLAG_E2E_ACK) != 0);                  // the bit round-trips
    CHECK((o.rts_flags & RTS_FLAG_RELAY) == 0);                    // and does NOT alias the neighbouring flags
    CHECK((o.rts_flags & RTS_FLAG_FLOOD) == 0);
    CHECK((o.rts_flags & RTS_FLAG_M_BROADCAST) == 0);
    CHECK_FALSE(o.m_broadcast); CHECK_FALSE(o.flood);
    // Sanity: an RTS with all four flag bits set carries all four (whole-nibble pack/parse).
    in.rts_flags = static_cast<uint8_t>(RTS_FLAG_M_BROADCAST | RTS_FLAG_RELAY | RTS_FLAG_E2E_ACK);
    in.id = RtsFlightIdentity{};   // §hybrid-rts S1: this is now an M_BROADCAST frame, which carries NO identity
    const size_t n2 = pack_rts(in, std::span<uint8_t>(b.data(), b.size()));
    CHECK(n2 == 9);                // ...and is 9 B, not 10
    auto out2 = parse_rts(std::span<const uint8_t>(b.data(), n2));
    CHECK(out2.has_value());
    CHECK((out2.value_or(rts_out{}).rts_flags & RTS_FLAG_E2E_ACK) != 0);
    CHECK((out2.value_or(rts_out{}).rts_flags & RTS_FLAG_RELAY)   != 0);
}

// (a) EXEMPTION: an over-airtime sender's PLAIN DM RTS is DROPPED by the backstop, but the SAME over-budget
// sender's RTS with RTS_FLAG_E2E_ACK set is NOT dropped (CTS proceeds) — an ack is never throttled.
TEST_CASE("e2e-ack exemption — an over-airtime sender's RTS is dropped, but its E2E_ACK RTS is exempt") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> airtime cap floor(0.35*10)=3ms
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    // drive sender 9 over the airtime cap: one overheard RTS (its own airtime >> 3ms cap)
    const size_t ov = mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb);
    node.on_recv(rb.data(), ov, m9);
    // a PLAIN DM RTS addressed to us -> DROPPED (backstop fires, no CTS)
    const size_t plain = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/1,/*plen=*/10, rb);
    node.on_recv(rb.data(), plain, m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 1);
    CHECK(hal.count("cts_tx") == 0);
    // the SAME over-budget sender, but the RTS marks RTS_FLAG_E2E_ACK -> NOT dropped, CTS proceeds
    const size_t ackrts = mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/2,/*plen=*/10, rb, RTS_FLAG_E2E_ACK);
    node.on_recv(rb.data(), ackrts, m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 1);   // STILL 1 — the ack RTS was exempt (no new drop)
    CHECK(hal.count("cts_tx") == 1);                         // CTSed
}

// (b) ANTI-SPOOF: a marked RTS -> a DATA whose type != DATA_TYPE_E2E_ACK -> e2e_ack_spoof fires + the sender is
// flagged; a SECOND marked RTS from that sender is then DROPPED (the exemption is revoked while flagged).
TEST_CASE("e2e-ack anti-spoof — a lied E2E_ACK bit flags the sender and revokes its exemption") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;   // budget 10ms -> cap 3ms (backstop armed)
    node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    // push sender 9 over the airtime cap so the backstop WOULD drop a plain RTS
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb), m9);
    // a marked E2E_ACK RTS addressed to us -> exempt -> CTS + _pending_rx (claimed_e2e_ack)
    hal._now = 1000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/5,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("cts_tx") == 1);
    // the DATA that follows is a PLAIN DM (type 0, NOT DATA_TYPE_E2E_ACK) -> the sender lied
    hal._now = 2000;
    node.on_recv(db.data(), mk_data(/*next=*/1, /*dst=*/1, /*ctr=*/0x0005, /*origin=*/9, "hi", db), m9);
    CHECK(hal.count("e2e_ack_spoof") == 1);                  // caught + flagged
    const Ev* sp = hal.last("e2e_ack_spoof");
    CHECK(sp != nullptr);
    if (sp) CHECK(sp->from == 9);                            // keyed on the PHYSICAL sender (RTS src), not the sealed origin
    // a SECOND marked RTS from 9 is now DROPPED — the exemption is revoked while flagged
    hal._now = 3000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/6,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 1);   // dropped despite the E2E_ACK bit (spoofer flagged)
    CHECK(hal.count("cts_tx") == 1);                         // no new CTS
}

// (d) a GENUINE marked RTS -> a real DATA_TYPE_E2E_ACK -> NO e2e_ack_spoof, the sender is NOT flagged.
TEST_CASE("e2e-ack anti-spoof — a genuine E2E_ACK does NOT flag the sender") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.001; cfg.duty_cycle_window_ms = 10000;
    node.on_init(cfg);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m9{8.0f,-80.0f,0,static_cast<int8_t>(9)};
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/99,/*dst=*/8,/*ctr_lo=*/0,/*plen=*/10, rb), m9);   // over cap
    hal._now = 1000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/5,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("cts_tx") == 1);
    // a REAL DATA_TYPE_E2E_ACK follows (inner body = acked ctr, 2 B) -> honest, no flag
    hal._now = 2000;
    const uint8_t ack_body[2] = { 0x05, 0x00 };   // acked ctr = 5
    node.on_recv(db.data(),
        mk_data_e2e(/*next=*/1, /*dst=*/1, /*ctr=*/0x0005, /*origin=*/9, /*flags=*/0,
                    ack_body, 2, db, /*type=*/DATA_TYPE_E2E_ACK), m9);   // type!=0 -> pack_data auto-sets DATA_FLAG_APP
    CHECK(hal.count("e2e_ack_spoof") == 0);                  // honest ack -> never flagged
    // a SECOND marked RTS from 9 is STILL exempt (not flagged) -> CTS proceeds
    hal._now = 3000;
    node.on_recv(rb.data(), mk_rts(/*src=*/9,/*next=*/1,/*dst=*/8,/*ctr_lo=*/6,/*plen=*/10, rb, RTS_FLAG_E2E_ACK), m9);
    CHECK(hal.count("rts_drop_originator_throttle") == 0);   // never dropped (honest sender stays exempt)
    CHECK(hal.count("cts_tx") == 2);
}

TEST_CASE("DM Inc 3 — ACK warn bit round-trips through pack/parse (byte1 rsv nibble, ACK stays 3 B)") {
    uint8_t buf[3];
    // warn=true: fits the byte1 rsv nibble with NO growth; all other fields survive.
    { ack_in in{}; in.ctr_lo = 5; in.budget_hint = 2; in.snr_bucket = 1; in.to = 42; in.warn = true;
      const size_t n = pack_ack(in, std::span<uint8_t>(buf, 3));
      CHECK(n == 3);                                        // 3 B — no growth (vs the Lua's 4 B)
      auto out = parse_ack(std::span<const uint8_t>(buf, 3));
      CHECK(out.has_value());
      const ack_out o = out.value_or(ack_out{});
      CHECK(o.warn == true);
      CHECK(o.ctr_lo == 5);
      CHECK(o.budget_hint == 2);
      CHECK(o.snr_bucket == 1);
      CHECK(o.to == 42); }
    // warn=false round-trips too (bit 0 clear).
    { ack_in in{}; in.ctr_lo = 5; in.budget_hint = 2; in.snr_bucket = 1; in.to = 42; in.warn = false;
      pack_ack(in, std::span<uint8_t>(buf, 3));
      auto out = parse_ack(std::span<const uint8_t>(buf, 3));
      CHECK(out.has_value());
      CHECK(out.value_or(ack_out{}).warn == false); }
}

TEST_CASE("Slice3 — the flat self-cap is removed (no originator_self_defer; own DMs not count-capped)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    node.on_init(cfg);
    // Seed a direct route to bob(2) so an origination reaches issue_send.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // Fire many own DMs, each spaced well past any burst floor: none may hit the (deleted) flat count-cap.
    for (int k = 0; k < 30; ++k) {
        hal._now = 2000 + static_cast<uint64_t>(k) * 10000;   // 10 s apart (> dm_min_interval)
        send_cmd(node, /*dst=*/2, "hi");
        node.on_timer(kQueueWakeupTimerId);                   // let any deferred re-pick drain
    }
    CHECK(hal.count("originator_self_defer") == 0);           // the flat self-cap defer no longer exists
}

// The DM burst floor is CHECKED at become_free (defer-in-place) but only ARMED (_last_dm_origin_ms stamped)
// when an own DM actually flies (issue_send). So it bites a 2nd own DM only once the queue is idle again
// (the single-flight gate already holds a 2nd DM behind an in-flight one). Here DM #1 completes its flight,
// then a 2nd own DM originated < 3 s later is deferred by the floor; one originated >= 3 s later passes.
static void complete_dm_flight_via2(Node& node, TestHal& hal, uint8_t ctr_lo, uint64_t cts_at, uint64_t ack_at) {
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    hal._now = cts_at; node.on_recv(cb.data(), cn, bob);
    node.on_timer(kCtsToDataGapTimerId);                 // CTS->DATA gap -> DATA tx
    std::array<uint8_t, 8> ab{};
    const size_t an = mk_ack(/*to=*/1, ctr_lo, ab);
    hal._now = ack_at; node.on_recv(ab.data(), an, bob);
}

TEST_CASE("Slice3 — dm_min_interval: a <3s 2nd own DM defers, a >=3s one passes") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bmeta{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bmeta);
    // DM #1 -> issues immediately (queue idle), stamps _last_dm_origin_ms=5000; complete the flight.
    hal._now = 5000; send_cmd(node, /*dst=*/2, "a");
    CHECK(hal.count("rts_tx") == 1);
    complete_dm_flight_via2(node, hal, /*ctr_lo=*/1, /*cts_at=*/5100, /*ack_at=*/5200);
    CHECK(hal.count("ack_rx") == 1);                 // flight #1 done -> queue idle
    // DM #2 only ~800 ms after DM #1's stamp -> picked at become_free, deferred by the 3 s floor.
    hal._now = 5800; send_cmd(node, /*dst=*/2, "b");
    CHECK(hal.count("rts_tx") == 1);                 // still 1 -> not issued
    CHECK(hal.count("send_blocked") >= 1);           // the DM burst floor tripped
    // Advance past 3 s from DM #1 (>=8000) and re-drain -> DM #2 now issues.
    hal._now = 8001; node.on_timer(kQueueWakeupTimerId);
    CHECK(hal.count("rts_tx") == 2);
}

// MF9: an own DM stamps _last_dm_origin_ms; an own e2e-ack originated < 3 s later must still enqueue AND
// drain — the DM burst floor is exempt for DATA_TYPE_E2E_ACK so a bridge's cross-layer ack-confirms never
// self-throttle. send_e2e_ack is private, so we drive it via the real RX path (an E2E_ACK_REQ DATA to us,
// origin=2 which we have a route to) — this exercises the exemption branch when become_free picks the ack.
TEST_CASE("Slice3 — own e2e-ack origination is NOT throttled by dm_min_interval") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);   // self = alice(1)
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    // Route to bob(2) so both the DM and the e2e-ack (back to origin 2) have a next hop.
    std::array<uint8_t, 64> bb{};
    const size_t bn = mk_beacon(/*src=*/2, bb);
    CHECK(bn > 0);
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 1000; node.on_recv(bb.data(), bn, bob);

    // An own DM to 2 -> issues + stamps _last_dm_origin_ms, then complete the flight (CTS -> DATA -> ACK)
    // so pending_tx clears and become_free is free to drain the ack next.
    hal._now = 5000; send_cmd(node, /*dst=*/2, "a");
    CHECK(hal.count("rts_tx") == 1);
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    hal._now = 5100; node.on_recv(cb.data(), cn, bob);
    node.on_timer(kCtsToDataGapTimerId);                 // CTS->DATA gap -> DATA tx
    std::array<uint8_t, 8> ab{};
    const size_t an = mk_ack(/*to=*/1, /*ctr_lo=*/1, ab);
    hal._now = 5200; node.on_recv(ab.data(), an, bob);   // ACK -> flight done, pending_tx clears
    CHECK(hal.count("ack_rx") == 1);

    // Now, only ~300 ms after the DM stamped the floor, receive an E2E_ACK_REQ DATA addressed to us (dst=1,
    // origin=2) -> the node originates an own e2e-ack back to 2. Exempt by TYPE -> must enqueue AND issue.
    // The RTS/CTS handshake precedes the DATA (src=2 -> us), matching the E2E-ACK delivery path.
    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/2, /*next=*/1, /*dst=*/1, /*ctr_lo=*/7, /*plen=*/15, rb);
    hal._now = 5500; node.on_recv(rb.data(), rn, bob);
    std::array<uint8_t, 64> db{};
    const uint8_t body[2] = { 'h', 'i' };
    const size_t dn = mk_data_e2e(/*next=*/1, /*dst=*/1, /*ctr=*/0x0007, /*origin=*/2,
                                  DATA_FLAG_E2E_ACK_REQ, body, 2, db);
    hal._now = 5600; node.on_recv(db.data(), dn, bob);
    node.on_timer(kPostAckTimerId);                      // deliver -> originate the E2E ack
    CHECK(hal.count("e2e_ack_tx") == 1);                 // the ack was enqueued despite < dm_min_interval_ms
    CHECK(hal.count("send_blocked") == 0);               // NOT throttled -> the DataType exemption held
    CHECK(hal.count("rts_tx") == 2);                     // and its RTS actually issued (not deferred in place)
}

// ---- R4.3 — adaptive beacon throttle + silence-jitter (THE determinism golden) ----
static Node* mk_throttle_node(TestHal& hal, uint32_t quiet_ms, uint32_t max_idle_ms) {
    Node* node = new Node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.quiet_threshold_ms = quiet_ms; cfg.beacon_max_idle_ms = max_idle_ms;
    node->on_init(cfg);
    return node;
}

TEST_CASE("R4.3 rand-order golden — silence-jitter draws ONLY when throttled+gate-passed (gates stay byte-identical)") {
    // (a) quiet=0 (the fast path EVERY existing gate uses): a periodic fire draws EXACTLY 1 (the re-arm),
    //     NOT 2 — proves NO silence-jitter draw is added, so the gate streams are unperturbed.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/0, /*max_idle=*/0);
        hal._now = 5000;
        const int rb = hal.rand_calls;
        node->on_timer(kBeaconTimerId);
        CHECK(hal.rand_calls - rb == 1);                  // re-arm only
        CHECK(hal.count("beacon_tx") >= 1);               // fast path emits
        delete node;
    }
    // (b) quiet>0, channel QUIET (since_rx=inf >= quiet -> gate passes): draws 2 (silence-jitter + re-arm).
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;                                // no prior RX -> since_rx = inf -> gate passes
        const int rb = hal.rand_calls;
        node->on_timer(kBeaconTimerId);
        CHECK(hal.rand_calls - rb == 2);                  // silence-jitter THEN re-arm
        delete node;
    }
    // (c) quiet>0, channel BUSY (fresh witness, since_rx=0 < quiet -> gate fails): draws 1 (re-arm only),
    //     emits beacon_skipped_busy, NO beacon, NO silence-jitter draw.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;
        std::array<uint8_t,16> rb_{}; RxMeta m2{8.0f,-80.0f,0,2};
        const size_t rn = mk_rts(2,99,8,0,10,rb_); node->on_recv(rb_.data(), rn, m2);   // a decode -> witness fresh
        const int beacons_before = hal.count("beacon_tx");
        const int rb = hal.rand_calls;
        node->on_timer(kBeaconTimerId);
        CHECK(hal.rand_calls - rb == 1);                  // re-arm only (gate failed -> no silence-jitter draw)
        CHECK(hal.count("beacon_skipped_busy") == 1);
        CHECK(hal.count("beacon_tx") == beacons_before);  // suppressed
        delete node;
    }
}

TEST_CASE("R4.3 witness — on_preamble_detected AND a decode both make the channel look busy (gate suppresses)") {
    // on_preamble_detected sets the witness even without a decode -> the next quiet>0 fire suppresses.
    TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
    hal._now = 200000;
    node->on_preamble_detected(200000);                   // channel busy NOW (IRQ, no decode)
    const int rb = hal.rand_calls;
    node->on_timer(kBeaconTimerId);
    CHECK(hal.rand_calls - rb == 1);                      // gate failed (since_rx=0) -> no silence-jitter draw
    CHECK(hal.count("beacon_skipped_busy") == 1);
    delete node;
}

TEST_CASE("R4.3 deferred jitter re-check — busy during the jitter window stands down; quiet emits") {
    // STAND DOWN: a decode lands during the jitter window -> the deferred fire skips (post_jitter).
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;
        std::array<uint8_t,16> rb_{}; RxMeta m2{8.0f,-80.0f,0,2};
        const size_t rn = mk_rts(2,99,8,0,10,rb_); node->on_recv(rb_.data(), rn, m2);   // fresh witness
        const int beacons_before = hal.count("beacon_tx");
        node->on_timer(kBeaconJitterTimerId);             // deferred re-check: channel busy -> stand down
        CHECK(hal.count("beacon_skipped_busy") == 1);     // stage=post_jitter
        CHECK(hal.count("beacon_tx") == beacons_before);
        delete node;
    }
    // EMIT: still quiet at the deferred fire -> beacon goes out.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
        hal._now = 200000;                                // no RX -> since=inf -> still quiet
        const int beacons_before = hal.count("beacon_tx");
        node->on_timer(kBeaconJitterTimerId);
        CHECK(hal.count("beacon_tx") == beacons_before + 1);   // emitted
        delete node;
    }
}

TEST_CASE("Cleanup #D — two periodic beacon defers in one jitter window BOTH fire (ring, not single-timer replace)") {
    // Pre-#D: the 2nd periodic defer's after(kBeaconJitterTimerId) REPLACED the 1st -> only 1 beacon fired where the
    // Lua's per-`after` closures fire both. Ring fix: each defer takes a free slot [27..30]. Draws are unchanged (each
    // periodic fire still draws its silence-jitter); only the lost-beacon edge is closed.
    TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/30000, /*max_idle=*/0);
    hal._now = 200000;                                        // no RX -> since_rx=inf -> quiet (both fires defer)
    hal._rand_ret = 5;                                        // jitter=5 (>0) so periodic_beacon_fire DEFERS, not emit-now
    const int b0 = hal.count("beacon_tx");
    node->on_timer(kBeaconTimerId);                          // periodic fire #1 -> defer ring slot 0
    node->on_timer(kBeaconTimerId);                          // periodic fire #2 -> defer ring slot 1 (NOT a replace)
    CHECK(hal.count("beacon_tx") == b0);                     // both deferred, nothing on air yet
    node->on_timer(kBeaconJitterTimerId + 0);               // slot 0 fires
    node->on_timer(kBeaconJitterTimerId + 1);               // slot 1 fires
    CHECK(hal.count("beacon_tx") == b0 + 2);                // BOTH deferred beacons emitted (pre-#D: only 1)
    delete node;
}

// The R4.2 #00 port: schedule_triggered_beacon draws a SECOND jitter (min-interval defer) ONLY in
// steady_state (now >= boot_grace 120000). Under boot grace it NEVER draws the 2nd -> every <120s gate
// stays byte-identical. (This is what lifts the R4.2 ">120s draw-for-draw" guard.)
TEST_CASE("R4.3 triggered-beacon min-interval — 2nd draw ONLY in steady_state (>120000ms)") {
    // (steady) past boot grace + a recent beacon within min_interval -> 2 draws (jitter + min-interval defer).
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/0, /*max_idle=*/0);   // quiet=0: clean beacon
        hal._now = 190000; node->on_timer(kBeaconTimerId);    // emit -> _last_beacon_tx_ms=190000, discovery exits
        hal._now = 200000;                                    // 200000 - boot(0) >= 120000 -> steady
        const int rb = hal.rand_calls;
        node->schedule_triggered_beacon();
        CHECK(hal.rand_calls - rb == 2);                      // trigger jitter + min-interval 2nd draw
        CHECK(hal.count("beacon_trigger_deferred") == 1);
        delete node;
    }
    // (under boot grace) now < 120000 -> steady_state false -> 1 draw, NO defer.
    {
        TestHal hal; Node* node = mk_throttle_node(hal, /*quiet=*/0, /*max_idle=*/0);
        hal._now = 50000; node->on_timer(kBeaconTimerId);     // emit at 50000 (discovery exits on timeout)
        hal._now = 100000;                                    // 100000 - 0 < 120000 -> NOT steady
        const int rb = hal.rand_calls;
        node->schedule_triggered_beacon();
        CHECK(hal.rand_calls - rb == 1);                      // jitter only, NO min-interval draw
        CHECK(hal.count("beacon_trigger_deferred") == 0);
        delete node;
    }
}

// review #00: the max-idle witness (_last_rx_bcn_ms) must be set AFTER the leaf guard, so a FOREIGN-leaf
// beacon does NOT count as a routing-refresh — else the B+C skip_clean (and hence the silence-jitter draw)
// desyncs from the Lua on multi-leaf channels. The routing-SF witness (channel-busy) IS set for all frames.
TEST_CASE("R4.3 max-idle witness ignores a foreign-leaf beacon (set after the leaf guard)") {
    TestHal hal; Node node(hal, /*id=*/1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.quiet_threshold_ms = 30000; cfg.beacon_max_idle_ms = 30000;   // small max_idle -> override eligible
    node.on_init(cfg);
    hal._now = 100000;                                        // past max_idle, no prior beacon (since_tx = inf)
    // a well-formed beacon stamped leaf_id=1 (FOREIGN): rejected at the leaf guard -> must NOT set _last_rx_bcn_ms.
    std::array<uint8_t,64> bb{};
    beacon_entry e{}; e.dest = 200; e.next = 201; e.score_bucket = 12; e.is_gateway = false; e.hops = 2;
    beacon_in bin{}; bin.leaf_id = 1; bin.src = 9; bin.key_hash32 = 0x1234;
    bin.entries = std::span<const beacon_entry>(&e, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta m9{12.0f,-70.0f,0,9}; node.on_recv(bb.data(), bn, m9);
    CHECK(hal.count("beacon_rx") == 0);                       // foreign leaf rejected (returns before the bcn witness)
    // periodic fire: the max-idle B+C sees since_bcn_rx = inf (the foreign beacon was NOT counted) + dirty_n=0
    // -> NOT skip_clean -> force_idle. (With the bug, the foreign beacon would set since_bcn_rx=0 -> skip_clean.)
    node.on_timer(kBeaconTimerId);
    CHECK(hal.count("beacon_max_idle_force") == 1);
    CHECK(hal.count("beacon_max_idle_skip_clean") == 0);
}

// ---- R4.5 — LBT (listen-before-talk) pre-checks (THE rand-order golden) ----
static Node* mk_lbt_node(TestHal& hal, bool lbt_enabled, std::vector<std::array<uint8_t,3>> vias) {
    Node* node = new Node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.lbt_enabled = lbt_enabled; cfg.nav_enabled = false;   // LBT rand-order tests isolate from NAV (its origination jitter draws a rand)
    node->on_init(cfg);
    std::array<uint8_t,64> bb{};
    for (auto& v : vias) {
        RxMeta m{12.0f,-70.0f,0,static_cast<int8_t>(v[0])};
        const size_t n = mk_beacon_route(v[0], 5, 9, v[1], v[2], bb);
        node->on_recv(bb.data(), n, m);
    }
    return node;
}

TEST_CASE("R4.5 rand-order golden — LBT draws ONLY when enabled + channel busy; the deferred re-fire draws nothing") {
    // (a) lbt_enabled=false (every gate): RTS goes straight out even on a busy channel — NO LBT draw.
    {
        TestHal hal; Node* node = mk_lbt_node(hal, /*lbt_enabled=*/false, {{2,1,14}});
        hal._now = 1000; hal._channel_busy_until = 99999;       // busy, but LBT disabled
        const int rb = hal.rand_calls;
        send_cmd(*node, 5, "hi");
        CHECK(hal.rand_calls - rb == 0);                        // disabled -> NO LBT draw
        CHECK(hal.last_tx("RTS") != nullptr);                   // RTS went straight to radio
        delete node;
    }
    // (b) lbt_enabled=true but channel IDLE: no defer, NO draw.
    {
        TestHal hal; Node* node = mk_lbt_node(hal, /*lbt_enabled=*/true, {{2,1,14}});
        hal._now = 1000; hal._channel_busy_until = 0;
        const int rb = hal.rand_calls;
        send_cmd(*node, 5, "hi");
        CHECK(hal.rand_calls - rb == 0);                        // idle -> NO LBT draw
        CHECK(hal.last_tx("RTS") != nullptr);
        delete node;
    }
    // (c) lbt_enabled=true + channel BUSY: ONE LBT draw + tx_lbt_defer, the RTS HELD; the deferred re-fire
    //     sends it with NO further draw (the __lbt_done once-guard).
    {
        TestHal hal; Node* node = mk_lbt_node(hal, /*lbt_enabled=*/true, {{2,1,14}});
        hal._now = 1000; hal._channel_busy_until = 5000;        // busy until 5000
        const int rb = hal.rand_calls;
        send_cmd(*node, 5, "hi");
        CHECK(hal.rand_calls - rb == 1);                        // ONE LBT backoff draw
        CHECK(hal.count("tx_lbt_defer") == 1);
        CHECK(hal.last_tx("RTS") == nullptr);                   // HELD — not on radio yet
        const int rb2 = hal.rand_calls;
        hal._now = 5000; hal._channel_busy_until = 0;           // channel cleared
        node->on_timer(kLbtDeferTimerId);
        CHECK(hal.rand_calls - rb2 == 0);                       // deferred re-fire: NO further draw
        CHECK(hal.last_tx("RTS") != nullptr);                   // now on radio
        delete node;
    }
}

TEST_CASE("R4.5 tx_flood — a beacon is DROPPED when the channel is busy longer than flood_lbt_max_defer_ms") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.lbt_enabled = true; cfg.quiet_threshold_ms = 0;        // quiet=0 fast path -> emit_beacon -> tx_flood
    node.on_init(cfg);
    hal._now = 1000; hal._channel_busy_until = 10000000;       // busy WAY past flood_lbt_max_defer
    const int rb = hal.rand_calls;
    node.on_timer(kBeaconTimerId);
    CHECK(hal.count("tx_flood_skipped") == 1);                 // page dropped
    CHECK(hal.last_tx("BCN") == nullptr);                      // nothing on radio
    CHECK(hal.rand_calls - rb == 1);                           // only the periodic re-arm draw (NO LBT defer draw)
}

// review #00/#02/#03: TWO concurrent LBT defers must BOTH fire — a single stash would clobber the first
// (drop it + desync the rand stream). The ring gives each defer its own slot + timer (Lua per-closure semantics).
TEST_CASE("R4.5 LBT ring — two concurrent deferred NACKs BOTH reach the radio (no single-stash clobber)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; cfg.lbt_enabled = true;
    node.on_init(cfg);
    std::array<uint8_t,16> rb{};
    hal._now = 1000; hal._channel_busy_until = 0;
    // make the node BUSY receiving (pending_rx) so further RTSes get BUSY_RX NACKs.
    { const size_t rn = mk_rts(2,1,9,5,10,rb); RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)}; node.on_recv(rb.data(), rn, m2); }
    CHECK(hal.count("cts_tx") >= 1);
    // busy channel + two more RTSes from DIFFERENT senders -> two BUSY_RX NACKs, both LBT-deferred (slots 0,1).
    hal._channel_busy_until = 5000;
    { const size_t rn = mk_rts(3,1,8,6,10,rb); RxMeta m3{8.0f,-80.0f,0,static_cast<int8_t>(3)}; node.on_recv(rb.data(), rn, m3); }
    { const size_t rn = mk_rts(4,1,8,7,10,rb); RxMeta m4{8.0f,-80.0f,0,static_cast<int8_t>(4)}; node.on_recv(rb.data(), rn, m4); }
    CHECK(hal.count("tx_lbt_defer") == 2);                     // both deferred (distinct slots, NOT clobbered)
    CHECK(hal.count("tx_lbt_defer_dropped") == 0);
    // fire both slot timers -> BOTH NACKs go to the radio (single stash would send only one).
    hal._now = 5000; hal._channel_busy_until = 0;
    node.on_timer(kLbtDeferTimerId + 0);
    node.on_timer(kLbtDeferTimerId + 1);
    int nack_tx = 0; for (const auto& f : hal.tx_frames) if (f.label == "NACK") ++nack_tx;
    CHECK(nack_tx == 2);
}

// ---- R4.5b — on_radio_busy stash retry (the busy-channel response retry) ----
TEST_CASE("R4.5b on_radio_busy — a blocked DATA clears awaiting_ack + retries from the stash (3x, one draw each) then gives up") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                                  // RTS
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                     // DATA tx -> awaiting_ack + DATA stashed
    CHECK(hal.last_tx("DATA") != nullptr);
    // a blocked DATA (the sim's safety-net couldn't send it): on_radio_busy(tag=DATA=2).
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    const int rb = hal.rand_calls;
    node->on_radio_busy(bi);                                  // retry #1 (retries 3->2)
    CHECK(hal.count("data_tx_blocked") == 1);
    CHECK(hal.rand_calls - rb == 1);                          // ONE retry-jitter draw
    node->on_timer(kRadioBusyRetryTimerId + 1);              // slot 1 = DATA -> re-issue
    int data_n = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data_n;
    CHECK(data_n == 2);                                       // original + the retry
    // exhaust the remaining retries -> giveup on the 4th block.
    node->on_radio_busy(bi);                                  // retry #2 (2->1)
    node->on_radio_busy(bi);                                  // retry #3 (1->0)
    const int rb2 = hal.rand_calls;
    node->on_radio_busy(bi);                                  // retries_left==0 -> giveup, NO draw
    CHECK(hal.count("tx_giveup") == 1);
    CHECK(hal.rand_calls - rb2 == 0);
    delete node;
}

TEST_CASE("R4.5b on_radio_busy — a DATA retry re-arms the ACK wait (port divergence: Lua on_handed re-arms, ours must too)") {
    // Regression guard: Lua's DATA on_handed (dv:10270-10278) sets awaiting_ack + start_ack_timeout, and the stash
    // retry re-fires on_handed. OUR retry_stashed re-sends the bytes only; without the explicit DATA re-arm the
    // re-sent DATA flies but the sender stays !awaiting_ack with no ack-timeout -> the returning ACK is dropped +
    // the flight never completes. Assert a matching ACK is ACCEPTED after the retry (only possible if re-armed).
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                                  // RTS
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                     // DATA tx -> awaiting_ack + DATA stashed
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi);                                  // clears awaiting_ack + cancels ack-timeout
    node->on_timer(kRadioBusyRetryTimerId + 1);              // retry: re-tx DATA + RE-ARM awaiting_ack + ack-timeout
    std::array<uint8_t,8> ab{}; const size_t an = mk_ack(1, 1, ab);
    RxMeta m3{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(ab.data(), an, m3);
    CHECK(hal.count("ack_rx") == 1);                          // re-armed -> the ACK completes the flight
    delete node;
}

TEST_CASE("§T2 retry_stashed — reports the synchronous HAL refusal with the stashed frame identity") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                         // initial DATA admitted + stashed
    BusyInfo bi{BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi);                                      // arm DATA stash retry
    hal.events.clear();
    const size_t frames_before = hal.tx_frames.size();
    const int calls_before = hal.tx_calls;
    hal.tx_answer = TxResult::busy;
    node->on_timer(kRadioBusyRetryTimerId + 1);                    // retry_stashed -> synchronous refusal
    CHECK(hal.tx_calls == calls_before + 1);                       // premise: HAL was called
    CHECK(hal.tx_frames.size() == frames_before);                  // refusal retained no frame
    CHECK(hal.count("tx_hal_rejected") == 1);
    const Ev* rejected = hal.last("tx_hal_rejected"); CHECK(rejected != nullptr);
    if (rejected) {
        CHECK(rejected->label == "DATA");
        CHECK(rejected->result == static_cast<int>(TxResult::busy));
    }
    CHECK(hal.count("tx_failed") == 0);                            // synchronous refusal is not a queued outcome
    CHECK(hal.count("tx_unknown") == 0);
    CHECK(hal.count("tx_aired") == 0);
    delete node;
}

// ---- §T1/T2 (N7) — `on_tx_complete` is THE entry; `on_radio_busy` is a thin refused adapter onto it ----
// T1 pins the refused adapter to the direct entry. T2 gives aired/failed/unknown real DeviceHal producers and reports
// them through telemetry, while deliberately preserving the T1 protocol boundary: no retry-state, timer, or stash
// mutation and no app/UI push (the latter is T3).
// ★★★ The negative half remains load-bearing: routing an `aired` outcome into the refusal body would emit
// `radio_busy`, clear `awaiting_ack`, cancel the ACK timeout, and consume a stash retry for a frame that flew.
// The named completion events below are evidence only; they must not change that state boundary.
TEST_CASE("§T1/T2 on_tx_complete — refused adapter is identical; aired/failed/unknown report telemetry only") {
    // Bring a node to a live DATA flight: awaiting_ack set, the DATA stashed in slot 1.
    auto to_live_data = [](TestHal& hal, Node& node) {
        send_cmd(node, 5, "hi");                                   // RTS
        std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
        RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node.on_recv(cb.data(), cn, m2);
        node.on_timer(kCtsToDataGapTimerId);                       // DATA tx -> awaiting_ack + DATA stashed
        hal.events.clear(); hal.armed.clear();
    };
    auto armed_in_retry_range = [](const TestHal& hal) {           // kRadioBusyRetryTimerId..+3 (the 4 stash slots)
        int n = 0; for (const auto& a : hal.armed)
            if (a.second >= kRadioBusyRetryTimerId && a.second <= kRadioBusyRetryTimerId + 3) ++n;
        return n;
    };

    // ---- ★ POSITIVE: the two entries are the same entry. Arm A goes through `on_radio_busy(BusyInfo)`, arm B
    //      calls `on_tx_complete` with the `refused` outcome the adapter builds. Same events, same draw, same timer.
    std::vector<std::string> seq_a, seq_b;
    int draws_a = 0, draws_b = 0, armed_a = 0, armed_b = 0;
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        to_live_data(hal, *node);
        const int rb = hal.rand_calls;
        BusyInfo bi{BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
        node->on_radio_busy(bi);
        for (const auto& e : hal.events) seq_a.push_back(e.type);
        draws_a = hal.rand_calls - rb; armed_a = armed_in_retry_range(hal);
        delete node;
    }
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        to_live_data(hal, *node);
        const int rb = hal.rand_calls;
        node->on_tx_complete(TxOutcome{ TxOutcomeKind::refused, BusyReason::channel_busy, TxResult::ok,
                                        /*tag=DATA*/2, /*seq=*/0u, /*sf=*/7, /*busy_until_ms=*/0 });
        for (const auto& e : hal.events) seq_b.push_back(e.type);
        draws_b = hal.rand_calls - rb; armed_b = armed_in_retry_range(hal);
        delete node;
    }
    CHECK(seq_a.size() >= 2);                                      // PREMISE: the arm is REACHED (a silent pair would compare equal)
    CHECK(seq_a == seq_b);                                         // ★★ the same body ran, in the same order
    CHECK(draws_a == 1);                                           // PREMISE: the retry jitter draw is what a refusal costs…
    CHECK(draws_b == draws_a);                                     // ★ …and the direct entry costs exactly the same
    CHECK(armed_a == 1);                                           // PREMISE: one stash re-issue timer…
    CHECK(armed_b == armed_a);                                     // ★ …armed identically

    // ---- ★★★ COMPLETION REPORTS: exactly one named telemetry event each, but no refusal event, draw, timer, or
    //      stash retry consumption. DeviceHal produces these three kinds; T3 will add the separate app/UI consumer.
    {
        TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
        to_live_data(hal, *node);
        const int rb = hal.rand_calls;
        const TxOutcomeKind kinds[] = { TxOutcomeKind::aired, TxOutcomeKind::failed, TxOutcomeKind::unknown };
        for (TxOutcomeKind k : kinds)
            node->on_tx_complete(TxOutcome{ k, BusyReason::none, TxResult::radio_error,
                                            /*tag=DATA*/2, /*seq=*/7u, /*sf=*/7, /*busy_until_ms=*/0 });
        CHECK(hal.count("tx_aired") == 1);
        CHECK(hal.count("tx_failed") == 1);
        CHECK(hal.count("tx_unknown") == 1);
        CHECK(hal.count("radio_busy") == 0);                       // ★★ never the refusal/retry path
        const Ev* aired = hal.last("tx_aired"); CHECK(aired != nullptr);
        const Ev* failed = hal.last("tx_failed"); CHECK(failed != nullptr);
        const Ev* unknown = hal.last("tx_unknown"); CHECK(unknown != nullptr);
        if (aired) {
            CHECK(aired->tag == 2); CHECK(aired->seq == 7u); CHECK(aired->sf == 7);
        }
        if (failed) {
            CHECK(failed->tag == 2); CHECK(failed->seq == 7u); CHECK(failed->sf == 7);
            CHECK(failed->result == static_cast<int>(TxResult::radio_error));
        }
        if (unknown) {
            CHECK(unknown->tag == 2); CHECK(unknown->seq == 7u); CHECK(unknown->sf == 7);
        }
        CHECK(hal.rand_calls - rb == 0);                           // ★★ no backoff draw
        CHECK(armed_in_retry_range(hal) == 0);                     // ★★ no stash re-issue armed
        // ★★ AND THE STASH IS UNTOUCHED: `tx_defer_max_retries` is 3, so four REFUSALS give exactly one giveup —
        //    on the fourth. If any of the three above had consumed a retry, the giveup would arrive one call early.
        BusyInfo bi{BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
        node->on_radio_busy(bi); node->on_radio_busy(bi); node->on_radio_busy(bi);
        CHECK(hal.count("tx_giveup") == 0);                        // ★ three retries still available => no giveup yet
        node->on_radio_busy(bi);
        CHECK(hal.count("tx_giveup") == 1);                        // ★★ …and the fourth is the giveup, exactly as the sibling case above
        delete node;
    }
}

// ★★★ §T1/T2 — THE `TxParams` BUILDER'S IDENTITY ROUTING, MEASURED RATHER THAN ARGUED. DeviceHal is now the
// production reader: it carries the value through queue/in-flight/outcome. Native TestHal reads it solely for direct
// regression evidence at the hand-off boundary, so this test can distinguish every sending site's identity without
// simulating the hardware completion ring.
// ⛔⛔ WITHDRAWN CLAIM, KEPT VISIBLE (§T1 round 2). This header used to read: *"the last arm cannot distinguish
//   `TxStashSlot::flight_gen` from the CURRENT `PendingTx::flight_gen` … distinguishing the two sources needs a
//   flight REPLACED while a stash retry is armed — reachable only with an outcome consumer that does not exist yet
//   (§T2/§T3)."* ★★ **THAT SECOND SENTENCE WAS WRONG, AND WRONG IN THE DIRECTION THAT MATTERS: it declared a site
//   unmeasurable while the instrument that measures it was already in this file.** The consumer the test needs is
//   NOT the outcome path — it is `TestHal::tx`'s `f.seq = p.seq` capture at the hand-off, four hundred lines above.
// ⛔⛔ WITHDRAWN ROUND-2 REACHABILITY CLAIM: *"What is genuinely unreachable is only the STATE (stash on flight A,
//   pending flight on B), and that is what a white-box seam is for."* The state is reachable through public APIs:
//   queue A and B, busy-refuse A's DATA, then overhear A's exact downstream-forward RTS. The implicit ACK closes A
//   and `become_free()` installs B while A's already-armed stash timer remains fireable. The NEXT case drives that
//   sequence without a friend seam and reddens the current-flight mutation directly at the HAL capture.
// ⓘ The last arm below still measures only that `retry_stashed` supplies the identity of the frame it re-sends;
//   the SOURCE question is the next case's, and neither claims the other's coverage.
TEST_CASE("§T1 tx_params_of — every hand-off carries the identity ITS OWN SITE supplied, and 0 where the frame has none") {
    // ---- ★ A BEACON HAS NO FLIGHT ⇒ 0, and `tx_flood` passes it DELIBERATELY rather than borrowing one.
    {
        TestHal hal; Node node(hal, 1, 0xABCD);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
        cfg.quiet_threshold_ms = 0;                            // quiet fast path -> emit_beacon -> tx_flood
        node.on_init(cfg);
        hal._now = 1000; node.on_timer(kBeaconTimerId);
        const auto* b = hal.last_tx("BCN"); CHECK(b != nullptr);   // PREMISE: the beacon really reached the radio
        if (b) CHECK(b->seq == 0u);                                // ★★ …carrying no identity
    }

    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    auto cts_then_data = [&](uint8_t ctr_lo) {                 // CTS in -> the CTS->DATA gap fires -> DATA out
        std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
        RxMeta m{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m);
        node->on_timer(kCtsToDataGapTimerId);
        (void)ctr_lo;
    };

    // ---- ★★ FLIGHT 1. The RTS and the DATA are BOTH handed by `tx_with_retry`, and the flight is ALREADY LIVE when
    //      the RTS goes out — so the RTS carrying 0 is the `tag == FrameTag::data` guard doing its job, not an
    //      absence of state to borrow. ⛔ That is the whole point: a CTS/ACK/NACK/RTS must not inherit an identity.
    send_cmd(*node, 5, "hi");
    { const auto* r = hal.last_tx("RTS"); CHECK(r != nullptr);
      if (r) CHECK(r->seq == 0u); }                            // ★★ an RTS of a LIVE flight still carries 0
    cts_then_data(1);
    const auto* d1 = hal.last_tx("DATA"); CHECK(d1 != nullptr);
    const uint32_t seq1 = d1 ? d1->seq : 0u;
    CHECK(seq1 != 0u);                                         // ★★ the DATA carries the live flight's identity

    // ---- ★★ THE STASH RE-ISSUE carries the identity of the frame it RE-SENDS, not a fresh or zero one.
    BusyInfo bi{BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi);                                   // arms the slot-1 re-issue
    node->on_timer(kRadioBusyRetryTimerId + 1);                // -> retry_stashed
    const auto* d1r = hal.last_tx("DATA"); CHECK(d1r != nullptr);
    CHECK(d1r != d1);                                          // PREMISE: a SECOND DATA really flew (else the next line is vacuous)
    if (d1r) CHECK(d1r->seq == seq1);                          // ★★ the re-issue is the SAME flight

    // ---- ★★★ A SECOND FLIGHT MUST CARRY A DIFFERENT IDENTITY. Without this the value could be any constant —
    //      this is what makes `seq1 != 0` mean "the live flight" rather than "some non-zero number".
    std::array<uint8_t,8> ab{}; const size_t an = mk_ack(1, 1, ab);
    RxMeta m3{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(ab.data(), an, m3);   // flight 1 completes
    CHECK(hal.count("ack_rx") == 1);                           // PREMISE: it really completed
    send_cmd(*node, 5, "again");
    cts_then_data(2);
    const auto* d2 = hal.last_tx("DATA"); CHECK(d2 != nullptr);
    if (d2) { CHECK(d2->seq != 0u);
              CHECK(d2->seq != seq1); }                        // ★★★ a NEW flight ⇒ a NEW identity (monotonic per flight)
    delete node;
}

// ★★★★ §T1 — THE STALE-STASH CASE. This is the one that separates `TxStashSlot::flight_gen` from the CURRENT
// `PendingTx::flight_gen`, i.e. the ONLY assertion in the tree that can catch the builder confirming the WRONG
// flight. ⛔ `retry_stashed` has NO PRE-TRANSMIT FLIGHT GUARD — unlike its sibling `duty_defer_fire`, it calls
// `_hal.tx()` unconditionally and only its POST-tx ACK re-arm is guarded — so a frame it re-sends may genuinely
// belong to a superseded flight. If the builder read the live flight there, that stale frame's completion would be
// attributed to the NEW flight and would FALSELY CONFIRM it.
// ★ THE STATE IS PUBLICLY REACHABLE. Queue A and B; busy-refuse A's DATA; then overhear A's exact downstream RTS.
//   `implicit_ack_from_forward` closes A and `become_free()` starts B, but does not cancel A's already-armed DATA
//   stash timer. That timer therefore re-sends A while B is current — the exact source-disagreement state.
// ⚠ BOTH VACUITY MODES ARE CHECKED EXPLICITLY BELOW: B must really become current, and the timer must really hand
//   A's byte-identical DATA to the HAL. Otherwise `seq == A` could pass without exercising `retry_stashed` against B.
TEST_CASE("§T1 retry_stashed — public A-to-B replacement carries the stale STASH identity, never live B's") {
    TestHal hal; Node* node = s4_sender(hal, /*dst=*/50, /*next=*/20);
    constexpr uint8_t data_slot = 1;                          // production retry-slot order: CTS, DATA, ACK, NACK

    // ---- Queue flights A and B. A starts; B must remain queued behind the live flight.
    hal._now = 1000; send_cmd(*node, /*dst=*/50, "a");
    const OurRts rts_a = our_last_rts(hal); CHECK(rts_a.got);
    CHECK(rts_a.ctr_lo == 1); CHECK(hal.count("rts_tx") == 1);
    hal._now = 1001; send_cmd(*node, /*dst=*/50, "b");
    CHECK(hal.count("tx_enqueue") == 2);
    CHECK(hal.count("rts_tx") == 1);                          // B is queued, not current yet

    // ---- Progress A through CTS to DATA and save the identity + exact bytes the HAL received.
    RxMeta m20{12.0f, -70.0f, 0, static_cast<int8_t>(20)};
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/20, /*data_sf=*/7, cb);
    hal._now = 1100; node->on_recv(cb.data(), cn, m20);
    node->on_timer(kCtsToDataGapTimerId);
    const auto* data_a = hal.last_tx("DATA"); CHECK(data_a != nullptr);
    const uint32_t gen_a = data_a ? data_a->seq : 0u;
    const std::vector<uint8_t> bytes_a = data_a ? data_a->bytes : std::vector<uint8_t>{};
    CHECK(gen_a != 0u); CHECK_FALSE(bytes_a.empty());
    CHECK(hal.count("data_tx") == 1);

    // ---- Refuse A's DATA asynchronously: the production path leaves its retry stash armed.
    BusyInfo bi{BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi);
    CHECK(hal.count("data_tx_blocked") == 1);
    int data_retry_arms = 0;
    for (const auto& a : hal.armed) if (a.second == kRadioBusyRetryTimerId + data_slot) ++data_retry_arms;
    CHECK(data_retry_arms == 1);                                // the timer fired below was genuinely armed for A

    // ---- Feed an exact downstream-forward RTS for A. The implicit ACK closes A and become_free starts B.
    std::array<uint8_t, 16> fb{};
    const size_t fn = mk_rts_for_frame(/*src=*/20, /*next=*/9, /*dst=*/50, rts_a.ctr_lo, rts_a.plen,
                                       fb, bytes_a.data(), bytes_a.size());
    CHECK(fn == 10);
    auto forwarded = parse_rts(std::span<const uint8_t>(fb.data(), fn)); CHECK(forwarded.has_value());
    if (forwarded) CHECK(rts_flight_identity_equal(forwarded->id, rts_a.id));
    hal._now = 5000; node->on_recv(fb.data(), fn, m20);          // past the own-DM burst floor, so B starts now
    CHECK(hal.count("implicit_ack_from_forward") == 1);
    CHECK(node->has_pending_tx());                               // A closed, and B is now the live flight
    CHECK(hal.count("rts_tx") == 2);
    const Ev* b_rts = hal.last("rts_tx"); CHECK(b_rts != nullptr);
    if (b_rts) CHECK(b_rts->ctr == 2);                           // ★ become_free installed/started queued B
    const OurRts rts_b = our_last_rts(hal); CHECK(rts_b.got);
    CHECK_FALSE(rts_flight_identity_equal(rts_b.id, rts_a.id)); // B is a distinct current flight on the wire
    const uint32_t gen_b = gen_a + 1u;                           // issue_send bumps once for the one newly-started B
    CHECK(gen_b != gen_a);

    // ---- Fire A's still-armed DATA retry while B is current. A's exact bytes + saved seq must reach the HAL.
    const size_t before = hal.tx_frames.size();
    const int data_before = hal.count("data_tx");
    node->on_timer(kRadioBusyRetryTimerId + data_slot);
    CHECK(hal.tx_frames.size() == before + 1);                   // ★ stale DATA really reached the HAL
    const auto* retried_a = hal.last_tx("DATA"); CHECK(retried_a != nullptr);
    if (retried_a) {
        CHECK(retried_a->bytes == bytes_a);                      // ★ A's stashed carrier, not B's RTS/current carrier
        CHECK(retried_a->seq == gen_a);                          // ★★★★ the identity saved with stale A
        CHECK(retried_a->seq != gen_b);                          // ★★★★ never B's current identity
    }
    CHECK(hal.count("data_tx") == data_before);                 // telemetry stays at A's one production DATA admit
    CHECK(node->has_pending_tx());                               // the stale retry did not replace current B

    // ---- ★★★★ §T3 (N15) — AND NOW THE CONSUMER SIDE OF THE SAME STATE, ASSERTED HERE RATHER THAN IN A FORKED
    //      FIXTURE (U1): this is the only place in the tree where a stale carrier is in flight while a DIFFERENT
    //      flight is current, which is exactly the state a false confirmation needs. Feed the completion the HAL
    //      would produce for A's re-sent bytes and the app must hear NOTHING — neither for A (its transaction is
    //      closed) nor, above all, for B (which never aired).
    { Push sink{}; while (node->next_push(sink)) {} }            // start from an empty ring
    node->on_tx_complete(TxOutcome{ TxOutcomeKind::aired, BusyReason::none, TxResult::ok,
                                    /*tag=DATA*/2, /*seq=*/gen_a, /*sf=*/7, /*busy_until_ms=*/0 });
    { int n = 0; Push sink{}; while (node->next_push(sink)) ++n;
      CHECK(n == 0); }                                          // ★★★★ the stale airing confirms NOTHING
    // ⚠ THE CONTROL THAT MAKES THAT ZERO MEAN SOMETHING: the SAME call with B's identity DOES push, so the zero
    //   above is the identity guard working, not an inert consumer.
    node->on_tx_complete(TxOutcome{ TxOutcomeKind::aired, BusyReason::none, TxResult::ok,
                                    /*tag=DATA*/2, /*seq=*/gen_b, /*sf=*/7, /*busy_until_ms=*/0 });
    { int n = 0; Push sink{}; PushKind k = PushKind::msg_recv;
      while (node->next_push(sink)) { ++n; k = sink.kind; }
      CHECK(n == 1); CHECK(k == PushKind::send_aired); }
    delete node;
}

// ==================================================================================================================
// §T3 — `PushKind::send_aired`: the CORE ownership rule, DM plane. (design §7.2 N8/N9/N10/N11/N12/N14e/N15)
// ==================================================================================================================
// ★★★ WHAT THESE MEASURE, AND WHY EACH ONE COULD COME OUT OTHERWISE. `aired` is the ONE attempt-level outcome allowed
// into the app push ring, because it can only ever be an UPGRADE. Everything below is about the guards that decide
// WHOSE upgrade it is: drop any one of them and a completion is attributed to a send this node never made, or to the
// wrong flight — the false confirmation [[B164]] is about, arriving from the other direction.
// ⓘ There is deliberately NO de-duplication anywhere in core: a repeated `aired` for the same live flight enqueues a
//   second push, and the CONSUMER's monotonic rank makes it idempotent. That is what keeps `sizeof(Node)` unmoved,
//   and it is asserted below rather than left as an assumption.
namespace {
// Drain the whole push ring; returns only the `send_aired` ones, so a case can assert BOTH "exactly one of mine" and
// "nothing else appeared".
struct PushDrain { int total = 0; std::vector<Push> aired; };
static PushDrain drain_pushes(Node& n) {
    PushDrain d; Push pu{};
    while (n.next_push(pu)) { ++d.total; if (pu.kind == PushKind::send_aired) d.aired.push_back(pu); }
    return d;
}
static TxOutcome aired_of(uint16_t tag, uint32_t seq) {
    return TxOutcome{ TxOutcomeKind::aired, BusyReason::none, TxResult::ok, tag, seq, /*sf=*/7, /*busy_until_ms=*/0 };
}
}  // namespace

TEST_CASE("§T3 send_aired (DM) — one push for the live LOCAL flight; zero for a stale seq or a non-DATA tag") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    const CmdResult sent = send_cmd(*node, /*dst=*/5, "hi");
    CHECK(sent.code == CmdCode::queued);
    CHECK(sent.ctr != 0);                                          // PREMISE: a real origination handle was minted
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                          // the DATA is handed to the HAL
    const auto* d = hal.last_tx("DATA"); CHECK(d != nullptr);
    const uint32_t gen = d ? d->seq : 0u;
    CHECK(gen != 0u);                                              // PREMISE: the hand-off carried this flight's identity
    (void)drain_pushes(*node);                                     // start from an empty ring

    // ---- ★★ N8: the completion for THIS flight, on a DATA tag ⇒ exactly ONE push, carrying the DM's own handle.
    node->on_tx_complete(aired_of(/*tag=DATA*/2, gen));
    PushDrain p = drain_pushes(*node);
    CHECK(p.total == 1);                                           // ⛔ nothing else was enqueued
    CHECK(p.aired.size() == 1);
    if (p.aired.size() == 1) {
        CHECK(p.aired[0].dst == 5);                                // ★ the peer, from the carrier — never rebuilt
        CHECK(p.aired[0].ctr == sent.ctr);                         // ★ the SAME handle `on_command` answered with
    }

    // ---- ★★ N9: a STALE identity confirms NOTHING. This is decision-3's tripwire: with a tag-only identity the
    //      completion of a superseded frame would be attributed to whatever flight is live now.
    node->on_tx_complete(aired_of(2, gen + 1000u));
    CHECK(drain_pushes(*node).total == 0);
    node->on_tx_complete(aired_of(2, 0u));                         // the "no identity" value beacons/RTS/CTS carry
    CHECK(drain_pushes(*node).total == 0);

    // ---- ★★ N10: every NON-DATA frame type is telemetry only, even carrying the live flight's identity.
    //      `frame_tag_of` masks §B186a's mobile-op high byte, so the high byte must not smuggle one through either.
    const uint16_t non_data[] = { /*beacon*/0, /*rts*/1, /*cts*/3, /*ack*/4, /*nack*/5 };
    for (uint16_t t : non_data) {
        node->on_tx_complete(aired_of(t, gen));
        CHECK(drain_pushes(*node).total == 0);
    }
    node->on_tx_complete(aired_of(/*mobile-op high byte + DATA*/0x0302u, gen));
    CHECK(drain_pushes(*node).aired.size() == 1);                  // ★ the masked low byte IS data ⇒ still owned

    // ---- ★ NO CORE DE-DUPLICATION, STATED AND MEASURED. Two attempts of one flight = two pushes; idempotence is
    //      the CONSUMER's rank, which is what lets `Node` carry no bit for this at all.
    node->on_tx_complete(aired_of(2, gen));
    node->on_tx_complete(aired_of(2, gen));
    CHECK(drain_pushes(*node).aired.size() == 2);
    delete node;
}

TEST_CASE("§T3 send_aired — N14e: a FORWARDED DM airs and pushes NOTHING (we made no send to report)") {
    TestHal hal; Node node(hal, 1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    std::array<uint8_t,64> bb{};                                   // a route to dst=5 via 7, so node 1 forwards
    const size_t bn = mk_beacon_route(/*src=*/7, /*dest=*/5, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
    RxMeta m7{12.0f,-70.0f,0,static_cast<int8_t>(7)}; node.on_recv(bb.data(), bn, m7);
    std::array<uint8_t,16> rb{}; std::array<uint8_t,64> db{};
    RxMeta m2{8.0f,-80.0f,0,static_cast<int8_t>(2)};
    hal._now = 1000; { const size_t rn = mk_rts(2,1,5,10,10,rb,0,/*origin=*/0,/*ctr=*/10); node.on_recv(rb.data(), rn, m2); }
    hal._now = 1100; { const size_t dn = mk_data(1,5,10,0,"x",db); node.on_recv(db.data(), dn, m2); }
    node.on_timer(kPostAckTimerId);                                // the ACK has aired -> the forward is issued
    CHECK(hal.count("rts_tx") >= 1);                               // PREMISE: node 1 really took the forward on
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/7, /*data_sf=*/7, cb);
    hal._now = 1200; node.on_recv(cb.data(), cn, m7);
    node.on_timer(kCtsToDataGapTimerId);
    const auto* d = hal.last_tx("DATA"); CHECK(d != nullptr);
    const uint32_t gen = d ? d->seq : 0u;
    CHECK(gen != 0u);                                              // PREMISE: a forwarded DATA really flew, WITH an identity
    (void)drain_pushes(node);
    // ★★ The completion is EXACT — same tag, same flight — and it must still produce nothing: `has_previous_hop`
    //    says this carrier is somebody else's message. Drop that clause and the companion receives a completion for
    //    a send this node never made.
    node.on_tx_complete(aired_of(2, gen));
    CHECK(drain_pushes(node).total == 0);
}

TEST_CASE("§T3 send_aired — N11/N12: a FAILED attempt pushes nothing; the retry that airs pushes exactly one") {
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    const CmdResult sent = send_cmd(*node, /*dst=*/5, "hi");
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);
    const auto* d = hal.last_tx("DATA"); CHECK(d != nullptr);
    const uint32_t gen = d ? d->seq : 0u;
    (void)drain_pushes(*node);
    hal.events.clear();

    // ---- ★★★ ATTEMPT 1 FAILS. It is REPORTED (telemetry) and it changes NOTHING the app can see. This is the
    //      false-negative mirror of [[B164]]: the reachable sequence is "attempt 1's start_transmit fails -> the MAC
    //      ack-timeout fires -> attempt 2 airs and the message is delivered", so a terminal panel state here would be
    //      exactly as wrong as today's premature SENT.
    node->on_tx_complete(TxOutcome{ TxOutcomeKind::failed, BusyReason::none, TxResult::radio_error,
                                    /*tag=DATA*/2, gen, /*sf=*/7, 0 });
    CHECK(hal.count("tx_failed") == 1);                            // ★ reported, not hidden
    CHECK(drain_pushes(*node).total == 0);                         // ⛔ and NOT pushed
    // ---- ★ N12's half that belongs here: `unknown` is the same — never a push, never `aired`.
    node->on_tx_complete(TxOutcome{ TxOutcomeKind::unknown, BusyReason::none, TxResult::ok, 2, gen, 7, 0 });
    CHECK(hal.count("tx_unknown") == 1);
    CHECK(drain_pushes(*node).total == 0);

    // ---- ★★★ THE RETRY AIRS. A later `aired` must never be suppressed by the earlier weaker attempts.
    node->on_tx_complete(aired_of(2, gen));
    PushDrain p = drain_pushes(*node);
    CHECK(p.aired.size() == 1);
    if (p.aired.size() == 1) { CHECK(p.aired[0].dst == 5); CHECK(p.aired[0].ctr == sent.ctr); }
    delete node;
}

// ---- shared-Lua-bug fixes (project_meshroute_shared_lua_bugs) ----
TEST_CASE("Shared-bug #1 — a DATA giveup releases the stranded flight so the TX queue drains") {
    // Pre-fix: on_radio_busy(DATA) cleared awaiting_ack + cancelled the ack-timeout; the exhausted-stash giveup then
    // returned with _pending_tx STILL set + no recovery timer, so become_free() was blocked behind a dead flight and
    // a queued 2nd message never sent. Fix: the DATA giveup resets pending_tx + become_free (mirror DATA-M dv:12151).
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "a");                                  // msg1 -> RTS
    send_cmd(*node, 5, "b");                                  // msg2 -> queued behind msg1
    int rts0 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts0;
    CHECK(rts0 == 1);                                         // only msg1 has RTSed
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)}; node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);                     // msg1 -> DATA tx (awaiting_ack + DATA stashed)
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=DATA*/2, /*sf=*/7, /*busy_until=*/0};
    node->on_radio_busy(bi); node->on_radio_busy(bi); node->on_radio_busy(bi);   // 3 retries (3->0)
    node->on_radio_busy(bi);                                  // retries exhausted -> giveup + release the flight
    CHECK(hal.count("tx_giveup") == 1);
    int rts1 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts1;
    CHECK(rts1 == 2);                                         // msg2 drained + RTSed (the queue was NOT stranded)
    const Ev* r2 = hal.last("rts_tx"); CHECK(r2 != nullptr);
    if (r2) CHECK(r2->ctr == 2);                              // the 2nd RTS is msg2
    delete node;
}

TEST_CASE("Shared-bug #2 — tx_with_retry duty pre-check defers an over-budget DATA, then re-issues when budget frees") {
    // Lua tx_with_retry (dv:3615-3635) duty-pre-checks + self-defers an over-budget frame; ours used to always
    // _hal.tx -> the sim's duty hard-block bounced it via on_radio_busy, consuming a stash retry per bounce. Fix:
    // the duty pre-check defers (no _hal.tx) + a timer re-runs tx_with_retry from the stash. Draw-free.
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000;   // budget 10000ms
    node.on_init(cfg);
    std::array<uint8_t,64> bb{}; RxMeta mb{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    const size_t bn = mk_beacon_route(/*src=*/2,/*dest=*/5,/*next=*/9,/*hops=*/1,/*score=*/14, bb);
    node.on_recv(bb.data(), bn, mb);                           // route to 5 via 2
    hal._airtime_used = 0;                                     // RTS/CTS fit
    send_cmd(node, 5, "hi");                                   // -> RTS (slot<0, NOT duty-pre-checked here)
    std::array<uint8_t,8> cb{}; const size_t cn = mk_cts(1, 2, 7, cb);
    RxMeta m2{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    hal._airtime_used = 9990;                                  // near budget -> the DATA (slot>=0) won't fit
    node.on_recv(cb.data(), cn, m2);
    const int rb = hal.rand_calls;
    node.on_timer(kCtsToDataGapTimerId);                      // do_data_tx -> tx_with_retry(DATA) -> OVER budget -> defer
    CHECK(hal.count("duty_cycle_blocked") == 1);
    int data0 = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data0;
    CHECK(data0 == 0);                                        // DATA NOT handed to the radio (deferred)
    CHECK(hal.rand_calls - rb == 0);                          // the duty defer is DRAW-FREE
    // review #1/#9: a duty-deferred DATA must NOT arm awaiting_ack (the Lua clears it, dv:10281-10283). If it did, the
    // short ack-timeout would fire before the long duty wait + draw a rand + re-RTS. Prove the ACK wait is disarmed by
    // firing a stray ack-timeout: it must no-op (zero draws, no re-RTS) — the bug the original test masked.
    int rtsBefore = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rtsBefore;
    const int rb2 = hal.rand_calls;
    node.on_timer(kAckTimeoutTimerId);                        // would draw + re-RTS if awaiting_ack were wrongly armed
    CHECK(hal.rand_calls - rb2 == 0);                         // NO spurious draw -> awaiting_ack was false on the defer
    int rtsAfter = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rtsAfter;
    CHECK(rtsAfter == rtsBefore);                             // NO spurious re-RTS from a premature ack-timeout
    hal._airtime_used = 0;                                     // budget frees
    node.on_timer(kDutyDeferTimerId + 1);                     // duty_defer_fire(DATA slot) -> tx_with_retry -> now fits
    int data1 = 0; for (const auto& f : hal.tx_frames) if (f.label == "DATA") ++data1;
    CHECK(data1 == 1);                                        // DATA re-issued after the budget freed
    // the re-issue RE-ARMS the ACK wait (anchored to the real send), so a matching ACK now completes the flight
    std::array<uint8_t,8> ab{}; const size_t an = mk_ack(1, 1, ab);
    node.on_recv(ab.data(), an, m2);
    CHECK(hal.count("ack_rx") == 1);                          // re-armed on the re-hand -> ACK accepted
}

TEST_CASE("Cleanup #A (redo) — over-budget RTS duty-deferred in the dedicated slot (flight_gen-safe), re-checks, then hands when budget frees") {
    // The #2 duty pre-check is slot>=0 only; #A duty-checks the RTS in lbt_complete + defers it in a DEDICATED slot
    // (not the shared LBT ring — that reuse was net-worse, review wgvbtirmu), flight_gen-keyed so the long wait is
    // safe. Draw-free; gate-inert. (start_rts_timeout is armed on the hand — the deliberate drift; not separately
    // assertable with the no-op TestHal::after.)
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000; cfg.nav_enabled = false;   // budget 10000ms; duty-rand test isolates from NAV
    node.on_init(cfg);
    std::array<uint8_t,64> bb{}; RxMeta mb{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    const size_t bn = mk_beacon_route(2,5,9,1,14,bb); node.on_recv(bb.data(),bn,mb);
    hal._airtime_used = 9990;                                   // near budget -> the RTS won't fit
    const int rb = hal.rand_calls;
    send_cmd(node, 5, "hi");                                    // RTS -> lbt_complete -> OVER budget -> dedicated duty defer
    int rts = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts;
    CHECK(rts == 0);                                            // NOT handed (deferred, not sim-bounced)
    CHECK(hal.count("duty_cycle_blocked") >= 1);
    CHECK(hal.rand_calls - rb == 0);                            // the defer is DRAW-FREE
    node.on_timer(kRtsDutyDeferTimerId);                       // STILL over budget -> re-defer (re-check), not handed
    rts = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts;
    CHECK(rts == 0);                                           // re-deferred, still off air
    hal._airtime_used = 0;                                      // budget frees
    node.on_timer(kRtsDutyDeferTimerId);                      // now fits -> hand the RTS (+ arm the CTS-wait)
    rts = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts;
    CHECK(rts == 1);                                           // handed once the budget freed
    CHECK(hal.rand_calls - rb == 0);                           // the whole defer/re-defer/hand path is DRAW-FREE
}

TEST_CASE("§T2 rts_duty_defer_fire — reports synchronous HAL refusal and retains the existing CTS-wait arm") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.duty_cycle = 0.10; cfg.duty_cycle_window_ms = 100000; cfg.nav_enabled = false;
    node.on_init(cfg);
    std::array<uint8_t,64> bb{}; RxMeta mb{12.0f,-70.0f,0,static_cast<int8_t>(2)};
    const size_t bn = mk_beacon_route(2,5,9,1,14,bb); node.on_recv(bb.data(),bn,mb);
    hal._airtime_used = 9990;
    send_cmd(node, 5, "hi");                                      // RTS is duty-deferred, never handed
    int timeout_arms_before = 0;
    for (const auto& a : hal.armed) if (a.second == kRtsTimeoutTimerId) ++timeout_arms_before;
    const size_t frames_before = hal.tx_frames.size();
    const int calls_before = hal.tx_calls;
    hal.events.clear();
    hal._airtime_used = 0;
    hal.tx_answer = TxResult::busy;
    node.on_timer(kRtsDutyDeferTimerId);                           // deferred RTS reaches HAL and is refused
    CHECK(hal.tx_calls == calls_before + 1);                       // premise: HAL was called
    CHECK(hal.tx_frames.size() == frames_before);                  // refusal retained no frame
    CHECK(hal.count("tx_hal_rejected") == 1);
    const Ev* rejected = hal.last("tx_hal_rejected"); CHECK(rejected != nullptr);
    if (rejected) {
        CHECK(rejected->label == "RTS");
        CHECK(rejected->result == static_cast<int>(TxResult::busy));
    }
    int timeout_arms_after = 0;
    for (const auto& a : hal.armed) if (a.second == kRtsTimeoutTimerId) ++timeout_arms_after;
    CHECK(timeout_arms_before == 0);                               // duty defer had not started the CTS wait
    CHECK(timeout_arms_after == 1);                                // existing recovery behavior is unchanged
    CHECK(hal.count("tx_failed") == 0);                            // synchronous refusal is not a queued outcome
    CHECK(hal.count("tx_unknown") == 0);
    CHECK(hal.count("tx_aired") == 0);
}

// Cleanup #B (pick_next_cascade_hop now refresh_route_order-s first, dv:5434) is exercised by the r5_cascade
// differential gate (which drives pick_next_cascade_hop through cascade_to_alt) and reuses the sort_candidates +
// resort_routes_for_neighbor_penalty machinery covered by the R4.2 demotion tests above ("route demotion — marking a
// CRITICAL primary reranks it below the viable alt"). A standalone catch-up-flip unit test is impractical: the
// snr/advertised bucketing collapses controlled candidate scores into one bucket (equal -> a stable re-sort can't
// flip back) or drops the weak candidate below the viability floor. (Review #12, LOW — covered, not separately tested.)

TEST_CASE("R4.5b on_radio_busy — a blocked RTS re-RTSes via the already-armed rts_timeout (port divergence fix)") {
    // Regression guard: Lua dv:12091 clears awaiting_cts on a blocked RTS, but its rts_timeout_fire ignores
    // awaiting_cts (captures ctr_lo) and retries. OUR rts_timeout_fire uses awaiting_cts as the staleness key, so
    // on_radio_busy(RTS) must NOT clear it — else the armed timeout bails and the blocked RTS is stranded forever.
    TestHal hal; Node* node = mk_sender_with_routes(hal, {{2,1,14}});
    send_cmd(*node, 5, "hi");                                  // RTS -> awaiting_cts + rts_timeout armed
    int rts0 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts0;
    CHECK(rts0 == 1);
    meshroute::BusyInfo bi{meshroute::BusyReason::channel_busy, /*tag=RTS*/0, /*sf=*/7, /*busy_until=*/0};
    const int rb = hal.rand_calls;
    node->on_radio_busy(bi);
    CHECK(hal.count("rts_tx_blocked") == 1);
    CHECK(hal.count("tx_giveup") == 0);                       // RTS is NOT stash-retried
    CHECK(hal.rand_calls - rb == 0);                          // on_radio_busy itself takes no retry draw for an RTS
    // the armed rts_timeout must still fire + drive a re-RTS (proves awaiting_cts was left intact).
    node->on_timer(kRtsTimeoutTimerId);                       // -> rts_timeout_fire -> retries_left-- + backoff draw
    node->on_timer(kRetryBackoffTimerId);                     // -> re-RTS
    int rts1 = 0; for (const auto& f : hal.tx_frames) if (f.label == "RTS") ++rts1;
    CHECK(rts1 == 2);                                         // the blocked RTS was actually retried
    delete node;
}

// ===== F route discovery (RREQ/RREP) — node_route_discovery.cpp =====
// F floods TX with the beacon tag, so the tests identify RREQ/RREP by parse_f + is_reply.

TEST_CASE("F RREQ addressed to us -> reverse path + RREP toward the forwarder") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = false;
    in.dst_id = 5; in.ttl_or_next_hop = 4; in.hops = 2; in.relay = 3;   // origin 10 seeks us(5); forwarder 3
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    CHECK(n == 9);   // R6.1: F is 7 + config_hash u16
    hal._now = 1000; node.on_recv(buf, n, meta);

    bool saw_rev = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 10) saw_rev = true;
    CHECK(saw_rev);                                          // reverse route to the origin installed

    bool saw_rrep = false; f_out rrep{};
    for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size()));
        if (p && p->is_reply) { saw_rrep = true; rrep = *p; }
    }
    CHECK(saw_rrep);
    if (saw_rrep) {
        CHECK(rrep.origin == 10); CHECK(rrep.dst_id == 5);
        CHECK(rrep.ttl_or_next_hop == 3);                    // unicast back to the immediate forwarder
        CHECK(rrep.relay == 5);                              // we stamped ourselves as the relay
    }
}

TEST_CASE("F RREQ relayed (no route) -> reverse path + ttl-decremented rebroadcast + flood dedup") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = false;
    in.dst_id = 20; in.ttl_or_next_hop = 4; in.hops = 1; in.relay = 3;
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 1000; node.on_recv(buf, n, meta);
    fire_rreq_forwards(node);                                // §F-XL-2: release the jittered (stashed) rreq_forward

    bool saw_rev = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 10) saw_rev = true;
    CHECK(saw_rev);

    auto count_rreq = [&]() { int c = 0; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };
    CHECK(count_rreq() == 1);                                // one rebroadcast
    f_out fwd{}; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) fwd = *p; }
    CHECK(fwd.origin == 10); CHECK(fwd.dst_id == 20);
    CHECK(fwd.ttl_or_next_hop == 3);                         // ttl 4 -> 3
    CHECK(fwd.hops == 2);                                    // hops 1 -> 2
    CHECK(fwd.relay == 5);                                   // we are the new forwarder

    hal._now = 2000; node.on_recv(buf, n, meta);             // SAME (origin,dst) again
    CHECK(count_rreq() == 1);                                // deduped: no second rebroadcast
}

// M4 (2026-07-04 wave-3): the RREQ hops byte is unauthenticated wire. learn_route_via stores hops = f.hops+1,
// so a forged f.hops==255 wraps (uint8) to a 0-hop reverse route that OUT-RANKS every real route AND re-seeds
// on each re-flood = network-wide poison from ONE crafted frame. The top-of-branch dv_hop_cap gate must drop it
// (no reverse route learned, no rebroadcast), while a below-cap RREQ still learns normally.
TEST_CASE("M4 — RREQ with hops==255 does NOT create a 0-hop poison route (dv_hop_cap gate); below-cap still learns") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto find_rt = [&](uint8_t dest) -> const RtEntry* {
        for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == dest) return &node.rt_at(i);
        return nullptr;
    };
    auto count_rreq = [&]() { int c = 0; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };

    // (1) FORGED RREQ: origin 10, hops == 255 (wraps to 0 on +1 if unguarded).
    f_in bad{}; bad.leaf_id = 0; bad.origin = 10; bad.is_reply = false;
    bad.dst_id = 20; bad.ttl_or_next_hop = 4; bad.hops = 255; bad.relay = 3;
    uint8_t bbuf[16]; const size_t bn = pack_f(bad, std::span<uint8_t>(bbuf, sizeof(bbuf)));
    hal._now = 1000; node.on_recv(bbuf, bn, meta);
    CHECK(hal.count("rreq_drop_hop_cap") == 1);             // gated at the top of the RREQ branch
    CHECK(find_rt(10) == nullptr);                          // NO reverse route learned (would have been 0-hop poison)
    CHECK(count_rreq() == 0);                               // NOT rebroadcast

    // (2) a legitimate below-cap RREQ (origin 11, hops 1) still learns a sane reverse route + rebroadcasts.
    f_in ok{}; ok.leaf_id = 0; ok.origin = 11; ok.is_reply = false;
    ok.dst_id = 20; ok.ttl_or_next_hop = 4; ok.hops = 1; ok.relay = 3;
    uint8_t obuf[16]; const size_t on = pack_f(ok, std::span<uint8_t>(obuf, sizeof(obuf)));
    hal._now = 2000; node.on_recv(obuf, on, meta);
    fire_rreq_forwards(node);                               // §F-XL-2: release the jittered (stashed) rreq_forward
    const RtEntry* e11 = find_rt(11);
    CHECK(e11 != nullptr);
    if (e11) { CHECK(e11->n >= 1); CHECK(e11->candidates[0].hops == 2); }   // f.hops(1)+1 = a sane 2, not 0
    CHECK(count_rreq() == 1);
}

// M6 (2026-07-04 wave-3): nav_duration_rts feeds the unauthenticated RTS payload_len byte into an airtime calc at
// max SF. A forged payload_len=255 must NOT arm NAV any longer than the real hard cap — otherwise a cheap overheard
// RTS silences a victim's TX for seconds. The clamp lives inside nav_duration_rts, so 255 == max_payload_bytes_hard_cap.
TEST_CASE("M6 — nav_duration_rts clamps payload_len (255 == max_payload_bytes_hard_cap, no max-SF blowup)") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);   // max SF12
    node.on_init(cfg);
    const uint8_t sf = 12;
    const uint8_t cr = node.active_cr();   // §rts-cr-overhear: the peer CR is a parameter now; this test is about the clamp, so hold it fixed
    const uint32_t d_cap = node.test_nav_duration_rts(sf, protocol::max_payload_bytes_hard_cap, cr);
    const uint32_t d_255 = node.test_nav_duration_rts(sf, 255, cr);
    CHECK(d_255 == d_cap);                                  // the forged 255 is clamped to the hard cap
    // A below-cap value is still honoured (the clamp only caps, never floors) -> smaller than the capped duration.
    const uint32_t d_small = node.test_nav_duration_rts(sf, 32, cr);
    CHECK(d_small < d_cap);
    // ★ §rts-cr-overhear: the clamp must bind at EVERY advertised CR — the forged-255 attack must not reopen
    // just because the attacker also advertises cr8 (the clamp runs before the airtime call, so it does not).
    for (uint8_t peer_cr = 5; peer_cr <= 8; ++peer_cr)
        CHECK(node.test_nav_duration_rts(sf, 255, peer_cr) == node.test_nav_duration_rts(sf, protocol::max_payload_bytes_hard_cap, peer_cr));
    // ★ §cts-len6-cr2: the CTS twin's bound MOVED — deliberately, and to a stronger one. It used to clamp
    // payload_len to max_payload_bytes_hard_cap (241), which was BOTH unnecessary (byte 3's 6-bit len6 cannot
    // express more than 63*4 = 252 anyway) and UNSAFE at the top (payload_len is inner+MAC, so it legally
    // reaches 249 under CRYPTED — clamping to 241 under-reserved a real 255-B frame, the one direction NAV
    // must never fail in). It now clamps the BYTE COUNT to lora_max_frame_bytes, so the worst case a forged
    // hint can buy is exactly what an ABSENT hint already buys — the M6 DoS ceiling is unchanged, provably.
    for (uint8_t peer_cr = 5; peer_cr <= 8; ++peer_cr) {
        CHECK(node.test_nav_duration_cts(sf, 255, peer_cr) == node.test_nav_duration_cts(sf, 0, peer_cr));
        CHECK(node.test_nav_duration_cts(sf, 32, peer_cr)   <  node.test_nav_duration_cts(sf, 0, peer_cr));
    }
}

// R6.1 §6.4: the membership gate must cover F (route-discovery is the bypass around the beacon gate). A divergent-config
// F is dropped + NOT relayed (1-hop flood containment); a matching one is processed normally.
TEST_CASE("R6.1 F-gate — a divergent-config F is dropped + NOT relayed; matching is processed") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    const uint16_t my_hash  = meshroute::leaf_config_hash(cfg.allowed_sf_bitmap, 0, 1250, 10000, 3000, nullptr, 0);   // NodeConfig anti-spam defaults
    const uint16_t diverge  = meshroute::leaf_config_hash((1u << 7), 0, 1250, 10000, 3000, nullptr, 0);   // bitmap {7} != {12} -> different hash
    CHECK(diverge != my_hash); CHECK(diverge != 0);

    auto count_rreq = [&]() { int c = 0; for (const auto& tf : hal.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };
    auto feed_rreq = [&](uint16_t ch, uint8_t origin) {
        f_in in{}; in.leaf_id = 0; in.origin = origin; in.is_reply = false;
        in.dst_id = 20; in.ttl_or_next_hop = 4; in.hops = 1; in.relay = 3; in.config_hash = ch;
        uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
        node.on_recv(buf, n, meta);
    };
    // (1) DIVERGENT F -> gate drops it: no reverse path, NO rebroadcast, conflict event.
    hal._now = 1000; feed_rreq(diverge, /*origin=*/10);
    bool rev10 = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 10) rev10 = true;
    CHECK_FALSE(rev10);
    CHECK(count_rreq() == 0);                                // contained to 1 hop (not relayed)
    CHECK(hal.count("leaf_config_conflict") >= 1);
    // (2) MATCHING F -> processed: reverse path + rebroadcast.
    hal._now = 2000; feed_rreq(my_hash, /*origin=*/11);
    fire_rreq_forwards(node);                                // §F-XL-2: release the jittered (stashed) rreq_forward
    bool rev11 = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 11) rev11 = true;
    CHECK(rev11);
    CHECK(count_rreq() == 1);
}

// §F-XL-2 (2026-07-19): a PROPAGATING RREQ forward (forwarded ttl > 0) is de-stormed — stashed + fired after a small
// delay in [rreq_forward_jitter_min_ms, max_ms] (the ring id band [85..88]) — NOT re-tx'd at the receive instant, so
// sibling relays that heard the same flood don't collide at the identical ms. The fired frame is byte-identical to the
// immediate tx (same pack_f bytes). A TERMINAL forward (forwarded ttl == 0) is NOT de-stormed (nothing re-propagates
// past it) -> it tx's immediately. The team plane shares the same ring + gate.
TEST_CASE("§F-XL-2 — a propagating RREQ forward is jittered (in-window) + byte-identical; a terminal forward is immediate") {
    auto count_rreq = [](TestHal& h){ int c = 0; for (const auto& tf : h.tx_frames) {
        auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) ++c; } return c; };

    // (1) PROPAGATING forward (rx ttl=4 -> fwd ttl=3): stashed, not immediate.
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = false;
    in.dst_id = 20; in.ttl_or_next_hop = 4; in.hops = 1; in.relay = 3;
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 1000; node.on_recv(buf, n, meta);
    CHECK(count_rreq(hal) == 0);                             // NOT re-tx'd at the receive instant (de-stormed)
    uint32_t delay = 0; bool armed_ring = false;
    for (auto& [d, id] : hal.armed) if (id >= kRreqForwardTimerBase && id < kRreqForwardTimerBase + kRreqForwardSlots) { delay = d; armed_ring = true; }
    CHECK(armed_ring);
    CHECK(delay >= protocol::rreq_forward_jitter_min_ms);
    CHECK(delay <= protocol::rreq_forward_jitter_max_ms);
    fire_rreq_forwards(node);                                // release the stash
    CHECK(count_rreq(hal) == 1);                             // now on-air
    // byte-identity: the fired frame equals the immediate pack_f of the same forward
    f_in ref{}; ref.leaf_id = 0; ref.origin = 10; ref.is_reply = false;
    ref.dst_id = 20; ref.ttl_or_next_hop = 3; ref.hops = 2; ref.relay = 5;   // ttl 4->3, hops 1->2, relay=us
    uint8_t rbuf[16]; const size_t rn = pack_f(ref, std::span<uint8_t>(rbuf, sizeof(rbuf)));
    const TxFrame* fwd_tx = nullptr;
    for (const auto& tf : hal.tx_frames) { auto p = parse_f(std::span<const uint8_t>(tf.bytes.data(), tf.bytes.size())); if (p && !p->is_reply) fwd_tx = &tf; }
    CHECK(fwd_tx != nullptr);
    if (fwd_tx) {
        CHECK(fwd_tx->bytes.size() == rn);
        if (fwd_tx->bytes.size() == rn) { bool same = true; for (size_t i = 0; i < rn; ++i) if (fwd_tx->bytes[i] != rbuf[i]) same = false; CHECK(same); }
    }

    // (2) TERMINAL forward (rx ttl=1 -> fwd ttl=0): immediate, NO ring timer armed.
    TestHal hal2;
    Node node2(hal2, /*node_id=*/5, /*key_hash32=*/0xBEEF);
    node2.on_init(cfg);
    f_in t{}; t.leaf_id = 0; t.origin = 12; t.is_reply = false;
    t.dst_id = 22; t.ttl_or_next_hop = 1; t.hops = 1; t.relay = 3;
    uint8_t tbuf[16]; const size_t tn = pack_f(t, std::span<uint8_t>(tbuf, sizeof(tbuf)));
    hal2._now = 1000; node2.on_recv(tbuf, tn, meta);
    CHECK(count_rreq(hal2) == 1);                            // terminal forward tx'd IMMEDIATELY
    bool ring2 = false; for (auto& [d, id] : hal2.armed) { (void)d; if (id >= kRreqForwardTimerBase && id < kRreqForwardTimerBase + kRreqForwardSlots) ring2 = true; }
    CHECK_FALSE(ring2);                                      // no de-storm timer for a terminal forward
}

TEST_CASE("F RREP addressed to the origin -> forward path + rrep_arrived") {
    TestHal hal;
    Node node(hal, /*node_id=*/10, /*key_hash32=*/0xABCD);  // we are the origin
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = true;
    in.dst_id = 20; in.ttl_or_next_hop = 10; in.hops = 2; in.relay = 7;   // addressed to us(10); forwarder 7 toward dst
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 1000; node.on_recv(buf, n, meta);

    bool saw_fwd = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 20) saw_fwd = true;
    CHECK(saw_fwd);                                          // forward route to dst installed
    CHECK(hal.count("rrep_arrived") == 1);
}

TEST_CASE("F RREP relayed (not the origin) -> forward path + RREP onward along reverse path") {
    TestHal hal;
    Node node(hal, /*node_id=*/5, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RxMeta meta{8.0f, -80.0f, 0, -1};

    // Lay a reverse route to origin 10 (via 3) first, so the RREP can route onward.
    { f_in q{}; q.leaf_id = 0; q.origin = 10; q.is_reply = false; q.dst_id = 99;
      q.ttl_or_next_hop = 4; q.hops = 1; q.relay = 3;
      uint8_t qb[16]; const size_t qn = pack_f(q, std::span<uint8_t>(qb, sizeof(qb)));
      hal._now = 1000; node.on_recv(qb, qn, meta); }
    const size_t tx_before = hal.tx_frames.size();

    f_in in{}; in.leaf_id = 0; in.origin = 10; in.is_reply = true;
    in.dst_id = 20; in.ttl_or_next_hop = 5; in.hops = 2; in.relay = 7;    // addressed to us(5)
    uint8_t buf[16]; const size_t n = pack_f(in, std::span<uint8_t>(buf, sizeof(buf)));
    hal._now = 2000; node.on_recv(buf, n, meta);

    bool saw_fwd = false; for (const auto& e : hal.events) if (e.type == "rt_update" && e.dst == 20) saw_fwd = true;
    CHECK(saw_fwd);
    bool saw_onward = false; f_out rr{};
    for (size_t i = tx_before; i < hal.tx_frames.size(); ++i) {
        auto p = parse_f(std::span<const uint8_t>(hal.tx_frames[i].bytes.data(), hal.tx_frames[i].bytes.size()));
        if (p && p->is_reply) { saw_onward = true; rr = *p; }
    }
    CHECK(saw_onward);
    if (saw_onward) {
        CHECK(rr.origin == 10); CHECK(rr.dst_id == 20);
        CHECK(rr.ttl_or_next_hop == 3);                      // toward origin via the reverse next-hop (3)
        CHECK(rr.relay == 5);
    }
}

// ===== E2E ACK (send_e2e) — do_post_ack hooks =====

TEST_CASE("E2E ACK — destination of an E2E_ACK_REQ DATA replies with an ack to the origin") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, /*plen=*/15, rb, 0, /*origin=*/0, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);

    std::array<uint8_t, 64> db{};
    const uint8_t body[2] = { 'h', 'i' };
    const size_t dn = mk_data_e2e(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/0,
                                  DATA_FLAG_E2E_ACK_REQ, body, 2, db);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);                          // deliver -> E2E ack reply
    CHECK(hal.count("delivered")  == 1);                     // the DM still delivers to the app
    CHECK(hal.count("e2e_ack_tx") == 1);                     // and we send an end-to-end ack to the origin
    CHECK(hal.count("tx_enqueue") == 0);                     // the ack is NOT an app DM (dm_delivery honesty)
}

TEST_CASE("E2E ACK — origin of an E2E_IS_ACK DATA confirms (no app delivery)") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCD);   // we are the origin being acked
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/4, rb, 0, /*origin=*/2, /*ctr=*/0x0009);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);

    std::array<uint8_t, 64> db{};
    const uint8_t acked[2] = { 5, 0 };                       // acked ctr = 5 (LE)
    const size_t dn = mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/2,
                                  /*flags=*/0, acked, 2, db, /*type=*/DATA_TYPE_E2E_ACK);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    const Ev* ack = hal.last("e2e_ack_rx");
    CHECK(ack != nullptr);
    if (ack) { CHECK(ack->from == 2); CHECK(ack->ctr == 5); }  // confirmed the DM we originated (ctr=5)
    CHECK(hal.count("delivered") == 0);                      // an E2E ack is NOT delivered as a message
}

// E2E-ack DURABLE RECEIPT (2026-06-23): the origin RECORDS the ack as a DM-store receipt (type=E2E_ACK, no body) AND
// emits a live send_e2e_acked push -> harness/companion can confirm "the dest got it" (was telemetry-only = invisible on metal).
TEST_CASE("E2E ACK — origin records a durable receipt + a send_e2e_acked push (not telemetry-only)") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCD);   // we are the origin being acked
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RamInboxStore dm(protocol::inbox_dm_store_bytes), ch(protocol::inbox_chan_store_bytes);
    node.inbox().on_init(&dm, &ch);                          // a backend installs durable stores (else record_ack is inert)
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t, 16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/4, rb, 0, /*origin=*/2, /*ctr=*/0x0009), meta);
    std::array<uint8_t, 64> db{};
    const uint8_t acked[2] = { 5, 0 };                       // acked ctr = 5 (LE)
    hal._now = 2000; node.on_recv(db.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/2,
                                  /*flags=*/0, acked, 2, db, /*type=*/DATA_TYPE_E2E_ACK), meta);
    node.on_timer(kPostAckTimerId);

    // 1) a live send_e2e_acked push: dst = the acker (2), ctr = the acked ctr (5)
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::send_e2e_acked) { got = true; break; } }
    CHECK(got);
    if (got) { CHECK(pu.dst == 2); CHECK(pu.ctr == 5); }

    // 2) a durable DM-store receipt (type=E2E_ACK, no body), NOT an app delivery
    CHECK(dm.count() == 1);
    CHECK(ch.count() == 0);                                  // a receipt does not touch the channel store
    struct Got { bool seen; InboxKind kind; uint8_t origin; uint32_t msg_id; uint8_t type; uint8_t blen; }
        g{ false, InboxKind::channel, 0, 0, 0, 99 };
    node.inbox().pull(0, 0, [](void* c, const InboxEntry& e) -> bool {
        auto* x = static_cast<Got*>(c);
        x->seen = true; x->kind = e.kind; x->origin = e.origin; x->msg_id = e.msg_id; x->type = e.type; x->blen = e.body_len;
        return true;
    }, &g);
    CHECK(g.seen);
    CHECK(g.kind == InboxKind::dm);
    CHECK(g.type == DATA_TYPE_E2E_ACK);                      // a receipt, distinguished by the type byte
    CHECK(g.origin == 2);                                    // the dest that confirmed delivery
    CHECK(g.msg_id == 5);                                    // = the acked ctr
    CHECK(g.blen == 0);                                      // no body
    CHECK(hal.count("delivered") == 0);                      // still NOT delivered as a message
}

// ===== OTA remote diagnostics (rcmd) — DATA_TYPE_REMOTE_CMD/RESP staging =====
TEST_CASE("rcmd: a REMOTE_CMD DM STAGES into the inbound slot (not inbox/delivered); take drains it; a 2nd-while-pending drops") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCDu);   // we are the target of the command
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };

    std::array<uint8_t,16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/15, rb, 0, /*origin=*/2, /*ctr=*/0x0009), meta);
    std::array<uint8_t,64> db{};
    const uint8_t body[6] = { 's','t','a','t','u','s' };
    hal._now = 2000; node.on_recv(db.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/2,
                                  /*flags=*/0, body, 6, db, /*type=*/DATA_TYPE_REMOTE_CMD), meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 0);                      // a remote cmd is NOT an app delivery

    // a 2nd REMOTE_CMD WHILE one is pending -> dropped (the slot keeps the first; rcmd is human-paced)
    std::array<uint8_t,16> rb2{};
    hal._now = 3000; node.on_recv(rb2.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/10, /*plen=*/15, rb2, 0, /*origin=*/3, /*ctr=*/0x000A), meta);
    std::array<uint8_t,64> db2{};
    const uint8_t body2[4] = { 'd','u','t','y' };
    hal._now = 4000; node.on_recv(db2.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x000A, /*origin=*/3,
                                  /*flags=*/0, body2, 4, db2, /*type=*/DATA_TYPE_REMOTE_CMD), meta);
    node.on_timer(kPostAckTimerId);

    Node::RemoteInbound ri;
    CHECK(node.take_remote_inbound(ri));                     // drains the FIRST (the 2nd was dropped)
    CHECK(ri.is_response == false);
    CHECK(ri.from == 2);
    CHECK(ri.len == 6);
    CHECK(std::string(reinterpret_cast<const char*>(ri.body), ri.len) == "status");
    CHECK_FALSE(node.take_remote_inbound(ri));              // slot cleared after the drain
}

TEST_CASE("rcmd: a REMOTE_RESP DM stages as is_response=true; send_remote_cmd/response return the sent ctr") {
    TestHal hal; Node node(hal, /*id=*/0, /*key=*/0xABCDu);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t,16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/0, /*dst=*/0, /*ctr_lo=*/9, /*plen=*/15, rb, 0, /*origin=*/7, /*ctr=*/0x0009), meta);
    std::array<uint8_t,64> db{};
    const uint8_t body[8] = { 'u','p','=','4','2','s',' ',' ' };
    hal._now = 2000; node.on_recv(db.data(), mk_data_e2e(/*next=*/0, /*dst=*/0, /*ctr=*/0x0009, /*origin=*/7,
                                  /*flags=*/0, body, 8, db, /*type=*/DATA_TYPE_REMOTE_RESP), meta);
    node.on_timer(kPostAckTimerId);
    Node::RemoteInbound ri;
    CHECK(node.take_remote_inbound(ri));
    CHECK(ri.is_response == true);                          // a RESPONSE, not a command
    CHECK(ri.from == 7);
    // send_* return the assigned ctr (origination ride; we just check they don't refuse the call)
    const uint8_t qb[4] = { 't','e','s','t' };
    (void)node.send_remote_cmd(5, qb, 4);
    (void)node.send_remote_response(5, qb, 4);
}

// ===================== L2c — DST_HASH verify-on-delivery + identity-preserving redirect =====================
// A DM addressed to our node_id but carrying a cleartext DST_HASH naming a DIFFERENT key was misdelivered
// by an id collision: do NOT deliver; FORWARD it (origin + ctr + flags + inner preserved — NOT re-sent) to
// the real owner of want_hash if resolvable, else flood a HARD H to learn it and drop. No renumber (§7.1).

TEST_CASE("L2c — DST_HASH matching our key delivers normally") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash(2, 2, 0x0005, /*origin=*/1, /*dst_hash=*/0xABCD, "hi", db);
    CHECK(dn > 0);
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    const Ev* dlv = hal.last("delivered");
    CHECK(dlv != nullptr);
    if (dlv) { CHECK(dlv->has_payload); CHECK(dlv->payload == "hi"); }   // body parsed past the 4-B hash prefix
    CHECK(hal.count("l2c_misdelivery") == 0);
}

TEST_CASE("L2c — DST_HASH mismatch, owner UNKNOWN: HARD H query, drop, no deliver, no renumber") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);       // would-be heal target; proves L2c does NOT renumber
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*dst_hash=*/0xFFFFFFFFu, "hi", db);   // unknown owner
    hal._now = 2000; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 0);                          // NOT delivered locally
    CHECK(hal.count("l2c_misdelivery") == 1);
    CHECK(hal.count("l2c_redirect_query") == 1);                 // HARD H flood to learn the owner
    CHECK(hal.count("h_tx") == 1);
    CHECK(hal.count("l2c_redirect_forward") == 0);               // nothing to forward to (unknown)
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);        // §7.1: L2c never renumbers
    CHECK(hal.count("join_deny_sent") == 0);
}

TEST_CASE("L2c — DST_HASH mismatch, owner KNOWN: FORWARD preserves origin + ctr (identity not corrupted)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    // A beacon from owner(3) binds 3 -> key_hash32 0x1234 (id_bind) AND installs a direct route to 3.
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/3, bb);
    RxMeta b3{ 8.0f, -80.0f, 0, static_cast<int8_t>(3) };
    hal._now = 500; node.on_recv(bb.data(), bn, b3);
    // A misdelivered DM (origin=1, ctr=0x0005) addressed to our id(2) but wanting key 0x1234 (owner 3).
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash(2, 2, /*ctr=*/0x0005, /*origin=*/1, /*dst_hash=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("delivered") == 0);                          // not for us
    CHECK(hal.count("l2c_misdelivery") == 1);
    const Ev* rf = hal.last("l2c_redirect_forward");
    CHECK(rf != nullptr);
    if (rf) { CHECK(rf->to == 3); CHECK(rf->ctr == 5); }         // forwarded toward owner 3, ORIGINAL ctr
    // Drive the forward leg (RTS->CTS->DATA) and prove the emitted DATA keeps origin=1 + ctr=5 (not re-sent).
    const Ev* rts = hal.last("rts_tx");
    CHECK(rts != nullptr);
    if (rts) CHECK(rts->next == 3);
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/2, /*tx_id=*/3, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b3);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            CHECK(d->ctr == 0x0005);                             // ORIGINAL ctr preserved (no new send_by_hash ctr)
            CHECK(d->dst == 3);                                  // re-targeted to the real owner
            auto inner = data_inner(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d);
            auto ui = parse_unicast_inner(inner, d->flags);
            CHECK(ui.has_value());
            if (ui) { CHECK(ui->origin == 1);                    // ORIGINAL sender preserved (NOT the redirector id 2)
                      CHECK(ui->has_dst_hash); CHECK(ui->dst_key_hash32 == 0x1234u); }
        }
    }
}

// A misdelivered CRYPTED DM (the dst_hash is cleartext, so the misdelivery branch fires BEFORE any open) must
// re-tx the originator's 8-B nonce-seed verbatim on the redirect leg — else the real owner computes the wrong
// nonce and the seal tag-fails (silent drop). Every other forward path carries the seed; the L2c redirect must too.
TEST_CASE("§1c L2c — a misdelivered CRYPTED DM redirect carries the nonce-seed (else the owner can't open the seal)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/3, bb);   // owner(3): id_bind 3->0x1234 + a route to 3
    RxMeta b3{ 8.0f, -80.0f, 0, static_cast<int8_t>(3) };
    hal._now = 500; node.on_recv(bb.data(), bn, b3);
    const uint8_t S[8] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88 };           // the originator's nonce-seed
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, /*ctr_lo=*/5, 20, rb, 0, -1, /*ctr=*/0x0005, S);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_crypted(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/1, /*dst_hash=*/0x1234u, S, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_misdelivery") == 1);                     // CRYPTED dst_hash names owner 3, not us(2) -> redirect
    const Ev* rts = hal.last("rts_tx"); CHECK(rts != nullptr); if (rts) CHECK(rts->next == 3);
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/2, /*tx_id=*/3, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b3);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            CHECK(d->crypted);
            auto sd = data_nonce_seed(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d);
            CHECK(sd.size() == 8);
            bool seed_ok = (sd.size() == 8); for (size_t i = 0; i < sd.size() && i < 8; ++i) seed_ok = seed_ok && (sd[i] == S[i]);
            CHECK(seed_ok);                                      // the originator's seed must survive the redirect (was zeroed -> RED)
        }
    }
}

// M3 (2026-07-04, crypto): a `sendhashx` (crypt=on) addressed to an UNRESOLVED hash PARKS, then flies when the
// binding arrives. Before the fix ParkedSend carried no crypt intent -> both drains called do_send() with the
// DEFAULT intent (resolves to _cfg.e2e_dm == false) -> the parked-then-drained DM went out CLEARTEXT, silently
// downgrading a confidential send (violating the node.h "never silently falls back to cleartext" invariant).
// The fix stamps p.crypt at park + threads it into do_send at BOTH drains. This drives the drain-on-answer path
// (drain_parked_sends) and asserts the emitted DATA frame is CRYPTED, not plaintext.
TEST_CASE("M3 — a PARKED crypt=on send flies CRYPTED when the binding arrives (no silent cleartext downgrade)") {
    TestHal hal;
    // node 1 with a route to dest 5 via next-hop 2 (the resolved id will be 5 -> uses this route).
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});           // via 2 (h1) to dest 5
    // The recipient B's crypto identity: its key_hash32 == ed_pub[:4]; we cache B's AUTHORITATIVE pubkey so the
    // seal can find it (else e2e_seal_inner fails no_pubkey -> refused, NEVER cleartext — the fail-loud contract).
    uint8_t seedA[32], seedB[32];
    for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 3); seedB[i] = uint8_t(200 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    node->set_crypto_identity(idA.x_secret, idA.ed_pub);           // node 1 can seal
    node->peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);   // B's pubkey cached
    hal.events.clear();

    // sendhashx: address B by its key_hash32 with crypt=on. The binding (idB.key_hash32 -> 5) is UNKNOWN -> PARK.
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_hash = idB.key_hash32; c.u.send.flags = 0; c.crypt = CryptIntent::on;
    const uint8_t body[] = { 's','e','c' };
    c.body = body; c.body_len = sizeof(body);
    const CmdResult r = node->on_command(c);
    CHECK(r.code == CmdCode::queued);
    CHECK(r.ctr == 0);                                             // parked (resolving), not sent yet
    CHECK(hal.count("send_parked_for_hash") == 1);

    // The owner's answer arrives: idB.key_hash32 -> node 5 (authoritative) -> drain_parked_sends -> do_send(5, .., p.crypt=on).
    std::array<uint8_t, 7> hbin{};
    hash_bind_inner hb{}; hb.target_layer = 0; hb.node_id = 5; hb.key_hash32 = idB.key_hash32; hb.authoritative = true;
    const size_t in = pack_hash_bind_inner(hb, std::span<uint8_t>(hbin.data(), hbin.size()));
    node->on_hash_bind_response(hbin.data(), static_cast<uint8_t>(in), hb.authoritative, /*team_plane=*/false);
    CHECK(hal.count("send_hash_resolved") == 1);                   // the parked DM drained + flew

    // Pump the flight: RTS went to next-hop 2; feed its CTS -> the CTS->DATA gap fires -> DATA on air.
    const Ev* rts = hal.last("rts_tx"); CHECK(rts != nullptr); if (rts) CHECK(rts->next == 2);
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    RxMeta m2{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    node->on_recv(cb.data(), cn, m2);
    node->on_timer(kCtsToDataGapTimerId);

    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) CHECK(d->crypted);                                 // ★ the DRAINED parked send is CRYPTED, not cleartext (M3)
    }
    delete node;
}

TEST_CASE("L2c — repeated misdeliveries for one hash collapse to ONE redirect action (anti-flood)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    for (uint8_t k = 0; k < 2; ++k) {                            // two misdeliveries, same want_hash, distinct ctr_lo
        const uint8_t ctr = static_cast<uint8_t>(5 + k);
        std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, ctr, 15, rb, 0, /*origin=*/1, /*ctr=*/ctr);
        hal._now = 1000 + 1000 * k; node.on_recv(rb.data(), rn, meta);
        std::array<uint8_t, 64> db{};
        const size_t dn = mk_data_dsthash(2, 2, ctr, 1, /*dst_hash=*/0xFFFFFFFFu, "hi", db);
        hal._now = 1500 + 1000 * k; node.on_recv(db.data(), dn, meta);
        node.on_timer(kPostAckTimerId);
    }
    CHECK(hal.count("delivered") == 0);
    CHECK(hal.count("l2c_misdelivery") == 2);                    // BOTH observed (telemetry every time)
    CHECK(hal.count("l2c_redirect_query") == 1);                 // but only ONE redirect action -> no flood storm
    CHECK(hal.count("l2c_redirect_parked") == 1);               // and only ONE parked entry (ring fills can't grow)
    CHECK(hal.count("l2c_redirect_suppressed") == 1);           // the 2nd copy is suppressed, not parked/flooded
    CHECK(hal.count("h_tx") == 1);
}

// --- confirmation-gated heal: the HARD-H resolution decides forward (stale binding) vs heal (real collision) ---

TEST_CASE("L2c — parked redirect resolves to a DIFFERENT id: forward, NEVER renumber (stale binding)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);       // joined: a heal WOULD renumber — it must not
    // Route to 7 via neighbour 4 (this beacon does NOT bind 0x1234), so a later forward to 7 can issue.
    std::array<uint8_t, 64> rbe{}; const size_t rben = mk_beacon_route(/*src=*/4, /*dest=*/7, /*next=*/4, /*hops=*/2, /*score=*/10, rbe);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) }; hal._now = 500; node.on_recv(rbe.data(), rben, m4);
    // Misdeliver a DM wanting key 0x1234 (unknown) -> park + HARD-H.
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, /*origin=*/1, /*dst_hash=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    CHECK(hal.count("l2c_redirect_forward") == 0);               // nothing forwarded yet (parked, awaiting resolution)
    // HARD-H answer: 0x1234 is at id 7 (the recipient MOVED; not our id) -> forward, no collision.
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2, 0, /*origin=*/0, /*ctr=*/0x0006);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/7, /*hb_key=*/0x1234, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_forward") == 1);               // forwarded to the moved recipient
    const Ev* rf = hal.last("l2c_redirect_forward");
    if (rf) { CHECK(rf->to == 7); CHECK(rf->ctr == 5); }         // ORIGINAL ctr preserved
    CHECK(hal.count("l2c_collision_confirmed") == 0);            // NOT a same-id collision
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);        // joined, but NEVER renumbered (no spurious churn)
}

TEST_CASE("L2c — parked redirect resolves to OUR id: CONFIRMED collision, we WIN -> DENY (no renumber)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0x0000ABCDu);  // LOW key -> we keep + DENY
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0xFFFF0000u, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    // HARD-H answer: 0xFFFF0000 is at id 2 (OUR id) -> proven same-id collision.
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2, 0, /*origin=*/0, /*ctr=*/0x0006);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0xFFFF0000u, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("addr_conflict_self_defended") == 1);        // the answer did NOT clobber our self-binding
    CHECK(hal.count("l2c_collision_confirmed") == 1);
    CHECK(hal.count("join_deny_sent") == 1);                     // we keep -> DENY the squatter
    if (const Ev* d = hal.last("join_deny_sent")) CHECK(d->reason == 4);   // MEDIATED
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);
}

TEST_CASE("L2c — parked redirect resolves to OUR id, we LOSE (joined): forced_rejoin (yield the id)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xFFFFFFFFu);  // HIGH key -> we yield
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);       // forced_rejoin only fires when DAD-joined
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x00000001u, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2, 0, /*origin=*/0, /*ctr=*/0x0006);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0x00000001u, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_collision_confirmed") == 1);
    CHECK(hal.count("addr_conflict_forced_rejoin") == 1);        // we are the squatter -> yield our id
    CHECK(hal.count("join_deny_sent") == 0);
}

// --- additional L2c coverage (hop-budget edge, slot-reuse, send-side stamping, age-out, beacon re-drain) ---

// DST_HASH DATA with an explicit hops_remaining (to drive the destination-exhausted edge).
static size_t mk_data_dsthash_hops(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin,
                                   uint32_t dst_hash, uint8_t hops_remaining, const char* body,
                                   std::array<uint8_t, 64>& b) {
    std::array<uint8_t, 40> inner{};
    inner[0] = static_cast<uint8_t>(dst_hash);        inner[1] = static_cast<uint8_t>(dst_hash >> 8);
    inner[2] = static_cast<uint8_t>(dst_hash >> 16);  inner[3] = static_cast<uint8_t>(dst_hash >> 24);
    inner[4] = origin;
    uint8_t bl = 0; while (body[bl]) { inner[5 + bl] = static_cast<uint8_t>(body[bl]); ++bl; }
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = DATA_FLAG_DST_HASH; in.next = next; in.dst = dst;
    in.hops_remaining = hops_remaining; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), 5 + bl);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
static CmdResult send_hash_cmd(Node& node, uint32_t dst_hash, const char* body) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 0; c.u.send.dst_hash = dst_hash; c.u.send.flags = 0;
    c.body = reinterpret_cast<const uint8_t*>(body);
    c.body_len = static_cast<uint8_t>(std::strlen(body));
    return node.on_command(c);
}

TEST_CASE("L2c — redirect re-budgets the hop count from rt (no destination-exhaustion underflow)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // Beacon from owner(3) -> authoritative bind 3->0x1234 + a DIRECT route to 3 (rt_hops=1).
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/3, bb);
    RxMeta b3{ 8.0f, -80.0f, 0, static_cast<int8_t>(3) }; hal._now = 500; node.on_recv(bb.data(), bn, b3);
    // A misdelivered DM that arrived AT us EXHAUSTED (hops_remaining==0; the dst is exempt from the NACK).
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{};
    const size_t dn = mk_data_dsthash_hops(2, 2, 0x0005, 1, /*dst_hash=*/0x1234, /*hops_remaining=*/0, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);                              // owner known -> immediate forward to 3
    CHECK(hal.count("l2c_redirect_forward") == 1);
    // Drive the forward leg and confirm the emitted DATA's hop budget is rt-derived (1 + slack 3 = 4), NOT the
    // 255->31 saturation a naive inherited budget would produce.
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/2, /*tx_id=*/3, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b3);
    node.on_timer(kCtsToDataGapTimerId);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) { CHECK(d->hops_remaining != 31);                // NOT the underflow saturation
                 CHECK(d->hops_remaining <= 5);                 // rt_hops(1)+slack(3) == 4
                 CHECK(d->ctr == 0x0005); }                     // identity still preserved
    }
}

TEST_CASE("L2c — park_send into a recycled redirect slot is NOT mis-drained as a redirect (slot reset)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    // (1) Misdeliver for 0xAAAA (unknown) -> parks a REDIRECT at slot 0 (is_redirect=true).
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0xAAAAu, "hi", db);
    hal._now = 1100; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    // (2) Age it out (no resolution) -> slot 0 vacated by in-place compaction, leaving stale is_redirect=true.
    // (P-BUDGET: the parked age-out is now hash_locate_giveup_ms, decoupled from send_defer_ttl_ms.)
    hal._now = 1100 + protocol::hash_locate_giveup_ms + 1; node.on_timer(2 /*kAgingTimerId*/);
    CHECK(hal.count("send_hash_giveup") == 1);                  // the redirect is gone; node is idle (no flight)
    // (3) A PLAIN send-by-hash for 0xBBBB (unknown) -> park_send REUSES slot 0; the reset must clear is_redirect.
    hal._now += 5000; send_hash_cmd(node, /*dst_hash=*/0xBBBBu, "yo");
    CHECK(hal.count("send_parked_for_hash") == 1);
    // (4) Resolve 0xBBBB -> id 8. It MUST drain via the plain (do_send) path, NOT the stale redirect branch.
    std::array<uint8_t, 16> rb3{}; size_t rn3 = mk_rts(4, 2, 2, 7, 7, rb3, 0, /*origin=*/0, /*ctr=*/0x0007);
    hal._now += 1000; node.on_recv(rb3.data(), rn3, m4);
    std::array<uint8_t, 64> ab2{}; size_t an2 = mk_data_hashbind(2, 2, 0x0007, /*hb_node=*/8, /*hb_key=*/0xBBBBu, true, ab2);
    hal._now += 100; node.on_recv(ab2.data(), an2, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("send_hash_resolved") == 1);                // plain send-by-hash path (correct, post-reset)
    CHECK(hal.count("l2c_redirect_forward") == 0);             // the recycled slot did NOT re-trigger a redirect
    CHECK(hal.count("l2c_collision_confirmed") == 0);
}

TEST_CASE("L2c send-side — originator stamps DST_HASH from an authoritative id_bind") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    uint32_t hh = 0;
    CHECK_FALSE(node.key_hash_of_id(2, hh));                    // unknown -> no stamp
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/2, bb);   // authoritative bind 2->0x1234 + route
    RxMeta b2{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) }; hal._now = 1000; node.on_recv(bb.data(), bn, b2);
    CHECK(node.key_hash_of_id(2, hh)); CHECK(hh == 0x1234u);
    hal._now = 2000; send_cmd(node, /*dst=*/2, "hi");
    std::array<uint8_t, 8> cb{}; const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb);
    hal._now = 2100; node.on_recv(cb.data(), cn, b2);
    node.on_timer(kCtsToDataGapTimerId);
    CHECK(hal.count("data_tx") == 1);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            auto ui = parse_unicast_inner(data_inner(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d), d->flags);
            CHECK(ui.has_value());
            if (ui) { CHECK(ui->has_dst_hash); CHECK(ui->dst_key_hash32 == 0x1234u); CHECK(ui->origin == 1); }
        }
    }
}

TEST_CASE("L2c — cfg/NV-provisioned LOSER (not joined): collision confirmed but NO renumber (healed=false)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xFFFFFFFFu);  // HIGH key -> would lose; but NOT joined
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // (no restore_join_state -> _joined stays false: an operator-pinned id)
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x00000001u, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb2{}; const size_t rn2 = mk_rts(4, 2, 2, 6, 7, rb2, 0, /*origin=*/0, /*ctr=*/0x0006);
    hal._now = 3000; node.on_recv(rb2.data(), rn2, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0x00000001u, true, ab);
    hal._now = 3100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_collision_confirmed") == 1);
    CHECK(hal.count("addr_conflict_forced_rejoin") == 0);       // operator-pinned id is NOT auto-reassigned
    CHECK(hal.count("join_deny_sent") == 0);                    // we lost, so no keep-DENY either
    if (const Ev* cc = hal.last("l2c_collision_confirmed")) CHECK_FALSE(cc->healed);   // surfaced, not healed
}

TEST_CASE("L2c — parked redirect ages out (send_hash_giveup) when its HARD-H never resolves") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0xDEADBEEFu, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    hal._now = 2000 + protocol::hash_locate_giveup_ms + 1;      // past the parked age-out (P-BUDGET: hash_locate_giveup_ms, not send_defer_ttl_ms)
    node.on_timer(2 /*kAgingTimerId*/);
    CHECK(hal.count("send_hash_giveup") == 1);                  // the unresolved redirect is given up (not stranded)
    CHECK(hal.count("l2c_redirect_forward") == 0);
    CHECK(hal.count("l2c_collision_confirmed") == 0);
}

TEST_CASE("L2c — parked redirect forwards on the owner's BEACON re-drain (drain_resolved_parked_sends)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_parked") == 1);
    // Owner(9)'s beacon (key 0x1234) lands -> authoritative bind 9->0x1234 + route; drain_resolved forwards.
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/9, bb);   // mk_beacon key == 0x1234
    RxMeta b9{ 8.0f, -80.0f, 0, static_cast<int8_t>(9) }; hal._now = 2500; node.on_recv(bb.data(), bn, b9);
    CHECK(hal.count("l2c_redirect_forward") == 1);
    const Ev* rf = hal.last("l2c_redirect_forward");
    if (rf) { CHECK(rf->to == 9); CHECK(rf->ctr == 5); }        // forwarded to the owner, original ctr
    CHECK(hal.count("l2c_collision_confirmed") == 0);
}

// --- review re-fix regressions: forwarder drop-not-defer + plain send-by-hash resolving to self ---

TEST_CASE("L2c — a no-route redirect DROPS (forwarder semantics), it does NOT defer/send_failed") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_join_state(/*epoch=*/0, /*joined=*/true);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    // An H answer (via relay 4) binds 7->0x1234 AUTHORITATIVE but installs NO route to 7 (only to relay 4).
    std::array<uint8_t, 16> rba{}; const size_t rna = mk_rts(4, 2, 2, 9, 7, rba, 0, /*origin=*/0, /*ctr=*/0x0009);
    hal._now = 500; node.on_recv(rba.data(), rna, m4);
    std::array<uint8_t, 64> aba{}; const size_t ana = mk_data_hashbind(2, 2, 0x0009, /*hb_node=*/7, /*hb_key=*/0x1234, true, aba);
    hal._now = 600; node.on_recv(aba.data(), ana, m4);
    node.on_timer(kPostAckTimerId);
    // Misdeliver a DM wanting 0x1234: owner 7 is known authoritatively -> immediate forward, but no route to 7.
    RxMeta from1{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000; node.on_recv(rb.data(), rn, from1);
    std::array<uint8_t, 64> db{}; const size_t dn = mk_data_dsthash(2, 2, 0x0005, 1, /*want=*/0x1234, "hi", db);
    hal._now = 2000; node.on_recv(db.data(), dn, from1);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("l2c_redirect_forward") == 1);              // we tried to forward (enqueued)
    CHECK(hal.count("send_no_route") == 1);                     // ...and DROPPED it (forwarder, is_forward=true)
    CHECK(hal.count("send_deferred") == 0);                     // NOT the originator defer path
    CHECK(hal.count("send_deferred_giveup") == 0);             // and thus no send_failed-to-local-app for a transit DM
}

TEST_CASE("L2c/H — a plain send-by-hash that resolves to OUR OWN id gives up (no self-addressed do_send)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // Address an UNKNOWN hash 0xCAFE -> send_by_hash parks (plain) + floods a soft H.
    hal._now = 1000; send_hash_cmd(node, /*dst_hash=*/0xCAFEu, "yo");
    CHECK(hal.count("send_parked_for_hash") == 1);
    // An H answer claims 0xCAFE is at id 2 (OUR id) with a foreign key -> self-guard refuses the bind, and the
    // plain parked send must NOT do_send to ourselves.
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(4, 2, 2, 6, 7, rb, 0, /*origin=*/0, /*ctr=*/0x0006);
    hal._now = 2000; node.on_recv(rb.data(), rn, m4);
    std::array<uint8_t, 64> ab{}; const size_t an = mk_data_hashbind(2, 2, 0x0006, /*hb_node=*/2, /*hb_key=*/0xCAFEu, true, ab);
    hal._now = 2100; node.on_recv(ab.data(), an, m4);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("addr_conflict_self_defended") == 1);       // the foreign-key bind on our id was refused
    CHECK(hal.count("send_hash_giveup") == 1);                  // the plain send gave up (resolved to self)
    CHECK(hal.count("tx_enqueue") == 0);                        // NO self-addressed do_send
}

// ---- NAV (virtual carrier sense) -------------------------------------------------------------------
// Overheard unicast RTS/CTS reserve the medium (nav_enabled); own unsolicited TX defers until it clears;
// a new addressed RTS during a reservation is ignored. Channel/broadcast RTS + addressed RTS don't set it.

TEST_CASE("NAV — an overheard unicast RTS reserves the medium (only when nav_enabled)") {
    // nav OFF (default) -> overheard RTS sets nothing
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = false;   // explicit OFF (firmware default is now ON)
      node.on_init(cfg);
      std::array<uint8_t, 16> rb{};
      const size_t rn = mk_rts(/*src=*/1, /*next=*/3, /*dst=*/4, /*ctr_lo=*/5, /*plen=*/20, rb);  // next=3 != me(2) -> overheard
      hal._now = 1000; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(node.nav_until_ms() == 0); }
    // nav ON -> overheard unicast RTS reserves into the future
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
      node.on_init(cfg);
      std::array<uint8_t, 16> rb{};
      const size_t rn = mk_rts(1, 3, 4, 5, 20, rb);
      hal._now = 1000; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(node.nav_until_ms() > 1000); }
}

TEST_CASE("NAV — an RTS addressed to us, and a channel/broadcast RTS, do NOT set NAV") {
    TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
    node.on_init(cfg);
    // addressed to us (next=2) -> our exchange, not a reservation to honor
    std::array<uint8_t, 16> rb{};
    const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, 5, 20, rb);
    hal._now = 1000; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
    CHECK(node.nav_until_ms() == 0);
    // M_BROADCAST (channel) RTS -> a flood, no CTS to protect -> no NAV
    rts_in mb{}; mb.leaf_id = 0; mb.src = 1; mb.next = 3; mb.dst = 4; mb.ctr_lo = 6; mb.sf_index = 3;
    mb.rts_flags = RTS_FLAG_M_BROADCAST; mb.payload_len = 20; mb.m_payload_id_lo16 = 0xBEEF;
    std::array<uint8_t, 16> mbb{};
    const size_t mbn = pack_rts(mb, std::span<uint8_t>(mbb.data(), mbb.size()));
    CHECK(mbn > 0);
    hal._now = 1100; node.on_recv(mbb.data(), mbn, RxMeta{8.0f, -80.0f, 0, 1});
    CHECK(node.nav_until_ms() == 0);
}

TEST_CASE("NAV — an overheard CTS reserves DATA+ACK; the reservation scales with the data SF") {
    uint64_t d7 = 0, d12 = 0;
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
      node.on_init(cfg);
      std::array<uint8_t, 8> cb{};
      const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/3, /*data_sf=*/7, cb);   // rx_id=1 != me(2) -> overheard
      hal._now = 1000; node.on_recv(cb.data(), cn, RxMeta{8.0f, -80.0f, 0, 3});
      d7 = node.nav_until_ms() - 1000; }
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;
      node.on_init(cfg);
      std::array<uint8_t, 8> cb{};
      const size_t cn = mk_cts(1, 3, /*data_sf=*/12, cb);
      hal._now = 1000; node.on_recv(cb.data(), cn, RxMeta{8.0f, -80.0f, 0, 3});
      d12 = node.nav_until_ms() - 1000; }
    CHECK(d7 > 0);
    CHECK(d12 > d7);                                            // SF12 DATA reserves longer than SF7
}

TEST_CASE("NAV — own RTS for a queued DM defers while the medium is reserved, then flies once it clears") {
    TestHal hal; Node node(hal, /*id=*/1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.lbt_enabled = false; cfg.nav_enabled = true;           // isolate NAV as the only defer source
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/2, bb);   // direct route to bob(2)
    hal._now = 1000; node.on_recv(bb.data(), bn, RxMeta{8.0f, -80.0f, 0, 2});
    // reserve the medium (overheard RTS from 3 -> next 4)
    std::array<uint8_t, 16> ob{}; const size_t on = mk_rts(/*src=*/3, /*next=*/4, /*dst=*/5, 7, 20, ob);
    hal._now = 1500; node.on_recv(ob.data(), on, RxMeta{8.0f, -80.0f, 0, 3});
    CHECK(node.nav_until_ms() > 1500);
    // originate to bob -> the flight decides to RTS, but NAV defers the actual hand-off
    hal._now = 1600; send_cmd(node, /*dst=*/2, "hi");
    CHECK(hal.count("rts_tx") == 1);                            // decided to send
    CHECK(hal.last_tx("RTS") == nullptr);                       // ...but NAV deferred it (not handed to the radio)
    // NAV clears -> draining the LBT-defer slot hands the RTS
    hal._now = node.nav_until_ms() + 10; node.on_timer(kLbtDeferTimerId);
    CHECK(hal.last_tx("RTS") != nullptr);
}

TEST_CASE("NAV — addressed RTS during a reservation: dropped iff nav_ignore_rts, answered by default") {
    std::array<uint8_t, 16> ob{}; const size_t on = mk_rts(/*src=*/3, /*next=*/4, /*dst=*/5, 7, 20, ob);  // overheard -> arms NAV
    std::array<uint8_t, 16> rb{}; const size_t rn = mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, 9, 15, rb);  // addressed to us(2)
    // nav_ignore_rts = true (802.11 blanket-NAV): the addressed RTS is dropped under the reservation.
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true; cfg.nav_ignore_rts = true;
      node.on_init(cfg);
      hal._now = 1000; node.on_recv(ob.data(), on, RxMeta{8.0f, -80.0f, 0, 3});
      CHECK(node.nav_until_ms() > 1000);
      hal._now = 1100; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(hal.count("cts_tx") == 0); }                       // dropped under the reservation
    // DEFAULT (nav_ignore_rts = false, sim-tuned): the SAME RTS during a reservation is still ANSWERED.
    { TestHal hal; Node node(hal, /*id=*/2, 0xABCD);
      NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.nav_enabled = true;   // nav_ignore_rts defaults false
      node.on_init(cfg);
      hal._now = 1000; node.on_recv(ob.data(), on, RxMeta{8.0f, -80.0f, 0, 3});
      CHECK(node.nav_until_ms() > 1000);                       // reservation IS active
      hal._now = 1100; node.on_recv(rb.data(), rn, RxMeta{8.0f, -80.0f, 0, 1});
      CHECK(hal.count("cts_tx") == 1); }                       // ...yet the request is answered (defer, don't refuse)
}

TEST_CASE("NAV — a fresh own DM origination is jittered (nav_enabled), de-syncing simultaneous originators") {
    // nav ON -> the origination is held by the jitter (rand forced > 0), then flies once it elapses
    TestHal hal; Node node(hal, /*id=*/1, 0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.lbt_enabled = false; cfg.nav_enabled = true;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{}; const size_t bn = mk_beacon(/*src=*/2, bb);
    hal._now = 1000; node.on_recv(bb.data(), bn, RxMeta{8.0f, -80.0f, 0, 2});      // route to bob(2)
    hal._rand_ret = 200;                                                            // force a non-zero jitter draw
    hal._now = 2000; send_cmd(node, /*dst=*/2, "hi");
    CHECK(hal.count("rts_tx") == 0);                                                // held by the jitter (not even decided yet)
    CHECK(hal.last_tx("RTS") == nullptr);
    hal._now = 2300; node.on_timer(kQueueWakeupTimerId);                            // jitter elapsed -> drain
    CHECK(hal.last_tx("RTS") != nullptr);                                           // now it flies

    // control: nav OFF -> no origination jitter -> the RTS is handed at once
    TestHal h2; Node n2(h2, /*id=*/1, 0xABCD);
    NodeConfig c2; c2.routing_sf = 7; c2.allowed_sf_bitmap = (1u << 12); c2.leaf_id = 0; c2.lbt_enabled = false; c2.nav_enabled = false;   // explicit OFF (firmware default is now ON)
    n2.on_init(c2);
    std::array<uint8_t, 64> b2{}; const size_t bn2 = mk_beacon(2, b2);
    h2._now = 1000; n2.on_recv(b2.data(), bn2, RxMeta{8.0f, -80.0f, 0, 2});
    h2._rand_ret = 200; h2._now = 2000; send_cmd(n2, /*dst=*/2, "hi");
    CHECK(h2.last_tx("RTS") != nullptr);
}

// -----------------------------------------------------------------------------
// ★★ Origination — LOCATION is a PER-SEND request (`send -l`), 2026-07-31
// §loc-per-send / open-bug-register B0. THESE TESTS REPLACE A PAIR THAT ASSERTED
// THE LEAK: the old "loc_in_dm + nonzero location sets DATA_FLAG_LOCATION (coords
// round-trip)" built a NodeConfig with e2e_dm default OFF and NO crypto identity
// -> want_crypt == false -> a PLAINTEXT DM, and then read the 6 location bytes
// straight off the UNSEALED wire via parse_unicast_inner and CHECKed they
// round-tripped. That passing assertion WAS the bug: the node's coordinates on
// the air in clear, readable by anyone in range. The before-arm was captured on
// the pre-fix tree (frame `300c02052802010001cdab0000 a9a2839a3a00 6869 00000000`
// — crypted=0, and the 6 packed bytes verbatim on the wire).
// The rule now (owner ruling 2026-07-30, twice): `cfg set loc_dm` is GONE, `-l` is
// per message, and a `-l` send that will not be SEALED is REFUSED — not silently
// stripped (the app must never believe it shared a position it did not) and never
// aired in clear. So the case above must now air NO FRAME AT ALL.
// -----------------------------------------------------------------------------
namespace {
struct OrigLoc {
    bool aired = false;          // ★ did ANY DATA frame reach the air? (the refusal tests assert false)
    bool crypted = false;        // CRYPTED on the wire
    bool flag = false;           // DATA_FLAG_LOCATION on the wire
    bool loc_in_clear = false;   // the 6 PACKED location bytes appear VERBATIM in the aired frame (the leak's signature)
    bool cleartext_loc = false;  // parse_unicast_inner (the UNSEALED reader) recovered a location
    int32_t lat = 0, lon = 0;
    bool failed = false; SendFailReason reason = SendFailReason::none;   // the send_failed push
    std::vector<uint8_t> frame;  // the aired bytes (so a receiver can be driven with them)
    uint8_t plen = 0;            // inner + MAC length, for the receiver's RTS
    uint16_t ctr = 0;
};
// Drive a REAL app-DM origination (node 1 -> node 2) all the way to the DATA frame and read the outcome OFF THE WIRE.
// `want_loc` sets DATA_FLAG_LOCATION in the command flags word exactly as console_parse's `-l` does — no signature
// change anywhere. `sealed` installs a crypto identity + the recipient's authoritative pubkey + e2e_dm, which is the
// ONLY configuration in which a location may travel.
static OrigLoc originate_dm_loc(bool want_loc, int32_t lat, int32_t lon, bool sealed, uint8_t body_len = 2) {
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 1); sB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    TestHal hal; Node node(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.lat_e7 = lat; cfg.lon_e7 = lon; cfg.e2e_dm = sealed;
    node.on_init(cfg);
    if (sealed) {                                             // mined from the "e2e wiring" harness below (U1)
        node.set_crypto_identity(idA.x_secret, idA.ed_pub);
        node.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    }
    // One beacon from node 2 carrying idB's key gives BOTH a direct route to 2 AND the authoritative id_bind
    // (2 -> idB.key_hash32) that DST_HASH — and hence the seal — requires.
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta m{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    node.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), m);

    const std::string text(body_len, 'x');
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 2;
    c.u.send.flags = static_cast<uint8_t>(want_loc ? DATA_FLAG_LOCATION : 0);   // == what `send 2 "…" -l` emits
    c.body = reinterpret_cast<const uint8_t*>(text.data()); c.body_len = body_len;
    (void)node.on_command(c);
    std::array<uint8_t, 8> cb{};
    RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 100; node.on_recv(cb.data(), mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb), bob);
    node.on_timer(kCtsToDataGapTimerId);                      // CTS->DATA gap -> DATA tx

    OrigLoc r{};
    { Push pu{}; while (node.next_push(pu)) if (pu.kind == PushKind::send_failed) { r.failed = true; r.reason = pu.reason; break; } }
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    if (!dataf) return r;                                     // ★ NOTHING AIRED — what a refusal must produce
    r.aired = true;
    r.frame.assign(dataf->bytes.begin(), dataf->bytes.end());
    // The leak's signature: are the 6 PACKED location bytes sitting verbatim in the aired frame?
    if (lat != 0 || lon != 0) {
        uint8_t loc6[6]; pack_loc6(lat, lon, std::span<uint8_t>(loc6, 6));
        for (size_t i = 0; i + 6 <= r.frame.size(); ++i) {
            bool same = true; for (int j = 0; j < 6; ++j) if (r.frame[i + j] != loc6[j]) same = false;
            if (same) r.loc_in_clear = true;
        }
    }
    auto d = parse_data(std::span<const uint8_t>(r.frame.data(), r.frame.size()));
    if (d) {
        r.crypted = d->crypted;
        r.flag = (d->flags & DATA_FLAG_LOCATION) != 0;
        r.ctr  = d->ctr;
        auto inner = data_inner(std::span<const uint8_t>(r.frame.data(), r.frame.size()), *d);
        auto mac   = data_mac(std::span<const uint8_t>(r.frame.data(), r.frame.size()), *d);
        r.plen = static_cast<uint8_t>(inner.size() + mac.size());
        if (!d->crypted) {                                    // only an UNSEALED inner is parseable in the clear
            auto ui = parse_unicast_inner(inner, d->flags);
            if (ui && ui->has_location) { r.cleartext_loc = true; r.lat = ui->lat_e7; r.lon = ui->lon_e7; }
        }
    }
    return r;
}
}  // namespace

// ★★ THE CONVERTED LEAK TEST. Same inputs as the old passing assertion (a `-l` request on a node that will NOT seal);
// the demand is inverted: air NOTHING, and say why. Asserting "no location on the wire" would NOT be enough — omitting
// it silently is failure mode (2) of the ruling, so the frame itself must never exist.
TEST_CASE("§loc-per-send — a `-l` DM that would go PLAINTEXT is REFUSED: NO frame aired at all") {
    OrigLoc r = originate_dm_loc(/*want_loc=*/true, 523000000, 134050000, /*sealed=*/false);
    CHECK_FALSE(r.aired);            // ★★ not "the location was omitted" — NOTHING WAS SENT
    CHECK_FALSE(r.loc_in_clear);     // and therefore the 6 bytes are nowhere on the air (this is the closed leak)
    CHECK(r.failed);                 // fail LOUD (C2), never a silent drop
    CHECK(r.reason == SendFailReason::unsealable);   // the app is told the fix is -e / e2e_dm / acquire the key
    // ★ SAME-SHAPE CONTROL — the identical send WITHOUT `-l` still flies. Without this the test would also pass if the
    //   harness were simply broken, and the refusal would not be attributable to the flag.
    OrigLoc ctl = originate_dm_loc(/*want_loc=*/false, 523000000, 134050000, /*sealed=*/false);
    CHECK(ctl.aired);
    CHECK_FALSE(ctl.flag);
    CHECK_FALSE(ctl.failed);
    CHECK_FALSE(ctl.loc_in_clear);   // an ordinary DM never carried a position and still does not
}

TEST_CASE("§loc-per-send — `-l` with NO FIX (0,0) is REFUSED (no_location), even when the DM WOULD be sealed") {
    OrigLoc r = originate_dm_loc(/*want_loc=*/true, 0, 0, /*sealed=*/true);
    CHECK_FALSE(r.aired);            // you asked for a position and there is none -> nothing is sent
    CHECK(r.failed);
    // ★ DISTINCT from `unsealable` deliberately: the remedy is a GPS fix / `cfg set lat`+`lon`, NOT encryption. Reporting
    //   the seal rule here would send the operator chasing the wrong thing.
    CHECK(r.reason == SendFailReason::no_location);
    OrigLoc ctl = originate_dm_loc(/*want_loc=*/false, 0, 0, /*sealed=*/true);   // control: no -l, no fix -> flies
    CHECK(ctl.aired); CHECK_FALSE(ctl.failed);
}

// ★★ "+6 B does not fit" — the OLD behaviour was a SILENT best-effort drop (the gate just left the flag clear and the
// DM flew without the position the user asked for). It is now fail-loud, and this test pins WHO makes it loud, honestly:
// NOT a dedicated location gate (that would be dead code — see the arithmetic at the site) but the SEAL, because a `-l`
// DM is sealed by construction and the 6 bytes are part of the sealed plaintext. Body 212 is inside the MEASURED band
// where the location is exactly what tips it over: 208 is the largest `-l` body that flies, 214 the largest without.
TEST_CASE("§loc-per-send — a `-l` whose +6 B does NOT fit is REFUSED LOUD, never silently dropped") {
    const uint8_t tip = 212;
    OrigLoc r = originate_dm_loc(/*want_loc=*/true, 523000000, 134050000, /*sealed=*/true, tip);
    CHECK_FALSE(r.aired);                    // ★ the DM did NOT fly without its position (the old silent drop)
    CHECK(r.failed);
    CHECK(r.reason == SendFailReason::too_large);
    // ★ SAME-SIZE CONTROL: the identical body WITHOUT `-l` is a legal DM and still flies ⇒ the refusal is attributable
    //   to the 6 location bytes, not to the body size, which is what makes the assertion above mean something.
    OrigLoc ctl = originate_dm_loc(/*want_loc=*/false, 523000000, 134050000, /*sealed=*/true, tip);
    CHECK(ctl.aired); CHECK_FALSE(ctl.failed);
}

// ★★ THE POSITIVE ARM — and it needs a SEALED origination, which needs an identity plus the recipient's authoritative
// pubkey. Proves the whole chain: enqueue_data admitted the request, e2e_seal_inner packed the 6 bytes INSIDE the
// ciphertext, nothing leaked in clear, and the real receive path opened it back to coordinates on the app push.
TEST_CASE("§loc-per-send — a SEALED `-l` DM carries the position INSIDE the ciphertext, and the peer opens it") {
    const int32_t LAT = 523000000, LON = 134050000;
    OrigLoc r = originate_dm_loc(/*want_loc=*/true, LAT, LON, /*sealed=*/true);
    CHECK(r.aired);
    CHECK_FALSE(r.failed);
    CHECK(r.crypted);                // sealed — the only way a location may travel
    CHECK(r.flag);                   // DATA_FLAG_LOCATION set (the receiver reads the sealed layout from it)
    CHECK_FALSE(r.loc_in_clear);     // ★ the packed bytes are NOT verbatim on the wire — they are inside the AEAD
    CHECK_FALSE(r.cleartext_loc);    // and the cleartext reader finds nothing

    // Now B (node 2, idB) receives that very frame and opens it — the real handle_data -> do_post_ack path.
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 1); sB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts_for_frame(/*src=*/1, /*next=*/2, /*dst=*/2,
                                       static_cast<uint8_t>(r.ctr & 0x0F), r.plen, rb, r.frame.data(), r.frame.size()), from1);
    halB._now = 2000; B.on_recv(r.frame.data(), r.frame.size(), from1);
    B.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (B.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) {
        CHECK(pu.enc);                                        // delivered SEALED
        CHECK(pu.has_location);                               // ★ the position survived the seal/open round trip
        long dlat = static_cast<long>(pu.lat_e7) - LAT; if (dlat < 0) dlat = -dlat;
        long dlon = static_cast<long>(pu.lon_e7) - LON; if (dlon < 0) dlon = -dlon;
        CHECK(dlat <= 512); CHECK(dlon <= 512);               // pack_loc6 quantises to ~1024e-7 deg (~11 m)
    }
    CHECK(halB.count("peer_location") == 1);
}

TEST_CASE("§loc-per-send — an ORDINARY DM (no `-l`) never sets LOCATION, sealed or plain") {
    CHECK_FALSE(originate_dm_loc(/*want_loc=*/false, 523000000, 134050000, /*sealed=*/false).flag);
    CHECK_FALSE(originate_dm_loc(/*want_loc=*/false, 523000000, 134050000, /*sealed=*/true).flag);
    // and both still fly — the per-send flag left the default path untouched, which is what the ruling bought
    CHECK(originate_dm_loc(/*want_loc=*/false, 523000000, 134050000, /*sealed=*/false).aired);
    CHECK(originate_dm_loc(/*want_loc=*/false, 523000000, 134050000, /*sealed=*/true).aired);
}


// ★ `send_layer -l` REFUSES. NEITHER cross-layer builder can carry a position: enqueue_cross_layer masks the flag off
// and packs lat/lon = 0, and the sealed substitute (DATA_TYPE_SEALED_RELAY) has no flags word on the wire for the
// receiver to read one from. Refusing at the verb covers both the static and the mobile-delegate fork, before any
// seal_ctr or MAC ctr is burned.
TEST_CASE("§loc-per-send — `send_layer -l` is REFUSED loud (cross-layer carries no position)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.lat_e7 = 523000000; cfg.lon_e7 = 134050000;
    node.on_init(cfg);
    Command c{}; c.kind = CmdKind::send_layer;
    c.u.layer.dst_hash = 0xA1B2C3D4u; c.u.layer.hops[0] = 2; c.u.layer.hop_count = 1;
    c.u.layer.flags = DATA_FLAG_LOCATION;
    const char* body = "hi"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 2;
    const CmdResult r = node.on_command(c);
    CHECK(r.code == CmdCode::err_unsupported);                // synchronous refusal, with the dst_hash echoed back
    CHECK(r.ctr == 0);                                        // no counter burned
    CHECK(r.dst_hash == 0xA1B2C3D4u);
    CHECK(node.test_tx_queue_n() == 0);                       // ★ nothing staged, so nothing can ever air
    { Push pu{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (node.next_push(pu)) if (pu.kind == PushKind::send_failed) { sf = true; rsn = pu.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::unsealable); }
    // ★ SAME-SHAPE CONTROL: the identical send_layer WITHOUT `-l` is NOT refused for this reason — it proceeds into the
    //   normal path (here: no bridging gateway is known, so err_no_gateway). Proves the refusal is the flag's, not the verb's.
    TestHal hal2; Node n2(hal2, /*id=*/1, /*key=*/0xABCD); n2.on_init(cfg);
    Command c2 = c; c2.u.layer.flags = 0;
    const CmdResult r2 = n2.on_command(c2);
    CHECK(r2.code != CmdCode::err_unsupported);
    { Push pu{}; SendFailReason rsn = SendFailReason::none;
      while (n2.next_push(pu)) if (pu.kind == PushKind::send_failed) { rsn = pu.reason; break; }
      CHECK(rsn != SendFailReason::unsealable); }
}

// -----------------------------------------------------------------------------
// Receive — a delivered DM that carried LOCATION surfaces the coords on the
// msg_recv Push + emits a peer_location telemetry (spec 2026-06-14 §5). DATA only
// (M receive is deferred).
// -----------------------------------------------------------------------------
TEST_CASE("receive — a delivered DM with LOCATION surfaces coords on the Push + emits peer_location") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };          // immediate sender = 1
    const uint8_t bodytext[] = { 'h', 'i' };
    const int32_t LAT = 523000000, LON = 134050000;
    uint8_t inner[64];
    const uint8_t flags = static_cast<uint8_t>(DATA_FLAG_SOURCE_HASH | DATA_FLAG_LOCATION);
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), flags, /*dst_hash*/ 0,
                                         nullptr, 0, 0, /*origin*/ 1, /*source_hash*/ 0xCAFEF00Du,
                                         bodytext, sizeof bodytext, LAT, LON);
    CHECK(il > 0);
    std::array<uint8_t, 16> rb{};
    hal._now = 1000;
    node.on_recv(rb.data(), mk_rts(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5,
                                   /*plen=*/static_cast<uint8_t>(il + 4), rb), meta);
    uint8_t frame[96]; const uint8_t mac4[4] = { 0, 0, 0, 0 };
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(mac4, 4);
    const size_t fl = pack_data(din, std::span<uint8_t>(frame, sizeof frame));
    CHECK(fl > 0);
    hal._now = 2000; node.on_recv(frame, fl, meta);
    node.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) {
        CHECK(pu.has_location);
        long dlat = static_cast<long>(pu.lat_e7) - LAT; if (dlat < 0) dlat = -dlat;
        long dlon = static_cast<long>(pu.lon_e7) - LON; if (dlon < 0) dlon = -dlon;
        CHECK(dlat <= 512); CHECK(dlon <= 512);
    }
    CHECK(hal.count("peer_location") == 1);                          // telemetry emitted for the sim/gate
}

TEST_CASE("receive — a delivered DM WITHOUT location leaves the Push unset (no peer_location)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{};
    hal._now = 1000; node.on_recv(rb.data(), mk_rts(1, 2, 2, 5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005), meta);
    std::array<uint8_t, 64> db{};
    hal._now = 2000; node.on_recv(db.data(), mk_data(/*next=*/2, /*dst=*/2, /*ctr=*/0x0005, /*origin=*/1, "hi", db), meta);
    node.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (node.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got); if (got) CHECK_FALSE(pu.has_location);
    CHECK(hal.count("peer_location") == 0);
}

// -----------------------------------------------------------------------------
// Phase 1 §4 — seal-on-send WIRING: an e2e_dm origination must emit a CRYPTED
// DATA (CRYPTED|DST_HASH flags, the 8-B nonce-seed trailer, body sealed). Drives
// the real enqueue_data -> issue_send (seed thread) -> do_data_tx (trailer) path.
// -----------------------------------------------------------------------------
TEST_CASE("e2e wiring — an e2e_dm origination emits a CRYPTED DATA (8-B trailer, body NOT cleartext)") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    // A learns node 2 (=B): a beacon from src=2 binds 2 -> idB.key_hash32 (authoritative) + a direct route to 2.
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    A.on_recv(bb.data(), bn, bm);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);

    send_cmd(A, /*dst=*/2, "secret-dm-xyz");                  // a CRYPTED origination (e2e_dm on, B's pubkey known)
    std::array<uint8_t, 8> cb{}; RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
    hal._now = 100; A.on_recv(cb.data(), mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb), bob);
    A.on_timer(kCtsToDataGapTimerId);
    const TxFrame* dataf = nullptr;
    for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) dataf = &f;
    CHECK(dataf != nullptr);
    if (dataf) {
        auto d = parse_data(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()));
        CHECK(d.has_value());
        if (d) {
            CHECK(d->crypted);                               // CRYPTED set on the wire
            CHECK(d->dst_hash);                              // CRYPTED => DST_HASH
            CHECK(data_mac(std::span<const uint8_t>(dataf->bytes.data(), dataf->bytes.size()), *d).size() == 8);  // 8-B nonce-seed trailer
            const char* secret = "secret-dm-xyz"; bool leaked = false;
            for (size_t i = 0; i + 13 <= dataf->bytes.size(); ++i) { bool mm = true; for (int j = 0; j < 13; ++j) if (dataf->bytes[i+j] != uint8_t(secret[j])) mm = false; if (mm) leaked = true; }
            CHECK_FALSE(leaked);                             // the body is sealed (never cleartext on the wire)
        }
    }
}

// Shared setup for the e2e fail-loud tests: A (e2e_dm on) learns B's authoritative pubkey + id_bind (so DST_HASH
// resolves and "no pubkey" is ruled out). Caller decides whether to install A's crypto identity / oversize the body.
static void e2e_learn_peer(Node& A, TestHal& hal, const Identity& idB) {
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    A.on_recv(bb.data(), bn, bm);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
}
static bool saw_ev(const TestHal& hal, const char* type) {
    for (const auto& e : hal.events) if (e.type == type) return true;
    return false;
}

// R3 (review): with e2e_dm ON but NO crypto identity installed (set_crypto_identity never called -> _x_secret is
// zeros), e2e_seal_inner must NOT seal under a zero key (a silent self-blackhole the recipient tag-fails). It must
// FAIL LOUD (e2e_no_identity) and enqueue nothing — never cleartext, never a bogus-key frame, no WANT_PUBKEY flood.
TEST_CASE("R3 fail-loud: e2e_dm without a crypto identity refuses to seal (no zero-key blackhole)") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    // DELIBERATELY no A.set_crypto_identity(...) -> _crypto_ready == false.
    e2e_learn_peer(A, hal, idB);
    send_cmd(A, /*dst=*/2, "secret-no-id");
    CHECK(saw_ev(hal, "e2e_no_identity"));                   // fail loud
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));                  // nothing enqueued (no bogus-key frame)
    CHECK_FALSE(saw_ev(hal, "h_tx"));                        // no spurious WANT_PUBKEY flood (we HAVE the pubkey)
    CHECK_FALSE(saw_ev(hal, "e2e_no_pubkey"));               // not misreported as a missing-pubkey
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;                 // §2/§5: the app is WARNED with the reason
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::no_identity); }
}

// R7 (review): a crypto RNG that returns an all-zero nonce seed collapses nonce uniqueness to the 16-bit ctr ->
// keystream reuse under the static per-pair key (catastrophic). e2e_seal_inner must refuse loudly (e2e_bad_rng),
// never seal with a degenerate nonce.
TEST_CASE("R7 fail-loud: an all-zero crypto seed refuses to seal (no nonce-reuse)") {
    TestHal hal; hal.zero_rng = true;                       // emulate a broken crypto RNG (all-zero seed)
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    e2e_learn_peer(A, hal, idB);
    send_cmd(A, /*dst=*/2, "secret-bad-rng");
    CHECK(saw_ev(hal, "e2e_bad_rng"));                      // fail loud
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));                 // nothing sealed/enqueued
    CHECK_FALSE(saw_ev(hal, "h_tx"));                       // no flood (the RNG is broken, not the pubkey)
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::bad_rng); }
}

// R2 (review): the cleartext enqueue fit-gates omit the +16 Poly1305 tag, so a body in [217,232] sets the DM flags
// and passes them, but e2e_seal_inner then overflows the inner. The shared 0-return handler can't tell overflow from
// no-pubkey, so it misfires a HARD WANT_PUBKEY flood (for a key it HOLDS) and silently drops the DM (anti-flood +
// lost-message bug). The oversize case must fail loud (e2e_seal_too_large) with NO flood.
TEST_CASE("R2 fail-loud: an oversize CRYPTED DM fails loud + does NOT flood (anti-flood / lost-message)") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    e2e_learn_peer(A, hal, idB);
    const std::string big(220, 'x');                       // in [217,232]: cleartext gates pass, CRYPTED (+16 tag) overflows
    send_cmd(A, /*dst=*/2, big.c_str());
    CHECK(saw_ev(hal, "e2e_seal_too_large"));              // fail loud, distinct from no-pubkey
    CHECK_FALSE(saw_ev(hal, "h_tx"));                      // NO spurious WANT_PUBKEY flood
    CHECK_FALSE(saw_ev(hal, "e2e_no_pubkey"));             // not misreported as a missing pubkey
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));                // the DM is not enqueued (it can never fit)
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::too_large); }
}

// §5 (E2E peer-key provisioning, 2026-06-16): key acquisition is USER-DRIVEN, never silently automated. A no-pubkey
// CRYPTED send no longer auto-floods WANT_PUBKEY — it WARNS the app (send_failed{no_pubkey}) and DROPS. The user then
// requests the key on-air (`reqpubkey`) or scans a QR (`peerkey`).
TEST_CASE("§5 no-auto-query — a no-pubkey CRYPTED send warns (send_failed{no_pubkey}) + drops, NO WANT_PUBKEY flood") {
    TestHal hal;
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    Node A(hal, /*id=*/1, idA.key_hash32);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = true;
    A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    // A learns node 2's id_bind (DST_HASH resolves) but NOT its pubkey -> the seal hits no_pubkey.
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 2; be.next = 2; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = idB.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    const size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    RxMeta bm{ 12.0f, -70.0f, 0, static_cast<int8_t>(2) };
    A.on_recv(bb.data(), bn, bm);                          // (deliberately NO A.peer_key_set for idB)
    send_cmd(A, /*dst=*/2, "secret-no-key");
    CHECK(saw_ev(hal, "e2e_no_pubkey"));                   // fail loud
    CHECK_FALSE(saw_ev(hal, "h_tx"));                      // §5: NO auto WANT_PUBKEY flood (the user must reqpubkey)
    CHECK_FALSE(saw_ev(hal, "tx_enqueue"));               // never cleartext
    { Push pf{}; bool sf = false; SendFailReason rsn = SendFailReason::none;
      while (A.next_push(pf)) if (pf.kind == PushKind::send_failed) { sf = true; rsn = pf.reason; break; }
      CHECK(sf); CHECK(rsn == SendFailReason::no_pubkey); }   // the app is warned -> offer Request-key / Scan-QR
}

// §8b (per-message crypt): a single DM's crypt is decided PER message (sendhashx/sendhash), not only by the global
// e2e_dm. want_crypt = (intent==on)?true : (intent==off)?false : e2e_dm. Drive the full flight + read the DATA frame's
// CRYPTED bit for all four (intent × e2e_dm) corners.
TEST_CASE("§8b per-message crypt — intent overrides e2e_dm; default follows it") {
    auto run = [](bool e2e_dm_on, CryptIntent intent) -> bool {            // returns: was the on-air DATA CRYPTED?
        TestHal hal;
        uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
        Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
        Node A(hal, /*id=*/1, idA.key_hash32);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.e2e_dm = e2e_dm_on;
        A.on_init(cfg);
        A.set_crypto_identity(idA.x_secret, idA.ed_pub);
        e2e_learn_peer(A, hal, idB);                                       // id_bind(2) + B's authoritative pubkey
        Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 2; c.u.send.flags = 0; c.crypt = intent;
        const char* body = "msg"; c.body = reinterpret_cast<const uint8_t*>(body); c.body_len = 3;
        A.on_command(c);
        std::array<uint8_t, 8> cb{}; RxMeta bob{ 8.0f, -80.0f, 0, static_cast<int8_t>(2) };
        hal._now = 100; A.on_recv(cb.data(), mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/12, cb), bob);
        A.on_timer(kCtsToDataGapTimerId);
        for (const auto& f : hal.tx_frames) if (!f.bytes.empty() && (f.bytes[0] >> 4) == 0x3) {
            auto d = parse_data(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
            if (d) return d->crypted;
        }
        return false;
    };
    CHECK(run(/*e2e_dm=*/false, CryptIntent::on));         // force CRYPTED even with e2e_dm OFF (sendhashx)
    CHECK_FALSE(run(/*e2e_dm=*/true,  CryptIntent::off));  // force PLAIN even with e2e_dm ON (sendhash)
    CHECK(run(/*e2e_dm=*/true,  CryptIntent::def));        // default follows e2e_dm (on -> crypted)
    CHECK_FALSE(run(/*e2e_dm=*/false, CryptIntent::def));  // default follows e2e_dm (off -> plain)
}

// -----------------------------------------------------------------------------
// Phase 1 §5 — open-on-receive WIRING: B receives a CRYPTED DATA, do_post_ack
// opens it (seed from the trailer, sender from origin->id_bind) and delivers the
// DECRYPTED plaintext to the app push. Drives handle_data (seed capture) -> do_post_ack.
// -----------------------------------------------------------------------------
TEST_CASE("e2e wiring — B opens a received CRYPTED DM and delivers the plaintext to the push") {
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);

    // A seals a DM to B (A holds B's pubkey).
    TestHal halA; Node A(halA, 1, idA.key_hash32); A.on_init(cfg);
    A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH;
    const uint8_t body[9] = { 't','o','p','-','s','e','c','r','t' };
    uint8_t inner[96], seed[8];
    Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t il = A.e2e_seal_inner(inner, sizeof inner, seed, flags, /*dst=*/idB.key_hash32,
                                       /*origin=*/1, /*ctr=*/0x0005, /*source_hash=*/idA.key_hash32, 0, 0, body, sizeof body, oc);
    CHECK(il > 0);
    uint8_t frame[128];
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(seed, 8);
    const size_t fl = pack_data(din, std::span<uint8_t>(frame, sizeof frame));
    CHECK(fl == 8 + il + 8);                                  // hdr + inner + 8-B nonce-seed trailer

    // B receives it. B holds A's pubkey + learns A's binding (origin 1 -> idA.key_hash32) from a beacon.
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32; bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts_for_frame(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/5, static_cast<uint8_t>(il + 8), rb, frame, fl), from1);
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);
    Push pu{}; bool got = false;
    while (B.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) {
        CHECK(pu.body_len == 9);
        bool same = true; for (int i = 0; i < 9; ++i) if (pu.body[i] != body[i]) same = false;
        CHECK(same);                                         // the DECRYPTED plaintext was delivered
        CHECK(pu.sender_hash == idA.key_hash32);             // the verified sender
        CHECK(pu.enc);                                       // §8b: a sealed DM is stamped enc=true
    }
}

// §3 — the E2E-ack is gated on a SUCCESSFUL open and TARGETS THE DECRYPTED origin (sealed since §1c). A sealed DM the
// receiver can't open is dropped BEFORE the ack — so a sender that gets no ack assumes "not delivered or not decrypted"
// and retries (the contract's only recovery; there is no per-message "locked" state).
namespace {
// Build a CRYPTED DM A->B (origin=1, ctr=5) with the given extra flags + drive it into B; returns the packed frame len.
size_t e2e_seal_AtoB(Node& A, const Identity& idA, const Identity& idB, uint8_t extra_flags, const char* body,
                     uint8_t* frame, size_t cap, uint8_t* inner, size_t inner_cap, uint8_t seed[8]) {
    const uint8_t flags = DATA_FLAG_CRYPTED | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH | extra_flags;
    uint8_t blen = 0; while (body[blen]) ++blen;
    Node::SealOutcome oc = Node::SealOutcome::ok;
    const size_t il = A.e2e_seal_inner(inner, inner_cap, seed, flags, idB.key_hash32, /*origin=*/1, /*ctr=*/0x0005,
                                       /*source_hash=*/idA.key_hash32, 0, 0, reinterpret_cast<const uint8_t*>(body), blen, oc);
    if (il == 0) return 0;
    data_in din{}; din.addr_len = 0; din.flags = flags; din.next = 2; din.dst = 2; din.hops_remaining = 31; din.ctr = 0x0005;
    din.inner = std::span<const uint8_t>(inner, il); din.mac = std::span<const uint8_t>(seed, 8);
    return pack_data(din, std::span<uint8_t>(frame, cap));
}
}  // namespace
TEST_CASE("§3 e2e-ack — fires only after a successful open, TARGETING the recovered origin") {
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halA; Node A(halA, 1, idA.key_hash32); A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    uint8_t frame[128], inner[96], seed[8];
    const size_t fl = e2e_seal_AtoB(A, idA, idB, /*extra=*/DATA_FLAG_E2E_ACK_REQ, "ack-me", frame, sizeof frame, inner, sizeof inner, seed);
    CHECK(fl > 0);
    // B holds A's key + a route to A (beacon), opens the DM, delivers, and ACKs.
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    std::array<uint8_t, 64> bb{}; beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32; bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts_for_frame(1, 2, 2, 5, static_cast<uint8_t>(fl - 8), rb, frame, fl), from1);
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);
    const Ev* ack = halB.last("e2e_ack_tx");
    CHECK(ack != nullptr);
    if (ack) CHECK(ack->dst == 1);   // §1c+§3: the cleartext origin is GONE, so dst=1 can ONLY be the origin recovered from the seal
}
TEST_CASE("§3 e2e-ack — a CRYPTED DM the receiver can't open is dropped with NO ack") {
    uint8_t seedA[32], seedB[32]; for (int i = 0; i < 32; ++i) { seedA[i] = uint8_t(i + 1); seedB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, seedA); identity_from_seed(idB, seedB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halA; Node A(halA, 1, idA.key_hash32); A.on_init(cfg); A.set_crypto_identity(idA.x_secret, idA.ed_pub);
    A.peer_key_set(idB.key_hash32, idB.ed_pub, Node::PeerKeyConf::authoritative);
    uint8_t frame[128], inner[96], seed[8];
    const size_t fl = e2e_seal_AtoB(A, idA, idB, /*extra=*/DATA_FLAG_E2E_ACK_REQ, "ack-me", frame, sizeof frame, inner, sizeof inner, seed);
    CHECK(fl > 0);
    // B does NOT hold A's key -> trial-decrypt finds no candidate -> e2e_open_no_key, silent drop (BEFORE any ack).
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg); B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts_for_frame(1, 2, 2, 5, static_cast<uint8_t>(fl - 8), rb, frame, fl), from1);
    halB._now = 2000; B.on_recv(frame, fl, from1);
    B.on_timer(kPostAckTimerId);
    CHECK(halB.count("e2e_open_no_key") >= 1);   // dropped (no key)
    CHECK(halB.count("e2e_ack_tx") == 0);        // and NO ack — a sender seeing no ack must retry (the recovery model)
}

// R6.1 leaf-config membership filter (§3.3): the misconfig gate — a same-leaf neighbour whose advertised config_hash
// diverges from ours is NOT peered (no route learned); a matching one IS. Uses REAL non-zero config_hashes (the test
// default 0 is the "no fingerprint" sentinel that bypasses the gate, so we set the hash explicitly here).
TEST_CASE("R6.1 peering filter — divergent leaf config is not peered; matching config peers (misconfig gate)") {
    TestHal hal; Node A(hal, /*id*/ 1, 0x1111);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    cfg.allowed_sf_bitmap = (1u << 12); cfg.duty_cycle = 0.01;       // bitmap {12}, duty 1%, name ""
    CHECK(A.on_init(cfg));
    const uint32_t a_hash = meshroute::leaf_config_hash(cfg.allowed_sf_bitmap, meshroute::duty_to_bp(0.01),
        meshroute::frac_to_bp(cfg.channel_active_fraction), meshroute::ms_to_u16(cfg.channel_min_interval_ms),
        meshroute::ms_to_u16(cfg.dm_min_interval_ms), cfg.leaf_name, cfg.leaf_name_len);   // must match cfg_config_hash()

    auto route_to = [](Node& n, uint8_t dest) {
        for (uint8_t i = 0; i < n.rt_count(); ++i) if (n.rt_at(i).dest == dest) return true;
        return false;
    };
    auto feed = [&](uint32_t config_hash, uint8_t src) {
        beacon_in in{}; in.leaf_id = 0; in.src = src; in.key_hash32 = 0x2000u + src;
        in.config_hash = config_hash;                                // a REAL advertised fingerprint
        std::array<uint8_t, 64> b{};
        const size_t n = pack_beacon(in, std::span<uint8_t>(b.data(), b.size()));
        RxMeta meta{}; meta.snr_db = 9.0f; meta.rssi_dbm = -70.0f; meta.recv_ms = hal._now; meta.src_hint = -1;
        A.on_recv(b.data(), n, meta);
    };

    // (1) DIVERGENT config (bitmap {7} != {12}) -> different hash -> NOT peered.
    const uint32_t diverge = meshroute::leaf_config_hash((1u << 7), meshroute::duty_to_bp(0.01),
        meshroute::frac_to_bp(cfg.channel_active_fraction), meshroute::ms_to_u16(cfg.channel_min_interval_ms),
        meshroute::ms_to_u16(cfg.dm_min_interval_ms), nullptr, 0);   // only the bitmap differs -> divergent hash
    CHECK(diverge != a_hash);
    feed(diverge, /*src*/ 2);
    CHECK_FALSE(route_to(A, 2));
    // (2) MATCHING config -> peered (route to src installed).
    feed(a_hash, /*src*/ 3);
    CHECK(route_to(A, 3));
}

// R6.2 config-sync: (A) an unmanaged node hearing a MANAGED beacon adopts the lineage (un-synced) + CONFIG_PULLs;
// (B) a synced member answering a CONFIG_PULL emits a C config frame (cmd 0xB). (Full pull->answer->adopt = the gate.)
TEST_CASE("R6.2 config-sync — unmanaged node pulls on hearing a managed beacon; a member answers") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // (A) JOIN-PULL
    TestHal hj; Node J(hj, /*id=*/7, 0x7777);
    NodeConfig jc; jc.routing_sf = 7; jc.leaf_id = 0; jc.allowed_sf_bitmap = (1u << 12); J.on_init(jc);
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = 0x2002;
    bin.lineage_id = 0xABCD; bin.config_epoch = 3; bin.config_hash = 0x9999;   // managed, non-zero hash
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    hj._now = 1000; J.on_recv(bb.data(), bn, meta);
    CHECK(hj.count("config_pull_tx") >= 1);                 // J pulled the config
    CHECK(J.config().lineage_id == 0xABCD);                 // adopted the target lineage...
    CHECK(J.config().config_epoch == 0);                    // ...but un-synced (epoch 0) -> participation-gated until adopt

    // (B) ANSWER
    TestHal hm; Node M(hm, /*id=*/2, 0x2002);
    NodeConfig mc; mc.routing_sf = 7; mc.leaf_id = 0; mc.allowed_sf_bitmap = (1u << 7) | (1u << 9);
    mc.lineage_id = 0xABCD; mc.config_epoch = 3; M.on_init(mc);
    q_in q{}; q.leaf_id = 0; q.src = 7; q.dest = 2; q.opcode = q_opcode::config_pull; q.pull_lineage = 0xABCD; q.pull_epoch = 0;
    std::array<uint8_t, 16> qb{}; size_t qn = pack_q(q, std::span<uint8_t>(qb.data(), qb.size()));
    hm._now = 1000; M.on_recv(qb.data(), qn, meta);
    CHECK(hm.count("c_config_tx") >= 1);                    // a member answers the pull with a C frame
}

// §mobile Option A: a MOBILE is NOT a leaf-config-plane member. Unlike the static joiner above, hearing a MANAGED beacon it
// must NOT adopt the lineage or fire a CONFIG_PULL — it stays UNMANAGED (lineage 0, always synced -> can originate DMs) and
// peers by nibble. This is what keeps a mobile OFF the static config plane (no CONFIG_PULL/REQ_SYNC broadcast -> no local-id leak).
TEST_CASE("§mobile Option A — a mobile hearing a MANAGED beacon does NOT adopt the lineage / does NOT CONFIG_PULL (stays unmanaged, still routes)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    TestHal hm; Node M(hm, /*id=*/7, 0x7777);
    NodeConfig mc; mc.routing_sf = 7; mc.leaf_id = 0; mc.allowed_sf_bitmap = (1u << 7); mc.is_mobile = true;
    M.on_init(mc);
    CHECK(M.config().lineage_id == 0);                      // a mobile starts (and stays) unmanaged
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 2; bin.key_hash32 = 0x2002;
    bin.lineage_id = 0xABCD; bin.config_epoch = 3; bin.config_hash = 0x9999;   // a MANAGED static leaf neighbour (e.g. the mobile's home)
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size()));
    hm._now = 1000; M.on_recv(bb.data(), bn, meta);
    CHECK(hm.count("config_pull_tx") == 0);                 // ★ NO config pull (Option A: a mobile is never a config member)
    CHECK(hm.count("leaf_join_pull") == 0);                 // ★ NO lineage-join
    CHECK(M.config().lineage_id == 0);                      // ★ did NOT adopt the lineage -> stays unmanaged -> leaf_config_synced() -> can originate DMs
    bool learned=false; for (uint8_t i=0;i<M.rt_count();++i) if (M.rt_at(i).dest==2) learned=true;
    CHECK(learned);                                         // ★ still PEERS by nibble -> learns the static route (reaches the mesh via its home), just not as a config member
}

// ★ C config frame (cmd 0xB) BOOTSTRAP: a managed joiner with allowed_sf_bitmap==0 (no data SF — the old routed
// CONFIG_ANSWER could NEVER reach it) DOES receive its config on the control plane. After adopt: sf_list/duty/name
// set, synced (epoch>0), and the post-adopt config_hash EQUALS the source's (§5 — proves no perpetual re-pull loop).
TEST_CASE("C config frame — empty-sf_list joiner adopts on a C frame; hash matches the source (§5)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    const uint16_t src_bitmap = (1u << 6) | (1u << 7);     // the mother's SF set (SF6+SF7)
    const double   src_duty   = 0.001;                      // 0.1% -> duty_bp 10
    const uint16_t src_epoch  = 5;
    const char*    src_name   = "hub";

    // the joiner: NO data SF + un-synced, target lineage already set (it heard the managed beacon that triggered the pull)
    TestHal hj; Node J(hj, /*id=*/7, 0x7777);
    NodeConfig jc; jc.routing_sf = 7; jc.leaf_id = 0; jc.allowed_sf_bitmap = 0; J.on_init(jc);
    J.mutable_config().lineage_id = 0xABCD;
    CHECK(J.config().allowed_sf_bitmap == 0);

    // anti-spam v2: the mother provisions the 3 promoted knobs at non-default values -> the joiner must adopt them.
    const uint16_t src_frac_bp = 2500;    // 0.25
    const uint16_t src_ch_ms   = 15000;
    const uint16_t src_dm_ms   = 4000;
    // a C frame addressed to the joiner: [cmd 0xB | leaf 0][src=2][dst=7] + body
    meshroute::CConfig cc{}; cc.allowed_sf_bitmap = src_bitmap; cc.duty_bp = meshroute::duty_to_bp(src_duty);
    cc.active_fraction_bp = src_frac_bp; cc.ch_interval_ms = src_ch_ms; cc.dm_interval_ms = src_dm_ms;
    cc.config_epoch = src_epoch; cc.leaf_name_len = 3; for (int i = 0; i < 3; ++i) cc.leaf_name[i] = src_name[i];
    uint8_t frame[3 + 12 + 16]; frame[0] = static_cast<uint8_t>((0xB << 4) | 0); frame[1] = 2; frame[2] = 7;
    const size_t bn = meshroute::pack_c_config(cc, frame + 3, sizeof(frame) - 3);
    hj._now = 1000; J.on_recv(frame, 3 + bn, meta);

    CHECK(J.config().allowed_sf_bitmap == src_bitmap);     // sf_list adopted (was 0)
    CHECK(J.config().config_epoch == src_epoch);           // synced (epoch>0) -> participation gate lifts
    CHECK(J.config().leaf_name_len == 3);
    // the 3 promoted anti-spam knobs adopted live into the joiner's NodeConfig
    CHECK(J.config().channel_active_fraction == doctest::Approx(0.25f));
    CHECK(J.config().channel_min_interval_ms == 15000u);
    CHECK(J.config().dm_min_interval_ms == 4000u);
    CHECK(hj.count("leaf_config_adopted") >= 1);
    // §5: the joiner's recomputed config_hash now EQUALS the source's -> no re-pull (the round-trip-through-the-gate invariant)
    const uint16_t src_hash = meshroute::leaf_config_hash(src_bitmap, meshroute::duty_to_bp(src_duty),
        src_frac_bp, src_ch_ms, src_dm_ms, src_name, 3);
    const uint16_t joiner_hash = meshroute::leaf_config_hash(J.config().allowed_sf_bitmap,
        meshroute::duty_to_bp(J.config().duty_cycle), meshroute::frac_to_bp(J.config().channel_active_fraction),
        meshroute::ms_to_u16(J.config().channel_min_interval_ms), meshroute::ms_to_u16(J.config().dm_min_interval_ms),
        J.config().leaf_name, J.config().leaf_name_len);
    CHECK(joiner_hash == src_hash);
}

// R6.2 §6.4 participation gate: an un-synced MANAGED node (lineage!=0, epoch 0) must NOT originate app DMs / F —
// only CONFIG_PULL. A synced node (epoch>0) or UNMANAGED node (lineage 0) originates freely.
TEST_CASE("R6.2 participation gate — un-synced managed node blocks app-DM origination; synced originates") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    // un-synced: lineage set (joining) but epoch 0 -> do_send blocked, send_failed{joining}.
    TestHal hu; Node U(hu, /*id=*/7, 0x7777);
    NodeConfig uc; uc.routing_sf = 7; uc.leaf_id = 0; uc.allowed_sf_bitmap = (1u << 12);
    uc.lineage_id = 0xABCD; uc.config_epoch = 0;   // managed target, NOT yet synced
    U.on_init(uc);
    send_cmd(U, /*dst=*/9, "hi");
    CHECK(hu.count("send_failed") >= 1);             // un-synced origination refused (send_failed{joining})
    CHECK(hu.count("tx_enqueue") == 0);             // and nothing enqueued
    // synced: same shape but epoch>0 -> originates (the DM is enqueued).
    TestHal hs; Node S(hs, /*id=*/8, 0x8888);
    NodeConfig sc; sc.routing_sf = 7; sc.leaf_id = 0; sc.allowed_sf_bitmap = (1u << 12);
    sc.lineage_id = 0xABCD; sc.config_epoch = 1;    // synced member
    S.on_init(sc);
    send_cmd(S, /*dst=*/9, "hi");
    CHECK(hs.count("tx_enqueue") >= 1);             // originated (enqueued)
}

// R6.3 §4.1 dynamic config write: an operator write on a MANAGED node bumps epoch = max_seen+1 + re-advertises;
// an UNMANAGED node (lineage 0) write is a no-op (no epoch plane).
TEST_CASE("R6.3 leaf_config_write — managed bumps epoch=max_seen+1 + re-advertises; unmanaged is a no-op") {
    TestHal h; Node n(h, /*id*/5, 0xAAAA);
    NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 7); c.duty_cycle = 0.01;
    c.lineage_id = 0xABCD; c.config_epoch = 3; n.on_init(c);
    n.mutable_config().allowed_sf_bitmap = (1u << 7) | (1u << 9);     // operator changes the data-SF set...
    CHECK(n.leaf_config_write());                                     // ...and commits (deliberate intent)
    CHECK(n.config().config_epoch == 4);                             // 3 -> max_seen(3)+1
    CHECK(h.count("leaf_config_write") == 1);
    // unmanaged -> no-op (returns false, epoch untouched)
    TestHal h2; Node u(h2, 6, 0xBBBB);
    NodeConfig uc; uc.routing_sf = 7; uc.leaf_id = 0; uc.allowed_sf_bitmap = (1u << 7); uc.lineage_id = 0; u.on_init(uc);
    u.mutable_config().allowed_sf_bitmap = (1u << 9);
    CHECK_FALSE(u.leaf_config_write());
    CHECK(u.config().config_epoch == 0);
}

// R6.3 §4.1 LWW: a same-lineage, same-epoch, DIFFERENT-hash beacon resolves by key_hash32 — I LOSE to a higher key
// (pull + adopt theirs, no bump), I WIN vs a lower key (keep mine, no pull). One-sided -> converges, no epoch war.
TEST_CASE("R6.3 LWW tiebreak — same-epoch diff-hash: lose to a higher key (pull), win vs a lower key (keep)") {
    RxMeta meta{8.0f, -80.0f, 0, -1};
    auto run = [&](uint32_t my_key, uint32_t their_key, bool expect_pull) {
        TestHal h; Node n(h, /*id*/5, my_key);
        NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 7); c.duty_cycle = 0.01;
        c.lineage_id = 0xABCD; c.config_epoch = 4; n.on_init(c);
        const uint16_t my_hash = meshroute::leaf_config_hash((1u << 7), 10000, 0, 0, 0, nullptr, 0);   // arbitrary base for the XOR-diverge below
        beacon_in b{}; b.leaf_id = 0; b.src = 2; b.key_hash32 = their_key;
        b.lineage_id = 0xABCD; b.config_epoch = 4;
        b.config_hash = static_cast<uint16_t>(my_hash ^ 0x5A5A);                              // guaranteed != my_hash, non-zero
        std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(b, std::span<uint8_t>(bb.data(), bb.size()));
        h._now = 1000; n.on_recv(bb.data(), bn, meta);
        CHECK(h.count("leaf_config_conflict") >= 1);                  // the same-epoch divergence is detected
        CHECK((h.count("config_pull_tx") >= 1) == expect_pull);      // loser pulls; winner doesn't
        CHECK(n.config().config_epoch == 4);                         // NEVER bumps on a tie (no epoch war)
    };
    run(/*my*/0x1000, /*their*/0x2000, /*expect_pull=*/true);        // their key higher -> I lose -> pull
    run(/*my*/0x2000, /*their*/0x1000, /*expect_pull=*/false);       // their key lower  -> I win  -> keep
}

// R6.3 provisioning verbs (live core seam): reset_leaf_epoch_state resets BOTH config_epoch AND _max_seen_epoch, so a
// fresh lineage (create) doesn't inherit the old leaf's epoch numbering. Proven via the epoch a subsequent write yields.
TEST_CASE("R6.3 reset_leaf_epoch_state — resets epoch + max_seen (no leak into a fresh lineage)") {
    TestHal h; Node n(h, /*id*/5, 0xAAAA);
    NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; c.allowed_sf_bitmap = (1u << 7); c.duty_cycle = 0.01;
    c.lineage_id = 0xABCD; c.config_epoch = 5; n.on_init(c);
    n.mutable_config().allowed_sf_bitmap = (1u << 9); CHECK(n.leaf_config_write());
    CHECK(n.config().config_epoch == 6);                             // 5 -> 6; leaf_config_write also sets _max_seen_epoch=6
    n.mutable_config().lineage_id = 0x1234; n.reset_leaf_epoch_state(1);   // 'create' a fresh lineage at epoch 1
    CHECK(n.config().config_epoch == 1);
    n.mutable_config().allowed_sf_bitmap = (1u << 11); CHECK(n.leaf_config_write());
    CHECK(n.config().config_epoch == 2);                             // 1 -> 2 (NOT 7) -> max_seen was reset, no leak
}

// R6.3 §7c: a beacon advertising a DIFFERENT wire_version is refused (no peer) + a RATE-LIMITED join_refused; a
// same-version beacon is processed normally.
TEST_CASE("R6.3 §7c — a foreign wire_version beacon is refused + rate-limited; same-version is processed") {
    TestHal h; Node n(h, /*id*/5, 0xAAAA);
    NodeConfig c; c.routing_sf = 7; c.leaf_id = 0; n.on_init(c);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    beacon_in b{}; b.leaf_id = 0; b.src = 2; b.key_hash32 = 0x2002;
    std::array<uint8_t, 64> bb{}; size_t bn = pack_beacon(b, std::span<uint8_t>(bb.data(), bb.size()));
    bb[3] = static_cast<uint8_t>((bb[3] & 0xF0) | 0x0E);              // wire_version 14 != ours (1)
    h._now = 1000; n.on_recv(bb.data(), bn, meta);
    CHECK(h.count("join_refused") == 1);                             // refused (visible, not just telemetry-dropped)
    h._now = 2000; n.on_recv(bb.data(), bn, meta);                   // within the cooldown
    CHECK(h.count("join_refused") == 1);                             // rate-limited -> NOT per-beacon
    // a same-version beacon (untouched nibble) is NOT refused
    std::array<uint8_t, 64> ok{}; size_t okn = pack_beacon(b, std::span<uint8_t>(ok.data(), ok.size()));
    h._now = 3000; n.on_recv(ok.data(), okn, meta);
    CHECK(h.count("join_refused") == 1);                             // unchanged
}

// ============================================================================
// SLICE 2 (asymmetric-link-aware routing, 2026-06-29): store the bidi plane.
// State-only / delivery-neutral: no penalty rides effective_score yet.
// ============================================================================

TEST_CASE("bidi constants — LinkBidi zero-default + constant seeds") {
    using namespace meshroute;
    // LinkBidi: unknown MUST be 0 so a zeroed _link_bidi slot reads as 'unknown'.
    CHECK(static_cast<uint8_t>(LinkBidi::unknown) == 0);
    CHECK(static_cast<uint8_t>(LinkBidi::confirmed) == 1);
    CHECK(static_cast<uint8_t>(LinkBidi::one_way) == 2);
    // Seeds (contract): one_way penalty == peer_silent_penalty_q4 (640 Q4).
    CHECK(protocol::bidi_penalty_one_way_q4 == protocol::peer_silent_penalty_q4);
    CHECK(protocol::bidi_penalty_one_way_q4 == 640);
    // Confirmation freshness TTL == next_hop_live_ttl_ms (20 min).
    CHECK(protocol::bidi_confirm_ttl_ms == protocol::next_hop_live_ttl_ms);
    CHECK(protocol::bidi_confirm_ttl_ms == 1200000u);
    // Slow-reprobe TTL + census headroom seeds.
    CHECK(protocol::link_reprobe_ttl_ms == 60000u);
    CHECK(protocol::heard_set_census_min_headroom == 4);
}

TEST_CASE("bidi state — _link_bidi defaults to unknown; degraded_from_wire defaults false") {
    using namespace meshroute;
    TestHal hal;                                  // defined at top of test_node_r3.cpp
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    // A zero-initialized LayerRuntime: every link reads 'unknown'.
    CHECK(node.link_bidi_state(0)   == LinkBidi::unknown);
    CHECK(node.link_bidi_state(42)  == LinkBidi::unknown);
    CHECK(node.link_bidi_state(254) == LinkBidi::unknown);
    // RtCandidate's new wire-inherited field defaults false (a value-initialized candidate).
    RtCandidate c{};
    CHECK(c.degraded_from_wire == false);
}

TEST_CASE("bidi note_link_confirmed — sets confirmed + stamps confirmed_ms") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    hal._now = 500000;
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    node.note_link_confirmed(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    CHECK(node.link_bidi_confirmed_ms(9) == 500000u);
    // A later re-confirm refreshes the timestamp (still confirmed).
    hal._now = 700000;
    node.note_link_confirmed(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    CHECK(node.link_bidi_confirmed_ms(9) == 700000u);
    // Other links untouched.
    CHECK(node.link_bidi_state(8) == LinkBidi::unknown);
}

TEST_CASE("bidi decay_link_bidi — confirmed decays to UNKNOWN past TTL, never to one_way") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    hal._now = 100000;
    node.note_link_confirmed(9);                                   // confirmed @ 100000
    // Not yet stale: just under the TTL -> stays confirmed.
    hal._now = 100000 + protocol::bidi_confirm_ttl_ms - 1;
    node.decay_link_bidi(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    // At/over the TTL: confirmed -> UNKNOWN (MF6: never one_way — staleness is not positive absence evidence).
    hal._now = 100000 + protocol::bidi_confirm_ttl_ms;
    node.decay_link_bidi(9);
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    // A one_way link is NOT touched by decay (positive evidence persists until gossip/CTS flips it).
    TestHal hal2;
    Node n2(hal2, /*node_id=*/7, /*key_hash32=*/0xABCD);
    n2.on_init(cfg);
    n2.set_link_bidi_for_test(5, LinkBidi::one_way);               // test seam
    hal2._now = 99999999;                                         // way past any TTL
    n2.decay_link_bidi(5);
    CHECK(n2.link_bidi_state(5) == LinkBidi::one_way);
}

TEST_CASE("bidi candidate_degraded — live OR of wire-inherited bit and local one_way") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    node.on_init(cfg);
    RtCandidate c{}; c.next_hop = 9; c.score = 0; c.hops = 2;
    // Neither component set -> not degraded.
    CHECK(node.candidate_degraded(c) == false);
    // Wire-inherited bit alone -> degraded.
    c.degraded_from_wire = true;
    CHECK(node.candidate_degraded(c) == true);
    // Clear the wire bit; mark the local link one_way -> degraded (the LIVE component).
    c.degraded_from_wire = false;
    node.set_link_bidi_for_test(9, LinkBidi::one_way);
    CHECK(node.candidate_degraded(c) == true);
    // confirmed local link, no wire bit -> NOT degraded (recomputed live, no stuck-degraded cache).
    node.set_link_bidi_for_test(9, LinkBidi::confirmed);
    CHECK(node.candidate_degraded(c) == false);
    // unknown local link, no wire bit -> NOT degraded.
    node.set_link_bidi_for_test(9, LinkBidi::unknown);
    CHECK(node.candidate_degraded(c) == false);
}

TEST_CASE("bidi hook — a real CTS from our flight's next-hop confirms the link") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    cfg.allowed_sf_bitmap = (1u << 7);                 // permit a data SF so a flight can arm
    node.on_init(cfg);
    hal._now = 1000;
    // Install a route to dest 20 via next-hop 9 and originate a DM so a pending_tx awaits a CTS from 9.
    CHECK(node.route_inject(/*dest=*/20, /*next_hop=*/9, /*hops=*/2, /*score_q4=*/(12 << 4)));
    send_cmd(node, /*dst=*/20, "x");                   // originate via the public send_cmd helper (do_send is private)
    CHECK(node.has_pending_tx());
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    // The real handle_cts flight-match is rx_id==self && tx_id==next (ctr_lo match was dropped — see
    // node_mac_rx.cpp:330): a CTS from 9 (tx_id=9) clearing us (rx_id=7) pins our flight. Use the existing
    // mk_cts idiom (the same one the RTS->CTS->DATA flight tests above use).
    std::array<uint8_t, 8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/7, /*tx_id=*/9, /*data_sf=*/7, cb);
    CHECK(cn > 0);
    if (cn > 0) {
        RxMeta meta{8.0f, -80.0f, 0, -1};
        hal._now = 1100;
        node.on_recv(cb.data(), cn, meta);
        // The real CTS from our next-hop confirms 9 is bidirectional.
        CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
    }
}

// ---- Slice 3: detection scan + degraded-from-wire inheritance ---------------

TEST_CASE("update_link_bidi_from_beacon: present->confirmed, absent+complete->one_way, absent+incomplete->no change") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // (a) PRESENT: advertiser 7's beacon lists [dest=5, next=5] -> "7 hears 5" -> 5<->7 confirmed.
    beacon_entry present[1] = {};
    present[0].dest = 5; present[0].next = 5; present[0].hops = 1;
    node.test_update_link_bidi_from_beacon(/*advertiser=*/7, present, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::confirmed));
    CHECK(node.bidi_penalty_q4(7) == 0);                     // confirmed => no penalty

    // (b) ABSENT + COMPLETE: advertiser 8's COMPLETE page omits dest=5 -> 8 does NOT hear 5 -> 5->8 one_way.
    beacon_entry other[1] = {};
    other[0].dest = 99; other[0].next = 99; other[0].hops = 1;   // some unrelated dest, NOT self
    node.test_update_link_bidi_from_beacon(/*advertiser=*/8, other, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(8) == static_cast<uint8_t>(LinkBidi::one_way));
    CHECK(node.bidi_penalty_q4(8) == protocol::bidi_penalty_one_way_q4);

    // (c) ABSENT + INCOMPLETE: advertiser 9 truncated its page (complete=false) -> NO state change (stays unknown=0).
    node.test_update_link_bidi_from_beacon(/*advertiser=*/9, other, /*n=*/1, /*complete=*/false);
    CHECK(node.link_bidi_at(9) == static_cast<uint8_t>(LinkBidi::unknown));
    CHECK(node.bidi_penalty_q4(9) == 0);
}

TEST_CASE("endpoint override: a [dest==self] entry confirms (never degrades) the receiver's own link") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // Advertiser 7 lists [dest=5] WITH the degraded wire-bit set (a stale third-party view that 7->5 is one-way).
    // The endpoint that RECEIVED 7's beacon has LIVE proof 7->5 works (it just decoded it), so the scan treats the
    // present self-entry as a CONFIRMATION and ignores the degraded bit entirely (design §1 endpoint override).
    beacon_entry e[1] = {};
    e[0].dest = 5; e[0].next = 5; e[0].hops = 1; e[0].degraded = true;
    node.test_update_link_bidi_from_beacon(/*advertiser=*/7, e, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::confirmed));   // NOT one_way, despite degraded bit
    CHECK(node.bidi_penalty_q4(7) == 0);
}

TEST_CASE("rt_merge: degraded_from_wire is inherited from the incoming entry and CLEARS on a clean re-advert") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);

    // Build advertiser 2's beacon carrying [dest=9, next=8, degraded=1] -> a degraded route to 9 via 2.
    auto make_beacon = [&](bool degraded, std::vector<uint8_t>& out) {
        beacon_entry ent[1] = {};
        ent[0].dest = 9; ent[0].next = 8; ent[0].hops = 2;
        ent[0].score_bucket = 12; ent[0].degraded = degraded;
        beacon_in in{}; in.leaf_id = node.active_layer_id() & 0x0F; in.src = 2; in.key_hash32 = 0x2222;
        in.entries = std::span<const beacon_entry>(ent, 1);
        out.resize(protocol::beacon_max_bytes);
        const size_t len = pack_beacon(in, std::span<uint8_t>(out.data(), out.size()));
        CHECK(len > 0);
        if (len > 0) out.resize(len);
    };

    RxMeta meta{}; meta.snr_db = 6.0f;

    // (1) DEGRADED advert -> the installed candidate for dest 9 via 2 carries degraded_from_wire.
    std::vector<uint8_t> bcn1; make_beacon(/*degraded=*/true, bcn1);
    node.test_ingest_beacon(bcn1.data(), bcn1.size(), meta);
    const RtEntry* e1 = nullptr;
    for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 9) e1 = &node.rt_at(i);
    CHECK(e1 != nullptr);
    if (e1 != nullptr) {
        bool found_deg = false;
        for (uint8_t j = 0; j < e1->n; ++j) if (e1->candidates[j].next_hop == 2) found_deg = e1->candidates[j].degraded_from_wire;
        CHECK(found_deg == true);
    }

    // (2) CLEAN re-advert (same route, degraded=0) -> degraded_from_wire CLEARS (fresh recompute, not sticky-OR).
    std::vector<uint8_t> bcn2; make_beacon(/*degraded=*/false, bcn2);
    hal._now += 1000;
    node.test_ingest_beacon(bcn2.data(), bcn2.size(), meta);
    const RtEntry* e2 = nullptr;
    for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 9) e2 = &node.rt_at(i);
    CHECK(e2 != nullptr);
    if (e2 != nullptr) {
        bool still_deg = false;
        for (uint8_t j = 0; j < e2->n; ++j) if (e2->candidates[j].next_hop == 2) still_deg = e2->candidates[j].degraded_from_wire;
        CHECK(still_deg == false);
    }
}

TEST_CASE("ingest_beacon drives update_link_bidi_from_beacon: complete page omitting self -> one_way") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // Advertiser 7's COMPLETE beacon (heard_set_complete=true) lists [dest=9,next=9,hops=1] but NOT self(5)
    // -> 7 does not hear 5 -> 5->7 one_way must be set by ingest_beacon's scan call.
    beacon_entry ent[1] = {};
    ent[0].dest = 9; ent[0].next = 9; ent[0].hops = 1; ent[0].score_bucket = 12;
    beacon_in in{}; in.leaf_id = node.active_layer_id() & 0x0F; in.src = 7; in.key_hash32 = 0x7777;
    in.heard_set_complete = true;                            // Slice 1 wire bit (byte-3 b4)
    in.entries = std::span<const beacon_entry>(ent, 1);
    std::vector<uint8_t> buf(protocol::beacon_max_bytes);
    const size_t len = pack_beacon(in, std::span<uint8_t>(buf.data(), buf.size()));
    CHECK(len > 0);
    if (len > 0) {
        buf.resize(len);
        RxMeta meta{}; meta.snr_db = 6.0f;
        node.test_ingest_beacon(buf.data(), buf.size(), meta);
        CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::one_way));
        CHECK(node.bidi_penalty_q4(7) == protocol::bidi_penalty_one_way_q4);
    }
}

// ── Asymmetric-link-aware routing, SLICE 4: bidi penalty in effective_score ───
TEST_CASE("§bidi — bidi_penalty_q4 is silent_penalty for one_way, 0 for unknown/confirmed") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.bidi_penalty_q4(2) == 0);                                  // unknown (zeroed slot) -> 0
    node.note_link_confirmed(2);                                          // confirmed (Slice 2)
    CHECK(node.bidi_penalty_q4(2) == 0);                                  // confirmed -> 0
    node.test_set_link_one_way(3);                                        // one_way
    CHECK(node.bidi_penalty_q4(3) == protocol::bidi_penalty_one_way_q4);  // one_way -> the full penalty
    CHECK(node.bidi_penalty_q4(3) == protocol::peer_silent_penalty_q4);   // seed == silent class
}

TEST_CASE("§bidi — a one_way next-hop drops effective_score by the bidi penalty (vs an unknown peer)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));
    CHECK(rt_primary_for(node, 5) == 2);                 // insertion-order tie holds (no bidi state yet)
    node.test_set_link_one_way(2);                       // via 2 is now one_way
    node.note_link_confirmed(3);                         // via 3 confirmed (fan-out re-sorts)
    CHECK(rt_primary_for(node, 5) == 3);                 // §bidi: confirmed via-3 now beats penalized one_way via-2
}

TEST_CASE("§bidi — a confirm/one_way transition re-ranks routes via that next-hop (fan-out)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));   // primary via 2
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));   // alt via 3
    CHECK(rt_primary_for(node, 5) == 2);
    node.test_set_link_one_way(2);          // transition on next-hop 2 -> fans out a re-sort NOW
    CHECK(rt_primary_for(node, 5) == 3);    // via-2 penalized, via-3 (unknown=0) promoted by the transition fan-out
    node.note_link_confirmed(2);            // recovery transition on next-hop 2 -> fan out again (penalty clears)
    CHECK(node.bidi_penalty_q4(2) == 0);    // via-2 recovered: no longer penalized
    CHECK(rt_primary_for(node, 5) == 3);    // stable sort (no id tie-break) keeps via-3 primary on the now-tie — recovery clears the penalty but does NOT spuriously flap the primary back
}

TEST_CASE("§bidi — route_strictly_better ranks confirmed > unknown > one_way at equal score/hops") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/4, /*hops=*/1, /*score=*/200));
    node.test_set_link_one_way(2);          // via 2 = one_way
    node.note_link_confirmed(4);            // via 4 = confirmed  [via 3 stays unknown]
    node.rt_resort_for_pick(5);             // force the full re-sort under the bidi penalties
    CHECK(rt_primary_for(node, 5) != 2);    // one_way is demoted out of primacy
    const RtEntry* e = nullptr; for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 5) e = &node.rt_at(i);
    CHECK(e != nullptr);
    if (e) CHECK(e->candidates[e->n - 1].next_hop == 2);   // one_way sorts LAST among the three
}

TEST_CASE("§bidi — a SOLE one_way route stays selectable: the DM still fires an RTS (no delivery loss)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // ONLY route to dst 5 is via next-hop 2
    node->test_set_link_one_way(2);                        // that sole next-hop is now authoritatively one_way
    CHECK(node->bidi_penalty_q4(2) == protocol::bidi_penalty_one_way_q4);   // it IS penalized in the score
    send_cmd(*node, /*dst=*/5, "hi");                      // originate — must NOT be dropped for lack of a viable hop
    const Ev* r = hal.last("rts_tx");
    CHECK(r != nullptr);                                   // an RTS WAS sent (sole one_way stayed selectable)
    if (r) CHECK(r->next == 2);                            // ...at the one_way next-hop (delivery not lost)
    CHECK(hal.count("send_no_route") == 0);                // not failed as "no route" — the route is viable-for-pick
    delete node;
}

// ── Companion-contract gap fixes: D7 — per-peer DM ctr survives reboot ────────
TEST_CASE("D7 — per-peer ctr floor: a fresh peer resumes above the persisted high-water; peer_ctr_high = max") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    node.restore_peer_ctr_floor(100);                      // the persisted pre-reboot high-water seeds the boot floor
    CHECK(node.test_next_ctr(9) == 101);                   // a peer NOT yet sent-to this boot resumes ABOVE the floor (no ctr re-mint -> no companion dedup-collision)
    CHECK(node.test_next_ctr(9) == 102);                   // continues from there
    node.test_next_ctr(10);                                // another fresh peer is also floored -> 101
    CHECK(node.peer_ctr_high() == 102);                    // max over ALL peers (9->102, 10->101)
    CHECK(node.test_next_ctr(9) == 103);                   // an already-active peer above the floor is NOT reset down
    node.restore_channel_ctr(500);                         // the self/channel counter is just one _peer_send_counter entry
    CHECK(node.peer_ctr_high() == 500);                    // ...and it feeds the lease high-water too (channel id-reuse fix subsumed)
}

// ★★ §b39 (register B39) — THE INVARIANT THE `ctr == 0` SENTINEL RESTS ON: next_ctr NEVER mints 0, so a zero `ctr` in a
// CmdResult can only mean "nothing was minted HERE" — the reading Node::on_command's send_channel arm documents (with
// the three ways it happens, one of which is a success). Pinned DIRECTLY here because the only prior coverage was
// INCIDENTAL: test_node_channel.cpp's §b40 case asserts the 65535 -> 1 wrap as a side effect of a 16-bit-width check,
// so a coder who "simplifies" the wrap to a bare `c + 1` — which WOULD yield 0 — would see a channel-push test go red
// and not a test that names the property he broke. Lives beside the D7 floor case above because the floor is the other
// half of next_ctr's range argument (both must be unable to reach 0).
TEST_CASE("§b39 — next_ctr NEVER mints 0: the 65535 wrap yields 1, and the reboot floor cannot produce a 0 either") {
    TestHal hal; Node node(hal, /*id=*/7, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; node.on_init(cfg);
    // The persisted floor is the only way to drive a fresh counter to the wrap boundary without 65535 calls.
    node.restore_peer_ctr_floor(65535);                    // pre-reboot high-water sits exactly AT the boundary
    const uint16_t w = node.test_next_ctr(9);              // floored up to 65535, then wrapped
    CHECK(w == 1);                                         // `c = (c >= 65535) ? 1 : c + 1` — the wrap lands on ONE...
    CHECK(w != 0);                                         // ...which is the sentinel invariant, asserted as itself
    for (int i = 0; i < 4; ++i) CHECK(node.test_next_ctr(9) != 0);        // and it stays non-zero across the boundary
    CHECK(node.test_next_ctr(11) != 0);                    // a FRESH peer at the same floor wraps the same way
    // No floor at all (the ordinary case): a never-used peer's FIRST mint is 1, so it cannot answer 0 either.
    TestHal h2; Node n2(h2, /*id=*/7, /*key=*/0xABCD);
    NodeConfig c2; c2.routing_sf = 7; c2.leaf_id = 0; n2.on_init(c2);
    CHECK(n2.test_next_ctr(9) == 1);
}

// ── Anti-spam v2 duty-channel-cap, SLICE 0 (inert) ───────────────────────────
TEST_CASE("Slice0 — channel_active_fraction Cfg field default + settable (inert)") {
    NodeConfig cfg;
    CHECK(cfg.channel_active_fraction == doctest::Approx(0.125f));   // seed default (deployment knob, not a wire const)
    cfg.channel_active_fraction = 0.25f;
    CHECK(cfg.channel_active_fraction == doctest::Approx(0.25f));
}

TEST_CASE("Slice0 — channel_duty_budget_ms() is the 5-min D (MF1), 0 when duty disabled (MF2)") {
    {   // duty ENABLED at 1%: D = 0.01 * originator_window_ms(300000) = 3000 ms — the cap basis the limits JSON shows
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.duty_cycle = 0.01;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 3000u);
        CHECK(node.channel_duty_budget_ms() != 36000u);    // MF1 guard: NOT the 1-HOUR budget (0.01*3600000)
    }
    {   // duty DISABLED (shipped default 0.0) -> D == 0 (the legacy-flat-cap sentinel)
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 0u);
    }
    {   // a 10% band scales linearly: 0.10 * 300000 = 30000 ms
        TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x1);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; cfg.duty_cycle = 0.10;
        node.on_init(cfg);
        CHECK(node.channel_duty_budget_ms() == 30000u);
    }
}

TEST_CASE("channel_cap_origin — MF2: duty disabled -> legacy flat cap") {
    TestHal hal;
    Node* off = mk_budget_node(hal, /*duty=*/0.0, /*window=*/3600000);   // duty<=0 -> channel_duty_budget_ms()==0
    CHECK(off->channel_duty_budget_ms() == 0u);
    CHECK(off->channel_cap_origin() == meshroute::protocol::cap_channel_origin_legacy);   // == 20
    delete off;
}

TEST_CASE("channel_cap_origin — MF1/MF3 formula: SF, N, and C>=1 floor") {
    // routing SF7 / BW250000 / CR5 / preamble 16; duty 1% over the 5-min window => D = 3000 ms.
    // T_ch(SF) = airtime_routing_ms(43) + airtime_ms(SF,250000,5,16,39); C = max(1, 3000/T_ch).
    auto mk = [](TestHal& h, int data_sf) {
        Node* n = new Node(h, /*id=*/1, /*key=*/0xABCD);
        NodeConfig c; c.routing_sf = 7; c.radio_bw_hz = 250000; c.radio_cr = 5;
        c.allowed_sf_bitmap = (1u << data_sf);                 // single DATA SF -> max_data_sf()==data_sf
        c.duty_cycle = 0.01; c.duty_cycle_window_ms = 3600000; // D (5-min) = 0.01*300000 = 3000
        c.channel_active_fraction = 0.125f;
        n->on_init(c);
        return n;
    };
    auto inject_n = [](Node& n, int N) {
        for (int i = 0; i < N; ++i) n.route_inject(static_cast<uint8_t>(20 + i), /*next=*/2, /*hops=*/2, /*score=*/160);
    };
    // pinned: SF9, small N (N_active floors at 1) -> cap == C
    TestHal h9; Node* n9 = mk(h9, 9);
    CHECK(n9->channel_duty_budget_ms() == 3000u);              // MF1: 5-min D, NOT the 1-h budget
    inject_n(*n9, 4); CHECK(n9->rt_count() == 4);
    const uint16_t cap_sf9_smallN = n9->channel_cap_origin();  // == C (N_active=1)
    delete n9;
    // N dependence (SF9): cap ∝ 1/N — fresh node per N for an exact ratio
    TestHal ha; Node* na = mk(ha, 9); inject_n(*na, 40); CHECK(na->rt_count() == 40);
    const uint16_t cap_sf9_N40 = na->channel_cap_origin(); delete na;       // N_active=floor(0.125*40)=5 -> C/5
    TestHal hb; Node* nb = mk(hb, 9); inject_n(*nb, 100); CHECK(nb->rt_count() == 100);
    const uint16_t cap_sf9_N100 = nb->channel_cap_origin(); delete nb;      // N_active=floor(0.125*100)=12 -> C/12
    CHECK(cap_sf9_N100 <= cap_sf9_N40);                                     // more originators -> smaller share
    CHECK(cap_sf9_N100 >= 1);                                              // clamp floor
    // SF dependence: higher SF -> larger T_ch -> lower cap (same small N)
    TestHal h7; Node* n7 = mk(h7, 7); inject_n(*n7, 4); const uint16_t cap_sf7 = n7->channel_cap_origin(); delete n7;
    TestHal h12; Node* n12 = mk(h12, 12); inject_n(*n12, 4); const uint16_t cap_sf12 = n12->channel_cap_origin(); delete n12;
    // RELATIONAL invariants (robust to the exact airtime): SF7 > SF9 > SF12 caps; N40 < smallN
    CHECK(cap_sf7 > cap_sf9_smallN);
    CHECK(cap_sf9_smallN > cap_sf12);
    CHECK(cap_sf9_N40 < cap_sf9_smallN);
    CHECK(cap_sf9_N40 >= 1);                                   // clamp floor
    // C>=1 floor: tiny D (duty 0.0001 -> D=30 << T_ch) must NOT invert the clamp
    TestHal ht; Node* nt = new Node(ht, 1, 0xABCD);
    NodeConfig ct; ct.routing_sf = 7; ct.radio_bw_hz = 250000; ct.radio_cr = 5;
    ct.allowed_sf_bitmap = (1u << 9); ct.duty_cycle = 0.0001; ct.duty_cycle_window_ms = 3600000;
    ct.channel_active_fraction = 0.125f; nt->on_init(ct);
    CHECK(nt->channel_duty_budget_ms() == 30u);               // 0.0001*300000
    CHECK(nt->channel_cap_origin() == 1);                     // D/T_ch=0 -> C floored to 1 -> cap 1 (no inversion)
    delete nt;
}

TEST_CASE("§mobile 2a — host accepts a mobile (DISCOVER->OFFER, CLAIM registers); static mesh unaffected") {
    TestHal hal; Node host(hal, /*id=*/20, /*key=*/0xAA20);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 4;  // host_mobiles default true
    host.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(-1) };

    // (1) a mobile DISCOVER on a FOREIGN leaf (7) -> leaf-exempt -> the host emits a mobile OFFER
    std::array<uint8_t, 9> db{};   // §S6: mobile DISCOVER = 9 B (last-home block)
    size_t dn = pack_j_discover({ /*leaf_id=*/7, /*gw=*/false, /*is_mobile=*/true, /*key=*/0xB0B1u }, db);
    hal._now = 1000; host.on_recv(db.data(), dn, meta);
    CHECK(hal.count("mobile_offer_scheduled") == 1);                 // leaf-exempt worked (a foreign-leaf mobile DISCOVER is accepted)

    // (2) a NON-mobile DISCOVER on a foreign leaf -> leaf filter still applies -> NO offer (static path byte-unchanged)
    std::array<uint8_t, 6> db2{};
    size_t dn2 = pack_j_discover({ 7, false, /*is_mobile=*/false, 0xBEEFu }, db2);
    hal._now = 2000; host.on_recv(db2.data(), dn2, meta);
    CHECK(hal.count("mobile_offer_scheduled") == 1);                 // unchanged -> the non-mobile foreign DISCOVER was leaf-filtered

    // (3) a mobile CLAIM -> claim-stands: registered, NO reply; idempotent re-CLAIM keeps ONE slot
    // ★★★ [[B147]] §MH-S2b — REWRITTEN IN PLACE (B101), AND THE REWRITE MAKES THE CASE STRONGER. It used to
    // CLAIM a HAND-PICKED local id (40) that the host had never proposed. Since [[B137]] the staged OFFER holds
    // a live RESERVATION for this mobile at the id the allocator really chose, and a CLAIM must now match that
    // promise on BOTH hash and id — so a self-invented id is refused as a stale echo (`mobile_claim_stale_id`).
    // ⇒ the case now does what a real mobile does: it TRANSMITS the staged OFFER, reads the proposed id OFF THE
    // WIRE, and claims THAT. It no longer assumes the allocation, which is the point.
    host.on_timer(80 /*kMobileOfferBackoffTimerId*/);          // the staged OFFER's jitter deadline (due at 1100, now 2000)
    uint8_t offered = 0;
    for (const auto& f : hal.tx_frames) {
        auto p = parse_j(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (p && p->opcode == static_cast<uint8_t>(j_opcode::offer) && p->is_mobile) offered = p->proposed_mobile_id;
    }
    CHECK(offered >= protocol::normal_node_id_min);            // PREMISE: the OFFER really flew and named an id
    std::array<uint8_t, 11> cb{};
    size_t cn = pack_j_claim({ /*leaf_id=*/4, false, /*is_mobile=*/true, /*key=*/0xB0B1u, /*proposed=*/offered, /*lease=*/0, /*epoch=*/1, /*nonce=*/0, /*chosen_host=*/20 }, cb);
    hal._now = 3000; host.on_recv(cb.data(), cn, meta);
    CHECK(hal.count("mobile_registered") == 1);
    CHECK(host.mobile_reg_count() == 1);
    hal._now = 4000; host.on_recv(cb.data(), cn, meta);       // same key -> refresh, not a new slot
    CHECK(host.mobile_reg_count() == 1);

    // (4) STATIC regression: a non-mobile CLAIM is handled by the static path (learns the binding); _mobile_reg untouched
    std::array<uint8_t, 11> sc{};
    size_t scn = pack_j_claim({ 4, false, /*is_mobile=*/false, 0xC0C0u, /*proposed=*/50, 0, 1, 0 }, sc);
    hal._now = 5000; host.on_recv(sc.data(), scn, meta);
    CHECK(host.mobile_reg_count() == 1);                      // static CLAIM did NOT touch the mobile registry
    CHECK(hal.count("mobile_registered") == 2);               // still 2 (from the two mobile CLAIMs in step 3) — the static CLAIM added NO mobile registration

    // (5) §chosen-host fix: a mobile CLAIM addressed at a DIFFERENT host (chosen_host_id != us) -> NOT recorded.
    // We are only a flood-hearer, not the host the mobile chose — so we must not mint ourselves a host (else we'd falsely proxy).
    std::array<uint8_t, 11> fb{};
    size_t fn = pack_j_claim({ /*leaf=*/4, false, /*is_mobile=*/true, /*key=*/0xF00Du, /*proposed=*/41, 0, 1, 0, /*chosen_host=*/99 }, fb);
    hal._now = 6000; host.on_recv(fb.data(), fn, meta);
    CHECK(host.mobile_reg_count() == 1);                      // ★ NOT recorded (host 20 != chosen 99)
    CHECK(hal.count("mobile_registered") == 2);               // unchanged — no false host minted
}

TEST_CASE("§mobile 6.4 / Wave 2 S6 — a CLAIM whose local id collides a DIFFERENT hosted mobile is NOT last-write-wins: the host keeps the owner + re-OFFERs a fresh id") {
    TestHal hal; Node host(hal, /*id=*/20, /*key=*/0xAA20);
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 4;
    host.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(-1) };
    // mobile A (key 0xAAAA) claims local id 40 -> registered
    std::array<uint8_t,11> ca{}; size_t cna = pack_j_claim({ 4, false, /*is_mobile=*/true, /*key=*/0xAAAAu, /*proposed=*/40, 0, 1, 0, /*chosen_host=*/20 }, ca);
    hal._now = 1000; host.on_recv(ca.data(), cna, meta);
    CHECK(host.mobile_reg_count() == 1);
    CHECK(hal.count("mobile_registered") == 1);
    // mobile B (key 0xBBBB) claims the SAME local id 40 (the concurrent-offer race) -> collision -> targeted DENY the loser, do NOT overwrite A
    std::array<uint8_t,11> cbf{}; size_t cnb = pack_j_claim({ 4, false, /*is_mobile=*/true, /*key=*/0xBBBBu, /*proposed=*/40, 0, 1, 0, /*chosen_host=*/20 }, cbf);
    hal._now = 2000; host.on_recv(cbf.data(), cnb, meta);
    CHECK(host.mobile_reg_count() == 1);                       // ★ B NOT added at the colliding id (only A remains)
    CHECK(hal.count("mobile_registered") == 1);               // ★ B was NOT last-write-wins recorded
    CHECK(hal.count("mobile_id_collision_deny") == 1);     // ★ the host detected it + targeted-DENYed the loser (B re-registers)
}

TEST_CASE("§mobile 6.4 / Wave 2 fix — the deferred drain honors the item's plane: a GLOBAL send to a team-peer id is NOT falsely drained via _rt_team (the RREQ-storm regression)") {
    TestHal hal; Node node(hal, /*id=*/18, /*key=*/0x1818u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(node.on_init(cfg));
    node.set_team_local_id(200);
    // learn 93 as a TEAM peer (_rt_team) — there is NO static route to 93 (_rt has none)
    RxMeta meta{12.0f,-70.0f,0,static_cast<int8_t>(93)};
    uint8_t ext[8]; size_t en = pack_team_id_tlv(0xABCD1234u, std::span<uint8_t>(ext, sizeof ext));
    beacon_in tb{}; tb.leaf_id=0; tb.src=93; tb.key_hash32=0x9393u; tb.is_mobile=true; tb.ext=std::span<const uint8_t>(ext, en);
    std::array<uint8_t,64> b{}; size_t bn = pack_beacon(tb, std::span<uint8_t>(b.data(), b.size()));
    node.on_recv(b.data(), bn, meta);
    CHECK(node.is_team_peer(93));
    // a GLOBAL send to 93 -> no static route -> defers. Firing the drain must NOT match the team route (else the item is
    // drained, re-issued GLOBAL, re-deferred -> re-stamped -> never ages out = the infinite ttl=1 RREQ storm).
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 93; c.u.send.plane = 2 /*GLOBAL*/;
    c.body = reinterpret_cast<const uint8_t*>("x"); c.body_len = 1;
    node.on_command(c);
    node.on_timer(kDeferredDrainTimerId);
    CHECK(hal.count("send_drained") == 0);   // ★ a GLOBAL item is NOT drained via _rt_team (was 1 pre-fix -> the storm loop)
}

TEST_CASE("§mobile Wave 2 fix — a HOME sending to its OWN hosted mobile by hash last-miles directly (addr_len=1), NOT an H flood (the home<->mobile deadlock)") {
    TestHal hal; Node host(hal, /*id=*/155, /*key=*/0x9999u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=7;
    host.on_init(cfg);
    RxMeta meta{8.0f,-80.0f,0,static_cast<int8_t>(-1)};
    // a mobile (hash 0x30740D91) registers with THIS host, claiming local id 40
    std::array<uint8_t,11> cb{}; size_t cn = pack_j_claim({ 7, false, /*is_mobile=*/true, /*key=*/0x30740D91u, /*proposed=*/40, 0, 1, 0, /*chosen_host=*/155 }, cb);
    host.on_recv(cb.data(), cn, meta);
    CHECK(host.mobile_reg_count() == 1);
    // the home originates a DM to the hosted mobile BY HASH -> direct last-mile (RTS next=40, addr_len=1), NO H flood
    hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = 0; c.u.send.dst_hash = 0x30740D91u; c.u.send.plane = 2 /*GLOBAL*/;
    c.body = reinterpret_cast<const uint8_t*>("to m"); c.body_len = 4;
    host.on_command(c);
    bool got_rts=false;
    for (auto& f : hal.tx_frames) { auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (pr && pr->next == 40) { got_rts=true; CHECK(pr->addr_len == 1); } }
    CHECK(got_rts);                          // ★ direct last-mile to the hosted mobile's local id 40
    CHECK(hal.count("h_tx") == 0);           // ★ NO H flood (no home<->mobile deadlock)
}

TEST_CASE("§mobile 6.4 / Wave 2 + step 2 — reqpubkey -t emits a TEAM-scoped WANT_PUBKEY with origin=team_local_id (answer routes back via _rt_team); plain reqpubkey (GLOBAL) from an UNREGISTERED mobile is SUPPRESSED (no return path -> fail loud, no flood)") {
    TestHal hal;
    uint8_t seedM1[32], seedM2[32];
    for (int i=0;i<32;++i){ seedM1[i]=uint8_t(i+1); seedM2[i]=uint8_t(200-i); }
    Identity idM1{}, idM2{}; identity_from_seed(idM1, seedM1); identity_from_seed(idM2, seedM2);
    Node m1(hal, /*id=*/17, idM1.key_hash32);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=0xABCD1234u;
    CHECK(m1.on_init(cfg));
    m1.set_crypto_identity(idM1.x_secret, idM1.ed_pub);
    m1.set_team_local_id(93);
    // reqpubkey <M2 hash> -t -> team-scoped WANT_PUBKEY, origin = OUR team id 93 (NOT node_id 17)
    hal.tx_frames.clear();
    Command c{}; c.kind = CmdKind::reqpubkey; c.u.resolve.dst_hash = idM2.key_hash32; c.u.resolve.dst_id = 0; c.u.resolve.plane = 1 /*TEAM*/;
    m1.on_command(c);
    bool got_h=false;
    for (auto& f : hal.tx_frames) if ((f.bytes[0] >> 4) == 0x7) {
        auto ph = parse_h(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        if (ph) { got_h=true; CHECK(ph->team_scoped); CHECK(ph->want_pubkey); CHECK(ph->origin == 93); }
    }
    CHECK(got_h);
    // control / §mobile step 2: plain reqpubkey (GLOBAL) from this UNREGISTERED mobile has NO return path — origin would be
    // our LOCAL node_id 17 (not team-scoped), which the owner cannot route back to (an F/RREQ for a local id can even resolve
    // a WRONG static node). So we FAIL LOUD and do NOT flood. (A REGISTERED mobile would instead stamp origin=home_id.)
    hal.tx_frames.clear();
    Command cg{}; cg.kind = CmdKind::reqpubkey; cg.u.resolve.dst_hash = idM2.key_hash32; cg.u.resolve.plane = 2 /*GLOBAL*/;
    m1.on_command(cg);
    bool got_global_h=false;
    for (auto& f : hal.tx_frames) if ((f.bytes[0] >> 4) == 0x7) got_global_h=true;
    CHECK_FALSE(got_global_h);   // ★ step 2: the unroutable-origin WANT_PUBKEY is suppressed, not flooded
}

TEST_CASE("§1.3 — effective_name defaults to 'MeshRoute node: 0x<hash>' (the STABLE hash) when empty; returns the set/ctor name otherwise") {
    TestHal hal; Node node(hal, /*id=*/17, /*key=*/0xDEADBEEFu);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; CHECK(node.on_init(cfg));
    char buf[40];
    uint8_t n = node.effective_name(buf, sizeof buf);
    CHECK(std::string(buf, n) == "MeshRoute node: 0xDEADBEEF");   // ★ empty -> hash default (uppercase 8-hex)
    node.set_name("Alice", 5);
    n = node.effective_name(buf, sizeof buf);
    CHECK(std::string(buf, n) == "Alice");
    Node named(hal, 3, 0x11u, "Bob");                            // ★ a ctor/sim name is kept (was discarded)
    n = named.effective_name(buf, sizeof buf);
    CHECK(std::string(buf, n) == "Bob");
}

TEST_CASE("§1.3 — peer_key_set caches a peer's name and REFRESHES it on every call (immutable key, MUTABLE name); peer_name_find reads it") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0x1234u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; CHECK(node.on_init(cfg));
    uint8_t ed[32] = {}; ed[0]=0xEF; ed[1]=0xBE; ed[2]=0xAD; ed[3]=0xDE; for (int i=4;i<32;++i) ed[i]=static_cast<uint8_t>(i);  // ed[:4] LE = 0xDEADBEEF
    const uint32_t h = 0xDEADBEEFu;
    char nm[32];
    CHECK(node.peer_key_set(h, ed, Node::PeerKeyConf::authoritative, "Alice", 5));
    CHECK(std::string(nm, node.peer_name_find(h, nm, sizeof nm)) == "Alice");
    CHECK(node.peer_key_set(h, ed, Node::PeerKeyConf::authoritative, "Alice-2", 7));   // same key, NEW name -> refreshed
    CHECK(std::string(nm, node.peer_name_find(h, nm, sizeof nm)) == "Alice-2");
    CHECK(node.peer_name_find(0x99u, nm, sizeof nm) == 0);                              // unknown hash -> 0
}

TEST_CASE("§mobile 2b — mobile FSM: DISCOVER, collect OFFERs, CLAIM the strongest, adopt; static never arms") {
    constexpr uint32_t kMobDisc = 74, kMobGuard = 75;        // mirror node.h's kMobileDiscover/ClaimGuardTimerId
    TestHal hal; Node mob(hal, /*id=*/0, /*key=*/0x7777);   // unprovisioned mobile
    NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 4; cfg.is_mobile = true;
    mob.on_init(cfg);
    RxMeta m5{ 5.0f, -80.0f, 0, static_cast<int8_t>(-1) }, m9{ 9.0f, -70.0f, 0, static_cast<int8_t>(-1) };

    // (1) DISCOVER fires (a DISCOVER frame goes out)
    hal._now = 1000; mob.on_timer(kMobDisc);
    CHECK(hal.count("mobile_discover_tx") == 1);

    // (2) collect two OFFERs (from a FOREIGN leaf 7 -> the mobile-OFFER leaf-exemption lets them in)
    auto feed_offer = [&](uint8_t resp, uint32_t rk, uint8_t local, RxMeta& meta) {
        j_offer_in o{}; o.leaf_id=7; o.is_mobile=true; o.responder_node_id=resp; o.responder_key_hash32=rk;
        o.data_sf_bitmap=0x06; o.proposed_mobile_id=local;
        o.target_key_hash32 = mob.key_hash32(); uint8_t buf[13]; size_t n = pack_j_offer(o, buf); mob.on_recv(buf, n, meta);
    };
    feed_offer(30, 0x3030u, 100, m5);
    feed_offer(31, 0x3131u, 101, m9);                       // stronger SNR
    CHECK(mob.mobile_offers_n() == 2);

    // (3) the guard fires -> CLAIM the STRONGEST (local 101 from responder 31) + adopt
    hal._now = 4000; mob.on_timer(kMobGuard);
    CHECK(hal.count("mobile_adopted") == 1);
    CHECK(mob.node_id() == 101);                            // adopted the stronger offer's local-id
    CHECK(mob.mobile_home_id() == 31);                      // homed to the stronger responder

    // (4) no-host: a fresh mobile with no OFFERs -> no adopt, re-arms (backoff)
    TestHal h2; Node mob2(h2, 0, 0x8888u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; c2.is_mobile=true; mob2.on_init(c2);
    h2._now=1000; mob2.on_timer(kMobDisc); h2._now=4000; mob2.on_timer(kMobGuard);
    CHECK(mob2.mobile_home_id() == 0);                      // not adopted (no host)
    CHECK(h2.count("mobile_no_host") == 1);

    // (5) STATIC: a non-mobile node never arms the FSM -> on_timer(kMobDisc) is a no-op
    TestHal h3; Node stat(h3, 20, 0xAA20u);
    NodeConfig c3; c3.routing_sf=8; c3.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c3.leaf_id=4; c3.is_mobile=false; stat.on_init(c3);
    h3._now=1000; stat.on_timer(kMobDisc); stat.on_timer(kMobGuard);
    CHECK(h3.count("mobile_discover_tx") == 0);             // a static node never DISCOVERs
    CHECK(stat.mobile_home_id() == 0);
}

TEST_CASE("§S6 — a registered mobile re-registers on a TARGETED DENY (concurrent-offer collision recovery); a foreign DENY is ignored") {
    constexpr uint32_t kMobDisc = 74, kMobGuard = 75;
    TestHal hal; Node mob(hal, /*id=*/0, /*key=*/0x7777);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4; cfg.is_mobile=true;
    mob.on_init(cfg);
    RxMeta m9{ 9.0f, -70.0f, 0, static_cast<int8_t>(-1) };
    // register: DISCOVER -> a TARGETED OFFER (local 101, home 31) -> guard -> adopt
    hal._now = 1000; mob.on_timer(kMobDisc);
    { j_offer_in o{}; o.leaf_id=7; o.is_mobile=true; o.responder_node_id=31; o.responder_key_hash32=0x3131u;
      o.data_sf_bitmap=0x06; o.proposed_mobile_id=101; o.target_key_hash32=mob.key_hash32();
      uint8_t buf[13]; size_t n = pack_j_offer(o, buf); mob.on_recv(buf, n, m9); }
    hal._now = 4000; mob.on_timer(kMobGuard);
    CHECK(mob.mobile_registered());
    CHECK(mob.node_id() == 101);
    const uint32_t disc_before = hal.count("mobile_discover_tx");

    // (A) a FOREIGN DENY (claimant != our key) for our id -> IGNORED (only the LOSER, addressed by its hash, yields — the
    //     recorded mobile must NOT re-register off a DENY meant for someone else)
    { j_deny_in d{}; d.leaf_id=7; d.denied_node_id=101; d.owner_key_hash32=0x9999u; d.claimant_key_hash32=0xBEEFu; d.reason=1;
      uint8_t buf[15]; size_t n = pack_j_deny(d, buf); mob.on_recv(buf, n, m9); }
    CHECK(mob.mobile_registered());                          // untouched
    CHECK(hal.count("mobile_id_denied") == 0);

    // (B) our TARGETED DENY (claimant == our key) for our adopted id -> re-register (reset -> unprovisioned + re-DISCOVER)
    hal._now = 5000;
    { j_deny_in d{}; d.leaf_id=7; d.denied_node_id=101; d.owner_key_hash32=0x9999u; d.claimant_key_hash32=mob.key_hash32(); d.reason=1;
      uint8_t buf[15]; size_t n = pack_j_deny(d, buf); mob.on_recv(buf, n, m9); }
    CHECK(hal.count("mobile_id_denied") == 1);               // ★ the mobile-side S6 branch fired
    CHECK_FALSE(mob.mobile_registered());                    // ★ registration reset (the host never recorded us)
    CHECK(mob.node_id() == 0);                               // ★ unprovisioned (a re-CLAIM follows)
    mob.on_timer(kMobDisc);                                  // the DENY armed a re-DISCOVER (@0); fire it
    CHECK(hal.count("mobile_discover_tx") == disc_before + 1);  // ★ a fresh registration cycle begins (-> a distinct id, 101 now taken)
}

TEST_CASE("§mobile 3a — host H-query proxy: answers for a hosted mobile; a non-host does not") {
    TestHal hal; Node host(hal, /*id=*/20, /*key=*/0xAA20u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4;
    host.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(-1) };
    // register a mobile (a mobile CLAIM -> _mobile_reg[0xB0B1 -> local 40])
    std::array<uint8_t, 11> cb{};
    size_t cn = pack_j_claim({ /*leaf_id=*/4, false, /*is_mobile=*/true, /*key=*/0xB0B1u, /*proposed=*/40, 0, 1, 0, /*chosen_host=*/20 }, cb);
    hal._now = 1000; host.on_recv(cb.data(), cn, meta);
    CHECK(host.mobile_reg_count() == 1);

    // feed a SOFT H-query for the mobile's hash -> the host PROXY-answers (the mobile's own beacon id_bind is skipped, 2b)
    std::array<uint8_t, 8> hb{};
    size_t hn = pack_h({ /*leaf_id=*/4, /*origin=*/30, /*key=*/0xB0B1u, /*ttl=*/3, /*hard=*/false }, hb);
    hal._now = 2000; host.on_recv(hb.data(), hn, meta);
    CHECK(hal.count("h_resolved") == 1);      // the host answered as a proxy for its hosted mobile

    // control: a node hosting NO mobile cannot resolve the mobile's hash -> no answer (byte-identical)
    TestHal h2; Node host2(h2, 21, 0xBB21u);
    NodeConfig c2; c2.routing_sf=8; c2.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); c2.leaf_id=4; host2.on_init(c2);
    h2._now=2000; host2.on_recv(hb.data(), hn, meta);
    CHECK(h2.count("h_resolved") == 0);       // _mobile_reg_n==0 -> no proxy
}

TEST_CASE("§mobile 3b A1 — a mobile_src RTS's local-id stays OUT of the global rt (the collision fix); a normal RTS learns") {
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=8; cfg.allowed_sf_bitmap=static_cast<uint16_t>(1u<<8); cfg.leaf_id=4;
    node.on_init(cfg);
    RxMeta meta{ 9.0f, -70.0f, 0, static_cast<int8_t>(-1) };
    auto feed_rts = [&](uint8_t src, bool mobile_src) {
        rts_in r{}; r.leaf_id=4; r.src=src; r.next=99; r.ctr_lo=1; r.dst=99; r.sf_index=0; r.rts_flags=0; r.payload_len=1;
        r.mobile_src=mobile_src; r.id=rts_flight_identity_plain(src, 1);   // §hybrid-rts S1
        uint8_t b[11]; size_t n = pack_rts(r, b); node.on_recv(b, n, meta);
    };
    // a NORMAL RTS from src 50 -> learned as a 1-hop neighbour (rt grows)
    const uint8_t rc0 = node.rt_count();
    hal._now=1000; feed_rts(/*src*/ 50, /*mobile_src*/ false);
    CHECK(node.rt_count() == rc0 + 1);        // learned
    // a MOBILE_SRC RTS from src 51 (a LOCAL id) -> NOT learned (A1: stays out of the global rt)
    const uint8_t rc1 = node.rt_count();
    hal._now=2000; feed_rts(/*src*/ 51, /*mobile_src*/ true);
    CHECK(node.rt_count() == rc1);            // ★ NOT learned -> a mobile's local-id can't collide the global rt
}

// ==================== §team-parity T6 (spec §3/T6, OWNER-RULED §11 2026-07-28) ==================================
// ONE ORIGIN NAMESPACE PER PLANE. Part A's corpus coverage is real but NARROW — only s28/s29 move (they are the only
// two scenarios with a HOMED team member), and s37 covers the R3/ack consequence. These cases pin the four per-plane
// DECISIONS themselves, including the two that no scenario exercises: the GLOBAL §18 carve-out and the not-team-DAD'd
// fallback. Part B's channel-ledger half is pinned in test_node_channel.cpp; its H-flood half is at the end of this block.

namespace {
// A HOMED (dual) team member — the configuration the whole slice exists for: a host-assigned static _node_id, a
// SEPARATE team_local_id, and an ACTIVE registration, so stamp_origin's pre-T6 expression yields the HOME's static id.
void t6_homed(Node& n, uint8_t static_id, uint8_t team_id_local, uint8_t home, uint32_t team) {
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=team;
    CHECK(n.on_init(cfg));
    n.set_team_local_id(team_id_local);
    n.test_set_my_mobile_reg(home, static_id);   // §S7 T-B seam: ACTIVE registration -> stamp_origin's `mob` is true
    CHECK(n.mobile_registered());
    CHECK(n.node_id() != team_id_local);         // the DUAL split is real: a host-assigned static id != the team id
}
CmdResult t6_send(Node& n, uint8_t dst, Plane plane) {
    Command c{}; c.kind = CmdKind::send; c.u.send.dst_id = dst;
    c.u.send.plane = static_cast<uint8_t>(plane);
    c.body = reinterpret_cast<const uint8_t*>("hi"); c.body_len = 2;
    return n.on_command(c);
}
}  // namespace

TEST_CASE("§team-parity T6/A — a HOMED member's TEAM-plane send stamps its team_local_id, NOT its home's static id") {
    // ★★ THE MEASURED DEFECT (BASELINE SCEN note / spec §11): pre-T6 this stamped origin = 101, the HOME's STATIC node
    // id, so the acking teammate addressed 101 on the STATIC plane — four static RREQ floods then e2e_ack_timeout in the
    // bench's own topology. s37 measures that end-to-end; this pins the stamp itself.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/254, /*key=*/0xA0D2u);
    t6_homed(X, /*static_id=*/254, /*team_id_local=*/210, /*home=*/101, TEAM);
    X.test_suspend_tx_drain(true);                                  // keep the DM in the queue so the stamp is readable
    X.test_learn_route(/*dest=*/220, /*via=*/220, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/true);
    CHECK(X.is_team_peer(220));
    CHECK(t6_send(X, /*dst=*/220, Plane::TEAM).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 1);
    CHECK(X.test_tx_origin(0) == 210);                              // ★★ the TEAM id — pre-T6 this read 101
}

TEST_CASE("§team-parity T6/A — the STATIC REDUCTION: the same member's GLOBAL send still bills its home (unchanged)") {
    // The other half of the ruling, and the reason the change is not `origin = team_local_id` unconditionally: a
    // registered mobile bills its home on the static plane because a home id is an accountable GLOBAL id, and the
    // reverse leg is last-miled by the home. That must survive T6 verbatim.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/254, /*key=*/0xA0D2u);
    t6_homed(X, /*static_id=*/254, /*team_id_local=*/210, /*home=*/101, TEAM);
    X.test_suspend_tx_drain(true);
    X.test_learn_route(/*dest=*/40, /*via=*/40, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/false);
    CHECK(t6_send(X, /*dst=*/40, Plane::GLOBAL).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 1);
    CHECK(X.test_tx_origin(0) == 101);                              // the HOME's id — the pre-T6 expression, verbatim
}

TEST_CASE("§team-parity T6/A — ★ THE §18 CARVE-OUT: a GLOBAL send to an id that COLLIDES a teammate's team id bills the home") {
    // ★ This is why the predicate is rt_find's exact dispatch and NOT `plane != GLOBAL`. Team local ids and static node
    // ids share 1..254, so a GLOBAL send can legitimately target a numeric id that is ALSO a known teammate's team id.
    // rt_find(dst, GLOBAL) forces _rt, so the flight is STATIC and its origin must be the accountable global id — a team
    // stamp here would put a team-plane identity on a static-plane frame, which is the mirror of the bug T6 fixes.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/254, /*key=*/0xA0D2u);
    t6_homed(X, /*static_id=*/254, /*team_id_local=*/210, /*home=*/101, TEAM);
    X.test_suspend_tx_drain(true);
    X.test_learn_route(/*dest=*/60, /*via=*/60, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/true);   // 60 is a TEAMMATE
    X.test_learn_route(/*dest=*/60, /*via=*/60, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/false);  // ...and a static id
    CHECK(X.is_team_peer(60));
    CHECK(t6_send(X, /*dst=*/60, Plane::GLOBAL).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 1);
    CHECK(X.test_tx_origin(0) == 101);                              // ★★ GLOBAL wins: the home id, not the team id
    // ...while the SAME dst on AUTO resolves to the teammate and therefore takes the team stamp.
    CHECK(t6_send(X, /*dst=*/60, Plane::AUTO).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 2);
    CHECK(X.test_tx_origin(1) == 210);                              // ★★ AUTO dispatches to _rt_team ⇒ the team id
}

TEST_CASE("§team-parity T6/A — an AUTO send to a KNOWN teammate takes the team stamp (the brief's literal form would have missed it)") {
    // ★ REPORTED DEVIATION, pinned here so the decision is visible in code, not only in a report. §3/T6's code sketch
    // keys on `plane == Plane::TEAM` alone. That would leave every AUTO-dispatched team DM — which is what
    // `send_hash <teammate>` without `-t` produces, i.e. s24/s25/s26's team traffic and the companion's default —
    // stamping the home id while rt_find(dst, AUTO) routes it on _rt_team. The origin would then name a node on the
    // OTHER plane from the one carrying the frame, which is exactly the defect §11 ruled against, and node_mac.cpp:70's
    // E2E-ACK gate admits precisely this case via its own `is_team_peer(dst)` arm.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/254, /*key=*/0xA0D2u);
    t6_homed(X, /*static_id=*/254, /*team_id_local=*/210, /*home=*/101, TEAM);
    X.test_suspend_tx_drain(true);
    X.test_learn_route(/*dest=*/220, /*via=*/220, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/true);
    CHECK(t6_send(X, /*dst=*/220, Plane::AUTO).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 1);
    CHECK(X.test_tx_origin(0) == 210);
    // Control: an AUTO send to an id that is NOT a teammate keeps the home stamp (AUTO is not "always team").
    X.test_learn_route(/*dest=*/41, /*via=*/41, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/false);
    CHECK_FALSE(X.is_team_peer(41));
    CHECK(t6_send(X, /*dst=*/41, Plane::AUTO).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 2);
    CHECK(X.test_tx_origin(1) == 101);
}

TEST_CASE("§team-parity T6/A — a member whose team-DAD has NOT completed falls back to the pre-T6 stamp (never origin 0)") {
    // ⚠ The hard precondition in stamp_origin. A member can hold _team_peer bits with team_local_id() still 0:
    // node_beacon.cpp's same-team learn does not require our OWN id to be adopted. Stamping 0 there would air the
    // reserved sentinel id. `send -t` is already refused loud in that window (node.cpp:1138); an AUTO send is not, and
    // this pins what it does instead. (Marked MISSING in-source: refusing it too is a separate, louder change.)
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/254, /*key=*/0xA0D2u);
    t6_homed(X, /*static_id=*/254, /*team_id_local=*/210, /*home=*/101, TEAM);
    X.test_learn_route(/*dest=*/220, /*via=*/220, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/true);
    X.set_team_local_id(0);                                         // DAD dropped (a team switch / not yet adopted)
    CHECK(X.is_team_peer(220));                                     // the peer bit SURVIVES — that is the trap
    X.test_suspend_tx_drain(true);
    CHECK(t6_send(X, /*dst=*/220, Plane::AUTO).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 1);
    CHECK(X.test_tx_origin(0) == 101);                              // the pre-T6 expression, NOT 0
    CHECK(X.test_tx_origin(0) != 0);
}

TEST_CASE("§team-parity T6/A — an OFF-GRID member is unchanged by the slice (node_id == team_local_id already)") {
    // Why s35a/s35b did not move: off-grid, team_dad_fire calls set_identity(team_local_id) (node_mobile.cpp:217), so
    // the two ids are the same value and both arms of the ternary agree. This is the algebra behind "0 of 34 off-grid
    // scenarios moved", pinned so a future edit cannot break it silently.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(X, 213, TEAM);
    X.test_suspend_tx_drain(true);
    X.test_learn_route(/*dest=*/234, /*via=*/234, /*hops=*/1, /*snr_q4=*/160, /*team_plane=*/true);
    CHECK(t6_send(X, /*dst=*/234, Plane::TEAM).code == CmdCode::queued);
    CHECK(X.test_tx_queue_n() == 1);
    CHECK(X.test_tx_origin(0) == 213);                              // == node_id == team_local_id: identical either way
}

TEST_CASE("§team-parity T6/B — the H-flood dedup ring is PLANE-KEYED: a team H and a static H sharing (origin, key_hash32) no longer suppress each other") {
    // ★★ THE ONLY DETECTOR for this half of Part B: all 35 corpus streams are byte-identical through it (measured
    // 0/34 on the pre-s37 corpus), because no scenario puts a node on both H planes at once.
    // ★ THE CONFIGURATION IS REACHABLE BY LIVE CONFIG, which is what makes the fix warranted rather than speculative:
    // handle_h returns before any mark for a static H iff _cfg.is_mobile (node_hashlocate.cpp:584) and for a team H iff
    // !same_team (:603), so a node with team_id != 0 && !is_mobile processes BOTH — and set_team_id (node.cpp:413)
    // never touches is_mobile, so `cfg set team_id <x>` / `team <id>` on the console produces exactly that node.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node N(hal, /*id=*/30, /*key=*/0x3030u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.team_id=TEAM;   // NOT is_mobile
    CHECK(N.on_init(cfg));
    N.set_team_local_id(93);
    const uint32_t UNKNOWN_KEY = 0xDEADBEEFu;    // resolves nowhere -> handle_h takes the FORWARD (dedup) path
    auto feed_h = [&](bool team_scoped) {
        h_in in{}; in.leaf_id = 0; in.origin = 77; in.query_key32 = UNKNOWN_KEY; in.ttl = 3;
        in.team_scoped = team_scoped; in.team_id = team_scoped ? TEAM : 0u;
        uint8_t buf[8 + 32 + 4 + 1 + 32];
        const size_t n = pack_h(in, std::span<uint8_t>(buf, sizeof buf));
        CHECK(n > 0);
        N.on_recv(buf, n, RxMeta{12.0f, -70.0f, 0, /*src_hint=*/-1});
    };
    feed_h(/*team_scoped=*/false);                       // a STATIC locate for key K from origin 77
    CHECK(hal.count("h_forward") == 1);
    feed_h(/*team_scoped=*/true);                        // a TEAM locate, SAME (origin, key_hash32) — a §18 collision
    CHECK(hal.count("h_forward") == 2);                  // ★★ forwarded: a different plane is a different flood
    // Symmetry + non-vacuity: the dedup itself still works WITHIN each plane (this is a plane split, not a disable).
    feed_h(/*team_scoped=*/true);  CHECK(hal.count("h_forward") == 2);
    feed_h(/*team_scoped=*/false); CHECK(hal.count("h_forward") == 2);
}

// ---- §team-parity T5 (spec 2026-07-27 §3/T5) — the TEAM BIDI PLANE -----------------------------------------------
// The team mirror of the static _link_bidi/_link_bidi_confirmed_ms, living in the _team_liveness slot's
// team_bidi_state / team_bidi_confirmed_s (node.h's PeerLiveness note). These cases are the ONLY detector for the
// per-plane RESOLUTION (which table a given id resolves against) — the corpus proves the feeds FIRE and that the
// static plane does not move, but no scenario puts the same numeric id on both planes at once, which is the §18
// collision the plane split exists for.

TEST_CASE("§team-parity T5 — a TEAM confirm writes TEAM state and NEVER the static _link_bidi (I2/I8), with the static arm as the same-site control") {
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg));
    X.set_team_local_id(93);                                  // DUAL member: node_id 17, team id 93
    hal._now = 500000;
    // (a) the TEAM arm — this is the write T2 marked ✖ twice ("there is nothing to write it to").
    CHECK(X.team_link_bidi_state(210) == LinkBidi::unknown);
    X.note_link_confirmed(210, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::confirmed);
    CHECK(X.team_link_bidi_confirmed_s(210) == 500u);          // SECONDS, not ms
    // ★★ THE ISOLATION ASSERTION: the static node_id-indexed array is untouched at that index.
    CHECK(X.link_bidi_state(210) == LinkBidi::unknown);
    CHECK(X.bidi_penalty_q4(210, /*team_plane=*/false) == 0);
    // (b) SAME-SITE CONTROL: the static arm still writes _link_bidi and leaves the team slot alone. Without this the
    // test above would also pass if note_link_confirmed had simply stopped writing anything.
    X.note_link_confirmed(211, /*team_plane=*/false);
    CHECK(X.link_bidi_state(211) == LinkBidi::confirmed);
    CHECK(X.link_bidi_confirmed_ms(211) == 500000u);
    CHECK(X.team_link_bidi_state(211) == LinkBidi::unknown);
    // (c) a re-confirm refreshes the second stamp (mirrors the static case's ms refresh).
    hal._now = 700000;
    X.note_link_confirmed(210, /*team_plane=*/true);
    CHECK(X.team_link_bidi_confirmed_s(210) == 700u);
}

TEST_CASE("§team-parity T5 — bidi_penalty_q4 resolves PER PLANE: one id, two verdicts (the §18 collision)") {
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg)); X.set_team_local_id(93);
    // id 60 is one_way as a TEAMMATE and confirmed as a STATIC neighbour — the two planes must disagree.
    X.set_team_link_bidi_for_test(60, LinkBidi::one_way);
    X.set_link_bidi_for_test(60, LinkBidi::confirmed);
    CHECK(X.bidi_penalty_q4(60, /*team_plane=*/true)  == protocol::bidi_penalty_one_way_q4);
    CHECK(X.bidi_penalty_q4(60, /*team_plane=*/false) == 0);
    // ...and the mirror image on another id, so neither answer is a constant.
    X.set_team_link_bidi_for_test(61, LinkBidi::confirmed);
    X.set_link_bidi_for_test(61, LinkBidi::one_way);
    CHECK(X.bidi_penalty_q4(61, /*team_plane=*/true)  == 0);
    CHECK(X.bidi_penalty_q4(61, /*team_plane=*/false) == protocol::bidi_penalty_one_way_q4);
    // An id with NO team slot is `unknown`, not penalized (OI2: only positively-confirmed one_way demotes).
    CHECK(X.team_link_bidi_state(62) == LinkBidi::unknown);
    CHECK(X.bidi_penalty_q4(62, /*team_plane=*/true) == 0);
    // Our OWN team id is never penalized even if a slot says one_way (mirrors liveness_penalty_q4's :89 carve-out,
    // which matters on a dual member where team_local_id != _node_id so the generic self-test misses it).
    X.set_team_link_bidi_for_test(93, LinkBidi::one_way);
    CHECK(X.bidi_penalty_q4(93, /*team_plane=*/true) == 0);
}

TEST_CASE("§team-parity T5 — candidate_degraded ORs the LOCAL team verdict in, per plane") {
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg)); X.set_team_local_id(93);
    RtCandidate c{}; c.next_hop = 60; c.hops = 1; c.degraded_from_wire = false;
    CHECK_FALSE(X.candidate_degraded(c, /*team_plane=*/true));    // clean wire + unknown team bidi
    X.set_team_link_bidi_for_test(60, LinkBidi::one_way);
    CHECK(X.candidate_degraded(c, /*team_plane=*/true));          // ★ pre-T5 this returned false (wire-only)
    CHECK_FALSE(X.candidate_degraded(c, /*team_plane=*/false));   // same-site control: the STATIC verdict is separate
    // The wire component still stands alone on either plane (MF5/OI1: it is an OR, never replaced).
    RtCandidate w{}; w.next_hop = 70; w.hops = 2; w.degraded_from_wire = true;
    CHECK(X.candidate_degraded(w, /*team_plane=*/true));
    CHECK(X.candidate_degraded(w, /*team_plane=*/false));
}

TEST_CASE("§team-parity T5 — the heard-set scan keys on the TEAM self-id, and our STATIC node_id does NOT confirm a team link") {
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg)); X.set_team_local_id(93);               // node_id 17, team id 93 — deliberately DIFFERENT
    // (a) PRESENT at hops==1 as our TEAM id -> the teammate hears us -> confirmed.
    beacon_entry present[1] = {}; present[0].dest = 93; present[0].next = 93; present[0].hops = 1;
    X.test_update_link_bidi_from_beacon(/*advertiser=*/210, present, 1, /*complete=*/true, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::confirmed);
    CHECK(X.link_bidi_state(210) == LinkBidi::unknown);           // I8 again: nothing in the static array
    // (b) ★★ THE SELF-ID SELECTION IS LOAD-BEARING: a page that lists our STATIC node_id (17) at hops==1 must NOT
    // confirm on the team plane — a teammate's page advertises TEAM ids, so a 17 there is somebody else entirely.
    // With a COMPLETE page the correct verdict is one_way, not confirmed.
    beacon_entry wrong_ns[1] = {}; wrong_ns[0].dest = 17; wrong_ns[0].next = 17; wrong_ns[0].hops = 1;
    X.test_update_link_bidi_from_beacon(/*advertiser=*/211, wrong_ns, 1, /*complete=*/true, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(211) == LinkBidi::one_way);
    // (c) ABSENT + INCOMPLETE page -> no change (absence is not authoritative). This is what T3's heard_set_complete
    // on team beacons made meaningful, and T5 is its consumer.
    X.test_update_link_bidi_from_beacon(/*advertiser=*/212, wrong_ns, 1, /*complete=*/false, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(212) == LinkBidi::unknown);
    // (d) hops must be 1: a 2-hop self-entry is not proof of a DIRECT link (same rule as the static scan).
    beacon_entry two_hop[1] = {}; two_hop[0].dest = 93; two_hop[0].next = 210; two_hop[0].hops = 2;
    X.test_update_link_bidi_from_beacon(/*advertiser=*/213, two_hop, 1, /*complete=*/true, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(213) == LinkBidi::one_way);
    // (e) one_way -> a later PRESENT entry recovers it to confirmed (the §7 recovery signal, team side).
    X.test_update_link_bidi_from_beacon(/*advertiser=*/211, present, 1, /*complete=*/true, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(211) == LinkBidi::confirmed);
    CHECK(hal.count("link_recover") >= 1);
}

TEST_CASE("§team-parity T5 — a member with NO team-DAD'd id records NOTHING from a team heard-set (C2: refuse, don't guess)") {
    // team_local_id()==0 is the reserved sentinel, so neither "present" nor "absent" carries information: a dest==0 row
    // would false-confirm, and absence of an id we do not have cannot mean the teammate ignores us.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg));
    CHECK(X.team_local_id() == 0);                               // DAD not yet complete
    beacon_entry zero_row[1] = {}; zero_row[0].dest = 0; zero_row[0].next = 0; zero_row[0].hops = 1;
    X.test_update_link_bidi_from_beacon(/*advertiser=*/210, zero_row, 1, /*complete=*/true, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::unknown);      // no confirm, and no one_way either
    CHECK(X.link_bidi_state(210) == LinkBidi::unknown);
    // Same-site control: once DAD completes, the identical call DOES record (so the refusal is the id, not the wiring).
    X.set_team_local_id(93);
    X.test_update_link_bidi_from_beacon(/*advertiser=*/210, zero_row, 1, /*complete=*/true, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::one_way);
}

TEST_CASE("§team-parity T5 — a liveness recovery does NOT clear the team bidi verdict (alive != hears-us)") {
    // clear_liveness_tiers deliberately omits the bidi fields: hearing a frame FROM a teammate proves it is alive, not
    // that it hears US. If a future edit adds them to that clear, this fires.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg)); X.set_team_local_id(93);
    X.set_team_link_bidi_for_test(210, LinkBidi::one_way);
    X.record_peer_rts_timeout(210, /*ctr_lo=*/1, /*team_plane=*/true);
    X.record_peer_rts_timeout(210, /*ctr_lo=*/2, /*team_plane=*/true);
    CHECK(X.test_team_penalty_q4(210) > 0);                       // a liveness tier is live
    X.clear_peer_suspect(210, "team_rx", /*team_plane=*/true);     // recovery-on-heard
    CHECK(X.test_team_penalty_q4(210) == 0);                      // ...clears the liveness tier
    CHECK(X.team_link_bidi_state(210) == LinkBidi::one_way);       // ★ but NOT the bidi verdict
    CHECK(X.bidi_penalty_q4(210, /*team_plane=*/true) == protocol::bidi_penalty_one_way_q4);
}

TEST_CASE("§team-parity T5 — decay_link_bidi's TEAM arm: confirmed -> unknown past TTL, NEVER -> one_way (MF6)") {
    // ✖ This arm has NO wired caller, exactly like the static one (nothing treats confirmed differently from unknown),
    // so this case is its ONLY detector — corpus-dark BY CONSTRUCTION, stated rather than left to look like coverage.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node X(hal, /*id=*/17, /*key=*/0x1717u);
    NodeConfig cfg; cfg.routing_sf=7; cfg.allowed_sf_bitmap=(1u<<7); cfg.leaf_id=0; cfg.is_mobile=true; cfg.team_id=TEAM;
    CHECK(X.on_init(cfg)); X.set_team_local_id(93);
    hal._now = 1000;
    X.note_link_confirmed(210, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::confirmed);
    hal._now = 1000 + protocol::bidi_confirm_ttl_ms - 1000;        // one second short of the TTL
    X.decay_link_bidi(210, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::confirmed);
    hal._now = 1000 + protocol::bidi_confirm_ttl_ms;
    X.decay_link_bidi(210, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(210) == LinkBidi::unknown);        // decayed
    // MF6: a one_way verdict is NEVER touched by decay (it took positive evidence to set; only evidence clears it).
    X.set_team_link_bidi_for_test(211, LinkBidi::one_way);
    hal._now += protocol::bidi_confirm_ttl_ms * 4;
    X.decay_link_bidi(211, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(211) == LinkBidi::one_way);
    // An id with no slot at all is a safe no-op (the static array always exists; the team table may not have the row).
    X.decay_link_bidi(212, /*team_plane=*/true);
    CHECK(X.team_link_bidi_state(212) == LinkBidi::unknown);
}

TEST_CASE("§team-parity T5 — a CTS on a TEAM flight confirms the team link; the static _link_bidi stays clean") {
    // The end-to-end feed (spec §3/T2 row 3's SECOND half, the ✖ T2 left for T5). Drives the real handle_cts arm.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node A(hal, /*id=*/213, /*key=*/0xA213u); t1_offgrid(A, 213, TEAM);
    std::array<uint8_t,64> bb{};
    const size_t bn = t1_team_beacon(/*src=*/234, TEAM, bb);
    A.on_recv(bb.data(), bn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(234)});
    CHECK(A.is_team_peer(234));
    CHECK(A.team_link_bidi_state(234) == LinkBidi::unknown);        // a beacon alone is not bidi proof
    CHECK(t6_send(A, /*dst=*/234, Plane::TEAM).code == CmdCode::queued);
    // The RTS must have flown and named 234 as the next hop (else the CTS below could not match this flight).
    uint8_t rts_next = 0;
    for (const auto& f : hal.tx_frames)
        if (auto pr = parse_rts(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()))) rts_next = pr->next;
    CHECK(rts_next == 234);
    // Answer it with a CTS from 234 addressed to us, so handle_cts's team arm runs. handle_cts pins the flight on
    // rx_id==us + tx_id==next (it deliberately does NOT match ctr_lo — see its note).
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/213, /*tx_id=*/234, /*data_sf=*/7, cb);
    CHECK(cn > 0);
    A.on_recv(cb.data(), cn, RxMeta{12.0f, -70.0f, 0, static_cast<int8_t>(234)});
    CHECK(A.team_link_bidi_state(234) == LinkBidi::confirmed);      // ★ the T5 confirm fired
    CHECK(A.link_bidi_state(234) == LinkBidi::unknown);             // ★★ and NOT into the static array (I2/I8)
}

// =============================================================================
// ★★★ §AB4 — RETENTION of a received position (address-book spec 2026-07-29 §2.7/§2.7.2).
// These sit here, beside the §loc-per-send suite, because THIS file already owns the only native harness that drives a
// real DM through handle_data -> do_post_ack (U1/U3: originate_dm_loc + the receive replay below it). AB4 adds no
// extraction and no second sealed/plaintext test — the retention is one call inside the `if (loc_present)` block that
// already did all of that — so the honest coverage question is only "does the hook fire, and on which arm".
// ★★ AND IT MUST BE NATIVE: measured, not assumed — ZERO of the 36 corpus scenarios airs a location (`peer_location` /
// `has_location` / `lat_e7` have no hits in any scenario NDJSON), so the block is never entered in the corpus and a
// poison probe on this logic would be VACUOUSLY 0/36. The cause is a SIM coverage gap, not a firmware one: the sim's
// `send` verb has no `-l` (its only DM suffix rule is `-t`), so no scenario CAN put a position on the wire.
// =============================================================================
namespace {
// A node-2 receiver wired to open node 1's sealed DMs, plus the beacon that gives it the route/id_bind. Mined verbatim
// from the §loc-per-send positive test above rather than re-derived (U1).
struct LocRx {
    TestHal hal; Node B; Identity idA, idB;
    LocRx() : B(hal, 2, 0) { }
};
// A PLAINTEXT DATA frame carrying DATA_FLAG_LOCATION + SOURCE_HASH — i.e. what an OLDER or FOREIGN node (or a spoofer)
// airs. Our own firmware cannot produce one: node_mac.cpp REFUSES a `-l` send that would not be sealed, so this frame
// has to be synthesised. ★ It is built with the CODEC's own pack_unicast_inner + pack_data, never a hand-rolled byte
// layout, so the test cannot drift from the wire (the sibling mk_data above predates flags/location and is left alone —
// changing its inner construction would be a refactor of a helper three other tests depend on).
static size_t mk_data_plain_loc(uint8_t next, uint8_t dst, uint16_t ctr, uint8_t origin, uint32_t source_hash,
                                int32_t lat, int32_t lon, const char* body, std::array<uint8_t, 96>& b) {
    const uint8_t flags = DATA_FLAG_LOCATION | DATA_FLAG_SOURCE_HASH;
    uint8_t bl = 0; while (body[bl]) ++bl;
    std::array<uint8_t, 64> inner{};
    const size_t il = pack_unicast_inner(std::span<uint8_t>(inner.data(), inner.size()), flags, /*dst_key_hash32=*/0,
                                         /*layer_ids=*/nullptr, /*n_layers=*/0, /*cur=*/0, origin, source_hash,
                                         reinterpret_cast<const uint8_t*>(body), bl, lat, lon);
    if (!il) return 0;
    const uint8_t mac[4] = { 0, 0, 0, 0 };
    data_in in{}; in.addr_len = 0; in.flags = flags; in.next = next; in.dst = dst;
    in.hops_remaining = 31; in.committed_hops = 0; in.prev_fwd_rt_hops = 0; in.ctr = ctr;
    in.inner = std::span<const uint8_t>(inner.data(), il);
    in.mac = std::span<const uint8_t>(mac, 4);
    return pack_data(in, std::span<uint8_t>(b.data(), b.size()));
}
}  // namespace

// ★★ THE SLICE'S CENTRAL ASSERTION: a SEALED `-l` DM, delivered through the real receive path, is RETAINED — and the
// stored anchor is `peer`, the strong pairwise one, because opening the seal with OUR key is what proves who sent it.
TEST_CASE("§AB4 — a SEALED `-l` DM is RETAINED against the sender's hash, anchored `peer`") {
    const int32_t LAT = 523000000, LON = 134050000;
    OrigLoc r = originate_dm_loc(/*want_loc=*/true, LAT, LON, /*sealed=*/true);
    CHECK(r.aired); CHECK(r.crypted); CHECK(r.flag);
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 1); sB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);
    CHECK(B.peer_loc_count() == 0);                      // nothing retained before the DM
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts_for_frame(/*src=*/1, /*next=*/2, /*dst=*/2,
                                       static_cast<uint8_t>(r.ctr & 0x0F), r.plen, rb, r.frame.data(), r.frame.size()), from1);
    halB._now = 2000; B.on_recv(r.frame.data(), r.frame.size(), from1);
    B.on_timer(kPostAckTimerId);
    // The push still behaves exactly as before — the retention is ADDITIVE, it does not consume the position.
    Push pu{}; bool got = false;
    while (B.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) { CHECK(pu.enc); CHECK(pu.has_location); CHECK(pu.sender_hash == idA.key_hash32); }
    CHECK(halB.count("peer_location") == 1);
    CHECK(halB.count("peer_location_unauth") == 0);       // ★ the sealed arm never takes the refusal branch
    // ★★ AND NOW THE NEW BEHAVIOUR: it is in the ring, keyed by the SENDER'S HASH (not its node id — §1.2).
    CHECK(B.peer_loc_count() == 1);
    int32_t lat = 0, lon = 0; uint32_t age = 99; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    CHECK(B.peer_loc_find(idA.key_hash32, lat, lon, age, src));
    CHECK(src == Node::PeerLocSrc::peer);                 // ★ the PAIRWISE anchor — sealed to us, opened with our key
    CHECK(age == 0);                                      // received "now" (t_s == now_s at 2000 ms)
    long dlat = static_cast<long>(lat) - LAT; if (dlat < 0) dlat = -dlat;
    long dlon = static_cast<long>(lon) - LON; if (dlon < 0) dlon = -dlon;
    CHECK(dlat <= 512); CHECK(dlon <= 512);               // pack_loc6 quantises to ~1024e-7 deg (~11 m)
    // ★ THE END-TO-END POINT OF THE SLICE: the address book now SHOWS it. Same view the app's `peers` reads.
    halB._now = 62000;                                    // 60 s later
    Node::PeerBookRow row{};
    CHECK(B.peer_book_by_hash(idA.key_hash32, row));
    CHECK(row.has_location); CHECK(row.loc_age_s == 60); CHECK(row.loc_src == Node::PeerLocSrc::peer);
}

// ★★ THE REFUSAL ARM (owner ruling O6). An unauthenticated position is spoofable by anyone in range, and a spoofed
// position in an address book is WORSE than an absent one because the UI presents it as fact — so it is pushed to the
// app exactly as before but NEVER retained, and the refusal EMITS so a spoof attempt is observable rather than silent.
TEST_CASE("§AB4 — a PLAINTEXT location is pushed but NOT retained, and emits peer_location_unauth") {
    const int32_t LAT = 523000000, LON = 134050000;
    uint8_t sA[32], sB[32]; for (int i = 0; i < 32; ++i) { sA[i] = uint8_t(i + 1); sB[i] = uint8_t(100 - i); }
    Identity idA{}, idB{}; identity_from_seed(idA, sA); identity_from_seed(idB, sB);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12);
    TestHal halB; Node B(halB, 2, idB.key_hash32); B.on_init(cfg);
    B.set_crypto_identity(idB.x_secret, idB.ed_pub);
    B.peer_key_set(idA.key_hash32, idA.ed_pub, Node::PeerKeyConf::authoritative);   // we DO hold the key — so a refusal
                                                                                    // here is about the FRAME, not the key
    std::array<uint8_t, 64> bb{};
    beacon_entry be{}; be.dest = 1; be.next = 1; be.score_bucket = 14; be.hops = 1;
    beacon_in bin{}; bin.leaf_id = 0; bin.src = 1; bin.key_hash32 = idA.key_hash32;
    bin.entries = std::span<const beacon_entry>(&be, 1);
    RxMeta from1{ 12.0f, -70.0f, 0, static_cast<int8_t>(1) };
    halB._now = 500; B.on_recv(bb.data(), pack_beacon(bin, std::span<uint8_t>(bb.data(), bb.size())), from1);
    std::array<uint8_t, 96> df{};
    const size_t dn = mk_data_plain_loc(/*next=*/2, /*dst=*/2, /*ctr=*/0x21, /*origin=*/1,
                                        /*source_hash=*/idA.key_hash32, LAT, LON, "hi", df);
    CHECK(dn > 0);
    std::array<uint8_t, 16> rb{};
    halB._now = 1000; B.on_recv(rb.data(), mk_rts_for_frame(/*src=*/1, /*next=*/2, /*dst=*/2, /*ctr_lo=*/0x1,
                                       static_cast<uint8_t>(dn - 8), rb, df.data(), dn), from1);
    halB._now = 2000; B.on_recv(df.data(), dn, from1);
    B.on_timer(kPostAckTimerId);
    // ★ UNCHANGED BEHAVIOUR: the app still gets the position. AB4 removed nothing — the companion decides what to do
    //   with an unauthenticated fix; the NODE just refuses to file it as a fact about that peer.
    Push pu{}; bool got = false;
    while (B.next_push(pu)) { if (pu.kind == PushKind::msg_recv) { got = true; break; } }
    CHECK(got);
    if (got) {
        CHECK_FALSE(pu.enc);                              // delivered in the CLEAR
        CHECK(pu.has_location);                           // ...and the position is STILL pushed, exactly as before
        CHECK(pu.sender_hash == idA.key_hash32);          // and it even names a hash — which is precisely not enough
    }
    CHECK(halB.count("peer_location") == 1);              // the pre-existing emit is untouched
    // ★★ THE TWO ASSERTIONS THAT ARE THIS SLICE: refused, and LOUDLY.
    CHECK(halB.count("peer_location_unauth") == 1);
    CHECK(B.peer_loc_count() == 0);                       // ★ NOT RETAINED
    int32_t lat = 0, lon = 0; uint32_t age = 0; Node::PeerLocSrc src = Node::PeerLocSrc::team;
    CHECK_FALSE(B.peer_loc_find(idA.key_hash32, lat, lon, age, src));
    // ...and the address book therefore shows the peer WITHOUT a position, rather than with a spoofable one.
    Node::PeerBookRow row{};
    CHECK(B.peer_book_by_hash(idA.key_hash32, row));
    CHECK(row.has_key);                                   // a fully-known peer in every other respect
    CHECK_FALSE(row.has_location);
}

// =============================================================================
// ★★★★★ §hybrid-rts S2 (2026-08-08) — RECEIVER IDENTITY CONTINUITY, MANDATORY DATA VALIDATION AND THE
// COMPLETED-FLIGHT CACHE.  Design: docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md §4.
//
// ⚠ WHY THESE CASES CARRY MORE WEIGHT THAN USUAL, stated so nobody trims them: the corpus CANNOT protect this
//   slice on two whole axes. The CRYPTED arm is 2 frames of 9 624 corpus-wide, and the plane cross-match cases
//   have no corpus instance at all (12 scenarios have a team plane, 12 produce a requeue, and the intersection is
//   empty — [[B160-COV]]). Everything below is therefore the ONLY detector for what it asserts.
// =============================================================================
namespace {
// The exact tuple the cache is keyed on, so a case can perturb ONE field at a time and see the miss.
struct S2Key { uint8_t from, dst; bool team; RtsFlightIdentity id; };
}  // namespace

TEST_CASE("§hybrid-rts S2 — the cache key is the COMPLETE tuple: perturbing ANY ONE field misses") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    const RtsFlightIdentity id = rts_flight_identity_plain(/*origin=*/1, /*ctr=*/0x0005);
    hal._now = 1000;
    node.completed_flight_store(/*from=*/1, /*dst=*/2, /*team=*/false, id, hal._now);
    CHECK(node.completed_flight_live_count(hal._now) == 1);
    // the exact tuple HITS — the positive control, without which every miss below would be vacuous
    CHECK(node.completed_flight_find(1, 2, false, id, hal._now) != nullptr);
    // ...and each single-field perturbation MISSES
    CHECK(node.completed_flight_find(9, 2, false, id, hal._now) == nullptr);            // immediate sender
    CHECK(node.completed_flight_find(1, 9, false, id, hal._now) == nullptr);            // dst
    CHECK(node.completed_flight_find(1, 2, true,  id, hal._now) == nullptr);            // ★ team/static PLANE
    { RtsFlightIdentity x = id; x.bytes[0] ^= 0x01;                                      // one identity bit
      CHECK(node.completed_flight_find(1, 2, false, x, hal._now) == nullptr); }
    { RtsFlightIdentity x = id; x.bytes[2] ^= 0x80;
      CHECK(node.completed_flight_find(1, 2, false, x, hal._now) == nullptr); }
    { RtsFlightIdentity absent{};                                                        // width 0 never matches
      CHECK(node.completed_flight_find(1, 2, false, absent, hal._now) == nullptr); }
    // ★ FULL-COUNTER DISTINCTNESS — the 4-bit alias this whole arc exists for. 0x0002 and 0x1002 share every bit
    //   the old RTS `ctr_lo` ever carried, and the two identities must not be interchangeable.
    node.completed_flight_store(1, 2, false, rts_flight_identity_plain(1, 0x0002), hal._now);
    CHECK(node.completed_flight_find(1, 2, false, rts_flight_identity_plain(1, 0x0002), hal._now) != nullptr);
    CHECK(node.completed_flight_find(1, 2, false, rts_flight_identity_plain(1, 0x1002), hal._now) == nullptr);
    CHECK((rts_flight_identity_plain(1, 0x0002).bytes[2]
           == rts_flight_identity_plain(1, 0x1002).bytes[2]));   // ...and they DO share the low byte
}

TEST_CASE("§hybrid-rts S2 — PLAINTEXT and CRYPTED identities never cross-match, even at the same numeric value") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    const uint8_t seed[8] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88 };
    const RtsFlightIdentity cid = rts_flight_identity_crypted(seed, 0x0005, /*dst=*/2);
    CHECK(cid.domain == RtsIdDomain::crypted); CHECK(cid.width == 4);
    // A plaintext identity whose THREE bytes are byte-for-byte the crypted tail's first three. Hand-built,
    // because the domains are 1:1 with the width today and only a crafted pair can separate the two fields.
    // ⚠ ALL FOUR BYTES are copied, INCLUDING bytes[3] which a real 3-byte identity always leaves 0. That is
    // deliberate and it is what makes this case detect the DOMAIN/WIDTH fields rather than an incidental byte
    // difference: a comparator that ignored domain AND width would now see two identical byte arrays. Measured —
    // with bytes[3] left at 0, a domain-and-width-blind comparator stays GREEN, i.e. the case would be vacuous.
    RtsFlightIdentity twin{}; twin.domain = RtsIdDomain::plaintext; twin.width = 3;
    for (uint8_t i = 0; i < RTS_ID_MAX_LEN; ++i) twin.bytes[i] = cid.bytes[i];
    hal._now = 1000;
    node.completed_flight_store(1, 2, false, cid, hal._now);
    CHECK(node.completed_flight_find(1, 2, false, cid,  hal._now) != nullptr);   // positive control
    CHECK(node.completed_flight_find(1, 2, false, twin, hal._now) == nullptr);   // ★ the domain separates them
    node.completed_flight_store(1, 2, false, twin, hal._now);                    // both coexist
    CHECK(node.completed_flight_live_count(hal._now) == 2);
    CHECK(node.completed_flight_find(1, 2, false, twin, hal._now) != nullptr);
}

TEST_CASE("§hybrid-rts S2 — TEAM and STATIC flights with the SAME numeric identity never cross-match") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    const RtsFlightIdentity id = rts_flight_identity_plain(50, 0x0021);   // the §18 one-numeric-space collision
    hal._now = 1000;
    node.completed_flight_store(/*from=*/93, /*dst=*/50, /*team=*/true, id, hal._now);
    CHECK(node.completed_flight_find(93, 50, true,  id, hal._now) != nullptr);   // positive control
    CHECK(node.completed_flight_find(93, 50, false, id, hal._now) == nullptr);   // ★ same numbers, other plane
    node.completed_flight_store(93, 50, false, id, hal._now);
    CHECK(node.completed_flight_live_count(hal._now) == 2);                      // two ENTRIES, not one refreshed
}

TEST_CASE("§hybrid-rts S2 — the TTL is DERIVED from gateway_send_giveup_ms, and its boundary is exact") {
    // ⛔ The derivation is the assertion: a bare 150000 here would drift the day the base moves.
    CHECK(protocol::completed_flight_cache_ttl_ms == protocol::gateway_send_giveup_ms);
    CHECK(protocol::completed_flight_cache_ttl_ms == 150000u);
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    const RtsFlightIdentity id = rts_flight_identity_plain(1, 0x0005);
    hal._now = 1000; node.completed_flight_store(1, 2, false, id, hal._now);
    const uint64_t T = protocol::completed_flight_cache_ttl_ms;
    CHECK(node.completed_flight_find(1, 2, false, id, 1000 + T - 1) != nullptr);   // before
    CHECK(node.completed_flight_find(1, 2, false, id, 1000 + T)     == nullptr);   // AT the boundary -> expired
    CHECK(node.completed_flight_find(1, 2, false, id, 1000 + T + 1) == nullptr);   // after
    // ★ THE TWO MEASURED HORIZONS THIS TTL EXISTS TO COVER (§HYBRID-RTS-S0), pinned as retention facts rather
    //   than as prose: the longest directly-measured exact retry, and the audited gateway-hold re-arrival.
    CHECK(node.completed_flight_find(1, 2, false, id, 1000 + 18971)  != nullptr);   // ⛔ a 10 s TTL loses this
    CHECK(node.completed_flight_find(1, 2, false, id, 1000 + 147658) != nullptr);   // ⛔ a 30 s or 60 s TTL loses this
    CHECK(18971u  > 10000u);
    CHECK(147658u > 30000u);
}

TEST_CASE("§hybrid-rts S2 — the cache holds cap_completed_flights live flights from ONE immediate sender, "
          "and evicts the OLDEST when full (a capacity of 1 would lose 105 of 429 measured corpus hits)") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    const uint8_t N = protocol::cap_completed_flights;
    CHECK(N >= 12);                                     // ★ the MEASURED requirement: 12 keeps 429/429 corpus hits
    // N distinct flights, ALL from the SAME immediate sender (the shape a capacity-one cache cannot hold).
    for (uint8_t i = 0; i < N; ++i) {
        hal._now = 1000 + i;                            // strictly increasing -> a deterministic eviction order
        node.completed_flight_store(/*from=*/7, /*dst=*/2, false, rts_flight_identity_plain(1, uint16_t(0x100 + i)), hal._now);
    }
    CHECK(node.completed_flight_live_count(hal._now) == N);
    for (uint8_t i = 0; i < N; ++i)                     // ★ EVERY one of them still hits — not just the newest
        CHECK(node.completed_flight_find(7, 2, false, rts_flight_identity_plain(1, uint16_t(0x100 + i)), hal._now) != nullptr);
    // one more -> the table is full of LIVE entries -> evict the OLDEST (min expiry), keep the rest
    hal._now = 1000 + N;
    node.completed_flight_store(7, 2, false, rts_flight_identity_plain(1, 0x0999), hal._now);
    CHECK(node.completed_flight_live_count(hal._now) == N);                                   // still bounded
    CHECK(node.completed_flight_find(7, 2, false, rts_flight_identity_plain(1, 0x0100), hal._now) == nullptr);  // the oldest went
    CHECK(node.completed_flight_find(7, 2, false, rts_flight_identity_plain(1, 0x0101), hal._now) != nullptr);  // the next did not
    CHECK(node.completed_flight_find(7, 2, false, rts_flight_identity_plain(1, 0x0999), hal._now) != nullptr);  // the new one is in
    // a re-store of a LIVE key REFRESHES it in place; it does not consume a second slot
    node.completed_flight_store(7, 2, false, rts_flight_identity_plain(1, 0x0101), hal._now);
    CHECK(node.completed_flight_live_count(hal._now) == N);
}

TEST_CASE("§hybrid-rts S2 — a DATA whose identity contradicts the RTS is REFUSED: no delivery, no ACK, "
          "no cache seed, and the reservation is CLEARED") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    // the RTS authorises flight (origin 1, ctr 0x0005); the DATA that arrives is (origin 1, ctr 0x1005) —
    // the SAME 4-bit ctr_lo, the same length, the same endpoints. Under the retired 7-B wire these were
    // indistinguishable; that is precisely the substitution this validation exists to catch.
    const size_t rn = mk_rts(1, 2, 2, /*ctr_lo=*/5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    const size_t dn = mk_data(2, 2, /*ctr=*/0x1005, /*origin=*/1, "hi", db);
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    CHECK(hal.count("cts_tx") == 1);                                  // the exchange really started (not vacuous)
    hal._now = 1500; node.on_recv(db.data(), dn, meta);
    node.on_timer(kPostAckTimerId);
    // ⛔ ALL FOUR PROHIBITIONS, each asserted separately — an emitted diagnostic is NOT the behaviour.
    CHECK(hal.count("rts_flight_id_mismatch") == 1);                  // (the named diagnostic, 5th)
    CHECK(delivered_payloads(hal).empty());                           // 1. no delivery
    CHECK(acks_on_wire(hal).empty());                                 // 2. no ACK on the wire
    CHECK(node.completed_flight_live_count(hal._now) == 0);           // 3. nothing seeded
    CHECK(node.seen_origin_count() == 0);                             //    ...and no dedup credit either
    // 4. the reservation is CLEARED: a NEW flight from a DIFFERENT sender is answered with a fresh CTS rather
    //    than the BUSY_RX NACK a still-held reservation would produce. That is the observable, not a getter.
    std::array<uint8_t, 16> rb2{};
    const size_t rn2 = mk_rts(4, 2, 2, /*ctr_lo=*/6, 15, rb2, 0, /*origin=*/4, /*ctr=*/0x0006);
    RxMeta m4{ 8.0f, -80.0f, 0, static_cast<int8_t>(4) };
    hal._now = 1600; node.on_recv(rb2.data(), rn2, m4);
    CHECK(hal.count("cts_tx") == 2);
    CHECK(nacks_on_wire(hal).empty());
}

TEST_CASE("§hybrid-rts S2 — the CRYPTED arm end to end: admission, validation, and a 7-B terminal CTS on the retry") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    const uint8_t S[8]  = { 0xA0,0xA1,0xA2,0xA3, 0xA4,0xA5,0xA6,0xA7 };
    const uint8_t S2[8] = { 0xA0,0xA1,0xA2,0xA3, 0xA4,0xA5,0xA6,0xA8 };   // ONE bit different
    std::array<uint8_t, 16> rb{}, rb2{}; std::array<uint8_t, 64> db{}, db2{};
    const size_t rn  = mk_rts(1, 2, 2, /*ctr_lo=*/5, 20, rb,  0, -1, /*ctr=*/0x0005, S);
    const size_t rn2 = mk_rts(1, 2, 2, /*ctr_lo=*/5, 20, rb2, 0, -1, /*ctr=*/0x0005, S2);
    const size_t dn  = mk_data_crypted(2, 2, 0x0005, /*origin=*/1, node.key_hash32(), S,  "hi", db);
    const size_t dn2 = mk_data_crypted(2, 2, 0x0005, /*origin=*/1, node.key_hash32(), S2, "hi", db2);
    CHECK(rn == 11);                                                  // ★ the CRYPTED wire really is 11 B
    hal._now = 1000; node.on_recv(rb.data(), rn, meta);
    hal._now = 1500; node.on_recv(db.data(), dn, meta);  node.on_timer(kPostAckTimerId);
    CHECK(hal.count("rts_flight_id_mismatch") == 0);                  // the matching DATA was ACCEPTED
    CHECK(acks_on_wire(hal).size() == 1);
    CHECK(node.completed_flight_find(1, 2, false, rts_flight_identity_crypted(S, 0x0005, 2), hal._now) != nullptr);
    // ★ the retry of the SAME sealed flight -> TERMINAL CTS, 7 B, echoing the 4-byte crypted identity
    hal._now = 6000; node.on_recv(rb.data(), rn, meta);
    { const auto* c = hal.last_tx("CTS"); CHECK(c != nullptr);
      if (c) { CHECK(c->bytes.size() == 7);
               auto pc = parse_cts(std::span<const uint8_t>(c->bytes.data(), c->bytes.size()));
               CHECK(pc.has_value());
               if (pc) { CHECK(pc->already_received);
                         CHECK(pc->id.domain == RtsIdDomain::crypted);
                         CHECK(rts_flight_identity_equal(pc->id, rts_flight_identity_crypted(S, 0x0005, 2)));
                         CHECK_FALSE(pc->team_plane); } } }
    // ★ ONE SEED BIT -> a DIFFERENT flight -> a cache MISS -> an ORDINARY CTS, and its DATA is accepted
    hal._now = 7000; node.on_recv(rb2.data(), rn2, meta);
    { const auto* c = hal.last_tx("CTS"); CHECK(c != nullptr);
      if (c) { CHECK(c->bytes.size() == 4);                            // ordinary (NAV hint present)
               auto pc = parse_cts(std::span<const uint8_t>(c->bytes.data(), c->bytes.size()));
               CHECK(pc.has_value()); if (pc) CHECK_FALSE(pc->already_received); } }
    hal._now = 7500; node.on_recv(db2.data(), dn2, meta);
    CHECK(hal.count("rts_flight_id_mismatch") == 0);
    CHECK(acks_on_wire(hal).size() == 2);
}

TEST_CASE("§hybrid-rts S2 — the store is keyed on the ON-AIR sender, NEVER on the simulator's src_hint") {
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    // The RTS airs src = 1; the PHY oracle claims 77. On metal src_hint is -1 and only the frame's own `src`
    // exists, so a key built from the oracle would be a sim/metal divergence — [[B156]]'s exact defect.
    RxMeta oracle{ 8.0f, -80.0f, 0, static_cast<int8_t>(77) };
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    const size_t rn = mk_rts(/*src=*/1, 2, 2, /*ctr_lo=*/5, 15, rb, 0, /*origin=*/1, /*ctr=*/0x0005);
    const size_t dn = mk_data(2, 2, 0x0005, /*origin=*/1, "hi", db);
    hal._now = 1000; node.on_recv(rb.data(), rn, oracle);
    hal._now = 1500; node.on_recv(db.data(), dn, oracle);
    const RtsFlightIdentity id = rts_flight_identity_plain(1, 0x0005);
    CHECK(node.completed_flight_find(/*from=*/1,  2, false, id, hal._now) != nullptr);   // ★ the ON-AIR src
    CHECK(node.completed_flight_find(/*from=*/77, 2, false, id, hal._now) == nullptr);   // ⛔ never the oracle
}

TEST_CASE("§hybrid-rts S2 — a stale reservation is RE-POINTED by a retried RTS, so a new flight from the same "
          "sender is not destroyed by the old flight's identity") {
    // The regression the dup-re-CTS branch's identity refresh exists for. A sender is single-slot stop-and-wait,
    // so an RTS for flight B on the same (from, dst, ctr_lo, len) means flight A died at ITS end. Without the
    // refresh, B's DATA would be validated against A's identity and dropped un-ACKed.
    TestHal hal; Node node(hal, /*id=*/2, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(1) };
    std::array<uint8_t, 16> rbA{}, rbB{}; std::array<uint8_t, 64> dbB{};
    const size_t rnA = mk_rts(1, 2, 2, /*ctr_lo=*/5, 15, rbA, 0, /*origin=*/1, /*ctr=*/0x0005);   // flight A
    const size_t rnB = mk_rts(1, 2, 2, /*ctr_lo=*/5, 15, rbB, 0, /*origin=*/1, /*ctr=*/0x1005);   // flight B, same tuple
    const size_t dnB = mk_data(2, 2, /*ctr=*/0x1005, /*origin=*/1, "hi", dbB);
    hal._now = 1000; node.on_recv(rbA.data(), rnA, meta);            // reservation for A
    hal._now = 1100; node.on_recv(rbB.data(), rnB, meta);            // A's DATA never came; B's RTS re-points it
    CHECK(hal.count("cts_tx") == 2);                                  // the dup branch answered (not a BUSY_RX NACK)
    CHECK(nacks_on_wire(hal).empty());
    hal._now = 1200; node.on_recv(dbB.data(), dnB, meta);
    node.on_timer(kPostAckTimerId);
    CHECK(hal.count("rts_flight_id_mismatch") == 0);                  // ★ B is NOT collateral damage
    CHECK(delivered_payloads(hal).size() == 1);
    CHECK(acks_on_wire(hal).size() == 1);
}

TEST_CASE("§hybrid-rts S2 [[B161]] — a RAW-INNER typed answer's RTS identity is the byte the DATA EXPOSES, "
          "not the originator's carrier `origin`") {
    // send_hash_bind_response enqueues a BARE `hash_bind_inner` with no `[origin]` prefix, so the receiver's
    // `parse_unicast_inner` reads inner[0] = target_layer = 0. Feeding `pt.origin` (= our node_id) into the
    // identity made 69 of 4 949 corpus DATA receptions unverifiable — all of them type 2 or type 8.
    TestHal hal; Node node(hal, /*node_id=*/2, /*key_hash32=*/0x0000BBBBu);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.allowed_sf_bitmap = (1u << 12); cfg.lbt_enabled = false;
    node.on_init(cfg);
    std::array<uint8_t, 64> bb{};
    { const size_t n = mk_beacon_route(/*src=*/9, /*dest=*/9, /*next=*/9, /*hops=*/1, /*score=*/14, bb);
      RxMeta m9{ 12.0f, -70.0f, 0, static_cast<int8_t>(9) }; node.on_recv(bb.data(), n, m9); }
    h_in q{}; q.leaf_id = 0; q.origin = 9; q.query_key32 = 0x0000BBBBu; q.ttl = 4;      // ask the OWNER for its own hash
    std::array<uint8_t, 48> qb{};
    const size_t qn = pack_h(q, std::span<uint8_t>(qb.data(), qb.size()));
    CHECK(qn > 0);
    RxMeta meta{ 8.0f, -80.0f, 0, static_cast<int8_t>(9) };
    hal._now = 1000; node.on_recv(qb.data(), qn, meta);               // -> enqueues the AUTHORITATIVE H_ANSWER
    const auto* rts = hal.last_tx("RTS");
    CHECK(rts != nullptr);
    if (rts) {
        auto pr = parse_rts(std::span<const uint8_t>(rts->bytes.data(), rts->bytes.size()));
        CHECK(pr.has_value());
        if (pr) {
            CHECK(pr->id.domain == RtsIdDomain::plaintext);
            CHECK(pr->id.bytes[0] == 0);      // ★ the DATA-EXPOSED origin (hash_bind_inner.target_layer)
            CHECK(pr->id.bytes[0] != 2);      // ⛔ NOT the carrier's `origin` (= our node_id) — the S1 behaviour
            // ★★★ AND THE SENDER MUST ACCEPT ITS OWN FLIGHT'S TERMINAL ECHO. This half exists because the first
            // S2 draft derived the RTS identity from the inner but the terminal-CTS comparison from `pt.origin`
            // — two derivations of one identity. On this exact frame shape they differ (0 vs 2), and the corpus
            // answered with 40 `cts_terminal_mismatch` refusals and a retry deadlock in s15/s15_metal/s27.
            // Collapsing both onto `Node::flight_identity` is the fix; this is its detector.
            cts_in tc{}; tc.already_received = true; tc.tx_id = 9; tc.rx_id = 2; tc.id = pr->id;
            std::array<uint8_t, 8> cb{};
            const size_t cn = pack_cts(tc, std::span<uint8_t>(cb.data(), cb.size()));
            CHECK(cn == 6);
            RxMeta cm{ 8.0f, -80.0f, 0, static_cast<int8_t>(9) };
            hal._now = 1100; node.on_recv(cb.data(), cn, cm);
            CHECK(hal.count("cts_terminal_mismatch") == 0);   // ★ the sender recognises its OWN identity
            CHECK_FALSE(node.has_pending_tx());               // ...and the terminal answer completed the flight
        }
    }
}

TEST_CASE("§hybrid-rts S2 — the WIRE declaration decides the plane, not whichever receiver predicate matched") {
    // ★★ THE (1,0) LAST-MILE AMBIGUITY, built as the real collision: a REGISTERED MOBILE that is ALSO a team
    // member whose team_local_id EQUALS its hosted local id. A host's `(addr_len=1, mobile_src=0)` last-mile then
    // satisfies BOTH `for_static_rts` (it is mobile, so addr_len 1 is its own marking) AND `team_addr_for_us`.
    // `team_addr_for_us` never consults `mobile_src`; only the wire declaration can tell the two apart.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node n(hal, /*id=*/17, /*key=*/0xA017u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.is_mobile = true; cfg.team_id = TEAM; CHECK(n.on_init(cfg));
    n.set_team_local_id(17);                                          // ★ the collision: team id == node id == 17
    CHECK(n.team_local_id() == 17);
    RxMeta from31{ 12.0f, -70.0f, 0, static_cast<int8_t>(31) };
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    // ARM 1 — the HOST's last-mile: (addr_len=1, mobile_src=0) => the wire says STATIC/GLOBAL.
    rts_in r{}; r.leaf_id = 0; r.src = 31; r.next = 17; r.ctr_lo = 5; r.dst = 17; r.sf_index = 3;
    r.payload_len = 15; r.addr_len = 1; r.mobile_src = false;
    r.id = rts_flight_identity_plain(/*origin=*/1, /*ctr=*/0x0005);
    const size_t rn = pack_rts(r, std::span<uint8_t>(rb.data(), rb.size()));
    CHECK(rn == 10);
    hal._now = 1000; n.on_recv(rb.data(), rn, from31);
    CHECK(hal.count("cts_tx") == 1);                                  // admitted (positive control)
    { std::array<uint8_t, 32> inner{}; inner[0] = 1; inner[1] = 'h'; inner[2] = 'i';
      const uint8_t mac[4] = {0,0,0,0};
      data_in d{}; d.addr_len = 1; d.flags = 0; d.next = 17; d.dst = 17; d.hops_remaining = 31; d.ctr = 0x0005;
      d.inner = std::span<const uint8_t>(inner.data(), 3); d.mac = std::span<const uint8_t>(mac, 4);
      const size_t dn = pack_data(d, std::span<uint8_t>(db.data(), db.size()));
      hal._now = 1100; n.on_recv(db.data(), dn, from31); }
    CHECK(hal.count("rts_flight_id_mismatch") == 0);                  // the STATIC plane validated it
    const RtsFlightIdentity id = rts_flight_identity_plain(1, 0x0005);
    CHECK(n.completed_flight_find(31, 17, /*team=*/false, id, hal._now) != nullptr);   // ★ stored as STATIC...
    CHECK(n.completed_flight_find(31, 17, /*team=*/true,  id, hal._now) == nullptr);   // ⛔ ...never as TEAM
    // ARM 2 (the converse) — the SAME endpoints with (1,1): the wire says TEAM, and it is stored as TEAM.
    std::array<uint8_t, 16> rb2{}; std::array<uint8_t, 64> db2{};
    rts_in t = r; t.mobile_src = true; t.ctr_lo = 6; t.id = rts_flight_identity_plain(/*origin=*/1, /*ctr=*/0x0006);
    const size_t rn2 = pack_rts(t, std::span<uint8_t>(rb2.data(), rb2.size()));
    hal._now = 2000; n.on_recv(rb2.data(), rn2, from31);
    CHECK(hal.count("cts_tx") == 2);
    { std::array<uint8_t, 32> inner{}; inner[0] = 1; inner[1] = 'h'; inner[2] = 'i';
      const uint8_t mac[4] = {0,0,0,0};
      data_in d{}; d.addr_len = 1; d.flags = 0; d.next = 17; d.dst = 17; d.hops_remaining = 31; d.ctr = 0x0006;
      d.inner = std::span<const uint8_t>(inner.data(), 3); d.mac = std::span<const uint8_t>(mac, 4);
      const size_t dn = pack_data(d, std::span<uint8_t>(db2.data(), db2.size()));
      hal._now = 2100; n.on_recv(db2.data(), dn, from31); }
    CHECK(hal.count("rts_flight_id_mismatch") == 0);
    const RtsFlightIdentity id2 = rts_flight_identity_plain(1, 0x0006);
    CHECK(n.completed_flight_find(31, 17, /*team=*/true,  id2, hal._now) != nullptr);  // ★ stored as TEAM
    CHECK(n.completed_flight_find(31, 17, /*team=*/false, id2, hal._now) == nullptr);
    // ★ AND THE TERMINAL CTS ECHOES THE STORED PLANE — the bit a sender binds on.
    hal._now = 3000; n.on_recv(rb2.data(), rn2, from31);
    { const auto* c = hal.last_tx("CTS"); CHECK(c != nullptr);
      if (c) { CHECK(c->bytes.size() == 6);
               auto pc = parse_cts(std::span<const uint8_t>(c->bytes.data(), c->bytes.size()));
               CHECK(pc.has_value()); if (pc) { CHECK(pc->already_received); CHECK(pc->team_plane); } } }
    hal._now = 4000; n.on_recv(rb.data(), rn, from31);                // the STATIC flight's retry
    { const auto* c = hal.last_tx("CTS"); CHECK(c != nullptr);
      if (c) { auto pc = parse_cts(std::span<const uint8_t>(c->bytes.data(), c->bytes.size()));
               CHECK(pc.has_value()); if (pc) { CHECK(pc->already_received); CHECK_FALSE(pc->team_plane); } } }
}

TEST_CASE("§hybrid-rts S2 — a DATA whose PLANE contradicts the reservation takes the same fail-loud path") {
    // The RTS declares TEAM (1,1) to our team id; the DATA that arrives is addressed to our STATIC id with
    // addr_len 0, so `for_team_data` is false. DATA carries no `mobile_src`, so the stored plane is the only
    // evidence there is — and a contradiction is a protocol failure, not a plane we may re-pick.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node n(hal, /*id=*/50, /*key=*/0xA050u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.is_mobile = true; cfg.team_id = TEAM; CHECK(n.on_init(cfg));
    n.set_team_local_id(93);                                          // team id 93 != node id 50
    RxMeta from60{ 12.0f, -70.0f, 0, static_cast<int8_t>(60) };
    std::array<uint8_t, 16> rb{}; std::array<uint8_t, 64> db{};
    const size_t rn = t2_team_rts(/*src=*/60, /*next=*/93, /*dst=*/93, /*ctr_lo=*/5, /*plen=*/8, rb,
                                  /*origin=*/70, /*ctr=*/0x0005);
    hal._now = 1000; n.on_recv(rb.data(), rn, from60);
    CHECK(hal.count("cts_tx") == 1);                                  // admitted on the TEAM plane
    { std::array<uint8_t, 32> inner{}; inner[0] = 70; inner[1] = 'h'; inner[2] = 'i';
      const uint8_t mac[4] = {0,0,0,0};
      data_in d{}; d.addr_len = 1; d.flags = 0; d.next = 50; d.dst = 50;   // ★ our MOBILE/static id, not the team id
      d.hops_remaining = 31; d.ctr = 0x0005;
      d.inner = std::span<const uint8_t>(inner.data(), 3); d.mac = std::span<const uint8_t>(mac, 4);
      const size_t dn = pack_data(d, std::span<uint8_t>(db.data(), db.size()));
      hal._now = 1100; n.on_recv(db.data(), dn, from60); }
    CHECK(hal.count("rts_flight_id_mismatch") == 1);                  // ★ the PLANE contradiction is fail-loud
    CHECK(delivered_payloads(hal).empty());
    CHECK(acks_on_wire(hal).empty());
    CHECK(n.completed_flight_live_count(hal._now) == 0);
}

TEST_CASE("§hybrid-rts S2 — the SENDER refuses a terminal CTS whose echo names a different flight (a delayed "
          "answer to flight A must not clear flight B)") {
    // ★★ Design §2.3's whole reason for the 6/7-B shape. CTS is retry/duty-stash eligible and that stash has no
    // flight-generation guard, so a terminal answer to A can land while B awaits its CTS on the same next hop.
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0; node.on_init(cfg);
    node.route_inject(/*dest=*/20, /*next_hop=*/20, /*hops=*/1, /*score=*/100);
    const uint8_t body[2] = { 'h', 'i' };
    hal._now = 1000;
    const uint16_t ctr = node.test_do_send_typed(/*dst=*/20, body, sizeof body, CryptIntent::def,
                                                /*override_dst_hash=*/0, /*type=*/0);   // flight B awaits its CTS
    CHECK(ctr != 0);
    const auto* rts = hal.last_tx("RTS"); CHECK(rts != nullptr);
    RtsFlightIdentity mine{};
    if (rts) { auto pr = parse_rts(std::span<const uint8_t>(rts->bytes.data(), rts->bytes.size()));
               CHECK(pr.has_value()); if (pr) mine = pr->id; }
    CHECK(rts_flight_identity_valid(mine));
    auto terminal = [&](const RtsFlightIdentity& id, bool team) {
        cts_in c{}; c.already_received = true; c.tx_id = 20; c.rx_id = 1; c.id = id; c.team_plane = team;
        std::array<uint8_t, 8> b{};
        const size_t n = pack_cts(c, std::span<uint8_t>(b.data(), b.size()));
        CHECK(n >= 6);
        RxMeta m{ 8.0f, -80.0f, 0, static_cast<int8_t>(20) };
        node.on_recv(b.data(), n, m);
    };
    // ⛔ ARM 1 — a terminal CTS for a DIFFERENT flight (one identity byte off): IGNORED, B stays pending.
    { RtsFlightIdentity other = mine; other.bytes[1] ^= 0x01; hal._now = 1100; terminal(other, false); }
    CHECK(hal.count("cts_terminal_mismatch") == 1);
    CHECK(hal.count("cts_rx") == 0);                                  // no success-shaped telemetry
    CHECK(node.has_pending_tx());                                  // ★ B is STILL pending
    // ⛔ ARM 2 — the RIGHT identity on the WRONG plane: also ignored.
    { hal._now = 1200; terminal(mine, /*team=*/true); }
    CHECK(hal.count("cts_terminal_mismatch") == 2);
    CHECK(node.has_pending_tx());
    // ★ ARM 3 (the positive control, without which the two refusals prove nothing): the EXACT echo completes it.
    { hal._now = 1300; terminal(mine, /*team=*/false); }
    CHECK(hal.count("cts_terminal_mismatch") == 2);                   // no third mismatch
    CHECK_FALSE(node.has_pending_tx());                            // ★ B completed on its OWN terminal answer
}

TEST_CASE("§hybrid-rts S2b — an OVERHEARD TERMINAL CTS must NOT arm NAV (no DATA follows it), while an "
          "overheard ORDINARY CTS still must (the positive control)") {
    // ★★★ THE BUG S2 SHIPPED. A terminal CTS means "I ALREADY HAVE THAT FLIGHT" ⇒ **no DATA and no ACK follow**.
    // S2 let an overhearer take the ordinary CTS path, which armed a NAV reservation for an exchange that will
    // never happen. `parse_cts` zeroes the shape (`chosen_data_sf = 0`, `payload_len = 0`) and `payload_len == 0`
    // is `nav_duration_cts`'s NO-HINT MAX-FRAME fallback, so the code ASKED for a full-frame reservation.
    // ⓘ Measured detail worth keeping: `data_sf = 0` drives `airtime.cpp`'s `den` to 0, which zeroes the payload
    //   term, so the reservation that actually landed was only `airtime_routing_ms(3) + 2*gap` — SMALL, but held
    //   against nothing at all. The assertion below is on `nav_until_ms`, so it catches the defect either way.
    // ⚠ THE TWO ARMS SHARE ONE NODE AND ONE `pack_cts` PRODUCER; the ONLY difference is the frame SHAPE.
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.nav_enabled = true;                        // ⚠ without this the whole test is vacuous
    CHECK(node.on_init(cfg));
    RxMeta from20{ 12.0f, -70.0f, 0, static_cast<int8_t>(20) };
    // Both frames are addressed to node 7 — NOT us (id 1) and not our team id ⇒ the OVERHEAR path.
    CHECK_FALSE(node.for_me_dst(7));
    // ⛔ ARM 1 — the TERMINAL shape, overheard. NAV must be UNTOUCHED.
    CHECK(node.nav_until_ms() == 0);               // precondition: nothing armed yet
    { cts_in c{}; c.already_received = true; c.tx_id = 20; c.rx_id = 7; c.team_plane = false;
      c.id = rts_flight_identity_plain(/*origin=*/9, /*ctr=*/0x0031);
      std::array<uint8_t, 8> b{};
      const size_t n = pack_cts(c, std::span<uint8_t>(b.data(), b.size()));
      CHECK(n == 6);                                                    // the canonical plaintext terminal wire
      hal._now = 1000; node.on_recv(b.data(), n, from20); }
    CHECK(node.nav_until_ms() == 0);               // ★★ THE ASSERTION: a terminal CTS reserves NOTHING
    // ★ ARM 2 — THE POSITIVE CONTROL. Without it, ARM 1 would pass on a build that never arms NAV at all.
    //   The SAME overhear path, the SAME non-addressee, an ORDINARY CTS ⇒ NAV MUST be armed.
    { cts_in c{}; c.already_received = false; c.tx_id = 20; c.rx_id = 7; c.chosen_data_sf = 12;
      c.payload_len = 40; c.cr_adv = 0;
      std::array<uint8_t, 8> b{};
      const size_t n = pack_cts(c, std::span<uint8_t>(b.data(), b.size()));
      CHECK(n == 4);
      hal._now = 2000; node.on_recv(b.data(), n, from20); }
    CHECK(node.nav_until_ms() > 2000);             // ★★ armed, and into the future — the instrument works
}

TEST_CASE("§hybrid-rts S2c — an UNBOUND terminal CTS refreshes/learns/clears NOTHING but IS still billed ONCE "
          "at its true 6-B airtime (the two classes of side effect, in one case)") {
    // ★★★ BLOCKER 2 (S2b) + THE S2c ADJUSTMENT (QA, 2026-08-09), asserted TOGETHER because either half alone is
    // a false pass: a test that only checked the charge would go green on a build that ALSO refreshed liveness,
    // and a test that only checked "unchanged" would go green on a build that had silently dropped the billing.
    //   ⛔ TRUST class — must wait for the bind: home-liveness refresh, pending state, timers, learns, emits.
    //   ✅ ACCOUNTING class — must NOT wait: the frame really did occupy the channel. Exactly ONE charge, at the
    //      terminal shape's TRUE 6/7-B length, or the terminal bit becomes an anti-spam escape hatch.
    // ⓘ S2b's original blocker: the §mobile `last_heard_home_ms` refresh and the meter both ran ABOVE the identity
    //   check, so a terminal CTS that FAILED its bind had already refreshed liveness (a TRUST leak — the shape of
    //   [[B147]]/[[B153]]). S2b then hoisted the bind above BOTH, which fixed the leak and opened the escape hatch;
    //   S2c splits them.
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 12); cfg.leaf_id = 0;
    cfg.is_mobile = true; cfg.nav_enabled = true; CHECK(node.on_init(cfg));
    hal._now = 500;
    node.test_set_my_mobile_reg(/*home_id=*/20, /*local_id=*/33);       // stamps last_heard_home_ms = 500
    const uint64_t heard0 = node.test_last_heard_home_ms();
    CHECK(heard0 == 500);
    node.route_inject(/*dest=*/20, /*next_hop=*/20, /*hops=*/1, /*score=*/100);
    const uint8_t body[2] = { 'h', 'i' };
    hal._now = 1000;
    const uint16_t ctr = node.test_do_send_typed(/*dst=*/20, body, sizeof body, CryptIntent::def,
                                                /*override_dst_hash=*/0, /*type=*/0);
    CHECK(ctr != 0);
    const auto* rts = hal.last_tx("RTS"); CHECK(rts != nullptr);
    RtsFlightIdentity mine{};
    if (rts) { auto pr = parse_rts(std::span<const uint8_t>(rts->bytes.data(), rts->bytes.size()));
               CHECK(pr.has_value()); if (pr) mine = pr->id; }
    CHECK(rts_flight_identity_valid(mine));
    // THE EXPECTED CHARGE — the anti-spam ledger bills `airtime_routing_ms(len)`, i.e. the frame's airtime at OUR
    // routing SF / active BW / active CR (node_mac.cpp:43). `len` is the ONLY variable under test, so read the
    // other three off the node rather than restating them: the assertion is about the LENGTH, not the PHY.
    auto air_at = [&](uint16_t len) {
        return static_cast<uint32_t>(airtime_ms(cfg.routing_sf, node.active_bw_hz(), node.active_cr(),
                                                protocol::preamble_sym, len));
    };
    CHECK(air_at(6) != air_at(4));       // ⚠ the instrument must be able to TELL a terminal frame from an ordinary
                                         //   one; if these collided, "billed at 6 B" would be unfalsifiable.
    int app = 0; uint8_t nrts = 0, ncts = 0; uint32_t air0 = 0;
    node.compute_originator_metric(/*sender=*/20, app, air0, nrts, ncts);
    CHECK(air0 == 0u); CHECK(ncts == 0);                                 // precondition: home 20 owes nothing yet
    const size_t armed0 = hal.armed.size();                              // every after() this node has armed so far
    // ⛔ A terminal CTS from our HOME (tx_id = 20), addressed to US, but echoing a DIFFERENT flight.
    RtsFlightIdentity other = mine; other.bytes[1] ^= 0x01;
    { cts_in c{}; c.already_received = true; c.tx_id = 20; c.rx_id = 1; c.id = other; c.team_plane = false;
      std::array<uint8_t, 8> b{};
      const size_t n = pack_cts(c, std::span<uint8_t>(b.data(), b.size()));
      CHECK(n == 6);                                                     // the plaintext terminal wire = 3 + width 3
      RxMeta m{ 8.0f, -80.0f, 0, static_cast<int8_t>(20) };
      hal._now = 9000; node.on_recv(b.data(), n, m); }
    CHECK(hal.count("cts_terminal_mismatch") == 1);                      // it WAS refused...
    // ---- ⛔ THE TRUST CLASS: nothing at all moved.
    CHECK(node.test_last_heard_home_ms() == heard0);                      // ★★ no home-liveness refresh
    CHECK(node.has_pending_tx());                                         // the flight is untouched...
    CHECK(hal.count("cts_rx") == 0);                                      // ...and the accept tail never ran, so the
                                                                          //   two _hal.cancel()s just above it didn't
    CHECK(hal.armed.size() == armed0);                                    // ★ no timer re-armed either
    // ---- ✅ THE ACCOUNTING CLASS: charged EXACTLY once, at the TRUE 6-B length.
    uint32_t air1 = 0; node.compute_originator_metric(20, app, air1, nrts, ncts);
    CHECK(air1 == air_at(6));                                             // ★★ once, and priced as the 6-B frame it
                                                                          //   was — not as a 4-B ordinary CTS
    CHECK(ncts == 1);                                                     // one CTS observation, not two
    // ★ THE POSITIVE CONTROL: the EXACT echo from the same home DOES refresh the clock, DOES clear the flight — and
    //   is ALSO billed, which is what stops the accounting assertion above from passing on a never-bills build.
    // ⚠ t=21000, not 9500: the ledger DEDUPS a same-(kind,rx_id) event inside originator_retry_dedup_ms (10 s) by
    //   refreshing it instead of appending, so a closer positive control could not show its own charge at all.
    { cts_in c{}; c.already_received = true; c.tx_id = 20; c.rx_id = 1; c.id = mine; c.team_plane = false;
      std::array<uint8_t, 8> b{};
      const size_t n = pack_cts(c, std::span<uint8_t>(b.data(), b.size()));
      CHECK(n == 6);
      RxMeta m{ 8.0f, -80.0f, 0, static_cast<int8_t>(20) };
      hal._now = 21000; node.on_recv(b.data(), n, m); }
    CHECK(hal.count("cts_terminal_mismatch") == 1);                       // no second refusal
    CHECK(node.test_last_heard_home_ms() == 21000);                       // ★★ a BOUND answer DOES refresh it
    CHECK_FALSE(node.has_pending_tx());                                   // and completes the flight
    CHECK(hal.count("cts_rx") == 1);                                      // the accept tail DID run this time
    uint32_t air2 = 0; node.compute_originator_metric(20, app, air2, nrts, ncts);
    CHECK(air2 - air1 == air_at(6));                                      // ★★ billed too, and exactly once
}

// =============================================================================
// §hybrid-rts S2d (2026-08-09) — THE METER'S PLANE GUARD READS THE WIRE ON A TERMINAL CTS.
//
// S2c derived meter eligibility for BOTH CTS shapes from LOCAL PENDING STATE
// (`for_me_dst && _pending_tx && next_is_local_id`). For the TERMINAL shape that is
// inference where the frame STATES the fact: `CTS_TERM_PLANE_BIT` (byte-0 bit 3) is the
// wire-declared plane, and at the producer `cin.tx_id = wire_team ? team_local_id() :
// _node_id` with `cin.team_plane = cf->team_plane == wire_team` ⇒ the bit is EXACTLY
// "`c.tx_id` is a team-local id", said by the node that chose the id.
// The inference was wrong two ways, both asserted below:
//   ① no pending flight at all ⇒ guard structurally false ⇒ a TEAM-plane terminal CTS
//      billed under its LOCAL id into the GLOBAL-keyed ledger (and the metric feeds
//      originator-drop, so mis-attribution can throttle an innocent peer). ⚠ THIS IS THE
//      REACHABLE CASE, not a corner — both terminal frames the corpus bills have no
//      pending flight.
//   ② an unrelated TEAM flight pending ⇒ guard true ⇒ a STATIC terminal CTS NOT billed,
//      an accounting escape hatch keyed on our own unrelated state.
// ★★★ LINEAGE — this is the arc's signature error: inferring from local state what the
// wire declares. [[B142]] (`LbtKind` alone), [[B147]] (hash alone), [[B153]]/[[B157]]
// (a terminal verdict from ambiguous bytes), S2's "store the WIRE plane, never whichever
// predicate matched" — and now the meter.
// ⚠ THE DISCRIMINATING MUTATION for ① and ② is to RESTORE S2c's control flow (collapse the
// hoisted derivation back to the pending-state inference for both shapes), NOT to delete the
// fix; a second mutation restores S2c's UNCONDITIONAL charge at the pure-overhear arm ①.
// =============================================================================
namespace {
// A team-capable node with a team_local_id and NO pending flight — the ① substrate.
// Deliberately NOT b160_collision_node: no route is injected, because ① is about a frame
// that has nothing of ours to bind to at all.
void s2d_team_node(Node& n, TestHal& hal) {
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.is_mobile = true; cfg.team_id = 0xABCD1234u; cfg.nav_enabled = true;
    CHECK(n.on_init(cfg));
    n.set_team_local_id(93);
    CHECK(n.node_id() == 30); CHECK(n.team_local_id() == 93);
    (void)hal;
}
// Air a terminal CTS at `now` and return nothing — the assertions read the ledger.
void s2d_rx_terminal(Node& n, TestHal& hal, uint8_t tx_id, uint8_t rx_id, bool team_plane,
                     const RtsFlightIdentity& id, uint64_t now) {
    cts_in c{}; c.already_received = true; c.tx_id = tx_id; c.rx_id = rx_id; c.team_plane = team_plane;
    c.id = id;
    std::array<uint8_t, 8> b{};
    const size_t n_b = pack_cts(c, std::span<uint8_t>(b.data(), b.size()));
    CHECK(n_b == 6);                          // the canonical plaintext terminal wire (3 + width 3)
    RxMeta m{ 8.0f, -80.0f, 0, static_cast<int8_t>(tx_id) };
    hal._now = now; n.on_recv(b.data(), n_b, m);
}
uint32_t s2d_air_of(Node& n, uint8_t sender) {
    int app = 0; uint8_t nrts = 0, ncts = 0; uint32_t air = 0;
    n.compute_originator_metric(sender, app, air, nrts, ncts);
    return air;
}
uint8_t s2d_ncts_of(Node& n, uint8_t sender) {
    int app = 0; uint8_t nrts = 0, ncts = 0; uint32_t air = 0;
    n.compute_originator_metric(sender, app, air, nrts, ncts);
    return ncts;
}
}  // namespace

TEST_CASE("§hybrid-rts S2d ① — an UNBOUND terminal CTS (no pending flight) is billed per the plane the FRAME "
          "DECLARES: a TEAM one is not charged to a global peer, a STATIC one is") {
    // ⚠ THE REACHABLE CASE. S2c filed this as an unclosable residual on the grounds that "the CTS carries no
    // plane mark" — true of the ORDINARY shape, FALSE of this one. With no pending flight the S2c guard is
    // structurally false, so EVERY team-plane terminal CTS was billed under its team-local id.
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    s2d_team_node(node, hal);
    CHECK_FALSE(node.has_pending_tx());                    // ★ the precondition that made S2c's guard false
    CHECK_FALSE(node.for_me_dst(7));                       // 7 is neither our node_id (30) nor our team id (93)
    CHECK(node.for_me_dst(30));                            // ...and 30 is, so the two arms below are distinct paths
    auto air_at = [&](uint16_t len) {
        return static_cast<uint32_t>(airtime_ms(7, node.active_bw_hz(), node.active_cr(),
                                               protocol::preamble_sym, len));
    };
    CHECK(air_at(6) != air_at(4));   // ⚠ ANTI-VACUITY: the instrument must be able to TELL a 6-B terminal frame
                                     //   from a 4-B ordinary one, or "billed at 6 B" is unfalsifiable.
    const RtsFlightIdentity id = rts_flight_identity_plain(/*origin=*/9, /*ctr=*/0x0031);
    // ⛔ (a) PURE OVERHEAR, TEAM plane (arm ① in handle_cts): tx_id 44 is a team-LOCAL id.
    s2d_rx_terminal(node, hal, /*tx_id=*/44, /*rx_id=*/7,  /*team_plane=*/true,  id, /*now=*/1000);
    CHECK(s2d_air_of(node, 44) == 0u);                     // ★★ NOT charged to global node 44
    CHECK(s2d_ncts_of(node, 44) == 0);
    // ⛔ (b) ADDRESSED TO US but with NOTHING to bind to (arm ③, `bound == false`), TEAM plane.
    //     ⓘ This branch emits NOTHING — `cts_terminal_mismatch` fires only when a flight existed to name — which
    //       is why a telemetry counter could not have measured this case. (S2c's inertness prediction did exactly
    //       that and was wrong.)
    s2d_rx_terminal(node, hal, /*tx_id=*/45, /*rx_id=*/30, /*team_plane=*/true,  id, /*now=*/3000);
    CHECK(hal.count("cts_terminal_mismatch") == 0);        // ★ no flight ⇒ no emit: the counter is blind here
    CHECK(s2d_air_of(node, 45) == 0u);                     // ★★ NOT charged to global node 45
    // ★ THE POSITIVE CONTROLS, one per arm. Without them (a)/(b) would pass on a build that bills NOTHING.
    //   Same shape, same absence of a pending flight, the only difference is the WIRE PLANE BIT.
    s2d_rx_terminal(node, hal, /*tx_id=*/46, /*rx_id=*/7,  /*team_plane=*/false, id, /*now=*/5000);
    CHECK(s2d_air_of(node, 46) == air_at(6));              // ★★ a STATIC overheard terminal CTS IS billed, at 6 B
    CHECK(s2d_ncts_of(node, 46) == 1);
    s2d_rx_terminal(node, hal, /*tx_id=*/47, /*rx_id=*/30, /*team_plane=*/false, id, /*now=*/7000);
    CHECK(s2d_air_of(node, 47) == air_at(6));              // ★★ and so is an unbindable STATIC one addressed to us
    CHECK(s2d_ncts_of(node, 47) == 1);
}

TEST_CASE("§hybrid-rts S2d ② — a mismatched STATIC terminal CTS is STILL BILLED while an unrelated TEAM flight "
          "is pending (S2c's guard skipped it: our own state silenced somebody else's frame)") {
    // THE MIRROR ERROR of ①. `next_is_local_id(pt.addr_len, pt.next)` is true for a TEAM flight (is_team_peer),
    // so S2c's guard suppressed the charge for ANY terminal CTS reaching the meter — including a STATIC one from
    // an unrelated node. That is an accounting escape hatch keyed on OUR pending state, not on the frame.
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    b160_collision_node(node, hal);                        // team peer 50 known; _rt_team 50 -> 50
    hal._now = 1000;
    CHECK(b160_send(node, /*dst=*/50, Plane::TEAM).code == CmdCode::queued);
    { rts_out r{}; CHECK(b160_last_rts(hal, r));
      CHECK(r.next == 50); CHECK(r.addr_len == 1); CHECK(r.mobile_src); }   // ★ a genuine TEAM flight is in flight
    CHECK(node.has_pending_tx());
    CHECK(node.is_team_peer(50));                          // ⇒ next_is_local_id(0, 50) is TRUE: S2c's guard fires
    auto air_at = [&](uint16_t len) {
        return static_cast<uint32_t>(airtime_ms(7, node.active_bw_hz(), node.active_cr(),
                                               protocol::preamble_sym, len));
    };
    CHECK(air_at(6) != air_at(4));                         // ⚠ anti-vacuity, as in ①
    CHECK(s2d_air_of(node, 44) == 0u);                     // precondition: node 44 owes nothing
    // ⛔ A STATIC terminal CTS from an UNRELATED node 44 (not our next hop 50), addressed to our node_id.
    //    `c.tx_id != pt.next` ⇒ `bound` stays false with NO emit — the same emit-less branch ①(b) uses.
    s2d_rx_terminal(node, hal, /*tx_id=*/44, /*rx_id=*/30, /*team_plane=*/false,
                    rts_flight_identity_plain(/*origin=*/9, /*ctr=*/0x0031), /*now=*/9000);
    CHECK(hal.count("cts_terminal_mismatch") == 0);        // not our next hop ⇒ no emit (counter blind again)
    CHECK(node.has_pending_tx());                          // ★ TRUST class untouched: our team flight survives
    CHECK(hal.count("cts_rx") == 0);                       // the accept tail never ran
    CHECK(s2d_air_of(node, 44) == air_at(6));              // ★★ THE ASSERTION: billed anyway, at its true 6 B
    CHECK(s2d_ncts_of(node, 44) == 1);                     // exactly once
}

TEST_CASE("§hybrid-rts S2d ③ — a VALID terminal CTS still does its legitimate work on BOTH planes, and is "
          "accounted per the plane it declares (STATIC billed; TEAM correctly not, its tx_id is a local id)") {
    // ★ THE INVARIANCE CONTROL: the fix must not disturb a bound terminal answer. It is deliberately GREEN under
    //   both mutations below — its job is to prove ①/② were not bought by breaking the accept path.
    // ⚠ A BRIEF PREMISE THAT DOES NOT SURVIVE THE CODE: "billed on BOTH planes" is not achievable and not wanted.
    //   On a bound TEAM flight `c.tx_id == pt.next` IS a team-local id, so charging it is precisely the
    //   mis-attribution ① removes. "Billed per its declared plane" is the property; for TEAM that means NOT billed.
    //   The STATIC half of ③ is covered end-to-end by the S2c case above (bind + refresh + clear + charge); this
    //   case adds the TEAM half, which had no coverage at all.
    TestHal hal; Node node(hal, /*id=*/30, /*key=*/0x3030u);
    b160_collision_node(node, hal);
    hal._now = 1000;
    CHECK(b160_send(node, /*dst=*/50, Plane::TEAM).code == CmdCode::queued);
    rts_out aired{}; CHECK(b160_last_rts(hal, aired));
    CHECK(aired.next == 50); CHECK(aired.src == 93);       // the TEAM wire marks the echo must match
    CHECK(rts_flight_identity_valid(aired.id));
    CHECK(node.has_pending_tx());
    CHECK(s2d_air_of(node, 50) == 0u);                     // precondition
    // ✅ THE EXACT echo from our next hop 50, on the TEAM plane, addressed to our TEAM id 93.
    s2d_rx_terminal(node, hal, /*tx_id=*/50, /*rx_id=*/93, /*team_plane=*/true, aired.id, /*now=*/9000);
    CHECK(hal.count("cts_terminal_mismatch") == 0);        // ★★ it BOUND (identity + width + domain + plane)
    CHECK(hal.count("cts_rx") == 1);                       // ★★ and the accept tail RAN — the legitimate work
    CHECK_FALSE(node.has_pending_tx());                    // ★★ the flight is terminally cleared, as designed
    CHECK(s2d_air_of(node, 50) == 0u);                     // ★★ and NOT billed: 50 here is a team-LOCAL id
    // ★ POSITIVE CONTROL for the accounting half, so "== 0" above is not a never-bills tautology: the SAME node
    //   id, a STATIC terminal CTS, IS charged. t = 21000 because the ledger dedups a same-(kind, rx_id) event
    //   inside originator_retry_dedup_ms (10 s) by REFRESHING it — a closer control could not show its own charge.
    auto air_at = [&](uint16_t len) {
        return static_cast<uint32_t>(airtime_ms(7, node.active_bw_hz(), node.active_cr(),
                                               protocol::preamble_sym, len));
    };
    CHECK(air_at(6) != air_at(4));                         // ⚠ anti-vacuity
    s2d_rx_terminal(node, hal, /*tx_id=*/50, /*rx_id=*/7, /*team_plane=*/false,
                    rts_flight_identity_plain(/*origin=*/9, /*ctr=*/0x0031), /*now=*/21000);
    CHECK(s2d_air_of(node, 50) == air_at(6));              // ★★ the instrument does bill this sender when told to
}

TEST_CASE("§hybrid-rts S2 — a `(1,0)` frame naming a STATIC team member's team id is OVERHEARD, not admitted "
          "(the wire plane is what decides, and the pre-S2 OR-admission got this wrong)") {
    // ★★ THE OTHER HALF OF THE §18 NUMERIC COLLISION, and the case that makes the discriminator load-bearing.
    // A host last-miles to ITS hosted mobile with `(addr_len=1, mobile_src=0)`. A NON-mobile team member whose
    // `_team_local_id` happens to equal that mobile's local id satisfies `team_addr_for_us` — and nothing else.
    // Pre-S2 the OR-admission CTS'd that frame, took a reservation for someone else's exchange, and would now
    // have stored a plane the wire never declared. The wire says STATIC; `for_static_rts` is false (we are not
    // mobile, and our node_id is not 17); therefore we are simply not the addressee.
    const uint32_t TEAM = 0x06EF37AEu;
    TestHal hal; Node n(hal, /*node_id=*/5, /*key=*/0xA005u);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0;
    cfg.is_mobile = false; cfg.team_id = TEAM; cfg.nav_enabled = true; CHECK(n.on_init(cfg));
    n.set_team_local_id(17);
    CHECK(n.team_local_id() == 17);
    RxMeta from31{ 12.0f, -70.0f, 0, static_cast<int8_t>(31) };
    std::array<uint8_t, 16> rb{};
    rts_in r{}; r.leaf_id = 0; r.src = 31; r.next = 17; r.ctr_lo = 5; r.dst = 17; r.sf_index = 3;
    r.payload_len = 15; r.addr_len = 1; r.mobile_src = false;                  // ⇐ the wire says STATIC/GLOBAL
    r.id = rts_flight_identity_plain(/*origin=*/1, /*ctr=*/0x0005);
    const size_t rn = pack_rts(r, std::span<uint8_t>(rb.data(), rb.size()));
    CHECK(rn == 10);
    hal._now = 1000; n.on_recv(rb.data(), rn, from31);
    CHECK(hal.count("cts_tx") == 0);                     // ★ NOT admitted — we are not its addressee
    CHECK(hal.count("rts_rx") == 0);
    CHECK(n.nav_until_ms() > 0);                         // ...and it took the ordinary OVERHEAR path (NAV armed)
    // ★ THE POSITIVE CONTROL, without which the silence above proves nothing: the SAME frame with the TEAM
    //   declaration `(1,1)` IS addressed to us and IS admitted.
    std::array<uint8_t, 16> rb2{};
    rts_in t = r; t.mobile_src = true; t.ctr_lo = 6; t.id = rts_flight_identity_plain(1, 0x0006);
    const size_t rn2 = pack_rts(t, std::span<uint8_t>(rb2.data(), rb2.size()));
    hal._now = 2000; n.on_recv(rb2.data(), rn2, from31);
    CHECK(hal.count("cts_tx") == 1);
    CHECK(hal.count("rts_rx") == 1);
}
