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
- C4 don't bump `wire_version` casually (forces a fleet reflash) — reuse an existing frame/field; guard optional state/verbs by `MR_FEAT_*`

**[DONE] — before you say "ready"**
- ★ D1 run the gate: native (`pio test -e native` then RUN `./.pio/build/native/program` — the wrapper lies "0 test cases"; the binary prints the real count, 0 failed) + s18 md5 EXACT + every board env, sequentially. **The current keystone md5 + per-scenario anchors live in `simulation/BASELINE.md` — read them there; NEVER hardcode or assume the value (it re-anchors when sim physics or lib/core legitimately changes; `3ac88d40` is retired).**
- D2 lib/core → the s18 md5 must reproduce the current `BASELINE.md` keystone (a `src/`-only change is inert by construction); a node.h reorder → `-Wreorder`-clean + `sizeof(Node)` assert + a per-board RAM diff (native alignment hides board padding)
- D3 report outcomes honestly — failures with their output; if a step was skipped, say so
- ★ D4 never `git commit` or offer to — leave green work uncommitted + report ready; the user commits + bench-verifies on metal

**[PROCESS] — task shape**
- P1 present the exact code state *before* proposing (any check / redesign / explore)
- P2 big or risky → design spec first (`docs/superpowers/specs/`), reviewed before code — then distil its durable agreements into a `MEMORY.md` line so they don't rot in a doc I won't reopen
- P3 my role is yours to assign per task (QA-gate / spec / implement) — I won't drift into coding what the coder owns; when unsure, I ask

## Map

- `docs/CODE_GUIDELINES.md` — code-quality discipline (read first).
- `simulation/BASELINE.md` — the mandatory test gate.
- `docs/protocol.md` — protocol behaviour & mechanisms; the "how it works" details live **here**.
- `docs/frames.md` — on-wire byte layout of every frame. Keep it **wire-oriented** (fields + byte offsets), not behaviour/rationale — that belongs in `protocol.md`. (It has already drifted toward prose; don't add to the drift.)
- `docs/firmware-dev-guide.md` — build / debug / upload mechanics.
- `docs/2026-07-04-codebase-review-triage.md` — cleanup plan/tracker.
- `lib/core/` — the protocol engine (compiled by the simulator → s18). `src/` — firmware integration: a deliberately-small `fw_main.cpp` (board/runtime glue) + `firmware_*` feature modules.
