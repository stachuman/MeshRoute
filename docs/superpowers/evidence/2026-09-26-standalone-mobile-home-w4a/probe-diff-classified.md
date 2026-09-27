# W4a — the classified stage A → final probe diff (brief §2.6 / §4.1 step 1)

Stage A froze `run.sh` `79d68a22…` and `probe_main.cpp` `d0cf6502…` (copies retained under the ignored artifacts
folder, `stageA-snapshot/`). Every hunk of the stage A → final diff is one of the classes below; nothing else changed.
The raw unified diffs are `stageA-to-final-probe_main.diff` and `stageA-to-final-run.sh.diff` (hashes in
`raw-logs.txt`). Baseline → stage A (the instrument repairs, product unchanged) is in `stage-a.txt`.

## probe_main.cpp

| Class | Where | What changed | Ledger row |
| --- | --- | --- | --- |
| Signature adaptation | trampoline declaration | `mr_probe_label_from_hash(hash, out, cap)` → `(…, cols)`; still defined inside the wrapper TU that includes the **file under test** | P28a ("the trampoline takes the new signature") |
| Changed expectation | P18a–P18e (14 lines) | `Wolfga` → `Wolfg` + `»`; unnamed `0x00c0` → `C0FFEE`. Ages, marker, frame counts, geo and blank checks unchanged | P18 |
| Label reworded | P18a (2 labels) | "clamped to six columns" → "abbreviated to six cells with the marker"; "the 0x<hash> label, clamped the same way" → "the unnamed key's six-digit member fingerprint" | P18 (text follows the expectation) |
| Strengthened | P15k, P21d | the team-only absence checks also exclude the abbreviation `Wolfg` + `»` | "must not change mechanically … strengthened" |
| Unchanged by rule | P21 precondition | the raw-cache `Wolfga` prefix check stays raw | "P21's raw-cache precondition stays raw" |
| Signature adaptation (expectation unchanged) | P23b oracle, P23d `blank_target` | `ui_fmt_invite_row(…, want)` → `(…, want, "")` — the blank field and 19 columns are still asserted | P23/P24 ("the blank field … unchanged") |
| Changed expectation (+ label) | P23d rule 3 | the name column reads `Wolfg` + `»` (six cells) | P23/P24 |
| Changed expectation | P23e / P24 `walk_to` (2) | `>Wolfga T221` → `>Wolfg» T221` | P23/P24 ("including the `walk_to` strings") |
| Unchanged | P23e confirmation name | `Wolfgangetta` (12 bytes) still fits the 14-cell carrier | P23/P24 ("the 12-byte confirmation name … unchanged") |
| Changed expectation (+ labels) | P28a | budget 14 passed; 15/27/32-byte oracles → first 13 bytes + `»` (built by `memcpy`, not the formatter); exactly 14 whole; fallback uppercase `0x%08lX`. Poison, canaries, short rename, cap 0/1 kept | P28a |
| Changed expectation (+ labels) | P28b | ` 0xb241 ` → ` 410007 `, ` 0xb2ee ` → ` EE41EE `; `H1`, `id 93` unchanged | P28b |
| Changed expectation | P28c / P28d | lowercase → uppercase `0xB2410007`, `0xB2EE41EE`; short names and `id 93` unchanged | P28c/P28d |
| Comments | P18, P28 fixture notes | the old spellings in explanatory comments corrected | (V1) |
| **New checks** | **P30** (new phase, before P26) | real lookup at every §2.3 site: a 20-byte name and `C5 82 41 42` at TEAM, compose header, DELIVERED, REPLY (all arms); candidate rows and NEW MEMBER (v3). All expected bytes literal. Restores P28's canonical fixture | "the real renderer probe shows …" |

## run.sh

| Class | Control | What changed |
| --- | --- | --- |
| Signature adaptation | W1 `ui_wrapper` | the generated trampoline forwards `cols` |
| Re-anchored, meaning kept | C120 | the REPLY line now passes `kReplyWhoCols`; RX routed to the SEND half |
| Re-anchored, meaning kept | B241a, B241b | onto the adapter's one formatter-call line: the pre-fix unterminated full-capacity copy; the last-byte-only terminator |
| Re-anchored, meaning kept | N4 (both subs), N9 | the resolver's budget argument (`sizeof label - 1`) |
| Re-anchored, meaning kept | O2 | budget `sizeof fp - 1`; still a device label, never the team token |
| Re-anchored, meaning kept | O3 | onto the candidate row's `name6` call |
| Re-aimed (B449 post-change proof) | O8 | drops the publication's `nn > 0` guard: the formatter's HASH branch fills the unnamed name field |
| **New** | W4a-S1 … W4a-S4 (`l2`), W4a-S5, W4a-S6 (`v3`) | silent clip at TEAM; sanitizer skipped; DELIVERED at 15; REPLY re-sanitized; invite carrier at 6; candidate row unprojected |
| Guards | every substitution above | its own `once` exactly-one guard (15 guards) |
| Helper relocated (instrument fix, no meaning change) | W3's `once()` | moved from inside W3's control block to top level beside `ctl()`, with its B449 paragraph and a note on why; its FAIL text says "a control guard" instead of "a W3 control guard". Without the move, the seven guards above it (C120, B241a, B241b, W4a-S1…S4) hit `once: command not found`, and the first chain ran 229 of 236 controls. See the report's §5.5 |
| Retirement (stage A, not in this diff) | O6 | removed in stage A with a comment naming B455 and O20 |
