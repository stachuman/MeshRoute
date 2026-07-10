<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 6.4: off-grid team bootstrap (team-DAD + persistent `_team_local_id`) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-10). **Makes a team of mobiles work with NO static infrastructure.** Today a team mobile only gets a routable id by registering with a static host (a mobile never hosts another, `node_join.cpp:278`), so an off-grid team (a hiking group) is inert — you saw it: two team mobiles, both `UNREGISTERED`, no routes. This slice gives a team member a **persistent, self-assigned `_team_local_id`** (a team-scoped DAD, no host) that the team plane keys on — so the cluster self-bootstraps, AND a member can later register with a static home **without churning the team**. The user commits; I quality-gate. Builds on 6.2 (the `_rt_team` plane) + the hash-locate spine. `team_id==0` = today, byte-identical.

## The identity model (the crux — read first)
A member's **hash** is its identity; ids are plane-specific handles.
- **`_team_local_id`** (NEW, persistent) — the member's id on the **team plane**. Self-assigned by team-DAD, survives everything. The 6.2 team plane (`_team_peer`/`_rt_team`, the team beacon src, team frames) keys on THIS.
- **`node_id`** — the member's id on the **static plane** (0 / unset off-grid; the home-assigned id once it registers with a static home). The static last-mile keys on this.
- Off-grid-only: the member lives entirely on `_team_local_id` (`node_id` unused). Dual (team + static): `_team_local_id` on the team plane, `node_id` on the static plane, **both reached by hash** — 6.2's separate `_rt_team` already keeps the two id-spaces from colliding (§18). A static registration changes `node_id`; `_team_local_id` never moves.

## ★★ Static-safety
Everything gates on `_cfg.is_mobile && _cfg.team_id != 0`. A static node / lone mobile never team-DADs, never sets `_team_local_id`, never emits a team beacon. `_team_local_id` defaults 0 → the beacon-src / frame-accept branches fall to today's `node_id` path. **s18 byte-identical** (`3ac88d40…`); s07/s21 (lone mobiles) unchanged.

## Fix 1 — the `_team_local_id` field (`node.h`)
`uint8_t _team_local_id = 0;` at class scope (beside `_node_id`). `0` = not yet team-DAD'd (a non-team node, or a team member mid-DAD). Accessor `uint8_t team_local_id() const`.

