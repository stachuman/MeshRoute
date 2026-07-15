# MeshRoute Frame Structures

On-wire layout of every MeshRoute frame — structure and field meaning only.

> **Scope:** wire structure only. **Behaviour** — emission policy, scheduling, drift, crypto/routing flow, defaults, cfg keys, NV versions — lives in the code (the source of truth) and the design specs under `docs/superpowers/specs/`; it is deliberately **not** duplicated here (duplicated behaviour drifts out of date faster than the wire does).

**Conventions**
- Byte 0's **high nibble (bits 7..4) is the command nibble (`cmd`)** that identifies the frame; the low nibble (bits 3..0) usually carries `leaf_id` (layer id, 0..15).
- Node ids are 8-bit short-ids. Reserved allocation: **`0`** = unprovisioned / no-use · **`1`–`16`** = GATEWAYS only (statically provisioned) · **`17`–`254`** = normal nodes (auto-assigned via DAD) · **`0xFF`** = unknown / broadcast sentinel. The reservation is enforced at pick time — `join_choose_candidate_id` scans `17..254` and never re-prefers a legacy `1..16` id (R6.3/G1).
- **Mobile/team LOCAL-id addressing (the plane-separation marks).** A mobile (home-assigned local id) and a team member (`_team_local_id`) both use a **LOCAL id** that may collide a normal node's global id (§18), so a frame carrying one self-describes so no static node learns/bills/routes it as a global identity. **`mobile_src` (RTS byte-5 b1)** = *the `src` is a LOCAL id* — set by a registered mobile **and** by a team unicast DM (§6.4: `mobile_src = pt.mobile_src || is_team_peer(pt.next)`). **`addr_len = 1` (RTS & DATA)** = *the `next` is a LOCAL id* (a home last-mile or a team peer). **`mobile_to`** = *the `to` is a LOCAL id* on **ACK byte-1 b1** and **NACK byte-1 b0** (a colliding static id ignores it). **`H_FLAG_MOBILE_REQ`** = *the H querier's origin is a LOCAL id* (owner won't id_bind it). **CTS/NACK-overhear by context** (the CTS has no spare bit — see CTS). All marks default `0` → backward-compatible. **Every learn/bill/id_bind site that could pollute the static plane checks these marks** (see the separation spec). See `docs/superpowers/specs/2026-07-10-protocol-plane-separation.md` (canonical) + `2026-07-07-mobile-node-handling-assumptions.md`.
- **`wire_version`** (a 4-bit cross-version handshake, currently `1`) rides two frames: **BCN** byte 3 (b3..0) and **J** byte 1 (b3..0). A neighbour whose `wire_version` differs is not interoperated with (no cross-version join; a `join_refused{wire_version}` push is rate-limited).
- Multi-byte integers are **little-endian** unless marked **BE**.
- `rsv` = reserved (zero on pack, ignored on parse).

## Command nibble map
| cmd | Frame | Length | Role |
|-----|-------|--------|------|
| 0x0 | BCN  | variable | periodic beacon |
| 0x1 | RTS  | 7 B · 9 B if M_BROADCAST · 43 B if FLOOD | request-to-send |
| 0x2 | CTS  | 3 B · 4 B with NAV payload_len | clear-to-send |
| 0x3 | DATA | 12+ B (MAC 4 B, or 8 B if CRYPTED; +1 TYPE byte if APP) | data plane |
| 0x4 | ACK  | 3 B | acknowledgement |
| 0x5 | NACK | 4 B | negative acknowledgement |
| 0x6 | Q    | 4 B (+ pull/config body) | query (REQ_SYNC / CONFIG_PULL / CHANNEL_PULL) |
| 0x7 | H    | 8 B · +32 B if WANT_PUBKEY | hash-locate flood (soft/hard) |
| 0x8 | F    | 9 B | route-find RREQ/RREP flood |
| 0x9 | J    | 6 / 8·13 / 11 / 15 B | join family (OFFER 13 B iff `is_mobile`) |
| 0xA | M    | 7+n B | channel message (lean; leaf-scoped gossip) |
| 0xB | C    | 15+n B | leaf-config answer (the CONFIG_PULL reply) |

---

## BCN — beacon · cmd 0x0 · variable

**Use** — advertises DV routes, identity, the leaf-config fingerprint, an optional window schedule, a channel digest, gateway-bridged-layer hints, peer-liveness gossip, and — when enabled — a seen-set. **Reply** — none; neighbours merge the routes, and an unseen digest id triggers a `Q:CHANNEL_PULL`.

Fixed **8-byte header + 6-byte leaf-config header (14 B, always present)**, then (in order) an optional schedule block, `n_entries` route entries, an optional 32-byte seen-bitmap, and an optional ext block.

| Byte  | Field                                              | Description                                                                                                       |
| ----- | -------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| 0     | cmd \| leaf_id                                     | bits 7..4 = `0x0`; bits 3..0 = leaf_id                                                                            |
| 1     | src                                                | sender node_id                                                                                                    |
| 2     | flags \| n_entries_lo                              | b7 has_schedule · b6 self_gateway · b5 is_mobile · b4 has_seen_bitmap · b3 has_ext · b2..0 = n_entries low 3 bits |
| 3     | n_entries_hi \| heard_set_complete \| wire_version | b7..5 = n_entries high 3 bits (`n_entries = lo \| hi<<3`, 0..63) · b4 = `heard_set_complete` · b3..0 = `wire_version` |
| 4..7  | key_hash32                                         | 32-bit identity hash (**LE**)                                                                                     |
| 8..13 | leaf header                                        | `lineage_id`(2) · `config_epoch`(2) · `config_hash`(2), all **LE** — the R6.1 leaf-config fingerprint (below)     |

- **`heard_set_complete` (byte 3 b4):** the bidirectionality-census flag — set when this beacon carries the sender's **complete** `hops==1` heard-set, so a receiver *absent* from a complete page learns the link **to the sender is one-way** (present ⇒ confirmed bidirectional). Default 0; gateways (`n_layers==2`) and mobiles never set it. See `2026-06-29-asymmetric-link-aware-routing-design.md`.
- **`wire_version` (byte 3 b3..0):** the 4-bit cross-version handshake (see Conventions). It occupies the byte-3 low nibble — **not** rsv.

**Leaf-config header (6 B, bytes 8..13, FIXED — pre-schedule, never truncated; `parse_beacon` rejects a beacon shorter than `8 + 6`):** `lineage_id`(u16 LE) · `config_epoch`(u16 LE) · `config_hash`(u16 LE). The same-`leaf_id`-but-divergent-config membership filter: `config_hash = BLAKE2b(sf_list ‖ duty_bp ‖ active_fraction_bp ‖ ch_interval_ms ‖ dm_interval_ms ‖ leaf_name_len ‖ leaf_name)[:2]` (the three anti-spam tunables joined the fingerprint 2026-07-03 — see the **C** frame). The route-entry page is byte-budgeted *after* this header (overhead = `8 + 6 + schedule + seen-bitmap + ext`) so a full page never overflows the 151-B frame and the header is always written. See `docs/specs/2026-06-05-identity-leaf-membership-join-design.md`.

**Body order:** `[schedule block if has_schedule]` → `n_entries × route entry (4 B)` → `[32-B seen-bitmap if has_seen_bitmap]` → `[ext_len (1 B) + ext payload if has_ext]`.

**Route entry (4 B):** `dest` · `next` · `score_bucket`(b7..4) \| `degraded`(b3) \| rsv(b2..1) \| `is_gateway`(b0) · `hops` (full byte, 1..255).
- **`degraded` (b3):** the asymmetric-link bit — the advertised route's next-hop is one-way / transitively-bad. A reader sorts it down (keep-don't-delete, never a hard drop) and re-advertises the bit, so one-way knowledge propagates and the mesh routes around the bad hop; it clears on recovery. Orthogonal to `score_bucket` (a one-way link can have excellent SNR). See the asymmetric-link spec.

