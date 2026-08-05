# On-device OLED + one-button UI — Phase A (Heltec V3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give a Heltec V3 team mobile a usable no-phone interface — status, teammates, inbox, canned messages, teammate DMs, and a long-press emergency — driven by one button on the on-board SSD1306.

**Architecture:** ⓘ *(§A0 2026-08-03: the board port now lives at `variants/heltec_v3/board_ui.cpp`; only the
board-INDEPENDENT units below stay in `src/`.)* Two **pure headers** in `src/` hold all logic (gesture classification; screens, compose modal, emergency and DM outcome machines) and are unit-tested natively. `src/firmware_ui.cpp` builds a plain-data snapshot, owns all render policy, performs sends and **correlates their outcomes**. `src/board_ui.cpp` owns only U8g2, I²C, GPIO and the ADC, behind a display-independent canvas.

**Tech Stack:** C++20, PlatformIO, doctest (native), U8g2 (page-buffer mode), Arduino-ESP32 (`heltec_v3` / `heltec_mobile`).

**Spec:** `docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`. Read §2, §2.1, §4 and §5 before Task 4.

*Revision 1 (2026-08-01): rewritten against the first review (`docs/archive/2026-08-01-onboard-oled-ui-review.md`) — send attribution, corrected retry arithmetic, long-gesture pre-emption, the DM outcome machine, the canvas boundary, edge-triggered blanking, cached battery sampling.*

*Revision 2 (2026-08-01): rewritten against the second review (`docs/archive/2026-08-01-onboard-oled-ui-second-review.md`) — **B38/B39/B40 registered as core prerequisites**, two independent send trackers with emergency priority, the `ctr == 0` "not sent" sentinel, the U8g2 per-page redraw, the hold deadline actually read, full `SendFailReason` propagation with late-ack upgrade, the reply whitelist, per-kind inbox bounds, and the battery attempt-cadence fix.*

---

## Global Constraints

- **Never `git commit`.** Project rule D4: the owner makes every commit. Each task ends with "report ready", not a commit.
- **The gate (D1):** `pio test -e native`, then **run** `./.pio/build/native/program` — the wrapper falsely reports "0 test cases"; the binary prints the real count and must show 0 failed. Then s18 md5 **exact** against the keystone in `simulation/BASELINE.md` (read it there; never hardcode). Then the board envs.
- **Board envs:** `gateway`, `xiao_sx1262`, `xiao_esp32s3` (the standing 3-env rule) **plus** `heltec_v3` and `heltec_mobile`, which this changes.
- **s18 must not move.** Everything here is `src/`-only, so the stream is inert by construction.
- ⛔ **`REQUIRE` DOES NOT COMPILE IN THIS SUITE — measured, not inferred (register B67).** `platformio.ini:48` sets
  `-fno-exceptions`, so doctest turns every `REQUIRE` into a **hard compile error** (`doctest.h:2824: static assertion
  failed: Exceptions are disabled!`). All 30 existing test files carry the note and **not one calls it**. This plan
  originally used `REQUIRE` at nine sites.
- ⛔⛔ **AND THE FIRST FIX FOR B67 WAS WORSE THAN B67 — register B70, measured.** The naive rewrite was
  `CHECK(expr); if (!expr) return;` — but `take_send_request` and the `match_*` matchers **CONSUME**. The `CHECK`
  drained and returned true, the `if` then got false and **returned early, silently deleting every assertion below,
  with a green tick on top**: the three-transmissions case ran **2 assertions instead of 11** and still reported
  `2 passed | 0 failed`. ⇒ ★ **THE ONLY CORRECT IDIOM FOR A CONSUMING CALL — call it ONCE, into a local:**
  ```cpp
  const bool got = m.take_send_request(req);   // ONE call
  CHECK(got == true);
  if (!got) return;
  ```
  ⚠ **This applies at all SEVEN sites, Task 4's included.** A Task-3 report claimed Task 4's four guards were sound
  "with no preceding CHECK"; they had one, and were equally broken. **Never evaluate a consuming expression twice.**
- **Warnings are gate-blocking.** Zero `-Wswitch`, no new warnings vs the pio baseline.
- **Author header:** every new source file gets `// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>` as line 2.
- **`sizeof(Node)` must not change.** No UI state enters `lib/core`.
- **Reuse, do not add.** The new firmware surfaces are: the UI TUs, the U8g2 dependency, the V3 battery reader, and **`mrfw::exec_command`** (Task 7 — owner-approved 2026-08-01; additive, no verb/key/NV/wire surface). If a task appears to need anything beyond these — **stop and ask**.
- **Everything in `mrfw` must be qualified** (`src/firmware_commands.h:23`) — `mrfw::dispatch`, `mrfw::exec_command`.
- ⚠ **`dispatch()` is a console-verb router, not the send path.** It returns `false` for `send` / `send_channel`; its callers do the `parse_command` + `on_command` work. Do not route UI sends through it — see Task 7.
- **Bench-only behaviour goes in `docs/2026-07-31-bench-test-script.md`** (rule M2) — the panel, the button and the ADC are unreachable by native tests and the sim, so their checks belong there with exact expected console lines.

### Prerequisites

✅ **Discharged.** `send_channel … -t -l -e` is built and honoured: the parser accepts `-e`/`-l` (`lib/console/console_parse.cpp:250,267`), and `Node::on_command` enforces the refusal matrix — `loc_unsealed`, `no_team`, `global_clear_copy`, `no_key`, `no_identity`, **`no_fix`** (`lib/core/node.cpp:1402-1526`). `team_channel_crypt` defaults **true** (`lib/core/node_carriers.h:184`).

✅⚠ **RE-CHECKED 2026-08-03: two of the three are genuinely discharged, and the third is NOT what this block says.**

| # | 2026-08-03 status |
|---|---|
| **B38** | ✅ **FIXED 2026-08-01.** `PICKED UP` is reachable on the team plane. ⚠ Carry the owner ruling with it: **`relayed` means FIRST RELAY ONLY, never coverage — and on a fully-1-hop team it reads `false` at 100 % delivery**, which is *accepted behaviour*. ⇒ **the emergency screen will legitimately show `NOT HEARD` on a small co-located team.** Task 8 must not "fix" that. |
| **B40** | ✅ **FIXED 2026-08-01.** `channel_sent.ctr` carries the full 16-bit origination handle. ⚠ It is a **LOCAL correlation handle only** — no peer can echo more than 8 bits, so never match it against a *received* id. |
| **B39** | ⚠⚠ **CLOSED AS AN INTERIM ONLY (comments + an invariant test). The ambiguity it names is STILL LIVE, and this block states it wrongly.** |

★★ **THE CORRECTED CONSTRAINT ON TASK 4 — read this instead of the old row.** This block said Task 3's
count-on-acceptance rule is *"unimplementable until accepted / blocked / refused are distinguishable."* B39's closure
proves that framing is **backwards**: there are **three** producers of `ctr == 0`, and **the third is a SUCCESS** —
on a registered mobile a plain/`-g` GLOBAL post goes through `do_send_channel_delegated`, a real MOBILE_SEND DM flies,
but the **home** mints the channel ctr, so `ctr` stays 0 (`node.cpp:1565-1573`). ⇒ **a discriminated result that splits
only accepted / blocked / refused would classify that success as a FAILURE.**

⇒ **The sound reading of `ctr`, and the rule Task 4 must be built on:**
- `ctr != 0` ⇒ **this node originated the post and owns that handle** — exact correlation is valid
- `ctr == 0` ⇒ ★ **no LOCAL handle exists, and whether anything flew is NOT answerable synchronously**

⇒ **Task 4 must NOT treat `ctr == 0` as failure.** On the `MR_UI_TEAM_CHANNEL_ID` path used by the emergency screen this
is reachable **only on a registered mobile doing a GLOBAL post** — so a team-plane emergency post is unaffected — but the
send tracker is generic and must state which case it is in. **A local handle for a remote mint is the missing piece and
is NOT built.** If Task 4 needs one, that is its own core slice, not something to improvise in the UI.

**⇒ Tasks 1, 2, 3, 5, 6 and 9 are CLEAR to proceed.** Tasks **4, 7, 8** are clear on B38/B40 and must be designed
against the corrected `ctr` semantics above rather than the withdrawn three-way split.

### Findings from Tasks 1–2 — rulings (2026-08-03)

★ **B65 — RULED: FIX IT (in Task 3, which already opens this header).** `_last_input_ms = 0` at construction can blank
the panel on its **first tick**, having drawn nothing — reachable whenever `mr_ui_init()` runs >15 s after boot.
⇒ **that is not hypothetical here: NV `format-on-corrupt` is a shipped path in this tree** (the InternalFS self-heal
slice), and it delays boot by design. A safety device whose screen is already dark the first time it is looked at
fails the **SAFETY-FIRST purpose ruling**, which decides this. **Fix: seed `_last_input_ms` from the first tick's
`now_ms` instead of 0** — a blank timer must measure "time since the user last acted", and before the first tick there
is no such time. All existing plan tests stay green; add one pinning that the first tick draws.

★★★ **B71 — OWNER-RULED 2026-08-04: after the emergency has been sent AND ITS RESULT SEEN, the NEXT PRESS restores the
normal cycle.** This supersedes my 08-03 QA ruling (which put the exit on `double`) — a **short press** is right, because
a hiker under stress must not need a compound gesture to leave the alarm screen.

**The complete state contract for UI-6, and it is safety-preserving because THREE existing rules compose:**

| situation | short press | why |
|---|---|---|
| emergency in flight — arming / sending / blocked / retrying, **no retained outcome yet** | ⛔ **does NOT exit.** Screen stays sticky | §4's stickiness: an outcome the hiker never saw is the failure SAFETY-FIRST exists to prevent |
| panel **blanked**, emergency outcome waiting behind it | ⛔ **does not exit — it WAKES, and the waking press is CONSUMED** (spec `:378`) | ★ this is what makes the ruling safe: the result is *always* displayed before any press can dismiss it |
| emergency screen awake, **a retained outcome showing** (`picked_up` / `not_heard` / final `blocked` / `reply`) | ✅ **acknowledges and restores the cycle** — `_emg → idle` | the owner's ruling; UI-6's exit condition, which did not previously exist |
| **long press**, from anywhere including blanked | re-fires the alarm | already built and pinned; unchanged |

⇒ ★ **`double` gets NO emergency-specific job at all.** Spec §4's *"double acknowledges sticky state"* **and** *"double
re-fires NOT HEARD"* are **both withdrawn** — they were the contradiction. And §5's *"the next press restores the
emergency screen, not the cycle"* is **corrected, not deleted**: it restores the **emergency screen** when waking from a
blanked panel, and the **cycle** on the press after that.

★ **This cannot trap the user, and that is a property of Task 3 rather than an assumption:** retries are bounded, so the
machine always terminates in `not_heard` — a *retained* outcome — so an exit is always eventually reachable.

⚠ **UI-6 owns the implementation.** Task 3's `_emg` exit path stays exactly as it is until then.

★★ **B78 — RULED (2026-08-04): `Emergency::failed` JOINS the retained set.** As built, a terminal failed alarm is the
**only** outcome that blanks at `MR_UI_BLANK_MS` (15 s) while `picked_up` / `not_heard` / `blocked` / `reply` all hold
30 s. ⇒ **That inverts the priority exactly backwards.** `failed` is the state in which the hiker most needs to keep
reading the screen — it is the one that says *the alarm did not go out, act by other means* — so it must not be the
fastest to go dark. **Add it to `hold_active()`'s retained set, holding for `kEmgHoldMs` like every other retained outcome.**
★★ **`kEmgHoldMs` IS OWNER-RE-RULED 2026-08-04: 120000 → 30000. ✅ DONE.**
⚠⚠ **AND THIS BLOCK HAS NOW GONE STALE TWICE IN TWO DAYS — both times for the reason it warns about.** v1 hardcoded
"120 s" in prose and died when the owner re-ruled the value. v2 then said *"`src/firmware_ui_model.h:80` still says
`120000`; change it"* — and by the time a coder read it, it was **changed** *and* **no longer at `:80`** (the new
comments had pushed it to `:85`). ⇒ ★★ **name the CONSTANT, cite no line, restate no value.** Read it from
`kEmgHoldMs`; derive every test timestamp from it so nothing pins a number that can move (V2 applies to a one-day-old
instruction as surely as to a month-old memory).
ⓘ The coder was right not to add `retain()` unilaterally (it would have been a dead term until this ruling) and right to
escalate it as a spec decision. ★ It also composes with **B71**: `failed` is a *retained* outcome, so the next short
press acknowledges it and restores the cycle — the hiker is never trapped on a failure screen.

⏳ **B69 — DEFERRED to UI-6/UI-7, with the obligation named.** `channel_remote_mint` is handled (it shares
`channel_no_relay`: no relay evidence ⇒ no `PICKED UP`, bounded retry) but **both land in the same `Emergency` state**,
so §B68's "render as SENT" currently has **no carrier**. Unreachable on the team-plane alarm path, so it is not a live
safety hole — but the channel path in UI-6/UI-7 must add a flag or a ninth state, or the distinction is unrenderable.

⏳ **B64 / B66 — DEFERRED to Tasks 6/7, and neither is a Task-3 concern.** Recorded so they are not rediscovered:
- **B64** — a roster that shrinks between ticks makes `activate()`'s `% team_shown` **retarget the DM to row 0** rather
  than the highlighted teammate. ⚠ **That is a MIS-SEND, not a display glitch**, so it needs a ruling before Task 7
  wires real sends. Already pinned by a test, so it is visible rather than latent.
- **B66** — `back` is identified **positionally** (`cursor + 1 == n`), with the count in the model and the strings in
  `firmware_ui.cpp`. Adding a canned text in one place silently turns "back without sending" into a **send**. Spec
  §3.2.2 calls this a one-line change; it is two places. Task 7 owns the strings — bind them together there.

ⓘ **Corrected while implementing (spec §3.2.2 + `kDmTextCount = 3`):** a compose list's last row is **ONE** row that is
both `back` **and** don't-send — not two rows. The brief said two; the spec is right.

### UI-5 rulings (2026-08-04)

★★★ **B87 — RULED 2026-08-04 (re-done; my first version was wrong in BOTH dimensions).**

⛔ **Error 1: I named TWO OLED envs. There are THREE.** `gateway_heltec` `extends = env:heltec_v3`, so it compiles
`board_ui.cpp` and pulls U8g2 too. I reasoned about "the Heltec envs" from memory instead of **deriving** the set —
the same sweep-scope class as the `_hal.tx` callers, third instance this arc.
⛔ **Error 2: the pin was UNENFORCEABLE.** An incremental `pio run` recompiles nothing and therefore emits **no
warnings at all**, so "a 130th warning fails" measured nothing. A count is a gate only if there is a repeatable way to
produce it.

⇒ **THE INSTRUMENT SHIPS WITH THE RULING: `tools/warning_census.sh`.** It derives the OLED set from `MR_FEAT_OLED` in
each env's **resolved** config (never a typed list), deletes the object dirs (⚠ `touch` does NOT invalidate
PlatformIO's content-signature cache), and **prints the object count beside every total** — a 0-object row is flagged
as measuring nothing, which has happened in this project.

**CURRENT CLEAN-BUILD CENSUS — 2026-08-05 after UI-6 (B106 re-pin), `tools/warning_census.sh`:** warning totals and
`-Wswitch` are the load-bearing pins; objects/RAM/Flash are non-vacuity and budget observations (ESP32 flash can
move by the documented banner-packing quantum, B86).

