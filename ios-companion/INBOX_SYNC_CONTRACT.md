# Inbox sync — BLE wire contract (PROPOSED)

> ## ⚠⚠ PENDING CONTRACT CHANGES — SPEC'D, **NOT YET IMPLEMENTED** (as of 2026-07-30)
>
> Everything **below** this box describes shipped firmware. This box lists changes that are **designed and
> owner-ruled but not built**, so the app team can plan — and so nobody implements against a surface that is about
> to move. **QA writes each section into this document as its slice lands; a coder never edits this file.**
>
> ### ✅ ★★ DONE 2026-07-31 (`§loc-per-send`) — `loc_dm` IS GONE, and `-l` IS LIVE
> **This one has landed** (register **B0** closed — it was a live leak: a plaintext DM aired the node's coordinates in the
> clear). ★ **`loc_in_dm` / `CfgOut.loc_dm` and TLV `0x18` are DELETED, not deprecated** — the `cfg` text dump no longer
> prints `loc_dm=`, and **`0x18` is RETIRED and must NEVER be reused**: an app still writing it is silently ignored
> (`dec_cfg`'s `default: break;`), which is precisely why the number can never mean anything else later.
> ★★ **New grammar:** `send <id|0xhash> "<text>" [-a] [-e] [-t] [-l]` — **`-l`** attaches this node’s position to **that one
> message**. There is no longer any node-level location setting to read or write.
> ⚠ **`send_layer -l` is REFUSED** (`err_unsupported` + `send_failed{unsealable}`) — cross-layer cannot carry a position
> (the SEALED_RELAY body has no flags word; extending it is a future wire change). **`send_channel` has no `-l`** →
> `bad_args`; a channel location stays T-K2/T-K5’s `inner_type = 1`.
> ★★ **A `-l` send REFUSES rather than sending without the position** — three cases, and the app must surface each:
> • **`send_failed{reason:"unsealable"}`** — the DM would not be sealed. Remedy: `-e`, `cfg set e2e_dm 1`, or acquire the
> peer's key. ⚠ **`unsealable` now has TWO meanings** (it already covered a team-key grant that could not travel sealed).
> • **`send_failed{reason:"no_location"}`** — ★ **NEW enum value, appended:** `-l` asked for a position and the node has
> **no fix**. Remedy: set `lat`/`lon` or wait for GPS. **This is NOT an encryption problem** — do not prompt for keys.
> • an oversize body refuses as **`too_large`** from the seal (the location rides inside the sealed inner).
> ★ **App-facing consequence worth designing for:** a `-l` DM to a peer whose key you do not hold **will refuse**. Surface
> `reqpubkey`/QR to the user — **do not silently retry**, and do not auto-issue `reqpubkey` (standing owner ruling).
> ⚠ It also carried **`kVersion` 22 → 23**, so **expect an unprovisioned node on first contact after that flash**
> (the second such bump; T-K1 was the first). `/mrid` identity is unaffected.
> ⚠⚠ **CORRECTED 2026-07-31 (`§ab1`): `/mrpeers` is NO LONGER unaffected — it is now a THIRD, NARROWER reprovision event.**
> `kPeersVersion` **1 → 2** (the record grew by name + confidence), and the store is **rejected outright** rather than
> migrated ⇒ **the pinned-peer store is LOST ONCE on the first boot after this flash, and every QR ceremony must be
> redone per contact.** ★ **Independent of `kVersion` (still 23) and of `/mrid`:** config and identity survive, the
> address book does not. **How to see it happen:** a new boot line `peers = N restored (P pinned, A authoritative)` —
> `peers = 0 restored` is the loss.
>
> ### ✅ ★★ NEW 2026-07-31 (`§ab4`) — the `peer` row gains a POSITION (RAM only)
> ```
> {"ev":"peer", … ,"lat":<i32 1e-7 deg>,"lon":<i32 1e-7 deg>,"loc_age_s":<u32>,"loc_src":"peer"|"team"}
> ```
> **All four fields appear TOGETHER or all four are ABSENT. Absence is NORMAL, not an error** — most peers have never
> sent a position.
> ★★ **TWO OBLIGATIONS ON THE APP. Both exist because the alternative actively misleads the user:**
> 1. **Render `loc_age_s` alongside any position — never a bare pin.** ★ **An age-less pin reads as CURRENT**, and this
>    book deliberately keeps positions that may be hours old. A three-hour-old fix drawn as "here now" is worse than no
>    pin at all. ⓘ `loc_age_s == 0xFFFFFFFF` means **maximally stale** (the node's clock went backwards) — treat it as
>    unknown-vintage, **not** as fresh.
> 2. ★★ **Render the `loc_src` distinction — the two values are NOT equally trustworthy:**
>    - **`"peer"` — PAIRWISE.** The position arrived in a DM **sealed to us and opened with our key**, so **only that
>      peer could have written it.**
>    - **`"team"` — GROUP.** Sealed to the **shared** team content key ⇒ it proves **MEMBERSHIP, not IDENTITY**:
>      **any holder of the team key could have written it, including forging another member's `source_hash`.**
>    ⇒ **a map that draws both identically OVERSTATES the weaker one.** This bound is accepted **by design** (the I9
>    pattern), not a defect awaiting a fix — the group key cannot distinguish members, so the UI must.
>    ⓘ **`"team"` cannot occur yet** — the channel source needs CL2 (spec-only today). Handle it now anyway: when CL2
>    lands it adds a **source**, not a schema change, so an app that already renders the distinction needs no update.
> ★ **Only an AUTHENTICATED position is ever stored.** A **plaintext** location is still delivered on `msg_recv`
> exactly as before but is **NEVER retained in the book** — an unauthenticated position is spoofable, and a spoofed
> position that the UI presents as fact is worse than an absent one. A refused one raises telemetry only
> (`peer_location_unauth`), no push.
> ⚠ **RAM ONLY, deliberately: a REBOOT CLEARS EVERY POSITION.** Two reasons, both intentional — a stale position is
> worse than none, and **a captured or stolen node must not yield every teammate's last known position.** ⇒ **the app
> must not treat the book as a position history**, and must expect all four fields to vanish across a node restart
> (unlike names and keys, which `§ab1` persists).

> ### ✅ ★★ NEW 2026-07-31 (`§ab3`) — the `peers` ADDRESS BOOK surface
> ```
> peers        -> {"ev":"peer","hash":<dec u32>,"conf":"overheard"|"authoritative"|"pinned","confirmed":<bool>
>                  [,"name":"…"][,"static_id":N][,"team_id":N][,"team_alias":N][,"aged":true]}   … then
>                 {"ev":"peers_end","count":N}
> peers all    -> {"ev":"peers_err","reason":"console_only"}      # TEXT CONSOLE ONLY, by design
> ```
> ★★ **Four obligations, each of which prevents a wrong UI:**
> 1. **Gate "send encrypted" on `conf >= authoritative`, NEVER on key presence.** `overheard` means the key is cached
>    but **cannot seal** — that is the offer-then-fail `no_pubkey` UX in one field.
> 2. ★ **`"aged":true` means the key is UNUSABLE** even though a name and ids are present. The row is still worth
>    showing (it is a real contact) but **encrypted send must be off for it** — the firmware already reports its `conf`
>    downgraded to `overheard` for exactly this reason.
> 3. ★ **`peers` is BOUNDED to ≤16 rows BY DESIGN, and `peers all` is console-only — neither is an oversight.**
>    Cardinality comes from a 256-row table, and a node **WEDGE from self-inflicted console flooding** is a defect this
>    firmware has already had once. **Do not build a pager expecting the full list over BLE; it is refused.**
> 4. **Absence of `static_id`/`team_id` is NORMAL** — most known peers have one, some have both (§18 dual identity),
>    and a row may have neither yet. ⓘ **`"team_alias":N` means N stale team-id rows carried this same hash and the
>    freshest won** — surface it as staleness, not as an error.
> ★ **Also: `peer_name` gains additive omit-when-0 `static_id` / `team_id`.** The first two fields and their order are
> unchanged, so an existing decoder keeps working.
> ⓘ **Console-side, `hashof <id> [-t|-s]` now searches BOTH planes** and says which matched — it previously answered
> `unknown` for a team id whose hash the node held. A *claimed* (unvouched) static binding is labelled `(claimed)`.

> ### ★★ NEW 2026-07-31 (`§role-model`) — `cfg set mobile` CAN NOW FAIL, and there is a new reply line
> The node now enforces **team ⇒ mobile** (`team_id != 0` implies `is_mobile`). Three app-visible consequences:
> - **`> cfg err role_refused <reason> …`** — a `cfg set mobile` that **always succeeded before** can now be REFUSED.
>   Reasons: `no_mobile_plane` (this firmware has no mobile plane) · `gateway_is_static` (a gateway cannot be mobile) ·
>   `hosting_mobiles n=N` (demote/promote refused while N guests depend on this node as their home) · `in_a_team`
>   (**`cfg set mobile 0` while in a team** — the app must send `team 0` or `leave` first). ⇒ **the app must handle a
>   failing role write**, and surface the named remedy rather than retrying.
> - **`> team err role_refused …`** — the same refusals also guard the team-implied promotion, so **`team <id>` can now
>   fail** where it previously always applied.
> - ⚠ **`> role -> MOBILE …` is printed BEFORE `> team -> team_id=0x…`** on a promoting `team` command. **If the app parses
>   the team reply POSITIONALLY, this will break it** — match on the prefix, not the line index.
> ★ **And treat "team ⇒ mobile" as an invariant, not a coincidence:** do not try to set the two apart.

> ### From `2026-07-30-channel-crypt-and-location-privacy-design.md`
> - **`send_channel … -e`** — encrypted team channel posts, plus a four-case flag matrix in which **two
>   combinations REFUSE**: `-e` without `-t` (there is no key for a global channel), and **`-t -g -e`** (BOTH would
>   air an identical copy in clear and defeat the encryption).
> - ~~**`-l` on `send` / `send_layer`**~~ ✅ **SHIPPED 2026-07-31 — see the DONE box above** for the as-built grammar. Two
>   deltas from this line: **`send_layer -l` refuses**, and the does-not-fit case reports **`too_large`** from the seal
>   rather than its own reason (a dedicated gate was measured unreachable). `-l` is still **NOT** on `send_channel`.
> - `enc:true` on `channel_recv` / `inbox_channel`; the **`team_channel_no_key`** push; `team_channel_crypt` config.
> - ⚠ **A drift fix owed here:** line ~28 below claims *"`send_channel`/`send_layer` REJECT `-t`"*. **False for
>   `send_channel`** — only `send_layer` rejects it.
>
> ### From `2026-07-29-peer-address-book-design.md`
> - ✅ ★★ **LIVE 2026-07-31 (`§ab2`) — `peer_key_cached` NOW CARRIES `"conf"`, and the `peername` verb exists.**
>   ```
>   {"ev":"peer_key_cached","hash":<dec u32>,"conf":"overheard"|"authoritative"|"pinned","pinned":<bool>[,"name":"…"]}
>   {"ev":"peer_name_set","hash":<dec u32>,"name":"<echoed>"}
>   {"ev":"peer_name_err","reason":"unknown_hash"|"too_long"|"bad_args"}
>   peername 0x<hash> "<name>"          # rename a CACHED peer; key + confidence untouched; WORKS on a pinned peer
>   peerkey <ed_pub hex64> ["<name>"]   # optional one-shot label — ⚠ BARE QUOTED, *not* name="…"
>   ```
>   ★★ **THE OBLIGATION, now finally satisfiable: gate "send encrypted" on `conf >= authoritative`, NEVER on key presence.**
>   `overheard` means the key is cached but **cannot seal** — offering encryption on it is what produced the
>   offer-then-fail `no_pubkey` UX. ⚠⚠ **And `pinned` is no longer ALWAYS `false`** — it was a hardcoded literal until
>   today, so **an app that read it as "was this a QR import" was silently wrong and is now correct.**
>   ⓘ The `peername` ack is **SYNCHRONOUS** (no push, no new `PushKind`); `name` is **echoed** so the app can confirm what
>   was stored; the cap is **32 with a REFUSAL, never truncation**; an **empty name is REFUSED**, not treated as "clear"
>   (there is no clear operation). Remedies: `unknown_hash` → `reqpubkey` first; `too_long` → shorten.
>   ★ **USB console line changed:** `KEY CACHED hash=0x… [name=…] conf=<level> nv=<put>` — it previously printed
>   **`(on-air, unpinned)` unconditionally**, the same lie as the JSON literal, even for a QR-pinned peer.
> - **`peername 0x<hash> "<text>"`** — a **synchronous ack**, not a push (owner-ruled), so **no new `PushKind`**.
> - **A `peers` view**: JSON **capped at the 16 `_peer_keys` rows**; the full up-to-256 known-nodes list is
>   **text-console only** behind `peers all` (owner-ruled). `hashof` becomes a view query.
> - NV gains **names + `authoritative` keys** — but ★ **the node remains a 16-slot ageing cache. The APP owns the
>   durable address book**; the node's view is a reconcile source, not the record.
>
> ### ⏳ IN REVIEW 2026-08-01 (`§id-hash` S1/S2/S2b) — `reqpubkey` grammar, and ★★ ONE BREAKING APP CHANGE
> **Status: implemented and gated, NOT committed, one QA blocker still open.** Written here now because one item
> **needs app action** and the app team should not discover it from a field failure.
>
> ★★★ **THE BREAKING ONE — a bare `reqpubkey <id>` CHANGED MEANING.** It used to be *implicitly TEAM-scoped*; it is
> now **AUTO**, resolved across both the static and team planes. This is **not** an additive change: it alters what an
> already-shipped outgoing command does.
> - ⇒ **`Command.reqPubkeyTeam` must emit `reqpubkey <id> -t`.** Changed in `Command.swift` in the same slice and
>   pinned by `CommandEncoderTests`, but ⚠ **NOT gate-verified — there is no `swift` toolchain in the firmware
>   environment. Run the package tests before shipping.**
> - **Without `-t`:** with both namespaces populated the node answers **`err_ambiguous_plane`**; with only a static
>   binding it **silently selects the static plane** despite the operation being named "team".
>
> **New grammar:** `reqpubkey <0xhash|id> [-s|-t]` — `-s` and `-t` are mutually exclusive; a bare id is AUTO. A hash
> target is unchanged.
>
> **New ack codes (additive):** `err_ambiguous_plane` (pass `-s` or `-t` — note the remedy is the *opposite* of
> `err_no_binding`'s) · `err_no_identity` (no Ed25519 identity, so a mutual exchange is impossible; remedy `regen`) ·
> ⏳ **`err_tx_queue_full`** — ★ **TRANSIENT: a bounded TX queue rejected the frame. Remedy is RETRY SHORTLY, and the
> app should offer exactly that rather than a key/plane remedy.** (In flight: renamed from `err_tx_ring_full` because
> **two** different bounded queues can reject, and a hint naming only one is a wrong diagnosis.)
>
> **`{"ack":…}` gains an optional `"plane":"team"|"static"`** — present only when the node actually chose a plane, so
> every pre-existing ack line stays byte-identical.
>
> ★★ **`reqpubkey_sent` changed TWICE, and the second one matters more:**
> 1. **`"hash"` now carries the RESOLVED hash** for the by-id form. It used to be **`0`**. It also gains the optional
>    `"plane"`.
> 2. ★★ **It now means "the TX path ACCEPTED the frame" — and that is DELIBERATELY NOT a claim of airtime**
>    (owner-ruled 2026-08-01). Previously *every* `queued` produced one, including paths that transmitted nothing: no
>    crypto identity · an off-grid mobile with no return route · a degenerate/self target · a codec failure · and a
>    frame discarded by a full TX queue. An app reading it as *"the request is on the air"* was being told so **five**
>    ways that were not true.
>    ⓘ **Why not "actually transmitted":** an LBT-deferred frame reaches the radio when a timer fires, **after** this
>    acknowledgement is returned — no synchronous ack can prove a future transmission. Acceptance is the strongest
>    honest synchronous claim, and it answers the app's real question: *is it reasonable to wait?*
> ⇒ **App consequence:** a `reqpubkey` answering `err_*` genuinely means nothing flew. A **local cache hit** (a hosted
> mobile whose key the node already holds) still answers `queued` and still fires `peer_key_cached` — but **no longer
> `reqpubkey_sent`**, because no query was flooded. ★ **Treat `peer_key_cached` as the success signal;
> `reqpubkey_sent` means only "a query was accepted for transmission — expect an answer later, or nothing."** Do not
> build a UI that promises delivery on it.
>
> ⓘ **`peers` over BLE is UNCHANGED by this arc.** S2 added static route-only rows and stopped listing the node
> itself, but both are `peers all` — **text-console only** (`include_id_rows=true`). The JSON book still returns
> exactly the ≤16 keyed rows described above.
>
> ### ⏳ ALSO IN REVIEW (`§id-hash` S3/S4a) — an id is now a CLAIM unless labelled otherwise
>
> ★★ **THE MODEL, and the app has to render it: `hash → pubkey` is self-verifying; `id → hash` is NOT.** A public key
> is checked against its own hash on arrival, so it cannot be forged. **An 8-bit id is an address, not a commitment** —
> so an id→hash binding learned over the air is a **claim**, and the node now says which is which.
>
> **`{"ev":"peer"}` gains `"team_auth":<bool>`** — `true` = we heard that teammate's own beacon (first-hand);
> `false` = somebody told us her number. ⚠ **`static_id` is still emitted BARE and that is a known gap (register
> B52):** a relayed soft answer lands a *claimed* static binding **today**, and every such row reaches you unlabelled.
> ⇒ **until `static_auth` ships, do not present a `static_id` as identity.** The text console already renders
> `static_id=N(auth)` / `(claimed)`; the JSON is the half that is missing.
>
> ★★ **`reqpubkey_sent` can now carry `"hash":0`, and it means something new (register B55).** A by-id `reqpubkey`
> against an *unresolved* id is **two-stage**: the frame that flies first is the **id→hash query**, not the pubkey
> request. The event still fires (the TX path did accept a frame), but `hash == 0` is the **only** signal that this was
> stage 1. ⇒ **an app treating `reqpubkey_sent` as "a pubkey is coming" will wait for one that is not sent.**
>
> ⚠⚠ **CORRECTED 2026-08-02 (S4b) — AND THE EARLIER ADVICE HERE IS NOW ACTIVELY WRONG.** This paragraph previously
> said *"on `hash == 0`, re-issue `reqpubkey <id>` once the binding lands, or wait for S4b which removes the case."*
> **Both halves are false:**
> - **`hash == 0` DOES NOT GO AWAY, and it should not.** It is the honest report of a real stage-1 acceptance: the
>   frame the TX path took *was* the id→hash query, and the hash is the very thing that frame went to ask for. A
>   synchronous acknowledgement cannot carry a value that does not exist yet. Removing the case would mean either
>   suppressing a true event or inventing a hash. (Same wall as `aired` → `accepted`, one level up.)
> - ★ **DO NOT RE-ISSUE.** S4b made the node consume the id→hash answer and emit the pubkey request **itself**. An app
>   still coded to the old advice fires a **redundant duplicate flood** — harmless (dedup and the intent refresh absorb
>   it) but wasted airtime on a constrained link.
>
> ⇒ **What the app should do on `hash == 0`: nothing. Wait.** The success signal is the **`peer_key_cached`** push;
> the failure signal is a bounded timeout, currently **console + telemetry only** — ⚠ **a stage-2 failure does NOT
> reach the app today (register B56).** Closing that needs a new `PushKind`, i.e. a contract decision, so until then an
> app must not present stage 1 as a promise.
>
> **Three smaller deltas ride with it:** a new **`err_resolve_pending_full`** ack — the pending-resolve ring (4 slots)
> is full; ★ **transient, retry shortly**, and the bound is *airtime* (each intent is a flood in flight), not memory.
> `err_ambiguous_plane` has a **second** cause — an *unresolved* id on a dual-plane node, where the query itself must
> pick a plane (previously it meant only "both planes hold this number"). And `err_no_binding` on a bare or `-t` id is
> now reachable from exactly one place: an explicit `-t` on a node that holds no team plane at all.
>
> ⇒ **Tracking: B52, B55** in `docs/2026-07-30-open-bug-register.md`.
>
> ⇒ **Tracking: `docs/2026-07-30-open-bug-register.md`** (**B42**–**B48**) and
> `docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md`.

> ⇒ **Tracking: `docs/2026-07-30-open-bug-register.md`** (entry **B0** is the live location leak).


The companion catch-up seam between the firmware persistent inbox
(`docs/superpowers/specs/2026-06-10-persistent-inbox-spec.md`) and the iOS app. **STATUS (2026-07-04): the
firmware side is IMPLEMENTED + verified against code** — the send / pull / inbox / `ready` / duty / e2e /
provisioning / anti-spam commands + pushes below are live in `lib/console/console_parse.cpp`,
`lib/console/console_json.cpp`, and `src/fw_main.cpp`. Landed since 2026-06-29: the send-verb unification,
the `reqpubkey_sent` + `e2e_acked` events, and the D7 DM-`ctr` persistence; **added:** anti-spam v2 (the
`limits` query + send-outcome feedback) and the `layer`/`leaf` terminology (a **layer** is the full 1..255
network id, **leaf** is its `& 0x0F` nibble). **The one item still open: `ready.bonds`** (deferred to the
notification slice). **★ ADDED 2026-07-09 — a *Mobile node + teams* section** (below, after leaf-config): the mobile console verbs (`cfg set mobile`, `mobile register/gateways/query/status`, `mobile_autoregister`) + team provisioning (`team new`/`team <id>`/`cfg set team_id`) are **LIVE**; the app-facing **JSON surface** (mobile state in `ready`, a `mobile_reg` push, `mobile status`/`gateways` as JSON) + the **team-channel `team_id` tag** are **PROPOSED** firmware asks; team routing/channel (6.2/6.3) + the hash-locate addressing spine are **in progress**. **★ UPDATED 2026-07-16 (this pass):** team 6.2/6.3/6.4 + **team multi-hop routing parity** (F + team H-flood) + **plane separation** are now IMPLEMENTED/committed (routing spine LIVE); the **`send`/`reqpubkey` HARD PLANE SPLIT** (`-t` = team plane — see ★ under *Commands*) is **LIVE and app-affecting**; the **node/peer NAME feature** landed (`ready.name` LIVE, peer names via the pubkey exchange + `nameof`); `rcmd` is now **authenticated**. ~~⚠ Re-verified against `console_json.cpp`: the mobile/team **STATE JSON** … is still genuinely PROPOSED~~ **→ ★ IMPLEMENTED + GATED 2026-07-16 (later the same day, uncommitted):** the whole mobile/team STATE-JSON surface (spec `2026-07-16-companion-mobile-team-json-surface.md` S1–S6) is now **LIVE in firmware** — `ready`/`cfg` mobile+team fields, `mobile_reg`/`team_reg` pushes, `mobile status`/`gateways` as JSON, the `team_id` tag on `channel_recv`+`inbox_channel`, and peer names as JSON. Final shapes inline below (each former PROPOSED block updated). ⚠ Rollout: the durable team tag bumped the inbox store version — the first boot after reflash **wipes the on-node inbox + bumps `inbox_epoch`** (the app's normal epoch path re-pulls cleanly; history already archived app-side is unaffected).

Framing matches the rest of the link: **app→node = line-ASCII commands, node→app = newline JSON.**

> **Firmware review (2026-06-10):** the epoch/store-reset section is **confirmed** — `inbox_epoch` in the
> `ready` snapshot maps to the firmware's `storage_epoch` (inbox spec §10.1, a hard Phase-2 requirement on
> the device store). **One required change:** channel identity is the **full 32-bit `channel_msg_id`**, not
> `ctr`. Phase 1 now stores the whole id (`InboxEntry.msg_id`, u32) — so dedup channels **exactly** by it,
> and **drop the `ctr` + body-tiebreaker workaround** (it was only there because the low-8 ctr wraps). DM
> identity stays `(origin, ctr)`. Applied inline below. (Other agreements: pull order DM-block-then-channel
> == `Inbox::pull`; `rx_ms` == `InboxEntry.rx_time_ms`; DM `ctr` == the firmware's `msg_id` for a DM.)

## Commands (app → node)

> ✅ **DONE — send verbs unified (firmware 2026-06-21, spec `2026-06-21-serial-interface-cleanup.md` §2; `Command.swift` migrated).** The 9 send verbs collapsed to **3 with a QUOTED body + `-a`/`-e` flags**; the old `send_ack`/`sendhash`/`sendhash_ack`/`sendhashx`/`sendhashx_ack`/`send_layer_ack` are **REMOVED** (a node now returns `unknown_verb`). `Command.swift` emits the unified form:
> ```
> send <id|0xhash> "<text>" [-a] [-e] [-t]    # id (<=254 bare decimal) vs a 0x-PREFIXED hash; -a=ack, -e=encrypt (hash only); -t=TEAM plane (see ★ below)
> send_channel <ch> "<text>"                 # no ack/enc — on a TEAM mobile it auto-broadcasts to the team (team_id-scoped; `send_channel`/`send_layer` REJECT -t)
> send_layer <0xhash> <l1,l2,…> "<text>" [-a] [-e] [-K]   # explicit cross-layer path; -e => SEALED (2026-07-29)
> ```
> ### ★ HARD PLANE SPLIT (2026-07-16) — `send` addresses ONE of three planes; a teammate REQUIRES `-t`
> The `send` verb now routes on an explicit plane (`console_parse.cpp:245`, `plane = -t ? TEAM : GLOBAL`). **This is a real command change the app must adopt** — the old "reaching a mobile is a plain send-by-hash, no app change" is **WRONG for a teammate**.
> - **`send <team_local_id> "…" -t`** → **TEAM plane**: the bare id is a **`team_local_id`** (1..254 on the team overlay), routed member-to-member via `_rt_team` (multi-hop over team beacons + team F discovery). **Within a team you MUST pass `-t`** to reach a teammate by id.
> - **`send <id> "…"`** (no `-t`) → **GLOBAL/HOME plane**: the id is a static `node_id`; from a mobile it routes via its **home** and **fails loud (`send_failed{no_route}`) if it has no home**. Never resolves a teammate.
> - **`send <0xhash> "…" [-e] -t`** → **team-scoped hash**: resolves the teammate's `hash→id` via the **team-scoped H-flood** (multi-hop; spec `2026-07-15-team-plane-routing-parity-design.md`). **Without `-t`**, a `0xhash` resolves on the **global** plane (a static peer, or a mobile via its home). `-e` = encrypt (see the team-crypto note in *Mobile node + teams*).
> - **`reqpubkey <0xhash> -t`** or **`reqpubkey <bare team_local_id>`** → team-scoped pubkey request (origin = our `team_local_id`, answered via `_rt_team`); a bare team-id is implicitly TEAM. Plain `reqpubkey <0xhash>` = global/home. (`console_parse.cpp:154-169`.)
>
> **Three-plane addressing the app must model** (all `send`/`reqpubkey`): **static** = plain id / `0xhash` (direct or routed) · **mobile** = plain `0xhash` (the firmware resolves it to the mobile's home + last-miles — no `-t`) · **team** = `-t` with a `team_local_id`, or `-t` with a `0xhash` (team H-flood). `-t` is the ONLY way onto the team overlay; a `team_local_id` is meaningless off it.
> **Swift:** `Command.send` needs a `teamPlane: Bool` → append `-t`; the team-local-id is a *distinct id space* from static node ids (don't mix them in one contact model — a team contact carries `{team_id, team_local_id}` and/or a hash).
> **★ 2026-07-19 (S2/S4, gated uncommitted):** (1) first-contact PLAINTEXT hash-sends auto-attach the sender's pubkey (INTRO — invisible to the app beyond a `peer_key_cached` arriving with the first message; `cfg set intro_attach 0` opts out; **new per-send flag `-K`** suppresses the attach for that one send, harmless with `-e`). (2) **`send_layer` under `e2e_dm`-ON now SEALS instead of refusing** — ★ **and since 2026-07-29 `send_layer` takes an explicit `-e`, and the HASH-RESOLVED cross-layer path seals too** (it previously discarded the crypt intent and sent **cleartext** — see the `§xl-crypt` BASELINE note). ⚠⚠ **A cross-layer sealed DM carries `DATA_FLAG_CRYPTED = 0` BY DESIGN** — it is a `DATA_TYPE_SEALED_RELAY` frame, because `e2e_seal_inner` refuses `CROSS_LAYER` and `enqueue_cross_layer` hard-sets it. ⇒ **the app must decide is-this-encrypted from the TYPE, never from the CRYPTED flag** (the old `err_unsupported` ack is gone — cross-layer encrypted DMs are live; the recipient's `msg_recv` carries `enc:true` + `origin_layer`). (3) A cross-layer/delegated sealed DM is sealed-CONTENT but attributable-envelope (the sender's hash rides in clear for routing/acks) — same-layer direct sealed DMs keep full sealed-sender privacy.
> ⚠ **HASH FORMAT CHANGE (2026-07-13): a key_hash32 argument MUST be `0x`-prefixed** (e.g. `0x8a3f1c02`) — on `send`, `send_layer`, `resolve`, `reqpubkey`, and `lookup`. This KILLS the id-vs-hash ambiguity: a **bare decimal is always a node id** (or a team-id for `reqpubkey`), a **`0x…` token is always a hash**. The old "exactly-8-hex auto-detected" form is GONE (a bare 8-hex now parses as an out-of-range decimal → `bad_args`). **`Command.swift` must prefix every hash argument with `0x`** (`hashof` on the node prints the `0x…` form for copy-paste).
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

- **⛔ `pull_inbox` STAYS RAW (§CUSTODY-C, 2026-08-30).** The firmware hides internal outcome records from its *own*
  OLED views only. The pulled stream is unchanged and still carries every `type:"e2e_ack"` record. **The decoder's
  obligation:** consume the receipt → correlate it to the OUTBOX by `(origin, ctr)` / `(sender_hash, ctr)` →
  **advance the DM cursor past it** → create **no** conversation row and **no** unread. This is what marks an
  *offline* message DELIVERED; the live `e2e_acked` push only covers the connected case, and `inbox_end` advances no
  cursor of its own. A firmware-side filter would lose that confirmation permanently. Classification is
  firmware-side (`data_type_traits().internal`); the companion rides the semantic wire names and never sees the C++
  trait table. (`AppModel.importInboxEntry`'s `isReceipt` arm already implements exactly this — verified 2026-08-30;
  this bullet makes the obligation contractual rather than incidental.)

## Whole-inbox clear — `clear_inbox confirm` (§CUSTODY-D, 2026-08-31)

Two verb forms, exact-token gated: `clear_inbox` (or any token other than exactly `confirm`) refuses and
changes **nothing**; `clear_inbox confirm` wipes **both** record stores (messages, receipts, tombstones).
Three ack shapes, NDJSON like every other ack:

```json
{"ack":"clear_inbox","result":"needs_confirm"}
{"ack":"clear_inbox","result":"cleared","epoch":4,"dm_seq":12,"chan_seq":7}
{"ack":"clear_inbox","result":"io_error","warning":"messages_may_remain","epoch":4,"dm_seq":12,"chan_seq":7}
```

`cleared` requires **both** stores empty with metadata persisted; `io_error` is explicitly possibly-partial
(both wipes are always attempted — one store may have cleared) and a later `clear_inbox confirm` after the
medium recovers completes the job. Two guarantees worth relying on: **`dm_seq`/`chan_seq` are the PRESERVED
sequence high-waters** (never zeros — no sequence is ever reused), and both read cursors are reset **on the
medium**. **No decoder change is expected**: the clear rides the established epoch path below — the epoch
changed, so reset cursors and re-pull; the re-pull returns nothing because the history really is gone. It
does not replace `prep-restart` or `factory_reset`; nothing outside the inbox is touched.

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

## `send_aired` — the ATTEMPT-LEVEL airing fact (§T3, 2026-08-14) · ★ PURELY ADDITIVE

A new push kind. ⛔ **No existing event changes**, and `PushKind` was **APPENDED** to, never renumbered — the numeric
values every earlier event carries are unmoved.

```json
{"ev":"send_aired","dst":5,"ctr":7}      // a DM:      dst = the peer, ctr = the origination counter
{"ev":"send_aired","dst":0,"ctr":300}    // a CHANNEL post: dst = 0, ctr = the 16-bit local correlation handle
```

**What it means, exactly:** the frame carrying a **locally originated** DM or channel post **physically left this
node's radio** — the SX1262 TxDone edge for that specific flight. Nothing more.

⛔ **IT IS NOT AN ACK AND IT IS NOT TERMINAL.** It says the frame was transmitted; it says nothing about anyone
receiving it. The authoritative send-level outcomes are unchanged and **still arrive afterwards**: `send_acked`,
`e2e_acked`, `send_failed`, `channel_sent` (and the ACK timeout). ⇒ **treat it as an UPGRADE of a queued state only:**

> `queued  <  send_aired  <  every terminal outcome`

⛔ **Never let a `send_aired` overwrite a terminal state that already arrived** — a delayed one can arrive after
`e2e_acked` or `send_failed`, and applying it there would downgrade a stronger fact to a weaker one.
ⓘ **It may repeat.** The MAC may transmit the same flight more than once; the firmware does not de-duplicate, so an
app must treat it as **idempotent**.

**Correlation, per plane — they do NOT share one key:**

| shape | correlate on | note |
|---|---|---|
| `dst != 0` (DM) | **`(dst, ctr)`** | the DM's own origination handle, as `send_acked` / `e2e_acked` already use |
| `dst == 0` (channel post) | ★ **`ctr` alone** | the SAME 16-bit handle `channel_sent` carries (§b40) |

⚠ **The channel `ctr` is a LOCAL handle and is 16-bit.** The wire carries only its low byte inside the channel
message id, so ⛔ never match it against a received `channel_msg_id`, and ⛔ never truncate it to 8 bits.

⛔ **Attempt FAILURES are deliberately not reported here.** A failed or unobserved transmit attempt is routinely
followed by a successful MAC retry, so surfacing one as an outcome would be a false negative. Only a positively
observed airing is pushed; failures reach the device's counters and telemetry only.

ⓘ **A companion that ignores this event entirely remains correct** — it is additive, and every existing outcome it
already handles still arrives.

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

### Node & peer human names (§1.3, 2026-07-14) — ✅ own name LIVE; peer name via the pubkey exchange
A node has a human name (default `MeshRoute node: 0x<hash>` derived from the stable hash). Two paths:
- **Own name → app:** ✅ **LIVE** — `ready` carries `"name":"<node name>"` (`console_json.cpp:299`, `write_ready`). Set it with **`cfg set name "<text>"`** (the **node** identity name — persisted to `/mrid`, distinct from **`cfg set leaf_name "<text>"`** which renames the *leaf* and bumps the config epoch). `MyCardView` uses `ready.name` for the QR `n`.
- **Peer name → app:** a peer's name **rides the pubkey exchange** — it's appended to the WANT_PUBKEY H query and to all three pubkey-answer frames, and cached alongside the peer key (immutable key / mutable name, refreshed on each exchange). So after a `reqpubkey`/answer (or a QR import), the node holds the peer's name. **Query it with `nameof 0x<hash>`** → today **human text** (`[nameof] 0x<hash> = "<name>"`, `firmware_commands.cpp:401`), **not JSON**. **Firmware ask — ✅ IMPLEMENTED 2026-07-16 (gated, uncommitted):** the cached peer name now reaches the app as JSON (spec `2026-07-16-companion-mobile-team-json-surface.md` §7): **`peer_key_cached` gains `"name"` (omit-when-unknown; the name is captured at cache time)** — `{"ev":"peer_key_cached","hash":3735928559,"conf":"authoritative","pinned":false,"name":"Alice's tracker"}` ⓘ **`conf` added 2026-07-31 (`§ab2`) — gate encrypted send on it, not on key presence** — and **`nameof 0x<hash>` answers `{"ev":"peer_name","hash":3735928559,"name":"…"}`** (`name` omitted when unknown; the old `[nameof]` human line is gone). `msg_recv` unchanged. So a contact auto-labels in the same event that enables encrypted send; the QR `n` stays the manual/override path.

### app → node: install a scanned peer's pubkey (PINNED / verified)
```
peerkey <ed_pub hex64>      # install a verified peer key from a scanned card. hash derives = ed_pub[:4].
```
- Firmware: 64-hex → 32 B; `key_hash32 = ed_pub[:4]`; `peer_key_set(key_hash32, ed_pub, PINNED)` (which
  re-verifies `ed_pub[:4]==key_hash32`). **PINNED = a new tier above `authoritative`: never LRU-evicted,
  never aged, and NEVER overwritten by an on-air `WANT_PUBKEY` answer for the same hash** — else an attacker
  who grinds a colliding 32-bit hash and answers on-air could replace the scanned key (defeating the
  ceremony). **NV-persisted** (a small `/mrpeers` store, the `/mrid` write pattern) so a verified contact
  survives reboot without re-scanning.
  ★★ **UPDATED 2026-07-31 (`§ab1`) — TWO app-facing IMPROVEMENTS the app currently assumes are absent:**
  **(1) the store is no longer "pinned keys only".** It now also persists keys learned **ON AIR** at `authoritative`,
  **with their cached names** ⇒ **the ability to send ENCRYPTED to an on-air peer now SURVIVES a reboot**, and a rebooted
  node no longer needs a manual `reqpubkey` per peer. (Provenance is preserved, not widened: a stored `authoritative`
  restores as `authoritative`, never as `pinned`, and any other value is skipped — so `pinned` still means "a human
  verified this by QR".)
  **(2) ⚠ the claim below that "the name is NOT sent" is STALE** — names have ridden the pubkey exchange since §S6, and
  as of `§ab1` they **persist** too. The app may treat a node-supplied name as a starting label. *(original text:)*
  The name is NOT sent (names are app-side; the firmware key cache is
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
reqpubkey <0xhash | id> [-s|-t]   # fire ONE HARD WANT_PUBKEY (the "request key" UX action)
```
⚠⚠ **THIS SECTION IS SUPERSEDED IN TWO PLACES BY THE `§id-hash` BLOCK AT THE TOP — read that one.** Kept here because
the surrounding mutual-exchange text below is still accurate. The two corrections:
1. **The id form exists and a bare id is AUTO, not TEAM.** `-s` / `-t` select the plane and are mutually exclusive.
   **`Command.reqPubkeyTeam` MUST emit `-t`.**
2. ★ **"The no-crypto-identity failure path keeps its existing error ack" was FALSE and is now fixed.** That path
   returned `queued`, which BLE turned into `reqpubkey_sent` — one of **five** ways the event claimed a request had
   flown when nothing had. It now answers **`err_no_identity`**.
- Firmware: `emit_hash_query(hash, hard=true, want_pubkey=true)`. The verb returns
  `{"ev":"reqpubkey_sent","hash":<resolved key_hash32>[,"plane":"team"|"static"]}` (`write_reqpubkey_sent`).
  ★ **`hash` is the RESOLVED hash for the id form — it used to be `0`.**
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
{"ev":"peer_key_cached","hash":3735928559,"conf":"authoritative","pinned":false}  // a key arrived → enable resend ONLY when conf >= authoritative
```
- `send_failed.reason` ∈ `no_pubkey · no_identity · too_large · bad_rng · no_route · joining · no_cts · no_ack · cap · min_interval · mobile_no_home · gateway_unreachable · e2e_ack_timeout · queue_full · reprovisioned · unsealable`.
  ⚠ **CORRECTED 2026-07-29 — this list had drifted and omitted TWO shipped reasons:** `reprovisioned` and
  `unsealable` (enum 16). The reason list near the foot of this document is **staler still** — treat *this* one
  as authoritative. ★ **`unsealable` is PERMANENT for that route, not transient.** It fires when a `-e` send
  cannot be sealed on the path it must take: a **typed** payload cross-layer (SEALED_RELAY has exactly one TYPE
  byte and spends it on itself), or a delegated team-key grant. **App action: send from a node on the target own
  layer** — or, for a team key, grant over the team plane (`-t`). Retrying the same route always fails.
  App maps `no_pubkey`
  → "recipient's key unknown — Request key / Scan QR"; permanent reasons (`too_large`/`no_route`) → plain fail.
  ★ `e2e_ack_timeout` (NEW 2026-07-24, enum 13): a `-a` send's requested E2E ack never arrived within the firmware
  deadline (same-layer 60 s / cross-layer+delegated 300 s, patience-derived). **Semantic: delivery was never
  CONFIRMED — NOT that it failed**: the DM may have arrived and the ack died returning; a LATE ack still fires
  `send_e2e_acked` and the app resolves (timeout-then-ack = delivered, slow confirm). Also NEW: `err_ack_ring_full`
  (CmdCode 9) — a new `-a` send is synchronously REFUSED while 8 sends already await acks (never silently dropped).
  (2026-07-21 3-A: `gateway_unreachable` NEW — a cross-layer DM held for a gateway window that never became reachable;
  `mobile_no_home` now actually renders — a pre-existing renderer hole made it read `"none"`; the previously-bare
  `reason:"none"` giveups now carry real reasons: the deferred-TTL giveup → `no_route`, the NACK-path giveups → `no_cts`.)
- ⚠⚠ **CORRECTION 2026-07-25 — TWO documented strings were NEVER RENDERED, and the "class closed" claim above was
  premature.** `sendfailreason_name`/`cmdcode_name` (`lib/console/console_json.cpp`) are hand-maintained tables that
  had drifted from their enums:
  - **`e2e_ack_timeout` (enum 13)** was documented 2026-07-24 but had no renderer case, so **from `8228b11` until
    2026-07-25 every e2e-ack timeout reached the app as `reason:"none"`.** Any app logic keyed on the documented
    string was dead code. Now renders as documented.
  - **`err_ack_ring_full` (CmdCode 9)** was likewise unrendered — `{"ack":…}` carried **`"err_unknown"`** over the
    same window, i.e. the deliberately-loud ring-full refusal was mute at the app boundary. Now renders correctly.
  - The 2026-07-21 3-A sweep was **incomplete**: the defer-queue-full refusal (`node_cascade.cpp` `defer_send`,
    nine lines below the giveup 3-A did fix) still pushed with **no `reason` key at all** — note `write_push` omits
    the key when `reason == none`, so the app saw an *absent field*, not the string `"none"`. It now carries
    `queue_full`.
  - Why this survived a green gate: the simulator **does not compile `lib/console`**, and its `push` event carries
    only `{ctr,dst,kind}` with no `reason` field — so the oracle showed correct strings while the app got the
    fallback. The class is now closed by a native test that walks every enumerator of every mapped enum (failing
    the BUILD on a new enumerator, and the TEST on a missing case) plus gate-blocking `-Wswitch`.
- ★ **NEW reason `queue_full` (2026-07-25, enum 14, appended — nothing renumbered):** the node's no-route **defer
  queue** (`cap_deferred_sends` = 32; 16 on a gateway build) was full, so the NEW send was **refused synchronously**
  rather than evicting an older held send. Typically fires when sends are originated faster than routes converge —
  e.g. just after boot. **Semantic: TRANSIENT — back off and resend.** Distinct from `no_route`, which means a
  specific send aged out without ever finding a route. ⚠ App note: previously indistinguishable from a legacy bare
  giveup, because the `reason` key was absent entirely.
- `join_refused.reason` gains (2026-07-21 3-A, mobile flavors): `phy_mismatch` — a TEAM member refused a home whose
  PHY differs from its team-provisioned config (`layer_id` = the candidate's layer, `dst` = its routing_sf; the
  P2-1 fail-loud now reaches the app on metal) — and `sf_list_mismatch` — configured-vs-offered sf_list low-byte
  divergence (`origin` = configured, `dst` = offered; ADVISORY, the mobile still adopts). `leaf_full` now also
  fires from a full TEAM-DAD pool (same reason value, team source). All rate-limited on the shared 60 s window.
- `peer_key_cached` lets the app prompt "secure send ready — resend" after a request resolves (or QR import).

> ### ★ RULING 2026-07-29 — `no_pubkey` is NEVER resolved automatically, and that is deliberate
> A CRYPTED send to a hash whose key we do not hold **fails loud** and does **not** escalate to an on-air
> `WANT_PUBKEY` locate. Verified in firmware: **every** send-by-hash locate passes `want_pubkey = false`
> (`node_hashlocate.cpp` — three sites), so the `AUTHORITATIVE_H_ANSWER` carries only the hash→id binding and
> the seal then refuses at `e2e_seal_inner`.
> **Why, and why the app must not paper over it:** auto-escalating would silently prefer the **on-air TOFU**
> path over the **MITM-resistant QR ceremony** — for a message the user explicitly marked `-e`. This document
> already states that on-air `WANT_PUBKEY` resolution is *not* MITM-secure while a physical scan **is** the
> trust ceremony; resolving automatically would make that trade on the user's behalf, invisibly.
> ⇒ **The app surfaces the choice; it must not issue `reqpubkey` silently on a `no_pubkey` push.** Offer
> **Request key** (on-air, TOFU — label it as such) *and* **Scan QR**, and let the user pick. ★ `reqpubkey` is
> **mutual** — the requester's own pubkey rides across every forward — so one call keys both ends; there is no
> need to ask the peer to run it too.
> ⚠⚠ **"Scan QR" here means the VERIFIED-PEER contact card (`p` = `ed_pub`), NOT the team QR — there are two QR
> types and they solve different problems.** Do not wire the team QR to a `no_pubkey` push; it carries no peer
> identity key and cannot resolve one.
>
> | | verified-peer QR | team QR |
> |---|---|---|
> | carries | the peer's **identity** `ed_pub` | the **team content** keypair + PHY params |
> | fixes | `no_pubkey` on a sealed **DM** | `team_channel_no_key` / un-keyed team member |
> | needs `reqpubkey`? | it **replaces** it | **no** — irrelevant to it |
>
> ★ **And note what the team QR does NOT need: nothing.** It is pure companion-link provisioning — the app writes
> `team <id> tkpub=… tkpriv=…` over USB/BLE, nothing is sealed and nothing goes on air, so it works on a node
> that has never met a teammate. One scan is full onboarding (overlay **and** content key).
> ⇒ **`team grantkey` (T-K3) exists for the REMOTE teammate** — already in the overlay, not standing next to you
> to scan. It ships the content key **over the radio**, which is precisely why it needs the recipient's identity
> key sealed first, and therefore why it is the one path `reqpubkey` gates.
> ⚠ This applies with most force to **`team grantkey`**, whose payload is a **private key**: it is the worst
> possible place to downgrade to TOFU without the operator knowing. Its refusal names `reqpubkey <hash>` as the
> remedy rather than performing it.
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
- `*_next_ms` fold the burst-floor remaining AND duty recovery — the "ready in N ms". ⚠ On a channel **cap**-block (window cap reached, the floor already passed) `ch_next_ms` is currently **`0`** (the exact cap-recovery time is a deferred refinement); the `send_blocked{reason:"cap"}` push still signals the block, so the app backs off + retries anyway. ★ `ch_min_ms` / `dm_min_ms` (and, via the fraction, `ch_cap`) are the **leaf's configured** values (see *anti-spam leaf tunables* below), not fixed firmware constants — so pacing reflects the actual leaf policy.

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
  field is reserved for a future channel-crypto phase — **now DESIGNED + RATIFIED (2026-07-26): the team
  encrypted channel** (`docs/superpowers/specs/2026-07-26-team-encrypted-channel-design.md`). See "Planned
  for v1" below; `enc:true` will start being emitted on encrypted team-channel deliveries.

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

## Leaf-config membership + provisioning (firmware R6.1–R6.3 DONE; companion surface IMPLEMENTED + GATED 2026-07-03)

R6 adds **managed leaves**: a fresh node sets a small radio floor (freq + control SF + **layer**), then **auto-joins** (DAD an id) and **auto-pulls its leaf config** — data SFs / duty / name / anti-spam tunables — from the network. The operator never hand-sets the data config on a joiner; only the *rendezvous floor* is manual. Firmware how-to: `docs/LEAF_PROVISIONING.md`. The R6 firmware (R6.1–R6.3) is committed/gated; the **companion JSON surface below is IMPLEMENTED + GATED** - the console_json membership fields + the `join_refused` push + the **`key=value`** join/create/leave verbs (`layer=`, this session's rename + quality-gate).

### Node → app: membership state (which leaf, synced?)
A node's leaf membership = `lineage_id` (u16; **0 = unmanaged / standalone**), `config_epoch` (u16), `leaf_name` (string), `layer` (1..255; the wire leaf nibble = `layer & 0x0F`), and **synced** (`lineage==0 || epoch>0`). Two carriers:

1. ✅ **`ready` snapshot gains them** (firmware ask — add to the `ready` writer):
```json
{"ev":"ready", … ,"lineage":41153,"epoch":3,"leaf":"north field","layer":2,"synced":true}
```
- `lineage:0` ⇒ app shows "unmanaged / standalone". `lineage≠0 & synced:false` ⇒ "joining…". `synced:true` ⇒ "member of <leaf>".
- *Note:* `layer` (the 1..255 network id; "leaf" is its `& 0x0F` nibble) — ⚠ the firmware currently sends the wire **leaf nibble** (0..15) under `layer` (the full id is NV-side; plumbing it is a deferred follow-up). Treat as an opaque label for now.

2. ✅ **`config_adopted` live push — DONE** (`console_json.cpp` `write_push` config_adopted arm reads `g_node.config()`; fired by the node's config-adopt path in `node_query.cpp`). Fires when the node adopts/updates its leaf config (on join, on a propagated operator write, on an LWW change):
```json
{"ev":"config_adopted","lineage":41153,"epoch":3,"leaf":"north field","layer":2}
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

### App → node: provisioning verbs (**`key=value` grammar as of 2026-07-03** — mirrors `gateway`; order-free)
For a "Join network / Create leaf / Leave" UI. All apply **live — no reboot**:
```
join   layer=<1..255> freq=<MHz> bw=<kHz> sf=<ctrl_sf>                                          # join existing net: floor, auto-DAD, auto-pull
create layer=<1..255> freq=<MHz> bw=<kHz> sf=<ctrl_sf> sf_list=<7,9> duty=<pct> name="<leaf>"   # mint a managed leaf — this node = mother
       [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]                               #   anti-spam knobs OPTIONAL → protocol defaults
leave                                                                                           # reset membership (wipe to default, KEEP freq)
```
- **`key=value`, order-free** (the same grammar as `gateway`). **`layer`** = the 1..255 network id (the on-wire **leaf** nibble = `layer & 0x0F`; "leaf" is reserved for that 0..15 nibble). `sf_list` = comma SFs (`7,9`, one token). `bw` = kHz, **may be fractional** (`62.5` / `41.67` / `31.25` — the firmware `atof`-parses it → 62500 Hz). `duty` = a **percent**, **may be fractional** (`0.1` = a tight EU sub-band). `name` = quoted (spaces OK). CR is a fixed low default (4/5), not exposed.
- **Anti-spam knobs** (`active_fraction` / `ch_min_ms` / `dm_min_ms`) are **optional** on `create`; omitted ⇒ the protocol **defaults** (`0.125` / `10000` / `3000`), never inherited from the minter's current settings. The app may expose them in an "advanced" create sheet or omit them (see the *anti-spam leaf tunables* section below + `docs/anti-spam.md`).
- **The old `leaf` command is gone:** `leaf create` folded into `create`; **rename a leaf via `cfg set leaf_name "<text>"`** (bumps the epoch, propagates live) — distinct from `cfg set name` (the **node** identity name).
- **Swift:** `Command.join` / `Command.createLeaf` emit the `key=value` `line` (`Command.swift`); the `.createLeaf` enum carries freq/bw/sf/layer/sfList/duty/name with **`bwKHz` + `dutyPercent` as `Double`** (fractional-capable, emitted compactly via `freqToken`). The anti-spam args are a future optional Swift field — omitting them yields the firmware defaults.
- After `join`/`create`, the node emits `config_adopted` + updated `ready` membership once it syncs. After `leave`, membership returns to `lineage:0` (unmanaged). A `send` before sync ⇒ `send_failed{reason:"joining"}`.
- **Normal nodes only.** Gateways provision differently (multi-layer; a future `join_as_gateway`) — out of this contract.
- ~~Ack shape = firmware's choice~~ **→ ✅ IMPLEMENTED + GATED 2026-07-16 (uncommitted): the join/DAD progress bracket.** The DAD takes ~6 s (3 s listen + 3 s claim-guard) and used to complete with NO app-visible event on an unmanaged leaf; now:

### ✅ Join/DAD feedback — `join_started` (verb ack) + `join_adopted` (push), 2026-07-16
```json
{"ev":"join_started","layer":4,"leaf":4,"freq_khz":869500,"sf":9,"bw_hz":125000}                                          // join accepted — DAD begins
{"ev":"join_started","create":true,"layer":4,"leaf":4,"lineage":41153,"leaf_name":"north field","freq_khz":869500,"sf":9,"bw_hz":125000}   // create variant
{"ev":"join_adopted","id":17,"layer":4,"epoch":3}                                                                          // the node adopted its id — DAD complete
```
- **`join_started`** REPLACES the human success line on `join`/`create` (usage / `nv_save_failed` errors stay human text). `"create":true` + `lineage` + `leaf_name` appear only for create. Integer `freq_khz`/`bw_hz`.
- **`join_adopted`** is a push, fired at the adopt itself — so it also arrives on the **boot DAD** (app connected at power-on) and on a **heal re-adopt** (address-conflict loser re-picks an id). ★ **App rule: on `join_adopted`, refresh identity state** (`ready.id` may have silently changed mid-session — before this push an id change was invisible). `epoch` = the DAD claim epoch.
- **The app's join spinner:** send `join`/`create` → `join_started` opens the bracket → `join_adopted` closes it (~6 s later; retries on a congested leaf just lengthen it) → on a **managed** leaf `config_adopted` then flips "joining…"→"synced" as before (an unmanaged leaf is done at `join_adopted`). Failure terminals unchanged: `join_refused{wire_version|leaf_full}`; a send meanwhile ⇒ `send_failed{joining}`.
- `leave`'s ack stays human text (deferred — it completes instantly; membership shows in the next `ready`).

### App → node: anti-spam leaf tunables (2026-07-03 — promoted to leaf config)
The anti-spam v2 knobs below are now **per-leaf config** (carried in the C config frame + folded into the `config_hash`), not fixed firmware constants — so a mother provisions them and a change **re-fingerprints** the leaf (members re-pull → `config_adopted`). Set via `cfg set` (applies **live**, is **persisted to NV** so it survives reboot, and on a **managed** leaf bumps `config_epoch` + re-advertises):
```
cfg set active_fraction <0..1>   # channel-cap fairness divisor (default 0.125): how aggressively the per-origin channel cap shares the mesh's channel capacity C
cfg set ch_min_ms <ms>           # channel burst floor (default 10000): min spacing between one origin's channel floods
cfg set dm_min_ms <ms>           # own-DM burst floor (default 3000): anti-per-keystroke — e2e-ack / rcmd are exempt
```
- All three are in the `config_hash`: changing one on a **mother** propagates to members (they re-pull + emit `config_adopted`); on an **unmanaged** node it's a local setting only.
- They are the source of the `ch_min_ms` / `dm_min_ms` (and via the fraction, `ch_cap`) fields the **`limits`** query reports (above) — so the app's pacing tracks the leaf's actual floors, not the defaults.
- **Optional on the `create` verb** (`[active_fraction=] [ch_min_ms=] [dm_min_ms=]`, 2026-07-03): a mother may set them at mint time; **omitted ⇒ the protocol defaults** (`0.125` / `10000` / `3000`), *never* inherited from the minter's current settings. Or tune them later on any managed node with `cfg set` (bumps the epoch → propagates).
- Wire: the C config frame grew **+6 B** (`active_fraction_bp` u16 · `ch_interval_ms` u16 · `dm_interval_ms` u16); `wire_version` is **unchanged** (the test fleet reflashes together — no mixed-version compat).
- **★ App flash-wear note:** every `cfg set` persists to flash immediately (so a reboot keeps it). If a UI control is bound to one of these knobs, send `cfg set` on **release / commit, not during a live drag** — a slider firing per-frame would hammer flash. The firmware skips byte-identical rewrites (a wear backstop), but the app must not rely on that to spam writes.

### Deferred
- Static-node cross-leaf roaming (auto `leave`+`join` with hysteresis) — subsumed by the **mobile node** below (a mobile IS the roaming primitive; a static node stays put).

## Mobile node + teams (mobile + teams 6.1–6.4 + routing parity + liveness committed; ★ the STATE-JSON surface IMPLEMENTED + GATED 2026-07-16, uncommitted)

A **mobile** is a roaming endpoint: a **stable hash** but a **home-assigned local id**, reachable by any node via its hash — the firmware resolves the hash to the mobile's current **home** node, which does the last-mile. A **team** is a `team_id`-scoped overlay of mobiles, for **member-to-member routing** + **group chat**. Landed since the 2026-07-09 revision (most gated in this session):
- **team-DAD** including **off-grid team-DAD (6.4)** — an off-grid team (no static home) self-assigns `team_local_id`s and routes among itself.
- **team-plane DV routing (6.2)** — a separate `_rt_team` table; teammates are reached member-to-member, incl. **multi-hop** (the s23/s24 scenarios).
- **team channel (6.3)** — a `team_id`-scoped group broadcast (send side live; see below).
- **team multi-hop routing parity** — team **F** reactive discovery + a **team-scoped H-flood** for hash resolution (so `send <hash> -t` reaches a *never-heard, multi-hop* teammate, not just a 1-hop neighbour). *Latest work — may still be uncommitted when you read this.*
- **plane separation (enforced + tested):** a team frame (beacon/DV/F/H/channel) is **never** learned/relayed on the **static** plane, and **never** by a **different `team_id`** — the static plane stays byte-identical (`s18` md5 tripwire), a co-located other team drops it. This is why the app's team ids live in their own id space (§ addressing above).

**★ Reality check for the app — RESOLVED 2026-07-16:** the command/routing spine was already live (`send -t`, `reqpubkey -t`, team channel, team-DAD); the app-facing **STATE JSON is now IMPLEMENTED + GATED too** (spec `docs/superpowers/specs/2026-07-16-companion-mobile-team-json-surface.md`, slices S1–S6; native 743/25450 · s18 byte-identical `3ac88d40…` · s22–s26 0-fail · boards 10/10 · RAM +128 B; uncommitted, bench-verify pending). **Conventions:** team ids ride as a **quoted lowercase hex string** exactly like `key` (`"team":"cccc0001"` — NOT decimal); `hash` fields stay decimal u32; every new `ready`/push field is **omit-when-inactive** (a static, teamless node's JSON is byte-identical to before — absent ⇒ false/0, the `enc`/`seq` convention). The blocks below carry the final LIVE shapes.

### App → node: mobile provisioning + control (console verbs LIVE; ✅ status/gateways JSON 2026-07-16)
```
cfg set mobile 1                      # make this node a mobile (persisted NV v6; REBOOT to start the FSM)
cfg set mobile_autoregister <0|1>     # 1 (default)=node auto-registers/roams; 0=the APP drives it
mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]   # (re-)register: current PHY / a given PHY / cycle learned nets
mobile gateways                       # list learned gateways + neighbouring networks (cross-layer)
mobile query <gw_id>                  # pull the layer directory from a gateway
mobile status                         # this mobile's registration + current PHY + known networks
```
- **App-driven model (`mobile_autoregister=0`):** the node does nothing autonomously — the app orchestrates register/roam via these verbs. Default-ON keeps a hands-off node self-registering. `mobile`/`mobile_autoregister` are preserved across `join`/`create` (same as any role config).
- ✅ **`mobile status` / `mobile gateways` (and the not-a-mobile error) now answer JSON** (2026-07-16, below); `mobile register`/`mobile query` keep human one-line acks (the state outcome arrives via the `mobile_reg` push / `mobile_gw*` stream — JSON acks deferred).

### Node → app: mobile state — ✅ IMPLEMENTED 2026-07-16 (the JSON surface, gated, uncommitted)
`ready` gains the role + registration + team, ALL **omit-when-inactive** (a static/teamless node is byte-identical); `cfg` gains `mobile_autoregister` + `team_id` (always present there — cfg is the explicit dump, `"team_id":"00000000"` when unset); two live pushes track registration:
```json
{"ev":"ready", … ,"mobile":true,"mobile_registered":true,"mobile_home":222,"mobile_local":17,"mobile_home_layer":4,"hosting":2,"team":"cccc0001","team_local":9}
{"ev":"mobile_reg","home":222,"local":17,"home_layer":4,"epoch":6,"registered":true}   // on register / re-home (a changed home + registered:true = a roam)
{"ev":"mobile_reg","home":0,"local":0,"registered":false}                              // home lost / deregistered ("searching for home…"; auto-rediscovers when autoregister=1)
{"ev":"team_reg","team":"cccc0001","local":9}                                          // team-DAD id adopted / conflict re-pick
```
- `mobile:true` gates the `mobile_*` block (omitted entirely on a static node). `mobile_home`/`mobile_local` = current home + home-assigned local id (0 = unregistered); `mobile_home_layer` present only when registered. `hosting` = mobiles THIS node hosts (omit when 0; a static host). `team` = the team_id as a **hex string like `key`** (omit when none); **`team_local`** = our OWN id on the team overlay (omit when 0) — the id teammates address us by.
- ✅ **`mobile status` as JSON:** `{"ev":"mobile_status","mobile":true,"registered":…,"home":…,"local":…,"epoch":…,"home_layer":…,"autoregister":…,"layer":…,"freq_khz":869525,"sf":9,"bw_hz":125000,"nets":2}` — integer kHz/Hz (no floats on the wire); `home_layer` omitted unless registered. On a non-mobile node any `mobile` verb answers `{"ev":"mobile_err","reason":"not_mobile"}`.
- ✅ **`mobile gateways` streamed** (the `routes`/`routes_end` pattern): `{"ev":"mobile_gw","gw":3,"leaf":4}`* then `{"ev":"mobile_net","layer":7,"name":"north field","freq_khz":869525,"sf":9,"bw_hz":125000}`* then `{"ev":"mobile_gw_end","gws":1,"nets":2}` — the roam-UI data (`mobile register freq=… sf=… bw=…` targets a `mobile_net` row).

### App → node: team provisioning (LIVE — 6.1)
```
team new              # MINT a fresh team_id = hash(key‖nonce) → this node is the team creator
team <hex_id>         # JOIN an existing team by id
team 0                # LEAVE
# ★★ REMOVED 2026-07-31 (§team-id-cfg-removal): `cfg set team_id` NO LONGER EXISTS -> `unknown_key`.
# Use `team <hex>` / `team new` / `team 0`. Those three are now the WHOLE surface. The key was an unguarded
# duplicate: it accepted `exportky` (-> LEAVE the team), `88A672BA` (-> team 88) and out-of-range values
# (-> garbage team on the 32-bit boards). ⓘ team_id STAYS fully readable: `cfg`/`status` text, the JSON, and
# binary TLV 0x12 in enc_cfg/dec_cfg are ALL unchanged -- tag 0x18 (loc_dm) was retired, 0x12 is NOT.
```
- Persisted (NV v18). A team is `is_mobile`+`team_id` — a mobile joins a team on its layer. ★ **And that pairing is
  becoming ENFORCED** (owner ruling 2026-07-31, spec `2026-07-31-node-role-model-design.md`): adopting a team will
  **auto-set `is_mobile`**, `cfg set mobile 0` will **refuse while in a team**, and `is_gateway`+mobile will be refused.
  ⇒ **the app should treat "team ⇒ mobile" as an invariant, not a coincidence**, and must not try to set them apart. **State:** the `team` field in `ready` (above) + `status` shows `team=0x…`.

### Team channel — group chat (6.3, IN PROGRESS)
A team channel = a `team_id`-scoped channel: any member broadcasts, only same-team members receive; static nodes never see it. Rides the existing channel surface with a **team_id** tag:
- **Send — ⚠ BREAKING (S7, 2026-07-19):** `send_channel` now plane-selects like `send`: **`-t` = TEAM** (the old auto-team must now be explicit — `-t` is ACCEPTED, no longer rejected) · **plain or `-g` = GLOBAL** (a registered mobile delegates via its home; an off-grid mobile's plain send fails loud) · **`-t -g` = BOTH planes**. `Command.swift` must add the flags; a team-chat send MUST pass `-t`. Also: a REGISTERED mobile now RECEIVES leaf/static channel messages (`channel_recv` without `team_id` — receiver-only).
- **Receive — ✅ IMPLEMENTED 2026-07-16:** `channel_recv` / `inbox_channel` gain **`"team_id":"cccc0001"`** (a **hex string**, like `key` — the old decimal sketch is superseded) when the message is team-scoped; omitted ⇒ a normal leaf channel — byte-identical. The app threads a team channel into its team view; DM/channel identity keys are unchanged (`channel_msg_id` stays the dedup key). The durable tag carries the ACTUAL team id (not a flag), so history stays correctly labelled across a team switch. ⚠ The record-format growth bumped the inbox store version — first boot after reflash wipes the on-node inbox + bumps `inbox_epoch` (normal epoch re-pull; `next_seq` preserved, so seq never reuses).

### Reaching a mobile vs a teammate — the plane decides (⚠ the send verb DIFFERS)
- **A home-attached mobile (NOT your teammate)** — plain **send-by-hash**, `send <0xhash> "…"` (`/ -e`), **no `-t`**. The firmware resolves the hash to the mobile's home and last-mile-forwards; the mobile's local id never appears app-side. Encrypted DMs use the same `peerkey`/`reqpubkey` path (the home carries the mobile's pubkey, transparent to the app). Unreachable (stale/no home) ⇒ `send_failed{reason:"no_route"}`. **This is the only case that is still "no app change."**
- **A teammate** — **`-t` is required** (see ★ HARD PLANE SPLIT under *Commands*): `send <team_local_id> "…" -t` (by id, member-to-member via `_rt_team`) or `send <0xhash> "…" -t` (team H-flood resolves it). **A teammate is NOT reachable by a plain send-by-hash** — a plain send goes to the global/home plane and will `no_route`.
- **Encrypted team DM:** provision first with **`reqpubkey <0xhash> -t`** (team-scoped, mutual — both cache each other's key over `_rt_team`), then **`send <0xhash> -t -e`**. Same `send_failed{no_pubkey}` / `peer_key_cached` UX as a static encrypted send; the only difference is the `-t` scope. (A team member's own hash is seed-derived, `ed_pub[:4]`, same as any node.)

### "Constant home traffic" the app will observe (not user DMs)
An **autoregistering** mobile (`mobile_autoregister=1`, default) chats with its home on two 10-min timers: a **re-CLAIM** (registration keepalive, `mobile_reclaim_ms`) and a **layer-directory pull** — `DATA_TYPE_MOBILE_LAYER_QUERY`(10)→`_ANSWER`(11) (`mobile_layer_query_period_ms`). Plus a one-time **pubkey push** to the home (`DATA_TYPE_MOBILE_PUBKEY_PUSH`(12)) so the home answers WANT_PUBKEY on its behalf, and, for app-delegated sends, `DATA_TYPE_MOBILE_SEND`(14). None of these are inbox records or app-visible pushes — they ride below the app. Set **`mobile_autoregister=0`** to make the app drive registration on demand (no autonomous keepalive).

## ⚙️ Duty-cycle status — companion readout (IMPLEMENTED — `write_duty` + `duty_pct`/`duty_avail_ms` in `ready`; spec `docs/superpowers/specs/2026-06-21-duty-cycle-readout.md`)

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

## Firmware asks (app → node agent · D31/§8.2 · living — amendable by BOTH sides)

App needs stated; the wire/design shape is the node agent's call — please update this doc with the final shapes
(the app builds decode-after-contract, never ahead of it). **Status: Ask 2 ✅ BUILT** (`loc_src:"team"` is live).
**Ask 1 ⊂ Ask 3** — Ask 3 (2026-08-16) supersedes and sharpens it with the profile split and the measured
foot-gun; read Ask 3 first, Ask 1 remains for the items it still carries (unlock/lock, verb coverage).

### Ask 1 — remote-admin app surface (P1-remote: configure a node THROUGH another node) — folded into Ask 3
The authenticated `rcmd` spine exists (open reads cleartext; sealed writes behind `unlock`; binary-TLV responses).
The app needs an app-consumable shape on the BLE side:
1. **`rcmd` responses as JSON lines** the companion can decode (e.g. `{"ev":"rcmd_resp","from":<id>,"verb":"…","ok":…,…}`
   — or a documented envelope), replacing the dead `[rcmd <from>]` console echo. Open reads (`status`/`routes`)
   ideally reuse the existing `status`/`route` writers tagged with `from`.
2. **`unlock <passphrase>` / `lock` over BLE with JSON acks** (+ an unlocked-state flag somewhere readable, e.g.
   in `ready` or an ack), so the app can gate its remote-write UI.
3. **An error model** the app can render: `no_admin_key` (target has none pinned) · `locked` (unlock first) ·
   `stale` (replay-rejected) · timeout behaviour (silent? push?).
4. Verb coverage for the P1 use cases: remote `cfg get/set`, `reboot`, `status`, `routes` (what else is cheap?).

### Ask 3 — the CONFIGURATOR path: let a mobile-attached phone configure another node (2026-08-16)

> ⚠ **This section is NEGOTIABLE and expected to move — on BOTH sides.** It states an app NEED and the product
> flow behind it; the wire/design shape is the node agent's and the owner's call. Amend it freely, and when a
> slice lands QA writes the as-built shape here (the app builds decode-after-contract, never ahead of it).

**Product context (owner, 2026-08-16 — this is *why*, and it is the part worth arguing with).** The companion is
reshaping into **two contexts on one phone**: a **mobile companion** (messaging/team/position — attached to the
user's own mobile) and a **configurator** (config + diagnostics, *target-scoped*: "configure node X"). Two entry
paths were specified:
- **A — direct:** connect over BLE to a fresh **static/gateway** node and configure it. ✅ Works today.
- **B — indirect:** start on the **mobile** companion → *view network* → pick a node → configure it remotely.
  ❌ **Structurally impossible today**, for the reasons below. **Path B is the flow this ask exists to unblock.**

**1. ★ Split the remote-management capability into ORIGINATE vs ACCEPT.**
Today one flag does both (`MR_FEAT_REMOTE_MGMT`, `lib/core/mr_features.h:11-49`), and it is **0 on the mobile
profile**. Consequences measured in-source:
- A mobile **cannot be administered** — `remote_exec` is an inert stub (`src/firmware_remote.cpp:158-160`); it
  ignores even the open `status`/`routes` reads a static node answers. ★ **The app is fine with this — a personal
  tracker should NOT be administrable. Keep ACCEPT = 0 on mobile.**
- But a mobile **can still issue `rcmd`** (`handle_rcmd` always compiles, `firmware_remote.cpp:182`) while the
  **sealing branch is gated out** (`:192-210`). So a gated verb from a mobile **flies in cleartext**, the target
  **silently drops it**, and the console prints a success-looking `> rcmd -> N "reboot" ctr=…`.
  ⇒ **This is a foot-gun, not a missing feature — it is the single most urgent item here.** Either make
  ORIGINATE work on mobile, or make the mobile **refuse loudly** so the app can say "this node cannot do that".
- **The ask: `MR_FEAT_REMOTE_ORIGINATE` = 1 on mobile, `MR_FEAT_REMOTE_ACCEPT` = 0.** The operator's phone is
  attached to a *mobile*; that is where the configurator flow begins. Without originate-from-mobile, Path B
  cannot exist at all.

**2. Route the `rcmd` RESPONSE to the requesting transport, as JSON.**
✅ *The plumbing already exists* — the command-sink consolidation gave the JSON handlers a `Print& out`
(`mrcon` on USB, a `LineSink` over BLE — `src/fw_main.cpp:311-313`), which is how `status`/`routes`/`cfg`/`peers`
reach the phone. ❌ **The `rcmd` response printer still hardcodes the USB console** — `mrcon.print(F("[rcmd "))…`
as human text at `src/fw_main.cpp:1464-1474`. So the phone that issued the command never sees the answer.
⇒ **The ask is small and mechanical: send that response through the same sink and emit it as JSON** — e.g.
`{"ev":"rcmd_resp","from":<id>,…}`; open reads could reuse the existing `status`/`route` writers tagged with
`from`. Shape is the node agent's call.

**3. Challenge–response (the replay scheme) — still owed.**
`src/firmware_remote.cpp:72-90` labels the current monotonic-counter scheme **known-broken by design, redesign
owed**, and a repo-wide grep for `challenge` finds only that comment — i.e. the ratified
`2026-07-26-remote-admin-challenge-response-design.md` is **not built**. The app cannot offer a trustworthy
remote-write UI on top of a scheme the firmware itself calls broken. App-side promise, unchanged: **the challenge
lifecycle stays INVISIBLE** (cache per node, auto-bootstrap, auto-resync once) — the operator never sees one.

**4. A failure vocabulary the app can render.**
"Silently dropped" is unusable in a UI. The app needs to distinguish, per attempt: **target cannot be
administered** (e.g. it is a mobile) · **no admin key pinned** · **locked — unlock first** · **stale/replay** ·
**no route / timeout**. Whether these arrive as `rcmd_err{reason}` or on the existing `send_failed` is the node
agent's call; the app only needs them to be *distinguishable*.

**5. Unlock/lock over BLE with acks** + a readable unlocked-state flag (in `ready`, or an ack), so the app can gate
its remote-write UI rather than discovering the lock state by failing. (Carried over from Ask 1, still open.)

★ **What the app will do meanwhile:** build the configurator **target-scoped** with capability gating, so the
remote leg is a *transport swap* and not a redesign — direct-BLE targets work today, remote targets light up when
the above lands. The app will **not** surface a mobile-originated `rcmd` as "sent" while §1 stands.

### Ask 2 — team position sharing (P3 hike mode) — ★ DECIDED 2026-07-16: TEAM-PLANE ONLY
User decision (D31, confirmed): **(b) team-scoped distribution, fully separated — visible IN-TEAM ONLY** (works
off-grid per 6.4; plane separation keeps the static mesh byte-identical/s18-inert; privacy = one toggle; airtime
stays inside the team). The mesh-wide BCN ext-TLV stays the SEPARATE Theme-C fleet-map rail (operator visibility,
opt-in, later). Sparse-team caveat accepted: team-plane propagation is member-to-member — islands connected only
via homes won't see each other's positions (fine for hike mode; note in the design).

**Position storage model (user-decided): RAM = live, FLASH = a user-set default.**
- **RAM (volatile) = the LIVE position** — phone-fed (e.g. `pos <lat> <lon>`, RAM-only, never persisted; lost on
  reboot — the phone simply re-feeds). This is the only path the periodic GPS feed uses (no flash wear).
- **NV lat/lon (the existing `/mrid` via `cfg set lat/lon`) = the DEFAULT** — set only by a dedicated user action,
  NEVER auto-updated by the feed. Effective position = RAM-live when fresh, else the NV default when set, else none.
1. The volatile feed verb (above) + its freshness window (when does a stale RAM fix fall back to the default?).
2. **Team-plane distribution** of the effective position, with a cadence/on-move knob (**opt-in**; off = never
   transmits position — the toggle is the whole privacy model).
3. **A member-position push to the app** (e.g. `{"ev":"peer_pos","hash":…,"lat_e7":…,"lon_e7":…,"age_ms":…}` or
   folded into a future known-nodes surface) — the app's directory already stores `latE7/lonE7/positionAt` per node.
4. Bounded staleness semantics (TTL / age) so the app can grey out old fixes.

## Adjacent BLE surface — implemented, not strictly "inbox" (2026-06-29)

These firmware→app events ride the same BLE TXD line, so the app's parser will see them; documented so it handles (or cleanly ignores) them. Not part of the inbox sync model.

- **Remote management: `rcmd <dst> <verb>` (BLE) — now AUTHENTICATED (2026-07 remote-admin, `src/firmware_remote.cpp`).** Two tiers: **open reads** (`status`, `routes`) ride **cleartext** and any node answers; **every other verb (reboot / prep-restart / config / `password rotate …`) is SEALED** to the target's pinned admin key and requires the operator to **`unlock <passphrase>`** first (derives the admin key into RAM; `lock` wipes it). A sealed `rcmd` to a node with no admin key pinned is silently dropped; a stale replay is rejected with a counter-hint. Responses come back as **binary-TLV** sealed blobs (`REMOTE_FLAG_SEALED`), not the old console text. Gated out entirely on the mobile profile (`MR_FEAT_REMOTE_MGMT=0`). Ack shape/line refs above are pre-cleanup — the app should treat `rcmd` as fire-and-observe and not depend on the old `[rcmd <from>]` console echo. (Design: `docs/superpowers/specs/archive/2026-07-13-remote-management-auth-design.md`.) ⚠ **The monotonic-counter replay scheme described here is being REPLACED (ratified 2026-07-26): challenge–response** — `docs/superpowers/specs/2026-07-26-remote-admin-challenge-response-design.md`. The companion becomes the primary remote-admin driver at v1; see "Planned for v1" below.
- **`{"ev":"version",…}`** (`fw`/`built`/`git`/`board`/`reset`) — the BLE `version` query (`fw_main.cpp:1457`).
- **`{"ev":"prep_restart","halted":true}`** — the BLE `prep-restart` ack (`fw_main.cpp:1463`).
- **`{"ev":"hash_resolved","node":…,"auth":…,"hash":…}`** — the `resolve <hash>` diagnostic answer (`write_push`, `console_json.cpp:148`). Distinct from `peer_key_cached` (the pubkey-cache event).
- **`{"ev":"e2e_acked","origin":<dst>,"ctr":<n>,"sender_hash":<h>}`** — the **live twin** of the durable `inbox_dm type:"e2e_ack"` receipt (`PushKind::send_e2e_acked` → `pushkind_name`/`write_push`, `console_json.cpp`; landed 2026-06-29, replaces the former `{"ev":"unknown"}` hazard). The app marks its OUTBOX message **DELIVERED immediately** (not only on the next pull): match `(origin, ctr)` — or `(sender_hash, ctr)` when `sender_hash != 0` (cross-layer ack) — to the OUTBOX, **identical to the durable `type:"e2e_ack"` rule**. **NOT** an inbound DM — do not render it. `origin` = the dest that confirmed delivery; `sender_hash` = 0 on a same-layer ack. **★ 2026-07-18 (S1 ack unification, gated uncommitted):** `sender_hash` is now **actually populated** on a cross-layer ack (it was declared but latently always 0 — the XL `(sender_hash, ctr)` match works for the first time); and on a same-layer ack to a **hosted mobile**, `origin` is now the TRUE confirming node (previously the home's id — which contradicted this very line). No app change needed; the documented matching rules simply hold now. Also NEW on `msg_recv`: **`"origin_layer":N`** (omit-when-0) on a CROSS-LAYER delivery — the sender's layer, i.e. the first half of the `(layer_path, hash)` REPLY address — **now ALSO durable (2026-07-19 batch B): `inbox_dm` records carry `"origin_layer":N` omit-when-0** (record header 31→32 B, BOTH store versions bumped ⇒ the first boot after reflash wipes the on-node inbox + bumps `inbox_epoch` again — the normal epoch re-pull). Also new: a home that cannot route a mobile's delegated send signals it via the next roster ⇒ the mobile emits `send_failed{no_route}` (the app's existing back-off surface — no new event).
- `cfg` / `status` / `route`+`routes_end` writers also stream over BLE (the Node/Network screens) — orthogonal to inbox sync; see the device-console design spec.

## Planned for v1 (ratified 2026-07-26 — designed, not yet built; companion index: `docs/superpowers/specs/2026-07-26-companion-v1-feature-roadmap.md`)

Two firmware feature arcs ship with v1 and have companion halves. These are PLANNED contract additions —
the exact JSON verbs/pushes land WITH each firmware slice (the coder + QA add the precise shapes then);
this section reserves the surface so the app team can plan.

### Team encrypted channel + team QR — `docs/superpowers/specs/2026-07-26-team-encrypted-channel-design.md`
- **Team QR** (a SECOND QR type alongside the verified-peer pubkey QR): render behind a "Share team" screen
  that warns it carries a PRIVATE key; payload = team PHY params + `team_ch_pub`/`team_ch_priv` + CRC32.
  Scanning provisions the node via an extended `team …` verb (`tkpub=/tkpriv=` hex64).

#### node → app: export the team channel keypair — ★ DEFINED (owner ruling 2026-07-29)

The import half shipped in **T-K1** (`c8c749d`): `team new` **always** mints an X25519 pair, and both
`team new` and `team <id>` accept `tkpub=<64 hex> tkpriv=<64 hex>` to **adopt** one instead (both or neither;
exactly 64 hex digits, case-insensitive, no `0x`). The **export** half is this:

```
team exportkey
  → {"ev":"team_key_export","team_id":858993459,"tkpub":"<64 hex>","tkpriv":"<64 hex>"}
  → holds no keypair:  {"ev":"team_key_err","reason":"no_key"}
  → not in a team:     {"ev":"team_key_err","reason":"no_team"}
```

- ★ **A refusal is a DISTINCT EVENT, never a success object with null fields.** This surface emits **zero** JSON
  `null` literals (measured) — every optional field is omit-when-absent — and the consumer here is a **QR
  encoder**: a null-blind encoder would write the literal `null`, or 32 zero bytes, into a team QR, and an
  all-zero scalar is exactly what the firmware **refuses** as a non-key. A distinct `ev` cannot be mistaken
  for a payload. Same idiom as the existing `mobile_err{reason}` / `peerkey_err{reason}`.
- **`no_team` is a real case, not defensive padding:** a node can hold a key while being teamless (`team new`
  then `team 0` — the team switch deliberately does not clear the key). Exporting then would yield
  `team_id: 0`, and a QR carrying `team 0` **provisions "leave"**. The firmware refuses to export it and does
  **not** clear the key.
- **Round trip is textually exact:** the hex this emits (lower-case, 64 digits, no `0x`) is byte-for-byte what
  `tkpub=`/`tkpriv=` accept, so an export on one node and an import on another need no normalisation step in
  the app. Pinned by a test.

**Lock state — the field to use for indicators:**
- `ready` gains `"team_ch_key":true|false`, **omitted when the node is not in a team** (following §S1's
  omit-when-inactive rule for the team block).
- the JSON `cfg` dump carries it **always** (matching how `cfg` treats `team_id` — an explicit dump, so the app
  gets a non-optional Bool).
- ⚠ **Never call `exportkey` merely to test for presence** — that is what this boolean is for. `ready` is
  fenced by test: it carries neither `tkpub`, `tkpriv` nor `team_key_export`, even on a keyed node.

**The `team` verb's refusal strings for bad `tkpub`/`tkpriv` — the app must match these verbatim.** ⚠ These are
**plain console text on `Print&`, not JSON**, so over BLE the app sees them as raw lines:

| trigger | exact device output |
|---|---|
| value is not exactly 64 hex digits | ⚠ **composed from three prints — match as prefix + suffix, not a fixed string:** `> team err: ` + `tkpub` \| `tkpriv` \| `tkpub/tkpriv` + ` needs EXACTLY 64 hex digits (32 bytes)` |
| only one of the pair given | `> team err: tkpub= and tkpriv= must be given TOGETHER (a keypair, not a half)` |
| tail exceeds the buffers | `> team err: args too long` |
| `team 0 tkpub=…` | ``> team err: tkpub=/tkpriv= make no sense on `team 0` (leave)`` |
| crypto validation fails | `> team err: tkpub=/tkpriv= REFUSED — not a valid X25519 keypair (all-zero, or tkpub is not tkpriv's public key). Team NOT joined.` |
| *(not input-driven)* RNG failure at mint | `> team err: team channel keygen FAILED (crypto RNG returned no entropy). Team NOT minted.` |

- **Available on every transport** (USB, BLE, companion) — owner ruling, see the risk note below.
- The pair is the **canonical RFC 7748 clamped** form as stored (T-K1), so what the QR carries is exactly what
  another node will adopt — no re-derivation, no normalisation step in the app.
- ⚠ **`ready` does NOT and MUST NOT carry it.** `ready` is unsolicited and fires on every connect; the private
  key is disclosed only in answer to this explicit verb. The `pubkey`-in-`ready` precedent above is for a
  **public** key and is deliberately not the model here.
- **Lock state** (which teams this node can read) stays the separate boolean `team_ch_key`; the app should use
  that for indicators and never call `exportkey` just to test for presence.

> ### ⚠ ACCEPTED RISK — recorded, owner-ruled 2026-07-29
> Any peer that can reach the console can read the team's content key and thereafter decrypt every team post,
> **silently and with no on-node trace.** The alternative options (disclose-once at mint; USB-only) were put to
> the owner with this stated and **"any transport" was chosen deliberately.**
> ★ **CONSEQUENCE: the standing watch-item "the BLE fallback exposes the full console" (command-sink
> consolidation, gated 2026-07-13) is no longer a watch-item — it is now the ONLY control protecting the team
> content key.** It should be treated as a dependency of this feature, not as unrelated cleanup. Closing it
> (pairing / an auth gate / a console allow-list on BLE) makes "any transport" safe; leaving it open means the
> team channel's confidentiality rests on nobody being in BLE range.
> **App-side obligations that follow:** store the pair in the **Keychain**, never in plists/logs/analytics; keep
> the "Share team" screen's PRIVATE-key warning; and do not cache the pair beyond what the share flow needs.
- **Key grant** (in-app, vetted): a keyholder grants a newjoiner the team content key via a sealed
  `TEAM_KEY_GRANT` DM (DATA_TYPE 19, body `[team_id][name][pub][priv]`); joiner surfaces `team_key_received`.
- **Encrypted posts**: `channel_recv`/`inbox_channel` will emit `enc:true` on an opened encrypted team post
  (the reserved field, above, goes live). An un-keyed overlay member relays but cannot read → the
  `team_channel_no_key` push prompts "ask a teammate for the key".
- **Lock-state per team** indicator; **member location on a map** (encrypted location inner-type, app-driven
  cadence).

#### The team key grant — `team grantkey` — ★ LIVE (T-K3, 2026-07-29)

Ships the team **content key** to a teammate over a **sealed DM**. Use this for the **remote** teammate — already
in the overlay, not present to scan the team QR. (Present? Use the QR: it needs no pubkey and no radio.)

```
team grantkey <0xhash | team-id> [name="<text>"] [-t]
```
`0x` + **1..8** hex digits (case-insensitive, non-zero) = the target's `key_hash32`. A bare decimal **1..254** = a
teammate's `team_local_id`, resolved via the beacon-only team-key cache — **implies `-t`**. `name=` is optional,
quotable, **max 32 chars** (33 refuses). `-t` forces the team plane. Keys are last-wins; any other key is `bad_key`.

**Success — a distinct event, never `team_key_err` with a happy reason:**
```json
{"ev":"team_key_grant","hash":3735928559,"ctr":1234,"parked":false}
{"ev":"team_key_grant","hash":1,"ctr":0,"parked":true}
```
`hash` decimal u32 (as `team_key_export`); `ctr` = the DM ctr, **0 = parked** behind a hash resolve; `parked` is
**explicit** so the app never infers state from a sentinel. Carries no key material and no name echo.

**Refusals reuse `team_key_err`** (U1) — `{"ev":"team_key_err","reason":"<r>"}`, `<r>` ∈ `no_team` · `no_key` ·
`no_identity` · `no_pubkey` · `self` · `delegated` · `too_large` · `bad_target`. No JSON `null` on this surface.
★ **`no_pubkey` is the one the app will see most**, and it is **not** auto-resolved — see the ruling above. Surface
`reqpubkey <0xhash>` (TOFU) *and* the **verified-peer** QR, and let the user choose.
★ **`delegated` is permanent, not transient:** a grant cannot ride the sealed-relay wrapper (it has one enclosed-type
byte, already spent), so a delegated grant would arrive sealed but **type-stripped** — the private key would land as
inbox *text*. Offer "grant over the team plane (`-t`)" or "from a node on the target's own layer". The same shape
arrives asynchronously as `{"ev":"send_failed",…,"reason":"unsealable"}` — **permanent for that route.**

**Receiver push — the key is ALREADY ADOPTED when this fires; it is a notification, not a request:**
```json
{"ev":"team_key_received","team":"cccc0001","hash":2712847316,"origin":213,"name":"Alpha Team"}
{"ev":"team_key_received","team":"00000011","hash":7,"origin":1}
```
`team` = hex8 (as `team_reg`); `hash` = the **granter's** `key_hash32` (the sealed-sender identity — same field name
as `peer_key_cached`); `origin` = the granter's node id on the receiving plane (diagnostic); `name` =
**omit-when-absent** and **not persisted on the node**, so the app must keep it if it wants the label.
⚠ It never carries the pair. `team exportkey` stays the single disclosure verb and `team_ch_key` the indicator.

**Wire:** `DATA_TYPE_TEAM_KEY_GRANT = 19`, body `[team_id u32 LE][name_len u8][team_name ≤32][tkpriv 32]` = 37..69 B,
**always CRYPTED** — the TYPE byte is cleartext, the body sealed. **No `tkpub` on the wire** (the receiver derives it,
so a mismatch is impossible). A grant that arrives **unsealed** is dropped loud, and a grant is **consumed on every
outcome** — it is never inbox'd and never surfaces as `msg_recv`.

#### Delegated cross-layer DMs now deliver — ★ BEHAVIOUR CHANGE (2026-07-30, `§deleg-ack-xl`)

A **home re-originating for a hosted mobile** toward a target on another layer used to stamp `SOURCE_HASH = mobile`
without recording the `ctr_H → ctr_M` translation. Three sites were affected. **What the app now sees differently
— all only for delegated sends:**

1. ★ **A delegated SEALED cross-layer DM now ARRIVES.** It was previously dropped at the recipient with **no push,
   no ack, and no trace on metal** (the receiver-side emit is telemetry, which is device-stripped).
2. **`send_e2e_acked` now carries the MOBILE's own ctr** for delegated sends — a push that previously never
   matched the app's outstanding message now matches.
3. **The spurious `send_failed{reason:"e2e_ack_timeout"}` on the MOBILE no longer fires** after the 300 s
   cross-layer budget for those sends.
4. **The HOME no longer arms or fires its own `e2e_ack_timeout`** for a DM it is merely relaying.

⇒ **If the app has a workaround for unmatched delegated acks or phantom timeouts, retire it.**

**New refusal:** a delegated cross-layer send that also requests sealing under our own identity is refused with the
existing `send_failed{reason:"unsealable"}` (no new reason value) — it would produce a frame that can never open.

#### `team <target>` now requires an unambiguous id — ★ GRAMMAR CHANGE (2026-07-30)

`team <garbage>` used to **silently leave the team**: `strtoul` consumes zero digits from a non-numeric tail and
returns 0, and `team 0` means leave. So `team exportky`, `team nwe` — and `team 0x`, `team 08`, `team 0abc`, which
*do* begin with a digit — all left. `team -1` silently **joined** team `0xFFFFFFFF`.

Now: **the ENTIRE target token must parse as one number** — one unconditional clause (tightened
2026-07-31; the earlier two-clause rule is superseded). `0`, `0x88A672BA`, `12345` ✓ · `0x`, `08`, `0abc`,
**`88A672BA`** (a hex id missing its `0x` — it used to join *team 88*) and `12abc` ✗. **`team 0` = leave is
unchanged.** The refusal states that nothing changed and now also names the likely mistake: *"a HEX id needs
its `0x`"*. ⚠ **Still open (register B17): an out-of-range target behaves differently on 32-bit boards than
in the 64-bit simulator.**

### Remote admin — challenge–response — `docs/superpowers/specs/2026-07-26-remote-admin-challenge-response-design.md`
- The companion becomes the PRIMARY remote-admin driver (v1). The monotonic counter/`floor=N` hint is
  REPLACED by a node-issued challenge.
- **Invisible challenge lifecycle**: the app caches the per-node challenge from sealed responses,
  auto-bootstraps on first contact (a `challenge==0` command), and auto-retries once on a resync response —
  the operator never sees a challenge. Sealed-response body gains a leading `[challenge 8]`.
- **Open reads** (`status`, `routes`) stay cleartext, need no unlock, carry NO challenge; **sealed verbs**
  (reboot/config/`password rotate`) require `unlock <pw>` + carry the challenge.
- New telemetry: `admin_challenge_resync` (rate-limited) — the app resyncs silently on it.
- (The open-read challenge piggyback — spec Q3 — is DEFERRED; the app relies on the bootstrap path.)

## OLED preset catalog — `ui preset` verbs + three NDJSON records (§UI-10/11 P2, 2026-08-25) · ★ PURELY ADDITIVE

The hard-coded OLED compose strings are now **defaults, not firmware policy**. The wearer's phrases live in a
separate versioned UI record (`/mrui`, magic `'MRU1'`) with **seventeen fixed stable slots**: one mandatory
`emergency`, eight `dm`, eight `channel`. ⚠ **The app must add these events before it exposes an editor.**

⛔ **There is no PUSH here.** This surface is request/response only: the app sends a verb, the node answers with
records. Nothing arrives unsolicited.

### app → node: the grammar (USB serial and BLE, identical bytes — one dispatcher, one emitter)

```
ui preset list
ui preset set <emergency|dm1..dm8|channel1..channel8> loc=<on|off> "<text>"
ui preset clear <dm1..dm8|channel1..channel8>
ui preset reset <emergency|dm1..dm8|channel1..channel8|all>
```

- The slot token IS the stable identity. ⛔ **Never derive `dmN` from a list position** — gaps are valid, and
  `dm1`, `dm4`, `dm8` may legitimately be the three visible rows on the panel.
- `<text>`: **1..17 printable ASCII bytes** (0x20..0x7e), at least one non-space; `"`, `\`, CR and LF are
  rejected. 17 is a UI safety bound: the compose row always shows a selection marker AND a location marker, so
  both location states consume 2 of the panel's 19 columns. ⛔ **Over-long text is REFUSED, never truncated** —
  the device must never send a suffix the wearer could not inspect.
- `loc=` takes **exactly** `on` or `off`. `loc=1`, `loc=ON`, `loc=maybe`, `loc=` and an absent term all refuse.
- A trailing token is **refused, not ignored** (`ui preset list all` is an error, not a `list`).
- An unrecognised sub-verb or an incomplete line gets the **human usage line** (`> ui err usage: …`), not a
  reason code. The reason codes below are reserved for the six ruled failures.

### node → app: the three records — one JSON object per line

```json
{"ev":"ui_preset","slot":"dm1","enabled":true,"text":"Are you OK?","location":false}
{"ev":"ui_presets_end","capacity":17,"dm_active":2,"channel_active":2,"generation":7}
{"ev":"ui_preset_err","reason":"bad_slot|bad_text|bad_location|mandatory|busy|store"}
```

- `list` emits **all seventeen `ui_preset` records in stable slot order, INCLUDING the disabled ones**, then
  `ui_presets_end`. A disabled slot renders `"enabled":false,"text":"","location":false`. ★ This is what lets the
  app address `dm5` to turn it ON; an enabled-only list could not.
- **Mutating verbs answer with the RESULTING record** for the slot they changed — ⛔ not a dump. `reset all`
  answers with the **full list** (17 + end), because it changed every slot and has no single one to name.
- `unchanged` looks identical to a change: the same record comes back. The verb succeeded; it simply cost no
  flash (byte-identical writes are coalesced). ⛔ Do not re-issue chasing a different reply.
- `capacity` is the record's own constant. Raising it is an explicit catalog-format revision.
- `generation` is a persisted **non-zero** uint32, incremented only on a successful durable update. **Compare it
  for EQUALITY, never ordering** — wrap skips zero and is therefore harmless. It is the token the panel seals
  into a pending send, so a change between the wearer's press and its execution is refused on the device.

### the six reasons, and what the app should do

| reason | meaning | app action |
|---|---|---|
| `bad_slot` | the slot token is outside `emergency` / `dm1..dm8` / `channel1..channel8` | fix the token; permanent |
| `bad_text` | absent/empty/all-space, >17 bytes, or a forbidden byte (`"` `\` CR LF, non-printable) | fix the text; permanent |
| `bad_location` | the `loc=` term is missing or is not exactly `on`/`off` | fix the term; permanent |
| `mandatory` | `clear emergency` — the emergency slot can never be disabled, cleared or emptied | permanent; offer `ui preset set emergency …` (editing its TEXT is allowed) or `ui preset reset emergency` |
| `busy` | **an emergency alarm is ACTIVE** | ★ TRANSIENT. Retry after the alarm ends. ⛔ Do not present it as a failure |
| `store` | `/mrui` is unreadable, **or** the one save attempt failed | show the node's own boot/`cfg` diagnostic; a device-level fault |

★ **`busy` covers EVERY mutating verb, including a no-op** — an alarm's retry series must not have its body or
its location policy changed halfway through. `list` is **not** a mutating verb and answers normally during an
alarm. ⛔ `busy` outranks `mandatory`: `clear emergency` during an alarm answers `busy`.

⚠ **`store` deliberately covers BOTH** an unreadable record and a failed write, because the design's reason set
is exactly six. The distinction the operator needs is carried at boot instead (see below). ⚠ **A save that
reports failure may have written PARTIALLY** — no reason value here may be read as "no flash was changed".

### location semantics per slot kind (what `location:true` will actually do)

| slot kind | `location:true` |
|---|---|
| `emergency` | with a fix: `-l` is added. **Without a fix: the alarm still sends, WITHOUT `-l`** — an alarm outranks its coordinates |
| `channel`   | `-l` is added. No fix/key/seal ⇒ a **loud refusal**; ⛔ `-l` is never silently stripped |
| `dm`        | `-l` is added to a **sealed** DM only — needs `e2e_dm=1`, a usable key and a fix. ⛔ No downgrade to make a preset send |

`location:false` emits the same command without `-l`. The core remains the final privacy gate: a
location-bearing message must be sealed.

### boot / status diagnostics (console text, NOT app events)

The node prints nothing at boot when `/mrui` is valid **or absent** — an ordinary first boot is not a fault. The
two fault states print exactly:

```
  ui presets = DEFAULTS (record invalid — repaired on next successful change)
  ui presets = DEFAULTS (store unreadable — changes disabled)
```

An `invalid` record repairs itself on the next successful mutation (the whole canonical catalog is rewritten);
an unreadable store refuses every mutation with `store` and **zero writes** until the device is dealt with.
`cfg` additionally carries `  presets: generation=<n> dm_active=<n> channel_active=<n> saves=<n>`.

### not in scope of this surface

⛔ Editing a phrase never touches `/mrcfg` — no radio, identity, team or key configuration can be reprovisioned
by this family. ⛔ There is no verb to read or set the catalog's generation directly; it moves only as a
consequence of a successful durable mutation.
