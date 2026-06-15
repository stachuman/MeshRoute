# Join + E2E — Phase 0: Doc Reconciliation + Open Decisions

**Date:** 2026-06-15 · **Status:** ✅ **CLOSED 2026-06-15** — all 12 decisions pinned + the §A reconciliations folded into the two specs (top-of-doc Phase-0 banners) + `frames.md` (Planned wire extensions). **Phase 1 (E2E crypto) is coder-ready.** This doc remains the detailed decision record.

Source: the 3-subagent review (2026-06-15) cross-referenced against the code. Prior belief corrected: **node_id DAD + identity are already BUILT**; the unbuilt work is **(1) E2E DM crypto** and **(2) leaf-membership/config-correctness**. Neither is implementation-ready until the §B decisions are made.

---

## §A — Doc reconciliation (code is truth; apply, no decision needed)

The docs lag the shipped code — fix these so the coder doesn't implement a stale rule:

- **A1. DAD tiebreak is KEY-ONLY.** node-id §6 (rule box) + join §5.3 still say "`claim_epoch` → `key_hash32`". Code: `join_tiebreak_wins` = `my_key < their_key`, `claim_epoch`/`nonce` vestigial (`node_join.cpp:26-28`). → rewrite §6 to key-only; mark `claim_epoch`/`nonce` reserved/inert.
- **A2. Beacon self-echo guard is DONE** (`node_beacon.cpp:260-267`), not a "blocking prerequisite". → drop the "prereq" framing in join §5.5 / node-id §4,§7.
- **A3. Retry has NO jitter** — fixed `join_retry_backoff_ms = 10000` (`node_join.cpp:159`). Doc claims "jitter". → correct (see also decision **G2**: do we *want* jitter?).
- **A4. NV `claim_epoch` is inert** (persisted, never bumped). → doc should say "reserved"; flag for a future NV cleanup.
- **A5. `WANT_PUBKEY`/`PAYLOAD_FLAG_PUBKEY` reference the DELETED payload-flags byte.** The pubkey-resolution wire must be re-specified against the current byte-1 DATA-flags + DATA-TYPE layout → folded into decision **E2**.

---

## §B — Open decisions

### E — E2E DM crypto

