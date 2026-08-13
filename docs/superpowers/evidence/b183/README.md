<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B183-DIAG — where the post-loss reattachment chain stops · **DIAGNOSIS ONLY, EVIDENCE, NO FIX** · 2026-08-12

⛔ **Nothing here is an owner ruling or a QA approval.** No firmware, telemetry, `simulation/`, corpus, anchor
or delivery-floor change was made. Recommendations below are recommendations.

**Baseline reproduced exactly.** HEAD `c7bca52`; `pio test -e native` then `./.pio/build/native/program` →
**1512 test cases / 81212 assertions / 0 failed**; `lus` md5 **`316b9cb1`**
(`316b9cb1008e0a78b5f8b2dc1f38526f  ~/lora-universal-simulator/build/orchestrator/lus`);
`s07` stream **`b3b7ce31` / 107989 events**, i.e. the live anchor.

---
## 1 — The answer in one line

**The chain does not stop at a protocol decision. It stops at the transmitter: a J-family frame
(DISCOVER / OFFER / CLAIM) is accepted by `_hal.tx()`, the radio layer then refuses it, and
`Node::on_radio_busy` drops it silently because a J frame is tagged `FrameTag::beacon`, for which
`retry_slot_of()` returns −1.** The named emits `mobile_discover_tx` and `mobile_offer_tx` have already fired
one layer above that loss, so the stream reads as "DISCOVERs go out and hosts answer" when **no such frame ever
existed on air**.

⚠⚠ **SCOPE OF THE CLAIM, stated up front so no later reader over-reads it.** This is a **core handling defect
demonstrated through the simulator's ASYNCHRONOUS busy callback** (`SimController` accepts the frame, refuses
it at the transmit step, and reports it via `onRadioBusy`). **Equivalent metal behaviour is UNVERIFIED:**
`DeviceHal::tx` answers `busy` **synchronously**, which the firmware would surface as `tx_hal_rejected` — a
**different** path this slice did not measure. ⛔ **Do not describe B183/B186 as a general modem or hardware
failure, and do not claim the failure is invisible on metal.**

⚠ **The QA correction was right and the 76-vs-59 funnel was a dead end.** The real signal is not
`mobile_offer_tx` **76** vs `mobile_offer_reservation_expired` **59**; it is `mobile_offer_tx` **76** vs
**OFFER frames actually transmitted 28**.

---
## 2 — `s07` post-loss transaction graph (whole-run totals deliberately excluded)

Post-loss interval = after each mobile's **final** `mobile_reset`. Correlation is by decoded J-frame
`key_hash32` / `target_key_hash32` / `chosen_host_id` plus `pkt`+arrival-window identity — never the
configured `node_id`. (`pkt` is a CONTENT hash reused by byte-identical re-emissions, up to 104×; the parser
binds a receive to an emission by `pkt` **and** `[tx.t, tx.t+airtime+500 ms]`, and **REFUSES** — never consumes —
any window a re-emission falls inside. `txgraph.py` exits 1 if it ever refuses; on `s07` it refuses **0** times.)

| mobile (`key_hash32`) | post-loss from | A intents | A **DISCOVER on air** | B host PHY rx | C staged | C `mobile_offer_tx` | C **OFFER on air** | C OFFER rx at mobile | D CLAIM | `mobile_attach_confirmed` |
|---|---|---|---|---|---|---|---|---|---|---|
| `mobile_walk_central` `0xe27bc270` | 2 985 221 | 10 | **10** | 27 (3–4 distinct hosts/round) | 27 | 27 | **0** | **0** | 0 | **0** |
| `mobile_bike_west_east` `0x9fc4f7d9` | 3 130 069 | 8 | **8** | 18 | 18 | 18 | **0** | **0** | 0 | **0** |
| `mobile_courier_south_north` `0xdb1b5c3d` | 2 634 448 | 14 | **0** | **0** | 0 | 0 | 0 | 0 | 0 | **0** |

Fan-out is real and healthy: each aired DISCOVER is received by **2–4 distinct eligible statics**, each of
which independently stages an OFFER and reserves a local id. Hosts seen offering post-loss: `DMASpaceMesh`,
`devhackchat`, `Stevens`, `CrossNet_Room` (walk); `dmatestbednode0`, `CrossNet_RPT1`, `CrossNet_Room` (bike).

### 2.1 The first missing edge, named

