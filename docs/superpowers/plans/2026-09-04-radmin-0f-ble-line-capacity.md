<!-- Author: OpenAI Codex -->
# Remote-admin v2 Slice 0f — derived BLE line capacity · dispatch brief · 2026-09-04

**Status: RE-ISSUED 2026-09-04 AFTER THE OWNER'S R-RA-24′ POLICY-B CORRECTION — AWAITING QUALITY-AGENT RE-GATE.** Dispatch model after PASS: **Opus**.
Authority: R-RA-24′ in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md` and §§12/19 of
`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md`. Pre-check input:
`docs/superpowers/plans/2026-09-04-radmin-0f-precheck.md`.

0f is one nRF52 BLE **capacity behavior change**. It replaces the 160-byte inbound line buffer with a
source-derived 275-byte buffer so BLE admits every canonical product line that can succeed. The binding maximum
is the 274-byte, three-hop `send_layer` form with its 226-byte depth-4 carrier body; the complete 239-byte DM
still fits in the canonical maximal `send` form. It does not add remote RPC, change the command parser, change a DM/frame cap,
change USB, or refactor the BLE intake.

## Contract and owner correction

The reviewed design text that launched the stopped run is kept visible below, but its 273-byte conclusion is
**withdrawn by R-RA-24′'s second same-day correction**. It remains historical evidence of the derivation defect,
not implementation authority:

> - **0f, BLE line capacity:** implement only R-RA-24′ bound 2. Replace `device_ble.h`'s 160-byte inbound line
>   buffer with one source-derived capacity that admits the longest canonical product line required by the ruling
>   (the earlier “longest syntactically legal” claim is withdrawn because whitespace/options can repeat):
>   `send 0xffffffff "<239>" -a -e -t -K -l` = 272 bytes, hence 273 bytes including NUL. Pin that the longest
>   legal remote wrapper plus its 201-byte tail also fits. Preserve the byte-at-a-time newline intake, loud
>   overflow refusal and USB behavior. A host wiring probe must execute the real BLE intake under several ATT
>   chunkings; native tests own parser/command-path boundaries; the ruled pair attributes the nRF52-only RAM
>   change. This capacity behavior change is not folded into 0c's dispatcher refactor.

The corresponding reviewed gate row is likewise superseded at its numeric boundary:

> | 0f | BLE line-capacity derivation and real-intake probe, `src/device_ble.h` | 36/36 unchanged; gateway RAM attributed, heltec byte-identical | **Part 61:** 272-byte line over real BLE under multiple write chunkings; 273-byte line refuses loudly |

R-RA-24′ still supplies the three-bound fence. Its first recorded bound-2 derivation is also kept visible:

> 1. `dm_max_body_bytes` = 239 stays the DM body authority, enforced where it is today (an over-long sealed body
>    refuses with `SealOutcome::too_large`).
> 2. `console_line_max_bytes` is PER TRANSPORT: USB 1024 (unchanged); BLE `g_line` grows 160 → **270**, DERIVED as the
>    longest syntactically legal local line + NUL. ⚠ QA CORRECTION (same day, after re-reading the flag grammar at
>    `lib/console/console_parse.cpp:119-124`, `:352-353`): the by-hash `send` also accepts `-K`, so the longest legal
>    line is `send 0xffffffff "<239>" -a -e -t -K -l` = **272** (not 269) and the derived buffer is **273** (not 270);
>    the ruling is the DERIVATION, so the number follows it (the brief `static_assert`s the buffer ≥ the grammar's
>    maximum; a bare literal is a structural RED). The longest legal `remote` line still fits (wrapper ≤ 60 + tail
>    201 + NUL = 262). The +113 B `.bss` lands on nRF52 builds only and is measured by the ruled pair (Slice 0f).
> 3. `remote_command_max_bytes` = the command TAIL after `--` = the smallest authenticated carrier's command capacity,
>    **201** today (0e: cross-layer by hash, depth 4, RPC cap 226 − 25), derived by Slice 2's `remote_body_cap` and
>    enforced by the ONE shared validator (R-RA-7: NUL/CR/LF + a length), which takes its bound per surface.

The current implementation authority is the later owner ruling, quoted verbatim from the ruling ledger:

> **Owner:** *"Agree - go with B."* ⇒ **policy B: the BLE line storage admits every canonical product
> line that can succeed = 274 + NUL = 275 bytes** (nRF52 `.bss` +115 B, ruled-pair measured). Policy A (288: also
> admit lines that only reach a named semantic refusal) is REJECTED — both refusals are loud and named, and A buys a
> different wording for `send_layer` bodies that can never be sent at any depth. Consequences: the derived
> expression keeps `send_layer` as a first-class term (`gw_env_max_hops`, the depth-4 carrier cap, its flag set) so a
> change moves the buffer automatically; 240/241-byte `send` bodies (273/274-byte lines) now reach the Node's
> `err_too_large` over BLE, which resolves the brief's 0f-3 contradiction; B288 (Author) records the derivation
> defect; the 0f brief is re-issued around 275; design §12/§19 and the companion contract state 275.

Only bound 2 changes production in this slice. Bounds 1 and 3 are regression/derivation pins; Slice 0f neither
moves their authorities nor anticipates Slice 2 or Slice 6.

## Required result

0f passes only when:

- the longest canonical `send` line is derived as 272 bytes from named grammar terms, including all five accepted
  flags `-a -e -t -K -l`;
- the longest canonical product line that can succeed is independently derived as the 274-byte three-hop
  `send_layer` form with its 226-byte depth-4 body and four accepted flags `-a -e -K -l`;
- `g_line` uses the maximum of the named `send`, `send_layer`, and transitional `remote` derivations plus one byte
  for NUL; no naked `275`, copied `239`/`226`, or second capacity formula controls
  admission;
- the longest legal `remote` line is independently derived as at most 261 bytes before NUL and statically proved
  to fit the same buffer;
- the exact 274-byte successful `send_layer` line reaches the supplied dispatch function byte-for-byte under 1-,
  20-, and 244-byte BLE writes, with the 272-byte maximal `send` form retained as a separate positive case;
- a 275-byte line dispatches nothing, emits exactly `{"err":"line_too_long"}\n`, consumes through newline, and
  leaves the next valid line usable;
- CR filtering, newline dispatch, empty-line suppression and the existing reply path remain unchanged;
- a full 239-byte plaintext DM reaches the real command path and queues or reaches its ordinary named routing
  result, while 240- and 241-byte DMs reach and refuse at the Node as `err_too_large`, and a full-length sealed DM reaches the semantic
  `too_large` refusal rather than BLE overflow;
- USB's 1024-byte buffer and overflow behavior are byte-for-byte unchanged;
- the production diff contains no BLE intake refactor, parser change, DM cap change, RPC implementation, frame
  change, companion code or documentation; and
- board measurements attribute the nRF52 RAM movement to the enlarged `g_line`, while the non-nRF52 ruled image
  remains byte-identical.

## STOP conditions

STOP and report before widening the slice if any of these occurs:

1. the full 239-byte DM cannot be admitted without changing `dm_max_body_bytes`, DATA packing, encryption,
   routing, command grammar or companion framing;
2. another non-debug product producer has a canonical line that can succeed above 274 bytes, or the source-derived
   `send_layer` maximum no longer equals 274 before NUL;
3. the longest R-RA-18 `remote` form plus its 201-byte tail does not fit the same derived buffer;
4. compiling or executing the real BLE intake requires a production extraction/refactor rather than a
   probe-local platform fake;
5. an ATT write boundary changes dispatch bytes, count, result, overflow behavior or reset state;
6. the 275-byte line is truncated, partially dispatched, split into commands, silently dropped, or emits the
   refusal more than once;
7. the full sealed line is rejected by BLE transport rather than reaching its existing semantic refusal;
8. `src/fw_main.cpp`, `lib/core`, `lib/console`, a frame/codec, `platformio.ini`, a feature macro, USB behavior,
   `simulation/BASELINE.md`, or companion production code must change;
9. corpus output moves, the simulator binary changes for this `src/`-only edit, `sizeof(Node)` moves, or
   `heltec_mobile` changes in any size/section/symbol/payload field;
10. gateway RAM movement is not completely attributable to `g_line` and necessary alignment, or production
    adds heap/dynamic allocation;
11. the new probe does not compile and execute the real `service_rx()` and `dispatch_current_line()` from
    `src/device_ble.h`, or a control can pass against a copy/model of the intake; or
12. any negative control is GREEN, vacuous, multi-matched or unusable and cannot be repaired within the
    authorized probe/tests.

## Fence and implementation choice

Allowed production edit:

- `src/device_ble.h`: the named grammar terms, derived line-capacity expression, compile-time relationship
  checks, `g_line` extent, and comments whose truth directly changes with that capacity.

The intake itself—`g_pos`, `g_overflow`, `dispatch_current_line()`, `service_rx()`, newline/CR handling and the
reply path—must not be refactored. This brief chooses a **probe-local `bluefruit.h` fake**, not a production
extraction: 0f is a capacity change, while an extraction would be a separate C1 refactor and commit. The fake
models only the Bluefruit/BLEUart API needed to compile and drive the real header. Reuse
`tools/probe_console_sink/fakes/Arduino.h`; do not mint a second Arduino fake. Any small nRF SDK stubs required
only to compile `device_rng.h` remain probe-local and contain no intake decision.

Allowed supporting edits:

- `tools/probe_ble_line/run.sh`;
- `tools/probe_ble_line/probe_main.cpp`;
- `tools/probe_ble_line/fakes/bluefruit.h` and only the minimum additional platform declarations needed to
  compile the real header;
- `tools/test_probe_ble_line.py`, auto-discovered by the standing tools sweep;
- existing native test files that own `console::parse_command` and `Node::on_command` boundary cases;
- `tools/probe_ui_model_mutations.py` only if an existing touched-file battery must be re-anchored—do not invent
  a battery for `src/device_ble.h` when the executed probe controls own that file; and
- `docs/superpowers/evidence/2026-09-04-radmin-0f.md`.

No coder edit belongs in `src/fw_main.cpp`, another `src/` file, `lib/`, `variants/`, `platformio.ini`,
`simulation/`, `ios-companion/`, wire/protocol/manual docs, the register, tracker, spec, bench script or memory.
Those documentation landings remain Author-owned after QG PASS. The only board environments are `gateway` and
`heltec_mobile`; the warning census's pinned environment set is the standing exception.

## 0f-1 — derive the storage authority from the grammar

Before editing, enumerate every non-debug product line producer that can carry a variable payload and calculate
its canonical maximum from source. Separately list permissive-parser forms and USB-oriented diagnostic grammars
(`testsend` in particular) as intentionally bounded by BLE transport rather than falsely assigning them a finite
   syntax maximum. Publish the full comparison table; `send_layer`, not `send`, is the ruled binding producer. The
   binding derivation is:

```text
send_line_max_bytes =
    len("send ")
  + len("0xffffffff")
  + len(" \"")
  + dm_max_body_bytes
  + len("\"")
  + len(" -a") + len(" -e") + len(" -t") + len(" -K") + len(" -l")
  = 272

