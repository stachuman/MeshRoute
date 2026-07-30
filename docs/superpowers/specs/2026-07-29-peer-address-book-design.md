<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# DESIGN SPEC — the peer address book

*2026-07-29. Owner-requested. All line references code-verified today against HEAD `3b7bf58`; re-verify before
relying on one (V1/V2) — this tree moves several times a day.*

**Status: SPEC'D AND COMPLETE, NOT DISPATCHED — ready to start.** ✅ The cross-layer cleartext fix that was
holding `lib/console/console_parse.cpp` + `console_json.cpp` **has landed** (`§xl-crypt`), as have the two bug
fixes after it, so **AB1 is unblocked and AB2/AB3 are free of file contention.** ⚠ **Two decisions are marked
OPEN in §2.6 — settle them before dispatching AB3**, not during it.

---

## 0. What the owner asked for, and what is actually missing

> *"address book - list of known peers: static_id(optional) AND/OR team_id(optional), hash, name,
> is_key_available … 1. hash (name, key) is not always known - sometimes we have only id 2. if hash is known -
> then it is unique key"*

The requested row shape is **right**, and every field already exists somewhere. Two things are missing, and one
of them causes a user-visible failure we spent a whole bench session diagnosing:

1. ★★ **Confidence is not exposed.** `peer_key_cached` emits a **hardcoded literal** `"pinned":false`
   (`console_json.cpp:250`). No code path makes it say anything else. So the app cannot distinguish
   `overheard` (key present, **cannot seal**) from `authoritative` (**can seal**) — sealing requires
   `conf >= authoritative` (`node_hashlocate.cpp:387`). ⇒ the app offers "send encrypted", the user tries, and
   gets `FAILED (no recipient pubkey)`.
2. ★★ **The app cannot push a name.** `peerkey` takes **exactly one** argument, the 64-hex pubkey
   (`console_parse.cpp:154-162`). `peer_key_set` *has* a `name` parameter whose **only** external caller is
   `fw_main.cpp:726`'s boot restore — which passes nothing. So a rename in the app can never reach the node.

Plus the amnesia that makes both worse: **NV stores neither names nor on-air keys** (§1.4).

## 1. Present state (code-verified 2026-07-29)

### 1.1 The three tables — and the join key is already `key_hash32`

| table | keyed by | fields | cap |
|---|---|---|---|
| `_peer_keys` | **`key_hash32`** | `ed_pub[32]`, `confidence`, `name[32]`, `name_len`, `peer_confirmed`, `last_seen_ms` | **16** |
| `_id_bind` | **`node_id`** | `key_hash32` (may be **0**), `source`, `confidence`, `last_seen_ms` | **256** |
| `_team_keys` | **team `id`** | `key_hash32`, `last_seen_ms` | **16** |

Also relevant: `_team_peer` (a bitmap of teammate ids, may have **no** `_team_keys` row) and
`MobileHomeBinding` (`mobile_hash → home_id`, whose own comment says **"No bijection"**).

★ **The cardinality is driven by `_id_bind` (256), not `_peer_keys` (16).** Most rows will therefore have a
hash and **no name and no key**. That shapes the UI and the dump sizing, not just the implementation.

### 1.2 The identity model — the owner's point 2, confirmed and sharpened

**The hash is the only stable identity.** `key_hash32 == ed_pub[:4]`, and `peer_key_set` **verifies** it
(returns false on mismatch), so it is cryptographically bound, self-verifying and documented *"IMMUTABLE key,
MUTABLE name"*. By contrast a static `node_id` is DAD-assigned and collidable, and a `team_local_id` is
per-team and reassigned by team DAD.

⇒ **ids are ADDRESSES; the hash is the IDENTITY.** An address book keyed on an id would alias and rot.

★ And **one hash can legitimately hold both a `static_id` and a `team_id`** — the §18 dual-identity space that
T6 dealt with. So `static_id?` **AND/OR** `team_id?`, both optional, is the correct shape.

### 1.3 Id-only rows are real — the owner's point 1, confirmed

Two distinct flavours, both representable and both needing a row:
- `_id_bind[i].key_hash32 == 0` — a static id from a beacon with no hash yet. The accessor documents this
  explicitly: *"0 if we've heard no beacon for it"*.
- a `_team_peer` bit set with **no** `_team_keys` row — a teammate we route to whose hash we never cached.

### 1.4 Persistence — one table with a lossy partial backup, not two tiers

`_peer_keys[16]` is the only store. `/mrpeers` (`PeerBlob`, `device_nv.h`) backs up a **subset** and loses three
things:

