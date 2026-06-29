<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Companion-contract gap fixes — DM-ctr persistence · reqpubkey_sent · send_e2e_acked JSON

**Status:** coder instruction. The user commits + flashes; I gate. **Device-side (firmware) only — no over-the-air wire change, no NV blob version bump.** Closes the three firmware gaps the 2026-06-29 audit of `ios-companion/INBOX_SYNC_CONTRACT.md` found (the contract is updated; these make the firmware match it).

## Why
Three small holes where the firmware doesn't yet meet the companion contract:
1. **D7 (the real bug)** — DM dedup identity is `(sender_hash, ctr)`, but the per-peer DM counter `_peer_send_counter[dst]` resets to 0 on reboot, so a sender reboot re-mints `ctr` values the companion has already archived → its next DMs are **silently deduped away**. Only the *self-keyed* channel counter persists today.
2. **`reqpubkey_sent`** — the contract documents `{"ev":"reqpubkey_sent","hash":…}` but the verb returns a generic `{"ack":"queued"}`.
3. **`send_e2e_acked`** — `pushkind_name` has no case for it, so if that live push is serialized over BLE it emits `{"ev":"unknown",…}`.

(`ready.bonds` is **out of scope** — deferred to the notification slice, per the contract.)

## Gap 1 — persist the DM send-ctr across reboots (D7)

**Generalize the existing `channel_ctr` lease from "the self counter" to "a per-peer high-water floor".** The self-keyed channel counter is just one entry in `_peer_send_counter`; the same persisted-with-margin value, applied as a *floor* to every per-peer counter on boot, closes the DM case too — and **reuses the existing `Blob.channel_ctr` u16 slot, so `kVersion` stays 15 (no re-provision).**

The lease invariant to preserve: the persisted value runs `margin` (256) **ahead** of the live counter, so on reboot every newly-minted `ctr` is `> ` any minted before the reboot (no `(sender_hash, ctr)` collision), while writes still fire only every ~`margin` sends.

Changes:
- **`node.h` — add a max-reader + a floor (keep `channel_ctr()`/`restore_channel_ctr` as-is).** Add `peer_ctr_high()` = the **max over all `_peer_send_counter` values** (0 if empty), and a per-`LayerRuntime` `uint16_t _peer_ctr_floor = 0;` with `void restore_peer_ctr_floor(uint16_t v) { _active->_peer_ctr_floor = v; }`. The existing `channel_ctr()`/`restore_channel_ctr` stay (the v15 self-counter path); what changes is that the LEASE now persists the *max* and restore also seeds the *floor*.
- **`node_mac.cpp` `next_ctr(dst)` (line 20):** apply the floor before incrementing —
  ```cpp
  uint16_t& c = _active->_peer_send_counter[dst];
  if (c < _active->_peer_ctr_floor) c = _active->_peer_ctr_floor;   // D7: resume above the pre-reboot high-water
  return ++c;
  ```
  So a peer not yet sent-to this boot starts at `floor+1`; an already-active peer continues. (Wrap edge: `_peer_ctr_floor` is the persisted high-water; a 16-bit wrap is the same edge the channel lease already tolerates.)
- **`fw_main.cpp` — the lease (≈:1809-1836):** read `g_node.peer_ctr_high()` instead of `channel_ctr()` for the `lease_due` check + the `leased = high + margin` write into `b.channel_ctr` (so the persisted value now covers DM peers, not just the self counter). Restore (≈:1684-1685): keep `g_node.restore_channel_ctr(nv.channel_ctr)` (v15 — seeds the self counter so `peer_ctr_high()` reads the floor immediately) **and add** `g_node.restore_peer_ctr_floor(nv.channel_ctr)`; `g_ctr_lease = nv.channel_ctr` as today. Live == lease at restore ⇒ no spurious/regressing write (the v15 discipline holds); the floor then seeds fresh DM peers on the first `next_ctr`.
- **Transition (old v15 blob):** a pre-fix blob's `channel_ctr` holds only the *self* counter, so the first post-upgrade boot seeds the floor from that (≥ 0, imperfect for DM peers that had outrun the self counter) — perfect from the first new-firmware lease write onward. No flag-day, no re-provision.

