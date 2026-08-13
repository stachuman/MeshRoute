<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B183-DIAG — where does the post-loss reattachment chain stop? · diagnosis-only brief · 2026-08-12

**Status: READY FOR DIAGNOSIS after QA correction on 2026-08-12.** This is a diagnosis boundary, not a recorded
owner ruling and not a claim that QA authored the original brief. The coder gathers and reports evidence; the
owner decides whether a later firmware or scenario slice is justified.

⛔⛔ **DIAGNOSIS ONLY. THE DELIVERABLE IS EVIDENCE, NOT A FIX.** Do not change firmware behaviour, B178 policy,
the 36-row corpus, scenario anchors, or the delivery floor. When the failing transition or scenario precondition is
established, stop and report it. Never commit or discard the existing working-tree changes.

**Current baseline:** HEAD **`c7bca52`**; native **1512 / 81212 / 0**; `lus` **`316b9cb1`**; corpus 36 rows.
B182's revised delivery tool reports **754 overall / s06 110 / s07 84 / s27 15 of 15**, but the delivery floor is
still frozen and unratified. Those figures are context only and are not an acceptance gate for this diagnosis.

---
## 1 — Observation and question

In `s07`, after a late home loss, all three mobiles continue discovering but never reattach while an apparently
eligible static remains within 0.4–1.7 km. Whole-run telemetry contains 76 `mobile_offer_tx` and 59
`mobile_offer_reservation_expired` events.

⚠ Those totals do **not** identify a failing transition. A single DISCOVER may reach several hosts, each host may
send an OFFER, the mobile selects at most one, and reservations for the unselected offers are expected to expire.
Consequently, an OFFER expiry is not inherently a failed reattachment and must not be the last row of a linear
funnel.

The diagnosis must answer:

1. Does the failure reproduce after a controlled home loss in a correctly configured minimal scenario?
2. If it does, at which edge of the selected attachment transaction is progress lost?
3. Is the cause firmware state/logic, RF delivery, receiver timing, collision, or an `s07` authoring precondition?

`s07` predates the current attachment design: its mobiles have fixed `node_id`s 50/51/52, set no
`mobile_autoregister`, no static sets `host_mobiles`, and its mobile DMs use `send_e2e <name>`. B182 established
that these facts affected DM attribution/addressing. Do not assume that they do or do not explain presence-plane
reattachment; the controlled scenario exists to separate those questions.

---
## 2 — Observe a transaction graph, not a funnel

For each post-loss DISCOVER actually transmitted, reconstruct this graph:

```text
DISCOVER PHY tx
  ├─> host A PHY rx ─> OFFER A PHY tx ─> mobile PHY rx ─┐
  ├─> host B PHY rx ─> OFFER B PHY tx ─> mobile PHY rx ─┤ choose at most one
  └─> ...                                                ┘
                                  selected OFFER
                                        │
                                  CLAIM PHY tx
                                        │
                                  chosen host PHY rx
                                        │
                                  host registration
                                        │
                         matching roster returned to mobile
                                        │
                              confirmed attachment
```

### 2.1 Identity and correlation

Do not invent an attempt index that the firmware does not emit. Use the simulator's unique packet record and the
decoded J-frame contents:

- DISCOVER transaction: `(mobile key hash, raw tx pkt, tx time)` plus its collection window;
- OFFER: `(responder/host, target mobile hash, proposed local id)` plus raw tx `pkt`;
- CLAIM: `(mobile hash, chosen host, proposed local id, epoch)` plus raw tx `pkt`;
- confirmed attachment: the same runtime tuple `(mobile hash, leased local id, epoch)` observed by the mobile.

Where two wire payloads are byte-identical, the simulator `pkt` identity and time distinguish the physical
emissions. Decode J frames according to `frame_codec.cpp`; do not correlate by configured mobile `node_id`.

### 2.2 Evidence strength

The existing named emits are not all proof of the event their English name may suggest:

- `mobile_discover_tx` is emitted before `tx_initiating`; it proves intent, not transmitter admission or airtime.
- `mobile_offer_tx` is emitted at the accepted transmitter handoff; it is stronger than staging, but is not by
  itself proof that the simulator delivered the physical frame.
- `mobile_adopted` proves provisional adoption/CLAIM staging, not final confirmed attachment.
- `mobile_registered` proves that the chosen host accepted and stored the CLAIM.
- `mobile_attach_confirmed` is the final mobile-side proof that a matching roster `(hash, local id, epoch)` returned.

For DISCOVER, OFFER, and CLAIM, use raw simulator `type:"tx"` records as physical transmission evidence and match
raw `type:"rx"` records by `pkt` at the intended receiver. Use the matching loss records (`collision`, `drop_weak`,
`drop_sf_mismatch`, `drop_preamble_miss`, `drop_rx_blind`, or another explicit simulator outcome) to explain a
missing receive. Named firmware events may corroborate the state transition but must not substitute for PHY
evidence.

There is no dedicated production `mobile_discover_rx`, `mobile_offer_rx`, or first-CLAIM rx emit. If the raw packet
records and decoded J frame cannot establish one of those edges without modifying firmware, record the exact
observability gap and stop; do not add telemetry in this slice.

### 2.3 Expected and suspicious reservation outcomes

Partition OFFER reservations by transaction identity:

- expiry of an OFFER that the mobile received but did **not** select is expected;
- release after the matching CLAIM reaches that host is expected;
- expiry of the OFFER selected by the CLAIM, before the matching host registration, is suspicious;
- a whole-run count of all expiries must never be presented as the last stage of an attachment funnel.

Report the graph separately for the **post-loss interval**. Whole-run totals mix initial attachment, successful
transactions, losing offers, and the suspected late failure and therefore cannot locate B183.

---
## 3 — Causes to test positively

For every missing graph edge, classify the result using positive evidence:

1. **RF/link budget:** was the frame transmitted and receivable at the intended receiver? Report the actual PHY
   parameters, SNR/link budget and any `drop_weak`; distance alone is not evidence.
2. **Receiver timing/state:** was the receiver listening on the correct frequency, SF, bandwidth, layer and window
   for the frame's airtime? Name an explicit `drop_rx_blind`, preamble miss, PHY mismatch, or prove the receive
   interval overlaps.
3. **Collision/contention:** correlate the lost `pkt` with a collision record and the overlapping transmissions;
   do not use a global collision total as proof about this transaction.
4. **Protocol eligibility/state:** prove the candidate is a live, non-gateway static with `host_mobiles` enabled
   and show whether it accepts, schedules, transmits or refuses the OFFER. For the chosen path, follow the exact
   lease and epoch through CLAIM, host registration, roster, and `mobile_attach_confirmed`.

Silence is not exclusion. Every zero must have a control demonstrating that the instrument can become nonzero.

---
## 4 — Minimal controlled scenario and controls

Develop the scenario under `/tmp`, but do not leave the only reproducer there. After minimisation, preserve the
exact scenario and any nontrivial parser/generator under `docs/superpowers/evidence/b183/`. This is a durable,
non-corpus diagnostic fixture: it must not be added to `simulation/`, the 36-row runner, or any anchor table.

Use the fewest nodes that can represent the mechanism—normally one mobile, an initial home that deliberately goes
away, and one eligible replacement static. State explicitly:

- unprovisioned mobile start and the service trigger used (`mobile_autoregister` or an explicit register request);
- which statics set `host_mobiles`, and that gateways cannot host;
- GLOBAL versus TEAM plane;
- a confirmed initial attachment before the controlled loss;
- the exact loss event and the eligible replacement remaining afterwards;
- assertions on `(mobile hash, leased local id, epoch)`, never the configured mobile `node_id`.

No DM is required to prove reattachment. If one is included as an end-to-end control, address by stable hash and
keep its result separate from the presence transaction.

### Required controls

1. **Healthy positive control:** without the home loss, prove a full graph through `mobile_attach_confirmed`.
2. **Post-loss positive control:** configure an easy, collision-free replacement and prove that the instrument can
   observe a complete reattachment if the implementation permits one.
