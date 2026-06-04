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
| 0x1 | RTS  | 7 B (9 B if M_BROADCAST) | request-to-send |
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

## RTS — request-to-send · cmd 0x1 · 7 B (9 B if M_BROADCAST)

**Use** — reserve the single TX slot with the chosen `next` hop before DATA (after listen-before-talk + a budget check). **Reply** — **CTS** to proceed, or **NACK** if refused. The M_BROADCAST variant (channel re-broadcast) expects *no* CTS — overhearers retune to the data SF to catch the DATA.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x1`; bits 3..0 = leaf_id |
| 1 | src | immediate sender (the requester) |
| 2 | next | next-hop being requested |
| 3 | ctr_lo \| addr_len | b7..4 = ctr_lo · b3..1 = addr_len (0 only, today) · b0 rsv |
| 4 | dst | final destination |
| 5 | sf_index \| rts_flags | b7..6 = sf_index · b5..2 = rts_flags · b1..0 rsv |
| 6 | payload_len | length of the DATA payload to follow (wraps mod-256) |
| 7..8 | m_payload_id | **BE**, present **iff** `rts_flags & M_BROADCAST` |

**sf_index:** 0..2 = singleton index into `allowed_data_sfs`; 3 = ANY (receiver picks data SF by SNR).
**rts_flags:** `M_BROADCAST = 0x01`, `RELAY = 0x02` (positioned at bits 2 (0x04) and 3 (0x08) within byte 5).

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

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| addr_len | bits 7..4 = `0x3`; b3..1 = addr_len (0 this phase); b0 rsv |
| 1 | flags | high nibble (b7..4); low nibble rsv |
| 2 | next | next-hop short-id |
| 3 | dst | final destination short-id |
| 4 | hops_remaining \| committed_hops | b7..3 = hops_remaining (5-bit, 0..31) · b2..0 = committed_hops (3-bit, 0..7) |
| 5 | prev_fwd_rt_hops | soft hop-gradient hint |
| 6..7 | ctr | 16-bit message counter (**LE**) |
| 8..13 | visited[6] | 6 fixed slots, one short-id each (0 = empty, no length prefix) |
| 14.. | inner | cleartext payload-flags byte + body (encrypted iff `CRYPTED`), n bytes — see below |
| last 4 | MAC | opaque 4-byte trailer |

**flags (byte 1 high nibble):** `PAYLOAD_TYPE_M = 0x1`, `PRIORITY = 0x2`, `E2E_IS_ACK = 0x4`, `E2E_ACK_REQ = 0x8`.

**Inner payload-flags (inner byte 0, always cleartext):** the inner begins with a flags byte that *types* the payload so a relay/receiver can act on it without decoding the body — `CROSS_LAYER`(b0, gateway envelope; the next byte is the cross-layer address length, then the path), `H_ANSWER`(b1, a public *hash-bind response* — relays read & cache the `key_hash32→node_id` binding, cache-on-pass), `AUTHORITATIVE`(b2, on an H-answer: owner-answered ⇒ overwrite vs cached ⇒ hint), `CRYPTED`(b3, the body after the prefix is encrypted), b7..4 rsv. **Invariant:** the flags byte and the cross-layer path are *always cleartext*; only the type-specific body is encrypted, and only when `CRYPTED` is set — so public payloads (H-answers, cross-layer routing) stay readable to forwarders while user content is sealed. This is how a node knows it may, and can, read an otherwise-opaque inner. (A *by-hash* DM also carries the intended `key_hash32` so the recipient can verify-on-use; mismatch → hard-H redirect — see H.)

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
DENY **reason:** `1 = CONFLICT`, `2 = PENDING_CLAIM`, `3 = OWN_ID_DEFENSE`.
