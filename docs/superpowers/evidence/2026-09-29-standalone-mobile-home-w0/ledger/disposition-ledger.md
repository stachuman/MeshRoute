# W0 — the §2.8 closed disposition ledger

Every changed, retired or added native expectation, probe check, control and mutation, each on its own row, with its reason and its must-fail result — built from the coder's working ledger (`artifacts/…/dispositions*.tsv`) and the FINAL chain's union and inbox-verbs logs.

## 1. Native, harness, fixtures, runner and inventory

| File | Item | Kind | Disposition / must-fail |
| --- | --- | --- | --- |
| `test/test_device_nv.cpp` | NEW device_nv/W0: id_blob_from_live copies EVERY field, stamps the record and zeroes the unused name bytes | new case (brief §2.7 native) | names of 0/1/31/32 bytes over a 0xA5-prefilled record, compared byte for byte with an independently built record; unused name bytes 0; INT32_MIN/INT32_MAX coordinates verbatim. Must-fail: devicenv W0-N1 seed, W0-N2 name, W0-N3 lat, W0-N4 lon, W0-N5 magic, W0-N6 version, W0-N10 no zeroing |
| `test/test_device_nv.cpp` | NEW device_nv/W0: an empty live name is a valid candidate — the unnamed node's record keeps seed and position | new case | a null pointer at length 0 is accepted; the whole record compared. Must-fail: W0-N1/N3/N4/N5/N6/N10 |
| `test/test_device_nv.cpp` | NEW device_nv/W0: arbitrary HIGH bytes are copied as bytes — no terminator, no repertoire, no truncation | new case | 32 bytes ≥0x80 with an embedded NUL, compared with memcmp (no raw byte ever printed). Must-fail: W0-N2 name, W0-N10 |
| `test/test_device_nv.cpp` | NEW device_nv/W0: a name over 32 bytes is REFUSED on its size_t length — before any narrowing, nothing usable left | new case | 33 / 256 / 65537 refused with a wholly zero record (no magic); a null pointer with a non-zero length refused. Must-fail: W0-N7 (33 accepted), W0-N8 (uint8 narrowing: 256), W0-N9 (uint16 narrowing: 65537), W0-N10 (a refusal leaves 0xA5) |
| `test/test_device_nv.cpp` | #include <string> | include | for the 65537-byte name |
| `tools/probe_ui_model_mutations.py` | devicenv W0-N1..W0-N10 | 10 new entries (brief §2.8: dropped seed/name/lat/lon/stamp, accepted overlength, narrowing before admission) | anchors in the pure helper, each matching exactly once; all RED in the dev run (56/56) |
| `tools/probe_ui_model_mutations.py` | PIN_CASES, PIN_ASSERTS 3055, 199532 -> 3059, 199638 | PIN re-sync | +4 cases/+106 assertions, all test_device_nv 27/471 -> 31/577 (measured by the full native binary per source file) |
| `tools/probe_inbox_verbs/probe_main.cpp` | the eight config-handler stubs | stub exclusion (brief §2.7) | wrapped in `#ifndef MR_PROBE_IDENTITY_ARM`: byte-identical for every build that does not define it (accept/client/oled here, the transcript driver, deferred-actions); left out ONLY of the identity arm, which links the real ones |
| `tools/probe_inbox_verbs/probe_main.cpp` | legacy adapter stand-in `mrfw::id_candidate_from_live` (labelled, W0R-2) | new provider, legacy builds only | builds from the fixture's live fields through the REAL `mrnv::id_blob_from_live`; reads no NV, installs nothing; excluded from the identity arm with the stubs. Link reconciliation: accept/client/oled here (dev run links + passes), deferred-actions (links + passes, source unchanged), transcript driver (NOT RUN, owner ruling 2026-09-30 B487/B488) |
| `tools/probe_inbox_verbs/probe_main.cpp` | install_live_name_pos() + R11 (R12..R22), R23, R25, R26 fixtures | live precondition (brief §2.7) | the seeded record's name and position are installed LIVE (regen's candidate is the live snapshot now); NO crypto (seed_id unchanged; R1..R17 still observe crypto becoming ready). Every expected output and persisted field unchanged; the R rows keep their exact assertions |
| `tools/probe_inbox_verbs/identity_main.cpp` | NEW identity arm driver | new file (brief §2.7) | M 140 + N 12 + G 18 + C 4 + R1 1 (+ R2 CLIENT) = 175 / 176 rows; each listed with its controls in dispositions-probes.tsv |
| `tools/probe_inbox_verbs/identity_platform.h` | NEW platform shim | new file (brief §2.7) | LoRa defaults, default_output_dbm via rf_capabilities.h, __FlashStringHelper, the one g_persist_team_local_id definition; force-included into the config TU only |
| `tools/probe_inbox_verbs/run.sh` | arms identity_accept / identity_client; CFG_TU/PROBE_SRC; build_variant's optional config TU; symbol ownership for identity_client; the `config` control kind; W0-C1..C12 (w0_ctl with intended-row check); PIN_CHECKS_IDENTITY_ACCEPT/CLIENT 175/176, PIN_CONTROLS_IDENTITY 13; md5 list + the four identity inputs; the arm-1/2-only gate on the shared R7/A7/A10 controls | runner (brief §2.7) | the builder boundary `rc=0\nif ! build_support;` still occurs exactly once; build_variant/build_support callable as before (CFG_TU empty => identical build); deferred-actions reruns green with its source unchanged |
| `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md` | 138 rows' `file:line` anchors | stock regeneration (line anchors only) | 197 semantic rows unchanged: the regenerated file equals the tracked one after normalising `src/<tu>:<line>` (98 firmware_commands.cpp + 178 firmware_config.cpp anchor mentions moved) |

