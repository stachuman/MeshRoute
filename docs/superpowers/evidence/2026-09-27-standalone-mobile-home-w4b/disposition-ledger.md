<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# W4b §2.9 disposition ledger (coder)

**Contract: brief revision 3** (`a89c075e…8ca5a`, P4 re-pin after the coder's STOP-1 — design r2.22 §6.1, B457).
Every expectation, probe check, renderer control and native mutation that W4b changed, retired or added — old → new,
the reason, and the witness that replaces it. **Base** is `git HEAD` = `c8e36d8` (owner commit; content-identical to
the pre-check inventory, owner ruling 2026-09-27). Each count below is re-derivable: the reconciliation scripts are in
[`recon/`](recon/) and compare `git show HEAD:<file>` with the frozen working tree (the tables here are their output,
annotated). Anything that changed and is not listed here is a STOP (brief §2.9).

## 1. Native expectation classes

Measured per file with the full native binary (base binary = a build of `git archive HEAD`, which reproduces the W4a pin
2986 / 197299 exactly):

| File | Base cases / assertions | W4b cases / assertions | Δ |
| --- | --- | --- | --- |
| `test_firmware_ui_model.cpp` | 380 / 6729 | 421 / 7913 | +41 / +1184 |
| `test_firmware_ui_status.cpp` | 12 / 115 | 13 / 182 | +1 / +67 |
| `test_firmware_ui_chrome.cpp` | 38 / 1933 | 41 / 1996 | +3 / +63 |
| `test_firmware_ui_team.cpp` | 23 / 161 | 23 / 161 | 0 |
| `test_firmware_ui_send.cpp` | 117 / 977 | 117 / 977 | 0 |
| **native total** | 2986 / 197299 | **3031 / 198613** | **+45 / +1314** |

### 1.1 `test_firmware_ui_model.cpp` (brief class: boot, focus and walk, exit destinations, Settings wrap, Home-origin branches, resource sizes)

**Fixture helpers (navigation only).** `to_menu_home(m, s)` is added (walk Home's list to `MENU` by identity, then
`double` → menu mode on Home; idempotent in menu mode) — the retired passive-STATUS boot state is now MENU MODE ON
HOME. Prefixed with it: `to_team`, `to_inbox`, `to_settings` (when not on SETTINGS), `fired_with_outcome`,
`on_team_with_outcome`; `to_inbox_ticks` walks MENU by ticks; `leave_settings_to_status`'s last short is conditional;
`check_passive_status` → Home in list focus. ⛔ No helper asserts anything new or drops an assertion.

**Fixture-prefix-only cases (16, no assertion changed):** `short press is SCREEN-AWARE`, `an empty TEAM list is passed
through`, `a non-team build cycles STATUS -> INBOX -> SETTINGS`, `double on STATUS activates nothing …`, `an empty TEAM
list opens no modal on double`, `blanking KEEPS the modal …`, `B101 — committing an alarm CLOSES …`, `F1 — a mid-frame
GESTURE …`, the three `R2 —` cases, `ui7-chan: a refused canned post is TERMINAL`, `ui17-passive: TEAM and INBOX LAND
PASSIVE …`, `ui17-passive: a passive preview records NO pick …`, `ui16-k7-act: pin 1`, `ui16-k7-keyless: pin 6`.

**Changed expectations (17 cases + 3 renamed; each is a W4b behaviour the design rules, never a weakening):**

| Case | Old → new | Reason (design r2.21) |
| --- | --- | --- |
| `ui-model: SEND double opens the channel compose list …` | result ack → `compose none` (passive SEND) → Send LIST, item 1, list focus | §6.5: acknowledging returns to the Send list |
| `ui-model: the channel list's last row is back` (renamed `… — MENU since W4b —`) | back row → passive SEND → MENU → menu mode on Home | §6.5: the Send list's exit row is MENU |
| `ui-model: B64 — the refusal is retired …` | leaving TEAM lands passive TEAM → menu mode on Home | §6.1 rule 2 |
| `ui-model: dirty starts true …` | + the boot tick captures Home's list (a visible change, owed a frame) | §6.1 rule 1 |
| `ui14-rows: the row labels …` | `CfgRow::back` label `BACK` → `MENU` | §6.1 Settings |
| `ui14-cycle: SETTINGS is the last slot …` | the walk off the menu's last row → the closed view → **wraps**; leaving via MENU | §6.1 Settings (M101 revised) |
| `ui14-back: BACK is safe …` | BACK → closed view on SETTINGS → MENU → menu mode on Home; draft kept (no writes) | §6.1 Settings (M102 revised) |
| `ui15-close: leaving SETTINGS by its BACK row …` | the same MENU destination | §6.1 Settings |
| `b232-exit` (renamed `… — W4b: MENU goes Home in menu mode, the walk WRAPS`) | both exits → closed view → MENU → Home menu mode; walk off last row wraps | §6.1 (a NAMED revision of B232) |
| `ui17-back` (renamed `… — W4b: MENU enters menu mode on the Home slot`) | BACK → passive same screen → MENU → menu mode on Home | §6.1 rule 2 (M109 revised) |
| `ui17-lex: the list's BACK row …` | the top-level exit spelling `kListBackText` → `kListMenuText` ("MENU") | §6.1 rule 2 |
| `ui17-entered: ONE predicate …` | STATUS / SEND `interactive` → entered (they have list focus now); passive → not | §6.1 rule 1 (one focus authority) |
| `ui17-passive: a double on a passive list ENTERS it …` | the prefix, and one more `short` after its `MENU` (the rail now walks from Home to reach TEAM); assertions unchanged | fixture (§6.1 rule 2) |
| `ui17-empty: an empty roster and an empty inbox …` | the exit lands in menu mode on Home; the rail walks from Home | §6.1 rule 2 |
| `ui16-K4: the note occupies …` | the key-received landing `ListView::passive` → **interactive** (Home, list focus) | ruling 2 (report §3): the landing screen's ordinary state |
| `ui16-reqpubkey-resources`, `ui16-k7-resources`, `ui16-k5-resources`, `ui16-k6-resources`, `ui10-p3-resources` | `sizeof` re-syncs only: UiState 504 → 520, UiSnapshot 1336 → 1368, UiModel 928 → 944 (15 lines, each annotated `W4b re-sync (owner-ruled §11.1)`) | §2.2 (owner-ruled allocation) |

**Kept (brief):** the lost-record/member refusal, safe confirmations, result delivery, R-7, unread and detail cadence
cases are unchanged except for the fixture prefix.

**New — the native navigation matrix (38 `w4b-` + 3 `b457` cases = 41, +1125 assertions)**, pre-check §8's list through the real
`on_gesture` / `on_tick`:
`w4b-profile` ×2 (five profiles, order, key-missing precedence, capability gating) · `w4b-refresh` (the pure capture
refresh) · `w4b-boot` (5 profiles × boot/list/menu) · `w4b-menu` ×2 (rail walk incl. gateway skip, double opens each in
list focus, SETTINGS over no service, MENU from TEAM/INBOX/SEND/SETTINGS, empty list) · `w4b-wrap` · `w4b-items` ×4
(INBOX/TEAM/SEND/MENU, MY DEVICE, KEY HELP incl. the vanished opener, setup returns incl. a removed opener) ·
`w4b-changed` ×4 (the §6.4 table: raise lit/dark, short/double clear and run nothing, wake, further change, emergency,
menu mode, counts, same-tick ruling) · `w4b-blank` ×2 (5 screens × 2 focus × {short, double, message wake}; sub-views +
setup note across the millis rollover) · `w4b-emergency` ×2 (arm/cancel from 10 states + 3 sub-views; fired alarm from
Home and from the Send list) · `w4b-admit` ×4 (clean/unsaved/conflict/both/unavailable/no adapter/recovered with
counted loads, writes and applies; Home INVITE ungated ×3) · `w4b-origin` ×7 (the §2.5 trace and its Settings
counterpart; 7 menu-return sites × {home, settings}; NEARBY BACK and saved-key decline; invite BACK, grant verdict,
closed window; fresh action keeps home; pre-emption retires, roster GrantOrigin separate) · `w4b-send` ×4 (result ack,
catalog change, the note's press table, the row labels) · `w4b-inbox` (list + menu preview commit, Home + aborted frame
do not) · `w4b-freeze` (projection only) · `w4b-resources`.

**Revision 3 additions (design r2.22):** `b457` ×3 — Home `INVITE MEMBER` over a CLOSED service (the flow runs ungated;
an emergency hold pre-empts it → the closed `CFG UNAVAILABLE` view in MENU MODE; `double` refused; `short` walks the
rail to Home; recovery: the store answers → the service opens → `double` opens the menu in list focus), the same for a
TEAM-roster grant, and the OPEN-service counterparts (both still land in the Settings menu, list focus). The two
clarifications as explicit cases: `w4b-changed: a press in the SAME tick as the change that raises the note is consumed
and the note STAYS (design r2.22 §6.4)` (retitled — it was the Ruling-4 case) and `w4b-send: with NO phrases, PRESET
CHANGED covers the Send list's MENU row for ONE press (design r2.22 §6.5)` (new).

### 1.2 `test_firmware_ui_status.cpp` (brief class: old STATUS rows retired and replaced; coordinate and fix kept)

| Retired case (base) | Replaced by |
| --- | --- |
| `row 0 names the team in EIGHT uppercase hex, and says NO TEAM for id 0` | `w4b-home: row 1's team line …` |
| `row 1 is the team-local id, BLANK with no team, and ME NO ID before team-DAD` | `w4b-home: row 1's team line …` (NO ID) |
| `row 2 says KNOWN — never HEARD …` | `w4b-home: INBOX and TEAM carry the STRIP's tokens …` |
| `row 2's NO TEAM KEY outranks the count …` | `w4b-profile` (model: key-missing precedence) |
| `row 3 combines the two unread counts …` | `w4b-home: INBOX and TEAM carry the STRIP's tokens …` |
| `row 3's HOME half is -- …` | — the HOME age is the strip's alone now (chrome cases, unchanged) |
| `gateway_heltec's shape … claims NOTHING` | `w4b-home: gateway_heltec's shape — no team plane …` |
| `EVERY row of the fully-populated body fits its own column budget` | `w4b-home: the action words … every row fits 19` + `w4b-mydevice` widths |

Changed (3, coordinate/fix kept): `row 4's priority …`, `(0,0) is NO LOCATION …`, `the coordinate TRUNCATES …` —
the width constant `kStatusWideCols` → `kHomeCols` (the retired constant's value, 19); assertions unchanged.
New: 9 `w4b-` cases (row 0 ME + identity at 0/4/16/17/19/20/32 bytes and high bytes; the team line; INBOX/TEAM tokens;
action words, MENU and widths; the My-device split at 0/16/17/19/20/32 and high bytes on BOTH rows; ID and position;
the notes; the body invalidation; the gateway shape).

### 1.3 `test_firmware_ui_chrome.cpp` (brief class: cue projection and equality; the new sub-view mapping)

New: `w4b-chrome` ×3 — the cue on 5 screens × 2 focus states, emergency normalisation, `UiChrome` 20 / align 2; cue-only
equality + invalidation; Home's sub-views → STATUS, the setup-block note → SETTINGS. Changed: `chrome-nav: the mapping
tracks the LIVE model …` — the fixture prefix plus one assertion that menu mode on Home still boxes STATUS (+1).
Prefix only: `chrome-nav: a REAL outcome landing on a live compose modal …`.
Strip tokens, priorities and emergency suppression: unchanged.

### 1.4 `test_firmware_ui_team.cpp`, `test_firmware_ui_send.cpp` (navigation fixtures only)

Each gains its own `static to_menu_home` (asserting nothing) and prefixes: team — the `team_settle` helper; send — the
`goto_inbox` and `N6Fix::to_verdict` helpers and six prefix-only cases (`ui-recv: the arrival serial WRAPS …`,
`ui-frame: F2 …`, `ui-frame: F3 …`, `ui7-B113 …`, both `ui7-slot` cases). ⛔ No row, send or tracker assertion changed — their
case and assertion counts are identical to base (23 / 161, 117 / 977).

## 2. Firmware-UI probe checks (`tools/probe_firmware_ui/probe_main.cpp`)

Source reconciliation (`recon/chk_recon.py`): **840 → 873 labels; 817 kept verbatim; 6 same label, changed expression;
17 retired; 50 new.** Final-run figures: see the report (base 529 / 980 / 529).

**Helpers (fenced by the brief):** `rail_cue_slot` / `is_cue_rect` (the gutter bar, x = 10, w = 2, one slot high; −2 on
more than one); `body_rects_on_page` excludes the cue by its own geometry; `rail_boxed_slot` / `rail_frames_on_page`
unchanged (x = 0, w = 10) — **exactly one box stays asserted**; `leave_list` reaches MENU MODE through `>MENU` (+700 ms
past the 2 Hz throttle before the read — a measured stale-frame early return); `walk_to_slot` / `to_cfg_menu` comments;
`status_row` / `status_row_is` / `kStatusNarrowColsExp` retired (unused); `kStatusTextXExpected` / `kStatusMark*`
kept as the FORBIDDEN literals the negative witnesses read. Fixture entry sequences: P3's first SETTINGS arrival is a
menu-mode walk; P9a/P9c reach SEND by the rail (`enter_list`).

| Check(s) | Disposition | Reason / witness |
| --- | --- | --- |
| P3u "…entry row replaces the notice" | expression: entry row without `>` | §6.5 preview has no arrow (C92, W4b-N4) |
| P6j "…walk reaches the BACK row" → "…MENU row" (+ no cue); P6j "one more short comes HOME" (expr `>MENU`) | rewritten | §6.1 rule 2 |
| P6k "double on BACK returns to the PASSIVE list" / "…passes the screen" → "double on MENU enters MENU MODE on the HOME slot" / "…walks the rail from Home to TEAM, the cue with it" | rewritten | §6.1 rule 2, cue (W4b-N1, W4b-N3) |
| P7b "the STATUS body no longer carries the withdrawn marker TEXT" → "the Home body carries no configuration marker TEXT (R-3)" | relabelled, same expression | C84 re-anchored |
| P7e "…on Home's row 2, the list window shrunk to rows 3-4" | new | C35 re-anchored |
| P14a "STATUS draws exactly ONE more non-text record …", "…MeshRoute mark ASSET at 24x24 on 12,12", "…STATUS row 0 … at the narrowed x=40 origin" | **retired** with the STATUS mark and rows | replaced by the negative witness "Home draws NO mark …", "…no Home row at the old x=40 origin", the x=12 positive term and the menu-mode 13-record census (W4b-N2a, C99, C124) |
| P14a cue checks ×3 (menu mode one cue; not a second box; list focus no cue) | new | §6.1 rule 4 (W4b-N1) |
| P14d "…passive form offers no BACK row" → "…no exit row" (MENU and BACK) | relabelled + expression | the leak would now read MENU (C93) |
| P14f "…STATUS uses BOTH origins, rows 0-2 at 40 in 14 cols" → "…Home draws its rows at the ONE ordinary origin, in 19 columns, none at 40" | rewritten | §6.2 / §2.7 (W4b-N2b, C108) |
| P17a ×6 (STATUS rows at x=40/12 + split) | **retired** | replaced by the six Home P17a checks (exact bytes; tokens against the strip) (W4b-N7, W4b-W1) |
| P17b ×4, P17c ×2 | expression: row 4 → My device row 3 | "Keep P17b's position proof, moved to My device" (C102, C103, C104) |
| P17m ×4 (My device rows), P17n ×9 (publication 0/3/32/6-byte names ×2 surfaces + BACK on MY DEVICE), P17g ×2 (frozen pages), P17r ×8 (press-free refresh: fix, name, local ID, team ID, profile ×2, strip-token guards) | new | §2.10 (W4b-W1/W2/W3, W4b-N5) |
| P18b "…the shared BACK row" → "…the shared MENU row" | rewritten | §6.1 rule 2 (W4b-N3) |
| P29b fmt "BACK follows unchanged at row 3" → "the exit row follows unchanged at row 3" (DM ` back, don't send`, channel ` MENU`) | rewritten | §6.5 |
| P29c entry row without `>` | expression | §6.5 |
| P29d ×11 (v3: Home CREATE confirmation + BACK; Home JOIN unsaved/conflict note rows, no arrow, rail SETTINGS, nothing saved/loaded, press → Home on JOIN) | new | §6.6 (W4b-N6) |
| P3v ×4 (v3, inside P3u's closed-service window; a ready profile set up and restored): Home INVITE opens ungated over the closed service; after a hold arms and is cancelled SETTINGS shows exactly `CFG UNAVAILABLE` on row 2; the cue beside SETTINGS and no arrow; `short` → Home with the cue | new — brief rev 3's real-render check (B457) | the model rule (native H25); measured in the dev loop: with the rule deleted (scratch `src/` copy) the menu-mode and `short` checks go RED (the text check stays green — the defect draws that text too). No `run.sh` control targets the v3-only P3v checks (roll-up exception) |

**Kept:** W4a name bytes (P28/P30), W3 paging and compose width (P29a/P29b bytes), every send and grant effect — their
assertions unchanged; only fixture navigation moved (P28c's loop was repaired by the `leave_list` throttle fix alone).

## 3. Renderer controls (`tools/probe_firmware_ui/run.sh`)

`recon/ctl_recon.py`: **236 → 238; 219 kept verbatim; 8 re-anchored; 9 retired; 11 new.** Final run: 238 verified /
0 unusable; the stage-A accounting "238 declared, 238 exactly once with the accepted outcome, 0 guard failure(s)"; the
independent count 238 = 238.

| Control | Disposition | Fails (measured, dev loop + full run) |
| --- | --- | --- |
| C83; C96–C98; C100–C101 | **retired** (kept visible as comments, with replacements) | replaced by Home's rows/placement/x12 checks and W4b-N2b / W4b-N7 / W4b-W1 |
| C121, C122, C123 | **retired** (all edit the deleted mark `draw_bitmap`) | replaced by P14a's negative witness and W4b-N2a |
| C35 | re-anchored onto `if (c.reboot) { body_text(2, …` → `if (false)`; label "…reaches the landing screen" | P7e (both) |
| C84 | re-anchored: Home's team line → the config marker text; label "…landing body" | P7b; P17a row 1; P17r team-line rows |
| C92 | re-anchored: `body_text(top, kSettingsEnterText)` → `"SETTINGS"` | P3u entry row, P7 landing, … (79 checks) |
| C93, C94 | **kept verbatim** (the predicate line is unchanged) | P6h / P14d; P6i |
| C95 | re-anchored onto `body_menu_row` ×2 (label "exit row (MENU)") | P6j/P6k … |
| C99, C124 | re-anchored with literal 12/12/24/24 on the `draw_hline` line | C99: P14a no-mark + census; C124: P14a body-rect census |
| C102 | re-anchored onto My device's `ui_status_location(…, false, s)` (live config) | P17b |
| C103, C104 | **kept verbatim** (publisher lines unchanged) | P17c |
| C108 | re-anchored: literal `draw_text(40, body_y(row), l)` | P14f; P18a "no TEAM row at the STATUS origin" |
| C90, C91, C105–C107, C109, C110 and every other control (C0 `must_build=no` included) | **kept verbatim** | their existing checks (full run: RED / C0 build failure) |
| W4b-N1 cue inverted · N2a mark on Home · N2b Home rows at x=40 · N3 exit row BACK · N4 Settings preview `>` · N5 My device abbreviated · N6 (v3) blocked note on row 2 · N7 Home window one row low | **new**, `once`-guarded, `must_build=yes` | N1: P6j/P6k/P14a cue; N2a: P14a no-mark + census; N2b: P14a/P14f/P17a x40; N3: P6j/P18b; N4: P3u; N5: P17n; N6: P29d rows; N7: P17a rows |
| W4b-W1 publisher omits the own name · W2 renderer reads the live name · W3 `ui_home_invalidate` left out | **new** — §2.10's three wiring controls | W1: P17a row 0 + P17n; W2: P17g; W3: P17r (fix, name, local ID, team ID) |

The board-UI reader W41's bare box statement is untouched; the cue is its own statement after it (W41 green in the
board-UI `--no-neg` run).

## 4. Native mutations (`tools/probe_ui_model_mutations.py`, AST-edited, never imported)

`recon/mut_recon.py` — batteries whose entry lists changed: `model`, `uistatus`, `chrome`, and the new `w4bhome`.

**`model` 239 → 239 (220 verbatim, 19 changed, 0 retired):**
- *Re-anchored, meaning and label kept:* M58 (`prov_block_note` realigned), M68/M69/V29 (BACK via `provision_menu_exit`),
  M83 (JOINING's exit), M106 (the auto-enter written on the menu-mode advance line — `list_follow_screen` is gone), N04
  (the re-read rides `tick_invite(s);`), N05/N06 (nearby BACK via the exit), V03/B249-1/-2/-3 (anchor now carries
  `case ProvRow::invite:` — Home INVITE repeats the three steps), V40 (the landing is `home_return()`), W15/W16 (the
  arms gained the Home-origin terminal line).
- *Revised contracts (label says so):* **M101** now re-inserts the withdrawn Settings walk-off (Settings wraps);
  **M102** MENU closes the menu but stays on SETTINGS instead of menu mode on Home; **M109** the TEAM/INBOX exit returns
  to the passive same screen instead of menu mode on Home.
- *Kept verbatim:* M107, M110–M120 (passive-no-action, containment, stale pick), S03 (a committed alarm closes compose),
  and the M55–59/M62–72/M76–78/M83–88, V- and W- families outside the re-anchors above.

**`uistatus` 13 → 19:** S02–S04, S13 kept verbatim. **Retired (visible, each with its replacement):** S01 (→ the
probe's C35 on P7e; `ui_status_location`'s restart arm stays pinned natively by `ui17-status`), S05 (→ S14), S06
(→ S16), S07 (→ S15), S08 (→ S18), S09 (→ `w4bhome` H01), S10 (→ S19), S11 (→ S20), S12 (→ the chrome home-age
entries). **New S14–S28:** team line — NO TEAM as a zero id, `T0` for a pending ID, no-plane `NO TEAM`, the `0x`
spelling; TEAM item `HEARD` and the zero count; INBOX raw sum and zero count; My device split at 16, row-1 sanitizer,
`NO NAME SET`; row-0 fabrication; `IN SETTINGS` for unavailable; the invalidation's same-length rename and screen.

**`chrome` 44 → 48:** X17–X24 / X27–X30 and every other entry verbatim; new X45 cue always on, X46 cue out of the
equality, X47 cue outside the rail's visibility, X48 setup-block note → STATUS.

**`w4bhome` (new, 26 → `src/firmware_ui_model.h`):** the brief's eleven (revision 3) — — H01 key-missing precedence; H02/H03 capability
gating (JOIN, INVITE); H04 arrow by row; H05 OPTIONS CHANGED not raised; H06 not refusing the double; H07 MENU leaves list
focus (wrong focus); H08 a blank re-homing; H09 an OQ-3 cancellation removed; H10 (+H10b PROVISION BACK) the Home-origin
exit to the PROVISION menu; H11 the blocked reason re-read; H12 a PRESET CHANGED press that sends — plus H13–H15 (Home
INVITE snapshot/announcement), H16 admission bypassed, H17 no unavailable reason, H18 return to item 1 not the opener,
H19 Settings PROVISION does not type the origin, H20 origin not retired, H21/H22 the Send list result/catalog returns,
H23 the same-tick note ruling, H24 My device's short — and **H25 (brief rev 3 entry 11, B457): the Settings menu shown
over a closed service** (the rule in `sync_settings` → `if (false)`).

**M103 — kept VISIBLE, not dispositioned by the coder** (QA decides at the freeze). Unchanged entry: it deletes BOTH
`_st.cursor = 0` and `_cfg_sel_valid = false` from `close_settings_menu()`. It was non-RED in the coder's pre-run
(before revision 3): the one caller then (`CfgRow::back`) is followed at once by `go_menu_home`, which repeats both
resets. Revision 3 adds a second caller (the B457 rule in `sync_settings`, which then sets menu mode). Its result in the
fresh final chain is reported as measured.

**PIN:** `PIN_CASES, PIN_ASSERTS = 2986, 197299` → `3031, 198613`, with its derivation line (§1's table).

## 5. The other fenced instrument

`tools/probe_board_abi.py` `PIN_TABLE`: UiState 504 → 520 (three ABIs), UiSnapshot 1336 → 1368, UiModel 928 → 944 native
/ 912 → 936 `heltec_mobile` and `gateway`, one derivation block; the stock `--repin` block equals the table.
`tools/test_probe_board_abi.py`: no dependent size assertion (grep) — untouched.
