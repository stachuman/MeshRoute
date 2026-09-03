# B278 — Mobile feedback for home-originated static custody failures

**Status:** DESIGN PASS · S0 + S1a + S1b + S2 + S3 + S4 + S5 ALL CLOSED 2026-09-03 · seventh corpus-table ruling landed · owner rulings R-S5-1 through R-S5-4 landed · SOFTWARE-COMPLETE / METAL-PENDING — bench Part 54
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

### 1.1 Owner rulings (2026-09-01)

The owner accepted the following design authority:

1. **Reuse `DATA_TYPE_CUSTODY_FAILURE` (`0x81`).** Allocate one translated-record flag and
   an eight-byte v1 tail. Do not allocate another DataType or a private mobile control frame.
2. **Reuse normal mobile delivery.** The home calls the existing hash-addressed typed-DATA
   path. Direct hosting, a redirect/breadcrumb, a cached new home and H-resolution remain
   the routing authorities; B278 adds no second mobile locator.
3. **A positive E2E ACK retires the correlation.** If the ACK reaches the home first, a later
   custody report remains a correct diagnostic at the home but is not forwarded to the
   mobile. Delivery is already positively confirmed, so losing a later, nonterminal
   diagnostic cannot downgrade the mobile's state.
4. **R1 = Option A: custody translation is available only to delegated flights carrying
   `DATA_FLAG_E2E_ACK_REQ`.** A plain non-E2E delegated send must remain byte-for-byte
   admission-compatible and must not consume one of the eight correlation rows. This
   preserves B251's explicit admission ruling and gives every custody-capable row a positive
   ACK release path.
5. **R2 REVISED after S0 (owner ruling 2026-09-02): one row, one 300 s lifetime.** Keep the
   custody constant named—`delegated_custody_ttl_ms = e2e_ack_deadline_xl_ms`—but prune the
   complete correlation row at that one boundary. The earlier 750 s proposal is withdrawn:
   its extra 450 s was B159's receiver-side DATA-dedup retention, not a measured custody-chain
   latency, while the originating mobile closes its own E2E operation at 300 s. A later
   custody notice is post-mortem evidence with no live operation to update. The standing S0
   census count of custody reports above 300 s is the trigger to revisit this ruling.
6. **Capacity remains eight (owner ruling 2026-09-02).** S0 measured a maximum of five rows
   even under the retired 750 s model and three under 300 s. Capacity is shared across every
   mobile hosted by one home, but a row exists only for an outstanding delegated `-a` send.
   Team creation/grants, registration, presence and key lookup consume no rows; a team-plane
   OLED `-t -a` DM bypasses home delegation. Failed, unacknowledged `-a` sends are the pressure
   case. S1b's correlation telemetry must make the occupancy observable on metal.
7. **R3: no false `deleg_fail` on a wrapper correlation collision (owner ruling
   2026-09-02).** The outward DM has already been admitted and may deliver, so
   `presence_mark_deleg_fail` would surface the false statement `send_failed{no_route}`.
   Preserve the measured consequence: release only the new correlation reservation, let the
   outward DM continue, leave any later ACK untranslated, and let the mobile's existing E2E
   deadline report the timeout. A faster truthful `correlation_lost` outcome, if ever wanted,
   requires a separate reviewed design.

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

1. it is allocated only when `DATA_FLAG_E2E_ACK_REQ` is set; B278 deliberately preserves
   that admission rule, so remote RPC and any other consumer which needs mobile custody
   feedback must request an E2E ACK;
2. its ACTIVE `peer` overwrites the originally requested target, while complete mobile-side
   correlation needs both the requested identity and the return discriminator;
3. it carries no outward `DataType`;
4. `deleg_ack_translate()` clears the row immediately, so a custody report arriving before
   an ACK would consume the ACK mapping if the same operation were reused; and
