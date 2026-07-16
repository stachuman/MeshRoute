# Team-plane multi-hop — design spec (v2, resolution-first)

*2026-07-15, rev. after coder QA (REVISE). The v1 conflated two planes and targeted the wrong first blocker. Corrected here: **s23's headline failure is hash→id resolution, which fail-louds UPSTREAM of any routing.** The team-scoped hash-locate that delivered s23 at `98f71dd` was later replaced by a fail-loud (`node_hashlocate.cpp:884`). This spec restores it (Plane 1 — the actual fix), then adds routing parity (Plane 2 — robustness + send-by-id multi-hop). All claims re-verified against the working tree.*

## 0. Root cause (corrected + verified)

- **The first blocker is resolution, not routing.** s23 does `send_hash cccc0003` (s23…json:14 — a never-heard 2-hop teammate by key-hash). `send_by_hash` reaches `node_hashlocate.cpp:884`: for an off-grid team member with an **unresolved** hash it emits `team_send_unresolved` + `send_failed(mobile_no_home)` and **returns 0 — before `park_send`/`emit_hash_query`.** F discovers a route to a *known id*; it cannot resolve a *hash*. So v1's F work changed the s23 path by nothing.
- **The only hash→id map is direct-heard-only.** `_team_keys` is written solely from a directly-heard same-team beacon (`node_beacon.cpp:718`, `team_key_set`); beacon DV pages + F frames carry no `key_hash32`. A 2-hop teammate is unresolvable.
- **The team-scoped H machinery is built but INERT.** `handle_h` already processes a `team_scoped` locate (`:524`), a same-`team_id` teammate answers (`:531`), and multi-hop forward **preserves team scope** (`:623` — "inert today (no originator sets team_scoped) until 6.2 turns it on"). `emit_hash_query(…, plane)` is plane-aware. The `:884` fail-loud is the one thing stopping the *originator* from lighting it up. **This is why Plane 1 is small.**
- Routing parity (Plane 2) is legitimate — it hardens the H-answer return path and enables send-by-**id** multi-hop — but it is **not** what unblocks s23.

## 1. Scope — two planes, resolution first + a test

- **Plane 1 (PRIMARY — the s23 fix):** un-block the team-scoped hash-locate (restore what `98f71dd` did).
- **Plane 2 (SECONDARY — robustness + send-by-id multi-hop):** F reactive discovery + liveness/freshness parity for `_rt_team`, on plane-private state.
- **Cadence (secondary load-reduction):** a team steady beacon period.
- **Test:** `s24` — co-located static + team multi-hop on shared RF (separation + multi-hop, incl. a dual relay).

## 2. Plane 1 — team-scoped hash resolution (req: fix s23) [PRIMARY]

**Restore the team H-flood (decided: option (a)).** Replace the `:884` fail-loud, for an off-grid team member with an unresolved teammate hash, with `park_send` + `emit_hash_query(key_hash32, hard=false, want_pubkey=false, Plane::TEAM)` — i.e. emit a **team-scoped** H query (sets `h.team_scoped` + `team_id`). This activates the already-built path: same-`team_id` teammates process it (`:524/:531`), relay it multi-hop preserving scope (`:623`), the owner answers, and the parked send delivers on the hash→id binding the answer carries. "Address a teammate by hash/name" stays a supported capability (not rewritten to send-by-id).

**★ Separate the H-flood (layer + team), or Plane 1 leaks (coder/user-caught).** Activating team-scoped H is *not enough* — the H forward path is **not team-gated today**: `:524` stops only a *mobile* relaying a *static* H; a **static node re-floods a `team_scoped` H** (the `:600–630` forward + `mark_hash_query_seen` run unconditionally). So a team H would put team traffic on the static plane. Required: a `team_scoped` H is answered/relayed **only by same-`team_id` members** — a static node (or wrong team) **drops it before any answer, forward, or `mark_hash_query_seen`** (add the `h.team_scoped && !same_team → return` guard ahead of the forward). It stays **layer-scoped** (`leaf_id`, byte 0) **and** team-scoped (`team_id`) — both dimensions, mirroring the F mark. Consequence: a team multi-hop propagates **only through team-member relays** (T1→T2→T3), never a static hop — the correct separation, same as team F. The team-H flood-dedup is naturally hash-separated (distinct `key_hash32`) once static drops it, but verify no aliasing.

