<!-- Author: OpenAI Codex -->
# id → hash resolution — implementation assessment

**Date:** 2026-08-01  
**Reviewed:** implementation and rework of S1, S2 and S2b from
`docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md`, plus the coder's poison-matrix report  
**Disposition after third review:** **changes requested** — the LBT defer-ring refusal is now propagated correctly,
and S2/S2b remain acceptable. S1 still has one blocking hardware-path gap: `reqpubkey_sent` can be emitted when the
separate DeviceHal outbound queue rejects or later drops the H frame.

This is a separate review artifact. It does not modify the design, implementation, baseline, bug register or companion
contract.

## 0. Second review after rework

### Original-finding resolution

| original finding | second-review result |
|---|---|
| claimed cross-ID rehome evicts an authoritative holder | **Resolved.** A fresh authoritative reverse holder now refuses a claimed rehome; authoritative rehome, claimed→claimed newest-wins, self-protection and expiry are covered. |
| companion `reqPubkeyTeam` emits a bare AUTO id | **Resolved in source.** It now emits `reqpubkey <id> -t`, with the exact line pinned by its Swift test. Swift remains unavailable in this environment, so the package test was not executed here. |
| silent `emit_hash_query` bail-outs become `reqpubkey_sent` | **Resolved for all four pre-transmit bail-outs and the hosted-mobile cache hit.** The new outcome mapping and BLE predicate are correctly tested. The transmit-admission finding below remains after those checks. |
| same-hash occupancy in both planes loses the team bit | **Resolved.** The resolver retains both presence bits; AUTO refuses ambiguity and explicit TEAM/GLOBAL selection is covered. |

### P1 — `aired=true` still survives a definitive LBT defer-ring drop

The new outcome closes the four early exits, but it stops one layer too soon. `emit_hash_query` calls the **void**
`tx_initiating` and unconditionally returns `HQueryOutcome::sent` (`lib/core/node_hashlocate.cpp:1688-1690`). When LBT
or NAV says the channel is busy, `tx_initiating` calls `schedule_lbt_defer` but discards its boolean result
(`lib/core/node_mac.cpp:1100-1106`). The shared ring has four slots; when they are occupied,
`schedule_lbt_defer` emits `tx_lbt_defer_dropped` and returns false (`:1216-1230`). No frame is handed to the radio and
none is retained for later transmission.

The failure nevertheless propagates as:

1. `HQueryOutcome::sent`;
2. `CmdResult{queued, ..., aired=true}` (`lib/core/node.cpp:1712-1727`);
3. BLE `reqpubkey_sent` (`src/fw_main.cpp:490-502`).

That directly violates the spec's binding rule that `reqpubkey_sent` is emitted only when a frame actually left
(`docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md:397-405`) and contradicts the `CmdResult::aired`
contract (`lib/core/command.h:138-146`). It is not merely a naming objection: the implementation has positively
decided to drop this exact frame.

The new tests do not cover the boundary. Every S1b reqpubkey outcome fixture sets `cfg.lbt_enabled = false`
(`test/test_node_hashlocate.cpp:3265-3365`), so `schedule_lbt_defer` cannot be reached.

**Proposed solution**

- Make `tx_initiating` report whether the frame was handed immediately or accepted into the defer ring. In the busy
  branch, return the existing `schedule_lbt_defer` result instead of discarding it.
- Add an `HQueryOutcome` value for transmit admission failure and map it to an honest retryable command error. A named
  `err_tx_busy`/`err_tx_queue_full` is clearer than reusing an unrelated error.
- Define the synchronous contract honestly. A successfully deferred frame has not *aired yet*; if the intended event
  means "accepted for immediate or deferred transmission", rename `aired` accordingly and amend the event contract.
  If it must literally mean "radio transmission happened", emit the success asynchronously from the eventual
  completion path instead.
- Add a native test with a busy channel: occupy all four shared defer slots, issue reqpubkey as the fifth initiating
  frame, assert `tx_lbt_defer_dropped`, no transmit, a non-success `CmdResult`, and no BLE-visible sent disposition.
  Drain one slot and repeat as the positive control.
- Poison the return propagation (force the failed admission to success) and require that test to redden.

Until this is fixed, B47 should not be marked closed and S1 should not be accepted.

### P3 — the CmdCode self-labelling test has already gone stale