## 2. The `devicenv` additions, with their final union verdicts

| ID | Label | Final verdict |
| --- | --- | --- |
| W0-N1 | ★★★ THE SEED IS DROPPED — every writer would save a record whose identity is not the running one | RED (7 assertions, match count 1) |
| W0-N2 | ★★ THE NAME BYTES ARE DROPPED — the record claims a name length over zero bytes | RED (5 assertions, match count 1) |
| W0-N3 | ★★ THE LATITUDE IS DROPPED — a rename or a longitude write zeroes the stored latitude (B440 restored) | RED (7 assertions, match count 1) |
| W0-N4 | ★★ THE LONGITUDE IS DROPPED — a rename or a latitude write zeroes the stored longitude (B440 restored) | RED (7 assertions, match count 1) |
| W0-N5 | ★★ THE MAGIC IS NOT STAMPED — the saved record never loads again (the next boot re-mints the identity) | RED (10 assertions, match count 1) |
| W0-N6 | the VERSION is not stamped — the saved record fails the exact version check at the next load | RED (10 assertions, match count 1) |
| W0-N7 | ★★★ AN OVERLENGTH NAME IS ACCEPTED — 33 bytes pass admission (B448 restored one byte later) | RED (3 assertions, match count 1) |
| W0-N8 | ★★★ NARROWING BEFORE ADMISSION — the length is narrowed to a uint8_t first, so 256 bytes wrap to an empty name | RED (6 assertions, match count 1) |
| W0-N9 | ★★ NARROWING BEFORE ADMISSION — the length is narrowed to the stored uint16_t first, so 65537 bytes pass as one | RED (3 assertions, match count 1) |
| W0-N10 | ★★ THE CANDIDATE IS NOT ZEROED FIRST — stale bytes survive past the name and in a refused candidate | RED (72 assertions, match count 1) |

## 3. The identity arms: rows, controls and their final verdicts

