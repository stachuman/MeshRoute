<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0a — bounded help-topic split · dispatch brief · 2026-09-04

**Status: QUALITY-AGENT PASS 2026-09-04 (two required fold-ins applied in place) — AUTHORIZED FOR DISPATCH.**
Dispatch model: **Opus**.
Authority: B208's owner ruling in `docs/2026-07-30-open-bug-register.md`, §19 Slice 0a and §19.1 of
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`. Quality-Agent pre-check input:
`docs/superpowers/plans/2026-09-04-radmin-0a-precheck.md`.

0a replaces the oversized serial `help` dump with a compact build-true topic index and one bounded
`help <topic>` response. It is a console-UX fix only: no command gains or loses behaviour, no parser other than
the help arm changes, BLE continues to refuse help as `console_only`, and the guarded console's 2048-byte stage
does not grow or gain a bypass/pager. The slice also regenerates the command inventory from source, as every
0a/0b/0c slice must do before Slice 1 opens the feature phase.

## Contract quoted verbatim

B208's owner ruling is the implementation authority:

> **Owner ruling 2026-08-17:** bare `help` and `?` print only a compact index of the help topics available in that
> build; `help <topic>` prints exactly one complete section. Canonical topics follow the existing headings:
> `messaging`, `identity`, `mobile`, `inbox`, `diagnostics`, `remote`, `test`, `provisioning`, and `cfg`;
> feature-gated topics are listed/accepted only when compiled. An unknown topic must print usage plus the valid
> topic names. Each individual response must fit the existing `MR_CONSOLE_STAGE_BYTES` and produce no
> `CONSOLE_DROP`. **Do not** enlarge the 2-KB stage, add a blocking/asynchronous pager, or bypass
> `GuardedConsole`. Pin: compact-index completeness, every topic's complete output and size bound,
> conditional-topic behaviour, unknown-topic refusal, `?` parity, and structural coverage proving no existing
> help line/command disappears during the split.

The reviewed slice assignment is:

> - **0a:** complete B208's bounded help-topic split;

The current §19.1 gate row is also binding:

> | 0a | help dispatch, `src/firmware_commands.cpp` | semantic identity; ruled pair unconditionally | none |

⚠ **AUTHOR CLARIFICATION FOR THE QUALITY-AGENT GATE:** the pre-check proves that this is a USB-only observable
and requires Bench Part 58 under M2. Therefore the row's historical `none` metal cell is not sufficient closure
evidence. 0a drafts Part 58 and B208 remains software-complete/metal-pending until it passes. The Author will
correct the §19.1 cell with the old text visible after QG PASS; the coder does not edit the design.

## Required result

0a passes only when all of the following hold:

- bare `help` and bare `?` emit the same compact index, byte for byte;
- the index contains the nine owner-named topics in owner order, except that a feature-gated topic is absent when
  that build cannot execute any command assigned to it;
- `help <topic>` accepts one exact lower-case canonical topic name and emits exactly that topic's complete section;
- an unavailable, unknown, empty or malformed topic tail emits one bounded usage response plus the valid topic
  names for that build, and never falls through to another command;
- each index, topic and refusal response is strictly smaller than `MR_CONSOLE_STAGE_BYTES` after the actual
  `println` line endings are counted, and a real `GuardedConsole` run reports zero dropped lines;
- every pre-slice non-heading help line appears exactly once across the applicable topic sections, under the same
  feature condition as the command it describes; no line is silently lost, duplicated, made unconditional or
  moved behind a narrower gate;
- every pre-slice display heading or separator has an explicit disposition: replaced by one canonical topic
  heading, retained once as a subordinate label/separator, or intentionally removed as obsolete layout only;
- the BLE `help`/`?` refusal remains byte-identical and still occurs before the shared text-dispatch fallback;
- every non-help command dispatch and output path is unchanged;
- the generated command inventory is regenerated from the final source and accounts for every command and gate;
  no inventory row is hand-edited or disappears as an incidental result of moving help text; and
- corpus semantics are byte-identical, `Node` ABI and board RAM are unchanged, and all flash movement is
  attributable to the help router/index/text layout.

The exact prose used by the compact index is the coder's choice. The nine topic identifiers and their order are
not. `? <topic>` is not introduced: B208 grants parity to the two **bare index** forms and grants topic selection
only to `help <topic>`.

## Topic ownership and build truth

Before editing, derive a source inventory of every current help `println`, its enclosing preprocessor condition,
and whether it is content, a heading or a separator. Then apply this ownership table. A line may have one owner
only.

| Canonical topic | Content owner | Availability rule |
|---|---|---|
| `messaging` | `send`, `send_channel`, `send_layer` and their continuation lines | always |
| `identity` | `whoami`, lookup/hash/name/resolve, peers, peer key/name and pubkey discovery | always |
| `mobile` | only the `mobile ...` command family | only when `MR_FEAT_MOBILE` compiles that family |
| `inbox` | pull, read-cursor, delete and clear commands | always |
| `diagnostics` | status/routes/duty/limits/cfg summary, sleep/debug/version/faults/crashtest, `rcmd`, prep-restart | always; `rcmd` stays here because its current help/handler is not the gated password family |
| `remote` | the existing password/unlock/lock management lines | only when `MR_FEAT_REMOTE_MGMT` |
| `test` | route injection, scheduled tests and factory reset | always |
| `provisioning` | team lifecycle/key-grant lines plus create/join/leave/gateway provisioning, each under its existing command/build condition | listed whenever at least one assigned command exists; current gateway help keeps the topic non-empty |
| `cfg` | the cfg-key catalog and OLED UI-preset help | always; UI-preset lines remain conditional on `MR_FEAT_OLED` |

This table classifies content; it does not authorize moving handlers or changing feature macros. In particular,
do not retain the current combined `MOBILE / TEAM` block as one gate: mobile lines follow `MR_FEAT_MOBILE`, while
team/provisioning lines keep their own existing availability. Derive the real profile matrix from production
macros and prove at least a full-feature/OLED profile, the `gateway` profile, and `heltec_mobile`'s profile. Do
not invent test-only product combinations merely to make a topic disappear.

⚠ **EXPECTED GATE ALIGNMENT, not STOP 4:** the current monolithic help compiles the mobile lines under
`MR_N_LAYERS < 2`, while the actual handler is compiled under `MR_FEAT_MOBILE`. Those expressions coincide in
every current product profile because only the gateway profile disables the mobile feature. 0a aligns the help
topic with the handler authority (`MR_FEAT_MOBILE`) and records the old/new condition in the line-disposition
manifest. That source mismatch is the reason for the alignment; it is not a contradiction requiring a STOP so
long as the real-profile matrix proves no command availability changes. If the expressions cease to coincide in
any real profile, STOP 4 applies.

The pre-check describes five historical top-level headings and four carved-out topics, while the current source
also contains gated/subordinate labels such as `UI PRESETS`, `REMOTE MANAGEMENT`, `PROVISIONING`, and `CFG KEYS`.
The implementation must not settle that counting difference by ignoring literals. The source-derived manifest
must enumerate every label and record its explicit disposition; the content-line multiset is the primary
no-loss authority.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. any one topic or the index cannot fit the existing 2048-byte stage including line endings;
2. meeting the limit requires increasing `MR_CONSOLE_STAGE_BYTES`, splitting one topic into pages/subtopics,
   blocking/draining inside the renderer, bypassing `Print&`/`GuardedConsole`, or removing help content;
3. the nine owner topic names are insufficient to give every current help line exactly one honest owner;
4. a command's real compile gate contradicts the topic table and resolving it would change command availability;
5. an unavailable topic must be accepted, or an available topic must be omitted, to preserve current behaviour;
6. BLE reaches the serial help renderer, changes its existing `console_only` response, or accepts
   `help <topic>`;
7. an executable help gate requires host-compiling all of `firmware_commands.cpp`, copying help strings into a
   test, or creating a second renderer/authority rather than extracting one narrow shared help unit;
8. the generated command-inventory tool cannot follow the final help-dispatch source shape without a principled,
   sabotage-tested parser extension, or regeneration changes any unrelated command/gate row;
9. a non-help dispatch arm, command result, wire/frame behaviour, feature policy, companion implementation,
   console stage, or board configuration must change;
10. corpus output moves, `sizeof(Node)` moves, board RAM moves, or a new runtime buffer/state/heap allocation is
    introduced;
11. either ruled board fails, or flash movement contains a symbol unrelated to the help extraction/router;
12. a negative control is GREEN, vacuous, multi-matched or unusable and cannot be repaired within the authorized
    probe/tool surface; or
13. a committed instrument reports PASS without executing the real help renderer through the real
    `GuardedConsole`, enforcing its derived check/control counts, and proving the checkout unchanged.

## Fence and implementation shape

Allowed production edits:

- `src/firmware_commands.cpp`: replace `dump_help` and its current help arm with exactly one call to the shared
  help-command router at the same dispatch position; and
- `src/firmware_help.h` (new): one narrow, allocation-free, header-only help authority containing the index,
  exact help-command recognition, topic availability decisions and section renderers used by both firmware and
  the executed probe.

The new header is the selected wiring seam because no native target compiles `src/firmware_commands.cpp`. It may
depend only on `Print`, compile-time product macros and constants already required by the help text. It must not
name `g_node`, NV, radio state, `Serial`, `mrcon`, dynamic storage or command handlers. All output goes through a
supplied `Print&`. Help text and the `help`/`?`/topic routing decision exist in this header exactly once; neither
the probe nor `firmware_commands.cpp` carries a shadow copy. `firmware_commands.cpp` retains only the call site,
which the structural checker pins to exactly one occurrence in the real `dispatch()` before the other verbs.

Allowed supporting edits:

- the existing `tools/probe_console_sink/` files needed to compile and execute the real help header through the
  real `src/console_sink.h`, correct the now-stale `dump_help` structural pins, and add isolated controls;
- `tools/test_probe_console_sink.py` (new), because no auto-discovered wrapper owns this probe today; it pins the
  default-gate counts, control execution and anti-vacuity behaviour;
- `tools/gen_command_inventory.py` and `tools/test_gen_command_inventory.py` only if the source move requires a
  principled parser update, with its own controls;
- the generated `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md`, produced only by
  `python3 tools/gen_command_inventory.py --write`; and
- `docs/superpowers/evidence/2026-09-04-radmin-0a.md`.

Do not create a second help probe when `probe_console_sink` is the canonical executed owner. Do not remove the
tracked `tools/probe_console_sink/run.sh.orig` backup in this slice; its hygiene row is separate. No coder edit
belongs in `src/fw_main.cpp`, `src/console_sink.h`, another `src/` file, `lib/`, `variants/`, `platformio.ini`,
`simulation/`, `ios-companion/`, `docs/frames.md`, `docs/protocol.md`, the manual, register, tracker, bench script,
design/spec status or memory. The Author owns the held documentation after QG PASS.

The only board environments are `gateway` and `heltec_mobile`. The warning census's own pinned environment set is
the standing exception to the two-environment rule.

## 0a-1 — freeze and classify the old help surface

Before production edits, write the evidence file's BEFORE table from the checked-out source. It must include:

- every help emission in source order, normalized rendered text, CRLF-inclusive byte count and occurrence count;
- its exact preprocessor condition and the command(s) it documents;
- content/heading/separator classification;
- its destination topic or explicit layout-only disposition; and
- totals for all lines and rendered bytes, derived rather than copied from B208 or the pre-check.

Turn that table into an executable completeness check. The final union of full-feature topic content must equal
the BEFORE content multiset exactly. Run equivalent comparisons for the real reduced profiles so a gated line
cannot survive in the index/section merely because the full-feature union is correct. Duplicate text must retain
multiplicity—set comparison is insufficient. A source move, spelling change or deletion without a recorded
disposition is a failure.

The checker must not mistake the new compact index, usage/refusal text or canonical topic headings for inherited
command help. Those new layout lines are a separately enumerated allowed addition.

## 0a-2 — one bounded router

Implement one help command-router entry point, taking `(line, len, Print&)` and returning whether it owned the
line, with enough context to distinguish:

- no topic: render compact index;
- exact available topic: render exactly one section; and
- unavailable/unknown/malformed topic: render bounded usage plus this build's valid topic list.

The router must return false for non-help input and match complete help tokens, not prefixes: `help messagingx`
is unknown, `help messaging x` does not silently select `messaging`, and `helpful` remains a non-help command.
Keep bare `?` on the no-topic arm. Preserve the shared dispatcher's existing ownership boundary: every owned help
form returns handled, and all output reaches the supplied sink. The real `dispatch()` calls this router once;
there is no second parser in the caller.

Each topic renderer should start with its canonical display heading and contain its inherited content in stable
order. Use compile-time gates for availability and gated lines; do not build a runtime topic table or store string
pointers in RAM merely to avoid a small explicit dispatch. The router may be an exhaustive if-chain over the nine
owner names, matching this file's existing parser idiom.

## 0a-3 — executed help/console gate

Extend `tools/probe_console_sink/` so its default gate compiles the real `src/firmware_help.h` and
`src/console_sink.h`, calls the real help command router with complete command lines, and renders through a real
`GuardedConsole` into the existing fake transport. The structural half pins the one real
`firmware_commands.cpp::dispatch()` call to that same router. The run must cover each real product macro profile
selected above. For every profile it must assert:

1. bare `help` and `?` are byte-identical;
2. the index has every and only available topic, exactly once and in owner order;
3. every available `help <topic>` is accepted and emits one complete section;
4. every unavailable owner topic is absent from the index and takes the refusal arm;
5. unknown, empty-tail and malformed forms emit usage plus exactly the valid names;
6. each response's rendered/staged byte count is below `MR_CONSOLE_STAGE_BYTES` after line endings;
7. the largest response is named from measurement, not assumed to be `messaging`;
8. `GuardedConsole` drains the complete response with zero `CONSOLE_DROP` and no fused/unterminated line;
9. the content multiset equals the frozen source baseline for that profile; and
10. the renderer and sink header hashes printed by the binary match the files under test.

The runner keeps its existing default-controls rule. It prints derived check/control pins and PASS only after the
real probe, structural checks and all controls succeed. `--no-neg` remains visibly probe-only and never prints
PASS. Isolated controls prove the real checkout unchanged.

Add or re-aim independent controls for at least:

- one inherited help line deleted;
- one inherited line duplicated or placed in two topics;
- one topic made larger than the stage;
- an unavailable topic listed in a reduced build;
- an unavailable topic accepted despite not being listed;
- an available topic omitted;
- bare `?` differing from bare `help`;
- an unknown/prefix topic selecting a real section;
- refusal omitting usage or one valid topic;
- removal, duplication or bypass of the real `firmware_commands.cpp::dispatch()` router call;
- direct `Serial`/`mrcon` output or bypass of the supplied `Print&`;
- compiling a copied help header or stale baseline rather than the checked-out source; and
- a runner/check-count reduction that would otherwise report success.

Every mutation anchor must match exactly once. A build failure counts as RED only when it is the expected direct
consequence of the mutated production decision; missing files, wrong include order, a broken probe or an unrun
case is unusable/fail-loud, never a successful control.

## 0a-4 — generated command inventory

Run the generator on the clean BEFORE tree and again on the final tree. Publish both inventories' row counts and a
semantic diff. The final tracked output comes only from:

```sh
python3 tools/gen_command_inventory.py --write
```

The expected command set and non-help gates are identical. The only permitted representation delta is whatever
the generator's schema honestly needs to describe the new `help <topic>` form; the existing `help (alias: ?)`
primary row remains. If moving the dispatch into the narrow header makes it invisible, update the generator to
scan the authoritative source shape and add both-direction tests: a real help arm is found, while a comment,
string example, helper definition or similarly shaped non-dispatch comparison is not. Do not special-case the
generated output or hand-maintain a second nine-topic list in the generator.

After regeneration, prove every inventory row still maps to a live source decision and record the new total. This
is the required 0a inventory generation; 0b and 0c will regenerate again from their own final trees.

## 0a-5 — full gate

Derive every number on the final tree; do not quote the figures below as results.

1. Confirm the base commit and record the complete pre-existing diff before work. Refuse a stale/wrong base.
2. Run the native binary and report cases/assertions/failures plus `PIN re-synced? yes/no`. With probe-owned 0a
   coverage and no native production surface, the current pin is predicted unchanged; any change is explained.
3. Run the complete console-sink probe with controls, its auto-discovered Python test and the full tools sweep.
4. Run the command-inventory generator check and its selftests/negative controls.
5. Re-run every standing wiring probe because 0a touches `src/`: firmware UI, inbox verbs, custody USB and BLE
   line. Record their derived pins.
6. Rebuild the simulator, run `tools/run_corpus.py --out <fresh-after-dir> --jobs 8 --require-anchors`, and use
   `tools/run_corpus.py --compare <validated-before-dir> <fresh-after-dir>` for the ordered BEFORE/AFTER check.
   Require 36/36 byte-identical output and a fresh keystone read from `BASELINE.md`; no re-anchor proposal is
   permitted. If no validated BEFORE directory from this exact base survives, generate it before editing or from
   a clean base worktree—never compare two AFTER runs and call that semantic identity.
7. Run the full ABI sweep and require `sizeof(Node)` unchanged on host, ARM and Xtensa.
8. Run `tools/measure_board.py pair --output <fresh-dir> --jobs 2` for exactly `gateway` and `heltec_mobile`.
   Require RAM, `.bss` and `.data` unchanged. Attribute every flash/text/symbol movement to the help split; B262
   means cross-checkout ESP32 payload hashes are not used as the sole comparison.
9. Run `tools/warning_census.sh` over its own pinned environment set, plus both standing source checkers and
   `git diff --check`.
10. Run the structural coverage/completeness checker against the final source and every selected profile, with
    all controls RED and zero unusable/vacuous entries.
11. State the full changed/untracked file inventory so the new header/evidence/instrument updates cannot be lost
    by `git commit -a`.

The report must contain: starting HEAD; scope/fence diff; BEFORE and AFTER help manifests; topic/profile matrix;
index and per-topic rendered byte counts; largest response; stage headroom; complete probe/check/control pins;
inventory semantic diff; native and PIN line; corpus/relink evidence; board/ABI/census/probe results; mutation
ledger; STOP audit; B208 closure recommendation; metal residue; held documentation; and explicit-add inventory.
The durable report is `docs/superpowers/evidence/2026-09-04-radmin-0a.md`; nothing load-bearing stays only in
scratch.

## Author landings held for QG PASS

The coder drafts complete replacement text in the evidence file but does not land these owner documents:

- `docs/manual/command-reference.md`: replace the current bare-help summary with the compact-index,
  `help <topic>`, build-conditional and unknown-topic contract; keep BLE's USB-only statement true;
- `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`: mark Slice 0a software-complete and
  correct §19.1's historical `none` metal cell to Part 58 with the old claim visible;
- `docs/2026-07-30-open-bug-register.md`: B208 to SOFTWARE-COMPLETE / METAL-PENDING on software PASS, with the
  reserved close sentence for Part 58;
- `docs/2026-07-31-bench-test-script.md`: **Part 58**, using a serial console on a full-feature node and a
  gateway/headless build:
  1. run bare `help`; require the compact valid-topic index and no `CONSOLE_DROP`;
  2. run `?`; require byte-for-byte the same index;
  3. run the measured-largest topic (expected hypothesis: `help messaging`); require its final line and no drop;
  4. run `help zzz`; require usage plus exactly the valid topic names;
  5. on `gateway`, require `mobile` absent from the index and `help mobile` to take the same refusal arm; and
- pipeline memory/tracker: record 0a's closure state and that 0b then 0c each regenerate the command inventory
  before Slice 1 begins.

No `docs/frames.md`, `docs/protocol.md` or companion-contract edit is owed: 0a changes no frame, protocol or
companion-visible transport. B208 closes only after Part 58 passes on metal; until then the exact status is
software-complete/metal-pending.
