<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0g — bare primary-command help · dispatch brief · 2026-09-05

**Status: AUTHOR DRAFT — awaiting Quality-Agent gate before dispatch.** Dispatch model after PASS: **Opus**.
Authority: the owner ruling dated 2026-09-05 in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`, B208 and B291/B293/B294/B295 in
`docs/2026-07-30-open-bug-register.md`, and §19/§19.1 of
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`. Review input:
`docs/superpowers/plans/2026-09-05-fable-review-pass1.md` F1/F3/F4/F5.

0g immediately follows 0a and precedes 0b/0c. It replaces the newly landed topic help with a much smaller,
build-true list of bare primary command names. Detailed syntax and explanations live only in
`docs/manual/command-reference.md`. The generated command inventory becomes the executable completeness and
feature-gate oracle, so adding or removing a command does not require reconstructing a historical help baseline.
The same slice makes the console-sink runner enforce its own pins and makes the inventory generator's advertised
`--check` mode real.

This is one bounded console-presentation change plus two instrument fixes. It does not add remote admin, change a
command handler, change a wire/frame, alter BLE help policy, change the console stage, or fix B292's separate BLE
notification hazard.

## Owner ruling quoted verbatim

> **Owner:** *"We need to fix help, and I'd do that quite dramatically - by removing any description, keeping bare
> command name - we will keep full help in manual."* … *"Agree - primary verbs only, start pass 2."*
> **Settled:** (1) `help` / `?` print the primary command NAMES only (the manual's 49 primary verbs, no flags, no
> descriptions, no sub-verbs), plus one pointer line to `docs/manual/command-reference.md`, which is the sole
> reference; (2) the generated command inventory is the oracle: every primary verb the build compiles appears
> EXACTLY once, under the same feature gate as its dispatch arm, and nothing else appears; (3) if the whole index fits
> `MR_CONSOLE_STAGE_BYTES` (expected: well under 1 KB — the coder MEASURES), `help <topic>` and the nine sections are
> RETIRED; (4) the BLE refusal of the whole help family (owner ruling 2026-09-04) stays; (5) this SUPERSEDES the
> content half of B208's 2026-08-17 ruling (topic sections) — B208's size/no-drop/no-pager/no-bypass half stands;
> (6) Bench Part 58 is re-issued for the new shape.

The Author's placement decision is also binding:

> **AUTHOR PLACEMENT DECISION 2026-09-05:** 0g runs immediately after 0a and before 0b/0c. This removes the
> historical-baseline trap and makes the inventory-backed primary-verb projection self-maintaining before either
> later dispatcher slice regenerates the inventory. It must in all cases land before Slice 6 adds `remote` to the
> verb set.

## Required result

0g passes only when all of the following hold:

- bare `help` and bare `?` render byte-identical output;
- that output contains one line per source-derived primary command name compiled into that product profile,
  sorted bytewise ascending, each line containing the bare name and nothing else;
- each primary command appears exactly once after complementary gates, aliases and duplicate dispatch arms are
  canonicalized;
- the sole non-command line in the successful response is the final exact manual pointer
  `docs/manual/command-reference.md`; no flag, syntax, argument, sub-verb, topic, description or command grouping
  remains in firmware help;
- `help <anything>` is a retired form: it emits one bounded usage refusal plus the same manual pointer and never
  renders a topic or falls through to another command;
- `helpful`, `HELP`, `?x` and `? <tail>` retain their current non-help ownership, while the BLE guard continues to
  refuse the complete lower-case `help` family and bare `?` before shared dispatch;
- the maximum successful/refusal response is measured through the real `GuardedConsole`, is strictly below
  `MR_CONSOLE_STAGE_BYTES` including line endings, and produces zero `CONSOLE_DROP`;
- the generated inventory, not a copied list or the manual, is the completeness and feature-gate oracle;
- `help_baseline.json` and `help_manifest.py` are deleted, every historical-content control is explicitly retired
  or re-aimed, and the one-time 0a no-loss proof remains preserved in its evidence document;
- the console-sink runner refuses a pin mismatch itself before printing PASS, while its auto-discovered wrapper
  remains an independent second reader of those pins;
- `python3 tools/gen_command_inventory.py --check` performs the same byte-for-byte verification as the existing
  bare invocation, and conflicting output modes refuse;