| File | Item | Kind | Disposition / must-fail |
| --- | --- | --- | --- |
| `tools/probe_inbox_verbs/identity_main.cpp` | M.<writer>.<medium>.out (28 rows: name/lat/lon/regen × healthy_diff/absent/unreadable/short/bad_magic/bad_version/oversize) | new rows (B440 matrix) | the exact success bytes through the real router (regen: through the supplied LineSink). Must-fail: W0-C2 (regen prints the NV record's name) |
| `tools/probe_inbox_verbs/identity_main.cpp` | M.<writer>.<medium>.writes (28) | new rows | exactly ONE /mrid write per command (by the NV fake's counter) |
| `tools/probe_inbox_verbs/identity_main.cpp` | M.<writer>.<medium>.rec (28) | new rows (the invariant) | the medium holds EXACTLY the live seed/name/position with only the intended field replaced, against a record built field by field — the durable record carries a DIFFERENT seed/name/position, so any NV-sourced field shows. Must-fail: W0-C1 (lat/lon from load_id), W0-C2 (regen from load_id), W0-C12 (adapter reads NV) |
| `tools/probe_inbox_verbs/identity_main.cpp` | M.<writer>.<medium>.live (28) | new rows | the live state is exactly the intended one (name, both mirrors, key hash; regen: the new identity installed). Must-fail: W0-C7 (NodeConfig lat mirror left stale) |
| `tools/probe_inbox_verbs/identity_main.cpp` | M.<writer>.<medium>.others (28) | new rows | every unrelated store (/mrcfg, admid, acl, mkeys, targets, peers — seeded) byte-identical: no identity writer touches another record, regen preserves the administration/controller stores |
| `tools/probe_inbox_verbs/identity_main.cpp` | N1 | new row (coalescing) | a byte-identical durable record answers ok with ZERO writes. Must-fail: W0-C8 |
| `tools/probe_inbox_verbs/identity_main.cpp` | N2 / N2b | new rows (W0R-1) | live `Live Name`, durable `Saved Name` with the live seed/position, request `Saved Name`: ok, zero writes, and `Saved Name` LIVE afterwards. Must-fail: N2 W0-C8/W0-C9, N2b W0-C10 |
| `tools/probe_inbox_verbs/identity_main.cpp` | N3a..N3e | new rows (repair) | live name == request but the record is absent / bad-magic / short / different / unreadable: repaired from live values, one write. Must-fail: W0-C9 (live-name equality) |
| `tools/probe_inbox_verbs/identity_main.cpp` | N4 | new row (live vs durable) | a durable record with a stale POSITION is repaired from the live position. Must-fail: W0-C9 |
| `tools/probe_inbox_verbs/identity_main.cpp` | N5 / N5b | new rows (failed save) | nv_save_failed after exactly one attempt; nothing published, the medium unchanged. Must-fail: N5b W0-C5 |
| `tools/probe_inbox_verbs/identity_main.cpp` | N6 | new row (counted live name) | a 32-byte LIVE name survives a coordinate write whole. Must-fail: W0-C11 |
| `tools/probe_inbox_verbs/identity_main.cpp` | G3 G4 G5 G6 G7 G9 G12 | new rows (B448 accepted) | spaces, quotes, all-space, 31, 32, a 2-byte char within 32, 32 high bytes: saved whole, made live, others intact (hex diagnostics only) |
| `tools/probe_inbox_verbs/identity_main.cpp` | G1/G1b, G8/G8b, G10/G10b, G11/G11b, G13/G13b | new rows (B448 refused) | empty -> bad_args; 33, a 2-byte char crossing byte 32, a 3-byte char crossing byte 32, 300 bytes -> too_long; b rows: ZERO writes, seed/name/both mirrors/membership/every NV slot unchanged. Must-fail: W0-C3 (early return removed), W0-C4 (clamp restored) |
| `tools/probe_inbox_verbs/identity_main.cpp` | G2 | new row | `cfg set name ` (empty value after the separator) -> bad_args, zero writes |
| `tools/probe_inbox_verbs/identity_main.cpp` | C.lat.err / C.lon.err | new rows (B482) | a refused coordinate save prints nv_save_failed after exactly one attempt |
| `tools/probe_inbox_verbs/identity_main.cpp` | C.lat.live / C.lon.live | new rows (B482) | ...and BOTH mirrors keep the old coordinate. Must-fail: W0-C6 (published before the save) |
| `tools/probe_inbox_verbs/identity_main.cpp` | R1 | new row | a refused regen save keeps identity, name, position and the medium (the supplied sink carries the error) |
| `tools/probe_inbox_verbs/identity_main.cpp` | R2 (identity_client only) | new row | remote debt refuses regen: `> regen err remote_busy`, zero writes, everything unchanged |
| `tools/probe_inbox_verbs/run.sh` | W0-C1 the lat/lon candidate is rebuilt from load_id | new control (config) | RED on M.lat.*.rec (+ lon rows) |
| `tools/probe_inbox_verbs/run.sh` | W0-C2 the regen candidate is rebuilt from an unchecked load_id | new control (router) | RED on M.regen.*.rec/.out |
| `tools/probe_inbox_verbs/run.sh` | W0-C3 the rename service loses its overlength early return | new control (config) | RED on G8/G10/G11/G13 (+b) |
| `tools/probe_inbox_verbs/run.sh` | W0-C4 the console arm clamps the name to 32 bytes before admission | new control (config) | RED on G8/G10/G11/G13 (+b) |
| `tools/probe_inbox_verbs/run.sh` | W0-C5 the requested name is published before its save | new control (config) | RED on N5b |
| `tools/probe_inbox_verbs/run.sh` | W0-C6 the coordinate is published before its save (B482) | new control (config) | RED on C.lat.live / C.lon.live |
| `tools/probe_inbox_verbs/run.sh` | W0-C7 a saved latitude is published to the global only | new control (config) | RED on M.lat.*.live |
| `tools/probe_inbox_verbs/run.sh` | W0-C8 the byte-identical no-op writes anyway | new control (config) | RED on N1/N2 |
| `tools/probe_inbox_verbs/run.sh` | W0-C9 the no-op test compares the live name (repair skipped) | new control (config) | RED on N3a..N3e (+N2, N4) |
| `tools/probe_inbox_verbs/run.sh` | W0-C10 the already-durable branch returns before making the name live | new control (config) | RED on N2b |
| `tools/probe_inbox_verbs/run.sh` | W0-C11 the adapter reserves a terminator byte | new control (config) | RED on N6 |
| `tools/probe_inbox_verbs/run.sh` | W0-C12 the adapter reads the seed and position from load_id | new control (config) | RED on M.name.*.rec (+ lat/lon/regen rec rows) |
| `tools/probe_inbox_verbs/run.sh` | W0 control classification (w0_ctl) | new rule | exactly-one source match, stock `ctl` classification (crash/build failure/vacuous never RED), THEN counted only if the INTENDED row is among the reddened ones; else unusable |
| `tools/probe_inbox_verbs/run.sh` | shared R7-C1 / R7-C2.. / A7-C1 / A7-C2 / A10-C1 block gate | gate widened | `[ "$MR_PROBE_ARM" != oled ]` -> `&& [ -z "$CFG_TU" ]`: they measure arm-1/2 rows only (on the identity arms each scored `passes`, measured); arms 1/2 unchanged (63/71) |
| `tools/probe_inbox_verbs/run.sh` | legacy C8/C9/C10, C12/C13, C35/C36, CLIENT C8-C10 | audited, unchanged | their anchors (`do_regen(out)`, do_regen's `out.` writes, the regen save line, `out.print(F("> regen ok"))`, the NV fake) are untouched; all RED on the legacy arms (63/71 verified) |

### Final control verdicts per identity arm (the rows each reddened)

| Arm | Control | Verdict | Rows reddened |
| --- | --- | --- | --- |
| identity_accept | W0-C1 | RED (15) | M.lat.healthy_diff.rec M.lat.absent.rec M.lat.unreadable.rec M.lat.short.rec M.lat.bad_magic.rec M.lat.bad_version.rec M.lat.oversize.rec M.lon.healthy_diff.rec M.lon.absent.rec M.lon.unreadable.rec M.lon.short.rec M.lon.bad_magic.rec M.lon.bad_version.rec M.lon.oversize.rec N6 |
| identity_accept | W0-C2 | RED (14) | M.regen.healthy_diff.out M.regen.healthy_diff.rec M.regen.absent.out M.regen.absent.rec M.regen.unreadable.out M.regen.unreadable.rec M.regen.short.out M.regen.short.rec M.regen.bad_magic.out M.regen.bad_magic.rec M.regen.bad_version.out M.regen.bad_version.rec M.regen.oversize.out M.regen.oversize.rec |
| identity_accept | W0-C3 | RED (8) | G8 G8b G10 G10b G11 G11b G13 G13b |
| identity_accept | W0-C4 | RED (8) | G8 G8b G10 G10b G11 G11b G13 G13b |
| identity_accept | W0-C5 | RED (1) | N5b |
| identity_accept | W0-C6 | RED (2) | C.lat.live C.lon.live |
| identity_accept | W0-C7 | RED (7) | M.lat.healthy_diff.live M.lat.absent.live M.lat.unreadable.live M.lat.short.live M.lat.bad_magic.live M.lat.bad_version.live M.lat.oversize.live |
| identity_accept | W0-C8 | RED (2) | N1 N2 |
| identity_accept | W0-C9 | RED (7) | N2 N3a N3b N3c N3d N3e N4 |
| identity_accept | W0-C10 | RED (1) | N2b |
| identity_accept | W0-C11 | RED (1) | N6 |
| identity_accept | W0-C12 | RED (35) | M.name.healthy_diff.rec M.name.absent.rec M.name.unreadable.rec M.name.short.rec M.name.bad_magic.rec M.name.bad_version.rec M.name.oversize.rec M.lat.healthy_diff.rec M.lat.absent.rec M.lat.unreadable.rec M.lat.short.rec M.lat.bad_magic.rec M.lat.bad_version.rec M.lat.oversize.rec M.lon.healthy_diff.rec M.lon.absent.rec M.lon.unreadable.rec M.lon.short.rec M.lon.bad_magic.rec M.lon.bad_version.rec M.lon.oversize.rec M.regen.healthy_diff.rec M.regen.absent.rec M.regen.unreadable.rec M.regen.short.rec M.regen.bad_magic.rec M.regen.bad_version.rec M.regen.oversize.rec N3a N3b N3c N3d N3e N4 N6 |
| identity_client | W0-C1 | RED (15) | M.lat.healthy_diff.rec M.lat.absent.rec M.lat.unreadable.rec M.lat.short.rec M.lat.bad_magic.rec M.lat.bad_version.rec M.lat.oversize.rec M.lon.healthy_diff.rec M.lon.absent.rec M.lon.unreadable.rec M.lon.short.rec M.lon.bad_magic.rec M.lon.bad_version.rec M.lon.oversize.rec N6 |
| identity_client | W0-C2 | RED (14) | M.regen.healthy_diff.out M.regen.healthy_diff.rec M.regen.absent.out M.regen.absent.rec M.regen.unreadable.out M.regen.unreadable.rec M.regen.short.out M.regen.short.rec M.regen.bad_magic.out M.regen.bad_magic.rec M.regen.bad_version.out M.regen.bad_version.rec M.regen.oversize.out M.regen.oversize.rec |
| identity_client | W0-C3 | RED (8) | G8 G8b G10 G10b G11 G11b G13 G13b |
| identity_client | W0-C4 | RED (8) | G8 G8b G10 G10b G11 G11b G13 G13b |
| identity_client | W0-C5 | RED (1) | N5b |
| identity_client | W0-C6 | RED (2) | C.lat.live C.lon.live |
| identity_client | W0-C7 | RED (7) | M.lat.healthy_diff.live M.lat.absent.live M.lat.unreadable.live M.lat.short.live M.lat.bad_magic.live M.lat.bad_version.live M.lat.oversize.live |
| identity_client | W0-C8 | RED (2) | N1 N2 |
| identity_client | W0-C9 | RED (7) | N2 N3a N3b N3c N3d N3e N4 |
| identity_client | W0-C10 | RED (1) | N2b |
| identity_client | W0-C11 | RED (1) | N6 |
| identity_client | W0-C12 | RED (35) | M.name.healthy_diff.rec M.name.absent.rec M.name.unreadable.rec M.name.short.rec M.name.bad_magic.rec M.name.bad_version.rec M.name.oversize.rec M.lat.healthy_diff.rec M.lat.absent.rec M.lat.unreadable.rec M.lat.short.rec M.lat.bad_magic.rec M.lat.bad_version.rec M.lat.oversize.rec M.lon.healthy_diff.rec M.lon.absent.rec M.lon.unreadable.rec M.lon.short.rec M.lon.bad_magic.rec M.lon.bad_version.rec M.lon.oversize.rec M.regen.healthy_diff.rec M.regen.absent.rec M.regen.unreadable.rec M.regen.short.rec M.regen.bad_magic.rec M.regen.bad_version.rec M.regen.oversize.rec N3a N3b N3c N3d N3e N4 N6 |
