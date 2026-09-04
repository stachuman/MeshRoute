<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0a — Quality-Agent pre-check ledger (2026-09-04): B208, the bounded help-topic split

Authority: design §19 Slice 0a; register row B208 (owner ruling 2026-08-17 quoted there). Verified at `HEAD 0ee0f9b`.
Every `file:line` moves; re-verify before quoting (V2). Hypotheses, not authority.

## 0. What 0a is, in one line

Replace the monolithic `help` with a compact index + `help <topic>` sections, each fitting the 2048-B guarded console
stage with zero `CONSOLE_DROP`, without enlarging the stage, adding a pager or bypassing `GuardedConsole`
(the owner's ruling, verbatim in B208). `src/`-only ⇒ corpus-inert; a USB-only observable ⇒ bench part owed (M2).

## 1. Source facts

| fact | anchor |
| --- | --- |
| the monolith: `dump_help(Print& out)`, 89 `println` lines, ≈ 6177 B of `F()` text (+CRLF) | `src/firmware_commands.cpp:994-1100` |
| the stage: `MR_CONSOLE_STAGE_BYTES` 2048; whole lines dropped, reported once as `!! CONSOLE_DROP lines=<N>`; the file itself records that a 6400-B stage would deliver all of `help` at +4.4 KB `.bss` — the ruling forbids that trade | `src/console_sink.h:45-65`, `:78-93` (`GuardedConsole`) |
| today's headings: `MESSAGING`, `IDENTITY / KEYS`, `INBOX`, `DIAGNOSTICS`, `TEST` (five) — the ruling names NINE topics: `messaging, identity, mobile, inbox, diagnostics, remote, test, provisioning, cfg` ⇒ four topics (`mobile`, `remote`, `provisioning`, `cfg`) are carved out of existing sections, not new text | `firmware_commands.cpp:997/1011/1038/1063/1079` |
| feature gates already inside the help: the `ui` lines (`MR_FEAT_OLED`), `rcmd`/`password`/`unlock`/`lock` (`MR_FEAT_REMOTE_MGMT`), `mobile …` (`MR_FEAT_MOBILE`), `create`/`join` (`MR_N_LAYERS`) — the ruling: gated topics are listed/accepted only when compiled | `firmware_commands.cpp:1071`, the gates around `dump_help` lines |
| `help` and `?` share one arm; `help <topic>` does not exist yet (the arm matches `len == 4` only) | `:1188` |
| BLE refuses `help` as `console_only` (JSON `write_err`), pinned by the console-sink probe's structural check ⇒ the split is a SERIAL surface; BLE behaviour must not change | `src/fw_main.cpp:588-589`, `tools/probe_console_sink/run.sh:91` |
| the generated inventory (Slice 0e) lists every verb with its gate; it is the completeness oracle for "no command disappears" | `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` (177 rows) — ⚠ 0a moves lines in `firmware_commands.cpp`, so it REGENERATES the inventory with `--write` (0e's standing rule) |

## 2. Shape the brief must pin

- Index: bare `help`/`?` print only the topic index (with the build's compiled topics) — must fit the stage.
- `help <topic>`: exactly one complete section; each section's byte length measured and asserted `< 2048` incl. CRLF
  and the guarded console's own framing; the largest section is the binding number (hypothesis: `MESSAGING`, the
  `send` family's five flag lines).
- Unknown topic: usage + the valid topic names; `help` on BLE unchanged (`console_only`).
- Structural coverage: every `F("…")` help line of the monolith appears in exactly one section (a test that unions
  the nine sections' lines and compares to the pre-split line set, minus the five old headings, plus the four new).
- Wiring gate: the console-sink probe (`tools/probe_console_sink/`) executes the real `GuardedConsole`; add the
  arm that drives `help` and each `help <topic>` through it and asserts `CONSOLE_DROP` absent and the byte count.

## 3. Gate and fence

- Fence: `src/firmware_commands.cpp` (`dump_help` → index + sections; the `help` arm gains the topic argument),
  the probe, a native test for the section/line-set contract (a `test/` TU that compiles `firmware_commands.cpp`'s
  help unit? — today `src/` is outside the native build (`test_build_src = no`): the section text lives in `src/`,
  so the coverage proof is the PROBE (host-compiled `src/` like `probe_custody_usb`) or a small `test/` harness
  that includes the help unit — the brief chooses and names the instrument), the inventory regeneration, evidence.
- Corpus 36/36 by construction (no `lib/`); boards: flash moves (text + the index), RAM +0 expected (`F()` strings
  are flash; the stage is unchanged); census; probes (console-sink probe gains arms; custody/inbox probes
  unchanged); sweep; checkers.
- Mutation: `src/firmware_commands.cpp` has no battery target today ⇒ the probe's negative controls are the
  mutation coverage (a section over the stage, a dropped line, a gated topic listed when not compiled, `?`≠`help`).
- Bench: **Part 58** (USB): bare `help` prints the index with no `CONSOLE_DROP`; `help messaging` (the largest)
  prints whole; `help zzz` refuses; on a gateway build `help mobile` is absent from the index and refused.

## 4. Owner decision

None beyond the standing ruling. One clarification the brief may state: the exact index wording is the coder's,
the topic NAMES are the owner's nine.
