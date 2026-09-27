# W4a — the re-anchor table (brief §2.6 controls, §2.7 mutations)

Each row is old anchor → new anchor, with the meaning kept and the measured RED. Every `run.sh` substitution has its own
`once` guard: the literal anchor occurs exactly once and the script changes exactly one line. The RED counts come from
the stock gate run (`gate5-fwui-default.log`), which prints only a count. The named checks come from scratch runs of the
same mutant (`control-scratch/`, where an `l2` control runs both arms, so its count is doubled).

## `tools/probe_firmware_ui/run.sh` controls

| control | old anchor (stage A) | new anchor (final) | meaning, kept | RED (baseline → final) | intended checks, reddened |
| --- | --- | --- | --- | --- | --- |
| C120 | the REPLY line `… label_for_origin(pu, who, uint8_t(sizeof who));` (+ SEND routing appended) | the same line with `kReplyWhoCols` (+ the same appended routing) | RX kinds routed to the SEND half | 68 → 77 | P1 wake/open checks (plus P30c, new) |
| B241a | the old 4-statement adapter body (multi-line sed) | the one line `(void)mrui::ui_fmt_identity(out, cap, raw, n, hash, cols);` | the pre-fix body: raw API at FULL capacity, no terminator, lowercase `0x%08lx` | 10 → 42 | P28a H1/H2 poisoned short names, the rename, the 14/15/27/32-byte and both fallbacks, cap 1 (+ P18/P28b–d/P30, which now see the raw bytes and lowercase spelling) |
| B241b | `peer_name_find(…, cap - 1)` + `out[n] = '\0'` | the same one line | only `out[cap - 1]` terminated | 5 → 40 | P28a H1/H2, the rename, the long names and both fallbacks (+ P18/P28b–d/P30) |
| N4 (2 subs) | anchors UNCHANGED (`r.snr_q4 = e->snr_q4;`, `else mrui::ui_fmt_nearby_row(label, sizeof label, r.team);`); replacement `label_for_team_id(r.team.reserved, label, uint8_t(sizeof label))` | replacement `…, uint8_t(sizeof label), uint8_t(sizeof label - 1))` (the new budget argument) | the advertiser NODE NAME drawn as the team | 8 → 8 | P21b–P21e, P21g, P22a/b, P21d "NODE NAME appears nowhere" |
| N9 | anchor UNCHANGED (`else mrui::ui_fmt_nearby_row(label, sizeof label, r.team);`); replacement `label_from_hash(r.team.team_id, label, uint8_t(sizeof label))` | replacement `…, uint8_t(sizeof label), uint8_t(sizeof label - 1))` | the team id resolved as a peer hash (a third spelling) | 8 → 8 | P21b–P21e, P21g, P22a/b, P21d "no `0x` spelling" |
| O2 | anchor UNCHANGED (`mrui::ui_fmt_team_fingerprint(fp, sizeof fp, s.team_id);`); replacement `label_for_team_id(s.my_team_id, fp, uint8_t(sizeof fp))` | replacement `…, uint8_t(sizeof fp), uint8_t(sizeof fp - 1))` | the window headed by a device label, not the team fingerprint (the budget ≥ 10 gives `0x…`, never the team token) | 1 → 1 | P23a "second row is the TEAM's own fingerprint" |
| O3 | `else mrui::ui_fmt_invite_row(label, sizeof label, marker, r.cand);` → `char(32)` | the new row call `mrui::ui_fmt_invite_row(label, sizeof label, marker, r.cand, name6);` → `char(32)` | every candidate row drawn unmarked | 1 → 2 | P23b rule 2 (+ P30d, new) |
| O8 | stage A: `o8a`/`o8b` (name through `label_from_hash`) | drop the publication's `if (nn > 0)` guard | **post-change proof (B449):** the unnamed member's name field filled from the formatter's HASH branch | 2 → 2 | exactly P23b rule 2 and the P23d blank-name precondition; row `>0x00B» T221 BEDEAD` (19 columns, the correct fingerprint kept) |
| O6 | — | retired in stage A (B455; O20 covers it) | — | 1 → — | — |

**New controls.** Each goes RED on its intended new checks:

| control | the wrong shape | RED (final) | checks |
| --- | --- | --- | --- |
| W4a-S1 (l2) | TEAM formatted at the carrier's 14 cells, so the row's `%-6.6s` clips silently | 17 | P18a (`Wolfga`, `0x00C0` instead of `Wolfg»`, `C0FFEE`), P18b–e, P28b, P30a |
| W4a-S2 (l2) | the adapter copies raw bytes (skips `ui_display_byte`) | 5 | P30a/b/c high-byte rows (TEAM, compose, DELIVERED, REPLY) + P28a cap 1 (the raw copy ignores `cap`) |
| W4a-S3 (l2) | DELIVERED formatted at the header's 15 | 1 | P30b DELIVERED long name at 19 |
| W4a-S4 (l2) | REPLY re-sanitized after formatting (`»` → `.`) | 1 | P30c REPLY marker intact |
| W4a-S5 (v3) | the invite carrier formatted at 6 cells | 4 | P30d NEW MEMBER at 14 + candidate row, P23d rule 3, P23e (the equal-width second pass re-sanitizes the `»` — measured) |
| W4a-S6 (v3) | the candidate row handed the unprojected carrier | 2 | P23d rule 3, P30d candidate row |

**Unchanged purpose and topology:** O20, O5, O9, O18, O22 and C114. C114's `if (hash != 0)` and the invite guard's
`if (hash != 0u)` remain distinct anchors. C0 still has `must_build=no` and still fails to build.

## `tools/probe_ui_model_mutations.py` entries

| entry | old anchor | new anchor | meaning, kept | RED |
| --- | --- | --- | --- | --- |
| uiinvite I07 | `snprintf(out, cap, "%c%-6.6s T%-3u %6s", marker, m.name, …)` | `…, marker, name6, …` | the name REPLACES the fingerprint | RED (13 assertions, match count 1) |
| uiinvite I08 | same | same, with `name6` | the name column falls back to a `0x` spelling | RED (5 assertions, match count 1) |
| uiinvite I09 | same | same, with `name6` | the column loses its own six-cell precision (RED through the synthetic over-long `name6`) | RED (20 assertions, match count 1) |
| uiteam T01, T02, T03, T09 | unchanged (the statements are byte-identical; `firmware_ui_team.h` changed comments only) | unchanged | T01 row bound (synthetic over-long carrier), T09 off-screen comparison (synthetic invisible tail) | RED (6 assertions, match count 1) · RED (41 assertions, match count 1) · RED (22 assertions, match count 1) · RED (3 assertions, match count 1) |
| I10, I29, I30, model M13 and every other entry | unchanged | unchanged | — | in the union |

**New battery `w4aident`** (`src/firmware_ui_model.h`, `ui_fmt_identity`): F01–F07 as §2.7 requires, plus F08 (the
capacity rule) and F09 (name before hash).

| entry | the wrong shape | union RED (assertions) |
| --- | --- | --- |
| F01 | ★★ the MARKER IS DROPPED — the last kept cell is the next name byte, a SILENT clip (B441's defect restored) | RED (34) |
| F02 | ★★★ the SANITIZER IS BYPASSED — raw name bytes reach the panel (UTF-8 as Latin-1 mojibake, lost cells) | RED (168) |
| F03 | ★★ the marker is placed BEFORE sanitizing, so the sanitizer turns the generated 0xBB into `.` | RED (31) |
| F04 | ★★★ an unnamed peer at 6-9 columns gets a CLIPPED `0x…` hash instead of its fingerprint (the third spelling) | RED (4) |
| F05 | ★★ the six-digit FINGERPRINT is used at 10+ columns, where the full `0x<HASH8>` fits and is ruled | RED (3) |
| F06 | ★★★ NO name and NO hash FABRICATES `0x00000000` instead of answering `none` (the caller's `id <n>` is lost) | RED (24) |
| F07 | ★★ the marker is written ONE PAST the budget — the abbreviated label is `cols + 1` cells wide | RED (36) |
| F08 | ★★ the CAPACITY rule is dropped — a destination shorter than the budget gets a partial result past its end | RED (6) |
| F09 | ★★ a NAME is ignored whenever a hash is known — the peer's chosen name is replaced by its hash token | RED (13) |

Battery: **9 RED / 0 unusable**, every anchor matching exactly once, clean baseline 2986 / 197299 / 0 on every worker.
