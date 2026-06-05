# Identity, Leaf Membership & Join — Design Spec

**Status:** DRAFT — cross-checked against the code 2026-06-05 (all cited facts verified:
`leaf_id` nibble, sync word `0x4D`, `beacon_max_bytes=151`, `CRYPTED` bit3, `node.h:465`,
monocypher vendored). Gaps #1–#5 from the cross-check folded in (marked **[xcheck]** below).
Second cross-check (join ↔ H plane) folded into §5.3/§5.5/§7 — surfaced a **blocking**
beacon-guard fix for collision detection. Not yet implemented; Slice A (identity) is ready;
the node_id pass (§5.3/§5.5) is gated on the guard fix.
**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com>
**Date:** 2026-06-05
**Scope:** node identity (keys/name), DM end-to-end crypto, leaf (layer) membership &
config-correctness, beacon protection, and the join process. Supersedes the open
questions in `lib/core/node.h:31-36`.

---

## 0. Threat model — read this first, it justifies every choice below

MeshRoute is an **honest-node** network. The design defends against **accidental
misconfiguration and staleness**, *not* against an active on-air adversary.

- A unique LoRa **sync word (`0x4D`)** already keeps foreign protocols
  (Meshtastic/MeshCore) off our air, so we never interoperate with or verify against
  another protocol. Our identity/crypto is therefore **purely internal**.
- We do **not** defend the broadcast (control) channel against forgery, spoofing, or
  eavesdropping. There is **no beacon signature and no group/channel key.**
- We **do** defend against: a node joining with the wrong leaf config and breaking the
  layer; a stale node re-asserting old config; and (opt-in) keeping **direct-message
  payloads** private end-to-end.

If the threat model ever changes to "malicious authenticated member," the upgrade path
is per-node beacon signatures (§7); nothing here precludes it, but we are explicitly
**not** paying that cost now.

---

## 1. Node identity

### 1.1 Keypair
- One **32-byte master seed** is the root secret. Everything derives from it.
- **Ed25519** (sign/verify) + **X25519** (ECDH) via vendored **monocypher 4.0.2**
  (`lib/monocypher/`).
  - NOTE: monocypher's `crypto_eddsa_*` is **curve25519 + BLAKE2b**, *not* RFC-8032
    SHA-512 Ed25519. This is fine because our use is internal (see §0). RFC-8032 would
    require also vendoring `optional/monocypher-ed25519.{c,h}` — deliberately not done.
  - **[xcheck] Consequence:** these keys/signatures are **not interoperable with stock
    Ed25519 tooling**. Fine while identity is internal — but a future app-level pubkey
    exchange (§1.3) with a phone app using standard Ed25519 would have to switch to the
    RFC-8032 variant. Recorded so it isn't a surprise when the app layer lands.
- Derivation at load:
  - `crypto_eddsa_key_pair(ed_secret[64], ed_pub[32], seed_copy[32])` → signing key + public key.
    (monocypher wipes the seed arg, so pass a copy.)
  - X25519 for ECDH (see §2 for the exact secret derivation; proven by doctest).

### 1.2 key_hash32 — a routing handle, NOT a security anchor
- `key_hash32 = first 4 bytes of ed_pub`.
- It is **32 bits**: a second-preimage (grind a keypair whose pubkey starts with a
  target 4 bytes) costs ~2³² keygens — hours-to-days, feasible. Therefore `key_hash32`
  is used **only** as a routing/resolution convenience. The **full pubkey is the
  identity**; anything trust-bearing keys on the full pubkey.

### 1.3 Name
- Human-readable label, **app-level**, exchanged together with the full pubkey (not on
  the wire hot path). Settable via `cfg set name`.

### 1.4 Storage & lifecycle
- Separate NV record **`/mrid`**: `{ magic, version, name_len, seed[32], name[32] }`.
  The config blob `/mrcfg` is untouched by this. **AS BUILT (device, 2026-06-05):**
  `ed_pub` is **not** stored — the seed is the single source of truth, so boot DERIVES
  `ed_pub`/`key_hash32`/`x_*` via `identity_from_seed` (a stored pubkey could never disagree
  with the seed). `src/device_nv.h` `IdBlob` + `load_id`/`save_id` (LittleFS `/mrid` / NVS key `id`).