3. **Eligibility mutation:** disable `host_mobiles` on the replacement; its DISCOVER reception may remain, but no
   valid OFFER may be selected from it.
4. **Edge-specific mutation:** once a suspected edge is found, perturb only the responsible RF/timing/state input
   and show that the matching raw receive or semantic transition changes as predicted.

Do not require a mutation to produce a vaguely "earlier" or "later" stop. State the exact graph edge expected to
change before running it, and reject a control that cannot exercise that edge.

---
## 5 — Allowed changes and gates

- No firmware behaviour or telemetry changes: no `lib/` or `src/` edits.
- No changes to `simulation/`, corpus runners, anchor tables, B178 policy, or the delivery floor.
- Diagnostic scripts and scenarios may be developed in `/tmp`; the final minimal reproducer and any necessary
  parser may be copied only to `docs/superpowers/evidence/b183/` with a short README or findings report.
- The bug register and this plan may be updated with measured findings.
- Print the full `lus` md5 beside every result set and record the exact invocation.
- Record recommendations as recommendations. Do not attribute approval or rulings to the owner or QA unless the
  thread contains the corresponding direct statement.
- Preserve all pre-existing working-tree changes. Never commit, stage wholesale, switch commits, or discard files.

---
## 6 — Required report

1. Current HEAD, native result, `lus` md5, and exact commands.
2. The post-loss transaction graph for `s07`, including every fan-out host and the selected-offer branch.
3. The same graph for the minimal scenario and each control.
4. The first missing or rejected **edge**, named with the raw packet/state evidence that locates it.
5. Per-edge RF, receiver timing, collision and eligibility results; global counts are supplementary only.
6. Reservation outcomes split into selected and unselected offers.
7. Evidence of final success only from matching `mobile_attach_confirmed`, not merely `mobile_adopted`.
8. Whether the defect reproduces outside `s07`; if not, the smallest `s07`-specific precondition that restores it.
9. Durable fixture/report paths plus exact replay instructions.
10. What could not be established and any missing observability.
11. `git status --short`, identifying pre-existing changes and proving no production, corpus, anchor, or floor edit.

⛔ Stop after locating the failing edge or disproving reproduction. Do not propose or implement a fix in this slice.
If the cause is RF, timing, collision, or scenario configuration, report B183 as such rather than forcing a
firmware diagnosis. If locating the edge requires new core telemetry, record that observability requirement and
stop.

---
## 7 — RESULT (appended 2026-08-12 by the diagnosis run) — ⛔ EVIDENCE ONLY, NO FIX, NO APPROVAL CLAIMED

**Full report + durable non-corpus fixture: `docs/superpowers/evidence/b183/README.md`.**
HEAD `c7bca52` · native **1512 / 81212 / 0** · `lus` **`316b9cb1`** · `s07` **`b3b7ce31` / 107989** (the anchor).

★ **THE FAILING EDGE IS NOT A PROTOCOL DECISION — IT IS THE TRANSMITTER.** A J DISCOVER/OFFER/CLAIM is tagged
`FrameTag::beacon`; `retry_slot_of(beacon)` = **−1** (`node_mac.cpp:1604`), so (a) `tx_with_retry`'s duty
pre-check is skipped (`:1720`, gated on `slot >= 0`) and (b) `Node::on_radio_busy` **returns at `node.cpp:2203`
with no retry and no attributable emit**. The simulator's `_hal.tx()` answers `ok` and refuses later via
`onRadioBusy`, so `lbt_complete` has already emitted `mobile_offer_tx` (`node_mac.cpp:1497`) for a frame that
never existed on air.

⚠⚠ **SCOPE, NARROWED AFTER INDEPENDENT REVIEW:** this is a **core handling defect demonstrated through the
simulator's ASYNCHRONOUS busy callback**. **Equivalent metal behaviour is UNVERIFIED** — `DeviceHal::tx` refuses
**synchronously**, which would surface as `tx_hal_rejected`, a **different** path this slice did not measure.
⛔ It must not be described as a general modem/hardware failure, and ⛔ no claim of metal invisibility is made.

