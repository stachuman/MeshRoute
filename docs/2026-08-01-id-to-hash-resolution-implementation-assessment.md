<!-- Author: OpenAI Codex -->
# id → hash resolution — implementation assessment

**Date:** 2026-08-01  
**Reviewed:** implementation of S1, S2 and S2b from
`docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md`, plus the coder's poison-matrix report  
**Disposition:** **changes requested** — S2 is acceptable; S1 and S2b have blocking correctness/integration gaps.

This is a separate review artifact. It does not modify the design, implementation, baseline, bug register or companion
contract.

## 1. Findings

### P1 — S2b is not upgrade-only across an ID rehome: a claim can still evict an authoritative binding

The matching-row change is correct but incomplete. For an existing row with the **same ID and same hash**, a claimed
observation no longer overwrites confidence, provenance or first-hand liveness
(`lib/core/node_hashlocate.cpp:108-136`). However, `id_bind_set` also enforces the reverse uniqueness rule, one hash to
one ID. Both accepted paths still call `id_bind_evict_other_hash_holders` without considering confidence
(`lib/core/node_hashlocate.cpp:137` and `:142`).

Consequently this sequence still demotes trusted state:

1. store authoritative `{id=10, hash=H}`;
2. receive claimed `{id=20, hash=H}` from a relayed soft answer;
3. the new-ID path evicts the authoritative ID 10 row, then inserts claimed ID 20.

The result is exactly the class S2b claims to remove: an unverifiable observation displaces authoritative state and
`key_hash_of_id` stops answering for `H`. The poison matrix does not protect this path: its own measurement says the
36-scenario corpus produces **zero claimed bindings**.

**Proposed solution**

- Before reverse-holder eviction, inspect the existing holder and its confidence.
- Refuse a claimed rehome when the same hash is held authoritatively under another ID; retain the authoritative row and
  emit the existing conflict telemetry or a specifically named rehome-conflict event.
- Continue to permit authoritative rehome, and decide explicitly whether claimed→claimed replacement is newest-wins.
- Add a native test for authoritative `{10,H}` followed by claimed `{20,H}`. Assert that ID 10 remains authoritative,
  ID 20 is absent, and the authoritative reader still answers. Keep the existing authoritative rehome test as the
  positive control.

Until this is fixed, B46 and S2b should not be marked closed.

### P1 — the checked-in companion's team operation no longer guarantees the team plane

The firmware intentionally changes bare `reqpubkey <id>` from implicit TEAM to AUTO. The checked-in companion still
defines `reqPubkeyTeam` as implicitly team-scoped and emits the bare form:

- `ios-companion/MeshRouteKit/Sources/MeshRouteWire/Command.swift:89-90`
- `ios-companion/MeshRouteKit/Sources/MeshRouteWire/Command.swift:150`
- pinned by `CommandEncoderTests.swift:76` as `reqpubkey 9`

That is not an additive/backward-safe contract change. With both namespaces populated, the app now receives
`err_ambiguous_plane`; with only a static binding, it can select the static plane despite the API operation being named
`reqPubkeyTeam`.

**Proposed solution**

- Change the companion encoding to `reqpubkey <id> -t` and update its test and stale comments.
- Add a firmware/app contract fixture for the exact emitted line.
- Update `INBOX_SYNC_CONTRACT.md` with the AUTO/`-s`/`-t` grammar, the new error code and optional result plane.

The Swift decoder itself is tolerant of the additive response fields: unknown JSON keys are ignored and an unknown ack
token maps to `.unknown`. The break is the **outgoing command's changed meaning**, not JSON decoding.

### P1 — `queued` / `reqpubkey_sent` can still claim a transmission that provably did not happen

`Node::on_command` calls the void `emit_hash_query` and then unconditionally returns `queued` with the resolved hash and
plane (`lib/core/node.cpp:1694-1698`). `emit_hash_query` can return without transmitting for at least:

- the self/degenerate target (`lib/core/node_hashlocate.cpp:1586`);
- no crypto identity (`:1587-1589`);
- an off-grid mobile with no global return path (`:1615-1621`);
- codec failure (`:1623-1625`).

BLE converts every such `queued` result into `reqpubkey_sent` (`src/fw_main.cpp:490-497`). This directly contradicts
the serializer/contract meaning that an on-air request was flooded. B47 records only the off-grid-mobile case, but the
no-identity case disproves the source comment and contract claim that it keeps an error acknowledgement. The existing
native no-identity test checks telemetry and no frame, but discards the `CmdResult`, so it does not detect the false
success (`test/test_node_hashlocate.cpp:1451-1461`).

S1 also makes the off-grid-global case newly reachable from the by-ID form. Calling the defect pre-existing does not
make the new public path safe.

**Proposed solution**

- Make the query-originator return a small outcome (`sent`, `no_identity`, `no_return_route`, `degenerate`,
  `encode_failed`) and map it to an honest `CmdResult`; or add equivalent preflights in the reqpubkey command arm as a
  deliberately narrow S1 fix.
