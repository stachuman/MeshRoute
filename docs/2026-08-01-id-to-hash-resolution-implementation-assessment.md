<!-- Author: OpenAI Codex -->
# id → hash resolution — implementation assessment

**Date:** 2026-08-01  
**Reviewed:** implementation and rework of S1, S2 and S2b from
`docs/superpowers/specs/2026-08-01-id-to-hash-resolution-design.md`, plus the coder's poison-matrix report  
**Disposition after sixth review:** **TX3 code is accepted; changes are requested only for contradictory current
documentation before the bundled landing. S1/S2/S2b remain acceptable as scoped.** The ring-full and deferred
DeviceHal-admission cases are now implemented, tested and poison-proven. The remaining gate work is to remove stale
claims that success means literal airtime or that the new cases are untested.

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

## 7. Fourth quality gate

### Resolution of the third-pass blocker

The command-specific blocker is resolved under the owner's clarified contract:

- `CmdResult::aired` is now `accepted`, and the companion contract correctly defines `reqpubkey_sent` as acceptance by
  the Node/DeviceHal TX path rather than proof of physical airtime.
- The immediate path propagates `schedule_lbt_defer` and `lbt_complete`; `tx_with_retry` now inspects `TxResult` and
  distinguishes `handed`, `deferred_retry_armed` and `rejected` without breaking the DATA timeout recovery rule
  (`lib/core/node_mac.cpp:1451-1499`).
- A synchronous LBT-ring or DeviceHal rejection maps to `err_tx_queue_full` and suppresses `reqpubkey_sent`.
- The two new native fixtures are non-vacuous: each rejected path has an accepting same-fixture control, and the late
  deferred rejection is independently observable in the test HAL.

On that narrow result, **S1 is acceptable**. S2 and S2b remain acceptable from the preceding gates. The blocker below
belongs to the separately registered, bundled **global** TX-path closure (B50), not to by-ID resolution itself.

### P1 — B50 is not global: `tx_flood()` still converts definitive drops into success and can retire channel state

The register says `tx_with_retry` is a function every TX path goes through and closes B50 globally
(`docs/2026-07-30-open-bug-register.md:793-817`). `tx_flood()` disproves both statements:

- on a short busy interval it discards `schedule_lbt_defer(...)`'s boolean and returns `true`, including when the
  four-slot defer ring is full (`lib/core/node_mac.cpp:1325-1331`);
- on a clear channel it calls `_hal.tx(...)`, discards `TxResult`, and returns `true`, including when DeviceHal's
  eight-slot queue rejects the frame (`:1334-1336`).

This is not merely a false telemetry result. `emit_beacon()` interprets that boolean as `sent` and commits the selected
channel digests under an explicit “ACTUALLY AIRED” invariant (`lib/core/node_beacon.cpp:517-525`). The commit increments
`bcn_ad_count` and can clear/retire a dirty digest (`lib/core/node_channel.cpp:890-907`). Therefore either definite
drop above can consume the advertisement horizon for a beacon that was never retained anywhere.

There is a second timing edge: a successfully deferred beacon commits its digest immediately, before the timer calls
DeviceHal. If that later call rejects, `tx_deferred_lost` is emitted but the already-consumed digest state is not rolled
back. Existing air-honesty coverage drives only the long-busy `tx_flood -> false` branch
(`test/test_node_channel.cpp:713-730`); it cannot see defer-ring exhaustion, immediate HAL rejection or late deferred
rejection.

**Proposed solution**

- Return the real `schedule_lbt_defer` result and inspect the immediate `_hal.tx` result. Do not close B50 until all
  direct `_hal.tx` callers have been classified; `tx_flood` is the definite missing caller.
- Make beacon digest commit follow the chosen admission boundary. A bounded implementation is to carry the selected
  digest IDs in the relevant `DeferredLbt` slot and commit them only when deferred `lbt_complete` succeeds. Immediate
  beacons commit only after DeviceHal accepts them; ring-full and HAL rejection leave the digest dirty. If the intended
  invariant is literal radio airtime instead, the commit must move to a DeviceHal/radio-start callback—the synchronous
  Node return cannot prove it.
- Add three native cases with positive controls: full LBT ring, immediate DeviceHal rejection, and accepted defer
  followed by DeviceHal rejection. In every rejection case assert that the digest remains dirty and its advertisement
  count is not burned; poison both discarded-result sites.

This blocks the **global B50 closure** and the bundled landing as currently documented. It does not reopen the corrected
`reqpubkey` result path.

### P2 — the claimed operator-visible late-loss report is debug-only on hardware

