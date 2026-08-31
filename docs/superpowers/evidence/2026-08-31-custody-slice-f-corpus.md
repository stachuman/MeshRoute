<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-F — corpus evidence and the re-anchor PROPOSAL · 2026-08-31

**Status: PROPOSAL. ⛔ The `^### 36/36 corpus` anchor table in `simulation/BASELINE.md` is NOT edited by this
document or by the agent that produced it.** It moves only on the owner's single ruling, as it did for
§CUSTODY-A, §CUSTODY-B and §CUSTODY-E. Everything below is measurement.

Slice F is **the first traffic-ADDING slice of the custody arc**. §CUSTODY-A moved a TYPE byte, §CUSTODY-B
removed pushes, §CUSTODY-E removed whole lines — all of them subtractive or substitutive. F makes relays
**originate new frames**, which route, occupy airtime, are received and are dropped. The corpus therefore cannot
be judged by "what disappeared"; it is judged by **"is every new frame one this relay was entitled to send, and
did nothing else appear?"**

---

## 1. The arms, and the control that makes them comparable

| | binary | provenance |
|---|---|---|
| BEFORE | `lus` md5 **`487d7314`** | built from a **pristine `git archive HEAD`** of `4d782a0` (Slice E), in a scratch tree |
| AFTER | `lus` md5 **`160346e5`** | the live worktree |

★ **THE BEFORE ARM REPRODUCES THE ANCHOR TABLE EXACTLY — 36/36 rows, 0 assertion failures**
(`tools/run_corpus.py --require-anchors`). That is the control that makes every delta below attributable: the
comparand is not asserted to be Slice E's state, it is *measured* to be it, including the s18 keystone
`32afbf11` / 269517 / 0 read from the table rather than assumed.

★★ **A SECOND AFTER ARM WAS RUN AND IS THE INERTNESS PROOF FOR THE WARNING FIX.** Fixing two
`-Wmaybe-uninitialized` warnings (see §6) changed `node_cascade.cpp` and produced a **different binary**
(`f01e9976` → `160346e5`) — and **all 36 streams are byte-identical between the two AFTER arms**
(`run_corpus.py --compare` reports exactly one difference: `lus_sha256`). A different binary yielding identical
streams is a stronger statement than an unchanged binary would have been.

---

## 2. The prediction, made BEFORE the AFTER arm — and where it was wrong

The prediction was recorded from the BEFORE arm alone: 89 transit terminal deaths corpus-wide, 30 of them
E2E-ACK carriers, ⇒ **~52–59 notices across 8 movers**.

**MEASURED: 25 notices across 4 movers.** The prediction over-counted by roughly a factor of two, and the reason
is a fact about the tree that the dispatch brief, the spec and I all missed:

> ⛔⛔ **`path_cascade_exhausted` IS EMITTED AT SIX SITES, NOT THREE.** Beside the three §CUSTODY-E selected
> cascade terminals in `node_cascade.cpp`, it is emitted at **three §10.2 DEFERRED sites in `node_mac_rx.cpp`** —
> the loop-duplicate NACK give-up (`:2384`), the long-busy requeue's queue-full arm (`:2421`) and the hop-budget
> NACK terminal (`:2458`). All three use plain `giveup_flight` / `terminal_carrier_outcome` and generate nothing.

⚠ And the correction to the correction, which cost a second round: **the NACK test must be by REASON, not by
presence.** `handle_nack`'s DUTY-BUDGET arm (`nack_reason_budget` = 1) *is* a selected terminal — §CUSTODY-E's
own banner names it as the fifth entry point — and only `busy_rx` (0), `hop_budget` (2) and `loop_dup` (3) reach
the §10.2 sites. Treating every NACK-coincident exhaustion as deferred rejected two perfectly legitimate notices.

⇒ **This is itself the §10.2 verification**, and it is a measurement rather than an argument: **43 deferred
NACK terminals fired corpus-wide and produced ZERO custody notices.**

The corrected BEFORE census, and what each condition prunes:

| population | count | fate |
|---|---:|---|
| `path_cascade_exhausted`, all sites | 152 | — |
| ⟶ §10.2 deferred receive-path terminals (NACK reasons 0/2/3) | 43 | ⛔ generate nothing (§10.2) |
| ⟶ own-origination terminals | 56 | ⛔ ineligible, §10.1(2) |
| ⟶ transit terminals carrying an E2E ACK | 15 | ⛔ ineligible, §10.1(11) |
| ⟶ **selected TRANSIT terminals (the candidate pool)** | **45** | the pool §10.1's remaining conditions filter |
| ⟶ **notices actually generated** | **25** | the remaining 20 pruned by plaintext / plane / XL / mobile / id / inner-parse |

ⓘ The derived terminal set is a **SUPERSET** of the production one, and that was measured rather than assumed:
a scratch-tree build that emitted the whole eligibility vector at every production call showed `s18_meshroute`
evaluating exactly **three** carriers (all own-origination, `is_transit = 0`), while the stream-only derivation
calls two of its exhaustions "transit" — `{dst, ctr}` is a flight identity only *within one origin*, and
telemetry cannot always disambiguate. A superset can only make more terminals available to match, never fewer;
what it can never do is invent an origin.

---

## 3. Per-stream measurement

`tools/compare_corpus_slice_f.py <before> <after>` — **PASS**, **15/15** selftest controls RED. ★ Every claim
the PASS line makes now has a control that can FIRE, and the claim-to-control audit is written into the
selftest itself: the bind-then-classify rule (S1-S6, including the three QG-required arms — **a STALE eligible terminal**, where
an older terminal matches and the current one does not; **a WRONG CURRENT terminal**, right node and instant but
the wrong carrier; and ★ **older ELIGIBLE → current INELIGIBLE → notice**, run for ALL THREE ineligible
classifications rather than one, because a tool could plausibly preserve `deferred_nack` and still drop
`own_origin`), the custody-line divergence (S7), the zero-terminal-no-movement rule (S8), the refusal-cause rule
(S9), §12's recursion gate (S10) and the two separation counts the derivation rests on (S11/S12). The per-stream
checks were extracted into a pure `check_stream()` precisely so C3/C4/C5 could be driven RED against doctored
views instead of being asserted.

ⓘ The tool's own C5 wording was **narrowed, not defended**: it claimed a refusal is checked "unless its stream
also shows the congestion that caused it", while the checker validates the **reason vocabulary** and nothing
else. No congestion is proven anywhere in this tool and none was built to justify the sentence.

| stream | BEFORE | AFTER | sel-transit | own | §10.2 | ack✗ | notices | refused | unsup | deliv | dup |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| `s06_seattle_lifecycle` | `e8595775` | **`303bada4`** ★ | 18 | 6 | 13 | 8 | **9** | 0 | 4 | 110→108 | 32→34 |
| `s07_seattle_mobile_meshroute` | `e50cbb56` | **`b2e97ff1`** ★ | 12 | 19 | 10 | 6 | **5** | 0 | 1 | 86→86 | 10→10 |
| `s27_cross_layer_mobiles_meshroute` | `a33faca4` | **`0abe1650`** ★ | 2 | 0 | 0 | 0 | **2** | 0 | 2 | 15→15 | 0→0 |
| `twin_9node_dm` | `50080992` | **`c93fe4eb`** ★ | 12 | 20 | 3 | 1 | **9** | 0 | 4 | 16→13 | 2→1 |
| `s18_meshroute` (keystone) | `32afbf11` | **`32afbf11` UNCHANGED** | 2 | 3 | 17 | 0 | 0 | 0 | 0 | 105→105 | 12→12 |
| `s15_three_layer` / `_metal` | — | UNCHANGED | 1 / 1 | 0 | 0 | 0 | 0 | 0 | 0 | 54→54 | 1→1 |
| `s17_metro` | — | UNCHANGED | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 29→29 | 3→3 |
| `s16_dense_gateway` · `sim_9node_base` | — | UNCHANGED | 0 / 1 | 4 / 4 | 0 | 0 | 0 | 0 | 0 | — | — |
| the other 26 streams | — | **UNCHANGED** | 0 | 0 | 0 | 0 | 0 | 0 | 0 | — | — |

★ **THE s18 KEYSTONE DOES NOT MOVE.** Its 22 exhaustions are 17 §10.2 deferred NACK terminals plus 3
own-origination terminals plus 2 the superset over-counts; the production path evaluated three carriers and
found none eligible. A slice that adds traffic and leaves the keystone byte-identical is the strongest single
statement in this table.

