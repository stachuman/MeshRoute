// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# E2E Sealed-Sender Redesign — privacy-preserving DM crypto

**Status:** coder instruction (design ratified in conversation 2026-06-16). The user does ALL commits — land work GREEN + uncommitted, report ready.

**Supersedes:**
- `ios-companion/INBOX_SYNC_CONTRACT.md` §"Receiving a sealed DM before you hold the sender's key — locked + auto-recover (2026-06-16)" — the **locked + auto-recover + `reqpubkey <sender_hash>`** model is DEAD. Sealing the sender makes an un-openable DM un-attributable, so there is no `<sender_hash>` to name. Replace per Slice 3.
- The cleartext-`origin` key-selection of the committed Phase-1 E2E (`cb64913`). That design leaks the originator to relays; this replaces it.

**The keystone discipline (unchanged):** every lib/core behavioural slice must leave the MeshRoute s18 **byte-identical**:
`cd ~/lora-universal-simulator && cmake --build build -j8 && ./build/orchestrator/lus -e meshroute /home/staszek/MeshRoute/simulation/s18_meshroute.json out.ndjson` → **157373 events / md5 `306c3cf4af65b56d6fc6415b964ad9f3`**. `e2e_dm` is OFF by default ⇒ no CRYPTED frames in s18 ⇒ every change here is inert there. `fw_main`/console/contract changes gate with a **board build** (`pio run -e gateway -e xiao_sx1262 -e heltec_v3`), not s18.

---

## 1. Why

