# Channel-Gossip Redesign — Flood + Repair Backstop

**Date:** 2026-06-08
**Status:** Design — approved direction; ready for an implementation plan.
**Author:** Stanislaw Kozicki / supervised port.

---

## 1. Goal & problem

The channel-message plane (ROADMAP §3) today is **lazy pull**: an originator buffers a message dirty, advertises its id in the next beacon's `CHANNEL_DIGEST`, and neighbours pull it on demand. Propagation is therefore **one beacon-period of latency per hop** (default `beacon_period_ms = 900 000` = 15 min); discovery is *only* via the beacon digest. On a bench it looks dead; even at a fast cadence it is beacon-RTT-per-hop.

**Redesign:** make a **managed flood** the fast primary path, and **demote the existing digest+pull to a repair backstop** for the rare holes. Result: best-effort delivery in seconds, made eventually-complete by the existing gossip — with almost no new machinery beyond the flood itself.

### Non-goals (explicitly out of scope here)
- **Subscription / multicast confinement.** This is a *flood*: every non-excluded node in the leaf receives and re-broadcasts a channel message, even nodes that don't care (the A-B-C-D "B/C carry it" cost is **not** solved here). A subscriber-confined model is deliberately deferred to a future spec; the flood is the transport it would build on.
- Cross-leaf delivery. Floods are **leaf-scoped** (see §7).
- Reliable per-node acknowledgement. Delivery is best-effort + repaired, not acked.

---

## 2. Architecture

Three layers, two of them already exist:

| Layer | Role | Status |
|---|---|---|
| **Flood** | Fast primary propagation (RTS-M + DATA-M, bitmap-suppressed) | **NEW** |
| **Repair** | Catch the holes the flood missed (dirty-digest → pull → re-send) | **REUSED**, demoted from primary to backstop |
| **Buffer / anti-spam** | Dedup store + per-origin admission | **REUSED** unchanged |

Message identity is the **existing 4-byte `channel_msg_id`** (`origin<<24 | key_hash32_low16<<8 | ctr`). The flood, the buffer, the digest, and the pull all key off it, so a flood-received message feeds the repair layer with no translation.

---

## 3. Wire formats

### 3.1 FLOOD RTS-M (control SF) — extends the existing RTS (cmd `0x1`)

New flag `RTS_FLAG_FLOOD = 0x04` (the `rts_flags` 4-bit field already carries `M_BROADCAST=0x01`, `RELAY=0x02`; `0x04` is free). A FLOOD RTS-M sets **both** `M_BROADCAST | FLOOD`: `M_BROADCAST` re-uses the overhear-retune so receivers catch the DATA-M; `FLOOD` selects the extended tail and the flood-forward logic.

```
byte 0   : cmd=0x1(7..4) | leaf_id(3..0)
byte 1   : src                              — immediate forwarder (neighbour-learn; changes each hop)
byte 2   : next        = 0xFF               — broadcast convention (no unicast next-hop)
byte 3   : ctr_lo(7..4) | addr_len=0(3..1) | rsv(0)
byte 4   : hop_left     (reuse the `dst` byte as the TTL safety cap; decremented each forward)
byte 5   : sf_index(7..6) | rts_flags(5..2) | rsv(1..0)   — sf_index = max_data_sf_index; flags = M_BROADCAST|FLOOD
byte 6   : payload_len  (of the DATA-M body that follows on the data SF)
bytes 7-10  : channel_msg_id (4 B, BIG-ENDIAN)            — IMMUTABLE
bytes 11-42 : coverage bitmap (32 B = 256 bits, bit i = node id i in THIS leaf)  — MUTABLE (OR'd each hop)
```
Total **43 B** on the control SF. (M_BROADCAST-without-FLOOD keeps the legacy 2-byte `id_lo16` tail — the pull-response path is unchanged.)

`next = 0xFF` and `hop_left` reuse the existing `next`/`dst` byte positions so `pack_rts`/`parse_rts` stay structurally the same; only the tail length branches on `FLOOD`.

### 3.2 DATA-M (data SF) — UNCHANGED

