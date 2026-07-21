# Review 2026-07-20 — sim realism / fail-loud config / cross-plane mechanism duplication

Four-agent review + QA verification (spot-checks of every load-bearing claim against code).
Scope per the owner: (1) simulation as real as possible + fail loud when something is not defined;
(2) all mechanisms — are we re-coding the same things for static and mobile/team networks?
Findings only — no code changed. Companion to `docs/superpowers/specs/2026-07-19-signal-strength-unification.md`
(the arc that motivated this review) and `docs/2026-07-04-codebase-review-triage.md` (the earlier structural review).

## PART 1A — sim physics realism (what the sim models vs a real SX126x)

Full inventory ran over 19 dimensions; realistic already: demod thresholds (exact match to the firmware q4
table), sigmoid PER, partial-overlap collisions, half-duplex, duty cycle, clock drift, link asymmetry
(offsets + directed shadow). Ranked gaps (all file:line verified):

1. **LBT/CAD model ≠ metal energy-detect** (`core/physics/LbtModel.cpp:11-32`). Sim = one CAD roll per frame
   at TX-start, probabilistic detection down to −15 dB; metal MeshRoute = noise-floor energy detect re-sampled
   at attempt time, deaf below ~0 dB. Sim over-defers on weak neighbors AND under-defers when the one-shot roll
   misses (whole airtime then invisible). Channel-access is the proven failure surface (s23 beacon starvation).
   Fix: medium — an `lbt_model:"energy"` mode (busy = any in-flight frame above threshold at the observer,
   evaluated at attempt time, draw-free).
2. **No BW/freq delivery gating** (`SimController.cpp:958-975` — gates ONLY on SF; the :1153 comment even
   admits sf+bw is required; QA-verified). Dual-BW gateway layers are RF-isolated on metal but leak in sim →
   the layer-isolation bug class is structurally invisible; a bricking BW misconfig is inert in sim.
   Fix: small — per-node rx bw check mirroring `sf_rx_set`, + `drop_bw_mismatch` event.
3. **TX→RX turnaround is dead code in the loop path** (`SimRadio.cpp:222` sets `_earliest_rx_ms` but the loop
   bypasses `startSendRaw` — `SimController.cpp:1431-1433` TODO(Y2); `tx_fail_prob` unplumbed the same way).
   A frame arriving <5 ms (metal: ~27-50 ms) after own TX end is received cleanly. This is the exact family of
   the three metal-only DM bugs already caught on the bench. Fix: small-medium (finish Y2 or a blind-window
   check at delivery).
4. **Fading excluded from collision/CAD verdicts** (`SimController.cpp:1473-1474`, `:1548` use the unfaded
   matrix SNR; fading applied only at delivery `:887-899`), and the default is i.i.d. (coherence 0) — no burst
   outages, so retry/backoff/liveness logic never sees a 5-s fade. Fix: small-medium, **gated on Slice C**
   (any new draw reshuffles the shared RNG).
5. **SF5/SF6 airtime divergence** (`SimRadio.cpp:133-150` = flat AN1200.13 4.25/+44 for all SF; QA-verified) —
   the firmware's own `airtime.cpp` deliberately implements the SX126x §6.1.4 case (6.25/+36). Sim frees the
   channel 1-2 ms earlier than the firmware's budget math at low SF. Fix: tiny (port the branch).
   Plus: **per-frame TX power is carried but never consumed** (`FirmwareNode.cpp:189`, no reader) — adaptive-power
   features would "work" in sim regardless. Fix: small.

Lesser items (documented, lower priority): no aggregate interference summation (3 weak interferers never sum);
cross-SF perfectly orthogonal (no Croce SIR penalty); preamble-miss i.i.d.; RSSI ≡ SNR+const and unshaped at the
report boundary (inert today — nothing in lib/core consumes RSSI — a trap only if RSSI-consuming features port);
noise floor not BW-derived (62.5 kHz layer gets no ~3 dB sensitivity edge → per-layer-BW tradeoffs sim-untestable);
`frequency_mhz` parsed but never consumed (placebo knob); multi-SF `sf_rx_set` >1 physically impossible (default safe);
mobility quantized to 60 s link-rebuild ticks.
Known/queued, excluded: shared mt19937 (Slice C), rx_window_slop opt-in, SNR report shaping (done).

