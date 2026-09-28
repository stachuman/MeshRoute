# Standalone Home W2 — independent QA pre-check

2026-09-28. QA seat: Codex; brief author: Claude. **Pre-check complete: the proposed B418/B451/B458 repair fits a tool-only brief. No production edit or owner ruling is needed for those three repairs.** B459 is a separately measured accounting defect, registered at the owner's explicit Q7 request and **not folded into W2**. This report does not approve an implementation or close any of those findings.

Recommended implementation fence: `tools/probe_board_ui/run.sh`, `tools/probe_board_ui/fakes/Arduino.h` (comments only), and `tools/probe_ui_model_mutations.py` (M103 retirement/history only). Evidence is additional. No `src/`, `lib/`, `test/`, `variants/`, `platformio.ini`, simulator, command-inventory regeneration, numeric-fake behavior, E14/B453 or B443 edit is necessary.

The evidence directory is [2026-09-28-standalone-mobile-home-w2-precheck/](2026-09-28-standalone-mobile-home-w2-precheck/), with its own `SHA256SUMS`. Source pins are by symbol (P4); line numbers below locate this exact base. All temporary mutations ran under `/tmp`, never in shared production or tool files. Their results are explicitly synthetic feasibility or fault measurements, not a W2 candidate gate.

## Q1. Committed base and preparation inventory

MeshRoute started **clean** at `8079e545fb2e1065c2bbc347eb9a66160f6cbf8e` (`W4b implemented`). Simulator started **clean** at `6585649ea5a780f0542b2931853a667be56a5b2b`. The entry inventory records **1702 MeshRoute paths** (1698 regular files and four tracked symlinks) and **285 simulator paths**. It contains 1699 file-content SHA-256 hashes, including the dereferenced `spec/dv_dual_sf.lua`, plus all four link targets. The final inventory audit added the three directory symlinks omitted by the initial file-only walk and verified every link against the clean entry commit. Tracked plus nonignored untracked paths are covered. There were no uncommitted candidates to reconstruct. Ignored build outputs are not source inputs and are not called clean evidence merely because git ignores them.

Reconciliation against W4b QA's receipt: all **15 implementation/test/tool pins**, the frozen brief and pre-check report match. The four preparation documents that differ from the coder freeze are the documented QA landing. All **five QA landing hashes**, including the metal plan, match the owner commit exactly. See `base-reconciliation.json`; it compares the entry inventory, before this pre-check added B459. Older documents still describing W4b as uncommitted are historical receipts, not the new base.

The brief should pin this commit plus the post-pre-check inventory and explicitly declare any subsequent author preparation edits. `inputs.json` preserves the clean entry set; `final-preservation.json` identifies this task's only existing-file edit (the requested B459 registration). The report/evidence are new. A commit is not a progress gate.

## Q2. W49: dispatch placement and offsets

Verified in real source:

- Old `DISPATCH_SIG` matches **0** definitions. Current prefix `bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {` matches **1**, at `firmware_commands.cpp:1548`. The definition has a trailing comment; match the prefix through `{`, not an end-of-line assertion that includes no comment.
- The complete `ui` arm occurs **once**, at :1636, between `#if MR_FEAT_OLED` and its immediately following `#endif` (:1635–1637). Recognition, `line + 2`, `len - 2` and return ownership remain intact.
- The stock predicate becomes GREEN when **only the extraction signature** is corrected in a scratch evaluation. All six stock control scripts then make it RED, but control 5 does so for the wrong reason.
- `static void dump_help(Print& out) {` matches **0**. Control 5 deletes the arm and reinserts nothing: measured file-wide arm count **1 → 0**, exactly the deletion fault already covered by control 1.
- `static void handle_teststatus(Print& out) {` exists **once**, :1534, immediately above dispatch.

Recommend the current full prefix for extraction, preserving the exact guarded arm and file-wide uniqueness clauses. Re-anchor control 5 to move the guarded arm to `handle_teststatus`: require **one deletion, one insertion, one arm still file-wide, zero in dispatch**, and RED specifically on placement. Keep the guard around the moved arm so the experiment isolates function placement. This is a structural mutant; the board-UI wiring controls do not compile these mutated command TUs. Do not claim compilation proof for it. No production marker is needed.

## Q3. W51: BLE delegation and its controlled witness

