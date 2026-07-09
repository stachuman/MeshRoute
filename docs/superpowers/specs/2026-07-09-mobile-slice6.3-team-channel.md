<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 6.3: team channel (group chat) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-09). **The last v1 piece.** A team channel = group chat scoped to a team (`is_mobile`+`team_id`): any member broadcasts, all members receive, static nodes ignore + don't re-flood. Reuses the existing channel/M-frame gossip. The user commits; I quality-gate. **Independent of 6.2's routing** but shares its team roster (`is_team_peer`) and the `team_id` config; land after 6.2. `team_id==0` = today's channel behaviour, byte-identical.

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
A static node (`team_id==0`) drops every team M; a member of the right team ingests + re-gossips it on the team overlay (the existing channel flood, now among members). No `is_team_peer` lookup needed here — the M is self-describing via `team_id`; `is_team_peer` (6.2) is what lets the flood actually *route* between members.

## Fix 3 — RTS-M `MOBILE` mark on a team broadcast (`node_mac.cpp:~597` `tx_m_broadcast_rts`)
When the M being broadcast is team-scoped (`entry.team_id != 0`):
```cpp
rin.mobile_src = true;   // §6.3: mark the team broadcast — a static relay sees MOBILE=1 and does NOT re-flood it (keeps the team broadcast off the static plane)
```
- The RX side already parses `mobile_src` (frame_codec.cpp:431). Add to the static relay path: a `mobile_src` M-broadcast RTS is **not re-flooded by a non-team node** (the static plane doesn't carry team traffic). A team member relays it (per the 6.2 team plane).

## Tests
- **Team M round-trip:** `pack_m`/`parse_m` with the team flag + `team_id` round-trips; a normal M (no flag) stays 7-B header (byte-identical).
- **★ Ingest gate:** a team M (`team_id=T`) → ingested by a `team_id=T` mobile; **dropped** by a static node, a lone mobile, and a `team_id=T'` mobile. A normal M (`team_id=0`) → ingested by leaf as today.
- **★ MOBILE mark:** a team broadcast's RTS has `mobile_src=1`; a static node does NOT re-flood it; a normal channel broadcast has `mobile_src=0` (unchanged).
- **★ Static/lone regression:** s18 byte-identical; s07/s21 + any channel scenario (s17?) 0-failures — no team M minted, normal channels untouched.
- **(Integration, needs a scenario):** a 3-mobile team → member A `do_send_channel` → B and C receive, a co-located static node does NOT. Pairs with the 6.2 team sim.

## Gate
- `pio test -e native` green. **s18 byte-identical** (`3ac88d40…`). s07/s21 + channel scenarios 0-failures. 4 boards.
- Land after 6.2 (shares the team roster + `team_id`).

## Sites
`node.h`(`ChannelEntry.team_id`) · `frame_codec.h`(`m_in`/`m_out` team flag + `team_id`) · `frame_codec.cpp`(`pack/parse_m` conditional 4 B) · `node_channel.cpp:276`(stamp team_id on send) · `:190`(ingest gate) · `node_mac.cpp:~597`(`mobile_src` on team broadcast) + the static-relay no-re-flood on `mobile_src` M. **Completes teams → v1 feature-complete.**
