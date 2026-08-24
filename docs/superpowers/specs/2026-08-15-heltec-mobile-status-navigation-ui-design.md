# Heltec mobile status strip and navigation rail redesign

**Date:** 2026-08-15  
**Status:** ⛔ **IMPLEMENTED (UNCOMMITTED) 2026-08-16 across §CHROME-1..4** — the four §10 slices have all landed.
⛔ **No owner or QA approval of the implementation is claimed here**; the metal acceptance of §12 is outstanding and is
`docs/2026-07-31-bench-test-script.md` Parts 24-25. ⓘ Every amendment block below is dated and several WITHDRAW an
earlier instruction — where an amendment and the original disagree, the amendment wins.
**Parent design:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`  
**Applies first to:** `heltec_mobile`; every OLED build must still compile and render honestly

## 1. Purpose

Replace the text-heavy OLED header with a glanceable status strip and add a persistent left navigation rail.

The two regions answer different questions:

- the **top strip** answers *“what is happening?”* — unread traffic, home-link state, team presence, team-key
  availability and battery voltage;
- the **left rail** answers *“where am I?”* — STATUS, TEAM, INBOX, SEND or SETTINGS.

This is a presentation and projection change. It does not change radio protocol, routing, mobile attachment,
message storage, settings persistence, gestures or send semantics.

## 2. Current behaviour being replaced

The current header is drawn in the 6x10 font at `y=7`, above a rule at `y=9`:

```text
DM<n> CH<n> T<shown>/<total> <voltage>
```

For example:

```text
DM2 CH5 T3/3 4.1V
```

Its fields have narrower meanings than their labels suggest:

| field | current authority and meaning |
|---|---|
| `DM<n>` | session unread DM arrivals: received since an INBOX list frame last completed visibly; not stored total |
| `CH<n>` | session unread channel arrivals under the same rule; not stored total |
| `T<a>/<b>` | `min(rt_team_count(), 8)` rows available to the UI versus `rt_team_count()`; not online/total |
| voltage | last valid battery sample, truncated to one decimal; `--` until one succeeds |

The new design must not silently reinterpret any of these values. In particular, neither a route-table entry nor a
presence roster is proof of general mesh connectivity.

## 3. Fixed display geometry

The panel remains 128x64 pixels. The existing fonts remain:

- `Font::small` = 6x10;
- `Font::large` = 10x20, emergency headline only.

No third font and no icon font are added. Icons are repository-owned monochrome bitmaps.

### 3.1 Top status strip

- `y=0..8`: status strip;
- `y=9`: existing full-width horizontal rule;
- the strip always has the full 128-pixel width;
- all icons fit within a 7-pixel height;
- text uses the existing small-font baseline at `y=7`.

The logical order is fixed:

```text
[mail][count] [home][age] [people][count] [key] [battery][voltage]
```

A conforming 128-pixel implementation must reserve fixed slots. Values are shortened to the limits in §4 rather
than pushing a later slot off-screen. Battery is right-aligned so `--` and `4.1V` do not move the preceding icons.

An indicative allocation, to be pinned by the renderer probe, is:

| item | pixel budget |
|---|---:|
| mail icon + up to `99+` | 25 px |
| home icon + compact age | 25 px |
| people icon + up to `9+` | 20 px |
| team-key icon | 8 px |
| battery icon + `4.1V` | 35 px |
| spacing/reserve | at least 15 px |

The exact `x` coordinates belong to one layout table in the renderer; they must not be repeated at individual draw
sites.

★ **AMENDMENT 2026-08-16 (§CHROME-3 implementation) — THE ALLOCATION ABOVE IS INDICATIVE AND SAYS SO; THESE ARE THE
PINNED VALUES.** The table lives in `src/firmware_ui.cpp` as one `kStrip[]` and is asserted independently by
`tools/probe_firmware_ui`'s P13a/P13b/P13c (which state the coordinates themselves rather than importing them, so a
bound cannot agree with a layout that has drifted):

| slot | glyph x | token x | right edge at its widest token |
|---|---:|---:|---:|
| mail | 0 | 8 | 25 (`99+`) |
| home | 28 | 36 | 53 (`59m`) |
| people | 56 | 64 | 75 (`9+`) |
| team key | 79 | — | 85 |
| battery | 91 | 104 | **127** (`4.1V`) |

⇒ `26 + 2 + 26 + 2 + 20 + 3 + 7 + 5 + 37 = 128` px exactly, with the battery's last column landing on x = 127.
Icons occupy `y = 0..6`; tokens sit on the existing `y = 7` baseline; the rule stays at `y = 9`.

★★ **AND THE READING OF "RIGHT-ALIGNED", stated because it is the one sentence here that admits two implementations.**
The design's own justification is *"so `--` and `4.1V` do not move the preceding icons"* ⇒ the battery **SLOT** is
anchored to the right edge and its glyph and token sit at **fixed** x inside it, so a shorter token leaves the trailing
columns empty rather than dragging anything. ⛔ The alternative — right-aligning the TOKEN's last pixel to x = 127 —
would move the token away from its own outline as the value narrows, and a flowed layout that packed each field after
the previous one would satisfy the sentence and break the picture the moment the mail count reached three digits.
P13c measures the ruled property directly: with a `4.1V` token and with a `--` token, **every earlier glyph is at the
same x**.

★ **AMENDMENT 2026-08-23 (§CHROME-5 — the DUTY gauge; owner+QA ruled, mirrored in the parent design §3.3).**
A sixth slot: a **7×7 duty-utilization gauge**, ⛔ **icon only, never a percentage** (the exact percentage and the
recovery time stay in the `duty` console verb and the companion diagnostics). The strip was exactly full at the
2026-08-16 pinned values, so the gaps shrink from the 2/3/5-px reserves to **one pixel between every pair** —
battery is ⛔ untouched:

| slot | glyph x | token x | right edge at its widest token |
|---|---:|---:|---:|
| mail | 0 | 8 | 25 (`99+`) |
| home | **27** | **35** | **52** (`59m`) |
| people | **54** | **62** | **73** (`9+`) |
| team key | **75** | — | **81** |
| **duty** | **83** | — | **89** |
| battery | 91 | 104 | **127** (`4.1V`) |

⇒ `26 + 1 + 26 + 1 + 20 + 1 + 7 + 1 + 7 + 1 + 37 = 128` px exactly, one-pixel gap between every pair, battery's
glyph/token x and right-anchoring unchanged. The three visual states, and their ONE semantic authority:

- **crossed gauge** — duty limiting disabled;
- **empty-to-full gauge** — approximate utilization, 0–99 %;
- **full gauge + warning mark** — 100 %: transmission currently duty-blocked.

★ **The authority is `Node::duty_status()`** (`lib/core/node_mac.cpp:1716`, `DutyStatus{pct, avail_ms, enabled}`
— `enabled == false` ⇒ crossed; `pct == 100` ⇒ blocked) — ⛔ **never raw `duty_ms` and never the separate
five-minute anti-spam budget**, which answer different questions. The reading is snapshotted once per frame,
**classified into its bucket BEFORE it enters the frozen `UiChrome`** (the renderer sees a bucket, never a pct),
and a repaint is owed **only when the visible bucket changes**. ⛔ No wire, NV, routing or `Node` change —
`duty_status()` is an existing `const` accessor. Implementation = the small plan
`docs/superpowers/plans/2026-08-23-chrome5-duty-gauge.md` (⛔ deliberately NOT part of §UI-16); the P13 probe
bounds restate the moved coordinates themselves, per this section's own rule.

### 3.2 Navigation rail

The rail exists below the status rule only:

- rail: `x=0..9`, `y=10..59`;
- normal body origin: `x=12`;
- normal body width: 116 px, therefore at most 19 fixed-width small-font characters;
- body baselines remain `19, 29, 39, 49, 59`.

Five 10-pixel slots align with those five body rows. Each contains a 6-7 pixel icon:

| order | screen | icon |
|---:|---|---|
| 1 | STATUS | dashboard/information |
| 2 | TEAM | people |
| 3 | INBOX | envelope |
| 4 | SEND | paper plane/outgoing arrow |
| 5 | SETTINGS | tools/gear |

The active icon has a one-pixel rectangular frame around its slot. The rail is an indicator, not a touch target;
the one-button gesture contract is unchanged.

Builds where TEAM/SEND are unavailable do not draw misleading dead icons. Their canonical slots remain empty; the
remaining icons keep the same locations rather than acquiring a second layout.

★ **AMENDMENT 2026-08-16 (§CHROME-4 implementation) — THESE ARE THE PINNED RAIL VALUES**, stated for the same reason
§3.1's strip table was: the numbers above are a description, and the renderer needs one table. It lives in
`src/firmware_ui.cpp` beside `kStrip[]` and is asserted independently by `tools/probe_firmware_ui`'s P14 (which states
the coordinates itself rather than importing them, so a bound cannot agree with a layout that has drifted):

| item | value |
|---|---:|
| rail column | `x = 0..9` (`kRailX` 0, `kRailW` 10) |
| slot `i` | `y = 10 + 10i`, height 10 ⇒ slots at 10, 20, 30, 40, 50 |
| glyph in slot `i` | `x = 1`, `y = 11 + 10i`, 7x7 — one clear pixel inside the frame on every side |
| selection frame | `draw_rect(0, 10 + 10i, 10, 10)` — a one-pixel OUTLINE, ⛔ never `drawBox` |

⇒ slot `i`'s LAST row is `19 + 10i`, i.e. exactly its body row's baseline, and the fifth slot ends on `y = 59`.

★★ **AND THE SLOT'S `y` IS A FUNCTION OF THE ENUMERATOR, NOT OF A RUNNING COUNTER** — `draw_rail` walks
`NavSlot(i + 1)` and `continue`s on an unavailable slot. That is what makes this section's last sentence STRUCTURAL
rather than careful: nothing below a slot is positioned relative to it, so an empty slot cannot move anything.
⛔ It is pinned by `tools/probe_board_ui`'s **W41** rather than behaviourally, and the reason is stated plainly: **no
host variant of `probe_firmware_ui` can compile a `!MR_FEAT_TEAM` `firmware_ui.cpp` against a team-enabled
`lib/core`**, so the one build where TEAM/SEND are absent (`gateway_heltec`) is reachable only by the LINKER and by
§12's bench check 25.7.

## 4. Status-strip semantics

### 4.1 Mail

The envelope number is the combined session-unread value:

```text
unread_dm + unread_ch
```

It preserves the current read rule:

- arrivals increment their existing uncapped serials;
- a fully drawn, actually visible INBOX list frame advances the read watermarks;
- opening or paging a message detail does not mark the list read;
- deletion does not itself define read state;
- reboot resets the UI-local session counters, as it does today.

Rendering is:

| true combined value | rendered value |
|---:|---|
| 0..99 | exact decimal |
| 100 or more | `99+` |

This is deliberately not `inbox_total`. A stored-total badge would remain non-zero after reading and, on the
Heltec's RAM inbox, disappear after reboot. The INBOX body remains the place for stored-row counts and DM/channel
identity.

### 4.2 Home link

The house indicator reports the existing **mobile-home plane**, not general mesh connectivity and not arbitrary RF
activity.

Its state comes from the existing `mobile_home_link()` projection:

| core state | icon |
|---|---|
| `unknown` | neutral/empty house |
| `confirmed` | normal house |
| `checking` | house with question marker |
| `lost` | crossed house |

The adjacent age is sourced from the existing `mobile_home_confirmed_ever()` and
`mobile_home_confirm_age_ms()` accessors. It means:

> age of the latest correlated bidirectional confirmation with the selected home

It must never be labelled or described as “connected”, “mesh online”, or “last packet heard”. A team message, a
foreign beacon or a one-way receive must not refresh it.

The 64-bit age remains 64-bit until the display formatter deliberately buckets it. A cast to 32-bit milliseconds is
forbidden; it would reintroduce the already-fixed approximately 49.7-day wrap.

Compact age rendering is:

| age | token |
|---|---|
| never confirmed | `--` |
| 0..59 seconds | `0s`..`59s` |
| 1..59 minutes | `1m`..`59m` |
| 1..23 hours | `1h`..`23h` |
| 1..99 days | `1d`..`99d` |
| 100 days or more | `old` |

On a non-mobile OLED build the home slot is blank, not crossed. “Not applicable” must not be rendered as a fault.

### 4.3 Team presence

The people indicator reports team routes known to the active runtime:

- no configured team: neutral people icon and `--`;
- configured team with zero team-route rows: people icon and `0`;
- configured team with one through nine rows: exact count;
- ten or more rows: `9+`.

The value uses the true `team_total`, never `team_shown`. The old `T8/12` UI-capacity fraction is retired.

The product wording is **“teammates heard/known”**, not “members online”. Individual route ages remain on the TEAM
screen. This slice does not invent an online threshold, a membership database or a presence timeout.

### 4.4 Team content key

The key icon means the shared team-channel **content key**, using `team_channel_key_present()`:

| team configuration | content key | icon |
|---|---:|---|
| no team | irrelevant | neutral/blank key slot |
| team configured | absent | crossed key |
| team configured | present | normal key |

It does not mean that the node has its own crypto identity or that peer public keys are cached.

### 4.5 Battery

Battery keeps the existing voltage authority and formatter semantics:

- fixed battery outline plus one-decimal voltage, including `V`;
- `--` before any valid measurement;
- last valid reading remains visible if a later sample is unavailable;
- truncation to one decimal is preserved;
- no percentage is introduced.

The battery outline is initially unfilled. Fill levels would imply an approved chemistry/discharge model and are
outside this design. A low-battery threshold is likewise out of scope.

## 5. Navigation and modal rules

### 5.1 Normal screens

The selection frame follows the frozen `UiState::screen`, never a renderer-local cursor.

Short-press screen cycling is unchanged. Moving the frame is a consequence of the existing state transition, not a
new transition.

### 5.2 Body-replacing views

The rail must describe the body actually being shown:

| visible body | selected rail icon |
|---|---|
| Inbox list, Inbox detail, `MESSAGE GONE` | INBOX |
| DM/channel compose list or send result | SEND |
| settings editor | SETTINGS |
| ordinary screen | corresponding `UiState::screen` |

The mapping is one pure function with an exhaustive switch. It must not be re-derived in the renderer and model.

### 5.3 Emergency exception

When `Emergency != idle`:

- the navigation rail is not drawn;
- the emergency body keeps `x=0` and the full 128-pixel width;
- the top status strip remains visible, as today.

This exception is load-bearing. The 10x20 emergency headlines already use the full 12-character width; shifting them
to `x=12` would clip safety text such as `NO RELAY HRD`.

## 6. Configuration-state indication

The SETTINGS rail icon carries the persistent configuration badge. A separate top-strip configuration icon is not
used; it would spend scarce width on a fact that already has a permanent semantic home.

The badge states are:

| condition | Settings icon treatment |
|---|---|
| clean | ordinary tools icon |
| unsaved OLED draft | tools + dot/asterisk |
| external persisted change conflicts with open draft | tools + exclamation |
| saved change requires reboot | tools + restart marker |

If more than one condition is true, the visible priority is:

```text
conflict > unsaved > restart-required > clean
```

This priority is only the compact badge. SETTINGS must continue to render actionable text distinguishing
`UNSAVED`, `RELOAD`/conflict and `RESTART NEEDED`. One small icon cannot safely replace those instructions.

The redundant `CFG* UNSAVED` / `CFG! RELOAD` decoration is removed from the STATUS title. The rail makes the state
visible from every ordinary screen.

The badge describes the OLED `ConfigService` draft and its reboot latch. It is not a new universal assertion that
every live-only serial/BLE setting equals or differs from flash. Broadening that authority is outside this slice.

### 6.1 AMENDMENT 2026-08-16 (QA-gate) — removing the STATUS markers RETARGETS LIVE INSTRUMENTS; the slice owns that

⛔⛔ **CORRECTED AT QG 2026-08-16 ON TWO COUNTS, BOTH KEPT VISIBLE (§3 rule 3).** The first version of this section
(a) published *"48 references across ten files"* above a table totalling **45 across seven** — the headline counted a
wider `grep` (including the register and plan documents) than the table enumerated, and the two were never reconciled;
and (b) instructed the slice to *"retarget every renderer assertion and mutation"*, which is **WRONG**, because
⛔⛔ **`CFG! RELOAD` REMAINS REQUIRED ACTIONABLE SETTINGS/SERVICE TEXT — §6 itself says so four paragraphs above.
Those tests must be RETAINED, not retargeted.** Only the **STATUS-title presentation** is removed.

★★ **THE MIGRATION IS A CLASSIFICATION, NOT A COUNT.** ⛔ Do not act on any total in this document; **re-census at
implementation time** (the tree moves) and sort every reference into exactly one bucket:

| bucket | what it is | action |
|---|---|---|
| **STATUS-title presentation** | the marker drawn on the STATUS screen, which this design deletes | **replace with badge checks** |
| **SETTINGS / service actionable text** | `CFG! RELOAD` and its siblings where SETTINGS or `ConfigService` state the operator's remedy | ⛔ **RETAIN — do not touch** |
| **historical documentation** | register rows, plan/spec records of past slices | **preserve as withdrawn history** |

★★ **AND THE HAZARD THAT MAKES THIS WORTH A SECTION: `tools/probe_ui_model_mutations.py` DEFINES MUTATIONS IN TERMS
OF THESE STRINGS.** Delete a string a mutation targets and that mutation stops reddening anything, while the harness
keeps reporting a control count — **an instrument that cannot fail, which is this project's most-recorded defect
class** (four were found in the §B200 arc alone, each green against the very defect it was written to catch).

⇒ **THE SLICE THAT REMOVES THE STATUS PRESENTATION OWNS ITS INSTRUMENTS:**
1. **retarget** the STATUS-bucket assertions and mutations onto the badge — the *fact* survives, only its presentation
   moves, so the coverage moves with it;
2. ⛔ **never delete a check merely because its string moved** — that silently drops coverage of a §UI-14 behaviour
   nobody decided to stop testing;
3. **update bench expectations in the same slice**, keeping withdrawn wording visible;
4. ★ **the badge's tests must fail against the old STATUS presentation and vice versa**, so the two cannot both pass
   and the transition is provably complete.

⚠ **And §6's own rule stays load-bearing while this moves:** SETTINGS must continue to render actionable text
distinguishing `UNSAVED`, `RELOAD`/conflict and `RESTART NEEDED`. **The icon may replace the STATUS decoration; it may
never replace the instruction.**

### 6.1.1 AMENDMENT 2026-08-16 (§CHROME-4 implementation) — the census AS EXECUTED, and what it found

**Re-run at implementation time** (`CFG* UNSAVED` / `CFG! RELOAD` / `cfg_marker_text`): **102 references across 21
files.** ⓘ Higher than every earlier figure again, and for the same reason each earlier one was wrong — a different
pattern set. **The number is not the point; the classification is**, and it came out as this section predicted:

| bucket | what happened | count |
|---|---|---:|
| ⛔ **RETAIN, untouched** | `src/firmware_config_service.h` (the SERVICE's ruled string) · `src/firmware_config.cpp` · `test/test_firmware_config_service.cpp` · `src/firmware_ui_model.h`'s `cfg_marker_text` / `settings_note` · **all five `tools/probe_ui_model_mutations.py` mutation targets** | 19 |
| **RETARGETED** (the fact moved, so the check moved) | 8 `probe_firmware_ui` checks read off **STATUS** → the badge glyph · 3 width assertions in `test/test_firmware_ui_model.cpp` (`strlen("STATUS ") + marker <= 21` → `marker <= 19`) · `run.sh`'s **C33** | 12 |
| **RETAINED AS TEXT, on SETTINGS** | 9 `probe_firmware_ui` checks that already read the marker on the SETTINGS screen, plus 6 NEW P14g checks requiring the words beside the badge | 15 |
| **historical / documentation** | the spec, plan, register and bench documents | ~56 |

★★★ **THE ONE THING WORTH RECORDING: NOT A SINGLE MUTATION TARGET WAS DELETED, AND NOT A SINGLE TEST WAS.**
`cfg_marker_text` and `cfg_save_panel` both SURVIVE with the same bodies — because the SETTINGS screen still calls
them — so `probe_ui_model_mutations.py`'s M46/M47/C30/C31 all still match at count 1 and still redden. ⇒ the hazard
this section was written about **did not materialise**, and the reason is structural rather than lucky: §6 removes a
PRESENTATION, and the presentation it removes was never the only caller of the string.

★ **§6.1 rule 4's two-directional proof is delivered by a pair of controls, not by argument:** `run.sh`'s **C33**
makes the badge blind (⇒ 5 checks red) and **C84** puts the withdrawn `CFG* UNSAVED` back on the STATUS body (⇒ 1
check red). Neither presentation can be present without the other's control firing, so the two cannot both pass.
**C85** and **C86** do the same for §6's *"may never replace the instruction"*: remove SETTINGS' marker row, or draw
it unconditionally, and the suite reddens either way.

## 7. Body migration to 116 pixels

Adding the rail reduces normal content from 21 to 19 small-font characters. Clipping existing strings is not an
acceptable implementation.

### 7.1 Rules

1. All ordinary body drawing uses the one `kBodyX = 12` authority.
2. Emergency remains the only full-width body.
3. Every rendered normal line is proven at or below 116 pixels.
4. Formatter buffers may remain larger than the visible line to satisfy compiler format analysis; buffer capacity
   must not be confused with pixel capacity.
5. Dynamic labels are explicitly clamped or moved to a second row. Panel clipping is not a truncation policy.
6. Two selectable preset strings must not become visually identical after clamping. If their visible prefixes
   collide, the UI needs distinct short labels; silently relying on the hidden suffix is forbidden.

### 7.2 Titles

The rail replaces label-only headings where doing so loses no information:

- remove the standalone `STATUS` title;
- remove the standalone `SETTINGS` title and use the gained row for the menu;
- retain the INBOX shown/total information, but it may be rendered as compact `stored <shown>/<total>` rather than a
  duplicate title;
- retain SEND destination/context text because `team channel` versus a selected person is meaningful;
- empty-state text remains explicit (`no teammates heard`, `no stored rows`, `CFG UNAVAILABLE`).

### 7.3 Required audit

The implementation slice must audit every normal renderer, including:

- STATUS identity, DM/channel recency and battery detail;
- TEAM labels, compact age and hop count;
- INBOX tag, preview, compact age, gone-row refusal and detail pagination;
- DM/channel preset lists and target headings;
- all send outcomes and failure reasons;
- SETTINGS labels, values, edit brackets, transient notes and restart/conflict text.

★ **AMENDMENT 2026-08-16 (QA-gate) — the audit must cover the strings §T3 ADDED, which postdate the first draft of
this list:** `QUEUED` (`DmState::waiting_ack`, `ChanState::waiting`), the now-earned `SENT, waiting`
(`DmState::aired_waiting`, `ChanState::aired`) and **`NO RELAY HEARD`** (`ChanState::no_relay`, renamed from
`SENT, no relay`). ⓘ All three are ≤ 14 characters and therefore fit the 19-column body — **but "it fits" is a
MEASUREMENT, not an assumption, and the point of §7.3 is that every string is measured rather than eyeballed.**
⚠ `NO RELAY HEARD` is also the string §5.3 cites as the reason emergency stays full-width; the two live at different
font sizes and must not be conflated (the emergency headline is `Font::large`, 12 columns; this one is `Font::small`).

Inbox detail wrapping must use the new visible body width at the model's freeze point. Changing only the draw origin
would clip already-frozen lines and would make pagination lie.

★★★ **AMENDMENT 2026-08-16 (§CHROME-4 implementation) — THE AUDIT'S RESULT, AND IT FOUND FOUR LINES THAT WERE
ALREADY BEING CLIPPED AT 21 COLUMNS.** §7.3's own sentence — *"it fits" is a MEASUREMENT, not an assumption* — is why
these were found at all; none of them is a consequence of the migration, and all four are registered as [[B202]]:

| line | widest reachable | what was being lost | resolution |
|---|---:|---|---|
| TEAM row `%c%-10s %4s %uh` | **25** | the age AND the hop count, on a full 14-column label | `%c%-9.9s %4.4s %uh`, hops bounded ⇒ 19 |
| `DELIVERED to %s` | **27** | the NAME of the person the message reached | two rows: `DELIVERED to` then the label (§7.1 rule 5) |
| INBOX preview `%c%-3s %-9s %4s` | **32** | the age; and a `%-3.3s` tag would have shown `CH2` for channel 255 | `%c%-5s%-8.8s %4.4s` ⇒ 19, tag column widened to 5 |
| `fmt_age` | **6** (`49710d`); hour bucket 5 (`23h59`) | put `DM 999, newest 23h59` at 20 columns | retired onto `mrui::ui_fmt_home_age` (3 columns, boundary-tested) |

⚠ **THREE STRINGS WERE RE-DERIVED RATHER THAN CLAMPED, and each cost is stated rather than smoothed over:**
`TEAMMATE GONE, repick` (21, and its own comment said it was *sized to* the 21-column panel) → **`TEAMMATE GONE, pick`**
(19 exactly, both halves of §B64's ruling intact) · `double = pick a text` (20) → **`double = pick text`** (18, the
article dropped) · the hour token `23h59` → `23h` and any age of 100 days or more → `old`, which is coarser and never
wrong. ⛔ **None of them was solved by letting the panel clip**, which §7.1 rule 5 forbids outright.

★★ **AND A WHOLE-LINE CLAMP WAS WRITTEN AND THEN REMOVED.** `body_text` briefly copied every line into a 19-column
buffer, which made §11.2's *"every normal text line fits the 116-pixel body"* **an instrument that could not fail** —
every drawn line would have been ≤ 19 by construction, so `probe_firmware_ui`'s P14f could never redden and a future
26-column format would have lost six columns of meaning silently, with a comment calling it a policy. ⇒ the width is
proven PER FORMAT (precisions, plus the audit written beside each screen) and MEASURED end to end by P14f.

ⓘ **§7.2's INBOX bullet: `INBOX <shown>/<total>` was KEPT.** The bullet requires the shown/total information to be
retained and offers `stored <shown>/<total>` as a permitted alternative ("may"); the existing form retains the
information at 13 columns, so it was left alone. Only the two titles §7.2 names outright — `STATUS` and `SETTINGS` —
were removed. ★ On SETTINGS the freed row is CONDITIONAL: it carries the `CFG* UNSAVED` / `CFG! RELOAD` instruction
when one stands (§6 requires that text) and belongs to the menu when the configuration is clean, which is the same
conditional-reservation shape `team_pick_gone` / `inbox_pick_gone` already use.

⛔ **CORRECTED 2026-08-16 (QG): this paragraph said "already-frozen 20-character lines". `kDetailCols` is 21**
(`src/firmware_ui_model.h:264`). ⇒ **the migration is 21 → 19 columns, and the page capacity 42 → 38 characters**
(`kDetailPageChars = kDetailCols * kDetailBodyRows`, `:266`). ⚠ **Pagination is derived, so it must be re-derived and
not merely re-clamped** — a page count computed from 21 while the renderer draws 19 is a lie the panel cannot show.

## 8. Architecture

### 8.1 Icons and canvas

The board boundary remains display-independent:

- firmware owns icon identity, bitmap bytes, placement and state selection;
- the board owns only copying pixels to its display library.

★ **AMENDMENT 2026-08-16 (QG): DEFINE THE BITMAP BYTE FORMAT IN THIS DESIGN, not per board.** Use **U8g2/XBM
convention — row-major, LSB-first, one bit per pixel, rows padded to whole bytes** — and state it as part of the
`draw_bitmap` contract. ⛔ Leaving it to the board is how the same asset renders **mirrored or bit-reversed** on the
V4 port, and §8.1's whole purpose is that the V4 port reuses these bitmaps unchanged. A conformance test drawing one
asymmetric glyph is enough to pin it.

Add generic canvas primitives, not semantic board calls:

```cpp
draw_bitmap(x, y, width, height, bits)
draw_frame(x, y, width, height)
```

`board_ui.cpp` must not gain functions such as `draw_mail_icon()` or include `firmware_ui_model.h`. The V4 port must
be able to reuse the same bitmaps and renderer.

★ **AMENDMENT 2026-08-16 (§CHROME-2 implementation) — THE OUTLINE PRIMITIVE IS NAMED `draw_rect`, NOT `draw_frame`.**
The block above names it `draw_frame(x, y, width, height)`. ⛔ **That name is already taken by a different concept in
the layer that will call it:** `draw_frame` is `src/firmware_ui.cpp:946`'s WHOLE-SCREEN composer — the function the
page loop calls once per page to draw the entire scene. Keeping the design's name would leave the slice-3/4 renderer
calling two `draw_frame`s in the same function, one meaning *"compose the entire screen"* and one meaning *"draw a
rectangle outline"*. They are separable by namespace (`mrui::draw_frame` vs the file-local one) and would compile;
a reader's mistake is the cost, and it is cheaper to avoid than to document.
⇒ **`draw_rect(int x, int y, int w, int h)`**, declared in `variants/heltec_v3/board_ui.h` and implemented in
`board_ui.cpp` as a pure forward to U8g2's `drawFrame`. ⓘ Everything else about the primitive is unchanged: a
one-pixel OUTLINE (⛔ never `drawBox`), compose-only, no semantics. `draw_bitmap` keeps the design's name.

⛔⛔ **CORRECTED 2026-08-16 (QG, after §CHROME-3) — THE DEFERRAL SPLITS PER PRIMITIVE; "deferred to slice 3" WAS
TOO COARSE AND IS WITHDRAWN AS A SINGLE CLAIM.** **Measured in all three OLED builds after §CHROME-3:**
`firmware_ui.o` references **`draw_bitmap`**, `board_ui.o` defines **both**, and ⛔ **nothing references `draw_rect`,
which remains ABSENT from the shipped image** (`nm -C firmware.elf`: `draw_bitmap` 1 hit, `draw_rect` 0, on
`heltec_v3` / `heltec_mobile` / `gateway_heltec`). ⇒ **§11.2's last bullet is now CLOSED FOR `draw_bitmap` BY
§CHROME-3, and TRANSFERS FOR `draw_rect` TO §CHROME-4**, whose navigation rail is its only legitimate caller.
⛔ **No dummy linkage anchor is to be added** — the reasoning §CHROME-2 recorded still stands: an anchor exists only
to satisfy a test, links dead code into every shipped image, and makes any flash figure measured behind it describe
the anchor rather than the feature. ⓘ The original wording follows.

✅ **CLOSED 2026-08-16 BY §CHROME-4, MEASURED — AND WITH NO ANCHOR.** The navigation rail's selection frame is
`draw_rect`'s first and only caller, so `nm -C firmware.elf` now reports **`draw_rect` 1 hit on all three OLED
environments** (`heltec_v3` `42003cb0` · `heltec_mobile` `42003e78` · `gateway_heltec` `420042b4`), alongside
`draw_bitmap`. **All FIFTEEN icon assets are likewise linked on all three** — including the six that were
`--gc-sections`'d until this slice (`kIconStatus`, `kIconSend` and the four `kIconSettings*`). ⇒ **§11.2's last
bullet is now CLOSED FOR BOTH PRIMITIVES AND FOR EVERY ASSET.**
ⓘ Stated rather than glossed: on `gateway_heltec` the TEAM and SEND glyphs are LINKED but never DRAWN — the slot mask
is a runtime value, so `rail_glyph`'s switch references them on every build. That is 14 bytes of flash for two
unreachable slots, and it is the price of keeping the icon table `#if`-free.

