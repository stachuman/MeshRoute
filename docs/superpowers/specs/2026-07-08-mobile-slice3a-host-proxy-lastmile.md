<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 3a: host H-query proxy + last-mile forward — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 3a of mobile v1** (design §13/§17; builds on 2a/2b + Slice-1 marks). The user commits; I quality-gate. **HOST side of reachability:** a host (1) answers H-queries for the mobiles in its `_mobile_reg`, and (2) forwards a DM addressed to it (`dst_hash`=a registered mobile) down to that mobile with the `addr_len=1` mark. **3b (mobile receive + `origin=home_id`) follows; the redirect + freshness EPOCH are Slice 4.**

## ★★ NON-NEGOTIABLE: the static mesh must be UNAFFECTED
Every new path is entered only when this node actually hosts mobiles (`_active->_mobile_reg_n > 0`); otherwise the loops don't execute and the code is byte-identical:
- **H-query proxy:** the `_mobile_reg` scan runs only if `_mobile_reg_n > 0` AND the normal `id_bind` lookup missed. **No H-answer wire change** (NO epoch here — the answer frame stays the exact size/bytes).
- **Last-mile fork:** the `_mobile_reg` match runs only if `_mobile_reg_n > 0`; a non-host falls straight through to the existing bridge/misdelivery path.
- **TxItem gains `addr_len`/`mobile_src` fields — default 0** → `pack_rts` emits byte-3/byte-5 exactly as today for every existing TxItem.
- **Gate:** native green + **s18 fresh-md5 byte-identical** (`3ac88d40e00d2605ff66659f696d52bf`, re-establish pre-slice — s18 hosts no mobiles → `_mobile_reg_n==0` everywhere → identical).

## Fix 1 — `TxItem` mobile fields + RTS packing
**`node.h`** `struct TxItem` (~line 261) — add:
```cpp
    uint8_t addr_len   = 0;      // §mobile 3a: 0=normal, 1=mobile-next (this DM's next-hop is a mobile local-id)
    bool    mobile_src = false;  // §mobile 3a: originator is a mobile (set in Slice 3b outbound; 0 for a host forward)
```
**`node_mac.cpp`** where a `TxItem` becomes an RTS (`rts_in rin{}` ~587, before `pack_rts` ~597) — carry the fields:
```cpp
    rin.addr_len   = pt.addr_len;      // §mobile 3a: 1 on a last-mile forward to a mobile
    rin.mobile_src = pt.mobile_src;    // §mobile: 0 for a host forward (set true only by a mobile's own outbound, 3b)
```
(Slice 1 already added `rts_in.addr_len`/`mobile_src`; this just wires the TxItem through. For a normal TxItem both are 0 → identical wire.)

## Fix 2 — H-query proxy (`node_hashlocate.cpp` `handle_h`)
After the existing resolution attempt (own-hash then `id_bind_find_by_hash`, ~line 442-446), before the answer/forward decision (~449), add the **`_mobile_reg` proxy** — only when the id is still unresolved and this is a soft query:
```cpp
    // §mobile 3a: I HOST this mobile → answer with MY id (home_id), as a proxy (claimed, not owner-authoritative)
    if (node_id < 0 && !h.hard && _active->_mobile_reg_n > 0) {
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
            if (_active->_mobile_reg[i].key_hash32 == h.key_hash32) {
                node_id = _node_id; authoritative = false;   // the querier caches hash→home_id (claimed)
                break;
            }
    }
```
`send_hash_bind_response(h.origin, _cfg.leaf_id, node_id, h.key_hash32, /*authoritative=*/false)` then routes the answer home exactly as an `id_bind` hit does — the querier's `id_bind` caches `mobile_hash → home_id`. **No frame change.** *(Freshness across two hosts during a re-registration overlap = the epoch, Slice 4.)*

