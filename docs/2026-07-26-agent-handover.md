<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# AGENT HANDOVER — MeshRoute QA-coordinator (written 2026-07-26, at context end)

**Read in this order:** `CLAUDE.md` → `docs/CODE_GUIDELINES.md` → **`docs/2026-07-25-agent-handover.md`**
(the base layer: role, the 14 hard process rules, dispatch-brief pattern, doc map — all still valid) →
**`docs/2026-07-26-slice-gate-method.md`** (the gate every slice runs) → this file (today's state + queue) →
`simulation/BASELINE.md` (**the anchors — always read them THERE, never assume**).

---

## 0. ⚠ THERE IS AN AGENT IN FLIGHT RIGHT NOW

A coder is running **Wave-4 #2+#3: the `bad_freq` false-success fix + closing the `-Wswitch-enum` blind spot.**
Its brief is preserved at **`docs/2026-07-26-inflight-brief-switchenum.md`** (it was in a session scratchpad that
is now gone). When it reports:

1. **Re-run its gate yourself** — never accept its numbers (rule V1). The gate is in the method doc §D.
2. Expect these specific things in its report, and check each:
   - `gw_parse_err_str` (`src/firmware_config.cpp`) now gives **`bad_freq`** its own string and **`ok`** an
     explicit case, with the `default:` removed so `-Wswitch` guards it. Verify: `GwParseErr` has **14**
     enumerators; before the slice the function handled **12** with `default: return "ok"`, so a real refusal
     printed **`> gateway err ok`** — a false success. Reachable via `lib/core/node.cpp:178-179`
     (`gateway freq0=`/`freq1=` ≤ 0), printed at `src/firmware_config.cpp:358`.
   - **`-Wswitch-enum` census before → after** for `lib/core`+`lib/console` (was **6 warnings at 4 sites**) and
     for `src/` via the real `pio` build.
   - Its verdict on **whether `-Wswitch-enum` can be a permanent gate at zero, or needs an audited exception
     list.** That answer is the deliverable I most wanted — it decides whether the owner's
     warnings-are-blocking ruling is enforceable or aspirational.
   - ★ **`lib/core/node.cpp:894` — I never verified this one.** Its switch on `wire::cmd_of()` does not handle
     `EXT`. If `EXT` is deliberately ignored, an explicit no-op case documents it. **If a real frame type is
     being silently dropped, that is a PROTOCOL finding, not a warning** — I told the coder to stop and report
     rather than paper it with a case label. Treat that possibility seriously.
3. `-Wswitch` in `lib/core`+`lib/console` **and** in the board build must stay **0** (the previous slice took it
   60 → 0; do not let it regress).

---

## 1. Repo state

| | HEAD | uncommitted |
|---|---|---|
| `/home/staszek/MeshRoute` | **`c823a61` "plumbing"** | ★ **ONLY the in-flight slice's 3 files** — `lib/core/node.cpp`, `lib/core/node_mac.cpp`, `src/firmware_config.cpp` (exactly the sites its brief names) + these three new `docs/` files |
| `~/lora-universal-simulator` | **`ee06845` "dedup"** | `orchestrator/runtime/NodeRuntimeWrapper.cpp` (3-B items 6+7) |

⚠ **The owner committed `c823a61` "plumbing" WHILE this handover was being written** — it swept in 3-B item 9's
12 `lib/core` files, `src/fw_main.cpp` (Wave-4 #1), `simulation/BASELINE.md` and the contract update, all of which
this document's earlier drafts listed as uncommitted. **So everything QA-gated through Wave-4 #1 is now
committed**, and the only firmware diff in the tree belongs to the agent still running. Re-check `git status`
before trusting any state list, including this one.

**All uncommitted work is gated GREEN.** The owner commits; never offer to (D4). The owner commits **mid-slice**
fairly often — when HEAD moves under a coder, diff against the true pre-slice parent, not `HEAD`. This very
handover is an example of why: the table above was stale within minutes of being written.

**Current anchors: the `2026-07-25j` era.** s18 keystone **`c9167d30`/271244**, delivery **{104, 99, 100}** for
seeds {1,42,100}. Native **854 / 26482 / 0**. `sizeof(Node)` **220584**. Full anchor list + all forensics in
`BASELINE.md`'s dated notes (25j is the era; 25k…26r are today's slices). ⚠ The header paragraph of BASELINE
item 3 still contains **superseded** historical values — the current list is in the **25j note**, further down.

---

## 2. THE QUEUE

### Wave-4 (owner ruling 2026-07-26: **NOT deferred to post-bench** — active now)

| # | item | who | gate class |
|---|---|---|---|
| ~~1~~ | ~~PushKind holes~~ | — | ✅ GO (`-Wswitch` 60→0) |
| **2+3** | **`bad_freq` + `-Wswitch-enum`** | **IN FLIGHT** | byte-inert for `src/`; the 4 `lib/` sites are compiled by `lus` |
| 4 | CR axis unplumbed end-to-end (no `simSetRxCr` exists at all) | coder | likely byte-inert (corpus is all `cr:5`) |
| 5 | Per-node frequency (sim has only a global `frequency_mhz`) | coder | schema addition, inert until used |
| 6 | Oracle catch-all — `NodeRuntimeWrapper.cpp:609`, **11 of 14 push kinds render `"send_failed"`**, inflating that field ~47× | coder | ★ **MOVES STREAMS** → re-anchor |
| 7 | `PreambleDetected` BW gate (SF-only today; sharpened by F-BW-TX) | coder | ★ **MOVES STREAMS** → re-anchor |
| 8 | **Fading activation** | ★ **QA (you)** | ★ **MOVES EVERYTHING** → re-anchor + likely retunes |

★★ **FADING ACTIVATION — read this before planning it.** It needs **zero code changes**: `snr_coherence_ms`
already defaults to 0 (the measured answer), and `snr_std_dev` is **REQUIRED per link** (a NaN sentinel makes the
run refuse to start — deliberate fail-loud from the 2026-07-20 review, so an omitted value cannot silently
disable fading). So there is **no default to flip**: all **4533 links across 28 scenario files already carry an
explicit `0`**, and activation means editing them to **0.35**. Scenario JSONs are QA-owned ⇒ **this is your work,
scripted, not a coder's.**
⚠ **It is not a mechanical sweep.** BASELINE records extensive tuning to hit specific SNR tiers (s27's phase-F
voluntary re-home needs a weak −5 dB home vs a strong +11 dB candidate; s28's F-PS-1 needs the weak-tier 60 s
check-alive). Tier boundaries are **−12 / −4 / +4 dB**, so ±0.35 dB on a link sitting near one can flap it — the
SNR-ceiling era did exactly this and produced 13 interim reds in s27 alone. Expect a **re-anchor + retune event**.
It will also be the **first time the fading code path is live**, i.e. the first real exercise of the §6.1.3(A)
verdict-cache slice, which has been correct-in-advance and dormant.
**The measured parameters** (v1, from real logs — method + the four parser bugs found while validating are in
BASELINE **25k**; tool is `tools/fading_from_logs.py`): **σ = 0.35 dB globally, coherence 0.** 19 links, 1515
samples, σ mean 0.355 / median 0.368, stdev 0.083. Receiver 105 (`M2`) excluded as a **faulty unit** — the owner
confirmed it was static, so its 2.3× σ is not motion. ⚠ **No motion measurement exists yet**; mobile-regime σ is
owed from a future hardware session.

### Owed — tests & coverage
- ★ **Jitter-window assertions** — 2 per de-storm window (h_forward, rreq_forward, mobile-OFFER). Recipe proven
  in BASELINE 26r; **must assert a LITERAL** — asserting `protocol::h_forward_jitter_max_ms` is **vacuous**
  because poisoning the constant moves both sides. Neither jitter constant is pinned in
  `test_protocol_constants.cpp` at all.
- Native tests missing: mobile-OFFER stash→fire · `age_out_denied_ids` · PHY predicates + `mhz_to_khz` +
  `node_join.cpp:579`
- **10 telemetry sites verified by nothing** (named in BASELINE 26r)
- **`src/` has ZERO behavioural coverage** — neither gate compiles it. Tractable fix: extract console-text
  rendering into `lib/console` (which the native build *does* compile), which would also make `fw_main`'s push
  rendering testable and close that defect class in one place for both audiences.
- Drifted claim: `test/test_node_join.cpp:5` advertises coverage that does not exist

### Owed — small cleanups
- 3 truncating kHz→Hz sites → route through `khz_to_hz()` + a `static_assert` that both forms agree.
  **Owner-agreed: NO behaviour change** — `LORA_BW` is a compile-time literal and truncation differs from
  rounding for **0 of 10** standard LoRa bandwidths (only 22 of 4991 0.1-kHz steps break, none a real bandwidth).
- `[[maybe_unused]]` on `node.cpp`'s `next` and `node_join.cpp`'s `prior` — 20 device warnings from the
  **inverse** of the telemetry hazard (consumed only inside stripped telemetry)
- `node_beacon.cpp:253` — commented-out long-form corpse directly beneath its live `MR_EMIT` replacement
- `join_refused` window family — 5 sites share `_last_join_refused_ms`; a narrower `join_refused_window_take()`
- `send_layer`'s `0x`-hex parse is **dark** — 0 of 186 `send_layer` and 0 of 37 hash commands use it. Needs a
  scenario (or the 3-D bare-hex-vs-`0x` grammar ruling).
- **Sim repo:** 4 silently-skipping webapp tests · `t23`/`t24`/`t25` invalid fixtures · the s15 chain broken at
  its base · `translate_s15_gateways.py` unrunnable (hardcoded path to a `MeshRoute/simulator/` dir that never
  existed)

### Rulings open for the owner
- **`is_gateway` config key is inert** — 8 scenarios set it; `on_init` re-derives `is_gateway ≡ n_layers==2`.
  Keep or drop?
- **4 remaining unchecked NV saves** — parked behind the remote-admin redesign (below)

---

## 3. Owner rulings made 2026-07-25/26 (binding; do not re-litigate)

1. ★ **Wave-4 is ACTIVE, not post-bench** — reverses the 2026-07-25 deferral of fading activation.
2. ★ **The Lua engine is DEPRECATED** — fail-loud refusal unless `allow_deprecated_lua`; default flipped to
   `meshroute`; **kept** as the frozen parity reference, no removal slice. (BASELINE 25e.)
3. ★ **Compiler warnings are GATE-BLOCKING** — `-Wswitch` at zero, no new warnings vs the **real `pio`** baseline
   (ad-hoc `g++ -fsyntax-only` counts differ and have already misled once).
4. ★ **Mark done-vs-missing IN CODE** — a partly-landed mechanism must state in-source what is done, what is
   missing, why deferred, and the hazard. Not a bare `TODO`; use the dated `§`-tag convention.
5. ★ **Remote-admin replay counter: REDESIGN, do not patch.** A monotonic counter over a lossy link cannot keep
   the operator informed (a lost admin DM desynchronises the pair and the *next* command fails with no warning).
   `§admin-replay-REDESIGN-OWED` is at the top of `remote_exec`. **The owner has since written
   `docs/superpowers/specs/2026-07-26-remote-admin-challenge-response-design.md`** — that item is in their hands.
6. **RadioLib pinned `7.7.1`** exactly (was `^7.6.0`; the tree held a 7.7.0/7.7.1 mix that skewed board
   baselines). Verified: the 3 lagging envs upgrade and build clean; the drift was costing +16 B flash on one env.
7. **The legacy sim test corpus is RETIRED** (112 files deleted, `s01` moved to `scenarios/deprecated/`), and its
   stale generators with it.
8. **6 `PushKind` holes → fixed** (Wave-4 #1) and the **oracle catch-all → to be fixed** (*"we need to know the
   reason, not just 'failed'"*).

---

## 4. Process lessons earned today — these are why the gates work

1. ★★ **THE POISON PROBE (method doc §E) — measure coverage, never reason about it.** Make the code under test
   return a wrong-but-valid value, rebuild, run all 27, record what moves; per site, so coverage is attributable.
   Then revert, prove the tree `diff`-identical, and re-run. **My inspection-based coverage claims were wrong
   twice** — on item 3 I asserted the corpus "genuinely exercises this slice" and the probe showed **1 of 13
   sites**. A probe that moves nothing is ambiguous (dead code vs weak probe) — distinguish them, e.g. at the
   object level.
2. ★★ **"A sweep is only as good as its scope, and the scope is never stated in the finding."** **Five
   instances:** 3-A's unchecked-save sweep was *file*-scoped (found 1 of 5) · 25m's `-Wswitch` sweep was
   `lib/`-scoped (missed a third list in `src/`) · my item-3 coverage claim · the `PushKind` holes only a **board**
   build reveals · and the newest — **the faulty scope was the FLAG**: `-Wswitch` is blind to any switch with a
   `default:`, which is how `bad_freq` survived. When a count says "the only" or "all N", establish what was
   searched.
3. ★★ **MR telemetry is SIM-ONLY by design** (device-stripped via `MESHROUTE_NO_TELEMETRY`; `MR_EMIT` expands
   *through* `MR_TELEMETRY`, so both strip identically). ⇒ **the oracle sees events hardware never emits.** This
   is why three enum→string defects shipped green: the sim showed the correct string while the app got the
   fallback. **When a gate is structurally blind to a change, byte-identity proves "no collateral damage" and
   nothing more — say so, and name the check that actually proves it.**
4. **Three automated-coverage blind regions, mapped:** `src/` (neither gate compiles it) · `lib/console` (the sim
   compiles none of it) · the sim wrapper (neither native suite references it).
5. **Refuse forced fits, loudly.** Coders correctly refused ~20 near-twins across the block (different quantity,
   an exemption, refuse-instead-of-evict, runtime-vs-compile-time cap). A forced fit is worse than a duplicate.
6. **Board baselines:** capture BEFORE **before dispatch**, in an **isolated `PLATFORMIO_BUILD_DIR`**, from a
   snapshot (`rsync` when the tree is dirty — `git archive HEAD` would attribute other slices' delta to you).
   I collided in the shared `.pio` tree once and destroyed a baseline.
7. **Prove `sizeof(Node)` POSITIVELY** — its `static_assert` is `#ifdef MESHROUTE_NATIVE`-guarded, so a bare
   `g++` that "compiles" proves nothing. Two coders hit that trap.
8. **The 3-B block bought invariants, not lines** (cumulative ≈ −250; the review's per-item savings were
   disproven five times out of six). Recalibrate expectations for any remaining dedup.

---

## 5. What was completed 2026-07-25/26 (context for BASELINE's notes)

Sim realism: BW-mismatch gating + the F-BW-TX TX-bandwidth mirror · the Lua deprecation · the harness
`send <name>` layer-resolution fix (closing a bug where a dual-layer gateway's alternating id made correctness a
function of *when* a command fired) · the delivery tool's fail-loud denominator and layer-aware keying (it had
been **hiding real failures in five scenarios**) · legacy-corpus retirement · one-source-of-truth de-forking of
the webapp's delivery analysis · the §6.1.2 turnaround plumbing, whose prerequisite fix (`§rx-window-arming`)
turned out to be the real story — **every reply leg of every handshake had run with zero RX→TX turnaround**, and
fixing it *improved* delivery (s18 88→99/113, s17 26/30→**30/30**).
**Then the 3-B dedup block, 9 of 9 complete**, every item byte-identical with ΔRAM +0 on every board.
Then Wave-4 #1 (`PushKind`), and #2+#3 in flight.

Fading was **measured on metal** (BASELINE 25k) and the v1 parameters are ready but **not applied** — see §2.
