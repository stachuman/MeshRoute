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

## Pushes (node → app)  — one JSON object per line

```json
{"ev":"inbox_dm","seq":42,"origin":2,"ctr":7,"sender_hash":3735928559,"rx_ms":123456,"body":"…"}
{"ev":"inbox_channel","seq":7,"origin":4,"channel_id":3,"channel_msg_id":68298753,"rx_ms":123456,"body":"…"}
{"ev":"inbox_end","dm_seq":42,"chan_seq":7,"epoch":3,"count":15}
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

## Firmware asks (small — so live pushes share the pulled inbox's identity keys)

So a message seen **live** and later **pulled** dedups on the same key, the live pushes need the same
identity fields the inbox now stores:
```json
{"ev":"channel_recv","origin":4,"channel_id":3,"channel_msg_id":68298753,"body":"…"}        // channel_msg_id is new
{"ev":"msg_recv","origin":2,"ctr":7,"sender_hash":3735928559,"body":"…"}                    // sender_hash is new
```
Both are at hand already: `node_channel.cpp` passes the full `channel_msg_id` to `record_channel`, and
`do_post_ack` has `sender_hash` (the parsed `source_hash`) right at the `msg_recv` push. (Firmware note:
each adds a `u32` to the `Push` POD + its `console_json` writer — a small Phase-3 task, landed with the
companion pushes.) Without them, a channel/DM seen live AND later pulled can duplicate.

## Open / deferred (match the inbox spec §8, §14)

- **No `delete`** in v1 (node self-manages via drop-oldest). Not in this contract.
- `mark_read` reply: a `{"ack":…}` or nothing — app doesn't depend on one. Firmware's choice.
- Absolute time: deferred (no node RTC). If a node later sends its current uptime in `inbox_end`
  (`"now_ms"`), the app can map `rx_ms` → wall-clock precisely instead of stamping pull-time.

## App-side reference (already implemented + tested)

`ios-companion/MeshRouteKit`: `Command.pullInbox/.markRead`, `Inbound.inboxEntry/.inboxEnd`,
`InboxEntry` (with `senderHash` + `channelMsgID`), `InboxSyncState` (epoch + cursors), `MessageIdentity`
(`.dmByHash(hash,ctr)` / `.dmByID(origin,ctr)` / `.channel(msgID)`), `ConversationStore.ingestInbox`
(dedup vs live + re-pull; DM threads key by `sender_hash` → straight into the contact, no resolve), and
`MockNodeLink` serves `pull_inbox` + `inbox_epoch` + `sender_hash`. App: `NodeProfileEntity.syncState`,
`AppModel.startInboxSync`. Aligned to the 2026-06-10 + 2026-06-11 firmware reviews (channel = 32-bit
`channel_msg_id`; DM = `(sender_hash, ctr)`; `inbox_end.epoch`). Run `swift test --scratch-path /private/tmp/mrk-build` (55 tests).
