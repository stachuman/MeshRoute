# Hybrid RTS flight identity — design specification · 2026-08-08

**Status: FULLY OWNER-RULED — the 10/11-B RTS core AND §2.3's terminal-CTS amendment.** ✅ **§2.3 was CONFIRMED by the owner 2026-08-09**, verbatim in `docs/2026-08-05-owner-rulings-ledger.md` **§1.10**; ⛔ the former status line (*"QA SAFETY AMENDMENT AWAITING OWNER CONFIRMATION"*) is WITHDRAWN. ⚠ The ruling's second half is easy to lose: **a mismatch MAY be billed as physical airtime but must change nothing else** — no liveness, timer, routing, pending-state or app-facing effect.
⛔ **The sentence that stood here — *"Coding must not start until the owner accepts or replaces §2.3 conditional
terminal-CTS correlation"* — is WITHDRAWN as of 2026-08-09: that acceptance ARRIVED** (ledger §1.10, verbatim), so
the sentence contradicted the status line directly above it. ✅ **Coding is unblocked and S1/S2/S2b/S2c/S2d have
landed.** This
supersedes the no-growth conclusion recorded by B153/B157. It does not erase that investigation: the deletion was locally safe, but its
system-level interaction cost was measured later and changed the decision.

⛔ **2026-08-27 B251 SUPERSESSION / CLOSURE HOLD:** B161's approved canonical typed-answer implementation exposed a
second identity alias in the required corpus. A type-8 answer from home 17 and a later hosted-mobile DM forwarded by
that same home can both present `(from=17, dst=30, GLOBAL, origin=17, ctr=1)` to the completed-flight cache, despite
being different flights. The receiver returned a false terminal CTS and discarded the DM (`s22`, 8 → 7 unique
deliveries, one assertion failure). See the B161 spec §11. Therefore B161, B157 and B153 remain open; do not cite the
old cumulative gate as proving universal identity safety.

**Scope:** unicast DATA/DM RTS identity and the two optimisations that consume it. This is a protocol/MAC change,
not a routing-policy tuning slice and not the B159 long-horizon DATA dedup fix.

---

## 1. Problem and ruling

The current 7-byte unicast RTS identifies a candidate flight only by
`(immediate src, dst, ctr_lo[4], payload_len)`. Two different messages can therefore produce byte-identical RTS
frames. The defect is not an unlucky hash collision: one gateway relaying same-length messages from different
mobiles to the same destination is an ordinary topology.

Measured across the 36-stream corpus:

- 4,137 completed-flight stores produced 66 at-risk pairs and 4 aliases: **6.1% overall**;
- for the gateway/different-origin design case, **4 of 5 pairs aliased (80%)** because peer counters were correlated;
- the false match can make either optimisation terminally credit the wrong flight:
  - receiver CTS `already_received`;
  - sender `implicit_ack_from_forward`.

Removing both terminal decisions fixed the silent-loss class, but changed the system the routing algorithm was
tuned around:

| arm | unique deliveries | `s06` | DM airtime |
|---|---:|---:|---:|
| pre-B153 base | 732 | 104 | reference |
| remove receiver CTS gate only | 728 | 106 | -1.63% |
| delete implicit ACK only | 731 | 97 | +0.63% |
| delete both | 708 | 85 | +9.14% |

The combined loss is non-additive: 19 of the 24 lost deliveries are an interaction term. Retry congestion is then
misread as link failure by the routing layer. That routing sensitivity is real and separately actionable, but it is
not a reason to remove two useful MAC optimisations when the wire can identify their premise honestly.

### Owner ruling

**Increase only unicast RTS identity enough to make the optimisation premise reliable, then restore both
optimisations keyed by that identity.** Do not change routing policy in this arc.

---

## 2. Wire shape

Ordinary unicast DATA RTS frames change, and the terminal completed-flight CTS gains a conditional identity echo.
Broadcast channel/flood RTS and ordinary non-terminal CTS frames retain their exact existing formats and sizes.