* `mobile_walk_central`, `mobile_bike_west_east` → **edge C, last hop: `mobile_offer_tx` (accepted HAL
  handoff) → OFFER PHY tx.** 45 of 45 post-loss OFFERs died there.
  Locating record, e.g. `DMASpaceMesh` at t=2 991 388:
  `script_emit mobile_offer_tx{to_key:3799761520, local_id:253}` and, **at the same millisecond**,
  `{"type":"tx_deferred","node":"DMASpaceMesh","label":"BCN","reason":"duty_cycle_exceeded"}` with no `tx`
  record for that frame anywhere in the stream.
* `mobile_courier_south_north` → **edge A: `mobile_discover_tx` (intent) → DISCOVER PHY tx.** 14 of 14
  post-loss DISCOVERs died there, same refusal reason, at the mobile's own modem.

### 2.2 Whole-run attribution — **69 of 71**, with 2 REFUSED, and every attributable emit is ZERO

⛔⛔ **CORRECTED IN PLACE. This section first claimed "100 % of the 71 losses attributed", and that figure was
an artefact of the instrument, not a measurement.** The first revision of `lostj.py` bound a missing J frame to
*"the next BCN refusal within 4000 ms"* — a **guess**, and exactly the rule [[B182]] was fixed for. Independent
review caught it. `graph.Stream.refusal_for` now accepts only two **exact** bindings (one same-millisecond BCN
refusal, or one LBT defer whose single fire time carries one BCN refusal) and **REFUSES everything else**, and
`lostj.py` **exits 1** when anything is unattributed. ⚠ The retired horizon was wrong in **both** directions: it
falsely *refused* nothing while falsely *attributing* two OFFERs whose only candidate lay 998 ms and 2150 ms
away — those two are now bound correctly by the same-millisecond rule, which is why the corrected total is 69
and not 67.

```
$ python3 lostj.py <s07.json> <s07.ndjson>          # lus 316b9cb1 · stream b3b7ce31 / 107989
mobile_discover_tx: 56 emits -> 33 DISCOVER PHY tx; 23 never aired
     14  ATTRIBUTED  reason=duty_cycle_exceeded  via=same_ms
      7  ATTRIBUTED  reason=self_tx_in_flight    via=same_ms
      2  ⛔ UNATTRIBUTED (ambiguity REFUSED, never guessed)
         · mobile_bike_west_east t=1375860: no same-ms BCN refusal and 2 concurrent LBT defer
           requests — no exact binding
         · mobile_courier_south_north t=2634448: 2 BCN refusals in the SAME millisecond
           (['duty_cycle_exceeded','duty_cycle_exceeded']) — cannot bind this frame to one of them
mobile_offer_tx   : 76 emits -> 28 OFFER    PHY tx; 48 never aired
     45  ATTRIBUTED  reason=duty_cycle_exceeded  via=same_ms
      2  ATTRIBUTED  reason=channel_busy         via=same_ms
      1  ATTRIBUTED  reason=self_tx_in_flight    via=same_ms
      0  ⛔ UNATTRIBUTED
tx_deferred_lost 0 · mobile_offer_dropped 0 · mobile_offer_admission_rejected 0
mobile_tx_rejected 0 · tx_hal_rejected 0 · mobile_tx_cancelled_stale 0 · mobile_offer_ring_full 0
exit code 1  (the CORRECT outcome: the stream contains 2 genuinely unattributable frames)
```

★ **Neither refused frame is in a post-loss interval**, so §2's post-loss graph is unaffected: `txgraph.py`
exits **0** on `s07` with **0 correlations refused**. The corrected figure does not weaken the finding — 45 of
45 post-loss OFFERs and 14 of 14 post-loss courier DISCOVERs are each bound to a same-millisecond refusal —
it only stops the harness from over-claiming.

★ Those seven zeros are the load-bearing part. The arc **did** make the **LBT-defer-ring** loss attributable
(`node.cpp:1374-1420`: `tx_deferred_lost` + `mobile_admission_rejected` + `mobile_offer_admission_rejected`).
The loss measured here takes a **different, unattributed path**, so not one of those instruments can fire.

### 2.3 The mechanism, verified in source (V1)

1. `jtx_fire` / `mobile_discover_fire` → `tx_initiating` → `lbt_complete`
   (`node_mac.cpp:1434`) tags every J frame `FrameTag::beacon`.
