# B161 — canonical origin for typed hash answers · design specification · 2026-08-27

**Status:** ✅ **CLOSED / COMBINED B251–B161 QG PASS — FINAL BOOKKEEPING COMPLETED 2026-08-27.** The approved
canonical wrapping is implemented and mutation-covered. The required 36-row gate exposed the hosted-mobile/home
counter collision recorded historically in §11; B251 corrected that boundary, restored all 36 scenarios and has now
passed independent QG. B112 remains a separate, non-blocking first-hop-ACK issue. B157 and B153 are now ready for
explicit re-evaluation; neither closes by implication.

**Owner ruling:** normalize DATA types **1, 2, 8 and 13** onto the existing standard plaintext-unicast inner envelope by adding its one-byte canonical `origin`; keep type 5 unchanged; do not change RTS or CTS lengths; do not add a legacy/raw compatibility parser. After this correction, re-run the hybrid-RTS safety and system gates before closing B157 and B153.

**Parent design:** `docs/superpowers/specs/2026-08-08-hybrid-rts-flight-identity-design.md` remains authoritative for the 10/11-byte RTS identity, the 6/7-byte terminal CTS, cache semantics and implicit-forward credit. This document completes that design's B161 residue. It does not redesign the already-landed hybrid wire.

**Frame authority:** `docs/frames.md`.

---

## 1. Problem

The hybrid RTS plaintext identity is the exact tuple:

```text
origin[8] | ctr_hi[8] | ctr_lo[8]
```

The identity must be derivable from the DATA that follows. `Node::flight_identity()` therefore reads the origin through `parse_unicast_inner()` rather than trusting `TxItem::origin`.

Most DATA producers already build the standard inner:

```text
[dst_hash?] [cross-layer path?] [origin] [source_hash?] [location?] [body]
```

Two direct producers do not:

1. `send_hash_bind_response()` emits types 1, 2 and 8 with a bare `hash_bind_inner`;
2. `send_mobile_pubkey_answer()` emits type 13 with a bare mobile `hash_bind_inner`, pubkey and name.

Consequently `parse_unicast_inner()` interprets the first body byte, `target_layer`, as the DATA origin. The current hybrid implementation is internally symmetric only because both RTS production and receive validation repeat that accidental interpretation. It is not a valid origin identity.

The original B161 shorthand called these “two typed-answer families.” The complete producer audit makes the actual wire scope explicit: **types 1, 2, 8 and 13**. The measured hybrid census observed the mismatch on types 2 and 8; absence of types 1 and 13 from that corpus is not evidence that their structurally identical construction is correct.

### 1.1 Why this blocks B157 and B153

An implicit-forward credit or terminal completed-flight CTS is safe only when the identity denotes the actual flight. Leaving typed answers keyed by `target_layer` creates a type-dependent definition of `origin`, so the universal safety claim for B157 cannot be made and B153 cannot close.

### 1.2 Non-goals

This slice does not:

- change routing policy, route penalties or retry policy;
- redesign B158 jitter or fix B166 NAV pricing;
- fix B159 long-horizon application DATA de-duplication;
- change H-query, channel M, FLOOD, ACK or NACK formats;
- add a DATA type, flag, version or compatibility discriminator;
- migrate type 5, which is already canonical;
- replace the dedicated hash-answer admission path with the generic app-DM enqueue path.

---

## 2. Approved wire change

Only the four typed DATA answers below grow, by exactly one byte. Their type-specific **body** stays byte-identical. The new byte is the standard unicast envelope's `origin` immediately before that body.

### 2.1 Complete frame table

The sizes below include the 8-byte DATA header, one TYPE byte and the 4-byte plaintext MAC trailer.

| DATA type | current typed inner | canonical typed inner | complete DATA |
|---|---|---|---:|
| 1 `H_ANSWER` | `[target_layer][node_id][key_hash32]` = 6 B | `[origin]` + same 6-B body = 7 B | 19 → **20 B** |
| 2 `AUTHORITATIVE_H_ANSWER` | same 6-B body | `[origin]` + same body = 7 B | 19 → **20 B** |
| 8 `MOBILE_H_ANSWER` | `[target_layer][home][key_hash32][epoch]` = 7 B | `[origin]` + same 7-B body = 8 B | 20 → **21 B** |
| 13 `MOBILE_H_ANSWER_PUBKEY` | mobile binding 7 B + `ed_pub[32]` + `name_len` + `name[N]` = `40+N` B | `[origin]` + same body = `41+N` B | `53+N` → **`54+N` B** |

