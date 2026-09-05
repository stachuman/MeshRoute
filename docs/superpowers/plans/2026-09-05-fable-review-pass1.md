<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Fable code review — pass 1 (2026-09-05): the pre-feature phase's production code + its two new instruments

Owner ruling 2026-09-04: *"Agree - first pass after 0a closes, then the seams."* Reviewed at `HEAD a018f65` ("0a"),
tree clean. Standard: `docs/CODE_GUIDELINES.md` + the CLAUDE.md rule board. Every finding below is verified against
the source; the behavioural ones carry an executed measurement. Findings are DRAFT register rows (M1) for the Author
to land; numbering is the Author's. I edited nothing under review. The owner rules which rows are fixed and in which
slice.

## Scope read in full

| unit | what it is | verdict |
| --- | --- | --- |
| `lib/core/node_hashlocate.cpp` :1722-1810, :1874-1892 (the four 0d home-bound arms) | R-RA-12 / D-0d-1 | clean — the arms and the kept-visible rationale agree with `can_host_mobiles()` and the cross-layer sibling |
| `src/device_ble.h` (whole file, 325 lines) | 0f derived BLE line capacity | one latent hazard (F2), one cosmetic (I1) |
| `src/firmware_help.h` (whole file, 257) + the one router call in `src/firmware_commands.cpp` + the widened guard `src/fw_main.cpp:588` | 0a help authority | clean router; one inherited help-text gap (F3) |
| `tools/probe_ble_line/` (run.sh, probe_main.cpp, 3 fakes) + `tools/test_probe_ble_line.py` | 0f instrument | clean — pins enforced in-runner, controls on isolated copies, fakes hold no intake decision |
| `tools/probe_console_sink/` (run.sh, probe_main.cpp, structural.py, negctl.py, ble_guard.py, help_manifest.py, help_baseline.json) + `tools/test_probe_console_sink.py` | 0a instrument | one maintenance defect (F1), one consistency gap (F4), one count literal (I2) |

## Findings — draft register rows

### F1 · MEDIUM · INSTRUMENT DESIGN — the help content baseline is frozen to the pre-0a dump and has no forward regeneration path

**Claim.** `tools/probe_console_sink/help_baseline.json` holds the PRE-0a `dump_help()` content multiset per profile,
and the DEFAULT console-sink gate compares the CURRENT `src/firmware_help.h` output against it on every run
(`run.sh:101-102`, `help_manifest.py compare`; the wrapper asserts 0 problems for all six profiles,
`tools/test_probe_console_sink.py::test_every_profile_reported_a_clean_content_multiset`). That was the right
no-line-lost proof for the split. But the ONLY documented regeneration source is the historical file
(`help_manifest.py` docstring: `git show 807ebde:src/firmware_commands.cpp … freeze`), and `freeze`/`verify` parse a
`dump_help()` body that no longer exists on main.
**Measurement (2026-09-05).** `help_manifest.py verify src/firmware_commands.cpp help_baseline.json` on `a018f65` →
`ManifestError: signature not found: 'static void dump_help(Print& out) {'` (loud, but it means main cannot re-derive
its own baseline); the same command against `git show 807ebde:src/firmware_commands.cpp` → `ok`. Consequence: EVERY
future help-text change — Slice 6's `remote` verb lines, F3's `-K` fix, any `cfg` key added to the CFG KEYS catalog —
turns the console-sink gate and the tools sweep RED (negctl's own H-C1 "delete ONE inherited help line" is exactly
the signature a legitimate edit produces), and the only sanctioned path cannot express the new text. The next coder
either hand-edits the JSON (forbidden by its own `_note`) or "re-freezes" from an ad-hoc source.
**Remedy (for the owner to place).** Keep the pre-slice equivalence as a RECORDED, one-time fact (0a evidence §4.2
already carries it) and re-point the frozen baseline at the CURRENT authority: a `freeze-current` mode that renders
`src/firmware_help.h` through the probe binary per profile (the same `HELP-CONTENT-BEGIN` block `compare` already
consumes), so a help edit re-freezes from the tree it lands on, with the diff of the baseline reviewable in the slice
that changes the text. `verify` then checks JSON ⇄ header rather than JSON ⇄ history. A small `tools/`-only slice;
no production code.

### F2 · LOW · LATENT TRANSPORT HAZARD — the direct BLE dispatch reply is one notification, but `g_out` is bigger than one notification

