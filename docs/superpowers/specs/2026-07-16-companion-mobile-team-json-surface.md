# Companion mobile/team JSON surface — firmware spec

*2026-07-16. For the firmware coder. Closes the gap the inbox contract flags as "the app can send/receive
on the team plane but cannot yet **observe** mobile/team state as JSON". Every claim below re-verified
against the working tree today (`console_json.cpp`, `console_parse.cpp`, `firmware_config.cpp`,
`node.h`, `node_channel.cpp`, `node_mobile.cpp`, `inbox.h`). Contract:
`ios-companion/INBOX_SYNC_CONTRACT.md` §*Mobile node + teams* — this spec **supersedes the PROPOSED
sketches there where shapes differ** (deltas called out inline).*

## 0. Why now / scope

The mobile+team **routing spine is live** (`send -t`, `reqpubkey -t`, team channel, team-DAD, mobile
registration — committed `7053970`), and mobile **liveness detection** is landing next. But the
app-facing **state JSON does not exist**: `ready`/`status` carry no mobile/team fields, `mobile
status`/`mobile gateways`/`nameof` answer in human console text (`firmware_config.cpp:577-645`,
`firmware_commands.cpp:406`), there is no registration push, and a team channel message is
indistinguishable from a leaf channel message in both `channel_recv` and the durable inbox.

**The T1000-E angle (assumption-stage, `meshroute-t1000e-feasibility`):** the proposed SenseCAP
T1000-E variant is a **screenless, buttonless, mobile-only** tracker — BLE + the companion app is its
*entire* management UI. For that device this JSON surface is not a nicety; it is the only way to see
"am I registered, to whom, on which network, in which team". Everything below therefore must compile
and run on `MR_PROFILE_MOBILE` (it does by construction — see §8 guards), and nothing below assumes a
USB console. No new firmware work is added *for* the T1000-E here; this spec is simply the
prerequisite observability layer that variant would inherit for free.

**In scope:** JSON writers + push plumbing + one durable-record field. **Out of scope:** any
routing/protocol behaviour change, any new OTA frame, the liveness mechanism itself (separate work —
but S2 defines the push it must fire), app-side Swift (a separate agent owns `ios-companion/`).

## 1. Wire conventions (bind all slices)

- app→node = line-ASCII verbs; node→app = one JSON object per line (the established framing).
- **No floats on the wire** (newlib-nano printf; the `write_status` precedent): frequencies ride as
  integer `freq_khz`, bandwidth as integer `bw_hz`.
- **`team_id` rides as a quoted lowercase hex string, exactly like `key`** (`key_hex32`,
  `console_json.cpp:264` — `"%08x"`). Rationale: the user *types* it as hex (`team <hex_id>`,
  `cfg set team_id <hex>`), and it is a key-derived 32-bit hash like `key`. ⚠ **Contract delta:** the
  contract sketch showed `"team":305419896` decimal — hex string wins; update the contract when this
  lands.
