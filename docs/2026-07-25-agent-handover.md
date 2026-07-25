<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# COMPLETE AGENT HANDOVER — MeshRoute QA-coordinator role (written 2026-07-25)

You are taking over as the **QA-gate coordinator** for MeshRoute. This document is self-contained: with it +
`CLAUDE.md` + the repo docs it references, you can run the operation. Read `CLAUDE.md` and
`docs/CODE_GUIDELINES.md` first — they are the always-binding base layer; this document layers the current
state, the queue, and the session-earned rules on top.

## 1. Role & division of labor (non-negotiable)

- **You**: QA-gate + dispatcher. You write dispatch briefs, launch implementation coder agents (opus-class),
  independently re-run every gate they claim, adjudicate forensics, own ALL edits to `simulation/*.json`
  scenarios, `simulation/BASELINE.md`, `ios-companion/INBOX_SYNC_CONTRACT.md`, `docs/*.md`, and the memory
  handover. You present findings/options with the exact code state FIRST (rule P1), and never open with a
  recommendation on check/redesign/explore asks.
- **Coder agents**: implement firmware/sim slices per your brief. They never commit, never edit QA-owned files.
- **The user (Stanislaw)**: makes ALL git commits in BOTH repos, bench-verifies on metal, and makes design
  rulings. NEVER run `git commit` or offer to (rule D4). Leave green work uncommitted + report ready.
- Big/risky work: design spec first (`docs/superpowers/specs/`), user-reviewed before code (P2).

## 2. Hard process rules (each one was earned; violating them has burned us)

1. **Verify against code, never comments/docs/reports** (V1). Grep the codec/source. Coders' claims get
   independently re-run — never trust their numbers, reproduce them.
2. **Verify the file after every claimed edit** — a prior agent twice CLAIMED spec edits it never executed.
3. **Refactor XOR feature/fix, never both** (C1). Byte-identity is the refactor proof; forensics the fix proof.
4. **Fail loud** (C2): no silent defaults, no silent drops. Any path that can stall silently forever must gain
   a loud giveup + Push (the e2e-ack-deadline slice is the model).
5. **Plane separation §18**: separate STATE (id universes, `_rt` vs `_rt_team`, separate ledgers), same CODE
   (parameterize à la `rt_merge(table, count, team_plane)` — never fork). Team frames are leaf-AGNOSTIC (P2-1).
6. **Simulations as realistic as possible** (standing user rule, ratified repeatedly). When sim and metal
   disagree, metal wins; flip defaults to measured-real values even when it forces re-anchors.
7. **Duplicated code is a BIG problem** (user, verbatim: "it takes time and has to be done") — dedup slices are
   first-class scheduled work, not shelvable polish. An approved ranked list needs no per-item re-ask.
8. **Patience/timeout constants are DERIVED, not magic** — from airtime + turnaround × hop budget (the P-BUDGET
   pattern in `protocol_constants.h`); comment the derivation.
9. **Never kill a background agent on an ambiguous label** (user: "if unsure, don't do anything"). If a kill is
   truly needed: kill + SendMessage-resume preserves the transcript and all tree edits.
10. **Sequential slices only** — two in-flight slices corrupt each other's anchor baselines. One coder at a
    time; QA-gate between.
11. **Assert vacuousness check**: any `not_contains`/count-0 containment assert must be verified NON-vacuous
    (the guarded pattern actually occurs somewhere in the stream).
12. **Multi-seed discipline** for delivery claims; 3-run md5 stability before any anchor is recorded.
13. When a coder dies mid-flight (API errors happen): check `git status` for partial edits, then
    SendMessage-resume the SAME agent id (it keeps transcript + tree); re-dispatch fresh only if the tree is
    clean and the transcript is lost.
14. Scenario JSON gotchas: run via `lus -e meshroute` FROM `~/lora-universal-simulator` cwd (JSONs reference
    `scenarios/dv_dual_sf.lua` relatively; without `-e` the Lua engine runs and everything is garbage);
    raw tx/rx/drop events key on node NAMES, `script_emit` on numeric indices.

## 3. Repo state (as of this handover)