2. `tx_with_retry` (`node_mac.cpp:1700`): the duty **pre-check** at `:1720` is gated on `slot >= 0`;
   `retry_slot_of(FrameTag::beacon)` = **−1** (`node_mac.cpp:1604`) ⇒ **a J frame gets no duty pre-check.**
3. In the simulator, `HalAdapter::tx` → `simTx` **returns `kSimTxOk`** (the frame is queued); the duty /
   LBT / self-TX hard block happens later in `SimController::stepTx` and comes back only as `onRadioBusy`
   (`SimController.cpp:1889-1995`). So `TxResult::ok`, `tx_hal_rejected` never fires, and
   `lbt_complete` emits **`mobile_offer_tx`** at `node_mac.cpp:1497`.
4. `Node::on_radio_busy` (`node.cpp:2183`) emits `radio_busy{reason, busy_until_ms}` — **which carries
   neither a tag nor any frame identity** — then `const int slot = retry_slot_of(tag); if (slot < 0) return;`
   (`:2202-2203`). **For a beacon-tagged frame: no retry, no re-arm, no attributable emit. The frame is gone.**

### 2.4 Why the mesh is out of airtime at exactly that time

⛔⛔ **CORRECTED 2026-08-12 BY OWNER RULING (ledger §1.19): THE DUTY WINDOW IS NOT A DEFECT, A TRIGGER TO REMOVE, OR
AN `s07` AUTHORING FAULT. `s07`'s SATURATION IS VALID STRESS BEHAVIOUR AND THE 1-HOUR ROLLING WINDOW STAYS.**
⇒ Read everything below as *the legitimate stress CONDITION under which the defect becomes observable*, never as its
cause. ⛔ **The defect is solely the core's handling of a busy-refused `FrameTag::beacon` J frame** — no retry, no
attributable emit, while `mobile_offer_tx` has already fired. ⛔ **No `s07` re-anchor is authorised, and its window
and load must not be retuned to restore delivery.**
ⓘ **And it is not `s07`-specific at all — measured 2026-08-12: 36 of 36 corpus scenarios are in the ONE-SHOT duty
regime** (none sets `duty_cycle_window_ms`; all inherit the 1 h default; every duration ≤ 1 h). A 1 h scenario seeing
exactly one window is **correct modelling** of a 1 %-of-rolling-hour limit. The rolling window's boundary behaviour
being untested anywhere is registered separately as **[[B188]]**, to be exercised with a **compressed explicit
`duty_cycle_window_ms`**.

`simulation.radio.duty_cycle = 1` (PERCENT ⇒ 0.01) and `duty_cycle_window_ms` defaults to **3 600 000 ms on
both sides** (`NodeRuntimeWrapper.cpp:259`, `node_carriers.h:213`) — which is **exactly `s07`'s
`duration_ms`**. So within this run the 1 % budget is a **36 000 ms allowance that never rolls** — ⓘ true of every
corpus scenario, not a property of `s07`.

**16 of 36 nodes end the run at ≥ 99.6 % of it**, and the crossing times bracket the failure window:

| node | airtime | % of 36 000 ms | first at ceiling |
|---|---|---|---|
| `DMASpaceMesh` | 35 983 | 100.0 % | 2 476 420 |
| `devhackchat` | 35 920 | 99.8 % | 2 567 830 |
| `Stevens` | 35 882 | 99.7 % | 2 612 833 |
| `mobile_courier_south_north` | 35 887 | 99.7 % | **2 621 890** (its home loss: **2 634 448**) |
| `dmatestbednode0` | 35 881 | 99.7 % | 2 736 657 |
| `CrossNet_Room` | 35 990 | 100.0 % | 3 055 360 |
| `CrossNet_RPT1` | 35 971 | 99.9 % | 3 117 029 |

⇒ **the "last ~15 min has zero hosted-mobile service" is the airtime budget running out, not geometry and not
a presence-plane decision.** The archive entry's "IT IS NOT GEOMETRY" was correct; the reason is airtime.

---
## 3 — The four causes, each tested positively