- every non-help command, handler, result, feature gate, BLE reply and dispatch order remains unchanged; and
- corpus/ABI/RAM remain unchanged, with all board flash movement attributable to deleting descriptive help and
  its obsolete instrument support.

The current manual's “49 primary command names plus `?`” is an input claim to reproduce, not a number to force.
The coder derives every profile's count from source. If the full-feature projection is not 49, the slice STOPs and
reports the exact source/manual discrepancy instead of tuning an allow-list until the number matches.

## The primary-verb projection

Extend `tools/gen_command_inventory.py` with one reviewed projection over its already classified rows. It must not
rescan C++ with a second parser. For a selected real product profile, the projection:

1. retains only command rows whose transport includes the serial dispatcher;
2. retains **Surface 1's top-level dispatch rows plus Surface 3's `parse_command` caller rows** that introduce a
   primary console verb; it excludes other caller rows, sub-verb and radio-only remote surfaces. Surface 3 is
   load-bearing here: its seven parser-owned primary names (`send`, `send_layer`, `send_channel`, the two peer
   verbs, `reqpubkey`, and `resolve`) are absent from Surface 1 and must not be replaced by a hand allow-list;
3. evaluates each row's recorded preprocessor gate against that real profile;
4. reduces a multiword form such as `cfg set` to its first primary token;
5. expands word aliases that are independently accepted spellings, while treating punctuation `?` as the alias of
   `help`, not a second primary command;
6. canonicalizes generator annotations such as `(alias: ...)` and `a|b` without maintaining a command allow-list;
7. de-duplicates repeated/complementary dispatch rows only after gate evaluation; and
8. returns the bytewise-sorted unique name list.

Publish this projection through a stable Python API used by tests and the console-sink gate. A CLI rendering is
allowed if useful, but a generated production header/build step is not: 0g does not change PlatformIO or make the
firmware build depend on Python. `src/firmware_help.h` remains the flash-resident presentation list and the
executed gate proves it equals the source-derived projection in both directions for every real profile.

**QA-gated start-state precision (the coder reproduces it rather than quoting it as its own measurement):** the
current tracked inventory projects to **42** names from Surface 1 alone and **49** after the qualifying Surface 3
`parse_command` rows are included. A 42-name result is a projection defect, not a source/manual discrepancy and
must be diagnosed before STOP 1 is raised.

The projection's own fixtures must cover at least: `cfg set` collapsing to `cfg`; the `help`/`?` punctuation
alias; a word alias; two complementary gated rows for one spelling; a duplicate arm; a serial+BLE row; a BLE-only
row; a radio-only row; a sub-verb; a disabled feature row; and an empty projection refusal. A primary command
added to a classified source surface must change the expected help list without editing the projection code.
The existing generator test that pins the help surface's two topic-macro gate names is re-aimed to the new
primary-name/gate contract; those retired macros must not be preserved merely to keep the old test green.

## Response shape

The successful response has no headings or prose. It is exactly:

```text
<primary-name-1>
<primary-name-2>
...
<primary-name-N>
docs/manual/command-reference.md
```

There is one name per terminated line. The names are sorted bytewise, making ordering source-independent and
reviewable. The manual path is last and is the only line excluded from the inventory equality check. `help` is a
primary name and appears once; `?` does not appear as a separate line.

The exact refusal prefix for argument-bearing `help` is the coder's bounded wording, frozen by a golden test, but
it must state that only bare `help`/`?` are accepted and end with the same manual path. It must not enumerate
topics, commands, flags or syntax beyond `help | ?`. The existing `help_command(line,len,Print&)` remains the one
router and continues to return false for non-help strings.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. the source-derived full-feature primary set is not exactly the manual's stated 49 names;
2. a primary/non-primary distinction requires a hand-maintained allow-list, manual scraping, or a second C++
   command parser rather than a principled projection of the generator's classified rows;
3. a command accepted by the serial dispatcher cannot be represented once under its real compile gate, or two
   source spellings cannot be canonicalized without changing command semantics;
4. the successful or refusal response reaches `MR_CONSOLE_STAGE_BYTES`, drops a line, needs paging, grows the
   stage, blocks/drains inside the renderer, or bypasses `GuardedConsole`;
5. any descriptive help text, topic identifier, flag, argument spelling or sub-verb must remain in firmware to
   make the list usable;
