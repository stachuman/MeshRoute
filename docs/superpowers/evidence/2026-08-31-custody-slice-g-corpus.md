<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CUSTODY-G — corpus evidence, and the four-row RE-ANCHOR PROPOSAL · 2026-08-31

**Status: PROPOSAL — awaiting the owner's single ruling. ⛔ `simulation/BASELINE.md`'s `### 36/36 corpus`
anchor table is NOT edited by this slice.** Sibling of `2026-08-31-custody-slice-f-corpus.md`, same method,
same two-arm discipline, and a **different obligation**: Slice F added traffic and had to ATTRIBUTE the
movement; Slice G is receiver-LOCAL and adds none, so this document's job is to prove movement is **absent**
everywhere except in three named event types.

---

## 1. The two arms, and the control that says they are the arms they claim to be

| | BEFORE | AFTER |
|---|---|---|
| MeshRoute source | pristine `git archive HEAD` (Slice F, `7f4fb36`) into a scratch tree | the working tree |
| `lus` md5 | `160346e5` | `51ba1406` |
| `lus` sha256 (manifest) | `bc1eb3c9…` | `a41b5040…` |
| runner | `tools/run_corpus.py --jobs 8`, 36/36 produced and validated, 0 failures | same |
| wall clock | 59.9 s | 59.2 s |

★ **THE BEFORE ARM'S OWN CONTROL:** it was run with `--require-anchors` and reported
**`anchors: 36/36 rows reproduce simulation/BASELINE.md`**. The BEFORE arm is therefore not merely "an older
build" — it is *the build the anchor table currently describes*, so every difference below is Slice G's and
nothing else's.

ⓘ The BEFORE `lus` md5 `160346e5` is byte-identical to the AFTER binary Slice F's evidence recorded, which is
the independent confirmation that the pristine extraction really is Slice F's committed state.

---

## 2. The prediction, made FIRST, from Slice F's accounting

Slice F measured **25 notices aired / 25 attributed / 0 unattributed**, and — the load-bearing figure —
**only 11 of them reached an addressee**, where they died at Slice B's fail-closed tail guard with one
bounded `unsupported_internal` each: **s06 4 · s07 1 · s27 2 · twin 4**. The other 14 died en route or were
still in flight. F's own evidence states it: *"11 `unsupported_internal` events are the ratified intermediate
state… Slice G replaces that drop."*

**THE PREDICTION, written before the AFTER arm was run:**

1. **Exactly four streams move** — s06, s07, s27, twin_9node_dm — and they are exactly the four Slice F moved,
   because a stream with no addressed 0x81 has nothing for Slice G to do.
2. **`unsupported_internal` 11 → 0** corpus-wide.
3. **`custody_failure_rx` 0 → 11** and **`push{custody_failure}` 0 → 11**, at the SAME nodes and instants.
4. **`seq = 0` on every one of them** — [[B134]]: `Inbox::on_init` has exactly one production caller
   (`src/fw_main.cpp`), which the simulator does not compile, so the corpus runs with storage DISABLED and
   §7.3 rules `seq = 0` for that case.
5. **Line-count delta = +11 per corpus**: each guard drop is *replaced* (−1 +1 = 0) and each receipt adds its
   push (+1). Per stream: s06 +4 · s07 +1 · s27 +2 · twin +4.
6. **The s18 keystone stays byte-identical at `32afbf11`** — s18 carries no notices at all (F measured 0), so
   the receiver is inert on it by construction.
7. **ZERO `custody_failure_reject`** — the notices are produced by the real generator, so if F and G agree
   about the record they exchange, all eighteen §13 validations pass.
8. **NOTHING ELSE MOVES.** Per the brief's amendment 2 this is not a hope but a STOP condition: G originates
   no frame, changes no route and spends no airtime, so any movement in deliveries, duplicates, DATA/RTS/CTS/
   ACK traffic or airtime would mean the slice did something it is not supposed to be able to do.

## 3. The measurement

**Every one of the eight predictions held, exactly.**

