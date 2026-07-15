# R3.x — Lossy / Concurrent Gate (design spec)

**Status:** IMPLEMENTED + verified. **Date:** 2026-05-30.
**Pins (decided):** P1=both (forced_drops + Bernoulli) · P2=BAND 10pp / K=2 then tighten · P3=new `dm_diff_band.py` ·
P4=2-node single explicit link · P5=wire into `run_tests.sh` post-loop · P6=yes, both jitter golden tests.

## Implementation findings (what the gate actually required)

Building the gate surfaced several realities that reshaped WI-1/WI-3 from the draft:

1. **The funnel cannot be asserted exactly — gate on the per-pair delivery BAND.** lua and
   meshroute diverge by design: (a) the Lua selects the DATA SF by an EWMA of SNR, while the C++
   port pins `data_sf` (the EWMA is deferred), and (b) the C++ wire is shorter so airtimes — hence
   every timeout/retry boundary — differ. So the forced gate runs `--band 0 --funnel report`: the
   per-pair delivery-% must match EXACTLY, but the retry funnel is informational (it diverges).
2. **Drop the RTS, not the CTS.** A forced CTS drop desyncs the SF — the receiver has already
   switched to `data_sf` awaiting DATA while the sender retries RTS on `routing_sf` → `drop_sf_mismatch`
   → giveup. Dropping the first **RTS** (alice→bob) lets the receiver never engage, so the retry is a
   clean fresh handshake. Both engines then deliver N/N. (Lua labels a retry `RTS-rty`, C++ labels all
   `RTS`; `nth=1` hits the original on both.)
3. **Match `data_sf` to the Lua's EWMA pick.** At the gate's SNR (9 dB) the Lua EWMA selects SF12, so
   the scenario sets `data_sf: 12` — otherwise the two engines transmit DATA on different SFs and the
   timing diverges. (This is also why the proven `r3_data_diff` uses SF12.)
4. **Kill the probabilistic per-frame losses** via `radio.hardware.rx_preamble_miss_prob: 0.0` +
   `decode_margin_steepness_db: 0.0` (they are nested under `radio.hardware`, NOT `radio`). A random
   CTS miss otherwise triggers the SF-desync of finding 2.
5. **Quiet the channel during the send window** — `beacon_period_ms` ≫ duration so steady beacons stop
   after discovery (routes persist via the 45-min aging TTL); place sends after discovery exit (~13.5s).
   Beacon/data collisions otherwise fire divergent retries.
6. **`node_id` ↔ array-index resolution — FIXED at the root.** The meshroute `send <name>` rewrite
   resolved the name to the array index (`_name_to_id`), but the route table is keyed by the protocol
   id, so `node_id ≠ index` silently targeted the wrong id → `send_no_route`. Fixed: the rewrite now
   resolves to `_nodes[idx]->protocolId()` (the id the firmware routes by); backward-compatible because
   `protocolId() == index` when `node_id` is unset.
7. **`load_config` requires an explicit `node_id` per node** (the r3 lineage relied on index defaulting);
   the band scenarios carry it. `dm_diff_band` computes the per-pair delivery directly from the
   `tx_enqueue`/`delivered` events (both carry origin+dst) rather than the heavy lifecycle analyzer —
   robust across the lua + meshroute event schemas.

**Post-review hardening (adversarial review, 28 candidates → 12 verified → 11 fixed):**
- **Vacuous-pass hole closed (HIGH).** The differential only checked cross-engine parity, so a
  common-mode regression delivering 0/N on BOTH engines passed (delta=0). Added `--min-delivery
  {none,one,full}` (an absolute per-engine floor) and wired `--min-delivery full` into the gate.
  Verified negatively: a partial-delivery scenario now fails `FLOOR MISSED` even with the band wide open.
- **Forced-drop counts once per physical FRAME** (was once per frame×receiver — a broadcast/wildcard
  `to` consumed N from `count`). 64-bit window math (overflow guard). Per-frame `dd_counted/dd_drop`.
- **First send moved to 18 s** (clears the last discovery beacon ~17.7 s) → the funnel collapses to
  exactly +1 `rts_tx` (the single legitimate forced retry); cts/data/ack/delivered now match 5/5 exactly.
- `--expect-drops 1` marked LOAD-BEARING in `run_tests.sh` (guards a mis-labelled directive that drops
  nothing); docstring/`_desc` corrected to `--funnel report` + the new flags; the concurrency test's
  `rt_update` assertion now names the dest=2 route (the `Ev` capture learned the `dest` key).
