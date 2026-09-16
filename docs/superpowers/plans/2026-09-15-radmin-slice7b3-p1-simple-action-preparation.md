<!-- QA/Author: OpenAI Codex; production coder: separate Codex session -->
# Slice 7b-3-P1 — simple-action preparation seams — 2026-09-15

**Revision 3 — INDEPENDENT QA PASS 2026-09-16 (Claude), owner commit `7442e6f` — see §6. B394/B399/B400 closed.**
Historical status line: **Revision 3 — CODER GATE PASS; FROZEN FOR INDEPENDENT QA; B394 preparation, before 7b-3 behavior.** This is a
behavior-preserving C1 refactor, not deferred execution. After source-validation passes, implement only this
fence. No new owner policy decision or B389 state allocation is needed for this zero-resident-state slice.
Base **`c591721c2e09bb4e7583f49e458da9e5155cf822`** plus the permitted preparation inventory in the P1
coder receipt §5. This documentation-only successor preserves every production input from QA-passed codec
`ac5f9a592065d08e7cc8c06ef395e79891d41b34`; simulator
**`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean/unchanged. Both repositories were clean at pre-check start.

The [revision-4 author pre-check](2026-09-15-radmin-slice7b3-reissue-precheck.md) provides fresh native/corpus,
source enumeration and measured candidate layouts. The [behavior brief](2026-09-13-radmin-slice7b3-deferred-actions.md)
is an implementation HOLD; its +80-byte proposal is not authority to add state here. This preparation gets
its own implementation report and complete independent gate. QA then refreshes the behavior
brief's base/anchors against that frozen input set; commits are not gates. Do not mix this refactor with scheduling, new remote admission or
production Node re-pins in a single change.

## 1. Source issue and binding constraints

The codec QA completion summary says twelve policy rows are preparable with existing seams. The actual
coder enumeration distinguishes four reusable no-argument effects from eight rows needing extraction.
Its +80-byte comparison explicitly covers only the four-row representation. This is recorded as B394;
neither the codec's software PASS nor B391's closure is revoked. The new author model separately prices the
proposed twelve-row shape; it does not establish that missing production interfaces already exist.

Read AGENTS/CODE_GUIDELINES, the September-7 role override, current register/MEMORY, R-RA-37/38/39, the codec
QA report including its byte-level ELF limitation, behavior revision 4 and the full typed-plan enumeration.
Verify all anchors at the actual base before editing. The following C1 rule is binding, verbatim:

> ★ C1 refactor XOR feature/fix — never both; never fold a file-move into a semantic edit

The code guidelines' hardware boundary is binding, verbatim:

> `device_fault.h` is **single-TU** — its ISR/exception vectors *and* the `MRFAULT_HW`/`MRFAULT_ESP32` macros are defined inside it; never `#include` it in a second TU.

| Source unchanged from ac5f9a5 at the reconciled base | Refactor obligation |
| --- | --- |
| `firmware_commands.cpp:1071–1089` | Factory-reset grammar skips leading spaces and accepts exactly seven remaining bytes `confirm`. Both inbox wipes are attempted independently; their conditional warning precedes NV factory_erase. The conditional NV-failure warning follows that call; reboot follows both checks. Partial failure does not skip later effects. No effect-only typed helper exists. |
| `firmware_commands.cpp:1096–1105` | Sleep uses an `off` prefix after leading spaces; every other tail selects on, including empty/unknown text. Preserve that local grammar and MR_NO_POWERSAVE's current limitation. |
| `fw_main.cpp:395–416` | Crash checks current debug admission, then prefix-parses hang/fault/reboot. Backend guards, messages, flushes and non-returning hardware operations are inside the board TU. A later raw-text call would repeat mutable admission. |
| `fw_main.cpp:293–298`, `:320–351`, `:423–447` | Reboot/prep/OTA wrappers are reusable effects, but return void or use global output. Prep reports wipe failures only through Print; typed outcome/sink binding is missing. Keep reset marking, delays, both wipes and halt order. |
| `device_ota.cpp:92–105`, `fw_main.cpp:335–351` | ota_start is already idempotent; the local ota wrapper intentionally toggles. Preserve both meanings. No remote ensure-entry caller is introduced here. |
| `firmware_remote_executor.h:41–55/87`, `firmware_commands.cpp:1666–1673` | Both current remote guards still refuse disruptive commands. They must remain effective throughout P1. |

## 2. Resulting interface and local behavior

**Owner fold-in (2026-09-15):** typed admission explicitly reports build-level support, including power-saving
compiled out, absent fault backend and absent OTA backend. Local handlers still run their existing grammar,
output and effects on those builds; an unsupported typed result is not permission to change local behavior.
No resident state is added. The later remote consumer uses this support result before promising.

Expose reusable bounded typed admission/argument results and effect-only entry points for reboot,
prep-restart, OTA entry, confirmed factory reset, sleep on/off and crash hang/fault/reboot. Semantic kinds
must own all choices; no stored command pointer, Print reference, string parser or borrowed request is an
activation plan. No production deferred record, queue, timer, armed flag or diagnostic cache is added here.

