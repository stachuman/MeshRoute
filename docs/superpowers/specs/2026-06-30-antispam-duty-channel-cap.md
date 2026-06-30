<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Anti-spam v2 — duty-anchored, SF/mesh-aware channel cap

**Status:** DESIGN (brainstormed 2026-06-30). Coder implements; I quality-gate; user commits + flashes. **No over-the-air wire change** (cleartext-keyed / self-computed; the `limits` query is BLE-only). Supersedes the footprint-budget draft — the `S = U·W` mesh-cost idea is dropped (see *Why*).

## Why
Today's airtime anti-spam has three parts:
1. **DM backstop** — per-**physical-sender** measured airtime at a relay (`node_mac_rx.cpp` `rts_drop_originator_throttle`). Sound + privacy-robust: keys on the cleartext immediate sender, never the **sealed** e2e origin.
2. **Flat self-cap** — `originator_self_cap_per_window = 20` own originations.
3. **Flat channel cap** — `channel_origin_max_per_window = 20` distinct floods per origin.

The two flat caps are **crude**: a cheap low-SF message and an expensive high-SF flood both count `1`, and the channel cap ignores mesh size. Goal: make the **channel** per-origin cap **SF-, mesh-, and duty-aware**, add **burst floors**, retire the redundant self-cap, and expose a **status query** so the companion can show the user their live limits.

**Why not a footprint mesh-cost budget (the earlier `S = U·W` draft):** `S = 25 %·W` is not a per-node budget — re-broadcasting at 25 % TX duty is 25× the EU 1 % limit. The real per-node governor is the **duty plane**. And the DM footprint keyed the **sealed** e2e origin, so it was never enforceable at a relay — only as a self-cap, which the duty plane already subsumes (own airtime is duty-limited and inherently SF-weighted). So the redesign is **channel-only**; DMs rest on the duty plane + the existing backstop.

## The model — one basis: the per-node duty `D`
`D` = the node's duty-cycle airtime budget per window (EU 1 % → `D = 0.01 · W`; `W` = window, reuse `originator_window_ms` = 5 min). The duty plane governs **volume**; anti-spam adds **fairness** (the per-origin cap) and **smoothness** (the burst floors) on top.

### Channel — the redesign
A channel message floods mesh-wide: **every** node re-broadcasts each distinct flood once (dup-suppressed). So each node's channel TX = `(distinct floods) · T_ch`, bounded by its duty:

```
C = D / T_ch          // distinct floods the mesh sustains per window — N-INDEPENDENT
```

`C` is the **total channel capacity** and the **single-screen ceiling** (every node sees every flood), and the **duty plane already enforces it**. So anti-spam's job is **not** to cap the total — it's to **share `C` fairly among originators**:

```
N_active   = channel_active_fraction · N             // active originators (most nodes are silent listeners)
cap_origin = C / N_active = D / (channel_active_fraction · N · T_ch)
```

- `T_ch` = on-air time of the flood as re-broadcast, at its **actual** SF (via `airtime_ms()`) → **SF-aware**.
- `N` = the node's route-table distinct-dest count → **mesh-aware** (`cap_origin ∝ 1/N`).
- `channel_active_fraction` ≈ **0.125** (25 % participate, split across ~2 channels).
- **Enforcement (unchanged in shape):** per **cleartext** origin (high byte of `channel_msg_id`). The receiver counts an origin's distinct floods this window and **drops the over-cap re-broadcast**; the originator self-applies. Only the *cap value* changes: `20` → `cap_origin`.
- **Clamps:** `N_active = max(1, ⌊frac·N⌋)`; `cap_origin = clamp(⌊C / N_active⌋, 1, ⌊C⌋)`.

### DM — duty plane + existing backstop (nothing new)
Keep the per-physical-sender measured-airtime **backstop** (the only relay-enforceable DM fairness, given the sealed origin); the **duty plane** governs own-origination volume (high-SF DM costs more airtime ⇒ fewer within duty). **Remove** the flat self-cap (redundant).