| env | objects | warnings | `-Wswitch` | RAM | Flash |
|---|---|---|---|---|---|
| `heltec_v3` | 326 | **180** | **0** | 214068 | 1251204 |
| `heltec_mobile` | 326 | **180** | **0** | 213588 | 1244648 |
| `gateway_heltec` | 326 | **176** | **0** | 238988 | 1220416 |

★ **QA-CONFIRMED 2026-08-05 by an independent census run** (separate build, separate reader): every figure above
reproduced, and **RAM is +760 B on all three, identical** — `gateway_heltec` is the tightest OLED env at
**238988 / 327680 = 72.9 %**. Flash +5.8…6.2 KB.
★★ **B106 — WHY THE WARNING PINS ROSE 178/178/174 → 180/180/176, and it is +1 TU, not new warning-generating code.**
`src/firmware_ui.cpp` is one new translation unit, and it must reach `g_node` / `g_hal` / `g_iradio`, so it includes
`fw_context.h` → the radio HAL. That pulls **RadioLib's `#warning`** (`-Wcpp`) and **`device_radio.h`'s `inline volatile`
globals** (`-Wvolatile`) — **each emitted once per including TU**. ⇒ **+2 per env, and not one of them is UI-6 code**;
UI-6's own 551 lines add zero. Attribution was A/B-controlled by the coder and **independently re-derived structurally
by QA** (the include chain above). The coder's own 10 `-Wformat-truncation` were **fixed, not pinned**.
⚠ **The `-Wswitch` 0 and the raw counts are the independent evidence; the script's `PASS` verdict is NOT.** Once the
coder owns `EXPECT_WARN`, a re-run compares the build against *the coder's* expectation — it is **self-referential by
construction** and cannot contradict a wrong pin. What QA verified independently is the **raw 180/180/176**, the object
count, the uniform +760, and the include-chain attribution. ⇒ read the verdict line as non-vacuity, never as assent.
⇒ **B105 is the cure and is the owner's call:** one `DeviceHal::radio()` accessor lets the feature layer include only
pure headers, which **removes both new warnings** and unlocks the `probe_firmware_ui` that **B104** records as missing.
ⓘ Superseded history: the 2026-08-04 post-B95/B96 pins were 325 objects / **178 / 178 / 174** at RAM 213308 / 212828 /
238228. B95 added the console stage's object + 2048 B; B96's controlled delta was RAM **−16 B** / Flash **+52 B** and
**no warning**. The absolute flash values are observations, not byte-identity requirements across sessions (B86).

**The ruling itself:** **ACCEPT these totals as the pinned baseline; a HIGHER count fails the gate.** Of the ~129 added,
**127 are our own blanket `-fno-rtti` (`platformio.ini:49`, `[common] build_flags`) reaching U8g2's 127 C TUs**, where
the flag is meaningless; 2 are U8g2's own, in a module `nm` proves is linked out. ⚠ **The obvious fix is NOT inert:**
`platformio.ini:45-46` documents `build_src_flags` as the scoping recipe, but that covers **`src/` only**, so moving
`-fno-rtti` there would **strip it from `lib/core`, `lib/console`, `lib/hal` and `variants/`**, changing our own
codegen. ⇒ a genuine build-system slice, and **C1 forbids folding it into a feature task.** Pinning keeps the gate's
power — it caught three enum bugs — bounded, and now measurable by one command.

★★ **The probe harness MUST LAND IN THE REPO. A reconstruction recipe is not enough.** 34 checks with 7 negative
controls, on a TU no native test and no simulator can reach, is **the only automated cover `board_ui.cpp` will ever
have** — and ⚠ **this project has already lost a proven 33-assert scenario to a scratch directory.** `test/` being
native-only is a reason it cannot be a `pio` env, **not** a reason it cannot be a committed script: land it as
`tools/probe_board_ui.sh` (or equivalent) invoking the two cross-compilers directly. Session-dead scratch is not a
storage location.

ⓘ **B92 (spec §11 font names disagree with what landed) and B85/B86/B88–B91/B93 are recorded in the register.**

★★★ **THE TASK-5 BLOCK BELOW STILL TEACHES THREE DEFECTS — two are HARD BUILD FAILURES (found by UI-5, fixed in the
implementation, NOT yet in this text):**
1. it **omits the three `mr_ui_*` hooks** `fw_main` calls unconditionally ⇒ **link failure**;
2. it reads **`MR_UI_BTN_PIN`, which Task 6 defines** ⇒ **compile failure**;
3. **nothing calls `board_init()`**, so Step 5's *"the panel lights"* is unreachable — and ★ **under `--gc-sections` the
   entire canvas links out, which would have made the flash measurement a VACUOUS ZERO.** That third one is the
   instructive one: the build would have been green and the number meaningless.
⇒ As implemented: the hooks stay (marked TEMPORARY) and `mr_ui_init()` paints one frame through the real page loop.
⚠ **The `-I variants/heltec_v3` is STILL DEAD at Task 5** — measured three ways; it becomes load-bearing at **Task 6**.

### Constants fixed by the owner

- `MR_UI_TEAM_CHANNEL_ID` — build constant, **default 0**.
- Emergency text `"I'm in danger"`; channel canned `"Got your message"` / `"All good"`; DM canned `"Are you OK?"` / `"I'm OK"`. Every compose list ends with `back, don't send`.

---

## ⛳ A0 — the placement slice (owner-ruled 2026-08-03). ★ The broad Phase 0 split is PARKED.

⚠⚠ **v1 of this section specified a four-step board-source split. It was MEASURED AND DISPROVEN before a line moved,
and is withdrawn.** Full record in the spec's **§0** + `simulation/BASELINE.md`'s top note + register **B61**; the
withdrawn QA brief is `docs/2026-08-03-phase0-qa-objective.md`. In one line: **there is no board tier to absorb** —
genuinely board-discriminating code is **3 lines** (`board_name()`, `src/firmware_commands.cpp:339-349`), not the ~25 the
old table claimed, because 23 of 26 board-macro sites are chip-family OR-chains and were double-counted on both sides.

★ **NOTHING BLOCKS PHASE A.** `MR_FEAT_OLED` defaults 0 and the seam (32 lines, one guard, zero board
conditionals. Only one narrow piece of placement is worth doing, and only because doing it *first* is cheaper than
moving the OLED implementation afterwards.

| step | scope | gate |
|---|---|---|
| **A0** | `git mv src/board_ui.cpp variants/heltec_v3/board_ui.cpp` (the **empty seam**, 32 lines) + rewire `heltec_v3` / `gateway_heltec` / `heltec_mobile` `build_src_filter` + `-I` | all **eleven** envs link; ⚠ **delta attributed, NOT byte-identical** — see below |
| then | implement the OLED feature **in the already-correct location** (Phase A proper, below) | as Phase A specifies |
| later | **V4** arrives as `variants/heltec_v4/` with its own `board_ui.* board_rf.* lora_fem.*`, pins, power | its own slice |

✅ **A0 HAS LANDED AND ITS GATE IS MEASURED. The prediction that stood here was WRONG IN BOTH HALVES** — recorded
because the mechanism it got wrong is a reusable rule (`simulation/BASELINE.md` §A0, register **B63**):

| this block predicted | measured, same-session, 22 clean builds |
|---|---|
| "~±200 B flash / −8 B RAM / ~14 framework fns on the **three Heltec** envs" | ⛔ **All three Heltec envs IDENTICAL** — flash-bearing sections, RAM and the 11 232-symbol multiset. The only delta is `.debug_line +15` / `.debug_str +15` = exactly `len("variants/heltec_v3/board_ui.cpp") − len("src/board_ui.cpp")` |
| "the other **eight** are exactly untouched" | ⛔ **Only `native` is untouched.** `+<board_ui.cpp>` sat in **three** base filters, and `MR_FEAT_OLED` defaults 0 ⇒ on the XIAO families the TU was a **zero-byte object that was still a link input**. The move makes those globs match nothing, so link inputs change on **10 of 11** envs |
| — | ★ **The only movers are the three `xiao_esp32s3` envs**: RAM **−8**, flash **+168 / +48 / +124**, ~30 **framework** symbols resized (WiFi/NetworkClient/Update) — **not one symbol of ours**. The identical removal is **free on ARM** (4 nRF52 envs: 0 sections, identical symbols/RAM/flash) |
| "+ an `-I` for the Heltec envs" | ⛔ **Not needed, and would be dead config** — the compiler's own `.d` shows all four headers resolving from `lib/core/` + `lib/hal/`. The `-I` becomes load-bearing only at **Task 5**, which creates `board_ui.h` *inside* `variants/heltec_v3/` |
| "three Heltec `build_src_filter`s" | ⛔ **One.** `gateway_heltec` and `heltec_mobile` inherit it via `extends` |

★★ **THE RULE THAT REPLACES "leaving `src/` costs ~192 B" (B63):** proven by probe, not argument — putting the same file
back into `xiao_esp32s3`'s link set **from its new directory** reproduced the pre-move image section-for-section and
symbol-for-symbol. ⇒ **the directory is irrelevant to code size; LINK-SET MEMBERSHIP of even a zero-byte object is the
entire effect, and it is xtensa-only.** The old "+192 B" figure came from relocating `device_ota.cpp` — a 227 KB object —
i.e. it measured **reordering non-empty objects**, a different phenomenon.

⚠ **`GIT_REV` embeds `-dirty`** — a clean BEFORE against a dirty AFTER differs by 6 `.rodata` bytes for a non-code
reason. Control for it or the attribution is noise.

⛔⛔ **A0 MUST BUILD ITS OWN BEFORE ARM IN THE SAME SESSION AS ITS AFTER ARM. Do not diff against a recorded grid.**
Measured 2026-08-03 by B61, which predicted "byte-identical on all eleven" and was **wrong**: 8 of 11 envs showed a
moving section against the earlier grid — four ESP32 envs `.debug_line` +3 B (DWARF, not flash-bearing), and
`xiao_sx1262` `.text` **−32** with `gateway` **+32**, *opposite signs, symbol multisets identical*. Proven **not** to be
the change: reverting `board_name()` to its exact pre-fix form and rebuilding still read `.text` 511044. It is a
one-time content-dependent step consistent with `__DATE__`/`__TIME__` literal packing in the banner strings, which this
ld script folds into `.text` — i.e. **the ±32 B flash floor, now measured on ARM, where the symbol multiset is blind to
it.** ★ **The earlier noise floor was measured on ESP32 only; the nRF52 floor is different.** Two builds of identical
source **within one session** reproduce exactly, so same-session pre/post is the only valid byte comparand.
⇒ the recorded grid stays useful as the **object-count / warning-count / `-Wswitch` reference** and as an
order-of-magnitude record — **never as a cross-session byte comparand.** Diffing against it would misattribute ±32 B of
banner packing to the file move, on top of the ~192 B the move genuinely costs.

★ **`variants/` is the ruled home** (already project-owned; `boards/` is PlatformIO's manifest tree; `platform/` is
reserved for a real chip-family abstraction later). ⚠ **Do not add `variants_dir` to the board manifest** — untested,
separate slice.

ⓘ **Independent of A0, its own commit (register B61):** make `board_name()`'s silent `#else → "native"` loud with
`#error`. **Verified safe** — all four target macros are declared once each (`platformio.ini:79/134/218/263`) and every
one of the seven extending envs re-lists `${env:<board>.build_flags}`, so all eleven envs satisfy an arm.

⇒ **The 56 `-D` flags** (25/16/15 per board) are the real board coupling, are heterogeneous (application capability ·
radio wiring · RadioLib · TinyUSB/framework · nRF52 storage), and need **their own design and tests**. Not a remnant of
this refactor, and **not a prerequisite for the OLED work.**

---

## File Structure

| file | responsibility |
|---|---|
| `src/firmware_ui_input.h` *(new, pure)* | debounce + gesture classification |
| `src/firmware_ui_model.h` *(new, pure)* | screens, list-aware cursor, compose modal, emergency machine, DM outcome machine. Owns `UiSnapshot`, `UiState`, `SendReq`, `SendOutcome` |
| `src/firmware_ui_send.h` *(new)* | the send tracker: typed result, `ctr`/peer/channel correlation, outcome window |
| `src/firmware_ui.cpp` *(new)* | snapshot building, **all render policy**, send execution, push correlation, battery cache, the three `mr_ui_*` hooks |
| `variants/heltec_v3/board_ui.cpp` *(modify — the empty seam, moved there by §A0)* | U8g2, I²C, button GPIO, battery ADC, panel power latch. **Nothing else.** |
| `variants/heltec_v3/board_ui.h` *(new — beside its .cpp; ★ Task 5 is where the `-I variants/heltec_v3` becomes load-bearing)* | the display-independent canvas. **Must not include `firmware_ui_model.h`.** |
| `test/test_firmware_ui_input.cpp` *(new)* | classifier tests |
| `test/test_firmware_ui_model.cpp` *(new)* | screens, compose, emergency, DM outcome tests |
| `test/test_firmware_ui_send.cpp` *(new)* | attribution/correlation tests |
| `platformio.ini` *(modify)* | U8g2 pinned; pins and constants for `heltec_v3` |

`lib/hal/mr_ui.h` is **not modified**; the three hooks are implemented in `firmware_ui.cpp`, not `board_ui.cpp`.

### Task ↔ spec-slice map

| plan task | spec slice |
|---|---|
| 1 | UI-1 |
| 2 | UI-2 |
| 3 | UI-3 |
| 4 | UI-4 |
| 5 | UI-5 |
| 6 | UI-6 |
| 7 | UI-7 |
| 8 | UI-8 |
| 9 | UI-9 |

---

### Task 1: Gesture classifier

**Files:** Create `src/firmware_ui_input.h`; test `test/test_firmware_ui_input.cpp`.

**Interfaces:**
- Consumes: nothing.
- Produces: `mrui::Gesture`, `mrui::InputCfg`, `mrui::InputFsm` with `Gesture update(bool pressed, uint32_t now_ms)` and `uint32_t hold_ms(uint32_t now_ms) const`.

A `short_press` is emitted only after the double window expires — a tap is single only in hindsight. So `update()` must be called every poll, pressed or not.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_input.cpp
#include <doctest.h>
#include "firmware_ui_input.h"
using namespace mrui;

static Gesture run_until(InputFsm& f, bool pressed, uint32_t from_ms, uint32_t until_ms) {
    Gesture got = Gesture::none;
    for (uint32_t t = from_ms; t <= until_ms; t += 5) {
        const Gesture g = f.update(pressed, t);
        if (g != Gesture::none && got == Gesture::none) got = g;
    }
    return got;
}

TEST_CASE("single tap yields short_press after the double window") {
    InputFsm f;
    CHECK(run_until(f, true,  0,   60)  == Gesture::none);
    CHECK(run_until(f, false, 65,  200) == Gesture::none);
    CHECK(run_until(f, false, 205, 500) == Gesture::short_press);
}
TEST_CASE("two taps inside the window yield double_press") {
    InputFsm f;
    run_until(f, true, 0, 60); run_until(f, false, 65, 120); run_until(f, true, 125, 180);
    CHECK(run_until(f, false, 185, 400) == Gesture::double_press);
}
TEST_CASE("hold yields long_arm then long_fire") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)    == Gesture::long_arm);
    CHECK(run_until(f, true, 905, 3600) == Gesture::long_fire);
}
TEST_CASE("release between arm and fire cancels and never fires") {
    InputFsm f;
    CHECK(run_until(f, true, 0, 900)      == Gesture::long_arm);
    CHECK(run_until(f, false, 905, 1200)  == Gesture::long_cancel);
    CHECK(run_until(f, false, 1205, 5000) != Gesture::long_fire);
}
TEST_CASE("bounce shorter than debounce_ms is ignored") {
    InputFsm f; f.update(true, 0); f.update(false, 10);
    CHECK(run_until(f, false, 15, 600) == Gesture::none);
}
TEST_CASE("hold_ms reports countdown progress") {
    InputFsm f; run_until(f, true, 0, 1000);
    CHECK(f.hold_ms(1000) >= 950);
}
```

- [ ] **Step 2: Run and verify it fails**

Run: `pio test -e native` then `./.pio/build/native/program` — FAIL, header missing.

- [ ] **Step 3: Write the implementation**

```cpp
// MeshRoute — src/firmware_ui_input.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure button-gesture classifier for the one-button board UI. No Arduino, no globals — the board samples the GPIO and
// feeds (pressed, now_ms); this decides what the press MEANT. Pure so the native suite can drive the timing table
// directly; reachable from tests via `-I src` (platformio.ini native env).
#pragma once
#include <cstdint>