Local router handlers still parse and apply immediately, through these shared operations. Parsing and
admission happen once at the existing local call; typed apply does not reparse text or re-evaluate the debug
gate. Preserve the one public dispatcher, its order and its transport/authority decisions. Reuse existing
pure predicates when exact grammar matches; `parse_confirm_token` is a candidate to verify, not permission
to broaden/narrow whitespace acceptance. Do not introduce a second full command map or parse Print output
to infer success. Return explicit unsupported/usage/admission values where already represented by local
behavior, with the same local messages and zero effects as before.

The future remote consumer needs a resolved backend and kind, not a new raw argument union. In this slice,
make those choices available from the actual shared checks and keep backend-specific effects in fw_main
when they depend on device_fault macros/ISR ownership. Ordinary feature logic goes in a firmware module.
A supported crash plan can later be applied without reading g_mr_trace_on again; local immediate execution
still checks it once. Do not grant any remote or physical-presence authority with this API.

Typed effect reporting must distinguish success/started, incomplete inbox erase, failed NV erase and
backend failure. Report both independent erase results before a non-returning reset/halt where necessary;
no short-circuit erase, swallowed failure or text scraping. Use call-scoped result/observer/output bindings,
not a resident vtable, cached Print, singleton mirror or Node pointer stored for later. Existing local
warnings and their exact `may remain` wording remain unchanged, including the effect order on partial failure.

Make the effect's output destination explicit. Existing local wrappers pass their original sinks and produce
the same bytes in the same order, including their established global-console destinations where applicable.
A future remote apply can use a non-forwarding sink plus typed reporting; adding that binding must not
change today's local USB/BLE behavior. If ota_start needs a supplied sink, retain its existing no-argument
API as a wrapper selecting the current sink and put the actual operation in one implementation. Do not
duplicate SoftAP/server startup or change HTTP upload, update verification, ota_stop or reboot-hook policy.
Preserve local OTA active→stop and inactive→start. The separate idempotent entry primitive remains available
for the later remote caller; merely preparing an entry does not start/stop a receiver or reset into DFU.

## 3. Fence

Permitted production paths: `src/firmware_commands.{h,cpp}`, `src/fw_context.h`, `src/fw_main.cpp`,
`src/device_ota.{h,cpp}` only for the explicit startup sink/binding, and a focused new
`src/firmware_action_effects.{h,cpp}` if needed. Prefer pure header helpers for grammar/types; if a new TU is
required, add it to each actual base build_src_filter in `platformio.ini`, deriving the inherited environments.
No file move bundled with new behavior. Do not extend device_fault.h's include ownership.

Tests/instruments: existing local command/sink tests, a focused pure grammar test if needed, the existing
console/inbox probes and their exact-source controls, and `tools/probe_deferred_actions/` for the actual
shared effect path if required. Mutation harness changes are only dependency/grammar/effect controls and
independently derived pins. Generated command inventory may change source/owning-function provenance only; all 204 semantic rows and
authority/transport/disruptive classifications must remain unchanged. B400 clarifies the necessary instrument
dependency: update `tools/gen_command_inventory.py` and its source-reader tests to follow the shared action
parsers and the existing confirmation predicate, verifying each caller hop. Preserve empty/missing/duplicate
and disconnected-source refusals and full-file coverage of every existing command file. The generic config
value-parser header is consumed only for its named confirmation predicate, with its enclosing feature gates.
This is source-provenance maintenance for the authorized refactor, not a new semantic policy or owner ruling. QA owns register/design/MEMORY/bench/brief landing.

OUT: all lib/core production changes, codec/reference bytes, remote guards/admission/executor behavior,
7b-3's state/status/counters/diagnostics/timer, B389 Node allocation, the 36 refused families' services,
new authority, NV formats, reset/wipe semantics, literal grammar fixes, simulator/anchors, controller work.
No product-visible verb, output line, delay or behavior changes in this refactor. Return a discrepancy to QA
instead of folding a latent local defect into P1.

Permitted preparation documents are both new September-15 plans, revised 7b-3 brief, author pre-check/evidence,
register B389–B398 entries and current dispatch/design/MEMORY/ledger/bench alignment, plus the codec brief's commit-status note. Preserve these and
all other tracked/untracked inputs. No reset, clean, commit or shared build/mutation overlapping coder edits.

## 4. Required executed proof

Drive the **real firmware_commands.cpp router/handlers** through existing host-fake probes, preserving local
text/JSON/USB/BLE outcomes and effect order. Existing wrapper stubs in probe_inbox_verbs are dispatch
evidence only; they do not execute fw_main's wipes, halt, debug gate, reset marks or OTA choice. Add executed
production-region extraction/compilation with fake hardware primitives for those board-owned functions,
using unique source boundaries and effective controls. Or compile the real owning TU where feasible.
State precisely which instrument compiles a whole TU and which compiles extracted production functions;
no copied handler, fake executor or source grep counts as the operation proof.

