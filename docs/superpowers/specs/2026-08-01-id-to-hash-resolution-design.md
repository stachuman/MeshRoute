<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# id → hash resolution — consistent across both planes

**Status: v2 AGREED — S1 / S2 / S2b IN FLIGHT, S3 onward not dispatched.**

| slice | state (2026-08-01) |
|---|---|
| **S1** | implemented, **QA: changes requested** — four blockers (below); rework in flight |
| **S2** | implemented, **QA: ACCEPTED** |
| **S2b** | implemented, **QA: changes requested** — the upgrade-only rule does not cover a rehome; rework in flight |
| **S3 · S4a · S4b · S5** | **not dispatched** — S4a/S4b wait on the §7 rulings (verb name, BLE availability, `HARD`-under-`BY_ID`) |

★ **Implementation assessment: `docs/2026-08-01-id-to-hash-resolution-implementation-assessment.md`** (independent QA,
the same reviewer as v1). Its four blockers were re-verified against source and **all upheld**: a claimed *rehome* still
evicts an authoritative binding through `id_bind_evict_other_hash_holders`; the checked-in companion's `reqPubkeyTeam`
emits a bare id whose meaning this spec changed from TEAM to AUTO; `reqpubkey_sent` can claim a flood that never
happened at **four** bail points, not the one B47 records; and `peer_book_by_id`'s display de-duplication suppresses a
plane bit at an **airtime** boundary, breaking D9 and explicit `-t`. ⚠ **The last two are lessons about this spec, not
just its code: D9 was specified against a resolver whose mask was built for rendering, and §5 never said what a
"queued" reqpubkey means when nothing flew.**

**v2 supersedes v1 in response to `docs/archive/2026-08-01-id-to-hash-resolution-design-review.md` (disposition: revise before
implementation).** All nine review findings were re-verified against source and **all are upheld** — none is disputed.
v1's two headline claims were wrong and are retracted in place: §3-D4 ("zero receiver changes") and §6-O3 ("genuinely
two-sided"). One requirement is **added beyond the review** (§3-D5c, the `_team_keys` liveness/eviction rule).

Every factual claim carries a `file:line`. Claims still needing measurement are ⚠ MEASURE — the coder's first
obligation, never an assumption to inherit.

---

## §0 The bench evidence

**Team plane** — 4-member team, `reqpubkey 114 -t` → `err_no_binding`:
```
[peer] hash=0x7B18ADA2 static_id=245(auth)      [peer] team_id=114   <- routable, unidentifiable
[peer] hash=0x6C297145 team_id=228              [peer] team_id=214   <- routable, unidentifiable
```
**Static plane** — same wall, `reqpubkey 109` → `err_no_binding`:
```
[route] dest=48  next=186 hops=3     [peer] hash=0x8CC9BDFF static_id=42(auth)  <- this is US
[route] dest=59  next=186 hops=2     [peer] hash=0x61CD83EA static_id=186(auth)
[route] dest=109 next=186 hops=2     [peers] count=2
[route] dest=186 next=186 hops=1
```

**Owner ruling that frames everything (2026-08-01):** *"we can be sure only if we scan QR or exchange keys out of
network. Otherwise we never can be sure."* ⇒ an on-air id→hash answer is a **claim**, and that must live in the data
model, not in a comment.

★ **This spec is not inventing the team-side store.** `on_hash_bind_snoop`'s header already scoped it and marked it
`✖ MISSING` on 2026-07-31 (`node_hashlocate.cpp:1245-1248`): *"a team-plane bind store with its own confidence field …
**needs the trust question in (1) answered first**."* The ruling above **is** that answer. This is the deferred half of
§hashbind-plane / register B2.

---

## §1 Verified state — five defects

### A — `reqpubkey <id>` is team-only by construction (no wire change to fix)

`console_parse.cpp:232` — `bool team = (out.u.resolve.dst_id != 0);` A bare decimal **forces** the team plane; line 234
accepts only `-t`, so no `-s` exists. `node.cpp:1646` then resolves via `team_key_of_id` alone, whose first line is
`if (_cfg.team_id == 0 || !is_team_peer(id)) return false;` (`node_routing.cpp:842`).

⇒ **on a static node `reqpubkey <id>` cannot succeed for any id.** Two commands on the bench node prove it:

| | reads | result |
|---|---|---|
| `hashof 186` | `key_hash_of_id` → `_id_bind` | `0x61CD83EA` |
| `reqpubkey 186` | `team_key_of_id` → `_team_keys` | `err_no_binding` |

Verbatim the defect `firmware_commands.cpp:527-530` records from 2026-07-30 — *"Each verb was correct about its own
table; neither answered the question."* **That fix landed on `hashof` and never on `reqpubkey`.**

★ **A SECOND SITE, and S1 is half-landed without it** (found while verifying the review; the sweep-scope meta-bug's
tenth instance — directory vs file scope, verb-prefix matching, and now transport scope). `src/fw_main.cpp:490-491`, the
**BLE** `reqpubkey_sent` echo, resolves the hash through one table too:
```cpp
if (rh == 0 && cmd.u.resolve.dst_id != 0) (void)g_node.team_key_of_id(cmd.u.resolve.dst_id, rh);
```
⇒ after `on_command` is fixed, a static-plane by-id `reqpubkey` over BLE still echoes `hash=0` to the companion. **Both
sites must move in S1**, and both must route through `peer_book_by_id` (U1) rather than gaining a second hand-rolled
two-table lookup.

### B — no id→hash for a node we route to but never heard directly

`_team_keys` is written at exactly one site, `node_beacon.cpp:831`, reached only from a **directly-heard same-team
beacon**. Multi-hop teammates get the `_team_peer` dispatch bit from the DV merge (`node_beacon.cpp:939-940`) off a
route entry carrying no key. Static is the same shape: `_id_bind` is fed by a heard beacon (`node_beacon.cpp:664`) or
an H answer, never by a route. **The only part needing the wire.**

### C — the `peers` view is asymmetric between planes

`peer_book_walk` has four passes (`node_hashlocate.cpp:462-507`): `_peer_keys` → `_id_bind` → `_team_keys` →
`_team_peer` bits. The fourth surfaces "teammate we route to, no key" as an id-only row. **No pass over `_rt`** ⇒ no
static equivalent. Hence 114/214 visible, 48/59/109 not.

### D — we list ourselves as a peer

`id_bind_set(_node_id, _key_hash32, IdBindSource::self, …)` (`node.cpp:77`, `:539`, `:864`) puts our own binding in
`_id_bind`; pass (2) has no self-skip ⇒ `static_id=42(auth)` on node 42. Text-console only — the JSON book passes
`include_id_rows=false` (`:466`) and we are not in `_peer_keys`.

### E — ★ `id_bind_set` is NOT upgrade-only (a LIVE defect, found by the review)

On a matching row, `node_hashlocate.cpp:102-104` writes the incoming `source` and `confidence` **unconditionally**:
```cpp
_active->_id_bind[i].last_seen_ms = now;
_active->_id_bind[i].source       = static_cast<uint8_t>(source);
_active->_id_bind[i].confidence   = static_cast<uint8_t>(confidence);
```
⇒ **a `claimed` observation silently DEMOTES an `authoritative` binding today**, with no new feature required: a
relayed soft H answer (`IdBindSource::h_relay`, `:1252-1253`) demotes a first-hand beacon binding, and the seal path
then refuses (`key_hash_of_id:148` filters non-authoritative) until the next beacon re-asserts it. This is a
**prerequisite** for D8, not an optional extra — and it wants registering on its own merits.

★★ **AND THE MATCHING-ROW WRITE IS ONLY ONE OF THE TWO DOORS — added after the implementation assessment (P1a).**
`id_bind_set` also enforces the reverse uniqueness rule (one hash → one id) via
`id_bind_evict_other_hash_holders(key_hash32, node_id)`, called from **both** accepted paths
(`node_hashlocate.cpp:137`, `:142`) and **taking no account of confidence**. So guarding only the same-row write leaves
the demotion fully alive through a **rehome**: authoritative `{id=10, H}` + a claimed `{id=20, H}` relayed answer evicts
the authoritative row and inserts the claim, and `key_hash_of_id` stops answering for `H`. ⇒ **the fix must inspect the
existing holder's confidence before reverse-holder eviction**: refuse a *claimed* rehome against an authoritative
holder, keep authoritative rehome working, and leave claimed→claimed as newest-wins (owner-ruled 2026-08-01 — there is
no trust ordering between two claims). ⚠ **No poison probe over the current corpus can catch this** — the corpus
contains **zero** claimed bindings in 304 885 measured samples, so the coverage must be native.