- Not fixed (by design): the forced-drop counter advances only on frames that survive physics — correct
  for the quiet/lossless gate where the targeted frame always arrives; documented in `JsonConfig.h`.

**Bernoulli result (P1's distribution sample, NOT wired):** under sustained random loss (loss=0.15,
N=20) the engines diverge by ~60pp (lua ~25%, meshroute ~85%): the Lua's slow (~10 s) retry latency
backs up catastrophically while the port's fast retry recovers. This is a genuine deferred behavioural
question (the EWMA + retry-timing divergence), NOT a gate to lock now — exactly P2's "never lock the
band on one run." `scenarios/r4_data_diff_bernoulli.json` is kept as a diagnostic/calibration tool that
surfaces this open question; it is **not** in `run_tests.sh`. Revisit when the EWMA `select_data_sf` is
ported (the cross-leaf milestone).
**Predecessor:** `2026-05-30-r3-data-plane-design.md` (R3 = idle+lossless RTS-CTS-DATA-ACK delivery, done).
**Working artifact** — not committed (per feedback_superpowers_not_committed). The USER commits the implementation.

---

## 1. Scope & goal

R3.x adds **NO new firmware logic** to `MeshRoute/lib/core/node.cpp`. R3 already ported the two
retry-jitter rand draws (`rts_timeout_fire` node.cpp:878, `ack_timeout_fire` node.cpp:895) and the
single-flight `tx_queue`, and unit-tested them in `test_node_r3.cpp` — but the **delivery gate runs
idle + lossless** (`scenarios/r3_data_diff.json`: `sigma_db=0`, seed 42), so it exercises **zero rand
under loss**. The two retry draws have therefore **never been proven draw-for-draw aligned** with the
Lua `mt19937`, and the floor-exact `retry_jitter_ms` range has never been fired.

R3.x builds the harness to fire those draws under **controlled, deterministic loss**, plus a single-node
concurrency invariant. The statistical delivery-%/funnel gate it produces is the **shared prerequisite**
for NACK and R4 (both fire under contention the idle diff cannot run).

**Four work items:** WI-1 deterministic SimController forced-drop hook · WI-2 `lbt_enabled` host
plumbing · WI-3 statistical delivery-%/retry-funnel gate (replaces exact set-parity) · WI-4 `become_free`
concurrency micro-gate (TestHal unit test).

**Hard invariants:**
- The meshroute Node's `_sim_rng` call order **MUST NOT shift** — R3.x is sim-harness + test only.
- All existing `test/t*.json` + `scenarios/s*.json` stay green (102/103/105 PASS).
- The lossy gate runs **lua then meshroute into SEPARATE NDJSON**.
- **s01 excluded** (intentional concurrent-flight stress, t=32000 overlap — memory project_s01_concurrent_send).
- No `pack_*/parse_*` change (would force a full s15+s17 multi-leaf sweep — feedback_wire_format_multileaf_sweep).

---

## 2. WI-1 — deterministic SimController forced-drop hook

**Why:** there is **no deterministic per-frame drop** in the sim today — only stochastic `sigma_db`
log-distance fading and a per-link Bernoulli `loss` that draws from the **shared `_rng`**
(SimController.cpp:991-1004). Neither can fire *exactly one* retry independent of seed/sigma. A forced
drop of the Nth `{from,to,label}` frame does.

- **2.1 Schema** (`core/topology/JsonConfig.h`): `struct DropDirective { std::string from, to, label; int nth = 0; int count = 1; };`
  + `std::vector<DropDirective> drop_directives` on `SimConfig`. Empty ⇒ feature OFF.
- **2.2 Parse** (`core/topology/JsonConfig.cpp` ~L347): a top-level `forced_drops` array, parsed **only
  if the key is present**; optional bounds-check in the validator ~L435.
- **2.3 Tally** (`SimController.h`/`.cpp`): `_cfg` is `const` ⇒ add a `mutable std::vector<int>
  _drop_match_count` sized in `initialize()` ~L563. Empty directives ⇒ zero iterations, **zero `_rng`
  draws**.
- **2.4 Insertion** (`deliverReceptionsForStep`, before `onRecv` at SimController.cpp:1060, **AFTER all
  physics gates**): for each surviving reception, match `{from→sender_id, to→rcv, label→tx.label}` via
  `_name_to_id`; the Nth index comes from `_drop_match_count[k]++` (there is no per-frame seq in
  `InFlight`). Drop via a `bool` flag, not `goto`.
- **2.5 EventLog** (`core/events/EventLog.{h,cpp}`): a new `dropForced(time_ms, from, to, sf, bw_hz,
  label)` ⇒ a distinct **`drop_forced`** event (NOT an RF-loss masquerade — keep it auditable).