6. removing `help <topic>` requires changing the BLE guard, shared dispatcher call site, non-help ownership or a
   command handler;
7. a real profile's output differs from the generator projection, or the six-profile gate cannot represent all
   current board macro combinations;
8. retiring the frozen baseline deletes or rewrites the historical 0a evidence, or a baseline-dependent control
   disappears without a recorded replacement/retirement;
9. `--check` cannot be added as an exact alias of the default verification without changing `--write`/`--stdout`
   semantics or generated row content;
10. the console-sink runner can still print PASS after any observed pin differs from its documented expected pin;
11. BLE help policy/output changes, B292's 244-byte notification issue is touched, or another `device_ble.h` line
    changes;
12. a command, wire/frame, storage, protocol, feature policy, board configuration, companion implementation or
    simulator behavior changes;
13. corpus output, `sizeof(Node)`, board RAM, `.bss` or `.data` moves;
14. a moved flash symbol is unrelated to the help renderer/list or removal of the obsolete help-baseline support;
15. any control is GREEN, vacuous, multi-matched or unusable and cannot be repaired inside the authorized
    instrument; or
16. a gate compares two lists derived from the same help header instead of independently comparing production
    output with the command inventory.

## Fence

Allowed production edit:

- `src/firmware_help.h`: replace topic renderers/index/usage with the bare primary-name response and retired-form
  refusal; retain the one allocation-free `help_command` seam and its supplied `Print&` contract.

No change is expected in `src/firmware_commands.cpp` because its one `help_command(line,len,out)` call remains the
correct wiring. Structural checks still pin that call and its order. No change is expected in `src/fw_main.cpp`:
the 0a owner-widened BLE family guard remains exact.

Allowed supporting edits:

- `tools/gen_command_inventory.py` and `tools/test_gen_command_inventory.py` for the primary projection and real
  `--check` mode;
- `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md`, regenerated only with `--write`;
- `tools/probe_console_sink/run.sh`, `probe_main.cpp`, `structural.py`, `negctl.py`, `ble_guard.py` and
  `tools/test_probe_console_sink.py`, only as required to replace topic/content checks with the inventory oracle,
  retain the BLE composition gate, and enforce pins in-runner;
- deletion of `tools/probe_console_sink/help_baseline.json` and
  `tools/probe_console_sink/help_manifest.py`; and
- `docs/superpowers/evidence/2026-09-05-radmin-0g.md`.

`tools/probe_console_sink/run.sh.orig` remains out of scope under B287. `src/device_ble.h` and
`tools/probe_ble_line/` remain out of scope under B292. No other `src/`, no `lib/`, `variants/`, `platformio.ini`,
`simulation/`, `ios-companion/`, manual, protocol/frame document, register, bench script, spec, tracker or memory
belongs to the coder. The Author owns held documentation after QG PASS.

The only ruled board environments are `gateway` and `heltec_mobile`. The warning census's own pinned environment
set remains the standing exception.

## 0g-1 — measure and freeze the new semantic boundary

Before editing, regenerate the current inventory and derive the primary projection for every real probe profile.
Publish:

- the projection algorithm's source row count at each stage;
- the Surface-1-only count and the added Surface-3 `parse_command` caller rows/count, reproducing the QA-gated
  42 → 49 distinction from the source rather than treating either number as an allow-list;
- each profile's sorted primary names and count;
- the union/difference among profiles with the owning feature-gate row for every difference;
- reconciliation against the manual's 49-name claim; and
- the current topic-help output sizes as the BEFORE comparison only.

Do not copy the 49 names from the manual, current help or this brief. The source inventory is the sole input. The
manual is checked for agreement, not parsed as authority.

After editing, capture the actual real-renderer output for the same profiles. Strip only the exact final manual
pointer, then compare ordered names against the independently derived projection. Require equality in both
directions, unique output names, and no empty profile.

## 0g-2 — simplify the production help authority

Delete all nine topic functions, topic-name/index prose, RF-capability text dependencies and topic comparisons
from `src/firmware_help.h`. Retain only:

- compile-time gates needed by the primary-name list;
- one flash-resident print of each primary name under the same effective build condition as the source command;
- the final manual path;
- one short argument-help refusal; and
- the exact-length `help_command` router.

