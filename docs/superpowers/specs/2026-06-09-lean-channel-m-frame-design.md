# Lean Channel-Message Frame (M · cmd 0xA) — Design Spec

**Date:** 2026-06-09
**Status:** Design — approved direction (frame layout agreed); ready for an implementation plan.
**Author:** Stanislaw Kozicki / supervised port.

---

## 1. Goal & problem

Channel messages today ride the **generic DATA frame** (`cmd 0x3` + `PAYLOAD_TYPE_M`): a 14-byte DM header + 4-byte MAC wrapping the channel inner. That carries ~12 B of **DM-only plumbing a channel flood never uses** — `next`, `dst`, `hops_remaining|committed`, `prev_fwd_rt_hops`, `ctr`, `visited[6]`, `MAC` — none of which `ingest_channel_m` even reads. Two concrete costs:

1. **A cross-leaf leak.** The DATA frame has **no `leaf_id`**, and the channel-M ingest is **promiscuous** (`node_mac_rx.cpp:304` — *"buffers it regardless of `next`, BEFORE the addressed-check"*). The leaf gate lives only on the RTS (`handle_rts:31`). So a gateway originating a channel message punches its DATA-M one hop into the **adjacent layer**, where a node sweeps it up (it's on the data SF for its own retune) with no leaf context. Measured: **6 / 1053 deliveries in s15 leaked L1→L2**, all from the one L1↔L2 bridge.
2. **Airtime bloat.** ~17 B of overhead per channel DATA-M on the (slow) data SF — pure collision exposure, in a plane we've shown is congestion-limited.

**Redesign:** a purpose-built **lean channel-message frame** with its own command nibble, carrying only what the channel plane needs + `leaf_id` (the leak fix) + forward-compatible room for encryption.

### Non-goals
- Channel encryption itself (deferred crypto slice). This frame only *reserves* the shape via `flavor`.
- Cross-leaf channel delivery (channels stay leaf-scoped; this *enforces* it).

---

## 2. Why a new command nibble (not `DATA`+flag)

Giving channel-M its **own cmd (`0xA`, free in the map)** means `leaf_id` rides **byte-0's low nibble** exactly like BCN/RTS/Q — so the leak gate is the *standard* byte-0 leaf check (`if ((b0 & 0x0F) != _cfg.leaf_id) drop`), at **zero extra bytes**, and the parse is fully decoupled from the DM frame. Overloading `DATA(0x3)+PAYLOAD_TYPE_M` would instead force `leaf_id` into the inner (+1 B) and keep the parse coupled to the DM layout.

**Deliberate divergence from the frozen Lua baseline** (which uses the DATA frame for channel-M). The channel plane is C++-focused; this is a C++-only wire choice, documented like the `data_sf` removal.

---

## 3. Wire format — **M** · cmd `0xA`

| Byte | Field | Notes |
|---|---|---|
| 0 | `cmd=0xA(7..4) \| leaf_id(3..0)` | **leaf_id = the leak gate** (standard byte-0 leaf check) |
| 1 | `channel_id` | which channel |
| 2 | `flavor` | encoding/crypto variant: `0=public` (plaintext) · `1=group` (encrypted) · … |
| 3–6 | `channel_msg_id` (4 B, **BE**) | identity; **== the FLOOD RTS-M tail (bytes 7–10)**; `origin = byte 3` |
| 7… | `payload` | by `flavor` (below) |

**Header = 7 B** (vs 24 B today → **~17 B leaner**; shorter overhear window + less collision on the data SF).

**Payload by flavor:**
- `public` → `payload = body` (plaintext).
- `group`/encrypted → `payload = [nonce/counter (N B) | ciphertext | Poly1305 tag (16 B)]`. The `channel_msg_id`'s `ctr` is only **8 bits** (wraps every 256 posts/origin), so it can't be the sole AEAD nonce under a shared group key — the sealed payload carries an explicit non-wrapping counter/nonce. `N` + the exact sealed layout = the **deferred crypto slice**; the frame reserves it via `flavor`, and plaintext channels pay **nothing** for it.

**Dropped (all DM-only, unread by channel-M):** `next`, `dst`, `hops_remaining|committed_hops`, `prev_fwd_rt_hops`, `ctr`, `visited[6]`, the 4-B `MAC`. Flood TTL is `hop_left` in the RTS-M; dedup/identity is `channel_msg_id`; there's no ACK to need `ctr`; loop-prevention is the bitmap, not `visited`.

### Two correctness properties this gives us
- **Leak plugged:** byte-0 `leaf_id` ≠ mine → drop, at ingest, before buffering. A cross-leaf stray dies on the standard check.
- **Desync-safe:** the DATA-M carries the **full 4-B id** the RTS-M announced. A node that retuned for id X but sweeps up a stray id Y reads Y's real id and attributes it correctly (matches its flood-states) instead of mis-crediting it as X.

---

## 4. Forwarding / identity (unchanged from the flood plane)

- The **FLOOD RTS-M is unchanged** — it already carries the 4-B `channel_msg_id`, `leaf_id` (byte 0), `hop_left`, and the 32-B bitmap. It now simply announces a shorter DATA-M (`payload_len` smaller → the overhear-window airtime calc shrinks automatically).
- Both the **flood-originate** and the **pull-response** emit this one M frame. The pull-response no longer needs addressing — the puller matches by `channel_msg_id`, so it rides the same broadcast M frame (drops the per-target `ctr`/`dst`).

---

## 5. How to change the implemented code

### `frame_codec.h` / `frame_codec.cpp`
- Add `wire::Cmd::M = 0xA`. Add `m_in` / `m_out` structs (`leaf_id, channel_id, flavor, channel_msg_id, body`).
- `pack_m` / `parse_m`: 7-B header + body (BE id). `parse_m` rejects `len < 7`. Remove the `data_m_inner` reuse for the channel path (keep `data_m_inner`/`PAYLOAD_TYPE_M` only if the legacy DM path still needs it — it does **not** once channel-M moves off DATA, so retire `PAYLOAD_TYPE_M`, `parse_m_inner`, and the `DATA_FLAG_PAYLOAD_TYPE_M` flag).
- **`frames.md`:** add the `M` row to the command map + a full section; delete the "Channel-M" inner shape from the DATA section.

### `node_mac_rx.cpp`
- **Dispatch:** route `cmd 0xA` to a new `handle_channel_data(bytes, len, meta)` (sibling of `handle_data`). It does the **standard byte-0 leaf gate**, `parse_m`, then `ingest_channel_m`. The promiscuous "buffer regardless of next" behavior stays — but now it's leaf-safe.
- Remove the `PAYLOAD_TYPE_M` branch from `handle_data`.
- The FLOOD overhear-retune block is unchanged (it keys off the RTS-M).

### `node_channel.cpp`
- **`ingest_channel_m`** — take the parsed `m_out` (now incl. `leaf_id`); the leaf gate is already done at dispatch, but assert/keep it defensively. No promiscuous-leak path remains.
- **`enqueue_flood_m`** / **`enqueue_channel_m`** — build the M frame (`leaf_id = _cfg.leaf_id`, `channel_id`, `flavor`, `channel_msg_id`, body) instead of a DATA+inner. Drop the `ctr`/`dst`/`visited` setup.
- The TX path (`tx_m_broadcast_rts` + the fire-and-forget DATA send) emits the M frame on the data SF after the RTS-M.

### `node.h`
- `data_m_inner` → `m_in`/`m_out` (or rename); `ChannelEntry` **+`uint8_t leaf_id`** (record it; leaf-aware buffer/digest/pull).
- Retire `DATA_FLAG_PAYLOAD_TYPE_M`.

### Sim / device
- No new wiring; the M frame is internal to the channel plane. The decoded trace (`frame_trace.h`) gains an `M` decode line.

---

## 6. Migration

No back-compat. This is a pre-deployment dev change (3 test nodes); the old `DATA+PAYLOAD_TYPE_M` channel path is **fully replaced**, not bridged. (If a wire-version gate is ever wanted, it's the `J` `wire_version` nibble — out of scope here.)

---

## 7. Testing

- **Native (`test_frame_codec.cpp`):** `pack_m`/`parse_m` round-trip (7-B header + body, BE id); reject `len<7`; `leaf_id` survives byte-0.
- **Native (`test_node_channel.cpp`):** update `mk_data_m` → `mk_m`; the existing flood/ingest/overhear cases pass on the new frame; **new leaf-gate case** — an M frame with `leaf_id != mine` is dropped at ingest (0 buffered), a matching one is buffered.
- **Sim (s15):** re-run; assert the **cross-layer leak goes 0** (the 6 L1→L2 deliveries disappear) and same-leaf coverage is unchanged; confirm the per-message airtime drop.
- Both boards build (heltec + xiao).

---

## 8. Deferred
- **Channel encryption** (the `flavor=group` sealed payload: nonce width `N`, key management, the AEAD) — its own slice; this frame is forward-compatible with it.
- **Subscription/multicast confinement** — still the future spec; orthogonal.