| stream | BEFORE | AFTER | events | `unsup{0x81}` | `custody_failure_rx` | `push{custody_failure}` |
|---|---|---|---:|---:|---:|---:|
| `s06_seattle_lifecycle` | `303bada4` | **`e8f862b0`** ★ | 69035 → 69039 | 4 → 0 | 0 → **4** | 0 → **4** |
| `s07_seattle_mobile_meshroute` | `b2e97ff1` | **`dab62874`** ★ | 111676 → 111677 | 1 → 0 | 0 → **1** | 0 → **1** |
| `s27_cross_layer_mobiles_meshroute` | `0abe1650` | **`662c6158`** ★ | 9431 → 9433 | 2 → 0 | 0 → **2** | 0 → **2** |
| `twin_9node_dm` | `c93fe4eb` | **`dd28f145`** ★ | 14548 → 14552 | 4 → 0 | 0 → **4** | 0 → **4** |
| `s18_meshroute` (keystone) | `32afbf11` | **`32afbf11` UNCHANGED** | 269517 | 0 | 0 | 0 |
| the other 31 streams | — | **UNCHANGED** | — | 0 | 0 | 0 |

**32/36 streams are byte-identical**, and every one of those 32 still reproduces its `BASELINE.md` anchor
(checked explicitly, not inferred from "the runner did not complain": 0 unchanged-but-anchor-mismatched).

★ **THE FULL EVENT-TYPE HISTOGRAM OF THE FOUR MOVERS, DIFFED:**

```
s06_seattle_lifecycle             -> {'unsupported_internal': (4,0), 'custody_failure_rx': (0,4), 'push:custody_failure': (0,4)}
s07_seattle_mobile_meshroute      -> {'unsupported_internal': (1,0), 'custody_failure_rx': (0,1), 'push:custody_failure': (0,1)}
s27_cross_layer_mobiles_meshroute -> {'unsupported_internal': (2,0), 'custody_failure_rx': (0,2), 'push:custody_failure': (0,2)}
twin_9node_dm                     -> {'unsupported_internal': (4,0), 'custody_failure_rx': (0,4), 'push:custody_failure': (0,4)}
```

**THREE KEYS. NOTHING ELSE MOVED IN ANY STREAM** — not `delivered` (222 → 222 over the four movers), not
`dup_drop` (45 → 45), not `rx`, `tx`, `tx_lbt_defer`, `rts_tx`, `rts_rx`, `cts_tx`, `cts_rx`, `data_tx`,
`data_rx`, `ack_tx`, `ack_rx`, `collision`, `radio_busy`, `path_cascade`, `rt_update`, `rt_penalty_rerank`,
`link_bidi_confirm`, `beacon_rx`, `drop_sf_mismatch`, `drop_preamble_miss`, `peer_suspect_mark` — or any
other emit type the engine produces. ⓘ That list is not a list of keys someone remembered to name: the check
is a summary of what C1's exact ordered residue found, not a separate weaker check.

⛔ **THE HISTOGRAM ABOVE IS A READER'S SUMMARY, NOT THE PROOF.** The proof is C1's exact ordered residue
(§4): with the three permitted shapes struck, every remaining line of all 36 streams is byte-identical and in
the same position. The histogram is shown because it names the delta in one line; it is not what was relied
on.

⇒ **The amendment-2 STOP condition was never approached.** Slice F's accepted delivery trade (758 → 753) is
untouched by G; the figure moves by zero.

★★ **ZERO `custody_failure_reject` corpus-wide.** All 11 notices — built by the real generator from real dying
transit carriers and flown over the real PHY — passed all eighteen §13 validations at their real addressees.
That is the strongest end-to-end evidence available that F's packer and G's parser agree about the record.

★ **`seq = 0` on all 11**, exactly as [[B134]] predicts. A representative pair, verbatim from
`twin_9node_dm`:

```json
{"type":"script_emit","node":2,"time_ms":95981,"emit_type":"custody_failure_rx","data":{"ctr":1,"dst":9,"reporter":5,"seq":0}}
{"type":"script_emit","node":2,"time_ms":95981,"emit_type":"push","data":{"ctr":1,"dst":9,"kind":"custody_failure"}}
```

ⓘ Note the two events' `ctr`/`dst` are **deliberately different quantities** from the guard drop they replace:
`unsupported_internal` carried the NOTICE frame's own outer `{ctr, dst}`, while `custody_failure_rx` carries
the FAILED flight's, read out of the record. That is §15.2's correlation pair — the thing the whole design
exists to hand the sender — and it is visible in the corpus for the first time here.

## 4. The accountant, and its controls

`tools/compare_corpus_slice_g.py` (new; the Slice-F comparator's sibling, with its own checks because the
obligation is different — see its module docstring).

