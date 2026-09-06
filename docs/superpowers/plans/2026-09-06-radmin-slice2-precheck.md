<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 2 — Quality-Agent pre-check ledger (2026-09-06): the remote codec and its independent KATs

Authority: design §19 item 2 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:1706-1710`),
§19.1 row 2 ("remote codec/KDF files and carrier-cap authority | zero remote events, 36/36 unchanged; ruled pair |
none", :1793), §8 (wire bodies :566-878), §9 (nonce/request-id rules :878-915), R-RA-3/4/5 (rulings ledger :40-70),
R-RA-13 (mandatory SOURCE_HASH), R-RA-25 (the 232 app-DM cap, mandatory hashes), the 0e characterization
(`test/test_radmin_characterization_0e.cpp`, evidence `2026-09-04-radmin-0e.md` §carrier table). Verified at
`HEAD 0da4d56` ("0b prep", the 1b base) — Slice 2's base is the commit AFTER 1b closes. Hypotheses, not authority.

## 0. What Slice 2 is, in one line

Land the pure remote codec — the frozen control byte/opcodes/slots, the §8 body layouts (encode + exact-length
decode), the BLAKE2b-512-truncated KDFs, the derived XChaCha nonce, the AAD rule, `remote_body_cap(RemoteCarrier)`
from the packers — pinned by INDEPENDENT known-answer vectors, with NO consumer (no RX/TX path change ⇒ zero remote
events, 36/36 by construction) and NO `wire_version` bump.

## 1. Source facts — the frozen table (design §8.1, ratified; append-only after this slice)

| item | value |
| --- | --- |
| `ctl` byte | bits 7..4 opcode; bits 3..0 ACL slot 0..9, A..E reserved (reject), F = sentinel (OPEN_EXECUTE, open response, bootstrap request only) |
| `REMOTE_CMD` opcodes | 0 AUTH_EXECUTE · 1 OPEN_EXECUTE · 2 BOOTSTRAP · 3 RESPONSE_ACK · 4 SAFE_ROLLOVER · 5 FORCE_ROLLOVER; 6..F reserved reject |
| `REMOTE_RESP` opcodes | 0 OUTPUT · 1 TERMINAL · 2 BOOTSTRAP · 3 ROLLOVER_RESULT · 4 PROTOCOL_ERROR; 5..F reserved reject |
| bodies + fixed overhead (§8.2-8.8, §8.11) | auth exec `[ctl 1][request_id 8][ct N][tag 16]` = 25 · open exec `[ctl][id][cmd N]` = 9 · bootstrap req `[ctl][id][controller_pub 32][tag]` = 57 · session control (ACK/SAFE/FORCE) `[ctl][id][tag]` = 25 · bootstrap resp `[ctl][id][admin_epoch 8][tag]` = 33 · rollover result `[ctl][id][epoch 8][abandoned 1][tag]` = 34 · auth resp `[ctl][id][seq 1][ct N][tag]` = 26 · open resp `[ctl][id][seq][pt N]` = 10 |
| terminal codes (§8.9) | `completed`, `scheduled` (+ bounded activation delay), `unknown_command`, `refused`, `output_truncated`, `internal_error`, `session_full`, `session_busy` (+ count); auth `PROTOCOL_ERROR` carries `already_acknowledged`. ⚠ the design lists NAMES ("Proposed meanings") — their NUMERIC values are not frozen anywhere yet: Slice 2 freezes them (Author lands the numbers in §8.9 with the brief; append-only after) — §4.3 |
| KDF labels (ASCII, no NUL) | `MeshRoute remote-admin v2 base` · `… v2 session` · `… v2 nonce` |
| key inputs | base = `label ‖ shared32 ‖ controller_ed_pub32 ‖ target_admin_ed_pub32`; session = `label ‖ base_key32 ‖ admin_epoch_le64`; BLAKE2b-512 truncated to 32 |
| nonce inputs | `label ‖ selected_key32 ‖ outer_type_u8 ‖ ctl_u8 ‖ request_id_le64 ‖ response_seq_u8 ‖ source_hash_le32` (+ `admin_epoch_le64` ONLY for the bootstrap RESPONSE and ROLLOVER_RESULT domains); BLAKE2b-512 truncated to 24; `selected_key32` = base key for bootstrap req/resp + rollover result, session key otherwise |
| AAD | `outer_type_u8 ‖ exact clear RPC-header bytes in wire order ‖ source_hash_le32` (ct/tag never repeated) |
| `request_id` | cryptographically random u64 (R-RA-5); RNG failure = loud pre-transmission refusal; evidence DERIVES the ≈2^-33 birthday bound at 2^16 requests (not a literal) |
| decoder rule | one layout chosen from (outer direction, opcode, slot class, EXACT length); an invalid authenticated request NEVER falls back to the open decoder; tag failure = silent; legacy bodies (`admin_auth` frame `[rand8 8][nonce_ctr 2][ct][tag 16]`) REJECT |
| DATA types | `REMOTE_CMD 0xA0` / `REMOTE_RESP 0xA1` REUSED (internal, `frame_codec.h:802-803`); the opcode namespace + labels are the subprotocol discriminator; ⛔ no `wire_version` change (design :1535-1540, C4/M3) |

## 2. Source facts — what exists to build on, and what must NOT be reused

| fact | anchor |
| --- | --- |
| primitives already KAT-pinned: `dm_kdf` (BLAKE2b-512 → 32) / `dm_nonce` (→ 24) / `dm_seal` / `dm_open` (XChaCha20-Poly1305 via monocypher `crypto_aead_lock/unlock`), X25519 (`crypto_x25519`), `crypto_blake2b`, `crypto_wipe` | `lib/core/dm_crypto.{h,cpp}` (:32/:45/:76 the truncation idiom), `lib/core/identity.cpp`, `lib/monocypher/src/monocypher.h` |
| the KAT idiom: vectors HARD-CODED in the test with the exact independent derivation documented beside them (`hashlib.blake2b(digest_size=64)[:32]`, draft-irtf-cfrg-xchacha-03 §A.3.1); NO tools script generates them ⇒ Slice 2's evidence must record the exact Python one-liners that produced every new vector, and the vectors must be produced by the REFERENCE, never by the production codec (R-RA-4: "not merely round-trip") | `test/test_dm_crypto.cpp:2-5, :26-44, :129`; `test/test_identity.cpp` |
| the legacy codec `admin_auth.{h,cpp}` (`admin_cmd_seal/open`, `AdminCmd{node_key_hash, counter}`, `admin_counter_ok`) — Slice 9 deletes it; Slice 2 must NOT extend or reuse it; its frame shape is the "legacy body" the v2 decoder rejects | `lib/core/admin_auth.h:30-44` |
| `data_inner_cap(flags,type,frame_cap)` / `data_frame_len` (air-fit) and `max_payload_bytes_hard_cap` 241 (storage) — the two bounds `remote_body_cap` derives from (0e: "STORAGE governs" for plaintext typed) | `lib/core/frame_codec.h:733-748`, `protocol_constants.h:1055` |
| ⚠ (superseded by R-RA-28 — see §4.1; kept as the pre-ruling reasoning) THE 0e CARRIER TABLE PREDATES R-RA-25. Its by-node-id rows (236 / 233 / 232 / 231 / 230 and the 236 response row) assumed NO `DST_HASH`; R-RA-25 makes `DST_HASH` mandatory whenever supplied or derivable, and every v2 target is resolved by KEY (design §6/§8.4: the full key, hash known) ⇒ every v2 carrier is hash-addressed ⇒ the live rows are the hash-addressed ones: same-layer 232 (231 for the typed mobile wrapper), cross-layer 229/228/227/226 — and the by-node-id rows are NOT v2 carriers. Slice 2's `remote_body_cap` must be derived under R-RA-25's field set (both hashes), with the 0e table re-run as KAT INPUT and its stale rows marked superseded — §4.1 | `docs/superpowers/evidence/2026-09-04-radmin-0e.md:352-364`; R-RA-25 |
| the 0e fixture is explicit: "Slice 2 owns the production `remote_body_cap(RemoteCarrier)`; what 0e produces is its KAT INPUT" | `test/test_radmin_characterization_0e.cpp:8-26` |
| no `remote_codec`/`radmin` production file exists; `lib/core` has only `admin_auth.*` for the legacy path | `ls lib/core` |
| the RX ownership seam (1b) will exist: `radmin_rx_owner(type, client_on, accept_on)` — Slice 2 does NOT wire the codec to it (zero remote events) | 1b brief/evidence |
| mutation harness: per-source-file targets; a NEW `lib/core` file has NO target ⇒ Slice 2 must ADD its own target (e.g. `radmin2codec`) with the ruled controls (corruption, nonce-separation, cross-domain inequality, exact-length, legacy rejection, no-fallback-to-open, RNG-refusal); `tools/probe_ui_model_mutations.py` `TARGET_SRC` | that harness |

## 3. Shape the brief must pin

- New production files (names the coder's, e.g. `lib/core/remote_codec.{h,cpp}`): pure, stateless, allocation-free
  functions — `remote_kdf_base`, `remote_kdf_session`, `remote_nonce(selected_key, outer_type, ctl, request_id,
  seq, source_hash, epoch_or_none)`, `remote_aad(...)`, `remote_encode_<body>` / `remote_decode(outer_type, bytes)`
  returning a tagged layout-or-refusal, `remote_body_cap(RemoteCarrier)`; the frozen table as enums/constexpr with
  `static_assert`s on every fixed overhead (25/9/57/25/33/34/26/10) and on the nibble split. No Node member, no
  timer, no NV, no `src/` include. Add the new `.cpp` to the sim's CMake source list? — ⚠ check whether the lus
  CMakeLists globs `lib/core/*.cpp` or lists files (if it lists, Slice 2 must add the file: a `simulation/` edit
  the brief must authorise explicitly; the byte-identity proof is unaffected since nothing calls the codec).
- KATs (`test/test_remote_codec.cpp`, native): every KDF/nonce/AAD domain pinned against the independent Python
  reference, BOTH directions (CMD/RESP) and cross-domain INEQUALITY (same request_id, every opcode pair → distinct
  nonces); bootstrap request has NO epoch input while bootstrap/rollover responses DO; two controllers sharing one
  seed but distinct `SOURCE_HASH` → distinct nonces; `selected_key32` = base key vs session key per §8.1;
  all-zero / low-order X25519 point refused; every-byte corruption of a sealed body fails the tag; reserved
  opcodes/slots reject; wrong length rejects (exact-length rule); a legacy `admin_auth` frame rejects; an
  authentication failure never yields an open decode; the encoder/decoder round-trip is a SECONDARY check only.
- `remote_body_cap` battery: for every live v2 carrier (hash-addressed set under R-RA-25), the REAL packers accept
  exactly at the cap and refuse at cap+1 (`pack_unicast_inner` + `pack_data`, the 0e idiom); the stale 0e rows
  (by-node-id, no DST_HASH) are recorded as superseded, not carried as caps.
- The 2^16 bound: a computed check (birthday approximation n²/2^65 at n = 2^16 ≈ 2^-33) in the evidence + a test
  that the ID is 64 bits wide and drawn from the RNG (RNG-failure path refuses loudly — testable via the HAL fake).
- Mutation: the new target's controls; PIN re-sync; batteries for touched existing files: none expected (only new
  files + tests) — state both selectors.

## 4. Open points — RESOLVED 2026-09-06 (R-RA-28 + Author preparation, reviewed by QA)

- **4.1 RULED (R-RA-28):** the admission authority ALWAYS reserves the four `DST_HASH` bytes, whether or not a legal
  carrier transmits the field; omitting it never enlarges the RPC body. ⚠ My premise "every v2 carrier is
  hash-addressed" is WITHDRAWN as the reason (administration identity ≠ routing identity; open requests make no key
  claim; the hosted last mile passes no destination override) — the caps stand on the reservation rule instead:
  same-layer incl. hosted last mile **232**, same-layer typed mobile wrapper **231**, cross-layer full depth 1..4
  **229/228/227/226**, typed mobile cross-layer wrapper destination depth 1..3 **228/227/226** (wrapper depth 4 is
  INVALID, not a 225-byte carrier — B309). Derivation = `min(storage 241, real air-fit) − origin 1 − SOURCE_HASH 4 −
  DST_HASH 4 − carrier path/wrapper extras`, from named constants + packers; refuse cap+1; ⚠ tests must separate
  ADMISSION refusal (the authority refuses cap+1) from PHYSICAL packing (a raw packer may still fit 233 in a
  same-layer shape lacking DST_HASH — that is not a packer refusal). The 0e table stays as historical packing
  measurement; its larger allowances are superseded (B308).
- **4.2 RESOLVED:** the simulator lists sources in `_meshroute_core_srcs` (`CMakeLists.txt:58`, consumed at `:83` for
  BOTH the normal and gateway core libraries) ⇒ the brief pre-authorises ONE line in that separate repository; both
  variants must compile the codec; both repositories' bases/diffs recorded; the owner commits each repo.
- **4.3 REVIEWED — the Author's terminal allocation is ACCEPTED with one obligation:** `0x00 completed · 0x01 scheduled ·
  0x02 unknown_command · 0x03 refused · 0x04 output_truncated · 0x05 internal_error · 0x06 session_full ·
  0x07 session_busy` (the listed order; append-only after Slice 2); `already_acknowledged = 0x00` lives in the
  separate authenticated `PROTOCOL_ERROR` domain. ⚠ Because byte `0x00` then means two things under two opcodes, the
  codec's decoded result must CARRY THE OPCODE DOMAIN (a typed value, never the bare byte), and the KATs must pin that
  the same `0x00` under TERMINAL vs `PROTOCOL_ERROR` decodes to two different meanings and that a `PROTOCOL_ERROR` body
  with any other byte rejects.

## 5. Numbering note for the Author

The 1b coder's evidence proposed **"B310"** for the mutation harness losing a worker on non-UTF-8 test output; the
register now uses **B310** for the parked last-mile unification. The harness finding still needs its OWN row (next free
number) — do not let the collision drop it (M1).

- **4.1 R-RA-25 re-derivation of the carrier caps:** confirm every v2 carrier is hash-addressed (targets resolved by
  full key ⇒ `DST_HASH` always derivable ⇒ mandatory), so the by-node-id rows of the 0e table are non-carriers and
  the live caps are 232 / 231 (typed wrapper) / 229..226 (XL by depth). Recommendation: confirm; the design's §8.11
  example (231 / 206) already assumes the hash-addressed wrapper.
- **4.2 the sim source list — VERIFIED: the simulator LISTS `lib/core` files explicitly**
  (`/home/staszek/lora-universal-simulator/CMakeLists.txt:65` `node_mac_rx.cpp`, `:76` `dm_crypto.cpp`, …), so a new
  codec `.cpp` is NOT compiled into the sim until it is listed there. Two honest options: (a) the brief pre-authorises
  the one-line addition in the SIM repo (a separate repository; the owner commits there too; report actions + md5;
  identity unaffected because nothing calls the codec) — needed so the sim links the same `lib/core` the boards do;
  (b) keep the codec header-only in Slice 2 (all `inline`/`constexpr`), which needs no list edit. Recommendation:
  (a) — a `.cpp` keeps the KDF/AEAD code out of every including TU and matches `dm_crypto.cpp`'s shape.
- **4.3 freeze the terminal-code numbers:** §8.9 gives names only; Slice 2 must pin numeric values (append-only
  after). Recommendation: the Author lands the numbers in §8.9 with the brief (an Author landing, owner informed),
  in the listed order 0..7 with `already_acknowledged` in the PROTOCOL_ERROR domain.

Starting pins = the Slice 1b closure commit's (native, ABI, boards, s18, lus md5, tools, probes) — record at brief
time. Bench: NONE (row 2 = none; nothing airs).
