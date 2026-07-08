<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 4a: the mobile-H-answer TYPE (epoch + freshest-proxy + repeat-robustness) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 4a of mobile v1** (design §17 C1 / §18; builds on 3a/3c). The user commits; I quality-gate. **One wire object closes three things at once:** (1) the §17-C1 **registration epoch** (freshest-proxy wins the old+new-home overlap), (2) the **repeat-send robustness** (the distinct TYPE *is* the "mobile proxy" signal — no `key_hash_of_id` guess), (3) it keeps the normal H-answer **byte-identical** (s18-safe).

## Why a new TYPE (the problem it retires)
Today the 3a proxy answers with the plain `DATA_TYPE_H_ANSWER` (claimed). The sender's `on_hash_bind_response` then (a) `id_bind_set(home, M)` — which pollutes id_bind and forces a hard-verify on repeats the soft-only proxy won't answer (the `send_hash_giveup` on s21 repeats), and (b) uses the `key_hash_of_id(home) != M` heuristic to populate `_mobile_home_cache` — which **fails when the sender can't see the home's beacon** (2+ hops). A distinct **`DATA_TYPE_MOBILE_H_ANSWER`** solves both: the sender knows it's a mobile proxy, so it caches `M→home` (with epoch) and **never touches id_bind**.

## ★★ Static-safety
- The normal `H_ANSWER`/`AUTHORITATIVE_H_ANSWER` frames + `hash_bind_inner` packing are **unchanged** → **s18 byte-identical** (`3ac88d40…`).
- `MOBILE_H_ANSWER` is emitted only by a node PROXYING a `_mobile_reg` mobile (s18 hosts none) and parsed only in a new dispatch case.
- **Fleet upgrades together** (no-mixed-firmware-per-leaf, established) — the 3c `key_hash_of_id` heuristic is REPLACED, not kept as a fallback.