- **2.6 Determinism:** OFF-default + post-physics placement ⇒ no new `_rng` draws and no reordering of
  existing draws; sim-side only ⇒ the meshroute `_sim_rng` is untouched. The only consequence is that
  the Node's own timeout-fire (and its jitter draw) is triggered — exactly the point.

---

## 3. WI-2 — `lbt_enabled` host plumbing

**Why:** the R3.x Node never calls `channel_busy_until()` (that's R4; `on_radio_busy` is a no-op
node.cpp:948). The **load-bearing** LBT today is the **sim-side defer** at SimController.cpp:1265. The
gate must run with that OFF so no LBT draw perturbs the retry stream.

- **3.1 Knob:** `bool _lbt_enabled = true` on **FirmwareNode** (a HOST knob — NOT in
  `meshroute::NodeConfig`, which is firmware-facing). Parse in `onInit` outside the `cfg` block.
- **3.2 Gate-1 (Hal, inert at R3.x):** `channel_busy_until()` → `(_lbt_enabled && _lbt) ?
  _lbt->busyUntil(...) : 0`. Inert now (Node never calls it) but correct for R4.
- **3.3 Gate-2 (load-bearing):** add `INode::lbtEnabled()` default-`true`; FirmwareNode overrides from
  `_lbt_enabled`. SimController.cpp:1265 becomes `if (_nodes[i]->lbtEnabled() && _lbt->isChannelBusy(...))`.
- **3.4 No node.cpp change.** `LbtModel` uses a **separate RNG** ⇒ disabling it does not perturb
  `_sim_rng`. Confirm `ScriptedNode::lbtEnabled()` defaults true (no behaviour change for Lua nodes).

---

## 4. WI-3 — statistical lossy gate (replaces exact set-parity)

The current `dm_diff.py` asserts **exact `(dst,payload)` set-parity** on an idle/lossless scenario
(dm_diff.py:99-114). Under loss the cross-engine RNG streams diverge in *timing*, so exact parity is
impossible — the gate becomes a **per-pair delivery-% band + bounded retry-funnel deltas**.

- **4.1 Scenario** `scenarios/r4_data_diff_lossy.json`: clone `r3_data_diff.json` (engine-neutral, seed
  42, `sigma_db=0`), add **explicit links** + either `loss=0.2` (Bernoulli distribution sample) and/or
  `forced_drops` (deterministic single retry) per **Pin P1**. N≥30 staggered sends of the gated pair.
- **4.2 Harness** `tools/dm_diff_band.py` (NEW): run once per engine into separate NDJSON; reuse
  `load_config`/`analyse`/`configured_pairs`/`summarise` from `dm_delivery_breakdown.py`. Replace exact
  parity with `pct = 100*arrived/sent` per `(origin,dst)`: assert `abs(pct_lua − pct_mr) ≤ BAND` **and**
  each within an absolute window. The retry **funnel becomes asserted**: a retry signal keyed off
  timeout-fire transitions (re-emitted `rts_tx` beyond the first per `(origin,dst,ctr)`, `rts_giveup`,
  `data_ack_giveup`, `data_rx_timeout`); assert `abs(delta) ≤ K`.
- **4.3 Pass:** all pairs within BAND **and** all retry/giveup keys within K. BAND/K per **Pin P2**.
- **4.4** Keep the strict R3 `dm_diff.py` exact-parity gate intact for the idle/lossless regime (**Pin P3**).

---

## 5. WI-4 — concurrency micro-gate (TestHal unit test)

**Invariant:** a 2nd same-priority `do_send` enqueued while `_pending_tx` is set must **not** issue until
the first flight completes and `become_free` re-drains (node.cpp:595-602 half-duplex gate + FIFO head).

- **Test** (`MeshRoute/test/test_node_r3.cpp`): seed a single-candidate route; `do_send#1` ⇒ `rts_tx==1`;
  `do_send#2` mid-flight ⇒ `tx_enqueue==2` **AND** `rts_tx` STILL 1 **AND** queue depth==1; feed CTS ⇒
  `data_tx==1`; feed ACK ⇒ `rts_tx` 1→2 with msg-b's ctr (re-drain fires ON the post-ACK path).
- **Why a unit test, not a scenario:** it's a pure single-node `_pending_tx`-gate invariant needing
  hand-fed mid-flight ordering; t86/t87 keep depth ≤ 1 so a scenario can't exercise it without timing
  fragility. `select_data_sf` is **pure** (node.cpp:561-566, returns `_cfg.data_sf`, sf_index=ANY=3) ⇒
  no `chosen_data_sf` divergence to chase; the EWMA must NOT be wired now (cross-leaf trap).
