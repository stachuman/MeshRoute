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

## 0.1 Status dashboard (reviewed 2026-06-14)

Legend: **✅ code-complete** (bench-verify pending) · **🔜 queued** (designed / next) · **⏸ blocked**
(firmware dep or open question) · **❌ not started**.

| Theme | Feature | Status | Note |
|---|---|---|---|
| A Messaging | real timestamps · outbox · delivery+retry · unread · channel labels | ✅ | all shipped 2026-06-12 |
| B Contacts | QR exchange (My card + scan) · name-in-`ready` · auto-contact | ✅ | B1 shipped |
| B Contacts | **contact card over mesh** (B2) | ⏸ | open: card scope Q1b |
| C Location | fixed-node **set/show** + map preview + "use my location" | ✅ | persisted in `/mrid` (D15) |
| C Location | **peers on a map** (BCN-ext position broadcast + position table) | ❌ | firmware beacon work + Q4b |
| C Location | mobile **phone-fed** position | ⏸ | R8 mobile (firmware) |
| C Location | dedicated **Map screen** / distance-bearing list / offline tiles | ❌ | app, gated on the broadcast data |
| D Network | status / cfg / routes screens · battery | ✅ | shipped; `cfg set` over BLE too |
| D Network | leaf panel (name/epoch/members) | ⏸ | R6 leaf-config join |
| D Network | gateway dual-layer UI (`layers[]`) | ⏸ | R7 gateway |
| E iPhone-app | background-BLE reliability · local notifications · tap-to-open · app badge | ✅ | E1 partial |
| E iPhone-app | CBCentralManager **State Restoration** (survive app *kill*) | 🔜 | deferred — needs persistent-session refactor + device test |
| E iPhone-app | **wake-on-message** advertising (D13) | ⏸ | firmware (Q15) |
| E iPhone-app | join / onboarding UX (D11) | ⏸ | R6 |
| E iPhone-app | iCloud backup · multi-node switcher · `bonds:N` multi-phone warning (D14) | ❌ | app |
| Hardening | `sender_hash` always-on (D5) · `now_ms` time anchor | ✅ | |
| Hardening | `ctr` persisted in NV (sender-reboot dedup collision, D7) | 🔜 | inbox-hardening agent |
| Crypto | E2E peer-key provisioning (wire + QR-key + auto-PIN) | ✅ | D20 — app side synced 2026-06-16 (fw crypto in progress) |
| Crypto | E2E surfacing UX (encrypt lock toggle · no-pubkey prompt · key-ready resend · `e2e_dm` setting) | ✅ | shipped 2026-06-16 |
| OTA | firmware update from the app (App "Phase 1") | ❌ | **never started — see §7** |

**Bench fixes landed 2026-06-13/14** (not roadmap features, but live): channel **self-echo** (node no longer
records its own channel posts), BLE **multi-notification chunking** (`tx_line` — `cfg`/wide-`status` no
longer truncate at one MTU), **map re-center** (`Map(position:)` reactive camera), **keyboard dismiss** on
the Node screen, battery `batt_raw` serial diagnostic.