### Burst control — minimum spacing (the floor on top of the average)
`cap_origin` bounds the **average** rate, but as a fixed-window counter it permits a **burst** — a whole window's floods at once. A per-origin **minimum interval** floors the *peak* rate (chosen over a token bucket for simplicity — it just extends the existing per-origin counter with a last-send timestamp):
- **Channel: `channel_min_interval = 10 s`** — **receiver-enforced** on the cleartext origin (same hook as the cap) **+ self**. Honesty-independent; unobtrusive for announcements/group-chat; kills flash-floods. In most regimes the cap already implies ≥ 10 s spacing (see the table), so here it mainly smooths bursts and binds the small-dense-low-SF corner (~6 s).
- **DM: `dm_min_interval = 3 s`, self-applied on own originations** — stops per-keystroke sends (one letter typed-and-sent repeatedly). It's a **self/UX throttle** (honest node spacing its own DMs); a malicious DM burst is still caught by the duty plane + the per-physical-sender backstop, not this floor. Sealed origin ⇒ can't be a per-origin receiver rule like the channel one.

### ⚠ Gateway cross-layer relays — EXEMPT from every limit (already in code; preserve it)
A dual-layer gateway (`is_gateway ≡ n_layers == 2`) **bridges** DM + channel traffic between its two layers. Re-injecting a message into the *other* layer, **on that layer it looks like the originator** — the DM physical sender is the gateway, and a bridged channel flood carries the gateway. **It is a conduit, not an originator, and must be exempt from *every* limit here:** `cap_origin`, both burst floors, and the DM backstop's self-accounting. Applying any of them would choke the bridge (a gateway legitimately carries high cross-layer volume) and mis-attribute that volume to the gateway "as origin."
- **Key the exemption on the cross-layer-relay flag, NOT the origin id** — the origin / physical-sender *is* the gateway on the far layer, so an origin-keyed cap or min-interval would wrongly bite. Test "is this a cross-layer forward?" **first**; if so, skip all limits.
- **This already exists — preserve it, don't re-derive:** the DM backstop's `rts_relay_exempt` (gateway cross-layer forward not counted) and a dual-layer gateway being **fully out of the channel plane** (census gated off; `is_gateway ≡ n_layers==2`). The new `cap_origin` / min-intervals must run **only for the node's own originations**, never for relays — verify against the current gateway path.
- `limits` / the feedback events report a gateway's **own** originations only; its bridge traffic is invisible to its limits (the bridge is never rate-limited).

