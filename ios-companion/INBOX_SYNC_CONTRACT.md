# Inbox sync — BLE wire contract (PROPOSED)

The companion catch-up seam between the firmware persistent inbox
(`docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md`) and the iOS app. **STATUS (2026-06-29): the
firmware side is IMPLEMENTED + verified against code** — the send / pull / inbox / `ready` / duty / e2e /
provisioning commands + pushes below are live in `lib/console/console_parse.cpp`, `lib/console/console_json.cpp`,
and `src/fw_main.cpp`. This is the shared contract; the few items still open are tagged inline (`reqpubkey_sent`
event, `ready.bonds`, the D7 DM-`ctr` persistence).

Framing matches the rest of the link: **app→node = line-ASCII commands, node→app = newline JSON.**

> **Firmware review (2026-06-10):** the epoch/store-reset section is **confirmed** — `inbox_epoch` in the
> `ready` snapshot maps to the firmware's `storage_epoch` (inbox spec §10.1, a hard Phase-2 requirement on
> the device store). **One required change:** channel identity is the **full 32-bit `channel_msg_id`**, not
> `ctr`. Phase 1 now stores the whole id (`InboxEntry.msg_id`, u32) — so dedup channels **exactly** by it,
> and **drop the `ctr` + body-tiebreaker workaround** (it was only there because the low-8 ctr wraps). DM
> identity stays `(origin, ctr)`. Applied inline below. (Other agreements: pull order DM-block-then-channel
> == `Inbox::pull`; `rx_ms` == `InboxEntry.rx_time_ms`; DM `ctr` == the firmware's `msg_id` for a DM.)

## Commands (app → node)

> ⚠️ **UPDATE REQUIRED — send verbs changed (firmware 2026-06-21, spec `2026-06-21-serial-interface-cleanup.md` §2).** The 9 send verbs collapsed to **3 with a QUOTED body + `-a`/`-e` flags**; the old `send_ack`/`sendhash`/`sendhash_ack`/`sendhashx`/`sendhashx_ack`/`send_layer_ack` are **REMOVED** (a node now returns `unknown_verb`). Migrate `MeshRouteWire`/`Command.swift` in lock-step:
> ```
> send <id|hash> "<text>" [-a] [-e]          # id (<=254) vs hash (8-hex) AUTO-detected; -a=ack, -e=encrypt (hash only)
> send_channel <ch> "<text>"                 # no ack/enc
> send_layer <hash> <l1,l2,…> "<text>" [-a]  # explicit cross-layer path
> ```
> Crypt: `-e` ⇒ CRYPTED; **absent ⇒ the node's `e2e_dm` default** (the old `sendhash` force-PLAIN semantic is dropped — `cfg set e2e_dm off` + no `-e` = plain). Ack: `-a` ⇒ E2E-ack-req (valid on `send`/`send_layer`). The emitted intents (ack/crypt/hash) are unchanged — only the wire syntax. The §"Per-message crypt" block below (which named `sendhashx`/`sendhashx_ack`) is superseded by `-e`.

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
{"ev":"inbox_dm","type":"e2e_ack","seq":43,"origin":2,"layer_id":5,"ctr":7,"sender_hash":3735928559,"rx_ms":124000,"body":""}  // E2E-ack RECEIPT (no body)
{"ev":"inbox_channel","seq":7,"origin":4,"layer_id":5,"channel_id":3,"channel_msg_id":68298753,"rx_ms":123456,"body":"…"}
{"ev":"inbox_end","dm_seq":43,"chan_seq":7,"epoch":3,"count":15,"now_ms":987654}
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
- **`type` (2026-06-23, E2E-ack receipts):** a DM record's optional **`type`** distinguishes a received MESSAGE from a
  delivery RECEIPT. **Absent / `0`** ⇒ a normal received DM (render it, as today). **`"e2e_ack"`** ⇒ a RECEIPT for a
  `-a` DM **this** node sent: `origin` = the node that **CONFIRMED** delivery (the original `-a` DM's recipient),
  `ctr` = the **acked** ctr, `body` empty. The app matches **`(origin, ctr)`** — or **`(sender_hash, ctr)`** when
  `sender_hash != 0` (a cross-layer ack: the 8-bit `origin` aliases across leaves, so the hash is the stable key) — to
  its **OUTBOX** and marks that sent message **DELIVERED**; it must **NOT** render a receipt as an inbound message.
  Receipts ride the **DM seq-cursor** (no new block / arg / cursor). There is also a non-durable **live fast-path**
  console line `E2E-ACKED ctr=<X> from=<D>` (the connected/harness case). ⚠ **Rollout:** the contract change rides the
  same `pull_inbox`, so an **un-updated** companion would mis-show a receipt as an empty-body DM — coordinate the update.

## Epoch & store-reset handling (FIRM)

`seq` is monotonic only **within an epoch**. A flash wipe (bootloader re-flash erasing QSPI, or a
format-on-dirty recovery — spec §10/§14) restarts seq at 1, so a node we'd synced to cursor 500 would
re-emit new messages at seq 1,2,3 — all < 500 — and a naive `seq > 500` would **silently miss them**.
So the sync layer:

1. Tracks **per node** `{ epoch, dm_cursor, chan_cursor }` — cursors are meaningful only within an epoch.
2. Reads the node's **inbox epoch** on connect — **DONE:** `"inbox_epoch":N` is in the `ready`
   snapshot (`{"ev":"ready",…,"inbox_epoch":3}`, `console_json.cpp:237` = the firmware's `storage_epoch`).
   The node bumps it on any store reset.
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

1. **D7 — persist the DM `ctr` across reboots — ⚠ STILL OPEN (verified 2026-06-29).** Why: dedup identity
   is `(sender_hash, ctr)`; today a sender reboot restarts `ctr` at 1, so its next messages REUSE identities
   the app has already archived and are **silently deduped away**. **Current state:** only the *self-keyed*
   `channel_ctr` is NV-persisted (blob v15, the lease-write pattern at `fw_main.cpp:1811-1836`); the per-peer
   DM counters `_peer_send_counter[dst]` (`node_mac.cpp` `next_ctr`) still reset to 0 on reboot — so D7 is
   **unfixed for DMs**. The fix = persist the per-peer DM ctr with the same rate-limited write pattern (zero wire cost).
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
{"ev":"peerkey_err","reason":"bad_hex"|"full"}                   // rejected (full = peer-key cache full; bad_hex = parse/length)
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
- Firmware: `emit_hash_query(hash, hard=true, want_pubkey=true)` (`node.cpp:831`). The verb returns the dedicated event `{"ev":"reqpubkey_sent","hash":<key_hash32>}` (`fw_main.cpp:1503` → `write_reqpubkey_sent`, landed 2026-06-29). The **no-crypto-identity** failure path keeps its existing error ack (fails loud, no flood — see below).
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
- `send_failed.reason` ∈ `no_pubkey · no_identity · too_large · bad_rng · no_route · joining · no_cts · no_ack`. App maps `no_pubkey`
  → "recipient's key unknown — Request key / Scan QR"; permanent reasons (`too_large`/`no_route`) → plain fail.
- `peer_key_cached` lets the app prompt "secure send ready — resend" after a request resolves (or QR import).
- **Mutual source (Slice 2):** you ALSO get `peer_key_cached` for the **requester's** hash when you ANSWER a
  contact's `reqpubkey` — you cached *their* key during the handshake, so you can now securely reply to them
  (no separate request needed). Same event/shape; the `hash` is the contact who just reached out.

### Anti-spam v2 feedback — advisory `limits` + actual send-outcome (2026-06-30)

`limits` (the query below) lets the app *predict* + pace; the three pushes after it report the *actual* outcome so it backs off. All are **local** (node → its own trusted companion; no OTA change — the node infers from what it already observes). NB the node's `send_blocked` *telemetry* is stripped on device (`MESHROUTE_NO_TELEMETRY`), so on metal the **push** below is the only send_blocked signal the companion receives.

**The `limits` query** (app → node `limits`; node → app one line — the advisory snapshot the app paces against):
```json
{"ev":"limits","win_ms":300000,"win_left_ms":142000,"n":40,"ch_sf":7,
 "ch_cap":8,"ch_used":2,"ch_min_ms":10000,"ch_next_ms":0,"ch_ceiling":42,
 "dm_min_ms":3000,"dm_next_ms":1200,"duty_ms":3000,"duty_used_ms":640}
```
- `win_ms` = the 5-min anti-spam window; `n` = mesh size the per-origin channel cap divides by; `ch_sf` = the DATA-M SF the cap is priced at.
- **channel:** `ch_cap` = this origin's per-window channel cap; `ch_used` = own distinct floods held this window; `ch_min_ms` = the channel burst floor; `ch_next_ms` = ms until a channel post is allowed (0 = now); `ch_ceiling` = C, the total duty-afforded channel capacity (0 = duty disabled → the legacy flat cap).
- **DM:** `dm_min_ms` = the own-DM burst floor; `dm_next_ms` = ms until an own DM is allowed.
- **duty:** `duty_ms` = the 5-min channel-duty budget D (0 = duty disabled); `duty_used_ms` = airtime spent this window.
- `*_next_ms` fold the burst-floor remaining, the channel window cap-wait, AND duty recovery — the true "ready in N ms". ★ `ch_min_ms` / `dm_min_ms` (and, via the fraction, `ch_cap`) are the **leaf's configured** values (see *anti-spam leaf tunables* under Leaf-config provisioning), not fixed firmware constants — so pacing reflects the actual leaf policy.

The outcome pushes:

```json
{"ev":"send_blocked","kind":"channel","reason":"min_interval","next_ms":7300}   // THIS node's own cap/floor blocked the origination pre-TX — hold + retry after next_ms
{"ev":"send_blocked","kind":"dm","reason":"cap","next_ms":0}                     // kind ∈ channel|dm ; reason ∈ cap|min_interval ; next_ms = ms until allowed (0 = floor passed, cap/duty blocks)
{"ev":"send_failed","dst":2,"ctr":7,"reason":"no_cts"}                           // a DM gave up after CTS-timeout retries (1st-hop backstop-drop / no route surfaces here too)
{"ev":"send_failed","dst":4,"ctr":9,"reason":"no_ack"}                           // a DM gave up after DATA-ACK-timeout retries
{"ev":"channel_sent","ctr":5,"relayed":true}                                     // an OWN channel post: a relay was overheard (origin re-offer confirmed) = success
{"ev":"channel_sent","ctr":6,"relayed":false,"reason":"no_relay"}               // the re-offer exhausted with no relay (1st-hop throttle or no neighbour)
```

- The app treats **`send_blocked` / `send_failed` / `channel_sent{relayed:false}`** as **stop-and-back-off** (don't keep firing) and **`e2e_acked` / `channel_sent{relayed:true}`** as success.
- **Enforcement is the 1st hop's** (it applies its own per-origin cap with its own `N`) **plus this node's self-gate**, so a send can still be rejected *after* the companion thought `limits` allowed it — hence the actual outcome, not just the advisory prediction.
- `send_failed.reason` for a DM giveup ∈ `no_cts · no_ack` (this node's cascade exhausted CTS-/ACK-timeout retries). The 1st-hop's *silent* backstop drop surfaces as `no_cts` (conflated with no-route — the app's reaction, back-off-and-retry, is identical). The OTA silent-drop is KEPT (an explicit reject frame would cost airtime + help a spammer calibrate).

### Per-message crypt + the "encrypted?" indicator (2026-06-16)
**Send — crypt is PER-MESSAGE**, not only the global `cfg set e2e_dm` default: the companion's send carries
an explicit crypt bit (the UX lock toggle); `e2e_dm` is the **default** applied when the send doesn't
specify. (Send form UPDATED 2026-06-21: the per-message crypt bit is the **`-e` flag** on `send <hash> "…" -e`
— the old `sendhashx`/`sendhashx_ack` verbs are REMOVED, see the UPDATE-REQUIRED banner under "Commands"; the
seal gate still uses `want_crypt = per_message ?? e2e_dm`.) A CRYPTED send with no authoritative key still fails loud
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

## Leaf-config membership + provisioning (firmware R6.1–R6.3 DONE; companion surface PROPOSED 2026-06-21)

R6 adds **managed leaves**: a fresh node sets a small radio floor, then **auto-joins** (DAD an id) and **auto-pulls its leaf config** — data SFs / duty / name — from the network. The operator never hand-sets the data config on a joiner; only the *rendezvous floor* (freq + control SF + leaf) is manual. Firmware how-to: `docs/LEAF_PROVISIONING.md`. The R6 firmware (R6.1–R6.3) is committed/gated; the **companion JSON surface below is DONE** - the 3 console_json additions + the join_refused push + the join/create/leave verbs (coder green-shaped, gate-pending).

### Node → app: membership state (which leaf, synced?)
A node's leaf membership = `lineage_id` (u16; **0 = unmanaged / standalone**), `config_epoch` (u16), `leaf_name` (string), `level_id` (1..255; the wire leaf nibble = `level_id & 0x0F`), and **synced** (`lineage==0 || epoch>0`). Two carriers:

1. ✅ **`ready` snapshot gains them** (firmware ask — add to the `ready` writer):
```json
{"ev":"ready", … ,"lineage":41153,"epoch":3,"leaf":"north field","level":2,"synced":true}
```
- `lineage:0` ⇒ app shows "unmanaged / standalone". `lineage≠0 & synced:false` ⇒ "joining…". `synced:true` ⇒ "member of <leaf>".
- *Note:* `level` (full 1..255) lands with the provisioning-verbs spec (which stores it); until then the firmware can send the wire **leaf nibble** (`leaf_id`, 0..15) under `level` — the app should treat it as an opaque label.

2. ✅ **`config_adopted` live push — DONE** (`console_json.cpp:157-163` reads `g_node.config()`; fired by the node at `node_query.cpp:203/219`). Fires when the node adopts/updates its leaf config (on join, on a propagated operator write, on an LWW change):
```json
{"ev":"config_adopted","lineage":41153,"epoch":3,"leaf":"north field","level":2}
```
- App: refresh the node's membership chip live ("synced to 'north field'").

### Node → app: a send blocked because not-yet-joined
✅ `send_failed.reason` gains **`joining`** — **DONE** (`SendFailReason::joining` at `command.h:99`, mapped by `sendfailreason_name` at `console_json.cpp:86`):
```json
{"ev":"send_failed","dst":2,"ctr":7,"reason":"joining"}   // managed leaf not yet config-synced — the participation gate
```
- App maps `joining` → **transient**: "still joining the network — retry shortly" (NOT a permanent fail like `no_route`; the gate lifts automatically once the config is pulled, then a `config_adopted` arrives). **Updated reason set:** `no_pubkey · no_identity · too_large · bad_rng · no_route · joining`.

### ✅ Node → app: the node CAN'T join — reason-coded `join_refused` (new push)
A node that refuses/can't join surfaces it (today a wire mismatch is telemetry-only ⇒ **invisible on metal**). New `PushKind::join_refused`:
```json
{"ev":"join_refused","reason":"wire_version","their_ver":2,"my_ver":1}   // the network's wire protocol is incompatible
{"ev":"join_refused","reason":"leaf_full"}                               // no free node id on this leaf
```
- `reason` ∈ `wire_version · leaf_full` (extensible). App: **`wire_version`** → a **blocking** "update firmware to match the network (wire v\<their_ver\>)" — the node will NOT join until updated; **`leaf_full`** → "this leaf is full — no address available". The node stays unjoined until resolved (it keeps retrying, so a later success/`config_adopted` clears the banner).
- Firmware: `wire_version` is detected from a beacon's version nibble (+0 B, version-stable), `leaf_full` from the DAD id picker (`docs/superpowers/specs/2026-06-21-leaf-provisioning-console-verbs.md` §7c).

### App → node: provisioning verbs (spec `docs/superpowers/specs/2026-06-21-leaf-provisioning-console-verbs.md`)
For a "Join network / Create leaf / Leave" UI. All apply **live — no reboot**:
```
join   <freq_MHz> <bw_kHz> <ctrl_sf> <level_id>                                   # join existing net: sets floor, auto-DADs id, auto-pulls config
create <freq_MHz> <bw_kHz> <ctrl_sf> <level_id> <sf_list> <duty%> "<leaf name>"   # mint a managed leaf — this node becomes the mother
leave                                                                             # reset membership (wipe to default, KEEP freq)
```
- `level_id` user-facing (1..255), wire nibble = `level_id & 0x0F`. `sf_list` = comma SFs (`7,9`). `duty` = a **percent**. CR is a fixed low default (4/5) — LoRa CRs interoperate, so it's not exposed.
- After `join`/`create`, the node emits `config_adopted` + updated `ready` membership once it syncs. After `leave`, membership returns to `lineage:0` (unmanaged). A `send` before sync ⇒ `send_failed{reason:"joining"}`.
- **Normal nodes only.** Gateways provision differently (multi-layer; a future `join_as_gateway`) — out of this contract.
- Ack shape = firmware's choice; recommend a `{"ev":"join_ok"|"join_err","reason":…}` line per verb (consistent with the other command acks).

### App → node: anti-spam leaf tunables (2026-07-03 — promoted to leaf config)
The anti-spam v2 knobs below are now **per-leaf config** (carried in the C config frame + folded into the `config_hash`), not fixed firmware constants — so a mother provisions them and a change **re-fingerprints** the leaf (members re-pull → `config_adopted`). Set via `cfg set` (applies **live**, is **persisted to NV** so it survives reboot, and on a **managed** leaf bumps `config_epoch` + re-advertises):
```
cfg set active_fraction <0..1>   # channel-cap fairness divisor (default 0.125): how aggressively the per-origin channel cap shares the mesh's channel capacity C
cfg set ch_min_ms <ms>           # channel burst floor (default 10000): min spacing between one origin's channel floods
cfg set dm_min_ms <ms>           # own-DM burst floor (default 3000): anti-per-keystroke — e2e-ack / rcmd are exempt
```
- All three are in the `config_hash`: changing one on a **mother** propagates to members (they re-pull + emit `config_adopted`); on an **unmanaged** node it's a local setting only.
- They are the source of the `ch_min_ms` / `dm_min_ms` (and via the fraction, `ch_cap`) fields the **`limits`** query reports (above) — so the app's pacing tracks the leaf's actual floors, not the defaults.
- **Not** part of the `create` verb (which stays freq/bw/sf/duty/name) — tune these with `cfg set` after the leaf exists, so `create` stays a simple rendezvous.
- Wire: the C config frame grew **+6 B** (`active_fraction_bp` u16 · `ch_interval_ms` u16 · `dm_interval_ms` u16); `wire_version` is **unchanged** (the test fleet reflashes together — no mixed-version compat).

### Deferred
- **Mobile-node roaming** (auto `leave`+`join` between leaves, with hysteresis) — a later phase; the three verbs above are the primitives it builds on.

## ⚙️ Duty-cycle status — companion readout (PROPOSED 2026-06-21, spec `docs/superpowers/specs/2026-06-21-duty-cycle-readout.md`)

How much of the legal airtime budget the node has spent — so the app can show a "transmitting / silent" gauge + a countdown. **0–100 %, where 100 % = the node must stay silent** (budget spent), plus the ms until it can transmit again.

### App → node: request it (on demand — the primary fetch)
Send the command over the BLE command channel (RXD), exactly like any console line:
```
duty
```
The node replies on TXD with one JSON line:
```json
{"ev":"duty","pct":42,"avail_ms":0,"enabled":true}      // 42% used, headroom (can TX now)
{"ev":"duty","pct":100,"avail_ms":73000,"enabled":true} // budget spent -> SILENT for ~73 s
{"ev":"duty","pct":0,"avail_ms":0,"enabled":false}      // duty limit disabled (unlimited)
```
- `pct` 0..100 (100 = silent). `avail_ms` = ms until some airtime frees (0 = available now; drives the countdown when `pct`=100). `enabled=false` ⇒ no duty limit configured — show "unlimited"/"—" and ignore `pct`.
- The value is **live/continuous** (rolling airtime window) — the app **polls `duty`** while a silent-countdown banner is on screen.

### Node → app: in the `ready` snapshot (so the app shows it on connect)
`ready` also carries `"duty_pct":42` (+ `"duty_avail_ms":0`) — an immediate starting value on connect; the `duty` query above is the live truth to refresh from.

## Adjacent BLE surface — implemented, not strictly "inbox" (2026-06-29)

These firmware→app events ride the same BLE TXD line, so the app's parser will see them; documented so it handles (or cleanly ignores) them. Not part of the inbox sync model.

- **OTA remote diagnostics:** `rcmd <dst> <query>` (BLE) → `{"ev":"rcmd_sent"}` ack (`fw_main.cpp:1465`); the multi-hop response `[rcmd <from>] …` lands on the console (`fw_main.cpp:2092`). Carried over-the-air by the `REMOTE_CMD`/`REMOTE_RESP` DATA TYPEs (see `docs/frames.md` §DATA).
- **`{"ev":"version",…}`** (`fw`/`built`/`git`/`board`/`reset`) — the BLE `version` query (`fw_main.cpp:1457`).
- **`{"ev":"prep_restart","halted":true}`** — the BLE `prep-restart` ack (`fw_main.cpp:1463`).
- **`{"ev":"hash_resolved","node":…,"auth":…,"hash":…}`** — the `resolve <hash>` diagnostic answer (`write_push`, `console_json.cpp:148`). Distinct from `peer_key_cached` (the pubkey-cache event).
- **`{"ev":"e2e_acked","origin":<dst>,"ctr":<n>,"sender_hash":<h>}`** — the **live twin** of the durable `inbox_dm type:"e2e_ack"` receipt (`PushKind::send_e2e_acked` → `pushkind_name`/`write_push`, `console_json.cpp`; landed 2026-06-29, replaces the former `{"ev":"unknown"}` hazard). The app marks its OUTBOX message **DELIVERED immediately** (not only on the next pull): match `(origin, ctr)` — or `(sender_hash, ctr)` when `sender_hash != 0` (cross-layer ack) — to the OUTBOX, **identical to the durable `type:"e2e_ack"` rule**. **NOT** an inbound DM — do not render it. `origin` = the dest that confirmed delivery; `sender_hash` = 0 on a same-layer ack.
- `cfg` / `status` / `route`+`routes_end` writers also stream over BLE (the Node/Network screens) — orthogonal to inbox sync; see the device-console design spec.