Hard privacy requirement (ratified by the author): **a relay ("go-by node") must never be able to tell who originated a DM.** The committed design leaks `origin` (the originator's node-id) in the cleartext AAD of every CRYPTED frame, and the receive path *uses* that leak to select the sender's key (`key_hash_of_id(origin) → pubkey → ECDH`). So the privacy bug and the key-selection mechanism are the same line.

We keep our algorithm (XChaCha20-Poly1305 + BLAKE2b-KDF + **static-static** X25519 ECDH — it gives implicit sender authentication for free: a valid tag proves who sealed it). We make it private by **sealing the sender and selecting the key by trial decryption** (the 16-B Poly1305 tag is the oracle). MeshCore was checked as a comparison and is *not* a model — it leaks a cleartext `src_hash` (or the full pubkey on first contact); we do better.

## 2. Ratified design (the 7 decisions)

1. **Algorithm unchanged.** XChaCha20-Poly1305 + `dm_kdf` (BLAKE2b) + static-static X25519. No ephemeral-static, no signatures (v1).
2. **Seal `origin`.** It moves from the cleartext AAD into the sealed plaintext. CRYPTED inner cleartext shrinks to `[dst_hash 4]`; AAD = `[dst_hash 4]`. `origin` joins the already-sealed `source_hash`/location/body.
3. **Key selection = trial decryption.** The receiver tries each cached **authoritative/pinned** peer key; the tag verifies for exactly one (false-accept 2⁻¹²⁸) → simultaneously decrypts, authenticates the sender, and identifies the key. Static-static ⇒ the per-pair key is constant ⇒ cache the derived AEAD key per peer; a trial is ≤`cap_peer_keys` (16) cheap AEAD-opens. **No cleartext sender hint** (any useful hint is a linkable fingerprint — rejected).
4. **Dedup.** Plaintext keeps `(origin, dst, ctr)`. CRYPTED keys on the **8-B cleartext nonce-seed** (already on the wire, preserved verbatim across forwards, random-per-message ⇒ unlinkable, and its uniqueness is already a crypto invariant). The seed extraction must be **hoisted above** the dedup.
5. **Bootstrap = mutual `reqpubkey`.** A pubkey request ALWAYS carries the requester's own pubkey. The target caches it (so it can later decrypt + reply) and answers with its own key routed to the requester's hash. Both sides hold both keys *before* any sealed message flows ⇒ the recipient can always decrypt. The pubkey rides the **one-time request**, never every message.
6. **E2E-ack semantics.** The ack is sent only after a successful open (the acker must decrypt to learn `origin`, and the ack means "I read it"). A missing E2E-ack = **"not delivered OR not decrypted"**, undifferentiated — the sender retries / re-handshakes. A receiver that cannot decrypt cannot identify the sender, so it cannot (and must not) NACK — silence is the only signal, which is exactly the assumed semantics.
7. **Undecryptable on arrival = silent DROP (option a).** No "locked" inbox state, no ciphertext at rest, no "Request key from X" UX. The sender's retry (after the handshake completes) re-delivers. Recovery is the handshake, not the message.

## 3. Current state (verified — change *from* here)

| Concern | Current code | File:line |
|---|---|---|
| Seal layout | inner = `[aad 5: dst_hash4‖origin1][ct][tag16]`; AAD = `[dst_hash][origin]` | `node_hashlocate.cpp:291-306`, `frame_codec.cpp:725` (`kAadLen=5`) |
| pt (sealed) | `{source_hash? · loc? · body}` | `node_hashlocate.cpp:293-300` |
| Open | caller resolves `sender_hash` via `key_hash_of_id(origin)`, single `peer_key_find`; AAD len 5 | `node_mac_rx.cpp:550-562`, `node_hashlocate.cpp:312-347` |
| Parse (CRYPTED) | reads cleartext `origin` (`:761-762`) then hands `ct‖tag` as body | `frame_codec.cpp:761-768` |
| Dedup | `sokey = (origin<<24)|(dst<<16)|ctr`; seed stashed LATER at `:488` | `node_mac_rx.cpp:367,393,488` |
| Wire (cleartext, CRYPTED) | full `ctr` @ bytes 6-7 ✓, 8-B seed trailer ✓, DST_HASH mandatory ✓ | `frame_codec.h:333,369`, `frame_codec.cpp:636-638,716` |
| reqpubkey | one-way: `emit_hash_query(hash,hard,want_pubkey)`; H frame `{leaf_id,origin,key_hash32,ttl,hard,want_pubkey}`; answer = DATA TYPE 5 `[target_layer][node_id][ed_pub32]` | `frame_codec.h:252-253` |

Confirmed feasible: the receiver rebuilds the nonce from `seed8` (cleartext trailer) + full `ctr` (cleartext bytes 6-7) + its OWN `dst_key_hash32` — **no sender info needed** (`dm_nonce(nonce, seed8, ctr, _key_hash32)`, `node_hashlocate.cpp:323`).

---

## 4. Slices

Land in order: **1a → 1b → 1c → 2 → 3**. Each is independently gateable. **1a and 1b are wire-invariant on purpose** (they de-risk the trial logic + the seed-dedup against the existing t94 golden); **1c is the wire change and MUST land last** — sealing `origin` while key-selection (`key_hash_of_id`) or the CRYPTED dedup still depend on cleartext origin would break routing.

### Slice 1a — Trial-decrypt key-selection (NO wire change)

Replace `origin → key_hash_of_id → single open` with trial decryption over cached keys. Origin stays cleartext for now (still in the AAD), so the **wire is unchanged and t94 must stay byte-identical** — this slice only changes *how* the receiver finds the key internally.

1. **Add a trial wrapper** in `node_hashlocate.cpp` (near `e2e_open_inner`):
   ```
   // Trial decryption: try each AUTHORITATIVE/PINNED cached peer key; the AEAD tag is the oracle.
   // On the first verifying key: decrypt + identify the sender (== that peer's hash) + recover origin.
   // Returns true + out-params on success; false if NO cached key opens it (caller drops — option a).
   bool Node::e2e_open_trial(const uint8_t* inner, size_t inner_len, const uint8_t seed8[8], uint8_t flags,
                             uint16_t ctr, uint32_t& sender_hash_out, uint32_t& origin_out,
                             uint32_t& source_hash_out, bool& has_location_out, int32_t& lat_out,
                             int32_t& lon_out, uint8_t* body_out, uint8_t& body_len_out);
   ```
   Body: iterate `_active->_peer_keys` (skip non-authoritative/pinned, skip expired); for each, call the per-candidate open with `sender_hash = peer.key_hash32`; first `true` → set `sender_hash_out = peer.key_hash32` and return. No match → return false.
   - Optimisation (do it): cache the derived 32-B AEAD key in the `PeerKey` entry when `peer_key_set` installs an authoritative/pinned key (static-static ⇒ constant). The trial then skips ECDH+KDF and is just ≤16 `dm_open`s. If you defer this, the trial still works (recomputes per candidate) — note it in the gate.

2. **`e2e_open_inner`**: add `uint32_t& origin_out`. In 1a, origin is still cleartext, so the *caller* keeps passing it in (set `origin_out = <cleartext origin>` for parity). The trial wrapper calls a per-candidate variant that does the open + the existing anti-spoof (`:344`). (1b moves origin into pt and this becomes the recovered value — keep the param so 1b is a one-line change.)

3. **Receive path** `node_mac_rx.cpp:550-562`: replace the `key_hash_of_id` + single `e2e_open_inner` with `e2e_open_trial(...)`. On success → `crypted_ok = true`, use the returned `origin`/`source_hash`/body. On false → **silent drop**: `MR_EMIT("e2e_open_no_key", EF_I("ctr", pa.ctr)); become_free(); return;` — NO push, NO ack, NO inbox entry (option a). Remove the old `e2e_open_no_pubkey`/`e2e_open_fail` split (folded into the single no-key drop + a tag-fail is just "no candidate opened").

**Gate 1a:** native (existing E2E units updated to the trial path) · **s18 `306c3cf4`** · 3 boards · **t94 byte-identical to its current golden** (wire unchanged ⇒ same events; the renamed receive-side drop telemetry isn't exercised — t94's deliveries all open, and the send-side `e2e_no_pubkey` is untouched). If t94's md5 changes here, the slice is wrong — the trial must be behaviour-equivalent to the old key-selection.

### Slice 1b — Dedup on the seed (CRYPTED, NO wire change)

`node_mac_rx.cpp` handle_data. Changes only the LOCAL dedup key for CRYPTED frames (the seed is already on the wire) — the wire is unchanged.
1. **Hoist** the seed extraction (`data_nonce_seed`, currently `:488`) to **before** the sokey at `:393`. For plaintext the seed is empty ⇒ a no-op there ⇒ the non-CRYPTED path is unperturbed.
2. Branch the sokey:
   - plaintext (`!d.crypted`): `sokey = (origin<<24)|(dst<<16)|ctr` — unchanged.
   - CRYPTED: fold the 8-B seed to 32 bits (e.g. first 4 bytes LE) → `sokey`. The seed is globally unique per message, so seed-alone is a sound flight-id; `dst` may be mixed in but isn't required. Loop detection (same sokey, different prev-hop) works because the seed is preserved verbatim on forward.
3. For CRYPTED, the `data_rx` telemetry's `origin` is incidental — it can keep reading the (still-cleartext until 1c) origin here; once 1c seals it, emit `origin=0`/omit. Either way only the seed-derived dedup key matters for routing.

**Gate 1b:** native (dedup-on-seed unit: a CRYPTED retransmit dedups; a looped CRYPTED frame via a different prev-hop → LOOP_DUP) · **s18 `306c3cf4`** (plaintext path byte-identical — verify the hoist didn't perturb the non-CRYPTED branch) · 3 boards · **t94 green** (CRYPTED dedup outcome unchanged ⇒ same events; re-record only if an outcome shifts).

### Slice 1c — Seal `origin` (wire change) — LAND LAST (needs 1a + 1b)

Move `origin` out of the cleartext AAD into the sealed plaintext. Land **only after** 1a (trial replaced `key_hash_of_id`) and 1b (CRYPTED dedup keys on the seed) — sealing origin while either still reads a cleartext origin breaks key-selection / dedup.

1. **`e2e_seal_inner`** (`node_hashlocate.cpp:271-310`):
   - `uint8_t aad[5] = {dst_hash×4, origin}` → `uint8_t aad[4] = {dst_hash×4}` (`:291-292`).
   - In the `pt` build (`:293-300`), **prepend** `origin`: `pt[pt_len++] = origin;` before the `source_hash` block.
   - Update the header comment (`:271`): inner = `[dst_hash 4][ct][tag 16]`; pt = `{origin · source_hash? · loc? · body}`.
2. **`e2e_open_inner`**: `aad_len = 5` → `4` (`:324`). After `dm_open`, the FIRST pt byte is `origin`: `origin_out = pt[off]; off += 1;` before the `source_hash` parse (`:332`). Keep the anti-spoof (`:344`) — under trial it confirms the opening candidate.
3. **`parse_unicast_inner`** (`frame_codec.cpp:737-782`): move the `if (flags & DATA_FLAG_CRYPTED)` check **above** the origin read. Under CRYPTED (after `dst_hash` + cross-layer), set `u.body = inner.subspan(off)` (ct‖tag) and return — **do NOT read a cleartext origin** (`u.origin` left 0/unset). Plaintext path unchanged (reads origin then source_hash?/loc?/body).
4. **`e2e_inner_regions`** observability (`frame_codec.cpp:722-732`): `kAadLen 5 → 4`; comment `[dst_hash 4]`.
5. **Receive path**: flip `e2e_open_trial`'s `origin_out` source from the cleartext field (1a parity) to the **decrypted pt byte** — it now *recovers* `origin` from the seal. Use that decrypted `origin` for the inbox/push/`send_e2e_ack`; `pa.origin` is no longer valid pre-decrypt for CRYPTED.

**Note:** `source_hash` inside the seal is now redundant under trial (the opening candidate *is* the sender's hash). Keep it for v1 (preserves the `:344` anti-spoof invariant + minimal change); dropping it (−4 B) is a future optimisation, not this slice.

**Gate 1c:** native (units: sealed-origin roundtrip — origin recovered from the seal, NOT readable cleartext; trial picks the right key; wrong/no key → drop) · **s18 `306c3cf4`** (e2e_dm off ⇒ inert) · 3 boards · **t94 RE-RECORDED** (wire changed; properties unchanged: encrypted delivery, opaque-without-key, no plaintext to overhearers).

### Slice 2 — Mutual `reqpubkey` (the bootstrap)

The WANT_PUBKEY request carries the requester's full pubkey so the exchange provisions BOTH directions.

1. **H frame** (`frame_codec.h` `h_in`/`h_out` `:252-253`, `pack_h`/`parse_h`): when `want_pubkey` is set, append the requester's `ed_pub[32]`. Conditional field (only when `want_pubkey`) so non-pubkey H floods are unchanged. Confirm the frame size budget (a WANT_PUBKEY H is now +32 B — acceptable; it's occasional).
2. **Answerer** (the WANT_PUBKEY H handler): `peer_key_set(requester_hash, requester_ed_pub, authoritative)` — cache the requester's key (so it can decrypt the requester's future sealed DMs + address the answer). Then send the TYPE-5 answer routed to the requester's hash (`= requester_ed_pub[:4]`).
3. **Requester** (TYPE-5 answer handler): `peer_key_set(target, target_ed_pub, authoritative)` — unchanged. Both sides now hold both keys.
4. Console `reqpubkey <hash>` + the app contract are **unchanged** — the app still just sends `reqpubkey`; mutuality is firmware-internal.

**Residual to DOCUMENT (don't hide):** the request itself can't be sealed (the requester lacks the target's key — chicken-and-egg), so the handshake exposes both identities to the flood region — the deliberate "establishing contact" moment; everything after is sealed. The directed-when-route-known optimisation (flood only to locate) is deferred to a follow-up.

**Gate 2:** native · **s18 `306c3cf4`** · 3 boards · a sim **t-gate** (see §5) proving A reqpubkey(+pubkey) → B caches+answers → both seal → bidirectional delivery with NO QR/pre-provisioning.

### Slice 3 — Semantics + cleanup (mostly docs)

1. **E2E-ack on-decrypt**: verify the ack branch (`node_mac_rx.cpp:612-614`) is reached only after a successful open (it is — open at `:550`, deliver+ack after) and that `send_e2e_ack` uses the **decrypted** origin. No code change expected; assert it in a test.
2. **Supersede the contract** (`ios-companion/INBOX_SYNC_CONTRACT.md`): delete the "locked + auto-recover" section; remove the `locked` field + the three-state model + the "Request key" action. Replace with: *a sealed DM that can't be opened is dropped silently; there is no per-message recovery; provisioning is the mutual `reqpubkey` handshake or QR; a sender that gets no E2E-ack assumes "not delivered or not decrypted" and retries.* Keep the `enc` indicator (still valid — a delivered DM was sealed).
3. **Document residual metadata leaks** (in the spec's §6 + a note in `frames.md`): cleartext `ctr`+`dst` enable coarse traffic analysis even with `origin` sealed; sealing `origin` removes opportunistic reverse-route learning so the ack/return path uses normal discovery (the handshake pre-warms it); the ack's `dst = origin` exposes the original sender as a *recipient* (the routing-necessary recipient exposure we already accept).
4. Confirm no half-built "locked" code exists (verified 2026-06-16: `locked` is absent from lib/src — only the contract doc had it). Nothing to revert in code; just do not build the locked-by-sender design.

**Gate 3:** 3 boards (contract is doc-only; the ack assertion is native) · the contract reads consistently with the code.

---

## 5. Tests

- **Update**: existing E2E open units → the trial path (`e2e_open_trial`).
- **New native units**:
  - sealed-origin roundtrip: seal→open recovers `origin` from the plaintext; `origin` is NOT present in the cleartext inner (assert the parsed cleartext region is `[dst_hash]` only).
  - trial: the correct cached key opens; a frame for a peer we DON'T hold → no candidate opens → drop (no push/ack/inbox); a tag-fail (corrupt) → drop.
  - dedup-on-seed: a CRYPTED retransmit (same seed) dedups; a CRYPTED loop (same seed, different prev-hop) → LOOP_DUP NACK.
  - ack-on-decrypt: an E2E-ack fires only when the open succeeds; no key → no ack.
- **t94** (`~/lora-universal-simulator/test/t94_e2e_dm_crypto.json`): re-record for the sealed-origin + trial flow. Properties to keep asserting: encrypted delivery succeeds; an overhearer sees no plaintext; a recipient without the sender key gets NO delivery (silent drop — no `locked`, no push).
- **New sim t-gate (t95 mutual handshake)**: A and B each seeded; A holds neither B's key nor a route; A `reqpubkey B` (carrying A's pubkey) → B caches A + answers → A caches B → A's sealed DM delivers; assert a sealed DM sent *before* the handshake is dropped (no delivery), and delivered *after*.

## 6. Residual leaks (v1 — documented, not fixed)

| Leak | Why accepted v1 |
|---|---|
| cleartext `ctr` + `dst` | routing + nonce need them; coarse traffic-analysis only, no identity |
| handshake exposes both parties | the request can't be sealed (no key yet); deliberate contact-establishment; sealed thereafter |
| ack `dst = origin` | the routing-necessary recipient exposure; we promised *sender* privacy, not recipient |
| reverse-route via discovery | sealing origin drops opportunistic reverse-route learning; handshake pre-warms; correctness intact |

§7 threat-upgrade (future): directed (non-flood) handshake; optional sealed signature for active-attacker auth; consider dropping cleartext `ctr` linkability.

## 7. Gate checklist (every slice)

- [ ] `pio test -e native` → all pass (`.pio/build/native/program | tail -3` for the true count)
- [ ] s18 meshroute → **157373 / `306c3cf4af65b56d6fc6415b964ad9f3`** byte-identical — **every slice** (e2e_dm off ⇒ CRYPTED paths inert; the only s18 risk is the 1b dedup-hoist perturbing the plaintext branch — verify it doesn't)
- [ ] `pio run -e gateway -e xiao_sx1262 -e heltec_v3` → 3 boards green
- [ ] t94 byte-identical (1a, 1b) / **re-recorded** (1c, Slice 2) / 0 assertion failures
- [ ] new units + t95 green
- [ ] contract + frames.md read consistent with the code; residual-leak notes present
- [ ] leave GREEN + uncommitted — the user commits