★★ **AMENDMENT 2026-08-16 (§CHROME-2 implementation) — §11.2's LAST BULLET IS DEFERRED TO SLICE 3, AND SAYING SO IS
THE POINT.** §11.2 requires *"the new canvas calls are linked and exercised in every OLED environment, not merely
present in source"*. ⛔ **Slice 2 CANNOT satisfy it and does not claim to:** the primitives are generic, the board may
not include the icon or chrome headers (§8.1), and the renderer that will call them is slice 3 — so until then
`draw_bitmap` / `draw_rect` have **no ODR-user anywhere in the tree** and `--gc-sections` is entitled to discard both
from every image. What slice 2 establishes instead is the weaker, measurable half: **both symbols are COMPILED and
DEFINED in `board_ui.o` on every OLED environment** (`nm` on the built object), and both are **exercised against the
real `board_ui.cpp`** by `tools/probe_board_ui/`. ⛔ **A dummy reference was deliberately NOT added to force linkage:**
an anchor whose only purpose is to satisfy a test would link dead code into every shipped image, and a flash figure
measured behind it would describe the anchor rather than the strip. ⇒ **the bullet is owed by slice 3**, where the
renderer becomes the caller and the flash cost becomes real and attributable.

The icons should live in a pure, Arduino-free UI asset/header unit and must not require heap allocation, Unicode,
UTF-8 decoding or an additional U8g2 font.