## Fix 3 — last-mile forward (`node_mac_rx.cpp`, the do_post_ack fork ~586)
The node received a DM addressed to it (`dst==_node_id`); the inner `dst_hash != _key_hash32`. **Before** the existing "not-for-me → bridge/misdelivery" branch (~585-586), add the **mobile redirect** — mirror `bridge_cross_layer` (node_mac_rx.cpp:786) / `l2c_enqueue_forward` (node_join.cpp:382):
```cpp
    if (ui && ui->has_dst_hash && ui->dst_key_hash32 != _key_hash32 && _active->_mobile_reg_n > 0) {
        for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i) {
            if (_active->_mobile_reg[i].key_hash32 == ui->dst_key_hash32) {
                TxItem it{};
                it.origin     = pa.origin;                              // PRESERVE the real originator (anti-spam)
                it.dst        = _active->_mobile_reg[i].mobile_local_id; // the mobile's local id
                it.addr_len   = 1;                                      // §mobile: next is a mobile local-id (Fix 1)
                it.mobile_src = false;                                  // a host forward, not a mobile origination
                it.is_forward = true;
                it.ctr = pa.ctr; it.ctr_lo = pa.ctr_lo4;                // carry the flight id
                it.flags = pa.flags; it.type = pa.type;
                it.inner_len = pa.inner_len;
                for (uint8_t j = 0; j < pa.inner_len; ++j) it.inner[j] = pa.inner[j];  // inner rides VERBATIM (E2E-sealed)
                // NOTE: do NOT rt_find(it.dst) here — the local-id can collide a global id. The next-hop is a DIRECT
                // send chosen in issue_send from addr_len==1 (Fix 4); the mobile is a 1-hop neighbour (the registrar).
                _active->_tx_queue[_active->_tx_queue_n++] = it;
                MR_EMIT("mobile_lastmile_fwd", EF_I("local", it.dst), EF_I("origin", it.origin));
                return;                                                 // handled — do NOT fall into the bridge/misdelivery path
            }
        }
    }
```
Notes:
- **The inner is copied verbatim** — the host does NOT decrypt (E2E-sealed to the mobile's key); it only re-addresses. Identical to how the cross-layer bridge forwards.
- **`it.dst` = the mobile's local id** and **`addr_len=1`** → the RTS that goes out (Fix 1) carries the mobile mark, so the mobile accepts it and a colliding static id does not (that acceptance logic is **Slice 3b**).
- If `_tx_queue` is full, drop (best-effort) — match the existing bridge behaviour.

## Fix 4 — last-mile next-hop must be DIRECT (the local-id-collision fix)
The last-mile forward's `dst` is a mobile **local id** that MAY equal a global node's id. `pick_next_cascade_hop`/`rt_find` (node_mac.cpp:525) would route by that value → possibly to the **global** node, not the mobile. A mobile is a **1-hop neighbour** of its host, so the last mile is a **direct** send. In `issue_send`, before the `pick_next_cascade_hop` call (~525), add:
```cpp
    uint8_t first;
    if (pt.addr_len == 1) { first = pt.dst; }        // §mobile 3a: mobile-next → DIRECT (skip route selection; the local-id must NOT be rt_find'd — it can collide a global id)
    else                    first = pick_next_cascade_hop(pt);
```
This is the *only* way a mobile-addressed frame gets a next-hop (the local-id is never in the global rt — see the paired 3b fix: `handle_rts` skips `learn_direct_neighbor` for a `mobile_src` RTS, so the mobile's outbound src can't pollute the rt). **Static-safe:** `addr_len` is 0 for every non-mobile TxItem → `pick_next_cascade_hop` runs exactly as today.

## Tests
**Node-level (`test/test_mobile.cpp` or `test_node_r3.cpp`):**
- **Proxy:** a host with a `_mobile_reg` entry for hash H, fed an **H-query for H** (soft) → it emits an H-answer resolving H → **its own `_node_id`**, confidence **claimed** (not authoritative). A host with `_mobile_reg_n==0` → no proxy (existing behaviour).
- **Last-mile:** a host with `_mobile_reg[{hash H → local 17}]`, fed a **DM `dst=_node_id`, inner `dst_hash=H`** → a forward `TxItem` is enqueued with `dst==17`, `addr_len==1`, `origin==the DM's origin`, inner copied verbatim; **returns before** the bridge path. `_mobile_reg_n==0` → no fork (existing bridge/misdelivery unchanged).
- **★ Static regression:** a non-host node (`_mobile_reg_n==0`) — H-query resolution + the DM not-for-me path are **unchanged** (reuse the existing hashlocate/bridge tests; assert no new branch taken).

## Gate
- `pio test -e native` green (incl. proxy + last-mile + static-regression) — via `./.pio/build/native/program`.
- **s18 fresh-md5 byte-identical** (`3ac88d40…`; rebuild `lus`, re-establish pre-slice) — the static tripwire (s18 has no hosted mobiles → both forks dormant).
- s07 runs clean (0 assertion failures); `mobile_lastmile_fwd` events appear once senders address the mobiles (full delivery is proven after 3b).
- 4 boards compile.

## Sites
`node.h`(`TxItem`: `addr_len`,`mobile_src`) · `node_mac.cpp`(RTS pack from TxItem ~587-597; **`issue_send` direct-next-hop for addr_len==1 ~525 — Fix 4**) · `node_hashlocate.cpp`(`handle_h` proxy hook ~446) · `node_mac_rx.cpp`(last-mile fork ~585-586) · tests. **NO origin change, NO receive-side mark handling (that's 3b), NO H-answer wire/epoch (that's Slice 4).** ⚠ The paired collision fix — `handle_rts` SKIPS `learn_direct_neighbor` when `mobile_src` (keeps the local-id out of the global rt so a mobile's E2E-ACK to a colliding global id routes correctly) — is **Slice 3b, §17 A1**; 3a's direct-send (Fix 4) + that 3b skip are both required for the local-id-collision case.