The channel id-reuse fix this generalizes is **subsumed** (the self counter is in the max and gets the floor), so the v15 channel behaviour is preserved.

## Gap 2 — emit `reqpubkey_sent`

The `reqpubkey <hex8>` BLE dispatch (`fw_main.cpp:~1503`) currently returns the generic `write_ack`. Replace it with the contract's documented event:
```json
{"ev":"reqpubkey_sent","hash":3735928559}
```
Add a `write_reqpubkey_sent(buf, cap, uint32_t hash)` to `console_json.cpp` (mirror `write_ack`'s shape) and call it from the reqpubkey arm after `emit_hash_query(hash, hard=true, want_pubkey=true)` (`node.cpp:831`) fires. `hash` = the requested `key_hash32`. (A node with no crypto identity already fails loud before the flood — keep that path returning its existing error.)

## Gap 3 — give `send_e2e_acked` a JSON case (no more `ev:"unknown"`)

The durable receipt path (`record_ack` → pull → `inbox_dm type:"e2e_ack"`) is the authoritative companion channel and already works; this adds the **live twin** so the app can mark its OUTBOX message **DELIVERED immediately** (not only on the next pull), and removes the `ev:"unknown"` hazard.

- **`console_json.cpp` `pushkind_name` (≈:65-77):** add `case PushKind::send_e2e_acked: return "e2e_acked";`.
- **`write_push` (`console_json.cpp`):** add a `send_e2e_acked` arm serializing the push's identity — the **live twin** of the durable receipt:
  ```json
  {"ev":"e2e_acked","origin":2,"ctr":7,"sender_hash":3735928559}
  ```
  `origin` = the node that **confirmed** delivery (the original `-a` DM's recipient), `ctr` = the **acked** ctr, `sender_hash` = the acker's `key_hash32` (present on a cross-layer ack — same field `record_ack`/`node_mac_rx.cpp:610` carries). The app matches `(origin, ctr)` — or `(sender_hash, ctr)` when `sender_hash != 0` — to its OUTBOX, identical to the durable `inbox_dm type:"e2e_ack"` rule; it must **NOT** render it as an inbound DM.

Update `INBOX_SYNC_CONTRACT.md`'s "Adjacent BLE surface" note + the `type:"e2e_ack"` section to reflect the live `e2e_acked` event once landed (the contract currently flags it as a gap-to-decide).

## Sites
- `lib/core/node.h` (`peer_ctr_high`/`_peer_ctr_floor`/`restore_peer_ctr_floor`) · `lib/core/node_mac.cpp:20` (`next_ctr` floor) · `src/fw_main.cpp` (≈:1684 restore, ≈:1809-1836 lease, ≈:1503 reqpubkey arm) · `lib/console/console_json.cpp` (`write_reqpubkey_sent`, `pushkind_name` + `write_push` `send_e2e_acked` arm) · `ios-companion/INBOX_SYNC_CONTRACT.md` (un-gap the two events).

## Tests / gate
- **Native:** a `next_ctr` floor test — seed `_peer_ctr_floor=N`, assert a fresh `next_ctr(dst) == N+1` and an active peer continues; `peer_ctr_high()` = the max over a populated `_peer_send_counter`. Codec round-trip / writer test for `reqpubkey_sent` + `e2e_acked` JSON shapes (mirror the existing `console_json` writer tests). The full native suite stays green.
- **No NV-version change** ⇒ no re-provision; confirm `kVersion == 15` is untouched and an old blob still loads.
- **Boards:** all 4 build (the e2e-ack/reqpubkey arms are device + ESP32 paths).
- **★ Metal (the real gate for D7):** flash; send a few `-a` DMs to a peer; note the ctrs; **reboot the sender**; send again — confirm the new ctrs are **above** the pre-reboot ones (read the inbox / the `e2e_acked` events), i.e. no dedup-collision. Confirm a channel send still continues its id (the v15 fix didn't regress). Confirm `reqpubkey` emits `{"ev":"reqpubkey_sent"}` and a received `-a` DM produces a live `{"ev":"e2e_acked"}` (never `ev:"unknown"`).
