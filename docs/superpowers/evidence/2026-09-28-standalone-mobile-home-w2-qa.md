<!-- Author: Codex (independent QA); 2026-09-28 -->
# Standalone Home W2 — independent QA gate

**INDEPENDENT QA PASS.** The frozen W2 candidate satisfies revision 3. QA reran all nine steps of §4.2's focused
chain, in order. **B418, B451 and B458 close.** B459 remains the next separately fenced tool package, before W6;
B460 remains outside W2. New **B461** records a pre-existing, nonblocking W50 comment error.

Contract: [W2 brief, revision 3](../plans/2026-09-28-standalone-mobile-home-w2-board-ui-readers.md), SHA-256
`341d4bc0c401c8fd254107eea90915cefb2dee94580cdcaf912a99f015c89737`.
Candidate: [coder receipt](2026-09-28-standalone-mobile-home-w2.md). Base MeshRoute
`8079e545fb2e1065c2bbc347eb9a66160f6cbf8e`; simulator `6585649ea5a780f0542b2931853a667be56a5b2b`, clean.
Nothing is staged or committed. A commit is not a blocker for the next package.

## Freeze and scope

All four candidate hashes match the coder's freeze. The six preparation pins, 19 read-only dependency pins and
brief hash match. Earlier evidence verifies: pre-check 26/26, first review 8/8, re-review 11/11, coder folder 44/44.
Against the re-review inventory, only the four fenced tools changed; all added paths are explained QA/coder evidence.
The complete QA entry inventory contains **1,797 MeshRoute paths and 285 simulator paths**, including symlinks.

| Fenced file | Independently verified SHA-256 |
| --- | --- |
| `tools/probe_board_ui/run.sh` | `0ee0bc90df8d8a6e1d5adce73958f2dd32061388c0f3644f70d9e1f88ce56fe8` |
| `tools/probe_board_ui/fakes/Arduino.h` | `deaa9f4a6f3d010fa4010722973c701c4f5effadb2442213812b7884f85ab853` |
| `tools/probe_inbox_verbs/transcript_main.cpp` | `3914de10acddb6a6e39384fa4f512461133e1d7ca538f50cb66939a8cd828163` |
| `tools/probe_ui_model_mutations.py` | `81842cc53d806f1ed4dbce5bb7699a945e50dc6381f8258cf3c60eec73e2781a` |

No product, build, test or simulator input changed. The approved four-file fence is respected. The two comment-only
edits have identical preprocessed output; an added executable declaration changes that output. The model harness's
AST equals the base AST with only M103 removed. Text comparison independently limits the delta to its tuple and
adjacent history comments. Its five named witnesses remain active.

## Fresh gate results

| Step | Independent result |
| --- | --- |
| 1 — scope/hygiene | Frozen hashes and preparation match; no unexplained changes; whitespace clean; simulator clean |
| 2 — comment-only proofs | Both files PASS; comment-only diff and preprocessing equality; executable controls RED |
| 3 — M103 retirement | Model 239 → 238; no other executable or text change outside the retirement hunk; history retained |
| 4 — stock board-UI default | Exit 0; V3 **124/124**, V4 **110/110**; structural **23/23**; wiring **60/60**, **186 controls RED**; trait controls **14 RED**; missing-trait controls **12 required compiler refusals**; canvas controls **60 V3 + 3 V4 RED** |
| 5 — stock console-sink default | Exit 0; **6 profiles / 720 checks / 84 structural / 905 BLE guard / 6 ownership / 3 ownership controls / 152 controls / 0 unusable** |
| 6 — stock model battery | Exit 0; **238/238 RED, 0 unusable**; each declared label executes once, each anchor matches once; eight clean worker baselines **3031/198613/0** |
| 7 — full tools discovery | **375 tests OK, 0 skipped**, exit 0 (737.98 s wall time) |
| 8 — stock inventory check | **197 command rows**, tracked inventory byte-identical to fresh generation |
| 9 — source-reader audit | 27 search patterns across lib/src/test/tools; affected readers exercised or proven unaffected below |

The board-UI run took 23.23 s; console-sink 67.83 s; the model battery 276.64 s. Times are informational.
Discovery prints an intentional B278 census-selftest failure from its negative test, and a `/dev/null`
`ResourceWarning`; its final unittest verdict is **375 / OK**, not a reduced suite or a hidden import failure.

PIN re-synced? YES — native 3031/198613 unchanged (8/8 clean model workers); model 239 − 1 retired M103 = 238 configured, 238 RED; wiring checks 59 + 1 = 60; wiring controls 171 + 6 W49 + 4 W51 + 4 W54 + 1 W54-help = 186, independently reconciled

## Control accounting and named failures

The stock default runner was invoked with shell tracing and its output captured separately. QA reviewed and reran
the coder's declaration, reconciliation, ordinal-map and per-control proof helpers on **QA's own run and fresh
scratch mutants**, checked their result objects explicitly, and added separate trace-increment and omission checks.
No coder result file supplies a QA gate figure.

