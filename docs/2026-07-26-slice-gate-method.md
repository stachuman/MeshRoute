<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# THE SLICE GATE METHOD — shared boundaries + gate for every dispatched slice

★ **PRESERVED FROM A SESSION SCRATCHPAD 2026-07-26.** This is the file all nine 3-B dedup slices plus the
Wave-4 slices were gated against. It is written in 3-B terms but is **generally applicable to any coder slice** —
future dispatch briefs should say "read `docs/2026-07-26-slice-gate-method.md` first" instead of restating it.
Its §E (the poison probe) and §C (inventory-is-suspect / refuse forced fits) are the two hardest-earned parts.

Every remaining 3-B slice references this file so the gate cannot drift between briefs. Your **item brief**
carries the mandate, the inventory and item-specific hazards; **this file** carries everything common.

## A. Role & boundaries

- **Implementation coder**, ONE slice. I am the QA-gate coordinator; the owner (Stanislaw) commits and rules.
- **NEVER run `git commit`/`add`/`stash`/`checkout --`, or offer to** (rule D4). Leave work uncommitted.
- **QA-owned — do NOT touch:** `simulation/*.json`, `simulation/BASELINE.md`, `tools/*`, all `docs/*.md`,
  `ios-companion/INBOX_SYNC_CONTRACT.md`, `dumps/`. **You do NOT re-anchor** — a refactor moves nothing, so
  there is nothing to anchor. If a stream moves, STOP and report.
- ⚠ Slices land back-to-back. **Read the tree as you find it, not as any document describes it.** Never revert
  or tidy another slice's work. The owner sometimes commits mid-slice — if HEAD moves under you, diff against
  the true pre-slice parent, not `HEAD`, and verify any intervening foreign change is unrelated to yours.
- Keep a **mine-vs-theirs ledger**.

## B. Read first

