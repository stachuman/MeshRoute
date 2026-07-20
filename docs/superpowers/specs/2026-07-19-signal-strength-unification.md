# Signal-strength unification — ONE canonical link-quality scale (2026-07-19)

**Status: RATIFIED by the user 2026-07-19 (questions 1–4 all "ok"). Slice A dispatched.**
Author of record: QA session; user rulings inline. Related: `2026-07-17-cross-layer-mobile-first-contact-design.md`
(§S6/D11 presence tiers — superseded in calibration by this spec), BASELINE.md 2026-07-19f.

## 1. Problem

The codebase carries FOUR signal-strength mechanisms; three agree on a physically realistic scale, one does not,
and the simulator hides the disagreement:

| # | Mechanism | Scale | State model | Verdict |
|---|-----------|-------|-------------|---------|
| 1 | DV route quality — beacon **4-bit bucket** (`bucket_of_snr_4b`, node_beacon.cpp:29; Lua dv:829-838) | −20…+11 dB, 2 dB bins, clamped | overwrite-on-merge (`rt_merge`) | on-scale — UNTOUCHED |
| 2 | ACK reverse-link **2-bit bucket** (`bucket_of_snr_2b`, node_mac_rx.cpp:21; Lua dv:842) | boundaries −12/−4 dB | per-exchange | on-scale — UNTOUCHED (packed but currently unconsumed — "wired at R3"; dormant-but-conforming) |
| 3 | SF selection (`select_data_sf_for_snr`, protocol_constants.h:565) | per-SF demod floors + margin | instantaneous | the physical anchor — UNTOUCHED |
| 4 | **Presence tiers** (`presence_quality_tier`, protocol_constants.h:476; §S6/D11) | **0/20/40 dB absolute** | EWMA α=5/16 (×3 copy-pasted sites) | **THE OUTLIER — recalibrate (Slice B)** |

Real SX126x hardware never *reports* SNR above ~+10…+13 dB. Mechanism 4's "ok" (≥20) and "strong" (≥40)
are unreachable on metal → every real link reads weak/critical → the §S6.4-C voluntary re-home rule
(candidate ≥2 tiers better) can effectively never fire.

**Why the sim never caught it:** `PathLossModel.cpp:69` computes `snr = rx_dbm − noise_floor` with **no
receiver-report saturation** — co-located links read 14…55 dB (s27) and up to **+82 dB** (s18, 5116 reports
above +12). Mechanisms 1–2 are immune (their wire buckets clamp); mechanism 4 feeds the raw EWMA into
absolute thresholds and inherits the inflation. s27's phase F encodes the inflated scale directly
(weak link `snr 14` vs strong `snr 55`).

## 2. The canonical scale (user-ratified)

