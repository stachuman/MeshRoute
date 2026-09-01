# B278 — Mobile feedback for home-originated static custody failures

**Status:** DRAFT FOR OWNER + QUALITY-AGENT REVIEW — no implementation authorized by this document  
**Date:** 2026-09-01  
**Depends on:** §CUSTODY A–G (landed), B251 (closed)  
**Required before:** remote-admin v2 Slice 9; any metal claim for custody-aware mobile send presentation  
**Separate prerequisite for remote admin:** B112 (the mobile wrapper may still be hop-ACKed before outward admission is known)

## 1. Decision summary

B278 extends custody **feedback**, not custody generation.

For a mobile-originated send, the home already turns the mobile flight identity
`{mobile, target, ctrM}` into a static/global flight under the home's identity and counter
`{home, resolved target, ctrH}`. A static relay can report a custody failure for the latter,
but the report currently terminates at the home. B278 makes the home correlate that report,
retain its existing local diagnostic, and originate one translated custody outcome back to
the mobile through the existing typed-DATA `send_by_hash` / `DST_HASH` / hosted-mobile
last-mile machinery.

```text
M1 -- MOBILE_SEND / ctrM --> H1 -- ordinary static DATA / ctrH --> ... relay R ... --> target
                                  ^
                                  | DATA_TYPE_CUSTODY_FAILURE v1, failed_ctr=ctrH
                                  |
                         H1 validates + stores
                                  |
                                  +-- translated 0x81 / DST_HASH=M1 --> current home --> M1
                                      {original report, reporter=R, mobile_ctr=ctrM}
```

The translation is a factual, unauthenticated custody observation. It is never an RPC
response, an authentication result, proof that the target missed the payload, or permission
to retry automatically.

### 1.1 Proposed rulings for this review

The design recommends all four:

1. **Reuse `DATA_TYPE_CUSTODY_FAILURE` (`0x81`).** Allocate one translated-record flag and
   a four-byte v1 tail. Do not allocate another DataType or a private mobile control frame.
2. **Reuse normal mobile delivery.** The home calls the existing hash-addressed typed-DATA
   path. Direct hosting, a redirect/breadcrumb, a cached new home and H-resolution remain
   the routing authorities; B278 adds no second mobile locator.
3. **A positive E2E ACK retires the correlation.** If the ACK reaches the home first, a later
   custody report remains a correct diagnostic at the home but is not forwarded to the
   mobile. Delivery is already positively confirmed, so losing a later, nonterminal
   diagnostic cannot downgrade the mobile's state.
4. **Use a derived 750 s correlation ceiling with no live-unconfirmed eviction.** The
   proposed bound is `e2e_ack_deadline_xl_ms + seen_origin_ttl_ms` = 300 s + 450 s. Keep the
   existing eight-row capacity initially; pressure refuses before the mobile hop ACK.
   Slice S0 must measure the resulting live occupancy and board cost before the constants
   become final.

## 2. Problem and current truth

### 2.1 The identity split

The current mobile path has two legitimate counters:

- `ctrM`: minted by the mobile for the hop/delegation the mobile owns;
- `ctrH`: minted by the home for the static flight it originates on the mobile's behalf.

The static relay sees only the home-originated carrier. The landed 24-byte custody record
therefore correctly contains:

- `failed_origin = H1`;
- `failed_dst = resolved target node id`;
- `failed_ctr = ctrH`;
- `failed_type = outward DataType`;
- `dst_hash32 = the outward target hash` when the carrier carried one; and
- `reporter_layer = H1/R's static layer` in the supported same-layer case.

M1, however, owns a pending operation under `ctrM` and its mobile-visible target. Counter-only
translation is unsafe; B251 already demonstrated why destination-scoped counters need the
rest of their identity.

### 2.2 Existing mechanisms that must be reused

The code already has both halves B278 needs:

- `DelegAck` in `lib/core/node.h` and the reserve/activate/translate implementation in
  `lib/core/node_hashlocate.cpp` preserve `ctrH -> ctrM` for an E2E ACK. The current row is
  24 bytes × 8, has RESERVED and ACTIVE phases, prunes before admission, and never evicts a
  live row.
