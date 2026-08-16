<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B200 — GPIO wake arming: arm at the sleep, never at boot · BLOCKING FIX · dispatch brief · 2026-08-15

**Status: DISPATCHED 2026-08-15 on an owner directive.** ★ Role split: the QA-gate wrote this brief and verifies the
claims at the code; **the OWNER runs QG and rules.** ⛔ **Never `git commit`, never `git add`, NEVER `git checkout --`
anything.** The tree carries the uncommitted B197/B198 slice; this fixes a regression **inside** it.

★★★ **THIS SLICE BLOCKS B197/B198. The current image PANICS ON DEMAND — worse than the defect it was fixing.**

---
## 1 — The defect, verified

`variants/heltec_v3/board_ui.cpp:328-331`:
```cpp
bool enable_button_wake() {
    if (gpio_wakeup_enable((gpio_num_t)MR_UI_BTN_PIN, GPIO_INTR_LOW_LEVEL) != ESP_OK) return false;
    return esp_sleep_enable_gpio_wakeup() == ESP_OK;
}
```
called **once at boot** from `mr_ui_init()`. `GPIO_INTR_LOW_LEVEL` is **level-triggered** and cannot be cleared while
the level holds ⇒ a **held button storms the shared GPIO ISR** ⇒ `Interrupt wdt timeout on CPU1`. The shared ISR is
live because RadioLib's `setPacketReceivedAction(mr_on_dio1)` (`lib/hal/device_radio.h:75`) attaches a DIO1 interrupt.
**Captured on metal**: `Core 1 was running in ISR context`, both panics sharing `EPC1: 0x420dc257`.
⛔ **The spec instruction that caused it — §3.1's *"`mr_ui_init()` calls `enable_button_wake()` once"* — is WITHDRAWN.**

---
## 2 — What to build (the owner's directive, made concrete)

1. ⛔ **REMOVE the permanent boot-time arming entirely.** No arm survives past a sleep.
2. ★ **RE-SAMPLE THE PHYSICAL BUTTON immediately before arming** — read the pin, do **not** trust `InputFsm`. The FSM
   is updated at tick time; the user can press between the tick and the sleep call, and arming a low-level wake on an
   already-low pin is the storm. **If the pin reads pressed, do not arm and do not sleep this pass.**
3. **Arm ONLY immediately before `esp_light_sleep_start()`** — `src/fw_main.cpp:994`, ⛔ **inside** the existing
   `if (rtc_gpio_is_valid_gpio((gpio_num_t)LORA_PIN_DIO1))` guard at `:984`. Outside it, a board whose DIO1 is not
   RTC-capable would arm and never sleep or disarm — B200 on a different board.
4. ★ **ROLL BACK EVERY PARTIAL FAILURE.** If `gpio_wakeup_enable()` succeeds and `esp_sleep_enable_gpio_wakeup()`
   fails, **undo the first** before returning. A half-armed pin is exactly the storm.
5. ★ **DISARM IMMEDIATELY ON EVERY RETURN PATH** from the sleep function — normal wake, early return, failure. ⛔ No
   path may leave the level interrupt armed while the CPU runs.
6. **LATCH SLEEP DISABLED after an arm/disarm HARDWARE failure** (boot-scoped). A node that cannot arm or disarm
   reliably must stop sleeping rather than risk the storm — fail closed, as the design already requires.
7. ★★ **KEEP `slept=` TRUTHFUL.** ⚠ Today `++g_sleep_count` is at `src/fw_main.cpp:1435`, **before**
   `board_sleep_until()` at `:1436`. With a fallible arm that becomes a **lie** — the counter would report sleeps that
   never happened, and `slept=` is the field the bench checks read. **Count only a sleep that actually occurred.**
8. **ADD WAKE-CAUSE AND FAILURE COUNTERS**, surfaced in `status` beside `slept=`:
   - per-cause wake counts from `esp_sleep_get_wakeup_cause()` — **GPIO**, **EXT1** (DIO1), **TIMER**;
   - arm-failure and disarm-failure counts.
   ★★ **The wake-cause counters are worth more than diagnostics: they CLOSE the attribution gap bench 23.1(b) could
   not.** That check proved the radio path survives but could not tell a DIO1 edge from the 1 s timer wake, because
   the cap wakes the MCU anyway. A per-cause counter answers it directly and permanently.