For type 13, `0 ≤ N ≤ 32`, so the complete frame changes from 53–85 B to **54–86 B**. This also corrects `frames.md`, whose current type-13 row omits the already-transmitted `[name_len][name]` tail.

Type 5 `AUTHORITATIVE_H_ANSWER_PUBKEY` is unchanged. It already uses `enqueue_data()` and therefore already airs:

```text
[DATA header][TYPE=5][dst_hash?][origin][pubkey-answer body][MAC]
```

### 2.2 Byte example

The canonical type-2 DATA is:

```text
[DATA fixed header: 8]
[TYPE=AUTHORITATIVE_H_ANSWER: 1]
[origin: 1]                         NEW STANDARD ENVELOPE BYTE
[target_layer: 1]                  existing body begins
[node_id: 1]
[key_hash32 LE: 4]
[plain MAC: 4]
```

`hash_bind_inner` remains the six/seven-byte **body codec**. Do not redefine it to include origin: it is a type-specific body, and changing that helper would make its existing callers ambiguous. The enclosing producer calls `pack_unicast_inner()` around the body.

### 2.3 Origin value

The new byte must come from the existing `stamp_origin(item, plane, dst)` authority:

- TEAM flight: `team_local_id()`;
- GLOBAL/static flight from a static node: `_node_id`;
- GLOBAL/static flight from a registered mobile: its routable home id, with the existing mobile-origin mark;
- current fallbacks and refusal policy remain those owned by `stamp_origin()`.

No producer may infer origin from `target_layer`, `to_origin`, `node_id`, the immediate next hop, `meta.src_hint`, or simulator metadata.

### 2.4 RTS and CTS consequences

RTS frame lengths remain exactly:

- 10 B for plaintext unicast;
- 11 B for encrypted unicast;
- 9 B for M broadcast;
- 43 B for FLOOD.

For the four affected plaintext answers:

- RTS byte 6 `payload_len` increases by one;
- RTS bytes 7..9 become `[stamped origin][ctr_hi][ctr_lo]` instead of `[target_layer][ctr_hi][ctr_lo]`;
- all addressing and plane bits retain their established meaning.

The concrete payload-length changes are:

| type | current RTS `payload_len` | new RTS `payload_len` |
|---|---:|---:|
| 1 / 2 | 10 | **11** |
| 8 | 11 | **12** |
| 13 | `44+N` | **`45+N`** |

These values are `inner_len + plain_mac_len`; they do not include the DATA fixed header or TYPE byte, matching the existing RTS contract.

CTS frame lengths do not change:

- ordinary CTS stays 3 B or 4 B;
- terminal CTS stays 6 B plaintext or 7 B encrypted;
- an ordinary 4-B CTS's rounded NAV length field may change only when the added payload byte crosses its existing four-byte quantisation boundary;
- a terminal plaintext CTS echoes the corrected three-byte identity.

ACK and NACK are unchanged. The MAC trailer retains its existing algorithm and size; its offset moves by one in the affected DATA frames.

### 2.5 Capacity and airtime

No global maximum-body constant changes. These are bounded internal answers, not ordinary user DMs. Type 13's maximum complete size of 86 B is well below the hard frame cap.

The affected DATA costs one additional byte. Depending on SF, CR and the previous LoRa symbol boundary, measured airtime may increase by zero or one encoded symbol block. The gate reports the real per-shape airtime; it must not assume one byte equals one fixed airtime cost.

---

## 3. Producer design

### 3.1 Preserve the dedicated queue path

Do not implement types 1/2/8/13 by routing them through generic `enqueue_data()` merely because type 5 uses it. That would also import generic DM behavior—anti-spam admission, crypto/default flags, telemetry, resolve/park rules and other policy—into a best-effort internal answer path.

The approved construction is:

1. build the existing type-specific body into a temporary buffer;
2. create the existing `TxItem` and select its plane;
3. call `stamp_origin(item, item.plane, to_origin)`;
4. wrap the body using `pack_unicast_inner(... flags=0, origin=item.origin, body=...)`;
5. fail closed if packing returns zero;
6. enqueue through the existing dedicated bounded queue behavior and retain current emits/order.

There must be one shared private helper for the two direct producers; do not leave two field-by-field rituals that can drift. A suitable shape is:

```cpp
bool Node::pack_typed_answer_inner(
    TxItem& item,
    Plane plane,
    uint8_t dst,
    const uint8_t* body,
    uint8_t body_len);
```