- `send_by_hash` and the hosted-mobile last-mile fork in `lib/core/node_hashlocate.cpp` /
  `lib/core/node_mac_rx.cpp` already route typed DATA to a stable mobile hash. They cover a
  locally hosted mobile and a mobile reached through a current/redirected home.

They are authorities, not examples. B278 generalizes the correlation row and reuses the
delivery path.

### 2.3 Why the current reverse-ACK row is insufficient

The current row cannot simply be queried from the custody receiver:

1. it is allocated only when `DATA_FLAG_E2E_ACK_REQ` is set; remote RPC and other internal
   carriers do not necessarily request an E2E ACK;
2. its ACTIVE `peer` overwrites the originally requested target, while complete mobile-side
   correlation needs both the requested identity and the return discriminator;
3. it carries no outward `DataType`;
4. `deleg_ack_translate()` clears the row immediately, so a custody report arriving before
   an ACK would consume the ACK mapping if the same operation were reused; and
5. its 300 s TTL is the delegated ACK deadline, not a derived outward-failure-plus-return
   diagnostic horizon.

### 2.4 Why the 24-byte report cannot be forwarded unchanged

An unchanged report is correctly rejected or miscorrelated at M1:

- `failed_origin` names H1, not M1;
- `failed_ctr` is `ctrH`, not `ctrM`;
- after H1 re-originates it, the outer origin names H1 and no longer names the reporting
  relay R; and
- the current receiver requires `failed_origin == this node` for a direct v1 report.

The translation must preserve the original record while explicitly carrying the missing
mobile identity.

## 3. Scope

### 3.1 In scope

- same-layer, plaintext, static/global outward carriers re-originated or counter-translated
  by a home for a hosted mobile;
- a complete bounded correlation from `{mobile, target, ctrM, type, layer}` to the admitted
  static flight `{resolved target, ctrH, type, layer}`;
- local home storage/push of the original report before translation;
- translated custody delivery through the existing mobile hash/last-mile path;
- mobile validation, durable custody record, live Push, JSON and USB presentation;
- exact monotonic interaction with an E2E ACK; and
- remote-admin v2's prerequisite transport evidence, without parsing an RPC body.

### 3.2 Explicitly out of scope

- team-plane custody;
- custody generation on M1→H1 mobile delegation itself;
- custody generation on H2→M2 hosted-mobile last mile;
- cross-layer custody generation or cross-layer failed-flight records;
- fixing B112's wrapper-ACK-before-outward-admission defect;
- automatic message/RPC retry;
- interpreting RPC request IDs, opcodes, authentication or responses in the correlation
  layer;
- durable correlation across a reboot of H1;
- an OLED history screen or durable outbox correlation (Slice H / companion work owns
  presentation); and
- cryptographic authentication of custody evidence.

## 4. One correlation authority

### 4.1 Rename and generalize, do not add a second ring

`DelegAck` becomes a delegated-flight correlation (names are implementation choices, but
there must be one ring). The proposed packed row is 32 bytes:

```text
uint64  ts_ms
uint32  mobile_hash
uint32  target                 original mobile-visible target
uint32  return_peer            discriminator exposed by a returning ACK / failed static flight
uint16  ctr_h
uint16  ctr_m
uint8   layer
uint8   outward_type
uint8   target_kind            node_id | key_hash
uint8   return_kind            node_id | key_hash
uint8   state                  free | reserved | active
uint8   obligations            e2e_ack | custody | custody_forwarded
padding to the board ABI's verified size
```

The row retains the original target instead of overwriting it at activation. The proposed
layout is 32 bytes on the present ABIs, making the existing eight-row ring 256 bytes instead
of 192 bytes. That **+64 B is a proposal, not an accepted estimate**: the ABI probe and both
ruled board builds must measure it.

### 4.2 Reservation identity

A RESERVED row is uniquely keyed by:

```text
{mobile_hash, ctrM, target_kind, target, outward_type, layer}
```

`target_kind` is the mobile API's addressing choice, not a fact re-derived after resolution:

- a same-layer `MOBILE_SEND` wrapper is hash-addressed, so it reserves `key_hash` and the
  wrapper's `dst_key_hash32`;
