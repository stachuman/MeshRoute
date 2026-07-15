# MeshRoute

C++ LoRa mesh firmware. **Before touching code, read `docs/CODE_GUIDELINES.md`** — how to keep the code clean and improve it one safe increment at a time.

## Non-negotiables

- **The user commits and bench-verifies.** Leave green, reviewed work uncommitted and report it's ready — never `git commit` or offer to. On-metal/hardware verification is the user's.
- **Behavior-preserving by default.** A change is a *refactor* (prove no behavior change) or a *feature/fix* (tracked as such) — never both, and never mix a file move with a semantic edit. Verify claims against the code (grep the codec/source), not comments or `ROADMAP.md`.
- **Run the gate before declaring an increment done:** native (`pio test -e native` **then run** `./.pio/build/native/program` — the pio wrapper misreports "0 test cases"; the binary prints the real count), **s18 byte-identity** (the `lib/core` behavioral oracle — md5 in `simulation/BASELINE.md`; a `src/`-only change is s18-inert by construction), and **every** board/profile env built **sequentially**. Full checklist in `docs/CODE_GUIDELINES.md`.

## Map

- `docs/CODE_GUIDELINES.md` — code-quality discipline (read first).
- `simulation/BASELINE.md` — the mandatory test gate.
- `docs/protocol.md` — protocol behaviour & mechanisms; the "how it works" details live **here**.
- `docs/frames.md` — on-wire byte layout of every frame. Keep it **wire-oriented** (fields + byte offsets), not behaviour/rationale — that belongs in `protocol.md`. (It has already drifted toward prose; don't add to the drift.)
- `docs/firmware-dev-guide.md` — build / debug / upload mechanics.
- `docs/2026-07-04-codebase-review-triage.md` — cleanup plan/tracker.
- `lib/core/` — the protocol engine (compiled by the simulator → s18). `src/` — firmware integration: a deliberately-small `fw_main.cpp` (board/runtime glue) + `firmware_*` feature modules.