5. its single 300 s TTL is the delegated ACK deadline, not a per-obligation
   outward-failure-plus-return diagnostic horizon.

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
uint8   custody_state          none | candidate | eligible | forwarded
padding to the board ABI's verified size
```

The row retains the original target instead of overwriting it at activation. S0 measured the
layout as 32 bytes on host, ARM and Xtensa, making the existing eight-row ring 256 bytes
instead of 192 bytes: projected **+64 B**. S1a must still measure the real post-refactor
`Node`; S0's mirror establishes the row layout, not the containing object's final padding.
`custody_state` is lifecycle state under the one row TTL, not a second clock.

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

The home reserves before it sends the hop ACK to the mobile only when the prospective
outward carrier carries `DATA_FLAG_E2E_ACK_REQ`. That one admission condition creates the
ACK obligation and, when the pre-activation portion of §5's predicate holds, a **provisional
custody candidate**. Activation confirms that candidate as the custody obligation or clears
it according to the actual outward arm. A plain non-E2E delegated send never reserves a row
and must retain the pre-B278 admission behavior recorded by B251.

If no row is available, the home sends the existing BUSY_RX NACK and does not ACK the mobile
carrier. “Retryable” is qualified: the NACK encodes 16 ms quanta capped at 255, so the sender
observes at most 4.08 s even if the home's computed wait is longer. **S0 correction F4:** the
long-busy arm does not honor that wait and does not reach the cascade's 60 s lifetime check;
it preserves `enqueue_time_ms`/`requeue_count`, sets `next_attempt_ms = 0`, and can immediately
repeat the full exchange while the receiver refuses. This unbounded retry-loop defect is
registered separately as B280. The 300 s B278 ruling merely keeps its exposure at today's
correlation lifetime; it does not fix or absorb B280. This is correlation-resource admission
only. It does not claim that the future static route, parked-send admission or RPC target
admission has succeeded; B112 remains open for that wider first-hop truth.

### 4.3 Activation identity

Activation occurs only after the outward item is genuinely queued/parked under the existing
`SendDispatch` admission authority. It records:

```text
{ctrH, return_kind, return_peer, layer, outward_type}
```

Activation is also the final custody-eligibility authority. It promotes the provisional
candidate only for an actually same-layer static/global outward DATA flight. It clears the
candidate, while retaining the ACK obligation, on all four presently ACK-only arms:

1. the direct last-mile case where this home also hosts the target (`addr_len == 1`);
2. the cached-home cross-layer `send_cross_layer` arm;
3. the wrapper's own cross-layer branch in `node_mac_rx.cpp` (currently near `:1757`); and
4. the corresponding cross-layer park-fire site in `node_hashlocate.cpp` (currently near
   `:2259`). This fourth arm is source-proven unreachable today: a cross-layer parked send is
   created with `reply_to_hash == 0`. It remains a synthetic structural pin so a future park
   producer cannot silently acquire an impossible custody obligation.

This finalization is mandatory because those facts are learned only after the mobile hop
ACK/reservation boundary. An ACK-only row then follows the same 300 s row lifetime and never
waits for a custody report which cannot exist.

No two ACTIVE rows may have the same wire-visible return key
`{ctrH, return_kind, return_peer, layer}`, **even when their mobile hashes or outward types
differ**. A custody record does not carry the mobile hash, so allowing two such rows and
choosing the first would be a misdelivery. `outward_type` remains an additional lookup
cross-check, not permission to alias the return key. This strengthens B251's present
collision check. **S0 correction F3:** this is not current behavior—the existing check also
requires equal `mobile_hash`, so two different hosted-mobile hashes may presently activate
the same wire-visible return key. **Slice-boundary correction 2026-09-02:** S1a preserves
that same-mobile-scoped decision byte-for-byte; S1b owns the stronger cross-mobile refusal
and must flip the existing B251 test which currently admits both rows while proving only ACK
lookup isolation. Putting the flip in the behavior-neutral refactor would violate C1, and
the corpus cannot expose the mistake because home counters are per destination.

Admission failure releases the reservation through the same one-owner cleanup path. A
minted counter is not admission evidence.

**R3 consequence:** a wrapper-path cross-mobile collision is discovered only after its
outward DM was admitted. It releases the new reservation but must not call
`presence_mark_deleg_fail`: the DM may still deliver, so `send_failed{no_route}` would be
false. The returning ACK remains untranslated and the mobile reaches its existing E2E
timeout. Direct transit remains different and loud: it refuses before the hop ACK with
BUSY_RX reason 2.

### 4.4 Custody lookup identity

The direct report at H1 matches exactly one ACTIVE row on all of:

```text
ctrH             == record.failed_ctr
return_kind      == node_id
return_peer      == record.failed_dst
layer            == record.reporter_layer
outward_type     == record.failed_type
custody obligation remains set
```

This wire-derived return key is the lookup authority. `DST_HASH` is an optional
cross-check, not a mandatory lookup component:

- if the failed record has `HAS_DST_HASH`, its `dst_hash32` must agree with a hash-addressed
  row's retained `target`;
- if `HAS_DST_HASH` is absent, lookup does not fail merely because the hash is unavailable;
  and
- the translated tail always carries the row's retained `target_kind` and full
  `target_value`, so mobile-side identity remains complete in either case.

The return-key authority remains correct when `DST_HASH` is absent, but the current
production reachability is narrower than the codec permits. A same-layer wrapper body is at
most 232 bytes after its DST_HASH/origin/SOURCE_HASH overhead, so its re-originated DATA
still fits `DST_HASH`; the cached-home arm also forces one. The production absent-hash case
is the direct-transit path, whose `target_kind == node_id` and `failed_dst` already complete
the identity. A hash-addressed record without `HAS_DST_HASH` remains a valid synthetic codec
vector and a future-compatible reason for retaining `target_value`; it is not claimed as a
current production path.

Zero matches means an uncorrelated H1 diagnostic. More than one match is an invariant
failure and also means an uncorrelated H1 diagnostic. Neither case forwards anything or
guesses by counter.

### 4.5 Lifecycle

- **ACK arrives first:** translate `ctrH -> ctrM`, queue the existing last-mile E2E ACK,
  clear the ACK obligation and clear the custody obligation as positively superseded. The
  row then frees. A later custody report is stored at H1 only and cannot downgrade DELIVERED.
- **Custody report arrives first:** store/push it at H1, originate the translated report,
  set `custody_forwarded` only when that send is queued or parked, and retain the row while
  the E2E ACK obligation remains. The later ACK still translates and then clears the row.
- **No custody-only row exists.** R1=A requires the E2E-ACK flag for custody correlation;
  a non-E2E flight does not reserve. A refused translated send leaves the custody obligation
  until the row's 300 s expiry, allowing a genuinely fresh repeat report to retry while the
  originating mobile can still be waiting.
- **Duplicate report after `custody_forwarded`:** no second translated send. Existing raw
  diagnostic-storage rules may still retain a genuinely new report at H1.
- **Expiry:** at age 300 s clear the complete row—ACK mapping, custody eligibility and
  forwarded state together. Expiry gains bounded scalar telemetry under S1b so the metal
  occupancy census is exact. A subsequent report remains a valid H1 diagnostic and is never
  attached to another mobile.

Custody processing therefore never consumes the ACK obligation. That is the key ordering
property B278 adds.

## 5. Which E2E-ACK mobile flights gain custody correlation

Custody eligibility is one named, two-phase predicate, not two hand-written type lists. The
reservation phase uses only facts knowable before the home constructs the outward
`PendingTx`; it can set only a provisional candidate. The activation phase combines that
candidate with the actual dispatch arm and either confirms or clears the custody obligation.

It returns true only when the prospective outward carrier is:

1. a verified direct hosted-mobile transit or a valid `DATA_TYPE_MOBILE_SEND` wrapper from
   a live hosted row;
2. explicitly requesting an E2E ACK through `DATA_FLAG_E2E_ACK_REQ`;
3. plaintext at the outward DATA-frame level;
4. static/global and same-layer;
5. prospectively a normal DATA carrier, not channel M/FLOOD;
6. not already known to be cross-layer, team, gateway re-inject or hosted-mobile last-mile;
7. not `DATA_TYPE_E2E_ACK` or `DATA_TYPE_CUSTODY_FAILURE`; and
8. addressed to a nonzero target with a complete target identity.

`DATA_TYPE_SEALED_RELAY` remains eligible because its **outer DATA frame** is plaintext; the
application body being sealed does not hide the routing identity the custody record needs.
Internal types, including `REMOTE_CMD` and `REMOTE_RESP`, are otherwise eligible exactly as
custody §10.1 rules, but only when their mobile carrier requests the E2E ACK required by
R1=A. This is why remote-admin v2 must set that flag; B278 does not change admission for a
plain non-E2E message.

The direct-mobile-transit and wrapper paths must both be tested. Adding the predicate only
to `send_by_hash` would miss the counter-translating forward path in `handle_data`; adding it
only to `handle_data` would miss the wrapper/park path. Activation must then clear the
provisional candidate for every actual last-mile or cross-layer arm listed in §4.3; a
reservation-time predicate alone is knowingly incomplete.

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

### 6.2 Eight-byte translated tail

When bit 6 is set, `record_len` is at least 32 and offsets 24–31 are:

```text
offset  size  field
24      1     original_reporter   outer origin of the direct report received by H1
25      1     target_kind         0 = node_id, 1 = key_hash
26      2     mobile_ctr          ctrM, little-endian and nonzero
28      4     target_value        original mobile-visible node id or key_hash32
```

The fixed prefix still carries:

- translating home: `failed_origin`;
- original static counter: `failed_ctr`;
- resolved static target: `failed_dst`, and optional carrier `dst_hash32` when present;
- outward type: `failed_type`;
- home/static layer: `reporter_layer`.

`target_value` is deliberately not inferred from the fixed prefix. For node-id addressing it
must equal `failed_dst`. For hash addressing it is the row's retained original target hash;
when the direct report carries `HAS_DST_HASH`, both hashes must agree, while absence of that
optional field remains valid. This keeps large-body delegation representable and makes the
mobile-visible identity complete.

No mobile hash is repeated in the body. The outer `DST_HASH`/direct hosted-row addressing is
the recipient authority, and repeating it would create two values to cross-check.

### 6.3 Codec authority

There remains one shared custody codec.

- `pack_custody_failure` continues to produce only the direct 24-byte form.
- Add one explicit translated pack operation using the same prefix writer and the eight-byte
  tail; no caller writes offsets.
- `parse_custody_failure` keeps its existing source-compatible return type and exposes the
  translated flag through `CustodyFailureRecord::notice_flags`. Add a separate
  `parse_custody_translated_tail(body, record)` operation returning the parsed eight-byte
  tail; do not force the four existing parse callers—including `src/fw_main.cpp`—through a
  return-type migration merely to expose the extension.
- bit 6 clear: the existing `record_len >= 24` future-tail rule remains unchanged.
- `custody_record_tail` must start after the prefix this record actually owns: 24 for a
  direct record, 32 for a translated record. Keep the existing caller-facing signature or
  add a source-compatible overload/helper; never slice a translated future tail at 24.
- bit 6 set: require `record_len >= 32`, valid `original_reporter`, a defined
  `target_kind`, a nonzero and kind-valid `target_value`, and nonzero `mobile_ctr`; require a
  node-id value in 1..254 which equals `failed_dst`, and require a hash value to equal
  `dst_hash32` only when `HAS_DST_HASH` is set; preserve any bytes beyond 32 as a future
  tail.
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
   translation action containing the 32-byte record and the exact row identity;
6. call `send_by_hash` for that bounded action;
7. update the row only if the send queued or parked and its complete identity still matches
   the materialized action; and
8. let the existing caller release the received custody carrier exactly once through
   `become_free()`.

Translation never suppresses or rewrites H1's original diagnostic. Storage failure at H1
does not fabricate persistence, but it also does not suppress the live best-effort mobile
outcome.

There must be one owner of the receive-carrier release. Do not call `send_by_hash` from
inside the current `custody_failure_receive()` **and** add another release there: its caller
already owns the single `become_free()`. The send-before-release order deliberately follows
the landed MOBILE_SEND re-origination precedent; `become_free()` is also the queue-drain
pump, so releasing first would change when the translated item can begin. The bounded action
may not copy `PostAck`, `PendingTx`, a 241-byte payload buffer or the complete correlation
ring; its record is exactly 32 bytes and its remaining fields are scalars. The slice must
measure the resulting RX-stack movement.

The translated send is:

```text
destination       row.mobile_hash, through send_by_hash
type              DATA_TYPE_CUSTODY_FAILURE
body              32-byte translated custody record
plane             GLOBAL
crypt             off
E2E_ACK_REQ        clear
generic lifecycle none (the existing DataType trait)
app inbox text    never
```

`send_by_hash` owns direct hosted delivery, redirects, cached remote homes and H-resolution.
Its cached-home arm currently uses `Plane::AUTO`. For a static home,
`flight_is_team_plane(AUTO, dst)` resolves structurally to GLOBAL because the team-plane arm
requires a mobile with a nonzero team id. B278 therefore changes no plane behavior: a pinned
test must prove AUTO and GLOBAL equivalent for this static-home arm. Making the cached arm
honor the supplied plane could change existing behavior for a dual team member and is
explicitly outside B278; any suspected console mis-plane is a separate register decision.
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
6. target identity is complete: `target_kind == node_id` requires
   `target_value == failed_dst`; `target_kind == key_hash` selects the tail's nonzero
   `target_value`, and, only when `HAS_DST_HASH` is set, requires
   `target_value == dst_hash32`;
7. if the translated DATA carries `DST_HASH`, it equals this mobile's stable hash; or,
   for the normal direct one-hop hosted form without `DST_HASH`, the node is mobile, its
   registration is active, `_my_mobile_reg.home_id == pa.origin`, and the report layer
   agrees with the active attachment; and
8. the record is not about an ACK or another custody report.

The direct-host form normally has no `DST_HASH`: `send_by_hash` passes
`override_dst_hash=0`, and hosted local ids are deliberately absent from `_id_bind`, so
`key_hash_of_id()` cannot reconstruct one. For a re-homed mobile, the outer origin may be
the old H1 while the last-mile carrier arrives through H2. The `DST_HASH == self` arm is what
makes that valid. The record remains an unauthenticated claim; no trust, key, route,
membership or retry decision follows from it.

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
  "target_kind": "hash",
  "target_hash": "a1b2c3d4",
  "mobile_ctr": 77
}
```