- All **60 declared wiring checks** execute once and pass; all **186 declared control scripts** execute unchanged
  and receive a RED result. Independent trace increments are exactly 1…60 and 1…186, with no failure increment.
- Structural calls, literal loop cardinalities, traits, missing-trait controls and both canvas-control lists also
  reconcile. Nothing is missing, duplicated or unknown. Removing W1's invocation or the last RED increment from
  a trace copy makes the reconciliation fail. B459's production runner gap remains open; this audit compensates.
- All **15 affected controls** match once, change bytes and fail their required clauses. W49 relocation leaves one
  arm globally, none inside `dispatch()` and one inside `handle_teststatus`; **only placement fails**. Changing
  either the destination signature or the source marker on separate, live-GREEN copies yields byte-identical
  control output. W54-help retains the literal and fails **only** the guard clause.
- W51's own-handler/refusal controls fail its token ban; removing the seam fails its seam clause; removing only
  the flush fails its flush clause. W54's four command controls keep their original effects.
- All **171 other controls** and their 56 check declarations are unchanged from base. Every affected old control
  has a successor; the only added check is W54-help and the only added control is W51's flush removal.
- Console-sink's stock controls pass their contracts. An additional fresh replay of exact X14–X17 edits confirms
  X14–X16 fail S23; X17 **deletes** the router call and fails S24/S53/S62. Its historical label is not its edit.

## Reader audit and coder dispositions

The new W49/W51/W54/W54-help labels have no external source reader. The edited runner's other references are
comments, documentation or usage strings. Console-sink reads the dispatch/flush anchors; those production inputs
are unchanged and its full gate ran. Help readers include console-sink, the inventory generator and feature
ownership checks; no help source changed, and the affected default gate, discovery and inventory checks pass.

M103 has no remaining executable definition. The harness readers in `test_worker_formula_derived.py` and
`test_mutation_unusable_reason.py` passed discovery; all code they inspect is unchanged. Other tool references to
the harness are comments. Historical “239 entries” measurements remain historical, not live pins.

The fake's consumers compile unchanged tokens. Firmware-UI and inbox-verbs also hash it before/after a run, without
pinning its old hash. `transcript.py` compiles `transcript_main.cpp`; its locator edit affects no tokens and that
file is absent from the in-run hash lists. BLE-line/custody-USB use their separate fake. No source reader depends
on either removed comment or the old S24 locator. The retained `run.sh.orig` reference is not an active gate entry.

Coder choices accepted: the W51 label now names the seam; W49's destination is the neighbour's body; dated history
is distinguished from corrected claims; the transcript's unchanged “fake is NOT edited” refers to its numeric
behaviour. **W54-help is a line-based source predicate**, matching W54's existing idiom; do not treat it as an
arbitrary C++ occurrence parser. Rendered help-name uniqueness is independently exercised by console-sink H2e and
its duplicate-name control H-C3, which went RED in this run.

**B461 — nonblocking comment drift.** `tools/probe_board_ui/run.sh`, W50 comment (line 1291 at freeze), calls the
wrapper-removal control “(c)”. It is control **2**; control **3** relocates the boot call into `loop()`. The same
comment and three controls exist at base. All three controls execute RED. Register the stale ordinal; a later
fenced comment-only edit can correct it with D6 reader/control verification. W2 does not authorize that edit.

Two QA evidence-helper setup errors failed loudly: a trace selector assumed the wrong shell quoting, and the first
W50-call collector stopped after its first continued line. They were corrected and rerun. Neither altered the
candidate, stock instruments or their captures; no failed helper result is credited as PASS.

## Preservation, landing and limits

After the last gate step, **all 1,797 MeshRoute input paths and 285 simulator input paths match their entry
inventory**, both HEADs are unchanged and nothing is staged. Only QA evidence was added during the chain.
After that preservation proof, QA lands the register closures/new B461 row, design W2 status and tracker/MEMORY
pointers. These four documentation changes are recorded separately; the approved brief and four frozen tools remain
unchanged. The metal plan is untouched.

Per brief §4.2, no separate native suite, corpus, board measurement/build, ABI or warning-census run was needed:
product/build/test inputs are preserved by hash and the model workers rebuild native in isolated copies. The
firmware-UI and inbox-verbs probes were not rerun because their only changed compiled inputs are proven comments
and their text readers are unaffected. The coder's pre-edit baseline was not repeated. No new metal PASS is claimed.

Future UI briefs use the **board-UI default run, exit 0**, plus independent declared-versus-executed reconciliation
until B459 closes. The old B418 three-failure exception is retired. B459 can start from this frozen, QA-passed
candidate without a commit; B460 and B461 are separately recorded follow-ups, with no scope grant here.

Evidence: [SHA256SUMS](2026-09-28-standalone-mobile-home-w2-qa/SHA256SUMS). Raw fresh logs and the full shell trace are
under ignored `artifacts/2026-09-28-standalone-mobile-home-w2-qa/`, hashed in the evidence folder's `raw-logs.json`.