**Seam:** the board owns the pin (U3 — `fw_main.cpp` must not learn `MR_UI_BTN_PIN` or `MR_FEAT_OLED`). Extend
`lib/hal/mr_ui.h` with the arm/disarm pair as unconditional seams plus non-OLED no-op stubs, exactly as the existing
five hooks do. ⛔ Do not export `board_ui.cpp` internals.

★ **A property worth keeping, because it makes the fix compose rather than fight:** `mr_ui_allows_sleep()` already
returns false while `input.active()`, so the sleep call is not reached with the button held. §2.2's re-sample closes
the residual race rather than replacing that guarantee.

---
## 3 — Probes: RETARGET, do not extend

⛔⛔ **The existing `probe_board_ui` W-checks PIN ARM-ONCE-AT-BOOT — they would go GREEN against this defect.** That is
the failure mode this project keeps recording. **Retarget them** to pin: arm **before** `esp_light_sleep_start()`,
disarm **after**, both **inside** the RTC guard, the pin re-sample **before** the arm, and rollback on partial failure.
**Controls that must be RED:** arm missing · disarm missing · order reversed · arm result ignored · rollback removed ·
re-sample removed · arm hoisted back to `mr_ui_init()` · the pair placed outside the RTC guard.
★ Every new assertion mutation-proven, match counts printed. **Ask of each: could it have come out otherwise?**

