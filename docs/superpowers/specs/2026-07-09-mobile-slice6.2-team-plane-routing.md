<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 6.2: team-plane DV routing + beacon team-TLV — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-09; **REVISED 2026-07-10 → SEPARATE `_rt_team` TABLE**). **The team becomes a network.** 6.1 gave `team_id` config/NV (inert); 6.2 gives it *routing behaviour*: team members find and route to each other on their own DV plane, with member-to-member locate resolving directly (not via a static home). The user commits; I quality-gate. **DEPENDS ON** the hash-locate spec (`2026-07-09-mobile-hash-locate-via-home.md`) — 6.2 refines its Fix 3 and turns on its `team_scoped` hook (both noted below). A team = `is_mobile`+`team_id` on one leaf; `team_id==0` is a lone mobile / any static node = today, byte-identical.

**★ ARCHITECTURE (user-decided 2026-07-10): a SEPARATE `_rt_team` routing table, NOT a shared `_rt`+bitset.** A team-plane id (a teammate's home-assigned LOCAL id) can COLLIDE with a static global id (§18 three namespaces) — sharing one `_rt` keyed by `dest` would clash. So the team plane gets its OWN `RtEntry _rt_team[]` table; the static `_rt` is untouched. Implementation: **REUSE the DV core via a `(RtEntry* tbl, uint8_t& cnt)` parameter defaulting to `_rt`/`_rt_count`** (`rt_find`/`rt_merge`/`rt_insert`/`rt_remove`/`rt_prune_cycle` gain the overload; the existing 1-arg signatures delegate → the static path is byte-identical). The transit-relax (old Fix 4) is GONE: team transit lives in `_rt_team` (which never applies the `route_uses_mobile_as_transit` block); the SEND path DISPATCHES `rt_find` by `is_team_peer(final_dst)` → `_rt_team` for a teammate, `_rt` for the home/static.

## Principle
Two mobile regimes on the same PHY:
- **Lone mobile** (`team_id==0`): identity-only beacon, reaches the world ONLY via its home. Unchanged.
- **Team mobile** (`team_id!=0`): emits FULL routing beacons marked `is_mobile`+`team_id`, learns same-team peers, and routes to/**through** them — a self-contained team mesh, no static infrastructure needed.

## ★★ Static-safety
Every path gates on `_cfg.is_mobile && _cfg.team_id != 0` (emit) or a matching `team_id` (ingest). A static node / lone mobile: no team-TLV emitted (empty ext → no bytes), no team peers learned (`is_team_peer` always false → the transit gate is unchanged), `team_scoped` never set. **s18 byte-identical** (`3ac88d40…`); s07/s21 (lone mobiles) unchanged.

## Fix 1 — beacon team-id TLV (free type 5)
- `protocol_constants.h`: `bcn_ext_type_team_id = 5`.
- `frame_codec.cpp` (mirror `pack/parse_gateway_layer_tlv`): `pack_team_id_tlv(uint32_t team_id, span)` → header `(5<<4)|4` + `u32_le(team_id)` = 5 B. `parse_team_id_tlv(ext) -> uint32_t` (0 = absent).
- `node_beacon.cpp:~316` (after the suspect ext, in the build block): `if (_cfg.is_mobile && _cfg.team_id != 0) ext_n += pack_team_id_tlv(_cfg.team_id, …);` — **ONLY a team mobile emits it** → static/lone beacon byte-identical.
- Ingest (`node_beacon.cpp:~551`, beside the other `parse_*_tlv`): `const uint32_t peer_team = parse_team_id_tlv(ext_span);` — feeds Fix 3.

## Fix 2 — a team mobile emits FULL routing entries (`node_beacon.cpp:331`)
The lone-mobile gate becomes team-aware:
```cpp
if (!_cfg.is_mobile || _cfg.team_id != 0) {   // was `!_cfg.is_mobile` — a TEAM mobile emits routes (its team-scoped rt); a LONE mobile stays identity-only
    … existing route-entry build (dirty pack + stable rotation + §5 census) …
}
```
(The census gate at `:358` `… && !_cfg.is_mobile` → `… && (!_cfg.is_mobile || _cfg.team_id != 0)` likewise.) A team mobile's rt holds only same-team peers (Fix 3), so it advertises the team plane, not the static one.

## Fix 3 — learn same-team peers + their routes (`node_beacon.cpp:~641` + REFINES the hash-locate Fix 3)
The hash-locate spec adds `if (!b.is_mobile && learn_direct_neighbor(b.src,…))` (a static node never routes to a mobile). 6.2 carves the team exception:
```cpp
const bool same_team_beacon = b.is_mobile && _cfg.team_id != 0 && peer_team == _cfg.team_id;   // peer_team from Fix 1 ingest
if (same_team_beacon) {
    _active->_team_peer[b.src >> 3] |= (1u << (b.src & 7));           // §6.2: same-team peer (new bitset, mirrors _mobile_peer)
    if (learn_direct_neighbor(b.src, meta_snr_q4, false)) rt_changed = true;   // a TEAM member routes TO its teammates
}
else if (!b.is_mobile && learn_direct_neighbor(b.src, meta_snr_q4, b.self_gateway)) rt_changed = true;   // hash-locate Fix 3 (static: no mobile route)
```
- `node.h`: `uint8_t _team_peer[32] = {}` on the active-layer struct (beside `_mobile_peer`); `bool is_team_peer(uint8_t id) const` (mirror `is_mobile_peer`). `_mobile_peer` is still SET (so non-team mobiles are still avoided) — a team peer is BOTH a mobile peer and a team peer.

## Fix 4 — route THROUGH a same-team member (`node_routing.cpp:617`)
```cpp
bool Node::route_uses_mobile_as_transit(uint8_t dest, uint8_t next_hop) const {
    return next_hop != 0 && dest != 0 && next_hop != dest
        && is_mobile_peer(next_hop) && !is_team_peer(next_hop);   // §6.2: a SAME-TEAM mobile IS a legal transit; a non-team mobile is not
}
```
Static nodes have no team peers (`is_team_peer` always false) → identical behaviour (block all mobile transit). A team member routes multi-hop through teammates. The DV acceptance gate (`node_routing.cpp:319`) is unchanged — it just now accepts same-team transits.

## Fix 5 — team-scope the H-query (`node_hashlocate.cpp:~819`, TURNS ON the hash-locate hook)
The hash-locate spec defined `h_in.team_scoped`/`team_id` (inert). 6.2 sets them at the cold-miss origination:
```cpp
if (_cfg.is_mobile && _cfg.team_id != 0) { in.team_scoped = true; in.team_id = _cfg.team_id; }
```
A same-team target then answers directly (its local id, for the team plane, per the hash-locate Fix 1); a different-team or static target falls through to the home/normal answer (harmless — team_id mismatch). No roster needed: the answer side self-selects on `team_id` match.

## AS-BUILT (2026-07-10 — SEPARATE `_rt_team`, supersedes the shared-`_rt` Fix 2/3/4 above)
The routing plane is a SEPARATE `_rt_team` table (not the shared `_rt`+`_team_peer` the Fix text sketched), because a teammate's local id can collide a static global id (§18). Implementation:
- **DV core parameterized** (`node_routing.cpp`): `rt_find`/`rt_insert`/`rt_merge`/`rt_remove`/`rt_prune_cycle` gained a `(RtEntry* rt, uint8_t& rt_count)` overload; the 1-arg forms are wrappers over `_active->_rt`/`_rt_count` (static path byte-identical). `rt_merge(...,team_plane)` skips the mobile-transit block for the team plane.
- **★ the 1-arg `rt_find(dest)` wrapper DISPATCHES:** `is_team_peer(dest) ? _rt_team : _rt`. This makes EVERY send-path lookup plane-aware with one change; `is_team_peer` is false for any static/non-team node (a `_team_peer` bit is set only when `_cfg.team_id != 0`) → byte-identical. `rt_merge`/`rt_prune_cycle`/beacon-emit use the EXPLICIT-table overloads so ingest/emit don't route through the dispatch.
- **Data** (`node.h`): `RtEntry _rt_team[cap_routes]` + `_rt_team_count` + `_team_peer[32]` + `is_team_peer`.
- **Emit** (`node_beacon.cpp`): a team mobile advertises FROM `_rt_team` via a `src_rt`/`src_cnt` local (team → `_rt_team`, else `_rt`) across the route-pack loop + census-skip + pack + dirty-clear; the entry gate `!_cfg.is_mobile` → `!_cfg.is_mobile || team_emit`. + the type-5 team-TLV.
- **Ingest** (`node_beacon.cpp`): `same_team_beacon` → `_team_peer` set + `learn_direct_neighbor(...,team_plane=true)` + the carried-DV-merge into `_rt_team` (`merge_rt`/`merge_cnt`); else the hash-locate Fix 3 (`!b.is_mobile` → `_rt`).
- **Transit (old Fix 4) — RESTORED after the adversarial verify.** I first dropped it thinking the separate table made it moot, but `route_uses_mobile_as_transit` gates BOTH merge AND send-time selection (`next_hop_selectable`), and a teammate is also a `mobile_peer`, so the select gate rejected a teammate transit → multi-hop A→B→C was defeated. FIX: `route_uses_mobile_as_transit` gains `&& !is_team_peer(next_hop)` (byte-identical for static; `is_team_peer` is team-only).
- **Multi-hop dispatch:** `_team_peer[dest]` is set for ANY team-reachable dest (direct OR a carried multi-hop route, gated on `rt_find(dest,_rt_team)!=null`), so `rt_find` dispatches a multi-hop teammate to `_rt_team` too (not just direct neighbours).
- **Team-plane AGING — IMPLEMENTED** (adversarial-verify follow-up): `age_out_stale_routes` is parameterized + ages `_rt_team` on the same `kAgingTimerId`; a full eviction CLEARS the `_team_peer` bit (maintains the `_team_peer ⟺ _rt_team-route` invariant → a roamed-away teammate stops shadowing the static plane; no table exhaustion).
- **Reprovision:** `clear_routing_state` now wipes `_rt_team_count` + scrubs `_team_peer` (join/create/leave + prep-restart) — a stale team plane no longer shadows a fresh config.

## Adversarial-verify findings (2026-07-10) — all FIXED
A 4-dim workflow (static-safety + refactor-correctness found ZERO issues) confirmed 5 findings, all now fixed: **🟠 HIGH** multi-hop team transit blocked at select-time (the `!is_team_peer` carve-out, above) + the multi-hop dispatch bit; **🟡 MEDIUM** reprovision didn't wipe the team plane; **🟡 MEDIUM×2 / LOW** `_rt_team` never aged + `_team_peer` set-only (the aging + clear-on-eviction, above). Gate: native **665** (+3 tests incl. multi-hop transit + §18 collision), s18/s09/s15 byte-identical, s07/s21 0-fail, 4 boards.

## Tests
- **Team-TLV round-trip:** `pack/parse_team_id_tlv`; a team mobile's beacon carries type-5, a lone mobile's / static's does NOT.
- **★ Team peer + route:** two same-team mobiles hear each other → each sets `_team_peer` + learns a route to the other; a DIFFERENT-team mobile beacon → no team_peer, no route (still avoided as transit).
- **★ Transit:** `route_uses_mobile_as_transit(dest, teammate)` == false (allowed); `(dest, non-team-mobile)` == true (blocked); a static node → true for ALL mobiles (unchanged).
- **Team-scoped query:** a team mobile's cold-miss H-query has `team_scoped=1, team_id=T`; a lone mobile's / static's has `team_scoped=0` (byte-identical frame).
- **★ Static/lone regression:** s18 byte-identical; s07/s21 (lone mobiles, team_id=0) 0-failures — no team-TLV, no team_peer, transit gate unchanged.
- **(Integration, needs a scenario):** a 2-3 mobile team, all same `team_id`, multi-hop — member A locates + DMs member C *through* member B, no static home involved. Flag: needs a new team sim (like s21 did for reachability).

## Gate
- `pio test -e native` green. **s18 byte-identical** (`3ac88d40…`). s07/s21/s09/s15 0-failures. 4 boards.
- Depends on the hash-locate spec landing first (Fix 3 refinement + Fix 5 hook).

## Sites
`protocol_constants.h`(`bcn_ext_type_team_id=5`) · `frame_codec.cpp`(`pack/parse_team_id_tlv`) · `node_beacon.cpp:331`+`:358`(team emits routes) · `:~316`(emit team-TLV) · `:~641`(learn `_team_peer` + route, refine hash-locate Fix 3) · `node.h`(`_team_peer[32]`, `is_team_peer`) · `node_routing.cpp:617`(transit gate) · `node_hashlocate.cpp:~819`(team-scope query). **6.3 (channel) is independent and rides this.**
