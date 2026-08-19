<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B214 — `cfg`'s `mobile-reg:` must report the FSM state, not the presence of a home id · dispatch brief · 2026-08-18

**Status: DISPATCHED 2026-08-18.** ★ Role split: the QA-gate wrote this brief and verifies your claims at the code;
**the OWNER runs QG and rules.** ⛔⛔ **NO DEVICE CONTACT** — native, host probes and `pio run` builds only.

## Read first
`CLAUDE.md` + `docs/CODE_GUIDELINES.md` · the **B214** row of `docs/2026-07-30-open-bug-register.md` (the metal
reproduction, the authoritative mapping and five pins — **it is the authority on scope**).

---
## 1 — The defect (verified, metal-confirmed)
`src/firmware_commands.cpp:308-310`:
```cpp
const uint8_t h = g_node.mobile_home_id();
out.print(F("  mobile-reg: "));
if (h) { out.print(F("REGISTERED home=")); out.println(h); } else out.println(F("UNREGISTERED (scanning)"));
```
It decides from **the presence of a home id alone** and ⛔ **never reads `mobile_attach_state()`**. Two consequences,
and the second is the worse one:
- **False "scanning":** metal 2026-08-18 printed `UNREGISTERED (scanning)` while `mobile status` on the same node read
  `"attachment":"dormant"`, `home_desired:false`, `retry_window_ms:0` — **nothing was scheduled.** ⚠ It made the owner
  ask how often the node would probe; the answer was *never*. **The question was manufactured by the false label.**
- ★★ **False POSITIVE:** **`claiming` already holds a PROVISIONAL home id**, so this prints
  **`REGISTERED home=<id>` before roster confirmation** — the console claims a registration that is not confirmed.

---
## 2 — What to build
**Derive the label from `mobile_attach_state()`** (`lib/core/node.h:592`) and format with the **existing**
`Node::attach_state_name()` (`:553`). ⛔ **Do not invent a second spelling** (U1) — and `node.h:550` warns never to
rewrite these as if-chains, because **`-Wswitch` cannot see those**.

| state | output |
|---|---|
| `attached` | `REGISTERED home=<id>` |
| `dormant` | `UNREGISTERED (dormant)` |
| `seeking` | `UNREGISTERED (seeking)` |
| `claiming` | `UNREGISTERED (claiming)` |
| `recovering` | `UNREGISTERED (recovering)` |

★★ **`attached` WITHOUT a home id must FAIL VISIBLY as inconsistent** — ⛔ never fall through to looking
unregistered, which is precisely how a broken attachment would hide. Emit something unmistakable (e.g.
`REGISTERED home=? (INCONSISTENT: attached with no home id)`).

ⓘ **No `#if MR_FEAT_MOBILE` needed:** the site is already inside `if (c.is_mobile)`, and the accessor has a
`MR_FEAT_MOBILE 0` stub returning `dormant` (`node.h:623`).

⛔ **OUT:** the mobile FSM itself · `mobile status`'s JSON (already correct) · any other `cfg` line.

---
## 3 — Pins (from the register row — normative)
1. `autoregister=0`, no session ⇒ ⛔ **must NOT say "scanning"** (says `dormant`).
2. an explicit `mobile register` outstanding ⇒ says **`seeking`**.
3. ★★ **`claiming` ⇒ must NOT say `REGISTERED`** — the false positive.
4. **`recovering` renders** — ⛔ not a default/fallthrough.
5. **`attached` with no home id ⇒ reports an inconsistency**, not "unregistered".

⚠ **Where coverage lives:** `src/firmware_commands.cpp` is compiled by neither the native suite nor the simulator ⇒
**structural**. ★ **`tools/probe_console_sink` ALREADY READS THIS FILE** (`structural.py`, `negctl.py`, `probe_main.cpp`)
— extend it there (U1), ⛔ do not start a new probe. The five state names are already natively covered via
`attach_state_name`; ⛔ do not duplicate that.

**Controls that must go RED:** the old `if (h)` gate restored · `claiming` mapped to `REGISTERED` · `recovering`
dropped to a default · the inconsistency arm removed · the label hand-spelled instead of via `attach_state_name`.

**Control rule:** ★ defect-specific regressions must **FAIL** against the current implementation or under a controlled
mutation; ★ unchanged positive controls must stay **GREEN**. ⛔ Not "everything must go red".
ⓘ **Prefer a counted/measured discriminator** — fifteen instruments in this arc were green against the defect they
were written to catch. Here: count occurrences of each label and of `attach_state_name`, not merely "it compiled".

---
## 4 — Gate
Baselines: native **1722 / 84041 / 0** · `probe_prov_tx` **19/19 + 40 controls RED** · `probe_board_ui` 120/120 +
14/14 + 52/52 + **153** RED · `probe_firmware_ui` 229/229 · `probe_console_sink` PASS · census **174 / 178 / 178**,
`-Wswitch` 0 · `lus` `b77cfd3d` · s18 keystone **`9868cad3` / 269905** · `sizeof(Node)` **221880**.

1. `pio test -e native`, **then RUN `./.pio/build/native/program`**.
2. **All four probes** with their control sets.
3. ✅ **`warning_census.sh` — RUN IT.** Owner-ruled and re-confirmed: the census stays as is and is **not** capped by
   the two-env limit. ⛔ Do not re-litigate. ★ `-Wswitch` **0** matters here: a `switch` over the five arms is the
   required shape, so a missing arm must surface as a warning.
4. **Four-step simulator proof.** `src/`-only ⇒ inert by construction — **prove it**; ⛔ no anchor-table edit.
5. ★★ **BOARD BUILDS: TWO ENVS ONLY.** ⛔ Not pre-authorised to escalate. `sizeof(Node)` from a **compile-only
   `static_assert` probe**. ⚠ [[B206]]: both arms in the **same directory**; deltas under ~32 B are noise; and
   ⛔ **a `/tmp` worktree is INVALID for flash measurement** (a longer path was measured to shift flash ~80 B).
6. **D2:** `sizeof(Node)` 221880, `kCap` 91, `git diff -- lib/` **empty** (this slice is `src/`-only).

**Report:** the new label logic with `file:line` · that `attach_state_name` is reused and the dispatch is a `switch` ·
each pin with its control and match count · native baseline → after · all four probes · census · the four-step result ·
the two envs · the D2 answer · exact final `git status --short` confirming nothing was committed · and the M1/M2 text
you owe (⇒ a metal line for the `dormant`/`claiming` labels belongs in the bench script).

⛔ **NEVER `git commit` / `git add` / `git checkout --`.** ⛔ **Do not touch** the bug register, the bench script, any
brief, `tracker.md`, or `docs/2026-08-11-mobile-home-metal-test-guide.md` — report owed M1/M2 text instead.
⛔ **Stop and report** if the label cannot be derived without touching the FSM, or if a corpus row moves.