| cause | verdict | positive evidence |
|---|---|---|
| **RF / link budget** | **NOT the cause** | Every post-loss DISCOVER produced real `type:"rx"` records at 2–4 eligible statics, SNR **+0.3 … −9.7 dB** against the `drop_weak` threshold of **−10.0 dB**. `drop_weak` appears only for the 26–29 far nodes. Reception is proven by the record, not inferred from distance. |
| **Receiver timing / state** | **NOT the cause** | The hosts not only PHY-received, they **ingested and acted**: `mobile_offer_scheduled` + an id reservation fired for the matching `to_key` within 100–1000 ms. No `drop_rx_blind` / `drop_sf_mismatch` / `drop_preamble_miss` on any post-loss DISCOVER path except one single `drop_preamble_miss` at a far node. |
| **Collision / contention** | **NOT the cause at the failing edge** | The lost OFFERs have **no `pkt` at all** — they were never on air, so no `collision` record can name them. The refusal is at the sender's own modem. (One adjacent contention effect IS observed and is not the main mechanism: 8 lost DISCOVERs + 1 lost OFFER die on `self_tx_in_flight`, e.g. `mobile_bike_west_east` t=1 375 860 fires a searching `presence_probe_tx` **and** a DISCOVER in the same millisecond, both LBT-defer into overlapping slots, and the mobile's own probe then blocks its own DISCOVER at t=1 376 394.) |
| **Protocol eligibility / state** | **NOT the cause** | `can_host_mobiles()` = `host_mobiles && !is_mobile && !is_gateway && n_layers==1` (`node.h:503`). `host_mobiles` defaults **true** (`node_carriers.h:99`); `s07` sets no gateways and no opt-outs ⇒ all 33 statics are eligible. Proven live rather than by config: the candidates **accepted** the DISCOVER, staged an OFFER, and reserved an id. |

**Every zero above has a control that makes the instrument non-zero** — see §4 (C1/C2 make edges A–D all
non-zero and reach `mobile_attach_confirmed`; C3 moves the eligibility term alone; C4/C5 move the transmitter
term alone).

---
## 4 — Reservation outcomes, split as §6 item 6 requires

`mobile_offer_reservation_expired` = **59**, bound to its own `mobile_offer_tx` at the same host for the same
`to_key`:

| partition | count |
|---|---|
| OFFER **aired** and was **not selected** by the CLAIM → **EXPECTED fan-out loser** | **17** (all pre-loss) |
| OFFER handed off but **never physically transmitted** → the id was reserved for a frame that never existed | **42** (39 post-loss + 3 pre-loss) |
| selected OFFER's reservation expiring **before** the matching host registration → **suspicious** | **0** |

⇒ no reservation defect exists. The expiries are a **downstream symptom**: an OFFER that never flew can
never draw a CLAIM, so its reservation must time out.

---
## 5 — Minimal controlled scenario and the four (five) controls

3 nodes, GLOBAL plane, no DM, no team id, no manual command (`mobile_autoregister` defaults **true**, so the
service trigger is the automatic boot arm). `HomeA` = the initial home, `dies_at_ms` 120 000. `HomeB` = the
eligible replacement, alive to the end. Assertions are on `(key_hash32, leased local id, epoch)`.

⚠ **Two scenario-authoring traps were hit and are recorded because they are silent:**
1. `velocity_mps: 0.0` + `path_loss.mobile_only: true` ⇒ `SimController::isMobileNode()` is
   `velocity_mps > 0` (`SimController.cpp:128`), so the "mobile" was treated as static and its links fell
   back to the (empty) topology table: **no link at all, 20 `mobile_no_host`, and the C1 positive control
   failed.** Fixed with `velocity_mps: 0.01`. **This is exactly why C1 is mandatory.**
2. Geometry **cannot** bias which OFFER wins: the firmware sees `shapeReportedSnr(snr, ceiling=+12)`
   (`FirmwareNode.cpp:96`), so any two strong OFFERs both report **+12 dB**, tie under
   `snr_db > best.snr_db` (`node_mobile.cpp:220-221`), and **arrival order decides**. `HomeB` is therefore
   given `start_at_ms: 60000` so the mobile deterministically attaches to the node that later dies.

Predicted edge, stated before each run, and the measured result:

| control | edge predicted to move | measured | verdict |
|---|---|---|---|
| **C1** healthy, no loss | none — prove the instrument reaches `mobile_attach_confirmed` | A 1/1 · B 1 host · C staged 1, `mobile_offer_tx` 1, **OFFER on air 1**, mobile rx 1 · D CLAIM 1 · `mobile_attach_confirmed(home 19, local_id 254, epoch 1)` @10 197 | ✅ instrument reachable |
| **C2** post-loss, replacement free + duty-idle | a **second complete A→D chain** post-loss | `presence_home_lost(19, miss 3)` @523 190 → 1 DISCOVER → HomeB rx → OFFER on air → CLAIM → **`mobile_attach_confirmed(home 20, local_id 254, epoch 2)` @537 332** | ✅ a completed reattachment IS observable |
| **C3** `host_mobiles=false` on HomeB | **edge C first hop**: B unchanged, staging → 0 | A 13/13 · B **12 host rx (unchanged)** · **staged 0, `mobile_offer_tx` 0, on air 0** · `mobile_no_host` 13 · post-loss confirmed 0 | ✅ exactly as predicted |
| **C4** HomeB TX duty exhausted (`duty_cycle: 0.0003`) | **edge C last hop**: B unchanged, staging **and** `mobile_offer_tx` unchanged, **only the PHY frame disappears**, with `tx_deferred{BCN,duty_cycle_exceeded}` at the same ms | A 14/14 · B **13 host rx** · **staged 13, `mobile_offer_tx` 13, OFFER on air 0**, 13/13 `BCN/duty_cycle_exceeded` · post-loss confirmed 0 | ✅ **reproduces the `s07` walk/bike signature** |
| **C5** mobile TX duty exhausted (`duty_cycle: 0.0014`) | **edge A**: intents keep firing, DISCOVER PHY tx → 0 | 12 intents, **0 DISCOVER on air**, 12/12 `BCN/duty_cycle_exceeded`; also `mobile_reclaim_tx` ×3 → `mobile_claim_exhausted` | ✅ **reproduces the `s07` courier signature** |

### 5.1 ★★ The fixtures have TEETH — `verify.py`, and it is mutation-proved

⛔⛔ **CORRECTED: as first preserved, this fixture COULD NOT FAIL.** C2–C5 had **zero** `expect` entries and C1
had one that only checked that *some* `mobile_attach_confirmed` contained `"home"`; none of the parsers exited
nonzero. All five replays therefore reported success no matter what the behaviour did — **the 23rd
instrument-that-cannot-fail in this arc, inside the artefact built to BE the durable record.** Independent review
caught it.

`verify.py` is the repair: **36 checks, exact tuples, exit 1 on any failure.** It pins per control exactly the
edge that control exists to move — C1 `(19, 254, 1)` confirmed with CLAIM PHY-reception at HomeA and
`mobile_registered(key, 254, 1)`; C2 additionally `(20, 254, 2)` **after** the reset, CLAIM PHY-received at
HomeB, `mobile_registered(key, 254, 2)`; C3 DISCOVER PHY-received **at HomeB** with staging/emit/PHY all **0**;
C4 emit **present** and OFFER PHY tx **0**, each loss bound to `duty_cycle_exceeded` at HomeB; C5 intent
**present** and DISCOVER PHY tx **0**, likewise bound.

★ **MUTATION-PROVED, so it cannot join the list it was written to escape: all 36 of 36 expectations, inverted one
at a time via `--mutate <id>`, produced a `FAILED` line and exit 1.**

⚠ **Writing it immediately caught a real defect in itself:** C3's check first counted receives at *eligible*
statics — but C3 makes the replacement ineligible **on purpose**, so that counter read 0 and would have
"confirmed" a reception failure that did not happen. It now counts receives **at the named static** and asserts
the eligibility flag separately (`C3-3a`/`C3-3b`).

`selftest.py` does the same job for the correlator: **12 synthetic-stream checks** pinning both the refusal and
its negative half (an unambiguous shape must still be attributed, so "always refuse" cannot pass).
★ **Regression-proved:** a scratch copy of `graph.py` reverted to the retired *warn-and-consume* + *4000 ms
horizon* behaviour makes `selftest.py` report **6 failures and exit 1**; the repo file's md5 was verified
unchanged afterwards and the restored self-test exits 0.

★ **C3 vs C4 is the discriminator.** An eligibility failure removes `mobile_offer_tx`; a transmitter failure
keeps it and removes only the PHY frame. `s07`'s post-loss stream **keeps** `mobile_offer_tx` ⇒ it matches
C4/C5 and **not** C3.

---
## 6 — Does it reproduce outside `s07`?

**Yes.** C4 and C5 reproduce both failing edges in a **3-node** scenario with a single-variable mutation, so
the mechanism is a property of the firmware's `FrameTag::beacon` handling, not of `s07`.

