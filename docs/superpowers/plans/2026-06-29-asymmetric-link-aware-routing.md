# Asymmetric-Link-Aware Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to drive this plan. Each task is strict TDD (write the failing test, watch it fail, implement the minimal real code, watch it pass, commit). Do NOT batch tasks; do NOT skip the "expect FAIL" step. The user handles all git commits and metal flashing — slices are sequenced so 1–3 are delivery-neutral and safe to flash ahead of the BASELINE-gated 4–6.
>
> **⚠ COMMIT POLICY ([[user-handles-commits]]):** every per-task "Step 5: Commit" is a **gate-and-STOP boundary, NOT a `git` command** — run the gate (`pio test -e native` etc.), confirm green, and **leave the work uncommitted for the user**. Ignore the literal `git add && git commit` text in the steps; the user runs every commit.

**Goal:** Give the route plane a first-class **bidirectionality signal**. Nodes learn which links complete an RTS→CTS→DATA→ACK handshake by reading *who their neighbours hear* (the reused `hops==1` route entries plus a 1-bit completeness flag), then **prefer** confirmed-bidirectional paths, **propagate** one-way knowledge as a transitive `degraded` bit, **stop** the doomed ~9–80-RTS storm on handshake-isolated nodes (e.g. node-72 topology's 204/247) via a slow re-probe, and **recover** for free when a link flips back — all keep-don't-delete and backward-compatible. This is the algorithm-level answer the saturation investigation converged on (the three MAC-level levers were all refuted): delivery is topology/asymmetry-bound, not MAC-bound.

**Architecture:** A NEW **bidirectionality plane** orthogonal to the existing liveness plane (which `clear_peer_suspect` keeps falsely-HEALTHY on an isolated peer's beacons). The plane has three parts: (1) **detect** — an ingest scan over a neighbour's beacon route entries (`[dest==self,next==self,hops==1]` present ⇒ confirmed bidi; absent AND the neighbour's complete-flag set ⇒ one_way; absent AND incomplete ⇒ no change), recording into a per-`LayerRuntime` `_link_bidi[256]` keyed by node-id; (2) **select** — a soft `bidi_penalty_q4` folded into `effective_score` (rides the SORT, never a `next_hop_selectable` hard gate, so a sole one-way route stays selectable); (3) **advertise + reprobe** — a leaf-only periodic direct-neighbour **census** packing mode (a NEW partition pass, NOT Phase-2 rotation reuse) that emits all `hops==1` entries + sets the complete-flag only when the full set fits the live `beacon_max_entries()`, a transitive on-wire `degraded` route-entry bit, and a slow re-probe (one RTS per `link_reprobe_ttl_ms`) intercepting the giveup path BEFORE `try_cascade_requeue`. **Seven independently gateable slices.** Two backward-compatible wire bits (default 0, no `wire_version` hard-bump). **Keep-don't-delete:** one-way routes stay in the table, reversible, so recovery is free (no re-discovery ceremony). The degraded effective state is **recomputed live** each `rt_merge` (`degraded_from_wire OR _link_bidi[next_hop]==one_way`), never a sticky cached OR (MF5/OI1); confirmation decay targets `unknown` only, never `one_way` (MF6).

**Tech Stack:** C++20, doctest native tests (`pio test -e native`), lus sim (`cmake --build ~/lora-universal-simulator/build --target lus -j8`, run on `simulation/topo_9node.json`), nRF52/ESP32 boards (all 4 build-gated).

## Shared interface contract

The coder MUST use these EXACT names/types/signatures across all slices — do not invent variants. Slot zero-init makes `unknown=0` the safe default for any cold `_link_bidi[]` slot.

```cpp
// node.h — the bidi link-state enum
enum class LinkBidi : uint8_t { unknown = 0, confirmed = 1, one_way = 2 };
//   unknown=0 so a zeroed LayerRuntime slot defaults correctly.

// node.h LayerRuntime (zero-initialized) — two new arrays, keyed by node_id:
uint8_t  _link_bidi[256]              = {};   // value = a LinkBidi
uint64_t _link_bidi_confirmed_ms[256] = {};   // last real-CTS / heard-set confirmation time
//   (DEDICATED decay source — do NOT overload _dest_seen_ms).

// node.h RtCandidate — the wire-inherited degraded component ONLY (a fact about what the advertiser said):
bool degraded_from_wire = false;

// frame_codec.h beacon_entry — WIRE = route-entry byte-2 bit 3 (b3, one of the 3 genuinely-free rsv bits b3..1):
bool degraded = false;

// frame_codec.h decoded-beacon struct — WIRE = beacon byte-3 bit 4 ONLY (b3..0 is wire_version — DO NOT TOUCH):
bool heard_set_complete = false;

// Node methods (declared in node.h; defined in node_routing.cpp unless noted):
int16_t bidi_penalty_q4(uint8_t next_hop) const;
//   returns protocol::bidi_penalty_one_way_q4 if _link_bidi[next_hop]==one_way, else 0 (unknown AND confirmed both 0).
bool    candidate_degraded(const RtCandidate& c) const;
//   c.degraded_from_wire || _link_bidi[c.next_hop]==(uint8_t)LinkBidi::one_way   (the LIVE recompute — never a sticky cache).
void    note_link_confirmed(uint8_t next_hop);
//   _link_bidi[next_hop]=confirmed; _link_bidi_confirmed_ms[next_hop]=now; fan out via
//   resort_routes_for_neighbor_penalty; MR_EMIT link_bidi_confirm (and link_recover if it was one_way).
void    update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* entries, uint8_t n, bool complete);  // node_beacon.cpp
//   the detection scan: present[dest==self]->confirmed; absent && complete->one_way (MR_EMIT link_one_way);
//   absent && !complete->no change.
void    decay_link_bidi(uint8_t next_hop);
//   if confirmed and now - _link_bidi_confirmed_ms[next_hop] >= bidi_confirm_ttl_ms -> set to unknown (NEVER one_way).

// effective_score (node_routing.cpp:83) becomes:
//   score - budget_penalty_q4 - liveness_penalty_q4 - bidi_penalty_q4(next_hop)

// protocol_constants.h (near peer_silent_penalty_q4 / next_hop_live_ttl_ms):
inline constexpr int16_t  bidi_penalty_one_way_q4       = peer_silent_penalty_q4;   // 640 Q4 seed (fallback peer_suspect_penalty_q4=192 if metal lucky-marginal gate strands good-RF one-way routes)
inline constexpr uint64_t bidi_confirm_ttl_ms           = next_hop_live_ttl_ms;     // 1200000
inline constexpr uint64_t link_reprobe_ttl_ms           = 60000;
inline constexpr uint8_t  heard_set_census_min_headroom = 4;   // census engages only if the full hops==1 set fits leaving >= this many slots in the live beacon_max_entries()

// Telemetry events (MR_EMIT, gated off on metal per the USB-CDC discipline):
//   link_bidi_confirm, link_one_way, degraded_advertise, link_reprobe, link_recover
```

**Penalty ordering invariant:** confirmed ≻ unknown ≻ one_way/degraded. `unknown==confirmed==0` penalty (OI2: only positively-confirmed `one_way` is demoted — a nudge on `unknown` would punish every not-yet-probed link on a cold mesh). The penalty rides the sort, so a degraded route **stays selectable when it is the sole candidate**.

## File structure

| File | Responsibility |
|---|---|
| `lib/core/frame_codec.h` | Add `bool degraded` to `beacon_entry` and `bool heard_set_complete` to the decoded-beacon struct; fix the stale `byte 3 : rsv(b4..0)` comment (line 43) to `rsv(b4) \| wire_version(b3..0)`; update the route-entry layout comment (line 51) to `score_bucket(4) \| degraded(b3) \| rsv(b2..1) \| is_gateway(b0)`. |
| `lib/core/frame_codec.cpp` | `pack_beacon`/`unpack_beacon`: the complete-flag in **byte-3 bit 4 ONLY** (leave `b3..0` `wire_version` untouched); route-entry encode/decode: `degraded` in byte-2 **bit 3**. Slice 1. |
| `lib/core/node.h` | Add `enum class LinkBidi`; `_link_bidi[256]` + `_link_bidi_confirmed_ms[256]` to `LayerRuntime`; `degraded_from_wire` to `RtCandidate`; declare the six Node bidi methods. Slice 2/3. |
| `lib/core/protocol_constants.h` | Add `bidi_penalty_one_way_q4`, `bidi_confirm_ttl_ms`, `link_reprobe_ttl_ms`, `heard_set_census_min_headroom` near `peer_silent_penalty_q4`/`next_hop_live_ttl_ms`. Slice 2/4. |
| `lib/core/node_routing.cpp` | Define `bidi_penalty_q4`, `candidate_degraded`, `note_link_confirmed`, `decay_link_bidi`; fold `bidi_penalty_q4(next_hop)` into `effective_score` (line 83); recompute `degraded_from_wire` live in `rt_merge`; fan out via `resort_routes_for_neighbor_penalty` (line 158). Slices 2/4/5. |
| `lib/core/node_beacon.cpp` | Define `update_link_bidi_from_beacon` (the detection scan, called on beacon ingest near `clear_peer_suspect`/line 79 and `rt_skip_silent_n2`/line 586); the NEW leaf-only census packing pass over `_active->_rt` filtering `candidates[0].hops==1`, with the live-`beacon_max_entries()` headroom check and complete-flag set, coexisting with the Phase-1 dirty pack without re-dirtying (post-pack dirty-clear line 359); emit `degraded` on-wire per `candidate_degraded`. Slices 3/5. |
| `lib/core/node_cascade.cpp` | The NEW `_link_bidi[from_next]==one_way` slow-reprobe interception in the giveup path **before** `try_cascade_requeue` (the `cascade_to_alt` no-alt branch, lines 107–115) — one RTS per `link_reprobe_ttl_ms`, the sole-route probe still flies. Slice 6. |
| `lib/core/node_mac_rx.cpp` | On a real CTS receipt, call `note_link_confirmed(next_hop)` — the positive recovery signal that flips `one_way→confirmed` and clears degraded. Slice 6. |
| `test/test_frame_codec.cpp` | Codec round-trip tests for both bits (inert default-0 backward-compat; `wire_version` survives a set complete-flag — MF1; `degraded` round-trips through the route entry). Slice 1. |
| `test/test_node_*.cpp` (the real routing/beacon/cascade test TUs, e.g. `test/test_node_r3.cpp` — coder FINDs the live file) | `bidi_penalty_q4`/`effective_score` ordering + sole-one-way-stays-selectable; the detection scan (present→confirmed, absent+complete→one_way, absent+incomplete→unconfirmed, endpoint-override); `candidate_degraded` live recompute + clear-on-recovery; `note_link_confirmed`/`decay_link_bidi` (decay→unknown, MF6); census packing (full set→flag set, over-cap→flag clear, leaf-only); slow-reprobe (one attempt/TTL, CTS→recover). Slices 2–6. |

## Slicing + gates

- **Slice 1 — Codec + round-trip tests** (delivery-neutral, safe to flash ahead): both wire bits added, inert and default-0; pure `frame_codec.{h,cpp}` round-trips proving backward-compat alone (old↔new fleet safe), including the MF1 `wire_version`-survives-complete-flag assertion. Gate = `pio test -e native` + all 4 boards build.
- **Slice 2 — Store the bidi plane** (delivery-neutral): `_link_bidi[256]` + `_link_bidi_confirmed_ms[256]` + `degraded_from_wire`, the local-confirm (`note_link_confirmed`) / decay (`decay_link_bidi`) hooks and constants, **no penalty wired yet**. Gate = native.
- **Slice 3 — Detect** (delivery-neutral): `update_link_bidi_from_beacon` ingest scan + the wire `degraded` inheritance into `degraded_from_wire` on `rt_merge`. Sets state; nothing reads it for selection yet. Gate = native.
- **Slice 4 — Select** (first **BASELINE-gated**): soft `bidi_penalty_q4` folded into `effective_score`, OUT of `next_hop_selectable`. Must be delivery-neutral where there is no asymmetry (a confirmed-bidi link is never penalized). Gate = native + BASELINE (s18 ≥ baseline, leaks 0, cross-layer held, churn sane — the wire bits shift byte-md5, expected).
- **Slice 5 — Advertise** (**BASELINE dense-inert gate**): the leaf-only census packing mode + transitive degraded propagation. Must stay delivery-neutral / inert where dense (flag-off). Gate = native + BASELINE (census emit-policy delivery-neutral with no asymmetry; never over `beacon_max_bytes`).
- **Slice 6 — Slow-reprobe + recovery** (**sim A/B + metal**): the `_link_bidi==one_way` interception before `try_cascade_requeue` (one RTS/`link_reprobe_ttl_ms`) + CTS→confirmed recovery via `note_link_confirmed`. Carries the win — the only slice needing the sim A/B on `simulation/topo_9node.json` (pass = DM delivery ≥ baseline AND `rts_tx` to the isolated nodes drops AND no false-demotion AND a recovery case propagates-then-clears) plus the metal node-72 lucky-marginal tune of `link_reprobe_ttl_ms`.

---

## SLICE 1: codec wire bits (both inert, default-0, backward-compat)

Add the two backward-compatible wire bits to the beacon codec — the route-entry `degraded` flag at byte-2 bit 3 (one of the genuinely-free rsv bits b3..1) and the beacon `heard_set_complete` flag at byte-3 bit 4 (the ONLY free bit; `b3..0` is `wire_version` and MUST survive untouched). Nothing reads either bit yet; the whole slice is structurally inert and proves backward-compat alone (both default 0 → an old frame parses as not-complete/not-degraded, and `wire_version` is never disturbed). All edits in `lib/core/frame_codec.{h,cpp}`; round-trip tests in `test/test_frame_codec.cpp`.

### Task 1: route-entry `degraded` bit (byte-2 b3) in the 4-B entry codec
**Files:** Modify `lib/core/frame_codec.h` (struct `beacon_entry` @:51-58, header comment @:51), Modify `lib/core/frame_codec.cpp` (pack_beacon entry loop @:58-63, `parse_beacon_entry` @:140-152), Test `test/test_frame_codec.cpp` (new TEST_CASE after the existing BCN block).
- [ ] **Step 1: Write the failing test** Append a new TEST_CASE in `test/test_frame_codec.cpp` (after the "BCN — n_entries 6-bit split" case, ~line 633):
```cpp
TEST_CASE("BCN — route-entry degraded bit (byte-2 b3) round-trips, default 0, leaves score/gw untouched") {
    // entry 0 degraded, entry 1 not; degraded must NOT disturb score_bucket(b7..4) or is_gateway(b0)
    beacon_entry ents[2] = {
        {0x05, 0x07, 0xC, false, 2},   // degraded_from_wire defaults false in struct
        {0x09, 0x07, 0xA, true,  3},
    };
    ents[0].degraded = true;
    beacon_in in{};
    in.leaf_id = 3; in.src = 0x11; in.key_hash32 = 0xDEADBEEF;
    in.entries = ents; in.seen_bitmap = {};
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 22);
    // entry 0 byte-2 = score(0xC<<4) | degraded(0x08) | is_gateway(0) = 0xC8; entry 1 = 0xA0|gw 0x01 = 0xA1
    CHECK(buf[16] == 0xC8);   // entries start at 14: dest,next,byte2,hops -> byte2 at 14+2
    CHECK(buf[20] == 0xA1);   // entry1 byte2 at 14+4+2

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    REQUIRE(o.has_value());
    auto e0 = parse_beacon_entry(fr, *o, 0);
    REQUIRE(e0.has_value());
    CHECK(e0->degraded);
    CHECK(e0->score_bucket == 0xC);
    CHECK_FALSE(e0->is_gateway);
    auto e1 = parse_beacon_entry(fr, *o, 1);
    REQUIRE(e1.has_value());
    CHECK_FALSE(e1->degraded);   // default 0 survives
    CHECK(e1->score_bucket == 0xA);
    CHECK(e1->is_gateway);
    // OLD-FORMAT entry (byte-2 has b3 clear) parses as not-degraded: the golden-minimal frame proves it
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native` Expected: compile error — `beacon_entry` has no member `degraded` (struct field not yet added). After the field is added but before pack/parse wiring, it would instead fail the `buf[16] == 0xC8` and `e0->degraded` CHECKs (byte-2 b3 never written/read).
- [ ] **Step 3: Implement** In `lib/core/frame_codec.h`, change the entry comment and add the field to `struct beacon_entry`:
```cpp
// Route entry (4 B): dest | next | score_bucket(4 hi)|degraded(b3)|rsv(b2..1)|is_gateway(b0) | hops(full byte).
struct beacon_entry {
    uint8_t dest;
    uint8_t next;
    uint8_t score_bucket;   // 4-bit
    bool    is_gateway;
    uint8_t hops;           // full byte (1..255)
    bool    degraded = false;   // WIRE = byte-2 b3 (one of the free rsv bits b3..1); one-way/transitively-bad next-hop
};
```
In `lib/core/frame_codec.cpp`, pack the bit in the entry loop (`:61`):
```cpp
        w.u8(static_cast<uint8_t>(((e.score_bucket & 0x0F) << 4) |
                                  (e.degraded ? 0x08 : 0x00) | (e.is_gateway ? 0x01 : 0x00)));
```
and parse it in `parse_beacon_entry` (after `:149`, the `is_gateway` line):
```cpp
    e.is_gateway   = (frame[off + 2] & 0x01) != 0;
    e.degraded     = (frame[off + 2] & 0x08) != 0;
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/frame_codec.h lib/core/frame_codec.cpp test/test_frame_codec.cpp && git commit -m "codec: add inert route-entry degraded bit (byte-2 b3), default 0"`

### Task 2: beacon `heard_set_complete` flag (byte-3 b4) — wire_version MUST survive
**Files:** Modify `lib/core/frame_codec.h` (byte-3 comment @:43, `beacon_in` struct @:81-95, `beacon_out` struct @:106-114), Modify `lib/core/frame_codec.cpp` (pack_beacon byte-3 @:39, parse_beacon byte-3 @:89/95-96), Test `test/test_frame_codec.cpp` (new TEST_CASE).
- [ ] **Step 1: Write the failing test** Append a new TEST_CASE in `test/test_frame_codec.cpp` (after Task 1's case):
```cpp
TEST_CASE("BCN — heard_set_complete flag (byte-3 b4) round-trips and wire_version SURVIVES") {
    const beacon_entry ents[] = {
        {0x05, 0x07, 0xC, false, 2},
        {0x09, 0x07, 0xA, true,  3},
    };
    beacon_in in{};
    in.leaf_id = 3; in.src = 0x11; in.key_hash32 = 0xDEADBEEF;
    in.entries = ents; in.seen_bitmap = {};
    in.heard_set_complete = true;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    CHECK(n == 22);
    // byte-3 = n_entries_hi(0) | complete(0x10) | wire_version(1) = 0x11
    CHECK(buf[3] == uint8_t(0x10 | (protocol::wire_version & 0x0F)));

    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    REQUIRE(o.has_value());
    CHECK(o->heard_set_complete);
    CHECK(o->wire_version == protocol::wire_version);   // b3..0 untouched by the complete flag
    CHECK(o->n_entries == 2);                            // n_entries_hi (b7..5) untouched

    // OLD-FORMAT frame: clear b4, keep wire_version -> parses as not-complete, version intact
    std::array<uint8_t, 32> old = buf;
    old[3] = uint8_t(old[3] & ~0x10);                   // strip the complete bit (old emitter never set it)
    std::span<const uint8_t> ofr(old.data(), n);
    auto oo = parse_beacon(ofr);
    REQUIRE(oo.has_value());
    CHECK_FALSE(oo->heard_set_complete);
    CHECK(oo->wire_version == protocol::wire_version);
    CHECK(oo->n_entries == 2);
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native` Expected: compile error — `beacon_in` has no member `heard_set_complete` and `beacon_out` has no member `heard_set_complete`. Once the fields exist but pack/parse aren't wired, `buf[3]` would read `0x01` (complete bit never OR'd) and `o->heard_set_complete` would be false.
- [ ] **Step 3: Implement** In `lib/core/frame_codec.h`, fix the stale byte-3 comment (@:43):
```cpp
//   byte 3     : n_entries_hi(b7..5) | heard_set_complete(b4) | wire_version(b3..0)   [n_entries = lo3 | hi3<<3; b4 = bidi heard-set authoritative-complete flag; b3..0 carry wire_version — see pack/unpack_beacon, NOT spare]
```
Add the input field to `struct beacon_in` (after the `is_mobile` field, ~:84):
```cpp
    bool     heard_set_complete = false;   // WIRE = byte-3 b4 ONLY: all hops==1 entries present this beacon (bidi census authoritative)
```
Add the output field to `struct beacon_out` (after the `wire_version` member, :108):
```cpp
    uint8_t  wire_version;   // §7c: byte-3 low nibble — cross-version handshake (checked before the format-dependent parse)
    bool     heard_set_complete = false;   // WIRE = byte-3 b4 ONLY (independent of wire_version b3..0)
```
In `lib/core/frame_codec.cpp`, OR the bit into byte-3 in pack_beacon (`:39`) — keep `wire_version` in b3..0 and `n_entries_hi` in b7..5 exactly as-is:
```cpp
    w.u8(static_cast<uint8_t>((((n_entries >> 3) & 0x07) << 5) |
                              (in.heard_set_complete ? 0x10 : 0x00) |
                              (protocol::wire_version & 0x0F)));   // n_entries_hi | complete(b4) | wire_version (b3..0)
```
In parse_beacon, decode b4 (after the `wire_version` line, `:96`):
```cpp
    o.wire_version    = static_cast<uint8_t>(b3 & 0x0F);   // §7c: cross-version handshake (byte-3 low nibble, fixed offset)
    o.heard_set_complete = (b3 & 0x10) != 0;               // byte-3 b4 ONLY — does not touch wire_version / n_entries_hi
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/frame_codec.h lib/core/frame_codec.cpp test/test_frame_codec.cpp && git commit -m "codec: add inert beacon heard_set_complete flag (byte-3 b4), wire_version preserved"`

### Task 3: combined backward-compat round-trip (both bits at once, plus full-page non-regression)
**Files:** Test `test/test_frame_codec.cpp` (new TEST_CASE only — no source changes; this locks the interaction of both bits).
- [ ] **Step 1: Write the failing test** Append a new TEST_CASE in `test/test_frame_codec.cpp`:
```cpp
TEST_CASE("BCN — both new bits coexist; old-format (all-zero new bits) parses clean") {
    beacon_entry ents[2] = {
        {0x12, 0x12, 0x7, false, 1},   // a hops==1 direct-neighbour-style entry, flagged degraded
        {0x40, 0x12, 0x3, false, 2},
    };
    ents[0].degraded = true;
    beacon_in in{};
    in.leaf_id = 1; in.src = 0x12; in.key_hash32 = 0x01020304;
    in.entries = ents; in.seen_bitmap = {};
    in.heard_set_complete = true;
    std::array<uint8_t, 32> buf{};
    size_t n = pack_beacon(in, buf);
    REQUIRE(n == 22);
    std::span<const uint8_t> fr(buf.data(), n);
    auto o = parse_beacon(fr);
    REQUIRE(o.has_value());
    CHECK(o->heard_set_complete);
    CHECK(o->wire_version == protocol::wire_version);
    auto e0 = parse_beacon_entry(fr, *o, 0); REQUIRE(e0.has_value()); CHECK(e0->degraded);
    auto e1 = parse_beacon_entry(fr, *o, 1); REQUIRE(e1.has_value()); CHECK_FALSE(e1->degraded);

    // OLD emitter: byte-3 b4 clear AND every entry byte-2 b3 clear -> default-0 reads
    std::array<uint8_t, 32> old = buf;
    old[3]  = uint8_t(old[3]  & ~0x10);   // clear complete
    old[16] = uint8_t(old[16] & ~0x08);   // clear entry0 degraded (byte2 at 14+2)
    std::span<const uint8_t> ofr(old.data(), n);
    auto oo = parse_beacon(ofr);
    REQUIRE(oo.has_value());
    CHECK_FALSE(oo->heard_set_complete);
    CHECK(oo->wire_version == protocol::wire_version);
    auto oe0 = parse_beacon_entry(ofr, *oo, 0);
    REQUIRE(oe0.has_value());
    CHECK_FALSE(oe0->degraded);
    CHECK(oe0->score_bucket == 0x7);   // payload bits untouched by the masking
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native` Expected: with Tasks 1+2 already merged this should PASS first try; if either bit was mis-placed (e.g. complete on a low-nibble bit clobbering `wire_version`, or degraded on b0 clobbering `is_gateway`) the `wire_version`/`score_bucket` CHECKs fail here — this is the regression net.
- [ ] **Step 3: Implement** No source change — Tasks 1 and 2 already provide the behavior; this task only adds the combined-coexistence assertion. (If it fails, the defect is in Task 1/2's bit placement; fix there.)
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add test/test_frame_codec.cpp && git commit -m "test: lock degraded+heard_set_complete coexistence and old-format backward-compat"`


---

## SLICE 2: Store the bidi plane (state only, no penalty applied)

This slice adds the bidirectionality *state* — the `LinkBidi` enum, the per-`LayerRuntime` `_link_bidi[256]` / `_link_bidi_confirmed_ms[256]` arrays, `RtCandidate.degraded_from_wire`, the constants, and the three state methods (`note_link_confirmed`, `decay_link_bidi`, `candidate_degraded`) — and wires the LOCAL confirmation hook at the real-CTS site in `handle_cts`. It is delivery-neutral by construction: no penalty rides `effective_score` yet (Slice 4 does that), and `candidate_degraded` is computed but not consumed. Slice 1's codec `degraded`/`heard_set_complete` bits and Slice 3's `update_link_bidi_from_beacon` are referenced by contract name only where mentioned.

### Task 1: Add the LinkBidi enum + the bidi constants
**Files:** Modify `/home/staszek/MeshRoute/lib/core/node.h` (add `enum class LinkBidi` near the other small enums, just above `struct RtCandidate` at line 185); Modify `/home/staszek/MeshRoute/lib/core/protocol_constants.h` (add constants right after `peer_dead_penalty_q4` at line 146, i.e. in the Peer-liveness block, and after `next_hop_live_ttl_ms` line 120 is reused by name); Test `/home/staszek/MeshRoute/test/test_node_r3.cpp` (append).
- [ ] **Step 1: Write the failing test**
```cpp
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
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `LinkBidi` is not a member of `meshroute`, and `protocol::bidi_penalty_one_way_q4` / `bidi_confirm_ttl_ms` / `link_reprobe_ttl_ms` / `heard_set_census_min_headroom` are undeclared.
- [ ] **Step 3: Implement** In `node.h`, immediately above `struct RtCandidate {` (line 185), add:
```cpp
// Per-next-hop bidirectionality state (asymmetric-link plane, 2026-06-29). unknown=0 so a zeroed
// _link_bidi slot defaults to 'not yet probed' (selectable, unpenalized). confirmed = a real CTS or
// a complete-heard-set hit; one_way = positive absent+complete evidence (NEVER mere staleness — see
// decay_link_bidi). Packed as a uint8_t array per LayerRuntime (room to grow).
enum class LinkBidi : uint8_t { unknown = 0, confirmed = 1, one_way = 2 };
```
In `protocol_constants.h`, after line 146 (`peer_dead_penalty_q4`), add:
```cpp
// ---- Asymmetric-link bidirectionality plane (2026-06-29 design) ------------
inline constexpr int16_t  bidi_penalty_one_way_q4   = peer_silent_penalty_q4;   // 640 Q4 seed — one_way sorts below
                                                                                // any viable confirmed/unknown route
                                                                                // (fallback peer_suspect_penalty_q4=192 if metal strands good-RF one-way routes).
inline constexpr uint64_t bidi_confirm_ttl_ms       = next_hop_live_ttl_ms;     // 1200000 — confirmed decays to UNKNOWN past this
inline constexpr uint64_t link_reprobe_ttl_ms       = 60000;                    // slow-reprobe: one RTS per TTL on a one-way sole route
inline constexpr uint8_t  heard_set_census_min_headroom = 4;                    // census engages only if the full hops==1 set fits leaving >= this many beacon slots
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/protocol_constants.h test/test_node_r3.cpp && git commit -m "bidi: add LinkBidi enum + bidirectionality constants (state-only slice)"`

### Task 2: Add the `_link_bidi` / `_link_bidi_confirmed_ms` arrays to LayerRuntime + `RtCandidate.degraded_from_wire`
**Files:** Modify `/home/staszek/MeshRoute/lib/core/node.h` (LayerRuntime, right after `_dest_seen_ms[256]` at line 1115; and `RtCandidate`, after `learned_layer_id` at line 192); add a public test read-accessor near `peer_penalty_q4` (line 480); Test `/home/staszek/MeshRoute/test/test_node_r3.cpp` (append).
- [ ] **Step 1: Write the failing test**
```cpp
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
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `Node` has no member `link_bidi_state`, and `RtCandidate` has no member `degraded_from_wire`.
- [ ] **Step 3: Implement** In `node.h`, in `struct RtCandidate`, immediately after `uint8_t learned_layer_id = 0;` (line 192), add:
```cpp
    bool     degraded_from_wire = false;   // the WIRE-inherited degraded component ONLY (a fact about what the
                                           // advertiser advertised). The LIVE degraded state is recomputed as
                                           // degraded_from_wire || _link_bidi[next_hop]==one_way (candidate_degraded) — NEVER a sticky OR.
```
In `node.h`, in `struct LayerRuntime`, immediately after `uint64_t _dest_seen_ms[256] = {};` (line 1115), add:
```cpp
        // Bidirectionality plane (asymmetric-link routing, 2026-06-29). Index = node_id, value = a LinkBidi.
        // Zero-init => every link defaults to 'unknown' (selectable, unpenalized). FULL 0..254 range, NO eviction
        // (like _dest_seen_ms) so a gossip-only or quiet peer keeps its state. _link_bidi_confirmed_ms is the
        // DEDICATED decay source — last real-CTS / complete-heard-set confirmation time (do NOT overload _dest_seen_ms).
        uint8_t  _link_bidi[256] = {};
        uint64_t _link_bidi_confirmed_ms[256] = {};
```
In `node.h`, immediately after the `peer_penalty_q4` accessor (line 480), add the public test read-accessor:
```cpp
    LinkBidi          link_bidi_state(uint8_t node_id) const { return static_cast<LinkBidi>(_active->_link_bidi[node_id]); }  // bidi plane read (test/status); unknown for any unprobed link
    uint64_t          link_bidi_confirmed_ms(uint8_t node_id) const { return _active->_link_bidi_confirmed_ms[node_id]; }    // last-confirmation ms (test/status); 0 = never confirmed
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node.h test/test_node_r3.cpp && git commit -m "bidi: add _link_bidi/_link_bidi_confirmed_ms to LayerRuntime + RtCandidate.degraded_from_wire"`

### Task 3: Implement `note_link_confirmed` (local confirmation setter + fan-out)
**Files:** Modify `/home/staszek/MeshRoute/lib/core/node.h` (declare in the public method zone near line 485, just after `rt_resort_for_pick`); Modify `/home/staszek/MeshRoute/lib/core/node_routing.cpp` (define, after `mark_neighbor_budget_tier` ends at line 227-ish — append in the routing-plane TU); Test `/home/staszek/MeshRoute/test/test_node_r3.cpp` (append).
- [ ] **Step 1: Write the failing test**
```cpp
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
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `Node` has no member `note_link_confirmed`.
- [ ] **Step 3: Implement** In `node.h`, immediately after `rt_resort_for_pick` (line 485), declare (public — driven directly by tests, like the other test-facing routing helpers):
```cpp
    void    note_link_confirmed(uint8_t next_hop);   // local bidi confirm (real CTS / complete-heard-set hit): set confirmed + stamp + fan out
```
In `node_routing.cpp`, append after `mark_neighbor_budget_tier`'s closing brace (~line 228):
```cpp
// Bidirectionality plane (2026-06-29): a LOCAL confirmation that next_hop hears us (a real CTS to our flight, or a
// complete-heard-set present hit). Set confirmed + stamp the dedicated decay source, then fan out via the
// resort_routes_for_neighbor_penalty pattern so a recovery re-sorts + re-dirties + re-advertises. Emits link_recover
// when the link was previously one_way (the §7 recovery signal). No penalty rides effective_score yet (Slice 4).
void Node::note_link_confirmed(uint8_t next_hop) {
    if (next_hop == 0 || next_hop == 0xFF) return;
    const bool was_one_way = _active->_link_bidi[next_hop] == static_cast<uint8_t>(LinkBidi::one_way);
    _active->_link_bidi[next_hop]             = static_cast<uint8_t>(LinkBidi::confirmed);
    _active->_link_bidi_confirmed_ms[next_hop] = _hal.now();
    resort_routes_for_neighbor_penalty(next_hop, "link_bidi_confirm", /*local_only=*/false);
    MR_EMIT("link_bidi_confirm", EF_I("next_hop", next_hop));
    if (was_one_way) MR_EMIT("link_recover", EF_I("next_hop", next_hop));
}
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "bidi: note_link_confirmed sets confirmed + stamps + fans out (link_bidi_confirm/link_recover emit)"`

### Task 4: Implement `decay_link_bidi` (confirmed -> unknown after TTL, never -> one_way)
**Files:** Modify `/home/staszek/MeshRoute/lib/core/node.h` (declare in the public method zone, just after the `note_link_confirmed` decl from Task 3); Modify `/home/staszek/MeshRoute/lib/core/node_routing.cpp` (define, after `note_link_confirmed`); Test `/home/staszek/MeshRoute/test/test_node_r3.cpp` (append).
- [ ] **Step 1: Write the failing test**
```cpp
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
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `Node` has no member `decay_link_bidi` (nor the `set_link_bidi_for_test` seam).
- [ ] **Step 3: Implement** In `node.h`, immediately after the `note_link_confirmed` declaration (Task 3), declare both `decay_link_bidi` and a test write-seam (mirrors the `route_inject` test-only mutators already in the public zone):
```cpp
    void    decay_link_bidi(uint8_t next_hop);   // confirmed + stale past bidi_confirm_ttl_ms -> unknown (MF6: NEVER -> one_way)
    void    set_link_bidi_for_test(uint8_t next_hop, LinkBidi v) { _active->_link_bidi[next_hop] = static_cast<uint8_t>(v); }  // test seam: seed a bidi state directly
```
In `node_routing.cpp`, append after `note_link_confirmed`:
```cpp
// Bidirectionality plane: a confirmed link whose last confirmation is older than bidi_confirm_ttl_ms decays to
// UNKNOWN — selectable + unpenalized again (a quiet-but-functional link must not self-degrade). MF6: it NEVER decays
// to one_way (that requires positive absent+complete heard-set evidence, set only by update_link_bidi_from_beacon).
// unknown/one_way slots are left as-is. DEFERRED: decay_link_bidi has NO wired caller in this initiative — it is a
// no-op for routing (confirmed and unknown are selection-equivalent, both 0 penalty), so a stale confirmed costs nothing.
// Kept for MF6-correctness; wire a lazy caller (e.g. inside candidate_degraded) ONLY if a future feature treats confirmed != unknown.
void Node::decay_link_bidi(uint8_t next_hop) {
    if (_active->_link_bidi[next_hop] != static_cast<uint8_t>(LinkBidi::confirmed)) return;
    const uint64_t now = _hal.now();
    const uint64_t conf = _active->_link_bidi_confirmed_ms[next_hop];
    if (now - conf >= protocol::bidi_confirm_ttl_ms)
        _active->_link_bidi[next_hop] = static_cast<uint8_t>(LinkBidi::unknown);
}
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "bidi: decay_link_bidi confirmed->unknown past TTL (never ->one_way, MF6)"`

### Task 5: Implement `candidate_degraded` (live recompute, never a sticky cache)
**Files:** Modify `/home/staszek/MeshRoute/lib/core/node.h` (declare in the public method zone, just after the `decay_link_bidi` decl from Task 4); Modify `/home/staszek/MeshRoute/lib/core/node_routing.cpp` (define, after `decay_link_bidi`); Test `/home/staszek/MeshRoute/test/test_node_r3.cpp` (append).
- [ ] **Step 1: Write the failing test**
```cpp
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
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `Node` has no member `candidate_degraded`.
- [ ] **Step 3: Implement** In `node.h`, immediately after the `decay_link_bidi` declaration (Task 4), declare:
```cpp
    bool    candidate_degraded(const RtCandidate& c) const;   // LIVE: c.degraded_from_wire || _link_bidi[c.next_hop]==one_way (never a sticky cache, MF5/OI1)
```
In `node_routing.cpp`, append after `decay_link_bidi`:
```cpp
// Bidirectionality plane (MF5/OI1): the LIVE effective degraded state of a candidate — the wire-inherited component
// (a fact about what the advertiser said, stored on the candidate) OR-ed with the local one_way verdict (recomputed
// from _link_bidi every call). NEVER a sticky cached bool: a stuck-degraded cache would never clear on recovery and
// defeat §7. Read by select (Slice 4) + advertise (Slice 5); no caller consumes it in this state-only slice.
bool Node::candidate_degraded(const RtCandidate& c) const {
    return c.degraded_from_wire
        || _active->_link_bidi[c.next_hop] == static_cast<uint8_t>(LinkBidi::one_way);
}
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "bidi: candidate_degraded live recompute (wire bit OR local one_way, MF5)"`

### Task 6: Wire the LOCAL confirmation hook at the real-CTS site in handle_cts
**Files:** Modify `/home/staszek/MeshRoute/lib/core/node_mac_rx.cpp` (line 336, inside `Node::handle_cts`, at the `c.tx_id == _active->_pending_tx->next` confirmation point); Test `/home/staszek/MeshRoute/test/test_node_r3.cpp` (append).
- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("bidi hook — a real CTS from our flight's next-hop confirms the link") {
    using namespace meshroute;
    TestHal hal;
    Node node(hal, /*node_id=*/7, /*key_hash32=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.peer_count = 0;
    cfg.allowed_sf_bitmap = (1u << 7);                 // permit a data SF so a flight can arm
    node.on_init(cfg);
    // Install a route to dest 20 via next-hop 9 and originate a DM so a pending_tx awaits a CTS from 9.
    CHECK(node.route_inject(/*dest=*/20, /*next_hop=*/9, /*hops=*/2, /*score_q4=*/(12 << 4)));
    send_cmd(node, /*dst=*/20, "x");                   // M4: originate via the public send_cmd helper (do_send is private)
    REQUIRE(node.has_pending_tx());
    CHECK(node.link_bidi_state(9) == LinkBidi::unknown);
    // Craft the CTS from 9 (tx_id=9) clearing us (rx_id=7), matching the pending flight's ctr_lo.
    cts_in ci{};
    ci.rx_id = 7; ci.tx_id = 9;
    ci.ctr_lo = static_cast<uint8_t>(node.pending_ctr_lo_for_test() & 0x0F);
    ci.chosen_data_sf = 7; ci.payload_len = 1; ci.already_received = false;
    std::array<uint8_t, 16> cb{};
    const size_t cn = pack_cts(ci, std::span<uint8_t>(cb.data(), cb.size()));
    REQUIRE(cn > 0);
    RxMeta meta{8.0f, -80.0f, 0, -1};
    node.on_recv(cb.data(), cn, meta);
    // The real CTS from our next-hop confirms 9 is bidirectional.
    CHECK(node.link_bidi_state(9) == LinkBidi::confirmed);
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: the final `CHECK(node.link_bidi_state(9) == LinkBidi::confirmed)` fails (state stays `unknown`) — the CTS clears the flight but `note_link_confirmed` is not yet called. (If `pending_ctr_lo_for_test` is missing, add it as a one-line public accessor `uint8_t pending_ctr_lo_for_test() const { return _active->_pending_tx ? _active->_pending_tx->ctr_lo : 0; }` next to `has_pending_tx` at node.h:488.)
- [ ] **Step 3: Implement** In `node_mac_rx.cpp`, at line 336, immediately AFTER the existing `learn_direct_neighbor` call (which fires only once `c.tx_id == _active->_pending_tx->next` is confirmed at line 334), add the bidi confirmation:
```cpp
    if (learn_direct_neighbor(c.tx_id, protocol::db_to_q4(meta.snr_db), false)) schedule_triggered_beacon();
    note_link_confirmed(c.tx_id);                        // bidi plane: a real CTS proves our next-hop hears us -> confirmed (clears any one_way + emits link_recover)
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node_mac_rx.cpp lib/core/node.h test/test_node_r3.cpp && git commit -m "bidi: confirm the link on a real CTS from our flight's next-hop (handle_cts hook)"`

---

## SLICE 3: Detection scan + degraded-from-wire inheritance

Implements `update_link_bidi_from_beacon` (the heard-set detection scan), wires it into beacon ingest, and threads the WIRE-inherited `degraded` bit through `rt_merge` (recomputed fresh per merge, never sticky). Assumes Slice 1 added `beacon_entry.degraded`/`beacon_out.heard_set_complete` and Slice 2 added `LinkBidi`, `_link_bidi[256]`, `_link_bidi_confirmed_ms[256]`, `RtCandidate.degraded_from_wire`, `note_link_confirmed`, `candidate_degraded`, `bidi_penalty_q4`, `decay_link_bidi` (referenced by contract name). All tests run with `pio test -e native`.

### Task 1: Detection scan — `update_link_bidi_from_beacon` (the three heard-set cases)
**Files:**
- Modify `lib/core/node.h` (declare the method near the other beacon helpers, ~line 798 in the route-table block; add a `MESHROUTE_NATIVE` test accessor in the public test-seam block ~line 480 next to `peer_penalty_q4`).
- Modify `lib/core/node_beacon.cpp` (define the method — insert after `apply_suspect_gossip`, which ends ~line 179, before `emit_beacon`).
- Test `test/test_node_r3.cpp`.

- [ ] **Step 1: Write the failing test** (append to `test/test_node_r3.cpp`)
```cpp
TEST_CASE("update_link_bidi_from_beacon: present->confirmed, absent+complete->one_way, absent+incomplete->no change") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // (a) PRESENT: advertiser 7's beacon lists [dest=5, next=5] -> "7 hears 5" -> 5<->7 confirmed.
    beacon_entry present[1] = {};
    present[0].dest = 5; present[0].next = 5; present[0].hops = 1;
    node.update_link_bidi_from_beacon(/*advertiser=*/7, present, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::confirmed));
    CHECK(node.bidi_penalty_q4(7) == 0);                     // confirmed => no penalty

    // (b) ABSENT + COMPLETE: advertiser 8's COMPLETE page omits dest=5 -> 8 does NOT hear 5 -> 5->8 one_way.
    beacon_entry other[1] = {};
    other[0].dest = 99; other[0].next = 99; other[0].hops = 1;   // some unrelated dest, NOT self
    node.update_link_bidi_from_beacon(/*advertiser=*/8, other, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(8) == static_cast<uint8_t>(LinkBidi::one_way));
    CHECK(node.bidi_penalty_q4(8) == protocol::bidi_penalty_one_way_q4);

    // (c) ABSENT + INCOMPLETE: advertiser 9 truncated its page (complete=false) -> NO state change (stays unknown=0).
    node.update_link_bidi_from_beacon(/*advertiser=*/9, other, /*n=*/1, /*complete=*/false);
    CHECK(node.link_bidi_at(9) == static_cast<uint8_t>(LinkBidi::unknown));
    CHECK(node.bidi_penalty_q4(9) == 0);
}
```

- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `'update_link_bidi_from_beacon' is not a member of 'meshroute::Node'` and `'link_bidi_at' is not a member` (method + accessor undeclared).

- [ ] **Step 3: Implement**
Declare in `lib/core/node.h` in the route-table block (after `liveness_penalty_q4` decl ~line 808):
```cpp
    // Slice 3: the bidirectionality DETECTION scan. For advertiser P's beacon heard-set (its hops==1 entries),
    // a [dest==self] entry proves P hears us -> confirmed (note_link_confirmed); an ABSENT self in a COMPLETE
    // page proves P does NOT hear us -> one_way; an absent self in a TRUNCATED page is unconfirmed (no change).
    void update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* entries, uint8_t n, bool complete);
```
Add the test accessor in the public test-seam block (after `peer_penalty_q4` ~line 480):
```cpp
#ifdef MESHROUTE_NATIVE
    uint8_t link_bidi_at(uint8_t node_id) const { return _active->_link_bidi[node_id]; }   // raw LinkBidi (test/white-box)
#endif
```
Define in `lib/core/node_beacon.cpp` after `apply_suspect_gossip` (~line 179):
```cpp
// Slice 3 §1: bidirectionality detection from a neighbour's heard-set. `advertiser` = P (we heard P's beacon, so
// P->us works). Scan P's hops==1 entries for [dest==self]: PRESENT => P hears us => us<->P CONFIRMED. ABSENT in a
// COMPLETE page => P does NOT hear us => us->P ONE_WAY. ABSENT in a TRUNCATED page (complete=false) => no change.
void Node::update_link_bidi_from_beacon(uint8_t advertiser, const beacon_entry* entries, uint8_t n, bool complete) {
    if (advertiser == 0 || advertiser == 0xFF || advertiser == _node_id) return;   // §P0 sentinel / self
    bool present = false;
    for (uint8_t i = 0; i < n; ++i)
        if (entries[i].hops == 1 && entries[i].dest == _node_id) { present = true; break; }
    if (present) { note_link_confirmed(advertiser); return; }   // CONFIRMED (Slice 2 hook: fan-out + MR_EMIT)
    if (!complete) return;                                      // truncated page -> absence is not authoritative
    if (_active->_link_bidi[advertiser] != static_cast<uint8_t>(LinkBidi::one_way)) {
        _active->_link_bidi[advertiser] = static_cast<uint8_t>(LinkBidi::one_way);
        MR_EMIT("link_one_way", EF_I("next_hop", advertiser));
    }
}
```

- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`

- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_beacon.cpp test/test_node_r3.cpp && git commit -m "bidi slice 3: detection scan update_link_bidi_from_beacon (present/absent-complete/absent-truncated)"`

### Task 2: Endpoint override — a third-party `degraded`/`[dest==self]` entry never poisons self's own link
**Files:**
- Test `test/test_node_r3.cpp` (no production change — locks the contract behaviour of Task 1's scan).

- [ ] **Step 1: Write the failing test** (append to `test/test_node_r3.cpp`)
```cpp
TEST_CASE("endpoint override: a [dest==self] entry confirms (never degrades) the receiver's own link") {
    TestHal hal; Node node(hal, /*id=*/5, /*key=*/0xABCD);   // self = node 5

    // Advertiser 7 lists [dest=5] WITH the degraded wire-bit set (a stale third-party view that 7->5 is one-way).
    // The endpoint that RECEIVED 7's beacon has LIVE proof 7->5 works (it just decoded it), so the scan treats the
    // present self-entry as a CONFIRMATION and ignores the degraded bit entirely (design §1 endpoint override).
    beacon_entry e[1] = {};
    e[0].dest = 5; e[0].next = 5; e[0].hops = 1; e[0].degraded = true;
    node.update_link_bidi_from_beacon(/*advertiser=*/7, e, /*n=*/1, /*complete=*/true);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::confirmed));   // NOT one_way, despite degraded bit
    CHECK(node.bidi_penalty_q4(7) == 0);
}
```

- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: FAIL only if the scan mishandles the degraded bit; with Task 1's implementation this should pass — if instead a regression made the scan read `degraded` it would FAIL with `link_bidi_at(7) == one_way (2)` instead of `confirmed (1)`. (This test pins the override so a later edit can't reintroduce it.)

- [ ] **Step 3: Implement** No production change — Task 1's scan already keys only on `dest == _node_id`/presence and never reads `entries[i].degraded`. This task asserts that invariant.

- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`

- [ ] **Step 5: Commit** `git add test/test_node_r3.cpp && git commit -m "bidi slice 3: lock endpoint override (self-entry confirms, ignores degraded bit)"`

### Task 3: `degraded_from_wire` inheritance through `rt_merge` (recompute-per-merge, never sticky)
**Files:**
- Modify `lib/core/node_routing.cpp` (`rt_merge` ~lines 230-287: set `cand.degraded_from_wire` onto the stored candidate on every install/refresh path so a clean re-advert CLEARS a previously-degraded candidate).
- Modify `lib/core/node_beacon.cpp` (the DV-merge candidate build ~lines 597-604: set `cand.degraded_from_wire = e.degraded` from the incoming entry).
- Test `test/test_node_r3.cpp`.

- [ ] **Step 1: Write the failing test** (append to `test/test_node_r3.cpp`)
```cpp
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
        REQUIRE(len > 0); out.resize(len);
    };

    RxMeta meta{}; meta.snr_db = 6.0f;

    // (1) DEGRADED advert -> the installed candidate for dest 9 via 2 carries degraded_from_wire.
    std::vector<uint8_t> bcn1; make_beacon(/*degraded=*/true, bcn1);
    node.ingest_beacon(bcn1.data(), bcn1.size(), meta);
    const RtEntry* e1 = nullptr;
    for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 9) e1 = &node.rt_at(i);
    REQUIRE(e1 != nullptr);
    bool found_deg = false;
    for (uint8_t j = 0; j < e1->n; ++j) if (e1->candidates[j].next_hop == 2) found_deg = e1->candidates[j].degraded_from_wire;
    CHECK(found_deg == true);

    // (2) CLEAN re-advert (same route, degraded=0) -> degraded_from_wire CLEARS (fresh recompute, not sticky-OR).
    std::vector<uint8_t> bcn2; make_beacon(/*degraded=*/false, bcn2);
    hal._now += 1000;
    node.ingest_beacon(bcn2.data(), bcn2.size(), meta);
    const RtEntry* e2 = nullptr;
    for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 9) e2 = &node.rt_at(i);
    REQUIRE(e2 != nullptr);
    bool still_deg = false;
    for (uint8_t j = 0; j < e2->n; ++j) if (e2->candidates[j].next_hop == 2) still_deg = e2->candidates[j].degraded_from_wire;
    CHECK(still_deg == false);
}
```

- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: `CHECK(found_deg == true)` FAILS (`found_deg` is `false`) — the DV-merge candidate build never copies `e.degraded`, so `degraded_from_wire` stays default-false.

- [ ] **Step 3: Implement**
In `lib/core/node_beacon.cpp`, the DV-merge candidate build (~line 597, the `RtCandidate cand{};` block before `rt_merge(e.dest, cand)`), add after `cand.learned_layer_id = _cfg.leaf_id;`:
```cpp
        cand.degraded_from_wire = e.degraded;   // Slice 3: inherit the advertiser's degraded wire-bit (recomputed per merge)