**E1 ★ Nonce construction (correctness-critical).**
Issue: `crypto_aead_lock` needs a 24-B nonce; under a *static* ECDH key, any (key,nonce) reuse is catastrophic. The obvious `(source_hash, ctr)` is unsafe (`ctr` 16-bit + per-next-hop → wraps at 65 k msgs). XChaCha20 was chosen *because* it's safe with random 24-B nonces.
Options: (a) carry an N-byte random nonce on the wire (N=8/12/24 — airtime vs collision margin; 12 B → ~2⁻⁴⁸ birthday, ample); (b) derive `nonce = BLAKE2b(source_hash‖dst_key_hash32‖ctr‖rand_carried)[:24]` carrying only ~8 random bytes; (c) persisted per-pair monotonic 64-bit counter (no wire cost, needs per-peer NV).
Rec: **(b)** — carry **8 random bytes**, derive the 24-B nonce, +8 B only.
DECISION: ✅ **Repurpose the existing 4-B DATA `MAC` trailer (currently zero-stubbed) into a CRYPTED-gated 8-B cleartext nonce-seed.** Non-CRYPTED frames keep the 4-B-zero trailer (→ **s18 byte-identical**, no bloat); a CRYPTED frame carries 8 B random there. `nonce[24] = BLAKE2b(rand8 ‖ ctr ‖ dst_key_hash32)[:24]` — **cleartext inputs ONLY** (`source_hash` is sealed → can't be a nonce input; the per-pair ECDH key already binds the sender). 8 B = 64-bit (birthday-safe; defeats the 16-bit-ctr wrap); 4 B would be unsafe. `DATA_MAC_LEN` becomes conditional (4 / 8) on the CRYPTED flag (read from cleartext byte-1 before the trailer). (AEAD **AAD** = the cleartext routing header → see E4.)

**E2 ★ Pubkey-acquisition wire (re-spec against current frames).**
Issue: a sender needs the recipient's `ed_pub` to seal; the old `WANT_PUBKEY` design used the removed payload-flags byte. The whole request→answer→cache loop is undesigned.
DECISION: ✅ **Reuse the H-query → DATA-answer path + cache-on-pass; GATED by a flag.** (1) H query gains `H_FLAG_WANT_PUBKEY` (a free H-flag bit) — set only when the sender intends to encrypt; routing-only resolves (gateway cross-layer, L2c heal) leave it clear → lean 6-B answer. (2) The answer is self-describing by **DATA `TYPE`**: keep `H_ANSWER` (6 B) + `AUTHORITATIVE_H_ANSWER`; ADD `H_ANSWER_PUBKEY` (+ authoritative variant) = `[target_layer 1][node_id 1][ed_pub 32]` (34 B; the **redundant `key_hash32` is DROPPED** — it's `ed_pub[:4]`). (3) **Cache-on-pass:** any node reads the TYPE; if pubkey-bearing, cache `ed_pub` (verify `ed_pub[:4]==`claimed hash), **keyed by FULL pubkey**, **confidence-tiered like `id_bind`** (authoritative owner-answer > overheard) → **also RESOLVES E7** (full-key cache key ⇒ 32-bit collisions distinguishable). TOFU/grind accepted (§0). Byte layout pinned in frames.md.

**E3 Replay protection for sealed DMs.**
Issue: AEAD = integrity, not freshness; a captured CRYPTED DM can be re-injected. Spec is silent.
Rec: **accept-none for v1** (honest-node threat model, §0) + document explicitly; revisit with the app-layer security phase. (A small per-`source_hash` ctr-window is the upgrade path.)
DECISION: ____

**E4 KDF construction.**
Rec: `key = BLAKE2b(shared_point ‖ "MR-E2E-v1" ‖ min(myhash,peerhash) ‖ max(myhash,peerhash))[:32]` — domain-separated + endpoint-bound (so the two directions share one key deterministically).
DECISION: ____ (confirm)

**E5 Forward secrecy.**
Reality: static-static X25519 ⇒ **no FS** (one seed compromise → all past/future DMs). 
Rec: **accept no-FS for v1**, on the record (consistent with §0). Ephemeral/FS is a future app-layer phase.
DECISION: ____ (confirm)

**E6 No-pubkey-known behaviour.**
Rec: **FAIL LOUD** — refuse the send + surface to the app; **never silently fall back to cleartext** (matches the codebase's no-silent-default ethos; cf. `data_sf` removal).
DECISION: ____ (confirm)

**E7 key_hash32 collision for the key cache.**
Rec: cache keyed by **full pubkey**; the existing `DST_HASH` verify-on-delivery + AEAD-auth-fail already *detect* a wrong recipient; on a cached collision, disambiguate via a fresh HARD-H (which returns the full pubkey).
DECISION: ____ (confirm)

### L — Leaf membership / config-correctness

**L1 ★ config_hash canonicalization (+ resolve a doc/code contradiction).**
Issue: §3.4 says hash an **ordered** `data_sf_list` ("do NOT sort") — but the code stores an **order-free `allowed_sf_bitmap` (u16)**. You can't hash an ordered list the config doesn't keep.
DECISION: ✅ Hash the canonical bytes the code stores: `config_hash (u32 LE) = BLAKE2b( u16 allowed_sf_bitmap LE ‖ u32 duty_ppm LE ‖ u8 name_len ‖ leaf_name )[:4]`. Use the **bitmap** — drop "ordered SF list, do NOT sort" (the bitmap == the ascending set `sf_index` already uses; two impls always agree). **Quantize duty → `u32 duty_ppm`** (0.01 → 10000; NOT the raw `double`). **`leaf_name` is a NEW field** (u8-len-prefixed UTF-8; lands Phase 2). v1 keeps the SF **set** (ascending preference); arbitrary SF order = a separate future code change. Byte layout frozen in frames.md.

**L2 lineage_id minting / `leaf create` UX.**
Rec: a `cfg` op (e.g. `leaf create`) mints a random 4-B `lineage_id` + sets the leaf-defining config (epoch 0); joiners adopt it from the beacon/CONFIG_PULL. Document that two operators independently "creating leaf 3" get **two different lineages = two non-interoperating leaves** (safe by design; a UX footgun to surface, not prevent).
DECISION: ____

**L3 CONFIG_PULL carrier.**
Options: (a) a `Q CONFIG_PULL` subtype (request) answered by a routed **DATA `TYPE=CONFIG_ANSWER`** (config body); (b) all-in-Q.
Rec: **(a)** — Q-request + routed-DATA-answer (reuses the routed delivery + the DATA-TYPE enum; mirrors E2's pubkey answer).
DECISION: ____

**L4 Epoch bump semantics.**
Rec: a writer **first syncs max-seen-epoch from beacons**, then `epoch = max_seen + 1`; ties (same epoch, diff hash) resolved by higher `key_hash32` (loser pulls, keeps epoch — no bump war); 2-B wrap handled by the same key-tiebreak. Document.
DECISION: ____ (confirm)

**L5 BCN +10 B leaf header = wire flag-day.**
Reality: adding `{lineage_id(4) ‖ epoch(2) ‖ config_hash(4)}` before the route entries grows every beacon → **breaks the frozen s18 byte-identical keystone** (this phase is *semantic* parity, re-baseline). No BCN version negotiation → hard flag-day (fine per the 3-test-node policy).
Rec: **accept**; write the header before route entries; re-baseline s18 + verify semantic parity for Phase 2.
DECISION: ____ (confirm)

### G — Gaps

**G1 ★ Gateway per-leaf DAD.**
Issue: a dual-layer gateway needs an independent `node_id` per leaf; the node-id design + state are single-leaf — undesigned.
Options: (a) keep gateway node_ids **STATIC** (cfg-set, as today) and defer live per-leaf DAD; (b) design two independent per-leaf claims now.
Rec: **(a)** for v1 — gateways are provisioned anyway (see `configuring-a-gateway.md`); defer live per-leaf DAD until needed.
DECISION: ✅ Static gateway node_ids (provisioned), defer live per-leaf DAD. **Reserved id map: `0` = unprovisioned/no-use · `1`–`16` = GATEWAYS ONLY · `17`–`254` = normal nodes (auto-DAD) · `0xFF` = broadcast/unknown sentinel.** The DAD candidate pick (`join_choose_candidate_id`) MUST exclude `1`–`16` (normal nodes pick from `[17,254]`); a gateway is cfg-provisioned with an id in `1`–`16`. Belt-and-suspenders: `cfg set node_id`/`l1_node_id` on a non-gateway build should reject `1`–`16`. → documented in frames.md Conventions.

**G2 Address-exhaustion retry.**
Reality: designed ("retry on a slow timer") but **not built** — an exhausted joiner gives up permanently (`node_join.cpp:116-121`).
Rec: add a slow-retry timer (ids free as bindings age). Small fix.
DECISION: ____ (confirm)

---

## §C — Verification gap (not a decision, an action)

The DAD sim-gates the spec cites as green — **`t91_node_id_dad_convergence`, `t92_node_id_collision_heal`, `t93` (138-node storm)** — **do not exist in the tree** (no `test/*.json` match). So the shipped DAD's convergence/heal/storm claims (incl. the "21/136 dup" that justified L2c) have **no reproducible artifact**. Action: **recreate them** as part of the join/crypto verification (they're the regression proof for both the shipped DAD and the new work).

---

## Next
1. You mark the `DECISION:` lines (or we talk through the four ★: **E1 nonce, E2 pubkey-wire, L1 config-hash, G1 gateway-DAD** — they have real trade-offs).
2. I fold the decisions + the §A reconciliations into the two specs + `frames.md`, and recreate the §C gates plan.
3. Then the coder starts — **Phase 1 (E2E crypto)** first (smaller, no flag-day), **Phase 2 (leaf membership)** second.