namespace mrui {

enum class Gesture : uint8_t { none = 0, short_press, double_press, long_arm, long_fire, long_cancel };

struct InputCfg {
    uint16_t debounce_ms = 25, double_gap_ms = 350, arm_ms = 800, fire_ms = 3500;   // fire_ms: bench-tunable
};

class InputFsm {
public:
    explicit InputFsm(InputCfg cfg = {}) : _cfg(cfg) {}

    Gesture update(bool pressed, uint32_t now_ms) {
        if (pressed != _raw) { _raw = pressed; _edge_ms = now_ms; }
        if (now_ms - _edge_ms >= _cfg.debounce_ms && _stable != _raw) {
            _stable = _raw;
            if (_stable) { _press_ms = now_ms; _armed = _fired = false; return Gesture::none; }
            return on_release();
        }
        if (_stable && !_armed && now_ms - _press_ms >= _cfg.arm_ms)             { _armed = true; return Gesture::long_arm; }
        if (_stable && _armed && !_fired && now_ms - _press_ms >= _cfg.fire_ms)  { _fired = true; return Gesture::long_fire; }
        if (!_stable && _pending_tap && now_ms - _release_ms >= _cfg.double_gap_ms) {
            _pending_tap = false; return Gesture::short_press;
        }
        return Gesture::none;
    }

    uint32_t hold_ms(uint32_t now_ms) const { return _stable ? now_ms - _press_ms : 0; }

private:
    Gesture on_release() {
        _release_ms = _edge_ms;
        if (_fired)       { _pending_tap = false; return Gesture::none; }
        if (_armed)       { _pending_tap = false; return Gesture::long_cancel; }
        if (_pending_tap) { _pending_tap = false; return Gesture::double_press; }
        _pending_tap = true; return Gesture::none;
    }
    InputCfg _cfg;
    bool     _raw = false, _stable = false, _armed = false, _fired = false, _pending_tap = false;
    uint32_t _edge_ms = 0, _press_ms = 0, _release_ms = 0;
};

}  // namespace mrui
```

- [ ] **Step 4: Run and verify all six cases pass**
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 2: Screens, list-aware cursor, compose modal

**Files:** Create `src/firmware_ui_model.h`; test `test/test_firmware_ui_model.cpp`.

**Interfaces:**
- Consumes: `mrui::Gesture`.
- Produces: `Screen`, `Compose`, `TeamRow`, `InboxRow`, `UiSnapshot`, `UiState`, `SendKind`, `SendReq`, `UiModel` with `on_gesture`, `on_tick`, `state()`, `clear_dirty()`, `take_send_request()`. Task 3 extends the same class; Task 4 feeds it typed outcomes; Task 6 fills `UiSnapshot`.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_model.cpp
#include <doctest.h>
#include "firmware_ui_model.h"
using namespace mrui;

static UiSnapshot snap(uint32_t now_ms = 1000) {
    UiSnapshot s{};
    s.now_ms = now_ms; s.team_shown = 3; s.team_total = 3; s.unread_dm = 2; s.unread_ch = 5; s.batt_mv = 3900;
    for (uint8_t i = 0; i < 3; ++i) { s.team[i].id = uint8_t(10 + i); s.team[i].last_heard_s = 60; }
    return s;
}

TEST_CASE("short press is LIST-AWARE: it walks TEAM before leaving it") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 0);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 1);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);   CHECK(m.state().cursor == 2);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::send);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::status);
}
TEST_CASE("an empty TEAM list is passed through, not a dead end") {
    UiModel m; auto s = snap(); s.team_shown = 0; s.team_total = 0;
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::team);
    m.on_gesture(Gesture::short_press, s); CHECK(m.state().screen == Screen::inbox);
}
TEST_CASE("double on TEAM opens the DM sub-view bound to the highlighted peer") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);   // cursor 1
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);
    CHECK(m.state().compose_peer == s.team[1].id);
    CHECK(m.state().cursor == 0);
}
TEST_CASE("sub-view: `back` leaves without sending") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::short_press, s);    // -> back
    m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::none);
    CHECK(m.state().screen  == Screen::team);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("sub-view: double on a message emits a DM request for the bound peer") {
    UiModel m; const auto s = snap(); SendReq req{};
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    m.on_gesture(Gesture::double_press, s);
    const bool got = m.take_send_request(req);   // §B70: ONE call — take_send_request DRAINS
    CHECK(got == true);
    if (!got) return;
    CHECK(req.kind == SendKind::dm); CHECK(req.peer_id == s.team[0].id); CHECK(req.text_index == 0);
    CHECK(m.state().compose == Compose::none);
}
TEST_CASE("sub-view auto-exits on inactivity WITHOUT sending") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::short_press, snap(1000)); m.on_gesture(Gesture::double_press, snap(1100));
    m.on_tick(snap(1100 + kBlankMs + 1));
    CHECK(m.state().compose == Compose::none);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("panel blanks and the waking SHORT press is consumed") {
    UiModel m;
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);
    m.on_gesture(Gesture::short_press, snap(1000 + kBlankMs + 10));
    CHECK(m.state().blanked == false);
    CHECK(m.state().screen  == Screen::status);
}
```

- [ ] **Step 2: Run and verify it fails** (`pio test -e native`, then run the binary)

- [ ] **Step 3: Write the implementation**

```cpp
// MeshRoute — src/firmware_ui_model.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Pure screen/state model for the one-button board UI. Consumes a gesture plus a plain-data snapshot and produces what
// to draw. Knows nothing of g_node, Arduino or the display — that is what keeps it native-testable and every hardware
// concern in board_ui.cpp. See docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md §2-§5.
#pragma once
#include <cstddef>   // std::size_t (copy_clamped) — do NOT rely on <cstdint> to drag it in transitively
#include <cstdint>
#include "firmware_ui_input.h"

namespace mrui {

inline constexpr uint32_t kBlankMs      = 15000;
inline constexpr uint8_t  kMaxTeamRows  = 8;    // spec §11: a 3-10 member group; the snapshot reports the TRUE total too
inline constexpr uint8_t  kMaxInboxRows = 8;
inline constexpr uint8_t  kLabelCap     = 14;   // display-clamped teammate label

enum class Screen  : uint8_t { status = 0, team, inbox, send, count };
enum class Compose : uint8_t { none = 0, dm, channel };

inline constexpr uint8_t kDmTextCount      = 3;   // "Are you OK?", "I'm OK", back
inline constexpr uint8_t kChannelTextCount = 3;   // "Got your message", "All good", back

// The model NEVER sends — it ASKS. firmware_ui.cpp drains the request, performs the send and feeds back a typed outcome.
enum class SendKind : uint8_t { emergency = 0, dm, channel_canned };
struct SendReq { SendKind kind = SendKind::emergency; uint8_t peer_id = 0; uint8_t text_index = 0; };

struct TeamRow {
    uint8_t  id = 0; uint32_t last_heard_s = 0; int16_t score_q4 = 0; uint8_t hops = 0;
    char     label[kLabelCap + 1] = {};   // resolved name / 0xhash / bare id, already clamped (spec §3.3)
};
struct InboxRow {
    bool     is_dm = false; uint8_t channel_id = 0; uint32_t rx_age_s = 0;
    char     text[21] = {};               // clamped to the panel width
};

struct UiSnapshot {
    uint32_t now_ms = 0;
    uint16_t unread_dm = 0, unread_ch = 0;
    uint32_t last_dm_age_s = UINT32_MAX, last_ch_age_s = UINT32_MAX;
    uint8_t  team_shown = 0, team_total = 0;      // shown <= kMaxTeamRows; total = rt_team_count() (spec §3.3)
    TeamRow  team[kMaxTeamRows] = {};
    uint8_t  inbox_shown = 0; uint16_t inbox_total = 0;
    InboxRow inbox[kMaxInboxRows] = {};
    uint8_t  my_team_id = 0; uint32_t team_id = 0;
    int32_t  batt_mv = -1;                        // <0 = unavailable -> render "--", never a guess
    bool     team_build = true;
};

struct UiState {
    Screen  screen = Screen::status;
    uint8_t cursor = 0;
    Compose compose = Compose::none;
    uint8_t compose_peer = 0;   // bound at ENTRY: the roster can reorder under an open modal, which would retarget it
    bool    blanked = false;
    bool    dirty   = true;
};

class UiModel {
public:
    void on_gesture(Gesture g, const UiSnapshot& s) {
        if (g == Gesture::none) return;
        _last_input_ms = s.now_ms;
        // ★ spec §4.2: emergency gestures pre-empt EVERYTHING — blank-wake and the compose modal both.
        if (g == Gesture::long_arm || g == Gesture::long_fire || g == Gesture::long_cancel) {
            _st.blanked = false; emergency_gesture(g, s); _st.dirty = true; return;
        }
        if (_st.blanked) { _st.blanked = false; _st.dirty = true; return; }   // the waking press is CONSUMED
        if (_st.compose != Compose::none) { compose_gesture(g); return; }
        if (g == Gesture::short_press)  { advance_or_next(s); _st.dirty = true; }
        else if (g == Gesture::double_press) { activate(s);   _st.dirty = true; }
    }

    void on_tick(const UiSnapshot& s) {
        tick_emergency(s);                                        // Task 3
        if (_st.compose != Compose::none && elapsed(s.now_ms, _last_input_ms) >= kBlankMs) {
            _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true;   // never outlive attention; sends nothing
        }
        if (!_st.blanked && !hold_active(s.now_ms) &&
            elapsed(s.now_ms, _last_input_ms) >= kBlankMs) { _st.blanked = true; _st.dirty = true; }
    }

    const UiState& state() const { return _st; }
    void clear_dirty() { _st.dirty = false; }
    // ★ TWO independent slots, emergency first. One shared slot would let a normal compose action OVERWRITE a queued
    // alarm, and (with the tick's in-flight gate) serialise the emergency behind a DM awaiting its e2e ack — which
    // defeats "long press fires from any screen". Normal work never touches the emergency slot. Spec §2.1.
    // ★ §B75: `DmState::submitting` is written HERE — this is the real hand-off to dispatch (spec :263). The enumerator
    //   existed with no writer, and an in-source comment wrongly called it "written-but-unread".
    bool take_send_request(SendReq& out) {
        if (_emg_req_pending) { _emg_req_pending = false; out = SendReq{SendKind::emergency, 0, 0}; return true; }
        if (!_req_pending) return false;
        _req_pending = false; out = _req; return true;
    }
    bool emergency_pending() const { return _emg_req_pending; }

protected:
    // Wrap-safe elapsed time. millis() wraps at ~49.7 days; `a >= b` would break across it, this does not.
    static uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
    void queue(SendKind k, uint8_t peer, uint8_t idx) {
        if (k == SendKind::emergency) { _emg_req_pending = true; return; }   // its own slot; never overwritten
        _req = {k, peer, idx}; _req_pending = true;
    }

    UiState  _st{};
    uint32_t _last_input_ms = 0;
    SendReq  _req{};
    bool     _req_pending = false;
    bool     _emg_req_pending = false;   // separate slot: normal work can never clobber a queued alarm

private:
    void advance_or_next(const UiSnapshot& s) {
        const uint8_t n = list_len(s);
        if (n > 1 && _st.cursor + 1 < n) { ++_st.cursor; return; }
        _st.screen = next_screen(_st.screen, s); _st.cursor = 0;
    }
    void activate(const UiSnapshot& s) {
        if (_st.screen == Screen::team && s.team_shown > 0) {
            _st.compose = Compose::dm; _st.compose_peer = s.team[_st.cursor % s.team_shown].id; _st.cursor = 0;
        } else if (_st.screen == Screen::send) {
            _st.compose = Compose::channel; _st.compose_peer = 0; _st.cursor = 0;
        }
    }
    void compose_gesture(Gesture g) {
        const uint8_t n = (_st.compose == Compose::dm) ? kDmTextCount : kChannelTextCount;
        if (g == Gesture::short_press) { _st.cursor = uint8_t((_st.cursor + 1) % n); _st.dirty = true; return; }
        if (g != Gesture::double_press) return;
        if (_st.cursor + 1 == n) { _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true; return; }  // `back`
        queue(_st.compose == Compose::dm ? SendKind::dm : SendKind::channel_canned, _st.compose_peer, _st.cursor);
        _st.compose = Compose::none; _st.cursor = 0; _st.dirty = true;
    }
    uint8_t list_len(const UiSnapshot& s) const {
        if (_st.screen == Screen::team)  return s.team_shown;
        if (_st.screen == Screen::inbox) return s.inbox_shown;
        return 1;
    }
    static Screen next_screen(Screen cur, const UiSnapshot& s) {
        for (uint8_t i = 1; i <= uint8_t(Screen::count); ++i) {
            const Screen cand = Screen((uint8_t(cur) + i) % uint8_t(Screen::count));
            if (s.team_build || cand == Screen::status || cand == Screen::inbox) return cand;
        }
        return Screen::status;
    }
    // Task 3 supplies these; declared here so on_gesture/on_tick compile in task order.
    void emergency_gesture(Gesture g, const UiSnapshot& s);
    void tick_emergency(const UiSnapshot& s);
    bool hold_active(uint32_t now_ms) const;
};

}  // namespace mrui
```

Task 3 defines the three declared members **inline in this same header**, immediately after the class. Do not leave them undefined at the end of Task 2 — instead add temporary inline definitions (`emergency_gesture` empty, `tick_emergency` empty, `hold_active` returning `false`) so Task 2 links and its tests run; Task 3 replaces them.

- [ ] **Step 4: Run and verify all seven cases pass**
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 3: Emergency and DM outcome machines

**Files:** Modify `src/firmware_ui_model.h`, `test/test_firmware_ui_model.cpp`.

**Interfaces:**
- Produces: `Emergency`, `DmState`, `SendOutcome`, and on `UiModel`: `on_send_accepted(SendKind, uint32_t now_ms)`, `on_send_refused(SendKind, Reason, uint32_t now_ms)`   // §B78: now_ms REQUIRED — see below, `on_outcome(const SendOutcome&, uint32_t now_ms)`, `emergency()`, `dm_state()`, `arming_secs_left()`, `retry_at_ms()`. Task 4 calls all of these.

Read spec §4 and §4.1-§4.4 first. **Three rules are non-negotiable and each fixes a bug found in review:**