```
In `lib/core/node_routing.cpp` `rt_merge`, copy the field onto the candidate on EVERY path that writes/refreshes a stored candidate so a clean re-advert clears it (recompute-per-merge, not sticky):
- new-dest install (~line 245, after `entry->candidates[0] = cand;` already copies the whole struct — no change needed there since `cand` carries the field).
- strictly-better in-place refresh (~line 257, `entry->candidates[i] = cand;` — whole-struct copy, carries the field — no change).
- the metadata-only branch (~lines 263-266) currently refreshes only `last_seen_ms/n2_hop/is_gateway/learned_layer_id` and SKIPS the new field — add the recompute so a clean re-advert through this path clears a stale degraded bit:
```cpp
            entry->candidates[i].degraded_from_wire = cand.degraded_from_wire;   // Slice 3: refresh the wire bit even on a metadata-only merge (clears on a clean re-advert)
```
(insert immediately after `entry->candidates[i].learned_layer_id = cand.learned_layer_id;` ~line 266). The new-candidate-with-room (~line 273) and full-table-replace (~line 283) paths whole-struct-copy `cand`, so they already carry the field.

- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`

- [ ] **Step 5: Commit** `git add lib/core/node_routing.cpp lib/core/node_beacon.cpp test/test_node_r3.cpp && git commit -m "bidi slice 3: inherit degraded_from_wire through rt_merge (recompute-per-merge, clears on clean re-advert)"`

