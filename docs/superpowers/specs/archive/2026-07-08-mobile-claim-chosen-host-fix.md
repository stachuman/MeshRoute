<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — CLAIM chosen-host fix (only the chosen host records) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). A **correctness fix to 2a** found via the focused milestone scenario (`s21`). The user commits; I quality-gate. **No wire change** — reuses a dead byte.

## The bug
`node_join.cpp:217-227` — the mobile-CLAIM handler records the mobile in `_mobile_reg` **unconditionally**: *any* node that hears the CLAIM flood mints itself a host. So a mobile that chose home **19** is also "hosted" by every node in flood range (e.g. relay **18**), all of which then proxy for its hash. A sender's H-query resolves to whichever false host answers first (`s21`: resolved to 18, ~1 km from the mobile, not its real home 19) → the last mile lands on the wrong node and fails. Also degrades s07 (every CLAIM mints false hosts).

**Root cause:** the CLAIM identifies the *local id* (`proposed_node_id`) but not *which host* the mobile accepted — and the local id can't disambiguate (two hosts both offer 17 from independent pools). The receiver has no way to know the CLAIM was meant for it.

## The fix — carry the chosen host id, record only if it's us. NO wire change.
The mobile already knows its chosen host (`OfferCand.responder_id`, `node_mobile.cpp:54/64`). Put it on the CLAIM in the **`nonce` byte** — which is **dead on the mobile path** (`node_mobile.cpp:58` sets it to `rand_range(0,256)`; the handler `return`s at :227 before any nonce/tiebreak use). Same 11-B frame, same byte positions; static CLAIMs byte-identical.

**1. `frame_codec.h` — alias a field onto the nonce byte** (`j_claim_in` ~:344, `j_out` ~:365):
```cpp
struct j_claim_in { ... uint8_t claim_epoch; uint8_t nonce;
                    uint8_t chosen_host_id = 0; };   // §mobile: reuses the NONCE byte iff is_mobile (nonce is dead on the mobile path)
// j_out: add `uint8_t chosen_host_id = 0;` alongside nonce
```
**2. `frame_codec.cpp` `pack_j_claim`** — the last body byte (byte 10, currently `w.u8(in.nonce)`):
```cpp
w.u8(in.is_mobile ? in.chosen_host_id : in.nonce);   // §mobile: a mobile CLAIM carries the chosen host id here, not a nonce
```
**3. `frame_codec.cpp` `parse_j`** (CLAIM case) — read that byte into BOTH (the handler picks by `is_mobile`):
```cpp
const uint8_t b = r.u8();  o.nonce = b;  o.chosen_host_id = b;   // same byte; static reads nonce, mobile reads chosen_host_id
```
**4. `node_mobile.cpp:58`** — set the chosen host instead of a random nonce:
```cpp
c.chosen_host_id = o.responder_id;   // §mobile: address the CLAIM at the host we picked (was: c.nonce = rand)
```
**5. `node_join.cpp:217`** — the guard, first line inside the `if (j.is_mobile)` mobile-CLAIM branch:
```cpp
if (j.is_mobile) {
    if (j.chosen_host_id != _node_id) return;   // §mobile: only the host the mobile CHOSE records it (not every flood-hearer)
    ... // existing record/refresh
}
```

## Static-safety
- Byte 10 packs `in.nonce` for a static CLAIM (unchanged) → **static CLAIMs byte-identical**; the guard is inside the `is_mobile` branch only.
- **s18 byte-identical** (`3ac88d40…`) — s18 has no mobile CLAIMs.

## Tests
- **Chosen host records:** a mobile CLAIM with `chosen_host_id == host._node_id` → host records it in `_mobile_reg`. Same CLAIM at a **non-chosen** node (`chosen_host_id != _node_id`) → **not** recorded (returns).
- **Codec round-trip:** `pack_j_claim` with `is_mobile=true, chosen_host_id=X` → `parse_j` yields `chosen_host_id==X`; with `is_mobile=false, nonce=N` → `nonce==N` (byte-identical to before).
- **★ Static regression:** a static CLAIM packs/parses byte-identically (nonce preserved); the DAD tiebreak path unaffected.

## Gate
- `pio test -e native` green (the two new codec/handler cases + static regression) — via `./.pio/build/native/program`.
- **s18 fresh-md5 byte-identical** (`3ac88d40…`).
- On **s21** (after this + 3c): `send_hash_resolved` targets the mobile's REAL home (the `mobile_adopted home`), and `mobile_lastmile_fwd` + a `msg_recv` at the mobile fire.
- 4 boards compile.

## Sites
`frame_codec.h`(`j_claim_in`/`j_out` `chosen_host_id`) · `frame_codec.cpp`(`pack_j_claim` byte-10 select; `parse_j` CLAIM byte→nonce+chosen_host_id) · `node_mobile.cpp:58`(set `chosen_host_id`) · `node_join.cpp:217`(the guard) · tests. **NO wire growth, NO wire_version bump.**