**Schedule block (gateway only):** a 1-byte lead `[gateway_spread_nibble(b7..4) \| schedule_count(b3..0)]`, then `schedule_count × 4-B record`. The lead byte carries the herd-spread hint (senders jitter their RTS across the window by it) and the record count (≤2). Each **record (4 B):** b0 = `layer_id`(b7..4) \| `(routing_sf−5)`(b3..1) \| `period_unit_5s`(b0) · b1 = `duration_100ms` (window length) · b2 = `offset_100ms` (countdown to that leaf's next window-open, re-stamped at TX) · b3 = `period_units` (×1000 ms if period_unit_5s=0, ×5000 ms if =1). A gateway emits one record per leaf it serves. *(Frequency is per-layer but provisioning-only — never on the wire.)*

**Seen-bitmap (32 B):** presence bit for node `id` at byte `id/8`, mask `1<<(id%8)`; set for nodes seen **directly** (non-transitive). Optional — **off by default** (`seen_bitmap_enabled`).

**Ext block (TLVs):** `ext_len (1 B)`, then a sequence of TLVs — each `[type (b7..4) | body_len (b3..0)] [body … body_len B]`. A reader scans for the type(s) it knows and skips the rest (forward-compatible; `body_len ≤ 15`). Defined types:

- **Type 1 — suspect-nodes gossip:** `[type<<4 | N] [node_id 1 B] × N` — peers the sender locally observed timing-out; a reader marks each at the **SUSPECT** tier. `N ≤ 8` (`peer_suspect_bcn_max`). The §P4 distributed-avoidance plane (a gossip-learned tier is never re-gossiped — anti-storm).
- **Type 2 — liveness-state gossip:** `[type<<4 | 2N] [node_id 1 B, state 1 B] × N` — peers at a worse tier, `state` = `2` SILENT / `3` DEAD. `N ≤ 7` (`peer_liveness_state_bcn_max`; 2 B/entry must fit the 4-bit TLV len).
- **Type 3 — channel digest:** `[count 1 B] [channel_msg_id 4 B BE] × count` — this node's dirty channel-msg ids; an unseen id triggers a `Q:CHANNEL_PULL`. `count ≤ 3` (`channel_dirty_max_per_bcn`). Gateways do **not** emit it (channel-plane consumer, Principle 11).
- **Type 4 — gateway-layer:** `[gw_id 1 B] × N`, then `⌈N/2⌉` packed leaf-nibble bytes (entry *2i* → low nibble, entry *2i+1* → high nibble). Each `(gw_id, dest_leaf)` pair = "gateway `gw_id` (its node_id on **this** leaf) bridges to `dest_leaf`." A gateway self-advertises the other leaf it serves; **every** node re-gossips its learned entries, so the mapping propagates **multi-hop** — the originator of a cross-layer DM reads it to choose a gateway it has a route to. `N ≤ 9` (9 + ⌈9/2⌉ = 14 B ≤ the cap); entries age out after `bridged_layers_ttl_ms`. A single-layer node has nothing to advertise → emits no type-4 TLV (wire-inert).

To be implemented - for mobile teams - BCN should include lat/lon

---

## RTS — request-to-send · cmd 0x1 · 7 B · 9 B if M_BROADCAST · 43 B if FLOOD

**Use** — reserve the single TX slot with the chosen `next` hop before DATA (after listen-before-talk + a budget check). **Reply** — **CTS** to proceed, or **NACK** if refused. The M_BROADCAST variant (channel re-broadcast) expects *no* CTS — overhearers retune to the data SF to catch the DATA. The **FLOOD** variant (channel-flood primary path) likewise expects no CTS, and carries a 4-B flood id + 32-B coverage bitmap (see below).

| Byte   | Field                 | Description                                                                                                                                       |
| ------ | --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0      | cmd \| leaf_id        | bits 7..4 = `0x1`; bits 3..0 = leaf_id                                                                                                            |
| 1      | src                   | immediate sender (the requester; for a FLOOD, the relaying forwarder — changes each hop)                                                          |
| 2      | next                  | next-hop being requested (**FLOOD: `0xFF`** broadcast)                                                                                            |
| 3      | ctr_lo \| addr_len    | b7..4 = ctr_lo · b3..1 = addr_len (`0` = normal; **`1` = mobile-next** — `next` is a LOCAL id; `2..7` hierarchy-deferred) · b0 rsv                             |
| 4      | dst                   | final destination (**FLOOD: `hop_left`** TTL cap, decremented each forward)                                                                       |
| 5      | sf_index \| rts_flags | b7..6 = sf_index · b5..2 = rts_flags · **b1 = `MOBILE`** (`src` is a mobile local-id / mobile-originated) · b0 rsv                                     |
| 6      | payload_len           | length of the DATA(-M) payload to follow (wraps mod-256)                                                                                          |
| 7..8   | m_payload_id          | **BE**, present **iff** `M_BROADCAST` **without** `FLOOD`                                                                                         |
| 7..10  | channel_msg_id        | **BE**, present **iff** `FLOOD` — the immutable flood id                                                                                          |
| 11..42 | coverage bitmap       | present **iff** `FLOOD` — 32 B (256 bits): bit `id` = node `id` already covered in this leaf (byte `id/8`, mask `1<<(id%8)`); OR'd-in at each hop |

**sf_index:** 0..2 = singleton index into `allowed_data_sfs`; 3 = ANY (receiver picks data SF by SNR). A FLOOD pins `sf_index` to the sender's `max_data_sf` (every flood DATA-M rides the largest allowed SF).
**rts_flags:** `M_BROADCAST = 0x01`, `RELAY = 0x02`, `FLOOD = 0x04`, `E2E_ACK = 0x08` (the constant values; positioned at byte-5 bits 2 (0x04), 3 (0x08), 4 (0x10), 5 (0x20) after the `<<2` shift).

**★ Flag space (RTS is out of nibble):** the 4-bit `rts_flags` nibble is **FULL** — all four bits are allocated (`E2E_ACK` took the last one). **Byte 5 b1 is the `MOBILE` bit** (`src` is a mobile local-id / mobile-originated — §mobile Slice 1). The remaining free wire bits in RTS are the **`rsv` bits**: **byte 5 b0** and **byte 3 b0** (2 bits) — packed `0`, ignored on parse (`pack_rts`/`parse_rts`), so a new node can claim one **backward-compatibly** (old senders send 0, old receivers ignore — the `E2E_ACK`/`MOBILE` precedent, no flag-day). `addr_len` (byte 3 b3..1): **`1` = mobile-next** (the `next` is a home-assigned LOCAL id — see Conventions); `parse_rts` **rejects `> 1`**, leaving `2..7` for the deferred hierarchy. Beyond those 2 `rsv` bits, a new RTS flag means widening the frame.

**`E2E_ACK = 0x08`** — an originator hint that the pending DATA is a `DATA_TYPE_E2E_ACK`. The 1st-hop neighbour's airtime backstop drops an over-budget sender's RTS *before* the DATA type is known; this bit lets the backstop **exempt an e2e-ack from the DROP** (throttling an ack is self-defeating — the sender never learns delivery and re-sends, creating the very traffic the throttle meant to suppress). The RTS is still **observed** (honest airtime metric — no bypass) and the hard duty-cycle limit still binds the sender's own ack originations. **Anti-spoof:** the relay verifies at DATA-time that the frame really is a `DATA_TYPE_E2E_ACK`; if not, the sender is flagged (`e2e_ack_spoof`) and its `E2E_ACK` bit is **ignored** for one originator window (the backstop re-applies). Backward-compatible — the 4th free bit of the nibble; old nodes ignore it (no flag-day) and simply keep applying the backstop.

**FLOOD (`M_BROADCAST | FLOOD`, 43 B)** — the channel-flood primary path (managed flood; the BCN digest + `Q:CHANNEL_PULL` are the repair backstop). It sets **both** flags: `M_BROADCAST` reuses the overhear-retune (receivers retune to the data SF to catch the DATA-M), `FLOOD` selects the 43-B tail and the bitmap-suppressed forward. The 4-B `channel_msg_id` + 32-B coverage bitmap replace the 2-B `m_payload_id` tail; `next` is the broadcast `0xFF` and the `dst` slot carries `hop_left`, so `pack_rts`/`parse_rts` keep the same byte positions and only the tail length branches on `FLOOD`. Forward rule: a receiver with an unmarked 1-hop neighbour re-floods (SNR-weighted backoff, OR-ing its own coverage into the bitmap and decrementing `hop_left`); all neighbours already marked → stay silent. The data-SF frame that follows is the lean **M frame** (cmd 0xA — see M), **not** a DATA frame; the RTS-M's `payload_len` carries that M frame's body length. **Leaf-scoped — never bridged across leaves.**

**Mobile-originated M_BROADCAST.** When a **mobile** sends an M_BROADCAST (team-formation / team-broadcast), it self-marks the RTS-M as mobile-originated via **the same `MOBILE` bit (byte-5 b1)** — one bit means "`src` is a mobile", unicast or broadcast. Effect (behaviour — later slices): **static nodes do NOT re-flood it** (the relation-based transit rule — a mobile's broadcast is not the static backbone's load); only mobiles / the team relay it within the cluster, and the co-located team receives it in one shot. See `docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md` §8/§12.

---

## CTS — clear-to-send · cmd 0x2 · 3 B · **4 B with NAV `payload_len`**

**Use** — the next hop's grant of an RTS; names `chosen_data_sf`, and `already_received` aborts a needless resend (lost-ACK recovery). **Reply** — the sender's **DATA** follows (the CTS is not itself acked).

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| flags | bits 7..4 = `0x2`; b3..1 = `(chosen_data_sf − 5)`; b0 = `already_received` |
| 1 | tx_id | CTS sender (the forwarder clearing the requester) |
| 2 | rx_id | intended requester id (the RTS sender being cleared) |
| 3 | payload_len | **optional** — present iff the sender attaches a NAV hint (the cleared DATA's inner+MAC length), so a CTS-overhearer can size an exact virtual-carrier-sense reservation |

`chosen_data_sf` in 5..12. `already_received` short-circuits a resend whose ACK was lost. No `ctr_lo`: `tx_id + rx_id` pin the flight under single-slot stop-and-wait, and `tx_id` disambiguates cascade alternates. The 4th byte is omitted (3-B CTS) when NAV is off or no length is attached; `parse_cts` accepts 3 **or** 4 bytes.

**Mobile last-mile — no mark, by design.** The CTS flags nibble is **full** (`(sf−5)` + `already_received`), so a CTS whose `rx_id` is a mobile's LOCAL id carries **no** mobile mark. It doesn't need one: the only node that could mis-fire on it is a home_node neighbour with a live flight, which almost always heard the **marked RTS** (`addr_len=1`) that opened the exchange and so knows local-id X is a mobile; the residual (a *hidden* colliding node with a coincident pending TX) costs at most a collision + retry, **never a mis-delivery**. See Conventions.

---

## DATA — data plane · cmd 0x3 · 12+ B

**Use** — the payload, sent on the granted SF after a CTS, then relayed hop-by-hop (each hop re-runs RTS/CTS/DATA). **Reply** — **ACK** from the next hop, else **NACK**; with `E2E_ACK_REQ` the final destination returns an end-to-end ACK (a DATA whose **TYPE** = `E2E_ACK`).

| Byte   | Field                            | Description                                                                                   |
| ------ | -------------------------------- | --------------------------------------------------------------------------------------------- |
| 0      | cmd \| addr_len                  | bits 7..4 = `0x3`; b3..1 = addr_len (`0` = normal; **`1` = mobile-next**, `next` is a LOCAL id; `2..7` hierarchy-deferred); b0 rsv       |
| 1      | flags                            | full byte (see **flags**) — `APP` gates a TYPE byte                                            |
| 2      | next                             | next-hop short-id                                                                             |
| 3      | dst                              | final destination short-id                                                                    |
| 4      | hops_remaining \| committed_hops | b7..3 = hops_remaining (5-bit, 0..31) · b2..0 = committed_hops (3-bit, 0..7)                  |
| 5      | prev_fwd_rt_hops                 | soft hop-gradient hint                                                                         |
| 6..7   | ctr                              | 16-bit message counter (**LE**)                                                               |
| 8      | TYPE                             | message kind (enum, **present iff `APP`** — see **TYPE**); else the inner starts here         |
| 8/9..  | inner                            | the inner (see **Inner layouts**) — starts at 9 when `APP`, else 8; **no payload-flags byte** |
| trailer | MAC / nonce-seed                | **4 B** (zero-stubbed) normally; **8 B** under `CRYPTED` (the cleartext AEAD nonce-seed)       |

Bytes 2..7 are the **fixed routing header** (`DATA_HDR_LEN = 8`) — relays read `next`/`dst`/`hops`/`ctr` at constant offsets regardless of `APP`. The TYPE byte sits where the old `inner[0]` payload-flags byte was (promoted into the cleartext header, gated by `APP`); only endpoints / cache-on-pass snoopers read it. A normal user DM (`APP=0`) carries **no** TYPE byte. Min sizes: **12 B** plain (8 hdr + 0 inner + 4 MAC), **13 B** with `APP`, **16 B** under `CRYPTED` (8-B trailer) — the `12+ B` header line is the plain case.

**flags (byte 1, full byte):** combinable modifiers; `APP` is **derived** from TYPE on pack (a non-zero TYPE sets `APP` and emits the TYPE byte, so the flag and TYPE can't disagree).

| bit       | flag          | status                                                                                                                                                                                                        |
| --------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| b7 `0x80` | `APP`         | a TYPE byte follows the header (derived from TYPE)                                                                                                                                                            |
| b6 `0x40` | `CROSS_LAYER` | **live** — the inner carries a cross-layer **layer-path** table (gateway routing); see below                                                                                                                  |
| b5 `0x20` | `CRYPTED`     | **live** — `origin` + everything after it SEALED (relays read only the cleartext `dst_key_hash32`); key selection = trial decryption; forces `DST_HASH`; the trailer grows to the 8-B nonce-seed              |
| b4 `0x10` | `E2E_ACK_REQ` | **live** — the final destination returns an end-to-end ack                                                                                                                                                    |
| b3 `0x08` | `LOCATION`    | **live** — opt-in 6-B sender location in the sealed inner (after `source_hash`); set ONLY on origination (`loc_in_dm` + a non-zero fix)                                                                       |
| b2 `0x04` | `SOURCE_HASH` | **live** — the inner carries the origin's `key_hash32` after `origin` (the stable sender identity; **default-on for app DMs**); **sealed under `CRYPTED`**                                                    |
| b1 `0x02` | `DST_HASH`    | **live** — the inner carries the recipient's `key_hash32` (verify-on-delivery)                                                                                                                                |
| b0 `0x01` | `PRIORITY`    | decoded-only (no behaviour wired yet)                                                                                                                                                                         |

**TYPE (byte 8, enum, present iff `APP`):** mutually-exclusive message kinds. `AUTHORITATIVE` is folded into the H-answer code (1 vs 2); the old `E2E_IS_ACK` flag became the `E2E_ACK` type.

| code | type                            | inner shape                                                                                                                                                                                                                  |
| ---- | ------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | `H_ANSWER`                      | `[target_layer 1][node_id 1][key_hash32 4 LE]` (6 B)                                                                                                                                                                         |
| 2    | `AUTHORITATIVE_H_ANSWER`        | same as `H_ANSWER`; the answer is the owner's (authoritative)                                                                                                                                                                |
| 3    | `E2E_ACK`                       | normal-unicast inner, `body` = the acked `ctr` (2 B LE). **§mobile reverse-ack:** carries `SOURCE_HASH` = the acked DM's originator hash when the sender ≠ its `origin` (a mobile — `key_hash_of_id(origin) != sender_hash`), so the ack (routed to `origin` = the mobile's `home_id`) is recognized at the home and **last-miled to the mobile**; a static→static ack has `source_hash == key_hash_of_id(origin)` → no `SOURCE_HASH` (byte-identical) |
| 4    | `H_ANSWER_PUBKEY`               | `[target_layer 1][node_id 1][ed_pub 32]` — **reserved, not emitted in v1**                                                                                                                                                   |
| 5    | `AUTHORITATIVE_H_ANSWER_PUBKEY` | same shape — **the v1-emitted pubkey answer** (owner-authoritative)                                                                                                                                                          |
| 6    | `REMOTE_CMD`                    | OTA remote-diagnostics: a console query keyword (plaintext inner)                                                                                                                                                            |
| 7    | `REMOTE_RESP`                   | OTA remote-diagnostics: the response text (plaintext inner)                                                                                                                                                                  |
| 8    | `MOBILE_H_ANSWER`               | `[target_layer 1][node_id=home 1][key_hash32=M 4 LE][epoch 1]` (7 B) — the registrar-proxy answer (§mobile §4a)                                                                                                              |
| 9    | `MOBILE_BREADCRUMB`             | `body [new_home_id 1][new_epoch 1][new_home_layer 1]` (3 B), rides `SOURCE_HASH`=M — mobile→old-home on re-register (§mobile §4b; the `new_home_layer` is §5b — so a stale OLD-layer home redirects to the right leaf)       |
| 10   | `MOBILE_LAYER_QUERY`            | empty body — a mobile asks a gateway "list the layers you bridge" (§mobile §5a, `SOURCE_HASH`=M)                                                                                                                             |
| 11   | `MOBILE_LAYER_ANSWER`           | `[count u8][ count × LayerRecord ]` — a gateway's layer directory (§mobile §5a)                                                                                                                                              |
| 12   | `MOBILE_PUBKEY_PUSH`            | `ed_pub[32]` — a mobile pushes its E2E pubkey to its HOME (`SOURCE_HASH`=M) so the home can answer WANT_PUBKEY on its behalf (§mobile hash-locate P2)                                                                        |
| 13   | `MOBILE_H_ANSWER_PUBKEY`        | the mobile hash_bind (7 B) ‖ the mobile's `ed_pub[32]` = 39 B — a home's WANT_PUBKEY answer for its LIVE mobile (§mobile hash-locate P2). Sender caches `peer_key(M)`+`mobile_home(M→home)`, **never** id_binds the local id |

*(code 0 = invalid — `APP=0` means no TYPE byte.)*

**[mobile-node §4 — locate + staleness]** `MOBILE_H_ANSWER` (8) is how a **registrar proxies a mobile's hash**: it answers `M → home_id` (always **CLAIMED** — the registrar isn't the hash's owner, so there's no authoritative variant), and the **distinct TYPE is the signal** that lets the sender cache `M → home` in a **separate mobile-home cache — NOT `id_bind`** (a mobile's LOCAL id must stay out of the global id-plane), with the trailing **`epoch`** (§17-C1) picking the freshest home during an old+new-home overlap. `MOBILE_BREADCRUMB` (9): on re-register the mobile tells its **old** home "I moved to `new_home_id` (epoch)"; the old home records a redirect and thereafter answers `MOBILE_H_ANSWER (M → new_home)` instead of dead-ending. Best-effort (TTL + re-query is the fallback). **Cross-layer (§5b):** the answer's header `target_layer` carries the home's LAYER, so a sender on another layer reaches the home via a **gateway** (the existing cross-layer DM — the bridge resolves the mobile on the target leaf via the home's proxy); the breadcrumb's `new_home_layer` lets a stale OLD-layer home redirect to the right leaf. See `2026-07-08-mobile-slice4a-mobile-h-answer-type.md` / `-slice4b-redirect-breadcrumb.md`.

**[mobile-node §6 — teams, IMPLEMENTED]** Team formation is **not** a dedicated `TEAM_ANNOUNCE` DATA type. A team is an `is_mobile`+`team_id` overlay; the wire carriers are: the beacon **team-id EXT-TLV (type 5)** (§6.2), the **`channel_flavor_team`** M-frame variant with a 4-B `team_id` tail (`M_FRAME_TEAM_HDR_LEN=11`, §6.3), the **`H_FLAG_TEAM`** team-scoped locate (§6.2), and — for team unicast DMs — the RTS/DATA **`addr_len=1` + `mobile_src=1`** marks (§6.4). A team member self-assigns a persistent `_team_local_id` via a team-scoped DAD (§6.4). **The team plane is leaf-AGNOSTIC:** a team member accepts an RTS addressed to its `_team_local_id` (`addr_len=1`) **regardless of the frame's `leaf_id`**, so a **mixed-registration team spans leaves** (an off-grid member on leaf 0 + a registered member on its home's adopted leaf); only the RTS is leaf-gated — CTS/DATA/ACK match on pending state (2026-07-12). **Team E2E-encryption by `_team_local_id`:** a teammate's `key_hash32` (on its beacon — dropped for the static id-plane at the `!b.is_mobile` guard) is cached **team-scoped** (`_team_keys`, NEVER `id_bind`, §18), so a CRYPTED DM addressed by `_team_local_id` derives `DST_HASH` locally; the recipient's pubkey comes from the team-scoped `WANT_PUBKEY` (`H_FLAG_TEAM`) — `reqpubkey <team_local_id>` resolves the hash from that same cache (2026-07-12). See `docs/superpowers/specs/2026-07-10-protocol-plane-separation.md`.

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

