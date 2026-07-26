<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-Admin Challenge–Response — design spec (2026-07-26, ★ FULLY RATIFIED — all §6 questions answered by the owner 2026-07-26; dispatch-ready)

## 0. Summary

Replace the monotonic replay counter (known-broken over a lossy link — see the `§admin-replay-REDESIGN-OWED`
note in `src/firmware_remote.cpp:72`) with a **node-issued single-use challenge**. The node owns a fresh
random challenge in RAM; a sealed command must carry the node's *current* challenge to be accepted; on
acceptance the node rotates the challenge and returns the new one in its (sealed) response. A lost command
costs one retry (the challenge was never consumed); a lost response is de-duplicated (exactly-once) via a
small recent-challenge cache; a reboot re-randomises and the operator resyncs in one round trip. **Clock-free,
RAM-only — the entire broken NV-persistence leg (`§nv-unchecked [3/5]`) is DELETED, not hardened.**
Ruled a **v1 feature** by the owner 2026-07-26. Challenge-response chosen over the time-window alternative
(no reliable clock on mobile/off-grid nodes).

## 1. Present state (code-verified 2026-07-26)

- **Transport (unchanged)**: sealed admin command rides `DATA_TYPE_REMOTE_CMD = 6`, response
  `DATA_TYPE_REMOTE_RESP = 7` (frame_codec.h:464-465). Inner body = `[REMOTE_FLAG_SEALED 1][sealed blob]`;
  an open read is a cleartext inner (firmware_remote.cpp:93/117). Both types are anti-spam/relay-exempt
  (node_mac.cpp:551/755) and ride normal routing/ACK.
- **The sealed blob** (`admin_cmd_seal`, admin_auth.cpp:31): `[rand8 8][nonce_ctr 2][ct][tag 16]`, plaintext
  = **`[node_key_hash 4][counter 4][cmd…]`** — THIS PLAINTEXT is what changes. Crypto = ECDH(admin,node) →
  dm_kdf → XChaCha20-Poly1305, `dm_nonce(rand8, nonce_ctr, node_key_hash)`. The response reuses
  `admin_cmd_seal` symmetrically with counter=0 (remote_seal_resp, firmware_remote.cpp:62).
- **The counter logic (being removed)**: sender `++g_admin_tx_ctr` per send (firmware_remote.cpp:203);
  receiver accepts `counter > floor` (admin_auth.h:44), advances + persists the floor via an UNCHECKED NV
  save (firmware_remote.cpp:105-113); `replay` verdict returns a `floor=N` hint (firmware_remote.cpp:98-102).
- **NV**: `admin_pubkey[32]`, `admin_provisioned`, `admin_counter_floor` (src/device_nv.h Blob). The floor
  field becomes dead (§4).
- **Admin key**: derived from a password via iterated BLAKE2b (`admin_key_from_password`), pinned per node;
  `admin_provisioned` gates the whole sealed path. UNCHANGED by this spec.

## 2. Design

### 2.1 Node challenge state (RAM only)
- `uint64_t _admin_challenge` — the current outstanding challenge. Seeded from the device crypto RNG at
  boot AND at every admin-key (re)provision. Re-roll if it lands on 0 (0 is reserved, §2.4).
- A small recent-consumed ring `AdminConsumed _admin_recent[N]` (N=4 recommended, RAM), each =
  `{uint64_t challenge, uint8_t resp[≤64], uint8_t resp_len}` — the exactly-once cache (§2.3). Sized to
  cover a burst of retries; evict-oldest.
- **NONE of this is persisted.** A reboot means a fresh challenge — an operator command minted against the
  pre-reboot challenge is stale and gets the resync hint (§2.4). Reboot is rare; this is the correct trade
  (it deletes the unchecked-NV-save replay hole entirely).

### 2.2 Sealed COMMAND plaintext (the format change)
`[node_key_hash 4][challenge 8][cmd…]` — the 4-byte counter becomes an 8-byte challenge (net +4 B). The
node_key_hash binds the command to this node (unchanged anti-cross-node check). Everything else in the
sealed-blob wrapper (rand8, nonce_ctr, ct, tag) is UNCHANGED.

