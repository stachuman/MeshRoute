# Mobile home attachment reliability and lifecycle

**Date:** 2026-08-07  
**Status:** proposed design; no implementation is claimed  
**Scope:** the mobile↔static-home attachment FSM, host OFFER scheduling, presence confirmation,
candidate-home monitoring, and hosted-row expiry. The J and P wire formats remain unchanged.

## 1. Outcome

Home attachment must work for the normal bench and product order:

1. one or more mobiles start while no static home exists;
2. a normal single-layer static node is provisioned later;
3. every mobile that is seeking a home attaches without an operator having to repeat `mobile register`;
4. the attached mobile continuously evaluates its current home and viable alternatives;
5. both sides converge on the same registration state after loss, reboot, roaming, or a dropped CLAIM.

Automatic *initial* attachment is a policy, not a simulation prerequisite. A manual `mobile register`
request must use the same reliable FSM as automatic attachment. Simulations that require automatic
attachment must say so explicitly in their node configuration.

No new frame family, DATA type, or wire byte is required. This design reuses:

- J `DISCOVER` / targeted `OFFER` / `CLAIM`;
- P searching/check probes and P rosters, **at their existing adaptive cadence** — no new periodic protocol (§7.2);
- the existing candidate-home table;
- `jittered_tx_stash.h` and the existing Hal RNG convention;
- the RTS precedent of starting a response deadline only after transmitter admission;
- the existing `mobile_liveness_ms` boundary;
- the **existing** timer `kMobileOfferBackoffTimerId = 80`, re-shaped as a deadline scan on the
  `park_reflood_arm` / `e2e_ack_deadline_arm_timer` precedent — **`TimerWheel::kCap` stays exactly 91** (§5.3).

Exactly one new constant is introduced: `cap_pending_mobile_offers` (§5.3). It is **host-side** and is
deliberately **not** derived from, aliased to, or defaulted from the existing mobile-side `cap_mobile_offers`.

## 2. Current implementation: verified facts

### 2.1 Late home is invisible until the next deterministic retry

`mobile_claim_guard_fire()` doubles `_mobile_backoff_ms` from 5 s to 120 s. The two-second OFFER
window sits before each delay, so a settled unregistered mobile sends approximately every 122 s.
A static home does not announce host availability. Starting it just after a DISCOVER therefore leaves
an otherwise perfect RF link idle for up to about two minutes. `mobile register` appears to repair the
problem because it replaces that delayed timer with an immediate one; it does not use another protocol.

### 2.2 One home can hold only one pending OFFER

`LayerRuntime::_pending_offer` is a single slot and `kMobileOfferBackoffTimerId` is a single timer.
Every later DISCOVER received during the 100..1000 ms host jitter overwrites the earlier mobile's
targeted OFFER and re-arms the timer. The source explicitly calls this "last DISCOVER wins."

The mobile no-host retry has no jitter. Mobiles that started together therefore remain phase-aligned.
With four mobiles and one late home, one mobile may win each roughly 122-second round while the other
three remain synchronized. A several-minute attach time is an expected result of the current design.

### 2.3 The OFFER window starts before the DISCOVER is handed off

`mobile_discover_fire()` calls `tx_initiating()` and immediately arms the claim guard. It ignores the
return value. `tx_initiating()` may instead:

- defer behind NAV/LBT;
- reject because the LBT defer ring is full;
- reach `DeviceHal::tx`, whose outbound ring may reject the frame.

The host adds its own jitter and can suffer the same deferral/rejection. `jtx_fire()` clears the host
stash regardless of the result. Thus the nominal two-second receive window is measured from a request
to transmit, not from the established transmitter-admission boundary.

### 2.4 CLAIM confirmation is declared but not implemented

The mobile sends CLAIM and immediately sets `_my_mobile_reg.active = true`. A dropped CLAIM leaves the
mobile active while the home has no row. `presence_claim_max_retries` exists but has no consumer;
`_presence_reg_confirmed` is written but never read. The old periodic re-CLAIM was retired.

Recovery eventually occurs only after the first presence interval plus unanswered-probe retries
(normally about 135..143 s), followed by a new DISCOVER cycle. That is not the documented same-home
re-CLAIM recovery.