- **Plaintext:** all fields cleartext in that order. `origin` = the sender's **stamped** identity: a static node's `node_id`, **or a registered mobile's `home_id`** (`stamp_origin` — the ROUTABLE proxy, since a mobile's local id is invisible to the static plane; the true sender rides `source_hash`). The **inner** `origin` matches the RTS-header `origin` — both are the stamped value — so the destination attributes/replies to a routable id (a bug where the inner used `node_id` while the RTS used `home_id`, making a mobile's E2E-ack unroutable, was fixed 2026-07-11). E2E-ack (`TYPE=E2E_ACK`) = this shape, `body` = the acked `ctr` (2 B LE); on a mobile's reverse leg it also carries `SOURCE_HASH` (see the E2E_ACK TYPE row).
- **CRYPTED (sealed-sender redesign, `2026-06-16-e2e-sealed-sender-redesign.md`):** only `dst_key_hash32` stays cleartext (the AEAD AAD); **`origin` and everything after it are SEALED** → a relay **cannot tell who sent the DM**. The 4-B MAC trailer grows to the 8-B cleartext **nonce-seed** (`nonce = BLAKE2b(seed8 ‖ ctr ‖ dst_key_hash32)[:24]`). `CRYPTED ⇒ DST_HASH`.
  - **Key selection = trial decryption** (no cleartext sender hint): the receiver tries each cached **authoritative/pinned** peer key; the tag verifies for exactly one → that key's owner *is* the sender (implicit auth), and `origin` is recovered from the seal. No candidate opens ⇒ **silent drop** (no push/ack/inbox). Bounded to ≤`cap_peer_keys` opens, only on DMs whose `dst_key_hash32` is ours.
  - `source_hash` is now redundant under trial (the opening key is the sender); kept in v1 for the anti-spoof check — dropping it (−4 B) is a future optimisation.
  - **Residual metadata leaks (accepted in v1):** sealing `origin` hides *who* sent a DM, but not the flow's existence. (1) `ctr` + `dst_key_hash32` stay cleartext, so a relay can still do **coarse traffic analysis**. (2) Sealing `origin` removes the opportunistic **reverse-route learning** a relay used to get from a cleartext sender — the return path falls back to discovery (the mutual `reqpubkey` handshake pre-warms it). (3) The E2E-ack's `dst_key_hash32 = origin` exposes the original sender as a **recipient** on the return leg — the routing-necessary recipient exposure we accept for every DM.

**② Hash-bind answer** (`TYPE = H_ANSWER` / `AUTHORITATIVE_H_ANSWER`; cleartext, 6 B):
`[target_layer 1 B] [node_id 1 B] [key_hash32 4 B LE]`. The `H_ANSWER` / `AUTHORITATIVE` distinction rides the frame TYPE (1 vs 2), **not** the inner. The pubkey variant (`TYPE = AUTHORITATIVE_H_ANSWER_PUBKEY`, code 5) replaces `key_hash32` with the 32-B `ed_pub`.

#### Cross-layer layer-path (iff `CROSS_LAYER`)

A non-destructive source-route over **layers** (not nodes), carried between `dst_key_hash32` and `origin`, **cleartext** (every gateway must read it). The layer list is **immutable in transit** — only the cursor advances — so the destination can **reverse** it to route the E2E-ack home.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | n_layers | total layers incl. both endpoints (**2..4**, `≤ gw_env_max_hops`) |
| 1 | cur | 0-based index of the layer the frame is in **now** |
| 2.. | layer_id[n_layers] | one **full byte** (8-bit id) each — `layer_id[0]` = the origin's layer, `layer_id[n_layers−1]` = the destination's layer, the rest transit in order |

Path size = `2 + n_layers` B. *(The ids are full 8-bit bytes — **not** nibble-packed — and `n_layers`/`cur` are separate bytes.)*

- **Forward:** a gateway bridging `layer_id[cur] → layer_id[cur+1]` increments **only** `cur` and re-transmits on the next layer — it never removes a list entry. (DATA carries no `leaf_id` of its own, so `cur` is the explicit position.)
- **Arrival:** the frame is in the destination's layer when `cur == n_layers−1`.
- **E2E-ack return** (`E2E_ACK_REQ` set): the destination builds the ack as a `CROSS_LAYER` DATA with `TYPE = E2E_ACK` whose `layer_id[]` is the **reverse** of the received list (`ack[i] = layer_id[n_layers−1−i]`), `cur` reset to 0 — so the ack walks home along the same layers. The ack's `dst_key_hash32` is the origin's `key_hash32`, read from the request's `source_hash` (present because `E2E_ACK_REQ` required it).

`hops_remaining = 0` on the wire means TTL-exhausted (drop). The MAC stays opaque (4 B, or the 8-B nonce-seed under CRYPTED).

---

## ACK — acknowledgement · cmd 0x4 · 3 B

**Use** — the next hop confirms a DATA landed, ending the stop-and-wait flight; also carries `budget_hint`, `snr_bucket`, and the anti-spam `AIRTIME_WARN`. **Reply** — none.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| ctr_lo | bits 7..4 = `0x4`; bits 3..0 = ctr_lo |
| 1 | budget_hint \| snr_bucket \| warn | b7..6 = budget_hint (2-bit) · b5..4 = snr_bucket (2-bit) · b3..2 rsv · **b1 = `MOBILE`** (`to` is a mobile local-id) · b0 = `AIRTIME_WARN` |
| 2 | to | addressed recipient node_id |

`ctr_lo` is retained (unlike CTS): the long ACK window with no sender field needs it to reject stale ACKs.

`AIRTIME_WARN` (b0): set by the receiver when this sender's overheard airtime is in the anti-spam warn band (≥ `originator_airtime_warn_fraction` × the per-sender airtime cap). The sender then parks new DM originations for `originator_ack_warn_backoff_ms`. The bit rides the spare rsv nibble, so the C++ cmd-nibble ACK stays **3 B**.

---

## NACK — negative acknowledgement · cmd 0x5 · 4 B

**Use** — a receiver declines an RTS/DATA it can't take, with a `reason` and a retry-after `payload`. **Reply** — the sender backs off (`BUSY_RX`/`BUDGET`), reroutes (`HOP_BUDGET`), or drops (`LOOP_DUP`).

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| reason | bits 7..4 = `0x5`; bits 3..0 = reason |
| 1 | ctr_lo \| mobile_to | b7..4 = ctr_lo · b3..1 rsv · **b0 = `mobile_to`** (the `to` is a mobile/team LOCAL id) |
| 2 | payload | reason-specific byte (0..255) |
| 3 | to | addressed recipient node_id |

**reason:** `0 = BUSY_RX`, `1 = BUDGET`, `2 = HOP_BUDGET`, `3 = LOOP_DUP`.

**`mobile_to` (byte 1 b0, §mobile):** mirrors the ACK's `mobile_to`. When a NACK is addressed to a mobile/team originator (a LOCAL id), `mobile_to=1` — the mobile accepts it, a **colliding static** id ignores it (`handle_nack` gate `(mobile_to==1)==is_mobile`). Without it a static node whose global id equals a mobile's local id could mis-consume a NACK meant for the mobile. Default `0` → backward-compatible.

---

## Q — query · cmd 0x6 · 4 B (+ CONFIG_PULL / CHANNEL_PULL body)

**Use** — a 1-hop query. `REQ_SYNC` (dest `0xFF`): a (re)joining **static** node asks neighbours to beacon now. `CONFIG_PULL`: pull a leaf's full config for a `{lineage, epoch}`. `CHANNEL_PULL`: request the channel msgs whose ids a BCN digest showed missing. **Reply** — `REQ_SYNC` → a **BCN**; `CONFIG_PULL` → a **C** frame (cmd 0xB, control-plane); `CHANNEL_PULL` → the holder re-broadcasts each msg as **M (M_BROADCAST)**.

**§mobile Option A — a mobile does NOT emit Q on the static plane.** A mobile is not a leaf-config-plane member: it never sends `REQ_SYNC` (it reaches the mesh via its home, not a full-table pull) nor `CONFIG_PULL` (it adopts only the host PHY, runs its own/default config). So the byte-3 `mobile` bit, though defined, is effectively **inert** — kept as a defensive marker (any learn site still skips a `mobile`-marked Q so a mobile's LOCAL id can never enter a static `_rt`). See `node_beacon.cpp` membership exemption + `send_req_sync_q` guard.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x6`; bits 3..0 = leaf_id |
| 1 | src | sender node_id |
| 2 | dest | target node_id (`0xFF` = REQ_SYNC broadcast) |
| 3 | opcode \| mobile | b7..6 = opcode · b5 = mobile · b4..0 rsv |
| 4..7 | lineage \| epoch | **CONFIG_PULL only** — `pull_lineage` (u16 LE) · `pull_epoch` (u16 LE) |
| 4 | count | **CHANNEL_PULL only** — number of channel ids |
| 5.. | channel_msg_id[count] | **CHANNEL_PULL only** — 4 bytes each, **BE** |

**opcode:** `1 = REQ_SYNC`, `2 = CONFIG_PULL`, `3 = CHANNEL_PULL`.

---

## H — hash-locate flood · cmd 0x7 · 8 B · **+32 B** when `WANT_PUBKEY` attaches the requester's pubkey

**Use** — flood to resolve an identity `key_hash32` → its `node_id`; `origin` is preserved so the answer routes home. **Soft** (default): any node answers from its own hash or `id_bind` cache. **Hard** (b0): owner-only — skip caches, flood until the owner answers. **WANT_PUBKEY** (b1): ask for the target's E2E **pubkey** (not just node_id) *and* (mandatorily) attach the requester's own `ed_pub` — the mutual exchange. **Reply** — a hash-bind/pubkey response (DATA) routed to `origin`.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x7`; bits 3..0 = leaf_id |
| 1 | origin | querier node_id, preserved across forwards |
| 2..5 | key_hash32 | identity hash being located (**LE**) |
| 6 | ttl | decremented per forward; 0 = drop |
| 7 | flags | b0 `HARD` · b1 `WANT_PUBKEY` · b2 `TEAM` (§mobile-team: appends `team_id[4]`) · **b3 `MOBILE_REQ`** (§mobile: origin is a LOCAL id — owner must NOT id_bind it); b7..4 rsv |
| 8..39 | requester ed_pub | 32 B — present **iff `WANT_PUBKEY`** (`parse_h` rejects a WANT_PUBKEY frame < 40 B) |
| … | team_id | 4 B **LE**, present **iff `TEAM`** (after the ed_pub, if any) |

**`MOBILE_REQ` (b3, §mobile):** the querier is a mobile/team member, so `origin` is a home-assigned/team **LOCAL** id, not a global identity. The owner-answerer caches the requester's key by **hash** (`peer_key_set`) but must **not** `id_bind` its local id (the seal-back routes by hash via the home / `_rt_team`). Set for any `is_mobile` querier (a `TEAM`-scoped locate implies it too). Default `0` → backward-compatible.

There is **no separate REQ_PUBKEY flag**: `WANT_PUBKEY` alone both requests the owner's pubkey and appends the requester's `ed_pub[32]`.

**Replies (routed DATA to `origin`, `CRYPTED`=0 so relays cache):**
- **Hash-bind** (`TYPE = H_ANSWER` / `AUTHORITATIVE_H_ANSWER`): inner `target_layer`(1) · `node_id`(1) · `key_hash32`(4 LE). Authoritative = the TYPE code (2 vs 1).
- **Pubkey** (`TYPE = AUTHORITATIVE_H_ANSWER_PUBKEY`, code 5): inner `target_layer`(1) · `node_id`(1) · `ed_pub`(32) (no `key_hash32` — it's `ed_pub[:4]`). **Mutual:** the owner first **caches the requester's `ed_pub`** (from the request) so it can decrypt the requester's sealed DMs + address this answer, then replies — **one round provisions BOTH directions** (the E2E bootstrap). The request rides the cleartext flood ⇒ both pubkeys are relay-visible: the deliberate "establishing contact" exposure; every DM after is sealed.

---

## F — route-find RREQ/RREP flood · cmd 0x8 · 9 B

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
| 7..8 | config_hash     | the R6.1 leaf-config fingerprint (u16 **LE**) — **mandatory** (`parse_f` rejects < 9 B); gates a divergent-config flood |

---

## J — join family · cmd 0x9 · 6 / 8·13 / 11 / 15 B

**Use** — OTAA-style join: a new node claims a layer short-id with no central authority. **Flow** — **DISCOVER** (broadcast) → **OFFER** (a responder proposes terms) → **CLAIM** (claims a `proposed_node_id`) → **DENY** (on conflict, with `reason`) or the claim stands.

Shared 2-byte header; body and length depend on opcode. All multi-byte fields **LE**.

| Byte | Field | Description |
|------|-------|-------------|
| 0 | cmd \| leaf_id | bits 7..4 = `0x9`; bits 3..0 = leaf_id |
| 1 | gateway_capable \| is_mobile \| opcode \| wire_version | b7 = gateway_capable · b6 = is_mobile · b5..4 = opcode · b3..0 = `wire_version` |
| 2.. | body | per opcode (below) |

**opcode:** `0 = DISCOVER`, `1 = CLAIM`, `2 = DENY`, `3 = OFFER`. **`wire_version`** (b3..0) is the cross-version handshake (see Conventions) — **not** rsv.

**DISCOVER (6 B):** bytes 2..5 = `key_hash32`.

**OFFER (8 B static / 13 B mobile):** byte 2 = `responder_node_id` · bytes 3..6 = `responder_key_hash32` · byte 7 = `data_sf_bitmap` · **[iff `is_mobile`:** byte 8 = `proposed_mobile_id` (host-assigned LOCAL id) · bytes 9..12 = `target_key_hash32` (the mobile this OFFER is addressed to — a mobile adopts only an OFFER for its own `key_hash32`)**]**.

**CLAIM (11 B):** bytes 2..5 = `key_hash32` · byte 6 = `proposed_node_id` · bytes 7..8 = `lease_age_seconds` (u16) · byte 9 = `claim_epoch` · byte 10 = `nonce`.

**DENY (15 B):** byte 2 = `denied_node_id` · bytes 3..6 = `owner_key_hash32` · bytes 7..10 = `claimant_key_hash32` · bytes 11..12 = `owner_lease_age_seconds` (u16) · byte 13 = `owner_claim_epoch` · byte 14 = `reason`.
DENY **reason:** `1 = CONFLICT`, `2 = PENDING_CLAIM`, `3 = OWN_ID_DEFENSE`, `4 = MEDIATED` (a third-party shared-neighbour heal).

**node_id assignment is Duplicate-Address-Detection (DAD), not OTAA:** a node listens, picks a free id (excluding every id in `id_bind` + the routing table, and the gateway band 1–16), broadcasts a **CLAIM**, and adopts it after a guard window unless **DENY**'d; a same-id collision heals by one side renumbering. **Tiebreak is `key_hash32`-only — lower key wins/keeps, higher yields** (`claim_epoch`/`lease_age_seconds` are carried but reserved/telemetry, not consulted). `DISCOVER`/`OFFER` are deferred (the listen + CLAIM/heal core is what's used). See `docs/specs/2026-06-05-node-id-auto-assignment-design.md`.

**Mobile registration [PLANNED — mobile-node feature].** A mobile REUSES this family with `is_mobile` set, **activating the deferred DISCOVER/OFFER** for active discovery: its DISCOVER is **leaf-exempt** (processed by any node on the freq/sf/bw, not just the same `leaf_id`); willing hosts reply with a mobile OFFER carrying a **LOCAL `proposed_mobile_id`** (from the host's local pool — Option B; a small OFFER wire-add), with SNR-weighted backoff + suppression to tame the response storm; the mobile CLAIMs the strongest, **adopts the offer's `leaf_id`**, and learns gateways from the host's BCN. Local ids need no global DAD and live in a separate id-space (see the mobile mark, Conventions). See `docs/superpowers/specs/2026-07-07-mobile-node-handling-assumptions.md` §13.

---

## M — channel message (lean) · cmd 0xA · 7+n B

**Use** — a single-leaf channel (broadcast-group) message; the data-SF frame announced by a FLOOD or M_BROADCAST **RTS-M** (the channel-flood primary path, or a `Q:CHANNEL_PULL` repair response). Purpose-built: it drops the ~17 B of DM-only plumbing the old `DATA + PAYLOAD_TYPE_M` carried (`next`/`dst`/`hops`/`ctr`/`visited[6]`/`MAC`) and rides `leaf_id` in byte 0, so the cross-leaf leak gate is the **standard byte-0 leaf check** (`(b0 & 0x0F) != leaf_id → drop`, before buffering). **Reply** — none (fire-and-forget); a receiver that retuned to the data SF (the overhear ARM) buffers it promiscuously. **Deliberate divergence from the frozen Lua** (which keeps channel-M on the DATA frame) — a C++-only wire choice, documented like the `data_sf` removal.

| Byte | Field          | Description                                                                                                                           |
| ---- | -------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| 0    | cmd \| leaf_id | bits 7..4 = `0xA`; bits 3..0 = leaf_id — **the leak gate** (a foreign-leaf M frame dies here)                                         |
| 1    | channel_id     | which channel                                                                                                                         |
| 2    | flavor         | **opaque flags byte** — by convention b0..2 = crypto variant (`0 = public` · `1 = group/encrypted` · …); the codec does **not** decompose it (encryption + a b3 `0x08` LOCATION bit are deferred proposals — see *Planned*) |
| 3..6 | channel_msg_id | **BE**; identity — `== the FLOOD RTS-M tail` (bytes 7-10) `== the Q:CHANNEL_PULL` id; `origin = byte 3`                               |
| 7..  | payload        | by `flavor` (`public` = plaintext body; `group`/encrypted = `[nonce \| ciphertext \| Poly1305 tag 16 B]` — the deferred crypto slice) |

**Header = 7 B.** The announcing RTS-M's `payload_len` carries this frame's **body** length (`n`); an overhearer sizes its data-SF retune window as `airtime(payload_len + 7)`. **Leaf-scoped — never bridged across leaves** (gateways are channel consumers/providers, never flood-bridges; see the channel-flood redesign spec). `channel_msg_id` is **BE** (distinct from the LE `key_hash32`/`ctr` elsewhere).

---

## C — leaf-config answer · cmd 0xB · 15+n B

**Use** — the control-plane reply to a `Q:CONFIG_PULL`: a mother hands a joiner its full leaf config on the **routing SF**, with no RTS/CTS/data-SF handshake — so an empty-`sf_list` joiner (no data SF yet) can still bootstrap. Replaces the old routed DATA `CONFIG_ANSWER` (DATA TYPE 6, removed 2026-06-22). **Reply** — none; a lost C is re-sent when the joiner re-pulls. Adopted only if addressed to us on our leaf nibble.

| Byte | Field | Description |
| ---- | ----- | ----------- |
| 0 | cmd \| leaf_id | bits 7..4 = `0xB`; bits 3..0 = leaf_id |
| 1 | src | the answering mother's node_id |
| 2 | dst | the joiner |
| 3 | sf_list | allowed-SF set, u8 wire form (`sf_bitmap_to_wire`) |
| 4..5 | duty_bp | duty cycle, 0.01 % units (LE) |
| 6..7 | active_fraction_bp | **anti-spam v2** — `channel_active_fraction`, 0.01 % units (LE) |
| 8..9 | ch_interval_ms | **anti-spam v2** — `channel_min_interval_ms`, ms (LE u16) |
| 10..11 | dm_interval_ms | **anti-spam v2** — `dm_min_interval_ms`, ms (LE u16) |
| 12..13 | config_epoch | LWW config version (LE) |
| 14 | leaf_name_len | 0..`leaf_name_max` |
| 15.. | leaf_name | the leaf name |

**Body = 12 B fixed + name** (bytes 3.., the `pack_c_config` form). The **`config_hash`** carried in the BCN leaf-header (+ the F/J frames) is `BLAKE2b(sf_list ‖ duty_bp ‖ active_fraction_bp ‖ ch_interval_ms ‖ dm_interval_ms ‖ leaf_name_len ‖ leaf_name)[:2]` — the same wire forms in the same order (**not** `config_epoch`, which is the LWW tiebreak, not identity). A mother and a joiner must derive identical bytes or the joiner re-pulls forever. The three anti-spam fields were promoted from firmware constants to per-leaf config on 2026-07-03; the frame grew +6 B but `wire_version` was **not** bumped, so **every node on a leaf must run the same firmware** (a mixed old/new fleet would misparse). See [anti-spam.md](anti-spam.md).

---

## Planned wire extensions

Decided in the design specs, **not yet on the wire** — listed so the reference stays complete:

- **M — `LOCATION` (opt-in, broadcast-public): DEFERRED proposal.** `flavor` bit `0x08`, a 6-B location after `channel_msg_id`; toggle `loc_in_m`. Needs threading the originator's location through the channel-flood plane with re-flood **preservation** (whole-leaf coverage, never the re-flooder's own location) + the RTS-M `payload_len +6`; `pack_m`/`parse_m` do **not** handle `0x08` yet.
- **DATA — `PRIORITY` (b0):** decoded but no scheduling behaviour wired yet.
- **DATA — cross-layer `CRYPTED`:** v1 cross-layer DMs are cleartext-only; sealing the cross-layer path is a future slice.

*(Recently shipped — now documented in their frame sections above, not here: the 6-B BCN leaf-config header; the BCN `heard_set_complete` + route-entry `degraded` bidirectionality bits; `wire_version` on BCN + J; the BCN suspect/liveness ext-TLVs (types 1/2); the CTS NAV byte; the F `config_hash`; the Q `CONFIG_PULL` opcode; DATA `CRYPTED`/sealed-sender, `LOCATION`, `DST_HASH`/`SOURCE_HASH`, `CROSS_LAYER` layer-path, and the `REMOTE_CMD`/`REMOTE_RESP` TYPE codes; the H mutual `WANT_PUBKEY` pubkey exchange.)*