| loss | evidence |
|---|---|
| **no names** | `PeerRec { key_hash32; ed_pub[32]; }` — that is the whole record, and the boot restore calls `peer_key_set(hash, ed_pub, pinned)` with no name (`fw_main.cpp:724-726`) |
| **pinned only** | *"PINNED keys only"*, re-installed as `PeerKeyConf::pinned` ⇒ **`authoritative` keys learned on-air do not survive a reboot** |
| **same cap** | `kMaxPinnedPeers = 16` == `cap_peer_keys` |

Written **only** by the `peerkey` command. `_id_bind` and `_team_keys` are **RAM only** — nothing persists them.
⇒ After a reboot the node knows pinned keys, **nameless**, and nothing else.

## 2. Design

### 2.1 The view is GENERATED, not stored (owner-agreed)

**No fourth table.** The address book is a **join on `key_hash32`** over three tables that are already
maintained. A stored copy would need syncing on every `peer_key_set` / `id_bind_set` / `team_key_set` and every
eviction — and this arc has been spent finding ledgers that drifted in exactly that way. A generated view costs
**zero RAM** and cannot go stale.

```
row = { hash?, static_id?, team_id?, name?, conf, peer_confirmed }
```

Emission, single pass, **deduplicating on `hash`**:
1. walk `_peer_keys` first (these are the rows that have names, keys and `peer_confirmed`);
2. walk `_id_bind` — hash present and already emitted ⇒ **merge** `static_id`; hash present and new ⇒ new row
   (no name, `conf` absent); hash **0** ⇒ id-only row;
3. walk `_team_keys` — merge `team_id` by hash, else a new row;
4. walk `_team_peer` bits nothing else covered ⇒ team-id-only rows.

⚠ **The reverse lookup (hash → id) needs a scan and may be AMBIGUOUS** — two `node_id`s can carry one hash if
one is stale (a node that changed id, a mobile local id colliding). Resolve by freshest `last_seen_ms`, and
**say in the emit** if the loser was dropped rather than silently picking.

### 2.2 `conf` replaces the boolean (owner-agreed)

`is_key_available` becomes the **level**, because a boolean is what creates the failure in §0.1:

- the view carries `conf` ∈ `none` | `overheard` | `authoritative` | `pinned`;
- **`peer_key_cached` gains `"conf":"…"`** and **keeps** its existing `pinned` field as a derived duplicate
  (`conf == "pinned"`) — it is a documented field and breaking it is not worth the tidiness;
- **`peer_confirmed` is exposed too** (already stored): it means *they hold OUR key*, i.e. a sealed reply can
  return. That is the difference between "I can encrypt to them" and "we can talk securely", which is what an
  address-book UI actually wants.

★ **The app must gate its "send encrypted" affordance on `conf >= authoritative`, not on presence** — that is
the whole point of this change, and it belongs in the contract as an obligation, not a hint.

### 2.3 `peername` — a new verb (owner-ruled 2026-07-29)

```
peername 0x<hash> "<text>"     →  {"ev":"peer_name_set","hash":<u32>,"name":"<text>"}
```
- Sets/overwrites the cached name **without touching the key or the confidence.**
- Chosen over extending `peerkey` because **rename-without-rekey is the common case**: a peer advertises the
  default `MeshRoute node: 0x…` and the user wants a real label; with `peerkey name=` alone that would mean
  re-sending the whole 64-hex pubkey to change a string.
- **C2:** refuse loud if the hash is unknown (nothing to attach a name to), if the name exceeds 32, and on a
  malformed hash. Never create a keyless placeholder row as a side effect.
- ⚠ **Optional companion one-shot, not required:** `peerkey <hex64> name="<text>"` for the QR-import flow where
  key and name arrive together. Both paths must land in the **same** `peer_key_set(…, name, name_len)` call —
  the parameter already exists (U1/U2, no new machinery).

### 2.4 NV: persist the name **and** on-air keys (owner-ruled 2026-07-29, both)

```c
struct PeerRec { uint32_t key_hash32; uint8_t ed_pub[32];
                 uint8_t confidence;              // NEW — never silently promoted
                 uint8_t name_len; char name[32]; };   // NEW
```
- `kPeersVersion` **1 → 2**. ★ **Independent of the config `kVersion`** — the in-source comment says *"a format
  change just bumps kPeersVersion (no migration)"*, so this does **not** touch the reprovision-on-reflash story
  that T-K1's `kVersion 22` created.
