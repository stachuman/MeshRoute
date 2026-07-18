# Cross-layer mobile DMs + first-contact key exchange — design spec (FOR REVIEW)

*2026-07-17. Authored for user review, then the coder. Grounded against HEAD `d5a9759` + the gated
team-digest fix; every present-state claim carries a file:line verified today. Companion scenario:
`simulation/s27_cross_layer_mobiles_meshroute.json` (written against THIS spec's target protocol —
**expected RED until the slices land**, then it joins `BASELINE.md` as mandatory).*

## 0. Aim + the airtime principle

Make a mobile able to reach another mobile on a **different layer** given the address `(layer, hash)`,
plaintext and encrypted, with first-contact key exchange that needs **no flood** — and keep every
mechanism on the cheapest airtime shape available:

1. **Discovery never crosses the gateway.** Anything inter-layer rides resolved, routed unicast.
   (Same invariant as the 2026-07-17 team-digest gate.)
2. **Keys travel at most once, attached to frames that are being sent anyway** where possible.
3. **Floods are per-layer, last-resort, and cached away** after first success.

## 1. Present state (grounded)

| Mechanism | State | Where |
|---|---|---|
| Static node `send_layer 0x<hash> <l…>` cross-layer | ✅ works end-to-end | `originate_layer_path` (node.h:542); gateway parks + far-leaf re-resolve incl. `mobile_home_find` + H-flood fallback confined to the target leaf (node_mac_rx.cpp:1079-1102) |
| Gateway-by-layer knowledge at every static | ✅ | `_bridged_layers` from the type-4 beacon TLV (node_beacon.cpp:612-618) |
| Mobile delegated send | ✅ same-layer only | `DATA_TYPE_MOBILE_SEND`(14) carries DST_HASH, **no layer** (node_hashlocate.cpp:871) |
| Home answers WANT_PUBKEY for its mobile | ✅ iff push landed | Fix 7 (node_hashlocate.cpp:590-593); the push `DATA_TYPE_MOBILE_PUBKEY_PUSH`(12) is **fire-once per adopt, unacknowledged** — metal shows `pubkey=no` hosts |
| Recipient-side decrypt | ❌ gap | `e2e_open_trial` = trial ECDH over **cached sender keys** (node_hashlocate.cpp:458); a registered mobile skips all static-plane H processing (`:533`) so it never caches a requester key; the home's proxy branch drops it too |
| Encrypted cross-layer | ❌ refused v1 | seal (`:367`) + open (`:416`) refuse `DATA_FLAG_CROSS_LAYER`. **Verified NOT a crypto binding problem:** AAD = dst_hash only (`:390`), nonce = seed‖ctr‖dst_hash (`:389`) — nothing the bridge rewrites is bound. The blocker is the sealed layer-local `origin` byte + ack addressing |
| Cross-layer key request | ❌ | WANT_PUBKEY is an H flood; doesn't (and shouldn't) cross |

## 2. The address model — consistent with the EXISTING cross-layer format (user correction 2026-07-17)

The protocol already addresses cross-layer as a **layer PATH**, not a single target: the console verb
is `send_layer 0x<hash> <l1,l2,…> "<text>" [-a]` (`Command.layer_path`, command.h:70-77) and the
CROSS_LAYER DM inner carries the **full preserved path** `[n_layers:1 | cur:1 | layer_ids n×1B]`
between dst_hash and origin (frame_codec.h:519-522, bounded by `gw_env_max_hops`), cursor-walked by
each bridging gateway. **This spec adds NO second addressing shape.** The app-level address of a
cross-layer contact is **`(layer_path, key_hash32)`** — for the one-gateway scenario the path is just
`[T]`; multi-gateway topologies use the same list. Path absent = today's same-layer/resolve behavior,
byte-identical. QR cards may later carry the path hint (`l=` beside `h=`) — companion contract, out
of scope here.

## 1b. ★ Compatibility cross-check (2026-07-17, code-verified, comments distrusted) — the change-site map

**Verified TRUE (the spec builds on these; do not re-derive):**
- The unicast-inner codec is single-sourced: `parse_unicast_inner`/`pack_unicast_inner` (frame_codec.cpp:890-975), path `[n_layers|cur|ids…]` ≤ `gw_env_max_hops=4` (protocol_constants.h:320), path immutable / cursor-only.
- **The 4e reversed-path XL E2E-ack EXISTS in code** (node_mac.cpp:364 implementation; :251 honors E2E_ACK_REQ; bridges pass the ack through node_mac_rx.cpp:722; ack-inner parse :824). SOURCE_HASH is REQUIRED for it (:237) — aligns with S4's hash-keyed rule.
- `do_send` already threads `type` + `override_source_hash` (node_mac.cpp:186) — the delegation plumbing largely exists.
- **Unwrap ordering is safe for a CROSS_LAYER-flagged wrapper:** `DATA_TYPE_MOBILE_SEND` is consumed FIRST in `do_post_ack` (node_mac_rx.cpp:684), before the last-mile (:699), bridge, and deliver forks — even on a gateway-home; and mobile→home is 1-hop by construction (no relay ever forwards a wrapper).
- TYPE wire encoding: the APP bit is DERIVED from `type != 0` and the TYPE byte rides at offset 8 (frame_codec.cpp:788-790) — INTRO(15) needs **no new flag bit**.
- Home-side gateway pick = `select_gateway_for_leaf` two-pass (node_mac.cpp:196-233).

