<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B196-FIX — assert `ESP_PD_DOMAIN_RTC_PERIPH` ONCE PER BOOT · dispatch brief · 2026-08-17

**Status: DISPATCHED 2026-08-17.** ★ Role split: the QA-gate wrote this brief and verifies your claims at the code;
**the OWNER runs QG and rules.**

★★ **OWNER RULING 2026-08-17 — APPROVED, reported form (⛔ not quoted).** Candidate **(a)**: call
`esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)` **exactly once per boot in the ESP32 `setup()` path,
before `loop()` can sleep**. Preserve `MR_NO_POWERSAVE`, the ESP32 compile guard, and the
`rtc_gpio_is_valid_gpio(LORA_PIN_DIO1)` guard. Remove the recurring call from `board_sleep_until()`. Keep the `ext1`
and timer configuration **per sleep attempt**. **Check the return and print one generic diagnostic on failure, then
continue** — ⛔ no new sleep-disable policy in this slice, and ⛔ the message must not be OLED-specific. **[[B196]]
stays OPEN until a fixed, revision-stamped image survives headless with `sleep=auto` and `slept` > 32,769 without
resetting.**

⚠ **This brief carries FIVE corrections made in place after a QG review** (search `CORRECTED 2026-08-17`). **Where a
correction and the original disagree, the correction wins**; withdrawn wording is left visible deliberately (§3
rule 3) and ⛔ must not be re-implemented.
⛔ **Never `git commit`, never `git add`, NEVER `git checkout --` anything, never check out another commit in this
working tree.** HEAD is `a1e53dd`; the tree carries doc edits only.

**Normative record: the [[B196]] row of `docs/2026-07-30-open-bug-register.md`. READ IT FIRST — it is the authority,
not this brief.** It carries the proven mechanism, the instruction-level evidence, two corrections made in place, and
the closure criterion. ⚠ **Two things on that row are WITHDRAWN; do not implement or re-derive them.**

---
## 0 — Scope: ONE call site, ONE property

**BUILD:** make the `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)` at **`src/fw_main.cpp:1030`**
happen **once per boot in the init path** instead of once per sleep attempt.

⛔⛔ **OUT — and this boundary is narrow on purpose, because the defect is a nine-hour reboot on a safety device and
the fix must be reviewable at a glance:** ⛔ no other sleep-path change · ⛔ no touching §B200's
`arm_button_wake` / `disarm_button_wake` / `clear_button_wake_state` (that arc closed after five rounds) · ⛔ no
[[B204]] boot-scrub change (adjacent in spirit, separate row, unruled) · ⛔ no `lib/core` change · ⛔ no wire, NV or
`Node` change · ⛔ no chrome/UI work.
⛔ **If you notice another defect, REGISTER it (M1) — do not fix it here.**

---
## 1 — Verified state (V1 — re-verify before relying on it)

| fact | where | note |
|---|---|---|
| the defect | `src/fw_main.cpp:1030` | `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)`, return **discarded** |
| it is the only one | `src/` `variants/` `lib/` | **exactly one** `esp_sleep_pd_config` **call expression in production code**, and **zero `ESP_PD_OPTION_OFF`** ones ⚠ (docs and comments name both — see §5) |
| the enclosing guard | `:1029` | `if (rtc_gpio_is_valid_gpio((gpio_num_t)LORA_PIN_DIO1))` |
| the enclosing `#if` | `:1017` / `:1028` | `#if !defined(MR_NO_POWERSAVE)`, then the ESP32 arm |
| the neighbours | `:1031` `ext1`, `:1034` `timer` | ⛔ **these MUST stay per-attempt** — the timer duration is recomputed every pass |
| the counter | IDF `sleep_modes.c:213` | **`int16_t refs`**, **incremented on every call until signed wrap**; aborts on call **#32,769** |

