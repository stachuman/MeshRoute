# MeshRoute iOS Companion — Session Handover (2026-06-12; ★ firmware-delta addendum 2026-07-16)

## ★ 2026-07-16 — firmware deltas the app MUST adopt + what's coming (read this first)

The firmware grew a **mobile-node + team plane** (roaming endpoints with a stable hash + a home
node; `team_id`-scoped overlays with member-to-member routing and group chat). Authoritative
app-facing doc: `INBOX_SYNC_CONTRACT.md` §*Mobile node + teams* (updated through 2026-07-16).
What it means for the app, in three buckets:

### 1. Breaking command changes — adopt in `Command.swift` NOW (already live in firmware)
- **★ HARD PLANE SPLIT:** `send`/`reqpubkey` gained a **`-t` tail flag** = TEAM plane. A teammate is
  reachable **only** with `-t` (`send <team_local_id> "…" -t` or `send <0xhash> "…" -t`); a plain
  send is the global/home plane and will `no_route` on a teammate. A home-attached mobile that is
  NOT your teammate stays plain `send <0xhash>` — no app change for that one case. `Command.send`
  needs a `teamPlane: Bool`; a team contact is a *distinct id space* (`{team_id, team_local_id}` +
  hash) — don't mix with static node ids in one contact model.
- **Hash args must be `0x`-prefixed** on `send`/`send_layer`/`resolve`/`reqpubkey`/`lookup` (the
  bare-8-hex autodetect is GONE → `bad_args`). Prefix every hash the app emits.
- `send_channel` on a team mobile auto-broadcasts to the team (no flag; it *rejects* `-t`).
- `rcmd` is now **authenticated** (sealed except `status`/`routes`) — treat as fire-and-observe.
- New `send_failed.reason` values to decode: `no_cts · no_ack · mobile_no_home` (+ the anti-spam
  `send_blocked`/`channel_sent`/`limits` surface — see the contract §Anti-spam v2).

### 2. Already-live JSON the app can use today
- `ready` carries **`name`** (node human name — use for the QR `n=`) + `pubkey` + leaf membership
  (`lineage`/`epoch`/`leaf`/`layer`/`synced`) + `duty_pct`/`duty_avail_ms`.
- `cfg set name "<text>"` = node identity name (distinct from `cfg set leaf_name`). Mobile/team
  console verbs (`cfg set mobile/team_id/mobile_autoregister`, `mobile register/gateways/query/
  status`, `team new/<id>/0`) are live — but their *output* is still human text (bucket 3).

### 3. ✅ LANDED IN FIRMWARE (2026-07-16, gated, uncommitted) — build the app side against these NOW
**Spec: `docs/superpowers/specs/2026-07-16-companion-mobile-team-json-surface.md` — IMPLEMENTED +
GATED** (native 743/25450 · s18 byte-identical · s22–s26 0-fail · boards 10/10; QA-verified). The
final shapes are folded into `INBOX_SYNC_CONTRACT.md` §*Mobile node + teams* — that doc is the
authority. Summary of what the app can now decode:
- **S1** `ready` gains `mobile`/`mobile_registered`/`mobile_home`/`mobile_local`/`mobile_home_layer`/
  `hosting`/`team`/`team_local` — ALL omit-when-inactive (absent ⇒ false/0); `cfg` gains
  `mobile_autoregister` + `team_id` (always present, `"00000000"` = unset). Team ids are **quoted
  hex strings like `key`** everywhere.
- **S2** `{"ev":"mobile_reg",…,"registered":true|false}` (register/roam/home-loss — the mobile
  connectivity chip) + `{"ev":"team_reg","team":"…","local":N}` (team-DAD).
- **S3** `mobile status` → `{"ev":"mobile_status",…}` (integer `freq_khz`/`bw_hz`, no floats);
  `mobile gateways` → streamed `mobile_gw`* / `mobile_net`* / `mobile_gw_end` (the roam-UI data);
  any `mobile` verb on a non-mobile → `{"ev":"mobile_err","reason":"not_mobile"}`.
- **S4/S5** `channel_recv` + `inbox_channel` gain `"team_id":"…"` (hex, omit ⇒ leaf channel) —
  thread team chat separately; identity keys unchanged. ⚠ The store-version bump means the first
  boot after reflash wipes the on-node inbox + bumps `inbox_epoch` — the app's normal epoch re-pull
  handles it.
