<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# ★ STATE / HANDOVER — 2026-08-16 18:48 local · written for context compaction

**Read this first.** Everything below is durable repo state; nothing important lives only in the conversation.

---
## 1 — ✅ RESOLVED 2026-08-17: THE SOAK CAUGHT [[B196]] AND THE ROOT CAUSE IS PROVEN

**Outcome: panic at `ran 9h02m29s`, backtrace captured, cause identified — `int16_t` overflow of the IDF
power-domain refcount, driven by our own unpaired `esp_sleep_pd_config(..., ESP_PD_OPTION_ON)` at
`src/fw_main.cpp:1030`.** Full evidence, arithmetic, the negative control and the three unruled fix
candidates are on the **[[B196]] row of `docs/2026-07-30-open-bug-register.md`** — that row is the authority,
not this file.

★ **The archive discipline paid off immediately:** the panic printed `ELF file SHA256: d964a5239` and
`~/MeshRoute-artifacts/soak-20260816-1646/firmware.elf` hashes to `d964a5239b568…`. **Provenance proven, so
the app frames decoded reliably** — the first capture in this arc where that was true. ⇒ **keep archiving the
ELF at flash time.**

⛔ **B196 is NOT closed.** ★ **The criterion is COUNTS, NOT UPTIME** (a wall-clock bound was written here first and
is withdrawn on the register row): a fixed image must survive headless with `sleep=auto` and **`slept` > 32,769**,
no reset — ~10 h is the practical target. **No fix is built; the shape is the owner's ruling.**

<details><summary>Historical — the soak parameters as written the night before</summary>

### the [[B196]] overnight soak

| fact | value |
|---|---|
| flashed | **2026-08-16 18:43:50**, env `heltec_mobile`, banner `a1e53dd-dirty` |
| powered on | **~18:48 local**, `last reset: POWER_CYCLE` |
| ⚠ **the ~9h window lands** | **~03:45–03:50 local** (every prior panic: **8h58m38s – 9h03m55s**) |
| monitor | `./meshroute_client.py -p /dev/ttyUSB0 monitor` — ⛔ **listen-only, no DTR reset, sends nothing** |
| the arm being tested | ★ **HEADLESS / SLEEPING** — the one that has never been cleanly run |

⛔⛔ **DO NOT SEND A SINGLE BYTE** until the morning read. One byte latches `g_host_present`,
`slept=` stays 0, and the run measures the awake arm instead — **which is exactly what happened on the
previous attempt** (`uptime_ms=33914064` ≈ 9h25m, `slept=0`, `sleep=off-host`).

ⓘ **`-dirty` is DOCS-ONLY.** The only uncommitted file at build time was
`docs/2026-07-30-open-bug-register.md`. **The binary corresponds to `a1e53dd`'s code.**

### Morning procedure, in this order
1. **the log** — `Guru Meditation` (panic + decodable backtrace) or a banner reading `last reset: WATCHDOG`;
2. **`faults`**; 3. **`status` ONCE**.
- **`slept=` large + `sleep=auto`** ⇒ the headless arm really ran. **`sleep=off-host`** ⇒ measured nothing new.

### What each outcome means
- **Panic ~9h02–9h04 with `slept=` large** ⇒ real and **sleep-related**; decode it (§2).
- **Survives past ~9h30 with `slept=` large** ⇒ with the previous night's awake survival, **both arms clean on
  this firmware** ⇒ B196 becomes *"not reproducible on the current build"* — ⛔ **still not a closure**.
- **`sleep=off-host`** ⇒ retry.

</details>

---
## 2 — DECODE ARTEFACTS

**★ Archived AT FLASH TIME for this soak** (the standing fix; two earlier captures were lost without it) —
the operator's `~/MeshRoute-artifacts/soak-<stamp>/` holding `firmware.{elf,map,bin}` + `COMMIT.txt` +
`SHA256SUMS`. **That triple is the correct decode target for tonight's run.**

