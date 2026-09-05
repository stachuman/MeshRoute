<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0c — transport-neutral local execution seam · dispatch brief · 2026-09-05

**Status: QUALITY-AGENT PASS 2026-09-05 — FOLD-INS LANDED; AUTHORIZED FOR DISPATCH ONCE THE EXACT BASE IS
PINNED.** Dispatch model: **Opus**.

Authority: §12 and §19 Slice 0c of
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`, the Quality-Agent pre-check
`docs/superpowers/plans/2026-09-04-radmin-0c-precheck.md`, and B298 in
`docs/2026-07-30-open-bug-register.md`.

**Base commit is deliberately not pinned yet.** This brief was written against the post-0b production shape at
`1e84be6` plus the Author's docs-only 0b landings. The dispatch base will be the owner's next clean commit
containing both. Before dispatch, the Author replaces this paragraph with that exact hash; the Quality Agent and
every isolated-worktree coder verify `git rev-parse HEAD` against it. A placeholder, a dirty tree, or a different
commit is a STOP, not licence to repin or repair the worktree silently.

0c is a C1 refactor only. The serial and BLE callers currently make the same parser/router decision in opposite
orders and render the parser-owned result in different envelopes. The slice creates one execution seam that owns
that decision once and lets each transport retain its exact existing bytes. It adds no command context, authority,
remote request, `DispatchResult`, feature policy, verb, handler, storage, wire or radio behavior.

## Authority quoted verbatim

From design §19:

> **0c:** make the existing dispatcher/caller output path transport-neutral without adding remote context
> or policy;

From design §12:

> Current local command execution is not yet perfectly unified: `send` and `send_channel` are handled by
> the serial/BLE callers around `dispatch()`. ⚠ **CORRECTED 2026-09-05 BY SLICE 0b; THE OLD CLAIM REMAINS VISIBLE:**
> this sentence formerly added that “`regen` writes through the global console sink.” That was B279 and is now
> false: `regen` and the canonical identity formatter use the supplied `Print&`, while the boot caller names
> `mrcon` explicitly. The implementation may not claim same-command parity until the remaining applicable paths
> use one transport-neutral execution seam and the supplied output sink. Per C1, that consolidation and remote
> enablement must remain separately reviewable.

From B298:

> **CLOSE BY 0c:** while that already-authorized slice touches this TU, use the correction idiom to say the header
> owns the bare primary-name index plus bounded retired-form refusal, keep the one-router/no-bypass rationale, and
> prove the comment-only delta under B254 rather than touching parsing.

## Required result

0c passes only when all of the following hold:

1. One production seam in `firmware_commands.cpp` owns the shared local line decision and both transport-format
   arms: offer the line to the console verb router and, when it is unmatched, parse and execute the
   `meshroute::Command`. Neither serial nor BLE open-codes that decision after the refactor.
2. The seam receives the caller-selected output capability. It never selects `mrcon`, `Serial`, BLE NUS, or a
   global fallback for the caller. Transport adapters may select their own already-existing sinks/buffers before
   entering it.
3. Serial keeps its current text contract byte-for-byte: router output, `peerkey`/`peername` JSON lines, the
   `CmdCode` line and its optional `dh`/`lp`/`plane` fields, the `reqpubkey` hint, parse error, empty-line silence,
   and the line-overflow refusal.
4. BLE keeps its current JSON/text contract byte-for-byte: its existing direct handlers and bounded/streamed
   distinctions remain outside or around the common seam as today; parser-owned commands retain
   `write_ack`/`write_reqpubkey_sent` and named parse errors; router-owned fallback commands retain the real
   `LineSink` and flush exactly once; the complete help family remains `console_only` before the fallback.
5. Every command side effect, `CmdResult`, accepted/refused decision, queue/counter change and NV mutation is
   identical per transport to the post-0b base. Output equality alone is not enough.
6. The parser/router ordering is justified by an exhaustive source-derived intersection. The Quality-Agent gate
   measured **41 router verbs and 7 parser verbs with an empty intersection**; those are hypotheses for the coder
   to re-derive, not figures to quote as its own. An unexpected non-empty result first triggers a projection audit
   against the exact post-0b source so a tool defect cannot manufacture an owner question. If a genuine line is
   accepted by both on any current profile, 0c STOPs for an owner ruling on the winner; it does not choose a new
   behavior by code order. The derived empty intersection is pinned so a future collision turns RED.
7. `Command::body` continues to borrow from the input line only while it is live: parsing and `Node::on_command`
   remain in one synchronous call chain. No returned object retains the pointer.
8. The existing UI-only `exec_command()` typed seam remains behavior-identical. Reusing an internal primitive is
   allowed only if its public `ExecResult` contract, two call sites and all UI probe results stay exact.
9. No second verb map, parser, formatter or result enum is introduced. The future `CommandContext` and
   `DispatchResult` belong to Slice 6, not 0c.
10. B298 closes in the touched `firmware_commands.cpp`: its complete active `topic`-help comment census is
    corrected with the old claims visibly withdrawn. The replacement describes the bare primary-name index,
    bounded retired-form refusal, one router and no bypass. The `firmware_commands.h` paragraph that says the two
    callers are deliberately not retrofitted because their orderings differ joins the same V1 correction census:
    after 0c they are retrofitted through this seam. Both corrections are prose-only and change no predicate.
11. The generated command inventory is regenerated only for source-anchor relocation. After normalising source
    file/line fields, its classified rows are identical in both directions.
12. Native/corpus/ABI/RAM remain unchanged; board flash movement is small and every moved symbol belongs to the
    authorized execution seam or its two caller adapters.

The name and exact type layout of the seam are implementation choices. A small transport-format enum, callbacks,
or a bounded adapter over the existing buffers can all be honest. It may not allocate a new persistent/static
buffer merely to make the signatures convenient, and it may not route BLE's streamed fallback through the
256-byte direct-reply buffer or route a direct reply through a different notification path.

## 0c-0 — freeze the two live contracts before editing

Before production changes, the coder derives and publishes:

- the exact current `service_console` and `ble_dispatch_line` decision graphs, including every handler that runs
  before their shared parser/router fork;
- a source-derived set of router-owned primary forms and parser-owned primary forms for every real feature
  profile, using the tracked command inventory rather than a hand list;
- the intersection, with an executed positive control that adding one synthetic collision makes the checker
  reject it;
- the complete call-site census for `dispatch`, `parse_command`, `g_node.on_command`, `handle_peerkey`,
  `handle_peername`, `write_ack`, `write_reqpubkey_sent` and `print_reqpubkey_hint` across the two callers; and
- BEFORE transcripts and side-effect fingerprints for the line matrix below, separately through the serial and
  BLE shapes.

The independently observed starting hypothesis is 41 router names, 7 parser names and zero intersection. The
coder recomputes all three counts. If they differ, diagnose inventory projection and post-0b source drift first;
only a source-verified real collision reaches the owner-ruling STOP.

The line matrix is generated from every classified primary verb and qualifying sub-verb, then adds explicit
boundaries: the three send verbs, a successful and malformed parser-owned command, a successful and malformed
router-owned command, `peerkey`, `peername`, `reqpubkey`, empty input, unknown input, `help`, `help messaging`, `?`,
`helpful`, and each transport's too-long line. Destructive or stateful verbs run against resettable fakes and
record the resulting state; they are never omitted merely because output comparison is awkward.

The BEFORE capture is a one-slice comparison artifact, not a new historical default gate. Keep its hashes and the
meaningful golden lines in the durable evidence, but do not recreate 0a's frozen-source trap. Permanent checks
must derive the command set from the current inventory and pin current semantic invariants.

## 0c-1 — introduce one bounded execution seam

Place the declaration with the existing firmware command authority in `firmware_commands.h` and the complete
implementation—including both transport-format arms—in `firmware_commands.cpp`. This placement is
load-bearing: `tools/probe_inbox_verbs` already compiles the real `firmware_commands.cpp` dispatcher with the
production device context, so it can execute the seam through a real `GuardedConsole` and a real `LineSink`.
The seam owns exactly:

1. the one router-versus-parser decision in the measured order;
2. parser success dispatch to `handle_peerkey`, `handle_peername` or `g_node.on_command`;
3. the transport-selected rendering of the typed parser result or parse failure; and
4. a typed internal completion sufficient for each caller to know whether bytes were buffered, streamed,
   completed or unmatched without scraping output text.

That internal completion is not the future public `DispatchResult`: keep it local/narrow and do not add remote
authority states. It must not change the public Boolean meaning of `dispatch()` unless every current caller and
probe is migrated in this same refactor with byte-exact proof.

Keep the current BLE direct handlers (`whoami`, `version`, `prep-restart`, `rcmd`, `duty`, `limits`, `status`,
`cfg`, `routes`, bounded peers and inbox verbs) and their buffer/stream choices behavior-identical. Moving a
direct handler into the shared seam is permitted only when the BEFORE/AFTER transcript and side effects prove
that the move is mechanically neutral; it is not required to satisfy 0c. The load-bearing consolidation is the
duplicated router/parser/Node fork presently at the bottom of BLE and inside serial.

No heap allocation, virtual dispatch, second command copy, retained body pointer, new global, or new static RAM
buffer. Existing stack scratch may be rearranged only with measured stack usage and no dynamic frame.

## 0c-2 — route both transports through it

`service_console` and `ble_dispatch_line` each become a one-line adapter to the common seam on the applicable
path. No host probe compiles `fw_main.cpp`; do not claim otherwise. The two adapters are therefore pinned
structurally as exactly one seam call each, with no residual `parse_command`, `dispatch` or `g_node.on_command`
call in either applicable block. The executable proof lives at the seam, not in a lookalike caller.

The serial adapter retains its 1024-byte intake and `GuardedConsole`; the BLE adapter retains its 275-byte intake,
256-byte direct result buffer, 1700-byte stream scratch and `LineSink` flushing rules. The seam must not know how
bytes arrived (USB reads versus ATT chunks), and transport adapters must not repeat command ownership.

Pin at minimum:

- a router-owned text command on each transport, with zero cross-sink bytes;
- a parser-owned queued command and named refusal on each transport;
- `peerkey` and `peername` success/failure envelopes;
- accepted `reqpubkey` using `write_reqpubkey_sent` on BLE and the existing text result/hint on serial;
- unknown, empty and malformed input;
- BLE help-family refusal before the shared fallback;
- a streamed router response larger than one BLE notification, proving the path did not collapse into the
  direct buffer;
- a direct BLE response proving it was not silently converted into the streaming path; and
- byte-identical USB and BLE results before/after for the complete 0c-0 matrix.

Every case also asserts the command ran at most once. A helper used by both callers while either caller retains a
second parse or second `Node::on_command` path is a failed refactor.

## 0c-3 — close B298 without hiding history

Correct all active topic-help claims in `src/firmware_commands.cpp`, including the include annotation, the
§B95/§B208 policy block and the `dispatch()` call-site annotation. Also correct the active
`src/firmware_commands.h` claim that the two callers are deliberately not retrofitted because their orderings
differ. Keep both retired designs visible under the correction idiom, then state the current authorities: one
bare inventory-pinned primary-name index, one bounded retired-form refusal, one supplied-sink router, no
direct-serial bypass or pager; and one transport-neutral seam now shared by both callers because the measured
router/parser intersection is empty.

Prove each correction sub-diff comment-only under B254 by comparing preprocessed output with comments removed, or
an equivalent token-level check. Do not change `firmware_help.h`, its predicates or the BLE guard to close a
comment row. B300's stale `lib/core/node.cpp` DM-cap comment remains outside this fence and open.

## 0c-4 — wiring and mutation gate

The executable authority is the existing `probe_inbox_verbs`: extend it to compile the real seam in
`firmware_commands.cpp` with the production device context and drive both format arms through its real
`GuardedConsole` and real `LineSink`. The seam's router/parser decision, output bytes and side effects are all
executed there. `fw_main.cpp` itself remains unhostable; the existing console structural gate pins each one-line
adapter to exactly one seam call and proves that no residual parse, dispatch or Node call remains. This combination
is the honest wiring gate: executable behavior at the policy-bearing seam, structural wiring at the two glue-only
call sites.

Every changed instrument enforces its own derived pins, runs controls by default, labels a control-free mode
`PROBE-ONLY — NOT A GATE`, preserves rejected output for diagnosis, hashes every source/fake it mutates, and
appears in the explicit-add inventory.

At minimum, independent controls turn RED when:

1. serial bypasses the seam;
2. BLE bypasses the seam;
3. either caller parses or executes a Node command a second time;
4. the router/parser order is reversed without the intersection authority changing;
5. a synthetic parser/router collision is accepted instead of refused by the checker;
6. serial renders a Node result with the BLE envelope or vice versa;
7. BLE fallback output uses the direct buffer or omits its one flush;
8. `peerkey` or `peername` bypasses its established handler;
9. accepted `reqpubkey` loses its BLE-specific event or serial hint;
10. empty/unknown/malformed ownership changes;
11. the supplied sink is replaced with `mrcon` on either transport;
12. the common seam retains a borrowed command body beyond the call;
13. a current router or parser primary form disappears from the source-derived matrix; and
14. one B298 topic-era comment survives as an active present-tense claim; and
15. the `firmware_commands.h` paragraph still claims the two callers are deliberately not retrofitted.

Each control applies once to an isolated copy, runs the executable seam driver or the appropriate adapter
structural checker, names the failed rows, and leaves the real checkout byte-identical. Structural checks are
sufficient only for the two one-line `fw_main.cpp` adapters; they may not replace execution of either format arm
inside the real seam. `src/` has no standing mutation-battery target for these decisions, so the evidence must
state that the source-file selector is empty and explain why the executable probe controls plus adapter pins are
the owning gate. Historical/dependency batteries are derived separately rather than invented to make the table
non-empty.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. the exact dispatch base has not been committed and written into this brief, the worktree is dirty at dispatch,
   or an isolated worktree starts from a different commit;
2. after the required projection/source audit, the router/parser intersection is genuinely non-empty for a
   current feature profile and no owner ruling names the winner;
3. byte output or side effects differ on either transport for any pre-slice line;
4. a command executes twice, becomes newly reachable/unreachable, changes feature gate, or changes which transport
   owns it;
5. preserving behavior needs command-name special cases in the common seam, a second verb map/parser, or parsing
   rendered text;
6. `CommandContext`, authority classification, the remote wrapper, `DispatchResult`, a remote handler, or any
   Slice-6 policy is needed;
7. a new persistent/static buffer, heap allocation, virtual interface, retained `Command::body` pointer, or
   dynamic stack frame is needed;
8. BLE help, direct-versus-streamed notification behavior, line capacity, USB intake capacity, or any sink's
   truncation/flush policy changes;
9. B298 cannot be closed as a comment-only delta, or closing it requires a help predicate/presentation edit;
10. any `lib/`, wire/frame, storage/NV, radio, companion, `variants/`, `platformio.ini`, simulation source,
    protocol/manual or feature-policy file must change;
11. the command inventory changes semantically rather than only through source anchors;
12. corpus output or the simulator binary changes, `sizeof(Node)` changes, board RAM/`.bss`/`.data` moves, or a
    flash symbol outside the authorized seam/caller adapters moves;
13. the real seam cannot be executed by `probe_inbox_verbs` through both format arms, or either one-line
    `fw_main.cpp` adapter cannot be pinned to exactly one seam call with no residual parse/router/Node decision;
14. a control is GREEN, vacuous, multi-matched, compile-only or unusable and cannot be repaired inside the allowed
    probe surface; or
15. an unrelated working-tree change, stale pin or unexplained warning remains.

## Fence

Allowed production edits:

- `src/firmware_commands.h` and `src/firmware_commands.cpp`: the one execution seam, narrow internal completion,
  existing router integration and B298 comment corrections;
- `src/fw_main.cpp`: only the serial/BLE caller consolidation and adjacent comments; and
- `src/dispatch_sink.h` or `src/console_sink.h` only if an existing zero-allocation adapter needs a behavior-neutral
  signature refinement. Touching either must be justified before the edit and separately byte/size-proven.

Allowed support edits:

- `tools/probe_console_sink/`, `tools/probe_inbox_verbs/` and their auto-discovered wrappers;
- `tools/probe_custody_usb/` only where an existing structural pin moves; its custody renderer behavior and pins
  otherwise remain exact;
- `tools/gen_command_inventory.py`, its tests, and
  `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` only for relocated source anchors—no
  classification/projection behavior change; and
- `docs/superpowers/evidence/2026-09-05-radmin-0c.md`.

No test under `test/` is expected because the native build does not compile `src/`; a new native lookalike is not
acceptable wiring evidence. No `lib/`, other `src/`, `device_ble.h`, `firmware_help.h`, `simulation/`,
`variants/`, `platformio.ini`, companion, protocol/frame/manual, register, bench, spec, tracker or memory file
belongs to the coder. Author landings follow Quality-Agent PASS. B300 and B303 remain separate.

The only ruled board environments are `gateway` and `heltec_mobile`. The warning census's own pinned environment
set remains the standing exception.

## Gate

Run sequentially on the final tree and record every figure as newly derived:

1. verify the exact base and clean start; publish the production/support diff and STOP-fence audit;
2. execute the BEFORE/AFTER transcript plus side-effect comparator for the complete line matrix through both real
   seam format arms, then run the permanent current-tree intersection/completeness gate and both `fw_main.cpp`
   adapter pins with every control;
3. run the complete default `probe_console_sink`, `probe_inbox_verbs`, `probe_firmware_ui`,
   `probe_custody_usb` and `probe_ble_line` gates; no control-free invocation is a gate result;
4. run `python3 -m unittest discover -s tools -p 'test_*.py'`;
5. regenerate the command inventory, prove the diff normalises to source anchors only, then require both the bare
   verification and `--check` to pass;
6. run the real native binary, reporting the case/assertion/failure totals and the exact line
   `PIN re-synced? YES — <derivation>`; the expected disposition is an unchanged PIN because no native test is
   added, but the coder derives it;
7. rebuild the simulator, record its action count and binary hash, run
   `tools/run_corpus.py --jobs=8 --require-anchors`, compare against the pre-slice corpus and require 36/36
   byte-identical streams with the current s18 keystone reproduced from `BASELINE.md`;
8. run both complete ABI probes with controls and require `sizeof(Node)` unchanged on host, ARM and Xtensa;
9. run `tools/measure_board.py pair --jobs=2` for exactly `gateway` and `heltec_mobile`; require RAM +0, unchanged
   non-code sections, and attribute every flash symbol;
10. run the warning census at all of its own pinned environments, both standing checkers, and `git diff --check`;
11. derive the source-file mutation selector and the dependency/historical selector separately, run their union
    where non-empty, and explain why any selector is empty; and
12. list every modified/untracked file explicitly, including every new probe artifact—never rely on
    `git commit -a`.

Any corpus mover is a STOP. There is no re-anchor path in 0c.

## Required evidence and held Author landings

The coder writes one durable report:

`docs/superpowers/evidence/2026-09-05-radmin-0c.md`

It contains the source-derived ordering/intersection table, complete BEFORE/AFTER transcript and side-effect
comparison, seam/caller trace, B298 token-level proof, control ledger, all gate outputs, STOP audit, findings and
complete commit inventory. Every number is measured by the coder; pre-check figures are hypotheses only.

After Quality-Agent PASS, hold for the Author:

- design §19 Slice 0c → software-complete / Part 60 metal pending;
- B298 → CLOSED with the comment-only proof;
- Bench Part 60: ten fixed lines over USB and BLE, using the evidence's pre-slice transcript and requiring the
  post-refactor bytes/outcomes to match per transport; and
- any new finding as a register row, including a reserved close shape when known.

No owner ruling is presently requested. A non-empty router/parser intersection is the named condition that creates
one.