### 2.5 Candidate monitoring exists, but is incomplete

A registered mobile currently stores up to eight other static nodes heard in beacons/rosters. This is
the correct base mechanism. Current limitations:

- passive beacon candidates are not proof that `can_host_mobiles()` is true;
- `presence_maybe_rehome()` does not reject a candidate whose `last_seen_ms` is stale;
- `first_seen_ms >= 60 s` can make one old observation look "sustained";
- reverse-link quality exists only after a candidate echoes a searching P-probe;
- active canvassing occurs only after home loss, not when a weak home first needs alternatives;
- proactive re-home rejects every candidate whose layer differs from `active_layer_id()`, even when its
  roster was received on the current PHY and therefore proves same-PHY reachability;
- candidates are all discarded after every adopt, removing an immediately useful fallback set.

### 2.6 The home does not actually deregister a silent mobile

`mobile_liveness_ms` is 1,500,000 ms = **25 minutes**. After that age, hash-locate stops proxy-answering
for the mobile. That is the only time-based effect today.

The `_mobile_reg` row is not compacted or removed. Consequently, indefinitely after the mobile dies:

- its local id remains reserved;
- it remains in emitted P rosters;
- it participates in channel-coverage calculations;
- it consumes one of 16 host slots.

The row is removed immediately only when the old home receives a non-searching P-probe naming another
home, when an authoritative static-id collision evicts it, or when the host is reprovisioned/restarted.
The intended documentation describes a 25-minute "prune" that the implementation does not perform.

## 3. Goals and non-goals

### 3.1 Goals

1. A late-starting ordinary static node wakes sleeping attachment retries promptly.
2. At least `cap_mobile_offers` concurrent discovering mobiles cannot overwrite one another's OFFERs.
3. Retry, OFFER, and CLAIM timing is jittered through shared primitives, with explicit RNG accounting.
4. A response window begins only after the initiating frame crosses the accepted handoff boundary.
5. A mobile reports `REGISTERED` only after its chosen home confirms the `(hash, epoch)` row.
6. Manual registration and automatic registration share one FSM and differ only in who enters SEEKING.
7. An attached mobile monitors home health and fresh, verified alternatives even if initial auto-attach
   was disabled.
8. A host physically removes expired direct and redirect rows on a documented deadline.
9. Gateways remain categorically ineligible homes through `can_host_mobiles()` (B132).
10. Failures are visible in `mobile status`, device statistics, and tests.

### 3.2 Non-goals

- no positive J ACK or new registration frame;
- no new DATA type;
- no persistent hosted-mobile registry;
- no simultaneous listening on another frequency/PHY;
- no change to team-DAD or team routing;
- no gateway hosting exception;
- no attempt to infer `host_mobiles` from a normal beacon: a beacon is only a hint, an OFFER/P-roster is
  the authority.

## 4. State and policy

### 4.1 Explicit state model

The implementation must expose these conceptual states even if represented by existing fields:

| State | Meaning |
|---|---|
| `dormant` | no home attachment is requested |
| `seeking` | DISCOVER cycles are active; no chosen home |
| `claiming` | a home/local-id was chosen; CLAIM sent, roster confirmation pending |
| `attached` | the chosen home roster confirmed our hash and epoch |
| `recovering` | a previously attached home is lost; candidate canvass/re-attachment is active |

`_my_mobile_reg.active` alone cannot distinguish `claiming` from confirmed `attached` and must not
remain the sole app-facing truth. `_presence_reg_confirmed` becomes load-bearing.

### 4.2 `mobile_autoregister` policy

Keep the existing field and JSON key for compatibility, but narrow its responsibility:

- `true`: boot enters `seeking` automatically and recovery may continue indefinitely;
- `false`: boot remains `dormant`; the user/app/Heltec starts attachment with `mobile register`;
- once an attachment session was explicitly started, confirmation, presence checks, candidate monitoring,
  proactive re-home and home-loss recovery continue independently of this initial-auto flag;
- `mobile unregister` ends the current volatile attachment session and returns to `dormant`.