- **S6** `peer_key_cached` gains `"name"` (omit-when-unknown) and `nameof 0x<hash>` answers
  `{"ev":"peer_name","hash":…[,"name":…]}` → contacts auto-label; QR `n` stays the manual path.
App work this unblocks: the three-plane contact model, the mobile connectivity chip + roam UI, the
team chat view keyed by `team_id`, contact auto-labeling. All additions are additive/omit-based —
decode-and-ignore keeps old app builds safe.
- **JOIN/DAD FEEDBACK (landed 2026-07-16, gated, uncommitted):** the join flow is now fully
  event-driven — no more `whoami` polling. `join`/`create` answer a JSON
  `{"ev":"join_started",…}` (create adds `create:true`/`lineage`/`leaf_name`), and the adopt fires
  `{"ev":"join_adopted","id":…,"layer":…,"epoch":…}` (~6 s later; a managed leaf then emits
  `config_adopted` as before — unmanaged is DONE at `join_adopted`). Failures unchanged
  (`join_refused{wire_version|leaf_full}`). ★ **App rule: on `join_adopted` always refresh the
  cached node identity — it ALSO fires on a boot DAD and on a silent mid-session id change (the
  address-conflict heal), which previously had no signal at all.** Shapes + spinner recipe in
  `INBOX_SYNC_CONTRACT.md` §Join/DAD feedback.
**Context:** a screenless SenseCAP T1000-E mobile-only tracker variant is feasibility-assessed
(PARKED) — if built, the companion is its ONLY management UI, so the mobile screens above should be
designed phone-first, not as a debug panel.

A native iOS app that is the human face of a MeshRoute LoRa node: connect over BLE, read/send DMs +
channel posts, view node status, (later) OTA. The node is name-agnostic (short ids + `key_hash32`), so
**the app owns the contact/name map and the durable archive**.

## Status: App Phase 2 (messaging core) — COMPLETE & VERIFIED ON HARDWARE

End-to-end BLE messaging works against a real XIAO nRF52840 node (verified 2026-06-12, node id 5):
connect + MITM-PIN pair → `whoami`→identity → send DM (by id / by hash) + channel → **receive live
messages**. 58 unit tests pass (`swift test`); the app builds + runs on device and in the Simulator
(via the mock). The firmware BLE companion (`src/device_ble.h`, another agent) is built + wired.

### The fix that unblocked receiving (don't lose this)
node→app notifications were truncated to ~20 bytes (no identity, no messages). Cause: the default ATT
**MTU 23** chunked multi-notification JSON and the SoftDevice's tiny default HVN queue dropped all but
the first chunk. Fix = **one line of firmware**: `Bluefruit.configPrphBandwidth(BANDWIDTH_MAX)` before
`Bluefruit.begin()` in `src/device_ble.h`. The app needed no change. (Full writeup in the
`ble-mtu-truncation-fix` memory.)

## Architecture (mirrors the firmware's lib/core vs device split)
- **`MeshRouteKit/`** — pure Swift package, headless-testable. `MeshRouteWire` (the wire contract:
  line-ASCII command encoder + Codable JSON decoder) + `MeshRouteCore` (domain models, `NodeLink`
  transport seam, `NodeSession`, `MockNodeLink`, dedup/sync logic).
- **`MeshRouteCompanion/`** — SwiftUI app: `BLENodeLink` (CoreBluetooth NUS central), SwiftData
  persistence, `AppModel` controller, Messages/Contacts/Node screens. Project is generated by
  `xcodegen generate` from `project.yml`.

## BLE contract
app→node = line-ASCII console verbs; node→app = newline-delimited JSON (`console_json.cpp`). NUS
`bleuart`: service `6E400001`, RX/write `6E400002`, TX/notify `6E400003`. Identity dedup: DM =
`(sender_hash, ctr)` else `(origin, ctr)`; channel = full 32-bit `channel_msg_id`. Inbox sync is
epoch-aware (`inbox_epoch` resets cursors) with model "B" live-`seq` gap recovery. Authoritative docs:
- `ios-companion/INBOX_SYNC_CONTRACT.md`
- `docs/superpowers/specs/2026-06-11-ble-companion-phase2-scope.md` (incl. the **Client reference**, §C.1–C.7)
- `docs/superpowers/specs/2026-06-10-ios-companion-app-design.md`

