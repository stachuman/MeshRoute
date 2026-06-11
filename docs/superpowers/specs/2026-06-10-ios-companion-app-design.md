# iOS Companion App — Initial Design

**Status:** INITIAL DESIGN (2026-06-10) — a starting spec for a *separate* app project. The app's
own repo implements this; it lives here because the **BLE protocol is the shared firmware↔app
contract**.
**Depends on:** the firmware BLE companion (`2026-06-10-ble-companion-ota-inbox-design.md`, Phase 2)
for messaging/config; the **OTA module depends only on the already-shipped bootloader DFU** (works
against today's firmware — see §7).

## 1. Goal

A native iOS app that is the human face of a MeshRoute node: connect over BLE to **read received
messages** (DM threads + channel feeds), **send** DMs + channel posts, **view/edit node config**,
see **node status**, and **update firmware OTA**. The node is name-agnostic (it knows short ids +
`key_hash32`, never names) — so the **app owns the contact/identity map** and the durable message
archive.

## 2. The BLE link (the contract)

- **Service:** the node's NUS-compatible `bleuart` GATT (firmware spec §A.2) — an RX characteristic
  (app→node, write) + a TX characteristic (node→app, notify). The app is the BLE **central**.
- **Pairing:** the node's static PIN (`ble_pin`). On first connect the app prompts for the 6-digit
  PIN → bond → encrypted link before the characteristics are usable. Bond persists.
- **Duty-cycle reality (important UX constraint):** a node may advertise only **periodically**
  (`ble_mode = periodic N`), or always (`on`), or never (`off`). The app must handle "the node isn't
  advertising right now": scan, show *"waiting for the node's next window (~N min)"* and connect when
  it appears; a press-to-advertise on the node opens an immediate window. For `on`/mobile nodes it's
  always discoverable.
- **iOS background-BLE limits (flag):** CoreBluetooth can't freely scan in the background — it needs
  **service-UUID-filtered scanning + State Restoration** to reconnect to a known node in the
  background, and even then it's best-effort. This bites the *periodic* model hardest (the app may
  need to be foregrounded to catch a window). Design the UX around foreground-connect; treat
  background reconnect as a bonus.
- **On connect:** drain live pushes; once the firmware inbox lands (firmware Phase 3), **pull the
  inbox** (`pull_inbox <cursor>`) and reconcile with the local store; `mark_read`/`delete` as the
  user reads.

## 3. Message protocol over BLE (shared contract — to finalise)

Reuse the firmware's typed command/push protocol. The **framing over the NUS characteristics is a
shared contract that must be defined once** for both sides. Current state: `console_json.cpp`
*encodes* results/pushes as JSON, but commands are parsed *only* as line-ASCII (`console_parse.cpp`)
— so there's no JSON command *decoder* on the firmware yet.

**Proposal:** newline-framed **JSON** both directions (extend the firmware BLE backend with a JSON
command decoder; the app uses Swift `Codable`). Commands app→node (`{"cmd":"send","hash":"…","ack":true,"body":"…"}`),
results/pushes node→app (`{"ack":"queued","ctr":…}`, `{"ev":"msg_recv","origin":…,"body":"…"}`,
`{"ev":"hash_resolved",…}`). JSON is debuggable and trivial to model in Swift; the modest size cost
is fine over BLE for the companion (not the LoRa hot path). *Alternative:* keep the existing
line-ASCII command format + parse JSON pushes (no firmware decoder needed, but two formats). Decide
in §9.

Command/push set mirrors `command.h`: `send`/`send_ack` (by id or `hash`), `send_channel`, `resolve`,
config get/set, plus the pushes `msg_recv`/`channel_recv`/`send_acked`/`send_failed`/`hash_resolved`
(and, Phase 3, the inbox-sync `pull_inbox`/`mark_read`/`delete`).

## 4. App features (screens)

- **Contacts / identity** — the local map **name ↔ `key_hash32` (+ last-known short id)**. Add a
  contact by hash (the peer reads their own `whoami` hash and shares it — QR is the nice path) or by
  picking a heard origin. Sending a DM addresses by **hash** (`sendhash`) so the node resolves it;
  `resolve` surfaces the current id. This is *the* reason the app exists — the node has no names.
- **Messages** — DM threads (grouped by contact) + channel feeds (by `channel_id`). Live via pushes;
  history from the local store (and the firmware inbox, Phase 3).
- **Compose / send** — DM (optionally request E2E ack) + channel post.
- **Config** — read/set the node's `cfg` keys over BLE (id, SFs, leaf, gateway, `ble_mode`,
  `ble_pin`, …); a settings screen with the same surface as the console `cfg`.
- **Node status** — routes / neighbors / duty / (battery if exposed) — the diagnostic reads.
- **Firmware update (OTA)** — §7.
- **Node manager** — the user may have several nodes; manage a list of paired node profiles.

## 5. Local data (on the phone — the durable archive)