Replace the consumed `_mobile_arm_once` idea with a volatile **home-service desired** state. An explicit
manual request must not get one unconfirmed RF attempt and silently stop. It remains seeking/recovering
until success or `mobile unregister`.

For fresh/factory configuration, `mobile_autoregister` should default **OFF**. Existing valid NV values
remain unchanged; no schema bump is needed solely for the default. Every simulation relying on automatic
attachment must set `mobile_autoregister: true` explicitly. This removes tests as a reason for a product
default.

After reboot, a device with the flag OFF is dormant again; that is the deliberate manual policy. A device
that must restore attachment after every reboot uses the flag ON.

### 4.3 Commands

- `mobile register [current | scan | freq=…]`: set home-service desired, enter `seeking`, and trigger an
  immediate first DISCOVER. Existing accepted spellings remain valid.
- `mobile unregister`: clear home-service desired, cancel registration/presence timers, drop the local
  attachment, and return to `dormant`. This adds no deregistration wire message; the old home ages the row
  out under §8.
- Repeating `mobile register` while already seeking is idempotent except that it may select another scan
  mode/PHY and trigger one immediate attempt.
- Repeating it while attached requests a controlled re-evaluation, not a second parallel transaction.

## 5. Reliable discovery and OFFER scheduling

### 5.1 Static-beacon wakeup

While `seeking` or `recovering`, hearing a valid non-mobile, non-gateway static beacon on the active PHY is
a **home hint**. If the next DISCOVER is farther away than the hint window, schedule an earlier DISCOVER
after a small random delay. Requirements:

- do not transmit directly from the RX handler;
- one pending hint wakeup per mobile;
- rate-limit hints so repeated beacons cannot bypass the retry floor;
- exclude gateway/self-gateway beacons (B132);
- the hinted node may have `host_mobiles=off`; only its OFFER proves eligibility;
- reset neither the scan set nor the full failure history until an OFFER is actually received.

This uses the existing beacon and J protocol and closes the late-home two-minute blind interval. A newly
provisioned static node already emits discovery/triggered beacons; no periodic empty roster or new beacon
capability bit is required.

### 5.2 Jittered retry

Retain capped exponential growth but make every no-host retry draw a delay. Use the existing
`retry_backoff_window()` family for the capped window and the canonical `Hal::rand_range(lo, hi)` contract.
Use **equal jitter** rather than an exact deadline:

```text
window = min(5 s * 2^attempt, 120 s)
delay  = rand(window/2, window + 1)       // inclusive upper bound through half-open Hal API
```

The non-zero lower half prevents a failed fleet from immediately storming again; the random upper half
breaks permanent phase alignment. The manual first attempt is immediate. An automatic boot attempt draws
a small startup jitter so a simultaneously powered fleet does not all DISCOVER at t=0.

The draw and its order are part of the simulation contract. Static-only scenarios must remain draw-inert;
mobile scenarios are expected to rebaseline explicitly.

### 5.3 Pending OFFER ring

Replace the one `_pending_offer` slot with a bounded keyed ring of
`cap_mobile_offers` entries. Generalize `jittered_tx_stash.h` rather than implementing another private
copy. The generalized admission must:

1. key an OFFER by `target_key_hash32`;
2. coalesce a duplicate DISCOVER for the same target without consuming another slot or moving its timer;
3. choose a genuinely free slot before any round-robin eviction;
4. never overwrite an armed OFFER for a different mobile;
5. return `armed`, `duplicate`, `full`, or `invalid` to the caller;
6. preserve the fit-before-draw and accepted-only cursor rules already owned by the helper;
7. retain B132's `can_host_mobiles()` check both at admission and at timer fire;
8. expose ring-full and transmitter-rejection counters.

Allocate one timer per ring slot, adjacent to the existing jittered-stash bands, and raise
`TimerWheel::kCap` deliberately. The timer-cap, `sizeof(Node)`, per-board RAM, and gateway feature-set costs
must be measured, not inferred.

### 5.4 CLAIM de-synchronization

Do not make all mobiles close a fixed two-second window and CLAIM in the same millisecond. Add a bounded
per-mobile claim jitter after the minimum OFFER collection interval. The chosen OFFER remains the strongest
received before that mobile's individual deadline.

