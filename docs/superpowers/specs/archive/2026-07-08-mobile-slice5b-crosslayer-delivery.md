<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 5b: layer-aware cross-layer delivery — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 5b of mobile v1** (design §11 / E1; builds on 4a/4b/5a). The user commits; I quality-gate. **Makes a DM reach a mobile that CROSSED layers.** After 5a a mobile's home may be on a different layer than the sender; the mobile's home LAYER must be carried end-to-end and the cross-layer bridge must resolve the mobile there. **Honest scope note:** this is *not* free reuse — beyond a sender-side cache/send (small), the **cross-layer bridge needs mobile-aware resolution** (Fix 3, the substantive part). E1 is the root: today `MOBILE_H_ANSWER.target_layer` = the *answerer's* layer, and the cross-layer bridge resolves `dst_hash` on the target leaf via `id_bind` only — a mobile isn't there.

## ★★ Static-safety
- Every fork is gated on a **mobile dst_hash** (a `_mobile_home_cache` hit) or `_cfg.is_mobile`; a static/gateway node with no mobile in play is unchanged.
- The `target_layer` field on `MOBILE_H_ANSWER` already exists (no growth); the breadcrumb grows +1 byte (mobile-only TYPE). `MobileHomeBinding`/`HostMobileEntry` gain internal-only layer fields.
- **Gate:** native + **s18 byte-identical** (`3ac88d40…`) + existing cross-layer (s09/s15) **unchanged** (no mobiles → no mobile dst_hash → the bridge's id_bind path runs as today).

## Fix 1 — `_mobile_home_cache` stores the home's LAYER (`node.h` + `node_hashlocate.cpp`)
- **`MobileHomeBinding`** — add `uint8_t home_layer;`.
- **`on_mobile_hash_bind_response`** (~:671) — the inner already carries `target_layer` (`hash_bind_inner`); pass it through: `mobile_home_set(hb->key_hash32, hb->node_id, hb->epoch, hb->target_layer);`.
- **`mobile_home_set`** (~:213) — store `home_layer` (freshest-wins already governs which binding survives). `mobile_home_find` returns it (out-param or the struct).

## Fix 2 — the sender routes cross-layer on a layer mismatch (`node_hashlocate.cpp` the mobile-home send path, ~:795-870 `drain_parked_sends` / `send_by_hash`)
Where a mobile-home cache hit turns into a DM to the home:
```cpp
    const int home = mobile_home_find(M, &home_layer);
    if (home >= 0) {
        if (home_layer != active_layer_id() && home_layer != 0)
            send_cross_layer(static_cast<uint8_t>(home), M, home_layer, body, len, flags);   // §5b: reach the home on ITS layer via a gateway (reuse; picks the gw via select_gateway_for_leaf)
        else
            do_send(static_cast<uint8_t>(home), body, len, flags, crypt, /*override_dst_hash=*/M);  // same-layer (4a path)
        return;
    }
```
(`send_cross_layer` node_mac.cpp:257 already builds the layer-path `[our_layer, home_layer]` + `dst_hash=M` and picks a gateway. Same-layer stays the 4a path — s21/s07 unchanged.)

## Fix 3 — the cross-layer bridge resolves a MOBILE on the target leaf (`node_mac_rx.cpp` `bridge_cross_layer` ~:831 + `drain_xl_handoffs_for_leaf` ~:889) — THE substantive part
Today the bridge resolves `dst_key_hash32` on the target leaf via `id_on_leaf_by_hash` (id_bind only). A mobile M isn't in id_bind — it resolves via the home's **proxy**. Add a mobile path to the target-leaf resolution:
1. When resolving `dst_hash=M` on the target leaf and `id_on_leaf_by_hash` misses, **check `_mobile_home_cache[M]`** (on that leaf's runtime) → if present, the target is the **home_id** (the home last-mile-forwards). Deliver the bridged DM to `home_id` (id-addressed on the target leaf), keeping the inner `dst_hash=M`.
2. On a miss, the existing **defer + H-flood on the target leaf** already fires; the home answers a `MOBILE_H_ANSWER` → the bridging node caches `M→home_id` (Fix 1) → the drain (`drain_xl_handoffs_for_leaf`) re-checks the cache and delivers to `home_id`. **⚠ verify the drain's re-resolution consults `_mobile_home_cache`, not only `id_bind`.**
3. The home (on the target leaf) receives the DM `dst=home_id, dst_hash=M` → its existing `do_post_ack` last-mile fork (3a) forwards to the mobile. **No new last-mile code** — only the bridge's *resolve-to-home* step is new.

## Fix 4 — the breadcrumb + redirect carry the new home's LAYER (`frame_codec` + `node.h` + `node_mobile.cpp` + `node_hashlocate.cpp`)
So a stale OLD-layer home redirects with the *right* layer (else 4b's redirect points a cross-layer sender at the wrong leaf):
- **Breadcrumb body** (4b) grows to `[new_home_id][new_epoch][new_home_layer]` (3 B). The mobile sets `new_home_layer = _my_mobile_reg.home_leaf_id` (5a-adopted).
- **`HostMobileEntry`** — add `uint8_t redirect_home_layer;`; the do_post_ack breadcrumb case records it.
- **The proxy hook** (~:509, the 4b redirect branch) — when redirected, answer `target_layer = redirect_home_layer` (not `_cfg.leaf_id`): the redirect `MOBILE_H_ANSWER` then carries the new home's actual layer.

## Tests
- **Cache-layer:** a `MOBILE_H_ANSWER (M→home, layer=B)` → `mobile_home_find(M)` yields home + `home_layer==B`.
- **Sender cross-layer:** a sender on layer A with `_mobile_home_cache[M→home, B]` → `send_cross_layer(...)` is called (a gateway leg to B), not a same-layer DM; A==B → the 4a same-layer path.
- **Bridge resolve-to-home:** a gateway bridging a `dst_hash=M` DM to leaf B, with `_mobile_home_cache[M→home]` on B → delivers to `home_id` (id-addressed), inner `dst_hash=M` intact; miss → defers + H-floods (existing).
- **Redirect layer:** a breadcrumb `[H2, epoch, layerB]` → `redirect_home_layer==B`; the redirect `MOBILE_H_ANSWER` carries `target_layer==B`.
- **★ Static regression:** a cross-layer DM to a static hash (no `_mobile_home_cache` entry) bridges exactly as today (s09/s15); s18.

## Gate
- `pio test -e native` green (Fix 1-4 + static cross-layer regression).
- **s18 byte-identical** (`3ac88d40…`); **s09/s15 cross-layer unchanged**.
- **Sim (a 2-layer + gateway + mobile-on-far-layer scenario, pairs with 5a):** a sender on layer A `send_hash`es a mobile now on layer B (behind a gateway) → delivered (`mobile_lastmile_fwd` on B + `msg_recv` at the mobile). Best-effort acceptable on the redirect edge.
- 4 boards compile.

## Sites
`node.h`(`MobileHomeBinding.home_layer`; `HostMobileEntry.redirect_home_layer`; `mobile_home_find` layer out-param) · `node_hashlocate.cpp`(`mobile_home_set`/`find` layer; `on_mobile_hash_bind_response` pass target_layer; the mobile-home send cross-layer fork; the proxy redirect `target_layer`) · `node_mac_rx.cpp`(`bridge_cross_layer` + `drain_xl_handoffs_for_leaf` mobile-home resolve-to-home) · `frame_codec.*`(breadcrumb body +layer) · `node_mobile.cpp`(breadcrumb sets new_home_layer) · tests. **Depends on 5a (the mobile actually on another layer). Fix 3 is the real work — the rest is small.**
