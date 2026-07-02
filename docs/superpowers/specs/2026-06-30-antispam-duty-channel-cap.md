<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Anti-spam v2 — duty-anchored, SF/mesh-aware channel cap

**Status:** DESIGN (brainstormed 2026-06-30) → **REVIEWED GO-WITH-FIXES (2026-06-30, 3-lens adversarial audit vs code)**. Coder implements; I quality-gate; user commits + flashes. **No over-the-air wire change** (cleartext-keyed / self-computed; the `limits` query is BLE-only). Supersedes the footprint-budget draft — the `S = U·W` mesh-cost idea is dropped (see *Why*).

## ★ REVIEW VERDICT + RESOLUTIONS (2026-06-30) — read before implementing
3-lens audit (anchors+duty-plane · formula/model · gateway+feedback) vs the code: **all three returned sound=false on the SAME two load-bearing blockers**, but the design SHAPE is sound (channel-only; duty as the volume governor; per-origin fairness share + burst floors; gateway exempt; advisory limits + outcome feedback). **GO-WITH-FIXES** — the 9 must-fixes are spec/anchor corrections, no rethink. The two ★ headline blockers MUST be proven in code (Slice 1) before any cap ships:

- **MF1 ★ DUTY-D IS PER-HOUR, NOT PER-WINDOW (12×).** The code's only budget is `_duty_cycle_budget_ms = duty_cycle · duty_cycle_window_ms` with **`duty_cycle_window_ms = 3600000` (1 HOUR)** (node.h:145, node.cpp:214) — NOT the 5-min `originator_window_ms`. Plugging it raw into `C = D/T_ch` makes every `cap_origin` **12× too loose**. FIX: the cap basis is a channel-window D = **`duty_cycle · originator_window_ms`** (1% → **3000 ms**, exactly what the spec's OWN `limits` JSON shows: `"duty_ms":3000` — the prose is wrong, the JSON is right). Add a public getter returning THIS scaled D; NEVER reuse `_duty_cycle_budget_ms`.
- **MF2 ★ DUTY DISABLED BY DEFAULT → degenerate clamp.** `duty_cycle = 0.0` is the shipped default (node.h:142) → `_duty_cycle_budget_ms == 0`, the duty plane is INERT (duty_status/backstop early-return at budget 0). With D=0: `C=0`, `cap_origin = clamp(0,1,0)` = an **inverted clamp** → gates every default node to 1/window or blocks all. The "duty plane already enforces C" premise is FALSE on a default node. FIX: an explicit **duty-disabled branch → keep the legacy flat cap** (mirror the backstop's no-op-at-0). Same `C<1` floor needed even with duty ON (tiny D / high SF).
- **MF3 T_ch undercounts ~2×.** The sample table assumes BW125, but `radio_bw_hz` **defaults to 250000** (node.h:135); AND every flood hop airs a **43-B FLOOD RTS-M at routing_sf** (node_mac.cpp:450) BEFORE the DATA-M (at SF7/BW250 the RTS-M is the LARGER half). FIX: `T_ch = airtime_routing_ms(43) + airtime_ms(max_data_sf(), radio_bw_hz, radio_cr, preamble_sym, M_FRAME_HDR_LEN+body)`; recompute every sample/cap @ BW250. (`max_data_sf()`, node_mac.cpp:415, is the DATA-M SF — NOT routing_sf.)
- **MF4 the channel SELF-GATE site is MISSING.** `do_send_channel` (node_channel.cpp:260) is the channel ORIGINATION path and does NOT route through `channel_origin_admit` (which self-bypasses at :80); it leans on `self_originate_observe()` (:270), which the spec removes. The cap_origin self-check + 10 s min-interval + `send_blocked{channel}` have **no home** unless `do_send_channel` is added as an explicit site (and :270's observe removed there).
- **MF5 "own-origination only" (Sites line 109) is self-contradictory.** `channel_origin_admit` is **receiver-enforced on OTHERS' origins** (by design — §Channel line 37); taken literally, line 109 guts the cap into a pure self-cap. TWO distinct sites: the **receiver-HOOK** (`channel_origin_admit`, others') + the **self-GATE** (`do_send_channel`, own). The ONLY relay exempt is the GATEWAY cross-layer relay — already handled by the `n_layers==2` early-returns (node_channel.cpp:79/173).
- **MF6 stale anchor names** (fix so a coder doesn't grep for fictions): `rts_relay_exempt` DOES NOT EXIST (real: `!(r.rts_flags & RTS_FLAG_RELAY)`, node_mac_rx.cpp:235, set via `is_gw_relay`→`RTS_FLAG_RELAY`, node_mac.cpp:575); `originator_self_cap_per_window` is a **Cfg field** (node.h:159), NOT in protocol_constants.h (removal: node.h:159 + node_mac.cpp:391-406); `airtime_ms(sf, bw_hz, cr, preamble_sym, len)` — mandatory `preamble_sym` (=protocol::preamble_sym); the `limits` dispatch is in **fw_main.cpp** (USB ~1314 + BLE ~1469-1495), only `write_limits`+snapshot in console_json.cpp.
- **MF7 ledger re-dimension.** Removing `channel_origin_max_per_window` breaks `ChannelOriginLedger.ev[]` (dimensioned by it, node.h:766) — add a replacement `cap_channel_origin_events` const for the array bound.
- **MF8 new public getter** for the channel-window-scaled D (MF1); `limits_snapshot`/cap both use it so the screen's `duty_ms` matches the cap basis (both 5-min).
- **MF9 gateway own-originations vs dm_min_interval.** A gateway's OWN e2e-acks / rcmd-responses (origin==self, !is_forward, !is_gw_relay — node_mac.cpp:300) are genuine own-originations NOT covered by the relay exemption → the 3 s `dm_min_interval` WOULD throttle them (delaying cross-layer ACK confirm). FIX: put `dm_min_interval` INSIDE the existing `origin==self && !is_forward && !is_channel_m` branch (auto-skips relays/floods) AND exempt the e2e-ack/rcmd DataTypes.

**Open/calibration resolutions:** (a) `channel_active_fraction`=0.125 seed, a **Cfg field** (not a protocol const) — ⚠ since `N_active=max(1,⌊frac·N⌋)` floors at 1, frac is **INERT for N<8** (s12 N≈12 → N_active=1); the calibration scenario MUST use a larger N to exercise the 1/N sharing. (b) `T_ch` = **RTS-M + DATA-M** (MF3). (c) D over the **5-min** cap window (MF1). (d) duty-disabled → **legacy flat cap** (MF2). (e) DM backstop **leave as-is** (already duty-anchored via `originator_airtime_share=0.35`, NOT 0.25 — node_mac_rx.cpp:238). (f) `channel_sent{relayed}`: true at `channel_reoffer_confirm` (node_channel.cpp:618); false at the `channel_reoffer_fire` **exhaustion** branch (:601 — currently bare `rp.active=false; return;` with NO emit → ADD it). (g) tiny-message count-abuse stays out of scope (airtime-only). **Soften:** the "every node re-broadcasts once" rationale is a **≤ upper bound** (silent-when-covered/dup-suppress/gateways-never) — reword the N-independence text to "≤ once"; `rt_count()` is O(1) cached (not a scan).

**Implementation slicing (7 slices; the two ★ blockers PROVEN in Slice 1 before any cap ships):** 0 = constants (frac/min-intervals Cfg + `cap_channel_origin_events`) + the scaled-D getter + ledger re-dimension · 1 = `channel_cap_origin()` formula + native math (SF/N/BW + clamps + **duty-disabled fallback** + C<1 floor + the BW250 sample table) · 2 = admit-swap (receiver-HOOK) + channel 10 s floor + the **do_send_channel self-GATE** (remove :270 observe) + s12 sim calibration · 3 = DM self-cap removal + `dm_min_interval` (inside the own-origin branch; exempt e2e-ack/rcmd) · 4 = gateway-exemption VERIFICATION (no new code) · 5 = the `limits` query · 6 = feedback events + contract doc. 0→1→2→3 is the critical path.

## Why
Today's airtime anti-spam has three parts:
1. **DM backstop** — per-**physical-sender** measured airtime at a relay (`node_mac_rx.cpp` `rts_drop_originator_throttle`). Sound + privacy-robust: keys on the cleartext immediate sender, never the **sealed** e2e origin.
2. **Flat self-cap** — `originator_self_cap_per_window = 20` own originations.
3. **Flat channel cap** — `channel_origin_max_per_window = 20` distinct floods per origin.

The two flat caps are **crude**: a cheap low-SF message and an expensive high-SF flood both count `1`, and the channel cap ignores mesh size. Goal: make the **channel** per-origin cap **SF-, mesh-, and duty-aware**, add **burst floors**, retire the redundant self-cap, and expose a **status query** so the companion can show the user their live limits.

**Why not a footprint mesh-cost budget (the earlier `S = U·W` draft):** `S = 25 %·W` is not a per-node budget — re-broadcasting at 25 % TX duty is 25× the EU 1 % limit. The real per-node governor is the **duty plane**. And the DM footprint keyed the **sealed** e2e origin, so it was never enforceable at a relay — only as a self-cap, which the duty plane already subsumes (own airtime is duty-limited and inherently SF-weighted). So the redesign is **channel-only**; DMs rest on the duty plane + the existing backstop.

## The model — one basis: the per-node duty `D`
`D` = the node's duty-cycle airtime budget per window (EU 1 % → `D = 0.01 · W`; `W` = window, reuse `originator_window_ms` = 5 min). The duty plane governs **volume**; anti-spam adds **fairness** (the per-origin cap) and **smoothness** (the burst floors) on top.

> ⚠ **MF1/MF2 (review): compute D, do NOT reuse the existing budget.** The firmware's `_duty_cycle_budget_ms` is over a **1-HOUR** window (`duty_cycle_window_ms = 3600000`), so it is **12× too big** for this 5-min cap basis — compute `D = duty_cycle · originator_window_ms` (1% → 3000 ms). AND `duty_cycle` **defaults to 0** (duty plane inert), which degenerates the clamp — when `duty_cycle ≤ 0`, fall back to the **legacy flat cap** (no new cap). See §REVIEW VERDICT.

### Channel — the redesign
A channel message floods mesh-wide: **every** node re-broadcasts each distinct flood once (dup-suppressed). So each node's channel TX = `(distinct floods) · T_ch`, bounded by its duty:

```
C = D / T_ch          // distinct floods the mesh sustains per window — N-INDEPENDENT
```

`C` is the **total channel capacity** ceiling (every node sees every flood, re-broadcasting each **≤ once** — dup-suppressed, silent-when-covered, gateways never; the duty plane bounds it **when duty is enabled**). So anti-spam's job is **not** to cap the total — it's to **share `C` fairly among originators**:

```
N_active   = channel_active_fraction · N             // active originators (most nodes are silent listeners)
cap_origin = C / N_active = D / (channel_active_fraction · N · T_ch)
```

- **`D` (MF1):** compute `D = duty_cycle · originator_window_ms` (1 % → **3000 ms** for the 5-min window) via a NEW public getter — do **NOT** reuse `_duty_cycle_budget_ms` (a 1-HOUR budget, 12× too big). **Duty-disabled (MF2):** `duty_cycle` defaults to 0 → fall back to the **legacy flat cap** (no `cap_origin`); the formula applies only when `duty_cycle > 0`.
- **`T_ch` (MF3):** the flood's on-air time as re-broadcast = `airtime_routing_ms(43)` (the FLOOD RTS-M at `routing_sf`) **+** `airtime_ms(max_data_sf(), radio_bw_hz, radio_cr, protocol::preamble_sym, M_FRAME_HDR_LEN+body)` (the DATA-M at `max_data_sf()`) → **SF-aware**. The RTS-M is the *larger* half at BW250; DATA-M-only ≈ halves `T_ch` (doubles the cap).
- `N` = `rt_count()` (route-table distinct-dest count, O(1) cached) → **mesh-aware** (`cap_origin ∝ 1/N`).
- `channel_active_fraction` ≈ **0.125** — a **Cfg field** (deployment knob, not a wire const). ⚠ `N_active` floors at 1, so frac is INERT for `N<8`.
- **Enforcement — TWO distinct sites (MF4/MF5):** the **receiver-HOOK** `channel_origin_admit` counts OTHERS' cleartext origins (high byte of `channel_msg_id`) + **drops the over-cap re-broadcast** — this governs a node's relay of *others'* floods (NOT own-origination-only). The **self-GATE** `do_send_channel` (node_channel.cpp:260, which does NOT route through admit) applies `cap_origin` + the 10 s floor to the node's OWN posts (replacing the removed `self_originate_observe`). Only the *cap value* changes: `20` → `cap_origin`.
- **Clamps:** `N_active = max(1, ⌊frac·N⌋)`; `cap_origin = clamp(⌊C / N_active⌋, 1, ⌊C⌋)` — with an explicit **`C ≥ 1` floor** (tiny D / high SF) so the clamp can never invert.

### DM — duty plane + existing backstop + the e2e-ack exemption
Keep the per-physical-sender measured-airtime **backstop** (the only relay-enforceable DM fairness, given the sealed origin); the **duty plane** governs own-origination volume (high-SF DM costs more airtime ⇒ fewer within duty). **Remove** the flat self-cap (redundant).

**★ e2e-ack GLOBAL exemption (2026-07-02 design add).** An e2e-ack must NEVER be throttled — throttling it is self-defeating: the sender never learns the DM arrived, so it **re-sends**, *creating* the traffic the throttle meant to suppress. ACKs are control traffic, not user spam. So e2e-acks are exempt from **every anti-spam count/interval limit**, but the **hard duty-cycle limit still binds** (the sender's own airtime budget governs its acks — the true, un-spoofable ceiling).
- **Originator self-throttle:** the `dm_min_interval` floor already exempts `DATA_TYPE_E2E_ACK`/rcmd by type (Slice 3) — the primary case.
- **Relays:** already exempt (`RTS_FLAG_RELAY`) — a forwarded ack is never dropped.
- **The originating first hop:** the neighbor's backstop drops at RTS-time, before the DATA type is known — so it needs a hint. **Wire (backward-compatible): `RTS_FLAG_E2E_ACK = 0x08`** (the 4th free bit of the `rts_flags` nibble; old nodes ignore it → they just keep applying the backstop, no flag-day). The originator sets it IFF the pending DATA is `DATA_TYPE_E2E_ACK`.
- **Anti-spoof (the bit is a bypass vector otherwise) — verify-at-DATA, flag-and-revoke:** the relay skips the backstop DROP iff (`RTS_FLAG_E2E_ACK` set **AND** the sender is not flagged a spoofer). It **still OBSERVES** the RTS airtime (honest metric, no bypass). At DATA-time it verifies the frame is really `DATA_TYPE_E2E_ACK`; if not, the sender lied → emit `e2e_ack_spoof`, flag the sender (`PeerLiveness.e2e_ack_spoof_until_ms = now + window`), and while flagged its `RTS_FLAG_E2E_ACK` is **ignored** (the backstop applies). One free pass, then revoked; a legit ack-sender is never flagged; duty binds everyone regardless. **★ Also update `docs/frames.md`** (RTS byte-5 `0x08 = RTS_FLAG_E2E_ACK`).

### Burst control — minimum spacing (the floor on top of the average)
`cap_origin` bounds the **average** rate, but as a fixed-window counter it permits a **burst** — a whole window's floods at once. A per-origin **minimum interval** floors the *peak* rate (chosen over a token bucket for simplicity — it just extends the existing per-origin counter with a last-send timestamp):
- **Channel: `channel_min_interval = 10 s`** — **receiver-enforced** on the cleartext origin (same hook as the cap) **+ self**. Honesty-independent; unobtrusive for announcements/group-chat; kills flash-floods. In most regimes the cap already implies ≥ 10 s spacing (see the table), so here it mainly smooths bursts and binds the small-dense-low-SF corner (~6 s).
- **DM: `dm_min_interval = 3 s`, self-applied on own originations** — stops per-keystroke sends (one letter typed-and-sent repeatedly). It's a **self/UX throttle** (honest node spacing its own DMs); a malicious DM burst is still caught by the duty plane + the per-physical-sender backstop, not this floor. Sealed origin ⇒ can't be a per-origin receiver rule like the channel one.

### ⚠ Gateway cross-layer relays — EXEMPT from every limit (already in code; preserve it)
A dual-layer gateway (`is_gateway ≡ n_layers == 2`) **bridges** DM + channel traffic between its two layers. Re-injecting a message into the *other* layer, **on that layer it looks like the originator** — the DM physical sender is the gateway, and a bridged channel flood carries the gateway. **It is a conduit, not an originator, and must be exempt from *every* limit here:** `cap_origin`, both burst floors, and the DM backstop's self-accounting. Applying any of them would choke the bridge (a gateway legitimately carries high cross-layer volume) and mis-attribute that volume to the gateway "as origin."
- **Key the exemption on the cross-layer-relay flag, NOT the origin id** — the origin / physical-sender *is* the gateway on the far layer, so an origin-keyed cap or min-interval would wrongly bite. Test "is this a cross-layer forward?" **first**; if so, skip all limits.
- **This already exists — preserve it, don't re-derive (MF5/MF6):** the DM backstop's relay skip is the inline test `!(r.rts_flags & RTS_FLAG_RELAY)` (node_mac_rx.cpp:235; the gateway sets `is_gw_relay`→`RTS_FLAG_RELAY`, node_mac.cpp:575) — there is **no `rts_relay_exempt` symbol**. A dual-layer gateway is **fully out of the channel plane** already (`channel_origin_admit`/ingest early-return at `n_layers==2`, node_channel.cpp:79/173), so `cap_origin` **cannot run on a gateway's channel plane**. ⚠ **The one real gap (MF9):** a gateway's OWN e2e-acks / rcmd-responses (origin==self, `!is_forward`, `!is_gw_relay` — node_mac.cpp:300) are genuine own-originations NOT covered by the relay flag → put `dm_min_interval` **inside** the existing `origin==self && !is_forward && !is_channel_m` branch (auto-skips relays/floods) AND **exempt the e2e-ack/rcmd DataTypes**, else the bridge's own acks self-throttle and delay cross-layer delivery confirmation.
- `limits` / the feedback events report a gateway's **own** originations only; its bridge traffic is invisible to its limits (the bridge is never rate-limited).

## Sample calculations
> ⚠ **The table below is STALE (MF3): it assumes BW125 + DATA-M-only, but the radio defaults to BW250 and `T_ch` must include the FLOOD RTS-M.** Net effect on the caps is roughly self-cancelling in magnitude but the shape shifts — treat these as ILLUSTRATIVE only; **Slice 1 recomputes the exact table from `airtime_routing_ms(43) + airtime_ms(max_data_sf, radio_bw_hz=250000, …)` and pins it in a native test.** The `600 ms/min` D basis is correct (1 % over the 5-min window = `duty_cycle · originator_window_ms`).

`D = 1 %·W` over `W = 5 min` ⇒ a duty budget of **600 ms/min** (×10 on a 10 %-duty band). Flood frame ~32 B, ~~BW125~~ (real: BW250), CR4/5; `T_ch` from the airtime model. `frac = 0.125` ⇒ `N_active = max(1, 0.125·N)`. **Rates are messages/minute** (enforcement counts distinct floods per origin over the 5-min window; `/min` = window-count ÷ 5).

| channel SF | `T_ch` | **`C` total (screen ceiling)** | `cap_origin` @N=12 | @N=40 | @N=100 |
|---|---|---|---|---|---|
| **6** | ~41 ms | **~15 /min** | ~9.7 /min | ~2.9 /min | ~1.2 /min |
| **7** | ~72 ms | **~8.3 /min** | ~5.5 /min | ~1.7 /min | ~0.7 /min |
| **8** | ~134 ms | **~4.5 /min** | ~3.0 /min | ~0.9 /min | ~0.4 /min |

- **Worked line (SF7, N=40):** `T_ch=72 ms` → `C = 600/72 ≈ 8.3/min` (≈42/window); `N_active=5` → `cap_origin ≈ 1.7/min` (≈8/window, **~35 s** average between an origin's floods).
- **Implied spacing (60 / cap_origin):** the cap already forces ≥ ~10 s between an origin's floods in nearly every regime (N=40 SF7 ≈ 35 s; N=100 SF8 ≈ 150 s); only the small-dense-low-SF corner is faster (N=12 SF6 ≈ 6 s, which the 10 s floor then catches).
- **N-independence:** total ceiling `C` depends only on SF + frame size + duty, *not* mesh size; `cap_origin` shrinks `∝ 1/N`.
- **Levers:** lower SF or a 10 %-band scale up (≈ ×3 per SF step; ×10 band). 16 B "ok" ≈ +40 %; 48 B ≈ −25 %.
- **Calibration check:** s12 (N≈12) legit ≈ 6/origin/window (~1.2/min) fits (SF7 cap ≈ 5.5/min). Large mesh (N=100) SF7 ≈ 0.7/min — tighten via SF / `channel_active_fraction` if legit rates exceed it.

## Status / introspection — the `limits` query (companion)
So the iOS companion can show the user their live limits + headroom, the node exposes one read-only query over the console/BLE surface (the same path as the other BLE events; **BLE-only, no OTA change**).

- **Command:** `limits` → one JSON line, live-computed on query (just counters + the `cap_origin` formula — cheap, idempotent):
  ```json
  {"ev":"limits",
   "win_ms":300000, "win_left_ms":142000,
   "n":40, "ch_sf":7,
   "ch_cap":8, "ch_used":2, "ch_min_ms":10000, "ch_next_ms":0, "ch_ceiling":42,
   "dm_min_ms":3000, "dm_next_ms":1200,
   "duty_ms":3000, "duty_used_ms":640}
  ```
- **`ch_next_ms` / `dm_next_ms` are the direct "when can I send next" answer** — ms until a send is *actually* allowed, the **max of** (burst-floor remaining, channel window-cap wait, duty recovery); `0` = ready now. So it stays honest when the binding constraint is the cap or duty, not just the 10 s / 3 s floor.
- The rest is context: `ch_cap`/`ch_used` = per-origin channel cap and floods used this window; `ch_ceiling` = total `C`; `*_min_ms` = the burst floors; `duty_*` = duty budget + used.
- The app renders e.g. **"Next channel: ready · Next DM: 1.2 s"** + context "Channel 6 of 8 left (resets 2m22s) · duty 21 %". **No node-side UI logic — the node ships numbers, the app formats.**
- Document under `INBOX_SYNC_CONTRACT.md`'s adjacent-BLE surface.

## Companion feedback — advisory `limits` + actual send-outcome
`limits` is **advisory** — it lets the companion *predict* and pace ("next DM in 1.2 s"). But **enforcement is the 1st hop's** (it applies `cap_origin` with *its own* `N` / state, which can differ from this node's estimate) plus this node's own self-gate — so a send can still be rejected *after* the companion thought it was fine. The companion therefore also needs the **actual outcome** to back off. **No OTA change** — the node infers from what it already observes and forwards a local event:

- **Self-gate (precise, pre-TX):** this node's own cap / min-interval blocks the origination → `{"ev":"send_blocked","kind":"dm|channel","reason":"min_interval|cap","next_ms":<wait>}`. The companion shows "rate-limited — retry in N s" and holds until `next_ms`.
- **DM downstream:** ACK → the existing `e2e_acked` (delivered). No CTS / no ACK after retries → `{"ev":"send_failed","kind":"dm","reason":"no_cts|no_ack"}`. The 1st-hop's *silent* backstop drop surfaces as `no_cts` (conflated with no-route — but the companion's reaction, back-off-and-retry-later, is the same).
- **Channel downstream:** the existing origin re-offer **relay-confirm** → `{"ev":"channel_sent","relayed":true}`; none within the confirm window → `{"ev":"channel_sent","relayed":false,"reason":"no_relay"}` (1st-hop throttle or no neighbour).

The companion treats `send_blocked` / `send_failed` / `relayed:false` as **stop-and-back-off** (don't keep firing) and `e2e_acked` / `relayed:true` as success — closing the loop the advisory `limits` alone can't. The OTA **silent-drop is kept** (an explicit reject frame would cost airtime and help a spammer calibrate); feedback is **local** (node → its own trusted companion). Event names finalised in `INBOX_SYNC_CONTRACT.md`.

## Constants
```
channel_active_fraction   // NEW, seed 0.125 — a Cfg FIELD in node.h (deployment knob), NOT a P-const (MF6); inert for N<8
channel_min_interval_ms   // NEW protocol const: per-origin channel burst floor, seed 10000 (10 s)
dm_min_interval_ms        // NEW protocol const: self DM burst floor, seed 3000 (3 s)
cap_channel_origin_events // NEW (MF7): array bound for ChannelOriginLedger.ev[] (was sized by channel_origin_max_per_window)
// D (MF1): compute duty_cycle · originator_window_ms (5-min, 1% -> 3000ms) via a NEW getter — do NOT reuse the 1-HOUR _duty_cycle_budget_ms
// reuse: originator_window_ms (W = 300000); N = rt_count(); duty_status().avail_ms for the used/recovery side
// REMOVE: channel_origin_max_per_window (protocol_constants.h:239); originator_self_cap_per_window (node.h:159 — a Cfg field, NOT a P-const)
```

## Sites
- `protocol_constants.h` — add `channel_min_interval_ms`, `dm_min_interval_ms`, `cap_channel_origin_events`; **remove `channel_origin_max_per_window`** (:239). (`channel_active_fraction` → a Cfg field in node.h, NOT here; `originator_self_cap_per_window` is already a Cfg field at node.h:159 — removed there.)
- `node.h` — the **channel-window-scaled-D getter** (`duty_cycle · originator_window_ms`, MF1/MF8); `channel_cap_origin()` with the **duty-disabled → legacy-flat-cap** branch + the **`C≥1` floor** (MF2); the `channel_active_fraction` Cfg field; a per-origin **last-flood timestamp** alongside `ChannelOriginLedger` (re-dimension `ev[]` by `cap_channel_origin_events`, node.h:766, MF7); a `limits_snapshot()` (`ch_next_ms`/`dm_next_ms = max(burst-floor remaining, channel cap-wait, duty recovery = `duty_status().avail_ms`)`; `duty_ms` on the **5-min** basis).
- `node_channel.cpp` — the **receiver-HOOK** `channel_origin_admit` (:94): replace the flat-`20` with `channel_cap_origin()` (`T_ch = airtime_routing_ms(43) + airtime_ms(max_data_sf(), radio_bw_hz, …)`; `N = rt_count()`) + the 10 s min-interval (per-origin last-flood) — for **OTHERS'** origins (`self`-bypass :80 + `n_layers==2` :79 preserved). The **self-GATE** `do_send_channel` (:260, MF4): apply `channel_cap_origin()` + the 10 s floor to **OWN** posts, **remove the `self_originate_observe()` at :270**, emit `send_blocked{channel}`. `channel_sent{relayed:true}` at `channel_reoffer_confirm` (:618); **ADD `{relayed:false}` at the `channel_reoffer_fire` exhaustion branch** (:601, currently a bare `rp.active=false; return;` with no emit).
- `node_mac.cpp:386-406` — **remove** the self-cap defer (keep the duty-gated path). Add the `dm_min_interval_ms` self-throttle **INSIDE** the existing `origin==_node_id && !is_forward && !is_channel_m` branch (auto-skips relays/floods, MF9) **and exempt the e2e-ack/rcmd DataTypes**; defer if `now − last_dm_origin < dm_min_interval_ms`; emit `send_blocked{dm}`. `send_failed{no_cts/no_ack}` on the CTS/ACK-timeout giveups (node_cascade.cpp already enqueues `send_failed` — add the reasons).
- **Gateway exemption (MF5/MF6) — mostly automatic, verify don't add:** the receiver-HOOK is already gateway-safe (`n_layers==2` early-return, node_channel.cpp:79); the DM self-throttle is relay-safe by living inside the `!is_forward` branch; the DM backstop skips `RTS_FLAG_RELAY` (node_mac_rx.cpp:235). **No origin-id keying, no new flag** — there is no `rts_relay_exempt` symbol.
- `node_budget.cpp` — drop `self_originate_observe`/`self_originate_count`; keep `track_originator_observation` / `compute_originator_metric`.
- `fw_main.cpp` — the `limits` verb dispatch in **BOTH** surfaces (USB ~1314 + BLE ~1469-1495, MF6). `lib/console/console_json.cpp` — `write_limits(...)` + the outcome writers (`send_blocked` / `send_failed` / `channel_sent`), mirroring the existing event writers.
- `ios-companion/INBOX_SYNC_CONTRACT.md` — document the `limits` event + the send-outcome events (mirror the companion-contract-gap pattern just landed).
- `node_mac_rx.cpp` — DM backstop unchanged (already duty-anchored via `originator_airtime_share=0.35`).

## Tests / gate
- **Native:** `cap_origin` formula (SF & N dependence; clamps at tiny/large N); channel admit drops at the computed cap, not `20`; `channel_min_interval` drops a too-soon second flood and admits one spaced ≥ 10 s; `dm_min_interval` defers a < 3 s second own-DM and passes one ≥ 3 s; `write_limits` JSON shape/values; the feedback events fire (`send_blocked` + `next_ms` on a self-gated send, `send_failed` on a DM CTS-timeout, `channel_sent relayed=false` on no relay-confirm); DM backstop + duty path unchanged; self-cap removal breaks nothing. Full native suite green.
- **★ Calibration (sim):** a channel-bearing scenario (s12) — legit traffic stays **dormant** (0 drops) at the computed caps + the 10 s floor; a single-origin flooder throttles at `cap_origin` and is spacing-limited; vary the channel SF and confirm the cap tracks `airtime_ms`. s18 DM path unaffected.
- **Boards:** all 4 build (the `limits`/console arms are device + ESP32 paths).
- **Metal:** a legit channel node never self-defers; a channel flooder dropped at ~`cap_origin` and can't burst < 10 s; a DM flooder still caught by the backstop; `limits` returns sane live numbers over BLE.
- **★ Gateway exemption:** a dual-layer gateway bridging cross-layer DM + channel traffic at high rate trips **no** limit (cap / min-interval / backstop) **even when it appears as the origin on the far layer**; its `limits` reflect only its own originations.

## Open / calibration — RESOLVED (2026-06-30 review)
- `channel_active_fraction`: **RESOLVED → 0.125 (per-channel) as a Cfg field**, per-leaf (`rt_count()` is per-`_active`). ⚠ inert for N<8 (floor-at-1) → the calibration scenario must use a LARGER N than s12 (N≈12 → N_active=1) to actually test the 1/N sharing.
- `T_ch` definition: **RESOLVED → RTS-M + DATA-M** (the FLOOD RTS-M at `routing_sf` is the larger half at BW250; `airtime_routing_ms(43) + airtime_ms(max_data_sf(), radio_bw_hz, …)`). MF3.
- **D window: RESOLVED → 5-min** (`duty_cycle · originator_window_ms`), NOT the 1-h `_duty_cycle_budget_ms`. MF1.
- **Duty-disabled: RESOLVED → legacy flat cap** when `duty_cycle ≤ 0`. MF2.
- **Scope note (unchanged):** this bounds channel **airtime** per origin. Tiny-message count-abuse is out of scope (inbox/app-layer concern).
- DM backstop cap: **RESOLVED → keep as-is** (already duty-anchored via `originator_airtime_share = 0.35 · _duty_cycle_budget_ms`, node_mac_rx.cpp:238 — in the spirit of the redesign, no change).