The existing DATA frame (cmd `0x3`, `DATA_FLAG_PAYLOAD_TYPE_M = 0x01`), inner = `channel_msg_id(4 BE) | channel_id(1) | flavor(1) | body`. Rides the data SF = `max_data_sf` (the sender's pick — see §4). The same frame the current pull-response already emits.

### 3.3 CHANNEL_DIGEST & CHANNEL_PULL — UNCHANGED

The beacon ext-TLV (`count | count × channel_msg_id`) and the Q `CHANNEL_PULL` (cmd `0x6`) are exactly as they are today; they serve the repair layer (§5).

---

## 4. Forwarding rules (the flood)

A node keeps its **hops==1 neighbour set** for each leaf it belongs to (from the routing table — entries with `candidates[0].hops == 1`), the **channel buffer** (dedup store + payloads, keyed by `channel_msg_id`), and a small **flood-state table** (§6).

### 4.1 Originate (`do_send_channel`)
1. Mint `channel_msg_id = channel_msg_id_mint(_node_id, _key_hash32, ctr)`.
2. Seed the bitmap: set **my bit + my hops==1 neighbour bits**.
3. Store in the channel buffer, mark **dirty** (arms the repair layer).
4. `self_originate_observe()` (channels share the DM self-cap — unchanged).
5. Pick `data_sf = max_data_sf()`, `sf_index = max_data_sf_index()`.
6. Enqueue FLOOD RTS-M (control SF) + DATA-M (`data_sf`), `hop_left = flood_hop_max`.
7. `schedule_triggered_beacon()` — so the repair digest is prompt, not 15-min.

### 4.2 Receive a FLOOD RTS-M (control SF), tail `{channel_msg_id, bitmap, sf_index, hop_left}`
1. **Gate:** drop if `leaf_id != _cfg.leaf_id`. (Gateways: §7 — no special-case needed.)
2. **Dedup:**
   - **Active flood-state for this id** (rebroadcast pending — an overheard duplicate): OR the incoming bitmap into `working_bitmap`, recompute my unmarked neighbours, and **cancel** the pending timer if none remain (the §4.5 while-pending path). No re-deliver, no new flood. Done.
   - **Already in the buffer, no active state** (already forwarded): drop. Done.
   - **New** (not in buffer, no state): create flood-state `{id, working_bitmap = incoming, src, rx_snr_q4, awaiting_data = true}`; continue.
3. **Catch the body:** retune RX to `sf_index`'s data SF for the DATA-M window — the **existing M_BROADCAST overhear-retune** (`set_rx_sf` + `kOverhearRetuneTimerId` + the metal slop). Gate on "I don't already hold this id."

### 4.3 DATA-M received (data SF) — `ingest_channel_m`
1. `channel_origin_admit(origin = id>>24, id)` — per-origin anti-spam (unchanged). Over budget → drop (no buffer, no deliver, **no forward**).
2. Store `{id, channel_id, flavor, body}` in the buffer, mark **dirty** (arms repair), deliver to the app (`channel_recv` push).
3. Mark the flood-state `awaiting_data = false`, cache the body in the flood-state (needed to re-flood). → **Forward decision (4.5).**

### 4.4 DATA-M NOT received (overhear window closed, flood-state still `awaiting_data`)
The `kOverhearRetuneTimerId` fire (retune back to routing) checks each `awaiting_data` flood-state: if its id is still absent from the buffer, **fast-self-pull** — enqueue a `CHANNEL_PULL` (existing `channel_pull_fire` path) to **`src`** (the node that relayed the RTS-M to us — a confirmed adjacent holder), instead of waiting for a digest. Then free the flood-state. (We caught the announce, missed the body → repair immediately.)

### 4.5 Forward decision (bitmap rule + SNR-x² backoff)
Let `unmarked = { n ∈ my hops==1 neighbours : bit n is 0 in working_bitmap }`.
- **`unmarked` empty → stay silent** (free the flood-state).
- **`unmarked` non-empty → arm a rebroadcast timer** at `now + backoff`, where
  ```
  snr_norm = clamp((rx_snr − snr_lo) / (snr_hi − snr_lo), 0, 1)
  backoff  = T_backoff · snr_norm²
  ```
  Strong/close link (`snr_norm→1`) → ~`T_backoff` (waits, usually cancelled); weak/far link (`snr_norm→0`) → ~0 (relays first → max new coverage). **Far-first** by default; flip the normalization to make it close-first. `rx_snr` is the SNR of the FLOOD RTS-M that won this flood-state.
- **While pending:** each overheard FLOOD RTS-M for this id → OR its bitmap into `working_bitmap`; recompute `unmarked`; **empty → cancel** the timer + free the flood-state.
- **On fire (`flood_rebroadcast_fire`):** set `{my unmarked neighbours + me}` in `working_bitmap`; `hop_left--` (drop if it reaches 0); pick `data_sf = max_data_sf()`; broadcast FLOOD RTS-M (control, updated bitmap) + DATA-M (cached body, `data_sf`). Free the flood-state.

Self-terminating: when no node has unmarked neighbours, the flood stops. The bitmap suppresses redundant rebroadcasts; the backoff + cancel avoids collisions and further suppresses.

---

## 5. Repair rules (existing gossip, demoted to backstop)

For holes that caught **neither** frame (out of range, asleep-through, or marked-but-missed). All three steps are existing code, **unchanged** — only their role changes (rare backstop, not the primary path):

1. **Advertise** — holding a message ⟹ dirty ⟹ the beacon `CHANNEL_DIGEST` lists the id, K times then retire (`build_channel_digest_ext`). The flood's `schedule_triggered_beacon()` (originate **and** first-receipt) makes this prompt.
2. **Hole pulls** — a node hearing a digest id absent from its buffer → jittered `CHANNEL_PULL` to the advertiser (`process_channel_digest` → `channel_pull_fire`).
3. **Serve** — the advertiser re-sends the DATA-M (M_BROADCAST RTS + DATA-M) to the puller (`handle_channel_pull` → `enqueue_channel_m`).

**Two hole-repair triggers:** *fast* self-pull (caught RTS-M, missed DATA-M — §4.4) and *slow* digest (caught nothing — this section). The flood handles ~99 %, so the slow path rarely fires.

---

## 6. New state, timers, parameters

### Flood-state table (NEW, in `Node`)
```cpp
struct FloodState {
    bool     active = false;
    bool     awaiting_data = false;   // RTS-M seen, DATA-M not yet (fast-self-pull candidate)
    uint32_t id = 0;                  // channel_msg_id
    uint8_t  src = 0;                 // who relayed it to us (for the pull target / learn)
    int16_t  rx_snr_q4 = 0;           // SNR of the winning RTS-M (for the backoff)
    uint8_t  bitmap[32] = {};         // working coverage
    uint8_t  body[channel_msg_max_payload_bytes] = {};  // cached for re-flood
    uint8_t  body_len = 0, channel_id = 0, flavor = 0, hop_left = 0;
};
FloodState _flood[cap_flood_pending];   // bounded ring; oldest-inactive evicted
```

### Timer ids (NEW)
Allocate a free range for the rebroadcast slots — `kFloodRebcastTimerId = 32`, slots `[32 .. 32 + cap_flood_pending − 1]` (32-39 for cap 8; the 32-47 band is currently free, and all < `kCap = 64`). `on_timer`: `id − kFloodRebcastTimerId → flood_rebroadcast_fire(slot)`.

### Constants (NEW, `protocol_constants.h`)
```cpp
constexpr uint8_t  cap_flood_pending = 8;        // concurrent in-progress floods (bounded)
constexpr uint8_t  flood_hop_max     = 16;       // TTL safety cap (≈ dv_hop_cap)
constexpr uint32_t flood_backoff_ms  = 2000;     // T_backoff — max rebroadcast jitter (tunable)
constexpr int16_t  flood_snr_lo_q4   = -15 * 16; // SNR-norm range lo (dB, Q4)
constexpr int16_t  flood_snr_hi_q4   =  10 * 16; // SNR-norm range hi (dB, Q4)
constexpr uint8_t  RTS_FLAG_FLOOD    = 0x04;     // frame_codec.h
```
`flood_backoff_ms` should be ≥ one RTS-M+DATA-M airtime so an overhearer can hear + cancel before its own timer fires.

---

## 7. Gateways & leaf separation

Gateways are **full flood participants, but never bridge a flood across leaves** — and the separation **is** the `leaf_id` field, so there is essentially no special-casing:
- The forward (§4) already keys on `leaf_id`: a node forwards a leaf-X flood only to its leaf-X neighbours, under `leaf_id = X`.
- A gateway simply has neighbours in more than one leaf, so it runs the flood **independently per leaf** and never re-emits a frame under a different `leaf_id`. A leaf-1 message reaches the gateway (it buffers + delivers it) and **stops there** w.r.t. leaf 2.
- Contrast with unicast DMs, which gateways **do** bridge cross-layer. Floods they do not.

Consequence: the 256-bit bitmap is **per-leaf** node-id space (ids 0..255 within *that* leaf), so the gateway keeps a separate flood-state per leaf.

---

## 8. How to change the already-implemented code

### `frame_codec.h` / `frame_codec.cpp`
- Add `RTS_FLAG_FLOOD = 0x04`. Add `flood_bitmap` (32 B) + the full `channel_msg_id` (4 B) to `rts_in`/`rts_out`.
- `pack_rts`/`parse_rts`: branch the tail on `FLOOD` — `FLOOD` ⟹ 4 B id + 32 B bitmap (43 B total); else the existing 2 B `id_lo16` (9 B) or no tail (7 B). Reuse `next`/`dst` byte slots for `0xFF`/`hop_left`.

### `node_channel.cpp`
- **`do_send_channel`** — ADD the flood origination (§4.1) on top of the existing buffer+dirty: seed bitmap, enqueue FLOOD RTS-M + DATA-M, `schedule_triggered_beacon()`.
- ADD: `flood_state_find/alloc/free`, `flood_forward_decision()` (§4.5), `flood_rebroadcast_fire(slot)`, `flood_fast_self_pull(id)`, and 32-byte bitmap helpers (set/test/OR, my-unmarked-neighbours scan over the routing table hops==1 set).
- **`ingest_channel_m`** — after the existing buffer+dirty+deliver, if a flood-state exists for the id (i.e. this DATA-M came from a FLOOD RTS-M), call `flood_forward_decision()`.
- **REUSED unchanged:** `channel_buffer_*`, `channel_origin_admit`, `channel_have_id_lo16`, `build_channel_digest_ext`, `process_channel_digest`, `channel_pull_recently/mark`, `channel_pull_fire`, `handle_channel_pull`, `enqueue_channel_m`, `cancel_channel_pull` — these become the repair layer.

### `node_mac_rx.cpp`
- **`handle_rts` M_BROADCAST block** — EXTEND: if `RTS_FLAG_FLOOD`, run §4.2 (dedup, create/merge flood-state, store the bitmap + `rx_snr`) before the existing overhear-retune. The forward decision is deferred to DATA-M ingest (§4.3).
- **`kOverhearRetuneTimerId` handler** — ADD the §4.4 fast-self-pull check (DATA-M didn't arrive for an `awaiting_data` flood-state).

### `node.cpp`
- **`on_timer`** — ADD the `kFloodRebcastTimerId + slot` branch → `flood_rebroadcast_fire(slot)`.

### `node_beacon.cpp`
- No change to digest building; the trigger-beacon lives in `do_send_channel` + the flood first-receipt.

### `node.h` / `protocol_constants.h`
- Add the `FloodState` struct + `_flood[]` table, the new timer-id constants, the §6 constants, and the new method declarations.

### Sim (`FirmwareNode.cpp`) / device (`fw_main.cpp`)
- No new wiring: `send_channel` already routes to `do_send_channel`; `channel_recv` already prints on device. The flood is internal to `Node`. `flood_*` SNR math uses `meta.snr_db` (real on metal, sim-provided in scenarios).

---

## 9. Testing

- **Native (`test_node_channel.cpp`):** new cases — FLOOD RTS-M pack/parse round-trip (43 B); originate seeds {self+neighbours}; receive-new buffers+delivers+arms-forward; the bitmap rule (all-neighbours-marked → silent; any-unmarked → rebroadcast); backoff cancel on overheard coverage; dedup drops a repeat; fast-self-pull on missed DATA-M; leaf-mismatch drop; hop_left TTL drop. Drive through `handle_rts`/`ingest_channel_m` with the in-memory `TestHal` (capture tx_frames + timers, as the existing suite does).
- **Sim scenario:** a multi-hop leaf (e.g. the line A-B-C-D and a denser mesh) — assert one flood reaches all nodes within a few hops' airtime, and that the repair fires only for injected holes. Verify gateways don't bridge between two leaves.
- **Metal (2-node + 3-node bench):** `send_channel 0 hello` → the other node prints `channel_recv` within a flood (no 15-min wait); kill one node mid-flood → it recovers via the digest backstop after a triggered beacon. Re-use the decoded `«rx`/`»tx` trace (add an `M`/`FLOOD` decode line).

---

## 10. Open / deferred
- **Subscription / multicast confinement** (the B/C cost) — future spec; builds on this flood + a membership primitive.
- **Adaptive data-SF** for the flood (pick per worst-neighbour SNR instead of `max_data_sf`) — optimization, later.
- **Bitmap size** — 32 B is fixed here; a smaller bitmap for small leaves (or a compressed coverage encoding) is a later airtime optimization on the control SF.
- **Originator re-flood** on low coverage — not included; rely on the repair backstop. Revisit if holes prove common.
