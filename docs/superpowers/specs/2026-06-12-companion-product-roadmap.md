# Companion Product Roadmap — LIVING proposal

**Status:** LIVING — the single tracking document for the companion's product direction (user
decision D1). Decisions land in §1 with dates; open questions live in §6 and get promoted to
decisions as they're answered. Both sides (app + firmware/wire) may change in service of UX.
**Authors:** Stani + app agent (firmware changes coordinated with the node agent).
**Refs:** `2026-06-10-ios-companion-app-design.md` (app v1 design) ·
`2026-06-11-ble-companion-phase2-scope.md` (BLE contract) · `ios-companion/INBOX_SYNC_CONTRACT.md` ·
`docs/specs/2026-06-05-identity-leaf-membership-join-design.md` (identity/leaf/join — "identity spec") ·
`docs/PORT_PLAN.md` (firmware tracks) · `docs/frames.md` (wire) ·
`2026-06-12-gateway-dual-layer-design.md` (R7 cross-layer — "gateway spec") ·
`ios-companion/HANDOVER.md` (session state).

---

## 0. Where we are (baseline 2026-06-12)

**App Phase 2 messaging core works on real hardware:** BLE pair (MITM PIN) → identity → DMs +
channel posts both ways → volatile-inbox catch-up (`pull_inbox`, epoch-aware, model-B gap
recovery) → unknown senders auto-create contacts. **Firmware:** same-layer core, identity
(Ed25519/X25519, `/mrid`, node names, `regen`), node_id DAD — done; **R6 leaf-config join = next**;
E2E DM crypto wire-reserved; R7 gateway **designed** (`2026-06-12-gateway-dual-layer-design.md`),
not coded; R8 mobile not started. The durable (QSPI) inbox is pending; the interim store is RAM.

## 1. Decision log

