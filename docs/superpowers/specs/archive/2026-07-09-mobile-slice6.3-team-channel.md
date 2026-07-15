<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 6.3: team channel (group chat) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-09). A team channel = group chat scoped to a team (`is_mobile`+`team_id`): any member broadcasts, all members receive, static nodes ignore + don't re-flood. Reuses the existing channel/M-frame gossip. The user commits; I quality-gate. **6.3-FIRST + STANDALONE (user-chosen order):** this slice proves the **1-hop team cluster** group chat and needs NONE of 6.2's routing — the M-frame is self-describing via `team_id`, so the ingest gate stands alone; **6.2 later adds multi-hop reach** (the team-plane DV that carries this same M-frame further). **Planes = BOTH (user-decided 2026-07-09):** a team member consumes the normal leaf channel AND its team channel (a team M is dropped only by non-members; a normal leaf M, `team_id==0`, is ingested by everyone incl. team members — the gate is additive). `team_id==0` = today's channel behaviour, byte-identical.

## Principle
The channel plane already floods an M-frame to same-leaf nodes. A **team channel** narrows that to a team: the M is **`team_id`-scoped** (only same-team members ingest) and its RTS is **`MOBILE`-marked** (static nodes drop + don't re-flood), so a team broadcast stays on the team overlay and never leaks onto the static plane.

## ★★ Static-safety
- A team send happens only when `_cfg.is_mobile && _cfg.team_id != 0`. A static node / lone mobile never mints a team M and never sets the RTS `MOBILE` mark → **s18 byte-identical** (`3ac88d40…`).
- A normal (non-team) channel M is unchanged: no `team_id`, `MOBILE`=0 → ingested by leaf as before. The team gate is additive.

## Fix 1 — a team member broadcasts a team-scoped M (`node_channel.cpp:276` `do_send_channel`)
When the sender is a team mobile, stamp the outgoing channel entry as team-scoped:
```cpp
// do_send_channel: if (_cfg.is_mobile && _cfg.team_id != 0) this is a TEAM channel msg
e.team_id = _cfg.team_id;   // §6.3: ChannelEntry gains uint32_t team_id (0 = normal channel)
```
- `node.h` `ChannelEntry`: append `uint32_t team_id = 0;` (0 = a normal channel, byte-identical).
- The M-frame carries it: `frame_codec.h` `m_in`/`m_out` gain `uint32_t team_id = 0` + a **team flag** (a spare bit in the M header's `flavor` or `cmd|leaf` byte); `pack_m`/`parse_m` append `team_id` (4 B) **only when the team flag is set** — a normal M stays `M_FRAME_HDR_LEN=7` + payload (s18 safe), a team M is `+4`.
- The RTS-M for a team broadcast sets `mobile_src=1` (Fix 3).

## Fix 2 — ingest gate: only same-team members accept (`node_channel.cpp:190`, the leaf-gate)
Beside the existing `if (m.leaf_id != _cfg.leaf_id) return;`:
```cpp
if (m.team_id != 0) {                                   // §6.3: a TEAM M — only a same-team member ingests
    if (!_cfg.is_mobile || _cfg.team_id != m.team_id) return;   // static node / lone mobile / other team -> DROP (never re-flood, never store)
}
// else: a normal channel M — unchanged
```
A static node (`team_id==0`) drops every team M; a member of the right team ingests + re-gossips it on the team overlay (the existing channel flood, now among members). No `is_team_peer` lookup needed here — the M is self-describing via `team_id`, so **6.3 stands alone at 1 hop**; `is_team_peer` (6.2) is later what lets the flood *route* multi-hop between members. (A team member ALSO still ingests a normal leaf M with `team_id==0` — planes=both.)

## Fix 3 — RTS-M `MOBILE` mark on a team broadcast (`node_mac.cpp:~597` `tx_m_broadcast_rts`)
When the M being broadcast is team-scoped (`entry.team_id != 0`):
```cpp
rin.mobile_src = true;   // §6.3: mark the team broadcast — a static relay sees MOBILE=1 and does NOT re-flood it (keeps the team broadcast off the static plane)
```
- The RX side already parses `mobile_src` (frame_codec.cpp:431). Add to the static relay path: a `mobile_src` M-broadcast RTS is **not re-flooded by a non-team node** (the static plane doesn't carry team traffic). At 1 hop (6.3) every member hears the origin directly, so no relay is needed; a team member's multi-hop relay rides the 6.2 team plane later.

## Tests
- **Team M round-trip:** `pack_m`/`parse_m` with the team flag + `team_id` round-trips; a normal M (no flag) stays 7-B header (byte-identical).
- **★ Ingest gate:** a team M (`team_id=T`) → ingested by a `team_id=T` mobile; **dropped** by a static node, a lone mobile, and a `team_id=T'` mobile. A normal M (`team_id=0`) → ingested by leaf as today.
- **★ MOBILE mark:** a team broadcast's RTS has `mobile_src=1`; a static node does NOT re-flood it; a normal channel broadcast has `mobile_src=0` (unchanged).
- **★ Static/lone regression:** s18 byte-identical; s07/s21 + any channel scenario (s17?) 0-failures — no team M minted, normal channels untouched.
- **(Integration, needs a scenario):** a 3-mobile team → member A `do_send_channel` → B and C receive, a co-located static node does NOT. Pairs with the 6.2 team sim.

## Gate
- `pio test -e native` green. **s18 byte-identical** (`3ac88d40…`). s07/s21 + channel scenarios 0-failures. 4 boards.
- **6.3-first + standalone** (this slice); 6.2 later adds the multi-hop team-plane DV that carries this M-frame further.

## Sites
`node.h`(`ChannelEntry.team_id`) · `frame_codec.h`(`m_in`/`m_out` team flag + `team_id`) · `frame_codec.cpp`(`pack/parse_m` conditional 4 B) · `node_channel.cpp:276`(stamp team_id on send) · `:190`(ingest gate) · `node_mac.cpp:~597`(`mobile_src` on team broadcast) + the static-relay no-re-flood on `mobile_src` M. **Completes teams → v1 feature-complete.**

## Implementation notes + adversarial-verify findings (2026-07-09)
The team_id is DERIVED at pack (`issue_m_broadcast`: `if (min.flavor & channel_flavor_team) min.team_id = _cfg.team_id`) — NOT threaded through `TxItem.inner` (the inner stays `[id4|ch|fl|body]`, unchanged), because a node only ever EMITS its own team's messages (originated, or relayed after the ingest team-match). `mobile_src` is set at the two enqueue sites from `(flavor & channel_flavor_team)` + propagated onto the RTS by `tx_m_broadcast_rts` (`rin.mobile_src = pt.mobile_src` — this was MISSING and is a required fix). A static/non-team node skips a `mobile_src` flood at the RTS RX gate (`node_mac_rx.cpp` flood + non-flood overhear: `&& !(r.mobile_src && _cfg.team_id == 0)`).
- **★ HIGH (FIXED) — the overhear retune window must size for the +4-B team M-frame.** A team M-frame is 11-B (`M_FRAME_TEAM_HDR_LEN`) vs 7-B; the overhearer's data-SF window was `airtime(payload_len + M_FRAME_HDR_LEN)` = `+7`, so at data SF ≥ 10 the +4-B airtime (41–164 ms) exceeds the +30 ms margin → the receiver retunes back before the frame's RX_DONE → `drop_sf_mismatch` → the team message silently drops on long-range configs. FIX (`node_mac_rx.cpp` both window sites): `m_hdr = r.mobile_src ? M_FRAME_TEAM_HDR_LEN : M_FRAME_HDR_LEN` (`mobile_src` exactly identifies a team frame). Regression test: `team_delay > plain_delay`.
- **LOW (DEFERRED to 6.2, correctness-safe) — a different-team member wastes one pull round-trip.** Two co-located teams share the team-PHY nibble, so a team-B member passes the RTS leaf gate + the flood participation gate (`team_id != 0`) for a team-A flood; if it CATCHES the FLOOD RTS-M but MISSES the DATA-M, it fast-self-pulls team-A's message, gets served, then DROPS it at the ingest team gate. Correctness holds (the message is always dropped, no mis-delivery, no re-flood — the pull-response is `mobile_src`-marked so static nodes don't amplify it); the cost is one bounded PULL↔response round-trip on the shared PHY, only in the miss-the-DATA repair case. A proper fix needs a team discriminator on the FLOOD RTS-M itself (the `FloodState` has no team_id — team_id lives only on the DATA-M), which overlaps 6.2's team-aware flood plane → deferred there.
