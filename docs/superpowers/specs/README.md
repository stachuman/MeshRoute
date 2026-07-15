# Design specs

Per-feature design records. **Once a feature ships, the code is the source of truth** — these capture the *why/how at design time* and are **not** maintained against the code afterward (the frozen-baseline principle). Read a spec for intent and history; read the code for what actually runs.

## Living / in-progress (top level)

The specs that are **not yet fully shipped** — in progress, deferred, parked, or forward-looking:

- [mobile-node-handling-assumptions](2026-07-07-mobile-node-handling-assumptions.md) — the living mobile-node assumptions doc (`[DECIDED]` / `[OPEN]` items)
- [protocol-plane-separation](2026-07-10-protocol-plane-separation.md) + [addendum-reaudit](2026-07-10-plane-separation-addendum-reaudit.md) — plane separation; the **PlaneRuntime per-plane leak *fix* is still deferred** (semantic, RAM-costed)
- [code-cleanup-safe-plus-failloud](2026-07-10-code-cleanup-safe-plus-failloud.md) — parked cleanup backlog
- [companion-product-roadmap](2026-06-12-companion-product-roadmap.md) · [companion-ui-redesign-and-node-directory](2026-07-02-companion-ui-redesign-and-node-directory.md) — forward product roadmaps
- [firmware-commands-seam-design](2026-07-15-firmware-commands-seam-design.md) · [node-legibility-design](2026-07-15-node-legibility-design.md) — the most recent structural cleanup (shipped; kept as recent reference — the node-legibility spec holds the reusable **`lib/core` reorg gate methodology**: lus-rebuild s18 + per-board `.bss` diff + `sizeof(Node)` tripwire + `-Wreorder`)

## Historical ([archive/](archive/))

Everything else — 97 specs for shipped features, now historical records. The parallel [`docs/specs/archive/`](../../specs/archive/) holds the 30 May–June codec/routing/HAL foundation specs.

**Convention:** a spec moves to `archive/` once its feature ships; new design work starts a new top-level spec here.