- **`hash` fields stay decimal u32** (matches `peer_key_cached`/`reqpubkey_sent`/`sender_hash` — the
  app already parses decimal hashes; don't fork the convention mid-stream).
- **Omit-when-inactive** for all new `ready` fields (the `enc`/`seq`/`leaf`/`layers` convention): a
  static, teamless node's `ready` line stays **byte-identical**, so existing native goldens for
  static nodes don't churn and old app versions see nothing new.
- ids (`home`, `local`, `gw`, `layer`) = bare decimal u8, as everywhere else.

## 2. S1 — `ready` + `cfg` gain the mobile/team state (the on-connect snapshot)

### `ready` (writer `write_ready`, `console_json.cpp:288`)

```json
{"ev":"ready", … ,
 "mobile":true,              // OMIT unless c.is_mobile
 "mobile_registered":true,   // OMIT unless mobile — g_node.mobile_registered()
 "mobile_home":222,          // OMIT unless mobile — mobile_home_id() (0 = unregistered)
 "mobile_local":17,          // OMIT unless mobile — mobile_local_id()
 "mobile_home_layer":4,      // OMIT unless mobile+registered — mobile_home_layer()
 "hosting":2,                // OMIT when 0 — mobile_reg_count() (static host: mobiles registered to us)
 "team":"cccc0001",          // OMIT when c.team_id==0 — key_hex32 style
 "team_local":9}             // OMIT when team_local_id()==0 — the OWN id on the team overlay
```

- Sources are all existing accessors: `node.h:223-243` (`mobile_reg_count`, `mobile_home_id`,
  `mobile_registered`, `mobile_local_id`, `mobile_home_layer`) + `node.h:157` (`team_local_id`)
  + `_cfg.is_mobile`/`_cfg.team_id`. The `!MR_FEAT_MOBILE` stubs (`node.h:238-243`) return 0/false,
  so the writer may call them unconditionally — the omit rules then make a static/gateway build emit
  nothing, with **no `#if` in console_json**.
- **`team_local` is new vs the contract sketch** (it had only `team`). The app needs its *own*
  team-overlay id to model the ★ HARD PLANE SPLIT (a teammate addresses *us* by it; the compose UI
  shows "you are member 9") — without it the app can observe everyone's plane but its own.
- `write_ready`'s signature grows; pass a small POD (`MobileReadyFields`) rather than seven scalars —
  the `StatusFields`/`LimitsFields` pattern already in the file.

### `cfg` (writer `write_cfg`, tail at `console_json.cpp:460-476`)

Add, next to the existing `"mobile"` (which stays — `cfg` is the explicit config dump, not a
snapshot, so these are **always present**, additive at the end before `layers`):

```json
{"ev":"cfg", … ,"mobile":true,"mobile_autoregister":true,"team_id":"cccc0001", … }
```

- `team_id` = `"00000000"` when unset (explicit in `cfg`, unlike `ready`'s omit).
- Round-trips the three `cfg set` keys the app can already write (`mobile` / `mobile_autoregister` /
  `team_id`, `firmware_config.cpp:171-173`) — a settings screen can read-modify-write without a
  side-channel. (`host_mobiles` is LIVE-only/not persisted — leave it out of `cfg` for now.)

## 3. S2 — registration pushes: `mobile_reg` + `team_reg` (new `PushKind`s)

The single most important slice for the app (and the whole UI for a T1000-E): registration is the
mobile's *connectivity* state, and today it changes silently. Two new `PushKind` values (append at
the enum end, `command.h:83`), reusing existing `Push` fields — **no POD growth**:

```json
{"ev":"mobile_reg","home":222,"local":17,"home_layer":4,"epoch":6,"registered":true}
{"ev":"mobile_reg","home":0,"local":0,"registered":false}          // home lost / deregistered
{"ev":"team_reg","team":"cccc0001","local":9}                      // team-DAD adopted/re-picked
```

Firing sites (all in `node_mobile.cpp`, next to the existing telemetry — which is **stripped on
metal** (`MESHROUTE_NO_TELEMETRY`), which is exactly why the push must exist):

1. **Adopt / re-home:** beside `MR_EMIT("mobile_adopted", …)` (`node_mobile.cpp:130`) — fires on
   every (re-)registration, including a roam to a new home (`epoch` = the post-increment reg epoch,
   `mobile_reg_epoch()`).
2. **Home loss:** wherever the in-flight **liveness work** clears `_my_mobile_reg.active`. This spec
   deliberately defines the `registered:false` shape *now* so the liveness slice emits it at its
   deregistration point instead of inventing its own — coordinate with that branch (uncommitted
   `node.cpp`/`node.h`, s25/s26 scenarios).
3. **Team-DAD adopt:** beside `MR_EMIT("team_dad_adopted", …)` (`node_mobile.cpp:177`) →
   `team_reg`. Also fires on a conflict re-pick (same site).

Field mapping in `Push`: `origin`=home, `dst`=local, `layer_id`=home_layer, `ctr`=epoch,
`relayed`=registered (or a dedicated bool — coder's choice, keep the POD lean);
`team_reg` puts team_id in the existing u32 `sender_hash` slot or adds nothing and reads
`_cfg.team_id` at write time (it's ambient config — reading it in the `write_push` arm via the
existing `g_node.config()` hook, the `config_adopted` precedent at `console_json.cpp`, is simplest).
Add both to `pushkind_name` + a `write_push` arm + native goldens.

**App rule (contract text to add):** on `mobile_reg` update the connectivity chip live;
`registered:false` ⇒ "searching for home…" (the FSM auto-rediscovers when `mobile_autoregister=1`);
a changed `home` with `registered:true` ⇒ a roam, not an error.

## 4. S3 — `mobile status` + `mobile gateways` answer JSON

Convert the two read verbs (`firmware_config.cpp:629-644` / `:603-621`) from human text to JSON
lines. Writers live in `console_json.cpp` (PODs in, no node.h dependency — pass the fields), called
from `handle_mobile`. These are **`src/`-only call-site changes + new lib/console writers** —
s18-inert by construction.

```json
{"ev":"mobile_status","mobile":true,"registered":true,"home":222,"local":17,"epoch":6,
 "home_layer":4,"autoregister":true,"layer":4,"freq_khz":869525,"sf":9,"bw_hz":125000,"nets":2}
```
- PHY block = the *live* layer values the human printout uses today (`c.layers[0].layer_id`, freq
  fallback `g_freq_mhz`, `c.routing_sf`, `g_node.active_bw_hz()`) — but as **integer kHz/Hz** (the
  freq source is a double MHz; emit `(uint32_t)(freq*1000.0 + 0.5)`).
- Unregistered ⇒ `"registered":false,"home":0,"local":0,"epoch":0` and `home_layer` omitted.
- On a non-mobile node the verb keeps its error, now as JSON:
  `{"ev":"mobile_err","reason":"not_mobile"}`.

`mobile gateways` — **streamed, one line per record + terminator** (the `routes`/`routes_end`
pattern; over BLE these go through the streaming sink, not the 256-B ack buffer):

```json
{"ev":"mobile_gw","gw":3,"leaf":4}                                              // per bridged gateway
{"ev":"mobile_net","layer":7,"name":"north field","freq_khz":869525,"sf":9,"bw_hz":125000}  // per learned network
{"ev":"mobile_gw_end","gws":1,"nets":2}
```
- Sources: `bridged_layer(i)` (`.valid/.gw_id/.dest_leaf`) + `learned_layer(i)`
  (`LayerRecord: layer_id/name[name_len]/freq_khz/sf/bw_hz` — freq is **already integer kHz** in the
  record). `name` JSON-escaped via the existing `j.str`.
- This is the data for the app's **roam UI** (`mobile register freq=… sf=… bw=…` targets one of
  these rows) — the app-driven `mobile_autoregister=0` mode is unusable without it.
- `mobile query <gw>` / `mobile register …` **keep their current human one-line acks** for now (the
  state outcome arrives as `mobile_reg`/`mobile_gw*` anyway); JSON acks are a deferred nicety (§9).

## 5. S4 — `team_id` tag on the live `channel_recv`

At the ingest/push site (`node_channel.cpp:234-244`) `m.team_id` is in scope and already stamped on
the buffered entry (`e.team_id`, `:216`). Add `uint32_t team_id = 0;` to the `Push` POD
(`command.h:109`) and set it for `channel_recv`; the `write_push` channel arm emits
**`"team_id":"cccc0001"` only when non-zero** (hex-string, omit-when-0 ⇒ every non-team channel
push byte-identical ⇒ static goldens + the s22 JSON comparisons unchanged).

```json
{"ev":"channel_recv","origin":4,"layer_id":4,"channel_id":0,"channel_msg_id":…,"seq":7,
 "team_id":"cccc0001","body":"…"}
```

- **Identity is unchanged** — dedup stays the full 32-bit `channel_msg_id`; `team_id` is display
  scoping (the app threads it into the team view), exactly like `layer_id`.
- Push POD growth: +4 B × `cap_push_ring` RAM — note the delta in the gate report (the team-parity
  commit budgeted +528 B RAM; this is the same ballpark game).

## 6. S5 — `team_id` on the durable `inbox_channel` (store format bump)

The costliest slice — the only one touching persisted bytes. `InboxEntry` (`inbox.h:26`) has **no
team field**, so a team broadcast pulled after reconnect loses its scoping (worse: after a team
*switch*, joining against the node's current team would mislabel old-team history — which is why the
record must carry the actual id, not a flag).

- `InboxEntry` += `uint32_t team_id;` (0 = not team-scoped). Serialized record header 27→31 B
  (`inbox_record_header_bytes`), field appended after `type` per the layout comment `inbox.h:41-45`.
- `record_channel` gains the param (pass `m.team_id` at `node_channel.cpp:234`); DMs record 0.
- `write_inbox_channel` emits `"team_id":"…"` **omit-when-0** (same rule as S4).
- **Bump the device store record version** — the established pattern ("each bumps the device store
  version so old records are rejected", `inbox.h:45`). ⚠ **QA requirement:** verify the
  version-reject path **bumps `storage_epoch`** (a rejected/reset store the app synced to must
  re-pull from 0 — if reject-on-load doesn't bump the epoch today, that's a latent bug this change
  would expose; fix it in this slice and add a native test: old-version record present → store
  resets → epoch != before).
- Rollout note for the contract: un-updated companions simply ignore the unknown field — additive.

## 7. S6 — peer names as JSON (closes the QR/name arc)

The name feature is half-landed for the app: `ready.name` is LIVE (own name → QR `n=`, done), but a
**peer's** cached name is reachable only via human text (`[nameof] …`, `firmware_commands.cpp:406`),
so a contact can't auto-label. Two small changes, one cache read (`peer_name_find`, `node.h:373`):

1. **`nameof 0x<hash>` answers JSON** (replacing the human line — it's an app-facing query verb; the
   serial user reads JSON fine, same as `status`):
```json
{"ev":"peer_name","hash":3735928559,"name":"Alice's tracker"}
{"ev":"peer_name","hash":3735928559}                            // cached key but no name / unknown hash
```
2. **`peer_key_cached` gains the name, omit-when-unknown** (`console_json.cpp:215-217`): the name
   rides the pubkey exchange and is cached at the same moment the key is — one field, no new push,
   and the app labels the contact in the same event that enables encrypted send:
```json
{"ev":"peer_key_cached","hash":3735928559,"pinned":false,"name":"Alice's tracker"}
```
   (The `Push` POD needs the name bytes — either copy ≤32 B into the existing `body[]` (it's empty
   for this kind, zero new RAM) or read the cache in the writer arm via the ambient-node hook. Prefer
   `body[]`: the cache may have aged by drain time.)

**QR forward path (optional 2c, decide-later):** when the app scans a card carrying `n=` + `p=`, it
installs the key via `peerkey <hex64>` — the *node* never learns the scanned name (the cache fills
only from the on-air exchange). Optional: `peerkey <hex64> [name="<text>"]` seeds the name cache too,
so `nameof`/serial UX match the app. Additive, small; skip if the name cache's write API doesn't
compose cleanly with a PINNED-tier entry.

⚠ **Contract delta:** the contract's peer-name paragraph offered "a `name` field on
`peer_key_cached`/`msg_recv` **or** a `peer_name` push" — this spec picks **`peer_key_cached`+`name`
AND a `peer_name` *query answer*** (not a spontaneous push), and does **not** touch `msg_recv`
(names on every DM is redundant bytes over BLE).

## 8. Cross-cutting: feature-split guards + numbers

- All accessors used above have `!MR_FEAT_MOBILE` (and host-side, `!MR_FEAT_MOBILE_HOST`) stubs
  returning 0/false — combined with omit-when-inactive, **every build (gateway, static,
  mobile-member) emits exactly the fields that can be non-trivial for it**, with no preprocessor in
  lib/console. `handle_mobile` (S3) is already inside `#if MR_FEAT_MOBILE`.
- On `MR_PROFILE_MOBILE` (→ T1000-E): MOBILE=1, MOBILE_HOST=0 ⇒ `hosting` always omitted; all of
  S1–S6 active. On the gateway build (TEAM + MOBILE-member compiled out): everything omits ⇒
  byte-identical `ready`.
- RAM deltas to report in the gate: Push +4 B (S4) × ring cap (+ optional name-in-body reuse = 0);
  InboxEntry +4 B serialized (S5).

## 9. Deferred (named so they don't silently vanish)

- JSON acks for `mobile register`/`mobile query` (human one-liners stay; state arrives via pushes).
- `peerkey … name=` seeding (§7, optional 2c).
- `mobile status` **known-networks inline** (the `nets` count is in S3; the rows come from
  `mobile gateways` — don't duplicate).
- Location/GNSS surface for the T1000-E (its raison d'être — but the variant itself is PARKED;
  when it un-parks, the phone-fed/`has_location` DM piggyback path is the starting point).
- `ready.bonds` (still owed to the notification slice — unrelated, listed in the contract).

## 10. Gate (per `simulation/BASELINE.md` + the mobile recipe)

1. **Native:** `pio test -e native` **then run `./.pio/build/native/program`** (the wrapper
   misreports; the binary prints the real count). New goldens: ready-mobile/ready-team/ready-static
   (static byte-identical!), cfg, mobile_status, mobile_gw stream, mobile_reg/team_reg push arms,
   channel_recv±team_id, inbox_channel±team_id, store-version reject→epoch-bump, peer_name.
2. **s18 byte-identity** (md5 in `BASELINE.md`): S1/S3 are `src/`-or-writer-only; S2/S4/S5 touch
   lib/core (`command.h`, `node_channel.cpp`, `node_mobile.cpp`, `inbox.h`) — additive fields +
   push-emits only, no routing/behaviour change ⇒ must come back **exact**. Run it, don't argue it.
3. **Mobile scenarios:** s22/s23/s24 (+ s25/s26 once the liveness branch lands) — 0-fail.
4. **All board/profile envs built sequentially** (10/10) — the omit-guards get their real test on
   the gateway + mobile profiles.
5. Coordinate S2's home-loss firing site with the in-flight liveness branch **before** implementing
   (same files: `node.cpp`/`node.h`).
