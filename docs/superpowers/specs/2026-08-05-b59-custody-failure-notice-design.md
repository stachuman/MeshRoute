<!-- Author: OpenAI Codex -->
# B59 custody-failure notice — short design

> **SUPERSEDED 2026-08-23.** Retained as the original B59 proposal; do not plan or
> implement from this file. The active design is
> [`2026-08-23-internal-data-and-custody-outcome-design.md`](2026-08-23-internal-data-and-custody-outcome-design.md),
> which incorporates the owner-approved internal DATA namespace, durable diagnostic
> records, inbox filtering/clear semantics, and the narrowed B59 v1 generation seam.

*2026-08-05. Status: WIRE AND HANDLING PROPOSAL; AUTOMATIC RETRANSMISSION POLICY OPEN. This is an
additive design for B59. It does not change route selection, cascade ordering or the previous-hop
loop guard.*

## 1. Outcome and semantics

When a relay has ACKed custody of a transit DATA and later terminally discards that DATA, it should
send a best-effort typed DM back to the DATA's original sender. The notice reports a fact about the
relay:

> I could not complete custody transfer for this DATA.

It does **not** prove that the destination failed to receive the DATA. A downstream node may have
received it while its hop ACK was lost, and an E2E ACK may still arrive later. Therefore this is not
a NACK and must not produce a terminal `send_failed` result.

Names:

```cpp
DATA_TYPE_CUSTODY_FAILURE = 20;
```

Recommended application/console vocabulary is `send_delivery_uncertain` / `DM UNCERTAIN`. Do not
call the wire type `POSSIBLE_NACK`: MeshRoute already has hop-level NACKs, and the new message has
different, deliberately non-authoritative semantics.

## 2. Wire body

The custody notice is a normal typed DATA with `app_dm=false`, a fresh outer counter, no
`E2E_ACK_REQ`, and no inbox delivery. Its outer origin identifies the reporting relay. The v1 body
is a fixed **24-byte** diagnostic record; `record_len` permits a later version to append fields while
v1 readers safely ignore the tail.

```text
offset  size  field
0       1     version             = 1
1       1     record_len          = 24 or greater
2       1     notice_flags
3       1     failure_reason
4       1     failed_origin       original DATA origin
5       1     failed_dst          original DATA destination
6       2     failed_ctr          little-endian
8       1     failed_type         original DATA type (0 for an ordinary DM)
9       1     failed_data_flags   original DATA flags visible to the relay
10      1     failed_plane        Plane::AUTO / TEAM / GLOBAL
11      1     reporter_layer      relay's active full layer ID
12      1     previous_hop        upstream custody source; 0 if unavailable
13      1     failed_next_hop     last attempted downstream hop
14      1     requeue_count
15      1     alternatives_tried
16      1     committed_hops
17      1     remaining_hops
18      4     dst_hash32          little-endian; 0 when absent/unavailable
22      2     reserved            transmit as zero; ignore on receipt
```

`notice_flags` v1:

- bit 0: the failed DATA was a forwarded/transit carrier;
- bit 1: terminal state was waiting for CTS;
- bit 2: terminal state was waiting for hop ACK;
- bit 3: route repair was requested during this failure;
- bit 4: the failed next hop was marked one-way;
- bit 5: `dst_hash32` is present and valid;
- bits 6–7: zero in v1.

`failure_reason` should use a custody-specific enum such as `no_cts`, `no_ack`, `no_route`,
`cascade_age`, `cascade_count`, `queue_full` and `load_shed`. It must not reuse the application
`SendFailReason` numeric encoding accidentally; map explicitly so either enum can evolve.

The body contains more detail than the first handler needs intentionally. It gives later policy and
bench analysis enough context to distinguish a dead edge, an ACK-loss ambiguity, route-repair
timing and queue/load shedding without changing the wire type again.

## 3. Relay handling

Generate the notice immediately before terminally deleting a `PendingTx`, only when all of these are
true:

1. the carrier is transit (`has_previous_hop`);
2. the failed type is not `DATA_TYPE_CUSTODY_FAILURE` or `DATA_TYPE_E2E_ACK`;
3. the original sender is addressable in the supported plane; and
4. this logical `{failed_origin, failed_dst, failed_ctr}` has not already produced a notice here.

Copy the diagnostic fields while `PendingTx` still exists, then perform the existing reset and
`become_free()` order. Enqueue the notice best-effort after the failed carrier is gone. Queue or
route failure of the notice is telemetry-only: it must never create a notice-about-a-notice,
requeue the discarded payload or hold the MAC state machine open.

The v1 implementation scope is the metal-proven **static, same-layer** B59 case. The notice uses
ordinary routing back to `failed_origin`. This permits relay 186 to send the new logical message
through node 42 even though the failed transit DATA was forbidden from selecting its own
`previous_hop`; the loop guard remains unchanged and applies only to that old transit flight.

Team, hosted-mobile and cross-layer return addressing require the same stable-hash/reversed-path
analysis as E2E ACK and are deferred rather than guessed. Unsupported cases emit local telemetry
and no notice.

## 4. Sender handling

A valid notice addressed to the original sender is consumed, never inboxed, and produces:

```text
send_delivery_uncertain {dst, ctr, reporter, reason, type, next_hop, layer, route_repair}
```

The state is advisory and non-terminal:

```text
awaiting confirmation -> delivery uncertain -> delivered, if a matching E2E ACK later arrives
```

The handler must not clear an E2E-ACK deadline, downgrade an already confirmed send, or emit the
existing terminal `send_failed` push. Duplicate notices for the same failed key are collapsed.

### Open policy: reconstructing and retrying protocol answers

B59's exact failed payload was an authoritative pubkey answer. The owner can reconstruct that
answer from its identity, so one bounded delayed retry with a **new DATA counter** may be useful and
is idempotent at the receiver. However, this spec does not yet approve it.

Decision still required:

- whether `H_ANSWER`, `AUTHORITATIVE_H_ANSWER`, and their pubkey forms are automatically regenerated;
- retry delay/jitter, retry cap and dedup key;
- whether retry waits for route-repair evidence or merely a bounded grace period; and
- which notice reasons are eligible.

Ordinary user DMs must not be automatically retried in v1. Their payload is no longer retained at
the origin after hop custody transfer, and retransmission under a new counter can create a visible
duplicate when the first copy arrived but its ACK path failed. Generic retry would need a separate
bounded origin cache and duplicate-delivery policy.

## 5. Required checks

1. Reproduce B59 in a native topology: terminal relay giveup emits exactly one type-20 notice back
   to the original sender while the previous-hop guard remains unchanged.
2. Pin the 24-byte codec round-trip, little-endian fields, `record_len >= 24`, reserved-zero encoder
   and unknown-tail tolerance.
3. A notice is consumed rather than delivered to the inbox and emits `send_delivery_uncertain`.
4. A later matching E2E ACK wins and reports delivery; an uncertain notice cannot downgrade it.
5. A failed or received custody notice never generates another custody notice.
6. Local-origin failures keep their existing `send_failed` behavior and do not emit an on-air
   notice.
7. Pubkey-answer automatic retry is demonstrably absent until the open policy is approved.
8. Gate airtime and queue effects: B59 traffic is additive only at a terminal transit discard, with
   no change to next-hop selection, cascade retries, RREQ policy or ordinary successful delivery.