The deferred-loss branch calls `MR_EMIT`, which is stripped on device, plus `_hal.log`
(`lib/core/node.cpp:1235-1253`). The register and source call that “no silent loss” and operator-facing. On metal,
however, the installed log sink prints only while `g_mr_trace_on` is enabled (`src/fw_main.cpp:538-542`); normal
`debug off` operation remains silent. The TestHal log assertion does not model that gate.

**Proposed solution:** choose one honest contract. If a late accepted-frame loss may yield “answer later, or nothing,”
describe `tx_deferred_lost`/`_hal.log` as debug telemetry and remove the no-silent-loss claim. If operators must be
notified, add an unconditional console/app-visible asynchronous failure signal (with correlation deferred to B39 if
necessary). This is diagnostic/product-contract work, not a reason to reject S1's synchronous acceptance result.

### P3 — acceptance rename left contradictory comments and register text

The live companion contract's new block is directionally correct, but several nearby sources still say “aired,” “will
fly,” or “on-air request was flooded,” notably `lib/core/node.h:115-124`, `:1508-1511`, the register's summary at
`docs/2026-07-30-open-bug-register.md:587-594`, and B47 at `:759-762`. Those claims are precisely what the owner ruling
removed. Reconcile them in the same documentation pass as B50 rather than preserving two definitions of success.

### Fourth-pass independent verification

- Fresh native tests: **1117/1117 cases, 72202/72202 assertions passed**.
- `pio run -e native`: passed. The pre-existing misleading-indentation warning at
  `lib/core/node_hashlocate.cpp:1243` remains.
- `pio run -e gateway -e xiao_sx1262 -e heltec_v3`: all passed.
  - gateway: RAM 81.5%, flash 57.3%
  - xiao_sx1262: RAM 70.9%, flash 62.9%
  - heltec_v3: RAM 64.2%, flash 36.1%
- `git diff --check`: passed.
- The checked-in baseline reports 36/36 scenarios and the documented TX capability poison; the scenario runner was
  not independently rerun in this pass.
- Swift remains unavailable, so companion tests were not executed.

**Fourth-pass landing order:** fix and poison-test both `tx_flood` rejection sites; make deferred beacon digest commit
follow the admitted frame; correct the debug-only and stale acceptance claims; then rerun the same gates. The by-ID
resolution slices themselves need no further redesign.

## 8. Fifth quality gate

### Resolution of the fourth-pass findings

- **Immediate `tx_flood` HAL rejection: fixed correctly.** The direct `_hal.tx` result now maps non-`ok` to `false`,
  preserving the digest (`lib/core/node_mac.cpp:1338-1349`). The paired native control is sound.
- **LBT-ring result propagation: fixed in code.** The busy path now returns `schedule_lbt_defer(...)` directly
  (`:1325-1336`). The coverage blocker below remains.
- **Direct HAL-call sweep: now correctly scoped.** There are four direct `_hal.tx` sites in `lib/core`; the register's
  classification of the two timeout-recovered sites, `tx_flood`, and `tx_with_retry` is credible.
- **Hardware late-loss visibility: fixed in source.** `node.cpp` marks the message `!!`, and the `fw_main` sink prints
  that prefix even with trace disabled (`src/fw_main.cpp:538-556`). The bench script now explicitly requires a
  `debug off` check. This is acceptable as bench-owed hardware validation.

### P1 — the load-bearing beacon ring-full fix still has no test

The implementation and baseline explicitly say the attempted fixture never reached a beacon defer: all beacons were
skipped for exceeding `_flood_lbt_max_defer_ms`. Consequently, poisoning the newly changed return at
`node_mac.cpp:1336` reddens no test. The S1c query fixture proves the shared ring can fill, but it cannot prove this
caller's return propagation or the channel-digest consequence.

This is straightforward to make deterministic; no production timing search is required. `NodeConfig` exposes
`flood_lbt_max_defer_ms` specifically for an explicit override.

**Proposed fixture**

1. Set `lbt_enabled=true`, `quiet_threshold_ms=0`, `flood_lbt_max_defer_ms=1000`, and
   `channel_dirty_max_advertisements=5`.
2. Create one dirty digest and install a direct neighbour that has not advertised holding it, preventing the
   no-neighbour holder-coverage path from retiring it early.
3. Set `busy_until = now + 100`; fire four beacon timers and assert four `tx_lbt_defer` events, zero
   `tx_lbt_defer_dropped`, and a still-dirty digest.