The firmware list is intentionally a presentation copy checked against the generator; it is not allowed to decide
completeness. Avoid runtime arrays, static RAM tables, heap allocation and generated build products. Preserve
`Print&` and `F()` storage. The successful output must be deterministic and byte-identical for `help` and `?`.

The router owns exact bare `help`, bare `?`, and `help` followed by a space so the retired topic forms fail loudly.
It does not own `helpful`, upper-case `HELP`, `?x` or `? <tail>`. The BLE guard remains wider in the matching
lower-case help-family dimension and continues to stop all owned help before shared dispatch.

## 0g-3 — retire and replace the historical baseline

Delete `help_baseline.json` and `help_manifest.py`. In the evidence, explicitly retire every baseline-dependent
test/control by old identifier and name its replacement:

- deleted inherited line → delete one primary name from firmware output;
- duplicated inherited line → duplicate one primary name;
- topic over stage → grow the bare index past the stage while leaving the source projection unchanged;
- unavailable topic listed/accepted → gated primary name present/absent on the wrong profile;
- topic-prefix/usage controls → an argument-bearing help form renders a successful index or falls through;
- stale/empty historical baseline → inventory missing a source command, firmware missing an inventory command,
  extra firmware name, and empty projection/output refusals; and
- the unmutated-baseline positive → every real profile's production output equals its source projection.

Keep or re-aim the supplied-Print, one-router-call, BLE-family, source-hash and stage/drop controls. No historical
0a evidence file is changed. Controls operate on isolated copies and prove the checkout unchanged.

At minimum, the final default gate must turn RED independently when:

1. a production primary name is deleted from help;
2. an extra/non-command name is added;
3. a name is duplicated;
4. two names are swapped;
5. a gated name appears on a disabled profile;
6. a gated name disappears on an enabled profile;
7. a new source primary command is added without changing help;
8. a source command is removed while help retains it;
9. the manual pointer is missing, moved from last, duplicated or changed;
10. a topic/description/sub-verb line returns;
11. `help <topic>` renders success rather than the retired-form refusal;
12. `help` and `?` diverge;
13. output exceeds the stage or produces `CONSOLE_DROP`;
14. the real `dispatch()` router call is removed, duplicated or bypassed;
15. the BLE guard fails to cover one router-owned line or swallows a non-help line; and
16. the runner's own observed profile/check/structural/BLE/control/unusable count differs from its pin.

## 0g-4 — console-sink self-pin enforcement (B294)

Move the derived expected pin values, with written arithmetic, into the default runner's own judgment path. Keep
the printed `PINS ...` line, but compare every field before PASS. Missing/non-numeric counts, extra/missing
profiles, zero counts and nonzero unusable controls are failures. The runner must print which term differed and
exit nonzero even if every probe binary it launched printed green.

The auto-discovered wrapper retains the same expected values as a second reader and pins that the runner performs
the judgment before PASS. Add an executed sabotage/selftest for each pin class; at least one must preserve a green
probe binary while shrinking the observed count, demonstrating the exact F4 hole. `--no-neg` stays
`PROBE-ONLY — NOT A GATE` and never prints PASS.

Do not make an environment variable or caller-supplied flag able to override production pins in a normal gate
run. Deliberate selftest injection must be confined to an explicit selftest path and must never share the PASS
wording.

## 0g-5 — truthful inventory CLI (B295)

Add `--check` as an explicit alias for the current default verification. Bare invocation and `--check` must read
the tracked output, compare it byte-for-byte with fresh generation, print equivalent PASS/refusal information and
return the same status. Use an argparse mutually exclusive group so `--check --write`, `--check --stdout`, and
`--write --stdout` refuse rather than applying precedence silently.

Update the generator comment and generated footer to name two commands that really work: `--write` to regenerate,
`--check` to verify. Regenerate the tracked inventory with the production generator. Controls must prove:

- bare and `--check` match on the real tree;
- both fail on a stale/missing tracked output;
- `--write` repairs an isolated stale copy and a following `--check` passes;
- every conflicting-mode pair refuses;
- unknown options still refuse; and
- the generated footer's advertised verify command executes successfully.

The expected inventory row delta from 0a is the removal of the nine `help <topic>` sub-verb rows only; derive and
publish the actual delta rather than quoting nine as a result. No non-help row, gate, transport or authority cell
may move. 0b and 0c each regenerate this inventory again from their own final tree.

## 0g-6 — full gate

Derive every result on the final tree.

