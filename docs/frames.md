# MeshRoute Frame Structures

On-wire layout of every MeshRoute frame — structure and field meaning only.

> **Scope:** wire structure only. **Behaviour** — emission policy, scheduling, drift, crypto/routing flow, defaults, cfg keys, NV versions — lives in the code (the source of truth) and the design specs under `docs/superpowers/specs/`; it is deliberately **not** duplicated here (duplicated behaviour drifts out of date faster than the wire does).

**Conventions**
- Byte 0's **high nibble (bits 7..4) is the command nibble (`cmd`)** that identifies the frame; the low nibble (bits 3..0) usually carries `leaf_id` (layer id, 0..15).
- Node ids are 8-bit short-ids. Reserved allocation: **`0`** = unprovisioned / no-use · **`1`–`16`** = GATEWAYS only (statically provisioned) · **`17`–`254`** = normal nodes (auto-assigned via DAD) · **`0xFF`** = unknown / broadcast sentinel. ⚠ **PLANNED (Phase 3, not yet enforced):** `join_choose_candidate_id` currently picks from `[1,254]` with no `1`–`16` exclusion — the gateway reservation lands with the per-leaf-DAD slice.
- Multi-byte integers are **little-endian** unless marked **BE**.
- `rsv` = reserved (zero on pack, ignored on parse).

## Command nibble map
| cmd | Frame | Length | Role |
|-----|-------|--------|------|
| 0x0 | BCN  | variable | periodic beacon |
| 0x1 | RTS  | 7 B · 9 B if M_BROADCAST · 43 B if FLOOD | request-to-send |
| 0x2 | CTS  | 3 B | clear-to-send |
| 0x3 | DATA | 12+n B | data plane |
| 0x4 | ACK  | 3 B | acknowledgement |
| 0x5 | NACK | 4 B | negative acknowledgement |
| 0x6 | Q    | 4 B (+ pull body) | query (REQ_SYNC / CHANNEL_PULL) |
| 0x7 | H    | 8 B | hash-locate flood (soft/hard) |
| 0x8 | F    | 7 B | route-find RREQ/RREP flood |
| 0x9 | J    | 6 / 8 / 11 / 15 B | join family |
| 0xA | M    | 7+n B | channel message (lean; leaf-scoped gossip) |

---

## BCN — beacon · cmd 0x0 · variable

**Use** — advertises DV routes, identity, a window schedule, a channel digest, gateway-bridged-layer hints, and — when enabled — a seen-set. **Reply** — none; neighbours merge the routes, and an unseen digest id triggers a `Q:CHANNEL_PULL`.

Fixed 8-byte header, then (in order) an optional schedule block, `n_entries` route entries, an optional 32-byte seen-bitmap, and an optional ext block.

| Byte | Field                 | Description                                                                                                       |
| ---- | --------------------- | ----------------------------------------------------------------------------------------------------------------- |
| 0    | cmd \| leaf_id        | bits 7..4 = `0x0`; bits 3..0 = leaf_id                                                                            |
| 1    | src                   | sender node_id                                                                                                    |
| 2    | flags \| n_entries_lo | b7 has_schedule · b6 self_gateway · b5 is_mobile · b4 has_seen_bitmap · b3 has_ext · b2..0 = n_entries low 3 bits |
| 3    | n_entries_hi          | b7..5 = n_entries high 3 bits (`n_entries = lo \| hi<<3`, 0..63); b4..0 rsv                                       |
| 4..7 | key_hash32            | 32-bit identity hash (**LE**)                                                                                     |
|      |                       |                                                                                                                   |

**Body order:** `[schedule record(s) if has_schedule]` → `n_entries × route entry (4 B)` → `[32-B seen-bitmap if has_seen_bitmap]` → `[ext_len (1 B) + ext payload if has_ext]`.

**Route entry (4 B):** `dest` · `next` · `score_bucket`(b7..4) \| rsv(b3..1) \| `is_gateway`(b0) · `hops` (full byte, 1..255).

