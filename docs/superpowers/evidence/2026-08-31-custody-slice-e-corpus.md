<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-E — corpus measurement, attribution, and the PRE-RULING re-anchor proposal

**Status: EVIDENCE + a PROPOSAL awaiting the owner's single ruling. ⛔ `simulation/BASELINE.md`'s
`### 36/36 corpus` anchor table is NOT edited by this document or by this slice.**

Slice: §CUSTODY-E (the typed terminal context; closes [[B263]]). Design authority: the custody spec
§9.3/§9.4/§11/§17-E. Instruments: `tools/run_corpus.py --jobs 8` on both arms, and the new
`tools/compare_corpus_slice_e.py` (7/7 controls, run FIRST).

## 0. The two binaries, and the positive control on the BEFORE arm

| arm | MeshRoute tree | `lus` md5 |
|---|---|---|
| BEFORE | a pristine `git archive HEAD` extraction (the repository is never modified to produce it) | `3668d44a146f52527cc15b8b21732e80` |
| AFTER  | the live slice tree | `487d7314f13831ba55a28b2f0360b96a` |
| AFTER (round 1, superseded) | the same tree before the QG round-2 fix | `a24556a2570fd9277c3630ee03198b39` |

★ **THE BEFORE BINARY REPRODUCES THE OWNER'S OWN `build/orchestrator/lus` MD5 EXACTLY (`3668d44a…`)**, so the
BEFORE arm demonstrably IS the pre-edit tree and the scratch CMake configuration is equivalent to the canonical
one. The AFTER md5 differs — the recompile control fired, so "nothing moved" could not have been produced by a
build that never happened.
★ **THE BEFORE ARM REPRODUCES ALL 36 ANCHOR ROWS** (`run_corpus.py`: `anchors: 36/36 rows reproduce
simulation/BASELINE.md`). Every delta below is therefore attributable to this slice alone, with nothing
subtracted — unlike Slice A, which had to work against 15 pre-existing movers.

## 1. The prediction, written BEFORE either arm was run

**THE EVENT-CLASS CLAIM — HELD, EXACTLY.** Only `emit_type:"push"` events whose `data.kind` is `send_failed`
may DISAPPEAR, and only from a node acting as a RELAY. Nothing may appear, no other event kind may move, and no
surviving line may change its bytes or its position — because the suppressed value is an app-ring Push, and
`push_send_failed` only calls `enqueue_push`: it feeds no routing, timing, admission or airtime decision.

**THE STREAM-LIST CLAIM — PARTLY WRONG, recorded rather than quietly dropped.** The prediction ranked
`s16_dense_gateway`, `s17_metro`, `s20_random_mesh`, `s19_singlelayer_multihop_chain`, `s06_seattle_lifecycle`,
`s18_meshroute`.
· CORRECT: s16 · s17 · s06 · s18.
· **PREDICTED AND DID NOT MOVE:** `s20_random_mesh`, `s19_singlelayer_multihop_chain` — both multi-hop, but
  neither produces a terminal give-up on a *transit application* carrier.
· **MOVED AND WAS NOT PREDICTED:** `s07_seattle_mobile_meshroute`, `s15_three_layer`, `s15_three_layer_metal`,
  `sim_9node_base`, `twin_9node_dm` — the 9-node scenarios in particular, which the prediction dismissed as too
  small to relay. `twin_9node_dm` alone lost 11 events.
⇒ The prediction's *whether* was right and its *which* was 4 of 9. That is exactly why the gate asserts the
EVENT CLASS rather than the scenario list: a mover list is a guess, a two-sided class claim is a measurement.

## 2. What the gate proves (`tools/compare_corpus_slice_e.py`)

For every one of the 36 streams, AFTER is BEFORE **with zero or more WHOLE LINES REMOVED**, every removed line
being a `push` whose `kind` is `send_failed`. Mechanically: delete those lines from BEFORE and the two files are
BYTE-IDENTICAL. The five sub-claims (D1 nothing added · D2 nothing modified · D3 only that class removed ·
D4 order and bytes preserved · D5 the per-kind census of everything else identical) and the reason the STRONG
form is available here — where Slice B could honestly assert only a multiset — are documented in the tool.

⚠ **THE ONE WAY IT COULD HAVE FAILED, NAMED IN THE PREDICTION AND THEN CHECKED.** `enqueue_push` is a
DROP-OLDEST ring of `cap_push_ring`; if a node's ring ever filled between two drains, suppressing one push would
change WHICH earlier push is evicted, and a push would APPEAR. D1 is that guard. **It did not fire** — zero
lines were added, in any stream.

**CONTROLS (run first; all 7 behaved as required):** a suppressed push accepted · an unchanged stream accepted ·
a deleted telemetry emit REFUSED · a deleted non-`send_failed` push REFUSED · an added line REFUSED · a modified
line REFUSED · a reordered pair REFUSED.