- **First boot:** if `/mrid` absent → generate seed from **hardware RNG** (`src/device_rng.h`:
  nRF52 `NRF_RNG` / ESP32 `esp_random()`), derive, persist. **DONE** — `fw_main` boot now
  loads-or-mints `/mrid` and calls `set_identity(node_id, g_identity.key_hash32)`, retiring the
  `key_for(id)` placeholder. (HW-RNG + flash are bench-verified by the user; the crypto is now
  genuinely linked into the firmware — XIAO Flash 169→201 KB.)
- **`regen`**: mint a fresh seed → new keypair → new `key_hash32`, persisted, node re-seeds its
  self-binding (keeps name + node_id). Console `regen` command — **DONE** (`do_regen`).
- **§1.4 PARITY NOW COMPLETE:** both backends derive `key_hash32 = ed_pub[:4]` via the same
  `identity_from_seed` — the sim from a scenario seed (Slice A2), the device from the `/mrid`
  HW-RNG seed (this slice). Only the seed *source* differs (determinism vs randomness), by design.
- **[xcheck] The seed is the single identity source on BOTH backends.** Today the sim
  injects `key_hash32` as a literal via node config (`node.cpp:36`; scenarios pass e.g.
  `0xAAAAAAAA`). That seam is replaced: a sim node config carries the **32-byte seed**
  (or a short deterministic seed expanded to 32 B), and `FirmwareNode` runs the *same*
  `identity` derivation as the device, so `key_hash32 = ed_pub[:4]` in both. No literal
  `key_hash32` path remains — sim and device cannot diverge on identity, and the sim
  keeps determinism by fixing the seed per node in the scenario JSON. (Sim seeds stay
  small/explicit; the HW-RNG path is device-only.) **This replacement is a SCOPED BREAKING
  CHANGE (Slice A2, §8): it changes every sim node's `key_hash32` from its literal, so the
  H-plane suite and any `key_hash32`-asserting scenario migrate with it — it is NOT part of
  the zero-disruption Slice A.**

### 1.5 Console surface — **DONE** (`fw_main`, device console)
- `cfg set name <str>` — set the node name (persisted to `/mrid`, separate from the `/mrcfg` config blob).
- `regen` — generate a new identity (new keypair). Boot + `regen` print `key_hash32` (hex) + name.

---

## 2. Direct-message end-to-end crypto (the ONLY on-air crypto)

The single cryptographic feature on the air: a sender that **knows the receiver's
pubkey** may encrypt the DATA payload end-to-end.

