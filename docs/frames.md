# MeshRoute Frame Structures

On-wire layout of every MeshRoute frame — structure and field meaning only.

**Conventions**
- Byte 0's **high nibble (bits 7..4) is the command nibble (`cmd`)** that identifies the frame; the low nibble (bits 3..0) usually carries `leaf_id` (layer id, 0..15).
- Node ids are 8-bit short-ids; `0xFF` is reserved (unknown / broadcast sentinel).
- Multi-byte integers are **little-endian** unless marked **BE**.
- `rsv` = reserved (zero on pack, ignored on parse).

## Command nibble map
| cmd | Frame | Length | Role |
|-----|-------|--------|------|
| 0x0 | BCN  | variable | periodic beacon |
| 0x1 | RTS  | 7 B · 9 B if M_BROADCAST · 43 B if FLOOD | request-to-send |
| 0x2 | CTS  | 3 B | clear-to-send |
| 0x3 | DATA | 18+n B | data plane |
| 0x4 | ACK  | 3 B | acknowledgement |
| 0x5 | NACK | 4 B | negative acknowledgement |
| 0x6 | Q    | 4 B (+ pull body) | query (REQ_SYNC / CHANNEL_PULL) |
| 0x7 | H    | 8 B | hash-locate flood (soft/hard) |
| 0x8 | F    | 7 B | route-find RREQ/RREP flood |
| 0x9 | J    | 6 / 8 / 11 / 15 B | join family |

---

## BCN — beacon · cmd 0x0 · variable

**Use** — broadcast periodically (and on a change, jittered) on the routing SF; advertises DV routes, identity, schedule, seen-set, and a channel digest. **Reply** — none; neighbours merge the routes, and an unseen digest id triggers a `Q:CHANNEL_PULL`.

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

**Schedule record (4 B):** b0 = `layer_id`(b7..4) \| `(routing_sf−5)`(b3..1) \| `period_unit_5s`(b0) · b1 = `duration_100ms` · b2 = `offset_100ms` · b3 = `period_units` (×1000 ms if period_unit_5s=0, ×5000 ms if =1).

**Seen-bitmap (32 B):** presence bit for node `id` at byte `id/8`, mask `1<<(id%8)`.

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

**FLOOD (`M_BROADCAST | FLOOD`, 43 B)** — the channel-flood primary path (managed flood; the BCN digest + `Q:CHANNEL_PULL` are the repair backstop). It sets **both** flags: `M_BROADCAST` reuses the overhear-retune (receivers retune to the data SF to catch the DATA-M), `FLOOD` selects the 43-B tail and the bitmap-suppressed forward. The 4-B `channel_msg_id` + 32-B coverage bitmap replace the 2-B `m_payload_id` tail; `next` is the broadcast `0xFF` and the `dst` slot carries `hop_left`, so `pack_rts`/`parse_rts` keep the same byte positions and only the tail length branches on `FLOOD`. Forward rule: a receiver with an unmarked 1-hop neighbour re-floods (SNR-weighted backoff, OR-ing its own coverage into the bitmap and decrementing `hop_left`); all neighbours already marked → stay silent. The DATA-M that follows is the unchanged **Channel-M** inner (see DATA). **Leaf-scoped — never bridged across leaves.**

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

## DATA — data plane · cmd 0x3 · 18+n B

**Use** — the payload, sent on the granted SF after a CTS, then relayed hop-by-hop (each hop re-runs RTS/CTS/DATA). **Reply** — **ACK** from the next hop, else **NACK**; with `E2E_ACK_REQ` the final destination returns an end-to-end ACK (a DATA with `E2E_IS_ACK`).

| Byte   | Field                            | Description                                                                                                                                                                                                        |
| ------ | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0      | cmd \| addr_len                  | bits 7..4 = `0x3`; b3..1 = addr_len (0 this phase); b0 rsv                                                                                                                                                         |
| 1      | flags                            | high nibble (b7..4); low nibble rsv                                                                                                                                                                                |
| 2      | next                             | next-hop short-id                                                                                                                                                                                                  |
| 3      | dst                              | final destination short-id                                                                                                                                                                                         |
| 4      | hops_remaining \| committed_hops | b7..3 = hops_remaining (5-bit, 0..31) · b2..0 = committed_hops (3-bit, 0..7)                                                                                                                                       |
| 5      | prev_fwd_rt_hops                 | soft hop-gradient hint                                                                                                                                                                                             |
| 6..7   | ctr                              | 16-bit message counter (**LE**)                                                                                                                                                                                    |
| 8..13  | visited[6]                       | 6 fixed slots, one short-id each (0 = empty, no length prefix)                                                                                                                                                     |
| 14..   | inner                            | typed inner (see **Inner layouts**) — for a normal / hash-bind frame byte 14 is the cleartext payload-flags byte; a **channel-M** frame (`PAYLOAD_TYPE_M`) has **no** flags byte (byte 14 is the `channel_msg_id`) |
| last 4 | MAC                              | opaque 4-byte frame trailer (currently zero-stubbed)                                                                                                                                                               |

