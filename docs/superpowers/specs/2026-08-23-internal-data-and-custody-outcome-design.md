<!-- Author: OpenAI Codex -->
# Internal DATA namespace and custody-outcome design

*2026-08-23. Status: FIRST REVIEW DRAFT. This specification supersedes the narrower
`2026-08-05-b59-custody-failure-notice-design.md`. It defines the DATA-type namespace,
the common semantics of protocol-internal DATA, durable diagnostic-outcome records, and
the bounded static/same-layer B59 custody-failure notice. It does not approve automatic
payload retransmission.*

## 0. Owner rulings and boundary

The following decisions are settled for this design.

1. MeshRoute is not deployed. It runs only on the owner's test hardware and all nodes may
   be reflashed together.
2. The DATA-type renumbering is intentionally wire-incompatible, but
   `protocol::wire_version` remains unchanged. Mixed pre/post-renumber builds are
   unsupported during this development transition. There are no legacy numeric aliases
   and no dual decoder.
3. A contiguous range is reserved for protocol-internal DATA. Every current internal
   DATA type moves into it. Application-bearing envelopes remain outside it even when a
   protocol wrapper carries them.
4. Only selected internal reports whose receipt is useful diagnostic evidence are stored.
   Internal classification alone never implies persistence.
5. The selected durable internal records initially are E2E-ACK receipts and custody-failure
   reports.
6. These records use the ordinary inbox retention policy. They have no protected quota,
   partition, priority, or immunity from drop-oldest eviction, append failure, individual
   deletion, or a future whole-inbox clear.
7. Internal records are excluded from the default message view and its unread count, but
   remain available through the raw/diagnostic inbox stream.
8. The persistent inbox-store version must change because old E2E-ACK records contain the
   old numeric DATA type. The upgrade wipes inbox records, preserves the monotonic next
   sequence, resets the read cursor, and increments the storage epoch. This is an inbox
   storage migration, not a wire-version or device-configuration-NV change.
9. A custody-failure report is advisory, cleartext and unauthenticated. It cannot trigger
   automatic retransmission, route penalties, trust changes, security decisions, or a
   terminal user-send failure.
10. B59 v1 covers only a terminal static, same-layer, plaintext, standard-unicast transit
    `PendingTx` in the existing cascade path. Other post-custody discard sites are named
    explicitly and deferred.
11. The previous-hop loop guard remains unchanged.
12. Automatic reconstruction or retransmission of the discarded payload is outside this
    design, including the authoritative pubkey answer from the original B59 incident.
13. The DATA/custody arc touches core code which predates the project's current quality
    discipline. Before renumbering or changing behavior, a bounded characterization and
    audit slice must map the live DATA paths and report every discovered defect or false
    claim. Findings are not silently absorbed into a larger implementation slice.

## 1. Current state and problem

### 1.1 The metal-confirmed B59 failure

The recorded topology was `42 — 186 — 109 — 48`. Requester 48 asked for the pubkey bound
to `0x8CC9BDFF`. Owner 42 generated authoritative pubkey-answer DATA counter 3598 and sent
it to relay 186. Relay 186:

1. received and cached the answer;
2. sent the upstream hop ACK, accepting custody from node 42;
3. exhausted RTS attempts toward node 48;
4. initiated route-repair logic;
5. entered the one-way slow-reprobe path; and
6. terminally discarded the transit flight.

The repaired route arrived immediately afterwards and began `186 → 42 → 109 → 48`. That
route was not selectable for the old transit flight because node 42 was its
`previous_hop`. The existing guard correctly prevents immediate loop-back. A later answer
with a new counter succeeded through the repaired route, proving that the first payload
was lost rather than merely delayed.

The missing mechanism is not another retry or a relaxation of route selection. It is a
bounded return report stating that one relay failed to transfer custody onward.

### 1.2 DATA types have no semantic namespace

`DataType` is a full `uint8_t`, but current values are allocated sequentially from 1
through 19. Protocol control, application-bearing envelopes, security transfers, remote
administration and acknowledgements are interleaved numerically. Consequently, common
internal behavior is implemented by scattered exact-type comparisons. The own-DM burst
floor, for example, separately names E2E ACK and the two remote-administration types in
two places.

Unknown nonzero types can also fall through the final receive path and become user inbox
content. That is tolerable for an unknown application payload but wrong for an unknown
protocol-control record.

### 1.3 Internal frames currently acquire user-send outcomes

The generic send lifecycle is expressed through `send_blocked`, `send_acked`,
`send_failed`, and `send_aired`. Those events correlate user-originated DM/channel work.
Protocol-internal traffic may currently reach some of the same generic paths merely
because it is a locally originated `TxItem`. That can create a completion for a user send
which never existed or collide with a real local `{dst, ctr}` pair.

Internal protocols retain their own explicit results: `send_e2e_acked`,
`peer_key_cached`, `team_key_received`, remote response handling, and similar typed
outcomes. The internal namespace must suppress only the generic user-send lifecycle, not
those protocol-specific results.

### 1.4 The inbox already stores one internal outcome

E2E ACK is stored in the DM record stream with `InboxEntry::type = DATA_TYPE_E2E_ACK`, an
empty body and the acknowledged counter in `msg_id`. The stored numeric type is part of
the persistent record semantics even though the record bytes do not otherwise change.
The companion pull path names numeric value 3 directly today. Renumbering therefore
requires a storage-version migration and removal of every numeric literal.

The OLED inbox adapter currently sees every pulled record, including internal receipts.
This design introduces a shared record-class predicate so the ordinary message view
contains only application records while diagnostics can still inspect all records.

## 2. Goals

This design shall:

1. create a stable and visibly distinct namespace for protocol-internal DATA;
2. place every current internal DATA type in that range;
3. define one authoritative source of DATA-type traits;
4. make unknown internal DATA fail closed at an addressed recipient;
5. keep internal frames under normal duty, LBT, routing and hop-custody rules;
6. prevent internal frames from generating generic user-send lifecycle events;
7. retain explicit protocol-specific results;
8. persist only explicitly selected internal diagnostic outcomes;
9. exclude internal records from the ordinary inbox and unread count while preserving
   diagnostic access and ordinary deletion/eviction;
10. report the exact static B59 custody loss back to the original sender without changing
    route selection, retry budgets or the previous-hop guard;
11. preserve the distinction between a relay report and delivery proof; and
12. allow a later genuine E2E ACK to override a correlated uncertainty presentation.

## 3. Non-goals

This design does not:

- change `protocol::wire_version`;
- accept mixed old/new DATA-type numbering;
- change DATA header size, `DataFlag`, frame length limits or the conditional TYPE byte;
- add authentication to the custody report;
- add a custody-return path for team, hosted-mobile, last-mile, gateway or cross-layer
  addressing;
- cover every current post-custody discard site in B59 v1;
- retain or retransmit the failed payload;
- automatically regenerate hash/pubkey answers;
- change route selection, candidate ordering, liveness, RREQ rate limits, hop budgets,
  cascade retry counts, backoffs or timing;
- relax `next_hop_selectable`'s previous-hop rejection;
- give diagnostic records protected storage;
- persist all internal traffic;
- create a separate diagnostic partition; or
- change device configuration NV, timers, PHY parameters, frame layout, or HAL
  TX-completion behavior.

## 4. Terminology

**Application-bearing DATA** carries a logical user message or channel post, possibly
inside a transport envelope. It retains user-send pacing and completion semantics even
when an addressed intermediate node consumes and unwraps the outer type.

**Protocol-internal DATA** performs protocol control, discovery, acknowledgement,
administration, key transfer or diagnostic reporting. It is never an ordinary message at
the addressed receiver.

**Internal outcome record** is an explicitly selected protocol-internal receipt persisted
in the inbox record stream for later diagnostic or companion correlation. Most internal
DATA is not an outcome record.

**Logical failed message key** is:

```text
{failed_origin, failed_dst, failed_ctr}
```

**Custody report key** is:

```text
{reporter_layer, reporter, failed_origin, failed_dst, failed_ctr}
```

The logical key names one original DATA. The report key names one relay's claim about
that DATA. Reports from different relays are distinct evidence.

## 5. DATA-type namespace

### 5.1 Ranges

```text
0x00          ordinary untyped DM; no TYPE byte is emitted
0x01..0x7F    application-bearing DATA types and envelopes
0x80..0xBF    protocol-internal DATA
0xC0..0xFD    reserved; not valid for origination in this design
0xFE          inbox-store tombstone only; never a wire DataType
0xFF          invalid/reserved
```

The range predicate is exact:

```cpp
data_type_is_internal(t) == (t >= 0x80 && t <= 0xBF)
```

Do not use `t & 0x80` as the predicate: that would accidentally classify the reserved
`0xC0..0xFF` space and the inbox-only `0xFE` tombstone as valid internal wire types.

### 5.2 Exact assignments

The renumbering is a single intentional transition. After it lands, numeric assignments
are wire contract and new members append within their appropriate reserved block rather
than renumbering existing members.

| Symbol | Old | New | Class | Notes |
|---|---:|---:|---|---|
| ordinary DM | `0` | `0x00` | application | No TYPE byte. |
| `DATA_TYPE_INTRO` | `15` | `0x01` | application envelope | Stripped, then delivered as the user's DM. |
| `DATA_TYPE_MOBILE_SEND` | `14` | `0x02` | application envelope | Delegates logical application traffic to a home. Its enclosed type may itself be internal. |
| `DATA_TYPE_SEALED_RELAY` | `17` | `0x03` | application envelope | Carries a sealed user payload. |
| `DATA_TYPE_CHANNEL_POST` | `18` | `0x04` | application marker | Enclosed marker, not a standalone outer wire type today. |
| `DATA_TYPE_APP_MESSAGE` | planned `21` | `0x05` | application envelope | Reserved for the separate app-code design; not implemented here. |
| `DATA_TYPE_E2E_ACK` | `3` | `0x80` | internal outcome | Explicit durable outcome. |
| `DATA_TYPE_CUSTODY_FAILURE` | planned `20` | `0x81` | internal outcome | Added by the B59 slices below. |
| `DATA_TYPE_H_ANSWER` | `1` | `0x88` | internal | Hash-binding answer. |
| `DATA_TYPE_AUTHORITATIVE_H_ANSWER` | `2` | `0x89` | internal | Authoritative hash-binding answer. |
| `DATA_TYPE_H_ANSWER_PUBKEY` | `4` | `0x8A` | internal | Reserved/not currently emitted. |
| `DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY` | `5` | `0x8B` | internal | Exact B59 failed payload type after renumbering. |
| `DATA_TYPE_MOBILE_H_ANSWER` | `8` | `0x90` | internal | Hosted-mobile binding answer. |
| `DATA_TYPE_MOBILE_BREADCRUMB` | `9` | `0x91` | internal | Re-home redirect. |
| `DATA_TYPE_MOBILE_LAYER_QUERY` | `10` | `0x92` | internal | Gateway directory query. |
| `DATA_TYPE_MOBILE_LAYER_ANSWER` | `11` | `0x93` | internal | Gateway directory answer. |
| `DATA_TYPE_MOBILE_PUBKEY_PUSH` | `12` | `0x94` | internal | Mobile-to-home key push. |
| `DATA_TYPE_MOBILE_H_ANSWER_PUBKEY` | `13` | `0x95` | internal | Hosted-mobile binding plus pubkey. |
| `DATA_TYPE_MOBILE_KEY_FORWARD` | `16` | `0x96` | internal | Home-to-mobile requester key. |
| `DATA_TYPE_REMOTE_CMD` | `6` | `0xA0` | internal | Outer request type retained for the independent-RPC redesign. |
| `DATA_TYPE_REMOTE_RESP` | `7` | `0xA1` | internal | Outer response type retained for the independent-RPC redesign. |
| `DATA_TYPE_TEAM_KEY_GRANT` | `19` | `0xA2` | internal | Sealed security transfer, explicitly consumed. |

The gaps within `0x80..0xBF` are deliberate. They leave blocks for core outcomes,
hash/key discovery, mobility and administration/security without making subrange position
a second behavior authority.

### 5.3 No wire-version bump and no compatibility decoder

`protocol::wire_version` remains at its current value by owner ruling. All test devices
must be reflashed for the transition. Code must not:

- accept both an old and a new number for one semantic type;
- retain literal comparisons such as `type == 3` for E2E ACK;
- translate old on-air values; or
- infer a type from its former ordinal.

Active source, `docs/protocol.md`, `docs/frames.md`, the current remote-administration
design, the app-code draft and native/simulator contracts must use symbolic names or the
new values. Archived point-in-time designs may retain their historical values when their
status is unambiguous.

## 6. One authoritative DATA-type trait policy

### 6.1 Shape

The implementation must expose one constexpr/no-RAM authority, for example:

```cpp
struct DataTypeTraits {
    bool known;
    bool internal;
    bool application_bearing;
    bool generic_send_lifecycle;
    bool persistent_outcome;
};

DataTypeTraits data_type_traits(uint8_t type);
```

The exact API name may follow surrounding idiom, but there must not be parallel lists in
the MAC, inbox, JSON and UI. Narrow protocol-specific properties remain explicit named
predicates or switch arms beside this authority.

`app_dm` remains an encoding/origination input and is not derived mechanically from the
range. Some internal records, notably a sealed team-key grant, need parts of the existing
application encryption path. Numeric classification must not silently rebuild or alter
their inner layout.

### 6.2 Common internal behavior

For any value in `0x80..0xBF`:

1. The addressed receiver consumes it in an explicit handler.
2. If the receiver has no handler, it emits bounded unsupported-internal telemetry and
   drops it. It never falls through to `record_dm`, `msg_recv`, or ordinary inbox text.
3. A relay which is not the destination remains content-blind and forwards it normally.
4. An own origination bypasses the user-DM `dm_min_interval_ms` floor and does not stamp
   `_last_dm_origin_ms`.
5. It does not emit generic `send_blocked`, `send_acked`, `send_failed`, or `send_aired`.
6. Its protocol-specific result remains intact. Receiving E2E ACK still produces
   `send_e2e_acked`; receiving a team-key grant still produces `team_key_received`; remote
   RPC still owns its response/timeout contract.
7. It remains subject to normal queue capacity, duty cycle, LBT, HAL admission, hop
   retries, cascade routing and route availability. “Internal” is not priority and not an
   airtime exemption.
8. Only `persistent_outcome` types are written to inbox storage.

The common policy replaces, rather than supplements, the duplicated exact-type exemption
lists in `become_free()` and `issue_send()`.

### 6.3 Properties which must remain type-specific

The internal range does not imply any of the following:

- RTS backstop exemption: only E2E ACK has its existing verified RTS hint;
- sealing: team-key grant retains its sealed-only checks;
- custody-report exclusion: only E2E ACK and custody failure are excluded in B59 v1;
- durable storage: initially only E2E ACK and custody failure opt in;
- authentication or trust;
- route priority; or
- automatic response/retry behavior.

The exact B59 pubkey answer is internal and must remain eligible for a custody-failure
report. Therefore `data_type_is_internal()` must never be used as “do not report custody
failure.”

### 6.4 Application-bearing envelopes

`INTRO`, `MOBILE_SEND`, `SEALED_RELAY`, `CHANNEL_POST`, and the planned `APP_MESSAGE`
remain in the application range because they carry logical user/application intent.
Their explicit receive handlers may consume or unwrap the outer representation, but they
retain whichever user pacing and outcome belongs to the logical send. An unknown
application-range type retains the existing typed-application fallback unless a later
application contract changes it deliberately.

## 7. Durable internal outcome records

### 7.1 Opt-in set

The initial persistent-outcome set is exactly:

```text
DATA_TYPE_E2E_ACK
DATA_TYPE_CUSTODY_FAILURE
```

Adding another type requires an explicit record mapping and presentation contract. Do
not persist hash answers, mobility control, remote RPC, key forwarding or team-key grants
merely because they are internal.

### 7.2 Record mapping

E2E ACK keeps its current logical record:

```text
kind          = dm
type          = DATA_TYPE_E2E_ACK
origin        = confirming destination
msg_id        = acknowledged DATA counter
body_len      = 0
```

A custody-failure report is stored as:

```text
kind          = dm
type          = DATA_TYPE_CUSTODY_FAILURE
origin        = reporting relay (outer DATA origin)
layer_id      = receiving/reporting static layer in v1
msg_id        = failed_ctr
sender_hash   = 0
enc           = 0
origin_layer  = 0
body_len      = record_len
body          = the validated custody record, including any accepted future tail
```

The body remains binary in storage. It must never be passed through the ordinary text
encoder or OLED byte sanitizer as if it were a message.

### 7.3 Record-before-push ordering

On valid receipt:

1. parse and validate the complete record;
2. append the internal outcome to the inbox when enabled;
3. obtain the assigned DM-store sequence under the existing gap-tolerant model;
4. enqueue the live `custody_failure` Push carrying that sequence; and
5. return without ordinary DM delivery or E2E-ACK generation.

When storage is disabled, the live push carries `seq = 0`. Ordinary append failure and
drop-oldest behavior remain unchanged; there is no retry or protected slot. Sequence gaps
and the storage epoch retain their existing meanings.

### 7.4 Default presentation and diagnostic access

`Inbox::pull()` remains the raw authority and streams both application and internal
records. Filtering belongs to the view/adapter:

- the companion's ordinary message list includes application records and excludes
  `data_type_is_internal(type)`;
- the OLED inbox list/detail does the same;
- internal records do not increment the ordinary DM unread count;
- `pull_inbox` and the companion diagnostic/outcome view expose internal records;
- E2E ACK and custody failure receive semantic encodings rather than binary text;
- a raw diagnostic can name unknown internal record types numerically; and
- application records continue to render as today.

The sequence stream is still shared. Advancing a DM read/sync cursor across an internal
record is valid; it does not make that record an unread user message.

This presentation filter is independent of the live protocol-specific result. Receiving
a valid E2E ACK must still enqueue `send_e2e_acked` immediately, and an exactly correlated
local send may therefore move to `DELIVERED` without waiting for an inbox pull or redraw.
Hiding the stored receipt from the ordinary inbox must not suppress, delay, recreate or
otherwise mediate that live push.

Filtering occurs before any visible row budget or visible-total calculation. In
particular, the OLED adapter must not publish the raw record count returned by
`Inbox::pull()` as `inbox_total`: it counts only application records admitted to the
ordinary view. Internal records therefore consume neither one of the per-kind visible row
slots nor the ordinary-view total, while the raw pull still visits and reports them. This
also ensures that internal records preceding a newer application message cannot hide that
message behind the display budget.

### 7.5 Individual deletion and whole-inbox clear

An internal report has no deletion protection. Once its sequence is known from the
diagnostic stream, the existing `del_msg dm <seq>` path may delete it exactly like another
DM-store record.

A later slice adds:

```text
clear_inbox confirm
```

Semantics:

1. without the exact `confirm` token, refuse with `needs_confirm` and make no change;
2. wipe both DM and channel records, including application messages, E2E receipts,
   custody reports and tombstones;