**Schedule record (4 B):** b0 = `layer_id`(b7..4) \| `(routing_sf−5)`(b3..1) \| `period_unit_5s`(b0) · b1 = `duration_100ms` (window length) · b2 = `offset_100ms` (countdown to that leaf's next window-open, re-stamped at TX) · b3 = `period_units` (×1000 ms if period_unit_5s=0, ×5000 ms if =1). A gateway emits one record per leaf it serves (≤2). *(Frequency is per-layer but provisioning-only — never on the wire.)*

**Seen-bitmap (32 B):** presence bit for node `id` at byte `id/8`, mask `1<<(id%8)`; set for nodes seen **directly** (non-transitive). Optional — **off by default** (`seen_bitmap_enabled`). The route-entry page is sized by a byte-budget that **reserves** the 32-B bitmap (+ schedule + ext) so a full page carrying it never overflows the 151-B frame.

**Ext block (TLVs):** `ext_len (1 B)`, then a sequence of TLVs — each `[type (b7..4) | body_len (b3..0)] [body … body_len B]`. A reader scans for the type(s) it knows and skips the rest (forward-compatible; `body_len ≤ 15`). Defined types:

- **Type 3 — channel digest:** `[count 1 B] [channel_msg_id 4 B BE] × count` — this node's dirty channel-msg ids; an unseen id triggers a `Q:CHANNEL_PULL`. `count ≤ 3` (the 4-bit `body_len` cap). Gateways do **not** emit it (channel-plane consumer, Principle 11).
- **Type 4 — gateway-layer:** `[gw_id 1 B] × N`, then `⌈N/2⌉` packed leaf-nibble bytes (entry *2i* → low nibble, entry *2i+1* → high nibble). Each `(gw_id, dest_leaf)` pair = "gateway `gw_id` (its node_id on **this** leaf) bridges to `dest_leaf`." A gateway self-advertises the other leaf it serves; **every** node re-gossips its learned entries, so the mapping propagates **multi-hop** — the originator of a cross-layer DM reads it to choose a gateway it has a route to. `N ≤ 9` (9 + ⌈9/2⌉ = 14 B ≤ the cap); entries age out after `bridged_layers_ttl_ms`. A single-layer node has nothing to advertise → emits no type-4 TLV (wire-inert).

---

## RTS — request-to-send · cmd 0x1 · 7 B · 9 B if M_BROADCAST · 43 B if FLOOD

**Use** — reserve the single TX slot with the chosen `next` hop before DATA (after listen-before-talk + a budget check). **Reply** — **CTS** to proceed, or **NACK** if refused. The M_BROADCAST variant (channel re-broadcast) expects *no* CTS — overhearers retune to the data SF to catch the DATA. The **FLOOD** variant (channel-flood primary path) likewise expects no CTS, and carries a 4-B flood id + 32-B coverage bitmap (see below).

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x1`; bits 3..0 = leaf_id |
| 1 | src | immediate sender (the requester; for a FLOOD, the relaying forwarder — changes each hop) |
| 2 | next | next-hop being requested (**FLOOD: `0xFF`** broadcast) |
| 3 | ctr_lo \| addr_len | b7..4 = ctr_lo · b3..1 = addr_len (0 only, today) · b0 rsv |
| 4 | dst | final destination (**FLOOD: `hop_left`** TTL cap, decremented each forward) |
| 5 | sf_index \| rts_flags | b7..6 = sf_index · b5..2 = rts_flags · b1..0 rsv |
| 6 | payload_len | length of the DATA(-M) payload to follow (wraps mod-256) |
| 7..8 | m_payload_id | **BE**, present **iff** `M_BROADCAST` **without** `FLOOD` |
| 7..10 | channel_msg_id | **BE**, present **iff** `FLOOD` — the immutable flood id |
| 11..42 | coverage bitmap | present **iff** `FLOOD` — 32 B (256 bits): bit `id` = node `id` already covered in this leaf (byte `id/8`, mask `1<<(id%8)`); OR'd-in at each hop |

**sf_index:** 0..2 = singleton index into `allowed_data_sfs`; 3 = ANY (receiver picks data SF by SNR). A FLOOD pins `sf_index` to the sender's `max_data_sf` (every flood DATA-M rides the largest allowed SF).
**rts_flags:** `M_BROADCAST = 0x01`, `RELAY = 0x02`, `FLOOD = 0x04` (positioned at bits 2 (0x04), 3 (0x08), and 4 (0x10) within byte 5).

**FLOOD (`M_BROADCAST | FLOOD`, 43 B)** — the channel-flood primary path (managed flood; the BCN digest + `Q:CHANNEL_PULL` are the repair backstop). It sets **both** flags: `M_BROADCAST` reuses the overhear-retune (receivers retune to the data SF to catch the DATA-M), `FLOOD` selects the 43-B tail and the bitmap-suppressed forward. The 4-B `channel_msg_id` + 32-B coverage bitmap replace the 2-B `m_payload_id` tail; `next` is the broadcast `0xFF` and the `dst` slot carries `hop_left`, so `pack_rts`/`parse_rts` keep the same byte positions and only the tail length branches on `FLOOD`. Forward rule: a receiver with an unmarked 1-hop neighbour re-floods (SNR-weighted backoff, OR-ing its own coverage into the bitmap and decrementing `hop_left`); all neighbours already marked → stay silent. The data-SF frame that follows is the lean **M frame** (cmd 0xA — see M), **not** a DATA frame; the RTS-M's `payload_len` carries that M frame's body length. **Leaf-scoped — never bridged across leaves.**

---

## CTS — clear-to-send · cmd 0x2 · 3 B

**Use** — the next hop's grant of an RTS; names `chosen_data_sf`, and `already_received` aborts a needless resend (lost-ACK recovery). **Reply** — the sender's **DATA** follows (the CTS is not itself acked).

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| flags | bits 7..4 = `0x2`; b3..1 = `(chosen_data_sf − 5)`; b0 = `already_received` |
| 1 | tx_id | CTS sender (the forwarder clearing the requester) |
| 2 | rx_id | intended requester id (the RTS sender being cleared) |

`chosen_data_sf` in 5..12. `already_received` short-circuits a resend whose ACK was lost. No `ctr_lo`: `tx_id + rx_id` pin the flight under single-slot stop-and-wait, and `tx_id` disambiguates cascade alternates.

---

## DATA — data plane · cmd 0x3 · 12+n B

**Use** — the payload, sent on the granted SF after a CTS, then relayed hop-by-hop (each hop re-runs RTS/CTS/DATA). **Reply** — **ACK** from the next hop, else **NACK**; with `E2E_ACK_REQ` the final destination returns an end-to-end ACK (a DATA whose **TYPE** = `E2E_ACK`).

| Byte   | Field                            | Description                                                                                   |
| ------ | -------------------------------- | --------------------------------------------------------------------------------------------- |
| 0      | cmd \| addr_len                  | bits 7..4 = `0x3`; b3..1 = addr_len (0 this phase); b0 rsv                                    |
| 1      | flags                            | full byte (see **flags**) — APP gates a TYPE byte                                             |
| 2      | next                             | next-hop short-id                                                                             |
| 3      | dst                              | final destination short-id                                                                    |
| 4      | hops_remaining \| committed_hops | b7..3 = hops_remaining (5-bit, 0..31) · b2..0 = committed_hops (3-bit, 0..7)                  |
| 5      | prev_fwd_rt_hops                 | soft hop-gradient hint                                                                        |
| 6..7   | ctr                              | 16-bit message counter (**LE**)                                                               |
| 8      | TYPE                             | message kind (enum, **present iff `APP`** — see **TYPE**); else the inner starts here         |
| 8/9..  | inner                            | the inner (see **Inner layouts**) — starts at 9 when `APP`, else 8; **no payload-flags byte** |
| last 4 | MAC                              | opaque 4-byte frame trailer (currently zero-stubbed)                                          |

Bytes 2..7 are the **fixed routing header** — relays read `next`/`dst`/`hops`/`ctr` at constant offsets regardless of `APP`. The TYPE byte sits where the old `inner[0]` payload-flags byte was (promoted into the cleartext header, gated by `APP`); only endpoints / cache-on-pass snoopers read it. A normal user DM (`APP=0`) carries **no** TYPE byte.

**flags (byte 1, full byte):** combinable modifiers; `APP` is **derived** from TYPE on pack (a non-zero TYPE sets `APP` and emits the TYPE byte, so the flag and TYPE can't disagree).

| bit       | flag          | status                                                                                                                                                                                                        |
| --------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| b7 `0x80` | `APP`         | a TYPE byte follows the header (derived from TYPE)                                                                                                                                                            |
| b6 `0x40` | `CROSS_LAYER` | reserved — the inner carries a cross-layer **layer-path** table (gateway routing)                                                                                                                             |
| b5 `0x20` | `CRYPTED`     | **live** — `origin` + everything after it SEALED (relays read only the cleartext `dst_key_hash32`); key selection = trial decryption; forces `DST_HASH`                                                       |
| b4 `0x10` | `E2E_ACK_REQ` | live — the final destination returns an end-to-end ack                                                                                                                                                        |
| b3 `0x08` | `LOCATION`    | **live** — opt-in 6-B sender location in the sealed inner (after `source_hash`); set ONLY on origination (`loc_in_dm` + a non-zero fix); see *Planned wire extensions*                                        |
| b2 `0x04` | `SOURCE_HASH` | **live** — the inner carries the origin's `key_hash32` after `origin` (the stable sender identity; **default-on for app DMs**); **sealed under `CRYPTED`**                                                    |
| b1 `0x02` | `DST_HASH`    | live — the inner carries the recipient's `key_hash32` (L2c verify-on-delivery)                                                                                                                                |
| b0 `0x01` | `PRIORITY`    | decoded-only (no behaviour wired yet)                                                                                                                                                                         |

**TYPE (byte 8, enum, present iff `APP`):** mutually-exclusive message kinds. `AUTHORITATIVE` is folded into the H-answer code (1 vs 2); the old `E2E_IS_ACK` flag became the `E2E_ACK` type.

| code | type                                                                   | inner shape                                                   |
| ---- | ---------------------------------------------------------------------- | ------------------------------------------------------------- |
| 0    | *(reserved / invalid — never on the wire; `APP=0` means no TYPE byte)* | —                                                             |
| 1    | `H_ANSWER`                                                             | `[target_layer 1][node_id 1][key_hash32 4 LE]` (6 B)          |
| 2    | `AUTHORITATIVE_H_ANSWER`                                               | same as `H_ANSWER`; the answer is the owner's (authoritative) |
| 3    | `E2E_ACK`                                                              | normal-unicast inner, `body` = the acked `ctr` (2 B LE)       |
| 4    | DATA_TYPE_H_ANSWER_PUBKEY                                              |                                                               |
| 5    | DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY                                |                                                               |

### Inner layouts

The `inner` has **no payload-flags byte** — its old flag-bits are now the byte-1 `flags`, its old type-bits are the `TYPE` enum. The presence of each optional field is read from the **byte-1 header flags** (not a payload byte). (Channel messages are no longer a DATA inner — they ride the lean **M** frame, cmd 0xA, below.)

**① Normal unicast** (incl. the E2E-ack) — fields in order:

| Field            | Size   | Present when                                             | CRYPTED?                                 |
| ---------------- | ------ | -------------------------------------------------------- | ---------------------------------------- |
| `dst_key_hash32` | 4 B LE | `DST_HASH` (default-on app DM; **mandatory if CRYPTED**) | cleartext (AAD + nonce input)            |
| layer-path       | var    | `CROSS_LAYER`                                            | cleartext (no cross-layer CRYPTED in v1) |
| `origin`         | 1 B    | always                                                   | **sealed**                               |
| `source_hash`    | 4 B LE | `SOURCE_HASH` (default-on app DM)                        | **sealed**                               |
| location         | 6 B    | `LOCATION`                                               | **sealed**                               |
| `body`           | n B    | always                                                   | **sealed**                               |
| Poly1305 tag     | 16 B   | `CRYPTED` (trails the ciphertext)                        | —                                        |

- **Plaintext:** all fields cleartext in that order. `origin` = the sender's node_id (destination attributes/replies by it). E2E-ack (`TYPE=E2E_ACK`) = this shape, `body` = the acked `ctr` (2 B LE).
- **CRYPTED (sealed-sender redesign, `2026-06-16-e2e-sealed-sender-redesign.md`):** only `dst_key_hash32` stays cleartext (the AEAD AAD); **`origin` and everything after it are SEALED** → a relay **cannot tell who sent the DM**. The 4-B MAC trailer grows to the 8-B cleartext **nonce-seed**. `CRYPTED ⇒ DST_HASH`.
  - **Key selection = trial decryption** (no cleartext sender hint): the receiver tries each cached **authoritative/pinned** peer key; the tag verifies for exactly one → that key's owner *is* the sender (implicit auth), and `origin` is recovered from the seal. No candidate opens ⇒ **silent drop** (no push/ack/inbox). Bounded to ≤`cap_peer_keys` opens, only on DMs whose `dst_key_hash32` is ours.
  - `source_hash` is now redundant under trial (the opening key is the sender); kept in v1 for the anti-spoof check — dropping it (−4 B) is a future optimisation.
  - **Residual metadata leaks (accepted in v1):** sealing `origin` hides *who* sent a DM, but not the flow's existence. (1) `ctr` + `dst_key_hash32` stay cleartext, so a relay can still do **coarse traffic analysis** (count/time DMs to a given recipient hash). (2) Sealing `origin` removes the opportunistic **reverse-route learning** a relay used to get from a cleartext sender — the ack/return path falls back to normal discovery (the mutual `reqpubkey` handshake pre-warms it). (3) The E2E-ack's `dst_key_hash32 = origin` (it routes *back* to the sender) exposes the original sender as a **recipient** on the return leg — the routing-necessary recipient exposure we already accept for every DM.

**② Hash-bind answer** (`TYPE = H_ANSWER` / `AUTHORITATIVE_H_ANSWER`; cleartext, 6 B):
`[target_layer 1 B] [node_id 1 B] [key_hash32 4 B LE]`. The `H_ANSWER` / `AUTHORITATIVE` distinction rides the frame TYPE (1 vs 2), **not** the inner.

#### Cross-layer layer-path (iff `CROSS_LAYER`)

A non-destructive source-route over **layers** (not nodes), carried between `dst_key_hash32` and `origin`, **cleartext** (every gateway must read it). The layer list is **immutable in transit** — only the cursor advances — so the destination can **reverse** it to route the E2E-ack home.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | n_layers \| cur | b7..4 = `n_layers` (2..15, total layers incl. both endpoints) · b3..0 = `cur` (0-based index of the layer the frame is in **now**) |
| 1.. | layer_id[n_layers] | one **4-bit** layer id each, packed 2/byte — entry `i` at byte `1+(i>>1)`, high nibble if `i` even, low if odd |

Path size = `1 + ceil(n_layers/2)` B. `layer_id[0]` = the **origin's** layer, `layer_id[n_layers-1]` = the **destination's** layer, the rest are transit layers in order. The 4-bit ids cap the network at **16 layers** — consistent with the 4-bit `leaf_id` used throughout the wire.

- **Forward:** a gateway bridging `layer_id[cur] → layer_id[cur+1]` increments **only** `cur` and re-transmits on the next layer — it never removes a list entry. (DATA carries no `leaf_id` of its own, so `cur` is the explicit position.)
- **Arrival:** the frame is in the destination's layer when `cur == n_layers-1`.
- **E2E-ack return** (`E2E_ACK_REQ` set): the destination builds the ack as a `CROSS_LAYER` DATA with `TYPE = E2E_ACK` whose `layer_id[]` is the **reverse** of the received list (`ack[i] = layer_id[n_layers-1-i]`), `cur` reset to 0 — so the ack walks home along the same layers. The ack's `dst_key_hash32` is the origin's `key_hash32`, which the destination reads from the request's `source_hash` (present because `E2E_ACK_REQ` required it).

- **`dst_key_hash32`** — the final-recipient `key_hash32`, **always cleartext** (leaks no more than the `dst` id). The node `dst` routes to verifies it: match → deliver; mismatch → an id collision misdelivered it → forward to the real owner + heal. Default-on for app DMs; +4 B.
- **`source_hash`** — the origin's stable `key_hash32` (the 8-bit `origin` is reassignable, the hash is not). Default-on for app DMs; the destination uses it as the DM's durable identity (inbox/app dedup) and, with `E2E_ACK_REQ`, as the ack's `dst_key_hash32`. Sealed under CRYPTED. +4 B.

`hops_remaining = 0` on the wire means TTL-exhausted (drop). The MAC stays opaque.

---

## ACK — acknowledgement · cmd 0x4 · 3 B

**Use** — the next hop confirms a DATA landed, ending the stop-and-wait flight; also carries `budget_hint`, `snr_bucket`, and the anti-spam `AIRTIME_WARN`. **Reply** — none.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| ctr_lo | bits 7..4 = `0x4`; bits 3..0 = ctr_lo |
| 1 | budget_hint \| snr_bucket \| warn | b7..6 = budget_hint (2-bit) · b5..4 = snr_bucket (2-bit) · b3..1 rsv · b0 = `AIRTIME_WARN` |
| 2 | to | addressed recipient node_id |

`ctr_lo` is retained (unlike CTS): the long ACK window with no sender field needs it to reject stale ACKs.

`AIRTIME_WARN` (b0): set by the receiver when this sender's overheard airtime is in the anti-spam warn band (≥ `originator_airtime_warn_fraction` × the per-sender airtime cap, i.e. 0.8 × 0.25 × duty budget). The sender then parks new DM originations for `originator_ack_warn_backoff_ms`. The bit rides the spare rsv nibble, so the **C++ cmd-nibble ACK stays 3 B**; the Lua's literal-`K`-tag ACK has no spare byte-1 bits and **grows to 4 B** (a 4th flags byte) to carry the same warn — the standard Lua-verbose / C++-compact wire split.

---

## NACK — negative acknowledgement · cmd 0x5 · 4 B

**Use** — a receiver declines an RTS/DATA it can't take, with a `reason` and a retry-after `payload`. **Reply** — the sender backs off (`BUSY_RX`/`BUDGET`), reroutes (`HOP_BUDGET`), or drops (`LOOP_DUP`).

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| reason | bits 7..4 = `0x5`; bits 3..0 = reason |
| 1 | ctr_lo | b7..4 = ctr_lo; b3..0 rsv |
| 2 | payload | reason-specific byte (0..255) |
| 3 | to | addressed recipient node_id |

**reason:** `0 = BUSY_RX`, `1 = BUDGET`, `2 = HOP_BUDGET`, `3 = LOOP_DUP`.

---

## Q — query · cmd 0x6 · 4 B (+ CHANNEL_PULL body)

**Use** — a 1-hop query. `REQ_SYNC` (dest `0xFF`): a (re)joining or mobile node asks neighbours to beacon now. `CHANNEL_PULL`: request the channel msgs whose ids a BCN digest showed missing. **Reply** — `REQ_SYNC` → a **BCN**; `CHANNEL_PULL` → the holder re-broadcasts each msg as **DATA (M_BROADCAST)**.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x6`; bits 3..0 = leaf_id |
| 1 | src | sender node_id |
| 2 | dest | target node_id (`0xFF` = REQ_SYNC broadcast) |
| 3 | opcode \| mobile | b7..6 = opcode · b5 = mobile · b4..0 rsv |
| 4 | count | **CHANNEL_PULL only** — number of channel ids |
| 5.. | channel_msg_id[count] | **CHANNEL_PULL only** — 4 bytes each, **BE** |

**opcode:** `1 = REQ_SYNC`, `3 = CHANNEL_PULL`.

---

## H — hash-locate flood · cmd 0x7 · 8 B · **+32 B** when a WANT_PUBKEY request attaches the requester's pubkey

**Use** — flood to resolve an identity `key_hash32` → its `node_id`; `origin` is preserved so the answer routes home. **Soft** (default): any node answers from its own hash or `id_bind` cache. **Hard** (b0): owner-only — skip caches, flood until the owner answers. **WANT_PUBKEY** (b1): ask for the target's E2E **pubkey** (not just node_id). **Reply** — a hash-bind/pubkey response (DATA) routed to `origin`.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x7`; bits 3..0 = leaf_id |
| 1 | origin | querier node_id, preserved across forwards |
| 2..5 | key_hash32 | identity hash being located (**LE**) |
| 6 | ttl | decremented per forward; 0 = drop |
| 7 | flags | b0 `HARD` · b1 `WANT_PUBKEY` · b2 `REQ_PUBKEY` (requester `ed_pub` appended); b7..3 rsv |
| 8..39 | requester ed_pub | 32 B — **only if `REQ_PUBKEY`** (mutual exchange, below) |

**Replies (routed DATA to `origin`, `CRYPTED`=0 so relays cache):**
- **Hash-bind** (`TYPE = H_ANSWER` / `AUTHORITATIVE_H_ANSWER`): inner `target_layer`(1) · `node_id`(1) · `key_hash32`(4 LE). Authoritative = the TYPE code (2 vs 1).
- **Pubkey** (`TYPE = H_ANSWER_PUBKEY`, answers `WANT_PUBKEY`): inner `target_layer`(1) · `node_id`(1) · `ed_pub`(32) (no `key_hash32` — it's `ed_pub[:4]`). **Mutual:** if the request carried `REQ_PUBKEY`+`ed_pub`, the owner first **caches the requester's pubkey** (so it can decrypt the requester's sealed DMs + address this answer), then replies — **one round provisions BOTH directions** (the E2E bootstrap). The request rides the cleartext flood ⇒ both pubkeys are relay-visible: the deliberate "establishing contact" exposure; every DM after is sealed.

---

## F — route-find RREQ/RREP flood · cmd 0x8 · 7 B

**Use** — on-demand route discovery (AODV) when the table has no route: an **RREQ** (`is_reply=0`) floods toward `dst_id`. **Reply** — an **RREP** (`is_reply=1`) from the destination (or a node that already has a route), routed back to `origin`; `relay` is the next-hop that path-learning records.

| Byte | Field           | Description                                                                                                |
| ---- | --------------- | ---------------------------------------------------------------------------------------------------------- |
| 0    | cmd \| leaf_id  | bits 7..4 = `0x8`; bits 3..0 = leaf_id                                                                     |
| 1    | origin          | querier node_id, preserved across forwards (RREP routes home to it)                                        |
| 2    | is_reply \| rsv | b7 = is_reply (0 = RREQ, 1 = RREP); b6..0 rsv                                                              |
| 3    | dst_id          | destination being sought                                                                                   |
| 4    | ttl_or_next_hop | RREQ: ttl (decremented per forward) · RREP: next_hop toward origin                                         |
| 5    | hops            | RREQ: hops from origin · RREP: hops to dst                                                                 |
| 6    | relay           | immediate forwarder's node_id; reverse/forward path learning takes next-hop from this (not the PHY sender) |
|      |                 |                                                                                                            |

---

## J — join family · cmd 0x9 · 6 / 8 / 11 / 15 B

**Use** — OTAA-style join: a new node claims a layer short-id with no central authority. **Flow** — **DISCOVER** (broadcast) → **OFFER** (a responder proposes terms) → **CLAIM** (claims a `proposed_node_id`) → **DENY** (on conflict, with `reason`) or the claim stands.

Shared 2-byte header; body and length depend on opcode. All multi-byte fields **LE**.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x9`; bits 3..0 = leaf_id |
| 1 | gateway_capable \| is_mobile \| opcode | b7 = gateway_capable · b6 = is_mobile · b5..4 = opcode · b3..0 rsv |
| 2.. | body | per opcode (below) |

**opcode:** `0 = DISCOVER`, `1 = CLAIM`, `2 = DENY`, `3 = OFFER`.

**DISCOVER (6 B):** bytes 2..5 = `key_hash32`.

**OFFER (8 B):** byte 2 = `responder_node_id` · bytes 3..6 = `responder_key_hash32` · byte 7 = `data_sf_bitmap`.

**CLAIM (11 B):** bytes 2..5 = `key_hash32` · byte 6 = `proposed_node_id` · bytes 7..8 = `lease_age_seconds` (u16) · byte 9 = `claim_epoch` · byte 10 = `nonce`.

**DENY (15 B):** byte 2 = `denied_node_id` · bytes 3..6 = `owner_key_hash32` · bytes 7..10 = `claimant_key_hash32` · bytes 11..12 = `owner_lease_age_seconds` (u16) · byte 13 = `owner_claim_epoch` · byte 14 = `reason`.
DENY **reason:** `1 = CONFLICT`, `2 = PENDING_CLAIM`, `3 = OWN_ID_DEFENSE`, `4 = MEDIATED` (a third-party shared-neighbour heal).

**node_id assignment is Duplicate-Address-Detection (DAD), not OTAA:** a node listens, picks a free id (excluding every id in `id_bind` + the routing table), broadcasts a **CLAIM**, and adopts it after a guard window unless **DENY**'d; a same-id collision heals by one side renumbering. **Tiebreak is `key_hash32`-only — lower key wins/keeps, higher yields** (`claim_epoch`/`lease_age_seconds` are carried but reserved/telemetry, not consulted). `DISCOVER`/`OFFER` are deferred (the listen + CLAIM/heal core is what's used). See `docs/specs/2026-06-05-node-id-auto-assignment-design.md`.

---

## M — channel message (lean) · cmd 0xA · 7+n B

**Use** — a single-leaf channel (broadcast-group) message; the data-SF frame announced by a FLOOD or M_BROADCAST **RTS-M** (the channel-flood primary path, or a `Q:CHANNEL_PULL` repair response). Purpose-built: it drops the ~17 B of DM-only plumbing the old `DATA + PAYLOAD_TYPE_M` carried (`next`/`dst`/`hops`/`ctr`/`visited[6]`/`MAC`) and rides `leaf_id` in byte 0, so the cross-leaf leak gate is the **standard byte-0 leaf check** (`(b0 & 0x0F) != leaf_id → drop`, before buffering). **Reply** — none (fire-and-forget); a receiver that retuned to the data SF (the overhear ARM) buffers it promiscuously. **Deliberate divergence from the frozen Lua** (which keeps channel-M on the DATA frame) — a C++-only wire choice, documented like the `data_sf` removal.

| Byte | Field          | Description                                                                                                                           |
| ---- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| 0    | cmd \| leaf_id | bits 7..4 = `0xA`; bits 3..0 = leaf_id — **the leak gate** (a foreign-leaf M frame dies here)                                         |
| 1    | channel_id     | which channel                                                                                                                         |
| 2    | flavor         | **flags byte** — b0..2 = crypto variant (`0 = public` · `1 = group` · …); **b3 `0x08` = LOCATION (DEFERRED — proposal, not implemented)** → would carry a 6-B location after `channel_msg_id`; b4..7 rsv                                                       |
| 3..6 | channel_msg_id | **BE**; identity — `== the FLOOD RTS-M tail` (bytes 7-10) `== the Q:CHANNEL_PULL` id; `origin = byte 3`                               |
| 7..  | payload        | by `flavor` (`public` = plaintext body; `group`/encrypted = `[nonce \| ciphertext \| Poly1305 tag 16 B]` — the deferred crypto slice) |

**Header = 7 B.** The announcing RTS-M's `payload_len` carries this frame's **body** length (`n`); an overhearer sizes its data-SF retune window as `airtime(payload_len + 7)`. **Leaf-scoped — never bridged across leaves** (gateways are channel consumers/providers, never flood-bridges; see the channel-flood redesign spec). `channel_msg_id` is **BE** (distinct from the LE `key_hash32`/`ctr` elsewhere).

---

## Planned wire extensions

Decided in the design specs and landing with their slices (not all wired yet) — listed so the wire reference stays complete:

- **BCN — +10 B leaf header** `{lineage_id(4) · epoch(2) · config_hash(4)}`, written **before** the route entries (so it survives the 151-B page truncation): the same-`leaf_id`-but-divergent-config filter. `config_hash = BLAKE2b(u16 allowed_sf_bitmap LE ‖ u32 duty_ppm LE ‖ u8 name_len ‖ leaf_name)[:4]`. **Wire FLAG-DAY** (no BCN version handshake → reflash-all). **⚠ Re-examine BCN truncation when this lands** — the leaf header must never be cut; pin the overflow priority (entries / seen-bitmap / ext). See `docs/specs/2026-06-05-identity-leaf-membership-join-design.md` §3 + phase-0 doc (L1/L5).
- **DATA — `DST_HASH`** (described above): **shipped** — the cleartext recipient `key_hash32` + verify-on-delivery (a mismatch identity-preservingly forwards the DM to the real owner; no renumber, see the node-id design spec §7.1).
- **DATA — `CRYPTED`: SHIPPED, + sealed-sender redesign** (`docs/superpowers/specs/2026-06-16-e2e-sealed-sender-redesign.md`): the AEAD seal (X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305) now covers **`origin` + everything after it**; AAD = the cleartext `dst_key_hash32` only; key selection by **trial decryption** (no cleartext sender). Nonce: the 4-B MAC trailer → an 8-B cleartext nonce-seed under CRYPTED (4-B-zero when off → s18 byte-identical), `nonce = BLAKE2b(seed8 ‖ ctr ‖ dst_key_hash32)[:24]`. See *Inner layouts*.
- **DATA — `LOCATION` (opt-in 6-B sender location): SHIPPED.** ~11 m, 21-bit lat + 22-bit lon quantized from the stored `int32 e7` (`g_lat_e7`/`g_lon_e7`); flag `0x08`, in the sealed inner after `source_hash` (private to the recipient once `CRYPTED` lands). Toggle `loc_in_dm` (default off → wire byte-identical; never sends `(0,0)`); set ONLY on origination. See `docs/superpowers/specs/2026-06-14-location-propagation.md`.
- **M — `LOCATION` (opt-in, broadcast-public): DEFERRED proposal.** `flavor` bit `0x08`, a 6-B location after `channel_msg_id`; toggle `loc_in_m`. NOT implemented this round — needs threading the originator's location through the channel-flood plane with re-flood **preservation** (whole-leaf coverage, never the re-flooder's own location) + the RTS-M `payload_len +6`; the codec (`pack_m`/`parse_m`) does NOT handle `0x08` yet. Future slice.
- **H — pubkey resolution (E2E): SHIPPED, + mutual** (redesign): `WANT_PUBKEY` H → owner answers DATA `TYPE H_ANSWER_PUBKEY` = `[target_layer 1][node_id 1][ed_pub 32]`. **Mutual:** a `REQ_PUBKEY` request appends the requester's `ed_pub`, so one round provisions BOTH directions (the bootstrap). Relays cache on-pass (verify `ed_pub[:4]==`hash). See **H**.
- **Q — `CONFIG_PULL`** subtype: pull a leaf's full config (SF-set / `leaf_name` / `duty_cycle`) for a `{lineage, epoch}`, answered by a routed DATA **TYPE `CONFIG_ANSWER`**. See phase-0 doc (L3).
- **J — `wire_version`** in byte 1's reserved nibble: a coarse wire-compat check at join.