**flags (byte 1 high nibble):** `PAYLOAD_TYPE_M = 0x1`, `PRIORITY = 0x2`, `E2E_IS_ACK = 0x4`, `E2E_ACK_REQ = 0x8`.

### Inner layouts

The `inner` slot has **three** shapes. **Channel-M** is typed by the header `PAYLOAD_TYPE_M` flag and has **no** payload-flags byte; the other two begin with a cleartext **payload-flags byte** (`inner[0]`).

**Payload-flags byte** (`inner[0]`, always cleartext) — types a non-M inner so a relay/receiver can act without decoding the body:

| bit | flag | meaning |
|-----|------|---------|
| b0 `0x01` | `CROSS_LAYER` | the inner carries a cross-layer **layer-path** table (gateway routing) |
| b1 `0x02` | `H_ANSWER` | the inner is a public *hash-bind response* (relays read + cache `key_hash32→node_id`, cache-on-pass) |
| b2 `0x04` | `AUTHORITATIVE` | on an H-answer: owner-answered (vs a cached relay) |
| b3 `0x08` | `CRYPTED` | `origin`+body sealed (relays still read the cleartext flags / `dst_key_hash32` / layer-path) |
| b4 `0x10` | `SOURCE_HASH` | the inner carries the **origin's** `key_hash32`, in the sealed region (hidden under `CRYPTED`); set **iff `E2E_ACK_REQ`** so the destination can address the E2E-ack back |
| b6 `0x40` | `DST_HASH` | the inner carries the recipient's `key_hash32` (L2c verify-on-delivery; shared by cross-layer) |
| b5,b7 | rsv | zero |

**① Normal unicast** (incl. the E2E-ack; `PAYLOAD_TYPE_M=0`, `H_ANSWER=0`):
`[payload-flags] [dst_key_hash32 4 B LE — iff DST_HASH] [layer-path — iff CROSS_LAYER] [origin 1 B] [source_hash 4 B LE — iff SOURCE_HASH] [body…] [Poly1305 tag 16 B — iff CRYPTED]`. The E2E-ack (`E2E_IS_ACK`) is this shape with `body` = the acked `ctr` (2 B LE). Everything from `origin` onward (incl. `source_hash`) is the region sealed under `CRYPTED`; the flags byte, `dst_key_hash32`, and layer-path stay cleartext.

**② Channel-M** (`PAYLOAD_TYPE_M=1`; no payload-flags byte):
`[channel_msg_id 4 B **BE**] [channel_id 1 B] [flavor 1 B] [body…]`.

**③ Hash-bind answer** (`H_ANSWER`; cleartext, 7 B):
`[payload-flags = H_ANSWER (+AUTHORITATIVE)] [target_layer 1 B] [node_id 1 B] [key_hash32 4 B LE]`.

#### Cross-layer layer-path (iff `CROSS_LAYER`)

A non-destructive source-route over **layers** (not nodes), carried between `dst_key_hash32` and `origin`, **cleartext** (every gateway must read it). The layer list is **immutable in transit** — only the cursor advances — so the destination can **reverse** it to route the E2E-ack home.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | n_layers \| cur | b7..4 = `n_layers` (2..15, total layers incl. both endpoints) · b3..0 = `cur` (0-based index of the layer the frame is in **now**) |
| 1.. | layer_id[n_layers] | one **4-bit** layer id each, packed 2/byte — entry `i` at byte `1+(i>>1)`, high nibble if `i` even, low if odd |

Path size = `1 + ceil(n_layers/2)` B. `layer_id[0]` = the **origin's** layer, `layer_id[n_layers-1]` = the **destination's** layer, the rest are transit layers in order. The 4-bit ids cap the network at **16 layers** — consistent with the 4-bit `leaf_id` used throughout the wire.