**SNR in q4 dB *as a real SX126x reports it*: saturated at a bench-tunable ceiling (default +12 dB),
quantized to 0.25 dB (= q4 granularity), meaningful window −20…+12 dB.** The per-SF demod-threshold
table is the physical reference. One coarse boundary family for quality quantization: **{−12, −4, +4} dB**
(the ACK bucket's existing −12/−4 extended by +4 — ONE family, ACK wire bytes unchanged).

**Non-goals (user-confirmed): the static wire does NOT change** — beacon 4-bit bucket, ACK 2-bit bucket,
demod table, route-score model all stay byte-identical in firmware. Unification = bringing the one outlier
onto the scale the static plane already defines + centralizing shared helpers so a fifth consumer can't drift.

## 3. Slice A — sim realism (sim repo; delivery-parity gate; keystone re-anchor USER-BLESSED)

**Change:** a REPORT-only saturation at the radio→firmware boundary (`FirmwareNode::onRecv` →
`NodeRuntimeWrapper.cpp:115` builds `RxMeta`): clamp the *reported* `snr_db` to `min(snr, ceiling)`
(ceiling default **+12.0**, sim-configurable; an escape value disables for A/B debugging) and **quantize to
0.25 dB steps**. Applies to every SNR the firmware sees (onRecv + preamble-detect). **Delivery/collision
physics stay on the true channel SNR** — we model what the chip *tells* the firmware, not the channel.

**Expected consequences:** every stream shifts, **including the s18 keystone `3ac88d40` — retired, first
re-anchor of the arc (user-blessed 2026-07-19)**. s18 forensic signature = top-compression of route scores
(the −11.76…+82 spread flattens at +12; many routes become score-equal at the ceiling). SF selection starts
exercising realistically (today every link clears every SF). **Known INTERIM reds until Slice B** (old
0/20/40 thresholds + saturated ≤+12 ⇒ every tier reads weak): s27 phase F's voluntary re-home
(`mobile_adopted home:106`) and any tier-delta-dependent assert — enumerated at A-QA, documented in
BASELINE, restored by B.

**Gate (A):** multi-seed s18 delivery parity (≥101/113, leaks 0, seeds {cfg,1,42,100}) · full delivery
table no-regression · 3-run md5 stability before ANY re-anchor · all mobile scenarios 0-fail EXCEPT the
enumerated interim tier reds · native suite untouched (firmware unchanged) · BASELINE full re-anchor with
a prominent keystone note. Scenario JSONs are QA-owned — the coder does NOT edit them.

⚠ Sim repo has PRE-EXISTING uncommitted mods (CMakeLists/SimRadio/JsonConfig/FirmwareNode) — NOT ours,
never revert; additive edits only.

### 3b. Slice A OUTCOME (2026-07-19, QA-gated — BASELINE 2026-07-19g)

Implemented per spec (`SnrReport.h` pure shaper; choke point FirmwareNode onRecv+onPreambleDetected; config
`simulation.radio.snr_report_ceiling_db` default +12, huge=escape hatch; quantize round-to-nearest 0.25 always-on;
physics untouched — escape-hatch s27 reproduces the pre-A stream byte-exactly). New keystone s18 `ed1e3980…`/251996,
delivery 99/113 (0 giveup, 7 in-flight) — **pending user ratification vs the old ≥101 floor**; s17 improved 26→29.
**FINDING:** the interim reds exceed the phase-F prediction (s27: 13, s28: +4) because s27/s28 were authored
DEGENERATE — nearly all links at snr 55.0 → all reports tie at +12 → route-score discrimination collapses →
fragile multi-hop chains flap on tie-order. Scenario-authoring artifact, not a protocol regression. ⇒ the Slice-B
scenario retune covers ALL s27/s28 links (realistic spread, e.g. strong +8 / mid +2 / weak −8), not just phase F.
Follow-up candidate (not a blocker): route-score tie-break stability at the report ceiling — dense metal
deployments genuinely saturate many links.

## 4. Slice B — presence onto the shared scale (firmware; s18-inert vs the NEW keystone)

1. `presence_quality_tier` boundaries → **critical <−12 · weak −12…−4 · ok −4…+4 · strong ≥+4**
   (replace `presence_q_*_min_q4`; keep them bench-tunable constants).
2. Centralize in a protocol_constants "link quality" section: the canonical report window/clamp, the ONE
   EWMA helper (α = 5/16 — today copy-pasted at node_join.cpp:615, node_mobile.cpp:327, :372), the
   {−12,−4,+4} boundary family. Pure refactor for existing users (byte-identical firmware behavior
   except the tier remap).
3. QA-owned scenario retune rides B: s27 phase F links onto the realistic scale (weak home ≈ −8 dB,
   strong candidate ≈ +8 dB) so the tier machinery is actually exercised in sim.

**Gate (B):** s18 byte-identical at the NEW (post-A) keystone — the s18-inertness proof survives ·
native suite (tier unit tests recalibrated) · interim tier reds flip GREEN · full scenario suite 0-fail ·
boards. Mobile/presence wire: the tier is 2 bits either way — no frame change.

### 4b. QA scenario retune — DONE pre-B (2026-07-20), and ★ FINDING F-PS-1

Retune landed (QA-owned, on the A build): **s27** — per-pair deterministic spread +6.0…+11.75 (0.25 grid,
symmetric), phase-F overrides M5–S3 = **−8.0** / M5–SX = **+11.0** → **0/30 failures** (`fa32d014`/10354, ×2
stable; phase F fired via the critical-tier path under the OLD thresholds — Slice B must show the
sustained-better path). **s28** — tight-high spread +10.0…+11.75 (matches the dense-strong authoring intent;
a wide +6 spread added 2 unrelated timing fragilities) → exactly **6 documented reds** (`ac28521d`/2316, ×2):
the 2 pre-existing F-TR DM-by-hash gaps + 4 NEW-named **F-PS-1**.

**★ F-PS-1 (plane-separation, GENUINE — exposed by realistic SNR, root-caused 2026-07-20):** a team mobile
that loses its home goes MUTE on the team plane until it re-registers. Chain: realistic weak-link tier →
check-alive T=60 s (correct, by design) → `presence_home_lost` → `mobile_reset` → `set_identity(0)` → the
**static unprovisioned guard `if (_node_id == 0) return err_unprovisioned` (node.cpp:930) sits BEFORE the
§S7 plane-select**, so `-t` TEAM sends are refused although the team identity (team_local_id, _rt_team) is
intact and §18 makes team membership home-independent. (Pre-A this was invisible: inflated SNR → tier
strong → T=480 s → home death went undetected inside the scenario window.) Likely also affects `send -t`
team DMs while searching — verify at fix time. Fix shape (own slice, NOT B): plane-select before the
unprovisioned guard — a team member's `-t` path gates on its TEAM identity; also audit what origin/ctr a
homed member's team originations use across the reset. s28's ch5 asserts stay as the documented carve
until it lands.

### 4c. Slice B OUTCOME (2026-07-20, QA-gated GO — BASELINE 2026-07-20 note)

Landed per spec: tiers {−12,−4,+4} (ACK-family extension documented in-code), the canonical link-quality
section + `snr_ewma_step`/`snr_ewma_update` helpers (per-site seed semantics preserved, unit-proven
bit-identical), native 817/26088, **s18 byte-identical at the new keystone** `ed1e3980`. QA post-B scenario
finalization: the sim's SF8 demod floor (−10 dB) + fading makes −8 dB links genuinely lossy → weak-home
links set to **−5.0 dB** (weak tier, real margin). s27 `d7632d3d`/9837 **31/31** with a NEW assert pinning
the **voluntary S6.4-C path** (`presence_rehome` observed — previously exercised nowhere in the suite);
s28 `b4796e3a`/2412 **35/41** = exactly the 6 finding-reds (F-PS-1 ×4 restored via genuine home-loss
detection; F-TR ×2). The S1/S2 zero-pull containment asserts were over-strict (legitimate static digest-repair
pulls now occur on lossy links) → narrowed to not-contains on the team-minted id families. **The unification
arc's firmware half is COMPLETE: every signal-strength consumer now lives on the one canonical scale.**

## 5. Slice C — per-node RNG streams (QUEUED, not dispatched)

The other sim-realism finding (BASELINE 2026-07-19d): the shared mt19937 draw-order coupling. Own slice,
own delivery-parity gate + full re-anchor. Deliberately NOT mixed into A — two stream-changing causes in
one re-anchor makes forensics unreadable.

## 6. Order & process

A → B → C, sequential dispatches (user-ratified). A's re-anchor lands first so B's s18-inertness is proven
against the new keystone. Coder implements; QA gates independently (incl. a reverted-change base build where
forensics need it); the USER commits both repos and bench-verifies on metal.