3. preserve each monotonic next-sequence high-water;
4. reset both read cursors;
5. increment the shared storage epoch exactly once;
6. leave routes, membership, identity, configuration, keys and counters outside the inbox
   untouched; and
7. report the new epoch and newest sequence values over the invoking output transport.

This is the targeted inbox-only operation. It does not replace `prep-restart` or
`factory_reset`.

### 7.6 Inbox-store migration

The semantic record version changes from 4 to 5 in every persistent inbox-store
implementation. Although the serialized header layout is unchanged, old E2E-ACK records
contain numeric type 3, which becomes `DATA_TYPE_SEALED_RELAY` after renumbering and must
never be reinterpreted.

On version mismatch, preserve the existing upgrade discipline:

- erase record segments;
- retain `next_seq` so a sequence is never reused;
- set version 5;
- reset the read cursor;
- increment the storage epoch; and
- avoid a second epoch increment in empty-store detection.

The first boot after an ordinary firmware flash therefore performs the semantic inbox
migration automatically; the owner does not need to erase all device memory manually.

The volatile `FixedInboxStore` needs no migration because it cannot survive a flash, but
its runtime filtering, deletion and future clear behavior must match. `device_nv.h`'s
configuration version and `protocol::wire_version` do not change.

## 8. Custody-failure semantics

When an eligible relay has already ACKed custody of a transit DATA and the selected v1
cascade path terminally discards its `PendingTx`, the relay originates one best-effort
typed DATA back to the failed DATA's original sender. It reports only:

> This relay could not complete onward custody transfer for this DATA.

It does not prove that the destination failed to receive the DATA. The destination may
have received it while a hop ACK was lost, another path may have delivered a copy, and an
E2E ACK may still arrive. The report is not a hop NACK and not a terminal application
failure.

The factual core/app vocabulary is `custody_failure`. `DM UNCERTAIN` is a derived
presentation used only when the report exactly correlates to a user-send record.

## 9. Custody-failure wire record

### 9.1 Carrier

```text
outer DataType     DATA_TYPE_CUSTODY_FAILURE (0x81)
outer counter      fresh reporter counter
outer route plane  Plane::GLOBAL
app_dm             false
E2E_ACK_REQ         clear
CRYPTED             clear
inbox delivery      no ordinary message delivery
```

`Plane::GLOBAL` is explicit. `AUTO` is forbidden because a team-local ID collision could
route the report onto the team plane. The reporter uses normal routing, queueing, LBT,
duty and hop retries. A terminal failure of the notice itself is telemetry-only and never
creates a generic user-send result or another custody notice.

### 9.2 Version-1 body

The v1 fixed prefix is 24 bytes. `record_len` may be greater than 24 when a later version
appends a tail; a v1 reader accepts and stores the validated tail while interpreting only
the first 24 bytes.

```text
offset  size  field
0       1     version             = 1
1       1     record_len          = 24..available body length
2       1     notice_flags
3       1     terminal_reason     CustodyFailureReason
4       1     failed_origin       original DATA origin
5       1     failed_dst          original DATA destination
6       2     failed_ctr          little-endian
8       1     failed_type         original DATA type; 0 for ordinary DM
9       1     failed_data_flags   header flags visible to this transit relay
10      1     failed_plane        CustodyFailurePlane
11      1     reporter_layer      relay's active full layer ID
12      1     previous_hop        upstream custody source
13      1     failed_next_hop     last attempted downstream hop
14      1     requeue_count
15      1     alternatives_tried
16      1     committed_hops
17      1     remaining_hops
18      4     dst_hash32          little-endian; zero when absent/unavailable
22      2     reserved            transmit zero; must be zero for version 1
```

The record codec is one shared pack/parse path used by core receive handling, Push JSON,
pulled-record JSON and native tests. Do not re-read byte offsets separately in those
consumers.

### 9.3 `notice_flags`

```text
bit 0   forwarded          must be 1 in v1
bit 1   failed_at_cts      terminal root was waiting for CTS
bit 2   failed_at_ack      terminal root was waiting for hop ACK
bit 3   repair_attempted   this terminal cascade pass invoked repair-request logic;
                           it does not prove an RREQ was admitted or aired
bit 4   next_was_one_way   failed_next_hop was classified one-way
bit 5   has_dst_hash       dst_hash32 is present and valid
bits 6-7                   zero in v1
```

Exactly one of `failed_at_cts` and `failed_at_ack` must be set. The stage remains separate
from the terminal reason: `cascade_age` after repeated CTS failures is different evidence
from a direct `no_cts` label which would hide why the carrier was finally deleted.

### 9.4 `CustodyFailureReason`

Wire values are explicit and independent of `SendFailReason`:

```text
0   invalid                  never transmitted
1   one_way_throttled        the MF4 reprobe window refused another burst
2   cascade_count            cascade_requeue_max reached
3   cascade_age              cascade_requeue_total_max_ms reached
4   queue_full               TX queue had no requeue slot
5   load_shed                load-adaptive effective requeue budget rejected it
```

When more than one of count, age and queue-full is simultaneously true at the current
combined check, precedence is `cascade_count`, then `cascade_age`, then `queue_full`,
matching the existing condition's evaluation order and making the diagnostic
deterministic. `load_shed` and `one_way_throttled` are separate branches.

`no_route`, gateway timeout, loop-duplicate NACK, hop-budget NACK and radio-busy retry
exhaustion are not v1 reason values because their current discard sites are outside the
approved generation seam.

### 9.5 `CustodyFailurePlane`

This is a notice-specific wire enum, not a serialization of C++ `Plane`:

```text
0   static_same_layer        the only value transmitted by v1
1   team                     reserved; unsupported in v1
2   hosted_mobile            reserved; unsupported in v1
3   cross_layer              reserved; unsupported in v1
255 unknown                  reserved; never transmitted by v1
```

`Plane::AUTO` is a routing selector and is not a diagnostic plane. A v1 carrier which
resolved to static/global records `static_same_layer` whether its stored selector was
`AUTO` or `GLOBAL`.

## 10. Relay generation scope

### 10.1 Required eligibility

A v1 notice is generated only when all of the following are true:

1. a live `PendingTx` exists;
2. it is transit: `has_previous_hop == true`;
3. it is a normal DATA flight, not channel M/FLOOD;
4. it is plaintext at the DATA-frame level;
5. it is static/global same-layer after resolving its routing selector;
6. it is not a gateway cross-layer re-inject, team flight, hosted-mobile last mile or
   mobile delegation;