The helper may stamp and wrap; it must not enqueue, allocate a counter, emit telemetry or select the DATA type. Those semantics differ between the two existing producers and must remain at their current owners.

### 3.2 Types 1, 2 and 8

`send_hash_bind_response()` already receives `team_scoped`. Its plane stays:

```text
team_scoped ? Plane::TEAM : Plane::AUTO
```

Set that plane before stamping. Build the six/seven-byte `hash_bind_inner` exactly as today, then wrap it as the standard body. `item.origin = _node_id` must disappear; `stamp_origin()` is the sole origin writer.

The H-answer type selection, binding confidence, queue-full behavior, counter allocation, emit names/fields and `become_free()` ordering remain unchanged.

### 3.3 Type 13

`send_mobile_pubkey_answer()` must build this unchanged body:

```text
[mobile hash_bind 7]
[ed_pub 32]
[name_len 1]
[name 0..32]
```

and then wrap it with `[origin]`.

Thread the already-known `h.team_scoped` into `send_mobile_pubkey_answer()` and select `Plane::TEAM` exactly as the sibling type-1/2/8 response does; otherwise select the existing AUTO/global behavior. Do not leave the answer plane to AUTO inference. This changes no frame length and prevents a current or future hosted-mobile answer from silently acquiring a plane-dependent origin. Add a reachable TEAM fixture rather than attempting to prove the path away.

### 3.4 Type 5 remains a positive control

Do not rewrite `send_hash_bind_pubkey_response()`. Tests must show that its canonical layout, origin and complete bytes are unchanged by the implementation. Type 5 is the positive example that the new types are joining.

---

## 4. Consumer design

Every affected consumer must parse the standard envelope once and pass only its `body` to the existing type-specific parser.

### 4.1 Destination consume

In `do_post_ack()`, `ui = parse_unicast_inner(pa.inner, pa.flags)` already exists. The affected branches become:

- types 1/2: require `ui`, then call `on_hash_bind_response(ui.body...)`;
- type 8: require `ui`, then call `on_mobile_hash_bind_response(ui.body...)`;
- type 13: require `ui`, then call `on_mobile_hash_bind_pubkey_response(ui.body...)`.

A missing/invalid canonical envelope is a malformed internal answer: fail closed, do not cache, drain a parked send, deliver bytes to the inbox, or fall back to interpreting `pa.inner` as the legacy body.

Type 5 continues to consume `ui.body` as it does today.

### 4.2 Relay snoop

Types 1/2 are snooped on the forward path. Reuse the already-parsed standard `ui` and pass `ui.body` to `on_hash_bind_snoop()`.

Do not retain the current hand-derived type-5 offset as a model for the new code. Where the standard parser is available, its `body` is the one authority. Whether cleaning the existing type-5 offset in this same slice is byte/behavior-inert must be proven first; otherwise leave it as explicit adjacent debt under C1.

Types 8 and 13 are not currently relay-snooped. This design does not add new snooping behavior.

### 4.3 Forwarding

Relays continue copying the complete `pa.inner` verbatim. They do not unwrap and repack it. This preserves origin, body and identity byte-for-byte across hops.

### 4.4 Semantic strictness and mixed fleets

There is no raw legacy alternative in the type consumer. A post-fix receiver presented with an old raw answer may complete the hop-level exchange before the deferred semantic parser runs, but it must not install/cache/drain from that malformed body.

Mixed pre-/post-B161 fleets are unsupported. Reflash every node in a bench topology together. Per the standing owner ruling for undeployed MeshRoute test hardware, no `wire_version` bump is required.

---

## 5. One identity authority after the fix

After canonicalization:

```text
TxItem.origin
    = stamp_origin(plane, dst)
    = first mandatory origin byte decoded by parse_unicast_inner(DATA inner)
    = plaintext RTS identity byte 0
    = completed-cache / terminal-CTS / implicit-forward origin
```

The comments in `node_mac.cpp` that describe typed answers as a deliberate raw-inner exception must be corrected in place. They are historical explanations of B161, not valid post-fix behavior.

Do not “simplify” `flight_identity()` back to `pt.origin` in this slice. Keeping identity frame-derived is the stronger invariant: the RTS identity is verified against what will actually air. The new tests prove that `pt.origin` and parsed origin agree for every production plaintext-unicast producer, but the comparator remains wire-derived.

---

## 6. Implementation slices

### S0 — controlled baseline and producer/consumer ledger