1. An attempt is counted on **acceptance**, never on request — a refusal or a pre-TX block must not consume one of the three alarms.
2. The retry deadline is `now_ms + next_ms` computed **when the block arrives**, not from the originating gesture.
3. `next_ms == 0` means "floor passed, cap/duty still blocking" — it must **not** retry immediately (that spins every tick and burns all three alarms in milliseconds). Use a UI backoff: 2 s, doubling, capped at 30 s, consuming no attempt.

- [ ] **Step 1: Write the failing tests (append)**

```cpp
TEST_CASE("arm then cancel never emits a send") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000));   CHECK(m.emergency() == Emergency::arming);
    m.on_gesture(Gesture::long_cancel, snap(2000));CHECK(m.emergency() == Emergency::cancelled);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("cancelled auto-returns to idle after its window") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_cancel, snap(2000));
    m.on_tick(snap(2000 + kCancelledMs + 1));
    CHECK(m.emergency() == Emergency::idle);
}
TEST_CASE("arming countdown is visible and decreases") {
    UiModel m;
    m.on_gesture(Gesture::long_arm, snap(1000));
    const uint8_t a = m.arming_secs_left(snap(1200));
    const uint8_t b = m.arming_secs_left(snap(2400));
    CHECK(b < a);
}
TEST_CASE("attempts are counted on ACCEPTANCE, not on request") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    const bool got = m.take_send_request(req);   // §B70: ONE call — take_send_request DRAINS
    CHECK(got == true);
    if (!got) return;
    m.on_send_refused(SendKind::emergency, RefuseReason::parser, 4600);   // §B78: the REFUSAL time, not the gesture's (4500)
    CHECK(m.emergency() == Emergency::failed);
    CHECK(m.attempts() == 0);                                          // no alarm consumed
}
TEST_CASE("exactly THREE accepted transmissions, then sticky NOT HEARD") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    for (int i = 1; i <= 3; ++i) {
        const bool got = m.take_send_request(req);   // §B70: ONE call — take_send_request DRAINS
        CHECK(got == true);
        if (!got) return;
        m.on_send_accepted(SendKind::emergency, 5000u * uint32_t(i));
        CHECK(m.attempts() == i);
        m.on_outcome(SendOutcome::channel_no_relay(), 5000u * uint32_t(i) + 100);
    }
    CHECK(m.emergency() == Emergency::not_heard);
    CHECK(m.take_send_request(req) == false);
}
TEST_CASE("blocked computes the deadline from the OUTCOME time, not the gesture") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(10000), /*now_ms=*/60000);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.retry_at_ms() == 70000);                    // 60000 + 10000, NOT 4500 + 10000
    m.on_tick(snap(69000)); CHECK(m.take_send_request(req) == false);
    m.on_tick(snap(70001)); CHECK(m.take_send_request(req) == true);
}
TEST_CASE("next_ms == 0 backs off instead of spinning, and consumes no attempt") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::blocked(0), 5100);
    CHECK(m.emergency() == Emergency::blocked);
    CHECK(m.retry_at_ms() == 5100 + kBlockedBackoffMinMs);
    m.on_tick(snap(5100 + kBlockedBackoffMinMs - 1)); CHECK(m.take_send_request(req) == false);
    m.on_tick(snap(5100 + kBlockedBackoffMinMs + 1)); CHECK(m.take_send_request(req) == true);
    CHECK(m.attempts() == 1);                            // the block did not consume an alarm
}
TEST_CASE("retry deadline is wrap-safe") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 0xFFFFF000u);
    m.on_outcome(SendOutcome::blocked(0x2000), 0xFFFFF000u);   // deadline wraps past 2^32
    m.on_tick(snap(0x00001001u));
    CHECK(m.take_send_request(req) == true);
}
TEST_CASE("long gestures work from inside a compose sub-view") {
    UiModel m; const auto s = snap();
    m.on_gesture(Gesture::short_press, s); m.on_gesture(Gesture::double_press, s);
    CHECK(m.state().compose == Compose::dm);   // §B67: was REQUIRE; nothing below dereferences, so a bare CHECK is sound here
    m.on_gesture(Gesture::long_arm, s);
    CHECK(m.emergency() == Emergency::arming);
}
TEST_CASE("long gestures work from a blanked panel") {
    UiModel m;
    m.on_tick(snap(1000)); m.on_tick(snap(1000 + kBlankMs + 1));
    CHECK(m.state().blanked == true);          // §B67: ditto
    m.on_gesture(Gesture::long_arm, snap(1000 + kBlankMs + 10));
    CHECK(m.emergency() == Emergency::arming);
    CHECK(m.state().blanked == false);
}
TEST_CASE("a matching teammate reply becomes sticky human confirmation") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_outcome(SendOutcome::channel_relayed(), 5100);
    CHECK(m.emergency() == Emergency::picked_up);
    m.on_reply("Ann", "on my way", 6000);
    CHECK(m.emergency() == Emergency::reply);
}
TEST_CASE("DM outcomes are independent of the emergency machine") {
    UiModel m; SendReq req{};
    m.on_gesture(Gesture::long_arm, snap(1000)); m.on_gesture(Gesture::long_fire, snap(4500));
    m.take_send_request(req); m.on_send_accepted(SendKind::emergency, 5000);
    m.on_send_accepted(SendKind::dm, 5100);                       // a DM in flight alongside
    m.on_outcome(SendOutcome::dm_no_key(), 5200);
    CHECK(m.dm_state()  == DmState::no_key);
    CHECK(m.emergency() == Emergency::firing);                    // UNTOUCHED
    m.on_outcome(SendOutcome::channel_relayed(), 5300);
    CHECK(m.emergency() == Emergency::picked_up);
}
```

- [ ] **Step 2: Run and verify it fails**

- [ ] **Step 3: Implement**

Add above `class UiModel`:

⚠ **This block also needs a new include** — `SendOutcome` names a `lib/core` type, so `firmware_ui_model.h` gains
`#include "command.h"` (the app seam: typed PODs, no Arduino, no heap, no `Node`, so the unit stays pure). Precedent:
`src/firmware_config_parse.h` includes `protocol_constants.h`.

```cpp
// ★★ kEmgHoldMs was OWNER-RE-RULED 2026-08-04 (120000 -> 30000). ⚠ This line and the constants test are the ONLY two
// places the number may appear — nothing else restates it, because the first B78 write-up hardcoded the old value in
// prose and went stale the instant it changed.
inline constexpr uint32_t kEmgHoldMs            = 30000;
inline constexpr uint32_t kCancelledMs          = 1000;
inline constexpr uint8_t  kEmgMaxTries          = 3;      // THREE TRANSMISSIONS, counted on acceptance
inline constexpr uint32_t kBlockedBackoffMinMs  = 2000;   // next_ms==0 policy: 2s, doubling, capped
inline constexpr uint32_t kBlockedBackoffMaxMs  = 30000;

enum class Emergency : uint8_t { idle = 0, arming, firing, blocked, picked_up, not_heard, reply, cancelled, failed };
enum class DmState   : uint8_t { idle = 0, submitting, waiting_ack, delivered, no_key, not_confirmed, failed };
// The COMPACT panel reason. Deliberately NOT a mirror of SendFailReason: `parser` has no core equivalent (the line
// never became a Command), and the three that do are the ones whose remedy differs. Everything else is `other`, with
// §B73's `fail_reason()` carrying the core reason verbatim beside it, so nothing is discarded.
enum class RefuseReason : uint8_t { parser = 0, unsealable, no_location, queue_full, other };
// ⚠ An ALIAS, not a UI enum. ★ And note the SPELLING: `meshroute` is not a namespace name in this codebase, it is the
// DEFAULT of an overridable macro (`command.h:13-15`, `-DMESHROUTE_NS=meshroute_gw` for the two-lib gateway variant),
// so a pure `src/` header writes `MESHROUTE_NS::` — the idiom already established at `src/firmware_config_parse.h:32`.
// `mrui::FailReason::x` and `FailReason::x` are the same value of the same type, so Task 4 may keep
// either spelling.
using FailReason = MESHROUTE_NS::SendFailReason;

// A correlated outcome. Built ONLY by the send tracker (Task 4) after it has matched ctr/peer/channel — the model never
// sees a raw Push, which is what makes a false PICKED UP structurally impossible (spec §2.1).
struct SendOutcome {
    // ★★ §B68 (2026-08-03): `channel_remote_mint` is the EIGHTH kind and it is a SUCCESS. Without it Task 4 must call a
    // DELIVERED message failed: on a registered mobile a plain/`-g` GLOBAL post goes through
    // `do_send_channel_delegated` — a real MOBILE_SEND DM flies — but the HOME mints the channel ctr, so `ctr` stays 0
    // (`lib/core/node.cpp:1565-1573`, register B39). ⇒ `ctr != 0` = we own the handle, exact correlation valid;
    // `ctr == 0` = **no LOCAL handle exists and whether anything flew is not answerable synchronously.**
    // ⓘ Unreachable on the team-plane alarm path (`MR_UI_TEAM_CHANNEL_ID`), so this is type correctness, not a live
    // safety hole — but the type must not make the truth unrepresentable. Render it as SENT, never as PICKED UP.
    // ★★ §B72 (QA, 2026-08-03): `channel_failed` was REQUIRED by this plan (the tracker then returned it at the since-DELETED `match_channel_failed`,
    // and a Task-4 test checks `Kind::channel_failed`) but was MISSING from the type — an independent compile probe fails with
    // `'channel_failed' is not a member of 'mrui::SendOutcome'`. ⇒ **without it a pre-enqueue SEAL FAILURE leaves an emergency
    // showing `SENDING...` forever** — a safety-path defect, not a typing nicety. It is the NINTH kind.
    // ★★ §B73 (QA, 2026-08-03): **both failure kinds CARRY THEIR `SendFailReason`.** Spec §147 requires the full reason reach
    // the UI; a reasonless `dm_failed()` left `refuse_reason()` stuck at `other`, so the screen could not say WHY.
    // ⚠ Check the enumerator spellings against `lib/core/command.h` — do not trust this block for them (plan `:972`).
    enum class Kind : uint8_t { channel_relayed, channel_no_relay, channel_remote_mint, channel_failed,
                                blocked, dm_acked, dm_no_key, dm_failed, dm_timeout };
    Kind     kind = Kind::channel_no_relay;
    uint32_t next_ms = 0;
    FailReason reason = FailReason::none;   // §B73: meaningful for channel_failed / dm_failed ONLY
    static SendOutcome channel_relayed()   { return {Kind::channel_relayed, 0}; }
    static SendOutcome channel_no_relay()  { return {Kind::channel_no_relay, 0}; }
    static SendOutcome channel_remote_mint() { return {Kind::channel_remote_mint, 0}; }   // §B68: accepted, ctr minted elsewhere
    static SendOutcome blocked(uint32_t n) { return {Kind::blocked, n}; }
    static SendOutcome dm_acked()          { return {Kind::dm_acked, 0}; }
    static SendOutcome channel_failed(FailReason r) { return {Kind::channel_failed, 0, r}; }   // §B72: pre-enqueue, TERMINAL
    static SendOutcome dm_no_key()         { return {Kind::dm_no_key, 0}; }
    static SendOutcome dm_failed(FailReason r) { return {Kind::dm_failed, 0, r}; }   // §B73: reason REQUIRED, never defaulted
    static SendOutcome dm_timeout()        { return {Kind::dm_timeout, 0}; }
};
```

Add to `UiModel`'s public section:

```cpp
    Emergency emergency() const { return _emg; }
    DmState   dm_state()  const { return _dm; }
    uint8_t   attempts()  const { return _tries; }
    // ⚠ Meaningful ONLY while `emergency() == Emergency::blocked`. §B74: it is no longer sentinel-encoded, so there is
    // no "no deadline" value to test for — the STATE is the predicate, and any 32-bit value is a legitimate deadline.
    uint32_t  retry_at_ms() const { return _retry_at_ms; }
    // ⚠ Both are written only when the model ENTERS a failure state; read them only while `dm_state() == failed` or
    // `emergency() == failed`. `no_key` / `not_confirmed` ARE their reason, which is why they get their own `Kind`.
    RefuseReason refuse_reason() const { return _refuse; }
    FailReason   fail_reason()   const { return _fail; }
    uint8_t   arming_secs_left(const UiSnapshot& s) const {
        if (_emg != Emergency::arming) return 0;
        const uint32_t left = _arm_fire_at_ms - s.now_ms;
        return (left > 60000u) ? 0 : uint8_t((left + 999) / 1000);        // wrap-safe: a huge value means past-due
    }

    void on_send_accepted(SendKind k, uint32_t now_ms) {
        if (k == SendKind::emergency) { ++_tries; _last_try_ms = now_ms; }
        else if (k == SendKind::dm)   { _dm = DmState::waiting_ack; }
        _st.dirty = true;
    }
    // The SYNCHRONOUS refusal path — it never became a core send, so `_fail` is CLEARED rather than left describing an
    // older failure. ★★ §B78 (owner-ruled 2026-08-04): a terminal FAILED alarm is RETAINED. ⚠ `now_ms` is a PARAMETER,
    // not `_last_input_ms`: the refusal can arrive well after the gesture, and anchoring on the gesture is the same
    // defect §4.3 exists to kill. Only the EMERGENCY branch retains — a DM refusal must not extend the alarm's window.
    void on_send_refused(SendKind k, RefuseReason r, uint32_t now_ms) {
        _refuse = r; _fail = FailReason::none;
        if (k == SendKind::emergency) { _emg = Emergency::failed; retain(now_ms); }
        else if (k == SendKind::dm)   { _dm  = DmState::failed; }
        _st.dirty = true;
    }
    void on_outcome(const SendOutcome& o, uint32_t now_ms) {
        using K = SendOutcome::Kind;
        switch (o.kind) {
            case K::dm_acked:   _dm = DmState::delivered;     _st.dirty = true; return;
            case K::dm_no_key:  _dm = DmState::no_key;        _st.dirty = true; return;
            case K::dm_timeout: _dm = DmState::not_confirmed; _st.dirty = true; return;
            case K::dm_failed:  _dm = DmState::failed;        note_failure(o.reason); _st.dirty = true; return;
            // ★ The channel kinds fall through to the emergency section. Listed EXPLICITLY with NO `default:` — §B72
            // was a kind the type did not carry, and a `default:` is exactly what lets a tenth land silently instead of
            // failing the build on -Werror=switch (proven: adding one gives `error: enumeration value … not handled`).
            case K::channel_relayed: case K::channel_no_relay: case K::channel_remote_mint:
            case K::channel_failed:  case K::blocked: break;
        }
        // ⓘ The live-state gate covers channel_failed too: a seal failure belonging to no live alarm is dropped whole.
        if (_emg != Emergency::firing && _emg != Emergency::blocked) return;
        if (o.kind == K::blocked) {
            _emg = Emergency::blocked;
            const uint32_t d = (o.next_ms > 0) ? o.next_ms : next_backoff();
            _retry_at_ms = now_ms + d; _retry_armed = true;    // ★ from the OUTCOME time, not the gesture
            retain(now_ms); _st.dirty = true; return;
        }
        // ★ §B72: pre-enqueue failure. TERMINAL — never one of the three alarms and never a retry (`unsealable` /
        // `no_location` are PERMANENT in command.h). §B78: it RETAINS, from the outcome time.
        if (o.kind == K::channel_failed) {
            _emg = Emergency::failed; note_failure(o.reason); retain(now_ms); _st.dirty = true; return;
        }
        if (o.kind == K::channel_relayed) { _emg = Emergency::picked_up; retain(now_ms); _st.dirty = true; return; }
        if (_tries >= kEmgMaxTries) { _emg = Emergency::not_heard; retain(now_ms); _st.dirty = true; return; }
        _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    // ★ Whitelist + "an alarm actually went out". Accepting every non-idle state would let a coincident channel-0 post
    // become REPLY during `arming` (before the user even committed), or after `cancelled`/`failed` — manufacturing
    // confirmation of a message that was never sent. Spec §4.4.
    void on_reply(const char* who, const char* text, uint32_t now_ms) {
        const bool ok = (_emg == Emergency::firing || _emg == Emergency::blocked ||
                         _emg == Emergency::picked_up || _emg == Emergency::not_heard || _emg == Emergency::reply);
        if (!ok || _tries == 0) return;
        copy_clamped(_reply_who,  who,  sizeof _reply_who);
        copy_clamped(_reply_text, text, sizeof _reply_text);
        _emg = Emergency::reply; retain(now_ms); _st.dirty = true;
    }
    const char* reply_who()  const { return _reply_who; }
    const char* reply_text() const { return _reply_text; }
```

