# MeshRoute — code guidelines for agents

Short, load-bearing rules for working on this firmware. Two goals: **every change leaves the code at least as clean as it found it**, and quality **improves one safe increment at a time**. Distilled from the 2026-07 cleanup arc (fw_main split, node.h legibility, module extractions). For build/flash/debug mechanics see `firmware-dev-guide.md`; for the required test gate see `simulation/BASELINE.md`.

## Prime directives

1. **Behavior-preserving by default.** A change is either a *refactor* (no behavior change — prove it) or a *feature/fix* (tracked as such). Never smuggle a behavior change into a "cleanup," and never mix a file move with a semantic edit in one increment.
2. **Code is truth — verify against it.** Comments, `ROADMAP.md`, and design docs drift. Before relying on a claim (a frame field, a flag, a "spare" bit), grep the actual codec/source. Fix comments that have drifted from the code you touch.
3. **The user commits and bench-verifies.** Leave green, reviewed work uncommitted and report it's ready — don't `git commit`. On-metal verification is the user's.

## Keep it clean

- **A helper exists → use it.** The recurring rot here is a helper that call sites bypass and hand-roll. Keep one conversion path for the data carriers (`TxItem`/`PendingTx`/`PostAck`, the NV `Blob` seed via `seed_blob_from_live`) — never rebuild a carrier field-by-field at a new site. That is the S1/L9 field-drop bug class.
- **`fw_main.cpp` is board/runtime glue only** — startup, loop, radio poll, sleep, transports, board-coupled actions (reboot/OTA/fault). Feature logic lives in a `firmware_*` module (`firmware_config` / `firmware_remote` / `firmware_inbox` / `firmware_commands`). Don't grow fw_main; extend or add a module.
- **Fail loud; no unagreed fallbacks.** Invalid/empty config refuses (e.g. empty `sf_list` → refuse to send), it does not silently default. Surface the error.
- **Feature-gate by profile.** `MR_FEAT_*` (derived from `MR_PROFILE_*`) compiles a feature out; the public API supplies inert stubs so callers stay stable. Guard new optional state/verbs the same way.
- **Respect the planes.** Static / mobile / team are separate id-namespaces (a static `node_id`, a mobile `local_id`, and a team `local_id` can be numerically equal). Mobile/team ids must never be written into static-plane `node_id`-indexed arrays/ledgers. Guard mobile/team code (`is_mobile` / `team_id`) so a static build is inert — s18 proves it.
- **The seam rules.** Cross-module shared globals are `extern` in `fw_context.h`, defined once in `fw_main.cpp`. `device_fault.h` is **single-TU** — its ISR/exception vectors *and* the `MRFAULT_HW`/`MRFAULT_ESP32` macros are defined inside it; never `#include` it in a second TU. Reach the WDT/reset via the `fw_*` wrappers. A macro-guarded function moved to a device_fault-free TU **green-compiles the `#else` branch = a silent runtime regression** no build or test catches.
- **Don't bump `wire_version` casually.** A bump forces the whole fleet to reflash together. Batch wire changes; prefer a fix that reuses an existing frame/field (e.g. the targeted `j_deny` already carries a claimant hash) over one that grows the wire.
- **Docs have a home.** On-wire byte layouts (fields + offsets) go in `docs/frames.md` — keep it strictly wire-oriented. Protocol behaviour and rationale ("how it works") go in `docs/protocol.md`. Never add behaviour notes or TODOs to `frames.md` (it has drifted — don't feed it).

## Improve gradually

- **Small, reviewable slices.** One cohesive increment per step, each independently gateable. For a big or risky area write a **design/scoping spec first** (`docs/superpowers/specs/`) and get it reviewed before moving code — the hardest clusters (device_fault-coupled, friend-seams, plane redesigns) earn a design pass.
- **Isolate the risky axis.** Land a wide mechanical change (e.g. a `static→extern` seam) as its *own* increment, so the diff is trivially auditable, before moving anything onto it.
- **Extract by cohesion, keep by co-location.** A shared type used across TUs → its own header. A struct that lives next to its one member → leave it (co-location *is* legibility). "Safe to move" ≠ "worth moving."

## The gate — run before declaring an increment done

- **native:** `pio test -e native` **then run** `./.pio/build/native/program` — the pio wrapper misreports "0 test cases"; the binary prints the real doctest count (0 failed required).
- **s18 — the `lib/core` behavioral oracle:** rebuild lus, run `lus -e meshroute simulation/s18_meshroute.json out.ndjson`, `md5sum out.ndjson` must equal the baseline in `simulation/BASELINE.md`. A change under `src/` is s18-inert *by construction* (the sim compiles `lib/core`, not `src/`); a change under `lib/core` **must** reproduce the md5, or it changed behavior — that is the tripwire.
- **boards:** build **every** profile env, **sequentially** (a new `.cpp` must be added to the three base `build_src_filter`s; the rest inherit via `extends`). Sequential dodges the known `.pio` nRF52 parallel-race false-fail.
- **verbatim moves:** a normalized diff of each moved function vs the pre-move commit must be **empty** (only `static`/namespace/documented-wrapper deltas). Baseline against an explicit SHA, not `HEAD` (it moves when the user commits mid-review).
- **member reorders / layout changes (node.h):** `-Wreorder`-clean (heed it, never `-Wno-reorder`); the `sizeof(Node)` static_assert holds; **and a per-board `RAM_used` diff vs baseline** — native's 8-byte alignment structurally *hides* a 4-byte-align board padding shift, so native + s18 + a green board build are all blind to RAM bloat.

## Pointers

- `simulation/BASELINE.md` — the mandatory test gate (delivery suite + mobile scenarios + s18 byte-identity).
- `docs/2026-07-04-codebase-review-triage.md` — the cleanup plan/tracker.
- `docs/firmware-dev-guide.md` — build, debug, upload mechanics.
- `docs/superpowers/specs/` — design specs; label superseded ones archival rather than deleting the history.