## PART 1B — fail-loud configuration audit

Corpus: 26 scenario JSONs / 4511 links / 333 meshroute nodes. Top hazards (QA-verified where marked):

1. **★ `"bw": 62.5` silently truncates to 62 kHz** — `JsonConfig.cpp:160` `get<int>()` (VERIFIED; s28 et al.
   carry `"bw": 62.5`; 12/26 scenarios). Every 62.5-kHz scenario runs ALL physics + the firmware's own airtime
   math at 62000 Hz where metal runs exactly 62500. Also the unset-default 62500 predates the kHz convention
   (would mean 62.5 MHz). Fix: parse as double ×1000, and make bw REQUIRED.
2. **Turnaround defaults 1/5 ms vs the one metal-measured datapoint 27/27 ms** (only `s09_metal` sets it;
   `rx_window_slop:"idealized"` in 24/26). 25/26 scenarios simulate a radio 5-27× faster at turnaround — where
   the CTS-wait metal bug hid. Decision needed: flip the DEFAULTS to the metal values (re-anchor event) or
   require them per scenario.
3. **Duty-cycle three-way disagreement + a 100× unit trap**: sim default 0.01, corpus split 0.1/0.01, device NV
   percent (`duty=0.1` on device = 0.001) vs scenario fraction (0.1 = 10%). Unify the unit + require the key.
4. **Unknown node.config keys silently ignored** — PROVEN live: s09/s09_metal/s10 set `"join_required": true`
   on meshroute nodes; the wrapper never reads it (VERIFIED: 0 matches) → runs false, no warning. Any typo'd or
   unported knob silently becomes a C++ default. Fix: whitelist-validate in `NodeRuntimeWrapper::onInit`
   (the existing return-false fail-loud path), + map-or-reject the currently-unmapped NodeConfig fields
   (`host_mobiles`, `join_required`, `req_sync_*`, `sync_response_*`, `gw_announce_*`, `intro_attach`, …).
5. **`link.snr` default 8.0 + `snr_std_dev` default 0 on 2503/4511 links** — an omitted snr silently becomes a
   healthy link (the same silent-optimism class as the fixed +12 report ceiling; the VARIANCE half is still
   defaulted away corpus-wide). Make per-link snr + std_dev required.