Before code changes:

1. record the clean native count/hash and applicable current simulator authority;
2. record whether the ruled board gate is green at baseline, with the build logs/census result that establish the answer; an inherited warning or failure is named before S1 rather than attributed after S3;
3. enumerate every direct `TxItem` producer with nonzero DATA type;
4. classify each as canonical-standard, encrypted-standard, raw-special or non-unicast;
5. prove the only raw unicast answer producers are the two named in §1;
6. record corpus counts for types 1, 2, 8, 13 and type 5 separately;
7. make a reproduction assert the current defect: for at least type 2 and type 8, `TxItem.origin != parse_unicast_inner(item.inner)->origin` before the fix.

Zero occurrences require a controlled producer fixture; they do not waive the type.

### S1 — canonical pack and destination consume

Implement the shared wrapping ritual and convert types 1/2/8/13. Convert their destination consumers to `ui.body`. Keep type 5 byte-identical.

This slice owns `frames.md` corrections because the wire changes here.

### S2 — relay and hybrid identity proof

Convert the types 1/2 relay snoop to the canonical body view. Prove the full identity chain through:

- originator RTS;
- originator DATA;
- relay forwarding without repack;
- receiver DATA validation;
- completed-cache terminal CTS;
- implicit-forward credit.

### S3 — system gate and closure

Run the current authoritative simulation/corpus tooling and the required native/board gates under the explicit scope in §8.2. Then:

1. close B161 only if every affected producer and consumer is canonical;
2. re-evaluate and close B157 only if no plaintext typed flight can earn forward credit under a different origin interpretation;
3. close B153 only after the parent design's current acceptance gate is satisfied or an explicit owner disposition records a measured exception;
4. leave B158, B159 and B166 open and separate.

Do not close all three rows merely because unit tests are green.

---

## 7. Required tests and controls

### 7.1 Codec and exact bytes

Pin exact complete bytes and lengths for:

1. type 1 GLOBAL, origin distinct from `target_layer`;
2. type 2 GLOBAL, same discriminator;
3. type 8 GLOBAL, origin distinct from target layer and home id;
4. type 13 with empty name;
5. type 13 with 32-byte name;
6. type 5 before/after exact-byte positive control;
7. TEAM type 1/2/8 with `team_local_id != node_id != target_layer`;
8. type 13 in every production-reachable plane.

For each affected type assert:

- parsed standard origin is correct;
- `ui.body` is byte-identical to the pre-fix type-specific body;
- type-specific parser sees the same target, id/home, hash, epoch, pubkey and name;
- complete DATA length is exactly the value in §2.1;
- RTS remains 10 B and carries the correct `payload_len` and origin identity;
- ordinary/terminal CTS length remains canonical.

### 7.2 End-to-end behavior

Pin independently:

- types 1/2 still update the correct static/team binding tier and drain the intended parked send;
- type 8 still updates only `mobile_home`, never `id_bind`;
- type 13 still verifies/caches the mobile pubkey and name, updates `mobile_home`, never id-binds the leased local id, and drains the parked encrypted send;
- type 5 behavior and bytes remain unchanged;
- a relay snoops types 1/2 from `ui.body` and forwards the original inner unchanged;
- malformed canonical inner produces zero cache/store/drain effects;
- an old raw body cannot be accepted through a fallback;
- TEAM and GLOBAL numeric-id aliases do not cross planes.

### 7.3 Hybrid safety regressions

At minimum:

1. two same-length typed answers with equal destination and counter low nibble but different stamped origins produce different plaintext RTS identities;
2. an identity mismatch prevents DATA acceptance, ACK, completed-cache seed and app/routing effects;
3. terminal CTS for one typed answer cannot clear another;
4. implicit forward of one typed answer cannot clear another;
5. exact retry of the same typed answer still earns the established optimization;
6. full counter remains significant (`0x0002 != 0x1002`);
7. TEAM/GLOBAL plane mismatch remains non-terminal even if numeric origin and counter match.

Every safety assertion requires a mutation or controlled negative that turns red when the new origin prefix, body offset, plane thread or identity comparison is removed.

### 7.4 Structural controls

Add checks that fail if:

- a producer assigns `_node_id` directly instead of calling `stamp_origin()`;
- a consumer passes `pa.inner` directly to one of the four affected body parsers;
- a second raw-vs-canonical compatibility arm appears;
- type 5 is accidentally repacked;
- type 13's name tail is omitted from the frame documentation or test length;
- the RTS/CTS length constants change in this slice.