| RTS kind | current | new | identity tail |
|---|---:|---:|---|
| plaintext unicast DATA | 7 B | **10 B** | exact 24-bit `(origin, ctr16)` |
| encrypted unicast DATA | 7 B | **11 B** | 32-bit digest of the clear nonce seed and flight context |
| M/channel broadcast | 9 B | **unchanged** | none |
| flood | 43 B | **unchanged** | none |

| CTS kind | current | new | correlation |
|---|---:|---:|---|
| ordinary, no NAV hint | 3 B | **unchanged** | non-terminal |
| ordinary, NAV hint | 4 B | **unchanged** | non-terminal |
| `already_received`, plaintext | 3/4 B | **6 B** | base 3 B + exact 3-B identity echo |
| `already_received`, encrypted | 3/4 B | **7 B** | base 3 B + exact 4-B identity echo |

For RTS, frame length is the canonical domain discriminator: 10 bytes means plaintext and 11 bytes means
encrypted. Terminal CTS mirrors that distinction at 6/7 B. No new flag bit is consumed.

### 2.1 Plaintext identity

Append exactly three bytes:

```
origin[8] | ctr_hi[8] | ctr_lo[8]
```

This is collision-free for the canonical plaintext DATA identity available to the relay. Plaintext DATA already
exposes origin, so the RTS reveals no new identity that the subsequent DATA does not reveal.

### 2.2 Encrypted identity

Append four digest bytes. The input is canonical and domain-separated:

```
domain = 0xE1
input  = domain || nonce_seed[8] || ctr_hi || ctr_lo || dst
tail   = first 4 bytes of BLAKE2b-512(input)
```

The four output bytes are copied to the wire in digest order; do not round-trip them through host-endian integer
layout. Use the project's existing Monocypher convention — BLAKE2b-512, then truncate — and one shared pure helper.
Do not request a 4-byte parameterized BLAKE2b output: it is a different digest from the project's established
`crypto_blake2b(..., 64, ...)` then-prefix convention. An ad-hoc XOR/fold
is not an acceptable substitute: it would restore correlated aliases under a new name.

The encrypted tail must not expose `origin`. The nonce seed is already carried clear by encrypted DATA, while the
digest preserves sealed-sender behavior at RTS time. Binding `ctr` and `dst` prevents reuse outside the exact flight
context. Conditional false identity probability is approximately `1 / 2^32`, not the current correlated 4-bit
space.

If the BLAKE2b helper's 64-byte temporary unexpectedly adds a material stack cost or drags code into a non-crypto
profile, stop and report the measured cost. Do not silently weaken the identity.

### 2.3 Terminal CTS correlation

A normal CTS is not terminal: it only authorizes DATA, and §4.2 rejects a stale or mismatched authorization before
delivery or ACK. It therefore remains byte-identical at 3 B, or 4 B when carrying the existing NAV hint.

An `already_received=true` CTS is terminal at the sender. Endpoints plus single-slot stop-and-wait do not bind it to
the current flight: CTS is retry/duty-stash eligible and that stash currently has no flight-generation guard, so a
delayed response to flight A can arrive while flight B to the same next hop is awaiting CTS. Without an echo it can
silently clear B.

Therefore a terminal CTS must echo the complete identity that produced the receiver cache hit:

- plaintext: exactly 6 B = the existing 3-B CTS base plus the 3-B plaintext identity;
- encrypted: exactly 7 B = the existing 3-B CTS base plus the 4-B encrypted identity;
- no NAV byte is present because no DATA follows;
- frame length is again the plaintext/encrypted domain discriminator;
- when `already_received=1`, the three otherwise-unused chosen-SF flag bits encode one team/static plane bit and two
  reserved-zero bits. The sender ignores chosen SF on this terminal shape.

The sender may clear its pending copy only after `tx_id`, `rx_id`, team/static plane, identity width/domain, and every
identity byte match the current `PendingTx`. A mismatch is ignored and leaves all pending state and deadlines intact.
A shorter 8- or 16-bit response tag is rejected: it would introduce a new probabilistic silent-loss decision after
this design paid the RTS cost to remove precisely that class.

### 2.4 Canonical parsing and fleet compatibility

