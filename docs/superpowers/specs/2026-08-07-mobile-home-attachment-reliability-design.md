# Mobile home attachment reliability and lifecycle

**Date:** 2026-08-07 · revised 2026-08-07 to the owner's nine ruling groups (§§1, 3.1–3.2, 4.1, 4.3, 5.3,
6.2–6.4, 7.1–7.2, 8, 9.1, 9.4, 10, 11, 12, 13 rewritten in place — the superseded formulations are gone, not
annotated)

**Status: ▶ UNPARKED 2026-08-10 BY OWNER INSTRUCTION — S0–S4b LANDED (uncommitted), ★★ S5 LANDED 2026-08-10
(uncommitted) EXCEPT ITS §8.3/§8.4 SWITCHING HALF, WHICH IS [[B171]], S6 NOT STARTED.**
> ★ **What S5 landed:** §9.1–§9.3 (the one removal primitive + the 25-minute PHYSICAL expiry of direct AND redirect
> rows, as a deadline scan on the existing aging timer — `TimerWheel::kCap` still 91), §9.2's breadcrumb-receipt
> lifetime stamp, §9.4's eight-step expired-id-return test (step 8 mutation-proven), §8.2's candidate freshness /
> after-gap reset / retention-across-adopt, §8.4's verified cross-layer widening with the team-PHY rule preserved,
> gate 24's "adequate before optimal" pin, and two of [[B154]]'s three §10 items.
> ⛔ **What it did NOT land, with its measurement:** §8.3's three searching-probe triggers and §8.4's
> `echo_tier != 0xFF` requirement are **one indivisible corpus-live sub-slice** — see [[B171]]. And [[B154]](a),
> the next-attempt remaining delay, stays open because both shapes cost more than a diagnostic field.
> ⛔ **One corpus row moves (`s07`, +2 lines = the new expiry event only) — registered as [[B170]], an OWNER
> DECISION. The anchor table was NOT edited.** Evidence: `simulation/BASELINE.md` §MH-S5 (top). Live S5 dispatch: [`../plans/2026-08-10-mobile-home-s5-candidates-lifecycle.md`](../plans/2026-08-10-mobile-home-s5-candidates-lifecycle.md).
⚠⚠ **THE BASELINE UNDER THIS SPEC MOVED WHILE IT WAS PARKED — READ THIS BEFORE TRUSTING ANY FIGURE BELOW.** The
hybrid-RTS arc completed: the unicast RTS is now **10 B plaintext / 11 B crypted** (was 7 B), the terminal CTS is
6/7 B, `sizeof(Node)` moved **221288 → 221880**, and the `^### 36/36 corpus` anchor table was **RE-ANCHORED
2026-08-10 on the owner's single ruling**. ⇒ **§12.2's *"a mover in S1/S4/S5/S6 blocks that slice"* rule still
holds but must be measured against the NEW table**, and ⛔ **no delivery, airtime or md5 figure written in this
spec before 2026-08-08 may be compared against a fresh measurement.** ⚠ Line references here may have rotted (V1).

⛔ **(was: PARKED 2026-08-08 BY OWNER INSTRUCTION, PARTLY IMPLEMENTED — S0–S4b LANDED (uncommitted), S5–S6 NOT
STARTED.)** ⛔ *"proposed design; no implementation is claimed"* — the original status line — **is WITHDRAWN as of
2026-08-08; it is false.** Work moved to
[`2026-08-08-hybrid-rts-flight-identity-design.md`](2026-08-08-hybrid-rts-flight-identity-design.md).

> ## ⏸ PARKING RECORD — what is DONE, what is NOT, and what is OWED
>
> ⚠⚠ **ALL OF IT IS UNCOMMITTED AND INTERLEAVED IN THE WORKING TREE WITH THE B153/hybrid-RTS work.** HEAD is
> `23181dd`. **Nothing below is committed** (D4 — the owner commits). ⇒ **A future session must not assume a clean
> tree, and must not "restore" any of this from HEAD** — it is not there.
>
> ### ✅ LANDED (each gated: native + s18/corpus + probes; board sweeps per the owner's per-slice rulings)
> - **S0 — red reproductions.** Five characterization tests pinning the five defects, **each mutation-proven capable
>   of failing** (a green suite would otherwise have proved nothing). ★ Method precedent: they are labelled as pinning
>   a defect and **must be rewritten in place, never deleted** (B101), by the slice that fixes each one.
> - **S1 — admission boundary.** DISCOVER/OFFER/CLAIM response timing anchored to **accepted transmitter handoff**
>   (`LbtKind::mobile_discover` / `mobile_claim` / `mobile_offer`), `mobile_offer_tx` split into
>   **`mobile_offer_scheduled`** (staged) + **`mobile_offer_tx`** (admitted) + **`mobile_offer_dropped`**, and **B142's
>   monotonic `_mobile_attach_gen`** closing an ABA hole where a stale completion consumed newer state.
>   ★ Key measured fact: **`tx_initiating` returns `true` for a DEFERRED frame** — a return-value test cannot detect
>   admission failure. **QA-approved.**
> - **S2 — keyed pending-OFFER scheduler.** `cap_pending_mobile_offers = 8` (⛔ **never aliased to
>   `cap_mobile_offers`**, which stays mobile-side), a **node-global** ring, **timer 80 as a deadline scan** with
>   **`kCap` still 91** (zero free timer ids), and **B137's pending-id reservation** so four concurrent OFFERs propose
>   four unique ids. Plus **B145** (same-millisecond OFFER burst), **B146** (deferred rejection uncounted),
>   **B147** (a CLAIM taking another mobile's reserved id, and its ordering hole). **QA-cleared.**
> - **S3 — the jitter-only re-anchor.** Four draw sites, one shared helper: boot jitter (autoregister only), §5.4 CLAIM
>   de-sync, §5.2 equal jitter on the no-host retry, and the admission-rejected retry. ★ **Measured invariant:
>   `moved ⟺ the scenario has ≥1 autoregistering mobile`, 8/8 and 28/28** — including **7 rows that carry mobiles with
>   `autoregister=false` and did NOT move**, which is the sharp half of the zero-draw proof.
> - **S4 + S4b — the confirmed attachment FSM.** The **three planes** (attachment · **home link** · mesh service) with
>   independent link states and a **confirmation age**; `_presence_reg_confirmed` and `presence_claim_max_retries` made
>   load-bearing (they were 2-writes/0-reads and 0-consumers); **`registered:true` moved to a matching chosen-home
>   roster** (a companion-contract change, documented); the **short SEARCHING solicitation probe** (first ask ~120 s →
>   **~3 s**; a claim-less home is *required* to ignore a SELECTED probe, which is why the old path could not detect a
>   lost CLAIM); **B139** fixed; `mobile unregister`; the 64-bit confirmation age; §10 diagnostics.
>   ⚠ **QA accepted S4b's LOGIC but blocked its INTEGRATION GATE on B153** — which is why the work parks here.
>
> ### ⛔ NOT STARTED
> - **S5** — candidate monitoring and lifecycle: passive collection, degraded/lost-only searching, freshness +
>   bidirectional verification, **"adequate before optimal"** switching, direct **and redirect** physical expiry at
>   `mobile_liveness_ms`, and the **returning-mobile / local-id-reuse** recovery (§9.4's eight-step native test).
> - **S6** — product integration: fresh-config default, companion status, Heltec states/commands, simulations, docs.
>
> ### ⚠ OWED WHEN THIS UNPARKS — do not rediscover these
> - ★★ **The owner's single re-anchor ruling on the 8 mobile movers** (the same 8 rows throughout S1–S4b; **`s18` never
>   moved for them**). The `^### 36/36 corpus` anchor table was deliberately **left unedited**. ⚠ **The hybrid-RTS work
>   will move `s18` and much more**, so the two re-anchors must stay **causally separable** — attribute the mobile-plane
>   8 to S1–S4b, never absorb them into the RTS re-anchor.
> - **B151** — §12.2's simulation scenarios were **never written** (the 4-mobile late-home scenario and the auto-OFF
>   scenario). Natively covered by gates 7/9/10, but ⚠ **these are the integration scenarios that would have caught the
>   original bench failure**, so the gap is the important kind.
> - **B152** — §7.2's two permissively-worded link refreshers are unwired (the negative half, gate 23, is implemented).
> - **B154** — §10's remaining two diagnostic fields, reassigned by name to **S5** in the operative §10 FIELD LEDGER.
> - **B150** — draw site D is corpus-dark. **B144** — a sim clock-rounding artefact that makes a respace branch
>   **structurally unreachable in simulation and reachable on metal**; the drift model hides a real device path.
> - **B155** — the register used id **B151 twice** for two different findings; recorded, deliberately not renumbered
>   (live citations exist).
>
> ### ⛔ DELIBERATELY OUTSIDE THIS SPEC (do not fold in when unparking)
> **B159** (sliding DATA-dedup expiry — its 30 s horizon expires inside a live retry sequence) and the routing
> **T1–T3** tuning (`peer_suspect_rts_timeouts = 1` lets one unanswered retry budget condemn a link, costing ~6
> deliveries **before** B153 existed). Both are real, both are separately actionable, and **neither is a mobile-home
> concern.**