Prefer compiler/AST or executable controls over substring-only checks where practical. Any mutation matching zero sites is unusable and must fail the harness loudly.

---

## 8. Gates and measurements

### 8.1 Functional gate

- native suite: zero assertion failures;
- the dedicated hash-locate, dual-layer and hybrid-RTS suites: green;
- all new mutation/negative controls: RED when armed, zero unusable;
- no warnings introduced in the approved build scope;
- `git diff --check`: clean.

### 8.2 Size/layout gate

Expected:

- `sizeof(Node)`: unchanged;
- no timer-id growth;
- no queue-capacity or carrier-layout growth;
- stack change limited to small local body/wrap buffers, measured on the affected tests/builds;
- type 13 buffer remains bounded and no duplicate maximum-sized buffers are simultaneously retained unnecessarily.

The owner-approved board-build scope is **two environments only: `heltec_mobile` and `gateway`** (owner ruling 2026-08-18, subsequently re-confirmed as the standing time-preservation rule). Report flash/RAM and warnings for those two builds. Do not expand to the former all-board build gate without fresh owner authority.

This limit does **not** reduce `tools/warning_census.sh`: run that tool at its existing three pinned OLED environments and require its existing object/warning counts plus `-Wswitch == 0`, in accordance with the separate owner ruling that “the census stays as is.” Thus “two board builds” and “the unchanged warning census” are two distinct obligations, not competing interpretations.

Record the exact two build commands, the census result and the before/after figures. A different scope must be stated and owner-authorized, not silently inferred from another slice.

### 8.3 Simulator/system gate

Use the current authoritative delivery tool and current ratified baseline—not the retired historical `732/733` figures embedded in the 2026-08-08 narrative. Report:

- which of 36 rows move and why;
- type 1/2/8/13 occurrence and delivery counts;
- unique deliveries overall and key scenarios including `s06`, `s07` and `s27`;
- assertion failures;
- terminal CTS hits/mismatches;
- implicit-forward credits by type and plane;
- DM and total airtime;
- duplicate application deliveries, while keeping B159 separate.

The one-byte wire change makes byte-identity movement expected where these answers occur. Attribution must prove that the movement is exactly the new origin prefix, corrected RTS identity/payload length, resulting CTS hint/echo where applicable, and downstream consequences. Do not blanket re-anchor unexplained rows.

Type 13 has one additional named movement source: threading the originating H query's `team_scoped` decision into `send_mobile_pubkey_answer()` replaces today's AUTO plane. Where a team-scoped hosted-mobile pubkey answer occurs, its route selection, RTS plane marks and stamped origin may therefore change in addition to the one-byte normalization. Attribute that arm separately; do not describe all type-13 movement as mere byte insertion.

### 8.4 Documentation and register gate

Update in the same landing:

- `docs/frames.md`: type rows, inner-layout prose, complete lengths, type-13 name tail and hash-bind body/envelope distinction;
- parent hybrid design: mark B161 normalization landed and link this completion spec;
- `docs/2026-07-30-open-bug-register.md`: B161/B157/B153 dispositions supported by the actual final gates;
- `tracker.md`: remove the hybrid closure chain from ongoing only when the three rows are genuinely disposed;
- any source comments/tests that still describe raw typed answers as current behavior.

Historical reports remain historical; correct their live-status banners or add explicit supersession links rather than rewriting old measurements.

### 8.5 Metal and reflash-skew gate

Add **bench Part 48 — B161 canonical typed-answer origin** to `docs/2026-07-31-bench-test-script.md` in the same landing. It is small but mandatory:

1. archive/identify both images and reflash every node in the test topology;
2. after all nodes run the new image, perform a real-air H lookup whose answer is type 1 or 2 and prove the requester installs the binding and the routed answer completes without repeated terminal-CTS mismatch;
3. with a hosted mobile available, repeat for type 8 and a WANT_PUBKEY/type-13 answer, proving mobile-home and pubkey/name installation; otherwise record these two arms `not-run`, not pass;
4. inspect the transmitted RTS/DATA logs: RTS remains 10 B, DATA is one byte larger and the logged origin is the responder's plane-correct origin, not `target_layer`;
5. perform one deliberately staggered reflash observation if practical, then finish the fleet update immediately.

The staggered interval is named **B161 reflash skew** and is expected/transient, not a compatibility mode:

- old receiver + new answer: the old consumer reads the new origin byte as `target_layer` and shifts the body interpretation;
- new receiver + old answer: the new consumer removes the old `target_layer` as if it were origin, then rejects the shortened/malformed body;
- after every node is reflashed, either symptom is a failure and must not be normalized as skew.

Do not leave a mixed fleet running merely to exercise this observation.

---

## 9. Acceptance criteria

The work is complete only when all statements below are true:

1. Every production type 1, 2, 8 and 13 DATA carries the canonical standard origin byte.
2. The body following that origin is byte-identical to the former type-specific payload.
3. Every affected destination/snoop consumer reads `ui.body`, with no legacy fallback.
4. Type 5 remains byte-identical and behavior-identical.
5. RTS stays 10/11 B and CTS stays 3/4/6/7 B; only the approved fields/affected DATA lengths move.
6. The origin comes from `stamp_origin()` and agrees with the DATA-exposed origin, RTS identity, terminal CTS and implicit-forward comparator.
7. TEAM and GLOBAL origins remain plane-correct through origin, relay, terminal CTS and cache keys.
8. Malformed/legacy raw answers produce no semantic state change.
9. `sizeof(Node)`, timer capacity and queue/carrier layouts do not grow.
10. The final native, mutation, build and current authoritative system gates pass or any measured exception has an explicit owner disposition.
11. B161 closes first; B157 and then B153 close only on their own demonstrated conditions.

---

## 10. Coder handoff summary

Implement this as a corrective completion of the existing hybrid wire:

```text
existing body codec
    -> stamp_origin(existing plane, destination)
    -> pack_unicast_inner(flags=0, origin, body)
    -> existing dedicated TxItem queue path
```

On receive:

```text
parse_unicast_inner(pa.inner, pa.flags)
    -> require success
    -> existing type-specific parser(ui.body)
```

Do not enlarge RTS/CTS, change type 5, use generic app-DM enqueue for the raw-answer producers, or accept the legacy raw shape. Treat type 13 and its plane/name tail as first-class—not corpus-optional—and finish with the B161 → B157 → B153 closure gate.

---

## 11. Historical implementation result — B251 counterexample (hold lifted 2026-08-27)

The producer/consumer normalization in this specification was implemented as written. Native reached **2,227 cases /
95,998 assertions / 0 failures**. All **15/15** B161 mutations turned RED with zero unusable controls: producer stamp,
raw wrapper, type-13 plane/name, type-5 preservation, strict destination and relay body offsets, legacy fallbacks,
terminal/implicit identity narrowing, and carrier-vs-frame authority.

The mandatory full corpus then falsified the sufficiency premise. In `s22_mobile_team_meshroute`:

1. At 455,177 ms S1 sends the canonical type-8 answer to S2. Its completed-flight key at S2 is
   `from=S1(17), dst=S2(30), plane=GLOBAL, plaintext identity=(origin=17, ctr=1)`.
2. M1 independently originates `mobile_to_static` with its own counter 1. Its home S1 forwards it to S2 using the
   intentionally routable mobile origin 17.
3. The forwarded RTS at 500,019 ms has the **same complete cache key and identity**. Only `payload_len` differs
   (12 for the type-8 answer, 25 for the app DM), and `payload_len` is deliberately not identity.
4. S2 returns a 6-byte terminal `already_received` CTS at 500,196 ms without receiving the DM DATA. M1 later emits
   `send_failed reason=e2e_ack_timeout` at 558,001 ms. Unique deliveries fall **8 → 7** and the scenario records one
   assertion failure (`S2 link_bidi_confirm 7 → 5` is a downstream symptom, not the defect).

The post-build simulator is `b668f0da`. **35/36** rows have zero assertion failures; s18 remains exactly
`9868cad3 / 269905 / 0`. Ten rows move, as recorded in the newest B161 note in `simulation/BASELINE.md`; the corpus
anchor table is deliberately untouched. The board/census tail of the final gate was not run after this blocker.

This was not fixed by restoring the raw typed-answer origin, comparing only a counter fragment, or adding
`payload_len` as though a length were unique identity. B251 therefore introduced the reviewed home-counter boundary
that distinguishes logical originators which legitimately share the same routed origin and independent counters. Its
corrected gate is now green and independent QG has passed, so this implementation hold is lifted and B251 is closed.
B161's final closure bookkeeping is complete. B157 and B153 remain open and are ready for explicit re-evaluation.
B112's broader first-hop-ACK contract remains separately open and does not block B251.

<!-- Author: OpenAI Codex -->