7. its standard unicast inner parses successfully;
8. the parsed origin equals `PendingTx::origin`;
9. `failed_origin`, `failed_dst`, `previous_hop`, and `failed_next_hop` are valid static
   node IDs in `1..254`;
10. `failed_ctr` is nonzero;
11. its type is neither `DATA_TYPE_CUSTODY_FAILURE` nor `DATA_TYPE_E2E_ACK`; and
12. deletion occurs in one of the selected terminal cascade branches represented by the
    v1 reason enum.

Internal types are otherwise eligible. This deliberately includes
`DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY`, the exact B59 case.

When a standard unicast inner contains a validated destination hash, copy it and set
`has_dst_hash`; otherwise transmit zero. Do not invent or reconstruct a hash from a node
ID.

### 10.2 Explicitly deferred custody-loss sites

The following current post-custody losses do not generate a v1 notice:

- a forwarder finds no route before installing `PendingTx`;
- gateway intra-layer relay policy drops after upstream custody;
- forwarding queue full before `PendingTx` exists;
- DATA radio-busy stash retry exhaustion;
- loop-duplicate and hop-budget NACK terminal paths;
- gateway-doorstep timeout/requeue loss;
- hosted-mobile last-mile loss;
- cross-layer handoff loss;
- malformed XL carrier rejection;
- reprovision/purge policy; and
- team-plane loss.

They require a later census and a shared post-custody-discard design. The presence of
`no_route` or another reserved concept in prose must not imply current coverage.

### 10.3 One generation per terminal carrier

The selected terminal helper is called once for one live `PendingTx`; it snapshots and
destroys that carrier once. Existing DATA identity dedup prevents ordinary hop
retransmissions from installing duplicate transit carriers. No new Node-resident
custody-report cache is added in v1.

The receiver/app correlation is idempotent by report key. A deliberately reconstructed
report with a fresh outer counter may remain as separate raw diagnostic evidence; it must
not repeat or downgrade a user-state transition. This follows the owner ruling that
diagnostic records receive no special storage protection or quota.

## 11. Typed terminal seam and ordering

The current terminal helper receives only `SendFailReason`, destination and counter. It
cannot build this record and currently emits a generic `send_failed` without knowing
whether the carrier is transit or internal. The implementation must introduce a typed
terminal context before B59 behavior.

The terminal context carries, without parsing event-name strings:

- root stage: CTS or hop ACK;
- terminal cause from the v1 enum;
- whether repair logic was attempted in this terminal pass; and
- an immutable reference to the complete live `PendingTx` until the bounded custody
  record has been materialized.

Do not copy the approximately 352-byte `PendingTx` onto a firmware stack merely to retain
24 diagnostic bytes. Read the authoritative carrier once, build the bounded record, then
perform the established reset sequence.

For one terminal carrier, order is load-bearing:

1. capture all diagnostic fields while `PendingTx` exists;
2. decide whether the local carrier owns a generic user-send lifecycle;
3. preserve the existing pre-reset generic `send_failed` behavior for eligible local
   application sends;
4. suppress that generic event for transit and protocol-internal carriers;
5. reset the failed `PendingTx`;
6. call `become_free()` in the existing order, allowing the next queued flight to become
   current; and
7. enqueue the custody notice best-effort from the saved snapshot.

The notice is never installed by mutating the failed carrier and never inherits its
counter, nonce seed, previous-hop exclusion, retry counters, flight generation or
alternatives. It is a new own-origin internal DATA.

Cause selection must be made before the combined state is erased. String prefixes such
as `"rts_*"` and `"data_*"` are not an authority for a wire enum.

## 12. Notice transmission behavior

The report uses the existing standard typed-DATA enqueue path and one carrier-conversion
path. It must not construct a parallel `TxItem` field by field when an existing enqueue
helper can preserve the carrier contract.

The report:

- is queued after the failed flight is closed;
- does not preempt a flight which `become_free()` has just installed;
- is exempt from the user-DM burst floor through the shared internal trait;
- receives ordinary duty/LBT/hop retry treatment;
- never requests an E2E ACK;
- does not arm a user E2E deadline;
- never emits generic `send_blocked`, `send_acked`, `send_failed`, or `send_aired`;
- never causes a custody notice about itself; and
- emits only bounded local telemetry if enqueueing, routing or final delivery fails.

Queue capacity and route failure are accepted best-effort losses. The report has no
reserved queue slot and no payload-retention retry.

## 13. Receiver validation and consumption

An addressed `DATA_TYPE_CUSTODY_FAILURE` is consumed before ordinary DM delivery. The
receiver must validate all of the following before storage, push, correlation or output:

1. the DATA is plaintext;
2. standard unicast parsing succeeds;
3. body length is at least 24;
4. `version == 1`;
5. `record_len >= 24` and `record_len <= available body length`;
6. flags bits 6-7 are zero;
7. `forwarded` is set;
8. exactly one of the CTS/ACK stage bits is set;
9. `terminal_reason` is a known nonzero v1 value;
10. `failed_plane == static_same_layer`;
11. `failed_origin` equals this node's static ID;
12. all four node-ID fields required by v1 are in `1..254`;
13. `failed_ctr` is nonzero;
14. `failed_type` is not E2E ACK or custody failure;
15. `reporter_layer` equals the active receiving full layer in the same-layer v1 case;
16. `has_dst_hash` agrees with a nonzero `dst_hash32`; when clear, `dst_hash32` is zero;
17. reserved bytes are zero; and
18. count/hop fields fit their protocol domains.

Unknown versions, short records, unknown reasons, reserved-bit violations and impossible
identities are dropped with bounded local telemetry. They are neither stored nor exposed
as user messages. A valid unknown tail is retained in the durable record and ignored by
the v1 semantic decoder.

The outer origin is the reporter. It is not authenticated. A mismatch between outer
context and body invariants is malformed, not evidence.

## 14. Live and pulled diagnostic contract

### 14.1 PushKind and carrier mapping

Append `PushKind::custody_failure`; never insert it among existing values. `Push` does not
grow. Reuse its existing fields as follows:

```text
kind       custody_failure
origin     outer reporting relay
dst        failed_dst
ctr        failed_ctr
layer_id   reporter/receiving layer
seq        assigned DM-store sequence, or 0 when inbox disabled
body       validated custody record bytes
body_len   record_len
```

Do not place the custody reason in `Push::reason`; that field is a `SendFailReason` and the
wire enum is deliberately independent. JSON and human output parse `Push::body` through
the shared custody codec.

### 14.2 JSON

