# Review — id → hash resolution design

**Reviewed document:** `docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md`  
**Review date:** 2026-08-01  
**Review basis:** current uncommitted specification and current firmware sources  
**Disposition:** revise before implementation  

## 1. Executive assessment

The design's central trust distinction is correct:

- a full public key is self-verifying against its hash;
- an on-air assertion that an 8-bit address belongs to a hash is not self-verifying;
- therefore a by-ID answer must enter the ID-binding store as `claimed`, not `authoritative`.

D4 identifies a real and dangerous implementation trap. Passing the existing owner-answer boolean unchanged into
`send_hash_bind_response` would select `DATA_TYPE_AUTHORITATIVE_H_ANSWER`, and the current receiver would map that to
`IdBindConf::authoritative`. D1 is also load-bearing: `key_hash_of_id` currently hard-rejects every non-authoritative
row, so a claimed binding needs an explicit reader floor before it can be displayed or used by `reqpubkey`.

The specification is not implementation-ready, however. The principal gap is that D4 describes only the ordinary
H-answer path, while the user-facing operation that needs unresolved by-ID discovery is `reqpubkey <id>`, whose
`WANT_PUBKEY` response uses a different data type and a different receiver. The team-plane receiver also currently
refuses to store an H answer at all. Consequently, the stated “zero receiver changes” and end-to-end S4 flow do not
hold across both planes.

The `PeerKeyConf::overheard` observation is correct in the current tree: it is used as a conservative initializer and
display value, but there is no producer that inserts a peer key at that confidence. This design is right not to assign
that separate problem to the ID-binding work.

## 2. Findings

### F1 — P1: unresolved `reqpubkey <id>` has no specified completion path

Current behavior:

- `Node::on_command(CmdKind::reqpubkey)` first resolves a decimal ID to a hash and returns `err_no_binding` if it
  cannot (`lib/core/node.cpp:1643-1648`);
- only after a hash exists does it call `emit_hash_query(... hard=true, want_pubkey=true, ...)`
  (`lib/core/node.cpp:1659`);
- an owner receiving a `WANT_PUBKEY` query does **not** call `send_hash_bind_response`; it calls
  `send_hash_bind_pubkey_response` (`lib/core/node_hashlocate.cpp:929-947`);
- that response is always `DATA_TYPE_AUTHORITATIVE_H_ANSWER_PUBKEY`
  (`lib/core/node_hashlocate.cpp:1030-1040`);
- its receiver caches only hash → pubkey and does not write ID → hash
  (`lib/core/node_hashlocate.cpp:1043-1055`).

D4's proposed `send_hash_bind_response(... authoritative=false)` therefore does not govern a by-ID
`WANT_PUBKEY` response. S4 does not say whether by-ID `reqpubkey` is one exchange or two, how it waits for the ID
answer, or how its second step is correlated and timed out.

**Proposed resolution:** specify a two-stage command flow if retaining the “no new response type” goal:

1. An unresolved, explicitly plane-selected `reqpubkey <id>` records a bounded pending `resolve-id-for-pubkey`
   intent and emits a by-ID H query with `want_pubkey=false`.
2. The ordinary H answer lands ID → hash as `claimed`.
3. Matching that answer consumes the pending intent and emits the existing HARD `WANT_PUBKEY` query by the returned
   hash.
4. The existing pubkey response self-verifies and fills hash → pubkey as `authoritative`.
5. A bounded timeout produces an explicit `reqpubkey` failure and clears the pending intent.

This is not prohibited auto-resolution: the operator explicitly requested `reqpubkey`; the second query is the
completion of that command. It preserves the current pubkey answer semantics and still requires only one new query
flag and no new DATA type. The specification must record that it costs two query/answer exchanges when the ID binding
is initially absent.

The alternative is a one-round by-ID+pubkey answer, but that requires either response metadata/new type or a receiver
rule that can distinguish the unverifiable ID assertion from the self-verifying pubkey. That design is not present.

### F2 — P1: D4's “zero receiver changes” is false on the team plane