And define the three members Task 2 declared:

```cpp
inline void UiModel::emergency_gesture(Gesture g, const UiSnapshot& s) {
    if (g == Gesture::long_arm)    { _emg = Emergency::arming; _arm_fire_at_ms = s.now_ms + kArmToFireMs; return; }
    if (g == Gesture::long_cancel) { _emg = Emergency::cancelled; _cancelled_until_ms = s.now_ms + kCancelledMs; return; }
    // long_fire — a NEW alarm: the budget, the backoff and any armed retry all reset, so a sticky NOT HEARD is always
    // re-firable by another long press.
    _emg = Emergency::firing; _tries = 0; _backoff_ms = 0; _retry_armed = false;
    retain(s.now_ms);
    queue(SendKind::emergency, 0, 0);
}
inline void UiModel::tick_emergency(const UiSnapshot& s) {
    if (_emg == Emergency::cancelled && elapsed(s.now_ms, _cancelled_until_ms) < (1u << 31)) { _emg = Emergency::idle; _st.dirty = true; }
    // ★★ §B74: gate on `_retry_armed`, NEVER on a sentinel value. `now_ms + next_ms` is unbounded, so it can land on
    // any 32-bit value — `now = 0xFFFFF000`, `next_ms = 0xFFF` makes exactly `0xFFFFFFFF`, and a sentinel there left
    // the alarm BLOCKED FOR EVER. Never reintroduce a magic deadline value.
    if (_emg == Emergency::blocked && _retry_armed &&
        elapsed(s.now_ms, _retry_at_ms) < (1u << 31)) {                 // wrap-safe "now >= deadline"
        _retry_armed = false; _emg = Emergency::firing; queue(SendKind::emergency, 0, 0); _st.dirty = true;
    }
    if (_emg == Emergency::arming) {                                     // dirty ONLY when the visible digit changes
        const uint8_t d = arming_secs_left(s);
        if (d != _last_countdown) { _last_countdown = d; _st.dirty = true; }
    }
}
// ★ The hold is a DEADLINE, not a duration. Returning kEmgHoldMs and letting on_tick measure it from _last_input_ms
// (an earlier draft) meant a reply that set a fresh _emg_hold_until_ms never actually extended the window, and
// picked_up fell back to the ordinary kBlankMs blank. Read the field; compare wrap-safely. Spec §4.3.
// ★★ §B78 (owner-ruled 2026-08-04): `failed` is IN the set. Every other emergency outcome held the panel; the one that
// says the alarm did NOT go out fell back to the 15 s blank — the worst place to lose the message. Both producers
// (`on_send_refused`, and `channel_failed` in `on_outcome`) retain from their own arrival time.
// ⓘ `arming` is in the set but nothing writes the deadline on `long_arm`; it is inert (arming lasts kArmToFireMs, so
// the kBlankMs blank cannot fire during it). Left as-is deliberately — do not rely on it, do not "fix" it unruled.
inline bool UiModel::hold_active(uint32_t now_ms) const {
    const bool retained = (_emg == Emergency::arming    || _emg == Emergency::firing  ||
                           _emg == Emergency::blocked   || _emg == Emergency::picked_up ||
                           _emg == Emergency::not_heard || _emg == Emergency::reply    ||
                           _emg == Emergency::failed);
    return retained && elapsed(_emg_hold_until_ms, now_ms) < (1u << 31);   // now < deadline, wrap-safe
}
```

and replace the blanking line in `on_tick` (Task 2) with:

```cpp
        if (!_st.blanked && !hold_active(s.now_ms) &&
            elapsed(s.now_ms, _last_input_ms) >= kBlankMs) { _st.blanked = true; _st.dirty = true; }
```

`_emg_hold_until_ms` is set on `long_fire` **and on every retained outcome** — `picked_up`, `not_heard`, `blocked`, `reply`, and (§B78) `failed` — so each refreshes the `kEmgHoldMs` window, all through the one `retain()` helper. Replace the Task 2 declaration of `blank_limit()` with `bool hold_active(uint32_t) const;`.

with the private members `_emg`, `_dm`, `_refuse`, `_fail` (§B73), `_tries`, `_retry_armed` (§B74) and `_retry_at_ms`, `_last_try_ms`, `_arm_fire_at_ms`, `_cancelled_until_ms`, `_emg_hold_until_ms`, `_backoff_ms`, `_last_countdown`, `_reply_who[kLabelCap+1]`, `_reply_text[21]`, and `kArmToFireMs = 3500` matching `InputCfg::fire_ms`. ⛔ **No `_no_deadline` sentinel** — §B74 is exactly that constant. Plus:

```cpp
    uint32_t next_backoff() {
        _backoff_ms = (_backoff_ms == 0) ? kBlockedBackoffMinMs
                                         : ((_backoff_ms * 2 > kBlockedBackoffMaxMs) ? kBlockedBackoffMaxMs : _backoff_ms * 2);
        return _backoff_ms;
    }
    static void copy_clamped(char* dst, const char* src, std::size_t cap) {
        std::size_t i = 0; for (; src && src[i] && i + 1 < cap; ++i) dst[i] = src[i]; dst[i] = '\0';
    }
    // ★ Spec §4.3: ONE helper, so every retained state refreshes the same deadline. Anchoring only at long_fire meant
    // an outcome a whole window later inherited the leftover time and the panel blanked seconds after the news.
    void retain(uint32_t now_ms) { _emg_hold_until_ms = now_ms + kEmgHoldMs; }
    // §B73: record an ASYNC failure in BOTH alphabets. The `default:` here is deliberate and is NOT §B72's hole:
    // `SendFailReason` is append-only and grows on core's schedule, a new reason is legitimately generic to this panel,
    // and `_fail` carries it losslessly anyway. Mapped = the three whose remedy differs.
    void note_failure(FailReason r) {
        _fail = r;
        switch (r) {
            case FailReason::unsealable:  _refuse = RefuseReason::unsealable;  break;
            case FailReason::no_location: _refuse = RefuseReason::no_location; break;
            case FailReason::queue_full:  _refuse = RefuseReason::queue_full;  break;
            default:                      _refuse = RefuseReason::other;       break;
        }
    }
```

- [ ] **Step 4: Run and verify every case passes** (Tasks 1-3 together)
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 4: Send tracker — attribution

**Files:** Create `src/firmware_ui_send.h`, `test/test_firmware_ui_send.cpp`.

**Interfaces:**
- ★★ §B79 (UI-4, 2026-08-04): **`bool tick(uint32_t now_ms, SendOutcome& out)` IS REQUIRED — it is the window's expiry
  AND `channel_remote_mint`'s ONLY producer.** Without it that kind had **no producer at all** while B39's producer (3)
  **emits no channel-level push whatsoever** (`lib/core/node.cpp:1631-1634`) ⇒ an `awaiting` slot could never be closed,
  **the alarm sat on `SENDING...` for ever and the send slot leaked permanently** — §B72's defect one level up, reached
  by a *success*. Safe on the alarm path as a property of UI-3: `on_outcome` routes it through `channel_no_relay` ⇒
  bounded retry → `NOT HEARD`, never `PICKED UP`.
