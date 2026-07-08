<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 3c: sender-side mobile-proxy resolution (unblock reachability) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 3c of mobile v1** (design §13/§17-C2/§18; the reachability BLOCKER found while gating 3a/3b). The user commits; I quality-gate. **This makes a DM sent to a mobile's stable hash M actually reach the mobile's home_node**, which then last-mile-forwards (3a). Without it, `send_hash <M>` resolves the proxy but the DM is **consumed by the home instead of forwarded**.

## The three blockers (all confirmed in code)
A DM to a mobile is addressed **by hash** (`send_hash M` / send-by-hash) — a mobile has a *local* id, so hash is the only external address (§18). The sender H-queries M; the mobile's home_node **proxies** (answers `M → home_id N`, CLAIMED, soft-only, `node_hashlocate.cpp:451`). Then:
- **A — hard-verify giveup:** `send_by_hash` re-queries with `hard=(id>=0)` to verify a claimed binding (`node_hashlocate.cpp:631-632`); the proxy answers only SOFT (`:451`) → the hard query is never answered → `send_hash_giveup` (`:817`).
- **B — can't cache the proxy binding:** `id_bind` is one-hash-per-node_id; N already owns its own AUTHORITATIVE hash, so a CLAIMED second hash (M) is **refused** (`node_hashlocate.cpp:73-80`, `addr_conflict_observed`). §17 said "id_bind caches hash→home_id sender-side (fine)" — it structurally **can't**.
- **C — wrong DST_HASH (the actual delivery blocker):** once resolved to N, the DM stamps `DST_HASH = key_hash_of_id(N)` = **N's own hash** (`node_mac.cpp:77-79` → `node_hashlocate.cpp:160`), not the queried M. So N sees `dst_hash == my key` → **consumes** the DM instead of last-mile-forwarding it. The DM must carry `DST_HASH = M`.

