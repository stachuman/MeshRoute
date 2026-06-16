# E2E Peer-Key Provisioning + No-Pubkey UX — Coder Instruction

**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com>
**Date:** 2026-06-16 · **Status:** INSTRUCTION for the coding agent · quality-gated after.

**Goal:** make E2E DM key acquisition **user-driven, never silently automated**, and add the **out-of-band
QR (verified)** path. Two ways a node learns a recipient's pubkey: (a) **QR/manual** — physically scanned,
the MITM-resistant path → a **PINNED** cache entry; (b) **on-air `WANT_PUBKEY`** — TOFU, but now fired
**only on an explicit user request**, never auto. A no-pubkey send **warns the app and drops** (option-1).
Interface = `ios-companion/INBOX_SYNC_CONTRACT.md` (Verified-peer provisioning §). **Keystone: e2e_dm off →
s18 `306c3cf4` byte-identical.**

## Locked decisions (2026-06-16)
- **PINNED** = a 3rd peer-key tier above `authoritative`: **never LRU-evicted, never aged, and NEVER
  overwritten by an on-air answer for the same `key_hash32`** (an on-air grind-collision must not replace a
  scanned key — security, not just UX).
- **NV-persist** pinned keys (`/mrpeers`, the `/mrid` write pattern) — survive reboot, no re-scan.
- **No auto-query:** a no-pubkey CRYPTED send does NOT fire `WANT_PUBKEY`; it warns + drops. The user
  **requests** (on-air) or **provides** (QR).
- Option-1: the drop emits an app-facing `send_failed{reason}` Push (not just telemetry).

## 1. PINNED tier (`node.h` PeerKey + `node_hashlocate.cpp`)
- `PeerKeyConf` (node.h:320) gains `pinned = 2` (> `authoritative = 1` > `overheard = 0`). (Or a separate
  `bool pinned` on `PeerKey` — your call; the enum tier is simplest given the existing `confidence` field.)
- `peer_key_set`: keep upgrade-never-downgrade; **a `pinned` entry is immutable to any non-pinned set**
  (an on-air `authoritative`/`overheard` `peer_key_set` for a pinned hash is a **no-op** — never rewrites
  the ed_pub or downgrades). Same `ed_pub[:4]==key_hash32` verify.
- `peer_key_find` / `peer_key_age_out`: **pinned entries never age out**; the LRU evict-oldest **skips
  pinned** (evict the oldest *non-pinned*; if all 16 are pinned, refuse a new non-pinned insert + telemetry
  `peer_key_full`).

## 2. `/mrpeers` NV store (`src/device_nv.h` + `fw_main.cpp`)
- A small record set `{ key_hash32, ed_pub[32] }[N]` (N≈16), the `/mrid` LittleFS/NVS pattern. Pinned-only
  (on-air keys stay RAM). Write on a `peerkey` install / delete; rate-limit like the identity NV.
- Boot: load `/mrpeers` → `peer_key_set(hash, ed_pub, PINNED)` for each (the device backend; the sim seam
  has no NV → pinned keys come only from the `peerkey` command in a scenario).

## 3. `peerkey` command — QR import (`console_parse` + the companion + `node.cpp`)
- `peerkey <ed_pub hex64>`: parse 64-hex → 32 B; `key_hash32 = ed_pub[:4]`; `peer_key_set(.., PINNED)` +
  `/mrpeers` write. Ack push `{"ev":"peerkey_set","hash":<u32>,"pinned":true}`; on bad hex / `ed_pub[:4]`
  mismatch → `{"ev":"peerkey_err","reason":"bad_hex"|"hash_mismatch"}`, NOT installed. (Console + the BLE
  companion command both route here.)

## 4. `ready.pubkey` export (`fw_main.cpp` / `console_json`)
- The `ready` snapshot JSON gains `"pubkey":"<64 hex>"` = `g_identity.ed_pub` (so the app's `MyCardView`
  emits the QR `p` field; `key_hash32` alone can't seal). A `regen` re-emits `ready`.

## 5. No-auto-query + the `send_failed` warn (`node_mac.cpp:88-118`)
- **REMOVE** the `emit_hash_query(...)` at the `no_pubkey` case (`:102`). The no-pubkey path now:
  `MR_EMIT("e2e_no_pubkey", …)` + **`enqueue_push(Push{send_failed, dst, ctr, reason=no_pubkey})`** + drop.
  NO query.
- **`Push` gains a `reason`** field (enum `no_pubkey · no_identity · too_large · bad_rng · no_route`); set it
  at every `send_failed` site (the existing `too_large` `:109` and no-gateway `node_mac.cpp:244/:252`). Carry
  `reason` in the `send_failed` companion JSON.
- `no_identity`/`bad_rng`/`too_large` already fail loud; add a `send_failed{reason}` Push to each so the app
  always learns (today only `too_large` pushes).

## 6. `reqpubkey` command — user-triggered on-air request (`console_parse` + companion + `node.cpp`)
- `reqpubkey <key_hash32 hex8>`: `emit_hash_query(hash, hard=true, want_pubkey=true)`; ack
  `{"ev":"reqpubkey_sent","hash":<u32>}`. This is the ONLY way `WANT_PUBKEY` fires now (+ a relay forwarding
  one — R4 unchanged).

## 7. `peer_key_cached` Push (`node_hashlocate.cpp` `on_hash_bind_pubkey`)
- `on_hash_bind_pubkey`'s successful `peer_key_set` (the existing `MR_EMIT("peer_key_cached")`) ALSO
  `enqueue_push(Push{peer_key_cached, hash, pinned=false})` → the app prompts "secure send ready — resend"
  after a request resolves.

## 8. ★ Update the t94 E2E scenario (the no-auto-query change BREAKS it)
`t94_e2e_dm_crypto` relied on the auto-`WANT_PUBKEY` to bootstrap. With no-auto, inject explicit
**`reqpubkey`** commands where the auto-query used to fire (alice requests bob's hash before the resend;
bob requests alice's). Re-record its golden `_events.ndjson`. The asserted properties are unchanged
(fail-loud-no-pubkey now also shows `send_failed{no_pubkey}`; ciphertext-opaque; encrypted delivery).

## 9. ★ Keystone + gate
- **s18 `306c3cf4` byte-identical:** all of the above is e2e-path or companion-only — `e2e_dm` defaults off
  → no `send_failed{no_pubkey}`, no `peerkey`/`reqpubkey`, PINNED/`/mrpeers` inert, `ready.pubkey` is a
  companion field absent from the sim event stream. Verify before/after.
- **Native units:** pinned never-overwritten-by-on-air + never-evicted/aged; `peerkey` installs + rejects
  bad-hex/hash-mismatch; no-pubkey send → `send_failed{no_pubkey}` + **NO `h_tx`** (no auto-query);
  `reqpubkey` → one HARD+WANT_PUBKEY `h_tx`; `/mrpeers` NV round-trip (device backend); `peer_key_cached`
  Push on cache insert.
- **Gate:** native + 4 builds (gateway <100%) + s18 byte-identical + **t94 re-recorded & green** (with the
  `reqpubkey` flow) + read the PINNED no-overwrite path by eye (the security-critical bit).