Both the live push and a pulled custody record produce the semantic event
`custody_failure`. A pulled record may additionally carry its receive timestamp. Required
fields are:

```json
{
  "ev": "custody_failure",
  "seq": 17,
  "reporter": 186,
  "reporter_layer": 1,
  "failed_origin": 42,
  "dst": 48,
  "ctr": 3598,
  "failed_type": 139,
  "stage": "cts",
  "reason": "one_way_throttled",
  "previous_hop": 42,
  "next_hop": 48,
  "requeues": 0,
  "alternatives": 1,
  "committed_hops": 0,
  "remaining_hops": 0,
  "repair_attempted": true,
  "one_way": true
}
```

`seq` is omitted or zero according to the existing live-push convention when storage is
disabled. `dst_hash` is emitted through the existing hash-formatting helper only when its
flag is valid. Unknown accepted tail bytes are not copied into JSON.

The ordinary `inbox_dm` text encoder must not stringify the binary record. E2E ACK keeps
its semantic `type:"e2e_ack"` contract, updated to the symbolic/new value rather than a
literal 3.

### 14.3 Human USB serial output

USB serial reports the same facts in one bounded line, for example:

```text
CUSTODY FAILURE reporter=186 layer=1 origin=42 dst=48 ctr=3598 stage=cts reason=one_way_throttled prev=42 next=48 repair=attempted one_way=1
```

No output may call it a NACK or claim non-delivery.

## 15. User-send correlation

### 15.1 Two independent results

Receiving a valid report always creates factual diagnostic evidence. Correlating it to a
user send is optional and secondary:

```text
validated report
    ├── durable custody_failure record + live diagnostic
    └── exact user-send match, if one exists
            └── derived presentation: DM UNCERTAIN
```

The exact B59 pubkey answer has no user-DM tracker. It therefore produces diagnostic
evidence only, which is still the purpose of the fix.

### 15.2 Correlation key

For the original sender, `failed_origin` must already equal self. A user outbox/tracker
matches on the complete pair:

```text
{failed_dst, failed_ctr}
```

Never match by counter alone. Cross-layer/stable-hash correlation is outside v1 because
such reports are not generated.

### 15.3 Monotonic evidence rules

1. No matching user transaction: retain/report the diagnostic only.
2. Waiting, queued, aired-waiting or generic not-confirmed transaction: record custody
   uncertainty without consuming its E2E correlation identity.
3. Already delivered: retain the diagnostic but do not downgrade the transaction.
4. Locally failed before custody: retain the diagnostic but do not overwrite the local
   terminal state; such a match is stale, malformed or forged evidence.
5. E2E deadline after a custody report: record the timeout fact but keep the stronger
   uncertainty presentation. The report does not cancel the deadline.
6. Later matching E2E ACK: upgrade to delivered from waiting, not-confirmed or uncertain.
7. Repeated same report key: no repeated user-state transition.
8. Different reporters for the same logical message: preserve every diagnostic report,
   but the user transaction remains one uncertain state.

`DM UNCERTAIN` is nonterminal in the sense that a later E2E ACK can replace it. It is not
permission to retry automatically.

### 15.4 Companion and OLED

The companion owns durable/outbox correlation across reconnects using the pulled outcome
record. The firmware OLED may correlate only a live report to a still-retained local
`SendTracker`; it must follow the same monotonic rules. The default OLED inbox excludes
the stored binary outcome. A dedicated OLED diagnostic-history screen is not required by
this design.

## 16. Security and resource bounds

The report is cleartext and has no end-to-end signature or MAC. The outer reporter ID and
body can be forged by a participant capable of injecting valid-looking mesh traffic.
Therefore:

- UI wording is “reported by relay,” never “proved by relay”;
- no payload is resent automatically;
- no route, liveness, trust, ACL or key state changes from the report;
- no peer is quarantined or penalized;
- no destructive or administrative action follows;
- validation failures are bounded telemetry only; and
- future automatic action requires a separate authenticated/corroborated design.

Normal radio duty and existing originator controls bound airtime. Inbox storage supplies
no extra defense: a valid report consumes one ordinary DM record and may eventually evict
old records under the same policy. This is owner-approved. Existing per-frame DATA dedup
prevents ordinary retransmissions from delivering the same outer frame repeatedly; the
presentation layer remains idempotent by report key.

The implementation must not grow `Push`. Any new Node field or ring requires the D2 size
and per-board RAM proof. This design intentionally requires no new custody-report dedup
ring or timer.

## 17. Implementation slices

The implementation plan must preserve attribution and C1. A suitable order is:

### Slice A0 — existing DATA-path characterization and quality audit

This is a bounded audit of the core surface this arc will change, not a general firmware
review and not a cleanup bundle. Before changing any numeric DATA type or behavior:

- derive an exhaustive matrix of every live DATA type and its producers, origination
  paths, relay/unwrap paths, addressed consumers, persistence policy, generic and
  type-specific outcomes, and active tests;
- trace admission, queueing, routing, retries, hop custody, HAL refusal and terminal
  cleanup far enough to identify which component owns each success or failure result;
- locate raw numeric type comparisons, duplicated type lists, unknown-type fallthrough,
  behavior asserted only by comments, and feature-gated paths missing from native or the
  simulator;
- add behavior-preserving characterization tests for load-bearing current behavior and
  mutation-check them so a green instrument demonstrably distinguishes its claim;
- record the current wire-sensitive corpus and board-gate baseline before Slice A changes
  it; and
- publish the matrix and findings in the design/ledger before implementation proceeds.

Every finding receives one explicit disposition:

1. **blocker** — reproduce it and fix it in its own reviewed slice before Slice A;
2. **adjacent correctness defect** — register it with evidence, severity, dependency and
   a concrete close-by rather than expanding the active slice;
3. **quality or observability debt** — register the missing truth/control and the gate
   which would close it;
4. **false or stale active documentation** — correct it in the slice which establishes
   the replacement truth; or
5. **verified existing behavior** — retain it as characterization, with its authority
   named, so a later review does not reopen it from intuition.

C1 applies throughout: A0 may add tests, probes and documentation, but it does not change
production behavior. A production defect discovered by A0 is either a blocker slice or a
separately ordered register item.