## Fix 1 — `DATA_TYPE_MOBILE_H_ANSWER` + the epoch on the inner (`frame_codec.h/.cpp`)
- **`frame_codec.h`** DATA TYPE enum (~:423-432, codes 1,2,3,6,7 used): add **`DATA_TYPE_MOBILE_H_ANSWER = 8`**. (Only the CLAIMED proxy answer needs it — the proxy is always `authoritative=false`, so no authoritative-mobile variant.)
- **`hash_bind_inner`** (~:532) — add `uint8_t epoch = 0;` (matches the CLAIM's `claim_epoch` width — see §epoch note). The field is packed ONLY for the mobile TYPE.
- **`pack_hash_bind_inner`** (frame_codec.cpp ~:964) — add an overload/param: pack the normal 6 B, then `if (mobile) w.u8(in.epoch);` → 7 B for the mobile variant. Normal callers pass no epoch → 6 B unchanged.
- **`parse_hash_bind_inner`** — read the trailing epoch byte only when the caller says mobile (driven by the frame TYPE, below): `o.epoch = (len >= 7) ? r.u8() : 0;`.

**§epoch note:** the CLAIM carries `claim_epoch` as `uint8_t`; `_mobile_reg[i].epoch`/`_my_mobile_reg.epoch` are `uint16_t` but only the low byte is ever transmitted. Keep the wire epoch **`uint8_t`** (1 B) for consistency; freshest-wins uses a **wrap-aware compare** (`int8_t(a - b) > 0`) so 255→0 rollover after 256 re-registrations doesn't invert.

## Fix 2 — the proxy emits `MOBILE_H_ANSWER` + the epoch (`node_hashlocate.cpp`)
- The 3a proxy hook (`:451`, the `_mobile_reg` scan) currently sets `node_id = _node_id; authoritative = false;`. Also capture the epoch + a flag: `mobile_proxy = true; mobile_epoch = _active->_mobile_reg[i].epoch;`.
- **`send_hash_bind_response`** (~:561-585) — add params `bool mobile_proxy = false, uint8_t epoch = 0`; at the TYPE line (~:573):
  ```cpp
  item.type = mobile_proxy ? DATA_TYPE_MOBILE_H_ANSWER
            : authoritative ? DATA_TYPE_AUTHORITATIVE_H_ANSWER : DATA_TYPE_H_ANSWER;
  ```
  and pack the epoch into the inner when `mobile_proxy` (Fix 1). The `handle_h` callsite (~:525) passes `mobile_proxy`/`epoch` through.

## Fix 3 — the sender handles `MOBILE_H_ANSWER`: cache-only, freshest-wins, **NO id_bind** (`node_mac_rx.cpp` + `node_hashlocate.cpp`)
- **`do_post_ack`** (node_mac_rx.cpp ~:617) — add the dispatch case:
  ```cpp
  if (pa.type == DATA_TYPE_MOBILE_H_ANSWER) { on_mobile_hash_bind_response(pa.inner, pa.inner_len); become_free(); return; }
  ```
- **new `on_mobile_hash_bind_response`** (node_hashlocate.cpp, beside `on_hash_bind_response`) — parse the inner (incl. epoch) → **`mobile_home_set(hb.key_hash32, hb.node_id, hb.epoch)`** and **nothing else** (NO `id_bind_set` — the mobile-proxy must stay out of id_bind, or repeats hard-verify again). *(This is the whole repeat-robustness fix.)*
- **REMOVE the 3c heuristic** in `on_hash_bind_response` (~:635-638, the `key_hash_of_id(node_id) != M` block) — the TYPE now carries the signal; a plain `H_ANSWER` for a hash we don't own is no longer treated as a mobile proxy.

## Fix 4 — `_mobile_home_cache` carries the epoch, freshest-wins (`node.h` + `node_hashlocate.cpp`)
- **`MobileHomeBinding`** (node.h ~:1162) — add `uint8_t epoch;`.
- **`mobile_home_set(mhash, home, epoch)`** (node_hashlocate.cpp ~:213) — on an existing entry for `mhash`, overwrite **only if the incoming epoch is fresher** (`int8_t(epoch - existing.epoch) > 0`) OR the same home (refresh `last_seen_ms`); a stale (older-epoch) answer for a different home is IGNORED. New entry → insert.

## Tests
- **Codec:** `pack_hash_bind_inner(mobile, epoch=E)` → 7 B, `parse` yields `epoch==E`; non-mobile → 6 B, byte-identical to today.
- **Proxy:** a host with `_mobile_reg[{M→home, epoch=E}]`, fed an H-query for M → emits a `DATA_TYPE_MOBILE_H_ANSWER` whose inner resolves M→home with `epoch==E`.
- **Sender cache-only:** feeding a `MOBILE_H_ANSWER` (M→home, E) → `mobile_home_find(M)==home` AND **no id_bind entry for (home,M)** was created. A plain `H_ANSWER` → the old id_bind path, **no** `_mobile_home_cache` write (heuristic gone).
- **Freshest-wins:** `mobile_home_set(M, H1, 5)` then `(M, H2, 6)` → `find==H2`; then `(M, H3, 4)` (stale) → still `H2`; wrap: `(M, H1, 255)` then `(M, H2, 0)` → `H2`.
- **★ Static regression:** `H_ANSWER`/`AUTHORITATIVE_H_ANSWER` round-trip + `on_hash_bind_response` byte-identical; s18.

## Gate
- `pio test -e native` green (codec + proxy + cache-only + freshest-wins + static regression).
- **s18 fresh-md5 byte-identical** (`3ac88d40…`) — the normal H-answer is untouched.
- **★ s21 REPEAT delivery — the caveat retires:** all three `send_hash` sends (t=120k/210k/270k) now deliver; **`send_hash_giveup` → 0** (the first flood populates `_mobile_home_cache` via the TYPE; repeats hit it, no hard-verify).
- 4 boards compile.

## Sites
`frame_codec.h`(`DATA_TYPE_MOBILE_H_ANSWER=8`; `hash_bind_inner.epoch`) · `frame_codec.cpp`(`pack/parse_hash_bind_inner` epoch) · `node_hashlocate.cpp`(proxy hook :451 epoch+flag; `send_hash_bind_response` params+TYPE ~:573; new `on_mobile_hash_bind_response`; REMOVE heuristic ~:635; `mobile_home_set` freshest-wins ~:213) · `node_mac_rx.cpp`(do_post_ack MOBILE_H_ANSWER case ~:617) · `node.h`(`MobileHomeBinding.epoch`) · tests. **NO change to the normal H-answer, NO breadcrumb/redirect (that's 4b).**