- ★ Also required and measured: **`accept(0)` normalises to `awaiting`**; **`late_ack` accepts ONLY an ack** (a later
  failure was downgrading `NO CONFIRM`); **`out.reason` set uniformly**; **`void close()`**, which UI-6/UI-7 must call
  to bound `late_ack` (§B83). ⚠ The test block needs **`#include <initializer_list>`** — the B40 case iterates a
  braced list (§B76's seventh error, missed by the scratch-TU probe).
- Produces: `mrui::SendTracker` with `void submit(SendKind, uint8_t peer_id, uint8_t channel_id, uint32_t now_ms)`, `void accept(uint16_t ctr, uint32_t now_ms)`, `bool tick(uint32_t, SendOutcome&)`, `void close()`, `void refuse()`, `bool match_channel_sent(uint16_t ctr, bool relayed, SendOutcome& out)`, `bool match_blocked(bool blocked_channel, uint32_t next_ms, uint32_t now_ms, SendOutcome& out)`, `bool match_dm(uint16_t ctr, uint8_t dst, bool acked, FailReason r, SendOutcome& out)`, `bool idle() const`.

★ **This is the task that prevents a false safety confirmation.** Pushes are node-wide: a console post, a BLE post or a canned message all raise `channel_sent`. Without correlation, any of them completes an emergency that was never transmitted. Read spec §2.1.

- [ ] **Step 1: Write the failing test**

```cpp
// test/test_firmware_ui_send.cpp
#include <doctest.h>
#include "firmware_ui_send.h"
#include <initializer_list>   // §B76 (seventh error): the B40 case iterates a braced list
using namespace mrui;

TEST_CASE("an unrelated channel_sent cannot complete the emergency") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(/*ctr=*/77, 1010);
    CHECK(t.match_channel_sent(/*ctr=*/12, /*relayed=*/true, o) == false);   // someone else's post
    CHECK(t.match_channel_sent(/*ctr=*/77, /*relayed=*/true, o) == true);
    CHECK(o.kind == SendOutcome::Kind::channel_relayed);
}
TEST_CASE("a blocked DM cannot block the emergency") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.match_blocked(/*blocked_channel=*/false, 5000, 1020, o) == false);
    CHECK(t.match_blocked(/*blocked_channel=*/true,  5000, 1020, o) == true);
}
TEST_CASE("a blocked event outside the outcome window is ignored") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.accept(77, 1010);
    CHECK(t.match_blocked(true, 5000, 1010 + kOutcomeWindowMs + 1, o) == false);
}
TEST_CASE("a DM outcome must match ctr AND peer") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, /*peer=*/174, 0, 1000); t.accept(/*ctr=*/900, 1010);
    CHECK(t.match_dm(900, /*dst=*/99,  true, FailReason::none, o) == false);   // §B76: 4th arg is a REASON, not a bool. right ctr, wrong peer
    CHECK(t.match_dm(901, /*dst=*/174, true, FailReason::none, o) == false);   // §B76. right peer, wrong ctr
    CHECK(t.match_dm(900, /*dst=*/174, true, FailReason::none, o) == true);    // §B76 — acked, so the reason is `none`
    CHECK(o.kind == SendOutcome::Kind::dm_acked);
}
TEST_CASE("no_pubkey maps to dm_no_key") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    const bool ok = t.match_dm(900, 174, false, FailReason::no_pubkey, o);   // §B76: the REASON, not a bool flag   // §B70: ONE call — the matcher CONSUMES
    CHECK(ok == true);
    if (!ok) return;
    CHECK(o.kind == SendOutcome::Kind::dm_no_key);
}
TEST_CASE("a refused submit leaves nothing to match") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.refuse();
    CHECK(t.idle() == true);
    CHECK(t.match_channel_sent(77, true, o) == false);
}
TEST_CASE("B39: a ctr==0 result awaits its outcome and never claims a channel_sent") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.awaiting_outcome(1010);
    CHECK(t.match_channel_sent(0, true, o) == false);              // ctr 0 is a sentinel, not a handle
    CHECK(t.match_blocked(true, 5000, 1020, o) == true);
}
TEST_CASE("§B84 an UNATTRIBUTABLE async send_failed does NOT end the alarm") {
    // Replaces the old "a channel seal failure is terminal" case, which exercised the DELETED
    // `match_channel_failed` (§B84). A pre-enqueue seal failure is now a SYNCHRONOUS refusal — covered by
    // test_firmware_ui_model's `on_send_refused` cases — so what needs pinning here is the safety default:
    // a `send_failed` the tracker cannot attribute must be IGNORED, leaving the bounded retry to reach NOT HEARD.
    // ⚠ Use a NON-channel shape that previously collided: `send_layer` also pushes reason=unsealable with dst=0
    // (`lib/core/node.cpp:1886`). Before §B84 this ended a live alarm and discarded two unspent transmissions.
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::emergency, 0, 0, 1000); t.awaiting_outcome(1010);
    CHECK(t.match_dm(/*ctr=*/0, /*dst=*/0, false, FailReason::unsealable, o) == false);   // unattributable -> ignored
    CHECK(t.idle() == false);                                                             // §B84: the alarm is STILL live (`idle()` exists; `state_is_awaiting()` never did)
}
TEST_CASE("B40: full-width counters correlate across the 8-bit boundary") {
    for (uint16_t c : {uint16_t(255), uint16_t(256), uint16_t(257), uint16_t(65535)}) {
        SendTracker t; SendOutcome o{};
        t.submit(SendKind::emergency, 0, 0, 1000); t.accept(c, 1010);
        CHECK(t.match_channel_sent(uint16_t(c & 0xff), true, o) == (c < 256));   // low-byte collider must NOT match
        SendTracker t2; SendOutcome o2{};
        t2.submit(SendKind::emergency, 0, 0, 1000); t2.accept(c, 1010);
        CHECK(t2.match_channel_sent(c, true, o2) == true);
    }
}
TEST_CASE("e2e_ack_timeout yields NO CONFIRM and a late ack upgrades it") {
    SendTracker t; SendOutcome o{};
    t.submit(SendKind::dm, 174, 0, 1000); t.accept(900, 1010);
    const bool ok_timeout = t.match_dm(900, 174, false, FailReason::e2e_ack_timeout, o);   // §B70: ONE call — the matcher CONSUMES
    CHECK(ok_timeout == true);
    if (!ok_timeout) return;
    CHECK(o.kind == SendOutcome::Kind::dm_timeout);
    const bool ok_lateack = t.match_dm(900, 174, true, FailReason::none, o);   // §B70: ONE call — the matcher CONSUMES
    CHECK(ok_lateack == true);
    if (!ok_lateack) return;
    CHECK(o.kind == SendOutcome::Kind::dm_acked);
}
```

⚠ **`SendFailReason::none` / `e2e_ack_timeout` spellings must be checked against `lib/core/command.h` before writing** (V2). Add `channel_failed(SendFailReason)` to `SendOutcome` in Task 3 alongside the existing factories, and a `RefuseReason` mapping for it in the model.

- [ ] **Step 2: Run and verify it fails**

- [ ] **Step 3: Implement**

```cpp
// MeshRoute — src/firmware_ui_send.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★ The attribution layer. Pushes are NODE-WIDE: channel_sent / send_blocked fire for every origination, including
// console, BLE and canned sends. Feeding them straight to the UI model lets an unrelated post complete an emergency
// that was never transmitted — a FALSE SAFETY CONFIRMATION. Nothing reaches the model until it has matched here.
// One in-flight UI send at a time, which is what makes the ctr-less send_blocked correlatable at all. Spec §2.1.
#pragma once
#include <cstdint>
#include "firmware_ui_model.h"

namespace mrui {

inline constexpr uint32_t kOutcomeWindowMs = 8000;   // how long an accepted send may still claim a ctr-less outcome

// ONE SLOT. The UI owns TWO of these (Task 6): an emergency slot and a normal slot, so an alarm never waits on a DM
// that is waiting on its e2e ack. Their pushes are told apart by kind plus ctr/peer.
class SendTracker {
public:
    void submit(SendKind k, uint8_t peer_id, uint8_t channel_id, uint32_t now_ms) {
        _k = k; _peer = peer_id; _chan = channel_id; _state = State::submitted; _submit_ms = now_ms; _ctr = 0;
    }
    // ★ B39, CORRECTED by §B84: `queued` with ctr==0 does NOT mean "not sent" — it means **NO LOCAL HANDLE EXISTS and
    //   transmission status is UNKNOWN** (three producers; the third is a delegated SUCCESS). Unattributable failures
    //   are ignored; expiry supplies `channel_remote_mint` and consumes one bounded attempt. next_ctr never yields 0, so zero is the
    // sentinel. §B84: `accept(0)` is LEGAL and NORMALISES to `awaiting` (below) — callers need not pre-check. ⚠ This clause used to
    // read "caller must not call this with 0", which contradicted the normalisation three lines down.
    // §B84: `accept(0)` NORMALISES to `awaiting` — a `_ctr == 0` row in `accepted` would be matched exactly by an
    // unrelated ctr-0 `channel_sent` and manufacture `PICKED UP`. Not a silent fallback (C2): there is only one
    // state a zero handle can describe.
    void accept(uint16_t ctr, uint32_t now_ms) {
        if (ctr == 0) { awaiting_outcome(now_ms); return; }
        _ctr = ctr; _accept_ms = now_ms; _state = State::accepted;
    }
    // §B84: accepted-shaped result with ctr==0 -> NO LOCAL HANDLE; transmission status UNKNOWN. A matching
    // send_blocked/send_failed is NOT guaranteed, and an unattributable one is ignored -> bounded expiry closes it.
    void awaiting_outcome(uint32_t now_ms)     { _ctr = 0; _accept_ms = now_ms; _state = State::awaiting; }
    void refuse()                              { _state = State::idle; }
    void close()                               { _state = State::idle; }
    bool idle() const                          { return _state == State::idle; }
    SendKind kind() const                      { return _k; }

    bool match_channel_sent(uint16_t ctr, bool relayed, SendOutcome& out) {
        if (_state != State::accepted) return false;
        if (_k == SendKind::dm) return false;
        if (ctr != _ctr) return false;                    // ★ the only reliable correlator
        out = relayed ? SendOutcome::channel_relayed() : SendOutcome::channel_no_relay();
        _state = State::idle; return true;
    }
    // send_blocked carries NO ctr (command.h). Scope by channel-ness + a bounded window. ⚠ This is WEAKER than exact
    // matching and must not be described as exact attribution — it is the best the current push schema allows.
    bool match_blocked(bool blocked_channel, uint32_t next_ms, uint32_t now_ms, SendOutcome& out) {
        if (_state != State::accepted && _state != State::awaiting) return false;
        if (_k == SendKind::dm) return false;
        if (!blocked_channel) return false;
        if (uint32_t(now_ms - _accept_ms) > kOutcomeWindowMs) return false;
        out = SendOutcome::blocked(next_ms);
        _state = State::idle; return true;
    }
    // ⛔ §B84: this introduced the DELETED matcher, and its premise was wrong twice over — the relevant failure is
    // POST-MINT and ASYNCHRONOUS (a counter is already burned), not "before enqueue", and it correlates with nothing.
    // Historical wording: a channel post that failed BEFORE enqueue (seal failure) — the ctr==0 case. Without this the emergency would sit
    // on SENDING... forever after a seal failure, because match_channel_sent can never fire for it.
    // ⛔⛔ §B84 — `match_channel_failed` IS DELETED (owner-ruled 2026-08-04). DO NOT REINTRODUCE IT.
    // §B80 gave it a `dst == 0` correlator; QA measured that **`dst == 0` does not mean "channel"** — six unrelated
    // operations emit exactly that shape (`node.cpp:1886/1906` send_layer, `node_hashlocate.cpp:1600/1636/1650`
    // hash-resolution + delegated grant/sealed-send, `node_mobile.cpp:401` mobile delegate). Since `channel_failed` is
    // TERMINAL, an unrelated console/BLE operation could **end a live alarm and discard its two unspent transmissions**.
    // ★ The QA-gate check that let it through tested the WRONG DIRECTION: it verified *every channel producer passes 0*
    //   and concluded *0 implies channel*. That is the converse, and it is false.
    // ⇒ THE CORRELATOR STAYS DELETED — but ⚠⚠ **THE FIRST RATIONALE FOR DELETING IT WAS HALF WRONG, corrected here
    //   after QA measured it. Two failure classes, not one:**
    //   • **PRE-ENQUEUE (preflight) failures ARE synchronous** — `exec_command` -> `ExecResult` ->
    //     `refuse_reason_of(r)` -> `on_send_refused(kind, r, now_ms)` -> `Emergency::failed`. Nothing to correlate. ✅
    //   • ★★ **THE POST-MINT SEAL FAILURE IS NOT.** `node_channel.cpp:~723-744` (dead RNG / `bad_rng`) has already
    //     **minted and burned a counter**, pushes `send_failed` carrying that inaccessible ctr, and `return 0` — so
    //     `on_command` answers **`CmdResult{queued, ctr = 0}`** (`node.cpp:1609/1653`) and the UI enters
    //     `awaiting_outcome`, NOT a synchronous refusal. **The core says so itself:** *"the reason arrives
    //     asynchronously and correlates with nothing."* ⇒ there IS an unattributable async failure on this path.
    // ⛔⛔ **AND THE 'FAILS SAFE TO NOT HEARD' CLAIM WAS FALSE — it was an INFINITE RETRY LOOP.** `_tries` is
    //   incremented ONLY by `on_send_accepted` (`firmware_ui_model.h:223`; `:326` — *"ACCEPTED transmissions, never
    //   requests"*), and a `ctr == 0` send never calls it. So: seal failure -> await 8 s -> `channel_remote_mint` ->
    //   retry -> await 8 s -> ... **for ever, with `attempts() == 0`**, never reaching `:278`'s `_tries >=
    //   kEmgMaxTries`. Unbounded airtime on the alarm path — worse in that dimension than what it replaced.
    // ⇒ ★★★ **THE RULE (owner-ruled 2026-08-04, completed by QA): AN EXPIRED UNATTRIBUTABLE EMERGENCY CONSUMES ONE
    //   BOUNDED ATTEMPT before `channel_remote_mint` is processed.** Three expiries then terminate in sticky
    //   `NOT HEARD` and no fourth request is queued. This matches the approved *"accepted by the transmitter is what
    //   we can establish"* policy, and fails safely: if the missing push really was a failure, we have honestly spent
    //   an attempt. ⓘ The cost of the ruling is that this rare path loses its precise terminal REASON — accepted.
    bool match_dm(uint16_t ctr, uint8_t dst, bool acked, FailReason r, SendOutcome& out) {
        if ((_state != State::accepted && _state != State::late_ack) || _k != SendKind::dm) return false;
        if (ctr != _ctr || dst != _peer) return false;    // ★ ctr AND peer
        // ★ In `late_ack` the ONLY thing that may still fire is the ack itself. The core deliberately permits
        // `send_e2e_acked` after `e2e_ack_timeout` (command.h:254), so we retain identity to UPGRADE NO CONFIRM to
        // DELIVERED (spec §3.4.1) — but letting a second, later `send_failed` through would DOWNGRADE an already
        // reported `not_confirmed` to a generic `failed`, discarding exactly the distinction command.h insists on
        // ("delivery was never CONFIRMED, NOT that it failed"). A repeat timeout is likewise not news.
        if (_state == State::late_ack && !acked) return false;
        if (acked) { out = SendOutcome::dm_acked(); _state = State::idle; return true; }
        out = (r == FailReason::no_pubkey)       ? SendOutcome::dm_no_key()
            : (r == FailReason::e2e_ack_timeout) ? SendOutcome::dm_timeout()
                                                 : SendOutcome::dm_failed(r);   // §B73: thread the reason
        // ⓘ `dm_no_key` / `dm_timeout` carry the reason too (SendOutcome::reason defaults to `none`, so the two
        // dedicated kinds would otherwise report `none` beside a state that IS its reason). Set it uniformly here so
        // a renderer never has to know which kinds happen to carry it.
        out.reason = r;
        _state = (r == FailReason::e2e_ack_timeout) ? State::late_ack : State::idle;
        return true;
    }

    // ★★ §B79/§B84 — REQUIRED. Task 6 calls this; without it an `awaiting` slot is never closed.
    // ★★★ THE CALLER OBLIGATION, AND IT IS SAFETY-CRITICAL — NOT A STYLE NOTE (owner-ruled 2026-08-04, register B84):
    // ON THE EMERGENCY SLOT, `on_send_accepted(SendKind::emergency, now_ms)` MUST BE CALLED **BEFORE** THE
    // `channel_remote_mint` IS PASSED TO `on_outcome`, SO THE EXPIRY CONSUMES ONE BOUNDED ATTEMPT.
    // ⚠ The earlier claim that this path "fails safe to NOT HEARD" was FALSE, and the measurement is arithmetic:
    // `UiModel::_tries` increments ONLY in `on_send_accepted` ("ACCEPTED transmissions, never requests"), and a
    // `ctr == 0` send never reaches it. Without the consumption the cycle is
    //     seal failure -> awaiting -> 8 s -> channel_remote_mint -> on_outcome -> re-queue -> awaiting -> 8 s -> …
    // FOR EVER with `attempts() == 0`, because `_tries >= kEmgMaxTries` can never become true. That is UNBOUNDED
    // AIRTIME on the distress path — in that one dimension worse than the permanent `SENDING...` it replaced.
    // ⇒ With the consumption: three expiries spend the three-alarm budget and the third terminates in sticky
    // `NOT HEARD` with no fourth request queued. If the missing push really was a failure, an attempt has been
    // honestly spent. Pinned by the four integration cases in test_firmware_ui_send.cpp, which are the tripwire for
    // the UI-6 glue that owes this call.
    bool tick(uint32_t now_ms, SendOutcome& out) {
        if (_state != State::awaiting) return false;                          // `accepted` never expires — see above
        if (uint32_t(now_ms - _accept_ms) <= kOutcomeWindowMs) return false;
        _state = State::idle;
        // A DM reaches `awaiting` only via a HASH-addressed send parked behind an H resolve
        // (node_hashlocate.cpp; "the ctr if sent immediately, else 0"), which the UI never issues — spec §3.4 sends
        // by team_local_id. Guarded anyway: `channel_remote_mint` for a DM would be a type error, so release the slot
        // and invent NOTHING. ⚠ The model is then left in `DmState::submitting`; that display residue is UI-7's, and
        // it is registered rather than papered over with a fabricated reason.
        if (_k == SendKind::dm) return false;
        out = SendOutcome::channel_remote_mint();
        return true;
    }

private:
    enum class State : uint8_t { idle = 0, submitted, accepted, awaiting, late_ack };
    State    _state = State::idle;
    SendKind _k = SendKind::emergency;
    uint8_t  _peer = 0, _chan = 0;
    uint16_t _ctr = 0;
    uint32_t _submit_ms = 0, _accept_ms = 0;
};

}  // namespace mrui
```

- [ ] **Step 4: Run and verify all six cases pass**
- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 5: Board canvas port

**Files:** Modify `platformio.ini` (`[env:heltec_v3]`), create `variants/heltec_v3/board_ui.h` (+ the `-I variants/heltec_v3` this needs — NOT earlier), modify `variants/heltec_v3/board_ui.cpp`.

**Interfaces:**
- Produces: `mrui::board_init()`, `begin_frame()`, `next_page()`, `set_font(Font)`, `draw_text(x,y,const char*)`, `draw_hline(x,y,w)`, `set_power_save(bool)`, `button_pressed()`, `battery_sample_mv()`.

**Read spec §5 first.** A full 1024 B frame at 400 kHz is ~25 ms of blocking I²C; `cts_to_data_gap_ms` is 5 and turnarounds are 5-8 ms. Page-chunking is why the `_1_` (page-buffer) constructor is specified — do not substitute a full-buffer driver.

- [ ] **Step 1: Add the dependency, pinned exactly**

In `[env:heltec_v3]` `lib_deps`:

```ini
  olikraus/U8g2 @ 2.35.30      ; PINNED EXACTLY (same rule as the RadioLib pin on this env): a caret lets different
                               ; checkouts resolve different versions and silently skews the board RAM/Flash baseline.
```

- [ ] **Step 2: Build and record the pre-UI baseline**

Run: `pio run -e heltec_v3` — SUCCESS. Record flash/RAM.

- [ ] **Step 3: Write the canvas header**

```cpp
// MeshRoute — variants/heltec_v3/board_ui.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The display-INDEPENDENT canvas the UI feature layer draws through. Nothing above this line knows U8g2 exists;
// nothing below it knows what a "screen" is (spec §2 hard boundary). ★ This header must NOT include
// firmware_ui_model.h — an earlier plan draft did, and that inverted the boundary the spec promises.
#pragma once
#include <cstdint>

namespace mrui {

enum class Font : uint8_t { small = 0, large };   // 6x10 / 10x20

void board_init();
void begin_frame();                 // compose a new frame; does NOT touch the bus
bool next_page();                   // push ONE page (~3 ms); true while pages remain
void set_font(Font f);
void draw_text(int x, int y, const char* s);
void draw_hline(int x, int y, int w);
void set_power_save(bool on);       // panel off/on WITHOUT clearing display RAM; latched, repeat calls are no-ops
bool button_pressed();
int32_t battery_sample_mv();        // one sample; <0 = unavailable. Caller decides WHEN (spec §7)

}  // namespace mrui
```

- [ ] **Step 4: Implement the board TU**

```text
⛔⛔ THIS BLOCK IS SUPERSEDED — §B85 (QA, 2026-08-04). The code that stood here HAD THREE DEFECTS, TWO OF THEM HARD
BUILD FAILURES, so copying it could not work:
  1. it OMITTED the three `mr_ui_*` hooks that `fw_main` calls unconditionally      -> LINK failure
  2. it read `MR_UI_BTN_PIN`, which Task 6 defines                                  -> COMPILE failure
  3. NOTHING called `board_init()`, so Step 5's "the panel lights" was unreachable  -> and worse:
     ★ under `--gc-sections` the ENTIRE canvas links out, which would have made the flash measurement a
       VACUOUS ZERO on a GREEN build. That is the instructive one.
  4. it never initialised Vext, leaving the panel power rail FLOATING (§B90 — not B91, which is the dead-panel
     reporting item).

⇒ THE REFERENCE IMPLEMENTATION IS THE LANDED FILE: `variants/heltec_v3/board_ui.cpp`. It is in the repo, it is
  gated, and it is covered by `tools/probe_board_ui/run.sh` (34 checks + 7 negative controls). Read it there.
  ⓘ Deliberately NOT re-pasted here: this plan has been corrupted twice by lifting code BY LINE RANGE (a clipped
  closing brace, a clipped anchor table). One authoritative copy, referenced — not two that can drift.

