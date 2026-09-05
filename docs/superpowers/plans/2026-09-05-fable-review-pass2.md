<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Fable code review — pass 2 (2026-09-05): the seams Slice 1/1b/2 build on

Owner ruling 2026-09-04/05: *"first pass after 0a closes, then the seams"* / *"start pass 2"*. Reviewed at
`HEAD a018f65`, tree clean. Standard: `docs/CODE_GUIDELINES.md` + the CLAUDE.md board. Every finding is verified
against the source; the behavioural one is REPRODUCED by execution against the native `libcore.a` (harness verbatim
below — the scratchpad is volatile, this file is the durable copy). Findings are DRAFT register rows (M1) for the
Author to land; the owner rules what is fixed and where. I edited nothing.

## Scope read

| seam | why it matters to the feature phase | verdict |
| --- | --- | --- |
| `lib/core/node_mac_rx.cpp` `handle_data` typed pre-tail chain :1914 (MOBILE_SEND fork), :2101-2214 (mobile / H-answer / REMOTE_CMD+RESP staging / E2E_ACK), :2263-2347 (custody failure, INTRO, SEALED_RELAY, CRYPTED trial, TEAM_KEY_GRANT), :2385-2408 (the fail-closed internal guard), the delivery tail | R-RA-19's Slice 1b refactor target; where v2 request/response carriers will be consumed | S3 seam notes; no defect in the chain itself |
| `lib/core/node_hashlocate.cpp` `send_by_hash` :1610-1810 (INTRO attach, team arm, authoritative binding, the delegated wrapper arms) | R-RA-13's mandatory-SOURCE_HASH path | **S1 (HIGH)** — the hashes are dropped for size |
| `lib/core/node_mac.cpp` `enqueue_data` :99-430, `do_send` :523, `enqueue_cross_layer` :576-660, the TX-time `dlen == 0` bail :2193-2215, the legacy `send_remote_cmd/response` :828-834 | the same-layer and cross-layer carriers | **S1** root cause; S2 comment drift; the cross-layer sibling is the honest reference |

## S1 · HIGH · SILENT FIELD DROP → SILENT LOSS — `enqueue_data` treats `DST_HASH` and `SOURCE_HASH` as optional-for-size

**Claim.** `lib/core/node_mac.cpp:217-226`: for an application DM the two hash fields are set ONLY IF they still fit
the 241-byte inner (`4 + 1 + body_len <= 241` for DST_HASH; `after_origin + 4 + body_len <= 241` for SOURCE_HASH).
`dm_max_body_bytes` (239) admits bodies that do not leave room for them, so for a plaintext DM of body 233..239 the
sender silently downgrades the carrier instead of refusing. Three consequences, from mild to severe:

1. **Static sender, bound target, body 233-236:** airs with `DST_HASH` but WITHOUT `SOURCE_HASH` — the receiver's
   `sender_hash` is 0 (inbox dedup identity and cross-layer E2E-ack identity lost). **Body 237-239:** airs with
   NEITHER hash — a by-hash `send` becomes a by-id send, deliverable to whoever holds the id (the id→hash binding is
   the one unverifiable link; DST_HASH is the routing instruction that pins the intended holder).
2. **Registered MOBILE sender, unresolved target (Slice 0d arm 3, the delegated `MOBILE_SEND` wrapper), body
   233-236:** `override_dst_hash` sets DST_HASH unconditionally, SOURCE_HASH is dropped for size. At the home, the
   MOBILE_SEND fork REQUIRES `ui->has_source_hash` (`node_mac_rx.cpp:1914`), so the wrapper is not delegated;
   `DATA_TYPE_MOBILE_SEND` is application-bearing (`frame_codec.h:884`, `internal = false`), so the fail-closed
   internal guard (:2403) does NOT catch it either — the wrapper body reaches the ORDINARY DELIVERY TAIL at the
   home: `record_dm` + `msg_recv` in the home's own inbox, from the mobile's local id, sender_hash 0. The mobile got
   `queued` + a link ACK; the target never sees the message; no `send_failed`. (Chain verified step by step in
   source; the RX-side reproduction is owed by the fixing slice.)
3. **Registered MOBILE sender, body 237-239:** `4 + 1 + body > 241` — `pack_unicast_inner` returns 0 and the
   plaintext arm at :398 never checks it: the item is QUEUED with `inner_len = 0`. At TX the RTS airs, then
   `pack_data` fails and the :2193 bail fires — which is ruled "deliberately silent for everybody" ([[B268]] note,
   :2202-2212: log + device-stripped telemetry, no push). ⚠ The bail's own comment (:2197) says it is "NOW
   UNREACHABLE FROM AN ORIGINATION"; that is true for the sealed arm only and FALSE for this one (S2).