**RESULT: PASS.** 36 streams · **27 byte-identical** · **9 pure-deletion** · **61 `push{send_failed}` events
removed in total** · 0 added · 0 modified · 0 other event class moved.
`send_giveup`, `rts_giveup`, `data_ack_giveup`, `path_cascade_exhausted`, `cascade_load_skip`, `link_reprobe`,
`r_tx` and every other telemetry emit are byte- and order-identical in all 36 streams.

## 3. Attribution — every removed push is a TRANSIT report, and no OWN send lost its outcome

[[B263]] is *"a transit terminal give-up pushes a generic `send_failed` under a FOREIGN `{dst, ctr}`"*. The
over-correction failure mode — silencing an OWN send — is the one thing that had to be excluded, so it was
MEASURED rather than argued:

| evidence | count |
|---|---:|
| removed `push{send_failed}` events | **61** |
| the reporting node RELAYED that exact `{dst, ctr}` for a FOREIGN origin (a `data_rx` at that node whose `origin` is neither itself nor the destination) | **57** |
| the reporting node ORIGINATES NOTHING AT ALL in that scenario (`s16_dense_gateway` node 20: **0** `tx_enqueue`, **68** `data_rx` — a pure relay, so every carrier it can lose is foreign by construction) | **4** |
| **an OWN origination that lost its outcome** | **0** |

★ THE SHARPEST SINGLE CASE, because it is [[B263]]'s collision hazard occurring in the corpus rather than in
prose: in `twin_9node_dm`, node 3 holds BOTH its own DM to `dst 8, ctr 1` (`tx_enqueue{origin:4}` at
121 089 ms) AND a relayed carrier for `dst 8, ctr 1` originated by node 6 (`data_rx{origin:6}` at 70 390 ms) —
**the same `{dst, ctr}` pair from two different origins.** The push removed at 123 251 ms is the RELAYED one;
node 3's own send still reports its `send_failed{dst 8, ctr 1}` at 181 090 ms in the AFTER stream, untouched.
That is exactly the app-visible lie the register row describes, and exactly the half that must survive it.

## 4. THE RE-ANCHOR PROPOSAL (§10-style) — for the owner's single ruling

36/36 measured · **0 assertion failures on every stream, both arms** · analyzer `lus` `487d7314`.
Every Δ below is *entirely* `push{send_failed}` removals — no other event moved on any row.

★★ **RECONFIRMED AFTER THE QG ROUND-2 FIX (2026-08-31), AND THE RECONFIRMATION WAS NOT A FORMALITY.** The
sentinel-laundering correction turned `custody_stage_fail_reason`'s ternary into an exhaustive switch — real
codegen, so `lus` moved `a24556a2` → `487d7314` and the arm was **re-run in full** rather than argued inert.
**All 36 streams are BYTE-IDENTICAL to the round-1 AFTER arm** (sha256 per stream, not just the manifest md5),
every row below reproduces exactly, and the delta gate re-passed against the same BEFORE arm with the same 61
removals. ⇒ the `invalid` arm is unreachable in corpus traffic, as predicted, and **this proposal is unchanged.**