## Sample calculations
`D = 1 %·W` over `W = 5 min` ⇒ a duty budget of **600 ms/min** (×10 on a 10 %-duty band). Flood frame ~32 B, BW125, CR4/5; `T_ch` from the standard LoRa airtime model (`airtime_ms()` refines ±10–20 %). `frac = 0.125` ⇒ `N_active = max(1, 0.125·N)`. **Rates are messages/minute** (enforcement counts distinct floods per origin over the 5-min window; `/min` = window-count ÷ 5).

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
channel_active_fraction   // NEW: active-participant fraction, seed 0.125 (≈25% / ~2 channels)
channel_min_interval_ms   // NEW: per-origin channel burst floor, seed 10000 (10 s)
dm_min_interval_ms        // NEW: self DM burst floor, seed 3000 (3 s)
// reuse: the duty-plane budget D (per window) + originator_window_ms (W = 300000); N = route-table dest count (live)
// REMOVE: originator_self_cap_per_window, channel_origin_max_per_window
```

## Sites
- `protocol_constants.h` — add the three consts; remove `originator_self_cap_per_window`, `channel_origin_max_per_window`.
- `node_channel.cpp` `channel_origin_admit` — replace the flat-`20` compare with `cap_origin` (`T_ch` via `airtime_ms` for the flood SF/size; `N` from the route table; clamps); add a per-origin **last-flood timestamp** → reject if `now − last < channel_min_interval_ms`. Emit `channel_sent{relayed}` from the relay-confirm path (true on confirm, false on its timeout).
- `node.h` — `channel_cap_origin()` + the duty-`D` accessor; per-origin last-flood time alongside the existing per-origin window state; a `limits_snapshot()` returning the `limits` fields — it computes `ch_next_ms`/`dm_next_ms` as `max(burst-floor remaining, channel cap-wait, duty recovery)`.
- `node_mac.cpp:386-406` — **remove** the self-cap defer (keep the duty-gated path); add the `dm_min_interval_ms` self-throttle on own DM originations (defer if `now − last_dm_origin < dm_min_interval_ms`). Emit `send_blocked` on the self-gate (cap / min-interval) and `send_failed` on a CTS/ACK timeout.
- **⚠ Gateway exemption (preserve):** gate the new `cap_origin` / min-interval / `send_blocked` checks on **own-origination only** — skip cross-layer relays (the **relay flag**, not the origin id, drives the skip; mirror the existing `rts_relay_exempt` + gateway-out-of-channel-plane discipline, `is_gateway ≡ n_layers==2`). A bridged message looks gateway-originated on the far layer; do not let that trip a limit.
- `node_budget.cpp` — drop `self_originate_observe/count`; keep `track_originator_observation` / `compute_originator_metric`.
- `lib/console/console_json.cpp` — a `limits` command dispatch + `write_limits(...)`, plus the outcome writers (`send_blocked` / `send_failed` / `channel_sent`) — mirror the existing event writers.
- `ios-companion/INBOX_SYNC_CONTRACT.md` — document the `limits` event + the send-outcome events.
- `node_mac_rx.cpp` — DM backstop unchanged (optional cap recalibration).

## Tests / gate
- **Native:** `cap_origin` formula (SF & N dependence; clamps at tiny/large N); channel admit drops at the computed cap, not `20`; `channel_min_interval` drops a too-soon second flood and admits one spaced ≥ 10 s; `dm_min_interval` defers a < 3 s second own-DM and passes one ≥ 3 s; `write_limits` JSON shape/values; the feedback events fire (`send_blocked` + `next_ms` on a self-gated send, `send_failed` on a DM CTS-timeout, `channel_sent relayed=false` on no relay-confirm); DM backstop + duty path unchanged; self-cap removal breaks nothing. Full native suite green.
- **★ Calibration (sim):** a channel-bearing scenario (s12) — legit traffic stays **dormant** (0 drops) at the computed caps + the 10 s floor; a single-origin flooder throttles at `cap_origin` and is spacing-limited; vary the channel SF and confirm the cap tracks `airtime_ms`. s18 DM path unaffected.
- **Boards:** all 4 build (the `limits`/console arms are device + ESP32 paths).
- **Metal:** a legit channel node never self-defers; a channel flooder dropped at ~`cap_origin` and can't burst < 10 s; a DM flooder still caught by the backstop; `limits` returns sane live numbers over BLE.
- **★ Gateway exemption:** a dual-layer gateway bridging cross-layer DM + channel traffic at high rate trips **no** limit (cap / min-interval / backstop) **even when it appears as the origin on the far layer**; its `limits` reflect only its own originations.

## Open / calibration
- `channel_active_fraction`: 0.125 (per-channel) vs 0.25 (total active); per-leaf vs global.
- `T_ch` definition: DATA-M only vs DATA-M + any per-hop control frame — settle from the flood frame in code (it scales every cap).
- **Scope note:** this bounds channel **airtime** per origin. Count-based abuse with *tiny* messages (high count, low airtime) is not an airtime problem and is out of scope — cap message-count at the inbox/app layer if it matters.
- DM backstop cap: keep as-is, or re-anchor to a fair share of the relay's duty `D`.