## 6. Transmitter admission owns response timing

Use the existing RTS discipline: the response timeout starts when the initiating frame crosses the accepted
handoff boundary, not when the caller asks to send it.

### 6.1 DISCOVER

- give DISCOVER an explicit initiating kind or completion token;
- clear the old offer collection before scheduling the new transaction;
- arm `kMobileClaimGuardTimerId` only after the DISCOVER is accepted by the actual LBT/HAL handoff path;
- if admission is rejected, do not report `mobile_no_host`; schedule an admission retry and record
  `tx_rejected`/`defer_full` as the last attempt result;
- a successfully LBT-deferred DISCOVER starts the guard when the defer fires and handoff succeeds.

### 6.2 OFFER

The generic jitter fire path must return/propagate transmitter admission. An OFFER slot may be released once
another structure owns an accepted deferred copy, but it must not disappear after a definitive rejection.
Re-arm it with bounded jitter or report an explicit drop so the source mobile's retry remains the backstop.

### 6.3 CLAIM

Apply the same admission reporting to CLAIM. A rejected CLAIM is a local retry condition, not evidence that
the home rejected registration.

## 7. Confirmation without a new wire message

J CLAIM remains idempotent and claim-stands on the home. P already carries the proof needed by the mobile:
the chosen home's roster contains `(mobile_hash, local_id, reg_epoch)`.

1. After choosing an OFFER, adopt the offered PHY/local id provisionally and enter `claiming`.
2. Send CLAIM with the new epoch.
3. Schedule a short jittered **searching P-probe**. Every eligible home may answer, while the chosen home's
   roster either proves the row or proves absence.
4. A chosen-home roster with matching hash+epoch sets `_presence_reg_confirmed=true`, enters `attached`, and
   emits the app-facing `mobile_reg{registered:true}` push.
5. A chosen-home roster that omits our hash means the CLAIM was not recorded: re-send the same CLAIM, with
   the same local id and epoch, up to `presence_claim_max_retries`.
6. Silence follows the same bounded re-CLAIM count. After exhaustion, reset and return to `seeking` rather
   than remaining falsely registered.
7. A targeted collision DENY still immediately abandons the provisional id and re-enters discovery.

The current immediate `mobile_reg{registered:true}` at OFFER-window close moves to step 4. `mobile status`
must expose `claiming` so the user does not see a false registration during confirmation.

## 8. Candidate-home monitoring and switching

### 8.1 Two levels of knowledge

- **Hint:** a static beacon heard on the current PHY. It supplies id/layer and one-way SNR but does not prove
  willingness to host.
- **Verified candidate:** a compatible P roster received in response to our searching probe, carrying our
  echo. This proves `can_host_mobiles()` at response time and supplies both link directions.

Keep `PresenceCand[cap_presence_candidates]` and its evict-stalest policy. Do not add a parallel table.

### 8.2 Freshness

- reject a candidate at selection if `now - last_seen_ms >= mobile_liveness_ms`;
- if a candidate is heard after that gap, reset `first_seen_ms`, `echo_tier`, and incompatibility evidence
  appropriate to a fresh observation;
- `first_seen_ms` alone never proves sustained availability;
- voluntary switching requires a recent verified echo (`echo_tier != 0xFF`), not merely an old beacon;
- retain other fresh candidates after an adopt; remove/update the chosen home instead of clearing the table.

### 8.3 When to canvass

- always collect passive same-PHY static hints while attached;
- when the current home becomes weak/critical, send a searching P-probe before evaluating a switch;
- send the existing searching probe immediately on home loss;
- ordinary healthy-home checks remain selected-home probes, avoiding fleet-wide roster traffic;
- home roster coalescing and its 10-second rate-limit remain the shared response de-storm mechanism.

### 8.4 Switch criteria

Keep the existing conservative policy:

- candidate bottleneck quality at least two tiers above current-home bottleneck;
- candidate observed for at least `presence_candidate_hold_ms`;
- minimum `presence_rehome_dwell_ms` since the last adopt;
- compatible wire version;
- recent bidirectional verification.