## 4. The attribution, and the two proof directions kept apart

⛔ **A generated notice cannot prove its own eligibility** — both the notice and the eligibility decision come
from the same binary. The two directions are therefore proved by different instruments and neither claim is
made by the other:

- **`enqueued ⇒ eligible` — THE CORPUS.** Every one of the 25 notices is bound to **the CURRENT selected
  transit terminal it came out of**, derived from events this slice neither emits nor shapes (`data_rx`,
  `nack_rx`, `e2e_ack_tx`, `path_cascade_exhausted`). **25/25 attributed, 0 unattributed.** The binding is:
    · **by EVENT ORDER, not by time** — a terminal, its give-up and the notice it produces come out of one
      synchronous call chain and share a `time_ms`, and several nodes emit inside that millisecond, so the
      stream's line order is the causal order;
    · **BIND FIRST, CLASSIFY AFTER** — the notice is bound to the LATEST terminal OCCURRENCE at that node below
      its ordinal, **of ANY classification** (selected-transit / deferred / own-origin / ack-excluded); THEN that
      exact occurrence must prove itself eligible. An older candidate is never consulted;
    · **synchronous** — the bound terminal must carry the notice's own `time_ms` (§11 step 7 runs inside
      `cascade_terminal_giveup`);
    · **the failed origin must be in THAT terminal's origin set**, which is derived from real `data_rx` events
      for that terminal's own `{dst, ctr}` — so the carrier's identity is pinned, not just the sender's;
    · **consumed once** (§10.3, one generation per terminal carrier).

  ⛔⛔ **A SECOND QG CORRECTION, 2026-08-31 round 3 — FILTER-THEN-BIND.** The round-2 binding searched the right
  way but was handed a PRE-FILTERED list: the derivation dropped the deferred / own-origin / ack-excluded
  occurrences before the binding ran. So an **INELIGIBLE current terminal was invisible**, and the sequence
  *older eligible terminal → current ineligible terminal → erroneous notice* still passed, because the older
  eligible one was still the last one the binder could see. ⇒ every occurrence is now preserved with its
  classification, the binding is over the **unfiltered** set, and eligibility is asked of the bound occurrence.
  ★ **THE FIGURES REPRODUCE A THIRD TIME, unchanged**: 25 notices, 0 unattributed, 4 movers, 758 → 753,
  62 → 63, keystone unchanged. Two successive tightenings moved nothing, which is the strongest thing that can
  be said short of a different instrument.

  ⛔⛔ **THIS PARAGRAPH IS A QG CORRECTION OF 2026-08-31 AND THE WEAKER CLAIM IS NAMED SO IT IS NOT RE-MADE.** It
  read *"at the same node, at or before the notice, naming an origin that node genuinely relayed a flight for"*.
  The first accountant **discarded the terminal's own `{dst, ctr}`** and searched a per-node pool for ANY
  earlier terminal carrying that origin — so a relay that failed twice for the same sender could have its second
  notice "attributed" to its first terminal. The instrument did not prove what this file claimed. ★ **The
  corrected binding is strictly stronger and REPRODUCES EVERY FIGURE:** 25 notices, 0 unattributed, 4 movers,
  deliveries 758 → 753, duplicates 62 → 63, keystone unchanged. Nothing moved under the tightening, which is
  what says the original attribution was *loosely proven* rather than *wrong*.
- **`eligible ⇒ enqueued` — NATIVE + THE MUTATION BATTERY.** §10.1's twelve terms each tested both ways off a
  real production transit carrier, plus one mutation per independent term. Not attempted here.

★ **THE PREFIX IS BEFORE-ANCHORED.** All **4/4** moved streams are **byte-identical to the BEFORE arm for
142 734 lines** before their first custody line, and that first differing line is in every case a custody line.
The divergence starts exactly where the slice acts; nothing before it moved.

★★ **THE §12 RECURSION GATE IS POSITIVELY EXERCISED IN THE CORPUS, not merely asserted in a test: two relayed
custody-notice flights reached a selected transit terminal at a downstream relay and generated NOTHING.** The
invariants hold corpus-wide — **zero 0x81-about-0x81, zero about an E2E ACK.**