★★ **ONE FINDING THAT MAKES THIS FIX PROVABLY BEHAVIOUR-PRESERVING, and you should confirm it yourself rather than
take it from me.** In the disassembly of that exact ELF, `esp_sleep_pd_config` writes `pd_option` **only when the
pre-call `refs` was zero** (`bnez.n a7, …` skipping the `s32i a3, a9, 0` at offset 0). ⇒ **the FIRST `ON` call sets
`pd_option`; all 32,768 later ones changed nothing but the counter.** ⇒ calling it once per boot leaves the configured
domain state **identical** — the redundant calls were pure counter churn.
ⓘ Corollary worth stating: if anything *did* reset `pd_option` mid-run, the current per-attempt code would **not**
restore it either (`refs != 0` skips the write). ⇒ **once-per-boot cannot be worse than the status quo on that axis.**

---
## 2 — ★ THREE V1 CHECKS YOU OWE, AND ⛔ NONE OF THEM MAY BE ASSUMED

1. **Audit every writer of `s_config.domain[]` — `refs` and `pd_option` — on the light-sleep path.** Disassemble
   `esp_light_sleep_start` / the sleep entry path in `~/MeshRoute-artifacts/soak-20260816-1646/firmware.elf` and find
   them all. ⓘ Strong indirect evidence about one field already: `refs` reached 32767, so **nothing zeroes `refs`**.
   ★★ **CORRECTED 2026-08-17 (QG) — WHAT THIS AUDIT CAN AND CANNOT CONCLUDE, because this brief previously
   contradicted its own §1 proof.** It said *"if `pd_option` IS reset per sleep, STOP AND REPORT — candidate (a) is
   then wrong"*. ⛔ **WITHDRAWN.** §1 establishes that later calls **cannot** rewrite `pd_option` while `refs != 0`
   ⇒ if something independently reset only `pd_option`, **the CURRENT per-attempt code would not repair it either.**
   That is a **separate pre-existing defect — register it (M1); it does not bear on the hoist.**
   ⇒ ★ **THE ONE FINDING THAT WOULD MAKE THE HOIST NON-EQUIVALENT IS NARROW AND WELL-DEFINED: something that zeroes
   `refs`**, because that would make later `ON` calls effective again (the `refs == 0` branch rewrites `pd_option`), so
   a per-attempt call would re-arm what a once-per-boot call could not. ⓘ The 32767 observation is strong evidence
   against exactly that. **STOP AND REPORT only for a finding of that shape** — not for any writer at all.
2. **Do `esp_sleep_enable_ext1_wakeup` and `esp_sleep_enable_timer_wakeup` carry any similar counter?** Believed plain
   idempotent setters — ⚠ **a belief, not a measurement.** ⓘ `esp_sleep_enable_gpio_wakeup` (`0x42049994`) is visibly
   a plain OR-into-a-word with no counter; check the other two the same way. **A second latent overflow on this path
   would be found now or in another nine hours.**
3. **Is `rtc_gpio_is_valid_gpio()` safe to call at your chosen init point?** It should be a pure pin-capability query,
   but the placement depends on it. If it needs initialisation you have not reached yet, **report rather than move
   the guard.**

---
## 3 — What to build

- **One `ON` call, once per boot, in the ESP32 init path of `src/fw_main.cpp`**, before `loop()` can run.
- ⛔⛔ **PRESERVE BOTH CONDITIONS EXACTLY** — `#if !defined(MR_NO_POWERSAVE)` **and** the ESP32 arm **and** the
  `rtc_gpio_is_valid_gpio((gpio_num_t)LORA_PIN_DIO1)` runtime guard. **An `MR_NO_POWERSAVE` build must gain no call it
  never had, and a board whose DIO1 cannot wake must configure nothing** — that keeps the hoist behaviour-preserving
  (C1: this is a FIX, and the only intended semantic delta is the call COUNT).
- **Delete the call at `:1030`.** ⛔ Do not leave it "for safety" — a second call is the defect.
- ★ **Leave a comment at BOTH ends** (M-mark done-vs-missing in code): at the new site, why once-per-boot and what
  overflows if it recurs; at `:1030`'s former home, that the domain is configured at init **and must not be
  re-asserted here**, naming [[B196]]. ⓘ The next reader's instinct will be to "restore" it next to the `ext1` call.
- **Pick the site by U3** (match the surrounding init idiom) and report the exact `file:line`.

