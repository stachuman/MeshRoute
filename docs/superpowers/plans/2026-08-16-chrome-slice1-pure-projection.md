<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §CHROME-1 — pure chrome, formatters, projection and navigation mapping · dispatch brief · 2026-08-16

**Status: DISPATCHED 2026-08-16 on an owner directive**, after the design passed two QG rounds and nine amendments.
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**
⛔ **Never `git commit`, never `git add`, NEVER `git checkout --` anything.** The tree is CLEAN at **`b8929e5`** —
keep it that way except for this slice.

**Normative design:** `docs/superpowers/specs/2026-08-15-heltec-mobile-status-navigation-ui-design.md`. **Read it in
full, including all NINE amendment/correction blocks** (search `2026-08-16`) — several *withdraw* an earlier
instruction, and one withdraws a test that would have caused harm. ⛔ Do not implement a withdrawn instruction.

**Baseline:** HEAD `b8929e5`, clean · native **1651 / 82867 / 0** · `lus` `b77cfd3d` · `s18` `9868cad3` / 269905 ·
`sizeof(Node)` **221880** · census **174 / 178 / 178**.

---
## 0 — Scope: §10 slice 1, and NOTHING else

**BUILD:** icon assets · compact formatters · the `UiChrome` projection · the §5.2 navigation mapping · native tests.
⛔ **OUT — these are slices 2-4 and building "just a bit" of them is the failure mode:** the canvas primitives and any
`board_ui` change (slice 2) · the status-strip renderer, header slots and repaint invalidation (slice 3) · the rail,
the 19-column body migration and the §7.3 audit (slice 4). ⛔ **No renderer change, no board change, no
`firmware_ui.cpp` drawing.**

---
## 1 — Verified state, and TWO findings the design's own rules turn into traps

`UiSnapshot` (`src/firmware_ui_model.h:325-347`) already carries: `unread_dm` / `unread_ch` (uint16, **clamped to
`kUnreadCap` = 999**, `:355`), `team_shown` / `team_total`, `batt_mv` (int32, `<0` = unavailable), `team_build`,
`ble_row`, `now_ms`.

### ⛔⛔ FINDING A — `UiSnapshot::now_ms` IS `uint32_t`, so the obvious way to carry the home age IS the forbidden cast
§4.2 forbids a 32-bit millisecond cast because it re-creates the ~49.7-day wrap this project already fixed once.
⚠ **The snapshot's own idiom invites exactly that**: `now_ms` is `uint32_t` and `last_dm_age_s` is a `uint32_t`
seconds age, so "age = now_ms − confirmed_ms" inside the snapshot would be the bug, written naturally.
⇒ **Carry the home age as `uint64_t` MILLISECONDS, or carry it ALREADY BUCKETED.** ⛔ Never as `uint32_t` ms.
★ §11.1 requires a test that **an age above `UINT32_MAX` ms does not wrap to a recent value** — make sure it can fail.

### ⛔ FINDING B — the home-link and team-key facts are NOT in the snapshot yet
`mobile_home_link()` (`lib/core/node.h:593`), `mobile_home_confirmed_ever()` (`:609`), `mobile_home_confirm_age_ms()`
(`:610`) and `team_channel_key_present()` (`:224`) are **`g_node` accessors**. The projection is **pure** and may not
call `g_node`. ⇒ **slice 1 DEFINES the snapshot fields these facts will ride in and builds the projection from them;
⛔ slice 3 wires the publisher.** Say plainly in your report that the fields are defined-but-unpublished.

ⓘ **The mail sum is of ALREADY-CLAMPED values** (each ≤ 999), so `99+` is reachable honestly — but the combined value
is *"at least this many"* above the clamp, never exact. **Do not describe it as an exact total.**

ⓘ **The three rail facts, per the owner's ruling (§8.2, option (a)):** `UiModel::state()` returns `_st` (`:841`) while
`emergency()` returns a **separate** `_emg` (`:983`), and TEAM availability is `team_build` (`:1600`). ⇒ `UiState`
alone cannot derive the rail, which is why the fields join `UiChrome`.

---
## 2 — What to build

1. **`UiChrome`** — one pure projection carrying **only already-classified display facts**: mail value + overflow ·
   home icon state + compact age token · team configured/count/overflow · team-key icon state · battery
   decivolts/unavailable · configuration badge · ★ **selected navigation slot · available-slot mask · rail
   visible/suppressed** (the three fields the owner ruled in).
   ⛔ **Equality is FIELD-BY-FIELD. `memcmp` over a struct with padding is forbidden** (§8.2) — padding is
   indeterminate and would make equality report differences that do not exist.
2. **Compact formatters**, to the design's exact boundary tables: mail (`0..99`, `100+ -> 99+`) · home age
   (`--` / `0s..59s` / `1m..59m` / `1h..23h` / `1d..99d` / `100d+ -> old`) · team (`--` / `0` / `1..9` / `9+`) ·
   battery (one-decimal truncation, `--`, `V` included).