- **`mobile_walk_central` / `mobile_bike_west_east`** stop at **edge C's last hop** (`mobile_offer_tx` → OFFER
  PHY tx): post-loss **45 of 45** OFFERs staged + handed off, **0** transmitted, each with a same-millisecond
  `tx_deferred{label:"BCN", reason:"duty_cycle_exceeded"}`.
- **`mobile_courier_south_north`** stops at **edge A** (intent → DISCOVER PHY tx): **14 of 14** post-loss
  DISCOVERs never aired, same reason, at its own modem.
- Whole run: `mobile_discover_tx` **56 → 33** on air · `mobile_offer_tx` **76 → 28** on air; **69 of the 71
  losses bound EXACTLY (59 duty, 8 self-TX, 2 channel-busy) and 2 REFUSED as ambiguous** — and **all seven**
  attributable-loss counters (`tx_deferred_lost`, `mobile_offer_dropped`, `mobile_offer_admission_rejected`,
  `mobile_tx_rejected`, `tx_hal_rejected`, `mobile_tx_cancelled_stale`, `mobile_offer_ring_full`) read **0**,
  so the loss takes the path the arc never instrumented.
  ⛔⛔ **CORRECTED IN PLACE: this line first said "100 % of the 71 losses attributed". That was an artefact of
  the instrument** — the first `lostj.py` bound a missing frame to *"the next BCN refusal within 4000 ms"*, a
  **guess**, and the very rule [[B182]] was fixed for. Independent review caught it; the correlator now accepts
  only two exact bindings, **refuses** everything else, and `lostj.py` **exits 1** on `s07` (the correct
  outcome). ★ Neither refused frame is post-loss, so the post-loss graph above is unaffected.
- **The four causes were excluded positively, each with a control:** RF (hosts really received, +0.3…−9.7 dB vs
  a −10.0 dB threshold) · receiver timing/state (hosts ingested and reserved an id) · collision (the lost
  frames have no `pkt` at all) · eligibility (`host_mobiles` defaults **true**, no gateways, hosts accepted).
- **Reservations split as §2.3 asks: 17 expected fan-out losers · 42 whose OFFER never physically existed ·
  0 suspicious.** ⇒ the QA correction was right: the 76-vs-59 funnel identified nothing.
- **Reproduces outside `s07`** in a 3-node fixture (controls C4 host-side, C5 mobile-side). The
  `s07`-specific *trigger* is that `duty_cycle_window_ms` defaults to `duration_ms`, making the 1 % budget a
  one-shot 36 s allowance that 16 of 36 nodes exhaust at t≈2.48–3.12 M.
- **Two silent scenario traps recorded** (both cost a failed control before being found): `velocity_mps: 0.0`
  with `mobile_only` path loss makes a "mobile" static and linkless; and `shapeReportedSnr` saturates at
  **+12 dB**, so geometry cannot bias OFFER selection — arrival order decides.
- **Observability gap recorded, NOT closed (no telemetry added):** `radio_busy` omits the `tag`/`len` that
  `BusyInfo` (`hal.h:40`) already carries, so **within the observed path** `mobile_offer_tx` is the last word.
  ⛔ **This is NOT a claim that the failure is invisible on metal** (see the scope note above). Registered as
  **B186**; the scenario half as **B187**.
- ★★ **HARNESS REPAIRED AFTER INDEPENDENT REVIEW — the preserved fixtures COULD NOT FAIL as first written**
  (C2–C5 had zero `expect` entries; no parser exited nonzero). Added `verify.py`: **36 exact-tuple checks,
  exit 1 on failure, and all 36 of 36 mutation-proved** — plus `selftest.py`: **12 synthetic checks pinning the
  ambiguity refusal and its negative half**, regression-proved (a scratch copy reverted to the retired
  guessing behaviour fails 6 of them). Writing `verify.py` immediately caught a defect in itself (C3 counted
  receives at *eligible* statics, but C3's replacement is ineligible by design).