## Build / run / test
- **Package tests (headless):** `cd MeshRouteKit && swift test --scratch-path /private/tmp/mrk-build`
  (the `/Volumes/MeshRoute` mount fails the compiler's index-store renames — MUST build to local disk).
- **App:** `cd MeshRouteCompanion && xcodegen generate`, open the `.xcodeproj`, set your Team
  (target ▸ Signing & Capabilities — a free Apple ID works), select your iPhone, Run; trust the dev
  cert on the phone. (Simulator has no BLE — it runs the mock.)
- **Firmware:** `pio run -e xiao_sx1262` **on the build PC** (pio + the Bluefruit BSP live there, not on
  this Mac). To enable BLE on a node: `cfg set ble_mode on` (or `periodic`) + reboot (default `off`);
  PIN default `123456` (`cfg set ble_pin`); the S140 SoftDevice must be flashed.

## Bundle id changed 2026-06-12: `eu.meshroute.companion` (was com.meshroute.companion)
Matches the booked project domain **meshroute.eu**. NOTE for the next device deploy: iOS treats it as
a NEW app — the old install stays (delete it), the on-device archive does NOT carry over (re-pull
rebuilds history from the node's inbox; contacts re-add/QR), and Xcode re-prompts signing/trust.

## Theme D (roadmap step 3) — SHIPPED 2026-06-12, bench-verify pending
status/cfg/routes over BLE → Node tab Status + Config sections + Network (routes) screen.
Firmware: `write_status` (enriched) / `write_route` / `write_routes_end` / `write_cfg` in
console_json.cpp; `ble_dispatch_line` wires status (buffered), cfg (buffered), routes (streamed via
tx_line). NO float on the wire (freq=Hz, duty×1000) — dodges the newlib-nano printf bug. Battery
(`read_batt_mv`) uses the authoritative MeshCore XiaoNrf52Board method (PIN_VBAT=D32/P0.31, ×3.0
divider, 3.0 V ref) — ON by default on XIAO, omits when implausible (USB-only); gated to NRF52_PLATFORM,
-DMR_NO_BATT to disable.
App: `NodeStatusSnapshot` enriched + `RouteInfo`/`NodeConfigInfo` + `NodeInfoViews.swift`. Needs PC
rebuild + `pio test -e native` (new goldens) + reflash. `status`/`cfg`/`routes` no longer return
`unknown_cmd`.
**Buffer fix (2026-06-12):** enriched `status` (~260 B max) and `cfg` (~298 B; gateway w/ layers[]
~680 B) overflow `device_ble.h`'s `g_out[256]`. Fixed in fw_main `ble_dispatch_line`: status+cfg now
format into the 1700-B `s_inbox_jb` and stream via `ble_sink` (return 0), like routes/pull_inbox —
zero new RAM, gateway-safe. (A too-small test buffer `b[200]` for the status golden was bumped to 256.)

## B1 (roadmap step 2) — SHIPPED 2026-06-12, bench-verify pending
QR contact exchange: Contacts tab → "My card" QR + camera scan (VisionKit; camera permission added
to project.yml — `xcodegen generate` already run). Wire format `meshroute://contact?v=1&h=…&n=…`
(`ContactCard`, tested; `&p=` pubkey reserved for B2). Firmware: `ready` now carries the `/mrid`
`"name"` (loaded per-whoami, no RAM cost) — rebuild+reflash with the Theme-A `now_ms` change; give
nodes names via `cfg set name <str>`.

## Theme C location (fixed-node set/show) — SHIPPED 2026-06-12, bench-verify pending
Format = int32 degrees×1e7 (`lat_e7`/`lon_e7`, 0,0=unset). Firmware: persisted in the **`/mrid`** record
(appended after `name`, no version bump; strict load → reflashing re-mints identity, accepted as a dev
system); `g_lat_e7`/`g_lon_e7`; `cfg set lat`/`lon`
(serial + **wired over BLE** in `ble_dispatch_line`, echoes fresh cfg); dump_cfg + help + `cfg` JSON
(`lat_e7`/`lon_e7`). App: `NodeConfigInfo.latitude/longitude/hasPosition`; Node-tab Location row + MapKit
preview + set sheet (manual or CoreLocation "use my location"); `AppModel.setNodeLocation`. New Info.plist
key: NSLocationWhenInUseUsageDescription. Needs PC rebuild + `pio test -e native` (cfg golden has
lat_e7/lon_e7) + reflash. Still TODO: BCN-ext broadcast so PEERS' positions show (firmware beacon +
position table) + the mobile phone-fed path.

## E1 reliability + notifications (roadmap step 4, in progress) — 2026-06-12
Background-BLE fixes (auto-reconnect now re-syncs; foreground re-sync; `bluetooth-central` bg mode) +
**local notifications** (a DM arriving while not on screen → `UNUserNotification` banner; live path only,
not bulk pull). `AppModel.requestNotificationAuthorization` (on launch) / `notifyInboundDM` /
`handleForeground`+`handleBackground` (RootView scenePhase). **Tap-to-open** via `NotificationRouter`
→ `openConversation` (tab/path hoisted to AppModel). **App-icon badge** mirrors unread. App now
**decodes `layer_id`** (D12) on all 4 message types. Still TODO for E1: **State Restoration** — survive
app termination; deferred because it needs a persistent-BLE-session refactor (today connect() makes a
fresh link/session each time; restoration needs the CBCentralManager created at launch with a restore
id + willRestoreState + pump-always-on) and on-device termination testing. Plus firmware wake-on-message.

## Theme A (roadmap step 1) — SHIPPED 2026-06-12, bench-verify pending
Timestamps (`now_ms` in `ready`/`inbox_end` + app `NodeTimeAnchor`; firmware needs PC rebuild +
`pio test -e native` + reflash), offline outbox (drain-on-connect), delivery retry, per-phone unread
(no `mark_read` sent — D14), local channel labels. Package tests green, app builds. New SwiftData
bits: `MessageEntity.isRead`, `ChannelLabelEntity` (additive migration).

## Fixes 2026-06-12 (bench-reported)
- **Channel self-echo** — a node could record/push its OWN channel post back as "received" (after the
  buffer evicted it + a re-flood/digest-pull re-ingested it; the app can't dedup — its sent copy has no
  channel_msg_id). Fixed in `node_channel.cpp ingest_channel_m`: skip the inbox record + channel_recv
  push when `origin == _node_id` (gossip/flood forwarding still runs). Needs PC rebuild + `pio test`.
- **Battery 3.7 V on USB = NOT a bug.** VBAT (D32/P0.31) reads the CELL via a ÷3 divider, never USB's
  5 V (max ~4.2 V even charging). 3.7 V is a real mid-charge LiPo; XIAO charges at only 50 mA so it
  rises slowly. Added `batt_raw`+`batt_mv` to serial `status` to verify vs a multimeter. With NO battery
  the reading is meaningless (can't reliably detect cell-absent).

## E2E peer-key provisioning (app side) — SYNCED 2026-06-16 (firmware crypto in progress)
Contract: INBOX_SYNC_CONTRACT.md §Verified-peer provisioning + `2026-06-16-e2e-peer-key-provisioning.md`.
App stays crypto-free (D6/D20) — opaque pubkey bytes. Wired: decode `ready.pubkey` + `send_failed.reason`
+ `peerkey_set`/`peerkey_err`/`reqpubkey_sent`/`peer_key_cached`; commands `peerkey <hex64>` / `reqpubkey
<hex8>` (`AppModel.provisionPeerKey`/`requestPubkey`); My-card QR now carries the pubkey (`&p=`); scanning
a card with a pubkey auto-sends `peerkey`. Mock serves `ready.pubkey` + the peerkey/reqpubkey acks.
**E2E surfacing UX SHIPPED 2026-06-16:** per-message **encrypt lock** in compose (`sendhashx`/`sendhashx_ack`,
hash-only, defaults to the `e2e_dm` app setting) → `MessageEntity.crypted` → the bubble lock marker;
`send_failed{no_pubkey}` → bubble "Request key" (`reqpubkey`); `peer_key_cached` → bubble "Key ready —
resend securely" (`AppModel.markKeyReady`, `MessageEntity.failReason`); Node-tab "Encrypt DMs by default"
→ `cfg set e2e_dm` + `@AppStorage("encryptDefault")`. App decodes `cfg.e2e_dm` (firmware doesn't emit it
in cfg yet — §8.2 ask). 73 tests, app builds.

## Product direction
The LIVING roadmap (decisions D1–D5, themes A–E, open questions Q1–Q10) is
`docs/superpowers/specs/2026-06-12-companion-product-roadmap.md` — the single tracking doc for
where the companion is going. The list below is the nearer-term engineering queue.

## Open items / next (priority order)
1. **`status`/`cfg`/`routes` over BLE — App Phase 3.** Today only `whoami` is JSON-wired; the Node-tab
   `status`/`cfg` buttons send verbs the firmware answers with `{"err":"unknown_cmd"}` (by design).
   Quick win: wire `status`→ the existing unused `write_status` in `ble_dispatch_line` (~2 lines).
   Bigger: the Config + Status screens + the `cfg`/`routes` JSON encoders.
2. **OTA (App Phase 1)** — embed Nordic `iOSDFULibrary`; works against the bootloader DFU. Not started.
3. **Durable inbox (firmware Phase 3)** — an INTERIM **volatile RAM** inbox (32 msgs/store, wiped on
   reboot, fresh epoch each boot) is LIVE in the default build since commit b8e6080 (2026-06-12) —
   record-on-delivery + `pull_inbox` + `mark_read` all work; boot banner: `inbox = RAM volatile`.
   The durable QSPI/LittleFS backend (`MRINBOX_QSPI_READY`) is still pending (another agent).
4. **Contacts QR exchange** (scan a peer's `whoami` hash); message timestamps from `rx_ms`;
   background-BLE / State Restoration.

## SOLVED (2026-06-12): messages received while away didn't show after connect
**Root cause:** `JsonBuf::i64` formatted with `snprintf("%lld")` — newlib-nano (the nRF52 BSP libc)
has an integer-only printf with no long-long support, so on metal it emitted the LITERAL `ld`:
`"rx_ms":ld` → every `inbox_dm`/`inbox_channel` line was invalid JSON → the app decoder fell back to
`.raw` (visible in the Console tab, never ingested). `inbox_end` has no `rx_ms`, so the pull
"completed" silently. Native tests (host libc handles `%lld`) could never catch it; found via the
USB-serial `pull_inbox 0 0` smoke test.
**Fix:** hand-rolled digit writer in `lib/console/console_json.cpp` `JsonBuf::i64` (libc-independent,
INT64_MIN-safe; host-verified against all edge cases; existing test_console_json expectations pin the
format). Needs rebuild + reflash on the build PC. NOTE the reflash wipes the RAM inbox → re-send test
messages after reboot, then connect the companion (new epoch → full re-pull → Messages populate).
**Known sibling (separate task):** `JsonBuf::f64` (`%.4g`) likely breaks the same way on metal — used
by the `rt_update` event's `score` field (diagnostics only, not messaging).
**Contract gap → DECIDED (roadmap D7):** DM dedup identity `(sender_hash, ctr)` collides when a
SENDER reboots (ctr restarts at 1). Fix = persist `ctr` in NV; owned by the inbox-hardening agent
(asks written in INBOX_SYNC_CONTRACT.md §"Hardening asks").
**Follow-up shipped (same day):** unknown senders auto-create a placeholder contact — an inbound DM
(live or inbox-pulled) whose `sender_hash` has no ContactEntity inserts one named "Node <id>"
(renameable in Contacts); a known hash refreshes `lastKnownID`. `AppModel.ensureContact`, called from
`insertInboundDM` + `importInboxEntry` (before the dedup check, so an epoch-reset full re-pull
backfills contacts for already-archived threads).

## Gotchas (also in `memory/`)
- **Hash presentation convention:** anything human-facing shows `0x`+hex8; the wire carries decimal
  u32 (`sender_hash`, `channel_msg_id`). Decimal belongs only in raw wire dumps.
- SwiftPM → local-disk scratch (`--scratch-path /private/tmp/mrk-build`). Xcode/`xcodebuild` is fine.
- Firmware builds happen on a **separate PC**; sandboxed `pio` here shows `._`/`.sconsign` FS errors
  (mount artifact, not code).
- The BLE MTU fix above.

## Git
Nested repo at `ios-companion/` (own git); outer firmware repo at `/Volumes/MeshRoute`. **Nothing has
been committed by the agent** — many uncommitted changes (the firmware `device_ble.h` MTU fix + the
entire `ios-companion/` app). Commit when ready.