⛔ **`~/MeshRoute-artifacts/b196/` and `b200/` were DELETED 2026-08-17 on an owner instruction** — B200 had closed with
its backtrace already decoded, and `b196/`'s ELFs could never decode anything (see below). **The surviving archive is
`soak-20260816-1646/` only**; the keep/delete rule now lives in `docs/2026-07-31-bench-test-script.md` §archive step 5.
ⓘ **The measurement that justified deleting `b196/` is kept because it is the durable lesson —
A REBUILD IS NOT THE SAME IMAGE:** `b8929e5` rebuild 1276784 B and `473581f` rebuild 1276720 B against a **flashed
1275984 B**; both rebuilds *larger*, cause **NOT KNOWN**, guessing forbidden. ⚠ **[[B206]] is NOT the explanation** —
the two-`__DATE__`-TU effect is ~16-32 B and cannot account for ~800; it may contribute, the bulk is unexplained.
⇒ for those (now-deleted) captures the split had been: **IDF/ROM frames
(`0x4037xxxx`, `0x4038xxxx`, `0x400xxxxx`) are RELIABLE** (identical
`framework-arduinoespressif32-libs @ 5.3.0+sha.489d7a2b3a` blob on both machines, verified from the
operator's build log); **MeshRoute frames (`0x42xxxxxx`) are NOT.**

```
~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -pfiaC -e <elf> <addrs…>
```
⓵ **Pipeline proven working 2026-08-16** — it resolved [[B200]]'s old frames to `gpio_isr_loop` /
`_xt_lowint1` / `panicHandler` / `xt_highint4`, recorded on that row as post-closure confirmation.

---
## 3 — CODE STATE
**HEAD `a1e53dd` "UI rework - ready for testing"** — the whole four-slice OLED chrome redesign is COMMITTED.
Working tree carries **only `docs/2026-07-30-open-bug-register.md`** (the B200 confirmation + [[B204]]).

**Completed and committed this session:** §B197/§B198 (sleep/wake + prompt repaint, both CLOSED on metal) ·
§B200 (GPIO wake arming, CLOSED on metal after five rounds) · §CHROME-1…4 (pure projection · canvas
primitives · status strip + invalidation · rail, badge and the 19-column body migration).

---
## 4 — WHAT IS OWED NEXT (nothing is blocked on me)
1. ⛔ **The chrome redesign has ZERO metal validation** — bench **Parts 24 and 25** are written and waiting.
   §12.11 / 25.6's **`slept=` regression guard** matters most; Part 25.3 (emergency full-width) is
   **safety-relevant**.
2. **[[B196]]** — ✅ diagnosed 2026-08-17 (§1). ⛔ **Still owed: an owner ruling on the fix shape (a)/(b)/(c),
   the implementation, and the > 32768 s headless re-soak that closes it.**
3. **[[B199]]** — the two watchdogs; B200 gave them a mechanism and predicts they are now unreproducible.
   ⓘ Supporting evidence already: boots 82/91 (`ran 6s` / `ran 0s`) were the last of that shape.
4. **[[B193]]** — bench Parts 19/20, the NV/power-cut qualification, still untouched.
5. **[[B202]]** (four pre-existing clipped body lines) · **[[B203]]** (two instruments that could not fail) ·
   **[[B204]]** (the boot-scrub `E (…)` line, new tonight).
6. **Owner rulings owed:** whether a **stale mutation-runner baseline pin** is a registrable defect (raised at
   §CHROME-1, still unanswered) · [[B204]]'s option (a)/(b)/(c).

---
## 5 — STANDING RULES THAT MUST SURVIVE COMPACTION
- ⛔ **The QA-gate NEVER commits** (D4) and never runs `git add` / `git checkout --`. **The owner commits.**
- ⛔ **QG is dispatched by the OWNER**, not by me. I write briefs, verify claims at the code, and relay.
- ⛔ **Never claim an owner or QA approval that was not given; never quote an owner ruling — reported form only.**
- ⛔ **Never edit `simulation/BASELINE.md`'s `^### 36/36 corpus` anchor table.** ⓘ Its `s18_meshroute` row reads
  **`9868cad3` / 269905 and DOES reproduce**; `1cd21235` is only the *pre* column of a before/after table.
  **No re-anchor is pending or owed.**
- ★ **Instruments that cannot fail: EIGHT recorded in this arc.** The standing question that would have caught
  most of them: **when a check encodes an order, a call or a precedence, verify it against the code that
  actually executes — not against the design's prose.** And: **a mutation whose target line has MOVED measures
  nothing** — it must be reported `unusable`, never as a pass, so re-run whole batteries.
- ★ **Point-in-time claims decay.** "Verified clean" is true only at its timestamp; re-measure before relying.