`CmdCode` now has values 0 through 11, but the invariant loop still iterates only `v < 10`
(`test/test_console_json.cpp:883`). It therefore excludes both newly appended errors while its comment says the walker
"cannot go stale unnoticed." The explicit mapper checks currently make the live names correct, so this is not a
runtime blocker, but the generic invariant no longer proves what it claims.

**Proposed solution:** derive the upper bound from the append-only enum (or add an explicit count sentinel handled by
the exhaustive switch) rather than copying a numeric literal. At minimum, update the bound to include values 10 and
11.

### P3 — landing and companion documentation still describe the pre-rework behavior

- The spec status table still says S1/S2b rework is in flight and repeats all four now-resolved findings
  (`docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md:4-18`).
- `ios-companion/INBOX_SYNC_CONTRACT.md:173` still says a bare team-local ID is implicitly TEAM; it is now AUTO.
- The same contract's reqpubkey section still documents only the hash form and says the no-identity path keeps an
  "existing error ack" (`:372-385`), the premise the implementation report correctly disproved.
- The contract and Swift model do not yet name `err_ambiguous_plane` / `err_no_identity` or retain the optional plane.
  Forward-compatible decoding prevents a crash, but both codes collapse to `.unknown`, losing their distinct remedies.

These documentation/model updates are non-blocking for the firmware mechanics but remain required integration work.

## 1. First-review findings (historical; resolved by the rework above)

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

## 3. First-pass independent verification

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

## 4. First-review landing order (superseded)

1. Complete the S2b trust rule across reverse-holder eviction and add the claimed-rehome mutation test.
2. Fix query outcome propagation so `reqpubkey_sent` means a query was actually emitted.
3. Preserve both plane bits for same-hash dual-plane occupancy and add the missing D9 edge test.
4. Update the companion team encoder to append `-t`, then update its contract/tests.
5. Correct the stale spec/register metadata.
6. Re-run native tests, the three board builds, the companion package tests, and the 36-scenario gate. Re-probe S2b
   with both same-row demotion and cross-ID claimed rehome poisons.


## 5. Second-pass independent verification

- Fresh native tests: **1114/1114 cases, 72126/72126 assertions passed**.
- `pio run -e native`: passed.
- `pio run -e gateway -e xiao_sx1262 -e heltec_v3`: all three passed.
  - gateway: RAM 81.5%, flash 57.2%
  - xiao_sx1262: RAM 70.9%, flash 62.9%
  - heltec_v3: RAM 64.2%, flash 36.1%
- `git diff --check`: passed.
- The coder's updated baseline records **36/36 scenarios**, zero assertion failures and no movers. The simulation sweep
  was assessed from that recorded baseline and was not independently rerun in this pass.
- Companion Swift tests could not be run because `swift` is unavailable. The corrected outgoing `-t` line and its
  exact checked-in expectation were verified by source inspection.

**Second-pass landing order:** fix and poison-test transmit admission first; then correct the CmdCode invariant and
the stale spec/companion documentation. Re-run the native, hardware, Swift and 36-scenario gates after that change.

## 6. Third quality gate

### Resolution of the second-pass findings

- **LBT defer-ring drop: resolved as scoped.** `tx_initiating` now returns the existing
  `schedule_lbt_defer` result; `HQueryOutcome::tx_dropped` maps to `err_tx_ring_full`; the fifth-frame test fills all
  four slots, proves the refusal and BLE disposition, then proves recovery.
- **CmdCode self-labelling invariant: resolved.** It now walks the entire uint8 range and filters through the
  exhaustive, `-Wswitch`-guarded `ord()` mapper, so appended enumerators are enrolled automatically.
- **Companion documentation: partially resolved.** The new top section correctly records AUTO, mandatory `-t`, the
  optional plane and resolved hash. Some closure/status and legacy sections remain stale, noted below.

### P1 — the separate DeviceHal TX queue can still reject a frame reported as `aired`

S1c propagates the Node's four-slot LBT defer-ring result, but the disposition still stops above the real hardware
enqueue. The clear-channel path is:

1. `tx_initiating` calls `lbt_complete` and then returns true unconditionally
   (`lib/core/node_mac.cpp:1110-1123`);
2. the H query uses `LbtKind::flood`, which becomes non-retryable `FrameTag::beacon`
   (`:1251-1275`, `:1347-1360`);