★ **AMENDMENT 2026-08-16 (QA-gate): the bitmaps must land in FLASH, not RAM.** Declare them `constexpr`/`const` at
namespace scope so they are read-only data; ⛔ a non-const array, or one built at runtime, spends scarce SRAM —
`heltec_v3` is already at **66.0 %**. ⇒ **§11.3's per-board delta must ATTRIBUTE the split (flash versus RAM), not
merely report a total**: a RAM rise here is a design error, not a cost.

### 8.2 One frozen chrome projection

Introduce one pure display projection for the strip and rail, conceptually `UiChrome`, containing only already-
classified display facts:

- mail value and overflow state;
- home icon state and compact age;
- team configured/count/overflow;
- team-key icon state;
- battery decivolts/unavailable;
- configuration badge.

The renderer consumes the frozen projection. It must not query `g_node`, `ConfigService`, counters or the battery
while U8g2 is replaying later pages of the frame.

Equality is field-by-field. `memcmp` over a struct with possible padding is forbidden.

★ **AMENDMENT 2026-08-16 — `UiChrome` NAMES NO RAIL STATE, yet §8.2 is the projection that freezes the chrome, and
the rail IS chrome.** The list above carries only strip facts plus the configuration badge; **selected navigation
slot, available-slot mask and emergency rail suppression appear nowhere.**

