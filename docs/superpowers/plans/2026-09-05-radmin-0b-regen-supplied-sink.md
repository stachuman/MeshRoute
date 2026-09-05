<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0b — `regen` uses its supplied sink · dispatch brief · 2026-09-05

**Status: DRAFT — awaiting Quality-Agent review.** Dispatch model after PASS: **Opus**.

Authority: B279 in `docs/2026-07-30-open-bug-register.md`, §19 Slice 0b of
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`, and the Quality-Agent pre-check
`docs/superpowers/plans/2026-09-04-radmin-0b-precheck.md`.

Dispatch base is exactly **`dec8417`** (subject `0h`). The coder verifies and records that commit before reading or
editing the implementation, including in every isolated worktree, and STOPs if it differs. Author-owned 0h doc
landings may be dirty beside that commit; they are not coder inputs and must remain untouched. The pre-check was
measured on an older tree, so every source anchor and figure in it is a hypothesis to relocate and re-derive.

0b is one small `src/` wiring correction. `regen` already mutates identity correctly, but it discards the
`Print&` selected by `dispatch()` and writes its result to global `mrcon`. The slice threads the selected sink
through the existing functions, preserving USB bytes and identity/NV behavior while making the same bytes reach a
BLE `LineSink` without leaking to USB. It adds no remote execution, parser, command, storage format, wire behavior,
or product policy.

## Authority quoted verbatim

From design §19:

> **0b:** fix B279's source-confirmed `regen` supplied-sink defect;

From B279:

> **CLOSE BY, as its own micro-fix before remote dispatch:** pass the supplied `Print&` through `do_regen` and the
> one canonical `print_identity` path (setup explicitly supplies `mrcon`), preserve identity/NV behaviour
> byte-for-byte, prove USB and BLE receive the same normal text through their own sinks, prove no cross-sink output,
> and add a mutation restoring the global sink. Do not combine with remote-management enablement (C1).

## Required result

0b passes only when all of these hold:

1. `print_identity` has one public signature, `print_identity(const mrnv::IdBlob&, Print& out)`. There is no
   parameterless overload, default sink or implicit fallback.
2. `do_regen` takes `Print& out`; every success and failure byte it owns goes to that sink, including the delegated
   `print_identity` call.
3. The dispatch arm calls `do_regen(out)`. The boot path in `setup()` calls `print_identity(idb, mrcon)` explicitly.
4. A successful USB-shaped dispatch emits byte-for-byte the existing line through `mrcon`: `> regen ok`, two
   spaces, `key_hash32= 0x` plus eight uppercase hex digits, the optional preserved name, and the existing line
   ending. No extra acknowledgement or JSON is added.
5. A successful BLE-shaped dispatch through the real `dispatch()` and a production-shaped `LineSink` returns the
   same bytes through that supplied sink and leaves `mrcon`/`Serial` empty.
6. `save_id` failure emits exactly `> regen err nv_save_failed` and the existing line ending through whichever
   sink invoked the command; it changes neither installed identity nor crypto identity.
7. Success preserves the existing operation order and effects: load the current identity record/name, fill a new
   seed, stamp the same magic/version, save `/mrid`, derive `g_identity`, install the node's routing identity, then
   install the crypto identity before reporting success.
8. The boot banner keeps using the same canonical identity formatter and its output bytes remain unchanged.
9. The drifted “Shared by boot, status, regen” comment is corrected in place with the old claim visible: source
   proves the formatter is shared by boot and `regen`, not `status`. The include comment that currently attributes
   `mrcon` ownership to `do_regen` is corrected in the same pass; after 0b the legitimate direct use in this TU is
   the separately inventoried boot restore path, not dispatcher-reachable `regen`.
10. Every other command, dispatcher arm, global-console writer, BLE fallback, NV record and RNG path is unchanged.
11. Native, simulator/corpus, command inventory, ABI and RAM are unchanged. Board flash movement is small and fully
    attributable to the three signature/call-site changes.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. the dispatch base differs from `dec8417`;
2. preserving the success or failure bytes requires changing the command syntax, parser, response text, line
   endings, identity derivation, RNG source, NV record, save order, node identity API or crypto identity API;
3. the BLE path requires an edit to `device_ble.h`, `dispatch_sink.h`, `console_sink.h`, a second parser, or a new
   response buffer;
4. the supplied sink cannot receive the complete response without a global-console write;
5. setup cannot pass `mrcon` explicitly without another `fw_main.cpp` behavior change;
6. a new production file, public overload, default sink, heap allocation, persistent field, wire field, feature
   flag or remote-management path is needed;
7. any `lib/`, `simulation/`, `variants/`, `platformio.ini`, companion, protocol/frame or production manual file
   must change;
8. corpus output, the simulator binary, `sizeof(Node)`, board RAM, `.bss` or `.data` moves;
9. the command inventory changes semantically rather than only relocating source lines;
10. a control is GREEN, vacuous, multi-matched, compile-only or unusable and cannot be repaired inside the allowed
    probe surface;
11. a probe cannot distinguish the supplied sink from `mrcon`, or a fake decides formatting/routing behavior that
    belongs to production; or
12. an unexplained flash symbol, warning, gate-pin movement or unrelated working-tree edit remains.

## Fence

Allowed production edits are exactly:

- `src/firmware_commands.h` — the one exported `print_identity(..., Print&)` declaration;
- `src/firmware_commands.cpp` — the formatter definition, `do_regen(Print&)`, dispatch call, and drifted comment;
- `src/fw_main.cpp` — only the boot call's explicit `mrcon` argument.

Allowed supporting edits are:

- `tools/probe_inbox_verbs/probe_main.cpp` and `run.sh`, extending the existing real-dispatch wiring gate with the
  success, failure and cross-sink cases and default negative controls;
- only the probe-local `tools/probe_inbox_verbs/fakes/Preferences.h` and `esp_random.h` changes strictly required
  to model deterministic `/mrid` success/failure and seed bytes. A fake may expose controllable storage/RNG facts;
  it may not format output, route a sink, derive identity, or answer a production decision;
- `tools/probe_console_sink/structural.py`, `negctl.py`, `run.sh` and its auto-discovered wrapper only if needed to
  pin the explicit boot call and update their derived counts; do not duplicate behavioral `regen` coverage there;
- `tools/gen_command_inventory.py` output regeneration via `--write` and
  `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md`, only for relocated source anchors; and
- `docs/superpowers/evidence/2026-09-05-radmin-0b.md`.

No native test file is expected: the native environment does not compile `src/`. No other `src/`, no `lib/`, NV
format, simulator, baseline, board configuration, companion, register, bench, spec, tracker or memory file belongs
to the coder. The Author owns held documentation after QG PASS. The two ruled board environments are `gateway` and
`heltec_mobile`; the warning census's pinned set remains the sole exception.

## 0b-0 — characterize before editing

Before changing production code, the coder re-derives and records:

1. every caller of `print_identity`, every caller of `do_regen`, and every direct `mrcon.` writer in
   `firmware_commands.cpp` and its sibling command TUs;
2. the exact current success bytes, with and without an identity name, from the formatter and the current USB
   dispatch shape;
3. the exact current NV-failure bytes and the fact that identity installation occurs only after a successful save;
4. the BLE fallback's real route—parser returns unknown, `dispatch(line,len,LineSink&)` owns `regen`, and the
   selected line sink is flushed—plus the BEFORE defect: supplied sink empty and global console non-empty;
5. current native/PIN, command-inventory row, simulator hash/anchors, both ABI sizes, board pair, warning census and
   every probe pin the final gate will compare; and
6. a prediction written before implementation: native, simulator and all 36 streams byte-identical; command
   inventory semantically identical; Node/RAM unchanged; only board code bytes may move.

Do not copy the pre-check's caller count, writer count, output length or line anchors. Derive them from `dec8417`.

## 0b-1 — thread the sink through one canonical path

Change the existing functions directly:

```cpp
void print_identity(const mrnv::IdBlob& idb, Print& out);
static void do_regen(Print& out);
```

Replace only the formatter/regen path's `mrcon` uses with `out`. Do not add a wrapper that silently supplies
`mrcon`; every caller must name its output authority. The dispatcher supplies its own `out`; setup supplies
`mrcon`. Preserve all formatting statements and their order so the sink name is the only output change.

Keep `peer_store_restore` and any other boot-only direct `mrcon` writer untouched. The final source census must
show that no dispatcher-reachable `regen` output uses `mrcon`, while legitimate non-dispatch boot writers remain.

Correct both affected comments with the V1 idiom: retain “Shared by boot, status, regen” as the withdrawn formatter
claim and state the source-confirmed replacement, “shared by boot and regen”; likewise retain and replace the
include comment's attribution of global `mrcon` to `do_regen`. Do not alter the status path or remove the include
while the separately inventoried boot writer still needs it.

## 0b-2 — extend the real-dispatch wiring gate

Use `tools/probe_inbox_verbs/` as the behavioral owner because it already compiles the real
`src/firmware_commands.cpp`, links the real core/console code, defines the production device context, and drives
`mrfw::dispatch(line,len,Print&)`. It also already provides the ESP32 Preferences and RNG fakes needed to expose
both NV outcomes. Do not create a second `regen` parser or a lookalike command handler.

Extend its probe with independently resettable capture sinks and deterministic probe-local storage/RNG state.
Every case calls the real dispatcher:

- **BLE-shaped success:** seed a valid `/mrid` record with a non-empty name; dispatch `regen` into a bounded
  `LineSink`/capture sink; flush it; assert exact success bytes, eight-digit uppercase hash, preserved name, one
  saved record with fresh deterministic seed/magic/version, updated `g_identity`, matching `g_node.key_hash32()`,
  and zero bytes in `Serial`/`mrcon`;
- **USB-shaped success:** reset state, dispatch the same command with `mrcon`, drain the real guarded console, and
  assert byte-identical content to the BLE-shaped success for the same deterministic seed/name;
- **save failure on both sinks:** force `save_id` to fail; assert the exact error line reaches only the selected
  sink, the success prefix/identity line is absent, and the previous `g_identity`/node identity remain unchanged;
- **optional-name boundary:** no-name and maximum-valid-name records preserve today's exact formatter behavior;
- **router boundary:** `regen` is owned exactly, while `regen `, `regenerate` and case variants retain their
  existing non-ownership; and
- **boot formatter:** call the canonical formatter with `mrcon` in the production boot shape and pin the same bytes;
  separately pin `fw_main.cpp` contains exactly one explicit `print_identity(idb, mrcon)` call.

The probe-local Preferences fake must distinguish absent/readable/writable/failing state and retain exact bytes;
the tests inspect what production wrote. The RNG fake returns a deterministic byte stream and records consumption;
production still calls `mrrng::fill`. Each fake change is additive and the existing inbox-verbs cases must retain
their previous verdicts.

Re-derive and enforce the runner's check/control pins with written arithmetic. `--no-neg` remains
`PROBE-ONLY — NOT A GATE`, never PASS. Extend its tree-integrity hash to every touched production/probe/fake file.

## 0b-3 — negative controls

Controls run by default on isolated copies, each exact-match count one. At minimum:

1. route the dispatch arm through `do_regen(mrcon)` instead of its supplied `out`; the USB arm remains green while
   the BLE response goes empty and `Serial` receives the leak;
2. route `do_regen`'s own success/error writes back to `mrcon`; the cross-sink and failure cases must RED;
3. route `print_identity`'s body back to `mrcon`; the success prefix may reach the supplied sink, but the identity
   tail leaks and the byte-exact/cross-sink cases must RED;
4. omit the explicit boot `mrcon` argument or route the boot call to a different sink in a compile-valid isolated
   source copy; the structural boot-owner check must RED;
5. suppress `LineSink::flush()` in the BLE-shaped probe drive; the case must demonstrate that the complete returned
   line, not merely staged bytes, is the observable contract; and
6. make the fake save fail after accepting bytes, or report success without retaining exact bytes; the probe must
   refuse its own dishonest storage instrument.

The unmutated positive must be green before controls are credited. A compile failure is not a RED behavioral
result. Every control reports applied count, build/run verdict, named failing check and real-tree md5 preservation.

## 0b-4 — inventory and compatibility proof

Regenerate the command inventory only after proving it is stale solely because source lines moved. The semantic
projection before/after must contain the same rows, verbs, sub-verbs, functions, transports, gates and surfaces;
the `regen` row still names the same function. Normalize only source line numbers for that comparison. Any other
difference is STOP 9.

Run all existing dispatcher/sink probes. The console-sink gate retains its 0g primary-name oracle and BLE help
policy; inbox-verbs retains all destructive-command checks; firmware UI, custody USB and BLE-line probes stay at
their established behavior. No existing command output may move.

## Final gate

The coder runs and reports every figure from the final tree:

1. the real native binary; it must reproduce the starting cases/assertions/failures because no native test or
   compiled source changed, with the exact line `PIN re-synced? YES — unchanged; <derivation>`;
2. simulator build action count and binary hash, then `tools/run_corpus.py --jobs=8 --require-anchors`; require
   36/36 ruled anchors and byte-identical streams, with no re-anchor proposal;
3. the extended `tools/probe_inbox_verbs/run.sh` default gate and `--no-neg`, with every new and existing control;
4. `tools/probe_console_sink/run.sh`, `tools/probe_firmware_ui/run.sh`,
   `tools/probe_custody_usb/run.sh` and `tools/probe_ble_line/run.sh` at their pins;
5. `python3 -m unittest discover -s tools -p 'test_*.py'`, including the console-sink auto-discovered wrapper;
6. both ABI probes with every control and unchanged `sizeof(Node)` on host, ARM and Xtensa;
7. `measure_board.py` for exactly `gateway` and `heltec_mobile` with `--jobs=2`; RAM, `.bss`, `.data`, object set
   and non-code sections unchanged, with every flash delta attributed to the authorized formatter/call functions;
8. `tools/warning_census.sh` on its own pinned environment set, including selftest and zero new warnings;
9. `python3 tools/gen_command_inventory.py --check`, then the deliberate `--write`, followed by a semantic
   before/after inventory comparison proving only source anchors moved;
10. both standing DATA-type/matrix checkers with their selftests; and
11. `git diff --check`, the exact fence diff, zero `lib/`/wire/NV-format diff, and complete tracked/untracked file
    inventory.

The evidence file records the source census, BEFORE reproduction, exact golden bytes, operation-order review,
success/failure state table, cross-sink traces, each control and its falsified claim, inventory comparison, all
gate figures, STOP audit, and the exact PIN line. Nothing durable may remain only in scratch.

## Held Author landings after QG PASS

The coder drafts exact text in the evidence file; the Author lands it only after PASS:

- B279 to CLOSED with the byte-exact USB/BLE and no-cross-sink evidence;
- remote-admin design §19 Slice 0b to QA-passed/software-complete;
- **Bench Part 59:** invoke `regen` over BLE NUS and require the complete `> regen ok  key_hash32= 0x…` line in the
  app with no USB leak; invoke it over USB and require the same format; reboot and require the new hash in the boot
  banner; and
- any source-confirmed finding discovered by the slice, registered rather than carried only in the evidence.

No wire documentation, companion contract, corpus re-anchor or owner ruling is expected. Every untracked file is
listed explicitly for `git add`; never rely on `git commit -a`.