Here `ctr` retains the established custody meaning (`failed_ctr`/`ctrH`) for compatibility;
`mobile_ctr` is the mobile correlation token. Consumers match the complete tuple:

```text
{failed_origin, reporter_layer, target_kind, target_value, mobile_ctr, failed_type}
```

Counter-only matching is forbidden. `reporter` is the original relay from the translated
tail, while the existing `failed_origin` field is the translator/static-flight origin and
the existing `ctr` field is the home counter. Emit exactly one target-value field:
`target_id` for node-id addressing or `target_hash` for hash addressing. Do not add aliases
such as `via_home` or `home_ctr`; duplicate values create a second compatibility surface.
USB uses the same semantic names and must retain the existing “NOT proof the destination
missed it” warning.

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

To qualify for B278 feedback and to release successful rows promptly, remote-admin v2 must
request the existing E2E ACK on its delegated request carrier, and its
explicit pre-tail `REMOTE_CMD` handler should send that ACK only after the request has crossed
its defined admission boundary. That is a remote-admin design requirement, not code added by
B278. A different positive-release mechanism would require a separately reviewed typed seam
and a new owner ruling; B278 will not inspect request IDs or response bodies.

## 10. Capacity and time

### 10.1 Single correlation lifetime

```text
delegated_custody_ttl_ms =
    e2e_ack_deadline_xl_ms
  = 300000 ms
```