1. `CLAUDE.md` — **★ C1 (refactor XOR feature: these are PURE REFACTORS)**, **U1 (extend, don't fork)**,
   **U3 (match the surrounding idiom; `fw_main.cpp` stays board/runtime glue, feature logic in `firmware_*`)**,
   **C2 (fail loud)**, V1, D1/D2/D3.
2. `docs/CODE_GUIDELINES.md`.
3. `docs/2026-07-20-realism-and-duplication-review.md` — PART 3 §3-B (your item's mandate).
4. `simulation/BASELINE.md` — **read the anchors THERE.** They were re-anchored on 2026-07-25 (`25j`) and notes
   are appended continuously; anything you remember from elsewhere is stale.

## C. ★ THE INVENTORY IS SUSPECT — verify it, and report what you find

The review's counts are from a 2026-07-20 audit and the tree has moved. QA has re-measured several items and
**both the review and QA have been wrong, in both directions**:

- item 1: QA listed 6 rings; there were **7**. QA said 3 age-out sweeps; there were **2**.
- item 3: QA "corrected" the review's 8 rounding sites upward to 14 — **the review was right**; a text-grep had
  lumped three different conversions together.

So: **re-count your item's sites yourself and report the true numbers.** A premise of mine you disprove is a
valuable, reportable result — several of this session's best outcomes were coders disproving their brief.

**Refuse forced fits.** If a candidate site shares the *shape* but differs in *meaning* (a different quantity, an
exemption, a refuse-instead-of-evict policy, a runtime-vs-compile-time cap), **leave it and say why.** A forced
fit is worse than a duplicate. Item 1 correctly refused four such near-twins.

## D. ★ THE GATE (identical for every 3-B slice)

```
# firmware native — the pio wrapper LIES ("0 test cases"); RUN the binary:
cd /home/staszek/MeshRoute && pio test -e native && ./.pio/build/native/program
# sim (produces the scenario streams; do not edit that repo):
cmake --build /home/staszek/lora-universal-simulator/build --target lus -j8
cd /home/staszek/lora-universal-simulator
./build/orchestrator/lus -e meshroute /home/staszek/MeshRoute/simulation/<s>.json /tmp/<s>.ndjson
# boards, into an ISOLATED build dir (see 4 below):
cd /home/staszek/MeshRoute && PLATFORMIO_BUILD_DIR=<scratch>/piobuild-after pio run -e <env>
```

1. **All 27 corpus scenarios BYTE-IDENTICAL** to the current BASELINE anchors — the 19 anchored plus the 8
   non-anchored (s06, s07, s20, the three `*_leaf_config`, `sim_9node_base`, `twin_9node_dm`). **`cmp` the full
   NDJSON**, not md5 prefixes. **0 assertion failures each.** 3-run stability on a few of the largest.
2. **Native count EXACT and UNCHANGED** (**854 / 26482 / 0 as of 2026-07-26** — it moved twice during this
   block, so **measure your own BEFORE** and cross-check BASELINE rather than trusting any figure written here). A pure
   refactor adds no tests and removes none. If your slice legitimately needs a new test, say so LOUDLY in
   DEVIATIONS and give the new numbers.
3. **`sizeof(Node)` unchanged** (220584 at time of writing — read `node.h`'s `static_assert`). ⚠ **Prove it
   POSITIVELY:** that assert is `#ifdef MESHROUTE_NATIVE`-guarded, so a bare `g++` that "compiles" proves
   NOTHING (an earlier coder hit exactly this trap and self-corrected). Use
   `-DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2 -std=gnu++2a`, and prefer a template-reveal or an independent
   `static_assert` that you can see fire.
   ★ **Layout lesson from item 1:** wrapping an existing array+count pair in a *member class* gives it its own
   tail padding and MOVES `sizeof(Node)`. **Non-owning free-function templates over the existing members**
   (`Entry (&ring)[Cap]`, `uint8_t& n`) keep layout identical *by construction*. Prefer that shape.
4. **Boards 10/10 with a BEFORE/AFTER RAM+Flash table.**
   - ★ Build **each** pass into an **isolated `PLATFORMIO_BUILD_DIR`** under your scratchpad. A concurrent
     session collided in the shared `.pio` tree on 2026-07-25 and destroyed a baseline; isolation is standing
     practice now.
   - ★ Derive the BEFORE tree from a **pristine `git archive` export**, never by reverting the working tree
     (that would flip sources under any concurrent build, and risks losing your own work).
   - **RAM must not move.** Flash may shift either direction if you explain it — item 1 saw every nRF52/ARM env
     shrink 3.4–4.1 KB while every ESP32/Xtensa env grew 0.25–0.5 KB, purely from inlining differences.
   - Note the three `gateway_*` envs compile `MR_FEAT_TEAM 0`, so `#else` arms of team-gated code ARE covered.
5. **★ WARNINGS ARE GATE-BLOCKING** (owner ruling 2026-07-25):
   - **`-Wswitch` must be 0** across `lib/core/*.cpp` + `lib/console/*.cpp` (zero-tolerance — this is the
     contract-bug class that shipped three enum→string defects).
   - **No NEW warnings of any class.** Capture the baseline from the **real `pio` build** (an ad-hoc
     `g++ -fsyntax-only` sweep gives different counts because the PlatformIO defines are absent), before and
     after, and report the delta. Do **not** clean up pre-existing warnings — that is its own slice.
6. **Net line delta**, so the dedup claim is measurable.

## E. ★ When the gate is structurally BLIND, say so

Byte-identity only proves something about code the corpus RUNS. Known blind spots:
- the sim's `push` emit carries only `ctr`/`dst`/`kind` — **never the `reason` string**, so no enum→string or
  push-reason change can ever move a stream;
- console/NV/board-only paths are not exercised by scenarios at all;
- long-TTL sweeps may never fire inside a ≤30-minute run (item 1's `age_out_denied_ids`).

For any converted site the corpus does not cover, **name it and say what does cover it** (a native test, or
algebra). Never present byte-identity as validation of something it cannot see. If nothing covers it, say that
plainly — an honest gap beats a false claim.

### ★★ THE POISON PROBE — MANDATORY. Measure coverage, do not reason about it.

**Do not decide by inspection which sites the corpus exercises.** QA has now been wrong about this twice, most
recently on item 3: I asserted the corpus "genuinely exercises this slice" because two converted sites sit in
the protocol engine the simulator runs. The item-3 coder measured it instead and found byte-identity covered
**exactly 1 of 13** converted sites — one of the two I named was never executed at all, and one whole helper had
**zero** corpus coverage. *Compiled ≠ executed.*

**Method** (cheap, and it is the only honest answer to "does the gate see this?"):
1. Temporarily make the extracted helper — or an individual converted site — return a **wrong but valid** value
   (e.g. `+7`, a flipped boundary, a different enum). It must be a change that *would* alter output if executed.
2. Rebuild `lus` and run all 27 scenarios.
3. Record which scenarios move. **Probe per helper, and where feasible per site**, so coverage is attributable
   rather than lumped — item 3 isolated `node_join.cpp:579` (not executed) from `node_mac_rx.cpp:838` (executed)
   exactly this way.
4. **Revert every probe, confirm the tree is `diff`-identical to what you gated, and re-run all 27** to prove the
   restoration. Item 3 did this; say that you did.

**Report a coverage matrix**: per site → corpus-covered (proven by probe) / native-test-covered (name the test) /
**algebra only**. A site in the third bucket is not a failure — it is an honest gap, and naming it is the point.
If a probe shows a site you expected to be covered is not, **that is a finding worth more than the refactor.**

⚠ A probe that moves **nothing anywhere** means either the site is dead code or your probe was too weak to be
observable — distinguish those two before concluding, and say which.

## F. Report structure (deviations LOUD)

```
INVENTORY CONFIRMED — your item's true counts at file:line; correct the brief where it is wrong
DESIGN              — the shape chosen + why; how layout/idiom constraints are met
COVERAGE            — ★ the POISON-PROBE matrix (§E): per site → corpus-covered (probe result) / native-test
                      (name it) / algebra-only. Plus confirmation that every probe was reverted and all 27
                      re-run clean afterwards.
GATE                — native (EXACT count) · 27 scenarios byte-identical · sizeof(Node) proven POSITIVELY ·
                      -Wswitch 0 + pio warning delta · boards 10/10 BEFORE/AFTER (isolated dirs) · line delta
DEVIATIONS          — ★ anything done differently, LOUDLY, with rationale
MINE-VS-THEIRS      — your hunks vs whatever else is uncommitted
FILES TOUCHED       — full list, both repos
```

Report honestly (D3): real output for failures, declare every skip and why, never claim a number you did not
personally reproduce.
