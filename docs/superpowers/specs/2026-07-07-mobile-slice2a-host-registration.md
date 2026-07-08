<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 2a: host-side registration + the J mobile-OFFER wire — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-07). **Slice 2a of mobile v1** (design `2026-07-07-mobile-node-handling-assumptions.md` §13/§17; builds on Slice 1's marks). The user commits; I quality-gate. This is the **HOST side** — a static node accepts a mobile: activate the deferred J `DISCOVER→OFFER→CLAIM` **for mobiles only**, a new `_mobile_reg` table, host-assigned LOCAL ids, with **claim-stands** confirmation (identical to the static DAD). **Slice 2b (mobile side) follows.**

## ★★ NON-NEGOTIABLE: the static mesh must be UNAFFECTED
Every new branch is **gated on `is_mobile`**; the existing static paths run byte-for-byte as before. Specifically:
- The **leaf filter** (`node_join.cpp:189` `if (j.leaf_id != _cfg.leaf_id) return;`) is bypassed **ONLY** for `(opcode == DISCOVER && is_mobile)`. CLAIM, DENY, and a non-mobile DISCOVER stay leaf-filtered exactly as today.
- The **static DAD** (CLAIM/DENY handling, `join_choose_candidate_id`) is **not touched** — the mobile CLAIM path is a *new* `if (j.is_mobile)` branch *inside* the CLAIM case, before the existing static logic.
- The **wire_version gate** (`node_join.cpp:190-193`) stays ahead of all opcode dispatch, mobile included.
- **OFFER is a deferred opcode** (no device emits/consumes it today), so widening its parse to a 9-B `is_mobile` variant cannot affect any existing frame.
- A **gate test proves it**: existing native suite green + a new regression case that a static CLAIM/DENY and a *foreign-leaf* non-mobile J are handled identically to before.

## Design decisions (forced by the wire, rationale in-line)
1. **9-B mobile OFFER = OFFER opcode (3) + `is_mobile`=1**, NOT a new opcode. The J opcode is 2 bits (`b5..4`, values 0-3 all used). So the mobile OFFER reuses opcode 3 with `is_mobile`=1 selecting a **9-B** body (the 8-B OFFER + a `proposed_mobile_id` byte). `is_mobile` on an OFFER means "this OFFER carries a mobile local-id" (the bit was unused on OFFER). No compat break (OFFER deferred).
2. **Confirm = CLAIM-STANDS, identical to the static DAD.** The static join has **no positive confirm** — a joiner broadcasts CLAIM and **adopts after a guard window unless DENY'd** (silence = success; self-heals via its ongoing beacons). The mobile does the same: it adopts the OFFERed id on no-DENY (Slice 2b); the host **records it in `_mobile_reg` on hearing the CLAIM** (recording is the whole job — no reply); a lost CLAIM self-heals via the mobile's **periodic re-CLAIM** (the registration refresh). **No re-OFFER, no new confirm mechanism** — the mobile path stays structurally identical to the static one.
3. **Local-id pool** = `17..254` minus {this host's already-registered mobiles' local-ids, the host's own `_node_id`}. It MAY overlap a neighbour's global id — the Slice-1 mark disambiguates (§17 A3). No global-DAD needed.

## Fix 1 — J mobile-OFFER wire (`frame_codec.h` / `frame_codec.cpp`)
**`frame_codec.h`** — `j_offer_in` (line ~340) gains a field; `j_out` (line ~358) gains one:
```cpp
struct j_offer_in { uint8_t leaf_id; bool gateway_capable; bool is_mobile; uint8_t responder_node_id;
                    uint32_t responder_key_hash32; uint8_t data_sf_bitmap;
                    uint8_t proposed_mobile_id = 0; };   // NEW: appended (9-B frame) iff is_mobile
// j_out (parse superset): add
    uint8_t proposed_mobile_id = 0;                       // NEW: valid iff opcode==OFFER && is_mobile (9-B)
```
**`frame_codec.cpp` `pack_j_offer`** (~636) — append the byte when `is_mobile` (8→9 B):
```cpp
    // … existing 8-B body …
    if (in.is_mobile) w.u8(in.proposed_mobile_id);        // §mobile 2a: 9-B mobile OFFER
    return w.ok() ? w.size() : 0;
```
(the `out.size() < 8` guard becomes `< (in.is_mobile ? 9 : 8)`.)

**`frame_codec.cpp` `parse_j`** — in the OFFER case (~691-696), branch on the already-parsed `is_mobile`:
```cpp
    case static_cast<uint8_t>(j_opcode::offer): {
        const size_t need = o.is_mobile ? 9 : 8;          // §mobile 2a: mobile OFFER is 9 B
        if (frame.size() != need) return std::nullopt;    // keep exact-length per opcode
        o.responder_node_id    = frame[2];
        o.responder_key_hash32 = /* LE u32 at 3..6 as today */;
        o.data_sf_bitmap       = frame[7];
        if (o.is_mobile) o.proposed_mobile_id = frame[8]; // NEW
        break;
    }
```
**No change to DISCOVER/CLAIM/DENY parse** (their exact-length checks stand).

## Fix 2 — host state: `_mobile_reg` + free-id picker (`node.h` / `node_join.cpp`)
**`lib/core/protocol_constants.h`** — add `constexpr uint8_t cap_host_mobiles = 16;` (per-leaf host capacity).

**`node.h` `LayerRuntime`** (~line 1270, before the struct closes) — the host's registry:
```cpp
struct HostMobileEntry { uint32_t key_hash32; uint8_t mobile_local_id; uint16_t epoch; uint64_t last_heard_ms; };
HostMobileEntry _mobile_reg[protocol::cap_host_mobiles];
uint8_t         _mobile_reg_n = 0;
```
**`node_join.cpp`** — a helper (near `join_choose_candidate_id`, :84):
```cpp
// §mobile 2a: pick a free LOCAL id (17..254) for a mobile, distinct across THIS host's mobiles + not our own id.
// Returns 0 if the pool is full. If key_hash is already registered, returns its existing id (idempotent re-DISCOVER).
uint8_t Node::find_free_mobile_id(uint32_t key_hash32) {
    for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == key_hash32) return _active->_mobile_reg[i].mobile_local_id;
    for (int id = protocol::normal_node_id_min; id <= 254; ++id) {
        if (static_cast<uint8_t>(id) == _node_id) continue;
        bool taken = false;
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].mobile_local_id == id) { taken = true; break; }
        if (!taken) return static_cast<uint8_t>(id);
    }
    return 0;   // pool full
}
```

## Fix 3 — `handle_j` (`node_join.cpp`) — DISCOVER handler + CLAIM extension + scoped leaf-exemption
**Leaf-exemption (line 189)** — bypass ONLY for a mobile DISCOVER:
```cpp
    const bool mobile_discover = (j.opcode == static_cast<uint8_t>(j_opcode::discover)) && j.is_mobile;
    if (!mobile_discover && j.leaf_id != _cfg.leaf_id) return;   // static frames + non-mobile DISCOVER: unchanged
```

**DISCOVER handler** — add BEFORE the `return;` at line ~242 (the "deferred" fall-through):
```cpp
    if (j.opcode == static_cast<uint8_t>(j_opcode::discover)) {
        if (!j.is_mobile) return;                                 // static never DISCOVERs → ignore
        if (!_cfg.host_mobiles) return;                           // NEW cfg flag, default TRUE (B3): willing host
        const uint8_t local = find_free_mobile_id(j.key_hash32);
        if (local == 0) return;                                   // pool full → stay silent (mobile picks another host)
        j_offer_in off{}; off.leaf_id = _cfg.leaf_id; off.gateway_capable = false; off.is_mobile = true;
        off.responder_node_id = _node_id; off.responder_key_hash32 = _key_hash32;
        off.data_sf_bitmap = /* this leaf's allowed-data-SF bitmap */; off.proposed_mobile_id = local;
        uint8_t buf[9]; const size_t n = pack_j_offer(off, std::span<uint8_t>(buf, sizeof buf));
        if (n) { /* SNR-backoff + suppress-after-K is a 2b/host-tuning knob; for 2a emit directly */
                 tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0); }
        return;
    }
```

**CLAIM extension** — a mobile branch at the TOP of the existing CLAIM case (line ~195), before any static logic:
```cpp
    if (j.opcode == static_cast<uint8_t>(j_opcode::claim) && j.is_mobile) {
        // CLAIM-STANDS (identical to static DAD): record / refresh this mobile — recording is the whole job, NO reply.
        int slot = -1;
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].key_hash32 == j.key_hash32) { slot = i; break; }
        if (slot < 0 && _active->_mobile_reg_n < protocol::cap_host_mobiles) slot = _active->_mobile_reg_n++;
        if (slot < 0) return;                                     // full → drop (mobile re-DISCOVERs elsewhere)
        _active->_mobile_reg[slot] = { j.key_hash32, j.proposed_node_id, j.claim_epoch, _hal.millis() };
        // (The mobile adopts on no-DENY — Slice 2b; a lost CLAIM self-heals via the mobile's periodic re-CLAIM.)
        return;                                                   // ← do NOT fall into the static DAD tie-break
    }
    // … existing static CLAIM logic unchanged below …
```
(`j.claim_epoch` rides the CLAIM already — it becomes the mobile's registration epoch, §17 C1.)

**New cfg flag:** `NodeConfig.host_mobiles` (bool, **default true**) — so a node can opt OUT of hosting mobiles (B3). Add to config + NV alongside `is_mobile`; a mobile node itself never hosts (guard `if (_cfg.is_mobile) return;` in the DISCOVER handler is fine too).

## Tests — `test/test_frame_codec.cpp` + a node-level test (`test/test_node_r3.cpp` or a new `test_mobile.cpp`)
**Codec (`test_frame_codec.cpp`):**
```cpp
TEST_CASE("J mobile OFFER — 9-B round-trip (Slice 2a); normal OFFER stays 8-B") {
    j_offer_in m{}; m.leaf_id=4; m.is_mobile=true; m.responder_node_id=7; m.responder_key_hash32=0xABCD1234;
    m.data_sf_bitmap=0x06; m.proposed_mobile_id=33;
    uint8_t buf[16]; size_t n = pack_j_offer(m, buf); CHECK(n == 9);
    auto o = parse_j({buf, n}); CHECK(o.has_value());
    if (o) { CHECK(o->opcode == (uint8_t)j_opcode::offer); CHECK(o->is_mobile); CHECK(o->proposed_mobile_id == 33);
             CHECK(o->responder_node_id == 7); }
    j_offer_in s{}; s.leaf_id=4; s.is_mobile=false; s.responder_node_id=7; s.responder_key_hash32=0xABCD1234; s.data_sf_bitmap=0x06;
    n = pack_j_offer(s, buf); CHECK(n == 8);                 // ← static OFFER unchanged
    auto o2 = parse_j({buf, n}); CHECK(o2.has_value());
    if (o2) { CHECK_FALSE(o2->is_mobile); }
}
```
**Node-level (registration + filtering regression):**
- Feed a host a **mobile DISCOVER** (is_mobile, a *foreign* leaf_id) → the host emits a **mobile OFFER** with a free `proposed_mobile_id` in 17..254 (leaf-exempt worked).
- Feed a **non-mobile DISCOVER on a foreign leaf** → **no OFFER, no state change** (leaf filter still applies).
- Feed a **mobile CLAIM** → `_mobile_reg` gains the entry (key_hash → local_id, epoch), **no reply emitted** (claim-stands); feed it twice → **idempotent** (same slot, refreshed `last_heard`).
- **★ Static regression:** a **static CLAIM** (is_mobile=0) that conflicts → still yields a **DENY** exactly as before; a static CLAIM that's clean → still learns the binding; **`_mobile_reg` untouched**. (Reuse/extend the existing DAD test.)

## Gate
- `pio test -e native` → all green **incl. the static-DAD regression** (prove no behaviour drift). Run `./.pio/build/native/program` for the real doctest count.
- **s18 byte-identical** — the sim-level "static unaffected" tripwire (no mobiles in s18 → the mobile code must be fully dormant). **DO NOT cite a memorized count** — establish the baseline FRESH each time (the sim + lib evolve): (1) rebuild `lus` (`cmake --build ~/lora-universal-simulator/build --target lus`), (2) on the code *without* this slice, `lus -e meshroute simulation/s18_meshroute.json /tmp/s18_base.ndjson && md5sum /tmp/s18_base.ndjson`, (3) with the slice, the md5 MUST be unchanged. **Current pre-2a baseline (2026-07-08, current lib/core incl. Slice 1 + sim): `md5 3ac88d40e00d2605ff66659f696d52bf`, 253088 events, 0 failures.** (s18 does NOT opt into the metal RX-window slop, so that work doesn't affect it.) *Note: s18 nodes are provisioned (no DAD), so s18 proves mobile-dormancy but NOT static-DAD-unchanged — the native CLAIM/DENY tests are that proof.*
- 4 boards compile (`xiao_sx1262`/`heltec_v3`/`xiao_esp32s3`/`gateway`).

## Sites
`frame_codec.h`(`j_offer_in`~340, `j_out`~358) · `frame_codec.cpp`(`pack_j_offer`~636, `parse_j` OFFER case ~691) · `protocol_constants.h`(`cap_host_mobiles`) · `node.h`(`LayerRuntime` ~1270: `_mobile_reg`/`_mobile_reg_n`; `NodeConfig`: `host_mobiles`) · `node_join.cpp`(leaf-exempt :189, `find_free_mobile_id` ~:84, DISCOVER handler + CLAIM mobile-branch in `handle_j` ~184-242) · config/NV plumbing for `host_mobiles` · tests. **No routing / MAC / beacon change this slice.**
