# Mobile reverse E2E-ACK to the originating mobile

**Date:** 2026-07-11
**Status:** Implemented + gated (native 683/683, s18 `3ac88d40…` byte-identical, s07/s21/s09/s15 clean, 4 boards).
**Author context:** MeshRoute C++ firmware. Follows the mobile plane-separation work.

## Problem

A registered mobile `M` stamps `origin = home_id` on every DM it sends (`stamp_origin`), so that
the reverse path can find it. But the transport **E2E-ACK** for a DM `M` originated was addressed at
the DM's `origin` (`send_e2e_ack(to_origin=dec_origin)`), i.e. at the **home**, not the mobile. Three
cases:

1. **M → its own home.** `origin == home_id == the recipient`, so the ACK was a self-send that never
   left. (Fixed earlier by the home last-miling its own mobile — the `sender_hash ∈ _mobile_reg`
   branch of `send_e2e_ack`.)
2. **M → a non-home static X, DIRECT** (M holds an authoritative bind for X): the ACK routed to
   `origin = home_id` → the **home** consumed it; M never learned.
3. **M → a non-home static X, DELEGATED** (M can't resolve X → `MOBILE_SEND` to home → home
   re-originates under its OWN ctr `ctr_H`): X's ACK (for `ctr_H`) routed to the home; M was waiting
   on its own `ctr_M` and never matched.

## Key insight

Because M stamps `origin = home_id`, **the ACK already routes home to H** in all cases. Unlike the
cross-layer ACK (where the 8-bit `origin` aliases across leaves, forcing the stable hash onto the
wire), here H is the ACK's destination. H only needs to (a) **recognize** the ACK belongs to a mobile
it hosts, and (b) know the mobile's ctr. This mechanism mirrors cross-layer's on-wire identity
(chosen for consistency), plus a home-local ctr map for the one case (delegated) where the ctr
differs.

## Design (3 pieces)

### Piece 1 — the acker echoes `source_hash` (wire)
`send_e2e_ack(to_origin, acked_ctr, sender_hash)`: when the acked DM's **sender differs from its
origin** — `sender_hash != 0 && key_hash_of_id(to_origin, oh) && oh != sender_hash` — stamp the ACK
with `DATA_FLAG_SOURCE_HASH = sender_hash` (reuses the existing bit; no new flag — the DATA flags
byte is full). This is the same on-wire identity the 4e cross-layer ACK carries.

**s18 safety:** a *static* sender's `source_hash` always equals `key_hash_of_id(origin)` (the sender
IS the origin), and an *unknown* origin yields `false` — so the gate never fires on a static-only
mesh. The only trip is `origin = home_id, source_hash = M` (a mobile). Verified: s18 md5 unchanged.

### Piece 2 — the home recognizes + last-miles (receive)
`do_post_ack`'s `DATA_TYPE_E2E_ACK` branch:
- read the acked ctr from `ui->body` when a `SOURCE_HASH` (or cross-layer) is present (it shifts the
  ctr past the 4-B hash), else the legacy `inner[1..2]`;
- if the ACK is **same-layer** (`!CROSS_LAYER`) and its `source_hash` names a hosted mobile
  (`∈ _mobile_reg`), re-address it as a **last-mile** DM to the mobile's local id (`addr_len=1`),
  translating the ctr for a delegated send (Piece 3), and **consume** (no `record_ack`/push to the
  home's app). Emits `mobile_reverse_ack`.

This alone fixes the **direct** case (the ACK already carries `ctr_M`).

### Piece 3 — the delegated ctr map (home-local, no wire)
When H re-originates a delegated `MOBILE_SEND` it uses its OWN `ctr_H ≠ ctr_M`. A bounded TTL ring
`_deleg_acks[8]` maps `{acker=X, ctr_H} → ctr_M`:
- **populated** at every re-origination site where `reply_to_hash != 0` (home-for-mobile): the
  immediate authoritative resolve (`send_by_hash`), and both park-drain sites
  (`drain_parked_sends`, `drain_resolved_parked_sends`) — delegated sends commonly park (M couldn't
  resolve X ⇒ H often floods too). `mobile_ctr` (`= ctr_M = pa.ctr` of the inbound `MOBILE_SEND`) is
  threaded through `send_by_hash`/`park_send`/`ParkedSend`.
- **consumed** in Piece 2 via `deleg_ack_translate(acker, ctr_H, &ctr_M)` (one-shot, TTL-pruned). A
  **direct** send has no entry → the ctr passes through unchanged.

TTL = 3 min (well past an e2e-ack round trip). Empty on any node hosting no mobiles → inert.

## End-to-end

- **Direct:** M→H(forward)→X. X echoes `source_hash=M`, acks `ctr_M`. ACK→H; `source_hash=M` hosted,
  map miss → last-mile `ctr_M` to M. ✓
- **Delegated:** M→H(`MOBILE_SEND`)→H re-originates `ctr_H` to X (records `{X,ctr_H}→ctr_M`). X echoes
  `source_hash=M`, acks `ctr_H`. ACK→H; hosted + map hit → last-mile `ctr_M` to M. ✓
- Both rely on X holding an authoritative bind for H (to compute `key_hash_of_id(home_id)`); it
  normally does (H beacons). Absent that, no regression — the ACK falls back to today's behavior
  (lands at the home).

## Files

- `frame_codec.h` — (none; reuses `DATA_FLAG_SOURCE_HASH`).
- `node_mac.cpp` — `send_e2e_ack` echo branch; `enqueue_data` `addr_len` param (last-mile origination).
- `node_mac_rx.cpp` — `do_post_ack` E2E_ACK: ctr-from-`ui->body`, hosted-mobile last-mile + consume;
  MOBILE_SEND fork passes `mobile_ctr = pa.ctr`.
- `node_hashlocate.cpp` — `deleg_ack_put`/`deleg_ack_translate`; `send_by_hash`/`park_send` thread
  `mobile_ctr`; capture `ctr_H` at the 3 re-origination sites.
- `node.h` — `_deleg_acks` ring + `DelegAck`; `ParkedSend.mobile_ctr`; signatures.
- `test/test_dual_layer.cpp` — Piece 1 (echo + static no-echo), Piece 2 (direct last-mile + non-host
  pass-through), Step 3 (map record + ctr translate).

## Known limits (deferred, non-blocking)

- **mobile → mobile** (target is itself a mobile): the re-origination goes via the target's home; the
  ACK returns to the *target's* home, not M's — not recorded. Deferred with the rest of mobile↔mobile.
- **Re-homing mid-flight:** the ACK still targets the `origin=home_id` stamped at send time; a mobile
  that re-homes before the ACK returns is handled by the breadcrumb/redirect plane, not here.
- **Expired delegated entry** (>3 min): a very late delegated ACK last-miles the untranslated `ctr_H`
  (mobile won't match). Harmless; the message was delivered.