**Measurement A — registered mobile, delegated plain DM (reproduction, linked against `.pio/build/native/lib6be/libcore.a` at `a018f65`):**
```
body=200 ctr=1 admit=1 queued=1  type=0x02 dst=2 flags=0x06 DST_HASH=1 SOURCE_HASH=1 inner_len=209  emits:
body=232 ctr=1 admit=1 queued=1  type=0x02 dst=2 flags=0x06 DST_HASH=1 SOURCE_HASH=1 inner_len=241  emits:
body=233 ctr=1 admit=1 queued=1  type=0x02 dst=2 flags=0x02 DST_HASH=1 SOURCE_HASH=0 inner_len=238  emits:
body=236 ctr=1 admit=1 queued=1  type=0x02 dst=2 flags=0x02 DST_HASH=1 SOURCE_HASH=0 inner_len=241  emits:
body=237 ctr=1 admit=1 queued=1  type=0x02 dst=2 flags=0x02 DST_HASH=1 SOURCE_HASH=0 inner_len=0  emits:
body=239 ctr=1 admit=1 queued=1  type=0x02 dst=2 flags=0x02 DST_HASH=1 SOURCE_HASH=0 inner_len=0  emits:
```
(type 0x02 = MOBILE_SEND to the home; flags 0x06 = DST_HASH|SOURCE_HASH, 0x02 = DST_HASH only; `admit=1` = queued.)

**Measurement B — static sender, authoritative binding 3↔0x33333333, plain DM by hash:**
```
static body=232 ctr=1 admit=1  type=0x00 dst=3 flags=0x06 DST_HASH=1 SOURCE_HASH=1 inner_len=241
static body=233 ctr=1 admit=1  type=0x00 dst=3 flags=0x02 DST_HASH=1 SOURCE_HASH=0 inner_len=238
static body=236 ctr=1 admit=1  type=0x00 dst=3 flags=0x02 DST_HASH=1 SOURCE_HASH=0 inner_len=241
static body=237 ctr=1 admit=1  type=0x00 dst=3 flags=0x00 DST_HASH=0 SOURCE_HASH=0 inner_len=238
static body=239 ctr=1 admit=1  type=0x00 dst=3 flags=0x00 DST_HASH=0 SOURCE_HASH=0 inner_len=240
```