- Accept unicast RTS only at exactly 10 B or 11 B.
- Reject legacy 7-byte unicast RTS; there is no ambiguous compatibility parser.
- Reject an M/flood flag combined with a unicast identity length, and reject a unicast shape at an M/flood length.
- Keep existing exact M and flood lengths unchanged.
- Accept ordinary CTS only at 3/4 B with `already_received=0`; accept terminal CTS only at 6/7 B with
  `already_received=1`, canonical reserved bits, and the required identity tail. Reject every cross-shape pairing.
- MeshRoute is not deployed. Per owner ruling §1.8, no `wire_version` bump is required for this test-hardware change.
  Mixed old/new firmware is unsupported; reflash every node in a bench topology before testing.

---

## 3. One identity path

There must be one producer for each identity and one comparator; do not reconstruct fields independently at RTS,
DATA, cache, and test sites.

Recommended pure API shape (names may follow the surrounding idiom):

```cpp
struct RtsFlightIdentity {
    uint32_t value;
    uint8_t width;       // 3 or 4; never 0 for unicast
};

RtsFlightIdentity rts_flight_identity_plain(uint8_t origin, uint16_t ctr);
RtsFlightIdentity rts_flight_identity_crypted(
    const uint8_t nonce_seed[8], uint16_t ctr, uint8_t dst);
```

The codec packs already-computed identity bytes; it must not own crypto. Sender, relay, DATA validation, completed
cache, terminal CTS echo/validation, and implicit-forward matching call the same helper.

Existing carrier conversions must be extended through their single canonical path (U2). Do not add a second
field-by-field `TxItem`/`PendingTx` conversion. Prefer recomputing from canonical fields already carried by the DATA
carrier over copying a redundant tag through every carrier.

---

## 4. Receiver contract

### 4.1 RTS admission

For a free receiver, a well-formed unicast RTS creates `PendingRx` containing the immediate hop ids, payload length,
team/static plane, identity width/domain, and identity value. It returns ordinary CTS and waits for DATA.

For an exact completed-flight cache hit from the same immediate sender, destination, team/static plane, domain/width,
and identity, the receiver may return the canonical 6/7-B terminal CTS from §2.3 with `already_received = true` and
the matching plane/identity echo. It does not allocate a fresh `PendingRx`. Payload length remains a consistency/NAV check; it is not
part of message identity.

### 4.2 DATA validation is mandatory

Before a received DATA can seed the completed-flight cache, recompute its identity from canonical DATA fields and
compare it with `PendingRx`. A mismatch is a protocol failure:

- do not deliver it;
- do not ACK it;
- do not seed or refresh the completed-flight cache;
- clear the receive reservation and emit a named diagnostic (`rts_flight_id_mismatch` or surrounding equivalent).

The sender then follows its existing bounded timeout/retry path. Do not invent a fallback that accepts a mismatch.

Only after matching DATA has been accepted and its ACK emitted may the receiver store the completed identity. S0
settled the retention horizon:

- the directly measurable exact-retry set reached 18,971 ms; the historical 10-second window covered only 73/74;
- the longer path audit found `gateway_hold_requeue.age_ms` at 149,134 ms over 1,175 firings and traced one exact
  flight revisiting one hop 147,658 ms after its earlier completion;
- that path is governed by `gateway_send_giveup_ms = 150000`, not by the 60-second cascade or 30-second defer
  horizons. At/after that bound the flight is given up rather than requeued.

Therefore define semantic `completed_flight_cache_ttl_ms` from `gateway_send_giveup_ms` (currently **150,000 ms**),
not from the retired `last_acked_ttl_ms` and not as a second unexplained literal. Boundary tests must pin the shared
derivation. The observed `send_giveup.age_ms` can exceed 150 seconds because a scheduled callback runs late; it does
not authorize another requeue beyond the give-up boundary.