The extracted `ble_dispatch_line` at `fw_main.cpp:519` contains **zero** forbidden `"ui`, `handle_ui`, or `preset_` tokens after the runner's comment stripping. The old fallback is absent. The real tail has exactly one:

```cpp
const mrfw::LineExec ex = mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, out, cap, ctx);
if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }
```

These are :681–682. The seam's router call is in `firmware_commands.cpp::exec_console_line`, :1741. The previous insertion point `if (e == ParseErr::unknown_verb) {` is absent everywhere in `fw_main.cpp`. **All three old W51 controls change zero bytes**, measured separately even though the stock runner never reaches them.

Recommend W51 require, **inside the extracted BLE function**, exactly one correctly argumented seam call and exactly one streamed-arm flush/return, while retaining the three-token negative-space ban. Use the current unique seam-assignment line as the insertion boundary for the two separate-handler controls. Remove that call for control 3. Add a fourth control that removes only the streamed flush: each positive obligation then has a local witness. No production edit is needed.

The seam-to-dispatch link already has a controlled witness in **console-sink `structural.py` S24**, not in the BLE-line byte assembler. Fresh execution of the stock structural reader: **84/84**. Exact stock source mutations, replayed on copies:

| Existing control | Actual fault | Required result reproduced |
|---|---|---|
| X14 | Adds residual direct dispatch in BLE | S23 RED |
| X15 | Deletes streamed response flush | S23 RED |
| X16 | Replaces `out, cap` with `nullptr, 0` | S23 RED |
| X17 | Deletes the seam's router call | S24 RED (also S53/S62) |

All four anchors match exactly once. `witness-audit.json` records literal substitutions and named failures. **Precision:** X17's label says “parser BEFORE router”; its actual substitution deletes the router call rather than moving it below the parser. It is a valid witness for the link requested here, not evidence of an executed reversal. This is an existing description discrepancy, outside the three-file repair fence; the author should retain that qualification if citing it. We do not silently reinterpret it as a stronger control.

The inbox-verbs real-seam rows X1/X2/X17/X20 additionally execute router ownership and supplied sinks, but this report does not claim a fresh full inbox gate.

## Q4. W54: retain an explicit help gate

Five current command-file tokens each occur once, inside the OLED regions: the preset-verbs include, the single catalog instance, `preset_boot_restore_console`, `handle_ui`, and the status catalog reference. The sixth token, `out.println(F("UI PRESETS`, occurs **zero** times. The current help block in `firmware_help.h:132–134` is:

```cpp
#if MR_FEAT_OLED
    out.println(F("ui"));
#endif   // MR_FEAT_OLED
```

The header's block occurs once. Console-sink compiles real help on OLED and headless profiles; `kGatedNames`/H3a and the inventory equality check would detect ungated `ui`. **There is no stock control that ungates this particular help row.** H-C5 ungates `team`; H-C6 removes `mobile`. Those controls are not a replacement witness for the `ui` preprocessor boundary. Do not retire this property by citing H-C5 as if it changed `ui`.

Smallest clear repair: keep W54 for its **five command-file tokens**, with controls 1/2/3/5; introduce **W54-help** using the same `wchk_in`/`oled_guarded` authorities against `firmware_help.h`, with the re-anchored control 4. Require the one `ui` line globally and inside its OLED region. Its control should replace the uniquely matched three-line block with an ungated `ui` line, preserving the literal (failure must be the missing guard, not deletion of the name). This needs no help-header edit. One multi-file W54 predicate is also possible, but must direct each mutant to the correct file rather than reading an unmutated global path.

Correct the present-tense comments in the edited reader area: W54 is not the only instrument able to see help; help no longer lives in the command TU; the current loop has six tokens, not “five sites all in commands”; W50 pins the unconditional boot call, not an OLED guard around that call. The W49–W51 introduction's assertion that `firmware_commands.cpp` cannot be host-compiled is historical: inbox-verbs now compiles it. `fw_main.cpp` remains structurally inspected. Keep dated history distinguishable from current claims.

## Q5. Disposition of all 14 dormant controls

The source audit extracted and executed the **stock sed expressions**, even where a failing live predicate normally suppresses the loop. `dormant-controls.json` includes each literal script. Ten change bytes; four are no-ops. A RED predicate on an already-failing baseline is not a successful control.