This is the one TTL for the complete row. The name stays custody-specific so a future
evidence-based retune is one named change, but there is no custody clock separate from the
ACK clock.

The retired 750 s proposal added `seen_origin_ttl_ms` (450 s), which B159 derived for a
different question: how long a receiver must remember DATA to reject a duplicate. That
constant combines the 150 s gateway doorstep window with one worst-legal-PHY exchange
margin. It was never a custody-chain latency derivation; at the slow-PHY/deep-chain corner
neither 300 s nor 750 s bounds the whole chain. The product authority is instead the mobile's
own operation: it reports `e2e_ack_timeout` and closes at 300 s, after which translated
custody is only a post-mortem diagnostic with no live state to update.

Expiry comparison keeps the existing exclusive-bound form (`age >= ttl` expires) and clears
the whole row. No bare `300000` literal may appear outside the named protocol constant and
tests. The S0 census's count of custody reports arriving above 300 s is a standing trigger:
a nonzero real count requests review of this ruling but never silently changes the constant.

### 10.2 Capacity policy

Start from the existing capacity of eight, with these rules:

- prune expired first;
- exact retry refreshes its own row;
- never evict a live unconfirmed row;
- ACK-first clears promptly;
- no custody-only row exists under R1=A; and
- full capacity refuses only a new qualifying E2E-ACK-requesting mobile carrier before its
  hop ACK. Plain non-E2E sends never consult the ring.