### Task 4: Wire the detection scan into `ingest_beacon` (called once per beacon over the parsed heard-set)
**Files:**
- Modify `lib/core/node_beacon.cpp` (`ingest_beacon`: after the DV-merge entry loop ~line 612, before the discovery re-check ~line 614, gather the parsed entries and invoke the scan).
- Test `test/test_node_r3.cpp`.

- [ ] **Step 1: Write the failing test** (append to `test/test_node_r3.cpp`)
```cpp
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
    REQUIRE(len > 0); buf.resize(len);

    RxMeta meta{}; meta.snr_db = 6.0f;
    node.ingest_beacon(buf.data(), buf.size(), meta);
    CHECK(node.link_bidi_at(7) == static_cast<uint8_t>(LinkBidi::one_way));
    CHECK(node.bidi_penalty_q4(7) == protocol::bidi_penalty_one_way_q4);
}
```

- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: `CHECK(node.link_bidi_at(7) == one_way)` FAILS (stays `unknown(0)`) — `ingest_beacon` never calls the scan, so no bidi state is derived from the heard-set.

- [ ] **Step 3: Implement**
In `lib/core/node_beacon.cpp` `ingest_beacon`, immediately after the DV-merge `for (uint8_t i = 0; i < b.n_entries; ++i) { ... }` loop closes (~line 612) and before the `// dv_dual_sf.lua:9680-9684` comment (~line 614), insert the scan over the re-parsed heard-set entries:
```cpp
    // Slice 3 §1: bidirectionality detection. Re-collect the beacon's hops==1 entries as the advertiser's heard-set
    // and scan for our own id: present => the advertiser hears us => confirmed; absent in a complete page => one_way.
    // (Separate from the DV-merge loop above, which split-horizon-skips dest==self and never sees the self-entry.)
    {
        beacon_entry heard[kMaxBeaconEntries];
        uint8_t hn = 0;
        for (uint8_t i = 0; i < b.n_entries && hn < kMaxBeaconEntries; ++i) {
            auto pe = parse_beacon_entry(std::span<const uint8_t>(bytes, len), b, i);
            if (pe) heard[hn++] = *pe;
        }
        update_link_bidi_from_beacon(b.src, heard, hn, b.heard_set_complete);
    }
```

- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`

- [ ] **Step 5: Commit** `git add lib/core/node_beacon.cpp test/test_node_r3.cpp && git commit -m "bidi slice 3: invoke detection scan in ingest_beacon over the parsed heard-set"`

---

## SLICE 4: bidi penalty in effective_score + the one_way transition fan-out

This slice wires the bidirectionality plane into route *selection* — `bidi_penalty_q4(next_hop)` composes into `effective_score` (so `confirmed ≻ unknown ≻ one_way` falls out of the existing `route_strictly_better` sort), and a `_link_bidi` transition re-ranks routes via the existing `resort_routes_for_neighbor_penalty` fan-out. Crucially the penalty rides the SORT only — it is NEVER added to `next_hop_selectable` (`node_cascade.cpp:38`), so a SOLE `one_way` route stays pickable and the DM still flies (avoids the 2026-06-18 freshness-hard-gate delivery regression). It assumes Slice 2's `_link_bidi[256]`, `LinkBidi`, and `note_link_confirmed` already exist. Exit gate: the BASELINE sim suite (s18 >= baseline, delivery-neutral where there is no asymmetry).

### Task 1: `bidi_penalty_q4(next_hop)` — penalty for a `one_way` next-hop, 0 otherwise
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/protocol_constants.h` (add `bidi_penalty_one_way_q4` near `peer_silent_penalty_q4` @ line 145)
- Modify: `/home/staszek/MeshRoute/lib/core/node.h` (declare the method next to `liveness_penalty_q4` @ line 808)
- Modify: `/home/staszek/MeshRoute/lib/core/node_routing.cpp` (define it right after `liveness_penalty_q4`, which currently ends at line 81)
- Test: `/home/staszek/MeshRoute/test/test_node_r3.cpp`

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("§bidi — bidi_penalty_q4 is silent_penalty for one_way, 0 for unknown/confirmed") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.bidi_penalty_q4(2) == 0);                                  // unknown (zeroed slot) -> 0
    node.note_link_confirmed(2);                                          // confirmed (Slice 2)
    CHECK(node.bidi_penalty_q4(2) == 0);                                  // confirmed -> 0
    node.test_set_link_one_way(3);                                        // one_way (Slice 2/3 test hook)
    CHECK(node.bidi_penalty_q4(3) == protocol::bidi_penalty_one_way_q4);  // one_way -> the full penalty
    CHECK(node.bidi_penalty_q4(3) == protocol::peer_silent_penalty_q4);   // seed == silent class
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `Node` has no member `bidi_penalty_q4` (and `protocol::bidi_penalty_one_way_q4` is undeclared).
- [ ] **Step 3: Implement**
In `protocol_constants.h`, after `inline constexpr int16_t peer_silent_penalty_q4 = 640;   // 40.0 dB` (line 145):
```cpp
// asymmetric-link-aware routing — one_way next-hop demotion (seed == silent class for clean ordering;
// fallback peer_suspect_penalty_q4=192 if the metal lucky-marginal gate strands good-RF one-way routes).
inline constexpr int16_t  bidi_penalty_one_way_q4 = peer_silent_penalty_q4;   // 640 Q4
```
In `node.h`, after the `liveness_penalty_q4` declaration (line 808):
```cpp
    int16_t     bidi_penalty_q4(uint8_t next_hop) const;            // §bidi: one_way next-hop demotion (one_way -> bidi_penalty_one_way_q4, unknown/confirmed -> 0)
```
In `node_routing.cpp`, immediately after `liveness_penalty_q4`'s closing brace (line 81):
```cpp
// §bidi: the bidirectionality penalty for `next_hop`. one_way -> bidi_penalty_one_way_q4; unknown AND confirmed -> 0
// (OI2 — the ONLY demotion is positively-confirmed one_way; a nudge on `unknown` would punish every not-yet-probed
// link on a cold mesh, the 2026-06-18 freshness-hard-gate mistake in spirit). const, non-mutating tier read.
int16_t Node::bidi_penalty_q4(uint8_t next_hop) const {
    if (next_hop == 0 || next_hop == _node_id) return 0;
    return _active->_link_bidi[next_hop] == static_cast<uint8_t>(LinkBidi::one_way)
               ? protocol::bidi_penalty_one_way_q4
               : 0;
}
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/protocol_constants.h lib/core/node.h lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "§bidi: bidi_penalty_q4 — one_way next-hop demotion (silent-class seed, unknown/confirmed=0)"`