| Check/control | Disposition | Measured current behavior; required future fault/failure |
|---|---|---|
| W49-1 | Keep | Deletes the only arm; uniqueness/presence RED |
| W49-2 | Keep | Changes `line + 2, len - 2` to unsliced input; exact arm RED |
| W49-3 | Keep | Removes handler but retains ownership return; handler presence RED |
| W49-4 | Keep | `strncmp` length 2 → 1; exact recognition RED |
| W49-5 | Re-anchor | Currently deletion only; move once to real neighbor, retain file-wide count 1, dispatch placement RED |
| W49-6 | Keep | Comments out dispatch OLED guard; guarded-arm RED |
| W51-1 | Re-anchor | Currently no-op; insert independent BLE `ui` handler before seam; forbidden-token half RED |
| W51-2 | Re-anchor | Currently no-op; insert BLE-only `ui` refusal before seam; forbidden-token half RED |
| W51-3 | Replace old literal | Currently no-op; remove actual seam call; positive half RED |
| W54-1 | Keep | Comments out preset include guard; include unguarded RED |
| W54-2 | Keep | Comments out catalog binding guard; instance/boot/handler region RED |
| W54-3 | Keep | Comments out status guard; status token unguarded RED |
| W54-4 | Re-anchor to help header | Currently no-op; retain `ui` literal outside its OLED guard; W54-help RED |
| W54-5 | Keep | Deletes only catalog instance; uniqueness/presence RED |

No property among these 14 needs retirement. The recommended additional W51 flush control is additive. Require each new/re-anchored edit to match once and make a real change; especially require both halves of the relocation control, not merely `cmp` detecting its deletion. Preserve existing control meanings and named failure clauses.

With the recommended separate W54-help and fourth W51 control, the predicted wiring total is **60 checks / 186 controls**: 59+1 checks, and 171 already exercised +6 W49 +4 W51 +4 W54-command +1 W54-help controls. Without the optional local flush control it would be 185 controls, with X15 explicitly supplying that witness. These are derivations for the author, **not results of an implemented repair**.

## Q6. Fresh full default run

Command: `bash tools/probe_board_ui/run.sh` (no `--no-neg`). **Exit 1, only W49/W51/W54 fail on real inputs.**

| Component | Independent result |
|---|---|
| V3 real canvas | 124/124, zero failures |
| V4 real canvas | 110/110, zero failures |
| Trait controls | 14 RED, zero failures |
| Missing-trait compile controls | 12 fail through their required compiler refusal |
| Structural readers | 23/23 |
| Wiring readers | 56 pass / 3 fail / 59 total |
| Wiring controls actually exercised | 171 RED; the 14 above did not execute in this stock run |
| `negctl.py`, V3 | 60 executed, each caught by failing checks |
| `negctl.py`, V4 | 3 executed, each caught by failing checks |

Both canvas-control arms verify the real source unchanged. No additional stale negctl anchor, build failure, surviving canvas mutant or vacuous control was observed. Raw log hash and compact summary are retained.

Previous full-run date is **not established by the retained evidence I could verify**. The 2026-09-18 8b receipt records a candidate supplemental run but explicitly names `--no-neg` only for its baseline; its compact retained directory does not establish which mode the candidate used. Recent W1/W3/W4a/W4b receipts explicitly used `--no-neg` or excluded the full probe. The searchable history is retained in `board-run-history.tsv`. I do not infer that the default controls ran from a wiring-control count, because those controls run in both modes.

After W2 passes independent QA, future affected UI briefs should require the **default run, exit 0**, with all controls accounted for; retire the B418 known-failure exception. `--no-neg` remains a diagnostic, not a substitute gate.

## Q7. Accounting (B459)

Confirmed: the runner has no declared-versus-observed check-set comparison, and there is no `tools/test_probe_board_ui.py`. `wchk_in` increments only counters for invocations that occur. Final success checks failures, not whether expected checks or controls disappeared.

Two **full default** synthetic runs used copies of all 1699 inventoried readable inputs, without changing any predicate or product input:

1. Remove only the W49/W51/W54 call sites: **exit 0**, wiring **56/56**, **171** controls. Three real baseline failures vanished.
2. From that diagnostic fixture, also remove healthy W1's call: **exit 0**, wiring **55/55**, **170** controls. The healthy check and its control vanished too.