- Note: `kQueueWakeupTimerId` is wired (node.cpp:113) but never armed — the drain is event-driven off
  `become_free` call sites (:591/718/822/830/848/886/903/911).

---

## 6. Gate definition (the R3.x acceptance criteria)

- **LOSSY:** statistical band on per-pair arrival-% + bounded retry-funnel deltas, lua-vs-meshroute,
  separate NDJSON, s01 excluded.
- **CONCURRENCY:** TestHal unit assertions on `become_free` drain ordering.
- **Regression:** t84/t85/t86/t87 + the doctest suite stay green; airtime golden tests (Pin P6) green.

---

## 7. Determinism argument

WI-1 OFF-default + post-physics placement ⇒ no new `_rng` draws / no reorder ⇒ meshroute `_sim_rng`
preserved. WI-2 skips only a sim-side no-op defer; `LbtModel` RNG is separate. WI-3/WI-4 are harness/test
only. The two retry draws fire at **identical mt19937 positions on both engines**: `retry_jitter_ms =
3·airtime_ms(routing_sf, bw, cr, preamble, 8)` — the literal **8 = RTS_LEN** (Lua timing constant
dv_dual_sf.lua:2895/8626-8627), deliberately NOT the 7-byte C++ wire (node.cpp:571-572 documents this to
keep the streams aligned) — drawn as `rand_range(0, jitter+1)` over an identical `[lo,hi)` distribution on
a shared `_sim_rng` (ScriptedNode and FirmwareNode rand impls are byte-identical). **Identical-by-
construction, but currently unpinned by any golden test** → Pin P6.

---

## 8. Non-goals

No firmware LBT-defer (R4); no cascade-to-alt on retry (R3+/NACK); no EWMA `select_data_sf` (moot while
sf_index=ANY=3 — it's the cross-leaf trap); no s01 change; no byte-for-byte wire parity; no
`pack_*/parse_*` change.

---

## 9. Pins to decide (before implementation)

| # | Decision | Options | Recommendation |
|---|----------|---------|----------------|
| **P1** | Lossy-gate drop mechanism | (a) forced_drops only · (b) Bernoulli loss only · (c) **both** | **(c)** forced_drops = the seed-independent single-retry mt19937-alignment check; Bernoulli = the N≥30 distribution band. Forced-drop is load-bearing. |
| **P2** | BAND (per-pair %) / K (funnel count) tolerance | (a) **10pp / K=2** then tighten · (b) 5pp / K=1 · (c) calibrate from a 4-seed run first | **(a)** start loose, tighten after a multi-seed calibration; never lock a band on one run (echoes s15 noise-domination). |
| **P3** | Harness location | (a) **new `dm_diff_band.py`** · (b) `--band` flag on `dm_diff.py` | **(a)** keep the strict R3 exact-parity gate intact; band is a sibling. |
| **P4** | r4 scenario topology | (a) **2-node single explicit link** · (b) t87-style 3-node line | **(a)** isolates exactly one retry draw, clean band denominator. Promote to 3-node only if forward-leg retry coverage is wanted. |
| **P5** | Run the band gate in `run_tests.sh`? | (a) **explicit post-loop step** · (b) keep manual | **(a)** a gate that isn't run isn't a gate — but bypass the t*/s* auto-discovery glob (engine-neutral scenario errors under the plain runner). |
| **P6** | Add jitter-range golden tests? | (a) **yes** (`airtime_ms(…,8)` at SF7/8/9 + `retry_jitter_ms==264`) · (b) no | **(a)** cheap; fails loudly at the source if a future len 8→7 "wire fix" de-aligns the streams. |

---

## 10. Files touched

`core/topology/JsonConfig.{h,cpp}` (WI-1) · `orchestrator/runtime/SimController.{h,cpp}` (WI-1+WI-2) ·
`core/events/EventLog.{h,cpp}` (WI-1) · `orchestrator/runtime/FirmwareNode.{h,cpp}` (WI-2) ·
`orchestrator/runtime/INode.h` + `ScriptedNode.{h,cpp}` confirm (WI-2) · `scenarios/r4_data_diff_lossy.json`
NEW (WI-3) · `tools/dm_diff_band.py` NEW (WI-3) · `test/run_tests.sh` (WI-3, P5) ·
`MeshRoute/test/test_node_r3.cpp` (WI-4) · `MeshRoute/test/test_airtime.cpp` (P6) · this spec.