### Task 2: compose `bidi_penalty_q4` into `effective_score`
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/node_routing.cpp` (`effective_score`, lines 83-86)
- Test: `/home/staszek/MeshRoute/test/test_node_r3.cpp`

`effective_score` is currently:
```cpp
int16_t Node::effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const {
    // §P2: subtract BOTH the R4.2 budget tier AND the liveness tier (suspect/silent/dead) — Lua effective_score@4140.
    return static_cast<int16_t>(c.score - budget_penalty_q4(c, cands, n) - liveness_penalty_q4(c.next_hop));
}
```

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("§bidi — a one_way next-hop drops effective_score by the bidi penalty (vs an unknown peer)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    // two equal-score, equal-hop candidates for dest 5: via 2 (will be one_way) and via 3 (stays unknown)
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));
    CHECK(rt_primary_for(node, 5) == 2);                 // insertion-order tie holds (no bidi state yet)
    node.test_set_link_one_way(2);                       // via 2 is now one_way
    node.note_link_confirmed(3);                         // via 3 confirmed (fan-out re-sorts, Task 3)
    CHECK(rt_primary_for(node, 5) == 3);                 // §bidi: confirmed via-3 now beats penalized one_way via-2
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: `CHECK(rt_primary_for(node, 5) == 3)` fails (got 2) — `effective_score` does not yet subtract the bidi penalty, so the two candidates stay tied and insertion order keeps via 2 primary.
- [ ] **Step 3: Implement**
Replace the `effective_score` body (line 85):
```cpp
int16_t Node::effective_score(const RtCandidate& c, const RtCandidate* cands, uint8_t n) const {
    // §P2: subtract BOTH the R4.2 budget tier AND the liveness tier (suspect/silent/dead) — Lua effective_score@4140.
    // §bidi: ALSO subtract the bidirectionality penalty (one_way next-hop). Rides the SORT only (composes here +
    // through route_strictly_better) — it is NOT a next_hop_selectable hard gate, so a SOLE one_way route stays pickable.
    return static_cast<int16_t>(c.score - budget_penalty_q4(c, cands, n)
                                        - liveness_penalty_q4(c.next_hop)
                                        - bidi_penalty_q4(c.next_hop));
}
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "§bidi: compose bidi_penalty_q4 into effective_score (one_way demoted in the sort, not a hard gate)"`

### Task 3: a `_link_bidi` transition fans out via `resort_routes_for_neighbor_penalty`
**Files:**
- Modify: `/home/staszek/MeshRoute/lib/core/node_routing.cpp` (`note_link_confirmed` — Slice 2's definition; add the fan-out + add a `test_set_link_one_way` test hook in `node.h`)
- Modify: `/home/staszek/MeshRoute/lib/core/node.h` (public test hook near `rt_resort_for_pick` @ line 485)
- Test: `/home/staszek/MeshRoute/test/test_node_r3.cpp`

Per the contract, `note_link_confirmed` must `fan out via resort_routes_for_neighbor_penalty`. This task adds that fan-out (and the `test_set_link_one_way` hook the earlier tasks already reference, which sets `_link_bidi[id]=one_way` and fans out too, mirroring the real Slice-3 detection transition).

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("§bidi — a confirm/one_way transition re-ranks routes via that next-hop (fan-out)") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));   // primary via 2
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));   // alt via 3
    CHECK(rt_primary_for(node, 5) == 2);
    node.test_set_link_one_way(2);          // transition on next-hop 2 -> MUST fan out a re-sort NOW (no manual resort)
    CHECK(rt_primary_for(node, 5) == 3);    // via-2 penalized, via-3 (unknown=0) promoted by the transition fan-out
    node.note_link_confirmed(2);            // recovery transition on next-hop 2 -> fan out again
    CHECK(rt_primary_for(node, 5) == 2);    // via-2 back to 0 penalty -> insertion-order tie restores it primary
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: compile error — `Node` has no member `test_set_link_one_way`. (After adding only the hook, the first `CHECK(... == 3)` would still fail because the transition does not yet re-sort.)
- [ ] **Step 3: Implement**
In `node.h`, after `rt_resort_for_pick` (line 485):
```cpp
    void              test_set_link_one_way(uint8_t next_hop) {                    // §bidi test: drive a one_way transition + its fan-out
        _active->_link_bidi[next_hop] = static_cast<uint8_t>(LinkBidi::one_way);
        resort_routes_for_neighbor_penalty(next_hop, "test_one_way", /*local_only=*/true);
    }
```
**M2: do NOT touch `note_link_confirmed` here.** Slice 2 already defines it complete — it fans out via `resort_routes_for_neighbor_penalty(next_hop, "link_bidi_confirm", /*local_only=*/false)` (a true `one_way→confirmed` recovery re-advertises the degraded-clear immediately; a no-op confirm moves no primary so it schedules no beacon — self-limiting). Adding a second, `local_only=true` call here would contradict Slice 2 and double-fan-out. This task's only production change is the `test_set_link_one_way` hook above — it drives a `one_way` transition + its fan-out so the Slice-4 selection tests can exercise re-ranking.
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add lib/core/node.h lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "§bidi: fan out a _link_bidi transition via resort_routes_for_neighbor_penalty (confirm + one_way re-rank)"`