---
## 4 — Documentation to withdraw and correct in place (§3 rule 3 — never delete)
- `docs/superpowers/specs/2026-08-14-b197-b198-ui-sleep-wake-design.md` **§3.1** (*"calls `enable_button_wake()`
  once"*) and **§3.4** (the wake-to-gesture sequence assumes a permanent arm) — **both withdrawn and rewritten.**
- `variants/heltec_v3/board_ui.cpp`'s DONE/NOT-DONE header block, which states the boot-arm contract.
- `docs/2026-07-31-bench-test-script.md` **Part 23**: add a check that **a long press no longer panics** — it is the
  reproducer — and note **23.1(b) MUST BE RE-RUN**: its pass was measured with the wake armed permanently and does
  **not** transfer to per-sleep arming.
- **[[B200]]** closed in place with the measurement; **[[B199]]**'s correction (B200 likely explains both watchdogs)
  confirmed or refined by whatever this slice establishes.

---
## 5 — ★ The ELF/provenance item, and a ONE-LINE ROOT CAUSE I FOUND

The metal capture's banner reads **`nogit`**, so the faulting image cannot be tied to a revision. **Verified cause:**
`pre:tools/git_rev.py` appears **exactly once** in `platformio.ini` — line **108**, inside `[env:xiao_sx1262]`.
**`[env:heltec_v3]` has its own `extra_scripts = pre:tools/wire_path.py` (`:290-291`) and no `extends`**, so it never
injects `GIT_REV` and `fw_main`'s `#ifndef GIT_REV #define GIT_REV "nogit"` fallback applies. ⇒ **every Heltec image
ever flashed has been unidentifiable from its banner — the boards that produce the field faults are the ones without
provenance.**

- **Add the `git_rev.py` pre-script to the OLED/Heltec envs** (`heltec_v3`, `heltec_mobile`, `gateway_heltec`).
  ⚠ It changes the banner string and therefore flash bytes — **not behaviour**; say so in the report.
- **Archive the stamped ELF for the metal build** alongside the slice evidence, with its `sha256`, so a future
  backtrace is decodable. ⓘ The captured fault reported `ELF file SHA256: 7a8aaa957`, which is **not** retained —
  that capture stays undecodable and this is what stops the next one from being.

---
## 6 — Gate
1. `pio test -e native`, **then RUN the binary** (the wrapper prints a false *"0 test cases"*).
2. **Both UI probes** with their control sets; the retargeted W-checks reported explicitly.
3. **`warning_census.sh`** at its pins.
4. **Simulator inertness, the four-step structural proof**: pre-`lus` md5 → canonical rebuild → **zero relevant build
   actions** → post md5 identical; `s18` as smoke only, keystone **read from `simulation/BASELINE.md`**, never
   hardcoded. ⛔ No 36-scenario rerun, ⛔ no anchor-table edit.
5. **Six board envs build**; per-board RAM/flash. ⚠ Flash moves on the Heltec envs from the `GIT_REV` string — expected
   and attributable.
6. **D2 explicitly**: `sizeof(Node)` unmoved; no new timer ids (`kCap` 91).
7. ⛔ Nothing committed (D4).

**Report:** each of §2.1-2.8 with `file:line` · the re-sample and why it is not redundant with `input.active()` · the
rollback and every return path · the latch · the corrected `slept=` accounting · the wake-cause counters and their
`status` field names · every retargeted probe check with its controls · the documentation withdrawals · the
`git_rev` change and the archived ELF path + sha256 · the gate · exact final `git status --short`.
⛔ **Anything you cannot establish, say so plainly — especially what only metal can settle.**

---
---
# ROUND 2 — QG HOLD (relayed by the owner 2026-08-15). ⛔ NOT READY FOR METAL. Three corrections.

⚠ QG's findings relayed by the owner — a recommendation, not an owner ruling (ledger §3 rule 5). **All three were
re-verified at the code before being written here.**
✅ **QG confirms the direction is right:** boot-time arming gone, pin re-sampled, partial arming rolled back, wake
scoped around the sleep. ⛔ Do not re-open any of that.

## R2.1 — ⛔⛔ `esp_light_sleep_start()`'s RETURN IS DISCARDED, and it re-breaks the owner's own requirement 7

`src/fw_main.cpp:1025` is a bare `esp_light_sleep_start();`. ESP-IDF documents that light sleep can return
**`ESP_ERR_SLEEP_REJECT`** or **`ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION`** — *the CPU did not sleep*. Today such a
rejection: **increments `slept=`** · **tallies a stale wake cause** · **returns `true` from `board_sleep_until()`**.
★★ **That is exactly the untruthful `slept=` the owner's directive item 7 required fixed — reintroduced through a
different door.**

**Required shape:**
```cpp
const esp_err_t sleep_rc = esp_light_sleep_start();
const bool disarm_ok = mr_ui_disarm_button_wake();     // ★ FIRST — see R2.2
if (!disarm_ok) ++g_wake_disarm_fail;
if (sleep_rc != ESP_OK) { ++g_wake_sleep_fail; return false; }
const auto cause = esp_sleep_get_wakeup_cause();
// tally cause
return true;
```
- **add a `wksleepfail=` counter** to `status` beside the others;
- **mutation controls for BOTH rejection results** (`ESP_ERR_SLEEP_REJECT`, `ESP_ERR_SLEEP_TOO_SHORT_SLEEP_DURATION`)
  — a control that only drives one of them leaves the other unmeasured.

## R2.2 — ⛔⛔ THE DISARM MUST BE THE FIRST OPERATION AFTER THE HALT RETURNS

Today the cause is read at `:1030` and the disarm happens at `:1035`. ⛔ **This contradicts the directive's *"disarm
immediately on every return"* AND the comment sitting directly above it**, which claims nothing may sit between the
halt and the disarm while three statements do.

★★★ **THE SAFETY ARGUMENT, and it is decisive: on a GPIO wake THE BUTTON IS BY DEFINITION STILL HELD LOW — that is
what woke the node.** So the interval between the halt returning and `gpio_wakeup_disable()` is **precisely the window
in which the storm condition is live**, on a running CPU. Reading the cause first extends exactly the wrong window.
⇒ **removing the active-low level interrupt is the safety-critical first operation; wake-cause inspection follows.**
ⓘ The withdrawn comment defended the old order as *"so no teardown can be suspected of clobbering it"* — that concern
is **speculative**, and if it were ever real it would surface benignly as `wk_gpio=0`, not as a panic. **Withdraw that
comment in place** (§3 rule 3) rather than deleting it.

⛔⛔ **AND THE PROBE CURRENTLY ENFORCES THE DEFECT: `tools/probe_board_ui/run.sh:658-661`'s `w31()` greps for
`… esp_sleep_get_wakeup_cause(); if (!mr_ui_disarm_button_wake())` — i.e. IT REQUIRES THE UNSAFE ORDER.** ★ This is the
**third** instance in this arc of an instrument pinning the very shape it should forbid (after the arm-once W-checks
and [[B195]]'s vacuous tripwire). **Retarget W31 to require: `sleep returns → disarm → inspect wake cause`, and add a
negative control that moves the cause inspection back BEFORE the disarm and MUST FAIL.**

## R2.3 — the `wkbusy != 0` metal expectation is INVALID

`docs/2026-07-31-bench-test-script.md:2283` requires `wkbusy=` **non-zero**, parenthesised *"you held the button
across sleep attempts, which is what it counts"*. ⛔ **That is wrong:** `mr_ui_allows_sleep()` already returns false
while `input.active()`, so a held button normally prevents the arm being **attempted at all**. `wkbusy` therefore
counts **only the narrow race** where the press lands between the UI tick and the physical re-sample — and it can
legitimately read **zero through every long-press test**.

**Replace step 4's expectation with:** `slept=` increased · **`wk_gpio=` increased after the sleeping-button test** ·
`wkarmfail=0` · `wkdisarm=0` · ⛔ **`wksleepfail=` RECORD IT — do NOT require zero (CORRECTED 2026-08-15: demanding zero here was the SAME error as the `wkbusy` one it sat beside; both documented refusals are legitimate and the firmware already treats them as *did not sleep*, not as hardware failures)** · **`wkbusy=` MAY BE ZERO — a non-zero value merely shows the race
guard was exercised.**

## R2.4 — Re-gate
Native (binary run) · both probes with control sets, W31's retarget and its new control reported explicitly ·
`warning_census.sh` at its pins · the four-step simulator-inertness proof (`lus` md5 → rebuild → **0 relevant build
actions** → identical md5; `s18` smoke only, keystone **read from `BASELINE.md`**) · six board envs with RAM/flash ·
`sizeof(Node)` unmoved · ⛔ nothing committed. ⛔ **[[B196]] untouched.**

---
---
# ROUND 3 — the fix is INCOMPLETE. Two-part correction, dispatched on an owner directive 2026-08-15.

⛔ **THE PANIC REPRODUCED ON THE ROUND-2 IMAGE** (`fw_main.cpp` mtime 19:27 < the flashed build 21:54, so the metal
image DOES carry round 2). **The B197/B198 block stands.**

## R3.1 — What the two-way test established (owner-run, and it discriminates cleanly)

| test | condition | result |
|---|---|---|
| **A** | power-on, `reboot` within ~1 s, long press. Up ~1 s ⇒ inside the 30 s grace ⇒ **never slept ⇒ NEVER ARMED** | ✅ **no panic** |
| **B** | `reboot` after `ran 1m17s` (past the grace, headless until the keystroke ⇒ **it slept, so it armed AND disarmed**), button pressed as the first boot lines appeared | ⛔ **PANIC** — `Interrupt wdt timeout on CPU1`, ISR context |

⇒ ★★★ **A boot that NEVER ARMED does not storm; a boot that ARMED AND DISARMED before the reset DOES.**
⇒ **`disarm_button_wake()` IS INCOMPLETE: something `gpio_wakeup_enable(GPIO0, GPIO_INTR_LOW_LEVEL)` sets survives
BOTH our disarm AND `RTC_SW_CPU_RST`** (a CPU-only reset — `do_reboot()` calls `ESP.restart()`, `src/fw_main.cpp:275`).
ⓘ **Corroboration:** this panic hit **MID-BANNER** (it cut the `inbox = RAM volatile…` line), i.e. **earlier than the
round-1 capture and as soon as interrupts went live** — the signature of a level interrupt already configured before
our code ran.

## R3.2 — Part 1: the disarm must clear the INTERRUPT TYPE, not just the wake source

**Leading mechanism:** `gpio_wakeup_disable()` clears the wake-**enable** bit but **not the pin's interrupt type**.
GPIO0 keeps `GPIO_INTR_LOW_LEVEL` in the GPIO matrix; a CPU-only reset does not clear it; the next boot re-installs
the shared GPIO ISR (RadioLib `attachInterrupt` via `setPacketReceivedAction`) and a held button asserts the level.
⇒ **`disarm_button_wake()` must ALSO `gpio_set_intr_type(MR_UI_BTN_PIN, GPIO_INTR_DISABLE)`**, and must still attempt
every withdrawal even if an earlier one fails (the existing rule). ⛔ **Verify the claim at the IDF source rather than
inheriting it from this brief** (V1) — if `gpio_wakeup_disable()` *does* clear the type, say so and report what else
survives.

## R3.3 — Part 2: a BOOT-TIME SCRUB, because a reset can happen WHILE ARMED

Part 1 fixes the orderly path. It does **not** cover a reset taken **while armed** — a panic or WDT during sleep —
after which **no disarm ever runs**. ⇒ **scrub GPIO0's wake enable AND interrupt type once at boot.**

★★ **THE PLACEMENT IS THE CRUX AND IT IS VERIFIED:** the scrub must run **BEFORE `g_radio.std_init()`
(`src/fw_main.cpp:622`/`:624`)**, which is where RadioLib installs the shared GPIO ISR. ⛔ **`mr_ui_init()`
(`src/fw_main.cpp:864`) IS FAR TOO LATE** — by then the ISR service exists and interrupts are live, so the storm has
already happened. Place it after `mrfault::fault_wdt_start()` (`:598`, so a hang in the scrub is still caught) and
before `:622`.

⚠ **Reuse the EXISTING seam — `mr_ui_disarm_button_wake()` already does exactly what a scrub is** (U1); ⛔ do not add a
sixth hook, and ⛔ `fw_main.cpp` still must not learn `MR_UI_BTN_PIN` or `MR_FEAT_OLED`.
⚠⚠ **BUT CHECK ONE THING BEFORE WIRING IT: a boot scrub must NOT spuriously latch sleep off.** Nothing is armed at
boot, and the latch is boot-scoped and unclearable — if the IDF returns non-OK for withdrawing a source that was never
enabled, the node would disable sleep for every boot for ever. **Establish the actual return codes for the
never-armed case and handle them; do not assume.**

★ **Note the shape: this is DISARM at boot — the exact MIRROR of the original B200 defect, which was ARM at boot.**
⇒ it also gives [[B199]]'s `ran 0s` a mechanism: a reset taken while armed leaves the next boot storming before its
first loop heartbeat.

## R3.4 — Tests
- **Probe:** the scrub call exists, goes **through the seam**, and sits **before** the radio init. **Controls:**
  scrub removed · scrub moved **after** `std_init()` · scrub replaced by an **arm** (the original defect, literally) ·
  the interrupt-type clear removed from the disarm · the disarm's remaining withdrawals skipped after a failure.
- **Native** where the logic is reachable; every new assertion mutation-proven with match counts.
- **Bench 23.6 gains the two reboot cases** as the reproducer: **(A)** power-on → `reboot` within ~5 s (never slept)
  → hold ⇒ no panic; **(B)** wait past 45 s so it sleeps, **read `slept=` to confirm it armed**, → `reboot` → hold
  ⇒ no panic. ⚠ **(B) is the one that fails today**, and ⛔ **the `slept=` read is not optional** — it is what proves
  the arm happened, which is the assumption the whole diagnosis rests on.

## R3.5 — Explicitly NOT in this slice
⛔ `tools/git_rev.py`'s silent `"nogit"` fallback (a C2 violation that has cost two undecodable captures) and a
`GIT_REV` environment override for cross-machine builds. **Recorded here so it is not lost; not authorised now.**
ⓘ The operator has since fixed their side (`git config --global --add safe.directory /mnt/MeshRoute`; `rev-parse`
now answers `473581f`), so the **next** build will stamp and be decodable.

---
---
# ROUND 4 — QG HOLD. Two firmware blockers: the ROLLBACK path was missed, and the teardown ORDER is wrong.

⚠ QG's findings relayed by the owner — a recommendation, not an owner ruling (ledger §3 rule 5). **Both re-verified at
the code.** ✅ Round 3's driver disassembly and its conclusion stand; ⛔ what is wrong is where the conclusion was
applied.

## R4.1 — ⛔⛔ BLOCKER: the PARTIAL-ARM ROLLBACK still clears only bit 10 — the SAME defect, a SECOND site

`variants/heltec_v3/board_ui.cpp:353-355`:
```cpp
    if (esp_sleep_enable_gpio_wakeup() != ESP_OK) {
        (void)gpio_wakeup_disable((gpio_num_t)MR_UI_BTN_PIN);   // ROLL BACK: never leave a half-armed level behind
        return WakeArm::failed;
    }
```
★★ **Round 3 proved at the linked driver that `gpio_wakeup_disable()` clears WAKEUP_ENABLE (bit 10) ALONE and leaves
`GPIO_INTR_LOW_LEVEL` in INT_TYPE (bits 9:7).** ⇒ **this rollback RECREATES EXACTLY THE ROUND-3 RESIDUE**, and the
caller is told `failed` — i.e. it believes nothing is armed. **The fix was applied to `disarm_button_wake()` and NOT
to the rollback.**
⚠ **And the comment on that very line asserts the property the code violates** — *"never leave a half-armed level
behind"* is precisely what it leaves. Same shape as round 2's comment that forbade what the statements above it were
doing. **Withdraw it in place.**

⛔⛔ **AND THE PROBE PINS THE INSUFFICIENT CALL: `tools/probe_board_ui/probe_main.cpp:423`'s P11j checks only
`gpio_wakeup_disable()`, so its GREEN is misleading.** ★ **FOURTH instance in this arc of an instrument enforcing the
shape it should forbid** (after the arm-once W-checks, [[B195]]'s vacuous tripwire, and W31's unsafe order).

## R4.2 — ⛔ BLOCKER: the storming state is cleared SECOND, not FIRST

`board_ui.cpp:385`'s teardown order is **(1)** wake-enable bit, **(2)** interrupt type, **(3)** sleep source.
★★ **Under this slice's OWN verified diagnosis, step (1) does not stop the storm — INT_TYPE is what drives the
interrupt.** After a GPIO wake the CPU is running and **the button is still low** (that is what woke it), so the
teardown spends two operations before touching the thing that is actually storming.
⇒ **`gpio_set_intr_type(MR_UI_BTN_PIN, GPIO_INTR_DISABLE)` MUST BE THE FIRST TEARDOWN OPERATION.**

## R4.3 — The required shape: ONE shared teardown (U1)

```cpp
static bool clear_button_wake_state() {
    const bool type_off = gpio_set_intr_type(MR_UI_BTN_PIN, GPIO_INTR_DISABLE) == ESP_OK;   // FIRST — the storm source
    const bool pin_off  = gpio_wakeup_disable(MR_UI_BTN_PIN) == ESP_OK;
    const esp_err_t src = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    const bool src_off  = src == ESP_OK || src == ESP_ERR_INVALID_STATE;
    return type_off && pin_off && src_off;
}
```
- **normal disarm calls it**; **partial-arm failure calls it** — ⛔ one teardown, two callers, never two copies;
- keep attempting **all three** after any failure, verdicts ANDed (unchanged);
- keep the narrow `ESP_ERR_INVALID_STATE` forgiveness and both opposing controls (C9u/C9v) — that was correct.

⛔ **NO `gpio_intr_disable()` IS JUSTIFIED** and adding one is out of scope: the linked `gpio_wakeup_enable()`
producer writes **INT_TYPE and the wake bit, not the CPU interrupt-enable field**, so the teardown stays symmetric
with the producer. ★ If you believe otherwise, **report it with driver evidence; do not add it silently.**

## R4.4 — Probes
- **Retarget P11j** to require the **full** teardown on the rollback path — its current green is the defect.
- **Require the interrupt-type clear FIRST** in both callers, and add a control **swapping it behind
  `gpio_wakeup_disable()`** which **must turn RED**.
- Add a control proving the two callers share **one** helper (duplicate the body into either site ⇒ RED), so the fix
  cannot drift apart again — this bug exists because one site was updated and the other was not.

## R4.5 — Re-gate
Native from the binary · both probes with control sets and the retargeted P11j reported explicitly ·
`warning_census.sh` at its pins · the four-step simulator-inertness proof · six envs with RAM/flash (⚠ expect the
three non-OLED envs to stay **byte-identical** — the seam is inert there) · `sizeof(Node)` unmoved · ⛔ nothing
committed. ⛔ [[B196]] untouched. ⛔ `git_rev.py` still NOT in scope.