The current destination receiver explicitly writes H answers only when `team_plane == false`
(`lib/core/node_hashlocate.cpp:1067-1077`). Team answers are deliberately not redirected into `_team_keys` because
that store currently lacks confidence (`:1072`, `:1228-1248`). The relay/snoop receiver applies the same refusal
(`:1249-1254`).

D2 fixes the reason for that refusal by adding confidence to `TeamKey`, but S3/S4 never explicitly reconnects either
receiver to the team store. Without that work, a by-ID team answer is parsed and logged but remains invisible to
`team_key_of_id`, `peer_book_by_id`, and all subsequent commands.

**Proposed resolution:** replace D4's global claim with the narrower statement:

> The existing ordinary answer type already maps to `claimed`; no new receiver-side trust codepoint is needed.
> Static ingestion can reuse the current mapping. Team ingestion must be added after S3 gives `_team_keys` a
> confidence dimension.

S4 must then specify destination and snoop behavior separately:

- destination of a team answer: `team_key_set(id, hash, source=h_query, confidence=claimed)`;
- relay observation, if retained: `source=h_relay`, `confidence=claimed`;
- D3 still forbids those cached rows from **answering** a future by-ID query; storage for display/lookup does not make
  the relay an answer authority;
- neither path may update `_team_peer` or manufacture team membership. A team binding is usable only when the
  existing membership/route gate says the ID is a team peer.

### F3 — P1: the D1 confidence floor is not propagated to every reader or to the generated view

Adding `min` only to `key_hash_of_id` and `team_key_of_id` is insufficient after `_team_keys` can contain claimed
rows:

- `team_id_of_key` is the reverse reader and currently accepts every fresh team row
  (`lib/core/node_routing.cpp:852-858`). A claimed row would become visible to send-by-hash unless this function also
  has an authoritative default floor.
- `peer_book_by_id` currently calls both accessors without a floor and hard-codes the static row as authoritative
  (`lib/core/node_hashlocate.cpp:526-545`).
- `PeerBookRow` has `static_authoritative` but no team-binding confidence field
  (`lib/core/node.h:756-780`). It cannot fulfil D5's requirement to display a team claim **labelled as a claim**.
- the proposed accessor signatures return only a hash, not the confidence actually selected. The generated view
  cannot label the result without a second table lookup or another API change.

**Proposed resolution:** make the confidence contract complete and symmetric:

```cpp
bool key_hash_of_id(uint8_t id, uint32_t& out,
                    IdBindConf min = IdBindConf::authoritative,
                    IdBindConf* actual = nullptr) const;
bool team_key_of_id(uint8_t id, uint32_t& out,
                    IdBindConf min = IdBindConf::authoritative,
                    IdBindConf* actual = nullptr) const;
bool team_id_of_key(uint32_t hash, uint8_t& out,
                    IdBindConf min = IdBindConf::authoritative,
                    IdBindConf* actual = nullptr) const;
```

`peer_book_by_id` should either accept its own floor or explicitly request `claimed`, populate both actual confidence
values, and expose both on `PeerBookRow`. Existing operational callers keep the default authoritative floor. Display
and `reqpubkey` explicitly request claimed. Tests should enumerate every reader of both stores so a new reader cannot
silently bypass the confidence policy.

### F4 — P1: `bindid` is not a protected or durable trust anchor under the proposed deferral of O2

Current static ID bindings are RAM-only and expire after the configured 48-hour TTL. They are cleared with routing
state. Team keys are also RAM-only. There is no current NV encoding of `IdBindConf`; the persistent confidence
encoding cited elsewhere belongs to `PeerKeyConf`, not `IdBindConf`.

More importantly, `id_bind_set` does not implement upgrade-only confidence:

- a same-ID/same-hash update always writes the incoming `source` and `confidence`, so a later claimed observation can
  demote an authoritative manual promotion (`lib/core/node_hashlocate.cpp:102-104`);
- a conflicting authoritative beacon/H answer can overwrite any non-self authoritative row (`:59-100`);
- no `IdBindSource` value identifies an operator assertion (`lib/core/node.h:110-114`).