- a direct hosted-mobile transit addressed by node id reserves `node_id` and `d.dst`, even
  when the ordinary L2-collision guard also attached a destination hash to the carrier.

This distinction is why the translated tail carries `target_kind`: the fixed custody prefix
can contain both values but cannot say which value names the mobile's pending operation.

An exact same-flight retry refreshes the reservation. A different tuple never replaces it.

The home reserves before it sends the hop ACK to the mobile when the prospective outward
carrier needs either:

- E2E-ACK translation; or
- custody translation under §5.

If no row is available, the home sends the existing retryable BUSY_RX NACK and does not ACK
the mobile carrier. This is correlation-resource admission only. It does not claim that the
future static route, parked-send admission or RPC target admission has succeeded; B112
remains open for that wider first-hop truth.

### 4.3 Activation identity

Activation occurs only after the outward item is genuinely queued/parked under the existing
`SendDispatch` admission authority. It records:

```text
{ctrH, return_kind, return_peer, layer, outward_type}
```

No two ACTIVE rows may have the same wire-visible return key
`{ctrH, return_kind, return_peer, layer}`, **even when their mobile hashes differ**. A custody
record does not carry the mobile hash, so allowing two such rows and choosing the first would
be a misdelivery. This strengthens B251's present collision check.

Admission failure releases the reservation through the same one-owner cleanup path. A
minted counter is not admission evidence.

### 4.4 Custody lookup identity

The direct report at H1 matches exactly one ACTIVE row on all of:

```text
ctrH             == record.failed_ctr
return_kind      == node_id
return_peer      == record.failed_dst
layer            == record.reporter_layer
outward_type     == record.failed_type
target_kind/id   == record.failed_dst
    OR
target_kind/hash == record.dst_hash32 with HAS_DST_HASH set
custody obligation remains set
```

Zero matches means an uncorrelated H1 diagnostic. More than one match is an invariant
failure and also means an uncorrelated H1 diagnostic. Neither case forwards anything or
guesses by counter.

### 4.5 Lifecycle

- **ACK arrives first:** translate `ctrH -> ctrM`, queue the existing last-mile E2E ACK, and
  clear the row. A later custody report is stored at H1 only. It cannot downgrade DELIVERED.
- **Custody report arrives first:** store/push it at H1, originate the translated report,
  set `custody_forwarded` only when that send is queued or parked, and retain the row if an
  E2E ACK is still owed. The later ACK still translates and then clears the row.
- **Custody-only row:** a successfully admitted translated report clears the row. A refused
  translation leaves it until expiry, allowing a genuinely fresh repeat report to retry.
- **Duplicate report after `custody_forwarded`:** no second translated send. Existing raw
  diagnostic-storage rules may still retain a genuinely new report at H1.
- **Expiry:** clear silently except for bounded scalar telemetry/counters. A subsequent
  report remains a valid H1 diagnostic and is never attached to another mobile.

Custody processing therefore never consumes the ACK obligation. That is the key ordering
property B278 adds.

## 5. Which mobile flights reserve custody correlation

The pre-ACK candidate predicate mirrors only facts knowable before the home constructs the
outward `PendingTx`. It is one named predicate, not a second hand-written type list.

It returns true only when the prospective outward carrier is:

1. a verified direct hosted-mobile transit or a valid `DATA_TYPE_MOBILE_SEND` wrapper from
   a live hosted row;
2. plaintext at the outward DATA-frame level;
3. static/global and same-layer;
4. a normal DATA carrier, not channel M/FLOOD;
5. not cross-layer, team, gateway re-inject or hosted-mobile last-mile;
6. not `DATA_TYPE_E2E_ACK` or `DATA_TYPE_CUSTODY_FAILURE`; and
7. addressed to a nonzero target with a complete target identity.

`DATA_TYPE_SEALED_RELAY` remains eligible because its **outer DATA frame** is plaintext; the
application body being sealed does not hide the routing identity the custody record needs.
Internal types, including `REMOTE_CMD` and `REMOTE_RESP`, are otherwise eligible exactly as
custody §10.1 rules.

