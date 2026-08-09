# Hybrid RTS flight identity — implementation plan · 2026-08-08

**Implements:** `docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md`

**Status: S0 COMPLETE · S1 CONDITIONAL PASS · §2.3 HOLD CLEARED · B160 + the `exchange_airtime_ms()` arm OWED BEFORE S2.**

★★ **OWNER RULING 2026-08-08 — §2.3 IS CONFIRMED; the S1+ hold is LIFTED.** ⛔ The former status line
(*"S1+ HOLD FOR OWNER CONFIRMATION OF §2.3 … do not start S1"*) is **WITHDRAWN**. The owner's words:
> *"The 6/7-byte terminal CTS with the complete identity echo and plane bit is the correct design. A terminal CTS can
> clear sender state, so accepting a shorter probabilistic tag or endpoint-only correlation would recreate silent-loss
> risk."*
⇒ ★ **Therefore S3 must compare endpoint, plane, domain, width AND all identity bytes before timer cancellation or any
state/telemetry change.** A shorter tag is **not** an acceptable substitute at any point in this arc.

**S1 verdict: CONDITIONAL PASS.** ⚠ It costs **−17 deliveries (708 → 691)** — attributed: identity wire −9, CTS-wait
correction −8, concentrated in `s16_dense_gateway` (collisions 1282 → 1464, `s16` −16, `s17` −1). ⇒ ★★ **S2–S4 must now
recover ≥41, not ≥24, to reach the ≥732 floor.** The −17 is acceptable **only as an attributed intermediate result.**