ⓘ **11 `unsupported_internal` events** are the ratified intermediate state: an aired notice is received by its
addressee, ACKed like any DATA, and dropped at Slice B's fail-closed tail guard. Slice G replaces that drop.

## 5. ⚠ OWNER FLAG — deliveries moved

**DELIVERIES 758 → 753 (−5). DUPLICATES 62 → 63 (+1).** Per the standing rule this is reported, not judged.

It is a **reshuffle, not a systematic regression** — measured by identity, not inferred:

| stream | lost | gained | net |
|---|---:|---:|---:|
| `twin_9node_dm` | 5 | 2 | **−3** (16 → 13) |
| `s06_seattle_lifecycle` | 4 | 2 | **−2** (110 → 108) |
| `s07`, `s27` | 0 | 0 | 0 |

Different `{origin, dst, ctr}` triples succeed in each arm; this is the B177/s15 timing-reshuffle class, driven
by the airtime the 25 notices occupy. `twin_9node_dm` is a 9-node mesh carrying 16 deliveries, so 9 notices is a
large relative load there. **The owner's ruling is owed on whether −5 net deliveries is an acceptable price for
the custody diagnostic.** No tuning knob was touched and none is proposed here.

## 6. The mutation battery, and the six survivors that were FIXED rather than accepted

Three new targets — `sliceFcodec` (`frame_codec.cpp`), `sliceFtypes` (`frame_codec.h`), `sliceFcascade`
(`node_cascade.cpp`). **Final: 43 entries, 43 RED, 0 survivors, 0 unusable; every match count exactly 1 and
every worker's source restored to a matching md5.**

The first run was **39 RED / 6 survivors**, and each survivor was a real gap in the tests, not a bad mutant:

| survivor | why it survived | what was added |
|---|---|---|
| F04 packer's exactly-one-stage rule dropped | the `forwarded` check caught the all-zero byte, and nothing tested BOTH stage bits or NEITHER | two packer refusals in `§CUSTODY-F/1g` |
| F22 the pack refusal is silent | the C2 arm is **unreachable from the production cascade** (E's seam guarantees no stage sentinel) | a native-only `test_custody_notice_enqueue` seam + `§CUSTODY-F/3bc`, which drives an eligible-but-unpackable snapshot and pins the loud refusal, with a control proving the seam is not inert |
| F33 §10.1(6b) cross-layer exclusion | flipping the XL flag on an ordinary inner makes it fail to PARSE, so §10.1(7) refused it first — the arm passed for the wrong reason | `§CUSTODY-F/3bb` packs a **genuine** cross-layer inner through `pack_unicast_inner`, so it parses, its origin agrees, and only the exclusion can refuse it |
| F44 `repair_attempted` hard-coded true | no case ever read §9.3 bit 3 | `§CUSTODY-F/3ba` pins the bit against the typed context both ways, through the wire round trip |
| **F29 §10.1(4) plaintext** | ⛔ **structurally subsumed — DELIBERATE ABSENCE** | under `DATA_FLAG_CRYPTED` the shared codec leaves `u.origin = 0` (`frame_codec.cpp:1028-1033`, "a relay must NOT learn who originated a CRYPTED DM") and `custody_node_id_valid(0)` is false, so §10.1(8)+(9) refuse the carrier whether or not (4) exists |
| **F36 §10.1(7) inner parse** | ⛔ **structurally subsumed — DELIBERATE ABSENCE** | `origin_agrees` conjoins `inner_parses`, and an unparsed inner yields `inner_origin = 0` — refused by (9) |

Both absences are recorded **in the battery's own target table with the `file:line` that causes them**, and both
terms are KEPT in the source: §10.1 lists them and a codec change could unmask either. ⓘ The sibling term
§10.1(6b) had the identical masking and was **rescued rather than waived** — which is what makes the two
remaining absences a measurement rather than a convenience.

⛔ Two further mutations are absent because they are **compiler-time controls, not runtime ones** (the arc's
standing idiom): §10.1(1)'s "a live `PendingTx` is not required" is not expressible without changing
`custody_notice_snapshot`'s signature, and "a `TxItem` is built field by field" has nothing to mutate —
`node_cascade.cpp` contains no `TxItem` on the notice path at all, the whole origination being one
`enqueue_data` call. F18 (deleting that call) is the structural pin, and it is RED.

## 7. What else was measured

- **Native: 2437 / 101569 / 0 → 2463 / 102123 / 0** (unchanged across the QG round-2 corrections — the
  `frame_codec.h` withdrawal is a comment and `compare_corpus_slice_f.py` is not compiled) (+26 cases, +554 assertions), RUN from the binary — the pio
  wrapper prints its usual false "0 test cases". The case delta is derived, not asserted: `test_custody_relay_f.cpp`
  adds **28** cases; `test_custody_terminal_e.cpp` goes 19 → **17**, retiring §CUSTODY-E/6 and /6b in place (the
  two cases that asserted `0x81` does not exist and that `node_cascade.cpp` calls no enqueue helper — Slice F is
  the slice they named as their terminator). 28 − 2 = **+26**. ✓
- **The final `test_custody_notice_enqueue` seam is byte-inert in the simulator, PROVEN rather than argued:**
  `cmake --build` recompiled **30** translation units and `lus` reproduced **`160346e5` EXACTLY**. A corpus row
  cannot move behind a byte-identical binary, so the 36-row run above stands. `s18_meshroute` was re-run anyway
  as a smoke check and reproduces **`32afbf11` / 269517 / 0**.
- **Warning census: PASS at its pins, NOTHING RE-PINNED** (173/178/177/177/182/182 across the six OLED envs,
  `-Wswitch` 0). ⚠ It went RED first: the eligibility gate's `ui->…` reads, guarded only by a separate `bool`,
  produced **two `-Wmaybe-uninitialized` warnings on every env**. Fixed at the source (one `if (ui)` block
  extracting the parsed values; a ternary form was measured and still warned), **not re-pinned**.
- **Boards, `pair --jobs=2`, both arms measured:**

  | env | RAM | flash | objects |
  |---|---|---|---|
  | `gateway` | 195 660 → **195 660 (+0)** | 508 716 → **509 452 (+736 B)** | 283 → 283 |
  | `heltec_mobile` | 205 620 → **205 620 (+0)** | 1 355 916 → **1 356 904 (+988 B)** | 327 → 327 |

  ⓘ **RE-MEASURED AFTER THE QG ROUND-2 `frame_codec.h` WITHDRAWAL, because that edit is NOT line-count-neutral
  (3 comment lines → 5) and [[B254]] requires the size fields rather than an argument.** Every field is
  BYTE-IDENTICAL to the row above — `gateway` 195 660 / 509 452 / 283, `heltec_mobile` 205 620 / 1 356 904 / 327
  — and the warning census re-passes at its pins. ⛔ `lus` also rebuilt (34 TUs) and reproduced **`160346e5`
  EXACTLY**, so no corpus row can have moved; `s18_meshroute` re-run anyway reproduces `32afbf11` / 269517 / 0.

  ★ **ZERO RAM on both.** `CustodyNoticeSnapshot` is a stack local at one function, never a `Node` member; the
  identical RAM on two different architectures is the measurement that says so. ⚠ `heltec_mobile`'s
  `payload_sha256` is path-dependent ([[B262]]) and the BEFORE arm was built at a different path, so payload
  hashes are deliberately not compared — RAM, flash and object counts are.

---

## 8. THE RE-ANCHOR PROPOSAL

**Four rows move. Thirty-two are byte-identical, including the `s18` keystone.** Every moved row's Δ is new
custody traffic and the timing it displaces, attributed above.

⛔ **This table is a PROPOSAL for the owner's single ruling. No agent edits `simulation/BASELINE.md`'s
`^### 36/36 corpus` table without it.** The proposed new keystone is **`s18_meshroute` UNCHANGED at
`32afbf11` / 269517 / 0** — the first custody re-anchor in this arc that does not move the keystone.

```
s06_seattle_lifecycle                        303bada4 lus: 69035 events emitted, 0 assertion failure(s)
s07_seattle_mobile_meshroute                 b2e97ff1 lus: 111676 events emitted, 0 assertion failure(s)
s09_two_layer_gateway                        71120178 lus: 2266 events emitted, 0 assertion failure(s)
s09_two_layer_gateway_metal                  0182f858 lus: 2343 events emitted, 0 assertion failure(s)
s10_two_layer_separation                     c44c0b39 lus: 2266 events emitted, 0 assertion failure(s)
s15_three_layer                              f95e2d60 lus: 51794 events emitted, 0 assertion failure(s)
s15_three_layer_metal                        3611a93b lus: 52237 events emitted, 0 assertion failure(s)
s16_dense_gateway                            5b30637c lus: 23898 events emitted, 0 assertion failure(s)
s17_metro                                    aa960050 lus: 1181178 events emitted, 0 assertion failure(s)
s18_meshroute                                32afbf11 lus: 269517 events emitted, 0 assertion failure(s)
s19_singlelayer_multihop_chain               c669b1ef lus: 1065 events emitted, 0 assertion failure(s)
s20_random_mesh                              db240065 lus: 40566 events emitted, 0 assertion failure(s)
s21_leaf_config_divergence                   d7db6a04 lus: 390 events emitted, 0 assertion failure(s)
s21_mobile_dm_milestone_meshroute            fc466e77 lus: 678 events emitted, 0 assertion failure(s)
s22_leaf_config_join                         baadfbed lus: 215 events emitted, 0 assertion failure(s)
s22_mobile_team_meshroute                    a47a9f76 lus: 1824 events emitted, 0 assertion failure(s)
s23_leaf_config_epoch_write                  0cd16bd5 lus: 219 events emitted, 0 assertion failure(s)
s23_mobile_team_multihop_meshroute           568c684f lus: 924 events emitted, 0 assertion failure(s)
s24_static_and_team_multihop_meshroute       d06536f4 lus: 1576 events emitted, 0 assertion failure(s)
s25_two_team_separation_meshroute            f87360c7 lus: 786 events emitted, 0 assertion failure(s)
s26_team_reroute_meshroute                   73a68a35 lus: 1037 events emitted, 0 assertion failure(s)
s27_cross_layer_mobiles_meshroute            0abe1650 lus: 9431 events emitted, 0 assertion failure(s)
s28_mixed_team_channels_meshroute            525756e2 lus: 3861 events emitted, 0 assertion failure(s)
s29_mixed_leaf_team_meshroute                bb534a88 lus: 2025 events emitted, 0 assertion failure(s)
s30_team_dad_mediation_meshroute             4a1de37d lus: 1034 events emitted, 0 assertion failure(s)
s31_dual_carrier_gateway                     4eafb125 lus: 2300 events emitted, 0 assertion failure(s)
s32_dual_cr_gateway                          9574f5dd lus: 2266 events emitted, 0 assertion failure(s)
s33_mixed_cr_channel_overhear                814ef421 lus: 2845 events emitted, 0 assertion failure(s)
s34_team_switch_clears_plane                 0c724c05 lus: 919 events emitted, 0 assertion failure(s)
s35a_cochannel_isolation_meshroute           bda1713b lus: 2356 events emitted, 0 assertion failure(s)
s35b_cochannel_isolation_control_meshroute   7dbc19ae lus: 1063 events emitted, 0 assertion failure(s)
s36_reprovision_purges_carriers              76d02e58 lus: 472 events emitted, 0 assertion failure(s)
s37_team_homed_origin_meshroute              db535d42 lus: 748 events emitted, 0 assertion failure(s)
s38_team_origin_learn_meshroute              52be507e lus: 522 events emitted, 0 assertion failure(s)
sim_9node_base                               e7a1c3d6 lus: 4945 events emitted, 0 assertion failure(s)
twin_9node_dm                                c93fe4eb lus: 14548 events emitted, 0 assertion failure(s)
```

The four movers, with their deltas:

| stream | BEFORE | AFTER | Δ events | attribution |
|---|---|---|---:|---|
| `s06_seattle_lifecycle` | `e8595775` / 65 541 | `303bada4` / 69 035 | +3 494 | 9 notices + their MAC exchanges + the reshuffle |
| `s07_seattle_mobile_meshroute` | `e50cbb56` / 109 576 | `b2e97ff1` / 111 676 | +2 100 | 5 notices + their MAC exchanges + the reshuffle |
| `s27_cross_layer_mobiles_meshroute` | `a33faca4` / 9 359 | `0abe1650` / 9 431 | +72 | 2 notices |
| `twin_9node_dm` | `50080992` / 14 597 | `c93fe4eb` / 14 548 | **−49** | 9 notices, **net negative** — the notices displaced more traffic than they added on a 9-node mesh |
