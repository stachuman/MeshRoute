<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0e — characterization and generated authorities · dispatch brief · 2026-09-04

**Status: QUALITY-AGENT PASS 2026-09-04 (one fold-in applied in place) — AUTHORIZED FOR DISPATCH.** Dispatch model after PASS: **Opus**.
Authority: the design-pass
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`, especially
R-RA-1/R-RA-2/R-RA-3/R-RA-20 and §§12–15. Pre-check input:
`docs/superpowers/plans/2026-09-04-radmin-0e-precheck.md`.

0e is an **instrument-and-measurement slice**. It produces four authorities needed by later
briefs: a complete command inventory awaiting owner classification; ABI measurements for
candidate bounded-state records and both timer strategies; packer-derived per-carrier RPC
caps; and the first concrete activation-budget numbers. It lands no production type,
capacity, timer, command policy, codec, configuration or behavior.

## Contract quoted verbatim from the reviewed design

> - **0e, characterization and generated authorities:** generate and pin the complete command/subcommand
>   inventory for the later owner classification (R-RA-1); define candidate value types for every
>   controller/target bounded state record and measure their cap/RAM/timer cost on host, ARM and Xtensa
>   (R-RA-2); and derive every carrier's request/response cap through the real packers, including boundary
>   and cap-plus-one probes including the outer-`CRYPTED` negative control (R-RA-3). It also derives
>   first-hop budget inputs from the production timing authorities, names the MAC CTS-wait and ACK-wait
>   windows, and reports the resulting `remote_scheduled_reply_path_budget_ms(cfg)` and default/floor
>   feasibility. This slice reports facts; it neither guesses authority nor lands capacities,
>   configuration, or the production function—Slice 7a owns that authority.

The gate ownership row is also authority:

> | 0e | generated inventory, ABI/cap/timing probes under `tools/` + fixtures under `test/` | 36/36 unchanged; host/ARM/Xtensa ABI and ruled pair | none |

The activation formula must be transcribed term-for-term, not summarized:

> ```text
> remote_scheduled_reply_path_budget_ms(cfg) =
>     airtime_ms(RTS at cfg PHY)
>   + airtime_ms(CTS at cfg PHY)
>   + airtime_ms(maximum TERMINAL{scheduled} DATA at cfg PHY)
>   + airtime_ms(ACK at cfg PHY)
>   + cts_to_data_gap_ms
>   + rts_max_retries * rts_busy_retry_ms
>   + MAC CTS-wait window at cfg PHY     (production authority named by Slice 0e)
>   + MAC ACK-wait window at cfg PHY     (production authority named by Slice 0e)
>   + cascade_requeue_base_ms
> ```

> - `remote_action_activation_min_ms(cfg) = remote_scheduled_reply_path_budget_ms(cfg)`; an action may not be
>   configured to fire before the scheduled terminal has left this first-hop reply path.
> - `remote_action_activation_default_ms(cfg) = 2 * remote_scheduled_reply_path_budget_ms(cfg)`. The factor
>   of two is the owner's explicit 2026-09-04 choice; no approximate literal is a design authority.
> - `remote_action_activation_max_ms = e2e_ack_deadline_xl_ms - 1`, strictly below the outer 300-second bound.

## Required result

0e passes only when all four deliverables are durable, independently checked and clearly
separated from the owner decisions they inform:

1. **Command inventory:** every production top-level verb, sub-verb, caller-only command,
   owning function, transport and compile gate is represented exactly once in deterministic
   generated output. Its authority-classification cells are empty.
2. **Candidate state measurements:** every bounded controller and target record named by
   §15 has a test-only candidate value type whose `sizeof` and `alignof` are measured on
   host, ARM and Xtensa. Aggregate RAM at candidate capacities, gateway headroom and both
   timer strategies are priced; no capacity or timer choice is made.
3. **Carrier caps:** every request/response carrier has one test-side cap derivation through
   the real DATA packers, with at-cap accepted, cap-plus-one refused and outer `CRYPTED`
   reducing the result. No production `remote_body_cap()` lands yet.
4. **Activation budget:** every term is read from the named production authority, including
   `start_rts_timeout()` and `start_ack_timeout()`. The current configured-PHY budget, floor,
   owner-selected 2× default and ceiling are published; Slice 7a remains the production
   owner.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. any file under `lib/`, `src/`, `variants/`, `simulation/`, `ios-companion/`, or
   `platformio.ini` must change;
2. the inventory needs a handwritten verb/sub-verb list, silently omits caller-only arms,
   cannot record feature gates, or gives any row an authority classification;
3. an added, removed, duplicated, renamed or gate-moved command can leave the inventory gate
   green, or the generator's own control can pass without reading the real source;
4. any candidate record must live under production headers, become a `Node` member, or
   causes an accepted capacity/timer choice to land;
5. the ABI extension can lose an existing pin, emit an unpinned value, treat a failed compile
   as absence, or alter the default probe result when no extra manifest is supplied;
6. either timer option cannot be priced from the current wheel/storage and shared-scan
   authorities, or a nonexistent free timer ID is assumed;
7. any carrier is missing from the cap table, its cap is computed from copied arithmetic
   without invoking the real packer, SOURCE_HASH is dropped to make it fit, or
   cap-plus-one/outer-`CRYPTED` is not observable;
8. the test-side cap derivation changes a production codec/API or creates a second wire
   authority that later slices could accidentally consume;
9. the CTS-wait or ACK-wait term cannot be tied to `start_rts_timeout()` /
   `start_ack_timeout()`, the maximum scheduled-terminal length cannot be derived, or the
   current budget/default does not fit below the 300-second ceiling;
10. any formula substitutes the 30-second cascade backoff cap or `send_defer_ttl_ms` for the
    one `cascade_requeue_base_ms` term;
11. native, simulator, either board image, ABI defaults, warning pins, or the current corpus
    changes despite the zero-production fence;
12. a new instrument has no stable path, no auto-discovered test or executable selftest, no
    sabotage controls, or can report PASS after producing no rows; or
13. a figure is copied from the pre-check rather than derived on the coder's tree.

## Fence and durable files

Allowed coder-owned paths:

- `tools/gen_command_inventory.py`;
- `tools/test_gen_command_inventory.py`;
- `test/radmin_0e_candidate_types.h`, the test-only candidate-type header;
- `tools/radmin_0e_abi_pins.json`, the extra-pins type/include manifest;
- `tools/probe_board_abi.py` and its auto-discovered tests, only for a backwards-compatible
  extra-pins/include interface;
- `tools/probe_ui_model_mutations.py`, only for the mechanically derived native PIN update;
- `test/test_radmin_characterization_0e.cpp`, the native fixture that compiles the candidate
  types, invokes the real packers, and reports the carrier-cap and activation-budget terms;
- generated inventory output at
  `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md`; and
- the slice report at
  `docs/superpowers/evidence/2026-09-04-radmin-0e.md`.

No other documentation belongs to the coder. In particular, no `docs/protocol.md`,
`docs/frames.md`, spec, register, tracker, bench, owner-ruling sheet or
`simulation/BASELINE.md` edit lands in 0e. The evidence contains exact drafts/data sheets for
the Author and owner.

The only board environments are `gateway` and `heltec_mobile`; together with host they are
the three ABI columns. The warning census's wider pinned environment set is the sole
standing exception. No third qualification board is added.

## Parallel-dispatch discipline with 0d

0d and 0e may run concurrently only in isolated worktrees with private build/output roots.
Their production authority is disjoint: 0d alone touches `lib/core`; 0e touches no production
file. The one expected mechanical integration point is the native PIN in
`tools/probe_ui_model_mutations.py`, because both slices add native cases. Each evidence file
records its own starting PIN and derived delta. The second branch to land rebases, reruns the
native binary, and derives the combined PIN rather than resolving the line by choosing one
side. It also reruns any evidence claim whose starting HEAD changed.

## 0e-A — complete generated command inventory

The generator must discover, normalize and deterministically sort all three command
surfaces verified by the pre-check:

1. top-level `dispatch()` if-chain verbs, including shared `help`/`?` spelling;
2. sub-verb tables/branches in `handle_ui`, `handle_cfg_set`, `handle_team`,
   `handle_mobile`, the legacy `admin`/`rcmd` owner, and `testsend`/`testch`; and
3. serial/BLE caller-only arms around `dispatch()`, including `send`, `send_channel`,
   `send_layer` and the BLE fallback's current status/version/routes/peers surface.

Each output row contains:

```text
verb | sub-verb or — | owning function | transport set | feature gate | file:line | authority
```

The `authority` cell is blank on every row. The generator may extract source structure, but
must not infer policy from command names, help prose, comments or previous design tables.
Feature-gated rows remain present with their exact macro rather than disappearing under the
host's current flags. Record aliases explicitly; do not duplicate one semantic arm merely
because it has two spellings.

The tracked table must equal fresh generation byte-for-byte. The auto-discovered test suite
must include at least these sabotage classes in isolated fixture copies:

- add and remove one top-level verb;
- add and remove one sub-verb;
- add and remove one caller-only arm;
- change a feature gate without changing a name;
- duplicate a normalized row;
- make generation return zero rows;
- leave a row without owner/function/transport/source provenance; and
- populate one authority cell.

Every sabotage must fail for its named reason. A regex that merely counts `strncmp` tokens
is not enough: fixture controls must exercise each supported source shape, and the real-tree
test must prove representative rows from all three surfaces were seen. The generated output
is the input to a later owner classification ruling; 0e does not annotate it.

## 0e-B — candidate bounded-state ABI and timer costs

Define test-only candidate value types for every bounded state record named in §15, with
fields and ownership recorded beside each type:

### Controller-side candidates

- pending request, including local transport, full target-administration identity, stable
  SOURCE_HASH, selected credential, target ACL slot, epoch, random 64-bit request ID, exact
  sealed request storage/reference and retry state;
- session/discovery cache keyed by full target and selected-controller public identities;
- authenticated response assembly;
- retained BLE result; and
- response-ACK debt.

### Target-side candidates

- seen-request/fingerprint record;
- authenticated transcript record/pool element;
- bounded ingress operation; and
- bounded open/bootstrap staging or admission-partition element; and
- deferred disruptive-action record.

If one bullet requires more than one value type, expose each independently rather than
hiding a large aggregate. Conversely, do not invent fields or merge ownership merely to
hit a preferred size. Every type stays outside production and is labelled a measurement
candidate, not a frozen API.

Extend `probe_board_abi.py` with
`--extra-pins tools/radmin_0e_abi_pins.json`; its generated TU includes
`test/radmin_0e_candidate_types.h` and emits size/alignment symbols beside the existing pins.
The default invocation and current PIN table must remain byte-for-byte equivalent in
meaning. Both coverage directions remain mandatory: every requested candidate emits and is
pinned for that run, and no unrequested symbol appears. A compile failure is a failed
measurement, never zero/absent.

For host, `gateway` ARM and `heltec_mobile` Xtensa publish:

- `sizeof` and `alignof` of each candidate;
- padding/offset explanation where ABIs differ;
- per-table aggregate at each **candidate** row count;
- combined controller and target totals by profile;
- resulting ruled-board RAM/headroom if those candidates were resident; and
- the exact assumptions separating value size from actual future object residency.

Price both timer strategies without choosing one:

1. increasing `TimerWheel::kCap`, including per-ID wheel storage, ABI/RAM cost and any
   scheduler scan consequence; and
2. one shared remote-admin expiry scan, using the existing re-armed shared-scan idiom,
   including timer storage, per-tick maximum row scan, cadence and worst-case expiry lag.

The report must state that the current wheel has no free ID and must not spend a hypothetical
92nd slot. The owner will rule exact capacities, resource partition and timer strategy after
QG PASS.

## 0e-C — packer-derived carrier-cap table

Create a test-side `RemoteCarrier` vocabulary covering every design carrier, at minimum:

- registered mobile → own home wrapper;
- home → target by node ID;
- home → target by key hash;
- each legal cross-layer path depth;
- target → home/static return by node ID or hash as applicable; and
- hosted-mobile final delivery, including the hash-addressed last-mile shape.

For every carrier, derive immutable inner overhead from the real flags/path fields and ask
the landed DATA codec's `data_inner_cap()` / `data_frame_len()` authority. Then invoke the
real packing path at the returned RPC-body cap and cap plus one. The output table contains
carrier, direction, required flags, path depth, immutable field bytes, storage cap, air-fit
cap, governing cap, authenticated command/output capacity and exact refusal at cap plus one.

Required controls:

- the registered-mobile example independently reproduces its field accounting rather than
  copying the design's number;
- `SOURCE_HASH` is present on every v2 carrier;
- hash-addressed and node-ID forms differ only where their actual fields differ;
- every legal cross-layer depth is exercised, including the minimum-cap carrier;
- outer `CRYPTED` recomputes against the lower air-fit answer and therefore lowers the cap;
- dropping SOURCE_HASH, DST_HASH or a path byte to recover room turns RED;
- substituting `dm_max_body_bytes`, 241, 214, 213 or another copied universal literal turns
  RED; and
- cap succeeds and cap-plus-one refuses through the same real packer for every row.

The helper remains test-only. Slice 2 owns the production `remote_body_cap(RemoteCarrier)`
API and may use this table as its KAT input only after QG accepts the derivation.

## 0e-D — first concrete activation budget

Name and reproduce the two MAC window authorities exactly:

- CTS wait: `start_rts_timeout()` and its attempt-dependent base/shift/slop calculation; and
- ACK wait: `start_ack_timeout()` and its DATA airtime, ACK airtime, slop and final arming
  adjustment.

Do not flatten either into an unexplained duration. For the ruled default static-plane PHY,
derive and publish every row of this sum:

```text
RTS airtime
+ CTS airtime
+ maximum authenticated TERMINAL{scheduled} DATA airtime
+ ACK airtime
+ cts_to_data_gap_ms
+ rts_max_retries * rts_busy_retry_ms
+ start_rts_timeout-derived CTS-wait window
+ start_ack_timeout-derived ACK-wait window
+ cascade_requeue_base_ms
= remote_scheduled_reply_path_budget_ms(cfg)
```

The maximum terminal frame uses §8.7/§8.9's authenticated envelope and result/delay detail,
then the same real DATA framing authority used by 0e-C. Publish host/simulator `slop=0` and
the separately identified metal-slop input; do not describe a bench estimate as a compile
constant. Compute:

- floor = one budget;
- default = two budgets, expressly the owner's R-RA-20 factor;
- ceiling = `e2e_ack_deadline_xl_ms - 1`; and
- remaining interval headroom.

Controls independently alter every input and require the result/default to move in the
correct direction. Specifically attack omission of each airtime, the CTS→DATA gap, busy
retry, either wait window and the one requeue; substitution of the 30-second backoff cap or
`send_defer_ttl_ms`; a stale terminal length; hard-coded PHY; a bare 3/15/30-second literal;
and a default not equal to exactly 2×. If the measured default does not fit the interval,
STOP rather than clamp.

0e publishes the first number and an owner input sheet. Slice 7a later owns the production
function, cfg field, migration and validation.

## Corpus, native, ABI, boards and tools gate

Run and record:

1. the clean native suite and real binary, deriving all new case/assertion movement and PIN;
2. `tools/run_corpus.py --jobs=8 --require-anchors` for all 36 streams against the current
   ruled table. ⚠ **QA fold-in 2026-09-04 (owner-agreed):** 0e runs in an isolated worktree, which has NO
   simulator build (the simulator is bound to the main tree through `MESHROUTE_DIR`). Do not claim a
   zero-action rebuild. Instead prove the worktree's `lib/core` and `lib/console` byte-identical to the
   main tree's HEAD (a directory md5 over both), then run the corpus with the main tree's binary:
   `run_corpus.py --lus /home/staszek/lora-universal-simulator/build/orchestrator/lus`, recording that binary's
   md5 as the one the S5 gate validated. The runner's own validation is the identity proof;
3. the full existing ABI probe unchanged, then its extra candidate-pins mode on host, ARM
   and Xtensa, with all coverage controls;
4. deterministic `tools/measure_board.py pair --jobs=2` for exactly `gateway` and
   `heltec_mobile`, requiring byte-identical payloads, sections, symbols, RAM and flash to
   the starting tree. Make the before/after comparison from the same checkout path; if an
   isolated-worktree comparison is also reported, apply B262's field/symbol method rather
   than treating heltec_mobile's path-dependent payload hash as image evidence;
5. `tools/warning_census.sh` at its own pinned set, the sole wider-env exception;
6. the complete auto-discovered tools suite, both standing checkers and both wiring probes;
   and
7. `git diff --check`, process cleanup, and exact final tracked/untracked inventory.

The native additions prove the real packers and any C++ candidate layout decisions. Python
selftests prove instrument parsing, validation and sabotage behavior. Every new tool must be
auto-discovered or have a default gate that runs its negative controls and refuses to print
PASS in probe-only mode.

No metal test is owed: 0e changes no device behavior. The measured slop value is an input
label for later bench confirmation, not permission to contact a device in this slice.

## Held Author/owner outputs

The evidence file must include exact ready-to-land material for:

- the generated command inventory, with its blank classification column, presented to the
  owner for the R-RA-1 authority ruling;
- the candidate-size/capacity/headroom/timer table and explicit owner choices required by
  R-RA-2/R-RA-10;
- the full carrier-cap table for Slice 2;
- the measured §13 budget-input/result table and the owner's concrete-default acceptance;
  and
- narrowly scoped design §13/§15 replacements that insert accepted measurements without
  rewriting the historical decisions.

The coder does not edit the design or ruling ledger. QG first verifies the measurements;
then the Author lands the accepted tables and records the owner's choices. No protocol,
frames, command-reference or bench edit is owed by characterization alone.

## Durable evidence and report shape

The one narrative report is:

`docs/superpowers/evidence/2026-09-04-radmin-0e.md`

It must contain:

- starting HEAD and complete changed/untracked inventory;
- the generator's source-shape model, complete row count, tracked-output hash and every
  sabotage result;
- explicit proof every authority-classification cell is empty;
- every candidate type/field, host/ARM/Xtensa size/alignment and aggregate/headroom table;
- both timer-option calculations without a recommendation disguised as measurement;
- every carrier's packer-derived cap and at-cap/cap-plus-one/outer-CRYPTED result;
- the activation formula with every named source input and first numeric result;
- native, corpus, simulator-inertness, ABI, board-byte-identity, warning, checker, probe and
  tools results;
- every STOP condition evaluated explicitly;
- the three owner-ruling input sheets and exact held design drafts;
- the exact line `PIN re-synced? YES — <derivation>`; and
- final `git status --short`, separating pre-existing work and naming every new file that
  needs explicit `git add`.

Every figure is derived by the coder on its tree. Pre-check counts, sizes, headroom and
timing estimates are hypotheses or cross-checks only. Expected owner rulings after PASS are:

1. command/sub-command authority classification over the complete generated table;
2. exact record capacities, authenticated/open partition and timer strategy; and
3. acceptance or override of the measured concrete activation default while preserving
   R-RA-20's formula and 2× rule.