- **Scheme:** X25519 ECDH(our X25519 secret, receiver's X25519 pubkey) → shared secret
  → KDF (BLAKE2b) → **XChaCha20-Poly1305 AEAD** (`crypto_aead_lock`) over the DATA body.
- **Pubkey conversion:** the receiver advertises only its `ed_pub`; the sender derives
  the receiver's X25519 pubkey via `crypto_eddsa_to_x25519(ed_pub)`. Our own X25519
  secret derives from our seed (the EdDSA scalar).
- **[xcheck] Derivation proof = a KAT, not just a round-trip.** The native doctest must
  pin the shared secret against a **known-answer vector** (monocypher's own test vectors),
  not only assert `A·B == B·A`. A symmetric round-trip passes even if both sides derive
  the *wrong* secret the same way; the KAT proves the derivation is *correct*. Gate this
  before any device wiring.
- **Flag:** reuses the existing DATA inner `CRYPTED` payload-flag (bit 3).
- **[xcheck] Guarantee (don't over-read "E2E"):** confidentiality against **passive**
  eavesdroppers / accidental cross-talk only — **not** MITM-secure. Pubkey resolution is
  TOFU over `H` (§7.1), so an active on-air node could answer first with its own pubkey.
  That is out of scope per §0 (no active adversary); stated here so the property is exact.
- **Dependency:** the sender must hold the receiver's **full pubkey**. `id_bind` today
  stores only `key_hash32 → node_id` and is unauthenticated TOFU — resolved via a sparse
  peer-key cache + a modified `H` lookup that returns the full pubkey (TOFU). See §7.1.
- **[xcheck] Use a HARD `H` query for pubkey resolution** (§5.5): you want the *owner's*
  authoritative key, not a stale claimed/snooped one — a hard query reaches the owner.
- **No** group key, **no** public-channel encryption. Channel/broadcast traffic is
  cleartext.

---

## 3. Leaf (layer) membership & config correctness

### 3.1 The problem
- `leaf_id` is a **4-bit on-air nibble** (`node.h:58`, byte0 low nibble) used as a
  same-layer filter. It is not unique and carries no config.
- **Failure to prevent:** a node with the same `leaf_id` but a *different*
  `data_sf_list` joins without syncing → the RTS/CTS/DATA SF negotiation silently
  breaks → the layer is "destroyed" (failed handshakes, wasted airtime). This is an
  **honest misconfiguration**, not an attack.

### 3.2 Mechanism — lineage + epoch + config fingerprint
A leaf instance is identified by an immutable **`lineage_id`** and an ordered, mutable
config tracked by **`epoch`** + **`config_hash`**.

| Field | Size (proposed, tunable) | Meaning |
|---|---|---|
| `lineage_id` | 4 B random | Minted once at leaf **creation**, immutable. Identifies *this* leaf instance's config family. Also disambiguates 4-bit `leaf_id` collisions. |
| `epoch` | 2 B | LWW counter; bumped on each deliberate config change (§4). |
| `config_hash` | 4 B | BLAKE2b (truncated) of the **canonical serialization** of leaf-defining config — chiefly `data_sf_list`. Divergence detector. |

The **beacon carries `{lineage_id, epoch, config_hash}`** (~10 B; ≈ +26 ms airtime at
SF8/BW125/CR4-5 at 2.56 ms/byte — negligible).

**[xcheck] Wire-order constraint:** the leaf header is written into the beacon **before
the route entries**. Route entries are already paged/truncated to fit `beacon_max_bytes`
(151 B); the leaf header must not be the thing that gets dropped, so a route-rich node
still advertises its lineage/epoch even when its route list is truncated. `pack_beacon`
emits: cmd byte → leaf header → schedule → route entries → seen-bitmap → ext TLVs.

### 3.3 Peering / adoption rules
On receiving a beacon with matching `leaf_id`:
- **Different `lineage_id`** → not my leaf. Ignore (it's a different/foreign leaf;
  self-isolating, harmless).
- **Same lineage, same `(epoch, config_hash)`** → peer normally.
- **Same lineage, higher `epoch`** → I am stale → **pull** the full config (extend
  `Q REQ_SYNC`, `node_query.cpp`) → adopt `{config, epoch}` → my beacons now carry the
  new epoch → it propagates.
- **Same lineage, lower `epoch`** → that neighbor is stale; ignore (it will heal when it
  hears a higher epoch).
- **[xcheck] Same lineage, *same* `epoch`, *different* `config_hash`** → the rare
  concurrent-write collision of §4.1 (two operators both wrote `epoch = max+1`), or a
  propagation race. Epoch can't break the tie, so the **deterministic tiebreak of §4.1**
  decides: the config from the **higher `key_hash32`** is canonical; the lower-`key_hash32`
  node **pulls + adopts it, keeping the same `epoch`** (it does NOT bump — bumping would
  start an epoch war). Stable comparison ⇒ one-sided, converges, no flapping. Do **not**
  peer on the diverging config while it persists, and **log it** — if it does not clear
  within a few beacon intervals it signals a config write that failed to bump `epoch`
  (a bug to surface).

**Self-isolation property:** a misconfigured or stale node is either a different lineage
(filtered) or a lower epoch (filtered + auto-pulls the correct config). It can never
corrupt a healthy leaf, and it cannot propagate its config without a deliberate operator
write (§4).

### 3.4 Canonical config hash
- `config_hash = truncate(BLAKE2b(canonical_bytes(leaf config)), 4)`.
- **Leaf-defining fields (the hashed set) — LOCKED**, in this fixed order, each length-prefixed:
  1. `data_sf_list` — **order is significant** (reserved for future SF-priority use):
     serialize **as configured, do NOT sort**.
  2. `leaf_name` — human label *for the leaf* (UTF-8). NOTE: distinct from the per-node
     `name` in §1.3; this one is part of leaf config and set at leaf creation.
  3. `duty_cycle` — the leaf-wide airtime/duty policy (all members share one budget, so
     it must agree leaf-wide).
- PHY params (control SF/BW/CR/freq) are **not** hashed — implicitly matched (you can't
  demod the control channel otherwise). Local params (LBT/beacon timing/route-cap) are
  not leaf-defining and stay out.
- Hash = `BLAKE2b(canonical_bytes)` truncated to 4 B. Honest-only ⇒ a 4-B collision just
  means a missed mismatch (rare, benign).

---

## 4. Dynamic config management

Config is **static by default, occasionally operator-nudged, recreated on catastrophe.**
No authority, no leader, no handoff, no recovery state machine.

### 4.1 Writes (changing live config)
- A config change happens **only via an explicit operator console command** on a real
  node (e.g. `cfg set ...`). This operator-command gate is the "deliberate intent"
  marker that prevents a merely-misconfigured node from ever propagating its config.
- A write sets `epoch = max_seen_epoch + 1`, keeps the same `lineage_id`, recomputes
  `config_hash`. **LWW**, tiebroken by the **stable `key_hash32`** (**[xcheck]** NOT the
  disposable `node_id` — a renumber must not change config-write precedence; this is the
  same stable handle §5.3 uses for node_id healing) for the rare concurrent case (§3.3).

### 4.2 Reads (adopting current config)
- Pull-on-stale per §3.3. **Join is the epoch-0 / no-lineage case of this same flow**
  (a fresh node is simply maximally stale — see §5).
- **Durability:** the beacon carries only the hash; the config *body* lives in every
  node that has pulled it, so any node at epoch N can serve the pull. The config
  survives its originator leaving, as long as it reached ≥1 neighbor.

### 4.3 Catastrophe backstop (replaces authority/handoff)
- If a leaf becomes unchangeable (founder gone) or wedged: **abandon the lineage, found
  a NEW leaf** (operator `create` → fresh `lineage_id` + config) and **manually
  re-point** nodes at it. The dead lineage stops being beaconed and ages out of route /
  `id_bind` tables on its own.
- **No automatic migration.** A partition looks identical to leaf-death; auto-defection
  would shatter a merely-split leaf. Re-pointing is a deliberate operator action.

### 4.4 NV
- Persist `{ lineage_id, epoch, config }`. `epoch` monotonic on any node that writes.
  A non-writer that forgets just re-learns from beacons (self-healing). Use
  epoch-in-NV + RAM-within-epoch if per-something counters are added later.

---

## 5. Join process

### 5.1 Preconditions
- A joiner knows only **freq / BW / control SF** (enough to hear the control channel).
  It does **not** know `data_sf_list`, `lineage_id`, or its `node_id` yet.

### 5.2 Flow
1. **Discover:** listen for beacons on the control channel; observe `{leaf_id,
   lineage_id, epoch, config_hash}` of the intended leaf. (Which leaf to join, on a
   `leaf_id` collision, is operator-told or first-heard — join UX detail.)
2. **Config sync:** pull the full leaf config (data_sf_list, …) for that lineage/epoch
   (extends `Q REQ_SYNC`). Adopt it → the joiner's fingerprint now matches → it is a
   member.
3. **Address (node_id) assignment — automated & self-healing (see §5.3).** The user
   **never** sets `node_id` — amateurs would collide constantly. The firmware
   auto-assigns a conflict-free id and heals collisions itself. Identity is the
   pubkey / `key_hash32`; `node_id` is a disposable routing address (`node.h:465`
   reassignable), so renumbering is harmless.
4. **Wire-compat check:** JOIN carries a **1-byte wire version** (`node.h:35-36`) =
   *wire* compatibility (can we parse each other's frames), **distinct** from *config*
   compatibility (lineage/epoch). Incompatible wire version → reject the peer.
   - **[xcheck] Known limitation:** the version is checked at JOIN, but a node parses a
     neighbor's **beacon** before it ever joins — so a wire-incompatible neighbor's
     beacon may misparse at first contact, before the JOIN gate. Mitigated by the sync
     word (`0x4D`) + the `leaf_id`/lineage filter + treating any wire bump as a flag-day.
     Carrying the version in the beacon header is a possible future hardening (not now).

### 5.3 node_id auto-assignment (CRITICAL — dedicated design pass pending)
**Why it's critical:** the leaf is joined by amateurs who don't understand the
mechanism. Assignment must be **zero-touch and self-healing**, over **lossy links** and
**partitions**, into a space of **254 usable ids** (`0x01`–`0xFE`; `0x00` = the unprovisioned
sentinel, `0xFF` reserved — `node.cpp:28` panics on it).

**Agreed direction — Duplicate-Address-Detection + heal (SLAAC/AODV-style):**
1. **Candidate:** reuse the NV-remembered id if present; else a first guess derived from
   `key_hash32` (stable, usually free) and/or a random free slot learned passively from
   beacons (`(node_id, key_hash32)` pairs are already on the air).
2. **Claim/probe (DAD):** announce "`key_hash32` K claims id X"; listen a few beacon
   intervals for an objection (id X held by a *different* `key_hash32`, or a competing
   claim).
3. **Adopt:** no objection → take X, persist NV, beacon as X. Objection → next free slot.
4. **Heal (the real guarantee):** probes are lost, nodes sleep, partitions merge — so a
   same-id / different-`key_hash32` conflict *will* still occur. The detector is a beacon
   whose `src == _node_id` but whose `key_hash32 != _key_hash32` (someone is beaconing as
   me) → emit `addr_conflict_observed` → the **DAD tiebreak (THE one canonical rule, §9 and
   frames.md P5 reference this)** makes exactly **one** side **renumber once** to a free slot
   and re-announce:
   > **Static, symmetric comparison only** — both sides read the same wire-carried values
   > and reach the same verdict: an established-holder bias via the **static, wire-carried
   > `claim_epoch`** (the holder with the senior claim keeps the id), then the **`key_hash32`
   > as the final deterministic tiebreak** (lower `key_hash32` yields). **Live
   > `lease_age_seconds` is NOT a primary key** — it is time-varying and evaluated
   > asymmetrically by each side, which can produce mutual-yield/mutual-keep flapping; it
   > stays informational at most. (The exact `claim_epoch` seniority direction is finalized
   > in this §5.3 dedicated pass; the *structure* — static primary → `key_hash32` final, no
   > live `lease_age` — is fixed.)

   Stable comparison ⇒ no flapping; upper layers re-bind by `key_hash32`.
   - **[xcheck] Detection requires a beacon-guard fix — see §5.5.** Today
     `node_beacon.cpp:203` drops *any* beacon with `src == _node_id` as a self-echo, which
     **silently swallows this exact collision**. The guard must narrow to a *true* echo
     (`src == _node_id && key_hash32 == _key_hash32`); the same-id/different-hash case is
     the collision, not an echo. The event is `addr_conflict_observed` (the **same-id /
     different-hash** path, `node_hashlocate.cpp:63`) — **not** the silent
     `one-hash→one-id` eviction (that is the *rejoin* healer, a different path).

**Hard points to nail in the dedicated pass:**
- **254-slot space.** Birthday collisions get likely past ~16 nodes, so **healing — not
  probing — is the correctness guarantee** (probing only cuts churn). This also **bounds
  leaf size**; define exhaustion behavior (refuse join / "leaf full").
- **Lossy / asleep / partition:** DAD alone is insufficient; the beacon-driven heal is
  the backstop and must converge without ping-pong.
- **NV churn:** persist the id but rate-limit writes (epoch/RAM pattern) to spare flash.

### 5.4 What join is NOT
- **No cryptographic authorization.** Possession of an identity key is **not** required
  to join (honest-node model). The identity keypair (§1) is **independent of join** — it
  exists for DM E2E and node identity/name, not for admission.
- `J` frames already exist in `frame_codec.cpp` as the handshake substrate.

### 5.5 Relationship to the H (hash-locate) plane — [xcheck]

Join/`node_id` assignment and the existing **H plane** are two operations on the **same
`id_bind` table**; this section reconciles them so the join slice reuses the H machinery
instead of duplicating it.

**Two orthogonal axes (don't conflate them).** The H plane has:
- a **query variant** — **soft** (consult the cache; *anyone who holds the binding
  answers*) vs **hard** (skip the cache; *only the owner answers* — verify-on-use);
- an **answer authority** — **claimed** (second-hand: snooped/relayed) vs **authoritative**
  (first-hand: the owner's own beacon, or the owner answering directly).

A *soft* query may return *claimed or authoritative*; a *hard* query returns *only
authoritative*. "Which `node_id` for this `key_hash32`, and how much do I trust it" is
(variant, authority) — both matter below.

**Directionality — DAD does NOT use an H query.** `H` is **hash-keyed**: `handle_h`
floods on `key_hash32` and answers *"which id does this hash use"* (hash → id). DAD asks
the **dual** — *"who holds id X"* (id → hash) — and there is **no id-keyed flood**. So DAD
must **not** be wired onto the H frame. It works off the `(src=node_id, key_hash32)` pair
that rides **every beacon** (passive observation, §5.3 step 1) plus the **self-collision
guard** (§5.3 step 4 / the `node_beacon.cpp:203` fix). The hash-keyed H flood stays for
hash → id resolution only.

**Confidence gates the heal (anti-flap).** Renumber **only** on an **authoritative**
collision — a *first-hand* beacon (`src == _node_id`, different hash) proving another node
is *live* on your id. **Never** renumber on a **claimed** (snooped/relayed) rumor, which
can be stale → otherwise two nodes ping-pong on gossip. Detection at beacon ingest is
first-hand by construction, so this falls out — but the tiebreak consumes the binding
**confidence**, not just `key_hash32` ordering.

**Synergies the join slice should reuse (free):**
1. **Rejoin = a SOFT self-`H` query.** A returning node floods `H` for its **own**
   `key_hash32`; a neighbor still caching `(myHash → oldId)` answers (claimed, but it
   reflects the old id) → recover the old `node_id` from the *network's* memory, not only
   NV, then run the DAD probe on it. It must be **soft** — a *hard* query is answered only
   by the owner, and the owner is the amnesiac rejoiner itself.
2. **E2E pubkey resolution = a HARD `H` query** (see §2 / §7.1). You want the *owner's*
   real current key; a soft/claimed answer can be stale → you'd encrypt to the wrong key.
3. **The renumber transient needs no new machinery.** While a node heals (renumbers), its
   `(hash → id)` binding is briefly stale network-wide. `send_by_hash` already
   **HARD-verifies a soft/claimed binding before use** (verify-on-use), so a DM addressed
   by hash self-corrects to the post-heal id.

**`id_bind_set` self-safety.** With the §5.3 guard fix, a peer's conflicting beacon for
*our* id is branched to the DAD heal **before** `id_bind_set`, so it never reaches the
`authoritative → overwrite` branch (`node_hashlocate.cpp:66`) that would otherwise rewrite
our own identity row. (A conflict between two *other* nodes still resolves normally inside
`id_bind_set`.)

---

## 6. Explicit non-decisions (recorded so they aren't re-litigated)

| Considered | Rejected because |
|---|---|
| Per-node **beacon signatures** (Ed25519) | +64 B/beacon ≈ +164 ms at SF8 (≈42% of the 151 B beacon page budget); no adversary to justify it. |
| Per-layer **group/channel key** | Public-channel privacy is not required; DM E2E (§2) covers private needs. Group key adds rekey-on-revoke pain for no benefit here. |
| Pairwise MAC on beacons | Flat, flooded, route-diverse mesh ⇒ a beacon is consumed by many neighbors; pairwise verification needs O(N) keys or a group key. Unavailable. |
| Persistent **leaf authority + handoff** | Maintaining/transferring it is itself a leaderless election — no net gain. Replaced by lineage + operator-gated LWW + leaf-death-recreate backstop (§4.3). |
| `key_hash32` as a trust anchor | 32-bit, grindable (§1.2). Routing-only. |

---

## 7. Open items / dependencies

1. **`id_bind` + pubkey resolution (E2E) — RESOLVED (approach):** keep `id_bind` as the
   routing table (32-bit hash → id); add a small **sparse peer-key cache** (~16–32
   entries) populated **on demand** via a **modified `H` request that returns the full
   pubkey** (TOFU trust). Pubkey never rides every beacon. **[xcheck] Use a HARD `H` query**
   (§5.5) so the pubkey is the owner's authoritative key, not a stale claimed/snooped one.
   Detailed in the E2E slice.
2. **node_id auto-assignment — DEDICATED DESIGN PASS (critical).** Direction agreed
   (§5.3 + §5.5: DAD + self-heal + NV, `key_hash32` = stable identity, reuses the H plane's
   `id_bind`/confidence model). **[xcheck] The pass MUST land the `node_beacon.cpp:203`
   self-echo-guard fix** (narrow to `src==me && hash==mine`) — without it the collision is
   undetectable (§5.3 step 4 / §5.5). Full allocation / tiebreak / exhaustion / convergence
   design is the next deep dive, **before** the join slice.
3. **Leaf-defining fields — LOCKED (§3.4):** `data_sf_list` (order-significant, not
   sorted) + `leaf_name` + `duty_cycle`.
4. **Beacon format change — ACCEPTED:** +10 B (`lineage_id` 4 / `epoch` 2 /
   `config_hash` 4), written **before route entries** (§3.2 [xcheck] wire-order). The BCN
   wire format will be revised to carry this leaf header.
5. **X25519 secret derivation — handled in Slice A** (EdDSA scalar from seed; proven by a
   **known-answer-vector doctest** (§2 [xcheck]), not merely the A·B==B·A round-trip).
6. **[xcheck] Identity source seam — DECIDED (design), lands in Slice A2:** the 32-byte seed
   is the single identity source on both backends; the sim injects a per-node seed and runs
   the device derivation (no literal `key_hash32` path). A **scoped breaking change** (incl.
   H-suite migration), **separate from the zero-disruption Slice A**. §1.4 / §8.2.

---

## 8. Sequencing (slices)

1. **Slice A — pure identity module (NOW, genuinely zero-disruption):**
   `lib/core/identity.{h,cpp}` (seed → Ed25519/X25519, `key_hash32 = ed_pub[:4]`, name) +
   the device HW-RNG seam + `/mrid` NV + `cfg set name` / `regen` + native doctest (keygen
   determinism, `key_hash32`, **ECDH known-answer vector** (§2 [xcheck]), not just the
   round-trip). **Self-contained — touches no sim/scenario, asserts only in its own doctest.**
2. **Slice A2 — sim identity seam.** A sim node may carry an identity **`seed`**; the harness
   derives `key_hash32 = ed_pub[:4]` via `lib/core/identity` (the SAME derivation as the device)
   and feeds the derived value to **both** engines. **AS BUILT (2026-06-05, capability only):**
   `lib/core/identity.cpp` + monocypher wired into the sim's `meshroute_core` (CMake `LANGUAGES C CXX`);
   `JsonConfig` parses `seed` (crypto-agnostic); `SimController` derives into a mutable
   `_resolved_key_hash32` (const `_cfg` untouched) consumed by both the `FirmwareNode` ctor and the
   Lua `registerNode` — **single source ⇒ both engines get the identical value** (verified: Lua-alice
   and meshroute-bob both see `0xf2e6f8d4` for `seed={1..32}`, the lib/core golden). Gate
   `test/t90_identity_seed_derivation.json`. The **literal `key_hash32` stays as a transitional
   fallback** — existing scenarios are UNCHANGED (full t-suite 79/85, no new regressions).
   **DEFERRED (user: "do not fix all scenarios"):** the bulk `key_hash32`→`seed` migration of the
   46 legacy scenarios + 38 value-asserting ones, and removing the literal path (the "no literal
   path remains" end state). New/E2E scenarios use `seed` now.
3. **E2E DATA crypto** — depends on A2 + (7.1) `id_bind` pubkey resolution.
4. **node_id auto-assignment design pass** (§5.3) — the critical deep dive, before join.
5. **R6 join + beacon fingerprint** — `lineage_id`/`epoch`/`config_hash` in the beacon,
   peering filter, config pull/adopt, node_id auto-assignment.
6. **Dynamic config write path** — additive on top of (5): operator-gated `epoch` bump.

**Slice A alone is "ready now"** — it depends on nothing and disrupts no scenario. A2 (the
sim seam + migration) and 3–6 follow.

---

## 9. Wire-format impact — [xcheck]

Byte-level layouts live in **`docs/frames.md` → "Proposed changes (DRAFT 2026-06-05)"**
(P1–P5). This section is the summary + the decisions that are now settled vs still open.
**Headline: no genuinely new frame is needed** — the one hot-path cost is the beacon
`+10 B`; the node_id DAD reuses the existing `J CLAIM`/`DENY`.

| Frame | Change | When | Cost |
|---|---|---|---|
| **BCN** (P1) | +10 B leaf header `{lineage_id 4, epoch 2, config_hash 4}`, a **fixed field before route entries** (survives 151 B truncation; *not* an ext-TLV). `key_hash32` already present. | every beacon | **+10 B** |
| **DATA** (P2) | new `CRYPTED` inner: `origin` sealed inside the AEAD (sender hidden), `+16 B` Poly1305 tag, nonce derived. | opt-in DM | +16…+40 B |
| **H / hash-bind** (P3) | H query `WANT_PUBKEY` flag (b1); hash-bind answer appends `ed_pub[32]` (`PAYLOAD_FLAG_PUBKEY`); **HARD** query for authoritative key. | on-demand unicast | answer +32 B |
| **Q** (P4) | new `CONFIG_PULL` subtype + a routed-DATA config-transfer (`PAYLOAD_FLAG_CONFIG`). | join / config change | new body ~20–40 B |
| **J** (P5) | add `wire_version`; DAD = existing `CLAIM`/`DENY`. | rare | +0 B (rsv) or +1 B/opcode |

**Decisions settled (this review — in scope for identity/leaf/join):**
- **Keep `J DISCOVER`** (active solicit) alongside passive beacon-listen — **re-evaluate in simulation**.
- **DAD tiebreak — the one canonical rule is §5.3 step 4** (static, wire-carried `claim_epoch`
  → `key_hash32` final; live `lease_age` is *not* a primary key). §9 does not redefine it.
- **Leaf header is a BCN *header* field** (before entries), not a trailing ext-TLV.

**Forward-references to a future E2E / CRYPTED slice — PROPOSED, pending code verification +
user ratification (NOT decided here).** These surfaced only because `origin`-privacy was
raised; they are **data-plane** changes that belong to the E2E slice, not this identity/leaf/
join design, and one rests on a code reading still to be confirmed:
- **CRYPTED encrypts `origin` too** (metadata privacy). Impact *as read from the code so far*
  (frames.md P2): anti-spam appears UNAFFECTED (keyed on the prev-hop `from`); loop *safety*
  appears UNAFFECTED (`hops_remaining` TTL); only the origin-keyed **LOOP_DUP** early-dup is
  lost. **Premise to verify:** that `visited[6]` is inert today (grep-indicated, contradicts
  an earlier note — confirm before relying on it).
- **PROPOSED — activate `visited[]` as the loop guard** to restore cross-path detection
  origin-independently (+0 wire), superseding the `sokey` LOOP_DUP; **no `from` fallback**
  (unsafe). *This changes data-plane loop safety — do not treat as decided until verified +
  ratified in the E2E slice.*
- **If CRYPTED is built, the required code changes** (frames.md P2 MUST-FIX): CRYPTED-aware
  `parse_unicast_inner`, relay skips origin dedup, activate `visited[]`, dm_delivery tolerant
  of origin-unknown.

**Still open (record before the relevant slice):**
- CRYPTED **nonce**: derive (recommended, +0 B) vs carry 24 B.
- CRYPTED **tag vs the legacy 4 B MAC** trailer: fold to tag-only vs keep both.
- **`J wire_version` width**: 4 rsv bits (+0 B) vs a full byte.
- **Config-transfer carrier**: the routed-DATA `PAYLOAD_FLAG_CONFIG` proposal (P4) vs a Q response body.
- **`J OFFER`** fate: narrow its use vs drop (its role is superseded by P4 + DAD).