The direct-mobile-transit and wrapper paths must both be tested. Adding the predicate only
to `send_by_hash` would miss the counter-translating forward path in `handle_data`; adding it
only to `handle_data` would miss the wrapper/park path.

## 6. Wire extension: translated custody record

### 6.1 Same DataType, same fixed prefix

The translated outcome remains `DATA_TYPE_CUSTODY_FAILURE (0x81)`. The first 24 bytes are
the original validated v1 record without reinterpretation.

Allocate `notice_flags` bit 6:

```text
0x40  CUSTODY_FLAG_HOME_TRANSLATED
0x80  remains reserved and must be zero
```

The current `0xC0` reserved mask becomes the bit-7 mask. A direct v1 transmitter still emits
24 bytes with bit 6 clear.

### 6.2 Four-byte translated tail

When bit 6 is set, `record_len` is at least 28 and offsets 24–27 are:

```text
offset  size  field
24      1     original_reporter   outer origin of the direct report received by H1
25      1     target_kind         0 = node_id, 1 = key_hash
26      2     mobile_ctr          ctrM, little-endian and nonzero
```

The rest of the complete identity already exists in the fixed prefix:

- translating home: `failed_origin`;
- original static counter: `failed_ctr`;
- mobile-visible target: `failed_dst` when `target_kind == node_id`, or `dst_hash32`
  when `target_kind == key_hash`;
- outward type: `failed_type`;
- home/static layer: `reporter_layer`.

No mobile hash is repeated in the body. The outer `DST_HASH`/direct hosted-row addressing is
the recipient authority, and repeating it would create two values to cross-check.

### 6.3 Codec authority

There remains one shared custody codec.

- `pack_custody_failure` continues to produce only the direct 24-byte form.
- Add one explicit translated pack operation using the same prefix writer and the four-byte
  tail; no caller writes offsets.
- `parse_custody_failure` returns whether the translated flag is set plus the parsed tail.
- bit 6 clear: the existing `record_len >= 24` future-tail rule remains unchanged.
- bit 6 set: require `record_len >= 28`, valid `original_reporter`, a defined
  `target_kind`, a target value consistent with that kind, and nonzero `mobile_ctr`;
  preserve any bytes beyond 28 as a future tail.
- bit 7, malformed length and every existing v1 invariant remain fail-closed.

The record version stays 1: the fixed prefix retains its meaning and the extension is
self-described by a newly allocated flag plus `record_len`. `protocol::wire_version` also
stays unchanged under M3's reflash-together ruling. During a mixed-build reflash, the old
receiver rejects bit 6 as reserved; it cannot misread the translated record as an ordinary
message.

### 6.4 Why not a second DataType

A second type would duplicate:

- the recursion exclusion;
- the internal/persistent trait decision;
- inbox visibility policy;
- Push and pulled-record rendering; and
- the semantic statement that this is custody evidence, not a new failure class.

The extension changes correlation metadata, not the reported terminal fact. One type with
one codec is the smaller and safer authority.

## 7. Home receive and translation

For a direct 24-byte report addressed to H1, the landed §CUSTODY-G order remains load-bearing:

1. parse and validate all direct-report rules;
2. append the original record to H1's inbox store;
3. enqueue H1's existing live custody Push;
4. look up the delegated correlation using §4.4;
5. if exactly one row matches and is not already forwarded, materialize one bounded
   translation action containing the 28-byte record and the exact row identity;
6. release the received custody carrier through the existing `become_free()` order; and
7. originate the translated record, then update the row only if its complete identity still
   matches the materialized action.

Translation never suppresses or rewrites H1's original diagnostic. Storage failure at H1
does not fabricate persistence, but it also does not suppress the live best-effort mobile
outcome.

There must be one owner of the receive-carrier release. Do not call `send_by_hash` from
inside the current `custody_failure_receive()` and then let its caller call `become_free()`
again. The bounded action may not copy `PostAck`, `PendingTx`, a 241-byte payload buffer or
the complete correlation ring; its record is exactly 28 bytes and its remaining fields are
scalars. The slice must measure the resulting RX-stack movement.

The translated send is:

```text
destination       row.mobile_hash, through send_by_hash
type              DATA_TYPE_CUSTODY_FAILURE
body              28-byte translated custody record
plane             GLOBAL
crypt             off
E2E_ACK_REQ        clear
generic lifecycle none (the existing DataType trait)
app inbox text    never
```

`send_by_hash` owns direct hosted delivery, redirects, cached remote homes and H-resolution.
B278 must not inspect `_mobile_reg` and then hand-build a second `TxItem` except inside the
existing helper's own implementation. A moved mobile may therefore receive the outcome
through its new home; the DATA outer origin remains H1 and the destination hash remains M1.

If enqueue/park is refused, emit one bounded scalar diagnostic such as
`deleg_custody_forward_refused{mobile_hash,dst,ctrH,ctrM,type}`. Never include record bytes,
never push a generic `send_failed`, and never generate custody about this custody carrier.

## 8. Mobile validation and consumption

### 8.1 Direct and translated modes are distinct

The receiver branches after the shared codec:

- **direct mode** (bit 6 clear): retain every landed §13 rule, including
  `failed_origin == this static node` and `reporter_layer == active_layer`;
- **translated mode** (bit 6 set): accept only on a configured mobile and apply the rules
  below. A static node never treats a translated record as its direct report.

### 8.2 Translated contextual validation

Before storage or Push, all must hold:

1. every common frame/codec/count/type rule from the direct receiver;
2. static/global plaintext arrival;
3. `pa.origin == record.failed_origin` — the translating DATA origin is the home which
   originated the failed static flight;
4. `original_reporter` is a valid static node id;
5. `mobile_ctr != 0`;
6. target identity is complete: `target_kind == node_id` selects `failed_dst`; a
   `target_kind == key_hash` requires `HAS_DST_HASH` and selects `dst_hash32`;
7. if the translated DATA carries `DST_HASH`, it equals this mobile's stable hash; or,
   for the direct one-hop hosted form without `DST_HASH`, `pa.origin`, `reporter_layer` and
   the current attachment agree with the selected home; and
8. the record is not about an ACK or another custody report.

For a re-homed mobile, the outer origin may be the old H1 while the last-mile carrier arrives
through H2. The `DST_HASH == self` arm is what makes that valid. The record remains an
unauthenticated claim; no trust, key, route, membership or retry decision follows from it.

### 8.3 Persistence and Push mapping

Translated custody remains a persistent internal outcome and stays hidden from ordinary
inbox rows/unread budgets under Slice C.

Store it in the DM outcome sequence with:

```text
origin       original_reporter
msg_id       mobile_ctr
type         DATA_TYPE_CUSTODY_FAILURE
body         full validated translated record, record_len bytes
layer        reporter_layer
```

The live `PushKind::custody_failure` reuses the existing `Push` without growing it:

```text
origin       original_reporter
dst          failed_dst
ctr          mobile_ctr
layer_id     reporter_layer
seq          assigned inbox sequence, or 0 when storage disabled
body         translated record
body_len     record_len
```

No new PushKind is needed. The translated bit in the body distinguishes the two forms.

### 8.4 JSON and USB

The direct event remains byte-compatible. A translated event adds, without renaming the
existing direct fields:

```json
{
  "ev": "custody_failure",
  "reporter": 23,
  "failed_origin": 11,
  "dst": 48,
  "ctr": 912,
  "delegated": true,
  "via_home": 11,
  "target_kind": "hash",
  "home_ctr": 912,
  "mobile_ctr": 77
}
```

Here `ctr` retains the established custody meaning (`failed_ctr`/`ctrH`) for compatibility;
`mobile_ctr` is the mobile correlation token. Consumers match the complete tuple:

```text
{via_home, reporter_layer, target_kind,
 target=(dst_hash when hash, else dst), mobile_ctr, failed_type}
```

Counter-only matching is forbidden. `reporter` is the original relay from the translated
tail, while `via_home` is the translator/static-flight origin. USB uses the same names and
must retain the existing “NOT proof the destination missed it” warning.

## 9. User and protocol semantics

### 9.1 Monotonic result rules

