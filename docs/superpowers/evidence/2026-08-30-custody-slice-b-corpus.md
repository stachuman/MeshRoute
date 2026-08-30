<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-B — corpus measurement, attribution, and the PRE-RULING re-anchor proposal

**Slice:** §CUSTODY-B (common internal behaviour), 2026-08-30. **Tree:** `HEAD = 8e7ad55` (§CUSTODY-A) plus this
slice's uncommitted work. **Analyzer:** `lus` md5 **`afa65f0bfe774f0177a83fd45d97be86`**, built from this tree's
`lib/core`; the pre-slice arm was produced by `lus` **`7e69bd7196367cf9d0fdbab14d05369d`**.

⛔⛔ **THIS IS A PROPOSAL, NOT AN ANCHOR.** `simulation/BASELINE.md`'s `### 36/36 corpus` table is **NOT** edited by
this slice and moves only on the owner's single ruling (the 2026-08-29 ruling forbids an agent editing it without a
new one). This file exists so that ruling is made against evidence living somewhere durable.

---

## 1. The prediction, and the fact that it was WRONG

The brief required predicting per-stream deltas **before** writing code. That prediction was made, recorded, and
then **rejected by this slice's own instrument** — which is why the instrument exists, so it is written down rather
than quietly replaced.

| | |
|---|---|
| **What was predicted** | Only §6.2(4)'s DM-floor widening is corpus-visible ⇒ only the **11** streams carrying a NEWLY-exempt internal type (`0x88`/`0x89`/`0x8B`/`0x90..0x96`/`0xA2`) can move. §6.2(5)'s Push suppression is invisible, because Pushes are an app-ring concern rather than telemetry. |
| **What was measured** | **18** streams moved, including seven carrying nothing but `E2E_ACK` (`0x80`) — a type the floor change cannot touch. |
| **Why the prediction was wrong** | The simulator **serializes every `Push` as a `push` event**. `send_acked` and `send_failed` are therefore fully corpus-visible. |
| **How it was caught** | `tools/compare_corpus_slice_b.py` rule-1 fired on exactly the seven unpredicted streams and named each one. |

⇒ the corrected invariant set below is **strictly stronger** than the original prediction, not looser.

## 2. The invariants the gate now proves

| # | invariant | result |
|---|---|---|
| **I1** | `unsupported_internal` appears **nowhere** — the pre-slice corpus was histogrammed by TYPE byte across all 36 streams and carries **zero** unknown-internal / `0x94` / out-of-range values, so the fail-closed guard is **corpus-inert by measurement**. | **holds, 0 occurrences** |
| **I2** | A stream moves **if and only if** it carries own-originated protocol-internal DATA. Two-sided: an internal-carrying stream that did *not* move fails as hard as a clean stream that did. | **holds, 18/18 and 18/18** |
| **I3** | `push{send_acked}` · `push{send_failed}` · the `send_blocked` emit may only **decrease**. | **holds; every delta negative** |
| **I4** | ★ A stream whose internal types are all *already* floor-exempt (`{0x80, 0xA0, 0xA1}`) sees no admission-timing change, so **`emit:push` must be its ONLY changed event kind**. | **holds on all 7 such streams** |
| **I5** | Secondary (re-timing) deltas are permitted **only** where a newly-exempt internal type is present, and each is reported in full. | **2 streams, both reported below** |

**I4 is the sharp one**: it is what separates §6.2(5)'s effect from §6.2(4)'s instead of letting one hide inside the
other, and it could most easily have failed — a floor change that leaked outside `0x80..0xBF` would show up there
immediately.

**Instrument controls — 4/4 RED**, each tripping its **named** invariant on a **class-chosen** target (a control
that happens to land on a re-timed stream would pass for the wrong reason; the first cut of the selftest did
exactly that and only worked because `s06` sorts first):