| # | scenario | pre-slice (= the current anchor table) | **MEASURED post-Slice-E** | events | Δ |
|---|---|---|---|---:|---:|
| 1 | `s06_seattle_lifecycle` | `7bc8d43e` | **`e8595775`** | **65541** | −20 |
| 2 | `s07_seattle_mobile_meshroute` | `7564f10d` | **`e50cbb56`** | **109576** | −12 |
| 3 | `s09_two_layer_gateway` | `71120178` | `71120178` *(unchanged)* | 2266 | — |
| 4 | `s09_two_layer_gateway_metal` | `0182f858` | `0182f858` *(unchanged)* | 2343 | — |
| 5 | `s10_two_layer_separation` | `c44c0b39` | `c44c0b39` *(unchanged)* | 2266 | — |
| 6 | `s15_three_layer` | `39d70c51` | **`f95e2d60`** | **51794** | −1 |
| 7 | `s15_three_layer_metal` | `b54b0a5c` | **`3611a93b`** | **52237** | −1 |
| 8 | `s16_dense_gateway` | `5d9a7186` | **`5b30637c`** | **23898** | −4 |
| 9 | `s17_metro` | `42e69427` | **`aa960050`** | **1181178** | −1 |
| 10 | `s18_meshroute` ★ **keystone** | `76a67335` | **`32afbf11`** | **269517** | −10 |
| 11 | `s19_singlelayer_multihop_chain` | `c669b1ef` | `c669b1ef` *(unchanged)* | 1065 | — |
| 12 | `s20_random_mesh` | `db240065` | `db240065` *(unchanged)* | 40566 | — |
| 13 | `s21_leaf_config_divergence` | `d7db6a04` | `d7db6a04` *(unchanged)* | 390 | — |
| 14 | `s21_mobile_dm_milestone_meshroute` | `fc466e77` | `fc466e77` *(unchanged)* | 678 | — |
| 15 | `s22_leaf_config_join` | `baadfbed` | `baadfbed` *(unchanged)* | 215 | — |
| 16 | `s22_mobile_team_meshroute` | `a47a9f76` | `a47a9f76` *(unchanged)* | 1824 | — |
| 17 | `s23_leaf_config_epoch_write` | `0cd16bd5` | `0cd16bd5` *(unchanged)* | 219 | — |
| 18 | `s23_mobile_team_multihop_meshroute` | `568c684f` | `568c684f` *(unchanged)* | 924 | — |
| 19 | `s24_static_and_team_multihop_meshroute` | `d06536f4` | `d06536f4` *(unchanged)* | 1576 | — |
| 20 | `s25_two_team_separation_meshroute` | `f87360c7` | `f87360c7` *(unchanged)* | 786 | — |
| 21 | `s26_team_reroute_meshroute` | `73a68a35` | `73a68a35` *(unchanged)* | 1037 | — |
| 22 | `s27_cross_layer_mobiles_meshroute` | `a33faca4` | `a33faca4` *(unchanged)* | 9359 | — |
| 23 | `s28_mixed_team_channels_meshroute` | `525756e2` | `525756e2` *(unchanged)* | 3861 | — |
| 24 | `s29_mixed_leaf_team_meshroute` | `bb534a88` | `bb534a88` *(unchanged)* | 2025 | — |
| 25 | `s30_team_dad_mediation_meshroute` | `4a1de37d` | `4a1de37d` *(unchanged)* | 1034 | — |
| 26 | `s31_dual_carrier_gateway` | `4eafb125` | `4eafb125` *(unchanged)* | 2300 | — |
| 27 | `s32_dual_cr_gateway` | `9574f5dd` | `9574f5dd` *(unchanged)* | 2266 | — |
| 28 | `s33_mixed_cr_channel_overhear` | `814ef421` | `814ef421` *(unchanged)* | 2845 | — |
| 29 | `s34_team_switch_clears_plane` | `0c724c05` | `0c724c05` *(unchanged)* | 919 | — |
| 30 | `s35a_cochannel_isolation_meshroute` | `bda1713b` | `bda1713b` *(unchanged)* | 2356 | — |
| 31 | `s35b_cochannel_isolation_control_meshroute` | `7dbc19ae` | `7dbc19ae` *(unchanged)* | 1063 | — |
| 32 | `s36_reprovision_purges_carriers` | `76d02e58` | `76d02e58` *(unchanged)* | 472 | — |
| 33 | `s37_team_homed_origin_meshroute` | `db535d42` | `db535d42` *(unchanged)* | 748 | — |
| 34 | `s38_team_origin_learn_meshroute` | `52be507e` | `52be507e` *(unchanged)* | 522 | — |
| 35 | `sim_9node_base` | `0def4316` | **`e7a1c3d6`** | **4945** | −1 |
| 36 | `twin_9node_dm` | `04647a50` | **`50080992`** | **14597** | −11 |

⛔ **On the ruling:** if the owner accepts, `simulation/BASELINE.md`'s `### 36/36 corpus` table takes the
**MEASURED post-Slice-E** column and the s18 keystone becomes **`32afbf11` / 269517 / 0**. Until then the
keystone of record remains **`76a67335` / 269527**, and this section is the standing evidence for the change.
⛔ Nine rows move; twenty-seven do not. A row that moved lost between 1 and 20 lines and NOTHING else.

## 5. Reproducing this

```sh
# BEFORE: a pristine HEAD tree, so the repository is never mutated to produce an arm
git archive HEAD | tar -x -C <scratch>/before-tree
cmake -S ../lora-universal-simulator -B <scratch>/lus-before -DCMAKE_BUILD_TYPE=Release \
      -DMESHROUTE_DIR=<scratch>/before-tree && cmake --build <scratch>/lus-before -j 8
cmake -S ../lora-universal-simulator -B <scratch>/lus-after  -DCMAKE_BUILD_TYPE=Release \
      -DMESHROUTE_DIR=$PWD && cmake --build <scratch>/lus-after -j 8

python3 tools/run_corpus.py --out <scratch>/corpus-before --jobs 8 --lus <scratch>/lus-before/orchestrator/lus
python3 tools/run_corpus.py --out <scratch>/corpus-after  --jobs 8 --lus <scratch>/lus-after/orchestrator/lus

python3 tools/compare_corpus_slice_e.py --selftest                      # the 7 controls, FIRST
python3 tools/compare_corpus_slice_e.py <scratch>/corpus-before <scratch>/corpus-after
```

## 6. The one prediction this document got wrong, kept in view

§1's stream list was 4 of 9. It is left standing rather than rewritten, because the gate that mattered — the
EVENT CLASS — is the one that was stated two-sidedly and the one that held. A corrected list would read as
though the prediction had been right.
