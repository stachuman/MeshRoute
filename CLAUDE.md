# MeshRoute

C++ LoRa mesh firmware. **Before touching code, read `docs/CODE_GUIDELINES.md`** — how to keep the code clean and improve it one safe increment at a time.

## Working rules

Check the relevant **[TRIGGER]** group *before* acting; cite rules by ID to steer me fast (e.g. "per V1", "you skipped [DONE]"). ★ = the hard non-negotiables. This board is the always-loaded index; rationale + detail live in `docs/CODE_GUIDELINES.md`, project state in the `MEMORY.md` index. When a correction recurs, sharpen a rule here — one line, detail goes in CODE_GUIDELINES.

**[REUSE] — before writing code**
- U1 grep for an existing fn / helper / constant first; extend it — don't fork a parallel one (the S1/L9 field-drop rot)
- U2 one conversion path for the data carriers (`seed_blob_from_live`, `TxItem`/`PendingTx`) — never rebuild a carrier field-by-field
- U3 match the surrounding file's naming + idiom; `fw_main.cpp` stays board/runtime glue — feature logic goes in a `firmware_*` module

**[VERIFY] — before asserting a fact or acting on one**
- ★ V1 verify against the code (grep the codec/source) — never comments, `ROADMAP.md`, or a design doc; fix drifted comments you touch
- V2 recalled memory + specs are point-in-time — re-check the `file:line` before relying on it

**[CHANGE] — shaping an edit**
- ★ C1 refactor XOR feature/fix — never both; never fold a file-move into a semantic edit
- C2 fail loud — no unagreed fallback/default (empty `sf_list` → refuse to send, don't silently default)
- C3 respect the planes — a mobile/team local-id never writes a static `node_id`-indexed array; runtime-gate so a static build stays inert
- C4 don't bump `wire_version` casually — ⚠ **NOT for reflash cost: owner re-confirmed 2026-07-31 that MeshRoute is UNSHIPPED (test hardware only), so a bump is FREE to deploy and is never a reason to stop.** The real cost is **attribution: a bump re-anchors all 36 streams at once, so bundling it with a behaviour change makes both unmeasurable** ⇒ if you need one, give it **its own slice/commit**. Still prefer reusing an existing frame/field; guard optional state/verbs by `MR_FEAT_*`

**[DONE] — before you say "ready"**
- ★ D1 run the gate: native (`pio test -e native` then RUN `./.pio/build/native/program` — the wrapper lies "0 test cases"; the binary prints the real count, 0 failed) + s18 md5 EXACT + every board env, sequentially. **The current keystone md5 + per-scenario anchors live in `simulation/BASELINE.md` — read them there; NEVER hardcode or assume the value (it re-anchors when sim physics or lib/core legitimately changes; `3ac88d40` is retired).**
- D2 lib/core → the s18 md5 must reproduce the current `BASELINE.md` keystone (a `src/`-only change is inert by construction); a node.h reorder → `-Wreorder`-clean + `sizeof(Node)` assert + a per-board RAM diff (native alignment hides board padding)
- D3 report outcomes honestly — failures with their output; if a step was skipped, say so
- ★ D4 never `git commit` or offer to — leave green work uncommitted + report ready; the user commits + bench-verifies on metal
- D5 pins consumed by strict source readers stay bare `NAME=<integer>`; put derivations above them and run full tools discovery after runner edits (B377/B387)
- D6 comment-only returns still audit exact-source mutation/probe readers; re-run affected controls before inheriting gate attribution (B391)

**[PROCESS] — task shape**
- P1 present the exact code state *before* proposing (any check / redesign / explore)
- P2 big or risky → design spec first (`docs/superpowers/specs/`), reviewed before code — then distil its durable agreements into a `MEMORY.md` line so they don't rot in a doc I won't reopen
- P3 my role is yours to assign per task (QA-gate / spec / implement) — I won't drift into coding what the coder owns; when unsure, I ask
- P4 briefs pin by SYMBOL (function / struct / constant name); a `file:line` is a hint — the coder relocates by symbol without a STOP; STOP-1 is for a semantic disagreement only (owner 2026-09-16). A brief is FROZEN from coder preflight PASS to the implementation freeze: a mid-slice ruling goes to the ledger + register, and the brief is refreshed only at a checkpoint (preflight STOP or freeze) with an explicit re-pin message naming the new hash and the delta (2026-09-16, after B402/R-RA-41 caused a STOP)
- P5 status has TWO homes — the design's §19.1 row + the register (§0 + rows); `tracker.md` and `MEMORY.md` carry one-line pointers, never narrative; a brief = contract + fence + gate list, rulings by link, not verbatim (owner 2026-09-16)
- P6 slice granularity — C1/C4 still split refactor↔feature and wire bumps; a producer-free codec change rides INSIDE its feature slice (KATs + reference gate it); slices sharing one product surface that cannot move the corpus are PAIRED (8a+8c); a `src`-only slice's independent gate = native + corpus + boards + touched batteries + affected probes (full union/census stay in the coder gate); `lib/core`/wire = full gate on both sides (owner 2026-09-16)
- P7 fence completeness — a brief that ADDS a `lib/core` TU fences the simulator source list (`lora-universal-simulator/CMakeLists.txt`, one line, same slice — the stock `lus` must link the frozen tree); a brief that REMOVES a symbol fences EVERY user across lib/src/test/tools (grep first) (B405/B406)

**[MAINTAIN] — living documents, not one-off artefacts**
- ★ M1 **`docs/2026-07-30-open-bug-register.md` is MAINTAINED, not archived** — every finding lands there with its
  measurement, entries are closed in place (never deleted), and §0 is the dispatch contract a coder is handed. **A bug
  found and not registered is a bug found twice.**
- ★ M2 **`docs/2026-09-20-metal-test-plan.md` is MAINTAINED** — it holds only what **no automated gate can reach**
  (a different ABI, a file neither native nor the sim compiles, flash wear, real radio). **Every slice that adds a
  metal-only behaviour adds its check here**, with the exact expected console line. Keep it short; it is the residue,
  not a re-test of the corpus. Current results live in its ONE table; archive old evidence and retire a check only against a verified instrument.
- ★★ M3 **MeshRoute is NOT DEPLOYED — it runs only on the owner's test hardware. WIRE CHANGES ARE FREE.** Owner
  re-confirmed 2026-07-31 and 2026-08-01. ⇒ **never contort a design to fit a spare bit or dodge a `wire_version`
  bump** — that is exactly how the DATA flags byte (`0xFF`) and `q_opcode` (2 bits) both reached exhaustion. Pick the
  RIGHT shape. ⚠ The one residual cost is **attribution**: a bump re-anchors all 36 streams at once, so give it **its
  own slice/commit** (see C4).

## Map

- `docs/CODE_GUIDELINES.md` — code-quality discipline (read first).
- `simulation/BASELINE.md` — the mandatory test gate.
- `docs/protocol.md` — protocol behaviour & mechanisms; the "how it works" details live **here**.
- `docs/frames.md` — on-wire byte layout of every frame. Keep it **wire-oriented** (fields + byte offsets), not behaviour/rationale — that belongs in `protocol.md`. (It has already drifted toward prose; don't add to the drift.)
- `docs/firmware-dev-guide.md` — build / debug / upload mechanics.
- `docs/2026-07-04-codebase-review-triage.md` — cleanup plan/tracker.
- `lib/core/` — the protocol engine (compiled by the simulator → s18). `src/` — firmware integration: a deliberately-small `fw_main.cpp` (board/runtime glue) + `firmware_*` feature modules.
