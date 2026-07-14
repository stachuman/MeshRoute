<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile TEAM vs HOME/GLOBAL plane separation — design

**Status:** design (2026-07-13), decisions LOCKED with the user. Two WAVES. **Wave 1 DONE+gated** (bench unblock: team-plane RTS/CTS/ACK use team_local_id). **Wave 2 DONE+gated** (S3 Plane enum + S4 `-t` hard split + S5 folded + S6 mobile-id dedup; native 722, s18 byte-identical). `-t` is a TAIL flag (`send <dst> "..." -t`). I quality-gate; the user commits.

## Problem

A mobile node lives on TWO addressing planes that are currently **conflated**:
- **TEAM plane** — a node is addressed by its `_team_local_id` (team-DAD'd, unique per team). Table `_rt_team`, `is_team_peer` dispatch, team beacons (type-5 team_id TLV). Direct teammate↔teammate; no infrastructure.
- **HOME/GLOBAL plane** ("map by home node") — a node is addressed by its static `node_id`, or as a home-hosted mobile by a **home-assigned local id**, reached as `home_id + dst_hash`; the home last-miles.

**Bench failure (root cause, confirmed):** two team mobiles BOTH provisioned with static `node_id=17` (a collision), distinct `team_local_id` 93/238, UNREGISTERED. `send 238` storms because the team-plane RTS/CTS ride the **static node_id**, not the team id: `node_mac.cpp:634 rin.src=_node_id`. M1 sends RTS `src=17`; M2 replies CTS `from=17 to=17`; M1 rejects it (flight-match `c.tx_id 17 ≠ pending.next 238`, and it looks like a self-echo) → RTS storm. Second blocker: the ACK/NACK accept gates (`node_mac_rx.cpp:1140/1201`) only accept `to==_node_id`, so a dual member drops team ACKs.

## The model (the load-bearing invariant)

**On the TEAM plane, every link-layer field (RTS/CTS/DATA/ACK `src`/`next`/`to`) carries the `team_local_id`, NEVER the `node_id`.** On the GLOBAL plane it carries `node_id` (or a home-assigned local id, disambiguated by the mobile marks `addr_len=1`/`mobile_src`/`mobile_to`). The two id-spaces are kept disjoint by: `rt_find` dispatch (`is_team_peer`→`_rt_team` else `_rt`), `for_me_dst` (accepts `_node_id` OR `_team_local_id`), and the wire marks. **A numeric collision (a `team_local_id` == some `node_id`) is HARMLESS iff no team frame ever presents a static id to the matcher.** Off-grid teams work today only because DAD collapses `node_id==team_local_id`, hiding the bug; a DUAL member (distinct ids) or two members sharing `node_id` expose it.

## Decisions (LOCKED)

1. **`send` is a HARD SPLIT, no cascade:** `send -t …` → TEAM plane only; plain `send …` → GLOBAL/home only, and **fails loud** for a mobile with no home. (Behaviour change: today a plain `send 0x<teammate>` silently team-resolves; the companion/scripts must add `-t`.)
2. **Team hash-locate is BEACON-ONLY:** `-t` resolves a teammate only from the team-key cache populated by heard team beacons; an unheard teammate **fails loud** (no team H-flood round-trip). Global functions still cascade team→global.
3. **Wave 1 first:** ship S0+S1+S7 (bench unblock) standalone, then Wave 2.

## WAVE 1 — bench unblock (make node_id collisions inert on the team plane)

All changes gate on team-ness; when `_team_local_id==0` (non-team) or `==_node_id` (off-grid) they are no-ops ⇒ **s18 byte-identical**.

### S0 — team-plane RTS/CTS use `team_local_id`
- **RTS src** (`node_mac.cpp:634` + the `team_next` at `:641`): after `rin.mobile_src` is set, `if (team_next) rin.src = _team_local_id;`. `team_next = (pt.addr_len==0 && is_team_peer(pt.next))` already exists.
- **M_BROADCAST RTS src** (`node_mac.cpp:493`): when `pt.mobile_src` (a team-channel flood), src = `_team_local_id`. **DEFERRED from Wave 1** — the team-channel path has no test yet and is not the bench issue (a DM). Fold into S6/a team-channel test.
- **CTS build — ALL THREE sites, atomically** (`node_mac_rx.cpp:230` dup-CTS, `:245` retry-CTS, `:361` main): `cin.tx_id = for_team_rts ? _team_local_id : _node_id;` (`for_team_rts` is computed once at `:191`, in scope for all three). `cin.rx_id = r.src` stays (already the RTS src = the sender's team id after the src fix).
- **handle_cts overheard test** (`node_mac_rx.cpp:393,398`): replace `c.rx_id == _node_id` / `c.rx_id != _node_id` with `for_me_dst(c.rx_id)` / `!for_me_dst(c.rx_id)` — so a teammate's CTS (rx_id = our `_team_local_id`) is recognised as clearing OUR flight, not dropped as overheard. The flight-match at `:415` (`c.tx_id != pending_tx->next`) already lines up once `next` is the team id.
- CTS has no wire mark (3-B, by-context); the fix is purely the two id VALUES + the RX-side recognition. No CTS wire change.

### S1 — ACK/NACK accept gate is dual-plane
- `node_mac_rx.cpp:1140` (ACK) and `:1201` (NACK): `if (k.to != _node_id || …)` → `if (!for_me_dst(k.to) || …)` (keep the `(mobile_to==1)!=is_mobile` mark check). Accepts `to==_team_local_id` for a dual member. The per-flight match (`ctr_lo`, `src_hint`) still disambiguates. `for_me_dst` returns false for a non-team node's `_team_local_id==0` ⇒ dead term ⇒ byte-identical.

### S7 — §18 dual-collision regression test
- `test/test_node_r3.cpp`: (a) a DUAL node `node_id=17`, `team_local_id=93`, teammate 238 learned → `send 238` emits an RTS with `src==93` (NOT 17). (b) a node `node_id=17`, `team_local_id=238` receiving a team RTS (`mobile_src`, `addr_len=1`, `next=238`, `src=93`) emits a CTS with `tx_id==238`, `rx_id==93`. (c) the ACK gate accepts an ACK `to=93, mobile_to=1` clearing a team flight. Falsifiable — reverting S0/S1 fails them.

**Gate:** native (RED→GREEN) + s18 md5 `3ac88d40…` byte-identical + s07/s21/s22 0-regression + boards (incl. non-team gateway). Bench acceptance = two dual team mobiles sharing `node_id`, `send <teammate>` delivers.

## WAVE 2 — full separation (spec'd, implement after Wave 1)

### S3 — `Plane{AUTO,TEAM,GLOBAL}` enum, threaded
Add `Plane` and thread it through `do_send`/`enqueue_data` (`node_mac.cpp:185`, `node.h:1135`), `send_by_hash` (`node_hashlocate.cpp:801`), `emit_hash_query` (`:889`). `enqueue_data` stamps `TxItem.plane` (NEW field) so the src decision + resolver order come from ONE source of truth, not re-inferred. Default `AUTO` = today's control flow. **Critic gap folded in:** `TxItem` and the forward/`PostAck` path must carry `plane`/`addr_len`/`mobile_src` so a multi-hop team DATA keeps `next=team_local_id` (today `addr_len` is lost on forward at `node_mac_rx.cpp:960-962`; the relay re-infers via `is_team_peer(pt.next)` OR carries it explicitly — spec the explicit field).

### S4 — the `-t` command split
- `console_parse.cpp:87-117 parse_send_tail`: add `allow_t`/`-t` → `bool team`. Console passes `allow_t=true`; companion/remote pass false until wired.
- `command.h` `SendCmd`: add `uint8_t plane` (AUTO=0/TEAM=1/GLOBAL=2); zero-init = AUTO ⇒ byte-identical for existing callers. **Host-side only, never serialized.**
- `node.cpp:856-869 on_command` fork: TEAM → require team membership, resolve team-only (`team_id_of_key` or `is_team_peer(dst_id)`), FAIL LOUD on miss (no home fallback). GLOBAL → skip team, id_bind→home; fail loud (`mobile_no_home`) if a mobile has no home. AUTO → today's cascade (only for callers that don't pass a plane; the console always passes TEAM or GLOBAL per decision 1).

### S5 — locate cascade team→global (for the GLOBAL/AUTO resolvers, per decision 2 beacon-only for team)
`send_by_hash` parameterized by plane: TEAM = `team_id_of_key` only + fail-loud (beacon-only, no team flood); GLOBAL = id_bind→home→global flood; AUTO = team-first-then-global (reorder `team_key_of_id` before `id_bind` ONLY for a team member under AUTO; id_bind-first for everyone else ⇒ s18 byte-identical).

### S6 — id-uniqueness
- **Team node_id collision:** no assignment change — Wave 1's link-layer fix makes `node_id` inert on the team plane; `team_dad` already guarantees a unique `_team_local_id` (defend at `node_beacon.cpp:694`). Add a boot WARN (diagnostic only) if two provisioned team mobiles share BOTH `node_id` AND `team_local_id`.
- **Two mobiles, same home, same local id:** `find_free_mobile_id` (`node_join.cpp:86-97`) is idempotent-per-hash but the CLAIM record (`:216-228`) is last-write-wins. Fix: at CLAIM, if the claimed `mobile_local_id` is bound to a DIFFERENT `key_hash32`, RE-OFFER a fresh id (bump `claim_epoch`). Document `_mobile_reg` per-leaf scope.

### Wave 2 slice order (critic-mandated)
S3 → {S4, S5, S6}. S4/S5 consume the `Plane` enum from S3. The registered-**and**-team member (a mobile with both a home and a team) is defined by decision 1: `-t` forces team, plain `send` uses the home — no ambiguity, no AUTO cascade on the console path.

## Wire safety
No new wire fields in either wave. `SendCmd.plane` is host-side only. Every RTS/CTS/ACK substitution fires only on a team flight (`team_next`/`for_team_rts`/`for_me_dst` with a non-zero `_team_local_id`); off-grid (`node_id==team_local_id`) and static (`_team_local_id==0`) hit the unchanged branch. **Keystone gate: s18 md5 `3ac88d40e00d2605ff66659f696d52bf`** after every slice — any drift = a team gate leaked into the static path.

## Open items deferred
- Full team H-query round-trip (reach an UNHEARD teammate) — deferred by decision 2 (beacon-only).
- Expose the chosen plane back to the app (a `plane` field in `send_acked`) — nice-to-have for the companion; consider in S4.
- Cross-layer + team interactions, multi-hop team forward (S2 within S3) — bench-verify-pending.