⚠ **This is also a correction to v1-O2:** there is **no `IdBindConf` NV encoding at all** (`grep` over
`src/device_nv.h` returns nothing; `kPeerConf*` is `PeerKeyConf`'s). `_id_bind` is RAM-only and TTL-bound at 48 h
(`protocol_constants.h:535`). v1's "a third tier reaches the NV encoding" was false.

---

## §2 The trust model

| binding | verifiable? | why | store | ladder |
|---|---|---|---|---|
| hash → pubkey | **YES** | `peer_key_set:250-252` recomputes `key_hash32_of(ed_pub)` and refuses a mismatch — *"A forged binding is REFUSED"* | `_peer_keys` | `PeerKeyConf{overheard,authoritative,pinned}` |
| **id → hash** | **NO** | an id is an address, not a commitment (`command.h:64`) | `_id_bind` / `_team_keys` | `IdBindConf{claimed,authoritative}` / **none** |

1. The owner's "we can never be sure" applies to **row 2 only**. Once a hash is trustworthy, fetching its pubkey over
   the air is cryptographically safe — which is why every on-air pubkey ingest caches `authoritative` and is right to.
2. **A QR sidesteps the question rather than answering it** — it hands you a hash and you address by hash. No id in the
   ceremony.
3. Static already models this: `IdBindSource{self,bcn,h_query,h_relay}` = *how*, `IdBindConf` = *how much*, with real
   discrimination in force (`node_beacon.cpp:664` first-hand → `authoritative`; `node_hashlocate.cpp:1077` H answer →
   `authoritative` iff the answerer was the owner). Team has neither field: `TeamKey{id,key_hash32,last_seen_ms}`
   (`node.h:2013`).
4. ★ **`PeerKeyConf::overheard` has no producer.** All five occurrences are read-initialisers or the JSON name mapper
   (`console_json.cpp:170`). The tier exists in the ladder, in NV and in the app contract with nothing writing it. **Out
   of scope here** (§8) — recorded so a reviewer does not assume the low tier is exercised somewhere and reason from it.

---

## §3 Design decisions

### D1 — one resolver, and the floor reaches EVERY reader (review F3)

`peer_book_by_id` (`node_hashlocate.cpp:526`) is already the dual-plane resolver and already returns a *mask*, not a
winner. Make it the only id→hash entry point (U1) — the A/`hashof` divergence happened precisely because two verbs each
read one table.

⚠ **Load-bearing:** `key_hash_of_id:148` hard-filters `confidence != authoritative → continue`. Without a floor
parameter a `claimed` binding is **invisible to every reader** and S4 would land a mechanism nothing can observe.

**The floor must be complete and symmetric — forward AND reverse — with the selected confidence returned**, because a
caller cannot label a claim it cannot see (review F3):

```cpp
bool key_hash_of_id(uint8_t id,  uint32_t& out, IdBindConf min = IdBindConf::authoritative, IdBindConf* actual = nullptr) const;
bool team_key_of_id(uint8_t id,  uint32_t& out, IdBindConf min = IdBindConf::authoritative, IdBindConf* actual = nullptr) const;
bool team_id_of_key(uint32_t h,  uint8_t&  out, IdBindConf min = IdBindConf::authoritative, IdBindConf* actual = nullptr) const;
```

`team_id_of_key` is the one v1 missed: it is the **reverse** reader on the live send-by-hash path and today accepts
every fresh row (`node_routing.cpp:852-858`), so claimed rows would leak into sending without it. Defaults keep every
existing caller byte-identical ⇒ S3 inert by construction.

`PeerBookRow` gains **`team_authoritative`** beside the existing `static_authoritative` (`node.h:768`) — without it
D6's "display a claim *labelled as a claim*" is unimplementable. The `peers`/`hashof` text rows and the JSON book
contract carry it.

⇒ **The test obligation is enumerative:** a test must walk every reader of both stores, so a future reader cannot
silently bypass the policy by omitting the parameter.

### D2 — `_team_keys` gains the ladder static already has

Add `source` + `confidence` to `TeamKey`, mirroring `IdBind` field for field. Reuse `IdBindConf` — no third enum (U1);
two levels is right per §2.2.

⚠ MEASURE `sizeof(TeamKey)`: `{u8,u32,u64}` is 16 B with a 3-byte hole after `id` and no tail pad. Two `uint8_t`s
should land free there (padding-placement rule, 9 prior applications), ×16 slots ×`MR_N_LAYERS` — a wrong guess is not
free. `offsetof`, not inference.

### D3 — only the owner may answer a by-id query; no cached answers

**A cached answer is allowed exactly when the answer is self-verifying.** Hash-keyed queries qualify — that is what
makes cache-on-pass sound. By-id ones do not, so a third party relaying its guess adds attack surface for nothing.
Enforcement: one skipped branch in `handle_h`, plus a self-match on `_node_id` (static) / `team_local_id()` (team)
instead of `_key_hash32`. **Storing a snooped row for display does not make the relay an answer authority** — D5b may
store, D3 still forbids answering.

### D4 — separate owner-detection from answer confidence (review F9)

The existing local `authoritative` conflates two facts. Model both:

- **`answered_by_owner`** — selects owner-only behaviour and pubkey possession
- **`binding_verifiable`** — selects plain vs AUTHORITATIVE id-binding answer

By-hash self-match: both true. **By-id self-match: `answered_by_owner = true`, `binding_verifiable = false`.** This
makes the trust property structural rather than dependent on passing a surprising `false` at one call site — which is
how the `WANT_PUBKEY` and mobile-proxy paths would otherwise drift.

### D5 — receiver ingestion, stated per plane (review F2 — v1's "zero receiver changes" is RETRACTED)

**D5a — static: reuse the existing codepoint, no receiver change.** `send_hash_bind_response(..., binding_verifiable
= false)` emits plain `DATA_TYPE_H_ANSWER`; `node_hashlocate.cpp:1077` already maps that to `IdBindConf::claimed`. The
bit already means *"this assertion is verifiable"*, and a by-id assertion is not — so this is honest reuse, not a
workaround. **The narrow true claim is: no new receiver-side trust codepoint is needed. Not "no receiver changes".**

**D5b — team: ingestion must be ADDED.** `node_hashlocate.cpp:1075` is literally `if (!team_plane)`, and `:1072` states
*"⚠ NOT redirected into team_key_set, and that is a deliberate refusal"* (§hashbind-plane / B2). D2 removes the stated
*reason* for that refusal but not the refusal. S4a must add, explicitly and separately:

- destination of a team answer → `team_key_set(id, hash, source = h_query, confidence = claimed)`
- relay observation, if retained → `source = h_relay, confidence = claimed`
- ★ **neither path may set `_team_peer` or manufacture membership.** A team binding is usable only where the existing
  membership/route gate already says the id is a team peer.

**D5c — ★ ADDED BEYOND THE REVIEW: `_team_keys` liveness and eviction must be protected.** The same in-source note
(`node_hashlocate.cpp:1240-1243`) names the hazard the review's F2 resolution does not: `_team_keys` is a 16-slot
**evict-OLDEST LRU** whose `last_seen_ms` means *"heard now"*, and cache-on-pass *"would both fake that liveness and
let transit traffic evict genuine beacon rows."* Both consequences are live under D5b, because `last_seen_ms` is
load-bearing twice — the 48 h freshness test in `team_key_of_id` and the eviction order in `team_key_set`
(`node_routing.cpp:828-841`). Therefore:

- a `claimed` write must **not** refresh the beacon-liveness stamp of an existing `authoritative` row
- eviction must **prefer a `claimed` victim over any `authoritative` row**, so a 2-hop query storm cannot evict the
  beacon rows the seal path depends on
- ⚠ if that needs a second timestamp, it is a `sizeof` change: MEASURE, do not assume the hole absorbs it

### D6 — gate by purpose

| operation | floor | rationale |
|---|---|---|
| `hashof` / `peers` display | `claimed` | shows a claim, **labelled** as one (needs `team_authoritative`, D1) |
| `reqpubkey <id>` | `claimed` | the pubkey self-verifies *against that hash*; fetching it is how you inspect a claim and upgrades nothing |
| plaintext `send <id>` | — | already works: `_rt` routes it, DST_HASH is an optional `||` (`node_mac.cpp:137`) |
| **DST_HASH stamping** | **`authoritative` — RULED, see D7** | |
| sealed send (`send -e`) | `authoritative` (default) | a claimed hash + a verified pubkey still seals your content to whoever claimed the number |
| `team grantkey` | `authoritative` (default) | it ships a private key |

### D7 — ★ a claimed binding NEVER stamps DST_HASH (v1-O3 RESOLVED; v1's framing RETRACTED)

v1 called this two-sided on the premise that a wrong hash makes the recipient reject the DM. **The premise was wrong.**
`node_mac_rx.cpp:1076-1078`: a DST_HASH that is not ours calls `l2c_handle_misdelivery(pa, ui->dst_key_hash32)` —
*"forward to the real owner (identity-preserving)"* — which forwards immediately given an authoritative binding for the
stamped hash, or parks the DM and emits a HARD H query (`node_join.cpp:438-470`).

⇒ **DST_HASH is a routing instruction, not a rejection guard.** A false claimed binding would **redirect a plaintext DM
to the owner of the false hash**, plus generate control traffic and enter collision recovery. Keep the `authoritative`
default on both accessors for the send path. **Claimed bindings may inform display and explicit pubkey inspection; they
must never drive L2c redirection.**

### D8 — manual confirmation is EPHEMERAL and named honestly (review F4)

v1 called `bindid` a trust-anchor write while deferring persistence. Given §1-E, that was unsupportable: the typed value
could age out at 48 h, vanish on reboot, be demoted by a matching `claimed` answer, and be overwritten by a later
authoritative observation. Choosing the review's smaller honest product:

**`confirmid <id> 0x<hash> [-s|-t]`** — RAM-only, TTL-bound, explicitly *not* pinned:

- new **`IdBindSource::manual`** so provenance is not mislabelled as a beacon or query learn (`node.h:113`). ⚠ **It
  lands HERE, in S5, with its producer — not earlier.** v2 first placed it in S2b; that would have added an enumerator
  with **no producer**, which is exactly the `PeerKeyConf::overheard` smell §2.4 criticises. Amended 2026-08-01.
- writes `authoritative`; **requires §1-E's upgrade-only fix first** or a later claimed answer demotes it
- match → promote · **differ → the operator's value wins, plus a loud `id_bind_conflict` naming BOTH hashes.** A
  mismatch means something answered untruthfully on air; silently overwriting destroys the one signal this mechanism
  generates
- team form must **not** set `_team_peer` (D5b): refuse when the id is not already a known/routable member rather than
  manufacturing membership from a typed hash
- documented as expiring; **the word "pinned" is not used**

**Persistent pinning (`IdBindConf::pinned` + NV + boot restore + list/delete + capacity + never-age/demote/evict) is
S5, a separate product decision.** It is *not* blocked by NV cost — id bindings are not persisted at all today (§1-E).

### D9 — bare-id plane selection is explicit (review F8)

`peer_book_by_id` returns a mask; `reqpubkey` must pick **one** query plane. Grammar:

- `-s` and `-t` are mutually exclusive and force that plane (matching `hashof`, `firmware_commands.cpp:537-540`)
- no flag, exactly one plane locally possible → use it
- no flag, **both** possible → distinct `err_ambiguous_plane`, require a flag
- no flag, **unresolved** id on a dual-plane node → require a flag rather than guess
- static-only node defaults static; team-only/off-grid defaults team
- the acknowledgement **echoes the selected plane**, and later the resolved hash

This keeps "mask, not winner" intact at the mutation/airtime boundary, where picking the wrong row costs more than a
display error.

★★ **ADDED after the implementation assessment (P2) — a gap in D9 as first written, not merely in its code.** D9 was
specified against `peer_book_by_id`'s mask without noticing that the mask carries a **display** rule:
`node_hashlocate.cpp:600` suppresses the team bit when both planes hold the **same hash**
(`&& !(mask && th == h)`). Harmless while the only consumer was `hashof`; wrong the moment a mask decides **airtime**:

- bare AUTO silently picks static instead of reporting ambiguity, and
- explicit **`-t` returns `err_no_binding` even though the team binding exists.**

⇒ **The presence mask must report plane OCCUPANCY, never identity de-duplication. Hash equality does not merge two
planes** — the routes, return paths and id spaces stay distinct (§18). Any de-dup belongs in the **renderer**. Every
future consumer of this mask inherits the rule: **if a decision spends airtime or mutates state, it may not read a
field shaped for a display.**

---

## §4 Wire format

`H` byte 7 holds `HARD 0x01 | WANT_PUBKEY 0x02 | TEAM 0x04 | MOBILE_REQ 0x08` (`frame_codec.cpp:603`) ⇒ **0x10 / 0x20 /
0x40 / 0x80 free.** Add `H_FLAG_BY_ID = 0x10`.

**Query key: reuse bytes 2–5 (0 extra bytes), with a CANONICAL encoding (review F7 — O1 answered "reuse, with
conditions"):**

- `BY_ID` ⇒ bytes 2–5 encode a **zero-extended** id; **bytes 3–5 MUST be zero on pack and on parse**
- ids **0 and 255 rejected** (0 = unprovisioned, 0xFF reserved — `peer_book_by_id:529`)
- dedup key includes `by_id` and uses the canonical value, so multiple 32-bit spellings cannot name one id while
  occupying different dedup slots (redundant floods, inconsistent telemetry)
- forwarders preserve the bit **and** the canonical value
- **in-memory naming becomes honest**: `query_key32` with `query_id()` / `query_hash()` accessors, so handlers stop
  casting a field named `key_hash32` that holds an id. The `f_in.ttl_or_next_hop` precedent (`frame_codec.h:399-401`)
  supports overloaded *wire* storage — it does not license a misleading struct field
- define whether `HARD` is forbidden or ignored under `BY_ID` (owner-only answering already gives the strongest
  possible by-id lookup under this trust model)
- all current `WANT_PUBKEY` / team / mobile length validation retained

**Dedup.** `HashQuerySeen{origin,key_hash32,t_ms,hard,want_pubkey,team_scoped}` — `hard` and `want_pubkey` are already
part of the key so variants don't suppress each other (`node.h:1703-1705`). `by_id` **must join it**. ⚠ MEASURE, but the
`sizeof(Node)` ledger records `HashQuerySeen` as 24 B with those three bools plus **5 bytes of tail pad**, so a fourth
bool should cost **0** — the same zero-cost placement `team_scoped` got under §team-parity T6/B.

**Forwarding.** `h_forward` (`node_hashlocate.cpp:963-970`) must preserve `by_id` as it preserves
`want_pubkey`/`team_scoped`/`mobile_req`.

**Mixed-version (review F5 — v1's compat claim RETRACTED).** v1 said an old receiver "matches nothing, stays silent."
Wrong: `h_forward` rebuilds the frame via `pack_h` from the flags it understands, so **an old relay strips `BY_ID` and a
valid by-id query degrades into a hash query for a small integer — failing silently after the first old hop.** A
low-valued or adversarially chosen hash could even match. The dedup key cannot fix a flag another node strips.

**Resolution: coordinated fleet upgrade, and mixed-version paths are UNSUPPORTED.** That is not a new constraint — M3 /
C4: MeshRoute is not deployed, runs only on the owner's test hardware, reflash-all is available, wire changes are free.
No `wire_version` bump is required *for compatibility*; if one is wanted as a fleet tripwire it gets **its own
slice/commit** (C4 — a bump re-anchors all 36 streams at once). The silent-degradation mechanism is recorded here so
nobody later assumes graceful fallback.

---

## §5 The unresolved-`reqpubkey` state machine (review F1 — MISSING from v1)

v1's D4 governed only `on_hash_bind_response`. But `reqpubkey` sets `want_pubkey=true`, and that answer travels a
**different type through a different receiver**:

- `node.cpp:1643-1648` returns `err_no_binding` **before** any query when the id will not resolve; only then
  `:1659` calls `emit_hash_query(..., hard=true, want_pubkey=true, ...)`
- the owner answers via `send_hash_bind_pubkey_response`, **not** `send_hash_bind_response`
  (`node_hashlocate.cpp:929-947`), always `DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY` (`:1030-1040`)
- **its receiver `on_hash_bind_pubkey:1045-1056` calls `peer_key_set` and NO `id_bind_set` at all** — it caches
  hash→pubkey and never writes id→hash

⇒ D5a cannot govern a by-id `WANT_PUBKEY` answer. **Two stages, specified:**

1. an unresolved, plane-selected (D9) `reqpubkey <id>` records a **bounded pending `resolve-id-for-pubkey` intent** and
   emits a by-id H query with `want_pubkey = false`
2. the ordinary H answer lands id→hash as `claimed` (D5a static / D5b team)
3. matching that answer **consumes** the pending intent and emits the existing HARD `WANT_PUBKEY` query **by the
   returned hash**
4. the existing pubkey answer self-verifies and fills hash→pubkey `authoritative` — **unchanged semantics**
5. a bounded **timeout** produces an explicit `reqpubkey` failure and clears the intent

★ **This is not prohibited auto-resolution.** `§no-auto-reqpubkey` (owner-ratified 2026-07-29, `node.cpp:187-193`)
forbids a *send* silently escalating to a pubkey request. Here the operator typed `reqpubkey`; stage 3 is the
**completion of the command they issued**, and it is bounded and reported. The spec must record the honest cost:
**two query/answer exchanges when the id binding is initially absent.**

The one-round alternative (a by-id query returning hash *and* pubkey together) needs either a new response type or a
receiver rule separating the unverifiable id assertion from the self-verifying pubkey inside one frame. **Not designed
here**; if wanted it is its own slice.

### §5.1 ★★ What `queued` MUST mean — ADDED after the implementation assessment (P1c)

v2 specified the stages and never said what the **acknowledgement** means when a stage does not fly. That omission is
the spec's, and it shipped a false success: `emit_hash_query` is `void` and returns early at **four** points —
degenerate/self (`node_hashlocate.cpp:1586`), **no crypto identity** (`:1587-1591`), off-grid mobile with no global
return path (`:1618-1621`), and a `pack_h` codec failure — while `on_command` returns `queued` unconditionally and BLE
converts every one of them into **`reqpubkey_sent`** (`fw_main.cpp:490-497`). B47 recorded only the third.

**Requirements, binding on S1's rework and on S4a/S4b:**

- **`reqpubkey_sent` is emitted only when a frame actually left.** It is the contract's "the request was flooded" event
  and must not be reachable from any bail point.
- **Each bail point is separately nameable** — `no_identity` / `no_return_route` / `degenerate` / `encode_failed`. They
  have different operator remedies, so collapsing them into one refusal repeats B34's loss of the reason.
- ★ **A local cache hit is a success but NOT a send.** The hosted-mobile branch (`node.cpp:1688-1691`) legitimately
  resolves the key locally and already fires `peer_key_cached`; it must not *also* claim a query flew.
- **Tests assert the `CmdResult` and the BLE-visible disposition, not only `h_tx` absence.** The existing no-identity
  test discards the result (`test_node_hashlocate.cpp:1451-1461`), which is exactly why this reached QA.

ⓘ This is B39's class ("a success that isn't") reached through a new door. The broader B39 redesign stays separate —
these branches are locally knowable and do not wait on it.

---

## §6 Slices

| | scope | wire | fixes | gate |
|---|---|---|---|---|
| **S1** | `reqpubkey` bare-id → `peer_book_by_id` at **BOTH** sites (`node.cpp:1646` **and** the BLE echo `fw_main.cpp:490`); `-s`/`-t` + D9 ambiguity errors; `hashof`-parity refusal text | none | **A** | s18 keystone reproduces — ⚠ verify the corpus issues no `reqpubkey`, don't assume |
| **S2** | `peers all` static `_rt` pass mirroring team pass (4); self-skip | none | **C, D** | view-only; JSON book untouched (`include_id_rows=false`) |
| **S2b** | §1-E upgrade-only `id_bind_set` — **matching row AND reverse-holder eviction** | none | **E** | a live demotion bug; prerequisite for S5 |
| **S3** | `TeamKey{source,confidence}`; floors + `actual` on all three accessors; `PeerBookRow::team_authoritative`; D5c liveness/eviction; view + JSON labelling | struct only, inert | prep for B | s18 keystone reproduces **by construction** (defaults) + `sizeof(Node)` assert + per-board RAM diff (D2) |
| **S4a** | canonical `H_FLAG_BY_ID`, owner-only answer, D4's two booleans, dedup + forward preservation, **static AND team ingestion** | 1 flag bit | **B** | re-anchors; own commit so the delta is attributable |
| **S4b** | pending `resolve-id-for-pubkey` intent + second-stage existing pubkey query + timeout | none | §5 | closes the operator workflow without overloading pubkey trust semantics |
| **S5** | `confirmid` (D8); optional `IdBindConf::pinned` + NV + companion contract | none | D8 | the operator trust decision stays attributable |

S1, S2 and S2b carry **no trust decision** and land independently. **S1 alone probably clears most of the bench pain** —
186 is the only directly-heard neighbour and `reqpubkey 186` starts working at once. S3 is separated from S4a so S4a's
re-anchor is attributable (C4). S1/S2b/S3 touch `lib/core` ⇒ D2 applies.

---

## §7 Open questions remaining

v1's O1 → **reuse, with §4's canonical encoding and honest naming.** O2 → **D8: ship ephemeral `confirmid`; pinning is
S5.** O3 → **D7: claimed NEVER stamps DST_HASH.** O4 → below.

- **O4** verb name — `confirmid` (recommended, honest for a RAM-only expiring promotion) vs `bindid`.
- **O5** BLE availability for `confirmid`. The review's position, which I endorse: **acceptable as a first-class
  structured companion operation**, because the BLE link is already the MITM-passkey-authenticated admin transport,
  already exposes `peerkey` installation, and already falls through to destructive console operations
  (`fw_main.cpp:483-515`) — excluding only this verb creates no coherent boundary. If it is included: show plane, id,
  old hash, new hash and resulting confidence; require explicit confirmation on conflict; return typed JSON
  success/conflict/error; never rely on the text-console fallback for the acknowledgement. ⚠ **If the ruling is instead
  that BLE is too weak for trust-anchor writes, it must apply consistently to `peerkey`, identity regeneration and
  factory reset — not to `confirmid` alone.** ⓘ This intersects the still-open **O4 (`team exportkey` over
  unauthenticated BLE)** from the 2026-08-01 handover.
- **O6** is `HARD` forbidden or merely ignored under `BY_ID` (§4).

## §8 What this deliberately does NOT do

- It does **not** unblock multi-hop `team grantkey`. Direct consequence of the owner's confidence split, and correct:
  the key ceremony still needs QR or a heard beacon. Stated so it is not discovered after S4a.
- It does **not** fill `PeerKeyConf::overheard` (§2.4).
- It does **not** touch plaintext `send <id>`, which already works.
- It adds **no** auto-escalation beyond §5's bounded, operator-initiated second stage.
- It does **not** make claimed bindings usable for sealing, DST_HASH, or membership.

## §9 Minimum gates

- static by-id owner answer lands **`claimed`**, never `authoritative`
- team by-id owner answer lands in **`_team_keys`**, never `_id_bind` — and never sets `_team_peer`
- a relay/cached binding **never answers** a by-id query (D3), even when stored for display (D5b)
- a claimed same-hash refresh **cannot demote** an authoritative row (§1-E) and cannot refresh its beacon liveness (D5c)
- eviction prefers a claimed victim over any authoritative row (D5c)
- claimed forward **and reverse** lookups fail under the default authoritative floor — `team_id_of_key` included
- `hashof` / `peers` / `reqpubkey` can explicitly read claimed and **label both planes** correctly
- a claimed row never stamps DST_HASH and never drives L2c redirect (D7)
- by-id `reqpubkey` completes both stages **or** produces one bounded timeout (§5)
- ids 0/255 and non-zero upper query bytes are rejected; by-id and by-hash dedup entries cannot alias
- a forward preserves `BY_ID` across every new-firmware hop
- the same numeric id in both planes requires or honours explicit plane selection (D9)
- a `confirmid` conflict preserves and reports **both** hashes; the declared expiring lifecycle is asserted, not assumed
- `sizeof(TeamKey)`, `sizeof(Node)` and per-board RAM deltas **measured**, never inferred
- ⓘ no mixed-version gate: coordinated upgrade per M3 (§4) — assert nothing we do not support

## §10 Obligations on landing (M1 / M2)

- Register **A, B, C, D, E** in `docs/2026-07-30-open-bug-register.md` (B42–B46) with their bench measurements
  **before S1 is dispatched**. ★ **E is a live demotion bug independent of this feature** and must not be buried
  inside the design entry.
- `docs/2026-07-31-bench-test-script.md` gains the `hashof 186` vs `reqpubkey 186` pair (S1), the `confirmid` conflict
  path (S5), and the D9 ambiguity refusal — console/operator behaviours no automated gate reaches.
- Per P2, once agreed, the durable agreements distil to one `MEMORY.md` line: §2's two-bindings model, D3's
  cached-answer principle, D5's per-plane retraction, D7's L2c finding. **Not written yet — not agreed yet.**
