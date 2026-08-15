# Heltec mobile status strip and navigation rail redesign

**Date:** 2026-08-15  
**Status:** DESIGN — not implemented  
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

Inbox detail wrapping must use the new visible body width at the model's freeze point. Changing only the draw origin
would clip already-frozen 20-character lines and would make pagination lie.

## 8. Architecture

### 8.1 Icons and canvas

The board boundary remains display-independent:

- firmware owns icon identity, bitmap bytes, placement and state selection;
- the board owns only copying pixels to its display library.

Add generic canvas primitives, not semantic board calls:

```cpp
draw_bitmap(x, y, width, height, bits)
draw_frame(x, y, width, height)
```

`board_ui.cpp` must not gain functions such as `draw_mail_icon()` or include `firmware_ui_model.h`. The V4 port must
be able to reuse the same bitmaps and renderer.

The icons should live in a pure, Arduino-free UI asset/header unit and must not require heap allocation, Unicode,
UTF-8 decoding or an additional U8g2 font.

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

## 13. Completion condition

The redesign is complete only when the strip and rail are both landed, all normal body strings have migrated to the
116-pixel content area, emergency remains full-width, and the metal checks above pass.

A partial state with icons drawn over 21-column content, stale home/team data, or icon-only configuration errors is
not an acceptable intermediate release.