A P roster received on the currently tuned PHY proves RF compatibility. Same-PHY candidates on another
layer may therefore participate; remove the unconditional `candidate.home_layer != active_layer_id()`
rejection for verified roster candidates. A different-PHY candidate still requires the learned-layer scan
during recovery/manual `scan`; this design does not retune periodically away from a healthy home/team.

The actual move remains reset + ordinary J discovery. The candidate does not receive a privileged direct
claim: all audible eligible homes may OFFER, and the strongest current OFFER wins.

## 9. Home-side deregistration and row lifetime

### 9.1 Authoritative rule

A direct hosted-mobile row is **live** while:

```text
now - last_heard_ms < mobile_liveness_ms     // currently 25 minutes
```

At `>= mobile_liveness_ms`, the home must physically compact it out of `_mobile_reg` and the parallel SNR
array. From that point the mobile is deregistered at that home: its local id is free, it is absent from
rosters and coverage accounting, and it consumes no host slot.

This aligns physical state with the proxy-answer rule that already uses the same constant. With presence
checks at 1..8 minutes and mobile beacons/probes refreshing `last_heard_ms`, 25 minutes leaves multiple
repair opportunities without permitting immortal rows.

### 9.2 Redirect rows

When a valid breadcrumb converts an old direct row into a redirect:

- stamp the row's lifetime clock at breadcrumb receipt;
- retain the redirect for `mobile_liveness_ms` so stale senders can follow it;
- then physically remove it under the same age-out sweep;
- redirect rows never reserve last-mile service or advertise as directly hosted.

The 25-minute redirect lifetime is longer than the existing five-minute sender-side mobile-home cache, so
every cache learned immediately before the move has a bounded redirect opportunity.

### 9.3 One removal primitive

Create one `mobile_reg_remove(slot, reason)` compaction primitive covering the row and every parallel array.
Use it for:

- timed direct expiry;
- timed redirect expiry;
- a probe selecting another home;
- static-id alias eviction;
- any future administrative removal.

Run `mobile_reg_age_out(now)`:

- from the normal aging timer;
- before allocating a local id or refusing because `cap_host_mobiles` is full;
- before emitting a roster/using hosted rows for channel coverage.

Do not duplicate age predicates at each consumer.

## 10. Diagnostics

Extend `mobile status` additively with:

- `state`: dormant/seeking/claiming/attached/recovering;
- `home_desired`;
- `confirmed`;
- current retry attempt/window and next-attempt delay;
- current scan index/count;
- offers collected in the current transaction;
- candidate count and verified-candidate count;
- last result: no_offer, tx_rejected, defer_full, claim_unconfirmed, denied, confirmed;
- time since last chosen-home confirmation.

The host `routes`/hosting section should print each row as direct or redirect plus its age. `status` already
exposes `txdrop`; add an OFFER-ring overflow counter if it cannot share an existing admission counter.

Device logs must distinguish **scheduled**, **transmitter-admitted**, and **confirmed**. An event named
`mobile_offer_tx` must not continue to mean only "copied into a stash."

## 11. Implementation slices

### S0 — red reproductions and observability

- Native multi-node fixture: four mobiles start and fail before a home exists.
- Prove current deterministic retry phase and one-slot overwrite.
- Prove guard-before-handoff with forced NAV defer longer than the offer window.
- Prove dropped CLAIM produces false active state and that `_presence_reg_confirmed` cannot affect it.
- Prove a row older than 25 minutes remains in `_mobile_reg` and in a roster.

### S1 — universal keyed jitter ring and admission boundary

- Generalize `jittered_tx_stash.h` with non-overwriting keyed admission and a result enum.
- Convert mobile OFFER to the ring; allocate/test the timer band.
- Propagate handoff result and anchor DISCOVER guard after admission.
- Add startup/retry/claim jitter with explicit RNG-count controls.

### S2 — confirmed registration FSM

- Make seeking/claiming/attached/recovering observable.
- Activate `presence_claim_max_retries` and `_presence_reg_confirmed`.
- Move `registered:true` push to matching-roster confirmation.
- Make manual registration a durable-in-RAM home-service request, not one attempt.
- Add `mobile unregister`.

### S3 — candidate monitoring and lifecycle pruning