1. Verify and record the base commit and all pre-existing modified/untracked files. Refuse a wrong worktree base;
   preserve the Quality Agent's review ledger and Author landings.
2. Run the native binary and report cases/assertions/failures plus `PIN re-synced? yes/no`. No native case is
   expected because the executed help seam lives under `src/`; any movement needs an attribution.
3. Run `tools/probe_console_sink/run.sh` in its default control-running mode, its explicit pin selftests, and
   `tools/test_probe_console_sink.py`. Run `--no-neg` only as the required non-gate control.
4. Run `python3 tools/gen_command_inventory.py --check`, bare verification, generator tests, projection fixtures
   and the complete tools sweep.
5. Re-run all standing `src/` wiring probes: firmware UI, inbox verbs, custody USB and BLE line. The BLE-line probe
   must remain at its existing pins and production header hash because B292 is out of scope.
6. Rebuild the simulator, create a fresh corpus with
   `tools/run_corpus.py --out <fresh-dir> --jobs 8 --require-anchors`, and require 36/36 current anchors with the
   keystone read fresh from `simulation/BASELINE.md`. Because 0g is `src/`/tools-only, the simulator binary and
   every stream must be byte-identical; no re-anchor is permitted.
7. Run the complete host/ARM/Xtensa ABI sweep and require `sizeof(Node)` unchanged.
8. Run `tools/measure_board.py pair --output <fresh-dir> --jobs 2`. Require RAM, `.bss`, `.data` and object count
   unchanged on `gateway` and `heltec_mobile`; attribute every flash/section/symbol delta to the simplified help.
   Apply B262's same-path/cross-tree payload-hash rule honestly.
9. Run `tools/warning_census.sh` over its pinned set, both standing source checkers and `git diff --check`.
10. Run every final 0g negative control with exact match count one; report RED/unusable/vacuous counts separately.
11. Record the complete modified/deleted/untracked inventory so the new evidence/brief and deletions cannot be
    lost by `git commit -a`.

The durable report is `docs/superpowers/evidence/2026-09-05-radmin-0g.md`. It must contain: start state; scope
diff; owner-ruling application; source-row-to-primary projection; per-profile name lists/counts/differences;
manual-49 reconciliation; actual rendered outputs and byte sizes/headroom; retired-control mapping; console-runner
pin derivation and sabotage results; inventory CLI/delta evidence; native/PIN, probes, corpus, ABI, boards,
warnings/checkers; mutation ledger; STOP audit; B208/B291/B293/B294/B295 dispositions; held documentation; metal
residue; and explicit-add inventory. Nothing load-bearing remains only in scratch.

## Author landings held for QG PASS

The coder drafts complete text in the evidence file; the Author lands it after PASS:

- `docs/manual/command-reference.md`: update the help row to say bare `help`/`?` list build-available primary names
  only; argument-bearing/topic help is retired; this manual is the only detailed command reference; BLE still
  refuses the help family;
- `docs/2026-07-31-bench-test-script.md`: replace/re-issue **Part 58** for the new owner-ruling shape:
  1. on a full-feature serial build, run `help`; require only bare primary names plus the final manual path and no
     `CONSOLE_DROP`;
  2. run `?`; require byte-identical output;
  3. compare the observed full-feature names with the report's source-derived list;
  4. run `help messaging`; require the retired-form refusal plus manual path and no topic content;
  5. on `gateway`, require `mobile` absent while always-compiled names remain, with no drop; and
  6. over BLE, require bare `help`, bare `?` and `help messaging` to retain the bounded `console_only` refusal;
- `docs/2026-07-30-open-bug-register.md`: B208 to SOFTWARE-COMPLETE / METAL-PENDING at the re-issued Part 58;
  B291 and B293 to mechanically landed; B294 and B295 closed with measured evidence; B292 remains open and
  separate;
- `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: mark 0g software-complete and record
  the measured primary-set/size result without rewriting the kept-visible 0a history; and
- tracker/pipeline memory: 0g closed; B296/B297's owner-ruled 0h hash-preservation slice runs next; B292 remains
  separately queued; then 0b and 0c regenerate the inventory in sequence before Slice 1.

No `docs/frames.md`, `docs/protocol.md` or companion contract change is owed: 0g changes no wire/protocol or
companion-visible transport. No separate metal work beyond the re-issued Part 58 is owed.