⛔⛔ **RULED AT QG 2026-08-16 — OPTION (a): THE RAIL STATE JOINS `UiChrome`.** ⓘ This amendment previously offered a
choice between **(a)** adding the fields and **(b)** deriving the rail from frozen `UiState` alone; **(b) is WITHDRAWN
because it is not merely less tidy — it is INCOMPLETE, and the code says so:**

- **emergency suppression is NOT in `UiState`** — `UiModel::state()` returns `_st` (`src/firmware_ui_model.h:841`)
  while `UiModel::emergency()` returns a **separate** `_emg` member (`:983`);
- **TEAM/SEND availability is NOT in `UiState`** either — it is a frozen snapshot/build-profile fact (`team_build`,
  `:1600`, false on a `!MR_FEAT_TEAM` build).

⇒ **`UiState` alone cannot derive the complete rail honestly**, and a renderer that reached past the projection for
the missing two would be doing exactly what §8.2 exists to forbid.

**`UiChrome` therefore gains three fields:**
- **selected navigation slot** (via §5.2's single pure mapping — the mapping stays, it simply feeds the projection);
- **available-slot mask** (which of the five slots this build draws at all);
- **rail visible / suppressed** (the emergency exception of §5.3).

### 8.3 Repaint invalidation

Snapshot-only facts currently can change without a UI gesture or app push. The new strip would otherwise become
stale—for example, a team route may arrive via a beacon, and the compact home age changes with time.

Therefore:

1. build the live `UiChrome` each UI tick from existing read-only authorities;
2. compare it with the chrome frozen for the most recently opened frame;
3. if the visible projection differs, mark the model dirty;
4. update the comparison reference only when a new frame freezes, not merely when a change is observed;
5. if a value changes while a page loop is open, retain dirty so one follow-up frame renders the newer projection.

This uses the existing UI tick and frame gate. It allocates no `Node` timer and may not bypass the MAC-idle paint
gate or the existing 2 Hz ordinary-frame throttle.

Under one minute the home-age token can change once per second. That is within the existing 2 Hz limit and normally
lasts only for the panel's 15-second awake window. Above one minute it changes only at the displayed unit boundary.

### 8.3.1 AMENDMENT 2026-08-16 (QA-gate), ⛔⛔ **CORRECTED AT QG THE SAME DAY — THE FIRST VERSION MISREAD THE MECHANISM AND ITS PROPOSED TEST WAS DANGEROUS**

⛔ **WITHDRAWN, KEPT VISIBLE (§3 rule 3).** The first version of this amendment asserted that *"a chrome difference
that marks the model dirty while the panel is BLANKED opens a frame, and an open frame INHIBITS LIGHT SLEEP"*, and
required slice 3 to prove that a blanked chrome change **marks the model clean**. ★★ **BOTH HALVES WERE WRONG, and
the code says so plainly:**

- `FrameGate::step` (`src/firmware_ui_model.h:1926-1932`) tests **`blanked` FIRST**, sets `_open = false`, and returns
  `FrameStep::blank` — ⇒ **`dirty` is never examined while dark, no frame opens, `frame_open()` stays false, and light
  sleep is NOT inhibited.**
- Blanking **deliberately sets `dirty = true`** (`:831`), and `:1931` states the rule outright: *"§B107: nothing is
  CLEARED here — an invalidation raised while dark survives"*.

⇒ ⛔⛔ **THE WITHDRAWN TEST WOULD HAVE BEEN HARMFUL: requiring a blanked chrome change to mark the model CLEAN means
clearing a dirty bit while dark, which ERASES A LEGITIMATE PENDING REDRAW that §B107 exists to preserve.** A test can
do damage, and this one would have.

**WHAT THE SLICE MUST ACTUALLY PIN — four behaviours, none of which touches the dirty bit's value while dark:**
1. a **chrome-only** change while blanked must **not** unblank, must **not** start a frame, must **not** produce bus
   traffic, and must **not** alter the attention clock (`_last_input_ms`).
   ★★ **THE TEST MUST BEGIN AFTER THE BLANKING EDGE HAS COMPLETED (QG 2026-08-16), because the first
   `set_power_save(true)` LEGITIMATELY ISSUES ONE PANEL COMMAND** — counting it would either fail a correct
   implementation or, far worse, invite someone to "fix" it by suppressing the edge itself. **Pin this sequence:**
   **(1)** blank and let the power-save edge complete · **(2)** record the bus-call count · **(3)** change mail/home/team
   chrome while still blanked · **(4)** run subsequent UI ticks · **(5)** require **zero ADDITIONAL bus calls**, **no
   frame opened**, and **`dirty` PRESERVED**;
2. ⛔ it must **never CLEAR an existing dirty bit** — §B107's survival rule is load-bearing and is not this design's
   to relax;
3. after **wake**, the first frame **freezes the current live chrome** (not a stale projection captured while dark);
4. **lit + clean + a visible chrome change ⇒ dirty.** ★ This is the positive half, and it is what the whole §8.3
   mechanism exists to achieve — a rule that never invalidates is as wrong as one that always does.

⚠ **THE METAL CHECK STANDS, and its VALUE is unchanged even though its rationale was wrong:** `slept=` must keep
climbing with the strip enabled. ⓘ It is no longer justified as *"otherwise a frame opens every second"*; it is a
**regression guard** — the chrome adds per-tick work and new invalidation paths to the one subsystem that took five
review rounds to stabilise, and a power regression there would present only as `slept=` failing to climb, with no
panic and nothing visible on the panel. **See §12's numbered check.**

## 9. Compatibility and non-goals

This design changes no:

- RF frame, app code, command, JSON or BLE contract;
- NV format or settings transaction;
- inbox storage/read-watermark rule;
- mobile attachment or home-selection algorithm;
- team-route lifetime;
- button gesture or screen-cycle order;
- emergency wording or send-outcome authority;
- `Node` member, `sizeof(Node)` or timer-wheel capacity.

The following are explicitly outside scope:

- touch navigation;
- percentage battery or estimated time remaining;
- RSSI bars or a generic “mesh connected” state;
- defining “online teammate” using a new UI timeout;
- waking the panel for every status-strip change;
- making the Heltec inbox durable;
- universal runtime-versus-flash dirty tracking.

## 10. Recommended implementation slices

Keep the work reviewable in four slices:

1. **Pure chrome and geometry:** icon assets, compact formatters, semantic projections, navigation mapping and native
   tests. No board or renderer change.
2. **Canvas extension:** generic bitmap/frame primitives, Heltec implementation and board-probe coverage. No UI
   semantics.
3. **Status strip and invalidation:** snapshot sources, frozen chrome, fixed header slots and repaint rules.
4. **Navigation/body migration:** rail, selection frame, modal mapping, 19-character audit and documentation/metal
   tests.

Do not combine an unrelated core, routing, sleep/wake or inbox-storage fix with these slices.

## 11. Automated acceptance

### 11.1 Pure/native tests

At minimum, pin:

- combined mail: `0`, `1`, `99`, `100 -> 99+`, and a sum crossing the boundary;
- compact home age at every boundary: unknown, 59 s/60 s, 59 m/60 m, 23 h/24 h, 99 d/100 d;
- an age above `UINT32_MAX` milliseconds does not wrap to a recent value;
- all four home-link icon states;
- no-team versus team-with-zero-peers;
- team counts `9` and `10 -> 9+`;
- all three team-key states;
- battery unknown and one-decimal truncation;
- configuration badge priority, including unsaved plus restart and conflict plus unsaved;
- normal screen-to-icon mapping, Inbox detail, both compose kinds and every send-result state;
- emergency suppresses the rail and selects no normal navigation icon;
- non-team builds expose no TEAM/SEND rail icon;
- visible chrome equality changes only when rendered output changes.

Every load-bearing test needs a negative control or mutation that makes it red.

### 11.2 Renderer and board probes

Pin:

- exact header slot coordinates and right-aligned battery field;
- no draw call exceeds `x=127` or `y=63`;
- every normal text line fits the 116-pixel body;
- exactly one navigation frame is drawn on ordinary/modal views;
- no rail call is made for an emergency frame;
- the correct icon remains selected on all eight U8g2 page replays;
- bitmap/frame calls touch no I2C outside the existing `next_page()` boundary;
- blanking remains edge-triggered and produces no repeated bus traffic;
- the new canvas calls are linked and exercised in every OLED environment, not merely present in source.

### 11.3 Build/system gates

- native suite green;
- firmware-UI and board-UI probes green with mutation controls;
- warning census at its approved baseline, with no new warnings;
- all OLED environments link; include at least one non-team OLED environment;
- per-board flash and RAM deltas measured and attributed;
- `sizeof(Node)` and timer capacity unchanged;
- simulator `lus` rebuilt with zero relevant build actions and its md5 unchanged, then `s18` exact. This slice is
  outside the simulator build; a stale executable is not proof.

## 12. Metal acceptance

On `heltec_mobile`, with the console log retained:

1. **Geometry:** cycle all five screens; exactly one rail icon is boxed, no normal text touches the rail or clips at
   the right edge.
2. **Modal mapping:** open Inbox detail and both compose paths; Inbox remains selected for detail and SEND for
   compose/results.
3. **Emergency width:** fire and cancel/complete an emergency; the rail disappears and every large headline remains
   complete.
4. **Mail:** receive one DM and one channel post; the envelope progresses `0 -> 1 -> 2`; opening detail does not
   clear it; one fully rendered Inbox list returns it to `0`.
5. **Home:** observe confirmed, checking and lost/recovering states against `mobile status`; the icon and displayed
   confirmation age agree with the console fields and never claim generic connectivity.
6. **Team:** compare the people count with the TEAM screen/`routes`; no team reads `--`, a configured team with no
   known teammate reads `0`.
7. **Key:** remove/adopt the team content key using existing supported commands; crossed/normal key follows
   `team_ch_key`, with no icon claiming the device identity is absent.
8. **Configuration:** edit without saving, create an external conflict, save a reboot-requiring field, and reboot;
   the Settings badge follows the priority table while SETTINGS states the actionable reason.
9. **Battery:** compare `--` and a valid voltage with existing battery qualification; the redesign does not add a
   percentage or change the measurement.
10. **Radio coexistence:** repeat navigation and the once-per-second young home-age update under DM traffic; no
    CTS/DATA timing regression or unbounded repaint loop is attributable to the display.
11. ★★ **IDLE SLEEP STILL WORKS WITH THE STRIP ENABLED (added 2026-08-16 — §8.3.1's regression guard, and it was
    missing from this numbered list while the amendment demanded it).** Persist `team 0` **before** rebooting so the
    node comes up with no peers; ⛔ **send NO console byte during the test boot** (one byte latches `g_host_present`
    and the node then never sleeps, so the check would pass over a node that was simply awake); wait past the 30 s
    boot grace and the 15 s panel blank; then read `status` **once, at the end**. **Require `slept=` > 0**, with
    `wkarmfail=0` and `wkdisarm=0`. ⓘ `wkbusy=` and `wksleepfail=` are informational — **record them, do not require
    zero** (§B200's bench Part 23 records why).

## 13. Completion condition

The redesign is complete only when the strip and rail are both landed, all normal body strings have migrated to the
116-pixel content area, emergency remains full-width, and the metal checks above pass.

A partial state with icons drawn over 21-column content, stale home/team data, or icon-only configuration errors is
not an acceptable intermediate release.