3. **The §5.2 navigation mapping** — ONE pure function, **`switch` with NO `default:`** so `-Wswitch` forces every
   future screen/modal to be classified rather than silently defaulting. It must cover: inbox list / detail /
   `MESSAGE GONE` ⇒ INBOX · compose + send result ⇒ SEND · settings editor ⇒ SETTINGS · ordinary ⇒ its own screen ·
   **emergency ⇒ rail suppressed, no slot selected**.
4. **Icon assets** in a pure, Arduino-free unit: `constexpr`/`const` at namespace scope (**flash, not RAM**),
   **U8g2/XBM convention — row-major, LSB-first, 1 bit/pixel, rows padded to whole bytes** (§8.1 amendment).
   ⛔ No heap, no Unicode, no extra U8g2 font. ⚠ Nothing references them yet, so the linker may drop them — **say so
   rather than reporting a misleading zero flash delta.**

---
## 3 — Traps
- ⛔ **The 32-bit age cast** (Finding A) — the single most likely defect in this slice.
- ⛔ **`memcmp` equality** — forbidden, and it would pass a naive test.
- ⛔ **A `default:` in the mapping** — it converts a future missing case from a build error into a wrong icon.
- ⛔ **No `Node` growth, no timer ids, no wire/NV change, no `sizeof(Node)` movement.**
- ⚠ **Do not "reinterpret" any existing field** (§2): `team_total` is route rows, not members online; the mail value
  is session-unread, not `inbox_total`; the home age is a confirmation age, **never** "connected".

---
## 4 — Tests
**§11.1 is normative — implement every bullet**, each **mutation-proven with match counts printed**. The four that
carry the most weight, because each pins a rule that is easy to satisfy wrongly:
- **the age above `UINT32_MAX` ms** (Finding A) — mutate to a 32-bit cast ⇒ must go RED;
- **every compact-age boundary in both directions** (59/60 s, 59/60 m, 23/24 h, 99/100 d) — an off-by-one here is
  invisible on a panel;
- **badge priority** `conflict > unsaved > restart-required > clean`, including *unsaved + restart* and
  *conflict + unsaved*;