- Emit `reqpubkey_sent` only for `sent`.
- Distinguish the hosted-mobile local-cache success from an on-air send; it already generates `peer_key_cached`, so it
  must not additionally claim that a query was flooded.
- Strengthen tests to assert the command result and the BLE-visible disposition, not only `h_tx` absence. Pair every
  negative fixture with a same-fixture successful flight, preserving the coder's good non-vacuity discipline.

The broader B39 result redesign may remain separate, but these reqpubkey-specific false-success branches are locally
knowable and need not wait for it.

### P2 — exact same-hash dual-plane occupancy bypasses D9 and can make forced `-t` fail

S1 decides ambiguity from `peer_book_by_id`'s mask. That resolver suppresses the team bit when the team and static rows
have the same hash:

```cpp
if (team_key_of_id(id, th) && !(mask && th == h)) {
    ...
    mask |= kPeerBookTeam;
}
```

(`lib/core/node_hashlocate.cpp:599-605`)

This was a display de-duplication choice, but it is not suitable as a plane-presence result at an airtime boundary.
When the same numeric ID is populated in both planes by the same identity:

- bare AUTO silently selects static instead of returning D9's ambiguity;
- explicit `-t` sees `has_team == false` and returns `err_no_binding`, even though the team binding exists.

The new D9 test covers only different hashes (`test/test_node_hashlocate.cpp:2846-2878`), so the exact-duplicate branch
is untested. D9 says **both possible** requires a flag and explicit plane selection must be honoured; hash equality does
not make the routes or return paths equal.

**Proposed solution**

- Make `peer_book_by_id` report presence in both planes even when the hashes match. It already has two output rows and a
  mask, so no row-list duplication is required.
- If display callers still want identity de-duplication, perform that only while rendering, not in the shared
  resolver's presence mask.
- Add a same-ID/same-hash dual-plane test: bare form is ambiguous, `-s` sends static, and `-t` sends team.

### P3 — documentation state contradicts the implemented slice

These are not runtime blockers, but should be corrected before landing:

- The spec still says `No code written` at line 4.
- The spec links the prior review at `docs/2026-08-01-id-to-hash-resolution-design-review.md`, while the worktree places
  it under `docs/archive/`.
- D8 correctly says `IdBindSource::manual` lands with `confirmid` in S5 (`:247-248`), but the S2b slice row still includes
  it (`:357`). The implementation's decision not to add a producerless enum is correct; the slice row is stale.
- The bug register calls the companion changes all backward-safe, but omits the outgoing bare-ID semantic break in the
  P1 finding above.

## 2. What is correct

- The parser's hash-versus-ID rules, `-s`/`-t` mutual exclusion and AUTO encoding are internally consistent.
- Carrying the resolved hash and selected plane in `CmdResult` removes the second BLE-side resolution and is the right
  U1 fix. Appending the field avoids silently shifting the 17 positional aggregate initialisers; the 4-byte temporary
  cost is acceptable.
- The different-hash dual-plane ambiguity case refuses before airtime, and the forced-plane cases are properly pinned.
- S2's static route-only pass is confined to `include_id_rows=true`, de-duplicates against actual `_id_bind`
  membership, preserves a foreign-key collision on our numeric ID, and renders ID-only rows without inventing a
  `(claimed)` assertion.
- S2b's **matching-row** no-demotion/no-liveness-refresh rule is correct and well tested.
- The poison probes are disciplined and their structural conclusions are credible: S1's by-ID arm and S2's firmware
  caller are outside simulator reach, while the clean corpus has no claimed-tier producer. The report correctly avoids
  treating byte identity as behavioral coverage in those cases.

## 3. Independent verification

- Existing native test binary: **1108/1108 cases, 72042/72042 assertions passed**.
- `pio run -e native`: passed.
- `pio run -e gateway -e xiao_sx1262 -e heltec_v3`: all three passed.
  - gateway: RAM 81.5%, flash 57.1%
  - xiao_sx1262: RAM 70.9%, flash 62.8%
  - heltec_v3: RAM 64.2%, flash 36.1%
- `git diff --check`: passed.
- The 36-scenario sweep and poison mutations were assessed from the recorded report/baseline, not independently rerun.
- Companion Swift tests could not be run in this environment because the `swift` executable is unavailable; the
  encoder mismatch is directly visible in source and its checked-in test expectation.

## 4. Recommended landing order

1. Complete the S2b trust rule across reverse-holder eviction and add the claimed-rehome mutation test.
2. Fix query outcome propagation so `reqpubkey_sent` means a query was actually emitted.
3. Preserve both plane bits for same-hash dual-plane occupancy and add the missing D9 edge test.
4. Update the companion team encoder to append `-t`, then update its contract/tests.
5. Correct the stale spec/register metadata.
6. Re-run native tests, the three board builds, the companion package tests, and the 36-scenario gate. Re-probe S2b
   with both same-row demotion and cross-ID claimed rehome poisons.