| # | Date | Decision |
|---|------|----------|
| D1 | 2026-06-12 | One living doc (this file) tracks the companion product direction. |
| D2 | 2026-06-12 | **Location model:** FIXED nodes carry lat/lon as a configured **property** (they don't move; set once). MOBILE nodes (which **do not route**; BCN `is_mobile`; R8) optionally get position **fed by the paired phone over BLE**. |
| D3 | 2026-06-12 | Theme split A–E + the priority order in §5 accepted as the working plan. |
| D4 | 2026-06-12 | **BLE availability:** while a user session is interactive, the node must NOT switch BLE off (a periodic window is pinned open by activity). Periodic wake (e.g. every 15 min) + background push notifications are a target UX. |
| D5 | 2026-06-12 | **Wire lock:** `sender_hash` (SOURCE_HASH) is ALWAYS included in DATA frames — no longer default-on-but-optional. Moves into the sealed region when CRYPTED lands. |
| D6 | 2026-06-12 | **The app stays crypto-free** — all signing/verify/encrypt is node-side (monocypher). Keys are opaque bytes to the phone ⇒ the RFC-8032 switch (Q5) is NOT needed; the monocypher EdDSA variant stays. |
| D7 | 2026-06-12 | **`ctr` persists in NV** (epoch+RAM write pattern) to kill the sender-reboot dedup collision. **Owned by the inbox-hardening agent** (user passes it along) — not this track. |
| D8 | 2026-06-12 | **Mobile nodes** send/receive DMs + channels and send Q and BCN — the ONLY restriction is they don't ROUTE others' traffic (their position changes). ⇒ mobiles beacon ⇒ **one position rail for everyone: the BCN ext TLV**; fixed vs mobile differs only in the position SOURCE (cfg property vs phone-fed). |
| D9 | 2026-06-12 | **Notification latency:** worst-case push delay = the BLE window period is ACCEPTED. |
| D10 | 2026-06-12 | **Two phones on one node is a supported case.** ⇒ read state must be per-companion (app-side); node-side `mark_read` becomes a retention/pruning hint, not the read state. Detail lands in the inbox contract with D7's owner. |
| D11 | 2026-06-12 | **The app is the join UI** (post-R6): discover beaconing leafs → show `leaf_name` → join. |
| D12 | 2026-06-12 | **(Q13 → gateway spec, fixed)** The receiving-layer field on pushes + inbox records is the **full 8-bit `layer_id`** (not the 4-bit leaf nibble, which aliases across 255 layers). Applies to the `Push` POD, the inbox record, and the `msg_recv`/`channel_recv`/`inbox_dm`/`inbox_channel` JSON. **Firmware emits it + app decodes it (2026-06-12)** — wired into `Inbound`/`InboxEntry` (optional, 0 on single-layer); not yet persisted/shown (gateway-era UI). |
| D13 | 2026-06-12 | **Wake-on-message BLE (user-proposed):** a node that RECEIVES a DM addressed to it turns BLE advertising ON (outside any periodic window), bounded — stop on companion connect or timeout. The app's background service-UUID scan wakes on the advert → connect → pull → local notification. Amends D9: worst-case stays the window period, but TYPICAL push latency becomes seconds. Params → Q15. |
| D14 | 2026-06-12 | **Amends D10 — two phones are NOT specially handled.** The app/node just WARN the user when more than one companion is bonded/has synced ("multiple phones paired — sync behavior is undefined"). Read state stays app-side per-phone (that part of D10 stands); no per-bond cursors, no multi-companion sync design. Closes Q12. Mechanism (cheap): `ready` gains a `bonds:N` count → app banners when N>1 (land with E1). |

## 2. Personas

1. **Off-grid group** (hike / sailing / festival): iMessage-feeling chat, who's around, where,
   did it arrive. The app's center of gravity.
2. **Community mesh operator:** coverage, node placement, gateways, announcements, health.
3. **Builder** (today's user): console, status, OTA, diagnostics.

## 3. Service facts the app builds on (collected 2026-06-12)

- **Identity:** 32-B seed → Ed25519 (sign) + X25519 (ECDH) via vendored monocypher.
  ⚠️ monocypher EdDSA is **curve25519+BLAKE2b, NOT RFC-8032** — the identity spec §1.1 [xcheck]
  explicitly flags that an app-level pubkey exchange needs either the RFC-8032 switch or
  app-opaque key handling. (Resolved → **D6**: the app stays crypto-free / keys opaque, so the
  monocypher variant stays.)
- `key_hash32 = ed_pub[:4]` is a **routing handle only** (grindable); the **full pubkey is the
  identity**. Anything trust-bearing keys on the full pubkey.
- **Nodes already have a name** (`/mrid`, `cfg set name`) — app-level, intended to be exchanged
  *together with the full pubkey* (identity spec §1.3). A contact card is exactly this planned
  exchange, materialized.
- **E2E DM crypto is node-side** (X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305, `CRYPTED` b3;
  passive-eavesdropper guarantee, TOFU pubkeys via **HARD H query + WANT_PUBKEY** + sparse
  peer-key cache). **The phone does no crypto** — the encrypted+MITM BLE link covers app↔node.
- **Leaf:** ≤254 nodes; leaf config = `data_sf_list` + `leaf_name` + `duty_cycle` (homogeneous,
  fingerprinted `{lineage_id, epoch, config_hash}` in every beacon). **Channels are leaf-scoped.**
  Duty cycle is leaf config.
- **Cross-layer (R7 — now DESIGNED:** `2026-06-12-gateway-dual-layer-design.md`): a gateway is a
  FULL member of two layers with ONE identity (`key_hash32`) and one `node_id` PER layer
  (`whoami` will surface both). Symmetric — no home/guest. `layer_id` is 8-bit (1..255);
  wire `leaf_id` = low nibble (derived filter). **Cross-layer = DMs only** (CROSS_LAYER DATA +
  preserved layer-path cursor, standard reversed-path E2E ack); **channels NEVER cross a leaf**.
  H answers carry `target_layer`; no bridging gateway → `err_no_gateway` (the app already models
  that ack code). **Companion contract impact (gateway spec §2, user-resolved):** inbox records +
  live pushes gain the RECEIVING layer, because an 8-bit `origin` is ambiguous across a gateway's
  two layers; DM identity stays `(sender_hash, ctr)`. The field is the FULL 8-bit `layer_id`
  (D12). Gateway `ready` shape → Q14.
- **Mobile nodes (R8, roadmap):** full participants — send/receive DMs + channels, send Q + BCN —
  except they do NOT route others' traffic (their position changes) (D8).
- **Time:** no RTC anywhere; nodes may be arbitrarily desynced. The phone is the only wall clock.
- **Wire extension points:** DATA byte-1 `APP` flag gates a cleartext `TYPE` byte readable by
  endpoints **and cache-on-pass snoopers** (the contact-card rail); BCN has trailing **ext TLVs**
  (the piggyback rail); DATA payload budget ≈ 241 B.
- **PORT_PLAN names an "App layer" track** (inbox persistence, known-nodes directory, channel
  subscriptions) — this roadmap is that track's product face.

## 4. Themes

### A — Messaging feel *(order #1)* — **SHIPPED 2026-06-12 (bench-verify pending)**
| Item | Status |
|---|---|
| **Real timestamps** | ✅ fw: `now_ms` in `ready`+`inbox_end` (rebuild/reflash needed); app: `NodeTimeAnchor` converts `rx_ms`→wall-clock, pull-time fallback on old firmware |
| **Outbox** | ✅ compose offline → `.outbox` state → drained FIFO on connect; failed-retry while offline re-parks to outbox |
| **Delivery polish** | ✅ state badges incl. outbox; failed bubbles get "Tap to retry" |
| **Unread** | ✅ per-phone `isRead` (D14), bold rows + per-thread count + Messages tab badge; read on thread view. `mark_read` is NOT sent (per D14) |
| **Channel names** | ✅ local labels via long-press → Rename channel (Q9 assumed-yes) |

### B — Contacts & identity *(order #2 QR, #5 cards)* — **B1 SHIPPED 2026-06-12 (bench-verify pending)**
- **QR exchange** ✅ — Contacts tab: "My card" (QR of the connected node's name+hash) + camera scan →
  add/rename contact. Payload format (versioned, forward-compatible, on the project domain —
  **meshroute.eu, booked 2026-06-12**):
  `https://meshroute.eu/c?v=1&h=<hex8>&n=<name>[&p=<ed_pub hex64, RESERVED for B2/E2E>]` —
  `ContactCard` in MeshRouteCore (tested); `meshroute://contact` legacy alias accepted; unknown params
  never fail the parse. https so a STOCK-camera scan can Universal-Link into the app once the domain
  hosts an `apple-app-site-association` (+ a "get the app" fallback page at `/c`) — future task.
  Physical presence = the trust ceremony; no signature (D6).
- **Name in `ready`** ✅ — `whoami` loads the `/mrid` name on demand (no RAM copy) and emits
  `"name":"…"` (omitted when unset); app shows it in Node tab, status pill, and uses it on My card.
- **Contact card over mesh** (fw M, app M): DATA `TYPE=CONTACT_CARD` `{name, key_hash32, ed_pub,
  sig}`; **verified node-side per D6** (the app never does crypto; keys are opaque bytes even in
  QR payloads), pass-through nodes snoop-cache (the spec's cache-on-pass), recipient app offers
  "Add contact?". Unlocks E2E later: cards distribute the full pubkeys E2E needs. Open: card
  scope (Q1 residue) — DM-to-one only, or also a leaf-flood "announce myself"?
- **Auto-contact** (done 2026-06-12): unknown `sender_hash` → placeholder "Node N", renameable.

### C — Location & map *(order #6)* — per **D2 + D8**
- **One rail (D8):** position rides the **BCN ext TLV** for every node type; fixed vs mobile
  differs only in the SOURCE — fixed = `cfg set lat/lon` NV property (set once), mobile =
  phone-fed over BLE (opt-in). Cadence: fixed every Nth beacon (≈0 airtime); mobile per-beacon
  or on-move. (Format/precision: Q4.)
- **App:** map with pins + last-seen age; **distance/bearing list fallback** ("Marek: 2.1 km NE,
  12 min ago") — works without map tiles, which matter off-grid (offline tiles = open UX issue).
- **Privacy:** per-contact opt-in, precision degrade (exact/±500 m/off), TTL on stored positions.

### D — Network awareness *(order #3)* — **SHIPPED 2026-06-12 (bench-verify pending)**
- **Phase 3 over BLE** ✅ — `status` enriched (uptime/duty/txq/rx-tx/routes/pending/lbt + optional
  `batt_mv`), `routes` streamed (`{"ev":"route",…}`×N + `routes_end`), `cfg` object (freq/SF/bw/cr/
  tx_power/duty/beacon/leaf/ble…). Wire: `write_status`/`write_route`/`write_routes_end`/`write_cfg`
  in console_json (no float on the wire — freq as Hz, duty×1000); `ble_dispatch_line` wires all three.
  App: enriched `NodeStatusSnapshot` + `RouteInfo` + `NodeConfigInfo`; Node tab gains Status + Config
  sections + a Network (routes) screen; auto-refresh on connect + manual refresh. Mock serves them.
- **Battery** ✅ — `read_batt_mv()` uses the **authoritative MeshCore XiaoNrf52Board method** (checked
  against `/Volumes/meshcore/MeshCore` 2026-06-12): VBAT_ENABLE=D14/P0.14 (held LOW by `initVariant`,
  so reading costs no extra power), PIN_VBAT=**D32**/P0.31/AIN7 (NOT pin 31 — that's NFC2 in our map),
  `mV = adc×ADC_MULTIPLIER(3.0)×AREF(3.0)/4.096`. **ON by default** on the XIAO; an implausible read
  (USB-only / no cell) → omit (app hides the row). Gated to `NRF52_PLATFORM` so Heltec/ESP32 builds
  skip it; `-DMR_NO_BATT` compiles it out.
- **Leaf panel** (leaf_name/epoch/member count) — deferred to R6 (leaf-config join lands those fields).
- **Gateway era** (post-R7): `status`/`cfg` already emit the additive `layers:[…]` array
  (`write_layers_array`, built by the gateway agent) — the app can adopt it when R7 lands (Q14).
- **Gateway era (post-R7):** a gateway's Node screen shows BOTH layers (per-layer node_id, SF,
  routes) + the window schedule ("layer B window opens in 4 s"); routes/dedup are per-layer.
  Channel threads become layer-scoped app-side (channels never cross — thread key gains the layer).

### E — A real iPhone app *(order #4)* — **E1 STARTED 2026-06-12**
- **Background BLE reliability** ✅ (2026-06-12, bench-verify pending) — three fixes for "screen off →
  message received by node → wake phone → nothing until manual reconnect":
  1. **Auto-reconnect now re-syncs** — `BLENodeLink` auto-reconnects a dropped link through the same
     session; the per-connection guards (`greetedThisConnection`/`syncStartedThisConnection`) were only
     cleared on a USER disconnect, so an auto-reconnect showed "connected" but never re-`whoami`/re-pulled.
     Now cleared on every `.disconnected`/`.failed` → the next `.connected` re-greets + catches up.
  2. **Foreground re-sync** — `scenePhase == .active` → `pull_inbox` from the current cursors, for the
     case where the link stayed up through suspend but a push was dropped (no disconnect event fired).
  3. **`UIBackgroundModes: bluetooth-central`** — so iOS keeps the connection alive / delivers
     notifications while backgrounded instead of suspending the app on screen-off.
- **Local notifications** ✅ (2026-06-12) — a DM arriving while not on screen posts a `UNUserNotification`
  banner (title = contact/Node name, grouped per conversation via `threadIdentifier`). Foreground +
  bulk pull-catch-up are suppressed; only the live path fires. Auth requested on launch.
- **Still TODO for E1:** CBCentralManager State Restoration (survive app *termination*), badge on the
  app icon, and the firmware D4/D13 wake-on-message advertising.
  With `periodic` ble_mode, worst-case latency = the window period — **accepted (D9)**.
  Firmware side of D4: **activity pins the window open** (proposed default: pinned while a BLE
  central is connected + a short linger (~2 min) after disconnect — Q7 ratifies).
- **iCloud backup** of archive + contacts (app M): the phone is the system's only durable store.
- **Join/onboarding UX — DECIDED (D11), post-R6:** the app IS the join UI: discover beaconing
  leafs → show `leaf_name` → join. First-run magic. Firmware ask (with R6): a BLE surface for
  "leafs heard" + a join verb.
- **Multi-node:** profiles already keyed by node key; switcher UI later. **Two phones on one node:
  warn-only (D14)** — read state per-phone app-side; the app banners "multiple phones paired —
  undefined behavior" (via a `bonds:N` count in `ready`, lands with E1).

### Protocol hardening
- **D5 locked:** `sender_hash` always in DATA.
- **D7 decided:** `ctr` persists in NV (epoch+RAM pattern) → no reboot collision. Owned by the
  inbox-hardening agent; this track just consumes the guarantee.

## 5. Sequencing (working order)

1. **A** — timestamps (`uptime_ms`), unread, delivery polish, outbox.
2. **B1** — QR exchange + name-in-`ready`.
3. **D** — Phase 3 status/cfg/routes → Node + Network screens.
4. **E1** — background BLE + notifications + D4 window-pinning (firmware).
5. **B2** — contact card over mesh (after Q1b).
6. **C** — location: fixed-node property → BCN ext → map + list view.
- Later: E2E surfacing in the app (lock icon, "exchange keys" flow), join UX (post-R6),
  gateway-era features (post-R7), iCloud.

## 6. Open questions

*(Answered 2026-06-12 → decisions: Q1-trust→D6, Q2→D7, Q3→D8, Q5→moot via D6, Q6→D9, Q8→D10,
Q10→D11.)* Remaining:

| # | Question | Blocks |
|---|---|---|
| Q1b | **Contact-card scope:** DM-to-one only, or also a leaf-flood "announce myself"? | B2 |
| Q4 | **Position TLV format + cadence:** int32 ×1e-7 lat/lon? Fixed = every Nth beacon (N?); mobile = per-beacon or on-move threshold? | C |
| Q7 | **Ratify the D4 default:** window pinned while a BLE central is connected, + ~2 min linger after disconnect. | E1 (fw) |
| Q9 | **Channel naming:** local labels v1 (assumed yes); leaf-level shared directory ever? | A |
| ~~Q12~~ | Closed by **D14**: read state app-side per-phone; multi-phone = warn-only, no design. | — |
| Q14 | **`ready` shape for gateways** (two per-layer node_ids): additive `"layers":[{"layer_id":N,"id":M},…]` keeping the existing `"id"` for single-layer compat? App decoding is tolerant either way — settle when R7 lands. | D (gateway era) |
| Q15 | **Wake-on-message params (D13):** triggers = DMs only, or channel msgs too? Advert duration (prop.: 2 min)? Only when a companion bond exists (prop.: yes)? Re-arm suppression so a burst doesn't re-advertise per message (prop.: one window per burst)? | E1 (fw) |

*(Q13 → D12, resolved in the gateway spec 2026-06-12.)*