★★ **THE RETURN VALUE IS RULED — IT IS NOT YOURS TO DECIDE.** (This brief previously left it to the coder "so the
owner can veto it"; ⛔ **that delegation is withdrawn** — the owner ruled it directly.) `:1030` currently **discards**
an `esp_err_t`. **Required:**
- **check the return** and on failure print **ONE generic diagnostic**, e.g. `!! RTC sleep power-domain configuration failed`;
- **then continue** — existing policy preserved;
- ⛔⛔ **the message must NOT be OLED-specific.** ★ **`xiao_esp32s3` / `gateway_esp32s3` / `xiao_esp32s3_mobile` compile
  this code and have NO panel** — reusing the `!! OLED button wake unavailable…` wording (as this brief first
  recommended) would print a false attribution on three envs;
- ⛔ **do NOT disable sleep on failure** — a sleep-disable policy is a separate decision and is **not** in this slice.

⚠ **State plainly in your report that this check is DEFENSIVE AND EXPECTED NEVER TO FIRE — and ⛔ do not present it as
a test.** Verified: the header documents only `ESP_OK` and `ESP_ERR_INVALID_ARG` *"if either of the arguments is out of
range"* (`esp_sleep.h:347-350`), **both of our arguments are in-range compile-time constants**, and the disassembly's
only error path is the two range checks (`bgeui a2, 6` / `bgeui a3, 3`). ⇒ **this is not a fallible hardware
operation.** It is cheap insurance against a future edit passing a wrong domain, nothing more.

---
## 4 — Traps

- ⛔ **Dropping either compile condition or the runtime guard** — the most likely way to make this a regression.
- ⛔ **Hoisting the `ext1` or `timer` arm along with it.** The timer duration is per-attempt by construction; moving it
  would break every deadline. **Only the `pd_config` moves.**
- ⛔ **Placing the call after something that can sleep.** Verify nothing between your site and `loop()` reaches
  `board_sleep_until()`.
- ⛔ **A one-shot `static bool` inside `board_sleep_until()`** — that is candidate (b); **the owner ruled (a).**
- ⛔ **`sizeof(Node)` must stay 221880**; no timer, no `kCap` change.

---
## 5 — Tests: ★★ BE HONEST ABOUT WHAT IS AND IS NOT REACHABLE HERE

⛔⛔ **THIS DEFECT IS STRUCTURALLY INVISIBLE TO EVERY AUTOMATED GATE WE HAVE, and pretending otherwise is worse than
admitting it.** It needs a real ESP32, real IDF state, and **32,769 sleep attempts**. Native does not compile this
arm; the 36 corpus streams never enter it; neither UI probe drives `fw_main.cpp`'s sleep path.

⇒ **What you CAN legitimately build is a SOURCE-SHAPE (wiring) check, following the established
`tools/probe_board_ui` W-series precedent:**
- ★★ **exactly ONE `esp_sleep_pd_config` CALL EXPRESSION in production code** — ⛔ **CORRECTED 2026-08-17 (QG): this
  read *"exactly one … in the tree"*, WHICH IS UNACHIEVABLE AND WOULD HAVE FAILED IMMEDIATELY.** The register row, this
  brief, and §3's two **required** comments all contain the identifier. ⇒ **scope it to `src/`, `variants/` and
  `lib/`, with COMMENTS STRIPPED, and count CALL EXPRESSIONS — not text occurrences.** ⓘ Cheapest sound source of
  truth is the compiler, not a grep: count against preprocessed/comment-stripped output. **A raw `grep -c` is exactly
  the vacuous instrument this section warns about.**
- it is **NOT** inside `board_sleep_until()`;
- it **IS** inside both the `MR_NO_POWERSAVE` and ESP32 conditions and under the `rtc_gpio_is_valid_gpio` guard;
- **ZERO `ESP_PD_OPTION_OFF` call arguments** — same rule, **call expressions in production code, comments stripped**
  (the register row and this brief both name the constant while discussing candidate (c)). This forbids a future
  half-pairing driving `refs` negative from the other side.

★★★ **THIS ARC HAS RECORDED EIGHT INSTRUMENTS THAT COULD NOT FAIL, AND A GREP-SHAPED CHECK IS THE EASIEST NINTH.**
⇒ **every bullet above needs a control that goes RED**: the call restored at `:1030`, the call present twice, the
guard removed, the `#if` dropped, an `OFF` introduced. **Print match counts, restore sources, md5-verify the restore.**
⚠ **And re-run the WHOLE battery, not just new entries** — a mutation whose target line has MOVED measures nothing and
is reported `unusable`, never as a pass. **This change moves lines.**
⛔ **Ask of every check: could it have come out otherwise?** If a check would pass against the unfixed tree, it is not
a check.

---
## 6 — Gate

⛔ **MEASURE THE BASELINE AT `a1e53dd` YOURSELF AND REPORT BOTH NUMBERS.** ⚠ **This brief deliberately pins NO
baseline figures**: the last ones I hold predate §CHROME-4, and a stale baseline pin is itself a defect this arc
already raised. **Do not copy figures from an older brief.**

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** — the wrapper lies "0 test cases"; the binary
   prints the real count. Report baseline → after.
2. **Both UI probes** with their control sets.
3. **`warning_census.sh`** at its pins, `-Wswitch` 0.
4. **Simulator inertness, the four-step structural proof** (pre-`lus` md5 → canonical rebuild → **zero relevant build
   actions** → identical md5); `s18` smoke only, keystone **read from `simulation/BASELINE.md`**. ⛔ No 36-run, ⛔ no
   anchor-table edit. ⓘ `lib/core` is untouched here, so the corpus is **inert by construction** (D2) — prove it
   anyway.
5. **Six board envs**, per-board RAM/flash **attributed**. ⛔⛔ **CORRECTED 2026-08-17 (QG) — THIS BRIEF'S ORIGINAL
   EXPECTATION WAS WRONG AND WOULD HAVE READ A CORRECT BUILD AS A REGRESSION.** It said *"the three non-OLED envs and
   the nRF52 envs must be byte-identical"*. ⛔ **WITHDRAWN: `MR_FEAT_OLED` IS IRRELEVANT HERE — the sleep arm is
   selected by PLATFORM, not by panel.** Verified from `platformio.ini`:

   | platform | envs | this code | expectation |
   |---|---|---|---|
   | **ESP32-S3** | `heltec_v3` · `gateway_heltec` · `heltec_mobile` · `xiao_esp32s3` · `gateway_esp32s3` · `xiao_esp32s3_mobile` | **compiled** | ⚠ **flash MAY move — attribute it**; ⛔ RAM must not |
   | **nRF52840** | `xiao_sx1262` · `gateway` · `production` · `xiao_mobile` | `#if`-excluded | ★ **byte-identical, no exceptions** |

   ⇒ ★ **`xiao_esp32s3` is non-OLED and STILL compiles the sleep arm** — precisely what the withdrawn wording got
   wrong. **Classify by the `board =` / `extends =` chain, never by name or panel.** Expect ~zero movement regardless;
   **RAM unchanged everywhere; an unexplained delta is a finding.**
6. ★★ **D2 explicitly:** `sizeof(Node)` **221880**; no timer.
7. **M2 — `docs/2026-07-31-bench-test-script.md`:** add the soak check, **stated in COUNTS not uptime**: headless,
   `sleep=auto`, **`slept` > 32,769**, no reset, ~10 h practical target; report `slept=` **plus** `wkarmfail=`,
   `wkbusy=`, `wksleepfail=` (attempts ≥ `slept`, so those three measure the gap); **preserve that build's
   `firmware.elf` and revision**; ⛔ **send no console byte until the final read** — one byte latches
   `g_host_present`, `slept=` stops, and the soak measures nothing.

**Report:** the new call site with `file:line` and both preserved conditions · the deletion at `:1030` and both
comments · **§2's three V1 checks, each with its evidence** · the return-value decision, called out · every wiring
check with its controls and match counts · native baseline → after · both probes · census · the four-step corpus
proof · per-board RAM/flash attributed · the D2 answer · the bench addition · exact final `git status --short` and
that **nothing was committed**.
⛔ **State plainly that no automated gate can confirm this fix, and that closure is the metal soak.**

**Stop and report rather than improvising if:** §2.1 shows `pd_option` is reset per sleep (⇒ candidate (a) is wrong) ·
§2.2 finds a second counter · the guard cannot be evaluated at your init site · `sizeof(Node)` moves · a corpus row
moves · or a board env shifts unexplained.