S0 discharged the capacity decision: maximum modeled occupancy was three rows at 300 s and
five under the retired 750 s counterfactual, with zero corpus refusals. Four of the five peak
rows belonged to one mobile whose targets never ACKed, confirming that failed `-a` sends—not
team setup—are the pressure shape. Capacity is aggregated across every mobile hosted by the
home. Eight remains the ruled cap; live rows are never evicted.

Team grantkey uses no E2E flag, registration/presence/key lookup are not E2E DMs, and a
team-plane OLED `-t -a` DM goes directly on the team plane rather than through home
delegation. S1b adds the scalar correlation lifecycle telemetry required to measure this same
occupancy on metal. B280 separately owns the BUSY_RX retry-cost defect exposed by saturation.

## 11. Failure matrix

| Condition | H1 local record | Mobile outcome | Correlation row |
| --- | --- | --- | --- |
| no map / expired map | yes | none | absent |
| ambiguous map | yes | none; loud invariant telemetry | unchanged until expiry |
| exact map, translated send queued/parked | yes | best-effort translated record | retain until ACK or the 300 s edge |
| exact map, translated send refused | yes | none; bounded telemetry | retain until ACK or the 300 s edge |
| custody first, then ACK | yes + mobile UNCERTAIN | ACK still translated; mobile may become DELIVERED | clear on ACK |
| ACK first, then custody | later report still stored at H1 | no late translated diagnostic | already clear |
| plain non-E2E delegated send | current behavior | no B278 feedback | no row; never a new BUSY_RX |
| direct-transit carrier has no `DST_HASH` | yes | complete node-id target from retained row | match by return key; hash check skipped |
| synthetic hash-addressed record has no `DST_HASH` | codec-valid only; not a current producer shape | complete hash target from translated tail | parser/consumer compatibility vector |
| mobile moved to another home | yes | route by mobile hash through current home | normal lifecycle |
| translated record malformed/wrong recipient | n/a at receiver | reject; no store/push | n/a |
| H1 reboots before report | report may be stored at H1 | no translation | volatile map deliberately lost |
| team/cross-layer/last-mile failure | current behavior | none from B278 | no B278 row/lookup |

## 12. Implementation slices

### S0 — CLOSED 2026-09-02 · characterization and fixed decisions

1. Enumerate every reserve/activate/put/release/translate call and both mobile delegation
   shapes.
2. Reproduce ACK-first one-shot clearing and non-E2E absence on current code.
3. Measure occupancy at the ruled 300 s / 750 s obligation bounds and all three ABI layouts.
4. Measure BUSY_RX retry amplification per refused `-a` send and count corpus custody
   reports whose origination-to-report age exceeds 300 s.
5. Predict corpus movers and prove which streams contain eligible mobile delegation plus a
   generated custody report.
6. Record baseline native, mutation, corpus, board and warning figures.

STOP if the eight-row/no-eviction design fails the approved workload or the 32-byte row is
not the measured layout.

**Completion:** PASS, neither STOP triggered. Evidence:
`docs/superpowers/evidence/2026-09-02-b278-s0.md`. The candidate row measured 32 bytes on
host/ARM/Xtensa; capacity eight passed; the corpus contained no delegated-custody
intersection and no custody receipt above 300 s. The owner consequently replaced the
then-measured 750 s counterfactual with §10.1's single 300 s row lifetime. S0 also found B280
(BUSY_RX retry bound) and B281 (board-runner output-path contradiction), and assigned the F3,
F4, F6 and corpus-reachability corrections recorded in this spec.

### S1a — CLOSED 2026-09-02 · behavior-neutral correlation refactor

1. Generalize the one ring and preserve target + return identities.
2. Add outward type and custody lifecycle state without changing which flights reserve;
   retain one timestamp and the one named 300 s row TTL—no per-obligation clocks.
