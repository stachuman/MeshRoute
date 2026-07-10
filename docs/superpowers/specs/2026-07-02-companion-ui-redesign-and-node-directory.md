# Companion UI redesign + Known-Nodes Directory (the "Mesh" epic) — LIVING SPEC

**Status:** LIVING — the single working document for the main-window redesign and the whole-mesh
("2b") epic. We iterate here; the roadmap (`2026-06-12-companion-product-roadmap.md`) keeps the one-line
decision pointer (D28) and the status dashboard.
**Authors:** Stani + app agent (firmware asks coordinated with the node agent).
**Refs:** `2026-06-12-companion-product-roadmap.md` (§7 gap analysis, §8 firmware asks) ·
`ios-companion/INBOX_SYNC_CONTRACT.md` · `2026-06-12-gateway-dual-layer-design.md` (layer_id).

## 0. Decisions locked (2026-07-02)

- **D-a — 4-tab shell:** **Messages · Contacts · Mesh · Device** (not 5 — a 5th tab overflows into iOS "More").
- **D-b — unified node model (Option A):** ONE `NodeEntity` keyed by `key_hash32`; a **contact is a named/favorited node**, not a separate store. Subsumes today's `ContactEntity`.
- **D-c — phased, app-first:** ship the shell + directory + a v1 Mesh from *have-now* data immediately; the rich map/fleet data rides two **firmware asks** specified here in parallel (§6). D27 (duty gauge) lands in this pass (§4).

**Open items resolved 2026-07-02** (the §8 leans adopted): a **contact = a named/favorited node** (§3.1); **`NodeProfileEntity` stays separate** (§3.2); id-only nodes = **provisional pseudo-id rows merged on `hash_resolved`** (§8.1); offline maps = **the bearing list is the off-grid view, no tile story** (§8.3); **alerts are Phase 3** (§8.6).

## 1. Why

Two problems, one redesign:
1. **The Node tab is a kitchen sink** (10 sections: connection, identity, status, config, location, membership, routes, security, diagnostics, mock, console). Every new firmware surface (duty, OTA, remote-admin) piles on. The debt only grows.
2. **The app has no model of the mesh.** It knows *contacts* (manually named) + the *connected* node. There is no "every node I've heard of" — which is the foundation the **map, fleet dashboard, and proactive alerts** (roadmap §7.1/§7.5) all read from. That directory is the "quiet keystone."

The redesign pays the debt *and* lays the keystone in one coherent move.

## 2. Information architecture — 4 tabs

| Tab | Owns | Persona |
|---|---|---|
| **Messages** | conversations (DMs + channels) — unchanged | off-grid group (1) |
| **Contacts** | named people (a *filter* over the directory: the named/favorited nodes) | off-grid group (1) |
| **Mesh** ★ new | the whole network — **Map ⇄ List** over the node directory | operator (2) + group (1) |
| **Device** | the connected node (my device) + app Settings | builder (3) + operator (2) |

**Cross-links dissolve the Contacts↔Mesh overlap** (they are the same rows, filtered):
- A **Mesh** node's detail → *Message · Add contact (name it) · Show on map*.
- A **Contacts** row → *Message · Show on map · Verify key*.

### 2.1 What moves where (from today's Node tab)

