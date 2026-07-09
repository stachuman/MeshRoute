<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 6.2: team-plane DV routing + beacon team-TLV — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-09). **The team becomes a network.** 6.1 gave `team_id` config/NV (inert); 6.2 gives it *routing behaviour*: team members find and route to each other on their own DV plane, with member-to-member locate resolving directly (not via a static home). The user commits; I quality-gate. **DEPENDS ON** the hash-locate spec (`2026-07-09-mobile-hash-locate-via-home.md`) — 6.2 refines its Fix 3 and turns on its `team_scoped` hook (both noted below). A team = `is_mobile`+`team_id` on one leaf; `team_id==0` is a lone mobile / any static node = today, byte-identical.

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