3. Preserve the current same-mobile-scoped activation-uniqueness decision byte-for-byte;
   the stronger cross-mobile refusal and its B251 test flip belong to S1b.
4. Route every current reserve/activate/release/translate site through the renamed authority.
5. Prove all 36 corpus streams byte-identical, current E2E ACK wire behavior exact, and
   measure the row/`Node` RAM movement independently of the admission change. Byte identity
   includes keeping every existing telemetry event name and field verbatim:
   `deleg_ack_reserved`, `deleg_ack_put`, `deleg_ack_put_refused`, and
   `mobile_ctr_admission_refused`.

No custody admission, wire or receiver behavior changes in S1a.

**Completion:** PASS; evidence:
`docs/superpowers/evidence/2026-09-02-b278-s1a.md`. The one row is 32 bytes
on host/ARM/Xtensa; `Node` and both ruled board RAM figures moved exactly +64 bytes, wholly
attributed to the eight-row ring. Native, both B251 batteries, the ABI probes, warning census
and all 36 corpus anchors passed; every corpus stream remained byte-identical. S1a added no
wire/NV/timer/telemetry behavior and has no metal residue—the D2 board diff is the RAM
authority. The same-mobile-scoped uniqueness behavior remains deliberately live for S1b to
change.

### S1b — R1/R2 admission and obligation behavior

1. Add a provisional custody candidate only to rows already reserved by
   `DATA_FLAG_E2E_ACK_REQ` and passing §5's reservation phase; confirm or clear it at every
   activation arm.
2. Preserve zero row allocation for every non-E2E delegated send.
3. Strengthen activation uniqueness across all mobiles: refuse a second ACTIVE row sharing
   `{ctr_h, return_kind, return_peer, layer}` even when `mobile_hash` differs, and flip the
   existing B251 case which currently admits both rows. Measure this admission change in
   S1b; do not describe it as refactor fallout.
4. Pin all activation paths, not only the resolved-id arm: resolved-id dispatch, direct-host
   counter translation, cached-home dispatch, and all three parked-send fire sites currently
   near `node_hashlocate.cpp:2259`, `:2277` and `:2331`. Explicitly drive and clear the
   provisional custody candidate on all four ACK-only arms: direct last-mile (`addr_len=1`),
   cached-home `send_cross_layer`, the wrapper XL branch near `node_mac_rx.cpp:1757`, and the
   XL park-fire site near `node_hashlocate.cpp:2259`. The last is synthetic: its current
   producer structurally leaves `reply_to_hash == 0`.
5. Apply `delegated_custody_ttl_ms == e2e_ack_deadline_xl_ms` to the complete row at the
   exclusive 300 s edge; do not add separate ACK/custody timers.
6. Land F6's scalar measurement surface: make release and expiry observable, include
   `target_kind` on reservation, and include `mobile_hash` plus `ctr_h` on reverse-ACK
   translation. Preserve all existing event names; attribute every added field/event and
   corpus movement to S1b.

No custody wire or receiver change in S1b.

**Completion:** PASS; evidence:
`docs/superpowers/evidence/2026-09-02-b278-s1b.md`. The two-phase authority now moves rows
`none -> candidate -> eligible`, all four ACK-only activation arms clear the candidate, and
cross-mobile return-key collisions refuse without overwriting or evicting the incumbent.
The owner-ruled wrapper consequence is proven end to end: only the new reservation is
released, no false `send_failed{no_route}` is emitted, and the untranslated ACK leaves the
mobile to its existing 300 s timeout. Release/expiry telemetry has one semantic authority
each. Native, both complete B251 mutation batteries, ABI probes, the ruled board pair and
the warning census passed; `DelegAck`, `Node`, and board RAM moved by zero. The ordered
corpus comparator attributed every delta to the new F6 telemetry: only `s07` and `s22`
moved, while delivery, duplicate, failure, routing and airtime ledgers remained identical.
The owner-approved seventh table ruling is landed in `simulation/BASELINE.md`; the keystone
remains `32afbf11 / 269517 / 0`. S1b owes no bench part. It also surfaced two separate
findings: B282 (wrapper-XL reservation leak) and B283 (the warning census's
`-Wunused-parameter` blind spot).

### S2 — codec extension

1. Allocate bit 6 and the 32-byte translated form.
2. Implement the one pack/parse authority and all direct/translated golden vectors.
3. Keep direct 24-byte output byte-identical.
4. Update `docs/frames.md` and `docs/protocol.md` drafts in the same slice; land them only
   after QG PASS.