> **A0 dispatch operationalization (supervisor, 2026-08-29 — merged from the reviewed dispatch brief so this
> spec is the one A0 document):**
> - **Where the matrix lands:** a NEW coder-authored evidence file
>   `docs/superpowers/evidence/2026-08-29-custody-a0-matrix.md` (the UI-16 N1-evidence precedent). The
>   maintained docs (register, this spec, bench, BASELINE) stay supervisor-landed from drafted text in the
>   coder's report.
> - **Standing context the audit must absorb (V1 against the tree, not these notes):** the DATA flags byte and
>   `q_opcode` are EXHAUSTED and `0x01` is aliased LIVE as `MS_ENCLOSED_TYPE` on the homed-mobile path — the
>   matrix carries the flags/TYPE-byte interaction per type, with `data_frame_len`/`data_inner_cap`
>   (`lib/core/frame_codec.h`, §B20/B21) as the one length authority; the §B159 physical-start
>   deadline/`TxOutcomeKind::expired` machinery and the §B134/§B260 inbox chain are fresh seams the ownership
>   trace crosses — their in-source ledgers are current authority.
> - **Gate constants:** native baseline 2333/98448/0 + the characterization cases with the PIN derivation
>   written (RUN the binary — the wrapper lies); new `a0*` targets in the isolated mutation harness
>   (`tools/probe_ui_model_mutations.py`, scratch trees), each RED at match count exactly 1, the existing
>   865+ anchors verified unbroken; an unmeasurable mutation is REMOVED and its absence documented, never
>   kept as decoration.
> - **C1 enforcement at the diff:** `git diff` shows ZERO production-source changes — tests/probes/evidence
>   only; the sole exception is V1's fix-drifted-comments duty, line-count-neutral (§B254) and listed.
>   Corpus: the 0-build-action proof + the s18 tripwire, keystone read from `simulation/BASELINE.md`. Boards:
>   only the baseline-RECORDING run (the ruled pair via the certified runner) — ⚠ §B262: the `heltec_mobile`
>   payload hash is same-path-only; record RAM/flash/objects/symbols across trees.
> - **Coder prohibitions (standing):** no `git commit`/`add`/`checkout`, no maintained docs, no `tracker.md`,
>   no `platformio.ini`, no parallel-session files, no pollers, never pipe the battery runner, no device
>   contact. Metal residue: none expected (an audit) — state so or draft the exception.

### Slice A — namespace transition

- add the range/trait authority and exact enum assignments;
- renumber every current type;
- reserve `APP_MESSAGE = 0x05` without implementing app codes;
- remove numeric literals and update active contracts;
- leave `protocol::wire_version` unchanged and pin that owner ruling;
- bump persistent inbox-store semantic version 4 → 5 and prove the wipe/epoch/high-water
  transition; and
- re-anchor wire-sensitive simulator baselines separately from B59 behavior.

No custody codec, PushKind or terminal behavior belongs here.

### Slice B — common internal behavior

- replace duplicated DM-floor exemption lists with the trait authority;
- suppress generic user-send lifecycle events for all internal types;
- preserve every protocol-specific event;
- add fail-closed addressed handling for unknown internal types;
- retain forwarding for unknown internal traffic; and
- prove application envelopes retain their user semantics.

If extracting an existing helper is required, make the behavior-neutral extraction its
own sub-slice before expanding semantics.

### Slice C — diagnostic inbox classification

- generalize internal-outcome record classification;
- make E2E ACK use symbolic traits rather than literal 3;
- exclude internal records from default companion/OLED inbox and unread presentation,
  before applying visible row budgets or visible-total calculations;
- preserve the independent live `send_e2e_acked` fast path and its exact-correlation
  transition to `DELIVERED`;
- retain them in diagnostic pull;
- prove ordinary eviction/deletion applies equally.

### Slice D — inbox-only clear

- add the confirmed `clear_inbox confirm` operation;
- preserve high-water values and bump the storage epoch once;
- prove the refusal and destructive-success contracts independently of B59; and
- leave every non-inbox subsystem untouched.

### Slice E — typed terminal context

- replace event-string reason inference at the selected cascade terminals with typed root
  stage and terminal cause;
- preserve local application `send_failed` ordering and values;
- suppress transit/internal generic completions through the common ownership policy; and
- do not emit custody traffic yet.

### Slice F — custody codec and relay generation

- add the v1 codec and numeric enums;
- snapshot eligible terminal transit carriers;
- reset/become-free in the established order;
- enqueue one global-plane internal report after the failed carrier is gone; and
- leave every deferred custody-loss site unchanged.

### Slice G — receiver, persistence and factual output

- validate and consume type `0x81`;
- record before Push;
- append `PushKind::custody_failure` without growing `Push`;
- produce live and pulled semantic JSON plus USB output;
- exclude it from ordinary message UI; and
- keep invalid/unsupported reports out of storage.

### Slice H — optional user-send presentation

- correlate exact `{dst, ctr}` user sends;
- add nonterminal `DM UNCERTAIN` presentation;
- preserve deadline and late-ACK behavior;
- keep delivered/local-failed states monotonic; and
- leave uncorrelated internal reports diagnostic-only.

No slice adds automatic payload retry.

## 18. Required verification

### 18.0 Characterization and audit gate

1. The A0 matrix is source-derived and exhaustive against the allocated `DataType` set;
   a count/sentinel or equivalent control fails if a new type is added without a row.
2. Every row names its real producer, receiver/consumer, relay treatment, persistence
   decision, outcome owner and executable coverage, including feature-gated absence.
3. Searches for raw semantic literals and duplicate policy lists are controlled: a zero
   result is accepted only when a mutation/reintroduced known instance makes the search
   fail.
4. Every new characterization test has at least one mutation or equivalent negative
   control proving it can fail for the behavior it claims.
5. A0 changes no production behavior. Any production diff is removed or dispatched as a
   separately reviewed blocker slice.
6. The current native, simulator/corpus, warning and ruled board baselines are recorded
   with reproducible tool authority; pre-existing red or uncovered paths are reported,
   never normalized.
7. Each finding is written to the maintained bug register or explicitly classified as
   verified behavior/document correction. No finding disappears into prose or a later
   slice's untracked scope.

### 18.1 Namespace and migration

1. Static assertions pin every new numeric value and all range boundaries.
2. Boundary tests cover `0x00`, `0x01`, `0x7F`, `0x80`, `0xBF`, `0xC0`, `0xFD`, `0xFE`,
   and `0xFF`.
3. A mutation replacing the bounded internal-range predicate with a high-bit test is RED.
4. A source/structural check rejects surviving semantic literals such as `type == 3`.
5. Every live DATA type round-trips through `pack_data`/`parse_data` at its new value.
6. `protocol::wire_version` remains exactly unchanged, with a control that fails if it is
   bumped during this transition.