Both retained 23 structural readers, trait controls and all **60 V3 +3 V4** source controls. The second run isolates loss of a healthy check against the first run's successful fixture. Neither is a valid product gate; they demonstrate the runner's missing completeness contract. `accounting-proof.py` and JSON reproduce and label the omissions; the shared runner is untouched.

Registered **B459**, next free **B460**, using the request's explicit Q7 exception to the general evidence-only constraint. No repair or scope expansion is inferred. Until separately repaired, a W2 gate must independently reconcile its declared call/control list with the run, rather than trust a lower green summary. A trace of the stock shell runner can provide invocation/control labels without changing it. Owner decides whether a separately specified B459 repair joins W2.

## Q8. M103 retirement feasibility (B458)

M103's only active executable definition is the three-field tuple in `tools/probe_ui_model_mutations.py:3514`. There are historical references in W4b evidence, the design/dispatch and this pre-check, so “the register is its only other mention” is not literally true; none is another active mutation or a selector that needs changing. No tools test pins M103 or the current model cardinality to 239.

Recommend replacing the active tuple and its now-obsolete explanation with a dated retirement comment retaining its ID, old effect (both cursor and selection-valid resets removed), W4b QA/B458 reason, and live witnesses M100/M101/M102/M105/H25. Follow the same file's retired CUSTODY-B precedent (:350), preserving history without an active tuple. Do not manufacture a private-state test for an obsolete selectable closed view.

Fresh **evidence-only retirement projection**: copied all 1699 inventoried files outside the repo, deleted only that tuple, then ran the stock harness's full `--target=model` path. **238 configured / 238 RED / 0 unusable**, exit 0. All eight workers independently derived **3031 cases / 198613 assertions / 0 failures**. Every original target file was unchanged after the run. `model-projection.json`, `model-exit.json` and the hashed full log record exactly what differed. This is feasibility evidence; the coder and QA must rerun against the eventual frozen W2 harness.

The native PIN stays **3031/198613**. No production layout, test count or simulator pin moves. `239 entries` at :628/:642 is explicitly the **2026-08-30 worker-speed comparison**, including historical 6-versus-8-worker measurements; leave it historical, rather than rewriting it as today's count. Dynamic configured totals fall by one. No other active cardinality pin was found. E14/B453 stays outside this package even though its tuple is in the same file.

## Q9. Shared fake comment and consumers (B451)

`Print::print` numeric overloads accept but ignore the base argument; **HEX produces decimal digits**. Preserve every executable byte/signature. The false assertion at Arduino.h:90–91 should be replaced by a current fidelity note:

- **board-UI:** no assertion depends on a based `Print` numeral. Its canvas assertions concern shim calls, GPIO, geometry and fonts, not those serial digits.
- **firmware-UI:** console assertions inspect fixed warning text or absence of output, not based digits. Panel identity hex is produced by UI formatters, not this `Print` numeric shim; do not conflate them.
- **inbox-verbs `probe_main.cpp`: X21–X26**, on **both ACCEPT and CLIENT**, compare all six TEXT/JSON `whoami` lines exactly: unnamed, `Bench 1`, 32-byte name. Their oracle at :991 uses `hash=0x%lu` deliberately. These **12 checks** depend on the fake's decimal rendering, not on production HEX fidelity. X1/X2/X17 exercise the same handler but check markers/ownership, not exact numeric spelling. Other fixed hashes produced by `snprintf` or typed emitters are not based-`Print` assertions.
- **inbox-verbs' separate `transcript_main.cpp`/`transcript.py` comparator** also reuses this fake and records based fields (`whoami`, `dh`, `lp`, etc.). It already states the radix limitation; comparing two transcripts proves identity with that same shim, not board radix fidelity. The current actual `dh/lp` radix source witness is console-sink **S27** (the transcript's old S24 comment is a stale locator, not an executable pin).

D6 text-reader audit found **two** in-run fake hashes, not only the one in the question: inbox-verbs `md5_sources` (:277–289) and firmware-UI `md5_sources` (:192–197). Both compare before/after within one run, with no persisted expected fake hash. A comment edit before a run changes both sides equally. No mutation/structural predicate or test was found matching the two comment lines themselves. The isolated transcript build includes the fake as a compiler input. The BLE-line and custody-USB probes use the **different console-sink fake**, not this one.

