<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0d — Quality-Agent pre-check ledger (2026-09-04): the static-home plane invariant

Authority: design §19 Slice 0d (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`,
implementation authority since round 3), rulings R-RA-12 and R-RA-19 (`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`).
Source facts verified at `HEAD c512580`. Every `file:line` moves; re-verify before quoting (V2). This ledger is brief
INPUT: the coder derives every figure again.

## 0. What 0d is, in one line

A `lib/core` BEHAVIOUR change (the only one in the pre-feature phase): the `send_by_hash` arms that send toward a
mobile's HOME stamp `Plane::GLOBAL` instead of `Plane::AUTO`, because a home is only ever a static node (R-RA-12),
so the wrapper can never be mis-routed onto the team plane when the home's static id collides with a teammate's
team-local id. Prediction-first corpus, s18-governed, its own commit (C1, C4 attribution).

## 1. The defect, term by term (V1)

| step | anchor | fact |
| --- | --- | --- |
| the three delegation arms | `lib/core/node_hashlocate.cpp:1771-1773` (sealed-relay wrapper), `:1783-1785` (enclosed-type wrapper), `:1787-1788` (plain `MOBILE_SEND`) | each is `do_send(_my_mobile_reg.home_id, …, /*plane=*/Plane::AUTO, …)`; reached only under `reply_to_hash == 0 && _cfg.is_mobile && _my_mobile_reg.active` (`:1736`) — the MOBILE's own delegation |
| how AUTO resolves | `lib/core/node.h:446-452` `flight_is_team_plane(plane,dst) = TEAM \|\| (AUTO && is_team_peer(dst))`; `lib/core/node_routing.cpp:823-825` `is_team_peer(id)` = the `_team_peer` id BITMAP | a teammate whose team-local id equals `home_id` sets that bit ⇒ the wrapper to the home is judged a TEAM flight |
| what a TEAM-judged wrapper does | `lib/core/node_mac.cpp:1099` `team_route` ⇒ `pick_next_cascade_hop` on `_rt_team` (`:1101`); `:1239` RTS `src = team_local_id()`; `:2143` `rt_find(pt.dst, pt.plane)` (`node_routing.cpp:15-28`: AUTO+peer ⇒ `_rt_team`); `:2166` RTS `addr_len` for team flights | the wrapper is routed to the TEAMMATE with that local id, on the team plane, with a team RTS — never to the home. Silent mis-delivery, no `send_failed` (the wrapper is a delegated app DM whose failure surfaces only as a timeout) |
| what GLOBAL does instead | `node_routing.cpp:15-28` GLOBAL forces `_rt`; `node_mac.cpp:183` `stamp_origin(item, GLOBAL, home)` (the same call the home's re-origination already makes with GLOBAL at `:883`); `:169` guard: for a registered mobile the second conjunct is false ⇒ the guard cannot fire either way | the wrapper routes on the static table to the home, static RTS, origin = home_id (unchanged stamp) |
| the invariant's source expression | `lib/core/node.h:639` `can_host_mobiles() = host_mobiles && !is_mobile && !is_gateway && n_layers == 1`, enforced at `node_join.cpp:242/434/799/1009/1182` | a home is static, not a gateway, single-layer — R-RA-12 in code today; 0d makes the SENDER side agree |
| the live comment 0d falsifies | `lib/core/node_mac.cpp:159-161` *"the ONLY producers of Plane::GLOBAL on a DM flight are console_parse.cpp:259 and the sim wrapper; every lib/core enqueue_data/do_send call passes AUTO or TEAM"* | after 0d three (or four) `lib/core` calls pass GLOBAL — correct in place with the old claim visible (design §19 0d already names this) |

## 2. The FOURTH arm — a decision the brief must make

`lib/core/node_hashlocate.cpp:1859` — the CACHED-HOME arm: `do_send(home, sbody, sblen, flags, crypt, override_dst_hash=key_hash32,
type=itype, override_source_hash=reply_to_hash, /*plane=*/Plane::AUTO, …)` — sends an outward same-layer DATA to the TARGET
mobile's cached home. Its destination is a home too, so R-RA-12's invariant applies. Facts:
- reached by ANY sender that is not a registered mobile: a static node, a home re-originating (`reply_to_hash != 0`),
  or an UNREGISTERED mobile (e.g. an off-grid team mobile) — for the last one the same `is_team_peer(home)` collision
  exists;
- B278 round 2 accepted *"cached-home AUTO stays unchanged and is pinned as GLOBAL-equivalent for a static home"*
  and §B278-S3 pinned that equivalence (`test/test_custody_receive_g.cpp:1926-1928`) — a pin about B278's attribution,
  not a ruling that the arm must stay AUTO forever;
- the S3 evidence recorded this arm as ACK-only custody (`commit_deleg_ack(..., DelegAckPeer::node_id, home, …)` at
  `:1861`), unaffected by the plane.
⇒ **Recommendation:** include it as the fourth arm of 0d (same invariant, one slice, one attribution), with the
§B278-S3 test re-aimed ("AUTO≡GLOBAL for a static home" → "GLOBAL, explicitly") and its own native case for the
unregistered-mobile sender. If the owner prefers B278's pin untouched, 0d does three arms and the fourth is booked
as a register entry. Out of scope either way: the typed H-ANSWER arms `:1242/:1266/:1401`
(`team_scoped ? TEAM : AUTO`, answers to a query origin, not wrappers to a home) and the parked-send drains
`:2514/:2575` (a registered mobile's wrapper is never parked; the drain replays a HOME's forward).

## 3. Corpus prediction — prediction FIRST, by measurement

- The predicate that moves a stream: at any of the changed arms, `is_team_peer(home_id) == true` at enqueue time.
  Only scenarios with BOTH a registered mobile and a team can satisfy it. Candidates (scenario JSON grep, 2026-09-04):
  `s22_mobile_team`, `s23_mobile_team_multihop`, `s24_static_and_team_multihop`, `s25_two_team_separation`,
  `s26_team_reroute`, `s28_mixed_team_channels`, `s29_mixed_leaf_team`, `s30_team_dad_mediation`,
  `s34_team_switch_clears_plane`, `s35a_cochannel_isolation`, `s37_team_homed_origin`, `s38_team_origin_learn`.
  `s07`, `s21`, `s27` have mobiles but no team ⇒ `is_team_peer` is always false ⇒ byte-identical by construction.
  The keystone `s18_meshroute` has no mobiles ⇒ byte-identical (D2 holds by construction).
- Team-local ids are assigned dynamically (team-DAD), so the collision cannot be derived from the JSON. Method (the
  B278-S0 census idiom): a THROWAWAY instrumentation build with one sim-only `MR_EMIT` at each changed arm reporting
  `is_team_peer(home_id)`, run over the 36 streams BEFORE the change, count per stream, discard the build; then apply
  the change and require every mover to be one of the streams with a non-zero count, attributed line by line.
  Zero count everywhere ⇒ 36/36 expected and a mover is a STOP.
- A mover means the corpus was carrying the DEFECT (a wrapper mis-routed to a teammate). That is a re-anchor
  PROPOSAL with per-stream attribution for the owner (eighth ruling), never an edit to `simulation/BASELINE.md`.

## 4. Native cases (the brief derives the exact list)

1. The collision case, per arm (sealed-relay, enclosed-type, plain; + cached-home if included): a registered mobile
   whose `_team_peer` bitmap has the bit for `home_id` set (a teammate with team-local id == home id). BEFORE: the
   wrapper's `PendingTx.plane == AUTO`, `team_route` true, RTS `src == team_local_id()`, next hop from `_rt_team`.
   AFTER: `plane == GLOBAL`, next hop from `_rt`, RTS `src == _node_id`, the HOME receives and unwraps
   (`node_mac_rx.cpp:1914` accepts the wrapper). The existing helper `send_by_hash_intent` (`test/test_dual_layer.cpp:239-246`)
   already passes AUTO from the caller — the arms ignore it, which is exactly the defect.
2. The no-collision control: identical bytes BEFORE/AFTER (the wrapper on `_rt`, same RTS).
3. The enqueue_data `:169` guard cannot fire for a registered mobile under either plane (a pinned equivalence).
4. The `:883` stamp precedent: origin = home_id, `mobile_src = true` unchanged.
5. If the fourth arm is included: the §B278-S3 case at `test_custody_receive_g.cpp:1926` re-aimed; an UNREGISTERED
   team mobile sending to a cached-home target with a colliding home id.
6. Mutation entries (file `lib/core/node_hashlocate.cpp` → target `b251hash`, or a new `radm0d` target; the brief
   decides): restoring `Plane::AUTO` on EACH arm separately must redden (three or four entries, match count 1);
   PIN re-sync.

## 5. Gate (D1/D2) and the fence

- Production: `lib/core/node_hashlocate.cpp` (the arms) + the comment correction in `lib/core/node_mac.cpp:159-161`.
  Tests: `test/test_dual_layer.cpp` (+ `test/test_custody_receive_g.cpp` if the fourth arm). Harness: entries + PIN.
  ⛔ Nothing else: no flags, no codec, no `src/`, no BASELINE, no docs beyond the evidence drafts.
- Native; lus RELINK control must FIRE (a `lib/core` change: md5 must move); corpus with the §3 prediction; ABI probe
  (no layout change ⇒ `Node` unmoved); ruled board pair (RAM +0 expected; flash ±small); warning census; both wiring
  probes; checkers; the batteries whose file is `node_hashlocate.cpp` (`b251hash`, `b161hash`, `grantpark`) in full.
- Bench: none (the collision is a host-reachable routing fact; the metal wrapper path is already covered by Parts 48/54).
- Docs drafts held in evidence: `docs/protocol.md` (one sentence: the wrapper to a home is global-plane by
  definition), the design's §4 status of the R-RA-12 arm (from "requires" to "landed"), register if a re-anchor.

## 6. Owner decision the brief needs

D-0d-1: include the cached-home arm `:1859` as the fourth arm (recommended) or keep B278's pin and book it separately.
