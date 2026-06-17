# Inbox sync — BLE wire contract (PROPOSED)

The companion catch-up seam between the firmware persistent inbox
(`docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md`) and the iOS app. The firmware **core +
stores** are being built; the **wire commands/pushes** below are not in `command.h` yet — this is the
app side's concrete, tested proposal (`ios-companion/MeshRouteKit`) for the firmware to emit/parse to.
Confirm/adjust, then it's the shared contract.

Framing matches the rest of the link: **app→node = line-ASCII commands, node→app = newline JSON.**

> **Firmware review (2026-06-10):** the epoch/store-reset section is **confirmed** — `inbox_epoch` in the
> `ready` snapshot maps to the firmware's `storage_epoch` (inbox spec §10.1, a hard Phase-2 requirement on
> the device store). **One required change:** channel identity is the **full 32-bit `channel_msg_id`**, not
> `ctr`. Phase 1 now stores the whole id (`InboxEntry.msg_id`, u32) — so dedup channels **exactly** by it,
> and **drop the `ctr` + body-tiebreaker workaround** (it was only there because the low-8 ctr wraps). DM
> identity stays `(origin, ctr)`. Applied inline below. (Other agreements: pull order DM-block-then-channel
> == `Inbox::pull`; `rx_ms` == `InboxEntry.rx_time_ms`; DM `ctr` == the firmware's `msg_id` for a DM.)

## Commands (app → node)

```
pull_inbox <dm_since> <chan_since>     # stream records with seq > each cursor; two INDEPENDENT seq spaces
mark_read  <dm|chan> <seq>            # advance the per-store read cursor (UX unread badges)
```
- The app holds one cursor per store and advances each to the highest `seq` it receives.
- `pull_inbox 0 0` = full history. Live `msg_recv`/`channel_recv` still deliver in real time; pull is
  only on-connect / been-away catch-up.

### Live + pull unified by `seq` — chosen model ("B", 2026-06-12)

The app keeps **one high-water per store**, advanced by BOTH live pushes and pull responses (it *is* the
`pull_inbox` cursor). For each live push carrying `seq`:
- `seq == high+1` → contiguous → apply + advance.
- `seq > high+1` → **gap** (a live push was dropped — the push ring is bounded/drop-oldest) → `pull_inbox <high> …` to backfill, then apply.
- `seq <= high` → already held (live/pull overlap, or an epoch re-pull) → dedup by stable identity.
- `seq` **absent** (the node's inbox is disabled) → best-effort live only; no gap-pull (nothing to pull from).

So a message dropped while connected is recovered **immediately** (the next push exposes the gap), not only
on reconnect. `pull_inbox`-on-connect stays the been-away catch-up; the live `seq` is the while-connected gap
detector. (Chosen over "A" = best-effort-live + reconcile-only-on-reconnect.)

## Pushes (node → app)  — one JSON object per line

```json
{"ev":"inbox_dm","seq":42,"origin":2,"layer_id":5,"ctr":7,"sender_hash":3735928559,"rx_ms":123456,"enc":true,"body":"…"}
{"ev":"inbox_channel","seq":7,"origin":4,"layer_id":5,"channel_id":3,"channel_msg_id":68298753,"rx_ms":123456,"body":"…"}
{"ev":"inbox_end","dm_seq":42,"chan_seq":7,"epoch":3,"count":15,"now_ms":987654}
```
- Emit the **DM block then the channel block** (matches `Inbox::pull`'s order), each oldest-first, then
  `inbox_end` with the newest seq per store + the number streamed (+ the `epoch` it was served under, so a
  mid-pull wipe is detectable).
- `seq` = the per-store cursor (`InboxEntry.seq`). `rx_ms` = `rx_time_ms` (node uptime; the **app**
  stamps wall-clock on pull). `channel_msg_id` = the full 32-bit `InboxEntry.msg_id` for a channel entry
  (`origin<<24 | key_hash16<<8 | ctr`); `origin` is also sent for display (== `channel_msg_id >> 24`).
- **DM identity (2026-06-11):** firmware now stores **`sender_hash`** = the sender's `key_hash32` (the DATA
  `SOURCE_HASH` field, default-on for app DMs) — the **stable** sender id (the 8-bit `origin` is reassignable).
  `ctr` (16-bit) is the firmware's `msg_id`. Dedup a **DM** by **`(sender_hash, ctr)` when `sender_hash != 0`,
  else `(origin, ctr)`**. (`sender_hash` is `0`/omitted only for legacy/non-`SOURCE_HASH` DMs.)
- These mirror `console_json.cpp`'s existing writers; the natural firmware shape is a
  `write_inbox_entry(buf, cap, const InboxEntry&)` paralleling `write_push`.
- **`layer_id` (2026-06-13, dual-layer gateway §2/Q13):** the **full 8-bit receiving `layer_id`** the message
  arrived on. A **gateway** is a member of two layers on one identity, so the 8-bit `origin` aliases across its
  two leaves — `(origin, ctr)` alone can't tell layer 7's node 5 from layer 39's node 5. The firmware now stamps
  the receiving `layer_id` on EVERY DM/channel record + live push (a normal single-layer node sends its one
  `leaf_id`, so existing behaviour is unchanged — just an added field). The app may thread it into the
  conversation/display; DM dedup identity is **unchanged** (`(sender_hash, ctr)`/`(origin, ctr)`) — `layer_id`
  is informational routing context, not part of the identity key.

## Epoch & store-reset handling (FIRM)

`seq` is monotonic only **within an epoch**. A flash wipe (bootloader re-flash erasing QSPI, or a
format-on-dirty recovery — spec §10/§14) restarts seq at 1, so a node we'd synced to cursor 500 would
re-emit new messages at seq 1,2,3 — all < 500 — and a naive `seq > 500` would **silently miss them**.
So the sync layer:

1. Tracks **per node** `{ epoch, dm_cursor, chan_cursor }` — cursors are meaningful only within an epoch.
2. Reads the node's **inbox epoch** on connect. **Proposed home:** `"inbox_epoch":N` in the `ready`
   snapshot (`{"ev":"ready",…,"inbox_epoch":3}`). The node bumps it on any store reset. Exact
   field/command is **TBD** with the companion phase — the sync layer is abstracted over it.
3. If the epoch changed (or first sync of this node): **reset both cursors to 0 and re-pull the whole
   inbox**.
4. **Dedup on import** by the **stable message identity** against the durable archive, so the re-pull-from-0
   merges into existing history instead of duplicating — a **channel** message by its full 32-bit
   `channel_msg_id` (exact; no body tiebreaker needed now the firmware sends the whole id), a **DM** by
   **`(sender_hash, ctr)` when `sender_hash != 0`, else `(origin, ctr)`** (`sender_hash` = the sender's stable
   `key_hash32`, now sent on every app DM). `seq`/`epoch` are deliberately NOT part of identity.
5. Advance each cursor to the max seq received for its store; persist `{ epoch, dm_cursor, chan_cursor }`.

Implemented + tested app-side: `InboxSyncState.beginSync(nodeEpoch:)`, `MessageIdentity`,
`ConversationStore.ingestInbox`, and `NodeProfileEntity.syncState` (see `AppModel.startInboxSync`).

## Firmware asks (small — so live pushes share the pulled inbox's identity keys + a live cursor)

So a message seen **live** and later **pulled** dedups on the same key **and** the client can detect a
*missed* live push (model "B" above), the live pushes need the inbox's identity fields **and its `seq`**:
```json
{"ev":"channel_recv","origin":4,"layer_id":5,"channel_id":3,"channel_msg_id":68298753,"seq":7,"body":"…"}   // channel_msg_id + seq + layer_id are new
{"ev":"msg_recv","origin":2,"layer_id":5,"ctr":7,"sender_hash":3735928559,"seq":42,"body":"…"}              // sender_hash + seq + layer_id are new (+ `enc` — see §Per-message crypt)
```
Identity fields are at hand: `node_channel.cpp` passes the full `channel_msg_id` to `record_channel`, and
`do_post_ack` has `sender_hash` (the parsed `source_hash`) right at the `msg_recv` push. **`seq`** is the
inbox record's per-store seq — so the firmware must **record BEFORE it pushes**: `record_dm`/`record_channel`
return the assigned seq, and `do_post_ack` / `ingest_channel_m` call them *before* `enqueue_push` (today the
push is enqueued *first* — flip the order, or stamp the seq into the already-built push). **`seq` is present
only when the inbox is enabled** (the device store is up); **absent/`0` ⇒ no durable store ⇒ best-effort
live only, no gap-pull.** Each field adds a `u32` to the `Push` POD + its `console_json` writer — a Phase-3
task, landed with the companion pushes. Without the identity fields a live+pulled message duplicates;
without `seq` a dropped live push is invisible until the next reconnect.

## Hardening asks for the inbox-hardening agent (decisions D7 + D10, roadmap 2026-06-12)

From `docs/superpowers/specs/2026-06-12-companion-product-roadmap.md` (user-ratified decisions):

1. **D7 — persist the DM `ctr` across reboots** (NV, the epoch+RAM write pattern from the identity
   spec §4.4 — rate-limited flash writes). Why: dedup identity is `(sender_hash, ctr)`; today a
   sender reboot restarts `ctr` at 1, so its next messages REUSE identities the app has already
   archived and are **silently deduped away**. Persisting `ctr` closes this with zero wire cost.
   (Observed live on the bench 2026-06-12 — small ctrs collide constantly under reflash cycles.)
2. **D10/D14 — two companions: WARN, don't design for it.** Read state is per-phone, app-side (the
   app does not send `mark_read` in v1); node-side `mark_read` stays a simple hint — do NOT build
   per-bond cursors. The only multi-phone behavior is a warning: when more than one companion is
   bonded, the app shows "multiple phones paired — sync behavior is undefined" (cheap mechanism:
   `ready` gains a `bonds:N` count; lands with the notification slice).

## Open / deferred (match the inbox spec §8, §14)

- **No `delete`** in v1 (node self-manages via drop-oldest). Not in this contract.
- `mark_read` reply: a `{"ack":…}` or nothing — app doesn't depend on one. Firmware's choice.
- ~~Absolute time: deferred~~ **DONE (2026-06-12, Theme A):** `ready` + `inbox_end` carry `"now_ms"`
  (node uptime at emit). The app anchors it against its wall clock at decode
  (`NodeTimeAnchor`: `wall(rx_ms) = capturedAt − (now_ms − rx_ms)`) so pulled records get TRUE receive
  times; absent field (older firmware) → pull-time stamping as before. A reboot resets uptime AND
  bumps the epoch, so an anchor never spans a reboot.

## App-side reference (already implemented + tested)

`ios-companion/MeshRouteKit`: `Command.pullInbox/.markRead`, `Inbound.inboxEntry/.inboxEnd`,
`InboxEntry` (with `senderHash` + `channelMsgID`), `InboxSyncState` (epoch + cursors), `MessageIdentity`
(`.dmByHash(hash,ctr)` / `.dmByID(origin,ctr)` / `.channel(msgID)`), `ConversationStore.ingestInbox`
(dedup vs live + re-pull; DM threads key by `sender_hash` → straight into the contact, no resolve), and
`MockNodeLink` serves `pull_inbox` + `inbox_epoch` + `sender_hash`/`channel_msg_id`/`seq` on live pushes.
**Model B (live-while-connected):** live `msg_recv`/`channel_recv` carry `seq`; `InboxSyncState.classifyLive`
(contiguous / gap / duplicate) + `AppModel.applyLiveSeq` pull-backfill on a gap and advance the cursor — so
a push dropped from the bounded ring is recovered immediately, not only on reconnect. App:
`NodeProfileEntity.syncState`, `AppModel.startInboxSync`. Aligned to the 2026-06-10/11/12 firmware reviews
(channel = 32-bit `channel_msg_id`; DM = `(sender_hash, ctr)`; `inbox_end.epoch`; live `seq` high-water).
Run `swift test --scratch-path /private/tmp/mrk-build` (58 tests).

## Verified-peer provisioning — QR pubkey exchange (B2 / E2E) — PROPOSED 2026-06-16

The QR contact card (`MeshRouteCore/ContactCard.swift`: `…/c?v=1&h=<hex8>&n=<name>[&p=<ed_pub hex64>]`)
already reserves **`p`** for the full pubkey. This is the **out-of-band, MITM-resistant** key path
(a physical scan is the trust ceremony) — distinct from, and stronger than, the on-air `WANT_PUBKEY`/TOFU
resolution (which is explicitly NOT MITM-secure, identity-spec §2 [xcheck]). The app stays crypto-free
(D6) — it only ferries opaque hex. Two interface additions:

### node → app: export the node's own pubkey (so `MyCardView` can emit `p`)
`key_hash32` (4 B, `ed_pub[:4]`) can't seal — the app needs the **full** `ed_pub`. The `ready` snapshot
gains it (`key` stays for display/routing):
```json
{"ev":"ready", … ,"key":3735928559,"pubkey":"<64 hex ed_pub>"}
```
- `MyCardView` builds `ContactCard(name:, hash: key, pubkeyHex: pubkey)` → the QR now carries `p`.
- `regen` changes the identity → the firmware re-emits `ready` (or a `{"ev":"identity","pubkey":…}` push)
  so the card refreshes.

### app → node: install a scanned peer's pubkey (PINNED / verified)
```
peerkey <ed_pub hex64>      # install a verified peer key from a scanned card. hash derives = ed_pub[:4].
```
- Firmware: 64-hex → 32 B; `key_hash32 = ed_pub[:4]`; `peer_key_set(key_hash32, ed_pub, PINNED)` (which
  re-verifies `ed_pub[:4]==key_hash32`). **PINNED = a new tier above `authoritative`: never LRU-evicted,
  never aged, and NEVER overwritten by an on-air `WANT_PUBKEY` answer for the same hash** — else an attacker
  who grinds a colliding 32-bit hash and answers on-air could replace the scanned key (defeating the
  ceremony). **NV-persisted** (a small `/mrpeers` store, the `/mrid` write pattern) so a verified contact
  survives reboot without re-scanning. The name is NOT sent (names are app-side; the firmware key cache is
  keyed by hash).
- Ack (node → app):
```json
{"ev":"peerkey_set","hash":3735928559,"pinned":true}             // installed
{"ev":"peerkey_err","reason":"bad_hex"|"hash_mismatch"}          // rejected, not installed
```
- App: when a scanned `ContactCard` has `pubkeyHex`, send `peerkey <p>` alongside the app-side `addContact`.
  A first encrypted DM to a pinned contact then **seals immediately** — no `WANT_PUBKEY` round-trip, no
  option-1 fail-loud drop.

### on-air key request — USER-TRIGGERED (decided 2026-06-16: no silent automation)
The firmware does **NOT** auto-flood `WANT_PUBKEY` on a failed encrypted send. On a no-pubkey send it
**warns the app and drops** (`send_failed` below); the user then either **requests** the key on-air or
**provides** it via QR. On-air resolution is thus an explicit action:
```
reqpubkey <key_hash32 hex8>     # fire ONE HARD WANT_PUBKEY for this hash (the "request key" UX action)
```
- Firmware: `emit_hash_query(hash, hard=true, want_pubkey=true)`; ack `{"ev":"reqpubkey_sent","hash":…}`.
- **Mutual (Slice 2, implemented 2026-06-17):** the WANT_PUBKEY H **always appends the requester's OWN pubkey**
  (the 8→40-B H), so ONE request provisions BOTH directions: the **owner caches the requester** (key + id_bind)
  before answering, and the requester caches the owner from the TYPE-5 answer. This is the bootstrap before any
  sealed DM flows. The request rides the **cleartext flood**, so the attached pubkey is **visible to every relay**
  — the deliberate "establishing contact" exposure (everything after is sealed). App command unchanged. A
  reqpubkey from a node with **no crypto identity fails loud** (no flood) — provision an identity first.
  *Directed-when-route-known is deferred.*

### UX pushes (node → app)
```json
{"ev":"send_failed","dst":2,"ctr":7,"reason":"no_pubkey"}     // a CRYPTED send was DROPPED — warn + offer Request-key / Scan-QR
{"ev":"peer_key_cached","hash":3735928559,"pinned":false}    // a key arrived (request answer / cache-on-pass / QR / mutual) → enable resend
```
- `send_failed.reason` ∈ `no_pubkey · no_identity · too_large · bad_rng · no_route`. App maps `no_pubkey`
  → "recipient's key unknown — Request key / Scan QR"; permanent reasons (`too_large`/`no_route`) → plain fail.
- `peer_key_cached` lets the app prompt "secure send ready — resend" after a request resolves (or QR import).
- **Mutual source (Slice 2):** you ALSO get `peer_key_cached` for the **requester's** hash when you ANSWER a
  contact's `reqpubkey` — you cached *their* key during the handshake, so you can now securely reply to them
  (no separate request needed). Same event/shape; the `hash` is the contact who just reached out.

### Per-message crypt + the "encrypted?" indicator (2026-06-16)
**Send — crypt is PER-MESSAGE**, not only the global `cfg set e2e_dm` default: the companion's send carries
an explicit crypt bit (the UX lock toggle); `e2e_dm` is the **default** applied when the send doesn't
specify. (Send form CONFIRMED 2026-06-16: a **`sendhashx` / `sendhashx_ack`** verb pair beside
`sendhash`/`sendhash_ack`; the seal gate uses `want_crypt = per_message ?? e2e_dm`.) A CRYPTED send with no authoritative key still fails loud
(`send_failed{no_pubkey}`).

**Receive — every delivered DM tells the app whether it was sealed.** A DM opened from a CRYPTED frame carries
**`"enc":true`** on BOTH the live `msg_recv` and the pulled `inbox_dm` (the app shows a lock). The field is
**OMITTED for a plaintext DM** — *absent ⇒ `false`*, the SAME convention as `seq`. (Implemented 2026-06-16 as
omit-when-false, NOT always-present, so the e2e-off event stream — incl. the `s18` golden trace — stays
byte-for-byte unchanged.)
```json
{"ev":"msg_recv","origin":2,"layer_id":5,"ctr":7,"sender_hash":3735928559,"seq":42,"enc":true,"body":"…"}   // sealed
{"ev":"msg_recv","origin":2,"layer_id":5,"ctr":7,"sender_hash":3735928559,"seq":43,"body":"…"}              // plaintext (enc OMITTED)
{"ev":"inbox_dm","seq":42,"origin":2,"layer_id":5,"ctr":7,"sender_hash":3735928559,"rx_ms":123456,"enc":true,"body":"…"}
```
- **App rule:** treat a MISSING `enc` as `false`. Only `enc:true` is ever emitted.
- Source: a delivered DM had `DATA_FLAG_CRYPTED` set AND opened (a CRYPTED frame that fails to open never
  delivers ⇒ `enc:true` ⇔ delivered-sealed). A plaintext DM omits `enc`.
- **Channels (later):** `channel_recv`/`inbox_channel` likewise OMIT `enc` (cleartext today ⇒ false); the
  field is reserved for a future channel-crypto phase.

### Receiving a sealed DM you can't open — silent DROP (sealed-sender redesign, 2026-06-16)
> **Supersedes the earlier "locked + auto-recover" model** (now dead). The originator is **sealed inside the
> ciphertext** (privacy: a relay must never learn who sent a DM), so an un-openable DM is **un-attributable** —
> there is no `sender_hash` to name, hence no "Request key from X", no `locked` inbox state, no ciphertext at rest.

- A CRYPTED DM the node can't decrypt (no cached key opens it under trial decryption) is **dropped silently** —
  **no push, no ack, no inbox entry.** There is **no per-message recovery**; the sender's retry after the
  handshake completes re-delivers. **Recovery is the handshake, not the message.**
- **Provisioning happens FIRST**, via the **mutual `reqpubkey` handshake** (below) or a QR `peerkey` — so both
  sides hold both keys before any sealed DM flows, and every delivered sealed DM opens.
- **No E2E-ack ⇒ "not delivered OR not decrypted"** (undifferentiated). The sender retries / re-handshakes. A
  receiver that can't decrypt can't identify the sender, so it cannot (and must not) NACK — **silence is the
  only signal.**
- The `enc` indicator (above) is unchanged — a *delivered* DM was sealed ⇒ `enc:true`; plaintext omits it.
  There is **no** third "locked" state; a DM is plaintext or `enc:true`.

### Deferred
- `peerkeys` (list pinned) + `peerkey_del <hash>` (un-verify) — app-side contact management; v2.