4. Fire a fifth beacon and assert one ring-full drop and that the digest remains dirty.
5. Fire deferred slot 0 to free capacity, then fire another beacon; assert the accepted advertisement reaches the
   configured horizon and retires the digest. This is the positive/recovery control.
6. Poison `return schedule_lbt_defer(...)` back to unconditional `true`; the fifth-beacon assertions must fail.

Until that test exists and the poison reddens it, B51's synchronous ring-full half is not gate-complete.

### P1 — approved transmitter-admission boundary is not yet implemented

The current choice commits a deferred beacon's digest immediately when the frame enters `_deferred_lbt`. If DeviceHal
rejects it when the timer fires, one advertisement count has already been consumed and is not rolled back. The coder
correctly reports this residual and the measured +64-byte cost of carrying three selected IDs through four slots.

The `reqpubkey_sent` ruling does not automatically settle this state machine: it defined an app event as **accepted**,
whereas the channel code still says the digest side effect occurs only for advertisements that **ACTUALLY AIRED**
(`lib/core/node_beacon.cpp:523-525`, `node_channel.cpp:890-895`, and `node.h:1307`). Extending the app-event ruling to
this independently load-bearing retirement rule is a design decision, not an implementation detail.

**Owner ruling, 2026-08-02:** for channel-digest accounting, “sent” means **accepted by the transmitter/DeviceHal**—the
strongest boundary the current architecture can observe. It does not mean literal RF airtime. This selects the deferred
DeviceHal-admission design, not commit-on-entry to the Node's LBT ring.

**Required implementation**

- Carry the selected digest IDs in the relevant deferred LBT slot (or equivalent bounded metadata).
- Do not commit them when `schedule_lbt_defer` accepts the frame.
- On the timer path, commit only if deferred `lbt_complete` reaches `_hal.tx` and DeviceHal answers `ok`; rejection
  leaves the count and dirty state unchanged.
- Immediate beacons continue to commit after their direct `_hal.tx` returns `ok`.
- Add paired late-rejection/acceptance tests and poison the deferred completion result.
- Rename “ACTUALLY AIRED” comments to the precise transmitter-admitted boundary. A later
  `DeviceHal::pump_tx` radio-start error is outside this achievable guarantee and must not be described as covered.

The current commit-on-LBT-entry behavior therefore remains a blocking mismatch with the approved contract.

### P3 — the acceptance cleanup is still incomplete

Stale current-tense claims remain in `src/fw_main.cpp:490-502`, `lib/core/node.h:1513-1516`, the digest comments cited
above, and the register summary at `docs/2026-07-30-open-bug-register.md:587-594`. Test comments at
`test/test_node_hashlocate.cpp:3428-3444` also retain `aired`/“will fly.” The baseline corrects the operator-log claim
at line 91 and repeats the old false version at line 97. These are non-runtime defects but should be reconciled with
the transmitter-admission ruling rather than adding another historical correction layer.

### Fifth-pass independent verification

- Fresh native tests: **1118/1118 cases, 72208/72208 assertions passed**.
- `pio run -e native`: passed; the pre-existing `node_hashlocate.cpp:1243` warning remains.
- `pio run -e gateway -e xiao_sx1262 -e heltec_v3`: all passed.
  - gateway: RAM 81.5%, flash 57.3% (464388 bytes)
  - xiao_sx1262: RAM 70.9%, flash 62.9% (510500 bytes)
  - heltec_v3: RAM 64.2%, flash 36.1% (1206752 bytes)
- `git diff --check`: passed.
- The recorded baseline reports 36/36 scenarios, zero failures and no movers; the scenario runner was not independently
  rerun in this pass.
- Swift remains unavailable, so companion tests were not executed.

**Fifth-pass landing order:** add and poison the deterministic beacon ring-full fixture; implement and test the approved
transmitter-admission digest boundary; reconcile the remaining acceptance/airtime wording; then rerun the same gates.

## 9. Sixth quality gate — TX3

### Verdict

**TX3 runtime implementation: GO. Bundled landing: documentation cleanup required.** No TX3 runtime blocker was found.
The concurrent B43 work is explicitly outside this verdict; no B43 source or behavior was assessed here.

### Resolution of the fifth-pass blockers

- **Beacon ring-full propagation is now covered.** The deterministic native fixture fills all four shared defer slots,
  distinguishes the fifth beacon's `ring_full` rejection through `beacon_tx.result`, keeps the digest dirty, and has a
  recovery control (`test/test_node_channel.cpp:773-843`). The reported poison of the return propagation reddens this
  test, so it is not merely exercising the shared ring in another caller.