**INCOMPATIBILITIES FOUND (each now addressed in its slice):**
1. **Path semantics — user hops EXCLUDE the sender's own layer.** `send_layer` PREPENDS the sender's layer to the supplied hops and fail-loud-validates (node.cpp:971-974). The wrapper must carry the **user hops verbatim**; the home prepends ITS layer (== the mobile's) and re-validates — identical semantics, no translation.
2. **S4 has FOUR refusal sites, not two:** seal (node_hashlocate.cpp:367) · open (:416) · the `send_layer`+`e2e_dm`-ON command refusal (node.cpp:967, R1 no-silent-downgrade) · the `enqueue_cross_layer` cleartext-only defense (node_mac.cpp:239-241, R1 defense-in-depth). All four lift together, each replaced by the SOURCE_HASH-required rule.
3. **★ Delegated CRYPTED is structurally broken TODAY (worse than the key gap):** the mobile branch passes `crypt` into the wrapper send (node_hashlocate.cpp:871) → `enqueue_data` **seals the WRAPPER itself** to the target's key (node_mac.cpp:119-127) → at the home `parse_unicast_inner` cannot yield a cleartext `source_hash` (it's sealed), the :684 gate fails, the frame falls through to normal delivery, trial-decrypt fails (sealed to the TARGET, not the home) → `e2e_open_no_key` **silent drop at the home**. (The frame_codec.h:450 "enclosed PLAINTEXT" comment describes intent, not an enforcement — nothing refuses the crypt.) S4 restructures to **seal-inner-then-wrap-plaintext**: the mobile seals the body to the target (pre-sealed blob), wraps it under a PLAINTEXT MOBILE_SEND envelope (cleartext dst_hash/source_hash/path + an enclosed-CRYPTED marker), and the home re-originates with CRYPTED set **without re-sealing**. Change sites: node_hashlocate.cpp:866-871 (wrap), node_mac.cpp:119 (skip the wrapper seal for type==MOBILE_SEND), node_mac_rx.cpp:684-694 (re-originate preserving CRYPTED).
4. **The home re-origination DROPS the enclosed TYPE and most flags:** node_mac_rx.cpp:690-691 re-originates with only `E2E_ACK_REQ|PRIORITY`, `CryptIntent::off`, and NO type — a delegated INTRO would lose TYPE 15 (and LOCATION is stripped). Fix: thread `pa.type` + the relevant flag set through (the `do_send` params exist).
5. **S1's real change site is the `send_layer` command case, not `send_by_hash`:** CmdKind::send_layer (node.cpp:960-990) never reaches the delegated branch — add the registered-mobile fork THERE (before explicit-path origination), mirroring node_hashlocate.cpp:870-871.
6. **INTRO on an un-upgraded node renders the pubkey bytes as message text:** the type dispatch is an if-chain (node_mac_rx.cpp:684-815) and unmatched types fall through to normal DM delivery. Covered by the fleet-reflashes-together policy (`wire_version` unchanged, per house rule) — but it's a visible artifact, not a silent one; note for the rollout.
7. **reqpubkey-XL as spec'd was under-designed → DEFERRED (v2).** Today `reqpubkey` only floods (H); a routed XL variant needs want_pubkey threaded through the gateway's far-leaf resolve (the :1102 `emit_hash_query` is plain) — a whole extra mechanism. INTRO (S2) already covers first contact, including encrypted-first: send the plaintext INTRO round first (the D1 flow). Dropping reqpubkey-XL removes a mechanism and its airtime; revisit only if a real "must encrypt the very first frame cross-layer" need appears.

## 3. Slices

### S1 — delegate `send_layer` from a registered mobile (unlocks plaintext M→XL→M; NO new verb, NO new format)
- **Console surface unchanged:** the existing `send_layer 0x<hash> <l1,l2,…> "<text>" [-a]` becomes
  legal on a registered mobile (today it would originate on the static plane with a local-id origin —
  the RREQ-storm class). The mobile wraps it exactly like `send`: `DATA_TYPE_MOBILE_SEND` to the home.
- **The wrapper carries the EXISTING wire path block, verbatim:** the MOBILE_SEND inner is extended
  with the same `[n_layers:1 | cur:1 | layer_ids n×1B]` layout the CROSS_LAYER inner uses
  (frame_codec.h:519 — reuse the pack/parse helpers, do NOT re-encode). Appended **conditionally**:
  absent ⇒ today's same-layer wrapper, byte-identical. Bounded by `gw_env_max_hops` as everywhere.