3. `tx_with_retry` discards `_hal.tx(...)`'s `TxResult` and returns true (`:1451-1477`);
4. `emit_hash_query` therefore returns `sent`, and the command returns `aired=true`.

On hardware, `DeviceHal::tx` has a separate eight-entry outbound queue. When full it increments `txq_drops`, returns
`TxResult::busy`, and **does not retain the frame** (`lib/hal/device_hal.cpp:10-28`,
`lib/hal/device_hal.h:91-100`). Because beacon/H frames have no stash retry, this is a definitive drop followed by
`reqpubkey_sent`.

The successful-defer premise is also too strong. A frame accepted into `_deferred_lbt` is handed to `DeviceHal::tx`
only when its timer fires. The hardware queue can become full during that wait, so "scheduled and will fly" is not
guaranteed. Even a successful DeviceHal enqueue is not literal airtime: `pump_tx` may later receive `radio_error` from
`start_transmit` and drop the entry (`device_hal.cpp:31-46`).

Current native coverage cannot see this: `test_node_hashlocate.cpp`'s HAL always appends the bytes and returns
`TxResult::ok` (`:36-43`). The new test proves only the Node LBT ring.

**Proposed solution**

- Decide the contract precisely:
  - if `reqpubkey_sent` means **accepted/scheduled by the Node TX path**, rename/document `aired` accordingly and stop
    claiming physical airtime; or
  - if it means **the radio actually started transmission**, emit it asynchronously from the hardware/simulator handoff
    that knows that fact. A synchronous `CmdResult` cannot prove a future deferred transmission.
- In either contract, propagate synchronous HAL admission failure: make `tx_with_retry` inspect `TxResult`, return
  false for `busy`/`too_long`/`radio_error`, propagate the flood result through `lbt_complete` and the immediate
  `tx_initiating` path, and map it to an honest retryable result.
- Since two different bounded queues can reject this command, consider renaming the still-uncommitted
  `err_tx_ring_full` to `err_tx_queue_full` (or use a general `err_tx_rejected`) and make the operator hint avoid
  claiming it was specifically the LBT defer ring.
- For a deferred H whose later HAL enqueue is busy, either retry/re-defer it or emit a later failure; otherwise the
  synchronous "will fly" assertion remains false.
- Add two native controls with a scriptable `TxResult`: idle-channel immediate HAL rejection, and a successful LBT
  defer whose timer later meets HAL rejection. Both must produce no sent disposition; pair them with `TxResult::ok`
  controls and poison the result propagation.

Until the contract and the DeviceHal rejection path agree, B47/S1 should not be marked closed.

### P3 — closure documentation is internally stale

- The spec top still describes the previous four blockers as current (`docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md:13-23`), while §5.1 still calls the now-implemented S1c work "in flight" and describes
  `tx_initiating` as void (`:415-424`).
- The bug register marks B47 closed and says the event fires only when a frame actually aired
  (`docs/2026-07-30-open-bug-register.md:724-739`), which the hardware queue path disproves.
- The companion's new review block still says `err_tx_ring_full` is pending (`ios-companion/INBOX_SYNC_CONTRACT.md:137-170`), while the later legacy reqpubkey section still documents only the hash form and repeats the disproven
  "existing error ack" claim (`:414-427`).

These are non-blocking documentation defects, but they should be corrected with the final contract ruling rather than
adding another layer of contradictory status text.

### Third-pass independent verification

- Fresh native tests: **1115/1115 cases, 72171/72171 assertions passed**.
- `pio run -e native`: passed.
- `pio run -e gateway -e xiao_sx1262 -e heltec_v3`: all passed.
  - gateway: RAM 81.5%, flash 57.2%
  - xiao_sx1262: RAM 70.9%, flash 62.9%
  - heltec_v3: RAM 64.2%, flash 36.1%
- `git diff --check`: passed.
- The recorded baseline reports 36/36 scenarios, no movers and zero assertion failures; it was not independently
  rerun in this pass.
- Swift remains unavailable, so the checked-in encoder expectation was inspected but not executed.

**Third-pass landing order:** resolve the event semantics and propagate/recover DeviceHal rejection; add immediate and
post-defer HAL-busy tests; then reconcile the spec, register and companion contract before rerunning all gates.