## Fix 2 — team-DAD: self-assign `_team_local_id`, no host (`node_mobile.cpp` + `node_join.cpp` reuse)
**Trigger:** a team member with no `_team_local_id`. Arm it when `team_id` becomes non-zero (`team new`/`team <id>` → `handle_team`) and at boot if a `team_id` persisted, gated on `_cfg.is_mobile`. Independent of the static DISCOVER — the team plane must come up whether or not a static host exists. (In the FSM, the `mobile_no_host` backoff at `node_mobile.cpp:63` is the natural place to *also* ensure the team-DAD has fired.)
**Mechanism** (reuse the static DAD, team-scoped):
1. Pick a candidate id via a team-scoped `join_choose_candidate_id` variant — free means: not a known `_team_peer`, not our own, 17..254. (Check `_team_peer` + `_rt_team`, NOT the static `id_bind`.)
2. Set `_team_local_id = candidate` (tentative) and emit a team beacon (src = candidate, type-5 team-TLV) — the claim announcement.
3. Guard window (reuse `join_claim_guard_fire`'s timer shape): if a **same-team** beacon arrives with `src == candidate` from a different `key_hash32` → conflict → re-pick (step 1). Else → confirmed.
4. On confirm, the member is a routable team peer (6.2 runs). Emit `team_dad_adopted(_team_local_id)`.
No wire change — the claim IS a normal team beacon (src + team-TLV); teammates already parse both.

## Fix 3 — team-DAD DEFENSE is TEAM-scoped (`node_beacon.cpp:~519`)
The hash-locate fix made a mobile NOT DAD-defend its local id vs a static/global beacon (`&& !_cfg.is_mobile`). Team-DAD needs the OPPOSITE for the team plane: a member defends `_team_local_id` **only against a same-team claim**. Add (beside the existing self-defense gate):
```cpp
if (b.is_mobile && _cfg.is_mobile && _cfg.team_id != 0 && peer_team == _cfg.team_id
    && b.src == _team_local_id && b.key_hash32 != _key_hash32) {
    addr_conflict_send_deny(_team_local_id, …);   // team-scoped DENY: someone else claims our team id
}
```
Never fires vs a static beacon (`peer_team==_cfg.team_id` guards it) → s18 unaffected. Residual out-of-range collision (two members pick the same id, never hearing each other) is self-healing: the moment their beacons meet, the DENY re-DADs one; until then §18's `_rt_team` isolation prevents mis-delivery.

## Fix 4 — the team plane keys on `_team_local_id` (`node_beacon.cpp:234` + `node_mac_rx.cpp:149/405`)
- **Beacon src** (`node_beacon.cpp:234` `in.src = _node_id`): a team member's beacon is its TEAM-plane presence → `in.src = (_cfg.is_mobile && _cfg.team_id != 0 && _team_local_id) ? _team_local_id : _node_id;`. Downstream is then automatic — a receiver's `b.src` IS the peer's `_team_local_id`, so `_team_peer[b.src]` and the `_rt_team` dest/next already key correctly (no other 6.2 change).
- **Team-plane frame src:** a team member's team routing/channel frames use `_team_local_id` as the link-layer src (it's a team member's team-plane id). Off-grid-only (`node_id==0`), this is its only src.
- **Frame mark-accept** (`node_mac_rx.cpp:149` RTS, `:405` DATA — currently `next != _node_id || (addr_len==1)!=is_mobile`): a team member accepts a mark-addressed frame to EITHER id:
```cpp
const bool for_team   = _cfg.team_id != 0 && _team_local_id && r.next == _team_local_id && r.addr_len == 1;
const bool for_static = r.next == _node_id && ((r.addr_len == 1) == _cfg.is_mobile);
if (!for_team && !for_static) return;   // overheard
```
(A non-team node: `_team_local_id==0` → `for_team` false → identical to today.)

## Fix 5 — the static transition is ADDITIVE (`node_mobile.cpp` `mobile_claim_guard_fire`)
`set_identity` already leaves `team_id`/`_rt_team`/`_team_peer` intact (node.cpp:38-44, verified). The one change: a team member registering with a static home must land the home-assigned id on `node_id` (the static plane) **while `_team_local_id` stays put** (the team plane). Since the team plane now keys on `_team_local_id` (Fix 4), the existing `set_identity(o.proposed_local_id)` on the static CLAIM no longer disturbs the team — `node_id` moves, `_team_local_id` doesn't. Verify: after a team member registers static, `_team_local_id`, `team_id`, and its `_rt_team` are unchanged, and its team beacon src is still `_team_local_id`.

## Fix 6 — `team new` sets the team PHY (`src/fw_main.cpp` `handle_team`)
The team PHY (freq/SF/BW/leaf) must be chosen so teammates hear each other AND so a member can later register with a compatible static network. Add optional `freq=/sf=/bw=` to `team new` (mirror `mobile register`'s parse): `team new [freq=<MHz> sf=<5-12> bw=<kHz>]` → mint the team_id AND set `_cfg.layers[0]` PHY + retune (reuse `adopt_mobile_phy`). Omitted ⇒ keep the current PHY (with a note that teammates must match). Persist to NV.

## Tests
- **★ team-DAD self-assign:** a team member with no host + `team_id=T` → assigns a non-zero `_team_local_id`; two same-team members converge to DISTINCT `_team_local_id`s (feed each other's beacons → the second re-picks on the DENY).
- **★ team plane keys on `_team_local_id`:** a team member's beacon carries `src=_team_local_id` (not `node_id`); a teammate learns `_team_peer[_team_local_id]` + an `_rt_team` route to it. A lone mobile / static: beacon `src=_node_id` (unchanged).
- **★ transition:** a team member (team plane up) registers with a static host → `node_id` becomes the home-assigned id, `_team_local_id`/`team_id`/`_rt_team` UNCHANGED; it now accepts a mark-frame to EITHER id.
- **team-DAD defense:** a same-team beacon claiming our `_team_local_id` (different key) → we DENY; a static/other-team beacon with the same src → we do NOT.
- **★ Static/lone regression:** s18 byte-identical; s07/s21 0-failures (no team member → `_team_local_id==0` → every branch is today's `node_id` path).
- **(Integration, needs a scenario):** 2-3 mobiles, same `team_id`, NO static → they team-DAD ids, beacon, and build `_rt_team` among themselves (member A DMs C through B). Then add a static host → one member registers, keeps the team, is now reachable both ways.

## Gate
- `pio test -e native` green. **s18 byte-identical** (`3ac88d40…`). s07/s21/s09/s15/s17 0-failures. 4 boards.
- Builds on 6.2 + hash-locate (landed).

## Sites
`node.h`(`_team_local_id`) · `node_mobile.cpp`(team-DAD fire from team-membership / no-host `:63`) · `node_join.cpp`(reuse `join_choose_candidate_id`/claim-guard/`addr_conflict_send_deny`, team-scoped) · `node_beacon.cpp:234`(beacon src=`_team_local_id` for a team member) · `:~519`(team-scoped DAD defense) · `node_mac_rx.cpp:149/405`(accept either id) · `node_mobile.cpp` `mobile_claim_guard_fire`(transition: static id→`node_id`, team intact) · `src/fw_main.cpp` `handle_team`(freq/sf/bw args) · tests. **Off-grid teams route standalone; a member bridges into a static network cleanly because the two planes never shared an id.**

## AS-BUILT + PERSISTENCE + adversarial-verify (2026-07-10)
**PERSISTENCE (user-required — a hiker powers off):** `_team_local_id` is NV-persisted (`device_nv.h` Blob **kVersion 18→19**; boot-load restores it -> CONFIRMED, no re-DAD; the `persist_cfg_if_needed` change-detect loop saves it on (re-)assign; preserved across create/join; **ZEROED on `team 0` / a team switch** in `handle_team`). A `set_team_local_id()` seam does the load/clear.
**★★ 4-dim adversarial workflow (7 findings) — FIXED the actionable ones:**
- **🟠 HIGH (FIXED) — the claim beacon was never emitted:** `team_dad_fire` called `schedule_triggered_beacon()`, which is a NO-OP for a mobile -> the DAD confirmed before announcing. FIX: `emit_beacon("triggered")` directly (announce NOW).
- **🟠 HIGH/MEDIUM (FIXED) — the team DENY was DEAD CODE (two members that DAD the same id never converge):** `addr_conflict_send_deny` carried the TEAM id but the DENY receiver keys on its static `_node_id`, so the claimant never yielded (+ it polluted `id_bind`). FIX: a deterministic KEY TIEBREAK — the LOWER `key_hash32` keeps the id, the HIGHER re-picks; both hear each other's beacon and apply the same rule -> converges, no DENY.
- **🟡 MEDIUM (FIXED) — team-DAD never fired at boot when `mobile_autoregister` OFF:** the boot arm was autoregister-gated. FIX: a team member kicks the FSM regardless of the toggle (`node.cpp:282`), and `mobile_discover_fire` fires team-DAD on the first tick.
- **⚪ NIT (FIXED)** — stale comment.
- **🔴 REMAINING GAP (flagged, NOT a 6.4 fix — spans 6.2/6.5) — team UNICAST DM last-mile:** the receive side accepts a frame to `_team_local_id` (Fix 4), but (B) the SEND path never sets `addr_len=1` for a team-peer dst, and (D) the deliver/forward + hop-budget decisions key `d.dst` on `_node_id` only (so a team dst is treated as a FORWARD, not delivered). So a team member cannot yet UNICAST-DM a teammate. The team ROUTING plane (`_rt_team`, 6.2) + the team-DAD ids (6.4) are in place; the team-DM last-mile needs: `enqueue_data` sets `addr_len=1` when `is_team_peer(dst)`, + a `for_me_dst(dst) = (dst==_node_id) || (team member && dst==_team_local_id)` helper at the ~3 deliver/forward sites (node_mac_rx.cpp:499/577 + deliver). Bounded but touches the core deliver path (byte-identity risk) -> a scoped follow-up. Team CHANNEL/broadcast (6.3) is unaffected.
Gate: native **669**, s18/s09/s15 byte-identical, s07/s21 0-fail, 4 boards, NV v19 (reprovision).