- **Firmware** `/home/staszek/MeshRoute`: **HEAD `8228b11` "e2e ack improvements"** — ⚠ the e2e-ack-deadline
  slice described below as uncommitted was COMMITTED by the user on 2026-07-25 09:24 (13 files incl. the QA
  BASELINE/contract edits + the s23/s27 seed re-pins). Tree was CLEAN at that point.
  *(Superseded text, kept for the audit trail: HEAD `2ba6405` "Mobiles fixing"; UNCOMMITTED = the
  e2e-ack-deadline slice — command.h, node.{h,cpp}, node_mac.cpp, node_mac_rx.cpp, protocol_constants.h,
  lib/hal/timer_wheel.h, test/test_node_e2e_ack.cpp — plus QA edits.)*
  **Current uncommitted (QA, 2026-07-25):** the turnaround 27→8 ms sweep — `simulation/s09_two_layer_gateway{,_metal}.json`,
  `simulation/s15_three_layer_metal.json`, `simulation/BASELINE.md`, `docs/2026-07-20-realism-and-duplication-review.md`,
  `lib/core/protocol_constants.h` (comment-only, proven inert), + this handover doc.
- **Sim** `~/lora-universal-simulator`: **HEAD `b7b8ab4` "Sim - towards reality"** (the Slice-C per-node RNG
  streams are COMMITTED). ⚠ STILL UNCOMMITTED: the **LBT-energy slice** — `LbtModel.{h,cpp}`,
  `JsonConfig.{h,cpp}`, `SimController.cpp`, `test_physics.cpp`, `test_wave2_config.cpp`. **The current anchors
  DEPEND on these 7 uncommitted files** — a fresh clone of the sim repo cannot reproduce `14808fd3` today. Flag
  this to the user at every opportunity: the firmware half of the realism era is committed, the sim half is not.
  The sim repo also carries the user's parallel-agent webapp/* edits — NOT yours, never revert.