WHAT THE LANDED FILE DOES THAT THIS BLOCK DID NOT:
  • defines the three `mr_ui_*` hooks, marked TEMPORARY — ★ Task 6 MUST delete/move them when `firmware_ui.cpp`
    takes ownership, or the link breaks on duplicate symbols. That hand-off is Task 6's first obligation.
  • `mr_ui_init()` paints one frame through the REAL page loop, so `board_init()` runs and the canvas survives
    `--gc-sections`.
  • drives Vext to the MeshCore-proven LOW. ⚠ Whether the panel is actually on that rail is NOT established —
    bench question 8.2, ahead of the reset pin.
  • `MR_UI_BTN_PIN` is supplied by `[env:heltec_v3]` (`platformio.ini:227`), not by this task's source.
```

⛔ **§B85 CORRECTION: the `mr_ui_*` hooks ARE defined here, TEMPORARILY.** Without them `fw_main`'s unconditional
calls do not link, so Task 5 could not build at all. ★★ **Task 6's FIRST obligation is to delete/move them** as
`firmware_ui.cpp` takes ownership — leaving both is a duplicate-symbol link failure. Historical wording: they live in `firmware_ui.cpp` (Task 6). `battery_init` / `battery_sample_mv` land in Task 9; until then provide `int32_t battery_sample_mv() { return -1; }` and an empty `battery_init()` so the TU links and the panel renders `--`.

- [ ] **Step 5: Build and flash; the panel lights** — ⚠ §B85: only reachable because `mr_ui_init()` now paints one
      frame through the real page loop. ⛔ **`-t upload` was NOT run in UI-5 (no board attached)**, so everything
      metal-only is UNVERIFIED and sits in bench-script **Part 8** with its expected panel content.

Run: `pio run -e heltec_v3 -t upload`. ⚠ **§B90 CORRECTION — the old advice here ("suspect the reset pin before the
driver") is superseded and now contradicts the bench sequence.** Vext (GPIO 36) was **never driven by this tree**, so
the panel rail was FLOATING; UI-5 now drives the MeshCore-proven LOW, but **whether the panel is on that rail is NOT
established.** ⇒ **Vext FIRST, reset second** — bench-script Part 8 question **8.2** ahead of the reset pin.

- [ ] **Step 6: Report ready — do NOT commit**

---

### Task 6: Feature layer — snapshot, render policy, tick

**Files:** Create `src/firmware_ui.cpp`; modify `platformio.ini` (`build_src_filter`, pins).

**Interfaces:** implements `mr_ui_init` / `mr_ui_tick` / `mr_ui_on_push`; owns `build_snapshot()`, `draw_frame()`, the battery cache and the push correlation.

- [ ] **Step 1: Add pins and constants**

```ini
  ; ✅ ALREADY LANDED IN UI-5 (`platformio.ini:227`) — do NOT add it again. Task 5 needed it to compile
  ; (`board_ui.cpp` reads it), which is why it moved forward from this task.
  ;   -DMR_UI_BTN_PIN=0          ; Heltec V3/V4 user button. Active LOW, INPUT_PULLUP.
                                ; NB GPIO0 is the ESP32-S3 boot strap: held across a reset it enters download mode.
  -DMR_UI_TEAM_CHANNEL_ID=0     ; owner ruling: build constant, no cfg key / NV field / console verb
  -DMR_UI_ADC_CTRL=37
  -DMR_UI_VBAT_READ=1
```

and `+<firmware_ui.cpp>` in `build_src_filter`.

- [ ] **Step 2: Write the snapshot builder and battery cache**

```cpp
// MeshRoute — src/firmware_ui.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The board-UI FEATURE layer (U3). Owns the model, the send tracker, ALL render policy, and the correlation of
// node-wide pushes into UI outcomes. Adds no new core API — every read is an accessor that already existed (spec §6).
#include "mr_features.h"
#if MR_FEAT_OLED
#include "firmware_ui_model.h"
#include "firmware_ui_send.h"
#include "board_ui.h"
#include "fw_context.h"
#include "firmware_commands.h"   // mrfw::dispatch
#include "dispatch_sink.h"

static mrui::UiModel    s_model;
static mrui::InputFsm   s_input;
static mrui::SendTracker s_tracker;
static uint32_t s_last_dm_ms = 0, s_last_ch_ms = 0;
static uint16_t s_unread_dm = 0, s_unread_ch = 0;
static int32_t  s_batt_mv = -1;
static uint32_t s_batt_next_ms = 0;

static bool mac_idle() {   // spec §5 rule 1 — the same predicate fw_main.cpp uses to decide it may sleep
    return !g_iradio.tx_busy() && g_hal.txq_depth() == 0;
}

// Battery: sampled at boot and every 30 s, only when the MAC is idle. An earlier draft sampled 8 ADC reads on EVERY
// service pass for a value that changes over minutes (spec §7).
// ★ The cadence gates on ATTEMPTED, not on SUCCEEDED. Gating on `s_batt_mv >= 0` meant a board whose reader returns
// the documented unavailable value was re-read on every idle pass, forever.
static bool     s_batt_attempted = false;
static void battery_maybe_sample(uint32_t now_ms) {
    if (!mac_idle()) return;
    if (s_batt_attempted && uint32_t(now_ms - s_batt_next_ms) >= (1u << 31)) return;   // wrap-safe "not due yet"
    const int32_t mv = mrui::battery_sample_mv();
    if (mv >= 0) s_batt_mv = mv;                    // keep the last GOOD value; an unavailable read never erases it
    s_batt_attempted = true;
    s_batt_next_ms = now_ms + 30000;
}
```

`build_snapshot(now_ms)` fills `UiSnapshot` from: `s_unread_dm/ch`; `s_last_dm_ms/ch` for ages; `g_node.rt_team_count()` into `team_total` and the first `kMaxTeamRows` of `rt_team_at(i)` into `team[]` with `team_shown`; each row's label resolved `team_key_of_id()` → `peer_name_find()` → `0x<hash>` → bare id, clamped to `kLabelCap`; `g_node.team_local_id()`; `g_node.config().team_id`; `s_batt_mv`. **No BLE field** — `mrble::connected()` is inert on ESP32 (`device_ble.h:47`), so V3 shows nothing rather than a permanently-false indicator.

- [ ] **Step 3: Write the render policy (in THIS file, not the board TU)**

`draw_frame(const UiState&, const UiSnapshot&)` draws the status bar (`DM<n> CH<n> T<shown>/<total> <volts>`), then dispatches on `compose` first, then `screen`, then the emergency overlay when `emergency() != idle`. All text formatting lives here; only `mrui::draw_text` / `set_font` / `draw_hline` cross the boundary. Emergency states use `Font::large`; everything else `Font::small`. Battery renders `3.9V` or `--`, never a percentage.

- [ ] **Step 4: Write the tick**

```cpp
// TWO trackers: an alarm must never queue behind a DM waiting on its e2e ack (spec §2.1).
static mrui::SendTracker s_tracker_emg, s_tracker_normal;
static mrui::UiState     s_frame_state{};    // frozen at frame start — a frame spans ticks; live state would tear it
static mrui::UiSnapshot  s_frame_snap{};

void mr_ui_tick(uint32_t now_ms) {
    // ★★ §B79: BOTH trackers must be TICKED or an `awaiting` slot is never closed — the alarm sits on
    // `SENDING...` for ever and the send slot leaks permanently. `tick()` is also `channel_remote_mint`'s ONLY
    // producer (B39's producer (3) emits no channel-level push at all). Do this BEFORE the paint decision.
    { mrui::SendOutcome o{};
      // ★★★ §B84 BLOCKER 1 — `on_send_accepted` MUST COME FIRST, and this wiring omitted it. `_tries` moves ONLY
      //   there (`firmware_ui_model.h:223`), so without it an expiry re-queues with `attempts() == 0` and the alarm
      //   retries FOR EVER, never reaching `:278`'s `_tries >= kEmgMaxTries`. The ordering is safety-critical.
      if (s_tracker_emg.tick(now_ms, o)) {
          s_model.on_send_accepted(mrui::SendKind::emergency, now_ms);   // consume ONE bounded attempt
          s_model.on_outcome(o, now_ms);
      }
      // ★★★ §B84 BLOCKER 2 — the NORMAL tracker's expiry must NEVER reach the emergency model. `tick()` yields a
      //   CHANNEL kind, and `on_outcome` lets a channel outcome move any LIVE alarm (`firmware_ui_model.h:~253`), so a
      //   CANNED send's expiry could alter a live emergency. Draining the slot is `tick()`'s real job here (it is the
      //   leak fix); routing its outcome into the emergency-capable entry point is what was unsafe.
      // ✖ MISSING, stated so it is not mistaken for done: the canned sub-view's own presentation update. There is
      //   NO canned-only entry point today — `on_outcome` is the only one — so **Task 7 owns adding it**. Until then
      //   the expiry is consumed and NOT routed. Do not "fix" this by calling `on_outcome`.
      (void)s_tracker_normal.tick(now_ms, o); }
    static uint32_t s_last_paint_ms = 0;
    static bool     s_frame_open = false;
    battery_maybe_sample(now_ms);
    const mrui::UiSnapshot s = build_snapshot(now_ms);
    s_model.on_gesture(s_input.update(mrui::button_pressed(), now_ms), s);
    s_model.on_tick(s);
    if (s_model.state().screen == mrui::Screen::inbox) { s_unread_dm = 0; s_unread_ch = 0; }

    // The emergency slot is checked FIRST and is not gated on the normal slot. If a canned channel post is still
    // outstanding when the alarm fires, ABANDON its UI tracking and take the channel — its late ctr will not match.
    mrui::SendReq req{};
    if (s_model.emergency_pending()) {
        if (!s_tracker_normal.idle() && s_tracker_normal.kind() != mrui::SendKind::dm) s_tracker_normal.close();
        if (s_model.take_send_request(req)) ui_perform_send(req, now_ms);
    } else if (s_tracker_normal.idle() && s_model.take_send_request(req)) {
        ui_perform_send(req, now_ms);
    }

    if (!mac_idle()) return;                                  // never start OR continue a paint mid-exchange
    // ★ U8g2 page mode redraws the WHOLE scene per page — the draw calls are clipped, not accumulated. Drawing once
    // at frame start and only advancing pages (an earlier draft) leaves 7 of 8 pages blank.
    if (s_frame_open) {
        draw_frame(s_frame_state, s_frame_snap);              // the FROZEN copies, so the image cannot tear
        s_frame_open = mrui::next_page();
        if (!s_frame_open) s_model.clear_dirty();             // dirty clears only when the LAST page has gone out
        return;
    }
    if (s_model.state().blanked) { mrui::set_power_save(true); s_model.clear_dirty(); return; }
    mrui::set_power_save(false);
    const bool emg = s_model.emergency() != mrui::Emergency::idle;
    if (s_model.state().dirty && (emg || uint32_t(now_ms - s_last_paint_ms) >= 500)) {
        s_frame_state = s_model.state(); s_frame_snap = s;    // freeze
        mrui::begin_frame(); draw_frame(s_frame_state, s_frame_snap);
        s_frame_open = mrui::next_page(); s_last_paint_ms = now_ms;
    }
}
void mr_ui_init() { mrui::board_init(); }
```

Emergency bypasses the 2 Hz throttle but **not** the MAC-idle gate, and the model marks itself dirty only when the countdown digit changes (Task 3), so this does not repaint at tick rate. One page per eligible tick keeps the bus held ~3 ms at a time.

- [ ] **Step 5: Write the push correlation**

```cpp
void mr_ui_on_push(const meshroute::Push& pu) {
    using PK = meshroute::PushKind;
    mrui::SendOutcome o{};
    const uint32_t now = uint32_t(g_hal.now());
    switch (pu.kind) {
        case PK::msg_recv:     s_last_dm_ms = now; if (s_unread_dm < 999) ++s_unread_dm; break;
        case PK::channel_recv:
            s_last_ch_ms = now; if (s_unread_ch < 999) ++s_unread_ch;
            if (pu.channel_id == MR_UI_TEAM_CHANNEL_ID)                      // spec §4.4: ONLY our channel qualifies
                s_model.on_reply(label_for_origin(pu), reinterpret_cast<const char*>(pu.body), now);
            break;
        // Each outcome is offered to the EMERGENCY tracker first, then the normal one. Whichever matches wins; an
        // unmatched push is ignored, which is the whole point of the tracker.
        case PK::channel_sent:
            if      (s_tracker_emg.match_channel_sent(pu.ctr, pu.relayed, o))    s_model.on_outcome(o, now);
            else if (s_tracker_normal.match_channel_sent(pu.ctr, pu.relayed, o)) { /* canned: no model state */ }
            break;
        case PK::send_blocked:
            if      (s_tracker_emg.match_blocked(pu.blocked_channel, pu.next_ms, now, o))    s_model.on_outcome(o, now);
            else if (s_tracker_normal.match_blocked(pu.blocked_channel, pu.next_ms, now, o)) { }
            break;
        case PK::send_e2e_acked:
            if (s_tracker_normal.match_dm(pu.ctr, pu.dst, /*acked=*/true, FailReason::none, o))
                s_model.on_outcome(o, now);
            break;
        case PK::send_failed:
            // §B84: the emergency arm is GONE. A pre-enqueue channel failure arrives SYNCHRONOUSLY
            // (`exec_command` -> `refuse_reason_of` -> `on_send_refused`), and an async `send_failed` that only
            // `match_dm` can attribute must NOT be allowed to end an alarm — six non-channel operations emit
            // `dst == 0`. Unattributed ⇒ ignored ⇒ the bounded retry reaches NOT HEARD. Fails safe.
            if (s_tracker_normal.match_dm(pu.ctr, pu.dst, false, pu.reason, o)) s_model.on_outcome(o, now);
            break;
        default: break;
    }
}
```

★★ **REQUIRED INTEGRATION REGRESSIONS (§B84/§B85) — the unit tests cannot reach these; they span tracker + model:**
1. the colliding `send_layer` shape `{dst = 0, reason = unsealable}` **cannot** produce `Emergency::failed`;
2. **each `ctr == 0` expiry consumes exactly ONE attempt** (`attempts()` goes 1, 2, 3 — not 0);
3. **three expiries terminate in sticky `NOT HEARD`**;
4. **no fourth emergency request is queued** after that.

★ Every branch that can move the emergency goes through a tracker. ⚠⚠ **CORRECTED §B84 (2026-08-04): this line used to say
*"`send_failed` must reach the emergency path too (`match_channel_failed`)"* — that is now WRONG and the matcher is
DELETED.** The concern it names is real (a seal failure must not leave the alarm on `SENDING...` with no terminal state,
which is how the first draft lost it) but the cure was unsound: `dst == 0` does not mean "channel", so an unrelated
console operation could end a live alarm. ⇒ **the terminal state now arrives SYNCHRONOUSLY** (`exec_command` ->
`refuse_reason_of` -> `on_send_refused` -> `Emergency::failed`), and the stuck-`SENDING...` case is closed by
**`tick()`** (§B79) rather than by correlating a push. An async `send_failed` the tracker cannot attribute is ignored.

⚠ Verify the exact `Push` field names against `lib/core/command.h` before writing (V2). `relayed` and `next_ms` are confirmed at `:188`/`:203`; `blocked_channel`, `ctr`, `dst`, `channel_id`, `reason` and the `SendFailReason` enumerator spellings (`none`, `no_pubkey`, `e2e_ack_timeout`, `unsealable`) must each be re-checked.

- [ ] **Step 6: Build, flash, verify the cycle**

Short presses walk TEAM then move on; counts and battery render; the panel sleeps after 15 s and the waking press does not change screen.

- [ ] **Step 7: Report ready — do NOT commit**

---

### Task 7: Sends, compose sub-views, inbox adapter

⛔ **Gated on B39** (and B38/B40 for the emergency half) — see Prerequisites.

**Files:**
- Modify: `src/firmware_ui.cpp`
- Modify: `src/firmware_commands.h`, `src/firmware_commands.cpp` (add `mrfw::exec_command` — Step 1)

- [ ] **Step 1: Implement `ui_perform_send` with a typed result**

```cpp
static const char* const kDmTexts[]      = { "Are you OK?", "I'm OK" };
static const char* const kChannelTexts[] = { "Got your message", "All good" };