⚠⚠ **AN EARLIER CUT OF THIS INSTRUMENT REDUCED EACH STREAM TO AN EVENT-TYPE HISTOGRAM AND WAS REJECTED AT QG
REVIEW, correctly.** A histogram is blind to a changed field, a changed timestamp, a reordering, a changed
non-`script_emit` line, a custody push carrying the wrong `{dst, ctr}`, and a *missing* `seq` (which compared
equal to zero). **Counting the right things is not the same as proving nothing else moved.** The instrument
below is the **Slice-A/E strong form** this arc already owns, and the figures in §3 are its output.

**THE DISCIPLINE: `AFTER == BEFORE, MINUS AND PLUS WHOLE PERMITTED LINES`.** Strike the three explicitly
permitted shapes from each side and every remaining line must be **byte-identical, in the same order, at the
same stream position**. Nothing on that path is parsed, normalised or summarised — a raw string comparison is
the only form that cannot be blind to a field somebody forgot to name. Memory is O(permitted lines), not
O(stream): the two sides are walked in lockstep (`s17_metro` alone is 1.18 M events).

| | check |
|---|---|
| C1 | **the EXACT ORDERED RESIDUE** — every non-permitted line, byte-for-byte, in position. Subsumes the old histogram entirely: `delivered`, `rx`, `tx`, `rts_tx`, `cts_rx`, `data_rx`, `ack_tx`, `collision`, `rt_update`, every timestamp, every field of every event and every non-`script_emit` line, because **nothing is excluded from it** |
| C1b | the streams whose bytes moved are EXACTLY those that carried an addressed 0x81, both directions |
| C2 | **the BIND, per occurrence:** every AFTER `custody_failure_rx` consumes a BEFORE `unsupported_internal{0x81}` at the SAME `(node, time_ms)`, each exactly once |
| C3 | no 0x81 guard drop survives, and no OTHER type's guard drop moved — **both owned by C1**, with a named control for each half (a surviving 0x81 has no AFTER sink and falls into the residue; another type's drop is an ordinary kept line) |
| C4 | **IDENTITY-BOUND pairing:** each receipt pairs with exactly one `push{custody_failure}` on **`{node, time_ms, dst, ctr}`** — ⛔ not on counts. A push naming a different failed flight is the §15.2 correlation-pair defect |
| C5 | every receipt carries an **EXPLICIT integer `seq == 0`**, tested as **`type(seq) is int and seq == 0`**. ⛔ A **MISSING** `seq` fails (`None` is not zero) — and so do JSON **`false`** and **`0.0`**: in Python `False == 0` and `0.0 == 0` are both true, and `isinstance(False, int)` is true as well because `bool` subclasses `int`, so the type IDENTITY is the only form that rejects them. `0` means exactly one thing: storage disabled (§7.3 / [[B134]]) |
| C6 | zero `custody_failure_reject` |

```
$ python3 tools/compare_corpus_slice_g.py <before> <after> --selftest
  streams: 36 · moved: 4 · custody receipts: 11
  …
  SELFTEST PASS — 15/15 controls RED (count derived). The result above is a measurement.
  PASS — the ONLY corpus delta is the addressed-0x81 outcome representation, and the ordered residue
         is byte-identical.
```

**The controls, each doctoring exactly one thing, each required RED — and the count is DERIVED from the
results list, never written down in prose** (an earlier revision of the docstring said "eight" while nine
ran, which is the same class of stale figure the checks themselves exist to catch):

1. **a FIELD of an ordinary event corrupted** — the hole the histogram form had
2. **two ordinary events REORDERED** — same multiset, different stream
3. **a custody push naming a DIFFERENT failed flight** (`dst` changed) — §15.2's pair broken
4. **a receipt with NO `seq` field** — a missing seq is not a zero
5. one `delivered` event added — the amendment-2 STOP condition
6. one ordinary event disappears
7. an `unsupported_internal{0x81}` survives the slice
8. a guard drop of ANOTHER type moved — Slice B's guard weakened
9. a receipt binding to no prior guard drop (the wrong frame consumed)
10. a guard drop never replaced (a notice silently stopped being consumed)
11. a diagnostic with no push
12. a nonzero `seq`
13. **`seq` is JSON `false`** — equal to `0` in Python, and not the integer `0`
14. **`seq` is the float `0.0`** — equal to `0` in Python, and not the integer `0`
15. a malformed-input rejection appears