- **Change sites (verified §1b):** the registered-mobile delegation fork goes in the
  `CmdKind::send_layer` case (node.cpp:960-990, BEFORE explicit-path origination), mirroring the
  `send` fork at node_hashlocate.cpp:870-871; the wrapper carries the **user hops verbatim** (own
  layer NOT prepended — the home prepends its own layer + re-runs the node.cpp:971-974 fail-loud
  validation, identical semantics); the home-side unwrap fork extends node_mac_rx.cpp:684-694
  (`ui->has_cross_layer` ⇒ `originate_layer_path`, threading `pa.type` + the flag set per §1b-4);
  no bridging gateway for hop 1 ⇒ `send_failed{no_route}` back to the mobile (fail loud).
- Far side is **already built** (gateway cursor-walk + far-leaf resolve, worst case ONE H flood
  confined to the target layer, cached after). The E2E-ack return rides the frame's own path
  reversed (the path is PRESERVED on the wire — same as static XL; verify the reverse-walk at
  implementation, frame_codec.cpp not comments). Airtime: +2+n B on the one-hop delegation leg;
  zero new floods.

### S2 — `DATA_TYPE_INTRO`(15): first-DM pubkey attach (the flood-killer)
**Answer to "is there an existing APP type?": no.** TYPE 4 is reserved for a different shape
(overheard/soft pubkey *answer*), 5/8/13 are locate answers consumed by the hash-locate machinery,
12 is mobile→home only. A first-contact DM needs its own type:
- **INTRO = a normal plaintext app DM whose inner prefixes the sender's key**:
  `[ed_pub 32][name_len u8][name ≤32][body…]`. Requires `SOURCE_HASH` present;
  receiver verifies `ed_pub[:4] == SOURCE_HASH` (the peerkey self-consistency rule) →
  `peer_key_set(authoritative)` + name cache (fires the existing `peer_key_cached` push/emit) →
  then delivers `body` exactly as a normal DM (inbox record, `msg_recv` push — type field 0 on the
  app surface; the INTRO framing is transport detail). Change sites: the type-dispatch chain in
  `do_post_ack` (node_mac_rx.cpp:684-815 — INTRO consumes the key prefix then falls into the normal
  deliver) + the attach fork in `enqueue_data`/`send_by_hash`; **for a DELEGATED INTRO the home must
  thread `pa.type` through the re-origination (§1b-4 — today node_mac_rx.cpp:690-691 drops it).**
- **Attach rule (D1, recommended):** automatic — a plaintext hash-addressed send rides as INTRO
  **iff we have not yet received a SEALED frame from `dst_hash`** (a `peer_confirmed` bit on the
  peer-key entry, set on the first successful `e2e_open` from that peer). Converges in one round
  trip: M1's first DM attaches (M3 gets M1's key); M3's reply attaches (no sealed-from-M1 yet →
  M1 gets M3's key); both seal thereafter; sealed traffic sets the bits and attaching stops.
  Worst case a handful of +33 B attaches vs. a ~77 B WANT_PUBKEY **flood × every relay in the layer**
  per key — the single biggest airtime win in this spec. Override: `-k` forces attach, and the
  attach applies only to hash-addressed sends (id-addressed sends have no key context).
- `reqpubkey` stays for the no-traffic bootstrap case (and gains `-L` in S4).

