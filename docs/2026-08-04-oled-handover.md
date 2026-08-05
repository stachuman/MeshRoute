<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# HANDOVER — Heltec V3 OLED UI + related bugs · 2026-08-04

*Written at 98 % context. ★ My role was **QA-gate**: coding agents implement, an independent QA agent reviews, the owner
commits and rules. **D4 stands: never commit.*** Supersedes nothing — `docs/2026-08-01-agent-handover.md` covers the
earlier id→hash arc and is still the map for that.

---

# ★★ OWNER RULINGS 2026-08-05 — BOTH OPEN UI-6 DECISIONS ARE NOW CLOSED. IMPLEMENT AS WRITTEN.

> ✅ **BOTH IMPLEMENTED, GATED AND UNCOMMITTED 2026-08-05** — register **B109** (R1) / **B110** (R2); evidence in the
> `simulation/BASELINE.md` §UI-6-RULINGS note (top). Native **1321 / 73703 / 0**; RED measured per ruling *and* against
> the two wrong fixes the rulings warned about. ⚠ **One finding worth the owner's eye:** R1's negative control was
> **vacuous on its first writing** and only a mutation caught it — see the note. ⚠ **One spec gap reported, not ruled:**
> the wake is REPLY-only; `blocked`/`picked_up`/`not_heard`/`failed` still arrive at a dark panel.