Honorable mentions: `radio.cr` default 1 is outside the validator's own [5..8] (fail-loud by accident — make it
explicit); `team_beacon_period_ms` inherited (5 min) in 4/6 team scenarios whose runs are 10-15 min (≈1-2 steady
team beacons per run — adjacent to the s23 starvation class); `allowed_data_sfs` absent = silent semi-brick;
sim path-loss tx_power 20 vs metal 22 dBm; `node_startup_jitter_ms` default 0 = unphysical sync boot.
Enforcement points: `JsonConfig::validateConfig` (already collects errors) for sim-globals + links;
`NodeRuntimeWrapper::onInit` for node.config (FirmwareNode's `n_layers` domain check is the model to copy).

## PART 2 — cross-plane mechanism duplication (static vs team vs mobile)

The *storage* separation is disciplined and §18-correct throughout (verified: separate tables/ledgers, the
write-alias history respected, delegated billing coherent). The *mechanism code*, however, is re-written per
plane in specific places, and small drift already exists. Two independent agents converged on the same list.

### ★ P2-1 — NEW FUNCTIONAL FINDING: the team leaf-gate contradiction (needs an owner ruling)
Four independently-coded copies of one rule disagree (all QA-verified):
- team F: leaf-AGNOSTIC (`node_route_discovery.cpp:228-234`, team check BEFORE the leaf gate, comment: "a mixed
  team spans leaves, so there is NO leaf_id gate");
- team RTS: leaf-agnostic carve-out (`node_mac_rx.cpp:31-40`, comment states the mixed-leaf design:
  off-grid member on leaf 0 + registered member on its home's adopted leaf);
- team-scoped H: **leaf-gated FIRST** (`node_hashlocate.cpp:608` unconditional, before the :640 team check);
- team beacons: **leaf-gated FIRST** (`node_beacon.cpp:489/:504` before `same_team_beacon:721`).
Consequence: a mixed-LEAF team can exchange F and RTS but can never FORM (no cross-leaf `_rt_team`, no team-key
cache — the beacon gate is the root) nor RESOLVE (no cross-leaf H). The F/RTS carve-outs are currently dead
scope. s28 doesn't catch it because its whole team shares one leaf number. **★ RULED by the user 2026-07-20: mixed-leaf teams ARE in scope ("that was the assumption from the beginning") —
a team's `create` defines the PHY all members operate on; members homed onto different layers MUST stay
team-reachable.** Fix slice dispatched: (Level 1) team-scoped frames accepted by team_id at ALL FOUR RX sites
(add the exemption to beacons + H, matching F/RTS); (Level 2, also ruled: option (a)) a TEAM member REFUSES to
register to a home whose PHY (freq/bw/routing_sf) differs from its team-provisioned PHY — fail loud, stay
off-grid-but-team-reachable (cross-LAYER re-home on the SAME PHY stays allowed — that is exactly the mixed-leaf
case). Note: F-SF-1 already preserves the sf-list half across registration; this guard covers the freq/bw/sf/cr
half. Acceptance = a new mixed-leaf s29 scenario; all existing streams byte-identical (single-leaf).

### P2-2 — `handle_f` vs `handle_f_team` + the rreq ledger forks (the historical 2×-bug site)
`node_route_discovery.cpp:224-340` vs `:350-394` (~45 re-coded lines, currently in sync — diffed gate-by-gate)
plus `rreq_seen_recently`/`mark_rreq_seen`/`rreq_rate_ok` forking inline over `_rreq_*_team` twins (~45 more).
The F-XL-2 zero-jitter flaw existed identically in both copies; every F change must be made twice. One found
drift (telemetry-only): team table-cap refusal is silent where static emits `table_cap_hit`. Unify to the
`handle_h` shape (one body, plane gates in thin wrappers; table-ref params à la `rt_merge`). Gate: s18
byte-identity incl. telemetry field order. Separate *stores* stay (deliberate §18).

### P2-3 — the 11× local-id guard + 4× team-acceptance predicate (the "missed twin" §18-alias source)
`!(addr_len==1 || is_team_peer(next))` hand-repeated at ≥11 write-sites (`node_mac_rx.cpp` ×9,
`node_cascade.cpp` ×2 — two in-code comments literally record "audit-caught missed twin");
`_cfg.team_id && _team_local_id && X==_team_local_id && addr_len==1` at 4 RX-acceptance edges.
Extract two named predicates. Trivial, high payoff.

### P2-4 — the liveness tier machine coded twice
`record_peer_rts_timeout` team branch (`node_routing.cpp:654-675`) + `clear_peer_suspect` team branch
(`:699-710`) + `team_liveness_slot` (`:585-603`) re-code the static tier cascade (suspect@1-2/silent@3/
dead@6-over-window, same constants inline — QA-verified) and the slot-eviction body. The gossip/advertise/
resort omissions are deliberate §2c; the THRESHOLDS existing twice is the drift risk. Extract a tier-map core
both branches call.

### P2-5 — DAD/allocation triplication + tiebreak divergence
Three allocators (static random-pick `node_join.cpp:135-162`; team same-shape copy `node_mobile.cpp:134-145`
WITHOUT denied-list/prefer-previous/mediation; hosted-mobile top-down `node_join.cpp:96-110`), three
loser-selection rules (key tiebreak / inline key compare `node_beacon.cpp:731` / arrival-order-wins).
The team inline compare should call `join_tiebreak_wins` (one line, zero behavior change). Team-DAD's missing
mediation (hidden-terminal A/C both holding id X, only B hears both → never converges) is a real gap — a
FEATURE decision, not a refactor. ★ STALE COMMENT (update-stale-comments rule): `node_join.cpp:87-89` claims
static DAD picks "BOTTOM-UP from 17" — the code picks uniformly at random (:161); the §6 "one rule for EVERY
heal" claim also now over-promises.

### P2-6 — cache-mechanics family (~12 hand-rolled bounded rings, 3 eviction disciplines)
Five (hash↔id/key) stores with different-and-justified trust models (don't merge), but: **evict-slot-0** in
`presence_note_candidate` (`node_mobile.cpp:386`) can clobber the BEST candidate (looks accidental;
`learned_layers_ingest` and `_notify_pending` same); `_team_keys` is the only binding store with NO TTL
(read-path guarded by `is_team_peer` — the invariant lives in the readers, not the store); a second pubkey/name
store (`HostMobileEntry.ed_pub/name`, inline hash check `node_join.cpp:616-623`) bypasses the `peer_key_set`
funnel (no confidence/pinning/push); ~8 hand-rolled `ed_pub[:4]` LE derivations that `identity.h` should own.

### P2-7 — plane-blind spots to add to the node.h leak-cluster list (document at minimum)
- `beacon_max_idle_force` counts dirty on static `_rt` only (`node_beacon.cpp:862`) while a team member
  advertises `_rt_team` → max-idle beacons can be suppressed with dirty team entries pending.
- `team_resort_routes_through` (`node_routing.cpp:270-278`) reranks without dirty-mark/trigger; its comment
  claims re-advertise on cadence but steady beacons are dirty-only → a team liveness rerank is never advertised
  until something else dirties the entry. Ruling: deliberate or fix (interacts with the previous item).
- `_per_origin_channel` relay ledger keyed by bare origin (team/static numeric collision shares a cap slot).
- `_seen_origins` flight dedup key has no plane bit (plaintext team/static DM alias iff origin+dst+ctr all
  collide; CRYPTED immune).
- `_hash_query_seen` has no plane key — safe ONLY by an unwritten role-exclusion (no node processes both
  planes' H floods today).

### Deliberate separations verified as correct — do NOT re-merge
`_rt`/`_rt_team`; the five identity stores; team plane never stamping static freshness (write-alias history);
team-DAD DENY-free convergence; presence plane vs peer-liveness (registration vs routing questions);
delegated-post billing at the home; separate team RREQ *stores* (the CODE is the duplication to fix).

## Proposed wave plan (for ratification)

- **Wave 1 — fail-loud + cheap correctness (firmware-inert or tiny):** bw double-parse + required-key set in
  JsonConfig; wrapper unknown-key refusal + map-or-reject unmapped fields; SF5/6 sim airtime; per-frame TX power
  plumb; team `table_cap_hit` telemetry; stale comments (§S0 bottom-up claim, §6 one-rule claim,
  team_resort re-advertise claim); document the P2-7 blind spots in node.h.
- **Wave 2 — ★ ALL RULED by the user 2026-07-21:**
  - **2.1 turnaround → REAL**: flip the baseline simulations to the metal values (27/27 ms + the metal
    rx_window_slop formula as the DEFAULT; idealized becomes the opt-in). Standing rule ratified:
    **"simulations need to be as realistic as possible."** Full re-anchor event (keystone retires again).
  - **2.2 duty unit → PERCENT EVERYWHERE, unified**: 1 = 1%. Scenario JSON moves to percent (corpus migrated
    ×100 preserving each scenario's effective duty), sim parse /100 internally, device console already percent;
    ADD the unit to the firmware help text so it's explicit at every authoring surface.
  - **2.3 team rerank advertise → FIX** using the existing mechanism (dirty-mark + triggered beacon, exactly
    what the static resort does).
  - **2.4 team-DAD mediation → ADD IT**, and simulate it (a hidden-terminal team-id-collision scenario where
    only a middle member hears both holders — mirrors static L2a).
  Sequencing: 2.1+2.2 = one sim-side slice (one re-anchor); 2.3 and 2.4 = firmware slices (s18-inert), 2.4
  ships with its scenario. Wave 2 dispatches immediately after Wave 1 lands.
- **Wave 3 — dedup refactors (s18-byte-gated, the `rt_merge` parameterization pattern):** P2-2 handle_f unify +
  ledger params; P2-3 predicates; P2-4 tier core; P2-5 tiebreak one-liner; P2-6 evict-slot-0 fix + `_team_keys`
  TTL + hash-derivation helper + HostMobileEntry through `peer_key_set`.
- **Wave 4 — sim physics (delivery-parity re-anchor class, sequenced with Slice C):** Slice C per-node RNG →
  LBT energy model → BW delivery gating → turnaround plumbing → fading-in-collision-verdicts.