### Task 4: `confirmed ≻ unknown ≻ one_way` ordering survives `route_strictly_better`
**Files:**
- Test only: `/home/staszek/MeshRoute/test/test_node_r3.cpp` (the composition lands via Tasks 1-3; this task pins the full 3-way ordering as a regression guard)

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("§bidi — route_strictly_better ranks confirmed > unknown > one_way at equal score/hops") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0xABCD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.allowed_sf_bitmap = (1u << 7); cfg.leaf_id = 0; node.on_init(cfg);
    // three equal-score equal-hop candidates for dest 5: via 2 (one_way), via 3 (unknown), via 4 (confirmed)
    CHECK(node.route_inject(/*dest=*/5, /*next=*/2, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/3, /*hops=*/1, /*score=*/200));
    CHECK(node.route_inject(/*dest=*/5, /*next=*/4, /*hops=*/1, /*score=*/200));
    node.test_set_link_one_way(2);          // via 2 = one_way  (penalty bidi_penalty_one_way_q4)
    node.note_link_confirmed(4);            // via 4 = confirmed (penalty 0)  [via 3 stays unknown, penalty 0]
    node.rt_resort_for_pick(5);             // force the full re-sort under the bidi penalties
    // primary = confirmed via 4 (or unknown via 3 — both 0 penalty, insertion tie) but NEVER the one_way via 2
    CHECK(rt_primary_for(node, 5) != 2);    // one_way is demoted out of primacy
    // and the one_way candidate sorts LAST among the three
    const RtEntry* e = nullptr; for (uint8_t i = 0; i < node.rt_count(); ++i) if (node.rt_at(i).dest == 5) e = &node.rt_at(i);
    REQUIRE(e != nullptr);
    CHECK(e->candidates[e->n - 1].next_hop == 2);   // one_way last
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: PASS in this task IF Tasks 1-3 landed; run it FIRST against a tree with Task 2 reverted to confirm it would fail (one_way via 2 keeps primacy / sorts first). If asserting pre-implementation, expected: `CHECK(rt_primary_for(node, 5) != 2)` fails (got 2 — no bidi penalty yet, insertion order keeps via 2 first).
- [ ] **Step 3: Implement** No production code — the ordering is produced by Tasks 1-3 composing through `route_strictly_better` (`node_routing.cpp:99`, `av > bv` on `effective_score`). This task is the standing 3-way ordering guard.
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add test/test_node_r3.cpp && git commit -m "§bidi: regression test — route_strictly_better orders confirmed > unknown > one_way"`

### Task 5: a SOLE one_way route STAYS selectable — delivery must not regress
**Files:**
- Test only: `/home/staszek/MeshRoute/test/test_node_r3.cpp` (guards that the penalty is OUT of `next_hop_selectable` / the both-non-viable branch + `pick_next_cascade_hop` still return the sole route)

This is the slice's load-bearing safety test: `bidi_penalty_one_way_q4` (640) drives a sole one_way route NON-viable in `route_strictly_better`, but the both-non-viable branch (`node_routing.cpp:119-122`) still returns it and `next_hop_selectable` (`node_cascade.cpp:38`) never consults the bidi penalty — so the send still fires an RTS at the one_way next-hop (catches the metal lucky-marginal delivery; Slice 6 then throttles re-probe rate).

- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("§bidi — a SOLE one_way route stays selectable: the DM still fires an RTS (no delivery loss)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // ONLY route to dst 5 is via next-hop 2
    node->test_set_link_one_way(2);                        // that sole next-hop is now authoritatively one_way
    CHECK(node->bidi_penalty_q4(2) == protocol::bidi_penalty_one_way_q4);   // it IS penalized in the score
    send_cmd(*node, /*dst=*/5, "hi");                      // originate — must NOT be dropped/deferred for lack of a viable hop
    const Ev* r = hal.last("rts_tx");
    CHECK(r != nullptr);                                   // an RTS WAS sent (sole one_way stayed selectable)
    if (r) CHECK(r->next == 2);                            // ...at the one_way next-hop (delivery not lost)
    CHECK(hal.count("defer_no_route") == 0);              // not parked as "no route" — the route is viable-for-pick
    delete node;
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native`  Expected: PASS already if `next_hop_selectable` is untouched (the guard's purpose). To prove it guards a real regression, temporarily add `if (bidi_penalty_q4(c.next_hop) >= protocol::peer_silent_penalty_q4) return false;` to `next_hop_selectable` and confirm `CHECK(r != nullptr)` then fails (no RTS — the sole hop was wrongly filtered). Revert the temporary line.
- [ ] **Step 3: Implement** No production change. The test asserts the contract invariant: `bidi_penalty_q4` is composed ONLY into `effective_score`/the sort (Tasks 1-2), never into `next_hop_selectable` (`node_cascade.cpp:38`). It locks out a future re-introduction of the 2026-06-18 freshness-hard-gate regression on the bidi plane.
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native`
- [ ] **Step 5: Commit** `git add test/test_node_r3.cpp && git commit -m "§bidi: guard — a sole one_way route stays selectable (RTS still fires; bidi penalty OUT of next_hop_selectable)"`

### Task 6: BASELINE sim gate — delivery-neutral where there is no asymmetry
**Files:**
- Test/gate: `/home/staszek/MeshRoute/simulation/BASELINE.md` (the result-comparison oracle), the native suite, and the lus engine
- No source change — this is the slice's exit gate.

- [ ] **Step 1: Write the failing test** No new doctest. The gate is the BASELINE result-comparison: with no asymmetric link present, no `_link_bidi[*]` ever reaches `one_way`, so `bidi_penalty_q4` returns 0 for every next-hop and `effective_score` is byte-identical to baseline. Define the expected: s18 delivery >= baseline, leaks 0, cross-layer held, churn sane.
- [ ] **Step 2: Run it, expect FAIL** Run: `pio test -e native` (full suite must be green first — all bidi tests from Tasks 1-5 pass). Then rebuild lus: `cmake --build ~/lora-universal-simulator/build --target lus -j8`
- [ ] **Step 3: Implement** No code. Run the BASELINE comparison on the asymmetry-free scenarios per `simulation/BASELINE.md`:
```cpp
// (shell, not C++) run the lus engine on the BASELINE scenario set and diff vs simulation/BASELINE.md:
//   ~/lora-universal-simulator/build/lus --engine meshroute <baseline scenario> ...
//   compare: s18 delivered >= baseline, leaks == 0, cross-layer delivery held, churn sane
```
- [ ] **Step 4: Run, expect PASS** Run: `pio test -e native` (green) + the BASELINE comparison shows s18 >= baseline and delivery-neutral on the no-asymmetry scenarios (the wire bits are inert this slice — no codec emit yet; that is Slice 5). Record the result in the slice's gate note.
- [ ] **Step 5: Commit** `git add simulation/BASELINE.md && git commit -m "§bidi: BASELINE gate — Slice 4 delivery-neutral (bidi penalty inert with no asymmetry; s18 >= baseline)"`

---

## SLICE 5: Advertise — degraded-bit propagation + the leaf-only heard-set census packing mode

This slice makes `emit_beacon` (node_beacon.cpp:181) carry the asymmetry signal on the wire: (a) every emitted route entry whose primary next-hop is `one_way` (locally OR wire-inherited via `degraded_from_wire`) sets `beacon_entry.degraded`, and (b) a NEW leaf-only **census** packing mode force-injects all `candidates[0].hops==1` direct-neighbour entries onto periodic (`dirty_only`) beacons WITHOUT re-dirtying them, setting the decoded `heard_set_complete` flag ONLY when the full hops==1 set fit against the LIVE `beacon_max_entries()` leaving `>= heard_set_census_min_headroom` slots — gateways skip by construction, and the pack is hard-rechecked to never overflow `beacon_max_bytes`. Depends on Slice 1 (codec carries `beacon_in.heard_set_complete` + `beacon_entry.degraded` on the wire, `beacon_out.heard_set_complete` on parse), Slice 2 (`_link_bidi[256]`, `RtCandidate.degraded_from_wire`), and Slice 3's contract method `candidate_degraded(const RtCandidate&)`. Reference all of these by their contract names.

### Task 1: Add a `test_emit_beacon` seam so tests can drive a deterministic periodic beacon
**Files:** Modify lib/core/node.h (near the other `test_*` seams at :486-487, i.e. `test_build_suspect_ext` / `test_apply_suspect_gossip`).
- [ ] **Step 1: Write the failing test** (add to test/test_node_query.cpp, in the existing `namespace`/TEST suite — it shares `TestHal`/`meta_at`)
```cpp
TEST_CASE("census seam — test_emit_beacon('periodic') drives one BCN frame out") {
    TestHal hal; Node node(hal, /*id=*/20, /*key=*/0xC0DE);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
    node.on_init(cfg);
    node.route_inject(/*dest=*/30, /*next=*/30, /*hops=*/1, /*score=*/96);  // a direct neighbour
    const size_t before = hal.tx_frames.size();
    node.test_emit_beacon("periodic");
    CHECK(hal.tx_frames.size() == before + 1);
    CHECK((hal.tx_frames.back().bytes[0] >> 4) == 0x0);   // cmd-nibble B (beacon)
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: pio test -e native  Expected: compile error — `class meshroute::Node has no member named test_emit_beacon`.
- [ ] **Step 3: Implement** add the public seam next to `test_apply_suspect_gossip` (node.h:487)
```cpp
    void              test_emit_beacon(const char* kind) { emit_beacon(kind); }   // census/advertise tests: drive a deterministic beacon (bypasses the throttle)
```
- [ ] **Step 4: Run, expect PASS** Run: pio test -e native
- [ ] **Step 5: Commit** `git add lib/core/node.h test/test_node_query.cpp && git commit -m "slice5: test_emit_beacon seam for census/advertise tests"`

### Task 2: Set the route-entry `degraded` wire bit when the primary next-hop is one_way (or wire-inherited)
**Files:** Modify lib/core/node_beacon.cpp (the entry-fill loop at :332-340, inside `emit_beacon`). Test: test/test_node_query.cpp.
- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("advertise — a one_way primary next-hop emits the entry degraded bit; confirmed/unknown does not") {
    TestHal hal; Node node(hal, /*id=*/21, /*key=*/0xBEAD);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
    node.on_init(cfg);
    node.route_inject(/*dest=*/40, /*next=*/40, /*hops=*/1, /*score=*/96);   // good route
    node.route_inject(/*dest=*/41, /*next=*/41, /*hops=*/1, /*score=*/96);
    node.note_link_confirmed(40);            // 40 = confirmed -> NOT degraded
    node.update_link_bidi_from_beacon(41, nullptr, 0, /*complete=*/true);    // 41 absent+complete -> one_way
    node.test_emit_beacon("periodic");
    REQUIRE(!hal.tx_frames.empty());
    const auto& f = hal.tx_frames.back();
    auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
    REQUIRE(o.has_value());
    bool saw40 = false, saw41 = false;
    for (uint8_t i = 0; i < o->n_entries; ++i) {
        auto e = parse_beacon_entry(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()), *o, i);
        REQUIRE(e.has_value());
        if (e->dest == 40) { saw40 = true; CHECK_FALSE(e->degraded); }
        if (e->dest == 41) { saw41 = true; CHECK(e->degraded); }
    }
    CHECK(saw40); CHECK(saw41);
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: pio test -e native  Expected: `e->degraded` for dest 41 is `false` (the emitter never sets the bit) — `CHECK(e->degraded)` fails.
- [ ] **Step 3: Implement** in the entry-fill loop at node_beacon.cpp:335-339, add the degraded assignment (the LIVE recompute via the Slice-3 `candidate_degraded`, never a sticky cache — MF5)
```cpp
        entries[k].dest         = re.dest;
        entries[k].next         = pc.next_hop;
        entries[k].score_bucket = static_cast<uint8_t>(bucket_of_snr_4b(pc.score));
        entries[k].is_gateway   = pc.is_gateway;
        entries[k].hops         = pc.hops;
        entries[k].degraded     = candidate_degraded(pc);   // §5 transitive: degraded_from_wire OR _link_bidi[next]==one_way (MF5 live recompute)
        if (entries[k].degraded) MR_EMIT("degraded_advertise", EF_I("dest",re.dest),EF_I("next",pc.next_hop));
```
- [ ] **Step 4: Run, expect PASS** Run: pio test -e native
- [ ] **Step 5: Commit** `git add lib/core/node_beacon.cpp test/test_node_query.cpp && git commit -m "slice5: advertise degraded route-entry bit for one_way next-hops"`

### Task 3: Census force-injects all hops==1 entries onto a dirty_only beacon (leaf-only, no re-dirty)
**Files:** Modify lib/core/node_beacon.cpp (insert a NEW census partition pass after the Phase-2 stable-rotation block at :330, before the entry-fill loop at :332). Test: test/test_node_query.cpp.
- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("census — a periodic (dirty_only) beacon force-injects ALL hops==1 entries even when none are dirty") {
    TestHal hal; Node node(hal, /*id=*/22, /*key=*/0xCAFE);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
    node.on_init(cfg);
    node.route_inject(50, 50, 1, 96);   // 3 direct neighbours (hops==1)
    node.route_inject(51, 51, 1, 96);
    node.route_inject(52, 52, 1, 96);
    node.route_inject(60, 50, 3, 80);   // a remote (hops==3) via 50
    node.test_emit_beacon("periodic");  // pumps a dirty page (first-learn dirtied) + clears dirty
    hal.tx_frames.clear();
    node.test_emit_beacon("periodic");  // STEADY state: nothing dirty -> only the census injects
    REQUIRE(!hal.tx_frames.empty());
    const auto& f = hal.tx_frames.back();
    auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
    REQUIRE(o.has_value());
    bool h50=false,h51=false,h52=false;
    for (uint8_t i = 0; i < o->n_entries; ++i) {
        auto e = parse_beacon_entry(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()), *o, i);
        REQUIRE(e.has_value());
        if (e->dest==50) h50=true; if (e->dest==51) h51=true; if (e->dest==52) h52=true;
    }
    CHECK(h50); CHECK(h51); CHECK(h52);   // the full hops==1 set rides the steady-state beacon
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: pio test -e native  Expected: the second (steady-state) beacon carries 0 entries (Phase 1 finds nothing dirty, Phase 2 is `!dirty_only`-gated → dormant) — `CHECK(h50)` fails.
- [ ] **Step 3: Implement** insert the NEW census pass at node_beacon.cpp:331 (right after the Phase-2 block closes, before the `for (uint8_t k = 0; k < n; ++k)` entry-fill at :332). MF3: a separate partition pass over `_active->_rt` filtering `candidates[0].hops==1`, force-injected on `dirty_only` beacons, skipping anything already packed, and — critically — NOT touching `dirty` (so the post-pack dirty-clear at :359 is untouched and we never re-dirty every period). Leaf-only (OI3): gateways (`n_layers==2`) skip.
```cpp
        // §5 census (MF3): on a steady-state (dirty_only) LEAF beacon, force-inject ALL direct-neighbour (candidates[0].hops==1)
        // entries that the dirty/stable passes did not already pack — a NEW partition pass, NOT Phase-2 reuse (Phase 2 is
        // !dirty_only-gated → dormant here). Does NOT set dirty (the :359 clear is untouched; no re-dirty-every-period). Gateways
        // skip by construction (OI3 leaf-only — they already skip the bitmap/digest). bidi_census_full tracked for the complete-flag.
        // M1: set bidi_census_full=true ONLY inside this gate, so a NON-census beacon (discovery/sync = !dirty_only, gateway,
        // or mobile) leaves it false -> heard_set_complete=false (absence is authoritative ONLY on a beacon that ran the census).
        if (dirty_only && _cfg.n_layers != 2 && !_cfg.is_mobile) {
            bidi_census_full = true;
            for (uint8_t i = 0; i < _active->_rt_count; ++i) {
                if (_active->_rt[i].n == 0 || _active->_rt[i].candidates[0].hops != 1) continue;
                bool already = false;
                for (uint8_t k = 0; k < n; ++k) if (pack_idx[k] == i) { already = true; break; }
                if (already) continue;
                if (n >= max_entries) { bidi_census_full = false; break; }   // ran out of slots -> set incomplete (Task 4)
                pack_idx[n++] = i;
            }
        }
```
add the declaration just before the entry-fill loop's preceding `in.entries =` line is built — place `bool bidi_census_full = false;` alongside the `uint8_t n = 0, ...` counters at node_beacon.cpp:311:
```cpp
    uint8_t n = 0, dirty_n = 0, stable_n = 0, total_dirty = 0;
    bool    bidi_census_full = false;   // §5: did the FULL hops==1 set fit THIS beacon (drives heard_set_complete, Task 4)
```
- [ ] **Step 4: Run, expect PASS** Run: pio test -e native
- [ ] **Step 5: Commit** `git add lib/core/node_beacon.cpp test/test_node_query.cpp && git commit -m "slice5: leaf-only heard-set census packing pass (force-inject hops==1, no re-dirty)"`

### Task 4: Set `heard_set_complete` ONLY when the full hops==1 set fit leaving >= headroom slots
**Files:** Modify lib/core/node_beacon.cpp (the census pass from Task 3 + the `beacon_in in{}` setup; set `in.heard_set_complete` before `pack_beacon` at :343-344). Test: test/test_node_query.cpp.
- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("census complete-flag — set when the full direct-neighbour set fit with headroom; clear when it overflowed") {
    // (A) small set fits with room -> complete=true
    {
        TestHal hal; Node node(hal, 23, 0xF00D);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
        node.on_init(cfg);
        node.route_inject(70, 70, 1, 96);
        node.route_inject(71, 71, 1, 96);
        node.test_emit_beacon("periodic"); hal.tx_frames.clear();
        node.test_emit_beacon("periodic");
        REQUIRE(!hal.tx_frames.empty());
        const auto& f = hal.tx_frames.back();
        auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        REQUIRE(o.has_value());
        CHECK(o->heard_set_complete);
    }
    // (B) too many direct neighbours to fit the headroom rule -> complete=false
    {
        TestHal hal; Node node(hal, 24, 0xF00E);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
        node.on_init(cfg);
        for (int d = 100; d < 100 + 40; ++d)              // 40 hops==1 neighbours > the live cap (34)
            node.route_inject(static_cast<uint8_t>(d), static_cast<uint8_t>(d), 1, 96);
        node.test_emit_beacon("periodic"); hal.tx_frames.clear();
        node.test_emit_beacon("periodic");
        REQUIRE(!hal.tx_frames.empty());
        const auto& f = hal.tx_frames.back();
        auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        REQUIRE(o.has_value());
        CHECK_FALSE(o->heard_set_complete);
    }
    // (C) M1: a SYNC / full-page beacon (!dirty_only) must NOT assert complete — the census runs only on dirty_only beacons.
    {
        TestHal hal; Node node(hal, 25, 0xF00F);
        NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
        node.on_init(cfg);
        node.route_inject(72, 72, 1, 96);
        node.test_emit_beacon("sync");
        REQUIRE(!hal.tx_frames.empty());
        const auto& f = hal.tx_frames.back();
        auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
        REQUIRE(o.has_value());
        CHECK_FALSE(o->heard_set_complete);   // M1: non-census beacon -> never authoritative-complete
    }
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: pio test -e native  Expected: `o->heard_set_complete` is always `false` (the emitter never populates `in.heard_set_complete`) — case (A)'s `CHECK(o->heard_set_complete)` fails.
- [ ] **Step 3: Implement** First, refine the census pass (from Task 3) so `bidi_census_full` also goes false when the set fit but left fewer than `heard_set_census_min_headroom` free slots (MF2 headroom rule against the LIVE `max_entries`). Replace the `if (n >= max_entries) { ... break; }` guard inside the census loop with a headroom-aware guard, and re-check headroom AFTER the loop:
```cpp
                if (n >= max_entries) { bidi_census_full = false; break; }
            }
            // MF2 headroom: the FULL set "fit" only if it left >= heard_set_census_min_headroom free slots vs the live cap.
            if (bidi_census_full && (max_entries - n) < protocol::heard_set_census_min_headroom) bidi_census_full = false;
        }
```
Then set the wire complete-flag right before `pack_beacon` (node_beacon.cpp:343, after `in.entries = ...` at :341):
```cpp
    in.heard_set_complete = bidi_census_full;   // §5/MF1 byte-3 bit 4 — authoritative only when the full hops==1 set fit (Task 3/4)
    if (in.heard_set_complete) MR_EMIT("link_census_complete", EF_I("n_entries",n),EF_I("max",max_entries));
```
- [ ] **Step 4: Run, expect PASS** Run: pio test -e native
- [ ] **Step 5: Commit** `git add lib/core/node_beacon.cpp test/test_node_query.cpp && git commit -m "slice5: heard_set_complete flag via the live beacon_max_entries headroom rule"`

### Task 5: Gateways skip the census (leaf-only, OI3)
**Files:** Test: test/test_node_query.cpp (the `n_layers==2` skip is already coded in Task 3; this proves it). No production change expected — if it fails, the bug is a missing/incorrect `n_layers != 2` guard in the census pass.
- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("census — a GATEWAY (n_layers==2) skips the census: a steady-state beacon injects no hops==1 entries") {
    TestHal hal; Node node(hal, /*id=*/1, /*key=*/0x6A7E);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0;
    cfg.n_layers = 2; cfg.is_gateway = true; cfg.quiet_threshold_ms = 0;
    cfg.layers[0].layer_id = 0; cfg.layers[0].routing_sf = 7;
    cfg.layers[1].layer_id = 1; cfg.layers[1].routing_sf = 7;
    node.on_init(cfg);
    node.route_inject(80, 80, 1, 96);    // a direct neighbour on the active leaf
    node.route_inject(81, 81, 1, 96);
    node.test_emit_beacon("periodic"); hal.tx_frames.clear();
    node.test_emit_beacon("periodic");   // steady state: gateway must NOT census-inject
    REQUIRE(!hal.tx_frames.empty());
    const auto& f = hal.tx_frames.back();
    auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
    REQUIRE(o.has_value());
    CHECK(o->n_entries == 0);             // nothing dirty + no census => empty page
    CHECK_FALSE(o->heard_set_complete);   // gateways never assert authority
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: pio test -e native  Expected: PASS if the `n_layers != 2` guard from Task 3 is correct; if the guard is missing the gateway census-injects entries and `CHECK(o->n_entries == 0)` fails. (This test pins the OI3 guard so a later refactor can't silently drop it. If on_init refuses this exact dual-layer cfg shape, adjust `cfg.layers[*]` to the minimum a gateway on_init accepts — the assertion is on the census skip, not the layer setup.)
- [ ] **Step 3: Implement** confirm the census pass condition is `dirty_only && _cfg.n_layers != 2 && !_cfg.is_mobile` (added in Task 3). No further change if Task 3 is correct.
- [ ] **Step 4: Run, expect PASS** Run: pio test -e native
- [ ] **Step 5: Commit** `git add test/test_node_query.cpp && git commit -m "slice5: pin the gateway census-skip (OI3 leaf-only)"`

### Task 6: The census never pushes the beacon over beacon_max_bytes (F2 hard re-check)
**Files:** Modify lib/core/node_beacon.cpp (the existing F2 overflow backstop at :345-349 already returns on `len == 0`; this task adds an explicit defensive assert-via-test that a maxed-out census stays within `max_entries` so `pack_beacon` never returns 0). Test: test/test_node_query.cpp.
- [ ] **Step 1: Write the failing test**
```cpp
TEST_CASE("census — never overflows: a saturated direct-neighbour set still emits a valid <= beacon_max_bytes frame") {
    TestHal hal; Node node(hal, 25, 0xBABE);
    NodeConfig cfg; cfg.routing_sf = 7; cfg.leaf_id = 0; cfg.quiet_threshold_ms = 0;
    node.on_init(cfg);
    for (int d = 90; d < 90 + 60; ++d)                 // 60 direct neighbours >> the live cap
        node.route_inject(static_cast<uint8_t>(d), static_cast<uint8_t>(d), 1, 96);
    node.test_emit_beacon("periodic"); hal.tx_frames.clear();
    node.test_emit_beacon("periodic");
    REQUIRE(!hal.tx_frames.empty());
    const auto& f = hal.tx_frames.back();
    CHECK(f.bytes.size() <= protocol::beacon_max_bytes);   // F2: never overflowed
    auto o = parse_beacon(std::span<const uint8_t>(f.bytes.data(), f.bytes.size()));
    REQUIRE(o.has_value());                                 // pack_beacon did NOT return 0
    CHECK_FALSE(o->heard_set_complete);                    // saturated -> never authoritative
    CHECK(hal.count("beacon_pack_overflow") == 0);         // the :345 fail-loud backstop never fired
}
```
- [ ] **Step 2: Run it, expect FAIL** Run: pio test -e native  Expected: PASS if the Task-3 `if (n >= max_entries) break;` slot guard holds the census to the byte-budget cap; if the census ignored `max_entries` it overflows → `pack_beacon` returns 0, `tx_frames` stays empty, and `REQUIRE(!hal.tx_frames.empty())` fails. (This pins the F2 guard.)
- [ ] **Step 3: Implement** confirm the census loop's `if (n >= max_entries) { bidi_census_full = false; break; }` clamp (from Task 3) bounds the pack to the LIVE `beacon_max_entries()` so the entry page can never exceed `beacon_max_bytes`. No further production change if Task 3 is correct.
- [ ] **Step 4: Run, expect PASS** Run: pio test -e native
- [ ] **Step 5: Commit** `git add test/test_node_query.cpp && git commit -m "slice5: pin the census F2 byte-budget non-overflow"`

### Task 7: BASELINE dense-inert gate — the census is delivery-neutral and adds no airtime where there's no asymmetry
**Files:** Test: rebuild the sim engine + run the BASELINE/topo gate. No production change (verification only).
- [ ] **Step 1: Write the failing test** (this task's "test" is the sim gate, not a doctest — record it as the gate command + the pass criterion). Build the sim engine and run the topology:
```cpp
// (gate, not a doctest) Rebuild lus after the lib/core change, then run the 9-node asymmetric topology.
// Pass = the census stays dense-inert: byte-md5 may shift (the new bits) but DM delivery >= BASELINE,
// leaks == 0, cross-layer held, and a dense/no-asymmetry node emits heard_set_complete=0 (flag-off = today's beacon).
```
- [ ] **Step 2: Run it, expect FAIL** Run: `cmake --build ~/lora-universal-simulator/build --target lus -j8 && ~/lora-universal-simulator/build/lus --engine meshroute simulation/topo_9node.json`  Expected (pre-implementation baseline capture): note the current `rts_tx`-to-isolated and DM delivery numbers from simulation/BASELINE.md to compare against.
- [ ] **Step 3: Implement** none — Tasks 2-6 are the implementation. This task only re-runs the gate after them.
- [ ] **Step 4: Run, expect PASS** Run: `cmake --build ~/lora-universal-simulator/build --target lus -j8 && ~/lora-universal-simulator/build/lus --engine meshroute simulation/topo_9node.json`  Pass = DM delivery >= BASELINE (simulation/BASELINE.md), leaks 0, cross-layer held; the census engaged sparse / inert dense (no full-page-every-period regression). Confirm s18 from the BASELINE suite is >= baseline (the wire-bit byte-md5 shift is expected; delivery must not regress).
- [ ] **Step 5: Commit** `git add -A && git commit -m "slice5: census BASELINE dense-inert gate green (delivery-neutral, no airtime regression)"`

---

## SLICE 6: slow re-probe interception + recovery

This slice stops the doomed 9–80-RTS cascade burst on a sole route whose failed next-hop is `_link_bidi[from_next] == one_way` (handshake-isolated, liveness-HEALTHY → §P3 does NOT catch it — MF4). It intercepts in `cascade_to_alt`'s no-alt branch (`node_cascade.cpp:107-115`), throttling to ONE RTS per `link_reprobe_ttl_ms` via a per-next-hop last-reprobe timestamp, while STILL firing the one probe (sole-route delivery must not regress). A successful CTS recovers (`note_link_confirmed`, wired in Slice 2) — this slice confirms `link_recover` is emitted there. §P3 (liveness-silent RREQ) is orthogonal + left untouched. Symbols from earlier slices (`_link_bidi`, `LinkBidi`, `note_link_confirmed`, `update_link_bidi_from_beacon`, `candidate_degraded`, `bidi_penalty_one_way_q4`, `link_reprobe_ttl_ms`) are referenced by their contract names.

### Task 1: Per-next-hop last-reprobe timestamp in LayerRuntime
**Files:** Modify `lib/core/node.h` (LayerRuntime, after `_mobile_peer[32]` at node.h:1119); Test `test/test_node_r3.cpp` (new TEST_CASE, after the cascade block ~line 541).

- [ ] **Step 1: Write the failing test** — a fresh node has its reprobe clock zeroed so the FIRST one-way giveup probes immediately (no spurious throttle on a never-probed link).
```cpp
TEST_CASE("bidi reprobe — a one-way sole route fires its FIRST probe immediately (clock starts at 0)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to dst 5 via next-hop 2
    // Mark next-hop 2 one-way: advertiser 2's complete heard-set OMITS self(1) -> Slice 3 detection sets one_way.
    node->update_link_bidi_from_beacon(/*advertiser=*/2, /*entries=*/nullptr, /*n=*/0, /*complete=*/true);
    hal._now = 5000;
    send_cmd(*node, /*dst=*/5, "hi");
    const int rts_before = hal.count("rts_tx");
    exhaust_rts_same_hop(*node);                           // no alt -> one-way interception
    CHECK(hal.count("link_reprobe") == 1);                 // the single throttled probe fired
    CHECK(hal.count("rts_tx") == rts_before + 1);          // exactly ONE re-RTS (the probe), no burst
    CHECK(hal.count("cascade_requeue") == 0);              // the burst requeue was suppressed
    delete node;
}
```
- [ ] **Step 2: Run it, expect FAIL** — Run: `pio test -e native` — Expected: compile error `'_link_reprobe_last_ms' was not declared` (and the interception branch in Task 2 not yet present, so `link_reprobe` count == 0).
- [ ] **Step 3: Implement** — add the array to `LayerRuntime` immediately after the `_mobile_peer[32]` member (node.h:1119):
```cpp
        // Slow-reprobe throttle (asymmetric-link slice 6): per-next-hop last single-probe time for a
        // _link_bidi==one_way sole route. FULL 0..255 range, eviction-free (like _dest_seen_ms) so an
        // isolated next-hop is throttled even if its PeerLiveness slot was LRU-evicted. 0 = never reprobed
        // (clock-at-0 -> the FIRST giveup probes immediately, then once per link_reprobe_ttl_ms).
        uint64_t      _link_reprobe_last_ms[256] = {};
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native` (passes once Task 2's interception lands; commit this task together with Task 2).
- [ ] **Step 5: Commit** — `git add lib/core/node.h test/test_node_r3.cpp && git commit -m "slice6: per-next-hop _link_reprobe_last_ms throttle clock in LayerRuntime"`

### Task 2: Intercept the no-alt giveup for a one-way next-hop (throttle to one probe per TTL, still fire the probe)
**Files:** Modify `lib/core/node_cascade.cpp` (`cascade_to_alt` no-alt branch, node_cascade.cpp:107-115); Test `test/test_node_r3.cpp` (new TEST_CASE after Task 1's).

- [ ] **Step 1: Write the failing test** — a SECOND giveup within `link_reprobe_ttl_ms` does NOT re-probe (one attempt/TTL), and a giveup AFTER the TTL probes again — while a NON-one-way sole route keeps the legacy burst requeue (no regression).
```cpp
TEST_CASE("bidi reprobe — one probe per link_reprobe_ttl_ms; non-one-way keeps the legacy requeue burst") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2
    node->update_link_bidi_from_beacon(/*advertiser=*/2, nullptr, 0, /*complete=*/true);  // 2 -> one_way
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
```
- [ ] **Step 2: Run it, expect FAIL** — Run: `pio test -e native` — Expected: `link_reprobe` count is 0 (no interception exists) so the first CHECK fails, and the one-way path currently falls into `try_cascade_requeue` so `cascade_requeue == 1` instead of 0.
- [ ] **Step 3: Implement** — replace the `else` body of `cascade_to_alt` (node_cascade.cpp:107-115, the no-alt branch). The new `_link_bidi==one_way` check runs BEFORE `try_cascade_requeue`; it is ORTHOGONAL to the §P3 liveness RREQ above it (which stays exactly as-is):
```cpp
    } else {
        // §P3 active rediscovery: all candidates exhausted AND the primary that just failed is SILENT/DEAD (confirmed
        // flaky, not merely congested) -> the route table holds only dead paths to dst. Flood an RREQ to find a FRESH
        // path NOW rather than stalling on the requeue / 3h aging — closes the no-alt dead-relay case (the user's bug:
        // a dest reachable only via a departed relay). Rate-limited (rreq_rate_ok); a normal congested giveup does NOT.
        if (liveness_penalty_q4(from_next) >= protocol::peer_silent_penalty_q4)
            emit_route_request(pt.dst, _cfg.dv_hop_cap);  // full-radius requery (network-wide configured TTL, like the deferred-drain requery)
        // Slow-reprobe interception (asymmetric-link slice 6, MF4): a one-way next-hop stays liveness-HEALTHY
        // (clear_peer_suspect fires on its every beacon) so §P3 above never triggers on it -> the giveup would
        // fall straight to the 9–80-RTS try_cascade_requeue burst. Instead: throttle to ONE RTS per
        // link_reprobe_ttl_ms (the probe catches metal lucky-marginal deliveries + a real CTS recovers via
        // note_link_confirmed). The single probe STILL flies (sole-route delivery must not regress).
        if (_active->_link_bidi[from_next] == static_cast<uint8_t>(LinkBidi::one_way)) {
            const uint64_t now  = _hal.now();
            const uint64_t last = _active->_link_reprobe_last_ms[from_next];
            const bool window_open = (last == 0) || (now - last >= protocol::link_reprobe_ttl_ms);
            if (window_open) {
                _active->_link_reprobe_last_ms[from_next] = now;
                MR_EMIT("link_reprobe", EF_I("origin", pt.origin), EF_I("dst", pt.dst),
                        EF_I("ctr", pt.ctr), EF_I("next", from_next));
                pt.alts_tried_n = 0;                          // re-allow the one-way hop for the single probe
                pt.next = from_next;
                pt.retries_left = effective_rts_max_retries(pt.requeue_count);
                pt.retry_attempt = 0;
                tx_rts_retry();                                // ONE probe (re-arms kRtsTimeoutTimerId), NO jitter
            } else {
                // Inside the throttle window: clean giveup, NO burst. The route stays in the table (reversible).
                MR_TELEMETRY(
                    EventField gf[] = { { .key = "dst", .type = EventField::T::i64, .i = pt.dst },
                                        { .key = "ctr", .type = EventField::T::i64, .i = pt.ctr } };
                    _hal.emit("path_cascade_exhausted", gf, 2);
                    _hal.emit(giveup_event, gf, 2); );
                { Push pu{}; pu.kind = PushKind::send_failed; pu.dst = pt.dst; pu.ctr = pt.ctr; enqueue_push(pu); }
                _active->_pending_tx.reset();
                become_free();
            }
            return;
        }
        try_cascade_requeue(pt, giveup_event);           // all candidates tried (NOT one-way -> legacy burst)
    }
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`
- [ ] **Step 5: Commit** — `git add lib/core/node_cascade.cpp test/test_node_r3.cpp && git commit -m "slice6: throttle the one-way sole-route giveup to one probe per link_reprobe_ttl_ms (MF4)"`

### Task 3: The single probe still completes a lucky delivery + a CTS recovers the link (link_recover)
**Files:** Modify `lib/core/node_routing.cpp` (`note_link_confirmed`, ensure the `link_recover` MR_EMIT — wired in Slice 2; this task asserts + backfills it); Test `test/test_node_r3.cpp` (new TEST_CASE).

- [ ] **Step 1: Write the failing test** — the throttled probe flies; a CTS on it flips `one_way → confirmed`, clears the candidate degraded state, and emits `link_recover`.
```cpp
TEST_CASE("bidi reprobe — the single probe flies, a CTS recovers (confirmed + degraded cleared + link_recover)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2
    node->update_link_bidi_from_beacon(/*advertiser=*/2, nullptr, 0, /*complete=*/true);  // 2 -> one_way
    // Sanity: the sole candidate to dst 5 reads degraded while 2 is one_way.
    // M3: rt_find is private — locate the entry via the public rt_count()/rt_at() seams.
    auto find_rt = [&](uint8_t dest) -> const RtEntry* {
        for (uint8_t i = 0; i < node->rt_count(); ++i) if (node->rt_at(i).dest == dest) return &node->rt_at(i);
        return nullptr;
    };
    const RtEntry* e = find_rt(5); REQUIRE(e != nullptr); REQUIRE(e->n == 1);
    CHECK(node->candidate_degraded(e->candidates[0]) == true);
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    const int rts_before = hal.count("rts_tx");
    exhaust_rts_same_hop(*node);                           // one-way interception -> ONE probe RTS to 2
    CHECK(hal.count("rts_tx") == rts_before + 1);          // the lucky-marginal probe actually flew
    const Ev* probe = hal.last("rts_tx"); REQUIRE(probe != nullptr); CHECK(probe->next == 2);
    // The probe gets a real CTS from next-hop 2 -> recovery.
    RxMeta m2{12.0f, -70.0f, 0, static_cast<int8_t>(2)};
    std::array<uint8_t,8> cb{};
    const size_t cn = mk_cts(/*rx_id=*/1, /*tx_id=*/2, /*data_sf=*/7, cb);
    node->on_recv(cb.data(), cn, m2);                      // CTS matched -> note_link_confirmed(2)
    CHECK(hal.count("link_recover") == 1);                 // it WAS one_way -> recovery emitted
    const RtEntry* e2 = find_rt(5); REQUIRE(e2 != nullptr); REQUIRE(e2->n == 1);
    CHECK(node->candidate_degraded(e2->candidates[0]) == false);   // recompute is live -> degraded cleared
    delete node;
}
```
- [ ] **Step 2: Run it, expect FAIL** — Run: `pio test -e native` — Expected: `link_recover` count is 0 if Slice 2's `note_link_confirmed` did not emit the recovery event on a `one_way → confirmed` transition.
- [ ] **Step 3: Implement** — **No production rewrite (M2).** Slice 2's `note_link_confirmed` already does everything this slice needs: it captures `was_one_way` before overwriting, sets `confirmed` + stamps `_link_bidi_confirmed_ms`, fans out via `resort_routes_for_neighbor_penalty(next_hop, "link_bidi_confirm", /*local_only=*/false)` (so the LIVE `candidate_degraded` recompute clears the degraded bit and the re-dirty/re-advertise propagates recovery downstream), and emits `link_recover` only on the `was_one_way` transition. Do **NOT** redefine it here — a second definition dropped the `next_hop==0||0xFF` sentinel guard and mis-called the 3-arg `resort_routes_for_neighbor_penalty` with one arg (compile error). This task is purely the **recovery verification test** above (it asserts Slice 2's `link_recover` + the live degraded-clear fire on a real-CTS recovery).
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`
- [ ] **Step 5: Commit** — `git add lib/core/node_routing.cpp test/test_node_r3.cpp && git commit -m "slice6: emit link_recover on the one_way->confirmed CTS recovery in note_link_confirmed"`

### Task 4: §P3 orthogonality guard — a liveness-silent (not one-way) no-alt giveup still RREQs + requeues, no link_reprobe
**Files:** Test `test/test_node_r3.cpp` (new TEST_CASE — regression guard only; no production change).

- [ ] **Step 1: Write the failing test** — a sole route whose next-hop is liveness-SILENT but NOT `one_way` still hits the §P3 RREQ + the legacy requeue, and never enters the bidi reprobe path (confirms the two planes don't cross-fire).
```cpp
TEST_CASE("bidi reprobe — §P3 liveness-silent path is orthogonal (RREQ + requeue, no link_reprobe)") {
    TestHal hal;
    Node* node = mk_sender_with_routes(hal, {{2,1,14}});   // sole route to 5 via 2; 2 stays _link_bidi=unknown
    // Drive next-hop 2 to liveness-SILENT (>= peer_silent_penalty_q4) WITHOUT touching the bidi plane.
    while (node->liveness_penalty_q4(2) < protocol::peer_silent_penalty_q4)
        node->mark_neighbor_silent_for_test(2);            // Slice-independent: existing liveness test seam (record_peer_rts_timeout driver)
    const int rreq_before = hal.count("route_request");
    hal._now = 1000;
    send_cmd(*node, 5, "hi");
    exhaust_rts_same_hop(*node);                           // no alt + silent -> §P3 RREQ + legacy requeue
    CHECK(hal.count("link_reprobe") == 0);                 // bidi plane NOT engaged (2 is unknown, not one_way)
    CHECK(hal.count("route_request") > rreq_before);       // §P3 RREQ fired (orthogonal, unaffected)
    CHECK(hal.count("cascade_requeue") == 1);              // legacy requeue path intact
    delete node;
}
```
- [ ] **Step 2: Run it, expect FAIL** — Run: `pio test -e native` — Expected: a compile error on `mark_neighbor_silent_for_test` if that seam name differs; if so, drive silence via the real path instead — replace the `while` loop with repeated `node->record_peer_rts_timeout(2, /*ctr_lo=*/0)` calls until `liveness_penalty_q4(2) >= protocol::peer_silent_penalty_q4` (both are existing public/test-reachable Node members; no new seam). Re-run; the assertions then exercise the unchanged §P3 branch and PASS without any production edit.
- [ ] **Step 3: Implement** — none (pure regression guard); if Step 2 showed the seam name mismatch, the only edit is in the TEST, swapping to the `record_peer_rts_timeout` driver shown above.
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native`
- [ ] **Step 5: Commit** — `git add test/test_node_r3.cpp && git commit -m "slice6: regression guard — §P3 liveness-silent path stays orthogonal to the bidi reprobe"`

### Task 5: Slice gate — native full suite + the sim A/B + metal-tune handoff
**Files:** none (verification only — `simulation/topo_9node.json`, `simulation/BASELINE.md`).

- [ ] **Step 1: Write the failing test** — n/a (the gate runs the already-written suite + the sim A/B; no new doctest).
- [ ] **Step 2: Run it, expect FAIL** — Run: `pio test -e native` — Expected: PASS (all of this slice's tests + the full existing suite green; if red, fix before gating).
- [ ] **Step 3: Implement** — run the ★ sim A/B (feature off↔on) on the asymmetric 9-node topology and record the result against the baseline:
```cpp
// (shell, not C++): rebuild the sim engine, then run feature off vs on and diff the airtime/delivery oracle.
// cmake --build ~/lora-universal-simulator/build --target lus -j8
// ~/lora-universal-simulator/build/lus --engine meshroute simulation/topo_9node.json   # baseline (feature default)
// Gate PASS requires ALL of:
//   (a) DM delivery >= BASELINE (simulation/BASELINE.md) — must NOT regress;
//   (b) total rts_tx to the isolated nodes (204/247) DROPS sharply — the link_reprobe count replaces the
//       9–80-RTS burst (target: the 247-RTS hotspot collapses to ~one probe per link_reprobe_ttl_ms);
//   (c) NO false-demotion: a confirmed-bidi link is never link_one_way'd and never link_reprobe'd;
//   (d) a RECOVERY case: flip a link bidi->one_way->bidi, confirm link_one_way then link_recover fire +
//       the degraded bit propagates then clears (candidate_degraded false again).
```
- [ ] **Step 4: Run, expect PASS** — Run: `pio test -e native` then the sim A/B above; confirm (a)-(d). Leave the metal node-72 tune of `link_reprobe_ttl_ms` (60000 seed) and the `bidi_penalty_one_way_q4` 640→`peer_suspect_penalty_q4`(192) fallback to the user (the lucky-marginal balance the sim is blind to — these are metal-only judgments per the spec).
- [ ] **Step 5: Commit** — no code change; report the sim-A/B numbers + the green native run to the user for the commit + the metal flash (the user does all commits + the node-72 tune).