Thus D6's typed value can age out, disappear on reboot, be demoted by a matching claimed answer, or be replaced by a
later authoritative network observation. Calling it a trust-anchor write while deferring O2 overstates its property.

**Proposed resolution:** choose one of two honest products:

1. **Ephemeral confirmation:** rename it to `confirmid` or document `bindid` as RAM-only/TTL-bound, make store updates
   upgrade-only, add an operator source for diagnostics, and do not call it pinned or a trust anchor.
2. **Persistent trust anchor:** add `IdBindConf::pinned`, an operator source, plane-scoped NV storage, boot restore,
   list/delete operations, capacity policy, and rules that pinned rows never age, demote, evict, or accept a conflicting
   on-air overwrite.

Recommendation: do not defer O2 while simultaneously shipping D6 as a trust anchor. Either defer D6 with O2, or ship
the smaller ephemeral operation under explicitly weaker semantics. A third enum value does not itself force an NV
change because ID bindings are not presently persisted; persistence is a separate product decision.

For the team form, manual binding must not set `_team_peer`. It should refuse or remain unusable when the ID is not an
already known/routable member, rather than manufacturing membership from an operator-entered hash.

### F5 — P1: the mixed-version safety conclusion is incorrect

An old receiver ignores the new `H_FLAG_BY_ID`, but it does not necessarily remain harmless:

- it interprets bytes 2–5 as a normal 32-bit hash. A low-valued real or adversarially generated hash can match;
- more generally, an old relay reconstructs the forwarded H frame from the flags it understands
  (`lib/core/node_hashlocate.cpp:963-970`, `lib/core/frame_codec.cpp:594-606`). It cannot preserve `BY_ID`, so a valid
  by-ID query becomes an ordinary hash query after the first old relay and fails silently.

Adding `by_id` to the new dedup key does not solve a flag stripped by old firmware. Therefore “old receiver matches
nothing, stays silent” is neither a protocol guarantee nor enough for multi-hop mixed-fleet operation.

**Proposed resolution:** either:

- bump/gate the wire version and declare by-ID resolution available only when the participating mesh is on the new
  version; or
- explicitly require a coordinated fleet upgrade and state that mixed-version paths are unsupported.

A new codepoint that old firmware drops rather than forwards would also work, but that is a different wire design.
The current one-flag design cannot provide transparent mixed-version forwarding.

### F6 — P1: a claimed DST_HASH is not merely a fail-safe recipient rejection

O3's premise is incomplete. A recipient that sees a different `DST_HASH` does not just reject the plaintext DM. It
invokes L2c misdelivery recovery (`lib/core/node_mac_rx.cpp:1074-1078`), which:

- forwards immediately if it already has an authoritative binding for the stamped hash; or
- parks the DM and emits a HARD H query for that hash (`lib/core/node_join.cpp:438-470`).

If a false claimed ID → hash is stamped, the true ID recipient can therefore redirect the plaintext DM to the owner
of the false hash. It also creates additional H traffic and can enter collision-recovery logic. The hash is no longer
just a rejection guard; it becomes a routing instruction.

**Recommendation for O3:** do **not** stamp `DST_HASH` from a claimed binding. Keep the authoritative default on both
`key_hash_of_id` and `team_key_of_id` for the send path. Claimed bindings may support display and explicit pubkey
inspection, but they must not drive L2c redirection.

### F7 — P2: O1 reuse is sound only with canonical validation and honest in-memory naming

Reusing bytes 2–5 is preferable to appending a byte, but the wire contract must state more than “low byte contains
the ID.” Otherwise multiple 32-bit values can name the same 8-bit ID while occupying different dedup keys, allowing
redundant floods and inconsistent telemetry.

**Recommendation for O1:** reuse the existing four-byte query slot, with these requirements:

- `BY_ID` means bytes 2–5 encode a zero-extended ID;
- bytes 3–5 must be zero on pack and parse;
- IDs 0 and 255 are rejected;
- the dedup key includes `by_id` and uses the canonical zero-extended value;
- forwarders preserve the bit and canonical value;
- introduce a neutral in-memory name such as `query_key32`, or explicit `query_id()` / `query_hash()` accessors, so
  handlers do not repeatedly cast a misleading `key_hash32` field;
- define whether `HARD` is forbidden/ignored for `BY_ID` (owner-only answering already supplies the strongest possible
  by-ID lookup under this trust model);
- retain all current `WANT_PUBKEY`, team, and mobile length validation.

### F8 — P2: S1 does not define plane selection for a numeric ID that exists in both planes

`peer_book_by_id` deliberately returns a mask because the same number may identify different static and team peers.
`reqpubkey`, however, must choose one query plane. Adding `-s` while retaining implicit `-t` does not define what a
bare decimal does when both rows exist—or what it does before an unresolved by-ID query has produced either row.

**Proposed resolution:** specify the grammar and ambiguity behavior explicitly:

- `-s` and `-t` are mutually exclusive and force the corresponding plane;
- if no flag is present and exactly one locally known plane is possible, use it;
- if both are possible, return a distinct ambiguous-plane error and require a flag;
- for an unresolved ID on a dual-plane node, require `-s` or `-t` rather than guessing;
- a static-only node may safely default to static; a team-only/off-grid context may safely default to team;
- the command acknowledgement must echo the selected plane and later the resolved hash.

This preserves the “mask, not winner” invariant at the mutation/airtime boundary, where choosing the wrong row has
more consequence than a display error.

### F9 — P2: D4 should separate owner detection from binding confidence

The current local variable `authoritative` means “this resolver matched as the owner” and controls answer type and
telemetry. D4 changes the answer confidence for a by-ID owner response without changing the fact that the responder
is the owner. Reusing one boolean for both meanings invites a later branch to make the wrong decision, especially in
the `WANT_PUBKEY` and mobile-proxy paths.

**Proposed resolution:** model two facts explicitly:

- `answered_by_owner` — selects owner-only behavior and pubkey possession;
- `binding_verifiable` (or `answer_confidence`) — selects plain versus authoritative ID-binding answer.

For a by-hash self-match both are true. For a by-ID self-match, `answered_by_owner=true` while
`binding_verifiable=false`. This makes D4 structural rather than dependent on passing a surprising `false` at one
call site.

## 3. Answers to the four open questions

### O1 — query key

**Agree with reuse**, subject to F7's canonical encoding and internal naming requirements. It costs no bytes and the
flag correctly types the existing query slot. The `ttl_or_next_hop` precedent supports overloaded wire storage, but
it also supports giving the handler an explicit semantic accessor rather than treating an ID as a hash throughout
the code.

### O2 — pinned `IdBindConf`

**Do not defer it if `bindid` is intended to be a durable trust anchor.** The current stores are volatile and the
current update logic does not protect a manual authoritative row. Either defer `bindid` with pinned persistence, or
ship an explicitly ephemeral confirmation operation with upgrade-only rules. The claim that adding the enum value
alone reaches an existing ID-binding NV encoding is not supported by the current tree.

### O3 — claimed DST_HASH on plaintext DM

**No. Keep the authoritative floor.** A mismatch invokes redirect and HARD-H recovery; it is not a simple local
reject. A malicious or wrong claimed hash can redirect the plaintext DM toward the wrong hash owner and generate
additional control traffic.

### O4 — verb name and BLE availability

`bindid` is understandable if the operation is genuinely persistent/pinned. `confirmid` is more honest for a
RAM-only, expiring promotion. In either form, require an explicit `-s` or `-t` whenever the plane is not uniquely
determined.

BLE inclusion is acceptable **as a first-class structured companion operation**, because the current BLE link is
already treated as the MITM-passkey authenticated admin transport, exposes `peerkey` installation, and falls through
to destructive console operations (`src/fw_main.cpp:483-515`). Excluding only this verb would not create a coherent
security boundary. The companion flow should:

- show plane, ID, old hash, new hash, and resulting confidence;
- require explicit user confirmation on conflict;
- return a typed JSON success/conflict/error result;
- never rely on the generic text-console fallback for the acknowledgement.

If the product decides BLE is not strong enough for trust-anchor writes, that ruling should be applied consistently
to `peerkey`, identity regeneration, factory reset, and the other existing admin operations—not only to `bindid`.

## 4. Required specification changes before dispatch

1. Add the complete unresolved `reqpubkey <id>` state machine, including bounded pending state, second-stage query,
   correlation, timeout, and acknowledgements.
2. Amend D4 to distinguish reuse of the confidence codepoint from the receiver changes required for the team store.
3. Add confidence floors to every forward and reverse reader; add team-binding confidence to `PeerBookRow` and all
   display/JSON contracts that promise claim labelling.
4. Decide whether D6 is ephemeral confirmation or persistent pinning, then specify overwrite, demotion, aging,
   eviction, reboot, capacity, conflict, list, and deletion behavior accordingly.
5. Resolve mixed-version operation: version gate/coordinated upgrade, or a different codepoint old firmware drops.
6. Record the O3 decision as “claimed never stamps DST_HASH.”
7. Define canonical BY_ID encoding and invalid flag/value combinations.
8. Define bare-ID plane ambiguity and explicit `-s`/`-t` behavior.
9. Add an operator source value or equivalent provenance; do not mislabel manual bindings as beacon/query learns.
10. Separate owner detection from response confidence in the implementation contract.

## 5. Suggested revised slicing

| Slice | Scope | Reason |
|---|---|---|
| S1 | Local `reqpubkey` resolver parity, `-s`/`-t`, ambiguity errors; no new wire | Fixes defect A without entangling trust or flooding. |
| S2 | Static-route diagnostic pass and self-skip | Pure view fix for C/D. |
| S3 | Confidence-aware `TeamKey`, upgrade-only conflict rules, all forward/reverse floors, view/renderer confidence | Makes claimed team state representable and safely observable before a producer exists. |
| S4a | Canonical BY_ID H query, owner-only answer, dedup/forward preservation, static+team receiver ingestion, version gate | Delivers one testable ID → claimed-hash mechanism. |
| S4b | Pending `reqpubkey-by-id` intent and second-stage existing pubkey query | Closes the actual operator workflow without overloading the pubkey answer's trust semantics. |
| S5 | Manual confirmation/pinning and optional persistence/companion contract | Keeps the operator trust decision and its NV/product policy attributable. |

## 6. Minimum gates

- static by-ID owner answer lands `claimed`, never `authoritative`;
- team by-ID owner answer lands in `_team_keys` as `claimed`, never `_id_bind`;
- a relay/cached binding never answers a by-ID query;
- a claimed same-hash refresh cannot demote an authoritative or pinned row;
- claimed forward and reverse lookups fail under the default authoritative floor;
- `hashof`, `peers`, and `reqpubkey` can explicitly read claimed and label both planes correctly;
- a claimed row never stamps DST_HASH and never drives L2c redirect;
- by-ID+`reqpubkey` completes both stages or produces one bounded timeout;
- IDs 0/255 and non-zero upper query bytes are rejected;
- by-ID and by-hash dedup entries cannot alias;
- a forward preserves BY_ID across every new-firmware hop;
- a mixed-version test demonstrates the selected version-gate or coordinated-upgrade behavior;
- the same numeric ID in static and team planes requires or honours explicit plane selection;
- manual conflicts preserve and report both hashes; the selected persistence/aging semantics survive their declared
  lifecycle;
- `sizeof(TeamKey)`, `sizeof(Node)`, and per-board RAM deltas are measured rather than inferred.

## 7. Final verdict

Approve the **trust model and direction**, including D1's confidence floor, D3's owner-only by-ID answers, and D4's
use of plain H answers for an unverifiable ID assertion. Do not dispatch S3/S4 from the current text. Revise the
end-to-end command flow, team ingestion, confidence propagation, manual-binding semantics, and mixed-version claim
first. Those are design obligations, not implementation details.