send_layer_line_max_bytes =
    len("send_layer ")
  + len("0xffffffff")
  + len(" ")
  + len("255,255,255")
  + len(" \"")
  + send_layer_body_cap_at_max_depth
  + len("\"")
  + len(" -a") + len(" -e") + len(" -K") + len(" -l")
  = 274

ble_line_storage_bytes = max(send_line_max_bytes, send_layer_line_max_bytes,
                             remote_line_max_bytes, other_product_line_maxima) + 1
                       = 275
```

Bind the 239 term to `protocol::dm_max_body_bytes`; do not copy its value as an independent authority. Express
the 226 term from `gw_env_max_hops` and the depth-4 cross-layer-by-hash carrier calculation, cross-checked by
packing cap and cap-plus-one; do not copy it as an independent authority. Express fixed grammar terms with
`sizeof("literal") - 1` or equivalently reviewable named constants, so deleting or adding a flag changes the
expression. Pin the five accepted by-hash `send` flags and the four accepted `send_layer` flags. Add compile-time
assertions that storage is exactly maximum-plus-NUL and that the derived `send` and remote forms fit.

The remote derivation is a capacity pin, not an implementation of `remote`:

```text
remote_wrapper_max_bytes = 7 + 32 + 3 + 11 + 3 + 4 = 60
remote_line_max_bytes    = 60 + remote_command_max_bytes = 261
storage required         = 262 including NUL
```

No production authority for the management-target label width exists yet: the target-book record is deliberately
deferred to its own storage slice. Therefore **32 bytes is a transitional design pin**, asserted at compile time
for this capacity proof and explicitly labelled for replacement by the target-book record's eventual named
constant. It is not attributed to `device_nv.h`'s unrelated node-name field. Likewise, until Slice 2 lands the
production carrier function, 201 is a transitional compile-time mirror of R-RA-24′ bound 3 rather than a second
permanent carrier authority. The `len(" using=key9") == 11` term is grounded in design §6.1's exact controller
slot names `key0` through `key9`; a wider key-name grammar is outside this slice and would invalidate the assertion.

## 0f-2 — executed BLE intake gate

Create `tools/probe_ble_line/` in the standing probe idiom. Its runner must:

1. compile the real `src/device_ble.h` nRF52 arm against the shared Arduino fake and probe-local Bluefruit/Nordic
   surface;
2. call the real `mrble::begin()` with a dispatch spy so `g_started` and the production callback path are real;
3. feed the fake `BLEUart` incrementally, invoking real `service_rx()` after each selected chunk;
4. capture both dispatch arguments and BLE writes; and
5. print and enforce derived check/control counts, the compiled header hash, tree-integrity hash and a final
   PASS only in its default control-running mode. `--no-neg` must say `PROBE-ONLY — NOT A GATE` and never PASS.

Required positive cases:

- the same exact 274-byte successful `send_layer` line under chunk sizes 1, 20 and 244 produces one byte-identical
  dispatch each;
- the exact 272-byte maximal `send` line dispatches byte-identically as its own positive control;
- a single write, uneven final chunk and a chunk boundary immediately before newline behave identically;
- CRLF and LF forms produce the same line bytes, with CR absent;
- empty LF/CRLF dispatches nothing;
- a one-byte line dispatches once;
- a non-empty dispatch reply is written unchanged; and
- after an overflow refusal, the next valid line dispatches normally.

Required refusal case: 275 non-newline bytes followed by newline yields no dispatch and exactly one
`{"err":"line_too_long"}\n`, regardless of chunking. Continue consuming after the first excess byte; do not let
the remainder become a second command.

Default negative controls must independently prove at least:

- restoring 160 rejects the 274-byte positive case;
- replacing the derived extent with a naked literal fails the structural derivation check;
- admitting `g_pos == sizeof(g_line)` makes the boundary control RED;
- removing/bypassing the overflow refusal makes the 275-byte case RED;
- dispatching an overflowed prefix makes the no-dispatch assertion RED;
- failing to reset after overflow breaks the recovery case; and
- compiling a copied intake or a header other than the live tree fails the source-hash pin.

Controls run against isolated copies and prove the real checkout unchanged. Build failure is FAIL/UNUSABLE,
never a RED verdict. The auto-discovered Python wrapper pins that the runner, real-header include, default
controls, enforced counts, `--no-neg` wording and anti-vacuity behavior all remain present.

## 0f-3 — command-path boundaries

In native tests, separate syntax, transport and semantic capacity:

- `console::parse_command` accepts the exact 272-byte by-hash line, preserves a 239-byte body byte-for-byte and
  sets ACK, encryption, team, no-intro and location intent;
- `console::parse_command` accepts the exact 274-byte three-hop `send_layer` line, preserves its 226-byte body
  byte-for-byte and sets ACK, encryption, no-intro and location intent; the production command path reaches its
  ordinary queue/routing result rather than a size refusal;
- a plaintext production-shaped 239-byte DM reaches `Node::on_command` and does not return a size refusal;
- 240- and 241-byte `send` bodies are preserved by parsing as far as the existing parser permits and are refused
  by `Node::on_command` as `err_too_large`—the Node's 239-byte DM authority, not a newly invented parser limit;
- the 239-byte sealed/hash form reaches sealing and refuses through the existing named `too_large` outcome; and
- the same semantic cases below the old BLE line boundary remain unchanged.

Do not “fix” the parser's existing hard-cap/clamp behavior in 0f. The test records which layer owns each refusal
and prevents a transport overflow from impersonating it.

The layer-ownership table in the evidence must name all three authorities explicitly:

| Layer | Current authority | 0f obligation |
|---|---|---|
| BLE line transport | `src/device_ble.h` intake and overflow branch | move only the derived accepted-line edge; keep the refusal bytes and state machine |
| command parser | `lib/console/console_parse.cpp:110` clamps the quoted body to `max_payload_bytes_hard_cap == 241` | characterize and leave unchanged; never attribute this clamp to BLE |
| DM semantic admission | `lib/core/node.cpp:1570-1571` refuses body lengths above `dm_max_body_bytes == 239` | prove 239/non-size-refusal and 240/`err_too_large`; leave authority unchanged |

## 0f-4 — gates and attribution

Run and report:

1. clean native build and real test binary, deriving every case/assertion delta and re-synchronizing the PIN;
2. the new BLE probe with default controls, plus its direct auto-discovered Python test;
3. canonical corpus runner over all 36 ruled streams with required anchors; because no simulator source changes,
   rebuild must take zero relevant actions and every stream/hash/event count must remain identical;
4. full ABI probe, proving `sizeof(Node)` unchanged on host, ARM and Xtensa;
5. deterministic `tools/measure_board.py pair --jobs=2` for exactly `gateway` and `heltec_mobile`: publish
   RAM/flash/section/object/symbol fields, show `g_line` 160 → 275 in the gateway ELF, attribute all nRF52 RAM
   movement including padding, and require every heltec field byte-identical;
6. `tools/warning_census.sh` over its own pinned set, with no warning or pin movement hidden;
7. console-sink, firmware-UI, inbox-verbs and custody-USB probes at their enforced pins;
8. both DATA-type/matrix checkers and the full auto-discovered tools suite;
9. a source census proving USB's `line[1024]`, its refusal text, BLE's overflow text and the companion's ATT
   chunking code are unchanged; and
10. `git diff --check`, final fence audit, process cleanup and exact tracked/untracked inventory.

The owner-ruled +115-byte gateway prediction is a hypothesis until remeasured on the implementation. Report the actual `g_line` symbol delta and whole
image RAM delta separately; any remainder needs section/symbol attribution. No re-anchor is permitted or expected.

## Metal residue — Bench Part 61

The evidence must draft Part 61 but the coder does not edit the bench script. On a real nRF52 `gateway` with BLE
secured and the iOS companion or an equivalent NUS client:

1. establish a routable plaintext hash target and prepare an exact 239-byte ASCII body;
2. send the exact 274-byte successful `send_layer 0x<hash> 255,255,255 "<226>" -a -e -K -l` form and require the
   ordinary named queue/routing result, never `line_too_long`;
3. send the 239-byte sealed form and require its semantic size refusal, never `line_too_long`;
4. send the exact 274-byte `send_layer` line once as 20-byte writes and once with a 244-byte first write; require
   identical reply, and separately send the 272-byte maximal `send` line;
5. send 275 non-newline bytes plus newline and require exactly `{"err":"line_too_long"}` with no command side
   effect; and
6. immediately send a short valid command and require success, proving overflow recovery.

The draft must give exact reproducible commands or a checked helper invocation, body byte-count checks, expected
NDJSON/ack lines, and a USB observation proving the LoRa send/refusal consequence. B278 Part 54 and remote-admin
RPC metal remain separate.

## Held Author landings

The evidence file must contain ready-to-land drafts for:

- `ios-companion/INBOX_SYNC_CONTRACT.md`: BLE NUS writes may be ATT-chunked arbitrarily; firmware accumulates one
  newline-delimited line up to 274 bytes, reserves byte 275 for NUL, and refuses 275 payload bytes as
  `line_too_long`; this permits the full 239-byte DM body;
- this design §12: R-RA-24′ bound 2 marked implemented without changing bounds 1 or 3;
- this design §19 and gate table: Slice 0f marked landed with actual native/board/probe evidence; and
- Bench Part 61 as above.

B288 records the stopped run's distinct derivation defect and the owner's policy-B correction. If the resumed
work exposes another distinct defect or gate gap, draft a new numbered register row under the normal M1
discipline rather than hiding it in prose.

## Durable evidence and report shape

The one durable report is:

`docs/superpowers/evidence/2026-09-04-radmin-0f.md`

It must contain:

- starting HEAD and current source census, with all pre-check line numbers re-resolved;
- complete variable-line grammar table and term-by-term 272/273 (`send`), 274/275 (`send_layer`), and 261/262
  (`remote`) derivations;
- production diff and proof that the intake logic itself is unchanged;
- every native case and its layer-owned outcome;
- BLE probe architecture, positive cases, enforced counts and every sabotage result;
- corpus, simulator action/hash, ABI, board, warning, probe, checker and tools results;
- exact `g_line` symbol and total RAM attribution on both ruled boards;
- every STOP condition evaluated explicitly;
- Part 61 and held documentation drafts;
- the exact line `PIN re-synced? YES — <derivation>`; and
- final `git status --short`, separating pre-existing work and naming every untracked file requiring explicit
  `git add`.

Every figure is derived by the coder. The pre-check's line numbers, 275-byte result, +115-byte RAM prediction,
board totals and test counts are hypotheses or owner-rule cross-checks, never copied as measured results. No owner
ruling is expected unless a STOP fires.