**Completion:** PASS; evidence:
`docs/superpowers/evidence/2026-09-02-b278-s2.md`. Bit 6 and the derived 32-byte form now
live behind one additive codec: the four existing parser callers remain source-compatible,
the direct 24-byte vector is byte-identical, translated offsets are pinned by two literal
golden arrays, and future-tail access starts after the record's own 24- or 32-byte prefix.
The named interim guard rejects a translated record immediately after shared parsing and
before every G contextual term, store, Push or presentation surface; S4 owns its replacement.
All four touched mutation targets passed in full (62 RED, zero unusable), all 36 corpus
anchors reproduce the seventh ruled table after a proven relink, `Node` and ruled-board RAM
are unchanged, and both wiring probes/checkers passed. The S2 wire text is landed in
`docs/frames.md` and `docs/protocol.md`. S2 has no producer, consumer, re-anchor, owner
ruling or bench residue; S3 is the first producer and Part 54 remains the end-to-end metal
gate.

### S3 — home translation

1. Preserve the existing direct store/push order.
2. Match complete identity after local handling.
3. Originate the translated `0x81` through `send_by_hash`.
4. Implement obligation transitions, no-map/ambiguous/refused telemetry and recursion proof.
5. Leave the cached-home `Plane::AUTO` behavior unchanged and pin that a static home's AUTO
   resolution is equivalent to GLOBAL for this arm.

**Completion:** PASS; evidence:
`docs/superpowers/evidence/2026-09-02-b278-s3.md`. The home now correlates a direct report
only after its existing store and Push, prunes before scanning, requires exactly one ACTIVE
eligible row on the complete return identity, originates one bounded 32-byte translation
through `send_by_hash`, and marks that exact row `forwarded` only after a queued or parked
dispatch and a complete identity recheck. ACK-first, custody-first, refusal, duplicate,
expiry, ambiguous, stale and recursion cases passed. Native finished at 2559/108102/0; both
touched mutation targets were fully RED (51 + 29, zero unusable); all 36 corpus anchors
reproduce with every S3 event at zero; `Node` and ruled-board RAM are unchanged; and the
measured receive-stack increase is static and fully attributed. S3 owes no re-anchor, owner
ruling or bench part. The protocol producer/lifecycle text is landed; S4 remains the first
translated receiver and presentation slice.

### S4 — mobile receive and surfaces

1. Split direct vs translated contextual validation.
2. Persist translated records and emit the reused PushKind.
3. Add JSON, pulled-record and USB fields.
4. Re-run Slice C visibility and unread-budget gates.
5. Add the generic exact-tuple consumer fixture; no OLED/RPC state machine is implemented
   here unless separately included by an approved amendment.

**Completion:** PASS; evidence:
`docs/superpowers/evidence/2026-09-02-b278-s4.md`. A configured mobile now consumes the
translated form only when the outer home and stable-hash or active-registration context
agree with the codec-validated record. It stores the full record before the existing live
Push, preserves the direct form byte-for-byte, and exposes one translated field authority
across live JSON, pulled JSON and the executable USB renderer. Slice-C OLED/unread exclusion
and raw pull remain intact, and the generic fixture proves the complete six-field body tuple
rather than counter-only or store-key matching. Native finished at 2578/108904/0; all five
mutation/dependency targets were RED; the new USB gate passed 27 checks and 10 controls;
all 36 corpus anchors reproduce with eleven direct receipts unchanged; `Node` and ruled-board
RAM are unchanged; and the static stack delta is fully attributed. S4 owes no re-anchor,
owner ruling or bench part. Part 54 remains S5's end-to-end metal gate.

### S5 — full gates, docs and metal

Run all gates below, propose any corpus re-anchor in-tree, and add bench Part 54. B278 becomes
software-complete only after QG; it closes fully after Part 54.

**Software completion:** PASS; evidence:
`docs/superpowers/evidence/2026-09-03-b278-s5.md`. The consolidated final-tree gate reproduced
native 2578/108904/0 with the PIN unchanged; ran the union of the mechanical changed-file
selector and the custody-arc target list (22 mutation targets, all RED, zero unusable); held
36/36 corpus anchors exactly with `s18` `32afbf11`/269517/0 and no translated-custody corpus
reach; reproduced both ABI probes, all three wiring probes, both checkers, the six-environment
warning census and 174 tool tests; and produced byte-identical gateway and heltec_mobile ELF
and payload images with zero RAM movement. The only implementation-side edit was a comment
correction in `test/test_custody_relay_f.cpp`, proven token-neutral apart from doctest
`__LINE__` values. No production, wire, baseline or corpus-table change landed. Owner rulings
R-S5-1 through R-S5-4 govern the conditional late-ACK observation, reboot no-map arm,
straight-to-metal proof and independent B283 instrument work. **This is software completion,
not B278 closure: bench Part 54 remains the sole undischarged §16 criterion.**

## 13. Required tests and falsifiers

### 13.1 Correlation

- same `{ctrM,target}` from two mobiles remains distinct;
- same home `{ctrH,failed_dst,layer}` cannot activate twice across different mobiles;
- target hash (when present), target id, layer and type each fail independently when changed;
- a counter-only matcher mutation is RED;
- ring full produces BUSY_RX before hop ACK and never forwards without a row;
- nine plain non-E2E delegated sends allocate zero rows and never gain a B278 BUSY_RX;
- queue/park refusal releases the reservation;
- exact retry refreshes only its row;
- ACK translation remains byte-identical and clears the row;
- custody-first does not break a later ACK translation;
- ACK-first prevents a later mobile custody translation;
- at `delegated_custody_ttl_ms-1` the complete row remains available for ACK and custody,
  while at the exact 300 s edge the complete row is gone and neither result translates; and
