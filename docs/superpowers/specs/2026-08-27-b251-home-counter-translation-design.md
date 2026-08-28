# B251 — Home counter translation for hosted-mobile plaintext transit

**Status:** ✅ CLOSED / INDEPENDENT QG PASS (2026-08-27)

**Closure effect:** B161, B157 and B153 are closed. B153 closes on the owner's one-time acceptance of the B182
`757/1041` result, not on a permanent absolute floor. B112 remains separate.

**Scope:** one corrective core slice. No RTS/CTS/DATA layout change.

## 1. Problem and invariant

A registered mobile owns the transport counter on its direct mobile-to-home hop. The home owns the transport counter on the static-network hop:

```
mobile -- ctrM --> home -- ctrH --> destination
mobile <-- ctrM -- home <-- ctrH -- destination ACK
```

Today an ordinary plaintext mobile send is forwarded with `ctrM` unchanged while its exposed origin is the home id. That lets it alias an unrelated home-originated `(origin=home,dst,ctrM)` flight in a downstream node's completed-flight cache. B161 made the affected typed answer canonical and exposed the collision in s22; accepting a new s22 anchor would hide, not fix, B251.

The fix is counter translation at the home boundary. The application envelope remains byte-for-byte intact; only the outer hop counter changes.

## 2. Verified current state

This section records source facts, not assumptions from earlier designs.

1. `stamp_origin()` gives a registered mobile's global-plane flight the home id and sets `mobile_src` (`node.h`).
2. `issue_send()` routes a registered mobile's global flight first to its home (`node_mac.cpp`).
3. `PendingRx` preserves `mobile_from`, the wire plane, and the exact RTS flight identity. `handle_data()` drops that carrier before `PostAck` is built (`node_mac_rx.cpp`).
4. A normal relay copies `PostAck::{origin,dst,ctr,inner,...}` into a `TxItem`; it therefore forwards `ctrM` today.
5. `next_ctr(dst)` is destination-scoped.
6. `DelegAck` is a bounded eight-row node-global ring, but its live key is only `(mobile_hash,ctrH)`, it evicts the oldest live row on pressure, and its put API cannot report refusal (`node_hashlocate.cpp`).
7. A returning same-layer E2E ACK exposes its acker as `PostAck::origin`; a returning cross-layer ACK also exposes the stable acker hash as `ui.source_hash`.
8. The completed-flight cache already includes the exact immediate on-air sender, destination, wire plane, identity domain and full RTS identity. Two live hosted mobiles have distinct local ids by the OFFER/CLAIM allocation and collision backstop, so the cache already distinguishes their first hops.
9. The separate `_seen_origins` key does **not** distinguish those mobiles: mobile-static traffic is currently keyed as `(home origin,dst,ctrM,mobile marker)`. A second mobile with the same `ctrM` is consequently classified as a different-previous-hop loop. This is the first-hop alias that the implementation must also close.
10. `DATA_FLAG_CRYPTED` flights use a nonce-seed digest for RTS/completed-flight identity, and their counter participates in seal validation. Rewriting it invalidates the message.
11. `DATA_TYPE_SEALED_RELAY` is plaintext-framed, while its opaque seal owns a separate `seal_ctr` inside the body. The outer MAC counter is independent. Production mobile hash/cross-layer sends already terminate at the home as `MOBILE_SEND` and are re-originated, rather than reaching the ordinary transit arm.

## 3. Exact eligibility predicate

Translate only a received DATA for which all of the following are true:

- the outer DATA is not `DATA_FLAG_CRYPTED`;
- the stored RTS wire plane is static/global, not team;
- `PendingRx::mobile_from` is true;
- the DATA is transit (`!for_me_dst(d.dst)`);
- the immediate on-air sender equals `mobile_local_id` in one live direct `_mobile_reg[]` row;
- the canonical plaintext-unicast inner parses;
- if SOURCE_HASH is present, it equals that row's `key_hash32`;
- the existing gateway intra-layer-relay policy would allow the forward.

`mobile_from` alone is insufficient because it historically covers both team and mobile-static traffic. The stored wire-plane discriminator is load-bearing.

Ordinary CRYPTED DMs are excluded. SEALED_RELAY may follow the plaintext rule because its carried seal counter is independent, but its inner bytes and seal counter must remain verbatim. Its current production delegation path remains unchanged.

## 4. First-hop identity

For an eligible home-boundary receive, the `_seen_origins` plaintext key replaces the redundant home-origin component with the verified hosted-mobile hash:

```
mobile-static namespace | mobile_hash32 | dst8 | ctrM16
```

The existing static, team and CRYPTED key shapes remain unchanged. The completed-flight cache remains keyed by the immediate local-id sender and needs no new field.