⛔ **CORRECTED (ledger §1.19) — the previous sentence called this an `s07`-specific *trigger* and said "remove that
and `s07` reattaches". BOTH FRAMINGS ARE WITHDRAWN:** the one-shot regime is **corpus-wide (36 of 36)**, and
*removing it* is explicitly **not authorised** — `s07`'s saturation is ruled **valid stress behaviour** and must not
be retuned to restore delivery. ⇒ What `s07` uniquely provides is **enough load and duration to EXHAUST the allowance
and thus EXPOSE the core defect**: its DM/RREQ/beacon load ends 16 of 36 nodes at ≥99.6 % of budget between
t≈2.48 M and t≈3.12 M — precisely the failure window. **C2 shows the instrument can observe a complete reattachment
when the budget is not exhausted; it is a positive control, NOT a proposed fix.**

`s07`'s known pre-existing authoring gaps (fixed `node_id`s 50/51/52, no `mobile_autoregister`, no
`host_mobiles`, `send_e2e <name>`) are **NOT** implicated in this defect: none of them touches the presence
plane, and `host_mobiles`/`mobile_autoregister` both default to the value the mechanism needs.

---
## 7 — Observability gap (recorded, NOT closed — no telemetry was added)

⛔ **This is the requirement to record; the brief forbids adding the emit in this slice.**

A J-family frame refused by the modem **after** an accepted `_hal.tx()` is unattributable from firmware
telemetry alone:

* `radio_busy` (`node.cpp:2185`) carries only `reason` and `busy_until_ms` — **no `tag`, no length, no frame
  identity** — although `BusyInfo` (`hal.h:40`) already carries `tag` and `sf`;
* `on_radio_busy` returns at `:2203` for `slot < 0`, so **no** `mobile_offer_dropped` /
  `mobile_admission_rejected` / `tx_deferred_lost` is raised — all seven such counters read **0** in `s07`;
* the loss was therefore only locatable via the **simulator's** `tx_deferred{label, reason}` record, which
  does not exist on metal.
* ⇒ **within the observed path**, `mobile_offer_tx` is the last word: nothing downstream of it records that the
  frame died. ⛔⛔ **THIS DOES NOT ESTABLISH THAT THE FAILURE IS INVISIBLE ON METAL, and an earlier revision of
  this line claimed exactly that — corrected in place.** The defect demonstrated here is a **core handling
  defect reached through the simulator's ASYNCHRONOUS busy callback**; `DeviceHal::tx` answers `busy`
  **synchronously**, which would surface as `tx_hal_rejected` — **a different path, on which this slice has no
  measurement.** Equivalent metal behaviour is **UNVERIFIED** (see §8).

**Also recorded (not acted on):** `tx_with_retry`'s duty **pre-check** is gated on `slot >= 0`, so a J frame
is never pre-checked even though the firmware holds the same 36 000 ms budget the modem enforces — the
firmware asks the radio for airtime it can already know it does not have.

✅ **UPDATED IN PLACE 2026-08-12 — §B186a LANDED THE ATTRIBUTION HALF OF RECOMMENDATION 1, IN A DIFFERENT SHAPE
THAN THIS SECTION PROPOSED, AND THE DIFFERENCE MATTERS.** `radio_busy` was **not** given `tag`/`len`: a separate
`mobile_tx_refused{op, reason, reason_name, sf, busy_until_ms}` now names the **mobile OPERATION** — DISCOVER /
OFFER / initial CLAIM / re-CLAIM, distinguished by a new `LbtKind::mobile_reclaim` and carried in
`TxParams::tag`'s high byte — because a raw tag would still have read *"a beacon"* for all four, and ⛔ no length
was plumbed (`BusyInfo` has none). **On `s07` it accounts for all 71 losses, 23 DISCOVER + 48 OFFER, matching
this section's census exactly — including the 2 frames `lostj.py` correctly REFUSED to bind.** ⚠ It does **not**
make the loss visible on metal: `MR_EMIT` is device-stripped, and the audit that came with it measured that
`Node::on_radio_busy` **has no caller on hardware at all** ([[B189]]) — so §8's first bullet stands, now with
evidence. Item 2 (retry/pre-check) remains **NOT implemented** ([[B186b]]); item 3 was **owner-ruled out**
(§2.4). Evidence: `docs/superpowers/evidence/b186a/README.md`. ⓘ The original recommendations follow unchanged.