- **Deferred digest commit now occurs at the approved boundary.** A deferred slot carries the three selected digest
  IDs. The timer path commits them only after `lbt_complete` admits the frame to `DeviceHal`; a late rejection leaves
  the digest untouched (`lib/core/node.cpp:1232-1262`, `lib/core/node_mac.cpp:1311-1361`). Immediate beacons likewise
  commit only after direct `_hal.tx` success.
- **The paired late-result test is non-vacuous.** The same fixture proves both a rejected deferred completion that does
  not commit and an accepted completion that does (`test/test_node_channel.cpp:856` onward). This directly exercises
  the state transition that was missing in the fifth pass.
- **Deferred-slot lifecycle is bounded and reset-safe.** All three IDs are overwritten on every allocation, including
  zeroing for frames with no digest metadata, so recycled slots cannot inherit a previous beacon's commit set
  (`lib/core/node_mac.cpp:1235-1252`). Zero is an unambiguous sentinel because channel message IDs are generated with a
  nonzero origin byte.
- **Layer ownership remains coherent.** The deferred record does not carry a layer, but production cannot switch the
  active layer while any deferred LBT slot is occupied: `layer_swap_blocked()` checks the ring, and the existing
  dual-layer test pins that rule (`lib/core/node.cpp:765-775`, `test/test_dual_layer.cpp:1456`). The later ID lookup
  therefore commits against the same active channel state from which the IDs were selected.
- **The cost is smaller than the earlier estimate.** Three IDs per four slots increase `Node` by 48 bytes; the board
  builds remain within their existing budgets. The checked-in baseline records only the expected defer-timing
  movements and reports all scenarios passing. That baseline was reviewed but not independently regenerated here.

### P2 — current documentation contradicts the completed TX3 behavior

These are not runtime defects, but they should be corrected before the bundle is described as closed:

- `test/test_node_channel.cpp:844-854` says the late edge is deliberately untested, that accepted defer commits at ring
  entry, and that implementation awaits an owner ruling. The implemented paired test starts immediately below that
  stale block. Replace it with the approved DeviceHal-admission contract or remove the obsolete history.
- `docs/2026-07-31-bench-test-script.md:195` says the beacon ring-full variant has **no automated test**. Point it to
  the deterministic native case at `test/test_node_channel.cpp:773` instead.
- `lib/core/node.h:1337-1340` still assigns digest commit to `emit_beacon` and calls the boundary “actually aired” /
  “on air.” Commit now occurs in the immediate/deferred transmitter-admission sites. Describe exactly that boundary.
- `test/test_node_channel.cpp:730-732` says only an “AIRED” beacon commits. The tested condition is DeviceHal acceptance,
  not observable RF airtime; use “transmitter-admitted.”
- The same acceptance vocabulary remains inconsistent in `test/test_node_hashlocate.cpp:3428-3436`,
  `docs/2026-07-31-bench-test-script.md:190`, and `docs/2026-07-30-open-bug-register.md:594`, where accepted work is
  described as “will fly,” “really aired,” or “actually aired.” Reconcile these with the owner-approved contract.

The B51/TX3 history in the bug register may retain rejected alternatives if clearly labelled historical, but its
current contract and acceptance criteria should not preserve the superseded meaning alongside the correction.

### Boundary and residual scope

For this gate, “sent” means handed to `DeviceHal` and accepted by it. It does not claim that a later
`DeviceHal::pump_tx` or radio-start operation produced literal RF airtime. This is the strongest observable guarantee
approved by the owner and is implemented consistently by TX3. No additional radio-driver acknowledgement was required.

### Sixth-pass independent verification

- An isolated native build's doctest executable passed **1126/1126 cases and 72348/72348 assertions**.
- Isolated `gateway`, `xiao_sx1262`, and `heltec_v3` builds all passed. The observed gateway and Heltec V3 figures were
  RAM 81.5% / flash 57.3% and RAM 64.3% / flash 36.1%, respectively.
- `git diff --check` passed.
- The shared `.pio/build/native` result was discarded after concurrent B43 activity caused PlatformIO to report zero
  cases. Verification used a separate build directory and invoked the resulting doctest binary directly.
- Hashes of the TX3 source and test files were unchanged across the isolated verification. The native total includes
  the concurrently modified worktree snapshot, but this review neither attributes those extra cases to TX3 nor grants
  B43 a quality verdict.
- The simulation report was inspected, not rerun. Swift/companion tests were not relevant to the TX3 digest path and
  were not run.

**Sixth-pass landing order:** correct the contradictory current comments and bench/register claims, then land TX3. No
further TX3 production-code change is required by this review.