Required discriminator test: two live hosted mobiles, same `ctrM`, same destination, back-to-back. Both first hops must be accepted and both must produce one outward flight. This test separately proves:

- the completed-flight cache does not swallow mobile B as mobile A's retry; and
- `_seen_origins` does not turn mobile B into a LOOP_DUP.

## 5. Admission and ACK ordering

For a fresh eligible DATA:

1. Preserve the validated incoming identity and hosted-row identity before resetting `PendingRx`.
2. Apply hop-budget and loop-dedup decisions first, as today.
3. Verify that one TX queue row can be reserved.
4. If E2E ACK was requested, reserve one DelegAck row. Expired rows are pruned first; no live row is evicted.
5. If either reservation fails, send a retryable BUSY_RX NACK to the original mobile counter/low nibble and do not ACK, seed the completed cache, record `_seen_origins`, or stage a forward. Emit and count `mobile_ctr_admission_refused`.
6. Stage the hop ACK with the original `ctrM`.
7. Allocate `ctrH = next_ctr(dst)` exactly once.
8. Build the outward `TxItem` from the accepted carrier, changing only `ctr/ctr_lo`, forcing `Plane::GLOBAL`, and preserving origin, destination, type, flags, inner bytes, nonce bytes (normally zero), logical source, previous hop and decremented hop budget.
9. Insert that item into the already-checked queue with a short `next_attempt_ms` hold beyond the post-ACK callback. This consumes the queue reservation before control returns to the main loop; another producer cannot steal it.
10. Mark `PostAck` as already prequeued so its relay/snoop work runs once but it does not enqueue a duplicate.
11. Store the original first-hop completed-flight and `_seen_origins` identities, then run the normal post-ACK timer.

The BUSY_RX NACK is preferred to silence because the sender already has a bounded same-hop wait/retry path for it. If the resource remains unavailable through that bounded lifetime, the existing retry/give-up machinery reports `send_failed`; there is no unbounded wait and no false success.

## 6. DelegAck strengthening

Keep the eight-row bounded ring. Do not add counter state to `_mobile_reg[]`.

Each row has three states: free, reserved and active. Reorder/reuse fields so the row remains the same measured size if possible:

- `mobile_hash`;
- `ctrM`;
- `ctrH` (active only);
- timestamp;
- one peer discriminator plus its kind;
- active layer;
- state.

The peer field is phase-dependent:

- reserved: the requested outbound target (node id for B251 by-id transit, stable hash for MOBILE_SEND/hash delegation);
- active: the return-side discriminator (same-layer ACK origin id, or cross-layer ACK source hash).

Lookup is therefore `(mobile_hash,ctrH,return-kind,return-peer,layer)`. This prevents equal destination-scoped counters for different destinations from cross-correlating and prevents cross-layer local-id aliases. The entry still carries `ctrM` and its timestamp.

Ring policy is exact and shared by reservation and direct puts:

1. prune expired rows;
2. refresh an exact existing row;
3. use a free row;
4. refuse when every row is live;
5. never evict a live row.

Only E2E-ACK-requesting sends reserve or activate a row. A real inbound MOBILE_SEND wrapper with E2E requested reserves before its hop ACK too; later immediate or parked resolution finalizes that reservation. A failed/given-up delegation releases it. Legacy white-box/direct helper calls may perform an immediate active put, but that put is also non-evicting and reports failure.

## 7. Reverse ACK

At the hosted-mobile last-mile fork:

- parse `acked_ctr = ctrH` from the E2E ACK body;
- derive return kind/key from the frame: cross-layer SOURCE_HASH when present, otherwise `PostAck::origin`;
- include `active_layer_id()` in the lookup;
- on an exact hit, rewrite only the two acked-counter body bytes to `ctrM`, consume the row, and last-mile the ACK;
- on a miss, leave the body unchanged, preserving direct-send behavior.

The last-mile ACK frame's own transport counter is unrelated and remains untouched.

## 8. Encryption proof

Required controls:

- a normal CRYPTED mobile flight keeps its original outer counter on every relay;
- its RTS identity is the nonce-derived digest, so it does not deterministically alias a plaintext `(home,ctrM)` flight;
- mutating a CRYPTED flight's counter while leaving its seal and nonce seed untouched makes validation/open fail;
- SEALED_RELAY, if exercised, keeps its internal `seal_ctr`, ciphertext and tag byte-exact while only the independent outer counter changes.

## 9. Tests and mutation controls

At minimum:

1. Exact pre-fix B251 reproduction: canonical home type-8 answer at ctr 1, followed by hosted-mobile plaintext transit at ctrM 1, previously yielding downstream `already_received`.
2. Fixed path: first hop/ACK use ctrM 1; outward RTS and DATA use a distinct ctrH; delivery succeeds; returned E2E ACK is last-miled as ctrM 1.
3. Two mobiles, same ctrM and same destination: both first hops accepted and exactly two distinct outward home flights.
4. One mobile to two destinations with coincident destination-scoped ctrHs: reverse ACKs map to the correct ctrMs without cross-correlation.
5. Duplicate first-hop RTS/DATA and lost-CTS/lost-ACK retry: one ctrH allocation and one outward DATA.
6. Full live correlation ring: BUSY_RX/refusal before hop ACK, no live eviction, refusal counter increments, retry path stays bounded.
7. Same first-hop with no E2E request consumes no ring row.
8. Team-plane traffic unchanged.
9. Ordinary static traffic unchanged.
10. Ordinary CRYPTED DM counter unchanged; counter-rewrite negative validation test.
11. Existing same-layer delegated and cross-layer delegated ACK translations remain correct with the strengthened key.

Mutation controls must turn RED when:

- translation is removed;
- live/current node state is used instead of the accepted flight's stored mobile identity;
- the first-hop mobile discriminator is removed;
- return destination/kind/layer is removed;
- a live mapping is evicted;
- the queue or ring admission check is moved below the hop ACK;
- a duplicate allocates another ctrH;
- CRYPTED traffic is translated.

## 10. Diagnostics and documentation

- Add a saturating or naturally wrapping counter exposed on USB serial `status` as `ctrrefuse=` (exact spelling to be pinned by tests) and an `MR_EMIT("mobile_ctr_admission_refused",...)` event.
- Update protocol documentation and B251/B161 status in the maintained bug register and baseline note.
- No frames-table layout change is required.
- Add one M2 bench residue: two real hosted mobiles concurrently send plaintext E2E-ACK-requesting DMs with equal first-hop counters through one home; both arrive, both ACKs correlate, and `ctrrefuse=0` under ordinary capacity.

## 11. Acceptance gate

1. Native wrapper plus direct native binary; all B251 mutations RED.
2. s22 restored to 8/8 deliveries and zero assertions.
3. All 36 corpus scenarios green; s18 exactly matches the current `simulation/BASELINE.md` keystone.
4. Attribute every B161 stream movement individually. Do not edit an anchor merely to accept B251 failure.
5. Warning census.
6. Owner-approved two-board gate, sequentially.
7. `git diff --check`; no staged files and no commit.

## 12. Non-goals

- No RTS, CTS or DATA layout/length increase.
- No new wrapper for ordinary by-id plaintext mobile traffic.
- No routing redesign.
- No durable application-dedup change.
- No ordinary encrypted-counter rewrite.
- No global counter namespace.

## 13. Implementation result (2026-08-27)

The implementation follows the approved counter boundary. The accepted-flight carrier retains the verified hosted-
mobile identity through PostAck; direct by-id admission precedes the first-hop ACK; one prequeued forward owns `ctrH`;
and reverse correlation is reserved/activated without live eviction. Non-E2E delegated traffic now emits the truthful
`deleg_originated` evidence instead of pretending that it consumed a `DelegAck` row.

The QG admission correction makes the scope of that statement explicit. Direct by-id transit prequeues before its hop
ACK. A `MOBILE_SEND` hash wrapper still has the separate B112 first-hop-ACK limitation, but it reserves required
correlation before the ACK and now requests `SendDispatch` from `send_by_hash()`. Only `queued` emits
`deleg_originated` and activates the row; `parked` keeps the reservation until a later real admission; `refused`
releases it and emits neither evidence nor a mapping. The real queue-full regression proves the refused row is reusable.

Measured coder gate:

- native: 2240 cases / 96469 assertions / 0 failures;
- B251 mutation batteries: 13 RED / 0 unusable, including restoration of "non-zero counter means admitted";
- delivery analyzer: 194/194 checks, s22 at 8/8 logical deliveries;
- full corpus: 36/36 green, zero assertions; s18 exact at `9868cad3` / 269905;
- warning census: PASS at 173/178/177/177/182/182, zero `-Wswitch`;
- `lus` md5: `d891a974d2e075f29227924f5bce3b22`;
- `sizeof(Node)` 222008 and `sizeof(DelegAck)` 24, both unchanged from the B161 input state;
- no B251 RTS/CTS/DATA length, NV-layout or timer-capacity change;
- corpus anchor table untouched.

Independent QG passed and B251 is closed. The named two-real-mobile metal residue remains scheduled but does not block
this disposition. B161's final closure bookkeeping is complete; the later final audit closes B157, and the owner's
one-time B182 acceptance closes B153. No commit was made for this slice when this historical result was recorded.