**Claim.** `src/device_ble.h` `dispatch_current_line()` (:≈208-220) writes the dispatcher's reply with ONE
`g_bleuart.write(g_out, n)`, `n ≤ sizeof g_out = 256`. The file's own `tx_line()` comment states the rule it then
ignores here: one write is ONE notification of at most `ATT MTU − 3` = 244 bytes at `BANDWIDTH_MAX`, so a longer line
loses its tail. A direct reply of 245..256 bytes would therefore be truncated silently — exactly the class `tx_line`
chunks to avoid.
**Measurement.** Every direct reply today is short: the longest is `limits` (`fw_main.cpp:490` comment "13 u32 fields
~185 B"), `version` ≈130 B, the rest < 60 B; the long responses (`cfg`, `peers`, `status`) go through `LineSink` →
`tx_line` and are chunked. So the hazard is UNREACHABLE today and the probe's C18 pins the single-write shape without
a >244-byte case. It is a fail-silent edge one new `write_*` away.
**Remedy.** Either `static_assert(sizeof g_out <= 244)` with the MTU term named beside `BANDWIDTH_MAX`, or send the
reply through `tx_line()` (chunked; same `g_conn_count` precondition holds because a line just arrived). Add the
>244-byte reply case to `probe_ble_line` (C18 would then pin "chunked, byte-exact"). `src/device_ble.h` + probe only.

### F3 · LOW · HELP TEXT DRIFT — `-K` (suppress the INTRO first-contact attach) is accepted by `send` and `send_layer` but documented by neither

**Claim.** `lib/console/console_parse.cpp:123` accepts `-K` on `send` and `send_layer`; the help lines
(`src/firmware_help.h` `topic_messaging`) list `send … [-a] [-e] [-t] [-l]` and `send_layer … [-a] [-e]`. Inherited
verbatim from the pre-0a dump (the split proved no line was lost — including this gap).
**Measurement.** `grep -c '\-K' src/firmware_help.h` → 1, and that one hit is the B208 ruling quoted in the file
comment, not a help line. `docs/manual/command-reference.md` is the authority for the flag; the console summary
contradicts it by omission.
**Remedy.** One-line edits to both `send` lines (`[-K]`), with the `MESSAGING` section re-measured (< 2048 B; it is
the second-largest topic). ⚠ This edit is the first customer of F1: it turns the frozen baseline RED, so it must
follow F1's remedy or be landed in the same slice.

### F4 · LOW · INSTRUMENT CONSISTENCY — `probe_console_sink/run.sh` prints its derived pins but enforces none of them in-runner

**Claim.** `run.sh:178` prints `PINS profiles= checks= structural= ble_guard= controls= unusable_controls=` and
prints PASS on `rc == 0` regardless of the counts; the counts are enforced ONLY by the wrapper
(`test_probe_console_sink.py` `PIN_*`). The sibling `probe_ble_line/run.sh:88-89, :274-283` enforces `PIN_CHECKS` /
`PIN_CONTROLS` in the runner AND the wrapper pins that it does. A coder running the console-sink runner alone gets
PASS with a reduced check count; the sweep catches it, but the two probes disagree about where the pin lives.
**Remedy.** Mirror the BLE-line runner: `PIN_*` at the top of `run.sh` with the derivation, compared before PASS;
keep the wrapper's pins as the second reader. `tools/` only.

### F5 · LOW · HYGIENE (pre-existing, 0e) — the inventory generator advertises a `--check` flag it does not have

**Claim.** `tools/gen_command_inventory.py:12` and the generated footer (`:659` → the tracked inventory line 2,
"verify: --check") name `--check`; `argparse` (`:736-739`) defines `--root/--out/--write/--stdout` only. The bare
invocation IS the check (`gen_command_inventory: PASS … matches fresh generation byte-for-byte`).
**Measurement.** `python3 tools/gen_command_inventory.py --check` → `error: unrecognized arguments: --check` (hit during
the 0a gate, 2026-09-04). **Remedy.** Either add `--check` as an explicit alias of the default or fix the two mentions;
regenerate the inventory (footer text changes) via `--write`.

## Informational (no row)

- **I1** `src/device_ble.h` `static_assert(kLineStorageBytes == kProductLineMaxBytes + 1)` restates the definition on
  the line above it; the probe's A4 text pin is what actually guards the shape. Harmless; drop or keep.
- **I2** `negctl.py:593` `CONTROLS-TOTAL` sums list lengths plus the literals `+ 8 + … + 2` for the groups that are
  not lists (the X-controls' inner rows, H-C16/17). A change in those groups must be re-summed by hand; the wrapper's
  `PIN_CONTROLS=42` would catch a drift, so this is a legibility note.
- **I3** The 0d arms: nothing to fix. The stack copies into `wbody[241]` on the two wrapper arms are pre-existing and
  bounded by the sealed-relay/body caps; not a 0d matter.

## What pass 2 will read (the seams Slice 1/1b build on)

`lib/core/node_mac_rx.cpp` :≈2195-2212 pre-tail handlers (R-RA-19's refactor target) and the `send_by_hash` /
`do_send(app_dm=true)` path in `lib/core/node_hashlocate.cpp` (R-RA-13), plus `node_mac.cpp` `enqueue_cross_layer`
(the sibling the 0d rationale leans on).

## Post-ruling disposition (owner, 2026-09-05)

Help becomes a bare primary-verb index with the manual as the only reference (ruling recorded in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`, "Owner ruling 2026-09-05"). ⇒ F1 + F3 CLOSED BY
REDESIGN (register rows still land, closing in place); F4 + F5 ride Slice 0g; F2 = its own tiny slice. Pass 2 starts.