This makes entry count, not expiry time, the likely binding constraint. A single "latest flight per immediate
sender" slot is not yet proven sufficient: one sender can retain several independently retryable flights during a
150-second gateway hold. Before choosing storage, measure the maximum simultaneously-live completed identities per
immediate sender and globally on the S1 wire arm, plus the hits that capacities 1..N would lose. Choose the smallest
bounded deterministic cache that meets the corpus acceptance floor without eviction-induced false misses; report
its RAM/layout cost and refuse to turn it into an unbounded structure. This completed-flight optimisation and B159
application-delivery dedup are different mechanisms; fixing one does not close the other.

### 4.3 Cache key and plane discipline

The cache match is the complete tuple:

```
immediate sender | dst | team/static plane | plaintext/encrypted domain | full identity
```

Use the on-air immediate sender consistently at store and lookup. Never key the store from simulator-only
`meta.src_hint`; that was B156 sim/metal divergence. Per-`LayerRuntime` storage separates layers, not the team and
static planes that coexist within one layer, so it is not sufficient plane isolation.

Do not conflate two different predicates:

1. `rts_wire_team_plane(r) = (r.addr_len == 1 && r.mobile_src)` is the plane the sender declares on the wire. It is
   receiver-independent and is the only plane evidence an overhearer can use.
2. `team_addr_for_us(r.next, r.addr_len)` is receiver-relative address admission: it says the frame names this node
   through its team-local address. It is not a wire-plane decoder.

S0 proved this canonical matrix across every origin, relay, last-mile, requeue, and cross-layer reinjection path.
There is exactly one unicast-RTS producer (`tx_rts_retry`), and all paths funnel through it. The only non-team
out-of-matrix cell, `(1,1)`, is configuration-unreachable because hosted-mobile state and mobile-origin state are
mutually exclusive (including runtime role changes). All 61 historical forward credits carried `(0,0)`. The matrix
may therefore be a fatal validator:

| `addr_len` | `mobile_src` | intended emitted meaning |
|---:|---:|---|
| 0 | 0 | ordinary static/global flight |
| 0 | 1 | registered-mobile-originated static/global flight |
| 1 | 0 | host-to-mobile last-mile static/global flight |
| 1 | 1 | team-plane flight |

For an addressed RTS, first resolve the receiver-relative targets already used by the live code:
`for_static_rts` and `team_addr_for_us`. Then use the wire declaration to select which target must be true:

- wire TEAM → `team_addr_for_us` must be true;
- wire STATIC/GLOBAL → `for_static_rts` must be true.

An overheard frame is not malformed merely because `team_addr_for_us` is false; the receiver is not its addressee.
After successful addressed admission, store the wire-declared plane in `PendingRx` and the completed cache. DATA has
no independent `mobile_src` plane bit, so validate it against the stored plane: stored TEAM requires the existing
`for_team_data`; stored STATIC/GLOBAL requires `for_static_data`. A failure is the fail-loud §4.2 mismatch. Terminal
CTS echoes that stored plane. Implicit-forward matching, which is necessarily an overhear path, compares the pure
wire declaration with the current pending flight plane and never calls `team_addr_for_us` on someone else's address.

If a future producer or role transition creates a fifth cell, fail its producer test; do not weaken the decoder or
fall back to `is_team_peer(src)`. Repair that producer in its own attributed slice or allocate an explicit plane
wire field. The discriminator itself consumes no new wire byte.

S0 also found a separate pre-existing carrier defect: `txitem_from_pending()` copies neither `pt.plane` nor any
equivalent, so all three requeue paths default an explicit flight back to `Plane::AUTO`. A GLOBAL flight requeued to
a destination that is also a team peer can consequently change planes. B160 owns that repair in a separate
pre-wire slice; S2 may assume Plane continuity only after the B160 gate passes.

---

## 5. Sender/relay contract

### 5.1 RTS production

Every unicast sender and relay computes the identity from the same canonical DATA carrier:

- plaintext: `origin + full ctr`;
- encrypted: clear nonce seed + full ctr + final destination.

Retries of the same flight reproduce identical identity bytes. A new flight that happens to share the old
`ctr_lo/payload_len` does not.

### 5.2 Restore exact forward-progress credit from forwarding

An overheard downstream RTS may clear the redundant local pending copy only if all of these match:

- expected next hop/source relationship;
- destination and plane;
- identity domain/width;
- full identity value.

With that equality the RTS is no longer an inference from ambiguous bytes: it identifies the same flight. Two
different proofs must remain distinct:

⛔⛔ **AMENDED 2026-08-10 BY §HYBRID-RTS-S4c — THE TEXT BELOW IS THE OPERATIVE VERSION.** ⛔ What stood here claimed a
**local-copy proof** resting on *"if this `PendingTx` has transmitted DATA at least once"*, carried as a durable
`data_ever_transmitted` fact *"set only at the actual DATA transmission boundary"* and labelled `basis=local_data`.
★ **THAT FACT CANNOT BE ESTABLISHED WHERE IT IS SET, AND IT IS NOT ESTABLISHED ANYWHERE ELSE EITHER.** `IHal::tx`
returning `ok` is an **admission** — on hardware an enqueue (`lib/hal/device_hal.cpp:10-12`), with the on-air send
deferred to `pump_tx()`. The frame can still be refused after admission (`Node::on_radio_busy(FrameTag::data)`, live
at **652** corpus events; `pump_tx()`'s failed arm drops it outright), and conversely two re-hand sites fly a DATA
without recording anything ([[B164]]). ⇒ the flag is wrong in **both** directions, so the honest specification is:

1. **Local-admission observation:** if a DATA for this `PendingTx` was **admitted to the radio** at least once, an
   exact downstream forward is *consistent with* the selected next hop having obtained this flight after a local
   attempt. ⛔ It is **not** proof that any DATA of ours reached the air.
2. **Alternate-path observation:** if no DATA has been admitted locally, an exact forward from the selected next hop
   shows that node obtained the same flight through another branch/path and that this pending local copy is
   redundant. This is progress evidence, not proof of final delivery and not a MAC ACK to this node.

⛔⛔ **AMENDED AGAIN 2026-08-10 BY §HYBRID-RTS-S4d — THE PARAGRAPH ABOVE IS THE OPERATIVE VERSION, AND WHAT MAKES ITEM
(2) TRUE IS A CODE CHANGE, NOT A WORDING.** ★ S4c fixed the name; it left item (2)'s **antecedent** unprovable. Item
(2) is a **CATEGORICAL** claim — *"if **no** DATA has been admitted locally"* — but the flag behind it had a single
writer in `do_data_tx`, i.e. on the **INITIAL** send path only, while `duty_defer_fire` re-runs `tx_with_retry` from
the stash and can obtain an admission there. ⇒ a duty-deferred DATA that was **later admitted** read `false` and was
labelled `alternate_path`, so the specification asserted something the implementation could not establish: `true`
meant *"an admission was observed"*, `false` meant only *"none was observed on the initial path"*.
★★ **S4d ESTABLISHES THE FACT AT THE ONE POINT EVERY ADMISSION CROSSES** — inside `Node::tx_with_retry`, immediately
after `_hal.tx()` returns `TxResult::ok`, for a live non-`m_broadcast` `FrameTag::data` flight — and REMOVES the
`do_data_tx` writer. Because **every** DM-DATA admission (initial and duty-deferred-retry) must pass that call, item
(2)'s antecedent is now exact in both directions, **and a future call path cannot bypass it**. ★ `retry_stashed`
requires **no** additional writer: reaching it presupposes a prior successful admission at that crossing point, and it
only ever re-sends the stashed bytes of the frame that was admitted.
⛔ **THIS DOES NOT TOUCH ITEM (1)'s LIMIT.** `local_admitted` is still **ADMISSION, NEVER AIRING** — `on_radio_busy`
and `pump_tx()`'s failed arm can drop an already-booked frame and nothing can unset the flag ⇒ [[B164]] stays open
**for airing only**. The honest one-liner for the flag is **"exact about admission, silent about airing"**.
⛔ **AND THE PROHIBITION BELOW STILL BINDS AND WAS OBEYED: S4d added NO per-path assignment.** A guarantee made at one
of several exits is not made at all — the same structural lesson as [[B162]]'s refusal banner.

★★ **BOTH OBSERVATIONS TAKE THE SAME ACTION, AND ONLY (2)'s REASONING EVER JUSTIFIED IT** — *"the flight is
progressing and this local copy is redundant"*. Nothing in the protocol consumes the distinction, so it is
**diagnostic only**.

Carry a `data_ever_admitted` fact in `PendingTx`, set at the **one** HAL-admission crossing point (inside
`tx_with_retry`, immediately after `_hal.tx()` answers `TxResult::ok`; ⓘ §HYBRID-RTS-S4d — this read *"a best-effort
… fact, set only at the HAL-admission boundary (`TxHandOff::handed`)"*, which described a write at ONE CALLER's exit
and was therefore best-effort by construction), and emit the credit diagnostic with `basis=local_admitted` or
`basis=alternate_path`. In
either case the redundant local `PendingTx` may be cleared, but the optimisation must **not** synthesize
`send_acked`, `send_e2e_acked`, delivered, or `send_failed`. Preserve any independently armed end-to-end ACK wait.
The command has already reported queued; ordinary non-`-a` sends receive no new terminal app outcome, matching the
historical implicit-credit behavior.

⛔ **NO CONSUMER MAY TREAT `local_admitted` AS AIRING EVIDENCE.** If one ever genuinely needs "aired" rather than
"admitted", establish it with a **flight-correlated TX-start/completion signal** and rename the fact back — that
option was considered in S4c, **deferred for want of a consumer, and explicitly not dismissed**. ⛔ It must NOT be
approximated by adding the assignment at [[B164]]'s two retry sites: that repairs the false negatives and preserves
the post-admission false positive. ⓘ §HYBRID-RTS-S4d honoured this exactly — it added **no** per-path assignment; it
CENTRALIZED the single existing one, which closes the admission-side false negative and leaves the airing question
(and option (b)) untouched.

Any mismatch is non-terminal and changes no pending deadline or route state.

---

## 6. Timing and airtime

The current CTS timeout prices an 8-byte routing packet while the unicast RTS is 7 bytes. That is one payload-byte
of growth allowance (and happens to occupy the same symbol bucket at the audited PHYs), but the proposed 10/11-byte
RTS exceeds it. B158 therefore becomes live again when unicast RTS grows.

Introduce/use semantic helpers for actual unicast RTS and terminal-CTS wire lengths. The CTS-wait calculation must
price the active 10- or 11-byte request plus the possible 6- or 7-byte terminal response; a sender cannot know in
advance whether the receiver cache will hit. A normal 3/4-B CTS cancels the timer as soon as it arrives, so this does
not delay a successful ordinary exchange, but it extends the no-response/retry boundary and must be corpus-attributed. Do not globally replace every `RTS_LEN`: M/flood frames and Lua-parity constants have
different meanings.

Before changing retry jitter or NAV constants, produce a use-site ledger and measure the repo `airtime_ms()` for
3/4/6/7-B CTS and 7/9/10/11-B RTS at every supported SF, bandwidth, and coding rate. Some sizes occupy the same LoRa symbol bucket;
that does not make a byte free at every PHY. Any timing change beyond the exact CTS-wait correction needs its own
attribution inside the implementation plan.

---

## 7. Required proof

### Codec and identity

- plaintext RTS packs to 10 B with exact known bytes; encrypted RTS packs to 11 B with a fixed known-answer digest;
- ordinary CTS 3/4-B golden vectors remain byte-identical; terminal plaintext/encrypted CTS pack to canonical 6/7-B
  vectors with exact plane and identity echoes;
- M and flood frames remain byte-identical at 9 B and 43 B respectively;
- malformed/legacy lengths and cross-kind combinations are rejected, including old 3/4-B `already_received=true`
  and 6/7-B terminal shapes without the bit;
- plaintext ids differing only above `ctr_lo` differ;
- encrypted retry input is stable; changing one seed/context bit changes the digest in the known-answer cases.

### Behavior

- reproduce the `s27` pair whose old RTS bytes collide; new identities differ and both payloads deliver;
- exact lost-ACK retry produces an identity-bound terminal CTS and does not redeliver the app payload;
- a delayed terminal CTS for flight A cannot clear a newer flight B with the same endpoints; a mutation that omits
  the echo comparison makes this regression red;
- a different flight with the same old tuple never produces that bit;
- DATA/RTS identity mismatch neither delivers, ACKs, nor seeds the cache;
- both `local_admitted` (S4 spelled it `local_data`) and `alternate_path` forward-credit bases clear only the exact
  redundant copy and emit no app success/failure outcome; a one-bit identity mismatch leaves it pending;
- the S0 producer ledger proves the canonical mark matrix; addressed admission combines the pure wire declaration
  with the receiver-relative target predicate, while an overhearer uses only the wire declaration;
- a same-layer team/static mismatch never cache-matches or earns forward credit;
- completed-cache retry ages are measured, and boundary tests pin the TTL derived from the live retry horizon;
- plaintext/encrypted and plane-local identities never cross-match;
- full-counter case such as `0x0002` versus `0x1002` is distinct.

Every safety test needs a mutation/negative control that makes it red when the identity check, DATA validation, or
full-width comparison is removed.

### System gate

The final comparison must report, not merely re-anchor. Its acceptance floor is deliberately conjunctive and no
⛔⛔ **CORRECTED 2026-08-09 BY [[B162]] — `732` IS RETIRED AND UNREPRODUCIBLE; THE FLOOR IS `≥733` OVERALL AND `≥104` IN `s06`** (the `s06` half was already right). The published per-row column was wrong on **10 of 36 rows, in both directions, from −4 to +9** — ⛔ NOT a constant offset, so nothing here may be patched by adding a difference. Authoritative ladder, one tool revision, one run set: **BASE 733 · DELETE 707 · S1 690 · S2 724 · CURRENT 719**. See `simulation/BASELINE.md` §B162.

existing comparison arm satisfies it: BASE supplies **733/104** deliveries but leaves `s27` red; DELETE makes
`s27` green but supplies only **707/85**. HYBRID must beat both relevant halves rather than choosing one as its
baseline:

- all 36 scenarios and assertion failures;
- ⚠⚠ **`≥733` IS A CONDITIONAL FLOOR: [[B163]] IS OPEN** (§B162 (12), re-confirmed by §B162 (17)-(20)). Two mobiles in
  `s07_seattle_mobile_meshroute` genuinely wear the same **leased** wire id at different times, so the alias refusal is
  **correct** and **`s07`'s figure may be short on every arm by an amount that is not derivable** without a
  time-windowed alias map. ⇒ **the total's uncertainty is one-sided and concentrated in one row**: report `s07`
  separately, and treat neither a pass nor a near-miss at `733` as settled until B163 is resolved. ⛔ Do not resolve it
  by picking one of its readings.
- unique deliveries overall and for `s06`, with target **at least 733 overall and 104 in `s06`** — measured by
  `dm_delivery_breakdown.py --mode dm --json` → `totals.unique_deliveries`, which is THE authority; the raw
  `delivered` event count is a CROSS-CHECK and is never the figure of record — unless the owner
  explicitly accepts a diagnosed delta;
- `s27` at zero failures with both previously lost messages delivered;
- DM and all-frame airtime against both pre-B153 base and the current no-growth implementation;
- collision census by plaintext/encrypted domain;
- duplicate application deliveries (must not worsen the pre-B153 count; B159 remains a separate fix);
- native, s18, every board environment, warning census, flash/RAM, `sizeof(Node)`, and layout/reorder checks.

The s18 hash and stream anchors are expected to move. Record exact before/after values and causally attribute each
slice; never normalize a failed assertion into a new baseline.

---

## 8. Explicitly out of scope

- routing penalty threshold / congestion-NACK policy / adaptive cascade retry (T1–T3);
- B159 sliding DATA-dedup expiry;
- new ACK/NACK wire types;
- compatibility with legacy 7-byte unicast RTS;
- channel/flood frame changes;
- unrelated refactoring.

<!-- Author: OpenAI Codex -->