7. Persistent store version 4 → 5 wipes records, retains `next_seq`, resets read cursor,
   increments epoch once and does not double-increment on empty detection.
8. An old stored type-3 E2E receipt cannot reappear as a sealed-relay/application record.
9. Volatile inbox behavior remains boot/session scoped.

### 18.2 Internal behavior

1. The pure trait table exhaustively classifies every allocated internal type; integration
   cases using representative direct, sealed and feature-gated producers prove consecutive
   internal originations bypass the user-DM floor and do not stamp it.
2. Application-bearing envelope controls remain subject to their logical user policy.
3. Internal success/failure does not emit generic `send_blocked`, `send_acked`,
   `send_failed`, or `send_aired`.
4. E2E ACK, team-key grant, hash/key and remote-RPC specific outcomes remain reachable.
5. An unknown addressed internal type is dropped and never inboxed; a relay forwards it.
6. Persistence is exact: E2E ACK and custody failure opt in; every other current internal
   type does not.
7. Mutations restoring either duplicate exact-type floor list or generic internal
   `send_failed` behavior are RED.

### 18.3 Codec

1. Pin the 24-byte prefix and every offset.
2. Pin little-endian 16/32-bit fields.
3. Pin all reason/plane numeric values.
4. Encoder always sets version, length and zero reserved bytes.
5. Decoder accepts a valid unknown tail and retains it for storage.
6. Short length, unknown version/reason/plane, reserved bits, contradictory stage bits,
   invalid IDs and inconsistent hash flag are rejected.
7. JSON and USB consume the shared parsed record, not duplicate offsets.

### 18.4 Relay behavior

1. Reproduce the exact B59 topology and failure shape in native tests.
2. The eligible relay emits exactly one type-`0x81` report addressed to the failed origin.
3. The previous-hop guard remains byte-for-byte/structurally unchanged; a mutation which
   relaxes it is RED.
4. The new report may select the former upstream node as its own global route because it
   is a new flight with no inherited previous-hop state.
5. Controls pin forced `Plane::GLOBAL`; an AUTO mutation is RED under a team-ID collision.
6. Controls pin snapshot → reset → `become_free()` → enqueue ordering, including a queued
   flight B becoming current before the notice is queued.
7. One mutation per terminal cause proves correct reason and CTS/ACK stage mapping.
8. Local-origin failure keeps its existing generic application result and emits no report.
9. Negative eligibility matrix: crypted, raw-inner/unparseable, team, mobile, XL,
   gateway-reinject, channel, E2E ACK and custody notice.
10. The exact authoritative pubkey answer remains positively eligible.
11. Notice route/queue failure creates telemetry only—no recursion and no generic app
    lifecycle push.
12. No original payload bytes are retained or regenerated after terminal deletion.

### 18.5 Receipt, storage and presentation

1. A valid addressed report is stored before its live Push; both carry the same assigned
   sequence.
2. Storage-disabled receipt still emits one live push with sequence zero.
3. Pulled and live JSON carry the same semantic report identity and fields.
4. Binary report bytes never pass through the ordinary inbox text encoder.
5. Default companion and OLED inbox views exclude E2E ACK and custody-failure records,
   while diagnostic pull exposes both.
6. Internal outcomes do not increment ordinary unread counts.
7. Individual `del_msg dm <seq>` deletes a custody report normally.
8. Normal fixed/durable drop-oldest eviction may remove it; no reserved capacity exists.
9. `clear_inbox` without confirmation is inert; confirmed clear wipes both stores, retains
   high-water values, resets cursors, increments epoch once and leaves non-inbox state.
10. One received E2E ACK simultaneously proves both independent paths: the exactly
    correlated live send becomes `DELIVERED`, while the stored receipt contributes no
    ordinary OLED/companion row, visible total or unread count.
11. Raw diagnostic pull still returns that same stored E2E-ACK record, and ordinary
    application records following any number of internal records remain eligible for the
    newest-visible row budget.

### 18.6 Correlation

1. Match requires both destination and counter.
2. With no matching tracker, receipt produces diagnostic-only output.
3. Waiting/not-confirmed becomes uncertain without consuming late-ACK identity.
4. Already delivered and locally failed are never downgraded.
5. Timeout after uncertainty does not erase the report.
6. Late E2E ACK upgrades uncertain to delivered.
7. Same report key is idempotent for user state.
8. Different reporters remain separately visible but produce one uncertain user state.
9. A mutation matching by counter only is RED.
10. No report causes retransmission, route mutation or trust mutation.

### 18.7 Project gates

Each implementation slice runs the gate proportionate to what it changes. Any `lib/core`
behavior or bytes require native, the simulator corpus and the current exact anchors from
`simulation/BASELINE.md`. The namespace transition is measured and re-anchored separately
from B59. Board builds run sequentially per the current gate; warning census must remain
free of new warnings and `-Wswitch`. Any Node/member change requires `sizeof(Node)`,
`sizeof(Push)` and per-board RAM evidence. `git diff --check` must be clean.

Documentation is part of each slice's gate, not an end-of-arc cleanup. A slice cannot pass
while an active statement about the behavior or bytes it changed remains stale:

- `docs/frames.md` owns exact wire facts — numeric assignments, flags, byte layouts,
  lengths and offsets;
- `docs/protocol.md` owns lifecycle semantics — origination, forwarding, consumption,
  persistence, failure and outcome behavior; and
- this design and the bug register own rationale, findings, deferred scope and closure
  evidence.

Slice A must update the complete DATA-number table in `docs/frames.md` and the namespace,
mixed-firmware/reflash ruling and migration behavior in `docs/protocol.md` in the same
slice. Later slices update those documents only for the behavior or wire format they
actually land. Historical fenced text may remain; an active false claim may not.

## 19. Documentation obligations

As each implementation slice lands:

- update `docs/protocol.md` with internal DATA behavior and custody semantics;
- keep `docs/frames.md` limited to the new numeric assignments and the 24-byte wire
  layout;
- update the current remote-administration design to symbolic/new outer types;
- update the app-code draft from proposed values 20/21 to custody `0x81` and reserved
  application marker `0x05`;
- update inbox/companion contracts for internal outcome records and `clear_inbox`;
- add only metal-inaccessible residue to the maintained bench script;
- update B59 in the maintained bug register when implementation and gates actually close
  it; and
- record simulator anchors and RAM/layout measurements in `simulation/BASELINE.md`.

This first draft approves the architecture and bounded v1 scope only. It is not an
implementation plan and does not mark B59 closed.