- **Forward:** a gateway bridging `layer_id[cur] → layer_id[cur+1]` increments **only** `cur` and re-transmits on the next layer — it never removes a list entry. (DATA carries no `leaf_id` of its own, so `cur` is the explicit position.)
- **Arrival:** the frame is in the destination's layer when `cur == n_layers-1`.
- **E2E-ack return** (`E2E_ACK_REQ` set): the destination builds the ack as a `CROSS_LAYER` DATA (`E2E_IS_ACK`) whose `layer_id[]` is the **reverse** of the received list (`ack[i] = layer_id[n_layers-1-i]`), `cur` reset to 0 — so the ack walks home along the same layers. The ack's `dst_key_hash32` is the origin's `key_hash32`, which the destination reads from the request's `source_hash` (present because `E2E_ACK_REQ` required it).

- **`dst_key_hash32`** — the universal *final-recipient* `key_hash32`, **always cleartext** (leaks no more than the cleartext `dst` id). The node the `dst` id routes to verifies it against its own: match → deliver; mismatch → an id collision misdelivered this DM → forward to the real owner (the DM still arrives) + heal. Same- and cross-layer share this one field. Default-on for app DMs (the send path looks the dst's hash up in `id_bind`); +4 B.
- **`source_hash`** — the **origin's** `key_hash32`, in the **sealed** region (after `origin`), so it's hidden from relays when `CRYPTED` is set. Set **iff `E2E_ACK_REQ`**: the destination reads it to learn who sent the DM and address the E2E-ack back — it becomes the ack's `dst_key_hash32`, and for cross-layer it pairs with the reversed `layer-path` to route the ack home. +4 B.
- **`CRYPTED`** — `origin`+body are sealed (XChaCha20-Poly1305) so the **sender is hidden** (only the destination decrypts; relays/eavesdroppers see the cleartext `dst`, `dst_key_hash32`, layer-path, and `visited`, never the originator). A 16-B tag trails. AEAD-auth failure on a misdelivered CRYPTED DM corroborates a collision.
- **Normal DM** (no CRYPTED): `origin` is the sender's node_id (the destination attributes/replies by it; `visited[]` is zero on origination, so it leaks no sender).

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

## H — hash-locate flood · cmd 0x7 · 8 B

**Use** — flood to resolve an identity `key_hash32` to its current `node_id`; `origin` is preserved so the answer routes home. **Soft** (default): any node answers from its own hash *or* its `id_bind` cache — fast, may be stale. **Hard** (flag b0): owner-only — skip caches and flood until the owner itself answers (authoritative). Start soft; escalate to hard on a verify-on-use mismatch. **Reply** — a *hash-bind response* (a DATA carrying the binding — see below) routed to `origin`; an authoritative (owner) reply overwrites stale caches along its path.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x7`; bits 3..0 = leaf_id |
| 1 | origin | querier node_id, preserved across forwards |
| 2..5 | key_hash32 | identity hash being located (**LE**) |
| 6 | ttl | decremented per forward; 0 = drop |
| 7 | flags | b0 = `HARD` (owner-only resolve, ignore caches); b7..1 rsv |

**Hash-bind response** (H's reply): a routed **DATA** to `origin`, typed by the `H_ANSWER` inner payload-flag (inner byte 0; `CRYPTED`=0, so relays read & cache it — see DATA). Body: `target_layer`(1) · `node_id`(1) · `key_hash32`(4 **LE**) — **no magic** (the `H_ANSWER` flag types it; supersedes the Lua `\x1fH1`); the *authoritative* bit rides the payload-flags byte (b2).

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

## Planned wire extensions

Decided in the design specs and landing with their slices (not all wired yet) — listed so the wire reference stays complete:

- **BCN — +10 B leaf header** `{lineage_id(4) · epoch(2) · config_hash(4)}`, written **before** the route entries (so it survives the 151-B page truncation): the same-`leaf_id`-but-divergent-config filter. See `docs/specs/2026-06-05-identity-leaf-membership-join-design.md` §3.
- **DATA — `DST_HASH`** (described above): **shipped** — the cleartext recipient `key_hash32` + verify-on-delivery (a mismatch identity-preservingly forwards the DM to the real owner; no renumber, see the node-id design spec §7.1).
- **DATA — `CRYPTED`** (described above): the wire flag/layout are locked; the AEAD seal/open (`origin`+body encryption) ships with the E2E / by-hash slice.
- **H — pubkey resolution:** a `WANT_PUBKEY` query flag (b1) + a hash-bind answer that appends the 32-B `ed_pub` (a new payload-flag in the free b5/b7 space — b4 is now `SOURCE_HASH`), for DM E2E key resolution.
- **Q — `CONFIG_PULL`** subtype: pull a leaf's full config (`data_sf_list`/`leaf_name`/`duty_cycle`) for a `{lineage, epoch}`, returned as a routed DATA typed by a new payload-flag (free b5/b7 space).
- **J — `wire_version`** in byte 1's reserved nibble: a coarse wire-compat check at join.