- every resolved-id, direct-host, XL, cached-home and parked-fire activation site is driven,
  with four explicit ACK-only controls proving provisional custody is cleared: direct
  last-mile, cached-home cross-layer, wrapper cross-layer, and a **synthetic-only**
  cross-layer park-fire control (unreachable from today's producer).

### 13.2 Eligibility and boundaries

Positive cases:

- E2E-ACK-requesting same-layer delegated DM;
- E2E-ACK-requesting plaintext outer `SEALED_RELAY`; and
- E2E-ACK-requesting `REMOTE_CMD` as an internal custody-eligible type.

Negative cases, one falsifier each:

- team plane;
- cross-layer;
- crypted outer DATA;
- otherwise-eligible carrier without `DATA_FLAG_E2E_ACK_REQ`;
- E2E ACK;
- custody report itself;
- hosted-mobile last-mile; and
- stale/spoofed hosted row.

B112 must retain a named characterization test: a wrapper may still be ACKed and later fail
outward admission for a reason unrelated to correlation capacity. B278 must not be reported
as closing it.

### 13.3 Codec and receiver

- direct 24-byte golden vector byte-identical;
- translated 32-byte golden vector;
- bit 6 with length 24–31 rejected;
- bit 7 rejected;
- translated target kind unknown, zero/invalid target, node-id mismatch, and
  hash-with-`HAS_DST_HASH` mismatch each rejected;
- hash target without `HAS_DST_HASH` accepted from the translated tail as a synthetic codec
  vector, including a 237-byte-body construction explicitly labelled non-production;
- the production same-layer wrapper maximum (232 bytes) retains `HAS_DST_HASH`, while a
  direct-transit absent-hash case passes with `target_kind == node_id` and
  `target_value == failed_dst`;
- reporter invalid and `mobile_ctr==0` rejected;
- direct unknown tail remains accepted and retained;
- translated future tail remains accepted and retained;
- translated form rejected by a static receiver;
- wrong outer home, wrong DST_HASH and wrong direct selected-home relation each reject;
- direct current-home delivery and moved-mobile-via-new-home delivery both pass;
- the direct current-home form normally carries no `DST_HASH` and requires
  `_my_mobile_reg.home_id == pa.origin`;
- another unknown internal type still reaches Slice B's fail-closed guard; and
- translated `0x81` failure produces no custody report about itself.

### 13.4 Persistence and presentation

- H1 stores the original 24-byte report before translation;
- M1 stores the translated record with `msg_id=ctrM` and original reporter;
- Push `ctr=ctrM`, body retains `ctrH`, and `seq` equals the stored sequence;
- live and pulled JSON agree on every semantic field;
- direct JSON is byte-identical;
- translated JSON exposes `mobile_ctr`, the existing home `ctr`, and exactly one of
  `target_id` / `target_hash`, with no `via_home` or `home_ctr` aliases;
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

- `docs/frames.md`: bit 6, the 32-byte translated tail and validation table;
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
The current `node_hashlocate.cpp:1869-1871` admission comment must be reviewed explicitly:
its rule that a delegated non-E2E send consumes no correlation row remains authoritative
under R1=A, while any `DelegAck`-only naming made false by the one-ring refactor must be
corrected without weakening that policy.

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
3. send a fresh tagged `-a` message, let R ACK custody, then remove T before R transfers
   onward;
4. prove H1 receives and stores the direct report under `ctrH`;
5. prove M1 receives exactly one translated report naming original reporter R, H1 in the
   existing `failed_origin`, the same target/type, the existing `ctr=ctrH`, and
   `mobile_ctr=ctrM`, with exactly one `target_id` or `target_hash` field and no aliases;
6. prove M1's ordinary OLED inbox/unread count does not gain a message row;
7. power-cycle M1 and prove the translated record survives in raw `pull_inbox`;
8. ⚠ **RULED R-S5-1 2026-09-03 — CONDITIONAL ON METAL.** This item originally required:
   *"run the ACK-order control: custody first then a valid late ACK upgrades the live consumer
   without a second translation or downgrade"*. That ordering is structurally unreachable on
   the four-node line after R's terminal destroys its last copy. S1b/S3 remain the normative
   proof. Record the upgrade/no-second-translation/no-downgrade result if a genuinely
   independent copy produces it; absence is not a metal failure;
9. ⚠ **RULED R-S5-2 2026-09-03 — H1 REBOOT ONLY.** This item originally allowed *"H1
   reboot/expiry"*. Reboot H1 after R's hop ACK and before R's terminal report; H1 retains the
   direct diagnostic and M1 receives nothing, never a wrongly correlated record. The 300 s
   expiry variant is dropped because R's ≤60 s terminal necessarily precedes it; and
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
