# Wire-Surface Reservation — Phases 1–3 (+ headroom)

**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com>
**Date:** 2026-06-15 · **Status:** BLESSED 2026-06-15 — the shared on-wire surfaces across the
join/E2E/membership phases, reserved up front so an incrementally-built phase can't box in a later
one. Verified against `lib/core/frame_codec.h` @ `5a9b71a`. **Phase 1 claims only the bold rows;
everything else is reserved-not-built.** Phases 2/3 MUST honor this map.

---

## 1. DATA byte-1 flags — `frame_codec.h:352` — **EXHAUSTED (8/8), no free bits**

| bit | flag | status |
|---|---|---|
| 0x80 | APP | live |
| 0x40 | CROSS_LAYER | live (gateway) |
| **0x20** | **CRYPTED** | **P1 activates the behaviour** (bit already allocated) |
| 0x10 | E2E_ACK_REQ | live |
| 0x08 | LOCATION | live |
| 0x04 | SOURCE_HASH | live |
| 0x02 | DST_HASH | live |
| 0x01 | PRIORITY | decoded-only, no behaviour — **the only reclaimable bit** |

**Rule:** Phases 2/3 take **no** new DATA flag. New DATA-level kinds go via the TYPE byte (§2).
`PRIORITY` is the sole reserve if a true new DATA boolean is ever forced.

## 2. DATA TYPE byte — `frame_codec.h:368` (present iff APP) — 1–3 used, 0 invalid

| val | type | phase |
|---|---|---|
| 1 / 2 | H_ANSWER / AUTHORITATIVE_H_ANSWER | live |
| 3 | E2E_ACK | live |
| 4 | `H_ANSWER_PUBKEY` (overheard / soft pubkey answer) | **reserved, NOT emitted in v1** |
| **5** | **`AUTHORITATIVE_H_ANSWER_PUBKEY`** = `[target_layer 1][node_id 1][ed_pub 32]` (34 B) | **P1** |
| 6 | `CONFIG_ANSWER` (routed answer to `CONFIG_PULL`) | P2 |
| 7–15 | reserved: membership/config follow-ons | P2/P3 |
| 16–31 | reserved: cross-cutting (OTA / companion / inbox-seq) | future |

**Pubkey confidence (the asymmetry vs `id_bind`):** a pubkey is **immutable + hash-verifiable**
(`ed_pub[:4]==key_hash32`), so a TYPE-5 (owner's) answer is **cached authoritative even when a relay
caches it on-pass** — authoritativeness can't decay. To **encrypt**, an *authoritative* pubkey is
required; if absent → **HARD `WANT_PUBKEY`** (owner-only answer) → TYPE 5. `id_bind` (mutable
node_id) is the opposite — relayed = *claimed*, verify-on-use — and is **unchanged**.

## 3. H-query flags, byte 7 — `frame_codec.h:250` — `HARD=0x01` used

| bit | flag | phase |
|---|---|---|
| 0x01 | HARD | live |
| **0x02** | **WANT_PUBKEY** (set together with HARD for E2E resolution) | **P1** |
| 0x04–0x80 | reserved: future H flags | — |

## 4. BLAKE2b domain-separation strings — convention `"MR-<PURPOSE>-v<N>"`

No protocol KDF strings exist yet — convention set here. (Exempt: `identity.cpp`'s seed→scalar
BLAKE2b is a fixed monocypher-mirror, not a protocol KDF.)

| string | purpose | phase |
|---|---|---|
| **`"MR-E2E-v1"`** | DM ECDH→KDF (`key = BLAKE2b(domain ‖ shared ‖ min‖max(hash))[:32]`) | **P1** |
| `"MR-CFGH-v1"` | `config_hash` (adds a domain tag to the L1 formula) | P2 |
| `"MR-OTA-v1"`, `"MR-SEQ-v1"` | reserved names: OTA image / inbox-seq | future |

## 5. J opcodes — `frame_codec.h:287` — DISCOVER=0/CLAIM=1/DENY=2/OFFER=3; DENY reasons 1–4 (5 free)

P3 gateway per-leaf DAD **reuses CLAIM/DENY** (no new opcode). Only open J item: `wire_version`
(design-spec §9-P5) → a reserved byte-1 nibble *if/when* it lands. **No P1 claim.**

## 6. Q frame (`CONFIG_PULL` request) — P2 design item, no P1 collision

Q distinguishes `REQ_SYNC` by a `dest=0xFF` convention, not a subtype enum. `CONFIG_PULL`'s
*request* carrier (add a subtype byte vs another convention) is a Phase-2 decision. The *answer* is
already reserved (TYPE 6, §2).

---

**Net Phase-1 footprint:** activate `CRYPTED` behaviour; DATA TYPE `5` (reserve `4`); H-flag `0x02`;
KDF string `"MR-E2E-v1"`. Plus the behavioural rule **CRYPTED ⇒ DST_HASH** and the 4→8-B nonce-seed
trailer (layout, not allocation — see the Phase-1 instruction).