- The user commits both repos; until then every gate runs against the working trees.
- The user runs PARALLEL agents sometimes (scenario edits can race your coders — a coder seeing "impossible"
  scenario states should re-check after the user's next commit before anyone panics).

## 4. The mandatory gate (run for EVERY change)

```
# firmware native — the pio wrapper LIES ("0 test cases"); run the binary for real counts:
pio test -e native && ./.pio/build/native/program        # current: 851 cases / 26327 assertions / 0 failed
# sim rebuild + scenarios (cwd + -e are mandatory):
cmake --build ~/lora-universal-simulator/build --target lus -j8
cd ~/lora-universal-simulator && ./build/orchestrator/lus -e meshroute \
    /home/staszek/MeshRoute/simulation/<s>.json /tmp/<s>.ndjson
python3 tools/dm_delivery_breakdown.py simulation/<s>.json /tmp/<s>.ndjson   # from the MeshRoute cwd
# boards (sequential): pio run -e <env> for: xiao_sx1262 heltec_v3 xiao_esp32s3 gateway gateway_heltec
#   gateway_esp32s3 production xiao_mobile heltec_mobile xiao_esp32s3_mobile
```

**Current anchors (2026-07-24b LBT-energy era — the authoritative copy lives in BASELINE.md's header; ALWAYS
read it there, never assume):** s18 keystone `14808fd3`/273283 · delivery s18 {97,88,99} 0-giveup 0-leaks ·
s19 8/8 · s15 7-seed XL 93.2% · s16 92.5% · s17 26/30 · mandatory s21 `f713d983`/657 · s22 `d2c2d079`/1724 ·
s23 `8fd96088`/795 · s24 `039352f6`/1354 · s25 `84ffe381`/557 · s26 `fa00b407`/712 · s27 `cc3baaf4`/9335 ·
s28 `9f2e956a`/3486 · s29 `a82d55a9`/1622 · s30 `00a96ca3`/823 — ALL 0 assertion failures, each.
`sizeof(Node)` = 220584 (static_assert in node.h). Timer wheel kCap 91, **0 free ids** — the next timer
feature forces a cap bump (trivial, but declare it).

**Gate classes** (pick per slice): (a) mobile/team/crypto firmware change → s18 BYTE-IDENTITY (the static-plane
inertness tripwire) + scenario anchors byte-identical-or-forensics; (b) routing/physics/behavior change →
delivery-parity (multi-seed, floors above) + full re-anchor with per-scenario forensics; (c) pure refactor →
byte-identity across the ENTIRE suite. Value-only shifts need a decomposition proof (strip the new events →
reproduce the old stream byte-exactly, or an emit-histogram diff).

## 5. Standing user rulings (binding; do not re-litigate)

- ★★ **THE LUA ENGINE IS DEPRECATED AND UNSUPPORTED (user ruling 2026-07-25, verbatim: "We do NOT support lua
  engine anymore… it is far behind firmware already").** Enforcement level chosen = the HARDEST tier: a Lua node
  is a **fail-loud REFUSAL** unless an explicit override opt-in is passed; the engine default flips
  `lua` → `meshroute`; plus the runtime warning and the doc/comment annotations. **NOT removed** —
  `scenarios/dv_dual_sf.lua` (12470 lines) + `ScriptedNode`/`LuaHost` (~950) are KEPT as the frozen parity
  reference the C++ port was validated against; no removal slice is scheduled. ⚠ Two facts that made this
  urgent: (a) Lua was the **default** (`JsonConfig.h:291`) and **389 of 722 nodes (54%) in `simulation/*.json`
  carry NO `engine` key** — including every node of s10/s16/s17 and the WHOLE mandatory s21–s30 suite — so the
  entire mobile/team suite was one forgotten `-e meshroute` away from silently running garbage (this already
  cost one agent a false-alarm all-red run); (b) the native tests `test_bw_mismatch` and
  `test_bw_tx_follows_retune` drive through the Lua `ScriptedNode` hook because `build_test.sh` cannot link
  `FirmwareNode` — so the override must be expressible IN CONFIG, not just as a CLI flag. Ruled dispositions:
  `test_bw_tx_follows_retune` is **DROPPED** (the dual-BW gateway probe carries the proof); `test_bw_mismatch`
  KEEPS its coverage by declaring the opt-in explicitly.
- ★★ **THE LEGACY SIM TEST CORPUS IS RETIRED, AND ITS STALE GENERATORS WITH IT (user rulings 2026-07-25).**
  Discovered while gating the Lua slice: the sim repo's own regression corpus had been **dead since the
  2026-07-21 Wave-1 required-keys change** (`simulation.radio.duty_cycle`, per-link `snr_std_dev`) and nobody
  noticed, because the project gates on MeshRoute's `simulation/` corpus + the two native suites. QA-measured:
  `test/t*.json` = **2 valid / 90 dead**; `scenarios/s*.json` = **13 valid / 20 dead**. Rulings: **delete** the
  dead `test/t*.json`; **retire (delete)** `test/run_tests.sh` + its 6 already-dead Lua-vs-meshroute
  differentials; **delete** the 20 dead `scenarios/s*.json`; **retire the stale generator tools**. ⚠ Carve-outs
  that bind: `test/t01_flooder.json` **survives** (`test_sim_controller.cpp` runs it and passes);
  `scenarios/s01_dv_dual_sf.json` **survives pending the owner's call** — `webapp/tests/test_sim_manager.py:16`
  + `test_simulations_router.py:18` resolve it as a real path and feed it to `lus`, and `webapp/` is the owner's
  territory; `scenarios/dv_dual_sf.lua` **stays** (frozen parity reference per the Lua ruling). ★ **Staleness of
  a generator MUST be decided by running it and validating the FRESH output — never by grep**: a grep of
  `duty_cycle`/`snr_std_dev` mislabels ≥3 tools in both directions (`gen_s18_singlelayer.py` greps "stale" but
  its artifact loads; `translate_*`/`inject_*`/`s15_route_convergence_sweep.py` are transformers/consumers that
  inherit their input's keys; `gen_s04_realistic.py` greps "stale" while its artifact is valid, i.e. the FILE
  was hand-migrated and re-running the generator would REGRESS it). Analysis/diff tooling
  (`analyze`, `dm_delivery_breakdown`, `dm_diff*`, `s3_diff`, `topology`, `visualize`, …) is **out of scope**.
- Duty = PERCENT everywhere (1 = 1%); bw = kHz everywhere (fractional ok); units explicit in help text.
- `mobile_autoregister=false` = NO DISCOVERs ever (team-DAD still runs; `mobile register` = one-shot arm).
- Mixed-leaf teams are IN SCOPE: team membership keys on `team_id` never leaf; a team member REFUSES a
  PHY-mismatched home (freq/bw/sf/cr; layer_id explicitly excluded — cross-layer same-PHY re-home is supported).
- Turnaround = 8 ms (bench-measured 2026-07-23; 27 was a stale overestimate — s09_metal still carries 27/27
  explicitly, disposition OPEN, see queue).
- SF range = 5..12 everywhere in code; SF5 currently does NOT work on metal (keep supported, don't bench-rely).
- E2e-ack deadline = Option A (firmware timer; `e2e_ack_timeout` means never-CONFIRMED, not failed).
- LBT = energy-detect default (`lbt_model:"energy"`, threshold 0 dB, ask-time, zero draws); `"cad"` = A/B only.
- The e2e ack is plane/crypto-agnostic (plaintext `-a` identical to sealed) — the user personally corrected a
  mis-framing here; be precise about this mechanism.
- s18's delivery reference legitimately moved DOWN with realism (101→99→{97,88,99}-era) — realistic numbers
  beat inflated ones; never "fix" a realism-driven drop by un-realisming the sim.

## 6. THE WORK QUEUE (dispatch in this order; one slice at a time)

### 6.1 Wave-4 physics remainder (sim repo; re-anchor class each)
1. **BW delivery gating** — ⏳ **IN FLIGHT (dispatched 2026-07-25)**; brief in the session scratchpad
   (`brief-bw-gating.md`). Verified state: the SF gate is at `SimController.cpp:1049` (against the live
   `_node_sf_rx_set[rcv]`); `tx.bw_hz` rides every frame but is never gated; EventLog has exactly 9 `drop*`
   emitters so BW is the 10th → the §F5 `dropCommon()` builder, **split as a SEPARATE step (C1)**.
   ★ **SCOPE CORRECTED — "BW/freq" is NOT one slice: the sim has NO per-node frequency at all**, only a global
   `simulation.radio.frequency_mhz = 868.0` (`JsonConfig.h:227`). Per-node freq = a config-schema change = its
   own later slice; the dispatched slice is BW-ONLY and says so loudly.
   ★ **Confirmed sim-only** (this was the open scope risk): the firmware *already* has `set_rx_bw`
   (`lib/hal/iradio.h:55`, `device_radio.h:216`) for the per-layer gateway retune, and the sim *already* routes
   it — `NodeRuntimeWrapper.cpp:66` → `ISimHal::simSetRxBw` → `FirmwareNode::simSetRxBw`
   (`FirmwareNode.cpp:206`), which today uses it ONLY for the RX-window-slop formula. The signal is flowing and
   being discarded for delivery; no firmware change is needed. Also settled for the brief:
   `_radios[rcv]->getBwHz()` is NOT a valid RX-BW source (each TX overwrites the radio's params via
   `setRadioParams`), and cross-SF quasi-orthogonality (`CollisionModel.cpp:35`) must NOT be extended to BW —
   a BW-mismatched frame stays a full interferer, it merely stops being decodable.
2. **TX→RX turnaround plumbing completion** (small-medium): `SimRadio.cpp:222` sets `_earliest_rx_ms` but the
   loop path bypasses `startSendRaw` (`SimController.cpp` ~:1431 TODO Y2); `tx_fail_prob` unplumbed the same
   way. Finish the plumbing so a node is deaf for `tx_to_rx_delay_ms` (8) after its own TX in ALL paths.
3. **Fading in collision/LBT verdicts** (small-medium; unblocked by Slice C's per-link streams): capture
   (`:1473`), CAD roll, AND the energy-LBT provider currently read the UNFADED matrix SNR while delivery reads
   faded; also consider a realistic nonzero `snr_coherence_ms` default (burst outages). Decompose: verdict-SNR
   change first, coherence default second.

### 6.2 Smalls (ride between slices; tiny)
- s10: delete its 23 dead Lua-era emit asserts (delivery-gated only; `join_data_sfs_adopted` etc. don't exist).
- SF8–12 advisory gap: `mobile_sf_list_mismatch` compares only the OFFER's low byte (frame field is 1 B);
  extend the DIAGNOSTIC (not the wire) to cover high SFs, or document.
- ~~s09_metal's explicit 27/27 turnaround: ASK THE USER~~ → ✅ **CLOSED 2026-07-25: updated to 8/8.** The user
  ruled: 27 was a mis-measurement, the bench reads **~5-8 ms**, 8 is the value of record (open sub-question,
  recorded not blocking: whether the delay is purely radio-module or partly firmware). Every stale 27 swept
  (s09_metal override, two `_desc` blocks, the sim `test_wave2_config` comment+printf, the firmware P-BUDGET
  context line — the patience constants themselves need no re-derivation, see BASELINE's 2026-07-25 note).
  s09_metal re-anchored `9f003405`/2456 with full +4-event attribution; s15_three_layer_metal byte-identical.
  ⚠ Lesson worth keeping: neither `_metal` variant had a BASELINE anchor, which is exactly how the stale 27
  survived four re-anchor events. Both are now anchored — **anchor a scenario or it will rot.**

### 6.3 3-B dedup — ALL NINE user-approved in this order (refactors; full-suite byte-identity gates)
Full details + file:line citations: `docs/2026-07-20-realism-and-duplication-review.md` PART 3 §3-B.
1. `RecentRing` header template — 7 hand-rolled recently/mark window rings + 3 age-out sweeps.
2. `push_send_failed()` + `giveup_flight()` — 23 Push fills + 6 giveup rituals (reason-hole fixes already done
   in 3-A; this is now pure extraction).
3. `parse_phy_args` — 3 freq/sf/bw parse blocks + 8 kHz→Hz roundings (validation gaps already fixed in 3-A).
4. NV `load_stamped`/`commit` — 7 copies of the load-or-seed/stamp/save ritual (one historical brick).
5. `JitteredTxStash` — H-forward/RREQ-forward stash rings are byte-for-byte twins (+ the OFFER slot).
6. Wrapper `parseHex32`/`parseDec` extraction + caps from protocol constants (the send_layer guard fix already
   landed in 3-A; this is the dedup half).
7. Wrapper key-table — generate the §1.2 whitelist AND the config-mapping walk from ONE {name, applier} list.
8. Shared test-Hal fixture (`test/support/test_hal.h`) — 7 parallel stubs; PICK THE CLAMPED rand semantic and
   audit the 4 unclamped-TU users.
9. `MR_TELEMETRY`→`MR_EMIT` conversion — 133 long-form sites, per-TU batches, byte-identity per batch.

### 6.4 Shelf (real findings; dedicated slices; rising priority marked ★)
- ★ **Flood-repair hardening** (deferred twice — the durable cure for the s23/s27 seed-fragility that
  re-pinning papers over): (a) re-enable team `flood_fast_self_pull` (`node.cpp` ~:824 gates it off on a
  foreign-team concern that DATA-M ingest already guards); (b) digest ingest fires `schedule_triggered_beacon`
  (a relay's fresh dirty entry currently waits out a 5-min periodic that may never come).
- **Origin-namespace alias**: a mobile stamps origin=home_id, so its own frames (e.g. layer_query) can
  (origin,ctr)-collide with its home's delegated re-originations in relay gateway-hold queues after a re-home
  ctr reset. Fix direction: plane/type discriminator in the queue key, or ctr-jump on re-home.
- **~386 s dead-home black-hole**: a mobile whose home died is inbound-unreachable until the presence cadence
  detects it (Slice-B tiers → T up to 480 s). Evaluate a faster detection tier or accept + document.
- Micro: the delegated e2e-ack wildcard entry matches its reverse-ack by ctr ALONE (key=0) — a concurrent
  direct `-a` with a colliding ctr could mis-clear it (benign: one missed timeout). / No scenario fires an XL
  `-a` send (the XL arm paths are native-tested only) — scenario-coverage candidate. / Route-score tie-break
  stability at the +12 dB report ceiling (dense metal saturates many links to equal scores). / Timer wheel at
  0 free ids.
- **3-C wire-risk class** (own slices, byte-proven, NEVER mixed with anything): frame_codec TLV-walk helper
  (4 identical hand-rolled loops); route the ~8 out-of-codec manual unicast-inner builders through
  `wire::Writer` (the layout truth lives in ≥3 files — the fleet-brick-risk class, see the
  wire-bits-verify-codec memory); the H-frame optional-block triple-encoding stays DOCUMENT-ONLY until a new
  optional block is actually needed.

### 6.5 THE METAL BENCH (Sunday 2026-07-26 — the user runs it; you prepare)
The whole realism era's acceptance test. Bench notes for the user (accumulated): BREAKING console input units
(`cfg set duty` now percent — `0.1` means 0.1%, type `10` for 10%; `cfg set bw`/`l1_bw` now kHz — `125000` is
rejected, type `125`); NV/store version bumps during the arc mean the on-node inbox wipes ONCE per reflash;
reflash all nodes of a leaf together (no-mixed-firmware rule). **Bench findings PREEMPT the entire queue** —
anything hardware contradicts jumps to the front, and the sim-vs-metal delta is itself the finding (the
realism arc exists to make those deltas visible).

## 7. Dispatch-brief pattern (what makes coder slices succeed here)

Every brief that worked contained: (1) role header (never commit; QA owns scenarios/BASELINE/contract/docs);
(2) the context docs to read FIRST; (3) Phase-1 root-cause evidence requirements BEFORE any fix, with "a
premise you disprove is a reportable result" (two of our best outcomes were coders disproving the brief);
(4) the exact design direction + the U1 extend-don't-fork constraint + which planes/§18 rules apply;
(5) the gate: exact current anchors, which gate class, decomposition builds where attribution matters
("run item X last and gate separately"); (6) memory-bounds care (no heap; bounded rings; sizeof(Node)
static_assert; timer-wheel headroom); (7) "report deviations LOUDLY" + machine-read report structure.
Independently re-run their gates. Adjudicate their deviations explicitly — accepted deviations get recorded
in BASELINE notes with rationale.

## 8. Document map

- `simulation/BASELINE.md` — THE gate document: current anchors in the header, dated forensic notes below
  (newest notes near the queue markers). You re-anchor it; keep the dated-note discipline.
- `docs/2026-07-20-realism-and-duplication-review.md` — the master findings doc: Part 1 sim realism,
  Part 2 plane duplication, Part 3 the second hunt (3-A done / 3-B queue / 3-C wire-risk / 3-E not-worth-it),
  the Wave-2 rulings, the Slice-C investigation.
- `docs/superpowers/specs/2026-07-19-signal-strength-unification.md` — the signal-strength arc (done) +
  Slice C §5.
- `ios-companion/INBOX_SYNC_CONTRACT.md` — the companion contract; every contract-visible change (PushKind,
  reasons, JSON fields) lands here, by you.
- `docs/protocol.md` / `docs/frames.md` — behavior / wire layout. frames.md stays wire-oriented.
- Memory dir (`~/.claude/projects/-home-staszek-MeshRoute/memory/`): `meshroute-mobile-qa-handover.md` is the
  living session handover (points here now); `MEMORY.md` is the index; the process-rule memories
  (agent-task-identity-discipline, duplicated-code-priority, present-state-before-proposing,
  user-handles-commits, wire-bits-verify-codec) are binding.

## 9. Session-history one-liner (context for WHY the state looks like this)

2026-07-19→25 arc: sim made realistic in waves (SNR report ceiling → metal defaults/units → per-node RNG →
8 ms turnaround → LBT energy-detect), and every realism step EXPOSED real protocol bugs that were then fixed
(F-SF-1 sf-list adopt, F-PS-1 team mute, F-TR-1/2 team hash routing, P-BUDGET patience, F-CH-RELAY holder
repair, P2-1 mixed-leaf teams, team-DAD mediation, the quiet-throttle plane-blindness, the route
re-advertisement antidote, the e2e-ack deadline). Delivery IMPROVED under realism nearly every time.
The suite grew from 6 to 11 mandatory scenarios (s21–s30 + s18), all currently green. That pattern —
harden the sim, chase what breaks, fix the protocol, re-anchor — is the operating rhythm to continue.