**One-line read:** Themes **A, B1, C-fixed, D, E1** are code-complete and waiting on your bench. Everything
else is either a firmware dependency (R6/R7/R8, E2E, durable inbox), one open question (Q1b/Q4b/Q15), or
a deliberately-deferred refactor (State Restoration). **§7** lists what the roadmap doesn't yet cover at
all; **§8** consolidates every firmware feature the companion needs and what each unblocks.

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
| D15 | 2026-06-12 | **(Q4 — location)** Position format = **int32 degrees × 1e7** (`lat_e7`/`lon_e7`; 0,0 = unset; no float on the wire). Persisted in the **`/mrid` identity record** (appended after `name`, no version bump; strict load → a legacy `/mrid` is rejected on first boot and the node re-mints its identity — fine, dev system, user accepts hash change). The app sets it via `cfg set lat`/`lon` — and **`cfg set` is now wired over BLE** generally (the phone can set node config; MITM-paired trusted owner). |
| D14 | 2026-06-12 | **Amends D10 — two phones are NOT specially handled.** The app/node just WARN the user when more than one companion is bonded/has synced ("multiple phones paired — sync behavior is undefined"). Read state stays app-side per-phone (that part of D10 stands); no per-bond cursors, no multi-companion sync design. Closes Q12. Mechanism (cheap): `ready` gains a `bonds:N` count → app banners when N>1 (land with E1). |
| D16 | 2026-06-14 | **E2E ack is per-DM, OFF by default.** A DM may optionally request an end-to-end ack (firmware `send_ack`/`sendhash_ack` verbs / `E2E_ACK_REQ`) — **default OFF** to save airtime (an E2E ack is a full return DATA + the cross-layer reverse path on a gateway). Exposed as a **per-message** toggle in compose, NOT a global setting, so the user pays the round-trip only when delivery confirmation matters. App: the toggle + a distinct **"delivered (E2E)"** state (vs queued/link-acked). Firmware: the verbs exist; needs a **distinct push for the E2E-ack arrival** so the app can tell end-to-end delivery from the link `send_acked` (→ §8.2). |
| D17 | 2026-06-14 | **Cross-layer DM addressing = layer-path + final hash** (confirms gateway spec §5). A cross-layer DM is addressed by a **list of layers to traverse** (`layer_ids[]` + a `cur` cursor, preserved for the reversed-path E2E ack) **plus the final recipient `dst_key_hash32`**. v1 (2 layers, 1 gateway): the **sending node** computes the 2-element path `[A,B]` from a hash-locate answer (`target_layer`) — the **app still addresses by contact hash only** (layer-agnostic); the node does the cross-layer work. Multi-gateway transit (3+ layers) = who builds `[A,B,C]`? → Q16. Companion impact: address-by-hash unchanged; the app shows a message's `layer_id` (already decoded), surfaces `err_no_gateway`, and the known-nodes directory (§8.2) tracks each node's layer. |
| D22 | 2026-06-16 | **Per-message crypt verb scheme = Option A (confirmed).** `sendhash` = force plain · `sendhashx` = force crypt · `send <id>` = use the node `e2e_dm` default. **NOT** adding `send`/`sendx`: encryption is hash/pubkey-based, so "force encrypt" only makes sense once you HAVE the recipient's hash+key — i.e. the hash-addressed verbs. An id-thread is an UNRESOLVED contact (no pubkey ⇒ a forced encrypt would just fail), so `send <id>` correctly = "node default". Maps 1:1 to the app: the compose lock toggle shows ONLY on hash threads (`canEncrypt`); id-threads hide it + use the default. **No app change needed** (already implemented this way). Minor: an id-send under `e2e_dm` may seal while the app shows plaintext (it can't know pre-resolution) — acceptable, id-threads are transient pre-resolution. |
| D21 | 2026-06-16 | **Message markers (implemented 2026-06-16).** Each DM bubble now shows: (1) the node **counter** `#ctr` (small caption); (2) a **crypted/plaintext lock** (closed-green = E2E-sealed · open = plaintext); (3) an **E2E-ack-requested** seal on outgoing. Data sources: counter = `MessageEntity.ctr` (already had it); ack = a **per-message compose toggle** (off by default, realizes D16) → `requestAck` → `send_ack`/`sendhash_ack`; crypted = `MessageEntity.crypted` decoded from the optional **`enc`** field on `msg_recv`/`inbox_dm` (wire field = `enc` per the 2026-06-16 contract; app decodes it now; firmware ask in §8.2). Outgoing-crypted stays false until the per-message crypt SEND is wired in the app. **Send form CONFIRMED 2026-06-16: `sendhashx`/`sendhashx_ack` verbs** (hash-only) — `SendDM.encrypt` + the verb mapping shipped in the app; what remains is the compose **lock toggle** → `encrypt` → outgoing-crypted marker (E2E surfacing slice). Distinct from the E2E-ack toggle already shipped. |
| D20 | 2026-06-16 | **E2E peer-key provisioning — companion contract (firmware crypto in progress).** App stays crypto-free (D6) — carries opaque pubkey bytes. node→app: `ready.pubkey` (ed_pub 64-hex → My-card QR `&p=`), `send_failed{reason∈ no_pubkey/no_identity/too_large/bad_rng/no_route}`, `peerkey_set`/`peerkey_err`/`reqpubkey_sent`/`peer_key_cached`. app→node: `peerkey <hex64>` (install a scanned card's key, **PINNED**) + `reqpubkey <hex8>` (on-air request). **App DONE 2026-06-16:** wire decode + My-card emits the pubkey + scanning a card with a pubkey auto-provisions it. **App E2E surfacing UX — SHIPPED 2026-06-16:** per-message **encrypt lock toggle** (`sendhashx`, hash-only, defaults to the `e2e_dm` setting) → outgoing crypted marker; `send_failed{no_pubkey}` → a **"Request key"** action (`reqpubkey`) on the failed bubble; `peer_key_cached` → the bubble flips to **"Key ready — resend securely"**; an **"Encrypt DMs by default"** toggle (Node tab → `cfg set e2e_dm` + an app default). Remaining: read `e2e_dm` back from `cfg` to reflect the node's actual default (§8.2). Refs: `2026-06-16-e2e-peer-key-provisioning.md`, `2026-06-15-phase1-e2e-dm-crypto.md`, INBOX_SYNC §Verified-peer provisioning. |
| D19 | 2026-06-14 | **(Firmware, coding agent — companion chose model A) Unified send-handle on `CmdResult`.** Every send verb returns `{code, ctr, dst_hash, layer_path}`. `send_layer`'s no-gateway/overflow becomes a **synchronous error** (not an orphan `dst=0,ctr=0` push). This reconciles the earlier "put `dst_hash` on the Push" idea — NOT needed: the app correlates by **`ctr`** (its existing model — ack→ctr→message, then async `send_acked`/`send_failed` match by ctr), so **no Push/TxItem identity threading** (the field-threading trap). App side: add optional `dst_hash`/`layer_path` (decimal u32) to the ack decode + a `ctr→(hash,path)` map; both 0 in v1 (`send`/`sendhash` auto-route). `layer_path` packing: hops right-aligned, `hop[0]` high byte (`(2<<8)|3 = 0x0203` for `[2,3]`); 0 = none (layer ids ≥1 ⇒ unambiguous). Open: `send_layer` arg format + the sync-before-async ordering guarantee. |
| D18 | 2026-06-14 | **Cross-layer addressing UX = auto-first, manual override is advanced.** The common case stays **zero-knowledge** — the user addresses by contact hash; the node resolves the layer + path via **live hash-locate** (authoritative). v1 needs NO manual addressing. Two additive aids: (a) the **QR card gains an optional `&l=<layer_id>` hint** (forward-compat like `&p=`) — a *seed/display hint only*, NEVER overrides the live H query (a stored/card layer goes stale: mobiles move, re-provisioning); (b) a contact gains an **optional "layer path" override** — an **advanced** field for **multi-gateway (3+ layers, Q16)** where auto-discovery can't build the chain; needs a firmware **send-with-explicit-path verb** (§8.2). The sender's OWN layer is the implicit origin, never part of the composed chain. |

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
  `https://meshroute.eu/c?v=1&h=<hex8>&n=<name>[&p=<ed_pub hex64, RESERVED B2/E2E>][&l=<layer_id> RESERVED hint, D18]` —
  `ContactCard` in MeshRouteCore (tested); `meshroute://contact` legacy alias accepted; unknown params
  never fail the parse. (`&l=` = a display/seed hint for cross-layer; the live H query stays authoritative.) https so a STOCK-camera scan can Universal-Link into the app once the domain
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

### C — Location & map *(order #6)* — per **D2 + D8** — **fixed-node SET/SHOW SHIPPED 2026-06-12**
- **Format (Q4 → decided):** **int32 degrees × 1e7** ("no float on wire"; `lat_e7`/`lon_e7`, 0,0 = unset).
- **Fixed-node location** ✅ — firmware: persisted in the **`/mrid`** record (appended after `name`,
  no version bump; reflash re-mints identity — fine, dev system); device globals `g_lat_e7`/`g_lon_e7`;
  `cfg set lat`/`lon <degrees>` (serial + **over
  BLE** — `cfg set` is now wired into `ble_dispatch_line`, replying with the fresh cfg); `dump_cfg` +
  help updated; `cfg` JSON emits `lat_e7`/`lon_e7`. App: `NodeConfigInfo.latitude/longitude/hasPosition`;
  Node-tab **Location row + MapKit preview** + a set sheet (manual entry **or** "Use my current
  location" via CoreLocation). Mock round-trips `cfg set lat/lon`.
- **Still TODO:** the **BCN ext TLV** broadcast (so PEERS see each other's positions — needs firmware
  beacon work + a position table) and the **mobile** phone-fed path (D8); then the map shows the whole
  mesh, not just the node you're connected to. Cadence: fixed every Nth beacon; mobile per-beacon/on-move.
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
  **Tap-to-open** ✅ — `NotificationRouter` (UNUserNotificationCenterDelegate) parses `dm-<hash>` →
  `AppModel.openConversation` switches to Messages + pushes the thread (`selectedTab`/`messagesPath`
  hoisted to AppModel; ThreadsList NavigationStack bound to it). **App-icon badge** ✅ mirrors unread.
- **Still TODO for E1:** CBCentralManager State Restoration — survive app *termination* (deferred: needs
  a persistent-BLE-session refactor of connect/disconnect + on-device testing across termination); and
  the firmware D4/D13 wake-on-message advertising.
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
| Q4b | **Position BCN-ext cadence** (the broadcast half, still TODO): fixed = every Nth beacon (N?); mobile = per-beacon or an on-move threshold? (Format settled → D15: int32 ×1e7.) | C broadcast |
| Q7 | **Ratify the D4 default:** window pinned while a BLE central is connected, + ~2 min linger after disconnect. | E1 (fw) |
| Q9 | **Channel naming:** local labels v1 (assumed yes); leaf-level shared directory ever? | A |
| ~~Q12~~ | Closed by **D14**: read state app-side per-phone; multi-phone = warn-only, no design. | — |
| Q14 | **`ready` shape for gateways** (two per-layer node_ids): additive `"layers":[{"layer_id":N,"id":M},…]` keeping the existing `"id"` for single-layer compat? App decoding is tolerant either way — settle when R7 lands. | D (gateway era) |
| Q15 | **Wake-on-message params (D13):** triggers = DMs only, or channel msgs too? Advert duration (prop.: 2 min)? Only when a companion bond exists (prop.: yes)? Re-arm suppression so a burst doesn't re-advertise per message (prop.: one window per burst)? | E1 (fw) |
| Q16 | **Multi-gateway cross-layer path (D17):** for 3+ layers, who builds the `[A,B,C]` layer-path — the **sending node** (needs a cross-layer topology view it doesn't have today), a **directory/gateway service**, or the **app**? v1 is single-gateway (the node computes `[A,B]` from a hash-locate `target_layer`). | R7+ cross-layer |

*(Q13 → D12, resolved in the gateway spec 2026-06-12.)*

## 7. Missing / proposed features (gap analysis 2026-06-14)

Themes A–E cover the *messenger*. These are the gaps — capabilities the roadmap doesn't yet name —
grouped by persona. Effort **S/M/L**; **app-only** = no firmware. ⭐ = recommended high-value.

### 7.1 Operator / fleet management (persona 2 — the biggest blank area)
- ⭐ **Remote node administration** *(user-proposed; fw L, app M)* — admin OTHER nodes over the mesh
  from the phone, not just the BLE-connected one: remote `cfg get/set`, `status`, `reboot`, `regen`,
  set-location. Mechanism: a new **`TYPE=ADMIN` DATA frame**, **signed by the owner's key** so a node
  only obeys its owner (the node verifies the Ed25519 signature — fits D6: the *node* does crypto, the
  app carries opaque bytes). Open design: the **authorization model** — is "owner" the identity that
  provisioned it, a per-node admin pubkey, or a leaf-admin key? → needs a decision + a small spec.
- ⭐ **OTA firmware update from the app** *(App "Phase 1" — never started; app L)* — update the
  connected node over BLE (Nordic DFU / the bootloader we already ship). Critical for a deployed mesh.
  Stretch: **mesh-relayed OTA** to remote nodes (bandwidth-bounded, ambitious — a later epic).
- **Fleet dashboard** *(app M, needs the §7.5 node directory)* — every known node: up/down, battery,
  last-seen, role, link quality. The operator's "is my network healthy" home screen.
- **Proactive alerts** *(app M + a little fw)* — node went silent / battery low / duty-cycle exhausted
  → a push notification. Turns the operator from polling to being-told.
- **Node provisioning wizard** *(app M)* — bring up a NEW node end-to-end from the phone: name, join a
  leaf, set role (fixed/mobile/gateway), set location. Extends D11 join + the `cfg set`-over-BLE we have.

### 7.2 Safety / off-grid (persona 1 — flagship potential)
- ⭐ **SOS / emergency beacon** *(fw M, app S)* — one tap → a **high-priority flood** carrying your
  location + a preset distress message, repeated on a schedule until cancelled. The data plane already
  has priority classes; this could be THE reason a hiking/sailing group buys in. Needs a priority/SOS
  flag + a guarded UI.
- **Share location in a message** *(fw S, app S)* — drop your current position into a DM/channel (a
  `TYPE=LOCATION` inline, or just text + the map renders it). Cheap, very useful off-grid.
- **Check-in / "I'm OK"** *(app M)* — manual or periodic location+status ping to a group; surfaces
  "last heard from X: 12 min ago".
- **Compass / bearing + distance to a contact** *(app S)* — when all you have is last-known positions,
  point-me-to-Marek beats a blank map. (The distance/bearing list under Theme C is the seed.)
- **Geofence / proximity alerts** *(app M)* — notify when a contact comes within / leaves a radius.

### 7.3 Messaging depth
- ⭐ **Per-message E2E-ack toggle** *(D16; app S — the `*_ack` verbs already exist + a small fw push)* —
  "request delivery confirmation" in the compose bar, **OFF by default** (an E2E ack costs a full return
  DATA), with a distinct **"delivered (end-to-end)"** state vs link-acked. Near-term + mostly app-side.
- **Reply / quote a specific message** *(app S local; M if echoed on the wire)*.
- **Message search** *(app S)* · **local delete / archive** *(app S)* · **drafts** *(app S)* — all
  app-only quick wins.
- **Reactions** *(app S local, or a tiny wire TYPE)*.
- **Priority surfaced in the UI** *(app S + fw flag)* — normal / high / emergency send classes (the
  data plane supports priority today; the app doesn't expose it).
- **Read receipts** *(needs an app↔app receipt — airtime cost)* — likely opt-in or skip.

### 7.4 Identity / trust / security
- **E2E encryption UX** *(app M, gated on the firmware E2E slice)* — a lock icon, "keys exchanged via
  card", the per-thread encrypted state. The wire (`CRYPTED`) is reserved; surface it when it lands.
- **Pubkey verification ("safety numbers")** *(app S, after cards carry `ed_pub`)* — compare full keys
  in person to defeat the TOFU/MITM gap the identity spec flags.
- **Block / mute** a contact or channel *(app S)*.
- **Paired-phone (bond) management** *(app S + `bonds:N` from D14)* — list / revoke bonded phones.
- **Rename my own node from the app** *(app S — transport exists)* — `cfg set name` over BLE is wired;
  just needs a field (today only settable on serial).

### 7.5 Known-nodes directory (a missing foundation)
- **A persisted directory of every node the app has heard of** *(app M)* — keyed by `key_hash32`:
  name, last-known role, leaf, position, battery, last-seen. PORT_PLAN's "App layer / known-nodes
  directory" — several features above (fleet dashboard, map, alerts) depend on it. Currently the app
  only models *contacts* (manual) + the *connected* node; there's no model of "the whole mesh."

### 7.6 Platform / polish
- **Settings screen** *(app S)* — notification prefs, units (km/mi), BLE PIN entry, theme. (None exist.)
- **Android companion** *(L)* — v1 is iOS-only; the `MeshRouteKit` split was designed to make a second
  client cheap, but it's a whole app.
- **Apple Watch / Live Activity / home-screen widget** *(app M)* — glanceable "connected · 2 unread".
- **Siri shortcuts / App Intents** *(app M)* — "message Marek on the mesh".
- **Backup & export** *(app M)* — iCloud (noted in E) + export chat/contacts.
- **Localization / accessibility / iPad layout** *(app M)*.

### 7.7 Top picks (if we want to prioritize the gaps)
1. **Known-nodes directory (§7.5)** — the missing data foundation that unblocks the map, fleet view,
   and alerts. Do this with/before the Theme-C broadcast half.
2. **OTA from the app (§7.1)** — highest operator value, and we already ship a DFU bootloader.
3. **Remote node administration (§7.1)** — your example; needs the authorization-model decision first.
4. **SOS / emergency beacon (§7.2)** — the off-grid flagship; small once a priority/SOS flag exists.
5. **Settings + rename-my-node + block/mute (§7.3/7.4/7.6)** — a cluster of app-only quick wins.

These are **proposals, not commitments** — promote any into Themes A–E (or a new Theme F "Operator /
fleet") and the §5 sequencing when you pick them up.

## 8. Firmware features the companion needs (and what each unblocks)

Consolidates EVERY firmware-side dependency in one place — both the existing port tracks and the
**companion-specific asks that aren't in any firmware backlog yet** (§8.2 is the new, actionable list
to hand the node agent). Status: **✅ done** · **🔧 designed / in-progress (other agent)** · **❌ not
started**. "Unblocks" = the companion feature(s) that cannot ship without it.

### 8.1 Already on the firmware roadmap / PORT_PLAN
| Firmware item | Status | Unblocks (companion) |
|---|---|---|
| `now_ms` in `ready`/`inbox_end` | ✅ | real timestamps (A) |
| `sender_hash` / `channel_msg_id` / `seq` / `layer_id` on pushes + inbox | ✅ | dedup, model-B gap recovery, layer tagging |
| `cfg set` + `status`/`cfg`/`routes` JSON over BLE | ✅ | Node + Network screens (D), set location, rename-node |
| **Durable QSPI inbox** (inbox agent) | 🔧 | message history surviving reboot (today RAM-volatile) |
| **`ctr` persisted in NV** (D7) | 🔧 | reliable dedup across sender reboots |
| **R6 leaf-config join** | ❌ | join / onboarding UX (D11); leaf panel name/epoch/members (D) |
| **R7 gateway dual-layer** | 🔧 designed | gateway UI (`layers[]`, per-layer node_id, window schedule); layer-scoped channels |
| **R8 mobile nodes** | ❌ | mobile phone-fed position; mobiles on the map |
| **E2E DM crypto (`CRYPTED` slice)** | 🔧 wire-reserved | E2E UX / lock icon; pubkey verification |

### 8.2 Companion-specific firmware asks — **NOT yet in any firmware backlog (add these)**
| Firmware ask | Effort | Unblocks |
|---|---|---|
| ⭐ **BCN ext-TLV position (+ optional health) broadcast** — nodes share lat/lon on the air | M | peers-on-a-map (C broadcast); fleet location |
| ⭐ **Known-nodes table queryable over BLE** — per heard node: `key_hash32`/id/name/role(gw/mobile)/leaf/last-seen/position/battery (aggregates id_bind + beacons + the position broadcast) | M | known-nodes directory (§7.5) → **map, fleet dashboard, alerts all depend on this** |
| **Wake-on-message BLE advertising** (D13) **+ activity pins the window open** (D4) | M | low-latency background notifications; interactive sessions don't drop BLE (E1) |
| **`bonds:N` count in `ready`** (D14) | S | multi-phone "undefined behaviour" warning |
| **`TYPE=CONTACT_CARD` DATA frame** + node-side sign/verify + cache-on-pass | M | contact card over mesh (B2); pubkey distribution for E2E |
| ⭐ **`TYPE=ADMIN` signed DATA frame** + node-side owner-authorization | L | **remote node administration** (§7.1) |
| ⭐ **App-facing OTA trigger** (enter-DFU verb/cfg over BLE; later mesh-relayed OTA) | M / L | **OTA from the app** (§7.1) |
| **Distinct E2E-ack push event** (vs the link `send_acked`) so the app can show end-to-end delivery | S | per-message E2E ack "delivered (E2E)" state (D16) |
| **`enc` flag on `msg_recv` + `inbox_dm`** (the node already decodes `o.crypted` — just surface it; field name = `enc`) | S | the per-message crypted lock icon (D21) |
| **`e2e_dm` (+ `loc_in_dm`) in the `cfg` JSON** (`cfg set e2e_dm` exists; just add to `write_cfg`) | S | the app reflects the node's actual encrypt-default (app decodes `e2e_dm` already) |
| **Send-with-explicit-layer-path verb** (app supplies `layer_ids[]`; node doesn't auto-compute) | M | manual cross-layer addressing / contact layer-path override (D18, multi-gateway) |
| **Priority / SOS send class + high-priority flood** (the data plane HAS priority — expose it) | M | SOS / emergency beacon (§7.2); priority messaging |
| **`TYPE=LOCATION` inline payload** in a DM/channel | S | share-location-in-a-message (§7.2) |
| **Node telemetry / event-log pull over BLE** (recent events, drops, errors) | M | remote debugging; logs view; alert sources |
| **Node-side alert events** (battery-low / duty-exhausted / peer-dead) pushed to BLE | S | proactive alerts (§7.1) |
| **Channel directory / subscription** (PORT_PLAN "channel subscriptions") | M | channel management UI |
| **Dynamic leaf-config write path** (R6.3) | M | leaf management UI (operator) |

### 8.3 Decisions these firmware asks need FIRST (block the spec)
- **Remote admin (`TYPE=ADMIN`) authorization model** — who is "owner"? provisioning identity / a
  per-node admin pubkey / a leaf-admin key. Blocks the admin frame + remote admin (§7.1).
- **OTA scope** — connected-node-over-BLE first, mesh-relayed later? Shapes the trigger + the epic size.
- **Position broadcast format + privacy** — Q4b cadence + per-contact opt-in / precision-degrade / TTL.
  Blocks the BCN ext-TLV.
- **SOS semantics** — repeat cadence, cancel, audience (leaf-flood?). Blocks the SOS class.

**Critical-path note:** the **known-nodes table over BLE (§8.2)** is the quiet keystone — the Map,
fleet dashboard, and alerts in §7 all read from it. It's worth specifying *before* those app features,
and it composes naturally with the position broadcast and (later) R6/R7 role/leaf data.