- **emergency suppresses the rail and selects no slot**, and **a non-team build exposes no TEAM/SEND slot**.
★ **"Visible chrome equality changes only when rendered output changes"** (§11.1's last bullet) is the one that
catches a `memcmp` or a stray field — **give it a mutation that adds an invisible field to the comparison.**

---
## 5 — Gate
1. `pio test -e native`, **then RUN the binary** (the wrapper prints a false *"0 test cases"*). From **1651 / 82867 / 0**.
2. **`warning_census.sh`** at its pins (**174 / 178 / 178**, `-Wswitch` 0) — ⚠ this slice deliberately provokes
   `-Wswitch`, so the census is how you prove every arm was **added** rather than defaulted.
3. **Simulator inertness, the four-step structural proof:** pre-`lus` md5 → canonical rebuild → **zero relevant build
   actions** → identical md5; `s18` as smoke only, keystone **read from `simulation/BASELINE.md`**. ⛔ No 36-run, no
   anchor-table edit.
4. **Six board envs build**; report per-board RAM/flash **with the flash/RAM split attributed** (§11.3 amendment).
   ⚠ **Predict and explain what you see**: pure headers that nothing calls yet should move neither meaningfully.
5. ★★ **D2 explicitly:** `sizeof(Node)` **221880** unmoved; `kCap` 91, no timer allocated.
6. ⛔ **Prove the slice is renderer- and board-inert**: `git status --short` must show **no** change under
   `variants/` and no drawing change in `src/firmware_ui.cpp`.

---
## 6 — Method
- ★★ **A fact is established by the act** — the home age is a *confirmation* age; ⛔ never render or name it as
  connectivity (§4.2).
- ★★ **Name the third state** — `--` (never confirmed) is not `0s`, and no-team is not team-with-zero.
- ★★ **Instruments that cannot fail** — this arc has recorded many; **four probes in the §B200 arc were green against
  the very defect they were written to catch.** Ask of every test: could it have come out otherwise?
- ⛔ **PROVENANCE (ledger §3):** never claim an owner or QA approval; **never quote an owner ruling** — reported form
  only.

**Report:** `UiChrome`'s fields with `file:line` · how the home age avoids the 32-bit cast · the field-by-field
equality · the mapping's exhaustive switch · the icon unit, its byte-order contract and whether the linker kept it ·
which snapshot fields are **defined but not yet published** · every test with its mutation and match count · native ·
census · the four-step corpus proof · per-board RAM/flash attributed · the D2 answer · the renderer/board-inertness
evidence · exact final `git status --short` and that nothing was committed.
⛔ **Anything you cannot establish, say so plainly.**

**Stop and report rather than improvising if:** a design bullet and an amendment disagree (⇒ **the amendment wins;
report the conflict**) · a formatter boundary in §4 disagrees with §11.1's test list · the projection cannot be built
without calling `g_node` · `sizeof(Node)` moves · or a corpus row moves.

---
---
# ROUND 2 — QG HOLD. Two functional mismatches and one vacuous test.

⚠ QG's findings relayed by the owner — a recommendation, not an owner ruling (ledger §3 rule 5). **All three
re-verified at the code.**

## R2.1 — ⛔⛔ THE RAIL CAN NAME THE WRONG BODY, and the test PINS the contradiction

**Verified.** The renderer (`src/firmware_ui.cpp:949-954`) draws in this order, each arm `return`ing:
**emergency → compose → inbox detail → screen.** The mapping (`src/firmware_ui_chrome.h:175-181`) tests
**`st.detail` BEFORE `st.compose`.** ⇒ with both `compose != none` **and** `detail != closed` — **both are valid
`UiState` values today** — the renderer draws **COMPOSE** while the rail says **INBOX**.

★★ **§5.2's rule is the tie-breaker and it is unambiguous: *"The rail must describe the body ACTUALLY BEING SHOWN."*
The renderer is the authority; the mapping is wrong.**

**Required precedence: `emergency → compose → inbox detail → settings → ordinary screen`.**
ⓘ Making the contradictory combination unrepresentable would also be sound, but those are legal `UiState` values
today and narrowing them is not this slice's scope. **Fix the ordering.**

⛔ **`test/test_firmware_ui_chrome.cpp:446` PINS THE WRONG ANSWER** — it asserts INBOX for the contradictory state, so
it currently **enforces the defect**. ★ **That is the FIFTH instrument in this arc to do so** (the arm-once W-checks,
[[B195]]'s vacuous tripwire, W31's unsafe order, P11j's bit-10 rollback — and now this). **Retarget it, and add a
control that restores detail-before-compose and MUST go RED.**

## R2.2 — ⛔ THE BATTERY GUARD BREAKS THE FROZEN GEOMETRY

`src/firmware_ui_chrome.h:348`'s upper clamp deliberately emits **`99.9V`** and calls it safe because *"nobody will
mistake it for a cell"*. ⚠ **That reasoning is about PLAUSIBILITY; the defect is GEOMETRIC.** `99.9V` is six
characters ≈ **30 px of text plus the 11 px battery icon = 41 px**, against §3.1's reserved **35 px** and its "at
least 15 px" overall spacing (design `:73`). ⇒ it overruns the frozen slot and pushes the strip out of budget.

⇒ **A value too wide to render as `x.xV` is UNAVAILABLE: render `--`.** ⛔ **Do NOT clamp it to a plausible-looking
`9.9V`** — that is precisely the *"never substitute a plausible default voltage"* rule the battery path already
carries (`variants/heltec_v3/board_ui.cpp`'s `battery_sample_mv` states it, and `UiSnapshot::batt_mv < 0` is the
existing expression of it). ★ **`--` is the honest answer and it already has a slot that fits.**

## R2.3 — ⛔ THE "EVERY SEND-RESULT STATE" TESTS ARE VACUOUS

`test/test_firmware_ui_chrome.cpp:426`'s two loops **discard the loop variable** (`(void)d;`) and rebuild the **same
`UiState`** each iteration. ⓘ The in-source comment is honest about what it pins — *"the MAPPING does not consult
it"* — **but the test then claims §11.1's "every send-result state" coverage, and that claim is false.** The loops
prove compose-result mapping N times, not that every real outcome leaves the SEND body active.

**Choose one and say which:**
- **(a)** drive **actual `UiModel` outcome transitions** so the states are genuinely reached; or
- **(b)** keep the mapping-level test, **delete the exhaustive-coverage claim**, and state that outcome exhaustiveness
  rests on the existing outcome-machine tests — **naming them**.
⛔ Either way the surviving assertion needs a **mutation that can redden it**; a loop that cannot fail differently
from its first iteration is not coverage.

## R2.4 — Minor
- `src/firmware_ui_chrome.h:16` **names the wrong test file** — correct it.
- The comment claiming **enum order** prevents badge-priority drift is **inaccurate**: the explicit `if` ordering and
  its tests are what prevent it. ⛔ A comment that credits the wrong mechanism sends the next reader to the wrong
  place when they change it.

## R2.5 — Re-gate
Native from the binary · the chrome/icons/model mutation targets with counts · `warning_census.sh` at its pins · the
four-step simulator-inertness proof · six board envs (⚠ the three non-OLED must stay **byte-identical**) ·
`sizeof(Node)` **221880** · ⛔ still **no** renderer/board change · nothing committed.