### S3 — hardening: the home as key custodian
1. **Push-until-confirmed:** `MOBILE_PUBKEY_PUSH` is re-sent with **`DATA_FLAG_E2E_ACK_REQ`**
   (it's a 1-hop DM — the existing ack machinery IS the confirmation; no new frame type). Unacked →
   re-push on the next 10-min re-CLAIM tick, until acked; re-armed on every (re-)adopt. Bounded:
   ~40 B/10 min, 1 hop, stops on ack. Fixes the metal `pubkey=no` finding. No NV persistence of
   hosted keys (a home reboot recovers within one re-CLAIM cycle; flash wear not worth it).
2. **Home forwards the requester's key to its mobile:** in the WANT_PUBKEY proxy-answer branch
   (node_hashlocate.cpp:590), the home additionally `peer_key_set`s the requester's appended key
   and forwards it to the hosted mobile as **`DATA_TYPE_MOBILE_KEY_FORWARD`(16)**, a 1-hop
   last-mile DM `[requester_ed_pub 32][name_len][name]` (D3: eager, one ~70 B local frame per NEW
   contact — piggybacking on the next last-mile DM saves nothing measurable and adds state).
   The mobile caches it (self-consistency-checked) → **closes the recipient-side decrypt gap** for
   the reqpubkey path, same- and cross-layer.
3. **Mobile-side overhear cache (TX-free):** at the mobile's static-H early-return
   (node_hashlocate.cpp:533), when the overheard H is a WANT_PUBKEY **for its own hash**, cache the
   appended requester key before returning (no answer, no relay, no TX). Redundant with (2) when
   the home is in range — kept because it costs nothing and covers the home-momentarily-deaf case.

### S4 — encrypted cross-layer (lift the v1 refusal — FOUR sites + the wrapper-seal restructure)
- **Lift all four refusal sites together** (§1b-2): seal node_hashlocate.cpp:367 · open :416 ·
  the command-level `e2e_dm`-ON refusal node.cpp:967 · the `enqueue_cross_layer` cleartext defense
  node_mac.cpp:239-241. AAD/nonce need **no change** (verified: AAD = dst_hash only, :390; nonce =
  seed‖ctr‖dst_hash, :389 — nothing the bridge rewrites is bound). Replacement rule at every site:
  XL CRYPTED **requires `SOURCE_HASH` in the sealed plaintext** (fail-loud otherwise); the sealed
  layer-local `origin` byte is ignored on an XL open; key delivery + E2E-ack ride the hash.
- **The delegated-CRYPTED restructure (§1b-3, fixes a TODAY-broken path):** seal-inner-then-wrap —
  the mobile seals to the target, wraps the sealed blob under a PLAINTEXT MOBILE_SEND envelope with
  an enclosed-CRYPTED marker; the home re-originates with CRYPTED set, never re-sealing. Change
  sites: node_hashlocate.cpp:866-871 · node_mac.cpp:119 · node_mac_rx.cpp:684-694.
- The E2E-ack reverse ride: **already built** — the 4e reversed-path XL ack (node_mac.cpp:364,
  bridges pass it through node_mac_rx.cpp:722) with SOURCE_HASH required (:237). S4 only extends it
  to sealed frames; for a delegated origin the home's ctr_H→ctr_M map (deleg_ack) already returns
  the ack to the mobile.
- ~~`reqpubkey` with a layer path~~ **DEFERRED to v2** (§1b-7): the routed-XL key request needs
  want_pubkey threaded through the gateway far-leaf resolve (node_mac_rx.cpp:1102 emits a plain H) —
  a whole extra mechanism that INTRO makes unnecessary for first contact (encrypted-first = do the
  plaintext INTRO round first, the D1 flow).

### S6 — the presence plane: P-probe / P-roster (replaces the periodic sync; the home-duty fix)

**The problem, metal-measured (home 43, 2026-07-17/18):** each hosted mobile polls
`MOBILE_LAYER_QUERY` every 600 s (node_mobile.cpp:198-208, no change detection) at FOUR
RTS/CTS/DATA/ACK exchanges per poll — 3 mobiles = **60% of the home's duty budget**, polling data
that is STATIC gateway config (node_mac_rx.cpp:768-789 builds the answer purely from
`_cfg.layers[]`). These are also exactly the frames the Loop-B budget-NACKs refused. AND the
presence machinery this traffic incidentally provides is SLOW anyway: mobile-side home-loss =
`max(90 s, 2×beacon_ms)` (node_mobile.cpp:38) = **30 min** at the fleet's `beacon_ms=900000`;
home-side mobile-loss = `mobile_liveness_ms` = **25 min** of black-hole proxying
(protocol_constants.h:408). A pure epoch-gated directory pull (this slice's earlier draft) fixes the
airtime but NOT presence — a moved-away mobile stays undetected on both sides. So presence becomes a
first-class mechanism:

**Two dedicated LOCAL frames — one cmd nibble, direction bit; LBT-only broadcasts; no CTS/ACK/relay/
flood, strictly 1-hop, TTL-free.** ⚠ Both are **LEAF-FREE** (user decision 2026-07-18): a searching
mobile attaches to the STRONGEST home regardless of layer (a preferred-layer list is a later option),
and on a shared PHY (the fleet runs layers 20+23 on one frequency) one probe canvasses every
co-located network. Byte 0's low nibble is therefore NOT a leaf gate — the rx dispatch must route
the P nibble BEFORE the standard byte-0 leaf filter (change site: the cmd-nibble dispatch in
node_mac_rx.cpp; non-hosting statics drop on frame type, cheap). The frame name avoids "S"/"M"
(M = the channel frame, 0xA); nibble chosen from the free set at implementation (D7 — verify the
actual map in frame_codec.h:9, J's nibble included; comments distrusted).

**P-probe (mobile → broadcast). Flag-gated composition — one frame, three payload blocks:**
| field | size | presence |
|---|---|---|
| cmd \| dir + flags | 1 | always — dir=probe; `lost`(searching) vs `check`(registered); HAS_LAST_HOME; HAS_PUBKEY |
| mobile key_hash32 | 4 | always (the identity — meaningless without it) |
| reg_epoch | 1 | always (the home detects a stale registration) |
| last_home_id + last_home_layer (full 8-bit) | 2 | iff HAS_LAST_HOME — feeds the new-home→old-home notify; the layer because the old home may be CROSS-LAYER |
| ed_pub | 32 | iff HAS_PUBKEY — attached while the current home hasn't confirmed key custody |
Steady-state `check` probe = **6 B**; a `lost`/registering probe with key = 40 B (rare).
**The HAS_PUBKEY block RETIRES `DATA_TYPE_MOBILE_PUBKEY_PUSH`(12)** — the key rides registration
itself, killing the push race (the metal `pubkey=no` custody hole) at the root; the roster's
per-mobile `has_key` bit is the delivery confirmation (S3's push-until-acked hardening shrinks to
"re-attach until the roster confirms").

**P-roster (home → broadcast; answers probes, coalesced ~1-2 s; also fired on any registry change):**
| field | size | notes |
|---|---|---|
| cmd \| dir + flags | 1 | dir=roster |
| home_id + home_layer (full 8-bit) | 2 | ⚠ leaf-free ⇒ `home_id` ALONE is ambiguous across co-located layers (43 exists on both) — the layer disambiguates AS DATA, and doubles as the layer a `lost` listener would adopt |
| dir_epoch | 1 | the layer-directory version (the earlier draft survives as a passenger: mobiles pull the directory ONLY when this byte changes — presence + directory stay one mechanism) |
| count | 1 | ≤ `cap_host_mobiles`=16 |
| per mobile: key_hash32 + local_id + reg_epoch + bits | 6 + | bits packed per-mobile: 2-bit link quality (home's SNR-EWMA tier — a mobile seeing itself WEAK pre-scans and re-homes BEFORE loss) + `has_key` |
3 mobiles ≈ **24 B**; 16 ≈ 105 B. A non-mobile hearing it: ignored by frame type. A SCANNING mobile
hearing any roster: passive host discovery (strongest-roster attach without probing).

**Semantics that close today's gaps:** hash present + epoch match ⇒ liveness refreshed both
directions (one probe from ANY mobile refreshes ALL via the shared roster; mobiles randomize probe
timing and SUPPRESS when another's probe/roster was just heard — the dv:11831 stand-down pattern).
Hash ABSENT ⇒ the home dropped me (reboot/eviction) ⇒ re-register NOW (today: silent until the
10-min re-CLAIM). Epoch mismatch ⇒ re-register. `dir_epoch` change ⇒ jittered directory pull.
Probe unanswered ×k ⇒ home LOST in ~2×probe-period (**minutes, decoupled from beacon_ms; was 30**)
⇒ the EXISTING DISCOVER/OFFER/CLAIM runs (registration + its concurrent-register protections stay
on the J plane — D8), with `j_discover` extended +3 B (last_home_id/layer/epoch) so the **NEW home
originates the old-home notify** — reusing `MOBILE_BREADCRUMB`(9) semantics home-sent (D10; robust
when the mobile sleeps right after adopting; cross-layer old home via last_home_layer + S1's path).
The old home's redirect machinery (node_hashlocate.cpp:560-575) is UNCHANGED.

**What it replaces / keeps:** replaces the 10-min per-mobile re-CLAIM keepalive AND the 10-min
layer-query poll (steady state ⇒ ~1 probe + 1 roster per check period per home ≈ 30 B unacked vs 12
relay exchanges/10 min); `mobile query <gw>` (manual) + `mobile_autoregister=0` app-driven mode
untouched; beacons keep their planes (static routing, team, DAD defense; the any-frame-from-home
liveness refresh stays). s18-inert by ABSENCE: no mobile, no P frames; the leaf-free dispatch is
type-gated so statics are untouched.
**Not authenticated (v1, same posture as beacons/DISCOVER):** a spoofed probe wastes one coalesced
roster (rate-limit rosters); a spoofed roster can delay/trigger one re-register cycle (bounded — a
re-register is CLAIM-protected); hashes/pubkeys broadcast here are already public on this mesh.
Change sites: new pack/parse pair in frame_codec.{h,cpp} · the pre-leaf-filter dispatch
(node_mac_rx.cpp cmd-nibble switch) · mobile FSM (probe timer + roster ingest + the DELETED
layer-query re-arm, node_mobile.cpp:198-208) · home roster emit + probe ingest (registry refresh) ·
`j_discover` +3 B (frame_codec.cpp pack/parse_j + node_join.cpp:305) · retire TYPE-12 push
(node_mobile.cpp push site + node_mac_rx.cpp:751 handler) · gateway `dir_epoch` derivation
(+ the type-4 TLV byte, node_beacon.cpp:349-352/:612-618) so homes learn it to re-advertise.

#### S6.1 — exact wire design (byte-level; LE where multi-byte, per house rule)

**P-probe (mobile → 1-hop broadcast, leaf-free):**
| off | field | notes |
|---|---|---|
| 0 | `cmd(7..4) \| dir(3)=0 \| LOST(2) \| HAS_LAST_HOME(1) \| HAS_PUBKEY(0)` | the low nibble is FLAGS, not leaf (leaf-free); dir=0 ⇒ probe |
| 1..4 | mobile `key_hash32` (LE) | always |
| 5 | `reg_epoch` (low byte of the u16 — the breadcrumb precedent) | always |
| 6 | `last_home_id` | iff HAS_LAST_HOME |
| 7 | `last_home_layer` (FULL 8-bit) | iff HAS_LAST_HOME |
| 6/8.. | `ed_pub[32]` | iff HAS_PUBKEY (fields in flag-bit order; parse fail-loud on short) |
Sizes: `check` = **6 B** · `lost` = 8 B · registering (`lost`+key) = 40 B.

**P-roster (home → 1-hop broadcast, leaf-free):**
| off | field | notes |
|---|---|---|
| 0 | `cmd(7..4) \| dir(3)=1 \| TRUNC(2) \| rsv(1..0)` | TRUNC reserved for a future cap > frame capacity (unused at cap 16) |
| 1 | `home_id` | + |
| 2 | `home_layer` (FULL 8-bit) | leaf-free ⇒ id alone aliases across co-located layers; this pair is the home identity AND what a scanner would adopt |
| 3 | `dir_epoch` | the layer-directory version (pull only on change) |
| 4 | `count` (≤ cap_host_mobiles) | + |
| 5.. | count × [ `key_hash32`(4) `local_id`(1) `reg_epoch`(1) ] | fixed 6 B entries |
| tail | quality bitmap: 2 bits/mobile, `ceil(count/4)` B; then has_key bitmap: 1 bit/mobile, `ceil(count/8)` B | entry order; quality 0=critical 1=weak 2=ok 3=strong |
Sizes: 3 mobiles = **24 B** · 16 = **107 B**. Approx airtime @ SF7/BW62.5 (the fleet's): probe ≈ 60 ms,
24-B roster ≈ 130 ms, 107-B ≈ 420 ms (implementation uses `airtime_ms` — these are sizing guides).

#### S6.2 — capacity: what actually limits hosted mobiles
- **Frame-bound ceiling: 39 entries** (5 + 6.375·N ≤ 255).
- **The REAL cap stays `cap_host_mobiles = 16`** (protocol_constants.h:400) — it is RAM-bound, not
  frame-bound: `HostMobileEntry` ≈ 84 B (node.h:1282-1285, ed_pub 32 + name 32) ⇒ 16 ≈ 1.3 KB/leaf.
  Raising it is a RAM decision (nRF52 budget), never a wire one until 39. TRUNC + round-robin roster
  windows are reserved for that future, NOT implemented in v1.

#### S6.3 — timing: jitter + the DYNAMIC check period (all named constants, bench-tunable)
| constant | v1 value | role |
|---|---|---|
| `presence_check_base_ms` | 120 000 | the mobile's check period T at quality=ok |
| `presence_check_min_ms` / `max_ms` | 60 000 / 480 000 | T clamps (see the quality rule) |
| `presence_probe_jitter_ms` | 0..8 000 | drawn per probe — desynchronizes the fleet |
| `presence_probe_retry_ms` × `k_miss` | 5 000 × 2 | unanswered probe → 2 retries → HOME LOST (detection ≈ T + 15 s; **was 30 min**) |
| `presence_roster_coalesce_ms` | 500..1 500 | the home collects probes, answers ONCE |
| `presence_roster_min_interval_ms` | 10 000 | roster rate limit (spoof/burst floor) |
| `presence_reregister_stagger_ms` | 0..5 000 | after a roster-absent (home reboot) — N mobiles don't DISCOVER simultaneously |
| `presence_rehome_dwell_ms` | 300 000 | anti-flap: min time between VOLUNTARY re-homes |
**Jitter rules:** (1) probe TX = timer expiry + jitter, LBT as usual; (2) **suppression**: hearing
ANY roster from MY home resets my timer (one mobile's probe refreshes all — the probing duty rotates
via the random jitter); hearing another mobile's probe for my home within my jitter window stands my
probe down (the dv:11831 pattern); (3) roster-on-registry-change is also coalesced (a home-reboot
re-registration burst → one roster). **Dynamic period rule (D11's consumer):** T is quality-driven
from MY 2-bit tier in the last roster — strong ⇒ min(4·base, max) · ok ⇒ base · weak ⇒ min ·
critical ⇒ min AND start the pre-scan (S6.4-C). Any anomaly (epoch mismatch, absent-from-roster,
re-home) resets T to base. The existing any-frame-from-home refresh (node_mobile.cpp:34-40 semantics)
stays — traffic IS presence; probes fire only in silence. Home-side prune: `mobile_liveness_ms`
tightens from 25 min to `3 × presence_check_max_ms` (24 min → keep 25 min v1; revisit after bench).

#### S6.4 — the exact processes
**A. Registration (fresh mobile):**
1. FSM start → passive listen (rosters + beacons) one scan dwell; overheard rosters seed the
   candidate-home list (strongest first) — a probe is NOT required to discover homes.
2. `j_discover` (existing J plane, D8) — now +3 B `[last_home_id][last_home_layer][last_reg_epoch]`
   (all 0 = fresh; frame_codec pack/parse_j + node_join.cpp:305 handler).
3. OFFER collect → targeted CLAIM → adopt (ALL existing, incl. the concurrent-register fixes).
4. **First probe = `check`+HAS_PUBKEY** (replaces TYPE-12 push): the home caches the key, next
   roster shows `has_key=1` + our hash + epoch → custody confirmed → future probes drop the key
   block. Roster shows `has_key=0` ⇒ re-attach on the next probe (the retry loop, for free).
**B. Steady state → home lost:**
1. T expires (dynamic, §S6.3) → jittered `check` probe → roster ⇒ refresh + recompute T. 
2. No roster after k_miss retries ⇒ **HOME LOST** (minutes): fire `mobile_reg{registered:false}`
   (the S2 app surface), then ONE `lost` probe (+last_home, +key): if the home was merely deaf
   one-way, it answers and we recover WITHOUT re-registration; overheard rosters from OTHER homes
   (leaf-free!) are simultaneously a fresh candidate list.
3. Still nothing ⇒ the existing scan/DISCOVER machinery over [current PHY ∪ learned_layers]
   (node_mobile.cpp scan set), j_discover carrying last_home → adopt at the new home → A.4.
**C. Proactive re-home (the NEW capability — leave BEFORE losing):**
1. Roster shows my quality ≤ weak ⇒ T=min + passive pre-scan builds candidates from overheard
   rosters/beacons (zero TX).
2. A candidate sustainedly stronger (≥2 tiers or the SNR-delta threshold, held ≥60 s — D11) AND
   dwell elapsed ⇒ DISCOVER/CLAIM at the candidate → adopt → **the clean handoff**: both homes
   alive, so the notify (D) lands immediately — zero black-hole window (today: unreachable until
   the 25-min proxy timeout).
**D. Old-home notify (every re-home where last_home ≠ 0 ≠ new):** the NEW home originates (D10)
the redirect DM — `MOBILE_BREADCRUMB`(9) payload `[new_home_id][new_epoch][new_home_layer]` —
routed to `last_home_id`, cross-layer via `last_home_layer` + the S1 path when needed. The old
home's existing redirect machinery (node_hashlocate.cpp:560-575) records it unchanged. Notify
undeliverable (old home DEAD — the common loss case) ⇒ fine: a dead home can't black-hole; the
`mobile_liveness_ms` prune is the alive-but-unreachable backstop, exactly as today.
**E. Home reboot heal:** mobiles' probes → roster WITHOUT their hashes ⇒ each re-registers
(staggered by `presence_reregister_stagger_ms`), keys re-attach via A.4 ⇒ full registry + key
custody restored in ~one probe cycle (today: silent until the 10-min re-CLAIM, keys until re-adopt).

#### S6.4b — considered + REJECTED (2026-07-18, user decision — do not re-propose)
Flag-gated **location (lat/lon 8 B) + team_id (4 B) blocks in the P-probe** (team location sharing
riding the presence cadence for free). Rejected because the probe is a CLEARTEXT broadcast: a
"share with team only" setting would be consumption-scoping for well-behaved nodes, not privacy —
any RF listener reads the position. Shipping a privacy-shaped knob without privacy is worse than
not shipping it. If team location sharing returns, it needs a sealed carrier (team-keyed), likely
on the team-beacon ext — a separate design, not a probe block.

#### S6.5 — retired / unchanged by S6
Retired: the 10-min re-CLAIM keepalive (mobile_reclaim_ms timer's keepalive role — the CLAIM stays
for registration only) · the 10-min `MOBILE_LAYER_QUERY` poll (dir_epoch-gated now) · TYPE-12
`MOBILE_PUBKEY_PUSH` (+ its :751 handler) · S3's push-until-acked hardening (subsumed by A.4).
Unchanged: J registration + targeted OFFER/DENY · redirect machinery · `mobile query <gw>` manual +
app-driven mode · beacons on all their planes · the any-frame liveness refresh.

### S5 — scenario s27 (the mandatory test, lands with the slices)
`simulation/s27_cross_layer_mobiles_meshroute.json` — see the file's `_desc` for the phase map.
Two layers (leaf 4 + leaf 7, distinct nibbles) bridged by one dual-layer gateway, 4 statics per
layer, 4 real-crypto mobiles (seed-derived hashes: M1 `0x2716EFCD` d1×32 · M2 `0x3A3E77A3` d2×32 ·
M3 `0xBCC13CC5` d3×32 · M4 `0x455FCF59` d4×32). Phases: register → **plaintext-first with INTRO
auto-attach** (same-layer M1→M2; cross-layer M1→M3, M1→M4; replies attach back) → **encrypted both
ways** (incl. cross-layer sealed + E2E-acks by hash) → **3-of-4 mobiles re-home** (dies_at_ms kills
homes S4, T1, T4; M2→S3, M3→T2, M4→T3; M1's home survives = the sender's stable anchor) →
**encrypted again post-re-home** (stale home caches re-resolve; the re-pushed keys serve at the new
homes) → **(F) PROACTIVE re-home (FSM S6.4-C):** M5 registered to S3 over a deliberately WEAK link
(SNR 14 → quality tier weak → T=min + pre-scan); the strong spare home SX (id 106) starts at 800 k
with BOTH homes alive → sustained-better + dwell → voluntary S3→SX re-home → the new-home notify →
a static `send_hash` to M5 at 1150 k must deliver THROUGH the redirect chain. That last assert is
green TODAY (via the stale proxy) and must STAY green across the implementation — it is the
zero-blackhole guard. When green: add to `BASELINE.md` (md5-anchored) + the gate recipe.

## 4. Airtime accounting (the aim, made checkable)

| Event | Cost today | Cost after |
|---|---|---|
| First contact + mutual keys (same layer) | WANT_PUBKEY flood (~77 B × relays) + answer, **and still undecryptable one way** | 2 plaintext DMs +33 B each (INTRO round trip) |
| First contact cross-layer | impossible | S1 unicast + ≤1 H flood confined to target layer (first time only) |
| Key custody at home | silent failure → app retry storms | ≤1 extra 1-hop frame per 10 min until acked, then 0 |
| Encrypted XL send | impossible | = plaintext XL cost + 16 B tag |
| Re-send to a known mobile | routed unicast | unchanged (cache TTL + epoch redirect) |

## 5. Decisions for review
- **D1** INTRO attach rule: recommended = auto on `!peer_confirmed(dst)` (§S2); alternatives: explicit `-k` only, or `cfg set intro_attach off` as an escape hatch (recommend shipping the cfg toggle anyway, default ON).
- **D2** push confirmation = reuse `-a`/E2E-ack on the 1-hop push (recommended; zero new frames) vs a dedicated ack type.
- **D3** requester-key forward: eager 1-hop frame (recommended) vs piggyback-on-next-DM.
- **D4** ~~XL origin-layer carriage~~ **RESOLVED (user correction folded in):** the CROSS_LAYER inner already carries the full preserved path + cursor (frame_codec.h:519) — the ack/answer return path = the traversed path reversed, same as static XL. Remaining implementation check only: verify the reverse-walk against frame_codec.cpp/the static XL ack path.
- **D5** type values: INTRO=15, MOBILE_KEY_FORWARD=16 (next free; coder re-verifies the enum).
- **D6** (S6) `dir_epoch` aggregation at the home when multiple gateways are known (XOR vs max vs
  per-gw) + whether any slow safety re-pull survives (≥6 h) or directory pulls are purely
  epoch-driven. Recommended: XOR + a 6 h safety pull.
- **D7** (S6) the P nibble: verify the free cmd-nibble map in frame_codec.h (0x0-0x2/0x4-0x8/0xA
  taken per the header; J's nibble + any others verified in CODE) and whether probe/roster share one
  nibble via the dir bit (recommended — nibble space is scarce) or take two.
- **D8** (S6) `lost`-probe as DISCOVER (merge) vs probe-then-existing-J-DISCOVER. Recommended: keep
  registration on J (the targeted-OFFER/concurrent-register fixes live there); the probe is
  presence-only and the pubkey block makes the j_discover +3 B extension the only J change.
- **D10** (S6) old-home notify origination: NEW home (recommended — survives an immediately-sleeping
  mobile, and the home holds the mesh/XL route) vs mobile-sent as today (node_mobile.cpp:110-121).
- **D11** (S6) the 2-bit quality tiers: source = the home's per-mobile SNR EWMA; thresholds TBD at
  bench (map to strong/ok/weak/critical; WEAK is the mobile's pre-scan trigger).
- **Slice order:** **S6 first** (independent, retires the measured 60% duty burn + the Loop-B NACK
  storm — the biggest airtime win per line of code), then S0 → S1 → S3 → S2 → S4 (s27 asserts
  phases as they land — it runs RED-partially throughout).

## 6. ★ FINDING from the s27 dry run (2026-07-17) — a REAL pre-existing bug, own slice (S0)

The first dry run (static ids 17..20, per the "statics start at 17" convention) dead-ended the
plain **same-layer** delegated send — under TODAY's protocol. Trace: at cold boot (t≈8 s) the home
allocated M1 the local id **18** before it had heard static S2 (node_id 18) — `find_free_mobile_id`
consults bindings that are still empty that early. From then on the home's mobile-transit filter
(`rt_skip_mobile_transit`, the origin-agnostic `route_uses_mobile_as_transit` — the KNOWN watch-item
from `2026-07-07-mobile-node-handling-assumptions.md`) rejected **every** route whose next hop is id
18 (i.e. via static S2), so the resolved send re-drained **every 1 s forever** (`send_deferred`/
`send_drained` loop — airtime/CPU burn, no giveup, no delivery, no failure surfaced). s22 never
trips it (high static ids); **the user's metal fleet (statics from 17 + hosted locals 17/18/19) is
exposed.** Fix sketch (S0, independent, do FIRST): `find_free_mobile_id` must exclude ids with a
known static binding AND re-check/evict on a later binding conflict; the transit filter must key on
the hosted-mobile REGISTRY (hash-marked), not the raw id. + a native test reproducing the cold-boot
alias, + a defer-loop giveup (bounded retries → `send_failed{no_route}`). s27 deliberately dodges
this (static ids 101+) so it tests the future protocol, not this bug.

## 7. Gate (every slice)
native (+tests per slice: wrapper codec round-trip incl. 0-layer byte-identity · INTRO parse/attach-rule/self-consistency-reject · push-retry-until-ack · key-forward cache · XL seal/open + hash-keyed ack) · **s18 md5 EXACT each slice** (all additions are mobile/flag-gated) · s22–s26 byte-identical until the slice that legitimately touches them (none should — these are all new-path) · s27 phase assertions flip green cumulatively · 10 boards sequential · RAM: +1 bit/peer-key entry (S2), +~0 (S1/S3/S4) — report per board.