**R1 — A REPLY WAKES A BLANKED PANEL.** Today nothing un-blanks on an incoming push, so a distress REPLY arriving at a
dark panel waits for a button press — found while disproving my "the blanked branch has the same defect" premise.
⇒ **an incoming reply must un-blank.** ⚠ Blanking stays **EDGE-triggered** (spec §5): un-blank is a **transition**,
`set_power_save(false)` once, **never a per-tick write**, and it must not become a wake-on-any-push (the reply
predicate is the same team-scoped one F4 landed — a stranger's channel-0 post must **not** light the panel).

**R2 — A DOUBLE PRESS UNDER THE EMERGENCY OVERLAY IS IGNORED ENTIRELY.** Owner ruled against both alternatives.
The overlay **absorbs** it: **no** emergency action (consistent with B71's *"double gets no emergency job"*), **no**
operation of the screen underneath, **no** dismiss and **no** re-fire. ⇒ closes QG's hidden-mis-send hazard, where
**two doubles could open and activate a compose view the user cannot see, during an alarm.**
★ The resulting gesture contract under the overlay, complete: **short** = the ruled exit *once the result has been seen*
(B71 + F3's presented-latch) · **long** = re-fire · **double** = nothing.
⚠ **Do not implement R2 as "consumed" in F3's sense without checking the call path** — F3 already consumes a *premature
short* press; R2 is a **different arm** and must not be folded into it by reusing the same flag.

---

# ⛔⛔ ADDENDUM 2026-08-05 — READ THIS FIRST. TASK 6 LANDED AND IS **NOT APPROVED**.

★ **Everything in §1 below is superseded by this block for the numbers, and by nothing for the method.** UI-6 (plan
Task 6) is implemented, builds green on every gate, and was **REJECTED by independent QA** for five safety-relevant
**feature-layer lifecycle** defects that the structural probe is blind to by construction. **Three coder dispatches to
fix them died to server `API 529 Overloaded` with ZERO work done** (tree verified pristine after each: 20 modified /
6 untracked, all three defect sites in original form, no `ui6fix-` scratch). **Owner ruled 2026-08-05: retry the coder
later** rather than have QA drift into the coding role (P3).

⚠⚠ **LIVE SAFETY DEFECT ON BENCH HARDWARE — F4 below.** Until it is fixed, a distress **REPLY / PICKED UP** shown on
the panel is **not trustworthy**: any node in radio range posting **plaintext on channel 0** — no team membership, no
key, no crypto — renders as *"someone answered my distress call."* Do not trust a reply indication during H6 bench work.

## Current gate numbers (all independently re-measured by QA 2026-08-05, not merely reported)

| | |
|---|---|
| HEAD | **`45c9cc1`** — nothing committed; **20 modified / 6 untracked** |
| native | **1285 / 73359 / 0** (was 1268/73247), 60 test objects — ★ QA re-ran and reproduced EXACTLY |
| s18 keystone | **`1cd21235` / 271629 EXACT**, corpus **36/36** — ★ **0 files changed under `lib/`**, so this is inert **by construction** (D2), which is why QA accepted the coder's measurement rather than re-running it |
| OLED census | **180 / 180 / 176 at 326 objects**, `-Wswitch` 0 — **RE-PINNED from 178/178/174 (§B106)**, QA-confirmed independently |
| RAM | **+760 B identical on all three**: `heltec_v3` 214068 · `heltec_mobile` 213588 · **`gateway_heltec` 238988 = 72.9 %** (tightest OLED env) |
| board probe | 38/38 + 8/8 structural, 8/8 controls red |

★ **§B106 — why the warning pins rose, verified structurally, not taken on faith:** `src/firmware_ui.cpp` is **+1 TU**,
and it must reach `g_node`/`g_hal`/`g_iradio`, so it includes `fw_context.h` → the radio HAL, pulling **RadioLib's
`#warning`** (`-Wcpp`) and **`device_radio.h`'s `inline volatile` globals** (`-Wvolatile`), **once per including TU**.
⇒ **+2 per env is +1 TU, not new warning-generating code**; UI-6's own 551 lines add zero.
⚠ **The census's `PASS` verdict is SELF-REFERENTIAL** now that the coder owns `EXPECT_WARN` — it compares the build
against the coder's own expectation and **cannot contradict a wrong pin**. The independent evidence is the raw counts,
the object count, the uniform +760, and the include chain. **Read a green verdict as non-vacuity, never as assent.**

## ★★ THE FIVE QA FINDINGS — every `file:line` and quoted line VERIFIED IN SOURCE by QA. Diagnosis is established; the fix shape is a strong proposal.

⚠ **Dispatch them in this FAIL-SAFE ORDER** (adopted after the three 529s): **F4 → F1 → F2 → F3/F5**, landing each with
its test and gating before the next, so an interruption leaves a coherent gated subset rather than a half-applied slice.

**F4 — SAFETY, false distress confirmation. DO FIRST: smallest diff, highest value.** `src/firmware_ui.cpp:~533` reads
`if (pu.channel_id == MR_UI_TEAM_CHANNEL_ID) { … s_model.on_reply(…) }` — **no team scope at all.**
★★ QA traced the ingest gate and **the hole is WIDER than QA's original "unrelated team" framing**:
`lib/core/node_channel.cpp:211-212` **drops** a foreign team's M (`m.team_id != 0 && _cfg.team_id != m.team_id`), so a
foreign *team* post never arrives — but its own comment says **"a normal leaf M (`team_id==0`) falls through → ingested
by everyone."** With `MR_UI_TEAM_CHANNEL_ID=0` that is the §2.1 false-confirmation class reached by a passer-by.
⇒ **The clause carrying the safety weight is `team_id != 0`, NOT the equality** (the equality is what ingest already
guarantees). **Say so in the comment** or a later reader deletes it as redundant.
★ **Use the existing helper (U1) — it collapses the predicate to ONE call.** `lib/core/node.h:274`:
`bool same_team(uint32_t their_team) const { return _cfg.team_id != 0 && their_team == _cfg.team_id; }`
⇒ `if (pu.channel_id == MR_UI_TEAM_CHANNEL_ID && g_node.same_team(pu.team_id))`
**`same_team` already implies `pu.team_id != 0`**, so QA's three-clause form is provably equivalent to this two-clause
one — **the coder must verify that equivalence, not trust it.** `Push::team_id` exists (`lib/core/command.h`, §S4) and
`node_channel.cpp:413` sets `pu.team_id = m.team_id`. ⛔ **No `lib/` edit needed or permitted.**

**F1 — HIGHEST severity: a newer UI state is permanently LOST during a paged frame.** `src/firmware_ui.cpp:~503`:
`if (!s_frame_open) s_model.clear_dirty();  // dirty clears only when the LAST page has gone out`. While a frozen frame
is mid-paging, an outcome or gesture sets `dirty=true`; completing the **old** frame clears it unconditionally, so
**PICKED UP / REPLY / FAILED may never be painted.** ⇒ **consume `dirty` when FREEZING/starting a frame**, not on final
page; final-page completion does **presentation bookkeeping only** (`s_last_paint_ms`, latches).
⚠ **The blanked branch (`~:492`) has the SAME defect** — it clears `dirty` while dark, so an invalidation raised while
blanked is discarded and the panel can **wake showing stale state**. **Fix both; they are one bug.**

**F2 — unread handling discards UNSEEN messages.** ① `mr_ui_on_push` (`~:523`) increments `s_unread_dm`/`s_unread_ch`
and **never requests a repaint**. ② `~:468` `if (s_model.state().screen == mrui::Screen::inbox) { s_unread_dm = 0;
s_unread_ch = 0; }` runs **every tick**, ahead of the blanked check and before any page reaches the panel — clearing
while **blanked, MAC-busy, emergency-covered, or with zero Inbox pages painted.** ⇒ invalidate on arrival; clear only
after a **complete, actually visible** Inbox frame; and **subtract only the counts FROZEN into that frame** — that last
part is what makes the fix correct, because a bare `= 0` after the frame still loses a mid-frame arrival.

**F3 — B102 must be FIXED, not merely recorded.** A queued short gesture can acknowledge an emergency result **before
its first complete frame**. Add an explicit **"retained outcome presented" latch**, set only after the matching
**frozen emergency** frame completes; a premature short press is **consumed** but must **not** dismiss nor operate the
screen beneath. ⚠ **Couples to B71** (owner-ruled exit): the latch is what makes *"its result seen"* **true rather than
assumed**, so **B71's exit must consult it.**

**F5 — B101:** when `long_fire` commits an emergency, **close the compose modal and reset its cursor** — a live canned
message under the alarm is an avoidable later mis-send. (QA called it a recommendation; treat as in scope.)

## ★★ WHY THE GREEN GATE MISSED ALL FIVE — the durable lesson, and it is an argument for B105
These are **lifecycle-ordering** bugs: the harm is *a frame that never happens* or *a count that vanishes*. **A
post-hoc enum assertion passes against every one of them.** That is exactly **B97** (four of UI-6's "required
integration regressions" were structurally **incapable of failing** — they hand-replicated the wiring they guarded) and
**B98** (a UI-6 test stayed **green against the very defect it names**, because the harm was a phantom queued alarm, not
a changed state). ⇒ **every fix needs its test written FIRST, the fix reverted, and the test MEASURED RED**, asserting
**paint/clear sequences and frozen-vs-live counts** — never end state.
⇒ **B105 is the structural cure and is OWNER-BLOCKED:** one `DeviceHal::radio()` accessor lets the feature layer
include only pure headers, which **removes both new warnings** *and* unlocks the `probe_firmware_ui` that **B104**
records as missing. Today render policy, the MAC-idle gate, the throttle and battery cadence have **no behavioural
probe at all** — which is why F1–F3 were reachable only by human review. It needs a `lib/hal` change ⇒ owner's call.

## Register state after QA's 2026-08-05 pass
**Closed:** B88 (all **nine** canvas entry points now called — QA derived the set digit-safely; an `[a-z_]+` return-type
pattern had silently missed `int32_t battery_sample_mv`, one of the three symbols the entry was about) · B90 (owner:
panel lit) · B91 (a **real** ACK probe, `Wire.endTransmission() == 0`, correctly ordered after `begin()` since U8g2 owns
`Wire.begin`) · B95 · B96 · B97 · B106.
★★ **B91 retroactively supplied the instrument B90 lacked** — flip `kVextOnLevel` to HIGH and read the boot line:
**probe goes quiet ⇒ the panel IS on the Vext rail; still ACKs ⇒ it is not.** Cheap curiosity, **not** a reason to
reopen B90.
**Open and owed to the coder:** F1/F2 need NEW ids (**B107/B108** — verify the next free id); **B101/B102/B103** close
with F5/F3/F4; **B69** is Task 7's. ⚠ **B98, B100–B104, B106 live only as §0 dispatch lines** (M1's contract surface) —
only B97/B99/B105 got `###` detail records; their measurements are in the `simulation/BASELINE.md` §UI-6 note.
**Owed by the OWNER:** **B105** (above) · **H5-02…H5-07** bench results · B95's two policy calls (`production` flash
**+7040 B**; `help` exceeding the stage) · **B100** (B71's ruled exit lists five states, only four exist — *"final
`blocked`"* is vacuous because the blocked arm always re-arms a retry).

---

## 1. State

| | |
|---|---|
| HEAD | **`45c9cc1`** |
| native | **1268 / 73247 / 0** — run `./.pio/build/native/program`; ⚠ `pio test` prints a false *"0 test cases"* |
| s18 keystone | **`1cd21235` / 271629**, 0 assertion failures |
| corpus | 36/36 |
| OLED warning gate | **`tools/warning_census.sh`** — pinned **178 / 178 / 174**, `-Wswitch` 0, exits non-zero on mismatch |
| board canvas probe | **`tools/probe_board_ui/run.sh`** — 34 checks + 7 negative controls, controls run by default |
| ★ hardware | **Heltec V3 BENCH-VERIFIED.** H5 group ran; the panel lit; radio checks passed |

**Uncommitted (a B95 coder is IN FLIGHT — do not disturb):** `src/console_sink.h` · `src/firmware_commands.{cpp,h}` ·
`src/fw_main.cpp` · `tools/probe_console_sink/` · the register · BASELINE · bench script · bench guide.
**Untracked, mine, NOT adopted:** `tools/wire_path.py` — see §5-B94.
⚠ Three stray `Untitled*` files in the repo root are **not ours**; leave them out of commits.

---

## 2. Records — read these, not this file, for detail

- **`docs/superpowers/specs/2026-07-31-onboard-oled-ui-design.md`** — the design. **§0** carries the board-source ruling.
- **`docs/superpowers/plans/2026-07-31-onboard-oled-ui-phase-a.md`** — ★ **the executable plan.** Its **Global
  Constraints** and **"Findings from Tasks 1–2 / UI-5 rulings"** blocks hold every ruling below. **Tasks 6–9 are there.**
- **`docs/2026-08-04-heltec-v3-oled-ui-bench-guide.md`** — the staged bench guide (H5 done · H6 after Task 6 · H7–H9 later).
- **`docs/2026-07-31-bench-test-script.md`** — the metal-only residue (M2). **Part 7** = id→hash, **Part 8** = OLED.
- **`docs/2026-07-30-open-bug-register.md`** — the index. **§0 is the dispatch contract; hand it over verbatim.**
- **`simulation/BASELINE.md`** — the evidence store, newest note at top. ⚠ **Anchor the anchor table on `^### `**, never
  on its heading text — that grep also matches the prose prescribing it and yields 1 row instead of 36 (**B77**).
- ⚠⚠ **`lus` PRINTS ITS ASSERTION-FAILURE LINE TO `stderr`, NOT stdout — MEASURED.** I told every agent "stdout" and one
  wasted a whole corpus sweep reporting *0/36 with 36 failures* while every md5 already matched. Use `2>&1`; and note a
  grep of the **ndjson** for `"ok":false` is vacuous either way — the count is never in the event file.

---

## 3. What landed

**A0** — the board seam moved to `variants/heltec_v3/board_ui.cpp`. ★ **`variants/` is the ruled home for per-board
sources**; `boards/` is PlatformIO's manifest tree; `platform/` is reserved for a real chip-family tier.
**Phase A Tasks 1–4** — gesture classifier · screen/cursor/compose model · emergency + DM machines · send tracker.
**All QA-APPROVED.** **Task 5** — the board canvas port (U8g2 `_1_` page mode, edge-triggered blanking, button, Vext).
**Bench-verified.** Plus **B30**, **B61**, and the id→hash arc (**B42–B48**, S1–S4b).

---

## 4. The queue

1. ✅ **B95 — DONE (uncommitted), and it REFUTED its own brief.** USB command responses fuse surviving `Print` fragments into false rows
   (`beacon_ms=900000168010102layer=5`). Brief: `docs/superpowers/plans/2026-08-04-console-response-line-integrity.md`
   — 9 invariants, 8 tests, negative controls, all specified. ★ **The trap: the naive fix is to flush/wait, which
   reintroduces the USB wedge this sink exists to prevent.** Complete line or zero bytes; count drops; deferred
   `!! CONSOLE_DROP lines=N`. ⚠ **Keep the 1700 B line scratch OFF the loop-task stack** — an overflow there previously
   produced a jump-to-0x0 brick, a USB wedge and `e2e-ack=0` from one root cause.
   ★★ **The refutation, measured, and it changes the guarantee:** the brief demanded *"a complete line **in one call**,
   or zero bytes"*. But `availableForWrite()` tops out at **128 B on ESP32-S3** and **256 B on nRF52**, while real rows
   are longer — `[cfg.layer0]` ~160 B, the `hashof` remedy ~392 B, 8 of 75 `help` lines >128 B. ⇒ one-call-per-line makes
   them **permanently undeliverable to a healthy idle host**: the probe measures strict-atomic delivering **2 of 8** `cfg`
   rows against the shipped sink's **8/8 byte-exact**.
   **Shipped guarantee instead:** *a committed line reaches the wire as a contiguous, gap-free, in-order run including
   its terminator, or not at all; a line is discarded only before its first byte.* ★ **The stage therefore had to live
   INSIDE `mrcon`, not in a wrapper on the command path** — with an in-order drain, any second `Serial` writer (an async
   push, a `!!` log, the banner) cuts into a half-drained line and recreates the fusion. Nothing waits/flushes/delays.
   `hl()` is **deleted** (it was the last direct-`Serial` writer); `print_sf_list` takes a sink; BLE `help` is refused.
   **Cost: RAM +2064 B** (the 2048-B stage, lever `MR_CONSOLE_STAGE_BYTES`) — `gateway` RAM **81.5 % → 82.4 %**;
   `MR_CONSOLE=0` is still **0 B**. Probe `tools/probe_console_sink/`: 52/52 + 11/11 structural, **13/13 controls red**.
   ⚠ **Two owner decisions deliberately NOT taken (C1):** (a) `production` flash **+7040 B** because `hl()`'s `(void)fs`
   used to let the linker GC all 6121 B of help text — `#if MR_CONSOLE`-gating `dump_help`'s body reclaims it; (b) `help`
   exceeds the stage ⇒ ~25 lines then `!! CONSOLE_DROP lines=N` (a 6400-B stage delivers all of it, +4.4 KB RAM).
   **Metal rerun owed** (bench script **Part 9**) — blocked by B96.

2. ⛔⛔ **B96 — THE LINUX BOARD GATE IS BROKEN AT HEAD. Fix this before Task 6.** `heltec_v3` / `heltec_mobile` /
   `gateway_heltec` **do not build on Linux at `45c9cc1`**: HEAD's own `lib_extra_dirs` points the LDF at the **old**
   framework package while the pinned platform uses `…@src-702d0f93…` ⇒ `SPI.cpp:121 too many arguments to
   'spiDetachSCK'` · `WiFi.h:29 IPv6Address.h: No such file`. ★ **Three controls: a PRISTINE WORKTREE at HEAD fails
   identically** (so it is not one machine's stale install, which is what I wrongly ruled in B94) · the B94 cache remedy
   does not help · removing **only** those two lines builds green and reproduces §UI-5's **211252 / 1244400** exactly.
   ⇒ **The fix is `env.PioPlatform().get_package_dir(...)` — i.e. `tools/wire_path.py`, already written and measured
   (untracked).** ⚠ It costs a **B87 re-pin** (RAM 211252→211236, Flash 1244400→1244452) and **must be verified on
   Windows**, where HEAD currently works and the bench ran. **Owner's slice.**
   ⚠ **This supersedes B94's "keep HEAD, it is an environment defect" ruling** — that was mine and the pristine-worktree
   control disproves it.

3. ★★ **Task 6 — the feature layer.** ⛔ **ITS FIRST OBLIGATION: delete the TEMPORARY `mr_ui_*` hooks from
   `variants/heltec_v3/board_ui.cpp`** as `firmware_ui.cpp` takes ownership — leaving both is a **duplicate-symbol link
   failure**. Task 5 needed them to link at all. **Also closes B88** (three canvas entry points are `--gc-sections`'d
   out today) and owes **B91**'s dead-panel report channel.
   ⚠ **`mr_ui_tick()` must call BOTH trackers' `tick()`**, and the emergency slot needs `on_send_accepted(...)`
   **before** `on_outcome` — see B84 below. The plan's wiring block already says so; I got it wrong twice.
4. **Task 7** — sends, compose sub-views, inbox adapter. Owes **B64** (⚠ a shrinking roster retargets a DM to row 0 —
   **a MIS-SEND, not a display glitch**), **B66** (`back` identified positionally; the count and the strings live apart),
   **B69** (`channel_remote_mint` shares a state with `channel_no_relay`, so "render as SENT" has no carrier).
5. **Task 8** (emergency on metal) · **Task 9** (V3 battery reader) · then **H6–H9** in the bench guide.
6. **Open, small:** **B90** (Vext — *probably closable*, the panel lit) · **B92** (spec fonts — spec corrected, verify) ·
   **B93** (`mr_ui.h` forward-declares `Push` in a **hardcoded namespace** — latent, breaks when `MESHROUTE_NS` moves).

---

## 5. Rulings — do not re-litigate

- ★★ **B71 (owner):** after the emergency is sent **and its result seen**, the **next short press** restores the normal
  cycle. Sticky until then; **`long` re-fires; `double` gets NO emergency job.** Safe because a waking press is consumed
  (spec `:378`), so the result is always displayed before any press can dismiss it. **UI-6 implements it.**
- ★★ **B78 (owner):** `Emergency::failed` **joins the retained set** and holds for **`kEmgHoldMs`**, re-ruled
  **120000 → 30000**. ★ **Name the constant, cite no line, restate no value** — this ruling went stale twice in two days
  for exactly that reason.
- ★★★ **B84 (owner):** **`match_channel_failed` is DELETED. Do not reintroduce it.** `dst == 0` does **not** mean
  "channel" — six unrelated operations emit that shape, and since `channel_failed` is terminal an unrelated console
  operation could **end a live alarm and discard its two unspent transmissions**. Pre-enqueue failures arrive
  **synchronously**; an **unattributable async failure is IGNORED**; and **an expired unattributable emergency CONSUMES
  ONE BOUNDED ATTEMPT** so three expiries reach sticky `NOT HEARD`. ⚠ Without that consumption it is an **infinite retry
  loop** — `_tries` moves only in `on_send_accepted`.
- ★ **B38 (owner):** `relayed` = **FIRST RELAY ONLY**, never coverage. On a fully-1-hop team it reads `false` at 100 %
  delivery — **accepted behaviour.** The emergency screen will legitimately show `NOT HEARD` on a small co-located team.
- ★ **B87:** OLED warning totals **PINNED 178/178/174**, `-Wswitch` 0, gated by `tools/warning_census.sh`. **THREE** OLED
  envs — `gateway_heltec` `extends heltec_v3`. Scoping `-fno-rtti` is its **own** build slice (`build_src_flags` covers
  `src/` only and would strip it from `lib/` + `variants/`).
- ★ **B94:** **KEEP HEAD's `lib_extra_dirs`** — bench-proven on Windows. It breaks a Linux box with a **stale duplicate
  framework package**; that is an **environment** defect (remove the leftover). `tools/wire_path.py` is a measured,
  host-independent alternative **deliberately not adopted**: it costs a **B87 re-pin** (+52 B flash). Take it only if a
  second host hits this.
- **`variants/` = per-board sources.** The broad board/chip split (old "Phase 0") is **PARKED**: measured **3**
  board-discriminating lines, not 25. The real coupling is **56 `-D` flags**, which needs its own design.

---

## 6. Method — the part worth carrying

★★★ **THE ONE LESSON, and it cost eight wrong hypotheses in a single thread: BEFORE TRUSTING ANY CHECK, ASK WHETHER IT
COULD HAVE FAILED.** Every wrong turn came from reasoning over an instrument that could not observe the thing asked
about:
- an **empty build directory** — my own rewritten gate had moved to temp dirs, so every `find`/`nm` inspected nothing;
- **`pio pkg list`**, which is **structurally silent** about framework built-ins and prints the same two libraries on the
  host where Wire demonstrably builds;
- a **truncated dependency graph** (`head -12`);
- a **Linux verification that cannot fail**, because discovery finds Wire regardless of the config under test;
- **negative controls wired to a vanished scratch directory** — they "passed" against nonexistent paths;
- and a **warning census that printed and exited 0** — I shipped it *as* the instrument that made a pin enforceable.

⇒ **A green result from an instrument you have not falsified is not evidence.** Every fix needs a probe that turns **red**
when that behaviour alone is reverted; every zero needs a positive control; every build needs its **object count**
printed (a 0-object build reporting 0 warnings is a green light wired to nothing).

★★ **ADDRESS CODE BY STRUCTURE, NEVER BY POSITION.** Six defects, six tools: a line-range lift that clipped a closing
brace · a first-match insertion that put config in **the wrong `[env:]` section** (and Linux could not detect it) · a
relative path in a cwd-resetting shell · a `[^)]*?` regex stopping at a nested paren · an anchor table found by line
range · a whitespace-mismatched replacement that **silently no-opped**. **Verify every edit's result** — brace balance, a
compile, section membership, a grep of the outcome.

★★ **DERIVE A SET, NEVER NAME ONE.** "every `tx_with_retry` caller" missed the defect (it lived in a function that
bypassed it; `grep -rn '_hal\.tx('` found four sites where prose found one). "The two OLED envs" missed
`gateway_heltec`. And ⚠ `grep -rl "aired"` also matches **`paired`** — match whole words, then read every hit.

★ **`REQUIRE` DOES NOT COMPILE** here (`-fno-exceptions`). ★★ **NEVER evaluate a CONSUMING expression twice** — the
`CHECK(expr); if (!expr) return;` idiom **silently deleted 9 of 11 assertions behind a green tick**. One call into a
local, distinct name per block.

★ **Premises in a brief are hypotheses, and saying so is where most value came from.** Coders correctly refused: the
`hash:0` deletion, the three-way `TxHandOff` that avoided a DATA regression, the `variants/` collision, `channel_failed`
being terminal. ⚠ **A report with no disproven premises is worth questioning.**

★ **When a defect is HOST-SPECIFIC, the remote instrument is the only one that counts.** Every decisive Wire fact came
from the owner's machine; I substituted local checks that structurally could not fail — and, worst of the thread, called
a **working, bench-proven fix "actively harmful"** and removed it, reading my own stale package install as evidence.

⚠ **Build systems fail in opposite directions:** ninja keys on **mtime** (`touch` after restoring); PlatformIO keys on a
**content signature** (`rm -rf .pio/build/<env>/{src,lib*}` — `touch` is a no-op). ⚠ An **incremental `pio run` emits no
warnings**, so a warning pin needs a clean build to mean anything. ⚠ **Serial `pio` only** — concurrent builds on one env
corrupt the archive and produce a false `FAILED` I once nearly reported as a finding. ⚠ **`post:` extra-scripts cannot
add TUs** ("the main program is already constructed") — use `pre:`. ⚠ **Never `git checkout --`** in this tree; it
destroyed an uncommitted slice's work.

★ **Probe harnesses are the only cover for `src/` and `variants/`** (neither is in the native build). `tools/probe_board_ui/`
is the model, and its properties were all QA findings against me: **paths by argv** · **mutate a COPY, never the real
source, and assert its md5 unchanged** · **controls run BY DEFAULT** · **prove the harness can fail.**