**Original status line (historical):** proposed design; no implementation is claimed  
**Scope:** the mobile↔static-home attachment FSM, host OFFER scheduling, presence confirmation,
candidate-home monitoring, hosted-row expiry, and safe return after host-side expiry. The J and P wire
formats remain unchanged.

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
2. At least `cap_pending_mobile_offers` concurrent discovering mobiles cannot overwrite one another's OFFERs.
3. Retry, OFFER, and CLAIM timing is jittered through shared primitives, with explicit RNG accounting.
4. A response window begins only after the initiating frame crosses the accepted handoff boundary.
5. A mobile reports `REGISTERED` only after its chosen home confirms the `(hash, epoch)` row.
6. Manual registration and automatic registration share one FSM and differ only in who enters SEEKING.
7. An attached mobile monitors home health and fresh, verified alternatives even if initial auto-attach
   was disabled — **passively, adding no steady-state airtime** (§7.2, §8).
8. A host physically removes expired direct and redirect rows on a documented deadline, and a mobile that
   returns after that removal re-attaches safely even though its old local id may already belong to
   somebody else (§9.4).
9. Gateways remain categorically ineligible homes through `can_host_mobiles()` (B132).
10. Failures are visible in `mobile status`, device statistics, and tests, and every statement the UI makes
    is scoped to the plane that actually measured it (§4.1): **home link, never "network connected"**.

### 3.2 Non-goals

- no positive J ACK or new registration frame;
- no new DATA type;
- no persistent hosted-mobile registry;
- no simultaneous listening on another frequency/PHY;
- no change to team-DAD or team routing;
- no gateway hosting exception;
- no attempt to infer `host_mobiles` from a normal beacon: a beacon is only a hint, an OFFER/P-roster is
  the authority;
- **no new periodic candidate-monitoring protocol and no new steady-state airtime** — the existing adaptive
  P cadence is retained unchanged (§7.2);
- no second host-side capacity derived from the mobile-side `cap_mobile_offers`;
- no extra timer-wheel id: `TimerWheel::kCap` is **not** raised.

## 4. State and policy

### 4.1 Three independent planes, not one "registered" bit

Registration is **not** one-dimensional. Three different questions are answered by three different
authorities, and no answer may be substituted for another:

| Plane | Question | The only authority for it |
|---|---|---|
| **Attachment** | *Which static node believes it is our home?* | a matching P roster from the selected home carrying our **hash, our local id and our epoch** |
| **Home link** | *Can this mobile and that home currently communicate, in both directions?* | a **recent correlated bidirectional exchange** with that home |
| **Mesh service** | *Can that home reach a particular destination?* | the **result of that specific route/send** |

★ **Presence can establish attachment and home-link confidence. It cannot assert general mesh
connectivity.** A P roster proves the home holds our row and answered us; it says nothing about whether
the mesh beyond that home can carry a given DM.

★ **UI, console and diagnostics must therefore say "home link" — never an unqualified "network
connected".** See §10.

⛔ **A local TX-admission failure, an LBT deferral, a full outbound/defer ring, or an unrelated route
failure must NOT be read as loss of the radio link to the home.** Those are local or mesh-plane facts; the
home-link plane is only moved by evidence about the home itself.

#### Attachment states

| State | Meaning |
|---|---|
| `dormant` | no home attachment is requested |
| `seeking` | DISCOVER cycles are active; no chosen home |
| `claiming` | a home/local-id was chosen; CLAIM sent, roster confirmation pending |
| `attached` | the chosen home roster confirmed our hash, local id and epoch |
| `recovering` | a previously attached home is lost; candidate canvass/re-attachment is active |

#### Home-link states (independent of the above)

| State | Meaning |
|---|---|
| `unknown` | no confirmation yet, **or** the home-service state is `dormant` |
| `confirmed` | a recent correlated bidirectional exchange with the selected home succeeded |
| `checking` | a confirmation is due, **or** a genuine home-path failure was attributed to the home |
| `lost` | the bounded run of presence misses (`presence_probe_k_miss`) failed |

The two are orthogonal: a node can be `attached` with the link `checking`, and it must render as such.

★ **Always expose the age of the latest confirmation.** Prefer **"Home confirmed 7 min ago"** to an
unconditional green **Connected** — a confirmation is a point-in-time measurement, and the honest display
of a point-in-time measurement is its timestamp (the same discipline as the emergency headline: a
display-shaped field must never overstate what was measured). ⛔ **Do not claim continuous connectivity
during silence.**

`_my_mobile_reg.active` alone cannot distinguish `claiming` from confirmed `attached`, and it cannot carry
the link plane at all, so it must not remain the sole app-facing truth. `_presence_reg_confirmed` becomes
load-bearing.

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

- `mobile register [scan | freq=<MHz> sf=<5-12> [bw=<kHz>]]`: set home-service desired, enter `seeking`,
  and trigger an immediate first DISCOVER.
  ⚠ **Verified against `src/firmware_config.cpp:1113-1130` (V1): there is no literal `current` argument.**
  Plain `mobile register` with no arguments DISCOVERs on the **current** PHY (`mobile_register_current()`);
  `scan` cycles `[current] ∪ learned` (`mobile_register_scan()`); `freq=…` retunes first
  (`mobile_register_phy()`). Those three spellings, and only those, remain valid.
- `mobile unregister`: clear home-service desired, cancel registration/presence timers, drop the local
  attachment, and return to `dormant`. This adds no deregistration wire message; the old home ages the row
  out under **§9** (home-side deregistration and row lifetime).
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

Retain capped exponential growth but make every no-host retry draw a delay. Use **equal jitter** rather than
an exact deadline:

```text
window = min(5 s * 2^attempt, 120 s)      // the EXISTING mobile ladder, retained verbatim
delay  = rand(window/2, window + 1)       // inclusive upper bound through half-open Hal API
```

**The window and the jitter come from different places, and this is the operational instruction, not a
caveat.** The `window` line is the mobile plane's **own existing** in-place accumulator
(`_mobile_backoff_ms = min(2 * prev, mobile_discover_backoff_max_ms)`, `node_mobile.cpp`) — **retain it
as-is; do not refactor it and do not route it through `retry_backoff_window()`.** Only the **equal-jitter
bounds** join the shared retry-backoff family, as the pure `protocol::equal_jitter_lo` /
`equal_jitter_hi_excl` pair declared beside `retry_backoff_window()`. The canonical
`Hal::rand_range(lo, hi)` half-open contract still applies to the draw itself.

> ⛔ **CORRECTED 2026-08-07 (§MH-S3-QA item 3).** This section used to read *"use the existing
> `retry_backoff_window()` family for the capped window"*. **That instruction cannot be followed**, verified
> in source before S3 wrote a line and re-verified here (V1, `lib/core/protocol_constants.h:143-147`):
> `retry_backoff_window(base, attempt, max_shift)` returns `base << min(attempt, max_shift)` — a **SHIFT**
> clamp — whereas the ladder above is `min(base * 2^attempt, ceiling)`, a **VALUE** clamp, and no shift of
> 5000 ms equals 120000 ms. Worse, `retry_backoff_max_shift` is globally **0** (shipped at 0 after the
> 24-seed `twin_9node_dm` BEB A/B refuted the hypothesis), so the function currently produces **no growth at
> all** — an implementer who followed the old sentence literally would have shipped a flat 5 s retry while
> believing they had built the ladder. ⛔ **Do not "unify" the two functions; they clamp different things.**

The non-zero lower half prevents a failed fleet from immediately storming again; the random upper half
breaks permanent phase alignment. The manual first attempt is immediate. An automatic boot attempt draws
a small startup jitter so a simultaneously powered fleet does not all DISCOVER at t=0.

The draw and its order are part of the simulation contract. Static-only scenarios must remain draw-inert;
mobile scenarios are expected to rebaseline explicitly.

### 5.3 Pending OFFER ring

#### 5.3.1 Two capacities that must never be confused

`protocol_constants.h:675` already defines `cap_mobile_offers = 8`. Verified (V1): it means **"OFFERs
collected by a discovering mobile in one DISCOVER window"** and is consumed **mobile-side** at
`node_join.cpp:368` (`_mobile_offers[]`, `node.h:2132`).

⇒ **`cap_mobile_offers` stays exclusively mobile-side.** The host ring gets its own, independent constant:

```text
cap_pending_mobile_offers = 8      // host: concurrently ARMED targeted OFFERs awaiting their jitter fire
```

⛔ **Never derive, alias, `= cap_mobile_offers`, or default one from the other**, and never describe them
as "the same 8". They answer different questions (how many homes may one mobile weigh, versus how many
mobiles may one home owe an OFFER), they are sized by different pressures (RF diversity versus host RAM
and `cap_host_mobiles = 16`), and either may be retuned alone. The equal starting value is a coincidence,
not a relationship.

#### 5.3.2 The ring

Replace the one `LayerRuntime::_pending_offer` slot with a bounded keyed ring of
`cap_pending_mobile_offers` entries. Generalize `jittered_tx_stash.h` where practical rather than
implementing another private copy (U1). The generalized admission must:

1. key an OFFER by `target_key_hash32`;
2. ★★ answer a **repeat** DISCOVER for the same target on **one** slot — but the answer is **TWO DIFFERENT
   BEHAVIOURS**, selected by whether that slot's OFFER has already flown. ⛔ **This item used to read simply
   *"coalesce a duplicate DISCOVER"*, and that single sentence was wrong for half the cases** (corrected
   2026-08-07 §MH-S2b/§MH-S2c against the implementation in `node.h`'s `PendingMobileOffer` comment):
   - **duplicate while the OFFER is still ARMED** (the entry holds packed bytes) → **COALESCE.** No extra
     slot, **no RNG draw, and the deadline is NOT moved** — an answer is already on its way inside the
     100..1000 ms window, and moving the deadline would let a retry-happy mobile postpone its own OFFER
     forever. Admission returns `duplicate`; the caller emits `mobile_offer_coalesced`, deliberately **not**
     `mobile_offer_scheduled`, because nothing was scheduled.
   - **re-DISCOVER AFTER the OFFER was transmitted** (the slot is reservation-only) → **RE-ARM THE SAME
     SLOT, RETAINING THE RESERVED ID.** The mobile did not hear the OFFER, so the correct answer is a new
     OFFER: a fresh frame, a fresh deadline, a fresh draw — into the slot it already owns. Admission
     returns `armed`. ⛔ Treating this as a "duplicate" would answer every re-DISCOVER with **silence for
     the whole reservation lifetime**; treating the first case as a re-arm reintroduces the deadline-shifting
     the coalesce exists to stop.
3. choose a genuinely free slot before any round-robin eviction;
4. never overwrite an armed OFFER for a different mobile;
5. return `armed`, `duplicate`, `full`, or `invalid` to the caller;
6. preserve the fit-before-draw and accepted-only cursor rules already owned by the helper;
7. retain B132's `can_host_mobiles()` check both at admission and at fire;
8. expose **ring-full** and **transmitter-rejection** counters. ⚠ **CLARIFIED 2026-08-07 ([[B146]]): the
   increment belongs to the ONE function that REPORTS the rejection, never to a call site** — the
   rejection has **two** callers (the immediate admission arm and the deferred-LBT loss arm), and a
   call-site increment silently under-reported the deferred one to **zero** while still emitting the drop
   event. The counter and the event must be equal by construction;
9. drive the transmitter-rejection response from the admission result: a rejection **retains or reschedules**
   the entry (§6.2) — it never silently drops it and never evicts a different mobile's entry;
10. ★★ **RESERVE the proposed local id at admission** — see the ruling immediately below. This is **required
    mechanism**, not an option.

##### ★★ Pending-id reservation — OWNER/QA RULING (B137), REQUIRED FOR S2

**[[B137]]:** `find_free_mobile_id()` (`node_join.cpp:73`) scans `_mobile_reg` only, and **a staged OFFER is
not a registry row**, so nothing reserves an id between OFFER and CLAIM — every concurrently discovering
mobile is offered the **same** id (measured: 254 for both). It is invisible today only because the
single-slot `_pending_offer` means one OFFER is ever transmitted: **the two defects mask each other**, and
**S2 removes the mask.**

⛔ **Relying on CLAIM-collision DENYs is NOT acceptable.** It would serialize four mobiles into **four
discovery rounds** and waste airtime — the arc's whole purpose is the opposite. ⇒ **S2 MUST implement
pending-id reservation:**

- the proposed local id is **reserved from OFFER admission** until a matching CLAIM arrives or a **bounded
  expiry** elapses;
- **`find_free_mobile_id()` excludes live reservations** as well as registry rows;
- a **repeat DISCOVER retains the same reservation** — under **both** arms of item 2 (coalesce *and*
  re-arm). It must never consume a second slot or re-draw an id; `find_free_mobile_id()` returns a live
  reservation's own id to its own key, which is what makes the re-arm arm keep it;
- **timer 80's deadline scan also expires reservations**, so a mobile that never CLAIMs cannot leak an id;
- ⇒ **four concurrent OFFERs propose four UNIQUE ids**;
- the targeted CLAIM-collision DENY (`node_join.cpp:225-233`, §9.4 step 5) **remains, as a race backstop
  only** — never as the primary allocator.

##### ★★★ A CLAIM IS MATCHED ON THE PAIR `(key_hash32, proposed_node_id)` — [[B147]], added 2026-08-07 §MH-S2b

⛔ **The ruling above specified the reservation but not how a CLAIM is correlated against it, and the first
implementation correlated on HASH ALONE** — it recorded whatever `proposed_node_id` the frame carried and
released "the claimant's" slot by hash. **A reservation is a promise about a PAIR, and half of it was never
read.** ⇒ A is offered X and lets the promise lapse · X is re-promised to B · A's *delayed* CLAIM for X
arrives · no registry row holds X yet, so A is recorded on **B's** reserved id, and B's own CLAIM then walks
into the very collision-DENY recovery this reservation exists to make unnecessary. The mobile CLAIM handler
therefore resolves, in this order:

1. **the claimant's OWN live reservation, FIRST.** If one exists and its `proposed_id` **differs** from the
   CLAIM's, the CLAIM is a **stale echo of an earlier round**: emit `mobile_claim_stale_id`, **DROP it,
   RETAIN the reservation, and return** — ⛔ do **not** DENY, because a DENY would make the mobile
   re-register and throw away the id the home is currently promising it, and do not fall through, for the
   reason in the ordering rule below;
2. the **registered-row** collision check (a differently-keyed hosted mobile already holds the id) →
   targeted DENY, release the loser's reservation;
3. the **reserved-elsewhere** check (the id is promised to another hash and the claimant holds no promise of
   its own) → the same targeted DENY, naming the **reservation holder** as owner;
4. otherwise **RECORD** — either the promise kept (`own` matches) or a late CLAIM whose reservation aged out
   against an id nobody else is promised. ⓘ The latter is the pre-reservation compatibility path and is
   **deliberately retained**: the reservation is an upper bound on a leak, not a licence to reject a mobile
   that took its time.

⚠ Liveness is `reserve_until_ms > now`, **never** *"has the sweep run"* — a dropped `_hal.after` must not be
able to reject a legitimate CLAIM forever. This is `find_free_mobile_id()`'s own stated invariant, inherited.

★★★ **THE ORDERING RULE, AND IT IS A DESIGN CONSTRAINT, NOT AN IMPLEMENTATION DETAIL (§MH-S2c):
A STALE FRAME MUST BE IDENTIFIED AS STALE BEFORE ANY BRANCH ACTS ON ITS CONTENTS.** Step 1 precedes step 2
for a measured reason: with the checks in the other order, the sequence *"A held X · X is now REGISTERED to
B · A re-DISCOVERs and is promised Y · A's delayed CLAIM for X arrives"* made the registered-row branch DENY
A **and release Y**, so a collision check on a stale frame **consumed state belonging to a newer
transaction** and step 1 was never reached. ⓘ Same family as [[B142]] (a stale LBT completion consuming the
newer attempt's stage). ⛔ **"Check the reservation but still fall through" is NOT the fix** — the
fall-through releases the reservation on its way out; the early return is load-bearing.

★ **Why:** the reservation makes four concurrent attachments cost **ONE** discovery round instead of four.

★ **The ring is NODE-GLOBAL, not per-layer.** `can_host_mobiles()` (`node.h:481`) is
`host_mobiles && !is_mobile && !is_gateway && n_layers == 1`, so **a legal home is necessarily a
single-layer non-gateway node**. Sizing the ring `× MR_N_LAYERS` would therefore buy no semantic capacity
whatsoever — it would only multiply RAM on a node that can never use the second dimension. One ring, owned
beside the other node-global pending structures.

#### 5.3.3 One existing timer, deadline scan — `kCap` stays 91

⛔ **No timer per ring slot. `TimerWheel::kCap` stays exactly 91.** Verified (V1): the highest allocated id
is `kE2eAckDeadlineTimerId = 90` (`node.h:999`), `kCap = 91` (`timer_wheel.h:25`), and `BASELINE.md` records
**0 free ids left**. A per-slot band is not available and is not needed.

Retain **`kMobileOfferBackoffTimerId = 80`** — the timer that already exists for exactly this job — and
re-shape it as a **deadline scan**, the established idiom of `park_reflood_arm()` /
`park_reflood_fire()` (`node.h:1100-1101`) and `e2e_ack_deadline_arm_timer()` / `e2e_ack_deadline_fire()`
(`node_mac.cpp:355-375`), both of which serve a whole multi-entry ring from **one** one-shot re-armed to the
earliest pending deadline. Generalize that helper where practical rather than writing a third copy (U1).

The contract:

- each ring entry carries its **own** due time (its individually drawn 100..1000 ms host jitter);
- timer 80 is armed for the **earliest** due entry only;
- on fire, transmit **at most one** due OFFER per callback, then **re-arm for the next earliest**;
- ★★ **the re-arm delay must be STRICTLY POSITIVE** — ⛔ **corrected 2026-08-07 §MH-S2b ([[B145]]); the
  sentence that used to stand here, *"one frame per callback keeps the host off a same-millisecond burst"*,
  was FALSE AS WRITTEN.** "At most one per callback" bought nothing on its own: an already-**overdue**
  remainder re-armed with delay **0**, `TimerWheel::pop_due` fires on `_due <= now`, and the device pump
  (`src/fw_main.cpp`) keeps popping against a clock it re-reads but which need not have advanced ⇒ the
  callback **re-entered inside the same pump pass** and four overdue OFFERs reached the radio in **one
  millisecond**. The floor is `protocol::mobile_offer_respace_ms`, substituted **only** where the computed
  delay would be 0 (a positive computed delay passes through untouched).
  ⓘ **`mobile_offer_respace_ms = 100` IS AN OWNER-OWED CHOICE, NOT A CORRECTNESS BOUND — state it as a
  tunable.** Measured: **any positive value, including a 1 ms floor, also stops the burst** (mutation
  M-B145-2). 100 ms is a *spacing* decision — it exceeds a short-SF J-frame's airtime — and it is
  deliberately **not** an alias of `join_offer_backoff_min_ms` despite sharing the value: that is a random
  window's lower bound, this is the minimum spacing between two OFFER transmissions.
- inserting an entry whose deadline is **earlier** than the current arming **re-arms** timer 80;
- a repeat DISCOVER for the same `target_key_hash32` follows **§5.3.2 item 2's two arms** — **coalesce**
  (deadline NOT moved, no draw) while the entry is still armed, **re-arm the same slot** once the OFFER has
  been transmitted — and **retains its existing id reservation in both** (§5.3.2's B137 ruling);
- an armed entry belonging to **another** mobile is **never** overwritten — a full ring returns `full`;
- the scan **also expires pending-id reservations** whose bound has elapsed (B137 ruling), so a mobile that
  is offered an id and never CLAIMs cannot leak it;
- cancelling/consuming the last entry cancels timer 80.

⚠ **This still moves `sizeof(Node)`.** ⇒ **memory is a go/no-go gate at the START of slice S2**, before the
scheduler is written:

- measure the per-entry record size and where the ring is placed in `Node`;
- update the native `sizeof(Node)` layout assertion **deliberately**, as a stated decision, not as a
  test-fix;
- produce the **full per-board RAM grid**, not a native number (native alignment hides board padding, D2);
- check the **tightest board's** percentage explicitly and report it;
- confirm `TimerWheel::kCap == 91` is unchanged.

If the tightest board cannot absorb the ring, `cap_pending_mobile_offers` is reduced **on its own merits** —
never by reaching for `cap_mobile_offers`.

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

The generic jitter fire path must return/propagate transmitter admission, and the **admission result decides
the entry's fate** (§5.3.2 rule 9). An OFFER entry may be released once another structure owns an accepted
deferred copy, but it must not disappear after a definitive rejection: **reschedule** it with a bounded new
deadline (which re-arms timer 80 if it is now the earliest) or **report an explicit drop** and bump the
transmitter-rejection counter, so the source mobile's own retry remains the backstop. A rejection must never
disturb any other mobile's armed entry.

### 6.3 CLAIM

Apply the same admission reporting to CLAIM. A rejected CLAIM is a local retry condition, not evidence that
the home rejected registration.

### 6.4 Admission failures are a local plane

⛔ A `tx_rejected` / `defer_full` / LBT-deferral result at **any** of the three sites above is a statement
about **this node's own transmitter**, not about the home. It must be surfaced as the last attempt result
(§10) and it must **never** move the home-link plane to `checking` or `lost` (§4.1). Only evidence about the
home — a missed presence check, an attributable home-path failure, or an exhausted miss run — does that.

## 7. Confirmation without a new wire message

### 7.1 Confirming the CLAIM

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

#### ★★★★ §MH-S4b — STEP 3 IS **TWO** DEADLINES AND A SUBSTATE, AND SAYING SO IS THE POINT (2026-08-08)

Steps 3-6 above are correct but under-specified, and §MH-S4 implemented them in a way that was self-defeating.
Both defects are recorded here in the OPERATIVE text, not in a note beside it (the eighth-time rule from
[[B151]]):

- **The probe must be `searching` (`selected_home_id = 0`), and step 3 already says so — it was implemented as
  a SELECTED probe.** A home that MISSED the CLAIM has no row for us and `presence_ingest_probe` ends with
  *"a check probe for a hash we don't host → ignore"*, so the one mechanism meant to detect the miss could not:
  the only remaining signal was our own timeout. A searching probe is answered by every eligible home, so the
  chosen home either rosters our triple (→ step 4) or rosters WITHOUT us (→ step 5, positive evidence).
- **"Schedule … then" is a WAIT, and a re-CLAIM may only be spent when that wait expires.** §MH-S4 sent the
  probe and spent a retry in the SAME callback, so the answer could not physically arrive first. The claiming
  deadline is therefore split in two, alternating on one timer id:

| substate | the deadline means | action | budget |
|---|---|---|---|
| not solicited | "we have not asked yet" | send the **searching** solicitation probe (`presence_claim_solicit_ms` after the CLAIM) | spends **nothing** |
| solicited | "we asked and are waiting" | the roster window expired in silence → step 5/6 | spends **one** re-CLAIM |

- ★ **`presence_claim_confirm_ms` MUST outlast the home's own roster rate limit** — `presence_roster_min_interval_ms`
  (10 s) + `presence_roster_coalesce_max_ms` (1.5 s) — or "silence" is declared while the answer is still
  legally queued at the home. A `static_assert` in `protocol_constants.h` pins the relationship.
- ★ **"Short" is short:** §MH-S4 armed `presence_check_base_ms` (120 s), so a mobile whose CLAIM was lost sat
  provisionally attached for two minutes before anything asked. The first ask is now ~3 s after the CLAIM, and
  in a live network the home's own registry-change roster usually confirms before that.
- ⓘ The substate is **not** a sixth `MobileAttachState`: §4.1's five values are the app-facing contract. It is
  surfaced separately as `claim_solicited` (§10), rendered only while `attachment == "claiming"`.
- ⛔ **A budget is spent by the physical act, never by the request.** Only a re-CLAIM our own transmitter
  ADMITTED counts. `tx_initiating` answers **true** for a frame accepted into the LBT defer ring — a deferred
  re-CLAIM legitimately IS in flight and counts — so the only refund case is a deferred frame the HAL later
  refuses, which never reached the air. (Fifth appearance of this rule in the arc: [[B84]], [[B145]]/[[B146]],
  [[B139]], here.)

The current immediate `mobile_reg{registered:true}` at OFFER-window close moves to step 4. `mobile status`
must expose `claiming` so the user does not see a false registration during confirmation.

A matching roster confirms **both** planes at once: it is authoritative **attachment** evidence (hash + local
id + epoch) *and* a correlated bidirectional exchange, so it also sets the **home link** to `confirmed` and
stamps its age (§4.1).

### 7.2 Maintaining home-link confidence — the airtime policy

⛔ **No new periodic protocol, and no new steady-state airtime.** Home-link confidence rides entirely on the
**existing adaptive P cadence**, retained unchanged (verified against `protocol_constants.h:690-696`):

| Home quality tier | Check period T |
|---|---|
| weak / critical (`presence_check_min_ms`) | ≈ 60 s |
| acceptable / ok (`presence_check_base_ms`) | ≈ 120 s |
| strong (`presence_check_max_ms`) | ≈ 480 s |

plus a **0–8 s probe jitter** (`presence_probe_jitter_ms`) and, on silence, **two retries ≈5 s apart**
(`presence_probe_k_miss = 2`, `presence_probe_retry_ms = 5000`) before the link is declared `lost`.

★ **The consequence, stated explicitly because it is the accepted default trade-off:** a mobile that walks
away from a **strong** home immediately after a confirmation, and then generates no traffic, can take
**≈8 minutes plus jitter plus the two retries** to notice the loss. That is the deliberate price of not
adding a faster keepalive to a duty-cycled LoRa link. It is not a defect and must not be "fixed" by a
shorter unconditional period.

Rules for moving the home-link plane:

- a **matching P roster** from the selected home is authoritative confirmation of attachment **and** of the
  bidirectional home link;
- **any correlated successful exchange** with the selected home may refresh link confidence — a completed
  MAC exchange or an answered probe already paid for itself;
- an **attributable home-path send failure** may trigger an **immediate** check (state `checking`), rather
  than waiting for the next scheduled T;
- **ordinary app traffic must not create extra maintenance traffic** — it may *refresh* confidence, never
  *schedule* an additional probe;
- ⛔ **inbound beacons alone are one-way hints, not bidirectional confirmation** — hearing the home proves
  only that we hear the home;
- ⛔ a local TX-admission failure, an LBT deferral, a full queue or an unrelated route failure moves
  nothing (§6.4);
- the UI **displays the confirmation age** and ⛔ **never claims continuous connectivity during silence**
  (§4.1, §10).

**Airtime gate (§12.1):** an idle attached mobile with a healthy home, in the presence of other audible
eligible static nodes, must emit **no more** frames per unit time than the current implementation at the
same quality tier. Candidate monitoring adds **zero** transmissions.

## 8. Candidate-home monitoring and switching

★ **The policy in one line: "adequate before optimal."** A home that is doing its job is kept, even when a
better one is audible. Switching is an exception justified by evidence, not a continuous optimisation.

### 8.1 Two levels of knowledge — collected passively

- **Hint:** a static beacon heard on the current PHY, **or** a P roster already emitted for somebody else and
  overheard. It supplies id/layer and one-way SNR but does **not** prove willingness to host.
- **Verified candidate / authority:** a **compatible roster echo carrying our own echo**, or an **OFFER**
  addressed to us. Either proves `can_host_mobiles()` at response time and supplies both link directions.

⛔ **A beacon is only a hint.** Only a compatible roster echo or an OFFER is authority.

Both levels are gathered **passively** from traffic that is already on the air — same-PHY static beacons and
already-emitted P rosters. ⛔ **No new periodic candidate-monitoring protocol, and no parallel table:** keep
`PresenceCand[cap_presence_candidates]` (`protocol_constants.h:716`, 8 entries) and its evict-stalest policy.

### 8.2 Freshness

- reject a candidate at selection if `now - last_seen_ms >= mobile_liveness_ms`;
- if a candidate is heard after that gap, reset `first_seen_ms`, `echo_tier`, and incompatibility evidence
  appropriate to a fresh observation;
- `first_seen_ms` alone never proves sustained availability;
- voluntary switching requires a recent verified echo (`echo_tier != 0xFF`), not merely an old beacon;
- retain other fresh candidates after an adopt; remove/update the chosen home instead of clearing the table.

### 8.3 When a searching probe is allowed

⛔ **Do not send a searching probe merely because another node may be stronger.** That is the airtime hole
this section exists to close: a fleet of mobiles each canvassing for a better home is a fleet-wide roster
storm bought with nothing.

A **searching** P probe is sent **only** when one of these three is true:

1. the current home's quality is **weak or critical**;
2. the current home **misses a check** (an unanswered probe, on the way to `lost`);
3. an **attributable home-path failure** occurs.

⛔⛔⛔ **IMPLEMENTATION STATE, 2026-08-11 (§MH-S5b-ii) — THIS SECTION IS *NOT* SATISFIED, AND THE STATE IS STATED HERE
because this is the instruction a reader follows.** ⛔ **This paragraph replaces, in place, an earlier one that said
*"1 and 2 are IMPLEMENTED"*; that was true of §MH-S5b and is FALSE of the tree as it now stands.**

- **TRIGGER 2 IS IMPLEMENTED** — `Node::presence_searching_probe_due()` (`lib/core/node_mobile.cpp`) reads
  `_presence_miss > 0`, an **admitted** miss, so a probe our own transmitter refused is not one.
- ⛔⛔ **TRIGGER 1 IS *DEFERRED* UNDER [[B178]] — implemented, measured, and then withdrawn.** §MH-S5b landed it
  (`_presence_prescan`) and the corpus priced it: a weak-home mobile's already-scheduled check flips from *selected* to
  *searching*, **every** eligible home answers, corpus P-roster airtime rises **+31 %** (33 518 → 43 752 ms), `s07`
  collisions go 2775 → 3528, and **6 unique deliveries are lost, all of them in `s07`** (734 → 728, below the `≥733`
  floor) — §8.3's *own* stated hazard arriving from a trigger §8.3 itself permits. The disjunct is therefore removed.
- ⛔ **TRIGGER 3 IS NOT IMPLEMENTED** — it needs a latch, i.e. a `Node` member and therefore D2, and it is its own
  slice.

★★★ **THE LIMITATION THIS LEAVES, WHICH MUST NOT BE PRESENTED AS COMPLETION: a weak but CONSISTENTLY RESPONDING home
will not proactively initiate candidate verification, so the mobile changes home only AFTER connectivity begins
failing.** A verified echo can only come from a *searching* probe, and today only a MISS (or loss, or `claiming`) sends
one. ⇒ **THIS IS A CONSERVATIVE INTERIM POLICY, NOT COMPLETED PROACTIVE ROAMING**, and ⛔ **§S6.4-C's purpose — *leave a
weak home BEFORE loss* — IS NOT MET.**

★★ **WHAT RETURNS ([[B178]]'s refined form, to be implemented and measured, ⛔ not yet designed into the numbered list
above):** a proactive searching probe **only** when the home is weak/critical **and** at least one candidate is
**fresh**, **compatible**, **passively observed**, **still unverified**, and whose **measured one-way quality could
possibly satisfy the two-tier improvement rule** — **and** the **candidate hold** and **anti-flap dwell** are already
satisfied, i.e. a canvass is only ever spent where a switch could actually complete. ⛔⛔ **The broad *"weak home + any
audible candidate"* form is REFUSED: in a dense scenario nearly every mobile has an audible candidate, so it would
reproduce the same storm unchanged — the audibility of a candidate is not evidence that switching is possible.**
⚠ **[[B177]] must be fixed FIRST** (the hosted-mobile beacon touch has no row-kind/freshness/epoch gate): its erroneous
refresh can alter the very liveness and quality inputs the refined trigger reads, so measuring the refined form on top
of it would measure the wrong tree.
⛔ Nothing here claims any owner or QA approval beyond the deferral itself, and the refined trigger's acceptance
criteria (the canonical floor — **`≥732` since 2026-08-11, provisional pending [[B163]]; ledger §1.17** — **plus** no increase in `presence_home_lost` in `s07`, bounded roster airtime and peak-window
collisions, and no repeated canvass once a plausible candidate is verified) are recorded in [[B178]], not met here.

Everything else stays passive:

- always collect passive same-PHY hints and overheard rosters while attached — **zero transmissions**;
- ordinary healthy-home checks remain **selected-home** probes, not searching ones — ⚠ **and while trigger 1 is
  deferred, so does a WEAK-but-answering home's check** (§MH-S5b-ii, above);
- on home **loss**, the existing searching probe fires immediately (unchanged);
- home roster coalescing and its 10-second rate-limit (`presence_roster_min_interval_ms`) remain the shared
  response de-storm mechanism.

★ **Keep the current home while it is adequate, even if another node is measurably stronger.** Once the home
is genuinely **lost**, recovery is free to take the **best currently eligible OFFER** — at that point there
is nothing adequate to protect.

### 8.4 Switch criteria

Keep the existing conservative policy:

A **voluntary** switch requires **all** of:

- **fresh evidence** — a current observation, not an old one (§8.2);
- **recent bidirectional verification** — a roster echo or OFFER, never a beacon alone;
- candidate **bottleneck quality ≥ 2 tiers** above the current-home bottleneck
  (`presence_rehome_tier_delta = 2`);
- the existing **60 s hold** — candidate sustained for `presence_candidate_hold_ms`;
- the existing **5-minute anti-flap dwell** — `presence_rehome_dwell_ms` since the last adopt;
- compatible wire version.

A P roster received on the currently tuned PHY proves RF compatibility. ★ **Equality of the layer nibble is
not required:** a same-PHY candidate advertising another **full layer id** remains valid once verified, so
remove the unconditional `candidate.home_layer != active_layer_id()` rejection for verified roster
candidates.

✅ **IMPLEMENTED 2026-08-11 (§MH-S5b): the verified-echo conjunct is enforced** — `presence_maybe_rehome` refuses any
candidate whose `echo_tier` is still `0xFF`, and the other five conjuncts are untouched with no constant re-tuned.
⚠⚠ **BUT READ IT TOGETHER WITH §8.3's DEFERRAL (§MH-S5b-ii): the ONLY thing that can set `echo_tier` is a *searching*
probe, and today only a MISS, a LOSS or `claiming` sends one.** ⇒ a voluntary switch is reachable **only after the home
has begun failing** — `s27`'s re-home still fires, so the mechanism is not disabled, but the *proactive* half of it is
gated behind [[B178]]'s refined trigger. ⛔ **Do not "restore" reachability by relaxing this conjunct** — trigger 2 is
what keeps it reachable, and §MH-S5b measured that dropping the conjunct instead is not what costs the deliveries.
★ **AND THE LAYER-NIBBLE REJECTION IS NOW GONE ALTOGETHER, NOT KEPT FOR HINTS.** §MH-S5 landed it as
`if (!verified && home_layer != active_layer_id()) continue;` and recorded that *"the rejection SURVIVES for
unverified ones, which is what keeps the widening safe"* — **that sentence is withdrawn as a live rule**: once an
unverified candidate is refused outright the layer test is unreachable, so keeping it would be dead code asserting a
policy that decides nothing. The widening itself is unchanged and is now unconditional. ⛔ The team-PHY restriction is
untouched (gate 26): the move is *reset + ordinary J discovery*, gated by `team_phy_ok` at `mobile_discover_fire`.
⚠ ★ **`mobile_verified_candidate_count()` remains the ELIGIBILITY FLOOR, not this predicate** — it applies
verification, freshness and compatibility and stops there; selection additionally applies the delta, the hold, the
dwell and the current-home exclusion. ⛔ **Do not reconcile the two by changing the count** — a golden test pins it.

★ **Same-PHY candidates are monitored passively; monitoring another PHY means leaving the current one.**
Retuning away from the current PHY blinds the mobile to its own home and team, so it is permitted **only
during recovery or an explicit manual `scan`** — never as background monitoring, and never while a healthy
home is attached.

⚠ **The team restriction is preserved unchanged.** A team mobile stays on its **provisioned team PHY**
(`team_phy_ok()`, `node_mobile.cpp:51-54`): it must **not** roam or scan onto an incompatible PHY, because
its teammates are unreachable there. Different-PHY recovery applies **only** where that existing team-PHY
rule already permits it; for a non-team mobile (`team_id == 0`) the rule is inert as today.

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
checks at 1..8 minutes and **validated registration probes** refreshing `last_heard_ms`, 25 minutes leaves
multiple repair opportunities without permitting immortal rows.

⛔⛔ **CORRECTED IN PLACE 2026-08-11 BY §B177-FIX (owner-ruled, ledger §1.16). THE SENTENCE ABOVE READ
*"mobile beacons/probes refreshing `last_heard_ms`"* AND THE BEACON HALF IS WITHDRAWN.** A **beacon is a
presence/candidate HINT ONLY** and no longer refreshes a hosted row: it matched the row by **hash alone**,
which restamped redirect rows past §9.2's breadcrumb clock, resurrected expired rows before compaction
(ledger §1.14) and — carrying **no `reg_epoch`** on the BCN wire — kept pre-re-home rows alive at an old
home. ⇒ the refresh authority is **the epoch-bearing P probe, on BOTH its arms** (selected and searching),
each gated on the shared `host_row_probe_refreshable()` predicate `(live · direct · epoch matches)`.
⛔ **No epoch byte or TLV was added to the beacon** — permanent airtime for a mechanism the P plane already
provides. **A stationary mobile is still covered because it is exactly the case that probes:** its own
adaptive check cadence is 60 000..480 000 ms (1–8 min), well inside the 25-minute expiry.

★ **Keep 25 minutes, and understand the asymmetry it creates — it is intended, not an inconsistency:**

- the **mobile** detects loss **much earlier** (T + 2 retries: ≈70 s weak, ≈490 s strong — §7.2);
- the **old home** deliberately **retains** the row far longer, so delayed traffic still lands and can be
  redirected (§9.2) rather than black-holed;
- a **successful re-home converts** the old direct row into a redirect **promptly**, via the non-searching
  probe naming the new home — the fast path does not wait for the timer;
- **silence eventually expires both** sides, so nothing is immortal.

The 25-minute constant is therefore the *slow backstop* for the case where no re-home ever happens, not the
detection mechanism.

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

### 9.4 Returning after host expiry, and local-id reuse

§9.1 makes host rows genuinely mortal, and that creates a case the current design has never had to face: a
host **physically removes** a direct row after `mobile_liveness_ms` (25 min), and **its former local id may
then be reassigned to a different mobile.** A mobile that was out of range for half an hour therefore comes
back holding state the host has already given away.

The resolution needs **no new mechanism** — it is the existing pieces, in order:

1. the returning mobile's **selected-home P probe cannot confirm**: the home's roster has no entry matching
   its `(hash, local_id, epoch)`, because the row is gone (and any row at that local id belongs to another
   hash);
2. after the **bounded miss run** (§7.2) the mobile's home link goes `lost` and it enters **`recovering`**;
3. recovery runs an **ordinary DISCOVER** — no privileged re-CLAIM of the remembered id;
4. the host allocates and offers a **currently free** local id, exactly as for a first-time mobile —
   ★ "free" meaning **neither a registry row nor a live pending-id reservation** (the B137 ruling, §5.3.2),
   so a returning mobile cannot be handed an id already promised to a mobile mid-handshake;
5. **residual race backstop only:** if two mobiles still converge on the same id — which the reservation
   makes a narrow race rather than the normal case — the CLAIM collision check fires and issues a
   **targeted DENY** to the loser only, keyed on its hash, so the recorded mobile is untouched.
   ⛔ **The DENY is never the allocator** (§5.3.2). ⚠ **UPDATED 2026-08-07 ([[B147]]): this is now TWO
   checks over two planes, run in a FIXED ORDER, not the single registered-row scan the line reference
   above described** — the claimant's **own** reservation is resolved first (a mismatching id ⇒ stale ⇒
   drop, retain, **no DENY**), then the registered-row collision, then the reserved-elsewhere collision.
   See §5.3.2's `(hash, proposed_id)` ruling for the full order and the reason it is an order;
6. the denied mobile **retries with another id**;
7. throughout, ★ **the mobile's cryptographic hash identity never changes** — the local id is a routing
   convenience, the hash is the identity.

★★ **Epoch alone is not the collision protection. Hash matching, reservation-aware free-id selection and —
as a backstop — the targeted CLAIM DENY jointly provide it.** The epoch distinguishes *generations of one
mobile*; it cannot distinguish *two different mobiles*, and a design that leaned on it alone would mis-handle
exactly this case.

**Required native test — expired-id return.** Not a variant of an existing case; its own scenario:

1. register mobile **A** at host **H**, confirmed;
2. **expire and physically remove** A's row (§9.1);
3. **assign A's old local id to a different mobile B**, registered and confirmed;
4. **return A with its stale state** (same remembered home, local id and epoch);
5. **prove A's stale P exchange does not confirm** — no roster match, so no `attached`;
6. **prove B is unchanged** — its row, local id and epoch are untouched at every step;
7. **prove A enters `recovering` and is issued a different local id**;
8. ★ **prove stale traffic addressed to A's old local id is not delivered to B as A** — the last-mile
   decision must be hash-anchored, not local-id-anchored.

Step 8 is the point of the test; steps 5–7 are the setup that makes it reachable.

## 10. Diagnostics

Extend `mobile status` additively with:

- `attachment`: dormant/seeking/claiming/attached/recovering;
- `home_link`: unknown/confirmed/checking/lost — **reported separately**, never folded into the line above;
- `home_desired`;
- current retry attempt/window and next-attempt delay;
- current scan index/count;
- offers collected in the current transaction;
- candidate count and verified-candidate count;
- last result: no_offer, tx_rejected, defer_full, claim_unconfirmed, denied, confirmed;
- **age of the last chosen-home confirmation** — always present, never suppressed when "healthy".

★ **Wording is part of the contract.** Every surface — `mobile status`, the companion app, the Heltec OLED —
says **home link**, and renders the age with it: **"Home confirmed 7 min ago"**, not an unconditional green
**Connected**. ⛔ **The phrase "network connected" (or any unqualified "connected") must not appear**: it
claims the mesh plane on the strength of a home-plane measurement (§4.1). A stale confirmation renders as its
age, not as a failure and not as success.

⛔ Do not render a local TX-admission failure as a home-link state; it belongs in `last result` (§6.4).

The host `routes`/hosting section should print each row as direct or redirect plus its age. `status` already
exposes `txdrop`; add an OFFER-ring overflow counter and a transmitter-rejection counter if they cannot share
an existing admission counter.

#### ★★★★ §10 FIELD LEDGER — WHAT IS DEVICE-VISIBLE, AND WHO OWNS EACH REMAINDER (§MH-S4b, 2026-08-08)

⛔ **This table exists because two slices had already quietly inherited the same §10 debt.** Every field above is
listed exactly once, with the slice that landed it or the slice that OWNS it. A field with no owner named here is
a bug in this table, not an implicit "later".

| §10 field | state | where |
|---|---|---|
| `attachment` | ✅ landed S4 | `mobile status` JSON |
| `home_link` (separate field, never folded) | ✅ landed S4 | ″ |
| `home_desired` | ✅ landed S4 | ″ |
| current retry **attempt** | ✅ landed S4 | `claim_retries` + `claim_retry_max` |
| current retry **window** | ✅ landed **S4b** | `retry_window_ms` = §5.2's no-host backoff accumulator |
| **next-attempt delay (remaining ms)** | ⛔ **STILL OPEN AFTER S5 — [[B154]](a), DECLINED WITH A REASON, NOT INHERITED** | needs either a `Hal` accessor for a pending timer's REMAINING time (an interface change across `DeviceHal`, the sim wrapper and every test fake) or a stored deadline member. ⛔ Deliberately NOT faked from a nominal constant: while `claiming` the ask/wait phase is instead reported honestly by `claim_solicited` + the two constants, and printing a nominal as if it were a remaining time is the display-shaped-field error this whole plane exists to prevent |
| current scan index/count | ✅ landed S4 | `scan_idx` / `scan_count` |
| offers collected | ✅ landed S4 | `offers` |
| candidate count | ✅ landed S4 | `candidates` |
| **verified**-candidate count | ✅ landed **S5** | `mobile_status` JSON `verified_candidates` + `Node::mobile_verified_candidate_count()`. **DERIVED, zero new state.** Applies BOTH halves of §8.1/§8.2 — `echo_tier != 0xFF` **and** freshness within `mobile_liveness_ms` — so the number rendered is the number `presence_maybe_rehome` would actually consider |
| last result | ✅ landed S4 | `last_result` |
| age of last chosen-home confirmation | ✅ landed S4, **64-bit end to end in S4b** | `home_confirm_age_ms` — the u32 cast that wrapped it at ~49.7 days is gone |
| solicitation substate | ✅ added **S4b** | `claim_solicited` (only while `claiming`) |
| OFFER-ring overflow counter | ✅ landed **S4b** | `status` text `offerfull=` + JSON `offer_full` |
| transmitter-rejection counter | ✅ landed **S4b** | `status` text `offerrej=` + JSON `offer_reject` |
| host `routes`/hosting rows as direct-or-redirect **plus age** | ✅ landed **S5** | `status` prints one line per row under `hosting=<n>`: `m[i] hash=0x… local=<id> DIRECT`/`REDIRECT-><home>` + `age=<s>s/1500s` (`src/firmware_commands.cpp` via the read-only `Node::host_mobile_row()`). ★ The age is the §9.2 ROW LIFETIME clock — the same value `mobile_reg_age_out()` tests — so the display and the expiry decision cannot disagree |
| device logs distinguish scheduled / transmitter-admitted / **confirmed** | ✅ completed **S4b** | scheduled+admitted landed in S1b (`mobile_offer_scheduled` / `mobile_offer_tx`); the CONFIRMED third had NO metal surface (`MR_EMIT` is device-stripped) and is now a `_hal.log` at the roster confirmation, naming the re-CLAIM count |

Device logs must distinguish **scheduled**, **transmitter-admitted**, and **confirmed**. An event named
`mobile_offer_tx` must not continue to mean only "copied into a stash."

## 11. Implementation slices

The ordering exists to keep **attribution** possible: exactly **one** slice (S3) is permitted to move the
RNG stream, so every other slice's corpus movement is a finding rather than noise (C1, C4).

### S0 — characterization reproductions and observability

Reproduce each defect as a native test **before any fix exists** — committed **GREEN**, asserting the
defective behaviour.

⛔ **NOT a committed failing test.** `pio test -e native` is the committed gate (D1), so a red test would
have to be skipped or disabled — **and a disabled test is precisely the instrument that cannot fail**, which
is the defect class this entire arc exists to stop. Writing the tests *after* each fix is worse: a test
written after a fix has never been shown to detect the defect, which is the provenance S0 exists to supply.
⇒ **the operational rule is:**

1. S0 commits **clearly labelled CHARACTERIZATION tests that assert TODAY'S DEFECTIVE BEHAVIOUR** — each one
   naming, in-source, the defect, the spec section that rules against it, the correct behaviour, and the
   slice that owns its rewrite. ⛔ An unlabelled characterization test reads as a test **endorsing** the bug.
2. Each is **mutation-proven capable of failing** before it is accepted (the control below).
3. **The owning fix REWRITES the same test in place**, so the behaviour change is visible in the diff.
4. ⛔ **Deletion or disabling is FORBIDDEN** — a deleted characterization test destroys exactly the evidence
   the rewrite exists to show.

The five defects to reproduce:

1. **one pending OFFER overwrites another** — two DISCOVERs inside the host jitter window, the first mobile's
   targeted OFFER is destroyed ("last DISCOVER wins", §2.2);
2. **synchronized mobiles stay synchronized** — mobiles started together remain phase-aligned across retry
   rounds because the no-host retry has no jitter (§2.2);
3. **the guard starts before admission** — the OFFER window is measured from the request to transmit, so a
   NAV/LBT defer longer than the window closes it before handoff (§2.3);
4. **a lost CLAIM creates a false registration** — the mobile reports active while the home has no row
   (§2.4);
5. **a host row survives past 25 minutes** — still in `_mobile_reg`, still in the emitted roster, still
   holding its local id (§2.6).

★ **Each reproduction must carry a mutation control, run at S0 time, not "later": applying the minimal shape
of the corresponding fix to `lib/core` must turn THAT test RED — and no other test.** A reproduction without
that control cannot prove which branch it exercised, and this arc has already shipped controls that were
vacuous. ⚠ **The mutation harness must ASSERT ITS MATCH COUNT** (exactly one site): a replacement that
matches zero sites silently no-ops, and "the mutation reddened nothing" then reads as *"the test is
vacuous"* — backwards. ⚠ **A single match is necessary but not sufficient** — a mutation can match once and
still be semantically inert (measured: gating the claim-guard arm on `!tx_initiating(...)` changes nothing,
because `tx_initiating` returns **true** on the defer path, `node_mac.cpp:1123`). A mutation that reddens
nothing must be **investigated**, never reported as a vacuous test.

★ **Every assertion must be able to fail.** No `>=`/`!= 0` where the exact value is knowable; no prose
claiming more than its check pins; no scan whose match count is unasserted; no comparison between two
constants the test itself just assigned.

Observability added here (state exposure, counters) is the instrumentation these five need — nothing more.

### S1 — admission boundary only

- Propagate the transmitter-admission result through DISCOVER, OFFER and CLAIM (§6).
- Anchor the claim guard **after** admission; report `tx_rejected` / `defer_full` instead of `mobile_no_host`.
- Establish that an admission failure never touches the home-link plane (§6.4).

⛔ **No RNG changes in this slice.** Constraint: **s18 and ALL corpus scenarios byte-identical.** If anything
moves, the slice is wrong — that is the whole point of separating it from S3.

### S2 — keyed pending-OFFER scheduler

- Introduce `cap_pending_mobile_offers` as an **independent** constant (§5.3.1).
- Replace `_pending_offer` with the **node-global** keyed ring (§5.3.2).
- ★★ **Implement PENDING-ID RESERVATION (the B137 ruling, §5.3.2)** — reserved at OFFER admission, excluded
  by `find_free_mobile_id()`, retained across a duplicate DISCOVER, expired by timer 80's scan. ⛔ **The
  CLAIM-collision DENY is a race backstop only; it is NOT the allocator.** Without the reservation, S2's
  ring merely converts one masked defect into four colliding CLAIMs and four discovery rounds.
- Re-shape **timer 80** as a deadline scan on the parked-reflood / E2E-ack precedent; **`kCap` stays 91**
  (§5.3.3).
- **Memory go/no-go FIRST**, before the scheduler lands: record size + placement, deliberate `sizeof(Node)`
  assertion update, full per-board RAM grid, the tightest board's percentage.

Preserve the **existing single-mobile draw behaviour** wherever practical: with one discovering mobile the
host should draw the same jitter, in the same order, as today. Expected corpus movement: the
**concurrent-discovery** scenarios (which were previously mis-serialised by the overwrite) move; **unrelated
scenarios do not**. Any unrelated mover is a defect in this slice, not a re-anchor.

### S3 — jitter-only re-anchor

- Jittered no-host retry (§5.2), boot startup jitter, per-mobile CLAIM de-synchronization (§5.4).

★ **This is the ONLY planned RNG re-anchor in the arc.** Every moved **mobile** scenario is attributed here,
individually, with its new md5 — no blanket waiver. **Static scenarios stay byte-identical**; a moved static
scenario means a mobile draw leaked into the static plane (C3) and blocks the slice.

### S4 — confirmed attachment FSM

- Make `dormant/seeking/claiming/attached/recovering` observable, and the **home-link states independently
  observable** (§4.1).
- Activate `presence_claim_max_retries` and `_presence_reg_confirmed`.
- Move the `registered:true` push to matching-roster confirmation (§7.1).
- Make manual registration a durable in-RAM home-service request, not one attempt; add `mobile unregister`.
- Link-confidence rules and the confirmation-age surface (§7.2).

### S5 — candidates and lifecycle

- Static-beacon wakeup for seeking/recovering mobiles (§5.1).
- Passive-only candidate collection, freshness, bidirectional verification, hint-vs-authority (§8.1–8.2).
- The three searching-probe triggers and "adequate before optimal" (§8.3), hysteretic switching including
  verified same-PHY cross-layer candidates and the preserved team-PHY restriction (§8.4).
- One removal primitive + 25-minute direct/redirect age-out (§9.1–9.3).
- **The expired-id return case and its native test (§9.4).**

### S6 — product integration

- Fresh default `mobile_autoregister=false`; preserve valid NV values.
- Make every simulation's desired mode explicit; add the late-home and auto-OFF scenarios (§12.2).
- Update companion controls/status decoding — attachment and home link as **separate** fields.
- Heltec setup exposes Register/Unregister, both state axes and the confirmation age; no hidden boot
  registration.

## 12. Required gates

### 12.1 Native/core

1. Four concurrent DISCOVERs at one host produce four correctly targeted OFFERs; no armed entry is
   overwritten. A same-mobile duplicate coalesces **while its OFFER is still armed**; a same-mobile
   **re-DISCOVER after transmission re-arms the same slot and keeps the same id** (§5.3.2 item 2 — ⛔ the
   original wording of this gate said only "coalesces" and would have passed a build that answered every
   re-DISCOVER with silence).
   ★★ **AND THE GATE MUST DRIVE THE ID ALLOCATION, NOT ASSUME IT (B137, §5.3.2).** Until S2, B137 and the
   single-OFFER slot **mask each other** — S2 removes the mask, so this gate is the first thing that ever
   exercises the path. It must assert, on the wire: the four OFFERs propose **four DISTINCT local ids**;
   a **duplicate DISCOVER keeps the same proposed id** (no re-draw, no second reservation); a reservation
   **expires** if its mobile never CLAIMs, and the id becomes offerable again; the four CLAIMs that follow
   are each accepted with **no DENY emitted** — a DENY on this path is a **failure of the gate**, because
   the targeted CLAIM-collision DENY is a race backstop only.
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

Added by the 2026-08-07 rulings (these are additional, not replacements):

16. **`TimerWheel::kCap` is exactly 91** — asserted, not inspected. No new timer id is allocated.
17. **Four concurrent mobiles receive four correctly targeted OFFERs**, with no armed entry overwritten by
    any other mobile's admission.
18. **A repeat DISCOVER for the same hash consumes no extra slot**, and **both** arms are asserted
    separately (§5.3.2 item 2): while the OFFER is **armed** it coalesces and **does not move the existing
    deadline**; **after transmission** it **re-arms the same slot and re-proposes the SAME reserved id**.

19. **A full ring refuses explicitly** (`full`) and **leaves every armed entry unchanged** — no eviction, no
    deadline movement, and the ring-full counter increments.
20. **A local TX-admission failure does not set the home link to `lost`** (nor to `checking`) — it appears
    only as `last result` (§6.4).
21. **A matching roster confirms BOTH planes** — attachment *and* the bidirectional home link, with the
    confirmation age stamped (§7.1).
22. **A home beacon alone cannot confirm the link** — receiving beacons while probes go unanswered still
    walks the link to `lost` (§7.2).
23. **A mesh no-route result does not mean the home link is lost** — an unroutable destination leaves the
    home link `confirmed` if its own evidence is fresh (§4.1).
24. **Healthy adequate home + measurably stronger candidate ⇒ no searching traffic and no switch** —
    asserted as *zero* additional transmissions, not merely "no adopt" (§8.3).
25. **Weak home + fresh verified candidate ⇒ switch after hysteresis** — tier delta, 60 s hold and the
    5-minute dwell all enforced (§8.4).
26. **A team mobile never scans or adopts an incompatible PHY**, in recovery or otherwise, beyond what the
    existing team-PHY rule permits (§8.4).
27. **Steady-state airtime is no worse than the existing adaptive cadence** — the §7.2 airtime gate,
    measured at each quality tier with other eligible statics audible.
28. **The strong-link idle-loss test documents the accepted ≈8-minute maximum detection interval** — the
    test asserts the bound and names it as the accepted trade-off, so a later "regression" is read as the
    ruling it is (§7.2).
29. **The expired-id return test follows §9.4** — all eight of its steps, ending with "stale traffic is not
    delivered to B as A".

Added by the 2026-08-07 §MH-S2b / §MH-S2c independent-QA rounds (additional, not replacements):

30. **An overdue OFFER re-arm uses a strictly positive delay** — driven through a **real `TimerWheel` with
    the production multi-fire drain** at a fixed timestamp, so *"at most one frame per callback"* is
    measured against the pump that actually exists, not against a fixture that invokes one callback per
    call ([[B145]], §5.3.3).
31. **The OFFER-rejection counter is incremented by the reporter, not by one call site** — asserted at
    **exactly one** increment on all three refusal paths (immediate `defer_full`, immediate HAL rejection,
    **deferred** HAL rejection) and **zero** on both success arms ([[B146]], §6.2).
32. **A CLAIM is matched on the PAIR `(key_hash32, proposed_node_id)`** ([[B147]], §5.3.2): a live
    reservation for a **different** id ⇒ dropped as stale, reservation **retained**, **no DENY**; an id
    reserved for another hash with no promise of the claimant's own ⇒ targeted DENY naming the reservation
    holder; no live reservation at all ⇒ still recorded; an **elapsed-but-unswept** reservation blocks
    nothing.
33. ★★ **The staleness test runs BEFORE the registered-row collision check** (§MH-S2c). The gate must
    construct the one arrangement in which the two branches disagree — the stale id sitting in a
    **REGISTERED ROW** while the claimant holds a live reservation for a **different** id — and assert:
    **no DENY**, the existing registration **unaltered**, the newer reservation **RETAINED**, and the
    claimant's subsequent CLAIM for *that* id **accepted**. ⓘ The retained-reservation assertion is the one
    that separates the correct early return from the tempting half-fix that reads the reservation and then
    falls through anyway.

✅ **DISCHARGED 2026-08-11 BY §MH-S5b — gates 11 (its unverified half), 24 (its zero-additional-transmissions half),
27 and 28.** Evidence: `simulation/BASELINE.md` §MH-S5b; the cases live in `test/test_node_join.cpp` and each is
mutation-proven at match count 1. What each gate now asserts, so a later reader does not re-derive it:
- **11** — an unverified candidate **on our own layer** (asserted, so §MH-S5's layer rejection cannot be the refusal),
  three tiers better, fresh, both hysteresis windows served ⇒ **no switch**; the same candidate once it echoes OUR
  hash ⇒ switch; an echo of **somebody else's** hash ⇒ still no switch.
- **24** — the previous form fired no timer, so it could not tell "no canvass" from "nothing was scheduled". It now
  **drives the probe deadline** and asserts the frame that leaves the radio is **SELECTED**, that it is the **only**
  frame, and that the stronger verified candidate is still counted as admissible (a refusal by policy, not blindness).
- **27** — per quality tier, with another eligible home audible: the check period **T** is the pre-existing §S6.3
  value, three periods produce exactly three probes at exactly **3 × 8 bytes**, and an audible candidate's roster
  produces **zero** frames from the mobile. ⚠ It measures the MOBILE; the answering rosters are corpus-measured in
  §MH-S5b (mobile probe airtime **falls** 30 821 → 25 702 ms; roster airtime **rises** 33 518 → 43 752 ms).
- **28** — the strong-link idle-loss interval is asserted as an **equality**, 495 000 ms (480 s + 3 × 5 s), with the
  ≤ 527 000 ms on-metal jittered worst case beside it, and the in-source block states that shortening the strong-tier
  period is a **design change to be argued**, not a regression to be filed. Metal residue: bench script **18.1**.
⛔ **Gate 26 is unchanged and was already discharged by §MH-S5**; §8.3's **trigger 3** is still unimplemented, so no
gate here covers it.

Every positive must have a nearby negative or mutation control that proves the asserted branch executed.
★ For each **S0 characterization** test the control is the inverse and is run **at S0 time**: applying the
minimal shape of the owning fix to `lib/core` must turn that one test RED and no other (§11 S0). When the
owning slice then lands, the same test **goes red for real and is rewritten in place** — ⛔ never deleted,
never disabled.

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

Static keystone scenarios must remain byte-identical **in every slice**. Mobile scenarios may move only in
**S2** (concurrent-discovery scenarios, from the corrected serialisation) and **S3** (the single planned RNG
re-anchor). Every mover is attributed individually to one of those two slices, with its new md5; nothing is
waived globally, and a mover in S1, S4, S5 or S6 blocks that slice.

### 12.3 Hardware

1. Power four mobiles first; wait until each is in its capped retry state.
2. Provision one ordinary static home without touching the mobiles.
3. Capture J DISCOVER/OFFER/CLAIM and P confirmation on both sides.
4. All four must show attachment `attached` **and** home link `confirmed` with a fresh age; the home must
   show four rows. No surface anywhere says "network connected".
5. Repeat with NAV traffic during registration; no two-second premature close.
6. Power one mobile off. Confirm proxying stops and the row is physically absent at the configured expiry
   boundary (use a temporary bench constant for time-compressed validation, then rebuild defaults).
7. Bring a stronger same-PHY static node into range; confirm no switch **and no extra transmissions** while
   the current home is healthy, then attenuate the home to weak and confirm canvass + hysteretic switch.
8. Walk a mobile out of range of a **strong** home immediately after a confirmation and generate no traffic;
   confirm detection within the documented ≈8-minute bound, and that the panel shows a **growing
   confirmation age** rather than an unqualified connected state throughout.

## 13. Documentation consistency obligations

When implemented, update together:

- `docs/protocol.md` presence-plane lifecycle **and the three-plane distinction** (§4.1);
- `docs/frames.md` behavioural registration text (wire bytes stay unchanged);
- `docs/2026-07-30-open-bug-register.md`;
- `docs/2026-07-31-bench-test-script.md` — the metal-only residue: the ≈8-minute strong-home detection bound
  and the expired-id return, neither of which an automated gate reaches on real timing (M2);
- companion mobile-status/command contract — **attachment and home link as separate fields**;
- simulation `BASELINE.md` with exact RNG/corpus movers, attributed to S2 or S3;
- stale source comments promising a periodic re-CLAIM or calling OFFER "deliberately single-slot";
- `protocol_constants.h` — `cap_pending_mobile_offers` documented as **host-side**, with an explicit note
  that it is **not** related to `cap_mobile_offers` beyond a coincidental starting value.

~~Do not describe `mobile_liveness_ms` as a home-side prune until the physical compaction and its boundary
tests exist.~~ ✅ **CONDITION MET 2026-08-10 (§MH-S5): both now exist** — `Node::mobile_reg_age_out()` physically
compacts the row and its parallel arrays at the boundary, and the boundary is asserted from BOTH sides for BOTH row
kinds, with the age-out predicate mutation-proven. ⇒ it **may** now be described as a home-side prune, and
`protocol_constants.h`'s own comment does so with its four consumers enumerated.

⛔ **No surface may state or imply that a P roster proves general mesh connectivity.** A roster proves
attachment and a live home link, and nothing beyond the home (§4.1).
