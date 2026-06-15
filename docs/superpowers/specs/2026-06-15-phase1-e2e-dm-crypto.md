# Phase 1 — E2E Direct-Message Crypto — Coder Instruction

**Author:** Stanislaw Kozicki <cgpsmapper@gmail.com>
**Date:** 2026-06-15 · **Status:** INSTRUCTION for the coding agent · quality-gated after.

**Goal:** let a sender that holds the recipient's **authoritative pubkey** seal a DM payload
end-to-end (X25519 ECDH → BLAKE2b KDF → XChaCha20-Poly1305). Opt-in, default OFF → **s18 stays
byte-identical**. The primitives already exist (`identity.cpp`: `ecdh_shared`, `ed_pub_to_x25519`,
`key_hash32_of`; monocypher: `crypto_aead_lock/unlock`, `crypto_blake2b`); this slice wires them
into the DATA path + the pubkey-resolution wire.

**Threat-model scope (design-spec §0/§2 — state it, don't over-claim):** confidentiality of the DM
**payload** against **passive** eavesdroppers only. Pubkey resolution is **TOFU over a HARD H query
→ NOT MITM-secure**. **No forward secrecy** (static-static X25519). **No replay protection** (v1).
`origin` and the routing header stay **cleartext** (the relay loop-dedup `_seen_origins` keys on
`(origin,dst,ctr)` and `visited[]` was removed — sealing `origin` would break LOOP_DUP with no
fallback; sender-metadata privacy is explicitly **declined**).

## Locked decisions (Phase 0 + the wire-surface reservation + 2026-06-15 review)

- **E1 nonce:** repurpose the 4-B DATA `MAC` trailer → a CRYPTED-gated **8-B cleartext nonce-seed**
  (`rand8`); `nonce[24] = BLAKE2b(rand8 ‖ ctr(2 LE) ‖ dst_key_hash32(4 LE))[:24]` — cleartext inputs
  only. Non-CRYPTED keeps the 4-B-zero trailer.
- **E4 KDF + AAD:** `key[32] = BLAKE2b("MR-E2E-v1" ‖ shared32 ‖ min(myHash,peerHash) ‖ max(..))[:32]`
  (both directions derive one key). **AAD = the cleartext routing-header bytes** of the inner.
- **E3 replay = none (v1); E5 FS = none; E6 no-pubkey = FAIL LOUD (never cleartext).**
- **E2/E7 pubkey wire:** H `WANT_PUBKEY` (0x02, set with HARD) → owner answers DATA **TYPE 5
  `AUTHORITATIVE_H_ANSWER_PUBKEY`** = `[target_layer 1][node_id 1][ed_pub 32]`. Cache keyed by
  `key_hash32`, **full `ed_pub` stored** (32-bit collision detectable). Authoritative **survives
  relay** (cache-on-pass). TYPE 4 reserved, not emitted v1.
- **DP1 seam:** `Node::set_crypto_identity(x_secret[32], ed_pub[32])`.
- **DP2 layout:** seal `{source_hash? + location? + body}`; cleartext `{dst_key_hash32 + layer-path?
  + origin}` = AAD; **CRYPTED ⇒ DST_HASH**; 16-B tag at inner end; trailer 4→8 B = nonce-seed.

---

## 1. Local crypto identity → `Node` (DP1)

- **`Node::set_crypto_identity(const uint8_t x_secret[32], const uint8_t ed_pub[32])`** (declare near
  `set_identity`, `node.h:286`). Store copies at **Node level** (identity is global — a gateway
  shares one identity across both layers; design-spec §1 / `configuring-a-gateway.md` "Identity is
  shared"): `uint8_t _x_secret[32]; uint8_t _ed_pub[32]; bool _crypto_ready=false;`.
- **`fw_main`** calls it after the `/mrid` load (near `:850`, where `g_identity` is derived +
  `set_identity(node_id, g_identity.key_hash32)` runs).
- **`SimController`** calls it from the per-node derived `Identity` (the Slice-A2 seam already derives
  one — feed `x_secret`/`ed_pub` through the `FirmwareNode` ctor).
- Sealing/opening when `!_crypto_ready` → **fail loud** (never silently skip encryption).

## 2. Crypto core (`frame_codec` or a new `dm_crypto.{h,cpp}` in lib/core)

Keep it platform-neutral (monocypher only). Two helpers + the KDF/nonce:

```
// key = BLAKE2b("MR-E2E-v1" ‖ shared ‖ min(a,b) ‖ max(a,b))[:32], a/b = the two key_hash32 (u32 LE bytes)
void dm_kdf(uint8_t key[32], const uint8_t shared[32], uint32_t my_hash, uint32_t peer_hash);
// nonce = BLAKE2b(rand8 ‖ ctr(2 LE) ‖ dst_key_hash32(4 LE))[:24]
void dm_nonce(uint8_t nonce[24], const uint8_t rand8[8], uint16_t ctr, uint32_t dst_key_hash32);
```

Seal/open wrap `crypto_aead_lock/unlock` (16-B tag out/in, 24-B nonce in, AAD = cleartext header).
`crypto_aead_unlock` returns non-zero on tag failure → the caller MUST treat that as a hard drop.

**`[:N]` convention = BLAKE2b-512 then truncate.** Both helpers compute the full 64-B
`crypto_blake2b(out, 64, …)` digest and take the first N bytes (`[:32]` key, `[:24]` nonce) — matching
`identity.cpp`'s precedent and the `[:4]` `config_hash`. **NOT** parameterized BLAKE2b-256
(`crypto_blake2b(out, 32, …)`), which mixes the length into the IV → different bytes. The KAT pins it.

## 3. CRYPTED wire layout (`frame_codec.{h,cpp}`)

- **`DATA_MAC_LEN` becomes conditional:** add `inline size_t data_mac_len(uint8_t flags){ return
  (flags & DATA_FLAG_CRYPTED) ? 8 : 4; }`. Replace the literal `DATA_MAC_LEN` uses in `pack_data`
  (`:635-656`), `parse_data` (`:661,685,693-694`), `data_mac` (`:703-705`) with `data_mac_len(flags)`.
  Non-CRYPTED path is unchanged → byte-identical.
- **`pack_data`:** reject `CRYPTED && !DST_HASH` (return 0). When CRYPTED, the trailer is the 8-B
  `rand8` (extend `in.mac` to carry 8, or add `in.nonce_seed`); the 16-B tag rides **inside `in.inner`**
  (the sealed inner = cleartext-header ‖ ciphertext ‖ tag — packed by the seal step, §4).
- **`parse_data`:** `o.crypted` already decoded (`:670`); use `data_mac_len(o.flags)` for `inner_len`
  / `mac_off`; expose the 8-B seed span (a `data_nonce_seed(frame,d)` helper) when crypted.
- **★ Cross-file MAC-size site (a `DATA_MAC_LEN` grep will NOT find this — different file, literal `4`):**
  `node_mac.cpp:477` announces the DATA size in the RTS as `rin.payload_len = pt.inner_len + 4 /*MAC_LEN*/`
  → must become `pt.inner_len + data_mac_len(flags)`. Miss it and a CRYPTED DM's RTS under-announces by
  4 B → the CTS-sized RX / NAV window (`nav_duration_cts:518`, `payload_len + 13`) comes up 4 B short →
  the 8-B nonce-seed trailer is truncated → **every CRYPTED DM fails to open**. Sweep for any other
  literal `+ 4` / `MAC_LEN` DATA-size math outside `frame_codec.cpp` (same RTS-payload-len footgun class
  as the location M-frame point).
- **Inner split:** the cleartext AAD region = everything `parse_unicast_inner` reads **up to and
  including `origin`** (`dst_key_hash32?` + `layer-path?` + `origin`); the sealed region = `source_hash?`
  + `location?` + `body`. `parse_unicast_inner` must, when `CRYPTED`, stop at `origin` and hand the
  rest (ciphertext+tag) to the open step rather than reading `source_hash`/`location`/`body` raw.

## 4. Seal-on-send (`node_mac.cpp` `enqueue_data` :52 → the pack/`do_data_tx` path ~:236+)

- **Encrypt intent:** a per-node `cfg` toggle `e2e_dm` (default **off**) gates whether app DMs
  originate CRYPTED (mirror `loc_in_dm`). Only set on **origination** (never relays/acks/forwards).
- When encrypting: set `CRYPTED | DST_HASH`; resolve the recipient's **authoritative** pubkey (§5) —
  on a miss, **park the send** and DO NOT transmit; pull `rand8` from a **NEW crypto-RNG HAL seam**
  (recon 2026-06-15: `hal.h:103` has ONLY `rand_range` — a *software* mt19937 `seed_rng`'d from boot
  `millis ^ key_hash32` on device, fine for jitter but **crypto-weak for a static-key nonce**; the real
  device entropy `mrrng::fill` in `device_rng.h` is NOT on the HAL). **Add `virtual void
  rand_bytes(uint8_t* out, size_t n) = 0;`** → device impl calls `mrrng::fill` (NRF_RNG / SD-RNG /
  esp_random); sim impl (`NodeRuntimeWrapper`) draws from the deterministic `_sim_rng` mt19937 (mirrors
  `simRandRange`) so E2E scenarios stay reproducible. ⚠ Do NOT build the seed from `rand_range(0,256)×8`
  — that's the weak software path on device (even though `node_join` uses it for the non-crypto J nonce).
  Then build the cleartext header (`pack_unicast_inner`
  up to `origin`), `dm_kdf` + `dm_nonce`, `crypto_aead_lock` over `{source_hash?+location?+body}` with
  AAD = the cleartext header bytes, append the 16-B tag, write the 8-B seed trailer.
- **Fail loud:** no authoritative pubkey after the H-resolution budget → drop the parked send + emit
  telemetry `e2e_no_pubkey` (origin/dst/hash). **Never** fall back to cleartext.

## 5. Open-on-receive (`node_mac_rx.cpp` `handle_data` :338)

- Gate `flags & CRYPTED`. Identify the sender from the **cleartext `origin`**:
  `origin → id_bind (node_id→key_hash32, authoritative) → peer-pubkey cache (key_hash32→ed_pub,
  authoritative)`. Derive `x_pub = ed_pub_to_x25519(peer ed_pub)`, `ecdh_shared(_x_secret, x_pub)`,
  `dm_kdf`, `dm_nonce(seed, ctr, our key_hash32)` (we are dst → `dst_key_hash32 == _key_hash32`),
  `crypto_aead_unlock` (AAD = the cleartext header). On success, parse the decrypted region; **verify
  the decrypted `source_hash` == the resolved sender's `key_hash32`** (binding confirmation).
- **Fail loud:** tag failure, or no authoritative sender pubkey → **drop** + telemetry
  `e2e_open_fail` / `e2e_open_no_pubkey`. Never deliver ciphertext to the app. *(Recommended: on
  `no_pubkey`, fire a HARD `WANT_PUBKEY` for `origin`'s hash so the sender's retransmit opens.)*
- The stash-in-`_post_ack` path (`:481-487`) must hold the **decrypted** body (open before stash), so
  delivery/forward downstream is unchanged.

## 6. Pubkey-resolution wire + peer-key cache (E2/DP3 + the refinement)

- **H query:** `H_FLAG_WANT_PUBKEY = 0x02` (`frame_codec.h:250`). `emit_hash_query`
  (`node_hashlocate.cpp:388`) gains a `want_pubkey` param; the E2E send path emits **HARD +
  WANT_PUBKEY**.
- **Owner answer:** on a `WANT_PUBKEY` query it owns, `send_hash_bind_response`
  (`node_hashlocate.cpp:295`) emits DATA **TYPE 5** with inner `[target_layer 1][node_id 1][ed_pub 32]`
  (drop the redundant `key_hash32`). HARD ⇒ only the owner answers.
- **Cache + cache-on-pass:** any node parsing a DATA TYPE 5 (delivered **or relayed-through**) inserts
  `ed_pub` **authoritative** into the peer-key cache after verifying `ed_pub[:4]==key_hash32`. Relays
  cache-on-pass → authoritative survives relay.
- **Peer-key cache** (model on `id_bind`, `node.h:752` / `node_hashlocate.cpp:50/157`): `struct PeerKey
  { uint32_t key_hash32; uint8_t ed_pub[32]; uint8_t confidence; uint64_t last_seen_ms; bool valid; }`;
  `_peer_keys[cap_peer_keys]` with `cap_peer_keys ≈ 16` (sparse; add to `protocol_constants.h`).
  Insert (authoritative never downgraded), find-by-hash, age-out. Per-LayerRuntime for v1 (mirrors
  `id_bind`; a Node-level promotion is a later option — note it in a comment).
- **Parked sends:** reuse the park-on-H → drain mechanism (`on_hash_bind_response` resolves parked
  sends). On a TYPE-5 authoritative insert, drain parked CRYPTED sends to that hash → seal + send.

## 7. ★ Keystone (the hard gate)

`e2e_dm` defaults **off**; CRYPTED is set only on an encrypted origination. With no encrypted DM,
`data_mac_len()` returns 4, no TYPE 5, no seal path runs → `pack_data`/`parse_data` produce the
**exact bytes they do today → the meshroute s18 keystone stays BYTE-IDENTICAL.** Gate (from
`~/lora-universal-simulator`): `lus -e meshroute /home/staszek/MeshRoute/simulation/s18_meshroute.json
out.ndjson` → **157373 events / md5 `306c3cf4af65b56d6fc6415b964ad9f3`** (the MESHROUTE engine).
**NOT** the Lua `scenarios/s18_singlelayer_dense.json` (= 779015 / `77205506…`), which runs the
reference model — it can't catch a C++ regression. This run also re-confirms LOCATION stayed inert
(shared layout). Verify before/after; any divergence with CRYPTED off is a bug, not a re-baseline.

## 8. Test plan (TDD)

- **Native — crypto core:**
  - **KAT, not a round-trip** (design-spec §2 [xcheck]) — anchor to **EXTERNAL** vectors where the
    primitive is standard (the whole DM stack is — only the *identity* EdDSA is monocypher's variant):
    - **ECDH** (`ecdh_shared`): the **RFC 7748** X25519 vector — the *must-have* ("A·B==B·A" can't catch
      both sides deriving the same *wrong* secret; the external vector can).
    - **AEAD** (`crypto_aead_lock`): the **draft-irtf-cfrg-xchacha §A.3.1** XChaCha20-Poly1305 vector
      (fetch via WebFetch) — proves monocypher's AEAD *is* the standard, not just self-consistent. A
      self-captured monocypher golden is the fallback only if the vector is unobtainable.
    - **KDF/nonce** (`dm_kdf`/`dm_nonce`): BLAKE2b anchors to **RFC 7693** if cheap; else a fixed-input
      golden is acceptable (deterministic over the externally-verified primitives). `[:N]` =
      blake2b-512-truncated (§2).
  - `dm_nonce` fixed-vector; seal→open round-trips `{source_hash, location, body}`; a flipped
    ciphertext/tag byte or wrong key → `unlock` fails.
- **Native — frame:**
  - pack/parse a CRYPTED DM: cleartext-header ‖ ciphertext ‖ 16-B tag ‖ 8-B seed; `data_mac_len`
    conditional; **`pack_data` refuses `CRYPTED && !DST_HASH`**.
  - **KEYSTONE unit:** CRYPTED-unset `pack_data` == today's bytes (byte-identical).
- **Native — resolution/cache:** peer-key insert (authoritative/overheard, no-downgrade), find, age;
  `ed_pub[:4]!=hash` rejected; cache-on-pass of a relayed TYPE 5 → authoritative; `WANT_PUBKEY` emit
  sets `0x02`; owner answers TYPE 5, a non-owner does not (HARD).
- **Native — fail-loud:** enqueue CRYPTED with no pubkey → parks, no TX; H budget exhausted → refuse +
  `e2e_no_pubkey`, **zero cleartext frames sent**. Receive CRYPTED from an unknown sender → drop +
  telemetry, no app delivery.
- **Sim:** 2-node — A `e2e_dm` on, B holds A's key (seeded or via the WANT_PUBKEY exchange) → B
  delivers plaintext (assert a `e2e_delivered`-style telemetry); a 3rd overhearing node sees
  **ciphertext only**. Control: `e2e_dm` off → **s18 byte-identical**.
- **Builds:** native + `gateway` + `xiao_sx1262` + `heltec_v3` (the `_x_secret`/`_ed_pub` + the
  ~16-entry cache add to RAM; the gateway build must still link < 100%).

## 9. Gate criteria

native (**KAT** + seal/open round-trip + `dm_nonce` vector + the **unset = byte-identical** keystone
unit + cache no-downgrade/cache-on-pass + fail-loud-no-cleartext) **+** 4 builds **+ s18
byte-identical** **+** the sim scenario (encrypted delivery, eavesdropper-sees-ciphertext, fail-loud
on no-pubkey) **+** read the seal/open by eye: AAD == the exact cleartext header bytes, nonce inputs
are cleartext-only, **CRYPTED ⇒ DST_HASH** enforced, and **no cleartext-fallback path exists anywhere**.