- Static-beacon wakeup for seeking/recovering mobiles.
- Freshness and bidirectional verification on the existing candidate table.
- Weak-home searching canvass and verified same-PHY cross-layer candidates.
- Shared row removal + 25-minute direct/redirect age-out.

### S4 — product defaults, simulations, companion and Heltec

- Fresh default `mobile_autoregister=false`; preserve valid NV values.
- Make every simulation's desired mode explicit and rebaseline only mobile scenarios whose RNG changes.
- Update companion controls/status decoding.
- Heltec setup exposes Register/Unregister and the five states; no hidden boot registration is required.

## 12. Required gates

### 12.1 Native/core

1. Four concurrent DISCOVERs at one host produce four correctly targeted OFFERs; no armed entry is
   overwritten. A same-mobile duplicate coalesces.
2. Ring overflow is explicit and previously armed OFFERs remain intact.
3. Gateway/mobile/`host_mobiles=off` nodes produce no OFFER at admission or timer fire (B132 preserved).
4. Two mobiles with identical boot time draw different retry/claim deadlines under controlled RNG values.
5. A DISCOVER deferred beyond two seconds does not close its receive window before handoff.
6. A rejected DISCOVER/OFFER/CLAIM is not reported as a successful send and has a bounded retry.
7. A lost first CLAIM is healed by same-epoch re-CLAIM and roster confirmation.
8. Exhausting re-CLAIM retries returns to discovery; the app never receives false `registered:true`.
9. Auto OFF emits nothing at boot; manual register keeps retrying and then confirms when a home appears.
10. Presence monitoring and weak-home candidate canvass run after a manual attach with auto OFF.
11. A stale/unverified candidate cannot trigger re-home; a fresh bidirectionally verified better candidate can.
12. Same-PHY/different-layer verified candidate can trigger ordinary discovery; different-PHY does not.
13. At 25 minutes minus 1 ms a direct row remains; at 25 minutes it is removed everywhere and its local id
    is reusable.
14. Redirect row lifetime is stamped at breadcrumb receipt and expires at its own 25-minute boundary.
15. Every removal path compacts the SNR/registry parallel arrays consistently.

Every positive must have a nearby negative or mutation control that proves the asserted branch executed.

### 12.2 Simulation

Add a focused late-home scenario:

- four mobiles start first with auto ON;
- no home exists through at least two failed attempts;
- one ordinary static home starts later and emits its normal startup beacon;
- all four mobiles reach **confirmed** attachment within one bounded discovery cycle after the beacon;
- the home contains four unique hashes/local ids;
- zero mobile attaches to a gateway or opted-out static;
- no manual command is used.

Also run an auto-OFF scenario where only the explicitly commanded mobile seeks and where its attached
presence/recovery still functions.

Static keystone scenarios must remain byte-identical. Mobile scenarios may move because this design
intentionally introduces RNG draws; every mover must be attributed, not waived globally.

### 12.3 Hardware

1. Power four mobiles first; wait until each is in its capped retry state.
2. Provision one ordinary static home without touching the mobiles.
3. Capture J DISCOVER/OFFER/CLAIM and P confirmation on both sides.
4. All four must show `attached/confirmed`; the home must show four rows.
5. Repeat with NAV traffic during registration; no two-second premature close.
6. Power one mobile off. Confirm proxying stops and the row is physically absent at the configured expiry
   boundary (use a temporary bench constant for time-compressed validation, then rebuild defaults).
7. Bring a stronger same-PHY static node into range; confirm no switch while the current home is healthy,
   then attenuate the home to weak and confirm canvass + hysteretic switch.

## 13. Documentation consistency obligations

When implemented, update together:

- `docs/protocol.md` presence-plane lifecycle;
- `docs/frames.md` behavioural registration text (wire bytes stay unchanged);
- `docs/2026-07-30-open-bug-register.md`;
- `docs/2026-07-31-bench-test-script.md`;
- companion mobile-status/command contract;
- simulation `BASELINE.md` with exact RNG/corpus movers;
- stale source comments promising a periodic re-CLAIM or calling OFFER "deliberately single-slot."

Do not describe `mobile_liveness_ms` as a home-side prune until the physical compaction and its boundary
tests exist.