### 2.3 Node verify + execute (the lifecycle)
On a sealed command opening successfully (tag + node_key_hash match — else silent drop, no oracle) with
carried challenge `X`:
1. **`X == _admin_challenge`** (the happy path): execute the command; build the response; **rotate**
   `_admin_challenge` to a fresh nonzero random `C'`; store `{X, response}` in `_admin_recent`; return the
   response (which carries `C'`, §2.5). Advance = "consumed X, now expect C'."
2. **`X` in `_admin_recent`** (a retry whose response was lost): **re-send the cached response, do NOT
   re-execute** — this is the exactly-once guarantee. The cached response already carries the post-execution
   challenge, so the operator resyncs from it.
3. **`X == 0`** (bootstrap/resync — §2.4): do NOT execute; return a response carrying the current
   `_admin_challenge` (sealed). This is how a cold or desynced operator learns the challenge.
4. **else** (stale/unknown `X`): the resync path — return a response carrying the current `_admin_challenge`
   (sealed), no execution. (Replaces the old `floor=N` hint; same one-round-trip recovery, now self-priming.)
   Emit a rate-limited `admin_challenge_resync` telemetry for observability.

⚠ **The exactly-once ring (step 2) is the load-bearing subtlety** — without it, a command retried because
its RESPONSE was lost would re-execute (double reboot / double key-rotate). This is the standard RPC dedup;
the coder MUST implement + test it (a retry with a just-consumed challenge replays the cached response and
does not re-run the verb). N=4 covers realistic retry bursts; document the bound (a retry older than N
consumed challenges falls to step 4 = resync, which is safe, just costs a round trip).