**The one thing to verify during implementation:** the H-**answer** return path. `98f71dd` delivered s23 this way (so the H reverse path works), but the `:884` comment claims "origin unroutable on the team plane." Confirm the answer returns via the H reverse breadcrumb; if it genuinely needs a team route, that dependency is exactly what Plane 2 provides (so the build order runs Plane 1 first and lets **s23-flips-green** adjudicate whether Plane 2 is required for the return or merely hardens it).

## 3. Plane 2 — F + liveness routing parity (req #2, all members) [SECONDARY]

- **F reactive discovery — per-PLANE, not per-home (§11.2 = all members).** Replace the blanket `if (_cfg.is_mobile) return;` (`node_route_discovery.cpp:133`) with a plane check: **team-plane F enabled for every team member** (`is_mobile && team_id && _team_local_id != 0` — dual *and* off-grid), while **static-plane F stays home-proxied** for mobiles. Rides the existing dual scaffolding (`node_mobile.cpp:30/167` team-DAD any member + keep dual host id; `node_mac.cpp:574` team dst → `_rt_team`, never home). **Team↔home fallback:** team-direct (`_rt_team`) preferred; home-proxy only when no team route yet.
- **Liveness / freshness for `_rt_team`** — team next-hops get the suspect/silent/dead penalties + the freshness-viability gate + `note_link_confirmed`-on-heard-traffic, so a team route survives between the 5-min team beacons.

## 4. The `Plane` substrate (req #2, "keep separate") — bigger than v1 admitted

v1 undersold this. **~7 liveness/bidi functions read/write `node_id`-indexed arrays and must fork by `Plane`** — and several write them **unconditionally today**, which is the exact static/team id-aliasing to stop (the s24 assertion-3 axis): `cascade_to_alt` writes `_link_bidi`/`_link_reprobe_last_ms` by next-hop; team-beacon RX marks `_dest_seen_ms` + `clear_peer_suspect` unconditionally. Thread `Plane{AUTO,TEAM,GLOBAL}` through F origin/flood/answer, liveness read/write, freshness gate, route select, the cascade, **and the H forward/answer gate (§2 — a non-same-team node drops a `team_scoped` H)**. Contract: `GLOBAL/AUTO` → existing state unchanged byte-for-byte; `TEAM` → team-private state; team local-ids never index a static array. The full separation surface is the union of §2 (H) + this section (F + liveness + cascade).

**★ s18 inertness is RUNTIME-gated, not compile-out.** `MR_FEAT_TEAM` compiles out only on gateway; **native runs s18 with it ON**. So *every* team branch must be **runtime-inert when `team_id==0`** (the s18 fleet has no teams) — the tripwire is the md5, and a stray unconditional team write breaks `3ac88d40`.

## 5. State + RAM (req #2 sizing) [DECIDED: cap_team = 16]

The team plane needs its own copies of the shared routing state, **all six** sized `[cap_team=16]` (aligned to `_team_keys[16]`): `_rreq_seen_team`/`_rreq_last_team`, `_peer_liveness_team`, and the team analogues of `_link_bidi`/`_dest_seen_ms`/`_link_bidi_confirmed_ms`. **NB (coder-caught):** leaving the small-cap ones at their *native* caps would cost ~7.8 KB/leaf (`sizeof(PeerLiveness)=72 B`) — *larger* than the 4.5 KB full-256 it's meant to avoid. Only all-six-at-`[16]` gives the ~1.9 KB target.

**Required: a `team_local_id → slot` map.** Team ids span 17..254 and **cannot index `[16]`**; `_team_keys[16]` is a linear-scan LRU, not a dense index. Pin the mapping (reuse/extend the `_team_keys` scan, or a parallel 16-slot directory) before implementation — it's the addressing substrate all six arrays share.

## 6. Cadence (secondary) [DECIDED: 5 min]

Set an off-grid team member's steady `beacon_period_ms` = `team_beacon_period_ms` = **300 000 ms (5 min)**. A node already exits discovery and beacons at `_cfg.beacon_period_ms` (`node.cpp:750`; the 5 s `discovery_beacon_period_ms` is the discovery burst only — the v1 "persists at 5 s" premise was wrong). 5 min = 3× static's 15-min default, safely under the 20-min freshness + 15-min silent ceilings, and ~15× lighter on duty than 20 s. Load-reduction, **not** the s23 fix.