```
an unpredicted event kind appears                    I4  on s06_seattle_lifecycle      RED
unsupported_internal appears (guard measured inert)  I1  on s09_two_layer_gateway      RED
a predicted-suppressed push re-appears (re-timed)    I3  on s15_three_layer            RED
an unrelated field moves in a BYTE-IDENTICAL stream  I2  on s09_two_layer_gateway      RED
```

⛔ `tools/compare_corpus_semantics.py` (Slice A's ordered comparator, 6/6 controls) is **untouched**. Slice A proves
"semantics may move nowhere"; Slice B proves "semantics move, here and only here". Two claims, two instruments.

## 3. Attribution — the three classes

**18 byte-identical** (carry no internal-typed DATA at all; nothing this slice changed can reach them):
`s09_two_layer_gateway` · `s10` · `s16` · `s17` · `s19` · `s20` · `s21_leaf_config_divergence` ·
`s22_leaf_config_join` · `s23_leaf_config_epoch_write` · `s23_mobile_team_multihop` · `s29` · `s30` · `s31` ·
`s32` · `s33` · `s35a` · `s35b` · `s36`.

**16 push-only** — `emit:push` is the *only* changed event kind; the whole delta is §6.2(5) removing generic
user-send outcomes from protocol-internal flights:

| stream | push-kind delta |
|---|---|
| `s06_seattle_lifecycle` | `send_acked −263 · send_failed −11` |
| `s07_seattle_mobile` | `send_acked −237 · send_failed −12` |
| `s09_two_layer_gateway_metal` | `send_acked −2` |
| **`s18_meshroute`** ★ keystone | `send_acked −369 · send_failed −9` |
| `s21_mobile_dm_milestone` | `send_acked −2` |
| `s22_mobile_team` | `send_acked −6` |
| `s24` · `s25` · `s26` · `s28` · `s34` | `send_acked −7 / −6 / −8 / −4 / −2` |
| `s27_cross_layer_mobiles` | `send_acked −49 · send_failed −2` |
| `s37` · `s38` | `send_acked −3 / −4` |
| `sim_9node_base` | `send_acked −13` |
| `twin_9node_dm` | `send_acked −33 · send_failed −4` |

**2 re-timed** — these additionally get §6.2(4)'s floor bypass, which changes *when* a hash answer is admitted and
therefore re-times everything downstream. ⚠ **Reported in full rather than waved through, including the losses:**

| stream | `send_blocked` | DM deliveries | channel receipts | airtime | frames |
|---|---|---|---|---|---|
| `s15_three_layer` | 18 → **0** | 53 → **54 (+1)** | 213 → **201 (−12)** | −13 513 ms (**−0.80 %**) | 4204 → 4165 |
| `s15_three_layer_metal` | 14 → **0** | 54 → 54 (**+0**) | 212 → **208 (−4)** | +24 248 ms (**+1.46 %**) | 4158 → 4192 |

⚠ **The channel-receipt losses are real and are not claimed as neutral.** They are a collision/timing reshuffle of
the same class `simulation/BASELINE.md` records for [[B177]] (a static↔static reshuffle), not a mechanism change:
no channel code was touched by this slice, both streams keep **0 assertion failures**, and `s15`'s DM delivery goes
*up* by one while its airtime goes *down*. The owner may reasonably want these two streams re-examined; they are
flagged here rather than buried in a total.

**Delivery/duplicate figures elsewhere are unchanged by construction**: the other 34 streams are either
byte-identical or differ solely in `push` events, which are an app-ring record and carry no frame.

---

## 4. THE RE-ANCHOR PROPOSAL (§10-style) — for the owner's single ruling

36/36 measured · **0 assertion failures on every stream** · analyzer `lus` `afa65f0b`.

| # | scenario | pre-slice (= the current anchor table) | **MEASURED post-Slice-B** | events | Δ |
|---|---|---|---|---:|---:|
| 1 | `s06_seattle_lifecycle` | `aa2380be` | **`7bc8d43e`** | 65561 | −274 |
| 2 | `s07_seattle_mobile_meshroute` | `57d608bc` | **`7564f10d`** | 109588 | −249 |
| 3 | `s09_two_layer_gateway` | `71120178` | `71120178` *(unchanged)* | 2266 | — |
| 4 | `s09_two_layer_gateway_metal` | `c0fc1168` | **`0182f858`** | 2343 | −2 |
| 5 | `s10_two_layer_separation` | `c44c0b39` | `c44c0b39` *(unchanged)* | 2266 | — |
| 6 | `s15_three_layer` | `0b546cf5` | **`39d70c51`** | 51795 | −646 |
| 7 | `s15_three_layer_metal` | `8bbff735` | **`b54b0a5c`** | 52238 | +419 |
| 8 | `s16_dense_gateway` | `5d9a7186` | `5d9a7186` *(unchanged)* | 23902 | — |
| 9 | `s17_metro` | `42e69427` | `42e69427` *(unchanged)* | 1181179 | — |
| 10 | `s18_meshroute` ★ **keystone** | `b7aeaeeb` | **`76a67335`** | **269527** | −378 |
| 11 | `s19_singlelayer_multihop_chain` | `c669b1ef` | `c669b1ef` *(unchanged)* | 1065 | — |
| 12 | `s20_random_mesh` | `db240065` | `db240065` *(unchanged)* | 40566 | — |
| 13 | `s21_leaf_config_divergence` | `d7db6a04` | `d7db6a04` *(unchanged)* | 390 | — |
| 14 | `s21_mobile_dm_milestone_meshroute` | `1c5db032` | **`fc466e77`** | 678 | −2 |
| 15 | `s22_leaf_config_join` | `baadfbed` | `baadfbed` *(unchanged)* | 215 | — |
| 16 | `s22_mobile_team_meshroute` | `e2f8f5a1` | **`a47a9f76`** | 1824 | −6 |
| 17 | `s23_leaf_config_epoch_write` | `0cd16bd5` | `0cd16bd5` *(unchanged)* | 219 | — |
| 18 | `s23_mobile_team_multihop_meshroute` | `568c684f` | `568c684f` *(unchanged)* | 924 | — |
| 19 | `s24_static_and_team_multihop_meshroute` | `74fec485` | **`d06536f4`** | 1576 | −7 |
| 20 | `s25_two_team_separation_meshroute` | `700d3437` | **`f87360c7`** | 786 | −6 |
| 21 | `s26_team_reroute_meshroute` | `86010f57` | **`73a68a35`** | 1037 | −8 |
| 22 | `s27_cross_layer_mobiles_meshroute` | `721de4b7` | **`a33faca4`** | 9359 | −51 |
| 23 | `s28_mixed_team_channels_meshroute` | `9a3b4cac` | **`525756e2`** | 3861 | −4 |
| 24 | `s29_mixed_leaf_team_meshroute` | `bb534a88` | `bb534a88` *(unchanged)* | 2025 | — |
| 25 | `s30_team_dad_mediation_meshroute` | `4a1de37d` | `4a1de37d` *(unchanged)* | 1034 | — |
| 26 | `s31_dual_carrier_gateway` | `4eafb125` | `4eafb125` *(unchanged)* | 2300 | — |
| 27 | `s32_dual_cr_gateway` | `9574f5dd` | `9574f5dd` *(unchanged)* | 2266 | — |
| 28 | `s33_mixed_cr_channel_overhear` | `814ef421` | `814ef421` *(unchanged)* | 2845 | — |
| 29 | `s34_team_switch_clears_plane` | `61ecb33e` | **`0c724c05`** | 919 | −2 |
| 30 | `s35a_cochannel_isolation_meshroute` | `bda1713b` | `bda1713b` *(unchanged)* | 2356 | — |
| 31 | `s35b_cochannel_isolation_control_meshroute` | `7dbc19ae` | `7dbc19ae` *(unchanged)* | 1063 | — |
| 32 | `s36_reprovision_purges_carriers` | `76d02e58` | `76d02e58` *(unchanged)* | 472 | — |
| 33 | `s37_team_homed_origin_meshroute` | `18f4b4aa` | **`db535d42`** | 748 | −3 |
| 34 | `s38_team_origin_learn_meshroute` | `626cf1ff` | **`52be507e`** | 522 | −4 |
| 35 | `sim_9node_base` | `f729db96` | **`0def4316`** | 4946 | −13 |
| 36 | `twin_9node_dm` | `2e038758` | **`04647a50`** | 14608 | −37 |

ⓘ The pre-slice column reproduced `simulation/BASELINE.md`'s anchor table **EXACTLY on all 36 rows** before any
code was written — unlike Slice A, which had to work against 15 pre-existing movers. This slice's deltas are
therefore attributable to it alone with nothing subtracted.

⛔ **On the ruling:** if the owner accepts, `simulation/BASELINE.md`'s `### 36/36 corpus` table takes the
**MEASURED post-Slice-B** column and the s18 keystone becomes **`76a67335` / 269527 / 0**. Until then the keystone
of record remains `b7aeaeeb` / 269905, and this section is the standing evidence for the change.

## 4b. [[B268]] ADDENDUM — the grant's own outcome pushes are CORPUS-INERT (measured after the fix)

The owner's [[B268]] ruling (option (b), 2026-08-30) landed **after** the measurement in §4: the team-key grant
gained two protocol-specific `PushKind`s (`team_key_grant_aired`, `team_key_grant_failed`) because §6.2(5) had
taken the generic pair away from it.

**Prediction, recorded before the re-run:** the corpus is **byte-identical on all 36 streams** to the §4 run.
Two independent reasons: (a) `DATA_TYPE_TEAM_KEY_GRANT` has **zero corpus reach** ([[B267]] — s18-inert, no
identities ⇒ no seals ⇒ the type is never emitted by any scenario), so neither new push can be minted; and
(b) the enumerators are **APPENDED** (17, 18), so no existing `PushKind` value moves and every serialized `push`
event keeps its name.

**Measured: 36/36 byte-identical to the §4 run** (`lus` `d1f6b2a3`, rebuilt — 34 build actions, so the binary
really did change). The §4 re-anchor proposal therefore stands **unaltered**, keystone included; the comparator
re-run against the pre-slice arm reproduces `18 byte-identical · 16 push-only · 2 re-timed · PASS` with **4/4
controls RED**. ⇒ the [[B268]] fix is corpus-inert, by measurement rather than by argument.

## 4c. THE CONTROLLED TWO-FACTOR ISOLATION (QG blocker 2) — the s15 causation, MEASURED

§3 reported the two re-timed streams' channel-receipt losses and attributed them to the §6.2(4) floor bypass
**by argument** ("no channel code changed"). QG correctly refused that: timing changes elsewhere are exactly how
collision reshuffles happen. So the two factors were separated and each measured alone, on the FINAL
(post-[[B268]]-blocker-1) tree.

**The arms, each a genuine one-factor edit:**

| arm | edit | holds out |
|---|---|---|
| **A — lifecycle suppression ONLY** | the two DM-floor halves reverted to their historical `{E2E_ACK, REMOTE_CMD, REMOTE_RESP}` lists (`node_mac.cpp`, 2 edits) | §6.2(4)'s floor widening |
| **B — internal floor bypass ONLY** | `generic_send_lifecycle` restored to `true` for every internal type in the trait table (`frame_codec.h`, 3 rows) — the floor reads `.internal` and is therefore untouched | §6.2(5)'s suppression |
| **FULL** | the shipped tree | — |

Separate `lus` builds per arm (`d034dd0b` · `580294df` · `7b1f2ec0`), all 36 streams each.

**Result — a clean factorial separation:**

| stream | BASE | arm A | arm B | FULL | `channel_recv` B/A/B/F | `msg_recv` B/A/B/F |
|---|---|---|---|---|---|---|
| **`s15_three_layer`** | `0b546cf5` | `f21f8adb` | `686bd1fa` | `39d70c51` | **213 / 213 / 201 / 201** | **53 / 53 / 54 / 54** |
| **`s15_three_layer_metal`** | `8bbff735` | `58193279` | `7d9847ff` | `b54b0a5c` | **212 / 212 / 208 / 208** | 54 / 54 / 54 / 54 |
| the other 16 movers | — | = FULL | **= BASE** | — | unchanged in every arm | unchanged in every arm |

⇒ **THE CAUSATION IS ISOLATED, and it is the floor bypass:**
- **Arm A leaves both streams' delivery outcomes BYTE-IDENTICAL to BASE** — 213 and 212 channel receipts, 53 and
  54 DM deliveries. Lifecycle suppression contributes **exactly zero** to the losses.
- **Arm B alone reproduces the FULL delivery figures exactly** — 201 / 208 channel receipts and the +1 DM
  delivery in `s15`. The floor bypass is the whole cause.
- ⓘ The two streams' *hashes* move in BOTH arms, which is expected (both factors touch them at all); the arms are
  separated by the DELIVERY OUTCOMES, which is the claim under test.
- ⓘ And the other 16 movers separate the opposite way — arm B is byte-identical to BASE for every one of them,
  so their `push`-only deltas are 100% lifecycle suppression with zero floor contribution.

**⇒ QG's pre-stated acceptance rationale applies as written:** protocol answers should not be delayed by a
user-DM pacing rule, and the collision reshuffle is preferable to preserving an incorrect throttle. Recorded here
either way, as instructed.

## 4d. [[B268]] BLOCKER-1 ADDENDUM — the shared terminal helper is corpus-inert

**Prediction, recorded before the re-run:** byte-identical to §4b on all 36 streams. Every one of the eleven
carrier-death sites passes its OWN unchanged `generic_owed` predicate into the shared helper, which applies only
the trait decision each site already applied — so no generic outcome moves; and the only NEW emission is
`team_key_grant_failed`, whose type has zero corpus reach ([[B267]]).

**Measured: 36/36 byte-identical to §4b** behind a genuinely rebuilt `lus` (`d1f6b2a3` → `7b1f2ec0`, 34 build
actions), and **zero `team_key_grant*` events in any stream** — the grant's zero reach, confirmed rather than
assumed. ⇒ the §4 proposal below stands unaltered on the final tree, keystone included.

## 5. Reproducing this

```bash
# ⓘ SUPERSEDED 2026-08-30 (§GATE-SPEED): the ad-hoc loop below is retired — the canonical runner is
#   tools/run_corpus.py (--jobs=1 is the sequential arbiter; --require-anchors turns BASELINE disagreement
#   into a refusal; every run self-validates: frozen-input snapshot, validated promotion, validated manifest):
#     python3 tools/run_corpus.py --out before --jobs 8     # from a pristine HEAD tree
#     python3 tools/run_corpus.py --out after  --jobs 8     # from the slice tree
#   The historical loop, kept as the record of what produced THIS file's figures:
# 1. the PRE-slice corpus, from a pristine HEAD (lus 7e69bd71)
git archive HEAD | tar -x -C /tmp/head && ...                 # or: git worktree add --detach /tmp/wt HEAD
for f in simulation/*.json; do b=$(basename $f .json); [ "$b" = topo_9node ] && continue
  ../lora-universal-simulator/build/orchestrator/lus -e meshroute "$f" before/$b.ndjson; done
# 2. the POST-slice corpus, same loop into after/ (lus afa65f0b)
# 3. the predicted-delta gate + its four negative controls
python3 tools/compare_corpus_slice_b.py before after
python3 tools/compare_corpus_slice_b.py before after --selftest   # 4/4 RED
```