### 2.4 Bootstrap & recovery (challenge value 0 reserved)
`challenge == 0` in a command = "I have no valid challenge — tell me the current one" (never executes,
§2.3 step 3). It is itself a sealed, admin-authenticated frame (only the real admin can mint it), so the
node never hands challenges to non-admins. `_admin_challenge` is always nonzero (re-roll on 0). This folds
cold-start (operator's first ever contact) and post-reboot/post-loss recovery into ONE path — no separate
"get challenge" verb. The operator's client: on any resync/bootstrap response, cache the returned challenge
and (if a command is pending) retry it with the fresh value.

### 2.5 Sealed RESPONSE plaintext (also changes)
`[resp_challenge 8][body…]` — every sealed response leads with the challenge the operator should use for its
NEXT command. Split cleanly from the command seal: parameterize `admin_cmd_seal`/`_open` to carry the
8-byte leading field as either a command-challenge or a response-challenge (U2: one codec, a role param —
do NOT fork a second sealer). Open (unsealed) reads: unchanged cleartext TLV, and they do NOT carry a
challenge (an open read needs no freshness — it's a public diagnostic); the operator bootstraps the
challenge via §2.4 when it first needs to send a SEALED command.

### 2.6 Sender (operator / g_admin) side
- Drop `g_admin_tx_ctr` entirely. The client caches `challenge_for[node_hash]` (learned from responses).
- To send a sealed command: use the cached challenge, or `0` if none cached (bootstrap). On a resync
  response, update the cache and auto-retry the pending command once.
- `rcmd` console output: report the challenge state ("resync — retry") instead of the old `ctr=`/`floor=`.
- The reject-hint round trip is GONE as a distinct concept — resync is just the normal response path.

## 3. Slices (gateable, sequential)

- **RA-1 (codec)**: change the sealed plaintext (command `[hash][challenge8][cmd]`, response
  `[challenge8][body]`); parameterize `admin_cmd_seal`/`admin_cmd_open`; update `AdminCmd`/verdict.
  Native KATs: seal→open round-trip both roles; a wrong-challenge open still opens (challenge is a
  post-open check, not part of the AEAD key) but verdict = resync. s18 byte-identical (admin is
  provisioned-only; no scenario provisions admin → inert — VERIFY).
- **RA-2 (node lifecycle)**: `_admin_challenge` + the recent-ring + the four-way verdict (happy / dedup /
  bootstrap / resync) in firmware_remote.cpp; rotate-on-accept; RNG seeding at boot + provision. DELETE
  the counter-floor read/advance/persist and the `admin_counter_ok`/`admin_counter_check_advance` path.
  Native tests: happy path rotates; lost-response retry replays cached response WITHOUT re-exec (the
  exactly-once test); challenge=0 bootstrap returns current, no exec; stale → resync; reboot → fresh.
- **RA-3 (sender + console + companion)**: client challenge cache + auto-retry; `rcmd` output; the companion
  contract (the remote-admin verbs/responses — list the JSON changes for QA). If the companion doesn't yet
  do remote admin, this is console-only + a contract stub.
- **RA-4 (NV cleanup)**: drop `admin_counter_floor` from the Blob (kVersion bump — reprovision-on-reflash
  noted); remove the two unchecked-save sites for it (firmware_remote.cpp §nv-unchecked [3/5]); update the
  §admin-replay-REDESIGN-OWED note to "DONE — replaced by challenge-response" (don't just delete the note;
  it's a decision record).

## 4. NV & the dead field
`admin_counter_floor` is removed (kVersion bump; `admin_pubkey`/`admin_provisioned` stay). This closes
`§nv-unchecked [3/5]` by DELETION — there is no longer a replay floor to persist, so no unchecked-save
replay hole. (The OTHER unchecked save, [4/5] the admin-key-rotate at firmware_remote.cpp:139, is a
SEPARATE issue — a rotate whose NV write fails leaves a volatile new key; that one still needs its own
ruling and is NOT in this spec's scope. Flag it, don't fold it in — C1.)

## 5. Security notes (state in-code)
- The challenge need NOT be secret — an attacker who sees it still cannot forge a sealed command (no admin
  key). Its only job is REPLAY prevention (single-use + rotation). It MUST be unpredictable enough not to
  collide/reuse across reboots → 64-bit crypto-RNG. A sequential challenge would also prevent replay but
  risks reuse after a reboot re-seed; random avoids it.
- The exactly-once ring prevents a captured-and-replayed valid command from re-executing (the challenge is
  consumed) AND prevents an honest retry from double-executing (cached response replay). Both covered by
  one mechanism.
- No forward secrecy change; the admin key + KDF are untouched. `admin_provisioned` still gates everything;
  an un-provisioned node silent-drops sealed commands as today.

## 6. Open questions — ★ ALL RULED by the owner 2026-07-26
- **Q1 — RULED: N = 4.**
- **Q2 — RULED: open (unsealed) reads are challenge-FREE** — public diagnostics need no freshness; only
  SEALED (gated) commands carry a challenge.
- **Q3 — DEFERRED (owner 2026-07-26): NOT required for v1.** With the companion doing full remote admin
  (Q4) and the client self-priming via the `challenge==0` bootstrap (§2.4), the open-read piggyback is a
  pure round-trip-saving nicety, not a correctness need — and §2.5 already specifies the canonical
  no-piggyback behavior (open reads carry no challenge; the client bootstraps on first SEALED contact).
  Deferring it keeps the spec internally consistent. Revisit only if bootstrap latency is measured to
  matter in the field. **No RA-slice implements Q3.**
- **Q4 — RULED: the iOS companion DOES remote admin at v1.** RA-3 is a FULL companion slice, not a stub:
  the client-side challenge cache + auto-retry live in the companion, and INBOX_SYNC_CONTRACT gains the
  remote-admin verbs + responses (sealed-command send, the response/challenge fields, the resync/bootstrap
  flow, the unlock-with-password gate). The companion is where an operator will most often drive remote
  admin — the UX must surface the challenge lifecycle as invisibly as possible (auto-bootstrap on first
  contact, auto-retry-once on resync). This makes remote-admin a v1 companion feature alongside the team QR.

## 7. Gate expectations
Every slice: **s18 BYTE-IDENTICAL** (admin is provisioned-only; verify no scenario provisions it → inert) ·
mandatory suite 0-fail (this touches no mesh/team/routing plane) · native KATs per slice, above all the
exactly-once test (RA-2) · boards · NV kVersion bump = reprovision-on-reflash for the bench. Docs land with
the slices: frames.md (the admin sealed-plaintext layout: command + response), protocol.md (the remote-admin
section — replace the counter description with the challenge lifecycle), INBOX_SYNC_CONTRACT (any companion
verb/response changes). The `§admin-replay-REDESIGN-OWED` note becomes a DONE decision-record.