- translated custody may move a matching live operation from waiting to **UNCERTAIN**;
- it never moves an operation to failed or delivered;
- it never triggers a retry;
- it must not consume the E2E-ACK correlation identity;
- a later exact E2E ACK may upgrade UNCERTAIN to DELIVERED;
- an ACK observed first wins; a later uncorrelated H1 report cannot downgrade the mobile;
- wrong home, layer, target, type or counter changes no operation; and
- a valid but unmatched translated report remains diagnostic only.

Slice H owns OLED send-state presentation. Remote-admin Slice 9 owns RPC pending-state
presentation. Both consume the same translated Push/record contract and neither may parse a
private home telemetry event.

### 9.2 Remote-admin integration rule

B278 never parses an RPC body. A custody report about `REMOTE_CMD` cannot satisfy
`REMOTE_RESP`, become an auth failure or prove non-execution.

To avoid successful mobile RPC requests occupying correlation rows until the long TTL,
remote-admin v2 should request the existing E2E ACK on its delegated request carrier and its
explicit pre-tail `REMOTE_CMD` handler should send that ACK only after the request has crossed
its defined admission boundary. That is a remote-admin design requirement, not code added by
B278. If remote admin chooses a different positive-release mechanism, it needs a separately
reviewed typed seam; B278 will not inspect request IDs or response bodies.

## 10. Capacity and time

### 10.1 Proposed TTL

```text
delegated_outcome_ttl_ms =
    e2e_ack_deadline_xl_ms + seen_origin_ttl_ms
  = 300000 + 450000
  = 750000 ms
```

The first term is the current delegated/cross-layer positive-receipt patience. The second is
the landed one-return-flight retention envelope, including the worst supported PHY exchange
margin. This is a bounded product correlation horizon, not a claim that arbitrary 31-hop
traffic completes within 750 seconds.

Expiry comparison is the existing exclusive-bound form (`age >= ttl` expires). No bare
750000 literal may appear outside the named protocol constant and its tests.

### 10.2 Capacity policy

Start from the existing capacity of eight, with these rules:

- prune expired first;
- exact retry refreshes its own row;
- never evict a live unconfirmed row;
- ACK-first clears promptly;
- successfully forwarded custody clears a custody-only row; and
- full capacity refuses the new qualifying mobile carrier before its hop ACK.

S0 must measure:

- maximum live rows in every mobile-bearing corpus stream at the proposed TTL;
- the existing B251 equal-counter/two-mobile cases;
- an eight-command successful remote-control model with prompt E2E ACK release;
- an eight-failure saturation followed by a ninth retryable refusal; and
- row and `Node` sizes on host, ARM and Xtensa.

If eight cannot cover the approved workload without ordinary false refusals, STOP for an
owner capacity/RAM ruling. Do not silently evict or shorten the bound to make a test green.

## 11. Failure matrix

| Condition | H1 local record | Mobile outcome | Correlation row |
| --- | --- | --- | --- |
| no map / expired map | yes | none | absent |
| ambiguous map | yes | none; loud invariant telemetry | unchanged until expiry |
| exact map, translated send queued/parked | yes | best-effort translated record | custody marked; retain only if ACK owed |
| exact map, translated send refused | yes | none; bounded telemetry | retain until expiry |
| custody first, then ACK | yes + mobile UNCERTAIN | ACK still translated; mobile may become DELIVERED | clear on ACK |
| ACK first, then custody | later report still stored at H1 | no late translated diagnostic | already clear |
| mobile moved to another home | yes | route by mobile hash through current home | normal lifecycle |
| translated record malformed/wrong recipient | n/a at receiver | reject; no store/push | n/a |
| H1 reboots before report | report may be stored at H1 | no translation | volatile map deliberately lost |
| team/cross-layer/last-mile failure | current behavior | none from B278 | no B278 row/lookup |

## 12. Implementation slices

### S0 — characterization and fixed decisions; zero production behavior change

1. Enumerate every reserve/activate/put/release/translate call and both mobile delegation
   shapes.
2. Reproduce ACK-first one-shot clearing and non-E2E absence on current code.
3. Measure the proposed TTL/cap occupancy and all three ABI layouts.
4. Predict corpus movers and prove which streams contain eligible mobile delegation plus a
   generated custody report.
