<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Authenticated Remote Management — design

**Status:** design (2026-07-13, brainstorming output; **realigned 2026-07-13** to the built prerequisites). Closes the deferred `M8 remote-reboot-auth` item. **Prerequisites BUILT + gated** (both uncommitted): the command-sink consolidation (`dispatch(line,len,Print& out)`, `2026-07-13-command-sink-consolidation-design.md`) **and** the remote binary response encoders (`lib/console/console_binary` TLV `enc_*`/`dec_*`, `2026-07-13-remote-binary-response-encoders-design.md`). Ready to plan.

> **★ Final decisions (2026-07-13) — these supersede the mechanism detail in §5–§6 where they differ:**
> 1. **Auth = authenticated encryption, not a separate signature.** A gated remote command is an **E2E-sealed DM** (`dm_crypto`, X25519 — the #3 team-encryption path) from the admin's **password-derived key** to the node; the node **trial-opens with its pinned `admin_pubkey`**, and the **AEAD (Poly1305) tag *is* the sender-authentication**. The §5–§6 Ed25519-signature envelope is **replaced** by this. Consequence: `admin set <password>` and every gated command ride **sealed — never cleartext on the mesh** (this is what resolves the §4 "full-console-remote" exposure). Replay = the in-body **counter** + node floor (kept). The admin still needs the node's pubkey (WANT_PUBKEY) to seal to it.
> 2. **Cleartext (unauthenticated, "anyone") set = `status` + `routes` ONLY.** Everything else is sealed + admin-only. Both have binary encoders — an open read answers with an **UNSEALED binary TLV** frame; a gated command answers **sealed** (decision 4).
> 3. **Prerequisites are BUILT (2026-07-13).** The command-sink consolidation landed `dispatch(line,len,Print& out)` (the three drifting dispatchers now share one verb table; serial=`mrcon`, BLE=companion-JSON + a text fallback). `remote_exec` was **deliberately left un-converged** so THIS spec owns the DM-fit response path. So remote-auth = a **thin sealed adapter**: `auth → default-deny classify → (data verb) fill an `enc_*` binary TLV | (action verb) execute the side effect → seal → REMOTE_RESP DM`. The default-deny classifier (§4/§5) + the feature-split gate (§10) apply on top.
> 4. **Remote responses are BINARY TLV, via `lib/console/console_binary` (BUILT).** Remote does NOT reuse the *text* `dispatch` output (that's USB/BLE) — it calls the `enc_status`/`enc_cfg`/`enc_routes`/… encoders (≤241 B DM, forward-compatible TLV). Open reads (`status`/`routes`) = unsealed binary; gated reads/writes = sealed binary (or a sealed `ok` ack for actions). This **replaces §6's "cleartext text response"**. Sealing the response back to the admin (confirming the real node answered) is in-scope now that the sealed command carries the admin's identity — see §6.

## 1. Problem
Remote management today (`rcmd`, 2026-06-24) is a **cleartext, unauthenticated** console-query-over-DM: `DATA_TYPE_REMOTE_CMD=6` / `_RESP=7`, transported by `send_remote_cmd`/`send_remote_response` (normal-routed, multi-hop, ACK'd), with `fw_main`'s `remote_exec(from, …)` owning the whitelist `status | faults | version | uptime | cfg | duty | reboot | prep-restart`. It acts on the sender's **unauthenticated, spoofable** node id — so **anyone can reboot any node**. We want authenticated writes.

## 2. The model — one password → one keypair (asymmetric, recoverable)
The admin credential is **one shared admin password** for the fleet. A password is just a seed:

```
seed        = BLAKE2b^N(password ‖ salt)   // iterated BLAKE2b (PBKDF2-style), N tuned to ~1–2 s on nRF52 — 32 B
admin_kp    = identity_from_seed(seed)      // Ed25519 (reuses the E2E-seam primitive)
admin_pubkey = admin_kp.ed_pub             // pinned on every node
```

> **KDF = iterated BLAKE2b, NOT Argon2 (decision 2026-07-13).** Argon2's memory-hardness needs a KB-scale work-area — a blocker on the RAM-tight gateway (the very node remote-mgmt targets) and future smaller boards, so it is dropped. The stretch is **CPU-hard only** via the iteration count `N`, using the already-vendored `crypto_blake2b` (zero extra RAM, no new dependency). Trade-off: offline grinding on a captured node's pinned pubkey is cheaper than under a memory-hard KDF — mitigated by a **high `N` + a real passphrase** (below). `N` is a tunable constant calibrated at the bench.

- **Provision:** derive the keypair, pin **`admin_pubkey` only** on the node (local, one-time). The private key is stored nowhere.
- **Admin:** on *any* device, type the password → re-derive the private key → sign the command. The credential lives in your head, not a device — **lose the device, use another**. That recoverability is the whole reason we rejected device-keypair delegation (below).
- **Node:** verifies the command signature against the pinned pubkey. Holds only a public key.

**Why asymmetric even though we want "simple":** `identity_from_seed` + Ed25519 sign/verify are already wired (the E2E seam), so asymmetric costs no more to build than a symmetric MAC — but a **physically-captured node yields only the pubkey** (offline-*grindable*), not the live fleet credential a symmetric shared-secret would hand over instantly.

**Honest trade-off (accepted):** the pinned pubkey lets a captured node **offline-grind the password** (guess → derive → compare). A random device key was un-grindable; a memorable credential isn't — and with a CPU-only (not memory-hard) KDF, grinding is GPU/ASIC-friendly. Mitigation is the **high iteration count `N` (slow per-guess)** + a real **passphrase, not a 4-digit PIN**. Inherent to any credential-you-can-remember; documented, not hidden. (An attacker must first *physically capture a static relay/gateway* to get the pubkey at all.)

**Salt:** a fixed protocol-constant salt is sufficient (security rests on passphrase entropy + the iteration count); a random **per-deployment** salt (set at provisioning, shared to admin devices) is a noted hardening against cross-deployment precomputation (rainbow tables).

## 3. Why NOT delegation chains (the rejected model, recorded)
The first design was per-node owner-root + signed delegation certs (`A→B→C`), with generation-bump revocation. It was rejected as **operationally impractical**: every admin is a *device-held* private key, so a lost/dead device is a lost credential, the root becomes its own thing to safeguard, and recovery means re-delegating. It also **didn't fit the wire**: a 2-cert chain ≈ 197 B + a command's 64 B sig ≈ 260 B > the ~220 B payload. The password model is recoverable, needs no chains, and a command is tiny.

## 4. Command model — two tiers (per §0.2, superseding the original wider open set)
- **Open (unauthenticated, "anyone"):** `status` + `routes` ONLY — the honest-net reachability/health reads. Answered with an **UNSEALED binary TLV** (`enc_status`/`enc_routes`, §0.4). No auth; a spoofed sender only gets public health data.
- **Gated (sealed + admin-only):** **everything else** — the other reads (`cfg`/`duty`/`limits`/`faults`/`gateway`, which leak config) **and** every write (`reboot`/`prep-restart`, `cfg set` OTA, provisioning, OTA-trigger, `password`-rotate, factory-reset). A gated command is an **E2E-sealed DM** (§0.1); the AEAD tag authenticates the admin. Answered with a **sealed** binary TLV (data) or a sealed `ok` ack (action).

The gate lives in the reworked `remote_exec`: **trial-open** the inbound DM with the pinned `admin_pubkey` → opened (authenticated) ⇒ run any verb; **unsealed** ⇒ only `status`/`routes` (else silent drop). Classify → data verb: `enc_*` → seal (if gated) → RESP; action verb: execute the side effect (via the shared `handle_*`/`dispatch` where one exists) → sealed `ok`.

## 5. Auth mechanism (the gated path)
> **★ Superseded framing (per §0.1):** the gated command is an **AEAD-sealed DM** (`dm_crypto` X25519), NOT a separate Ed25519 signature. The **Poly1305 tag *is* the authentication** (a valid open against the pinned `admin_pubkey` = a genuine admin) — so there is no explicit `ed25519_sig` field. Everything else below stands: the **`node_key_hash` + `counter`** live in the sealed body (cross-node + replay defense), and the **reject-hint / counter-resync** flow is unchanged. Read "signed body" as "the sealed plaintext" and "valid signature" as "successful AEAD open".

A gated command carries a **signed (sealed) envelope**:

```
signed body = { node_key_hash : u32,   // THIS node — cross-node replay defense (a `reboot` for X can't hit Y)
                counter        : u32,   // monotonic per credential — same-node replay defense
                command        : bytes }
envelope    = signed_body ‖ ed25519_sig(admin_privkey, signed_body)   // 64 B sig
```

**Node verify (all must hold, else drop):**
1. `ed25519_check(sig, signed_body, pinned admin_pubkey)` — a valid admin signature.
2. `node_key_hash == my key_hash32` — the command was signed *for me*.
3. `counter > my persisted counter-floor` — not a replay.
→ execute + advance the floor (persist, reusing the `channel_ctr`-lease write-coalescing pattern) + respond.

**Counter bootstrap / resync (one-shot, no round-trip in the common case):** the admin device tracks its own monotonic counter. A *fresh* device (or one behind the floor) sends a valid signature with a stale counter → the node replies **`counter-floor = N, retry higher`** — *only after a valid signature* (so an unauth'd prober never learns the floor; it gets silent drop). The device bumps above N and retries once. Single-device steady state = pure one-shot.

**Failure handling:** invalid signature, wrong `node_key_hash`, or a gated command with no envelope → **silent drop, no response** (no liveness/auth oracle to an unauthenticated sender; the node's existence is already public via beacons, but its command surface shouldn't be probeable).

## 6. Wire format (all ≤ 220 B)
- **Open command** — `DATA_TYPE_REMOTE_CMD`, cleartext body = the query keyword. **Unchanged.**
- **Gated command** — an **AEAD-sealed DM** (`dm_crypto` X25519, §0.1). The **sealed plaintext** = `[node_key_hash u32][counter u32][command …]` (8 B + command); `dm_crypto` wraps it with its nonce + Poly1305 tag (the auth). Framing (for the plan): a leading flag/version byte on the `REMOTE_CMD` body distinguishes sealed-vs-open (recommend the flag byte over a new DATA type). Size ≈ **8 B + command + `dm_crypto` overhead** — fits the ≤220 B payload with ample command headroom. No 64-B signature (the tag replaces it).
- **Response** — `DATA_TYPE_REMOTE_RESP`, a **binary TLV** frame from `lib/console/console_binary` (`[ver=1][msg_type][tag][len][value LE]`, ≤241 B, forward-compatible — §0.4). An **open** read (`status`/`routes`) answers **unsealed**. A **gated** command answers **sealed** to the admin via the existing X25519 path — the node caches the admin pubkey from the just-opened command, so sealing back is free and confirms *the real node* answered (no longer deferred — the sealed inbound gives us the admin identity). Lists (`routes`) that exceed one DM use the encoders' fit-N + `truncated` flag (no multi-DM stream in v1).

## 7. Node state (new)
- `admin_pubkey[32]` — pinned, in NV (v-bump the `device_nv` Blob).
- `admin_counter_floor : u32` — persisted, write-coalesced like `channel_ctr`.
- One transient reject-hint is stateless (computed from the floor); **no per-admin table, no nonce slot** (one-shot, not challenge-response).

## 8. Provisioning & rotation — the `password` primitive
- **Set (the `password` primitive):** a first-class **local console/BLE command `password <password>`** (a `dispatch` verb) → `BLAKE2b^N(password ‖ salt)` → `identity_from_seed` → **pin `admin_pubkey` → NV**, then **discard the private key** (never stored). **LOCAL-ONLY — the `password` verb is never accepted over the mesh** (setting the credential must be physical/MITM-resistant; it is not in the remote verb set at all). One-time; re-run to change. (This is the old `admin set <password>`, renamed to a clean primitive.)
- **Rotate/revoke:** re-run `password` locally, **or** an over-the-air `password rotate <new_pubkey>` **gated** command (sealed by the *old* admin — so a compromised-but-not-yet-rotated fleet can be rotated remotely; the counter-floor resets with the new key). Rotation is the revocation story — there is no per-command revocation (one credential).

## 9. Reuses vs new
- **Reuses:** `identity_from_seed` + Ed25519 + the `dm_crypto` X25519 seal/open (E2E seam), `crypto_blake2b` + its incremental `_init`/`_update`/`_final` (vendored monocypher — the iterated KDF), **`lib/console/console_binary` TLV `enc_*`/`dec_*` (BUILT — the response encoders)**, **the `dispatch(line,len,Print& out)` verb table (BUILT — for the `password` local command + any action verb's side effect)**, the `DATA_TYPE_REMOTE_CMD/_RESP` transport + `_remote_inbound` staging, the `channel_ctr`-lease NV write-coalescing pattern.
- **New:** the iterated-BLAKE2b password KDF wrapper, the `admin_pubkey`/`admin_counter_floor` NV fields, the **sealed-command trial-open + counter gate** in the reworked `remote_exec`, the open/gated classifier, and the **`password` `dispatch` verb** (local-only). (No signed-envelope codec — §0.1 replaced the Ed25519-signature envelope with the AEAD-sealed DM.)

## 10. Feature-split gating — static + gateway only, never mobile
The **entire remote-command subsystem** — the transport (`DATA_TYPE_REMOTE_CMD/_RESP`, `send_remote_cmd`/`send_remote_response`, the `_remote_inbound` RX staging at `node_mac_rx.cpp:803`), `remote_exec()`, **and all of the new auth** (`admin_pubkey` + `admin_counter_floor` NV, the iterated-BLAKE2b KDF, the sealed-command codec, the verify gate) — is gated behind a new compile-time feature **`MR_FEAT_REMOTE_MGMT`**, per `lib/core/mr_features.h` + the feature-split design (`docs/superpowers/specs/2026-07-12-firmware-feature-split.md`).

- **On (default `1`):** the full/production build (**static nodes** — mobile-host "option A" is the full build), the **`MR_PROFILE_GATEWAY`** build, and `native`/`lus` (all-features). Static relays and gateways are exactly the **infrastructure you can't easily reach physically** — the reason remote management exists.
- **Off (`MR_FEAT_REMOTE_MGMT 0`):** the **`MR_PROFILE_MOBILE`** build — **one line** in that profile block (like `MR_FEAT_MOBILE_HOST 0` in the gateway block). A mobile is a **roaming personal endpoint managed locally by its owner** (companion/BLE), never a remotely-administered relay.

**Rationale — both feature-split axes:**
1. **Flash/RAM** (the split's purpose): a mobile image sheds the `admin_pubkey`/counter NV, the iterated-BLAKE2b KDF + the X25519 seal/verify path, and the rcmd transport/exec — dead weight on a roaming endpoint.
2. **Attack surface:** a mobile is the **most physically-capturable** node (it travels with a person). Compiling remote-mgmt out means a captured mobile holds **no `admin_pubkey` to offline-grind** (§2's caveat simply doesn't apply to mobiles) and runs **no verify/exec path** — a captured mobile can't be a foothold into fleet admin.

**Convention:** feature STATE is `#if MR_FEAT_REMOTE_MGMT`-declared and the API stubs **inert** when off — a mobile that *receives* a `REMOTE_CMD` DM has no handler compiled, so the frame is silently dropped (unknown DATA type), like any other compiled-out feature. `MR_FEAT_REMOTE_MGMT` is **independent** of `TEAM`/`MOBILE` (a static relay has remote-mgmt with neither); the only rule is `MR_PROFILE_MOBILE ⇒ 0`.

## 11. Parked (explicitly out of scope for v1)
- **Admin quorum / network governance** (M-of-N admins agree → a fleet-wide config change applies to all nodes). It composes with this design — a quorum is *K signatures instead of 1* over a payload, and it would ride the existing R6 leaf-config (`lineage_id`/`config_epoch`/`config_hash`) propagation plane. It needs **per-admin pubkeys + a threshold policy** the one-password model deliberately doesn't hold. **Forward-compat hook:** the gated-command flag/version byte leaves room to later carry a *list* of `{signer, sig}` (length 1 now, K later) — no wire rework.
- **Delegation / credential transfer** (`A→B→C`) — rejected in §3; not revisited.

## 12. Testing
- **Native:** a valid **sealed** `reboot` opens + executes; a bad tag / wrong-`node_key_hash` / stale-`counter` all reject; the reject-hint fires only after a successful open; open reads (`status`/`routes`) answer with an **unsealed binary TLV** (decodable by `dec_status`/`dec_routes`) with no seal; a gated read answers **sealed**; `password rotate` (old-admin-sealed) swaps the pubkey + resets the floor; **iterated-BLAKE2b(password)→pubkey is deterministic** (a fixed vector) and password-sensitive (1-char change → different key); the **`password` verb is rejected when arriving over the mesh** (local-only). Round-trip the sealed command + the binary response end-to-end. (native = all-features, so `MR_FEAT_REMOTE_MGMT=1`.)
- **s18 byte-identity:** the auth lives in `fw_main`/`device_nv` (device-side) + a new DATA-type/flag that a static node never emits → **s18 must stay `3ac88d40…`** (the mandatory static-inertness gate). The signed-envelope codec in `lib/core` must be `0`-flag byte-identical for non-signed frames.
- **Feature-split builds:** the **`MR_PROFILE_MOBILE`** env compiles clean with `MR_FEAT_REMOTE_MGMT=0` — all rcmd/auth stubbed inert, **no `admin_pubkey` NV field on a mobile**; a mobile that receives a `REMOTE_CMD` frame drops it (no handler). The **`MR_PROFILE_GATEWAY`** + full/native builds keep it on.
- **Boards:** all envs build. The KDF is **CPU-only (no memory work-area)** — so it's a **time-budget** check (the iteration count `N` must derive in a few seconds on the nRF52), NOT a RAM check. (Mobiles are exempt — they don't compile it.)

## 13. Open items (for the plan / review)
- The KDF **iteration count `N`** — calibrate at the bench for ~1–2 s on the nRF52 (CPU-only; no RAM constraint). Higher `N` = slower offline grind, but also a slower local `password` command — pick the knee.
- Framing: confirm the **flag-byte-on-REMOTE_CMD** (recommended, §6) vs a new DATA type.
- The `cfg`/`duty` open-vs-gated question is **RESOLVED** (§0.2/§4): only `status`+`routes` are open; `cfg`/`duty`/`limits`/`faults`/`gateway` are gated (they leak config).
- Which `status`/`routes` binary fields an **unauthenticated** caller may see (the open `enc_status` may want a trimmed field set vs the gated one — a §later field-mask, mirrors the `cfg`-overflow mask in the encoders spec §6).