For a strictly comment-only edit, demonstrate byte equality after removing only the approved comment delta (or preprocessed-token equality) and retain this reader census. No full firmware-UI/inbox-verbs rerun is required merely to regenerate an unpinned in-run MD5. Any executable fake change, moved matched text or additional consumer change invalidates that exemption and needs its own fence/gate. Board-UI will compile the shared fake in W2's required default run anyway.

## Q10. Recommended gates and author handoff

A tool-only gate should measure the edited instruments. It should not claim fresh firmware/corpus evidence from unchanged source. Recommend the **same focused chain for coder and independent QA**:

1. Commit-plus-SHA preflight; freeze all preparation inputs, inventory every changed/untracked path. Prove `src/`, `lib/`, `test/`, `variants/`, `platformio.ini`, simulator and all non-fenced build/test inputs unchanged at finish. Match simulator commit and clean source inventory. Scope audit plus whitespace checks.
2. Verify B451 is comments only and re-check all source readers against the final comment. AST-check M103 is inactive; all other battery entries, targets, worker logic and native PIN remain identical. Preserve its retirement history.
3. Stock **board-UI default run**, exit 0. Verify baseline counts, each repaired reader GREEN, each control genuinely applied and RED for its named property. Enumerate all declared versus executed checks/controls independently while B459 remains open. No skipped-control credit. Keep missing-trait compile-failure contracts separate from behavioral RED controls.
4. Stock **console-sink default run** if, as recommended, W51 delegates the seam-to-dispatch proof to S24/X17. This is the extra witness missing from the proposed gate list: verify the referenced controlled instrument, not only its comments. A separately specified focused replay of the stock S23/S24 controls is a narrower alternative; this pre-check demonstrates that replay without changing that instrument.
5. Stock **model battery**, all **238 RED**, zero unusable/vacuous; worker native baselines must agree at the unchanged PIN. No full union is needed for deleting one authorized tuple if every other entry is byte-identical.
6. **Full tools discovery** (`python3 -m unittest discover -s tools -p 'test_*.py'`) after runner edits, per D5. Require the suite actually runs with no skipped/import-failed tests. Keep command inventory in check mode: `python3 tools/gen_command_inventory.py --check`; the independently verified baseline is **197 rows**, byte-identical. Do not regenerate it.
7. Freeze report, final input preservation, simulator state, exact approved brief hash, all output counts/control dispositions and a checksummed evidence folder. Independent QA repeats the above from that freeze.

No separate native suite, corpus, board builds/measurements, ABI sweep or warning census is necessary for this strictly tool-only fence: product/build/test inputs cannot change, and the model instrument itself rebuilds/runs native in isolated workers. This is a proposed package gate, not a retroactive waiver for product edits. No metal check is added. If implementation needs a production marker, numeric fake change, broader reader repair or new test in `test/`, stop for a fence decision.

The three repairs fit one tool-only brief. B459 is the only newly requested owner scope choice; leaving it separate does not block writing the present W2 brief, provided the gate carries independent completeness reconciliation. The design's W2-before-W6 dependency remains correct: W6 changes the `/mrui` sites these readers protect.

## Evidence and limits

Measured here: clean bases/full input inventories; all dormant control scripts; full board-UI default baseline; two full synthetic omission runs; existing console-sink structural reader and its four exact seam controls; full scratch 238-entry retirement projection; command-inventory check; tools discovery (**375 tests, OK, zero skipped**, exit 0; 737.695 s). No W2 candidate was installed or gated. The scratch experiments did not preserve the three simulator-directory convenience symlinks under `spec/`; neither the board-UI runner nor the model battery reads those aliases. The final brief input inventory does preserve their identities. Native counts above come from the scratch battery workers, not an independently rebuilt shared native binary. No corpus, firmware board build, full firmware-UI/inbox/console-sink gate or metal run is claimed.

The first attempt to import the stock console structural module from the evidence driver lacked its sibling module search path and failed before any measurement (`ModuleNotFoundError: ble_guard`). The evidence driver was corrected to use the stock module directory; the complete reported replay then ran. No failed attempt was counted as a result. Raw stdout includes the tools suite's ResourceWarning if present; the suite exit/result is recorded separately.

Only the pre-check report/evidence and the requested B459 register row/next-free update are this task's repository changes. Nothing is staged or committed. The simulator is unchanged.