The fix is §17-C2: a **separate `mobile_home` cache** (id_bind can't hold it) + carrying the **queried hash M** onto the DM.

## Fix 1 — the `_mobile_home_cache` (node.h, in `LayerRuntime` beside `_id_bind`)
```cpp
struct MobileHomeBinding { uint32_t mobile_hash; uint8_t home_id; uint64_t last_seen_ms; };  // NO bijection: many mobiles → one home OK
MobileHomeBinding _mobile_home_cache[protocol::cap_mobile_home_cache];   // e.g. 16
uint8_t           _mobile_home_cache_n = 0;
```
`protocol_constants.h`: `cap_mobile_home_cache = 16;` and `mobile_home_cache_ttl_ms = 300000;` (5 min — §17-C2 "short, minutes").

## Fix 2 — helpers (`node_hashlocate.cpp`, mirror `id_bind_*`)
- `int Node::mobile_home_find(uint32_t mhash) const` — linear scan; return `home_id` or `-1` (skip TTL-expired vs `mobile_home_cache_ttl_ms`).
- `void Node::mobile_home_set(uint32_t mhash, uint8_t home)` — insert/update (refresh `last_seen_ms`); evict oldest if full. **SILENT — emit NO new event** (so s18 stays byte-identical even if it ever writes).
- `void Node::mobile_home_age_out()` — TTL drop; call alongside `id_bind_age_out()` (node.cpp:718).

## Fix 3 — populate on the proxy answer (`node_hashlocate.cpp` `on_hash_bind_response`, ~579-597)
Right after the existing `id_bind_set(...)` for the answer, detect the **mobile-proxy signature** — a CLAIMED answer `M → N` that id_bind REFUSED because N already holds a *different authoritative* hash:
```cpp
    // §mobile 3c: a CLAIMED answer M→N where N owns a DIFFERENT authoritative hash = a host PROXYing for a mobile.
    // id_bind can't hold it (one-hash-per-id) — cache it separately so send-by-hash can reach the home.
    if (!authoritative) {
        uint32_t nhash = 0;
        if (key_hash_of_id(hb->node_id, nhash) && nhash != hb->key_hash32)   // N has its own (different) authoritative hash
            mobile_home_set(hb->key_hash32, hb->node_id);                     // M → N (home)
    }
```
(`key_hash_of_id` returns only authoritative — exactly the "N owns a different hash" test. No wire change.)

## Fix 4 — use the cache in `send_by_hash` (`node_hashlocate.cpp` ~625-634), BEFORE the hard-verify escalation
```cpp
    int id = id_bind_find_by_hash(key_hash32, &conf);
    if (id >= 0 && conf == authoritative) { /* existing: send now */ }
    else {
        const int home = mobile_home_find(key_hash32);        // §mobile 3c: a cached mobile→home?
        if (home >= 0) {
            // Send to the home DIRECTLY, carrying the MOBILE's hash as dst_hash (so the home forwards, not consumes).
            // NO hard-verify (a mobile has no authoritative global binding — the proxy is claimed by design).
            do_send(static_cast<uint8_t>(home), body, body_len, flags, crypt, /*override_dst_hash=*/key_hash32);
            return; // resolved via the mobile-home cache
        }
        // else: existing park + emit_hash_query(hard = id>=0) path (first-contact still floods once → proxy answers SOFT → Fix 3 caches it)
    }
```
So the **first** send to a new mobile floods a SOFT H-query (proxy answers → Fix 3 caches M→N), and **subsequent** sends hit the cache. (If first-contact must also succeed without a warm cache, the parked-send resume path (Fix 5) already carries M → so even the first resolved send is correct.)

## Fix 5 — carry the queried hash M onto the DM (Blocker C — the delivery fix)
Thread an `override_dst_hash` through the send so the DM's `DST_HASH` is **M**, not `key_hash_of_id(N)`:
- `node.h` / `node_mac.cpp` — `Node::do_send(uint8_t dst, const uint8_t* body, uint8_t len, uint8_t flags, CryptIntent crypt, uint32_t override_dst_hash = 0)`.
- `enqueue_data` (node_mac.cpp:77-79) — when `override_dst_hash != 0`, use it directly:
  ```cpp
  uint32_t dh = 0;
  if (override_dst_hash) { dh = override_dst_hash; item.flags |= DATA_FLAG_DST_HASH; }
  else if (app_dm && key_hash_of_id(dst, dh) && /*fits*/) { item.flags |= DATA_FLAG_DST_HASH; }
  ```
- `drain_parked_sends` (node_hashlocate.cpp:~753) — pass the parked queried hash: `do_send(resolved_id, p.body, p.body_len, p.flags, p.crypt, /*override=*/p.key_hash32);` so **even the first, flood-resolved** send to a mobile carries `DST_HASH = M`.

## ★ Static-safety (CORRECTED — do NOT gate the cache read on `_mobile_reg_n`)
The **sender is not the host** (its `_mobile_reg_n == 0`), so a `_mobile_reg_n>0` gate would wrongly disable the cache. Instead s18 is byte-identical because the cache is **inert** there:
- **Never read in s18** — s18 sends by name/id, never `send_hash`; `send_by_hash` is the only reader.
- **Effectively never written** — populated only by Fix 3's proxy signature (a CLAIMED answer refused due to a node-id conflict); DAD'd s18 has unique ids, so this is rare, and the write is **SILENT** (no new event) so even a stray write can't change byte output.
- **`override_dst_hash` defaults 0** → `enqueue_data`/`do_send` behave exactly as today for every non-mobile send.
- **Gate:** native green + **s18 fresh-md5 byte-identical** (`3ac88d40e00d2605ff66659f696d52bf`, re-establish pre-slice).

## Tests
**Node-level (`test_mobile.cpp`):**
- `mobile_home_set(M, N)` then `mobile_home_find(M) == N`; a 2nd mobile M2→N coexists (no bijection); TTL expiry drops it.
- **Fix 3:** feed `on_hash_bind_response` a CLAIMED `M→N` where N holds a different authoritative hash → `mobile_home_find(M)==N`. A CLAIMED answer for a hash N *does* own → NOT cached.
- **Fix 5:** `do_send(N, …, override_dst_hash=M)` → the emitted DM has `DATA_FLAG_DST_HASH` and inner `dst_key_hash32 == M` (NOT N's hash). `override=0` → unchanged (`key_hash_of_id(N)`).
- **Fix 4:** with `_mobile_home_cache[{M→N}]`, `send_by_hash(M)` sends to `dst==N` with `dst_hash==M`, **no** `emit_hash_query`/park.
- **★ Static regression:** a node with no mobile-home entries + `override_dst_hash=0` — `send_by_hash` + `enqueue_data` byte-identical (reuse existing hashlocate tests).

**Sim — the reachability MILESTONE (the whole point):** on the **focused scenario** (static mesh + one **stationary** registered mobile + a static `send_hash` to it), assert a **`msg_recv` at the mobile** (and `mobile_lastmile_fwd` at its home). `send_hash_giveup` → 0 for that flow. *(Draft scenario provided separately; it needs its host-OFFER/registration tuned first — s07's 36-node mesh registers mobiles fine, so this is sim-config, not code.)*

## Gate
- `pio test -e native` green (cache + Fix 3/4/5 + static regression).
- **s18 fresh-md5 byte-identical** (`3ac88d40…`).
- Focused-scenario milestone: a DM to a stationary mobile **delivers** (`msg_recv` at the mobile).
- 4 boards compile.

## Sites
`node.h`(`LayerRuntime._mobile_home_cache[]`/`_n`; `do_send` `override_dst_hash` param; helper decls) · `protocol_constants.h`(`cap_mobile_home_cache`, `mobile_home_cache_ttl_ms`) · `node_hashlocate.cpp`(`mobile_home_find/set/age_out`; `on_hash_bind_response` Fix 3 ~585; `send_by_hash` Fix 4 ~625; `drain_parked_sends` Fix 5 ~753) · `node_mac.cpp`(`do_send` + `enqueue_data` `override_dst_hash` ~77) · `node.cpp`(`mobile_home_age_out()` in the aging tick ~718) · tests. **NO wire change. NO epoch (Slice 4). NO team (Slice 6).**