Cover exact factory confirm/space/refusal cases; both inbox failure combinations and NV failure with
non-short-circuited order; prep's learned-state→two wipes→halt→truthful output; sleep empty/on/off/off-prefix/
unknown tails and local powersave-disabled behavior; crash debug-off/usage/prefixes/backend branches,
hang/fault/reboot and flush/reset marking; OTA inactive/active/start-failed paths, existing local toggle and
idempotent entry; original sinks and no duplicate invocation. Capture complete output and operation logs
before/after the refactor. Board/hang/reset fakes must terminate the harness safely; they are not metal proof.
Controls must catch wrong grammar, repeated debug admission, second erase skipped, warning suppressed,
effect reordered, wrong backend/macro branch, sink escape and toggle substituted for entry.

## 5. Full gate, handoff and next base

Run behavior revision 4 §7's complete chain for this refactor: fresh native wrapper **and actual binary**;
current 94-array independent reference/old-89 identity/five controls; fresh normal/gateway simulator compile
provenance and 36 validated byte-identical streams; both ABI instruments and controls; six probes default
and --no-neg (including CLIENT arm); new effect probe likewise; tools full discovery with a real ELF/no hidden
skips; inventory write/bare/check and semantic comparison; authority plus six selftests; A0; DataType literal
check; both repositories' whitespace; deterministic base/final gateway then heltec_mobile under private
.pio-measure, and the census's six pinned environments. No other normal board pair. Preserve all warnings,
pristine ELFs and attribution; Node/resident RAM must stay unchanged. New transient stack costs are measured.

Derive changed configured TARGET_SRC **S** and historical/dependency **H**, run their union. H includes all
52 batteries/817 configured controls from the codec gate, plus any needed effect dependencies and new
controls. Do not repeat its reported 816 RED as a measurement. Only known sliceBmac M04/B342 remains
unusable, separately reported. Every new native RED must compile and fail an executed assertion. Preserve
source-restoration hashes and every worker baseline. D5 strict pins stay bare; D6 requires source-reader
audits after moves/comments and actual affected controls, including B391 X09.

Coder receipt: **`docs/superpowers/evidence/2026-09-15-radmin-slice7b3-p1.md`**. Record all sources, actual
HEAD/brief hash, complete new instruments, output/operation equivalence, measurements/failures/reruns,
selectors, hashes, limits and freeze. Include the exact line
**`PIN re-synced? YES — <independently derived base + additions = final>`**. QA independently reruns every
required instrument on that complete freeze before PASS. No new metal behavior is claimed by a refactor.

Standing STOP, verbatim:

> Any stream delta — STOP.

Also STOP on local output/grammar/effect-order drift, a remote disruptive action newly admitted, resident
state/RAM growth, unexplained flash/stack movement, unsupported backend accidentally green-compiling,
lost/unusable/vacuous controls beyond B342, out-of-fence changes or a mismatched/concurrent base.
The owner commits when they choose; no step waits for a commit. B394 closes only when these shared seams are implemented and gated;
QA then refreshes 7b-3 to that frozen input set (last commit plus SHA-256 inventory). The B389 allocation is ruled (R-RA-40, +80 B, no resident state in P1); the later behavior gate remains separate.

### P1 coder completion — 2026-09-15

The [coder receipt](../evidence/2026-09-15-radmin-slice7b3-p1.md#11-frozen-coder-handoff--2026-09-15)
records the completed implementation gate: native 2916/184587/0, corpus 36/36 byte-identical, tools 351/no
skips, full probes/reference/ABI/checkers/census, 826 RED plus known B342 across 53 batteries, unchanged
Node/resident RAM and measured flash/stack movement. B399/B400 are folded and await independent closure.
This is the coder's result, not independent QA PASS. The complete uncommitted freeze is the next QA input;
7b-3 behavior remains pending independent QA and the separate behavior brief refresh. No commit is a gate.

## 6. Independent QA gate — PASS (2026-09-16)

QA re-executed every §5 instrument on the committed tree `7442e6f` (freeze inventory matched): native
**2916/184587/0**; reference 94/94; corpus **36/36** with the simulator inert (0 build actions, identical `lus`);
ABI Node unchanged; six standing probes controlled + `--no-neg` + CLIENT arm; new deferred-actions probe
**150/151/158/158 checks, 39 transcripts each, 19 controls RED**; tools **351 OK / 0 skipped**; inventory 204
byte-identical; authority/A0/literals/whitespace; census at pins; board pair **gateway 203956 RAM (0) / 568220
flash (−2368); mobile 207756 (0) / 1373576 (+584)**; union **53 batteries / 826 RED / 1 known unusable B342 /
827 / 0 vacuous**. **Independent C1 proof:** pristine `c591721` handlers vs final, all four variants,
**156/156 transcript lines byte-identical**. Not independently reproduced: the coder's symbol-level flash
attribution and per-function stack frames. One LOW finding registered (B401, displaced include comment).
Evidence: `../evidence/2026-09-15-radmin-slice7b3-p1-qa-gate.md` + `…-p1-qa/`.