## 7. Wire (req #3 — free; static wire byte-identical) [DECIDED]

- **Team H query** already carries `team_scoped` + `team_id` (`h_in`, built) — Plane 1 needs **no new H wire**, just an originator that sets them.
- **Team F** (Plane 2): **F is 9 B, not 7 B** (coder-caught — `pack_f`/`parse_f` reject `<9`; `config_hash` mandatory at bytes 7–8 since R6.1; the `frame_codec.h:309` "7 B" comment is stale). Set **byte-2 b6 = `TEAM`** (7 free rsv bits there — the mark is genuinely free + s18-inert) and **append `team_id` (4 B) at offset 9, after `config_hash`**. Team F = **13 B**; static F stays **9 B byte-identical**. `f_in`/`f_out` gain `bool team; uint32_t team_id;`.
- No `wire_version` bump — static wire unchanged; team surface is new with no deployed fleet.

## 8. Test — `s24` (req #4)

`simulation/s24_static_and_team_multihop_meshroute.json`: a **static** line S1↔S2↔S3 (no S1↔S3) **and** a **team** line T1↔T2↔T3 (no T1↔T3) co-located on identical RF, all hearing each other. **★ The relay T2 is HOME-attached (DUAL); T1/T3 off-grid** (+ a home H for T2) — the hardest case: one node live on the mobile-home plane *and* the team plane. Assertions:
1. **Multi-hop, both planes:** S1→S3 via S2 (static) **and** T1→T3 via T2 (team) deliver.
2. **Static↔team separation:** no static `_rt` entry keyed by a team local-id; no team beacon/F/H learned into static `_rt`. **★ A static node NEVER relays or answers a `team_scoped` H** — `script_emit_not_contains` `h_forward`/answer from a static node for the team origin; the team H reaches T3 only via team-member relays (T2). Same for team F.
3. **★ Dual-plane separation:** T2's team relay uses `_team_local_id`/team-private state, never its home/static id-plane; its home traffic never indexes team state. T1→T3 routes team-direct through T2, not via T2's home.
4. **No mutual choke** with the tuned cadence.

Add to `BASELINE.md` alongside s22/s23. Also assert **s23 delivers** (`send_hash` resolves + delivers).

## 9. Gate

- **s18 md5 `3ac88d40…` EXACT** — the runtime-inertness tripwire (team branches inert at `team_id==0`; native has `MR_FEAT_TEAM` on).
- **s23 flips green** — the acid test Plane 1 must satisfy (v1 failed it).
- **s24** — separation + dual-multi-hop proof (assertion 3 = the dual-plane axis).
- **s22** stays green · native · 10 boards · **per-board RAM diff = the budgeted 16-entry `[cap_team]` delta** (not "equal").
- Bench-verify (user, metal): the 5-min cadence + a real off-grid team line.

## 10. Build order

**D (s24 red harness) → Plane 1 (`:884` → team-scoped H; s23 turns green baseline-style) → §4 `Plane` substrate + team state (the wide/risky one, isolate it like the fw_context seam) → Plane 2 (F + liveness; s24 robustness).** Cadence folds in anywhere. If Plane 1 alone flips s23 **and** s24 green, Plane 2 is hardening, not a blocker — decide at that gate.

## 11. Decisions (resolved 2026-07-15)

1. **Resolution = option (a)** — restore the team-scoped H-flood; send-by-hash/name stays supported.
2. **F for all team members** (dual + off-grid), team-plane only; static-F stays home-proxied.
3. **`cap_team = 16`**; over-cap team-DAD/join = **fail-loud clean rejection to the joiner** (today `team_dad_no_free_id` silently no-ops — must become an explicit reject).
4. **`team_beacon_period_ms = 5 min`** (§6).
5. **Wire:** team H reuses existing `team_scoped`/`team_id`; team F = byte-2 b6 `TEAM` + `team_id` at offset 9 (F is 9 B) → 13 B; static byte-identical (§7).
6. **Runtime-gate** team branches (not compile-out) for s18 inertness (§4).