5. Record baseline native, mutation, corpus, board and warning figures.

STOP if the eight-row/no-eviction design fails the approved workload or the 32-byte row is
not the measured layout.

### S1 — correlation authority

1. Generalize the one ring and preserve target + return identities.
2. Add outward type and obligation bits.
3. Strengthen activation uniqueness across all mobiles.
4. Reserve custody candidates before ACK on both wrapper and direct-transit paths.
5. Keep existing E2E ACK wire behavior and release ordering exact.

No custody wire or receiver change in this slice.

### S2 — codec extension

1. Allocate bit 6 and the 28-byte translated form.
2. Implement the one pack/parse authority and all direct/translated golden vectors.
3. Keep direct 24-byte output byte-identical.
4. Update `docs/frames.md` and `docs/protocol.md` drafts in the same slice; land them only
   after QG PASS.

### S3 — home translation

1. Preserve the existing direct store/push order.
2. Match complete identity after local handling.
3. Originate the translated `0x81` through `send_by_hash`.
4. Implement obligation transitions, no-map/ambiguous/refused telemetry and recursion proof.

### S4 — mobile receive and surfaces

1. Split direct vs translated contextual validation.
2. Persist translated records and emit the reused PushKind.
3. Add JSON, pulled-record and USB fields.
4. Re-run Slice C visibility and unread-budget gates.
5. Add the generic exact-tuple consumer fixture; no OLED/RPC state machine is implemented
   here unless separately included by an approved amendment.

### S5 — full gates, docs and metal

Run all gates below, propose any corpus re-anchor in-tree, and add bench Part 54. B278 becomes
software-complete only after QG; it closes fully after Part 54.

## 13. Required tests and falsifiers

### 13.1 Correlation

- same `{ctrM,target}` from two mobiles remains distinct;
- same home `{ctrH,failed_dst,layer}` cannot activate twice across different mobiles;
- target hash, target id, layer and type each fail independently when changed;
- a counter-only matcher mutation is RED;
- ring full produces BUSY_RX before hop ACK and never forwards without a row;
- queue/park refusal releases the reservation;
- exact retry refreshes only its row;
- ACK translation remains byte-identical and clears the row;
- custody-first does not break a later ACK translation; and
- ACK-first prevents a later mobile custody translation.

### 13.2 Eligibility and boundaries

Positive cases:

- plain same-layer delegated DM;
- plaintext outer `SEALED_RELAY`; and
- `REMOTE_CMD` as an internal custody-eligible type.

Negative cases, one falsifier each:

- team plane;
- cross-layer;
- crypted outer DATA;
- E2E ACK;
- custody report itself;
- hosted-mobile last-mile; and
- stale/spoofed hosted row.

B112 must retain a named characterization test: a wrapper may still be ACKed and later fail
outward admission for a reason unrelated to correlation capacity. B278 must not be reported
as closing it.

### 13.3 Codec and receiver

- direct 24-byte golden vector byte-identical;
- translated 28-byte golden vector;
- bit 6 with length 24–27 rejected;
- bit 7 rejected;
- translated target kind unknown, hash-without-`HAS_DST_HASH`, and node-id mismatch each
  rejected;
- reporter invalid and `mobile_ctr==0` rejected;
- direct unknown tail remains accepted and retained;
- translated future tail remains accepted and retained;
- translated form rejected by a static receiver;
- wrong outer home, wrong DST_HASH and wrong direct selected-home relation each reject;
- direct current-home delivery and moved-mobile-via-new-home delivery both pass;
- another unknown internal type still reaches Slice B's fail-closed guard; and
- translated `0x81` failure produces no custody report about itself.

### 13.4 Persistence and presentation

- H1 stores the original 24-byte report before translation;
- M1 stores the translated record with `msg_id=ctrM` and original reporter;
- Push `ctr=ctrM`, body retains `ctrH`, and `seq` equals the stored sequence;
- live and pulled JSON agree on every semantic field;
- direct JSON is byte-identical;
- translated JSON exposes both `home_ctr` and `mobile_ctr`;
- ordinary OLED inbox rows and unread counts exclude both forms;
- raw `pull_inbox` returns both forms; and
- a target/counter-only UI/RPC matcher cannot promote a wrong operation.

