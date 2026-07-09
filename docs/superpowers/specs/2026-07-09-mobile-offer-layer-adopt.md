<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — adopt the host's leaf + sf_list from the OFFER — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-09). **Bug fix** found on the bench: a registered mobile keeps its OWN `leaf` / `sf_list` instead of the home's. The user commits; I quality-gate.

## The bug
The J-OFFER already carries the host's `leaf_id` **and** `data_sf_bitmap` (its sf_list) — `frame_codec.h:340`, packed at `node_join.cpp:283`. But `OfferCand` (`node.h:1251`) stores only `responder_id / responder_hash / proposed_local_id / snr_db`, and the capture at `node_join.cpp:295` **discards `j.leaf_id` + `j.data_sf_bitmap`**. So at adopt (`mobile_claim_guard_fire`) the mobile has no host layer to adopt and `phy = scan_phy(0)` falls back to its OWN `layers[0]` (leaf/sf_list from the manual `mobile register`, or defaults). Result on the bench: mobile `leaf=0` / `sf_list=7` while home 222 is `leaf=4` / `sf_list=6,7`.

**Why it matters:** last-mile DATA from the home negotiates a data-SF from *its* list (e.g. SF6) — a mobile whose list is SF7-only may not receive it; and the leaf mismatch (byte-0 wire filter) is the same root. The design already intends this ("adopts the offer's leaf_id as its layer") — the fields are just dropped.

## ★★ Static-safety
- Every change is on the **mobile adopt path** (`_cfg.is_mobile`, `mobile_claim_guard_fire`, the mobile OFFER-collector). Static nodes never run it.
- **No wire change** — the OFFER already carries `leaf_id` + `data_sf_bitmap`; we just stop discarding them. So **s18 stays byte-identical** (`3ac88d40…`).
- `adopt_mobile_phy` gains a defaulted param → existing callers unchanged.

## Fix 1 — `OfferCand` carries the host layer (`node.h:1251`)
```cpp
struct OfferCand { uint8_t responder_id; uint32_t responder_hash; uint8_t proposed_local_id; float snr_db;
                   uint8_t leaf_id; uint8_t data_sf_bitmap; };   // §mobile: the HOST's layer (from the OFFER) — adopted on registration
```

## Fix 2 — stop discarding them (`node_join.cpp:295`)
```cpp
_mobile_offers[_mobile_offers_n++] = { j.responder_node_id, j.responder_key_hash32,
                                       j.proposed_mobile_id, meta.snr_db,
                                       j.leaf_id, j.data_sf_bitmap };   // §mobile: keep the host's leaf + sf_list to adopt on CLAIM
```

## Fix 3 — split the radio retune from the config adopt (`node.h` decl + `node.cpp:397`)
The config (leaf + sf_list) must be adopted **always**; the radio retune stays multi-PHY-only (single-PHY is already tuned, and retuning arms a spurious blind window). Add a defaulted param:
- **Decl** (`node.h:857`): `void adopt_mobile_phy(const LayerConfig& phy, bool retune_radio = true);`
- **Def** (`node.cpp:397`): wrap ONLY the four `_hal.set_rx_*` calls in `if (retune_radio) { … }`. The `_cfg.*` writes (layers[0], routing_sf, allowed_sf_bitmap, leaf_id) + `_routing_snr_floor_q4` stay unconditional.

## Fix 4 — adopt the host's leaf + sf_list on CLAIM (`node_mobile.cpp:79-85`)
Replace the `phy`/reg/adopt lines:
```cpp
LayerConfig phy = scan_phy(_mobile_scan_idx);          // BY VALUE (mutated below) — freq/bw/routing_sf from the scanned PHY (already tuned here)
phy.layer_id          = o.leaf_id;                     // §mobile: adopt the HOST's leaf (from the OFFER), NOT our own (scan_phy(0) = self)
phy.allowed_sf_bitmap = o.data_sf_bitmap;              // §mobile: adopt the HOST's sf_list (from the OFFER)
set_identity(o.proposed_local_id, _key_hash32);
_joined = true;
_my_mobile_reg = { true, o.responder_id, o.proposed_local_id, o.responder_hash,
                   o.leaf_id,                          // §mobile: the HOST's leaf (was phy.layer_id = self on single-PHY)
                   _my_mobile_reg.epoch, _hal.now() };
adopt_mobile_phy(phy, /*retune_radio=*/scan_set_count() > 1);   // §mobile: config (leaf+sf_list) ALWAYS; radio retune only for a multi-PHY scan
```
(Everything else — breadcrumb, `mobile_adopted` emit, triggered beacon, re-CLAIM arm — is unchanged.)

## Notes / limits (call out, don't fix here)
- `data_sf_bitmap` is 1 byte = the host's `allowed_sf_bitmap & 0xFF`. SF6/SF7 live in the low byte, so the common case is exact; a mesh using **SF ≥ 8** wouldn't convey those bits — a pre-existing OFFER wire limit, out of scope.
- `o.leaf_id` is the leaf **nibble** (byte-0 filter), not the full `layer0_id` (e.g. 100). Correct for single-PHY / intra-mesh; the full layer_id isn't in the OFFER (irrelevant until cross-layer).
- The manual `mobile register`'s `allowed_sf_bitmap = (1u<<sf)` seed is now overridden on adopt, so it only affects the pre-adopt DISCOVER (which uses `routing_sf`, not the sf_list) — harmless.

## Tests (`test_dual_layer.cpp`)
- **★ Adopt inherits the host layer:** a mobile (seeded `leaf=0`, sf_list SF7-only) collects an OFFER with `leaf_id=4`, `data_sf_bitmap=`{SF6|SF7} → after `mobile_claim_guard_fire`: `_cfg.leaf_id==4`, `_cfg.allowed_sf_bitmap` has both SF6 and SF7, `_my_mobile_reg.home_leaf_id==4`.
- **Single-PHY = no radio retune:** with `scan_set_count()==1`, adopt still sets the config (leaf/sf_list) but does NOT call `set_rx_freq` (assert via a HAL spy / call-count), so no blind window.
- **★ Regression:** s07 + s21 still deliver (0 assertion failures — s21's `MobileM` push + `Home` last-mile). If the s21/s07 scenarios provision mobile and host with the same leaf/sf_list, the adopt is a no-op there — the unit test above is the real proof; the scenarios prove no regression.

## Gate
- `pio test -e native` green (+ the two new cases).
- **s18 byte-identical** (`3ac88d40…`); **s07 / s21 deliver, 0 failures**.
- 4 boards compile.

## Sites
`node.h`(`OfferCand` +2 fields; `adopt_mobile_phy` decl +param) · `node_join.cpp:295`(capture `j.leaf_id`/`j.data_sf_bitmap`) · `node.cpp:397`(`retune_radio` guard on the `set_rx_*` block) · `node_mobile.cpp:79-85`(adopt the OFFER's leaf+sf_list; `home_leaf_id=o.leaf_id`; `adopt_mobile_phy(phy, scan_set_count()>1)`) · `test_dual_layer.cpp`(2 cases). **No wire, no console, no NV.**