| Today (Node tab) | Goes to |
|---|---|
| Connection · Identity | **Device** → *Connection*, *Identity* (Identity gains **My card** share + **rename node**) |
| Status · Config · Routes | **Device** → a *Details* disclosure (my node's own telemetry) |
| Location (set/map preview) | **Device** → *Identity* (it's my node's position); the node also appears on the **Mesh** map |
| Network membership + Duty (D27) | **Device** → *Network* section (chip + Join/Create/Leave + duty gauge) |
| Security (encrypt-by-default) | **Device** → *Security* |
| Diagnostics · Mock demo · Console | **Device** → an *Advanced* disclosure (collapsed by default) |
| — (new) | **Device** → *Settings* (app prefs: notifications, units, theme, BLE PIN) |

Net: Device goes from a flat 10-section list to ~6 grouped sections, most collapsed.

## 3. Data model — the unified Node directory (Option A)

### 3.1 `NodeEntity` (new `@Model`, keyed by `key_hash32`)

```
NodeEntity
  hash32        UInt32   @Attribute(.unique)   // key_hash32 — the stable identity; the directory key
  name          String?                        // user-given name ⇒ this node is a "contact"; nil = heard-only
  favorite      Bool                           // pinned/favorited (a contact even without a custom name)
  lastKnownID   Int?                           // most recent short id (reassignable; from resolve / heard origin)
  role          String                         // "normal" | "gateway" | "mobile" | "unknown"
  lineage       Int?                           // leaf membership (0 = unmanaged; nil = unknown)
  leafName      String?
  level         Int?
  latE7, lonE7  Int?                           // last-known position (nil = unknown) — firmware-gated for remotes
  battMv        Int?                           // last-known battery — firmware-gated
  linkScoreQ4   Int?                           // route score (Q4 dB) when reachable
  hops          Int?                           // route distance when reachable
  verified      Bool                           // key PINNED via QR (safety-numbers ceremony) vs TOFU
  firstSeen     Date
  lastSeen      Date                           // any evidence (message, beacon, resolve, route, ready)
  positionAt    Date?                          // when lat/lon was last updated (drives staleness / TTL)
```

- **A "contact" = `name != nil || favorite`.** The Contacts tab = `NodeEntity` where that holds, sorted by name. No separate table.
- **DM threads stay keyed by hash** (`MessageEntity.threadHash`) — unchanged. The thread *title* now resolves via `NodeEntity.name` (was `ContactEntity.name`).
- The **connected node itself** is a `NodeEntity` (role from `ready`; `verified = true`; it's "me" — flag or derive from `nodeIdentity.key`).

### 3.2 Migration (from `ContactEntity`)

- One-time: for each `ContactEntity` → upsert a `NodeEntity{hash32, name, favorite:true, lastKnownID, firstSeen:createdAt, lastSeen:createdAt, role:"unknown", verified:<pinned?>}`, then retire `ContactEntity`.
- `MessageEntity` / `ChannelLabelEntity` / `NodeProfileEntity` are **unchanged**. (`NodeProfileEntity` = per-connected-node inbox-sync cursors — orthogonal to the directory; keep separate.)
- SwiftData: additive new model + a lightweight one-time copy at launch; drop `ContactEntity` reads after the copy. (Migration mechanics = an open item, §8.)

### 3.3 What populates it

**Have now (app-only, Phase 1):**
| Source | Fills |
|---|---|
| migrated contacts | hash, name, favorite |
| connected node `ready` | the "me" node: hash, id, name, role, membership |
| `hash_resolved` / id-binding | hash ↔ `lastKnownID` |
| inbound DM `sender_hash` (+origin) | a heard node (replaces today's auto-"Node N" contact) |
| channel minter (`channel_msg_id>>24`) | a heard origin id |
| `route` rows | reachable id + hops + score + gw flag + layer (⚠ id-only until resolved — see §8) |
| my node `cfg` lat/lon | the connected node's position |

**Needs firmware (Phase 3, §6):** remote-node **position**, **battery/health**, and a one-shot **known-nodes table** so the directory is populated proactively (not only from nodes that happen to message/route).

## 4. Device tab (de-cluttered) + D27 duty gauge

Sections (most collapsed): **Connection · Identity** (name, hash, My-card, **rename node** via `cfg set name`) **· Network** (membership chip + Join/Create/Leave + **duty gauge**) **· Security · Details** (status/config/routes disclosure) **· Settings · Advanced** (diagnostics/console/mock).

- **D27 duty gauge (this pass):** `duty` command + `ready.duty_pct`/`duty_avail_ms` → a transmitting/silent gauge + a silent-countdown that polls `duty` while on screen. Lands in *Network*.
- **Settings (new home for §7.6):** notification prefs, units (km/mi), theme, BLE PIN entry.

## 5. Mesh tab — Map ⇄ List

A segmented toggle over the one directory.

- **List:** every `NodeEntity`, sortable (name · last-seen · battery · leaf · link) + filters (All · Named · Nearby · Gateways · This leaf). Row: name/hash, role glyph, leaf, last-seen, battery, reachability.
- **Map:** MapKit pins for nodes with a position; tap → node detail. A **distance/bearing list fallback** ("Marek — 2.1 km NE, 12 min ago") that works with **no map tiles** (matters off-grid).
- **Node detail** (shared by Mesh + Contacts): *Message · Add/Rename contact · Verify key (safety numbers) · Show on map · Set location (if mine) · Remote-diag (`rcmd`) · Block/Mute*.
  - This screen is where the parked app-only wins land: **safety-numbers/verify** + **block/mute** (roadmap §7.4).

**v1 (Phase 1–2, app-only):** List from have-now data; Map shows fixed nodes (their `cfg` lat/lon) + the bearing fallback. **v2 (Phase 3):** the firmware broadcast fills positions/health for the whole mesh.

## 6. Firmware asks (parallel — for the node agent)

These are the keystone; the app ships Phase 1–2 without them, then *gets richer* when they land (no rework).

### 6.1 Position broadcast — BCN ext-TLV (roadmap §8.2)
Nodes share `lat_e7`/`lon_e7` (+ optional role/battery) on the air via the beacon's trailing ext-TLV rail, so peers build each other's positions. **Cadence (Q4b):** fixed = every Nth beacon (N?); mobile = per-beacon or an on-move threshold. **Privacy:** per-contact opt-in · precision degrade (exact/±500 m/off) · TTL on stored positions. → unblocks peers-on-a-map + fleet location.

### 6.2 Known-nodes table over BLE (roadmap §8.2 — the keystone)
A query that returns, per heard node, the directory row the app can't fully build itself:
`key_hash32 · id · name? · role(gw/mobile/normal) · lineage/leaf · level · last_seen · position · battery · link`.
Aggregates id_bind + beacons + the §6.1 broadcast. → proactively populates the directory ⇒ **map, fleet dashboard, and alerts all depend on this.**

Proposed shape (to finalize with the node agent): a `nodes` command → a streamed `{"ev":"node",…}`×N + `nodes_end` (mirrors the `routes` / `pull_inbox` stream pattern the app already consumes).

## 7. Phasing

- **Phase 1 (app-only): shell + directory spine + D27. ✅ DONE 2026-07-02.** 4-tab shell (`Messages · Contacts · Mesh · Device`) · unified `NodeEntity` + `ContactEntity→NodeEntity` migration (a contact = a named node; heard nodes are directory rows) · **Mesh** tab = the directory List (filter All/Contacts/Reachable + search) + a node-detail (message/rename/resolve) · **Device** de-cluttered (Connection · Identity+rename · Network[membership+**duty gauge**+routes] · Security · a *Diagnostics & details* subscreen) · **D27** duty gauge polling on screen. 84 wire tests, app builds. *Sources wired now: contacts, DM senders, resolves. Deferred: route-sourced rows (the §8.1 pseudo-id merge).*
- **Phase 2 (app-only): Map v1 + node detail + Settings.** Fixed-node map + bearing fallback · node-detail screen (verify/block/rename/rcmd) · Settings section.
- **Phase 3 (firmware-gated): the rich mesh.** §6.1 position broadcast + §6.2 known-nodes table → whole-mesh map/fleet · then proactive **alerts** (battery-low / duty-exhausted / peer-dead) as a follow-on.

## 8. Open questions (resolve as we go)

1. **Directory key for id-only nodes:** route dests arrive as a short *id* (no hash until a resolve). Mirror the DM thread-key trick (hash when known, else a pseudo-id row that merges on resolve), or omit id-only rows from the directory until resolved? *(lean: provisional pseudo-id rows, merged on `hash_resolved`.)*
2. **Retention / pruning:** how long to keep a heard node with no recent evidence? A `lastSeen` TTL + a manual "forget"? Favorites/contacts never pruned.
3. **Map offline tiles:** MapKit needs network for tiles — off-grid it degrades to a blank map. Acceptable (bearing list is the real off-grid view), or do we need a cached/offline tile story?
4. **SwiftData migration** of `ContactEntity` → `NodeEntity`: lightweight additive + one-time copy, or a versioned migration plan? Any risk to shipped contacts on-device.
5. **Position privacy** (§6.1) — the opt-in/precision/TTL model is Q4b; blocks the broadcast spec.
6. **Alerts scope** (§7.1) — in this epic (Phase 3) or a separate follow-on spec?
7. **"Me" vs peers on the map** — the connected node is a `NodeEntity`; does it get special styling / always-centered?

## 9. Non-goals (this spec)

- Gateway dual-layer UI (R7) — firmware-gated.
- **Mobile nodes + teams (D29)** — a future theme once the JSON surface lands: the directory already models **role=mobile** (§3.1), **team channels** (`team_id`-tagged) thread into a team view on the Messages side, and a mobile roam/register UI is separate. Messaging a mobile is plain `send <hash>` (no app change).
- **Anti-spam send-pacing (D29)** — a messaging-reliability slice (decode `send_blocked`/`channel_sent`/`limits`, back off, "retry in N s") tracked in the roadmap, not part of this UI redesign.
- *Terminology note (D29, done):* this spec's `level` reads as **`layer`** everywhere now (the 1..255 net id; a *leaf* = its `& 0x0F` nibble); the `join`/`create` verbs are `key=value` with fractional `bw`/`duty`.
- OTA / remote-admin *write* path — the node detail exposes `rcmd` *read* diagnostics; the signed `TYPE=ADMIN` write path is its own spec (roadmap §7.1) with the authorization-model decision.
- Message-depth features (search/reply/drafts/reactions) — app-only quick wins, tracked in roadmap §7.3; can slot into Messages independently.