**Why this is the feature phase's problem, not just a DM bug.** R-RA-13 makes clear-inner `SOURCE_HASH` MANDATORY on
every v2 carrier through this exact `send_by_hash` / `do_send(app_dm=true)` path; Slice 2 derives
`remote_body_cap` assuming the hash is always present (the 0e table's 226 for cross-layer, 231/232 same-layer). A
v2 request of maximal size would be silently stripped of the field the AEAD binds as AAD — the target rejects or,
worse, mis-attributes. The cross-layer sibling already does it right: `enqueue_cross_layer` sets
`DST_HASH|SOURCE_HASH` UNCONDITIONALLY and refuses on `n == 0` (`node_mac.cpp:590-598` → `err_too_large`,
`xl_send_too_large`). `test_node_r3.cpp`'s §B20/B21 sweep pins that a 239-byte plaintext "AIRS" (:7402) but never
asserts the flags, so the drop is invisible to the gate today.

**Remedy (owner's call — a BEHAVIOUR change, its own slice, corpus-attributed).** Recommended: **(a) one honest cap** —
an application DM either carries both hashes or refuses `too_large`: `enqueue_data` sets the flags unconditionally
for `app_dm` (as the cross-layer sibling does) and refuses when the inner overflows (`push_send_failed(too_large)`,
`err_too_large`), which makes the effective plaintext body cap `241 − 1 − 4 − 4 = 232` (`dm_max_body_bytes` derived
from it, not 239; the 0f BLE derivation follows automatically through `protocol::dm_max_body_bytes`). Rejected
alternative **(b)**: keep 239 for by-id DMs and refuse only on the `override_dst_hash`/delegated arms — two caps and
a by-hash `send` that still quietly becomes by-id. Either way: the plaintext arm checks `pack_unicast_inner`'s
return (never queue `inner_len 0`), the r3 sweep gains flag assertions, and a native case reproduces shape 2 at the
home (wrapper without SOURCE_HASH must REFUSE, never deliver locally). Prediction-first: s18 and the corpus have 0
DMs in the 233-239 band? — the fixing slice measures it (`delivered` payload lengths) before claiming 36/36.

## S2 · LOW · COMMENT DRIFT (V1) — the B20 bail claims unreachability it no longer has

`node_mac.cpp:2197-2199`: "IT IS NOW UNREACHABLE FROM AN ORIGINATION: enqueue_data sizes the seal against
`data_inner_cap`" — the sealed arm is sized; the PLAINTEXT arm (:398-402) is not (S1 shape 3 reaches the bail).
Fix with S1 (same lines).

## S3 · seam notes for the Slice 1b brief (no defect today; obligations the brief must carry)

- The legacy `REMOTE_CMD`/`REMOTE_RESP` staging arm (`node_mac_rx.cpp:2195-2212`) sits BEFORE both open steps
  (SEALED_RELAY :2313, CRYPTED trial :2322) and reads the CLEARTEXT `ui->body`; it does not require
  `ui->has_source_hash` and keys the reply on the 8-bit `pa.origin` (`_remote_inbound.from`). Fine for the legacy
  `app_dm=false` senders (:828-834, no SOURCE_HASH by construction); the v2 accept entry point (Slices 5/7b) must
  REQUIRE `has_source_hash` and key on `ui->source_hash` (R-RA-13); 1b's behaviour-neutral split must not move the
  arm past the open steps (a v2 body is RPC-encrypted inside a plaintext-framed DM — the design's carrier).
- One shared slot: `_remote_inbound` (node.h:2905, UNCONDITIONAL, `inbox_max_body` = 241 B of RAM on every profile
  incl. client-only mobiles) with a drop-full path (`remote_inbound_drop_full`). R-RA-22's partitioned admission
  (2 ingress headers + open/bootstrap staging) replaces it in Slice 5; 1b must keep the drop attributable and gate
  the accept-side slot by role (R-RA-8/17) so a mobile build stops paying for it.
- `if (n > protocol::inbox_max_body) n = inbox_max_body;` (:2205) is a silent clamp by shape; a no-op today
  (body ≤ 241) — the v2 arm must refuse, never clamp (C2).
- The fail-closed guard (:2403) protects only `traits.internal` types. v2's REMOTE_* carriers must be allocated as
  INTERNAL (0x80..0xBF, per the §CUSTODY-A namespace) so a role-disabled build drops them there — an
  application-bearing allocation would fall to the delivery tail exactly as S1 shape 2 does.

## Harness (verbatim; compile with the native env's flags, link the four native archives)

```
g++ -std=gnu++2a -fno-exceptions -fno-rtti -Og -DPLATFORMIO=60119 -DMR_RADIO_CANARY=1 -DPROTOCOL_VERSION=1 -DMR_CONSOLE=1 -DLORA_FREQ=869.4625 -DLORA_BW=125.0 -DLORA_SF=8 -DLORA_CR=5 -DLORA_TX_POWER=22 -DLORA_PREAMBLE_SYM=16 -DLORA_DUTY_CYCLE_PCT=10 -DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2 -Ilib/core -Ilib/hal -Ilib/monocypher/src -Itest -Isrc repro.cpp .pio/build/native/lib6be/libcore.a .pio/build/native/libf08/libconsole.a .pio/build/native/lib095/libhal.a .pio/build/native/lib1d5/libmonocypher.a
```
`repro.cpp` (Measurement A):
```cpp
// Scratch reproduction (QA pass 2): does a registered mobile's plain DM by unresolved hash lose SOURCE_HASH / inner
// for bodies near dm_max_body_bytes? Links the native libcore.a; nothing in the repo is touched.
#include "node.h"
#include "support/test_hal.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace meshroute;
namespace {
class StubHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    TxResult tx(const uint8_t*, size_t, const TxParams&) override { return TxResult::ok; }
    void     set_rx_sf(int) override {}
    void     set_rx_freq(double) override {}
    void     set_rx_bw(uint32_t) override {}
    void     set_rx_cr(uint8_t) override {}
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    bool     after(uint32_t, uint32_t) override { return true; }
    void     cancel(uint32_t) override {}
    void     set_protocol_id(int) override {}
    void     emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind); }
};
}
namespace meshroute {
struct DualLayerTestAccess {
    static void mobile(Node& n, uint8_t home, uint32_t hk) {
        n._cfg.is_mobile = true; n._joined = true;
        n._my_mobile_reg.active = true; n._my_mobile_reg.home_id = home;
        n._my_mobile_reg.home_key_hash32 = hk; n._my_mobile_reg.my_local_id = n.node_id();
    }
    static uint8_t  txn(Node& n)            { return n._active->_tx_queue_n; }
    static uint16_t sbh(Node& n, uint32_t h, const uint8_t* b, uint8_t bl, Node::SendDispatch* d) {
        return n.send_by_hash(h, b, bl, /*flags*/0, CryptIntent::off, /*reply_to*/0, /*mobile_ctr*/0,
                              Plane::AUTO, /*type*/0, /*suppress_intro*/true, d);
    }
};
}
int main() {
    const uint8_t lens[] = { 200, 232, 233, 236, 237, 239 };
    for (uint8_t len : lens) {
        StubHal hal; Node node(hal, /*id=*/5, /*key=*/0x11111111u);
        NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1; cfg.is_mobile = true;
        if (!node.on_init(cfg)) { std::printf("on_init failed\n"); return 2; }
        DualLayerTestAccess::mobile(node, /*home*/2, 0x22222222u);
        node.test_suspend_tx_drain(true);   // keep the item in the queue so its flags/inner are inspectable
        std::vector<uint8_t> body(len, 'B');
        Node::SendDispatch d{};
        const uint8_t before = DualLayerTestAccess::txn(node);
        const uint16_t ctr = DualLayerTestAccess::sbh(node, 0xDEADBEEFu, body.data(), len, &d);
        const uint8_t after = DualLayerTestAccess::txn(node);
        std::printf("body=%3u ctr=%u admit=%d queued=%d", len, ctr, static_cast<int>(d.admit), after - before);
        if (after > before) {
            const uint8_t i = static_cast<uint8_t>(after - 1);
            uint8_t il = 0; node.test_tx_inner(i, il);
            const uint8_t f = node.test_tx_flags(i);
            std::printf("  type=0x%02x dst=%u flags=0x%02x DST_HASH=%d SOURCE_HASH=%d inner_len=%u",
                        node.test_tx_type(i), node.test_tx_dst(i), f,
                        !!(f & DATA_FLAG_DST_HASH), !!(f & DATA_FLAG_SOURCE_HASH), il);
        }
        std::printf("  emits:");
        for (auto& e : hal.emits) if (e.find("send") != std::string::npos || e.find("too_large") != std::string::npos || e.find("refus") != std::string::npos || e.find("fail") != std::string::npos) std::printf(" %s", e.c_str());
        std::printf("\n");
    }
    return 0;
}
```
`repro_b.cpp` (Measurement B):
```cpp
// Scratch reproduction B (QA pass 2): a STATIC sender, authoritative id<->hash binding, plaintext DM by hash.
#include "node.h"
#include "support/test_hal.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace meshroute;
namespace {
class StubHal : public mrtest::TestHalBase {
public:
    std::vector<std::string> emits;
    TxResult tx(const uint8_t*, size_t, const TxParams&) override { return TxResult::ok; }
    void set_rx_sf(int) override {} void set_rx_freq(double) override {} void set_rx_bw(uint32_t) override {} void set_rx_cr(uint8_t) override {}
    uint64_t airtime_used_ms(uint64_t) override { return 0; }
    bool after(uint32_t, uint32_t) override { return true; } void cancel(uint32_t) override {} void set_protocol_id(int) override {}
    void emit(const char* kind, const EventField*, size_t) override { emits.push_back(kind); }
};
}
namespace meshroute { struct DualLayerTestAccess { static uint16_t sbh(Node& n, uint32_t h, const uint8_t* b, uint8_t bl, Node::SendDispatch* d) { return n.send_by_hash(h, b, bl, 0, CryptIntent::off, 0, 0, Plane::AUTO, 0, true, d); } }; }
int main() {
    const uint8_t lens[] = { 232, 233, 236, 237, 239 };
    for (uint8_t len : lens) {
        StubHal hal; Node node(hal, /*id=*/5, /*key=*/0x11111111u);
        NodeConfig cfg; cfg.routing_sf = 8; cfg.allowed_sf_bitmap = static_cast<uint16_t>(1u << 8); cfg.leaf_id = 1;
        if (!node.on_init(cfg)) { std::printf("on_init failed\n"); return 2; }
        node.test_id_bind_set(/*id=*/3, 0x33333333u, /*authoritative=*/true);
        node.test_suspend_tx_drain(true);
        std::vector<uint8_t> body(len, 'B');
        Node::SendDispatch d{};
        const uint16_t ctr = DualLayerTestAccess::sbh(node, 0x33333333u, body.data(), len, &d);
        std::printf("static body=%3u ctr=%u admit=%d", len, ctr, static_cast<int>(d.admit));
        if (d.admit == Node::SendDispatch::Admit::queued) {
            uint8_t il = 0; node.test_tx_inner(0, il); const uint8_t f = node.test_tx_flags(0);
            std::printf("  type=0x%02x dst=%u flags=0x%02x DST_HASH=%d SOURCE_HASH=%d inner_len=%u", node.test_tx_type(0), node.test_tx_dst(0), f, !!(f & DATA_FLAG_DST_HASH), !!(f & DATA_FLAG_SOURCE_HASH), il);
        }
        std::printf("\n");
    }
    return 0;
}
```
