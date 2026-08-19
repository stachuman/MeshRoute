<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §UI-15 slice 2 — CORRECTIONS after a QG HOLD · dispatch brief · 2026-08-19

**Status: DISPATCHED. Three correctness blockers, all verified at the code.** ⛔ **NO DEVICE CONTACT.**
★ The slice's own work is sound — the pure service, the eight-cell matrix, coalescing and the non-finite guard all
passed. **These three are at the boundaries the service trusts.**
⛔ Build on the uncommitted slice 1 + slice 2 tree; do not revert or re-derive either.

---
## Blocker 1 — ⛔⛔ A STORAGE FAILURE IS REPORTED AS "NO PROFILES"
The service's tri-state is correct, but **the primitive beneath it cannot supply the third state**:
- **nRF52** `read_slot` (`src/device_nv.h:455-462`): `InternalFS.begin();` — ⛔ **return value ignored** — then
  `if (!f.open(...)) return -1;   // absent slot (first boot) — not an error`.
- **ESP32** (`:531`): `if (!p.begin(s.ns, true)) return -1;` — a **backend open failure** returns the same `-1`.
- `join_blob_state` (`:330`): `if (n == kSlotAbsent) return JoinRead::absent;` — **unconditionally**.
⇒ **a broken filesystem or an unopenable NVS namespace announces `NO PROFILES`** — the exact honesty requirement this
slice exists to satisfy, defeated one layer down.

★ **Required: the primitive must distinguish RECORD-NOT-FOUND from STORAGE-FAILURE.**
⚠⚠ **AND THIS IS A SHARED PRIMITIVE — `read_slot` also serves `/mrcfg`, `/mrid`, `/mrpeers`, `/mrfault`.** ⛔ **Do NOT
casually widen its contract**: those callers have their own load semantics and a silent meaning-change there is far
worse than the bug. **Two acceptable shapes — pick one and say why:**
(a) a **distinct read path for `/mrjoin`** that reports the third state; or
(b) an **out-param / sentinel on `read_slot`** that existing callers ignore unchanged.
⛔ **The test must exercise the REAL distinction** (an open/begin failure), not a synthesised return value.

## Blocker 2 — ⛔ AN OVERSIZED `/mrjoin` IS ACCEPTED, AND THE TEST THAT "COVERS" IT IS VACUOUS
`read_slot` reads **only `len` bytes** (`:460`), so a **longer** file returns exactly `sizeof(JoinBlob)`; if the
**prefix** carries a valid magic/version, `join_blob_state` accepts it. ⛔ There is **no file-size check**.
★★ **And the native oversize test feeds an artificial over-long length the real backend can NEVER return** ⇒ it proves
nothing about the shipped path. **That is the nineteenth instrument in this arc that could not fail.**
⇒ **check the ACTUAL file size on nRF52 before accepting**, and **rebuild that test so it reflects what the backend can
really produce.**

## Blocker 3 — ⛔ THE CONSOLE VIOLATES ITS OWN MALFORMED-INDEX RULE, WITH NO COVERAGE
`atol` at both sites (`src/firmware_config.cpp` — the `clear` and `set` arms) ⇒ **`clear 2junk` is accepted as slot 2**
and **`set 1x …` as slot 1**. And `list extra`, `clear 1 extra`, `reset confirm extra` **silently ignore trailing
tokens**. The brief required: *a malformed or out-of-range index refuses loudly and writes nothing* (C2).
⇒ **strict decimal parsing** (whole token consumed, digits only) **+ reject unexpected trailing tokens.**
★★ **AND IT NEEDS RUNNABLE COVERAGE: `src/firmware_config.cpp` is outside the native suite and NO probe covers
`joinprofile`.** ⇒ ★ **Extract the strict index/tail parse into the PURE header `src/firmware_config_parse.h` so the
native suite tests it directly** — the same move that made [[B212]]'s classifier provable, and the pattern this arc has
now validated twice. A structural probe alone cannot show what a malformed token *does*.

---
## Pins
1. **A simulated backend open/begin failure reports STORAGE FAILURE, ⛔ never `NO PROFILES`** — and the fake must fail
   the way the real backend fails.
2. **An oversized file is REJECTED** (`invalid`), driven by a **real over-length file**, not a synthesised length.
3. **`clear 2junk` · `set 1x …` · `clear 0` · `clear 5` ⇒ refused, ZERO writes.**
4. **`list extra` · `clear 1 extra` · `reset confirm extra` ⇒ refused**, ⛔ not silently accepted.
5. **The eight-cell matrix still holds** — ★ *unchanged positive controls, must stay GREEN.*
6. ⛔ **`/mrcfg`, `/mrid`, `/mrpeers`, `/mrfault` behaviour is UNCHANGED** — assert it if you touch `read_slot`.

**Control rule:** ★ defect-specific regressions must **FAIL** under a controlled mutation; ★ unchanged positive
controls stay **GREEN**.
ⓘ ★★ **Re-pin `BASE_CASES/BASE_ASSERTS` in `tools/probe_ui_model_mutations.py` if the native counts move** — a stale
pin **ABORTS every battery without applying a mutation** (`:1039-1043`), which is **[[B217]]**, registered because
slice 1 did exactly that and silently disarmed all four targets. **Confirm the battery actually RAN.**

## Gate
Baselines: native **1754 / 84519 / 0** · four probes at their current pins · census **174/178/178**, `-Wswitch` 0 ·
`lus` `b77cfd3d` · s18 **`9868cad3` / 269905** · `sizeof(Node)` **221880**.
1. `pio test -e native`, **then RUN the binary**. 2. All four probes. 3. ✅ census (uncapped, owner-ruled).
4. Four-step corpus — a moved row ⇒ **STOP**. 5. ★★ **TWO ENVS ONLY**, same directory ([[B206]]).
6. **D2:** `sizeof(Node)` 221880, `kCap` 91, `git diff -- lib/` **empty**.

**Report:** which shape you chose for blocker 1 **and why** · the size check · the strict parser and **where it lives** ·
each pin with its control and match count · proof the four other records are unchanged · the full gate · exact final
`git status --short` · the M1/M2 text you owe.
⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ Do not touch the register, the bench script, any
plan/brief, `tracker.md`, `B164.md` or `docs/manual/`.
⛔ **Stop and report** if blocker 1 cannot be fixed without changing the shared contract for the other four records.