The phone, not the node, is the long-term store (the node's inbox is bounded). Persist with
**SwiftData/Core Data**:
- **Contacts** (name ↔ hash ↔ id).
- **Message history** (DMs + channels), full archive; **dedup/merge by the STABLE message identity** when
  pulling from the node so a reconnect doesn't duplicate — a **channel** message by its full 32-bit
  **`channel_msg_id`**, a **DM** by **`(origin, ctr)`**. (The firmware inbox now stores the whole
  `channel_msg_id`; do NOT key channel dedup on the low byte / the node's `seq`.)
- **Node profiles** (paired nodes + their config snapshots).

### 5.1 Node-reset (seq-epoch) handling — **REQUIRED** (do this before the sync hardens)

The firmware inbox hands each record a per-store **`seq`** that the app uses as its **sync cursor**
(`pull_inbox <dm_since> <chan_since>` returns `seq > cursor`). But **`seq` is monotonic only within one
node-storage epoch.** If the node's flash inbox is wiped — factory reset, an OTA that erases the data
partition, or a format-on-dirty recovery — `seq` **restarts at 1**, so a cursor the app already advanced
past would make it **silently miss** the node's new messages (firmware persistent-inbox spec §6/§10.1).

The app MUST:
1. **Persist a `storage_epoch` per node** (the firmware exposes it — a u32 that bumps on any inbox wipe;
   firmware spec §10.1). Read it on connect alongside `whoami`.
2. **On a changed epoch ⇒ the node's history was wiped ⇒ reset that node's cursors to 0 and re-pull from
   the start.** This is safe + non-duplicating *because* the archive dedups by the **stable identity**
   above (the re-pulled messages already exist locally), NOT by `seq`.
3. *Fallback if the firmware exposes no epoch yet:* treat `node.newest_seq < my_cursor` as a reset signal
   and re-sync — weaker (misses a wipe-then-refill past the old cursor); prefer the explicit epoch, and
   flag it to the firmware side. (Cross-ref: persistent-inbox spec §10.1 lists epoch-exposure as a hard
   Phase-2 requirement on the device store.)

**Never assume `seq` is globally monotonic across a node's lifetime** — only within an epoch.

## 6. iOS stack

- **Swift + SwiftUI**, **CoreBluetooth** (central, with State Restoration for known nodes).
- **Nordic `iOSDFULibrary`** (Swift Package) for OTA (§7).
- **SwiftData/Core Data** for persistence.
- Pairing: handle passkey (PIN) entry + bonding via CoreBluetooth.

## 7. OTA from the app

- **App-OTA can ship early — it depends only on the bootloader DFU, not the firmware companion.** The
  node already has the `ota` command + the OTAFIX bootloader. The app embeds Nordic's **iOSDFULibrary**
  to discover the bootloader's DFU advert and push the firmware **`.zip`** over BLE.
- **This also fixes the "stuck in DFU / unrecognized USB" issue** you hit: a proper DFU library sends
  the post-transfer execute/reset, so `bootloader_dfu_start` returns and the node auto-boots the new
  app — no manual re-plug. (That problem was the absence of that finalise step, not the firmware.)
- **Trigger:** Phase 1 the `ota` command is USB-console-only, so until the firmware companion (Phase 2)
  the user enters DFU via the console (or a button) and the app does the **push**. Once the companion
  BLE exists, the app sends `ota` itself → fully in-app OTA.
- **Phase 2+:** the in-session `BLEDfu` (0xB1) path (firmware §B.1) once the node runs the SoftDevice.
- **Firmware delivery:** the `.zip` from a release URL (or bundled for dev). The DFU init packet
  carries device-type compat; no app-level signing initially.

## 8. Phasing (app)

- **App Phase 1 — OTA push** (Nordic `iOSDFULibrary`): works against *today's* firmware bootloader DFU,
  and removes the manual-reset pain. Useful before the firmware companion exists.
- **App Phase 2 — Companion core** (needs firmware BLE companion): connect + pair + read live messages
  + send DMs/channels + contacts. The MVP of "read inbox / drive the node from your phone."
- **App Phase 3 — Config + status** screens.
- **App Phase 4 — Inbox sync** (needs firmware Phase 3 inbox): pull history, durable archive,
  read/delete.

## 9. Open decisions (to confirm on review)

1. **Native iOS (assumed) vs cross-platform.** "iPhone-aimed" → native Swift/SwiftUI (best for
   CoreBluetooth + Nordic DFU + background-BLE). If Android is wanted later, the alternative is
   Flutter (`flutter_blue_plus` + a Nordic DFU plugin) — but it complicates the DFU + background paths.
   Recommend native iOS for v1.
2. **BLE framing:** JSON both ways (needs a firmware JSON command decoder) vs line-ASCII commands +
   JSON pushes vs a compact binary. Recommend JSON both ways — finalise as the shared contract.
3. **OTA library:** embed Nordic `iOSDFULibrary` (recommended — seamless + fixes the reset) vs punt to
   a separate DFU app.
4. **Contact exchange UX:** QR of the `whoami` hash (recommend) vs manual hash entry vs pick-from-heard.

## 10. Non-goals (initial)

- Not a mesh-wide visualizer/dashboard; not multi-user accounts. It's a single-user companion to
  their own node(s). Android, web, and a desktop client are out of scope for v1 (the protocol stays
  transport-agnostic, so they remain possible later).