### 13.5 Instrumentation

- dedicated mutation targets with exact match counts and all controls RED;
- both feature/wiring probes build and run (B271 lesson);
- the DataType/custody matrix checker remains green;
- native PIN derived from the clean run;
- full corpus through the canonical parallel runner, prediction first;
- any semantic corpus movement is a STOP; attributed translated-notice additions may produce
  an in-tree re-anchor proposal but the coder never edits `simulation/BASELINE.md`;
- ruled board pair through the deterministic parallel measurement runner;
- ABI probe pins the row and `Node` on host/ARM/Xtensa;
- warning census, `git diff --check`, source/docs search controls; and
- no untracked instrument omitted from the final file inventory.

## 14. Documentation obligations

On implementation PASS, update together:

- `docs/frames.md`: bit 6, the 28-byte translated tail and validation table;
- `docs/protocol.md`: delegated-custody correlation, monotonic ACK interaction and explicit
  unauthenticated/no-auto-retry language;
- `docs/superpowers/specs/2026-08-23-internal-data-and-custody-outcome-design.md`: add the B278 extension without
  rewriting v1 history;
- `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: replace the B278 prerequisite with
  the landed carrier/tuple and retain B112 separately;
- `docs/2026-07-30-open-bug-register.md`;
- `docs/2026-07-31-bench-test-script.md` Part 54; and
- `tracker.md` / project memory at closure.

Active false comments in `node.h`, `node_hashlocate.cpp`, `node_mac_rx.cpp`, `frame_codec.h`,
`console_json.cpp` and `fw_main.cpp` must be corrected in the slice which establishes their
replacement truth. Historical statements remain visible with the correction idiom.

## 15. Metal Part 54 — required closure shape

Use at least four radios:

```text
M1 (mobile controller) -- H1 (its home) -- R (static relay) -- T (static target)
```

H1→T must route through R. A fifth node/current H2 is optional for the re-home arm.

Required observations:

1. establish M1 attachment to H1 and record M1's stable hash, local id and home layer;
2. establish a positive plaintext/global `-a` control from M1 to T and prove the returned ACK
   uses M1's counter;
3. send a fresh tagged message, let R ACK custody, then remove T before R transfers onward;
4. prove H1 receives and stores the direct report under `ctrH`;
5. prove M1 receives exactly one translated report naming original reporter R, H1 as
   `via_home`, the same target/type, and both `home_ctr=ctrH` and `mobile_ctr=ctrM`;
6. prove M1's ordinary OLED inbox/unread count does not gain a message row;
7. power-cycle M1 and prove the translated record survives in raw `pull_inbox`;
8. run the ACK-order control: custody first then a valid late ACK upgrades the live consumer
   without a second translation or downgrade;
9. run the no-map control after H1 reboot/expiry: H1 retains the direct diagnostic and M1
   receives nothing, never a wrongly correlated record; and
10. if a fifth node is available, re-home M1 after origination but before translation and
    prove the same hash-addressed outcome reaches M1 through H2.

The run is invalid unless the trace proves R accepted the failed carrier before its onward
terminal. A direct H1 origination failure is not custody and cannot pass this test.

## 16. Closure criteria

B278 is closed only when:

1. the reviewed rulings in §1.1 are accepted or replaced explicitly;
2. one correlation authority covers ACK and custody without counter-only matching;
3. direct custody behavior remains compatible;
4. translated records use the existing mobile routing/last-mile authority;
5. ACK-before/after monotonic behavior is proven;
6. no-map, ambiguity, full-ring, expiry and re-home cases fail safely;
7. protocol and frame documents match the implemented wire bytes;
8. native, mutation, corpus, board, ABI, warning and wiring gates pass; and
9. bench Part 54 passes on real radios.

Closing B278 does **not** close B112, broaden custody generation beyond v1's static/global
same-layer relay seam, or implement remote administration. It supplies the exact mobile
feedback carrier those later consumers require.