⚠⚠ **CONTROLS 13 AND 14 WERE ADDED AFTER A SECOND QG FINDING, AND THEY MATTER:** C5's first cut tested a bare
`seq != 0`, which **passed** doctored streams carrying `"seq": false` and `"seq": 0.0` with **zero findings**
(QG reproduced both). `isinstance(seq, int)` would not have fixed it either — `bool` subclasses `int`. The
check is now `type(seq) is int and seq == 0`, and the failure message names the offending type
(`'False (bool)'`, `'0.0 (float)'`) so the distinction is visible rather than inferred. **A predicate that
cannot tell `false` from `0` is not a type-strict check, however it is described in prose.**

★ **THE DOCTORED LINE IS CHOSEN AFTER THE FIRST PERMITTED LINES ON BOTH SIDES, deliberately** — a control
acting at stream position 0 would never exercise the striking and could pass against a comparison that
ignored the permitted shapes entirely. Measured: the residue controls report divergence at **kept-line
#6979** of `s27`, i.e. the walk had already struck a guard drop from BEFORE and an rx+push from AFTER before
it could reach the doctored line. ★ The undoctored pair is asserted GREEN first, so a control cannot be red
for free.

## 5. THE RE-ANCHOR PROPOSAL — four rows, for the owner's single ruling

**⛔ NOTHING IN `simulation/BASELINE.md` HAS BEEN EDITED.** The four rows below are a proposal; the keystone
and the other 31 rows are unchanged and need no ruling.

```
s06_seattle_lifecycle                        e8f862b0 lus: 69039 events emitted, 0 assertion failure(s)
s07_seattle_mobile_meshroute                 dab62874 lus: 111677 events emitted, 0 assertion failure(s)
s27_cross_layer_mobiles_meshroute            662c6158 lus: 9433 events emitted, 0 assertion failure(s)
twin_9node_dm                                dd28f145 lus: 14552 events emitted, 0 assertion failure(s)
```

**UNCHANGED, and stated so the ruling is bounded:**

```
s18_meshroute                                32afbf11 lus: 269517 events emitted, 0 assertion failure(s)
```

**WHY THE MOVEMENT IS FULLY ATTRIBUTED:** the four rows move by **+11 lines in total**, all of them in three
event types, at eleven `(node, time_ms)` points that each previously held a guard drop of `0x81`. The
attribution is not "these are the only streams we expected to move" — it is C1's EXACT ORDERED RESIDUE
(every non-permitted line byte-identical and in position) plus C2's per-occurrence bind and C4's
identity-bound pairing, all measured, with every control RED and the count derived.

## 6. What else was measured in the same session

- **native** 2463 / 102123 / 0 → **2503 / 103323 / 0** (+40 cases, +1200 assertions; the derivation is in
  `tools/probe_ui_model_mutations.py`'s `PIN_CASES` note).
- **s18 keystone**, run directly as the D1 tripwire: `32afbf11` / 269517 events / 0 assertion failures —
  **read from `BASELINE.md`'s anchor table, never assumed**.
- **boards**, both arms MEASURED with `tools/measure_board.py pair --jobs=2` (BEFORE built from the same
  pristine tree as the corpus BEFORE arm):

  | env | RAM | Δ RAM | flash | Δ flash | objects |
  |---|---:|---:|---:|---:|---:|
  | `gateway` | 195660 → 195660 | **+0** | 509452 → 513564 | **+4112** | 283 → 283 |
  | `heltec_mobile` | 205620 → 205620 | **+0** | 1356904 → 1359476 | **+2572** | 327 → 327 |

  `sizeof(Push)` is unmoved (its `static_assert` compiles on every board ABI), so `Node::_push_ring[32]` and
  `sizeof(Node)` are unmoved — which is why the RAM delta is exactly zero on both.
- **warning census** PASS at its pins, nothing re-pinned, `-Wswitch` **0** on all six OLED envs.
- ⛔ **USB COVERAGE, STATED HONESTLY:** §14.3's console line is **board-compiled only** — `src/fw_main.cpp`
  is outside the native build (§B115), so its `-Wswitch`-guarded arm proves the kind is HANDLED, never what
  it prints. **The JSON is the host-proven surface; the USB line's CONTENT is metal-pending (bench Part 53).**
- **probes**: `tools/probe_firmware_ui/run.sh` PASS (223 controls verified, 0 unusable) ·
  `tools/probe_inbox_verbs/run.sh` PASS (39 checks / 8 controls, both at their pins).
- **checkers**: `check_a0_matrix.py` PASS + 10/10 selftest controls RED ·
  `check_data_type_literals.py` PASS + 4/4 selftest controls RED.