**Owed BEFORE S2, in this order (QA's sequencing):**
1. ✅ *(done 2026-08-08)* the stale M/flood comment at `frame_codec.h:302` and B158's **false** channel-capacity
   dependency, both withdrawn in place.
2. **The isolated `exchange_airtime_ms()` measurement arm** — compare `a(8)+a(4)`, `a(10)+a(4)` and a conservative
   `a(11)+a(4)`, focused on **`s16` deliveries, collisions, spread nibbles, jitter caps, CTS and DATA counts**.
   ⛔ **Do NOT alter `retry_jitter_ms()` in that slice** — it is an explicit Lua-parity constant and stays untouched.
3. **B160 — `Plane` continuity** (`txitem_from_pending` drops it ⇒ requeue resurrects a flight as `Plane::AUTO`), landed
   **separately** with its own attribution.
4. **Then S2.**

**Coverage holes that must not reach the final gate** (they do not block S1): the **encrypted arm is corpus-dark**
(2 frames of 9 624) ⇒ **S2–S4 need encrypted validation / cache / terminal-CTS cases**; and the **3-B CTS has zero corpus
instances** ⇒ add a **NAV-disabled node exchange or a small dedicated scenario before S3/S6**.

Leave every slice uncommitted (D4).
Do not start routing tuning or B159 in this arc.

## Outcome

Replace the ambiguous 7-byte unicast RTS with a 10-byte plaintext / 11-byte encrypted shape, validate the identity
against DATA, and restore the two exact-flight optimisations removed by B153/B157:

1. receiver CTS `already_received` after a completed identical flight, conditionally extended to echo the exact
   identity and plane;
2. sender forward-progress credit for the identical flight, retaining `implicit_ack_from_forward` only as the
   historical telemetry name and never as an app ACK.

The point is to repair the evidence those optimisations consume, not to retune the routing system around their
absence.

---

## S0 — COMPLETE: freeze evidence and build the comparison harness (no production behavior change)

S0 passed. The durable evidence is `simulation/BASELINE.md` §HYBRID-RTS-S0. In summary: BASE and DELETE reproduced
byte-for-byte; the single unicast-RTS producer and four-row plane-mark matrix are proven; all 61 historical credits
were unambiguous `(0,0)`; the applicable retention bound is the 150-second gateway-hold class; M is 9 B and flood
is 43 B; and B160 was discovered. HYBRID remains unmeasured until S1.

### Work

1. Read current `simulation/BASELINE.md`, B153/B157/B158/B159, and the B153-DIAG/DIAG2 evidence. Record current HEAD,
   native count, lus checksum, s18 hash/event count, 36-row anchors, unique-delivery totals, and airtime totals.
2. Preserve three named comparison arms from one calibrated base:
   - `BASE`: reconstructed pre-B153 behavior/binary;
   - `DELETE`: current no-growth B153+B157 behavior;
   - `HYBRID`: proposed 3-byte plaintext / 4-byte encrypted identity.
3. Reconstruct the earlier uniform 4-byte arm only as a measurement control. Its known historical evidence was:
   native 1439/75977/0, s27 5→0, s18 `504785b9`/277266, all 36 rows moved, RAM delta 0. Confirm or explain any
   difference; do not treat those numbers as the final gate.
4. Add/reuse read-only scripts that calculate from NDJSON:
   - unique app deliveries and duplicates by scenario;
   - `send_failed` totals;
   - RTS/DATA/ACK/NACK airtime through the repo's `airtime_ms()` at each event's actual PHY;
   - old-tuple at-risk pairs/aliases and new-id aliases, separated by plaintext/encrypted domain.
5. Instrument completed-flight retry age: for every matching retry RTS, record elapsed time since the receiver
   completed that exact flight, identify which cases the restored cache would recover, and print distribution,
   maximum, and counts at/beyond 10 s and 30 s. Audit the live retry/cascade/gateway handoff horizons that can
   preserve the same identity and revisit one hop.
6. Produce a reachability ledger for every unicast RTS producer, relay, requeue, last-mile, and cross-layer reinjection
   path. For each, record semantic plane plus emitted `(addr_len, mobile_src)` and prove or refute the four-row canonical
   matrix in spec §4.3. Separately census the historical 61 implicit-forward firings by pending plane and observed marks.
7. Print positive controls: event count reproduced, a deliberate `len+1` airtime mutation disagrees, and the
   reconstructed BASE lus is byte-identical to its recorded binary.

### Gate

- Real tree production sources remain byte-identical during diagnosis.
- The comparison tool reproduces the published 732/728/731/708 delivery ladder or reports a named cause.
- A mutation of each census key changes its result; no green-from-zero probe.
- Every reachable RTS producer fits the canonical plane-mark matrix. If not, stop before making plane disagreement
  fatal and report the exact producer and affected historical credits. Receiver cache may use the receiver-relative
  admitted plane, but implicit-forward credit stays disabled for every ambiguous mark until the wire is repaired.

**Stop condition:** if BASE cannot be reconstructed/calibrated, report before implementation. Attribution without a
common base is not acceptable.

---

## S0P — B160 Plane continuity repair (separate pre-wire slice)

This is a pre-existing carrier defect, not part of the identity wire change. Land and attribute it before S1 so its
behavior delta cannot be assigned to the new codec.

### Production change

1. In the single canonical `txitem_from_pending()` helper, copy `pt.plane` to `it.plane` alongside the other shared
   flight fields. Update the helper shared-core ledger.
2. Do not add assignments at the three callers. The purpose of the helper is to keep cascade requeue, gateway hold,
   and long-busy requeue from drifting.
3. Preserve explicit GLOBAL, TEAM, and AUTO values verbatim. Do not re-resolve an explicit plane from `dst`.

### Required regressions

- an explicit GLOBAL flight whose destination is also known as a team peer remains GLOBAL after each of the three
  requeue mechanisms;
- an explicit TEAM flight remains TEAM, and a legitimately AUTO carrier remains AUTO;
- each case proceeds far enough to inspect the next RTS/route decision, rather than testing only the helper return;
- mutation: omit the one helper assignment and prove all three path cases red.

### Gate

- native, s18, and all 36 corpus streams; attribute every moved stream specifically to restored Plane continuity;
- `sizeof(TxItem)`, `sizeof(PendingTx)`, `sizeof(Node)`, and board RAM must not move (the field already exists);
- no caller-local Plane reconstruction or fourth pending-to-item converter exists.

---

## S1 — one identity helper and canonical codec

### Production changes

1. U1-audit existing nonce/hash helpers first. Add exactly one pure flight-identity producer in `lib/core`, colocated
   with the existing canonical nonce derivation if that avoids duplication.
2. Implement:
   - plaintext bytes: `origin | ctr_hi | ctr_lo`;
   - encrypted bytes: first 4 bytes of BLAKE2b-512 over `0xE1 | seed[8] | ctr_hi | ctr_lo | dst`, following the
     project's existing hash-then-truncate convention.
3. Extend `rts_in`/`rts_out` by appending identity/domain fields so positional initializers are not silently shifted.
   Use byte arrays or explicit codec writes; never serialize a host-endian `uint32_t` by memory copy.
4. Change unicast codec only:
   - plaintext exactly 10 B;
   - encrypted exactly 11 B;
   - legacy 7 B rejected;
   - M 9 B and flood 43 B exact and byte-identical;
   - cross-kind/length combinations rejected loudly.
5. Extend the CTS codec conditionally:
   - ordinary `already_received=false` shapes remain byte-identical at 3/4 B;
   - terminal plaintext/encrypted shapes are exactly 6/7 B: the 3-B base plus the matching 3/4-B identity;
   - terminal CTS carries no NAV byte; its otherwise-unused chosen-SF bits carry one plane bit plus reserved zeros;
   - reject old 3/4-B terminal bits, 6/7-B non-terminal shapes, non-canonical reserved bits, and wrong tail widths.
6. Add semantic `unicast_rts_wire_len(crypted)` and `terminal_cts_wire_len(crypted)` helpers (or surrounding idiom).
   Do not redefine global RTS/CTS constants.
7. Audit every unicast RTS producer and relay and populate the new identity from the existing canonical DATA carrier;
   extend only the single `TxItem`/`PendingTx` conversion if a field is genuinely missing. Parser output must expose the
   domain and identity to the receiver.
8. In the same slice, update `start_rts_timeout` to price the actual 10/11-B request plus the possible 6/7-B terminal
   response. The sender cannot know whether the receiver cache will hit. The current estimate prices a phantom
   8-byte RTS for the live 7-byte wire (one payload byte of allowance), but 10/11 bytes exceed it;
   S1 must not create an intermediate under-timed protocol.

### Tests

- fixed plaintext byte vector, including counter high bits;
- fixed encrypted BLAKE2b known-answer vector;
- exact retry stability and one-bit/context changes;
- canonical length matrix including malformed 7/8/9/10/11/12 cases;
- unchanged M/flood and ordinary 3/4-B CTS golden vectors;
- fixed 6/7-B terminal CTS vectors, including plane and identity bytes;
- CTS cross-shape matrix: terminal bit, frame length, reserved bits, domain, and tail width must agree;
- compiler-visible field/default tests for every current initializer shape.

### Gate

- native test wrapper and real binary pass;
- full corpus is run and archived as `S1-WIRE`; assertions must remain green even though every unicast wire event may
  move;
- report flash/stack cost of BLAKE2b helper per board profile;
- no behavior claim yet: the terminal CTS codec exists, but `already_received` and forward credit remain disabled in
  this slice.

**Do not update canonical baselines yet.** S1 is an attribution arm, not the final protocol.

---

## S2 — identity continuity, DATA validation, and completed-flight cache

### Production changes

1. Confirm the B160 S0P gate is complete, then trace the full `TxItem → PendingTx → wire → PendingRx → DATA/PostAck`
   path. Extend only the existing canonical conversions (U2); do not hand-copy a parallel carrier.
2. Verify sender and relay recomputation uses the canonical DATA fields wired in S1. Prefer recomputation over adding
   redundant fields where the carrier already retains origin/ctr/seed/dst.
3. Store incoming identity/domain and explicit team/static plane in `PendingRx`. Measure layout before choosing member
   order; do not reorder unrelated members to hide a size increase. Per-layer storage is not plane isolation.
4. At DATA reception, recompute and compare before delivery/ACK/cache store. Mismatch:
   - emits named diagnostic;
   - clears pending RX;
   - no app delivery, ACK, cache store, or route success credit.
5. Before adding storage, instrument the S1 arm to measure the maximum simultaneously-live completed identities per
   immediate sender and globally over the 150-second gateway-hold lifetime. Report occupancy distribution, maximum,
   and the exact cache hits lost by capacities 1..N. A zero-overlap result needs a known-positive synthetic control.
6. Restore a bounded completed-flight cache in each `LayerRuntime`, using the on-air immediate sender for both store
   and lookup. Never use `meta.src_hint` as link identity. Store `dst + team/static plane + domain/width + full id + expiry`.
   Choose the smallest deterministic capacity that loses no required hit at the acceptance floor; do not assume one
   latest entry per sender and do not allocate an unbounded/per-peer dynamic structure.
7. Add two deliberately distinct helpers/contracts:
   - pure `rts_wire_team_plane(r)` for the sender-declared wire plane and overhear matching;
   - existing receiver-relative `team_addr_for_us(next, addr_len)` for addressed team admission.
   For an addressed RTS, wire TEAM requires `team_addr_for_us`; wire STATIC/GLOBAL requires `for_static_rts`. Store the
   admitted wire plane in `PendingRx`/cache. At DATA, stored TEAM requires `for_team_data`; stored STATIC/GLOBAL requires
   `for_static_data`. An overhearer never calls `team_addr_for_us` to classify someone else's frame. Never infer plane
   from `is_team_peer(src)`.
8. Define `completed_flight_cache_ttl_ms = gateway_send_giveup_ms` (currently 150,000 ms). Do not restore the
   historical 10-second value by name or duplicate 150,000 as an unrelated literal. Expire/replace entries
   deterministically. A delayed `send_giveup` callback beyond 150 seconds does not extend the retry lifetime.

### Tests

- every live conversion carries or can reproduce the same identity;
- relay recomputation equals origin computation for plaintext and encrypted flights;
- mismatch test asserts all four absences: delivery, ACK, cache, route-success side effect;
- cache key includes immediate sender, dst, team/static plane, domain, and full identity;
- capacity tests cover two or more live identities from one immediate sender; a capacity-one mutation must lose a
  measured/synthetic required hit and make the test red;
- every reachable producer fits the S0 canonical mark matrix; poison each producer class independently;
- addressed wire TEAM + false `team_addr_for_us`, and addressed wire STATIC/GLOBAL + false `for_static_rts`, are
  refused before `PendingRx` or cache lookup; an overheard team frame with false `team_addr_for_us` is not rejected as
  malformed and remains available to the implicit-forward matcher;
- stored TEAM + false `for_team_data`, and stored STATIC/GLOBAL + false `for_static_data`, produce no delivery, ACK,
  cache, or route-success credit;
- otherwise identical same-layer team/static flights do not cache-match;
- sim `src_hint != on-air src` does not change the key;
- expiry boundary before/at/after `gateway_send_giveup_ms`; a 10-second mutation must under-deliver the measured
  18,971-ms retry and a 30-second mutation must miss the audited gateway-hold case;
- mutation: skip DATA validation or key by `src_hint` and prove the suite red.

### Gate

- native + real binary;
- s18 and all 36 scenarios, with S2 delta versus S1 attributed;
- `sizeof(Node)`, `sizeof(LayerRuntime)`, `sizeof(PendingRx)`, `-Wreorder`, per-board RAM and flash.

### ★ THE LANDED BOUNDARY (2026-08-09) — **S2 AND S3 SHIPPED TOGETHER; THIS SECTION DESCRIBES WHAT IS IN THE TREE**

**S2 as landed INCLUDES the whole of S3 below.** The plan as written above ended S2 with *"zero local producers of
`already_received=true`"* and left S3 to restore the terminal CTS separately; the implementation combined them,
because the completed-flight cache S2 builds has no observable effect until something reads it, and the terminal CTS
is the only reader. Combining is therefore sound — but the two slices are **one commit and one measurement**, and no
arm exists that has S2's cache without S3's terminal answer.

⇒ **The consequences for anyone reading this plan as a checklist:**
- the S2 gate item *"zero local producers of `already_received=true` remains true at the end of S2"* **does NOT hold
  and must not be asserted** — the landed slice deliberately produces them (202 terminal CTS frames in the corpus at
  the S2 measurement, 188 after the S2b NAV fix below);
- the S2-vs-S1 corpus delta (+37 deliveries on the notes' metric) is the delta of **S2+S3 combined**, and cannot be
  split between them after the fact;
- **S2b (2026-08-09)** is a follow-up correcting two ordering/behaviour defects in the landed combination — see
  `simulation/BASELINE.md` §HYBRID-RTS-S2 for what it changed and what it cost.

---

## S3 — restore exact `already_received` CTS — ★ **LANDED AS PART OF S2, NOT SEPARATELY** (see the boundary note above)

### Production changes

1. In free-receiver RTS admission, consult the completed cache only with the complete canonical tuple.
2. Exact match emits the canonical 6/7-B terminal CTS with `already_received=1`, explicit plane, and the complete
   cached identity echo. It carries no NAV hint and does not allocate a fresh `PendingRx`.
3. At the sender, accept terminal completion only while `awaiting_cts` and only when endpoints, plane, domain/width,
   and every echoed identity byte match the current `PendingTx`. Check before cancelling timers, learning/confirming a
   link, clearing state, or emitting any success-shaped telemetry.
4. A terminal mismatch is ignored and leaves all state/deadlines unchanged. An RTS cache miss follows ordinary receive
   admission and waits for DATA.
5. Keep pending-RX duplicate re-CTS separate and ordinary: `already_received=0`, 3/4 B, DATA still required. Telemetry
   `dup` is not a proxy for the terminal bit; tests must parse the actual CTS frame.
6. Audit the CTS retry/duty stash. A stale terminal frame may still be transmitted, but its echo must make it harmless
   to a later flight; do not claim the stash is flight-bound when it is not.

### Required regressions

- lost ACK → identical retry → canonical terminal CTS → sender completes → app payload delivered once;
- delayed/stashed terminal CTS for completed flight A arrives while newer flight B to the same next hop awaits CTS:
  echo mismatch leaves B pending, all timers intact, and no success-shaped event emitted;
- same old tuple, different full counter/origin → ordinary CTS and DATA accepted for the new flight;
- encrypted same seed/context → terminal match; changed seed/context → ignored terminal response;
- exact plane isolation, including same numeric ids in team and static planes of one layer;
- normal 3/4-B CTS remains byte-identical and schedules DATA; old 3/4-B terminal form is rejected;
- `s27` old-collision pair both deliver;
- mutations: omit sender echo/plane comparison or accept a 3/4-B terminal CTS; each makes the delayed-A/new-B case red.

### Gate

- native + full corpus;
- report completed-cache hit count, 6/7-B terminal CTS count, stale-terminal mismatch count, unique deliveries,
  duplicates, and actual CTS airtime delta from S2;
- `s27` zero assertion failures and both `re-m1`/`re-m3` delivered.

---

## S4 — restore exact implicit ACK from downstream forwarding

### Production changes

1. Reintroduce the forwarding-credit arm with exact identity matching: expected next hop, destination, team/static
   plane, domain/width, and full identity.
2. Carry `data_ever_transmitted` in `PendingTx`, set only at the actual DATA transmission boundary. An exact match may
   clear the redundant local pending copy under one of two named bases: `local_data` when this node previously aired
   DATA, or `alternate_path` when the selected next hop already forwards the exact flight without a local DATA send.
3. Neither basis is final-delivery evidence. Do not emit `send_acked`, `send_e2e_acked`, delivered, or `send_failed`;
   preserve any independently armed end-to-end ACK wait. A mismatch changes no deadline, pending state, route state,
   or app outcome.
4. Decode and compare team/static plane through S2 shared wire helper, not from layer membership or peer lookup.
5. Remove/fence the obsolete claim that `payload_len` disambiguates identity. It remains only a frame consistency/NAV
   field.

### Required regressions

- exact downstream forward clears the correct pending flight without another DATA send;
- `local_data` basis: an exact forward after a real DATA transmission clears the copy and emits only the named
  diagnostic;
- `alternate_path` basis: the measured `awaiting_cts`/no-local-DATA shape clears only the exact redundant copy,
  emits only the alternate-path diagnostic, and produces no app success/failure push;
- the `s27` two-origin, same-`ctr_lo`, same-length frame does not clear the other flight;
- one-bit id, destination, next-hop, plane, or domain mismatch leaves the flight pending;
- same-layer team/static identities with otherwise identical numeric fields do not cross-match;
- no producer or test uses telemetry `dup` as a wire-bit proxy;
- mutations: remove full-id/plane comparison, set `data_ever_transmitted` before radio DATA transmission, or
  synthesize `send_acked`; each must make a targeted case red.

### Gate

- native + full corpus;
- count exact implicit credits by pending state and compare with the historical 61 firings;
- split those credits by `local_data` versus `alternate_path`, and reconcile with the measured 7/46/8 state census;
- unique deliveries and airtime versus S3, DELETE, and BASE;
- no silent disappearance: every originated payload is delivered or has an attributable terminal failure.

---

## S5 — remaining RTS/CTS timing audit (B158) and PHY ledger

The exact CTS-wait correction lands with S1 because the grown frame cannot be left temporarily under-timed. This
slice audits the remaining timing meanings and closes B158 with full PHY evidence; it is not optional cleanup.

### Production changes

1. Audit every production use of `RTS_LEN`, `airtime_routing_ms(8)`, RTS retry jitter, NAV/reservation, Lua parity,
   M-broadcast, and flood timing. Produce a table with semantic owner for every use.
2. Verify `start_rts_timeout` prices the actual 10/11-B unicast RTS plus the possible matching 6/7-B terminal CTS
   from S1, while preserving the existing turnaround components. Ordinary 3/4-B CTS still cancels the wait early.
3. Change retry jitter/NAV only where the use-site actually represents unicast RTS airtime and the measured longer
   frame violates its invariant. Do not blanket-replace constants for M/flood or Lua.
4. Add a timing helper test over every supported SF/BW/CR for CTS 3/4/6/7 B and RTS 7/9/10/11 B. Include
   `s06` BW 62.5 kHz and SF12 worst case.

### Gate

- table of repo `airtime_ms()` for CTS 3/4/6/7 B and RTS 7/9/10/11 B at all supported PHY combinations;
- timeout margin non-negative for plaintext and encrypted unicast including their longest possible terminal CTS at
  every PHY;
- M/flood and unrelated Lua timing byte/behavior-identical unless a separately named invariant required movement;
- full native/corpus gate to attribute any retry-schedule delta specifically to timing.

---

## S6 — cumulative decision gate and durable documentation

### Mandatory cumulative comparison

Produce one table for BASE, DELETE, prior uniform-4B control, and final HYBRID:

- native cases/assertions/failures;
- s18 hash/event count;
- 36/36 assertions and changed streams;
- unique deliveries overall, `s06`, `s18`, `s27`, twin;
- duplicate application deliveries;
- send failures by terminal class;
- RTS/CTS/DATA/ACK/NACK and total airtime using actual event PHY;
- old-tuple at-risk/alias count and new plaintext/encrypted identity collision count;
- flash/RAM per board and `sizeof` deltas.

### Acceptance floor

- this is intentionally a Pareto gate which no existing arm passes: BASE provides 732/104 but `s27` is red, while
  DELETE provides zero `s27` failures but only 708/85; HYBRID must satisfy both halves simultaneously;
- all assertions green; `s27` has zero failures and both target payloads deliver;
- target ≥732 unique corpus deliveries and ≥104 in `s06`; any shortfall needs a flight-level diagnosis and explicit
  owner acceptance, not a baseline edit;
- no worsening beyond the pre-B153 duplicate count (B159 is not fixed here);
- new plaintext identity has zero aliases by construction/census; encrypted identity has zero observed aliases;
- final DM airtime must be reported against both BASE and DELETE. Do not describe symbol-event count as airtime.

### Full D1/D2 gate

1. `pio test -e native`, then run `./.pio/build/native/program`.
2. Build lus and run s18 plus all 36 corpus streams with zero assertion failures.
3. Build every board environment sequentially; run warning census.
4. Verify `-Wreorder`, `sizeof(Node)` assertion, and per-board RAM diff.
5. Run codec negative/mutation probes and print match counts/positive controls.

### Documentation

- `docs/frames.md`: exact 10/11-byte unicast RTS offsets; 9/43 unchanged;
- `docs/protocol.md`: identity, validation, completed cache, already-received and implicit-ACK behavior;
- `simulation/BASELINE.md`: each slice's causal delta and the final anchors;
- `MEMORY.md`: if the project index exists at implementation time, add one durable line pointing to the ruled design;
- bug register:
  - B153 closed by strong flight identity + restored receiver optimisation, preserving the deletion history;
  - B157 closed by exact identity + restored implicit ACK, preserving the deletion history;
  - B158 reopened then closed by measured actual-length timing;
  - B156 remains closed only if `src_hint` cannot enter the restored cache key;
  - B159 remains open and explicitly independent;
  - B160 closed only after all three requeue paths preserve explicit Plane through the shared helper;
- bench script: all nodes reflashed together; lost-ACK/retry observation if a controllable metal procedure exists.

Do not commit. Report the exact final tree status and every skipped gate (D3/D4).

---

## Stop/escalate conditions

Stop and return evidence rather than improvising if:

- the encrypted digest requires exposing origin or adding a new crypto dependency;
- canonical DATA cannot reproduce the RTS identity at a relay;
- codec cannot reject legacy/untyped lengths unambiguously;
- DATA mismatch currently cannot be dropped without an unrelated state-machine redesign;
- final delivery stays below the acceptance floor after exact identity is restored;
- a proposed timing fix would change M/flood/Lua behavior without a demonstrated invariant;
- the implementation starts requiring routing T1–T3 or B159 to become green.

<!-- Author: OpenAI Codex -->