- **Persist `authoritative` as well as `pinned`**, storing the confidence so **nothing is silently elevated** —
  pinned stays pinned, authoritative stays authoritative. Today a reboot costs the ability to send encrypted to
  every on-air peer, recoverable only by a manual `reqpubkey` each (and §0's ruling means that stays manual).
- Boot restore must re-install **with the stored name and the stored confidence** — the current call passes
  neither (`fw_main.cpp:726`).
- ⚠ Cap stays **16**. If `authoritative` keys now compete with `pinned` for those slots, **pinned must win** —
  an eviction policy question to settle in the slice, not silently.

### 2.5 ★★ The lookup verbs read DIFFERENT id namespaces — bench-proven 2026-07-30

Added after the owner hit it on metal. This is §1.2's identity model failing in miniature, and it is the clearest
possible argument for the row shape in §2.1.

**The transcript, on team node 114 (off-grid, `team_local_id = 114`), asking about 228:**

```
hashof 228                     -> id=228 -> unknown
reqpubkey 228                  -> »tx H … hash=6C297145 … HARD WANT_PUBKEY     (it KNEW the hash)
                               -> KEY CACHED hash=0x6C297145 name=… (on-air, unpinned)
hashof 228                     -> id=228 -> unknown          ← STILL
```

**Cause — the two verbs interpret the same number in two different namespaces:**

| verb | a bare decimal means | table read |
|---|---|---|
| `reqpubkey <id>` | a **`team_local_id`** — *"resolve the hash from the team key cache"* (`console_parse.cpp:166`) | **`_team_keys`** |
| `hashof <id>` | a **static `node_id`** | `key_hash_for_id()` → **`_id_bind` only** (`node.h:624-628`) |

So `reqpubkey 228` found `6C297145` in the team cache and worked; `hashof 228` looked in `_id_bind`, found 0, and
said unknown. **Both are correct for the table they read.** Neither is correct as an answer to *"what is the hash
of 228?"*, which the node can answer perfectly well.

**The three owner questions, answered:**
1. **`reqpubkey` uses the TEAM id** (bare decimal ⇒ `team_local_id`, from the team key cache). It also carries a
   plane (`console_parse.cpp:181` assigns `team ? 1 : 2`).
2. **`hashof` uses the STATIC id**, and only `_id_bind`. ⇒ the owner-proposed `hashof <id> -t` is the right
   *shape*, and matches `send -t` / `send_hash -t` / `reqpubkey -t`. ★ **But the better answer is to make `hashof`
   a VIEW query** (§2.1): search **both** namespaces and **report which one matched**, because guessing is the
   defect. ⚠ **If the same number matches in BOTH** — the §18 collision, entirely possible — it must report
   **both rows**, never silently pick one. Keep `-t` (and a static counterpart) purely to *disambiguate* when the
   caller already knows which plane it means.
3. **"If we requested the pubkey, `hashof` should be known"** — agreed, and this is the sharpest of the three.
   After the exchange the node holds `_team_keys[228] = 6C297145` **and** `_peer_keys[6C297145] = {ed_pub, name,
   authoritative}`. Everything needed is present; only the *lookup* was aimed at the wrong table.

★★★ **AND THE OBVIOUS FIX IS FORBIDDEN — write it down so nobody takes it.** The tempting repair is to have the
pubkey answer write `228 → 6C297145` into `_id_bind`. **Do not.** That is the **plane-blind `_id_bind` ingest**
already on the bug queue: a *team-scoped* answer writing the *static* map is an **I2 breach**, the same class the
PLANE-A/B slice found and the same class as spec §12's read-path leak. ⇒ **Q3 is fixed by the VIEW, never by a
write.** A team id belongs in `_team_keys`; the view is what joins it to the hash for display.

**Consequences for §2.1's row:** this is why `static_id?` and `team_id?` are **separate optional fields** rather
than one `id`, and why the view — not any single table — is the only correct answer to an id-shaped question.
`hashof`, `nameof` and the `peers` dump should all read the **same** view, so they can never disagree again.

### 2.6 ⚠ TWO OPEN DECISIONS — settle these BEFORE dispatching AB3

**(a) How large may the dump be, and over which transport?** ★ **This is not cosmetic.** §1.1 measured that
cardinality is driven by `_id_bind` (**256**), not `_peer_keys` (16), so a naive full dump is a few hundred rows —
and a node **WEDGE from self-inflicted console flooding** is a defect this project has already fixed once (the
`mrcon` drop-never-block sink). Emitting that over BLE walks straight back into it.

★ **QA's proposed cut, for owner confirmation:**
- **The JSON address book = rows backed by `_peer_keys` (≤ 16)**, enriched with `static_id`/`team_id` from the
  other two tables. That is bounded, and it is what an address book *is* — an entry you can name and talk to.
- **The full "known nodes" list (up to 256 id-only rows) stays TEXT-console only**, behind an explicit `peers all`.
  It is a diagnostic, not a contact list.
- Rationale: it matches §6 (the app owns the durable book and reconciles from `peer_key_cached` pushes), and it
  keeps the JSON surface bounded by construction rather than by a paging protocol nobody has to implement.

**(b) Is `peer_name_set` a PUSH or a synchronous command ack?** §2.3 shows the shape but not the mechanism, and it
decides real work: **a push needs a new `PushKind` enumerator — which §5 requires be APPENDED**, because the sim
bridges `PushKind` on its raw `uint8_t` with a `static_assert` twinned in two sim files (T-K3 caught itself
inserting one). A synchronous ack needs no enum and no sim coordination.
★ **QA recommends the synchronous ack:** `peername` is operator-initiated and its result is immediate, so there is
nothing asynchronous to notify — and it avoids touching an enum whose ordering has already cost this arc two
near-misses. The `peerkey_set` ack is the precedent to mirror (U3).

## 3. Slices

- **AB1** — NV: `PeerRec` gains `confidence` + name, `kPeersVersion` 1→2, boot restore honours both, eviction
  policy pinned-over-authoritative. Gate: corpus byte-identical (NV is outside the sim build), native
  round-trip incl. a v1-blob rejection test.
- **AB2** — `peername` (+ optional `peerkey name=`), and `peer_key_cached` gains `conf`. Gate: byte-identical;
  native for every refusal; a JSON golden for `conf`.
- **AB3** — the generated view: a `peers` console dump + its JSON surface **bounded per §2.6(a)**, **and rewire `hashof`/`nameof` onto it**
  (§2.5 — they must not keep reading one table each). Gate: byte-identical; native for the merge/dedup incl. the
  ambiguous-reverse-lookup case, all four id-only/hash-only shapes, and ★ **the §2.5 regression directly: after a
  successful `reqpubkey <team-id>`, `hashof <that id>` must resolve** — with a test asserting `_id_bind` was **NOT**
  written (the forbidden fix).

★ **AB2 and AB3 both touch `console_parse.cpp`/`console_json.cpp`** — do not run them concurrently with each
other or with the cross-layer cleartext fix.

## 4. Out of scope (v1, explicit)

Naming an **id-only** peer (there is no hash to attach a name to — and no stable key to attach it *by*).
Persisting `_id_bind`/`_team_keys`. Any `overheard` → `authoritative` promotion. Auto-`reqpubkey` — **forbidden
by the 2026-07-29 ruling**, see `ios-companion/INBOX_SYNC_CONTRACT.md`.

## 5. Gate expectations

Standard gate (`docs/2026-07-26-slice-gate-method.md`). Specifics:
- **All 36 scenarios byte-identical** for every slice — `src/` and `lib/console/` are outside the sim build (T3
  measured this). ⚠ **State that as "the corpus cannot reach it", never as evidence of correctness**, and lean
  on native tests with same-line controls.
- `sizeof(Node)` **must not move** — no new `Node` members; the view is computed and NV is not `Node` state. If
  it moves, escalate to §D4's six-env grid.
- ★ **The four detector probes are a hard item** on every slice: P-T7/s38 **8 of 16** · P-T1/s35a **20** (site
  = `node.cpp:1309`, *not* the ack-gate fix — the earlier pin said `:1308`, one line off, and it cost a coder a run) · P-T6A/s37 **12 of 36** · P-T6A+P-T7/s37 **16 of 36**.
- ★ **If an enum is extended, APPEND** — the sim bridges `PushKind`/`SendFailReason` on their raw `uint8_t` with
  a `static_assert` twinned in two sim files; an insert silently renames a value for every scenario. And note
  `-Wswitch` fires for these only on the **board** build (`fw_main.cpp` is outside native) — T-K3's evidence.

## 6. The expectation to set with the app team

Even with §2.4, the node's book is a **16-slot cache**, and `_peer_keys` ages (`peer_key_find` → *"false:
absent/aged"*). **The app owns the durable address book**; the node's view is a **reconcile source** — *"here is
what I currently know"*, not *"here is the address book"*. That belongs in the contract as a stated property, so
the app team does not discover it.