**Recommendations — recommendations only, no approval claimed, sequencing is the owner's call:**
1. Give `radio_busy` the `tag`/`len` it is already handed, so a metal log can attribute the drop.
2. Decide whether a beacon-tagged J frame should be duty-pre-checked and/or recovered in `on_radio_busy`
   (⚠ any such change moves corpus streams and must be its own slice with its own attribution).
3. Separately decide whether `s07` should keep a duty window equal to its own duration — that is a scenario
   question, and re-tuning it would re-anchor `s07`.

---
## 8 — What could not be established

* Whether a **metal** modem refuses a J frame in the same circumstances: `DeviceHal::tx` answers `busy`
  synchronously (so it would surface as `tx_hal_rejected`, a *different* path), whereas the simulator's
  refusal is asynchronous via `onRadioBusy`. **The two paths are not proven equivalent** and this slice did
  not test hardware.
* Whether any post-loss OFFER would have been *selected* had it flown — no OFFER reached any mobile
  post-loss, so the selection rule is untested in that interval.
* The pre-loss `presence_probe_tx` / `mobile_discover_tx` same-millisecond self-block (8 DISCOVERs,
  1 OFFER) is **observed but not diagnosed**; it is a separate contention question.

---
## 9 — Replay instructions

```bash
cd /home/staszek/MeshRoute
md5sum ~/lora-universal-simulator/build/orchestrator/lus          # expect 316b9cb1...

# (a) s07, the whole-run + post-loss graph
~/lora-universal-simulator/build/orchestrator/lus \
    simulation/s07_seattle_mobile_meshroute.json /tmp/s07.ndjson   # b3b7ce31 / 107989
md5sum /tmp/s07.ndjson
cd docs/superpowers/evidence/b183
python3 txgraph.py ../../../../simulation/s07_seattle_mobile_meshroute.json /tmp/s07.ndjson
python3 lostj.py   ../../../../simulation/s07_seattle_mobile_meshroute.json /tmp/s07.ndjson

# (b) the minimal fixture + its five controls (regenerate: python3 gen.py <outdir>)
for f in b183_c1_healthy b183_c2_postloss b183_c3_ineligible b183_c4_hostduty b183_c5_mobileduty; do
  ~/lora-universal-simulator/build/orchestrator/lus $f.json /tmp/b183/$f.ndjson
  python3 txgraph.py $f.json /tmp/b183/$f.ndjson
done

# (c) ★ THE GATE — must print "36 checks, 0 failed" and exit 0
python3 verify.py /tmp/b183 ; echo "verify exit=$?"

# (d) prove the instruments can fail (both must exit 0 = the proofs themselves pass)
python3 selftest.py ; echo "selftest exit=$?"
for id in $(grep -oE "C\.(eq|ge)\('[^']+'" verify.py | sed "s/.*('//;s/'//"); do
  python3 verify.py /tmp/b183 --mutate "$id" >/dev/null 2>&1 || echo "mutation $id detected"
done            # expect 36 lines
```

**Exit-code contract:** `verify.py` 0 = all 36 checks pass · `selftest.py` 0 = the refusal logic behaves ·
`txgraph.py` 0 = nothing refused (⇒ 0 on `s07` and on all five fixtures) · `lostj.py` **1 on `s07`**, which is
CORRECT — the stream holds 2 genuinely unattributable frames; `--allow-unattributed` reports them without
failing.

**Files.** `gen.py` regenerates the five JSONs byte-identically (verified) · `graph.py` the shared stream reader,
J-frame decoder (layout from `frame_codec.cpp` `pack_j_*` / `parse_j`) and **the ONE correlator, which refuses
ambiguity** · `txgraph.py` the transaction graph · `lostj.py` the lost-J-frame attribution census ·
**`verify.py` the 36-check gate** · **`selftest.py` the proof that the correlator can fail.**
⛔ Do not add a second correlator: `txgraph.py`, `lostj.py` and `verify.py` all bind through
`graph.Stream.arrivals` / `graph.Stream.refusal_for` so a future relaxation cannot be made in one caller only.

⛔ **These five scenarios are a NON-CORPUS diagnostic fixture.** They must never be added to `simulation/`,
the 36-row runner, `BASELINE.md`'s anchor table, or any delivery-floor computation.