static void ui_perform_send(const mrui::SendReq& req, uint32_t now_ms) {
    char line[96]; int n = 0;
    if (req.kind == mrui::SendKind::dm) {
        if (req.text_index >= 2) return;
        // §3.4 cleartext DM by team_local_id. -t = TEAM plane, -a = end-to-end ack (the confirmation a channel post
        // can never give). NO -e: the parser gates it allow_e=by_hash and rejects it on an id target; crypt stays
        // `def` = the node's e2e_dm. We do NOT force plaintext — CryptIntent::off was deliberately removed.
        n = snprintf(line, sizeof line, "send %u \"%s\" -t -a", unsigned(req.peer_id), kDmTexts[req.text_index]);
        s_tracker.submit(mrui::SendKind::dm, req.peer_id, 0, now_ms);
    } else {
        const bool emergency = (req.kind == mrui::SendKind::emergency);
        const char* body = emergency ? "I'm in danger"
                                     : (req.text_index < 2 ? kChannelTexts[req.text_index] : nullptr);
        if (!body) return;
        // ★★ §4.1: -l is CONDITIONAL. on_command REFUSES `-t -l` with no_fix when lat_e7==0 && lon_e7==0
        // (node.cpp:1526). Sending it unconditionally would turn "no fix" into NO ALARM AT ALL.
        const meshroute::NodeConfig& cfg = g_node.config();
        const bool have_fix = emergency && (cfg.lat_e7 != 0 || cfg.lon_e7 != 0);
        n = have_fix
            ? snprintf(line, sizeof line, "send_channel %u \"%s\" -t -l -e", unsigned(MR_UI_TEAM_CHANNEL_ID), body)
            : snprintf(line, sizeof line, "send_channel %u \"%s\" -t -e",    unsigned(MR_UI_TEAM_CHANNEL_ID), body);
        s_tracker.submit(req.kind, 0, MR_UI_TEAM_CHANNEL_ID, now_ms);
    }
    if (n <= 0 || size_t(n) >= sizeof line) { s_tracker.refuse(); s_model.on_send_refused(req.kind, mrui::RefuseReason::other, now_ms); return; }   // §B78: now_ms is REQUIRED — a gesture-anchored deadline is already spent by the time a refusal lands

    // ★ The synchronous result must reach the model TYPED — never a discarded BufferSink, or a parser refusal leaves
    // the panel on SENDING... forever (spec §2.1).
    mrui::SendTracker& tr = (req.kind == mrui::SendKind::emergency) ? s_tracker_emg : s_tracker_normal;
    const mrfw::ExecResult r = mrfw::exec_command(line, size_t(n));
    if (!r.ok || r.result.code != meshroute::CmdCode::queued) {
        tr.refuse(); s_model.on_send_refused(req.kind, refuse_reason_of(r), now_ms); return;   // §B78: now_ms is REQUIRED — a gesture-anchored deadline is already spent by the time a refusal lands
    }
    // ★ B39, CORRECTED by §B84: `queued` is NOT proof of transmission — but ctr==0 is NOT proof of failure either. It
    //   means no local handle exists and the status is UNKNOWN. A blocked or seal-failed channel post returns queued with ctr==0
    // (node_channel.cpp `return 0` -> node.cpp wraps it). next_ctr never yields 0, so zero is the sentinel — but §B84:
    // it marks NO LOCAL HANDLE / status UNKNOWN, not "nothing was sent". We do NOT wait for a matching push (it may
    // never come, or be unattributable); bounded expiry closes the slot and CONSUMES one attempt.
    if (r.result.ctr == 0) { tr.awaiting_outcome(now_ms); return; }
    tr.accept(r.result.ctr, now_ms);
    s_model.on_send_accepted(req.kind, now_ms);
}
```

**★ Add `mrfw::exec_command` — approved 2026-08-01 as the one firmware modification in this plan.**

⚠ **First, a correction to an earlier draft of this plan: `dispatch()` does NOT handle `send` / `send_channel`.** It is a *verb router* for console-only verbs (`cfg`, `routes`, `team`, `pull_inbox`, …) and returns `false` for anything else — the send path lives in its **callers**: `service_console` runs dispatch-first-then-`parse_command` (`fw_main.cpp:792`), and `ble_dispatch_line` runs `parse_command`-first-then-dispatch (`fw_main.cpp:484-514`). Calling `dispatch()` with a `send_channel` line would have returned `false` and sent nothing.

So the helper is **not** a wrapper around `dispatch()`. Add to `src/firmware_commands.{h,cpp}`:

```cpp
// firmware_commands.h — ADD THIS INCLUDE: the header names meshroute::console::ParseErr in a public struct, so it
// must include the declaration directly rather than relying on an unrelated transitive header.
#include "console_parse.h"

// inside namespace mrfw
struct ExecResult {
    bool                     ok = false;   // false => the line did not parse as a Command
    meshroute::console::ParseErr parse_err = meshroute::console::ParseErr::ok;
    meshroute::CmdResult     result{};     // valid only when ok
};
// Parse ONE command line and execute it on the node, returning the TYPED result. No text output: the caller wants
// the CmdResult, not a human string. This is the send path `service_console` and `ble_dispatch_line` each open-code;
// the board UI is the third consumer and the first that needs the result programmatically.
ExecResult exec_command(const char* line, size_t len);
```

```cpp
// firmware_commands.cpp
ExecResult exec_command(const char* line, size_t len) {
    ExecResult r{};
    meshroute::Command cmd{};
    r.parse_err = meshroute::console::parse_command(line, len, cmd);
    if (r.parse_err != meshroute::console::ParseErr::ok) return r;
    r.ok = true; r.result = g_node.on_command(cmd);
    return r;
}
```

**Deliberately NOT retrofitting the two existing call sites in this plan.** They use *opposite* orderings relative to `dispatch()`, so unifying them is a behaviour change on two working transports and needs its own slice and gate (C1: refactor XOR feature). `exec_command` is additive: nothing that works today changes. Note the retrofit as a follow-up cleanup, do not perform it here.

`refuse_reason_of(const ExecResult&)` maps a non-`ok` parse → `RefuseReason::parser`, `err_ack_ring_full` → `RefuseReason::ack_ring_full`, and **everything else → `RefuseReason::refused`** with the `CmdCode` carried alongside for the panel text.

⚠ **Do not map `err_no_binding`/`err_unsupported` to `unsealable`.** That was over-broad: `err_unsupported` covers both `unsealable` *and* `no_location`, and the two have opposite remedies (enable encryption vs set a position). `CmdCode` **cannot** distinguish them — only `SendFailReason` can, which is why B39's richer result matters. Until it lands, show the generic refusal and the code; do not invent a specific reason. `queue_full` is the *deferred-send* reason and is unrelated to the ack ring — do not conflate them.

Keep the mapping a small `switch` in `firmware_ui.cpp` so `-Wswitch` covers new `CmdCode` values.

- [ ] **Step 2: Implement the compose sub-view render** — header line (`to: <label>` / `to: team`), then the item list with a `>` marker on `cursor`, `back, don't send` last. Outcome states replace the list: `SENDING...`, `SENT, waiting`, `DELIVERED to <label>`, `NO KEY`, `NO CONFIRM`, or the refusal reason.

- [ ] **Step 3: Implement the inbox adapter over `Inbox::pull()`**

Call `g_node.inbox().pull(dm_since, chan_since, cb, ctx)` directly — **never** dispatch textual `pull_inbox` into the 512 B `BufferSink`. Prefix each row `DM` or `CH<n>`; clamp body text to 20 chars.

⚠ **Allocate the row budget PER KIND — 4 DM + 4 channel, not one shared pool of 8.** `pull()` visits the DM block first and the channel block second, so a shared "keep the newest 8" lets a chatty channel evict every DM row on a screen whose entire purpose is showing both. `pull()` returns the DM block oldest-first then the channel block oldest-first (`inbox.h:107-109`) — render in that block order and do **not** imply chronological interleaving. Viewing does **not** advance the durable `mark_read` cursor.

- [ ] **Step 4: Build, flash, verify all four paths**

1. **Channel** — SEND → `double` → `double` on "Got your message": the second node receives it.
2. **`back`** — open either sub-view, walk to the last item, `double`: closes, **nothing transmitted** (confirm no dispatched line on the console).
3. **DM** — TEAM → walk to a teammate → `double` → `double` on "Are you OK?": the second node receives it and the sender shows `DELIVERED`.
4. **Inbox** — rows appear labelled `DM`/`CH`, bounded and readable.

- [ ] **Step 5: Report ready — do NOT commit**

---

### Task 8: Emergency end-to-end on hardware

⛔ **Gated on B38 / B39 / B40.** Cases 3, 4 and 8 below cannot pass against the current core contract: with B38 open, a team post can never report `relayed=true`, so `PICKED UP` is unreachable and the alarm always ends `NOT HEARD`. **Do not "adjust" the expectations to match** — that would bake the bug into the acceptance criteria.

**Files:** Modify `src/firmware_ui.cpp` (render only — the machine is built and tested in Task 3).

- [ ] **Step 1: Render the emergency states** (`Font::large`)

`arming` → `RELEASE TO CANCEL` + `arming_secs_left()`; `firing` → `SENDING...`; `blocked` → `BLOCKED` + `retry in Ns` from `retry_at_ms()`; `picked_up` → **`PICKED UP`** (never `DELIVERED` — it means a neighbour re-flooded it); `not_heard` → `NOT HEARD` + `hold=retry`; `reply` → `REPLY <who>` + clamped text; `cancelled` → `CANCELLED`; `failed` → the refusal reason.

- [ ] **Step 2: Bench matrix (two nodes, same team)** — add each line to `docs/2026-07-31-bench-test-script.md` per M2

1. Long-press from every screen **and from inside both compose sub-views** reaches SENDING.
2. Release at ~3.0 s cancels and auto-returns after ~1 s; release past 3.5 s fires.
3. Second node off → `NOT HEARD` after exactly **3 accepted transmissions**, then sticky.
4. Second node on → `PICKED UP`; a reply on channel 0 → `REPLY`.
5. Fire twice inside 10 s → `BLOCKED` with a live countdown, then auto-fires (`channel_min_interval_ms` 10000).
6. Emergency on a blanked panel works and wakes it.
7. **Conditional `-l`, both halves:** with `cfg set lat/lon` set → the receiver's record carries a position. With `lat 0` / `lon 0` → **the alarm still goes out**, positionless, with no `no_location` refusal. If this fails, the conditional is inverted and the feature is broken where it matters.
8. **Attribution:** while the emergency is `firing`, post a canned channel message from the *console* of the same node — the emergency state must **not** move.
9. **Blanked panel produces no repeated I²C** — confirm by trace counter or scope.
10. **Emergency pre-emption:** send a `-a` DM, and *while it is still awaiting its ack*, long-press. The alarm must reach `Node::on_command` in the **same service pass** — not after the DM's ack or deadline. Repeat with an outstanding canned channel post, and confirm the abandoned post's late outcome does not move the emergency.
11. **Full-frame integrity:** confirm the whole scene renders (not just the top eighth) and that one frame is eight page transfers.
12. **Hold deadline:** on `PICKED UP`, the panel stays on for **`kEmgHoldMs`** (owner-re-ruled 2026-08-04 → **30000**), not `MR_UI_BLANK_MS`; a reply arriving late restarts the window. ★ Read both from the constants — this line said "~120 s" and went stale.
13. **Battery cadence:** with the reader forced to return `<0`, confirm sampling is attempted every ~30 s, **not** every pass.

- [ ] **Step 3: Report ready — do NOT commit**

---

### Task 9: V3 battery reader

**Files:** Modify `variants/heltec_v3/board_ui.cpp`.

Spec §7 is the authority. **V3 polarity is auto-detected** (boards past rev 3.2 inverted it) — do not hardcode LOW. V3 has **no** settling delay; do not import V4's `delay(10)`.

- [ ] **Step 1: Implement**

```cpp
static bool s_adc_active_high = false;

static void battery_init() {
    pinMode(MR_UI_ADC_CTRL, INPUT);
    s_adc_active_high = (digitalRead(MR_UI_ADC_CTRL) == LOW);      // probe idle, invert: MeshCore HeltecV3Board::begin()
    pinMode(MR_UI_ADC_CTRL, OUTPUT);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);  // park inactive
}

namespace mrui {
int32_t battery_sample_mv() {
    analogReadResolution(10);
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? HIGH : LOW);
    uint32_t raw = 0;
    for (int i = 0; i < 8; ++i) raw += analogRead(MR_UI_VBAT_READ);
    raw /= 8;
    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);
    return int32_t(5.42f * (3.3f / 1024.0f) * float(raw) * 1000.0f);
}
}
```

- [ ] **Step 2: Verify against a multimeter** — within ~50 mV. A consistent ratio error means this revision's divider differs; record the measured value, do not tune to taste. Add the check to the bench script (M2).

- [ ] **Step 3: Confirm the cadence** — instrument or trace that sampling happens ~every 30 s and never while the MAC is busy.

- [ ] **Step 4: Report ready — do NOT commit**

---

## Final gate

- [ ] `pio test -e native` then **run** `./.pio/build/native/program` — real count printed, 0 failed
- [ ] s18 md5 **exact** vs the current `simulation/BASELINE.md` keystone
- [ ] The mandatory mobile/team scenarios per `BASELINE.md` §2 — 0 assertion failures
- [ ] `pio run` for `gateway`, `xiao_sx1262`, `xiao_esp32s3`, `heltec_v3`, `heltec_mobile` — green, no new warnings
- [ ] Flash/RAM delta recorded for the Heltec envs vs the Task 5 Step 2 baseline
- [ ] Bench-only checks added to `docs/2026-07-31-bench-test-script.md` (M2)
- [ ] Report ready with the numbers. **The owner commits.**

## Open items this plan does not decide

- **Arm duration 3.5 s** — a bench opinion. Tune `InputCfg::fire_ms` (and the matching `kArmToFireMs`) after Task 8.
- **V3 panel reset pin** — 21 per `board_ui.cpp:14`; MeshCore's V3 variant defines none. Confirm during Task 5 Step 5.
- **Retrofitting `service_console` and `ble_dispatch_line` onto `mrfw::exec_command`** — a genuine dedup (both open-code the same parse + `on_command` sequence), but they use opposite orderings relative to `dispatch()`, so it is a behaviour change on two working transports. Its own slice, its own gate; explicitly out of this plan.
