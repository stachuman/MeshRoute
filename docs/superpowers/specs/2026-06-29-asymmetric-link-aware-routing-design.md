<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Asymmetric-link-aware routing — bidirectionality sensing + degraded-route propagation

**Status:** DESIGN (brainstormed 2026-06-29) → **REVIEWED GO-WITH-FIXES (2026-06-29, 3-lens adversarial audit, all sound=true, code-verified)**. Coder implements; I quality-gate; user commits + flashes ([[user-handles-commits]]). **Wire change** — 2 backward-compatible bits, no `wire_version` hard-bump. Supersedes the MAC-level approach: the three saturation levers (BEB backoff, overheard reserve-yield, dead-handshake fast-fail #3) were all refuted/reverted, converging on *delivery is topology/asymmetry-bound, not MAC-bound* — this is the algorithm-level (routing) answer that conclusion called for.

## ★ REVIEW VERDICT + RESOLUTIONS (2026-06-29) — read before implementing
A 3-lens adversarial audit (anchors+capacity, airtime+compat, routing-logic) verified the design against the code: **all three returned sound=true** and converged on the same fixes. **Verdict: GO-WITH-FIXES** — no design rethink, but the following MUST be honored (the byte-3/cap anchors are corrected inline above; these are the rest):

- **MF1 — wire bit placement** (corrected inline): complete-flag = byte-3 **bit 4 only** (not "1 of 5 rsv"); `b3..0`=`wire_version`. A literal mis-wire bricks the fleet (every peer rejects the beacon). Fix `frame_codec.h:43`'s stale comment too. Degraded bit = byte-2 b3..1 (genuinely free). Add a round-trip test asserting `wire_version` survives a set complete-flag.
- **MF2 — capacity** (corrected inline): real cap **34** (not 35), via `beacon_max_entries()` minus the 6-B `BCN_LEAF_HEADER`; bitmap-on = 26. Census headroom computes against the **live** `beacon_max_entries(151, sched, bitmap, ext)` at emit time, never a constant.
- **MF3 — the census is a NEW packing mode, not Phase-2 reuse.** A steady-state periodic beacon is `dirty_only=true` (`node_beacon.cpp:205`) and the Phase-2 stable/remote rotation is `!dirty_only`-gated (`:322`) → **dormant** on exactly the beacon class the census targets. So the census force-injects all `hops==1` entries (mostly NOT dirty in steady state) via a NEW partition pass over `_active->_rt` filtering `candidates[0].hops==1`. The plan MUST state: (a) census entries coexist with the Phase-1 dirty pack + the post-pack dirty-clear (`:359`) **without re-dirtying every period** (else the differential design dies and every node beacons a full page each 15 min); (b) the complete-flag is set ONLY when the **full** `hops==1` set fit THIS beacon (per the MF2 runtime cap); (c) a hard re-check that the census never pushes the beacon over `beacon_max_bytes` (the F2 silent whole-beacon drop, `:345`). Note dense meshes also self-throttle via the R4.3 channel-busy beacon suppression — frame "sparse-engages / dense-inert" against BOTH levers.
- **MF4 — slow-reprobe interception site (unspecified, and NOT covered by §P3).** Verified: §P3 RREQ fires only when `liveness_penalty_q4(from_next) >= peer_silent_penalty_q4` (`node_cascade.cpp:112`). A handshake-isolated one-way node stays **liveness-HEALTHY** (`clear_peer_suspect` on its every beacon, `node_beacon.cpp:79`), so §P3 does NOT fire — the giveup falls to `try_cascade_requeue` = the exact 9–80-RTS burn. The bidi plane is ORTHOGONAL to liveness. The plan MUST add a NEW `_link_bidi==one_way` interception in the giveup path **before** `try_cascade_requeue` (the `cascade_to_alt` no-alt branch `node_cascade.cpp:107-115`, or `rts_timeout_fire`) → one RTS per `link_reprobe_ttl_ms`, while STILL sending the one probe (sole-route delivery must not regress). §P3 departed-relay recovery is unaffected (keys on liveness-silent).
- **MF5 — degraded recompute-per-merge, never sticky-OR.** Derive `degraded` fresh each `rt_merge` as `(per-candidate wire-inherited bit) OR (_link_bidi[next_hop]==one_way)`; a sticky cached OR creates a stuck-degraded state that never clears on recovery (defeats §7).
- **MF6 — `_link_bidi` confirmation decay targets `unknown`, never `one_way`** (`one_way` requires positive absent+complete evidence; a merely-stale confirmed link must decay to selectable `unknown`, else quiet-but-functional links self-degrade).

**Open-item resolutions** (the spec's 4, now answered — see §"Open items"):
- **OI1 (storage):** SPLIT — store ONLY the wire-inherited component as a per-candidate field (`RtCandidate.degraded_from_wire`; it's a fact about what the advertiser said, not locally re-derivable); recompute the local component live as `degraded_from_wire || _link_bidi[next_hop]==one_way` at score/advertise time; on a `_link_bidi` transition fan out via the `resort_routes_for_neighbor_penalty` pattern (`node_routing.cpp:158`) to re-dirty + re-advertise + propagate recovery.
- **OI2 (unknown penalty):** strictly **0** at first cut (unknown==confirmed in penalty; the ONLY demotion is positively-confirmed `one_way`). A nudge on `unknown` would punish every not-yet-probed link on a cold mesh — the 2026-06-18 freshness-hard-gate mistake in spirit. Gate-sweep can raise later.
- **OI3 (gateway census):** make the census an explicit **leaf-only** feature; gateways skip by construction (they already skip the seen-bitmap + channel digest, Principle 11) — don't rely on the headroom rule to happen to disengage.
- **OI4 (telemetry):** implement all 5 events (`link_bidi_confirm`/`link_one_way`/`degraded_advertise`/`link_reprobe`/`link_recover`) as `MR_EMIT`-gated (off on metal, per the USB-CDC discipline) — they're the oracle for the sim-A/B airtime gate + the metal lucky-marginal gate.

**Constant seeds:** `one_way penalty = peer_silent_penalty_q4` (640 Q4) for clean ordering — but it forces almost any one-way route non-viable (fine: sole still delivers, confirmed-alt wins); **fallback `peer_suspect_penalty_q4` (192 Q4)** if the metal lucky-marginal gate shows good-RF one-way routes stranded vs RF-worthless `unknown` alts.

**Implementation slicing (7 independently gateable slices):** 0 = these spec corrections (done) · 1 = codec + round-trip tests (both bits, inert, default-0 — proves backward-compat alone) · 2 = store the bidi plane (`_link_bidi[256]` + `degraded_from_wire`, local-confirm/decay hooks, no penalty yet) · 3 = detect via the heard-set ingest scan (+ degraded inheritance) · 4 = select (soft penalty in `effective_score`, OUT of `next_hop_selectable`; first BASELINE-gate — must be delivery-neutral where no asymmetry) · 5 = advertise (the census packing mode + degraded propagation, leaf-only; BASELINE dense-inert gate) · 6 = slow-reprobe + recovery (carries the win — the only slice needing the sim A/B on `topo_9node.json` + the metal node-72 tune). Slices 1–3 are delivery-neutral by construction (safe to commit/flash ahead of 4–6).

## Why

Every unicast forward is **RTS → CTS → DATA → ACK**, so a single hop A→B needs the link **bidirectional**: RTS(A→B), CTS(B→A), DATA(A→B), ACK(B→A). An **asymmetric** link (A's transmissions reach B, B's don't reach A — different noise floors / TX power / interference at each end, extremely common in real LoRa) can **never** complete that hop. A **handshake-isolated** node (e.g. 204 & 247 in the live node-72 topology) has *zero* bidirectional links — it is fully present in the *hearing* graph (we decode its beacons) yet unreachable by the handshake model. ~37 % of the node-72 DMs touch such nodes; baseline delivery (~57 % sim / ~63 % metal) sits on this structural ceiling.

The route plane is **symmetry-blind**: routes are adopted from *one-directional* reception (hearing a beacon → installing a route via the sender), and the liveness plane is **beacon-cleared** — `clear_peer_suspect` fires on *any* RX including an isolated peer's beacons (`node_beacon.cpp:79`), so liveness rates a handshake-isolated node as *healthy*. Asymmetry is discovered only at DATA-plane RTS-giveup, after a doomed peer spams **~9–80 RTS over ~60 s** of cascade-requeue — stealing airtime (the scarce resource; EU 1 % duty cycle) and battery.

**Goal:** give the route plane a first-class **bidirectionality signal**, enrich the table with per-link one-way state, *prefer* confirmed-bidirectional paths, *propagate* the knowledge mesh-wide, and *stop the doomed airtime burn* — while **keeping** one-way routes in the table (reversible; no re-discovery ceremony when they flip back to bidirectional).

## Locked design decisions (brainstorm 2026-06-29)

1. **Ambition = avoid-and-enrich.** No flood (rejected — airtime). Airtime is the prime constraint. Tag/enrich, don't delete; reversible.
2. **Detection = proactive gossip** (not local-inference-only) — to avoid the doomed-RTS *discovery ceremony*.
3. **Encoding = reuse existing `hops=1` route entries** as the heard-set (no new heard-set bytes).
4. **Authoritativeness = a 1-bit "heard-set complete" flag** — *absence* ⇒ one-way only when the page is complete; truncated ⇒ "unconfirmed" (never demote). Auto-adapts: engages in sparse, disengages in dense.
5. **Cadence = the complete direct-neighbor census on every ~15-min periodic beacon** (neighbor entries get slot priority; remote routes rotate the leftovers) — but the census **engages only when the direct-neighbour set fits while leaving remote-route rotation headroom**; otherwise it skips (complete-flag clear) and the beacon is exactly today's. So dense meshes are genuinely inert (no convergence cost), and the feature self-disengages precisely where asymmetry doesn't matter.
6. **Selection = soft penalty** in the existing sort; never hard-drop a sole path.
7. **Advertise = mark degraded on-wire** (1 route-entry bit), transitive, cleared on recovery — *not* suppress/omit (keeps the route visible + recoverable mesh-wide).
8. **Isolated sole-route = slow re-probe** (one attempt per TTL), not fast-fail — retains marginal/lucky deliveries + auto-recovery.

## Current state (verified 2026-06-29 by code inspection — anchors)

- **Beacon capacity:** `beacon_max_bytes = 151` (`protocol_constants.h:99`). ⚠ CORRECTED (review 2026-06-29): `kMaxBeaconEntries = 35` (`node_beacon.cpp:24`) is ONLY the **static array ceiling**. The true per-beacon usable count from `beacon_max_entries()` (`frame_codec.cpp:74-78`) subtracts overhead = `8 + BCN_LEAF_HEADER_LEN(6)` = 14 B → **(151−14)/4 = 34** with no blocks (NOT 35), **26** with the bitmap (NOT 27), fewer with a schedule/ext-TLV. The census headroom math MUST use the live `beacon_max_entries()`, never 35. Route entry = 4 B: `dest | next | score_bucket(4b) | rsv(3b) | is_gateway(1b) | hops` (`frame_codec.h:51-58`).
- **Beacon flags:** byte 2 = `has_schedule | self_gateway | is_mobile | has_seen | has_ext | n_entries_lo`. ⚠ CORRECTED (review 2026-06-29): **byte 3 = `n_entries_hi(b7..5) | rsv(b4) | wire_version(b3..0)`** (`frame_codec.cpp:39` pack, `:96` parse) — **NOT** "rsv(5 spare bits)". There is **exactly ONE free bit (b4)**; `b3..0` is `wire_version` (`protocol_constants.h:77`), load-bearing for the §7c cross-version join (old peers reject any beacon whose `bytes[3]&0x0F != 1`). The heard-set-complete flag MUST take **bit 4 only**. The `frame_codec.h:43` comment (`rsv(b4..0)`) is itself stale → fix it to `rsv(b4) | wire_version(b3..0)` in the codec change. (Route-entry rsv: byte-2 b3..1 ARE genuinely free — `frame_codec.cpp:61/148-149` — so the `degraded` bit is sound there.)
- **Cadence:** `beacon_period_ms = 900000` (15 min, `node.h:51/106`); discovery 5 s; triggered beacons 2–10 s on `rt_changed` (`node_beacon.cpp:622`). Steady-state beacons are **dirty-only differential** (`dirty_only = !in_discovery() && kind!="sync"`, `node_beacon.cpp:205`).
- **Entry selection** (`node_beacon.cpp:311-330`): Phase 1 packs **all dirty** entries; Phase 2 round-robins **stable** entries from the `_beacon_offset` cursor **only on full-page (`!dirty_only`) beacons**; dirty cleared after pack (`:359`).
- **Freshness vs period:** `next_hop_live_ttl_ms = 1200000` (20 min, `protocol_constants.h:120`) **>** beacon period (15 min) ⇒ a neighbor heard each period stays "fresh," so a same-SNR re-hear ties in `route_strictly_better` → `MergeAction::none` (`node_routing.cpp:263-267`) — **no re-dirty**. So the `[dest=A,next=A,hops=1]` "I-hear-A" entry is dirtied at first-learn (`:247`), promote/evict, or a strictly-better-SNR refresh (`:259`) — *not* every period. (Route age-out: neighbor 45 min, remote 3 h — `protocol_constants.h:118-119`.)
- **Route table:** `RtEntry{dest, RtCandidate candidates[3], n, dirty}`; `RtCandidate{next_hop, score, hops, last_seen_ms, n2_hop, is_gateway, learned_layer_id}` (`node.h:194-199`). `cap_routes` per leaf. `rt_merge` (`node_routing.cpp:230`) installs/refreshes; `route_strictly_better` (`:99`) decides betterness; `effective_score = score − budget_penalty_q4 − liveness_penalty_q4` (`:83`).
- **Selection / cascade:** `pick_next_cascade_hop` (`node_cascade.cpp:52`) two-pass; `next_hop_selectable` (`:38`) skips sentinel/loop/tried/blind/mobile-transit (not a freshness hard-gate). §P3 RREQ on cascade-exhaustion-with-silent-primary (`:108`).
- **Liveness plane:** `PeerLiveness` (`node.h:1013`) — tiers suspect/silent/dead from `record_peer_rts_timeout` (`node_routing.cpp:464`), **cleared by any RX** via `clear_peer_suspect` (`:483`). Penalty `liveness_penalty_q4` {192/640/1280 Q4} (`:70`). **No bidirectionality / reverse-link tracking exists today.**
- **★ Precedent for degraded propagation:** `rt_skip_silent_n2` (`node_beacon.cpp:586`) — a node already **refuses to install** a beacon-carried route whose advertised next-hop (`e.next`) is locally known silent/dead. The degraded plane generalizes this from "drop" to "mark + propagate + recover."
- **Latent heard-set machinery:** the 32-B `seen_bitmap` (`seen_bitmap_enabled=false` default) was a *who-I've-heard* bitmap; it cost −7 s18 deliveries of beacon capacity (35→27 entries) and gave no measured benefit when applied to *freshness*. We do **not** revive it — the heard-set rides the existing route entries instead.

## The mechanism

### 1. Detect — bidirectionality via the heard-set

The only unknown for "can A use B as a next-hop" is **"does B hear A?"** (A already knows it *hears* B — it has a route to B). B answers it implicitly: B's `hops=1` route entry `[dest=A, next=A]` means **"B hears A."**

A node Q, reading neighbour P's beacon (Q hears P ⇒ P→Q works):
- `[dest=Q, next=Q, hops=1]` **present** ⇒ P hears Q ⇒ Q→P works ⇒ combined with P→Q ⇒ **Q↔P confirmed bidirectional.**
- **absent AND P's complete-flag set** ⇒ P does *not* hear Q ⇒ **Q→P one-way.**
- **absent AND complete-flag clear** (P truncated its page) ⇒ **unconfirmed** — no state change, no demotion.

This is OLSR-style link sensing, adapted. **Endpoint override (capture this):** the degraded bit on the entry whose `dest == self` is ignored by that endpoint — a node that *received* the beacon has live proof the forward hop works; the degraded bit governs *third-party* path choice, not the endpoint's own link.

### 2. Wire changes — two backward-compatible bits

- **Beacon "heard-set complete" flag** — **byte-3 bit 4 ONLY** (the single free bit; ⚠ NOT a low-nibble bit — `b3..0` is `wire_version`, see Current-state correction). Set by the emitter when **all** of its `hops=1` direct-neighbour entries are present in this beacon (none dropped for capacity). Default 0. A codec round-trip test MUST assert `wire_version` survives with the complete-flag set.
- **Route-entry `degraded` flag** — 1 of the 3 `rsv` bits in the 4-B entry (`frame_codec.h:51-58`): `dest | next | score_bucket(4b) | degraded(1b) | rsv(2b) | is_gateway(1b) | hops`. Default 0.

Both default 0 ⇒ an old node reads as "not complete-authoritative / not degraded," and a new node reading an old node never false-demotes. **No flag-day, no `wire_version` hard-bump** (add a telemetry/observability note only). Codec: extend `pack_beacon`/`unpack_beacon` and the route-entry encode/decode; add round-trip unit tests.

### 3. Store — per-neighbour bidi state + candidate degraded

- **Per-neighbour link state**, full-range (keyed by node-id, survives `PeerLiveness` LRU like `_dest_seen_ms`): `enum LinkBidi { unknown, confirmed, one_way }` packed as 2 bits — `uint8_t _link_bidi[256]` per `LayerRuntime` (256 B/layer; room to grow). Last-confirmed freshness reuses the existing `_dest_seen_ms[256]` / `next_hop_live_ttl_ms` machinery — a confirmation older than the freshness TTL decays toward **`unknown` only** (MF6: never `one_way`, which needs positive absent+complete evidence).
- **`RtCandidate.degraded_from_wire`** (per-candidate stored bool — the wire-inherited component ONLY, a fact about what the advertiser advertised). The effective degraded state is **recomputed live** each `rt_merge` / at score+advertise time as `degraded_from_wire OR (_link_bidi[next_hop] == one_way)` (OI1/MF5 — NOT a single sticky cached bool, which would go stuck-degraded). A `_link_bidi[next_hop]` transition fans out via the `resort_routes_for_neighbor_penalty` pattern (`node_routing.cpp:158`) to re-sort + re-dirty + propagate.

### 4. Select — soft penalty, never drop sole

Add `bidi_penalty_q4(next_hop, candidate)` to `effective_score` (`node_routing.cpp:83`), composing with `budget_penalty_q4` + `liveness_penalty_q4`. Ordering: **confirmed ≻ unknown ≻ one_way/degraded.** Suggested seeds (Q4 dB, metal-tunable): `confirmed = 0`, `unknown = 0` (prefer confirmed without punishing the not-yet-seen), `one_way/degraded ≈ peer_silent_penalty_q4` class. It rides the **sort** (`route_strictly_better`), so a degraded route **stays selectable when it is the sole candidate** (no false-negative delivery loss) — same altitude as the liveness penalty, *not* a `next_hop_selectable` hard gate (the Phase-2 freshness-hard-gate regression of 2026-06-18 is the cautionary precedent).

### 5. Advertise / propagate degraded

When emitting a route entry whose next-hop's `LinkBidi == one_way`, set `degraded = 1`. The bit is **transitive**: on `rt_merge` of an incoming entry `[dest=C, next=…, degraded=d]` learned via advertiser P, the resulting candidate (next_hop = P) is degraded if `d == 1` **OR** `_link_bidi[P] == one_way`. So degradation flows downstream to every node that would route *through* the bad hop; SNR (`score_bucket`) is left untouched (a one-way link can have excellent SNR — that's the whole point of a separate bit). Generalizes the existing `rt_skip_silent_n2` (`node_beacon.cpp:586`) from drop → mark. No loop risk (degradation only flows downstream; it never feeds back into `_link_bidi`, which is set ONLY by local detection/CTS).

**Recompute, never sticky-OR (MF5 / OI1).** Derive the candidate's effective `degraded` FRESH on each `rt_merge` as `degraded_from_wire OR (_link_bidi[next_hop] == one_way)`. Store ONLY the **wire-inherited component** as a per-candidate field (`RtCandidate.degraded_from_wire` — it is a fact about what the advertiser advertised, not locally re-derivable); recompute the local `_link_bidi` component live at score/advertise time. A single sticky cached bool that OR-accumulates across merges goes **stuck-degraded** and never clears on recovery (defeats §7). On a `_link_bidi[next_hop]` transition, fan out via the `resort_routes_for_neighbor_penalty` pattern (`node_routing.cpp:158`) to re-sort + re-dirty + re-advertise + propagate the recovery.

### 6. Isolated sole-route — slow re-probe

For a **sole** route whose next-hop is authoritatively `one_way` (the handshake-isolated dest, e.g. 204/247: we hear its beacons so the route is fresh+viable+sole), do **not** fire the ~9–80-RTS cascade burst. Instead **re-probe once per `link_reprobe_ttl_ms`** — a single RTS attempt that (a) costs almost nothing, (b) catches the metal *lucky marginal-link* deliveries the idealized sim can't see (the #3 metal caveat: 0 % sim vs ~40 % metal), and (c) **a successful CTS is a positive recovery signal** → `LinkBidi → confirmed`, clear degraded. Keep the route in the table throughout (reversible, no re-discovery).

**Interception site (MF4 — this is a NEW plane, not §P3).** The existing §P3 RREQ does NOT catch this: it fires only when `liveness_penalty_q4(from_next) >= peer_silent_penalty_q4` (`node_cascade.cpp:112`), but a one-way isolated node stays **liveness-HEALTHY** (`clear_peer_suspect` fires on its every beacon, `node_beacon.cpp:79`), so §P3 never triggers and the giveup falls straight to `try_cascade_requeue` = the exact burst we target. The bidi plane is ORTHOGONAL to liveness. So add a NEW check keyed on `_link_bidi[from_next] == one_way` in the giveup path **before** `try_cascade_requeue` — the `cascade_to_alt` no-alt branch (`node_cascade.cpp:107-115`) or `rts_timeout_fire` — that throttles to one RTS per `link_reprobe_ttl_ms`. The sole-route's single probe still flies (delivery must not regress). §P3 departed-relay recovery is unaffected (it keys on liveness-silent — orthogonal).

### 7. Recover — reversible, no re-discovery ceremony

`one_way → confirmed` when **either** the node reappears in the neighbour's *complete* heard-set (gossip) **or** a re-probe gets a real CTS (local). On recovery: `_link_bidi → confirmed`, candidate `degraded` cleared, the clean route re-advertised (`dirty`), and recovery propagates downstream. Because the route was never deleted, there is **no re-discovery flood** — the entire "ceremony" the user flagged is skipped.

## Constants (named, tunable — seeds; gate-sweep + metal-tune)

```
bidi_penalty_one_way_q4     = peer_silent_penalty_q4 (640 Q4) seed — clean ordering (one_way sorts below any viable
                            // confirmed/unknown). FALLBACK peer_suspect_penalty_q4 (192 Q4) if the metal lucky-marginal
                            // gate shows good-RF one_way routes stranded vs RF-worthless unknown alts. unknown = confirmed = 0 (OI2).
bidi_confirm_ttl_ms         = next_hop_live_ttl_ms (1200000)  // confirmation freshness; decays confirmed -> UNKNOWN ONLY
                            // (MF6: never -> one_way — one_way requires positive absent+complete evidence, not mere staleness).
link_reprobe_ttl_ms         = 60000 (seed; the #3 TTL lesson — metal-tune the lucky-delivery balance)
heard_set_census_headroom   // a named fraction of the LIVE beacon_max_entries(151, sched, bitmap, ext) — true cap 34, NOT 35
                            // (MF2) — evaluated AFTER the variable blocks are sized (node_beacon.cpp:304-307). Run the census
                            // (all hops==1 entries + complete-flag) ONLY when the FULL hops==1 set fits leaving >= this many
                            // slots; else skip (flag clear) -> today's beacon. LEAF-ONLY (gateways skip, OI3). Auto-disengage.
```

## Compat / rollout

- Both new bits default 0 → **clean incremental rollout**, mixed old/new fleet safe, no flag-day.
- **Local no-CTS backstop DEFERRED** — gossip-first. The local `rts_no_cts` streak (the #3 signal, reset only by a real CTS) is the natural detector for the dense/truncated (flag-off) case where gossip can't make absence authoritative; add **only if** metal shows that case needs it. Hooks left, not built.
- **The census is a NEW packing mode (MF3), not a reuse of Phase-2 rotation.** A steady-state periodic beacon is `dirty_only=true` (`node_beacon.cpp:205`) and the Phase-2 stable/remote rotation is `!dirty_only`-gated (`:322`) → DORMANT on exactly the beacon class the census targets. So the census is a NEW partition pass over `_active->_rt` (filter `candidates[0].hops==1`) that force-injects all direct-neighbour entries + sets the complete-flag. It MUST: (a) coexist with the Phase-1 dirty pack + the post-pack dirty-clear (`:359`) **without re-dirtying every period** (else every node beacons a full page each 15 min, defeating the differential design); (b) set the complete-flag ONLY when the full `hops==1` set fit THIS beacon (per the live `beacon_max_entries`, cap 34); (c) hard-re-check it never pushes the beacon over `beacon_max_bytes` (the F2 silent whole-beacon drop, `:345`). **Leaf-only (OI3):** gateways skip the census by construction (they already skip the seen-bitmap + channel digest). Dense meshes ALSO self-throttle via the R4.3 channel-busy beacon suppression — so "sparse-engages / dense-inert" holds via BOTH levers. The seen-bitmap convergence regression does NOT recur: that was +32 B shrinking the full-page DISCOVERY budget (35→27, slow cold-start); the census adds ZERO bytes to a beacon that fits and rides steady-state beacons — a different class.

## Tests / gate

- **Native:** codec round-trip for both bits; `route_strictly_better`/`effective_score` with `bidi_penalty` (confirmed ≻ unknown ≻ one_way; **sole one-way stays selectable**); degraded inheritance through `rt_merge` + clear-on-recovery; the heard-set parse (present→confirm, absent+complete→one_way, absent+incomplete→unconfirmed); endpoint-override; slow-reprobe (one attempt/TTL, CTS→recover).
- **★ Sim A/B (the airtime gate):** the node-72 9-node topology (`simulation/topo_9node.json` — already carries the asymmetric 204/247) at feature off↔on. Pass = **DM delivery ≥ baseline** (must not regress) **and** total `rts_tx` to the isolated nodes **drops** (the airtime win) **and** no false-demotion (a confirmed-bidi link is never penalized). Watch a *recovery* case (flip a link bidi→one-way→bidi, confirm the degraded bit propagates then clears).
- **BASELINE suite** (`simulation/BASELINE.md`): result-comparison (the wire bits shift byte-md5) — s18 ≥ baseline, leaks 0, cross-layer held, churn sane. The census emit-policy must stay delivery-neutral where there's no asymmetry (dense = flag-off = inert).
- **Boards:** all 4 build (lib/core + frame_codec).
- **★ Metal (the real delivery gate):** node-72 / lab harness, off↔on. Watch the isolated nodes' **own** delivery (the lucky-marginal balance the sim is blind to) + their airtime; tune `link_reprobe_ttl_ms`. The authoritative judgment of whether the slow-reprobe TTL trades airtime for lucky deliveries correctly.

## Open items — RESOLVED (2026-06-29 review)

- **OI1 — `RtCandidate.degraded` storage:** SPLIT. Store ONLY `degraded_from_wire` per candidate; recompute the local `_link_bidi[next_hop]==one_way` component live; fan out on a `_link_bidi` transition via the `resort_routes_for_neighbor_penalty` pattern (`node_routing.cpp:158`). NOT a sticky cached bool (would go stuck-degraded — see §5/MF5).
- **OI2 — `bidi_penalty` for `unknown`:** strictly **0** (= confirmed); only positively-confirmed `one_way` is demoted (a nudge on `unknown` punishes every not-yet-probed link on a cold mesh — the freshness-hard-gate mistake). Gate-sweep may raise later.
- **OI3 — gateway census:** **leaf-only** feature; gateways skip by construction (not via the headroom rule happening to disengage).
- **OI4 — telemetry:** implement all 5 (`link_bidi_confirm`/`link_one_way`/`degraded_advertise`/`link_reprobe`/`link_recover`), `MR_EMIT`-gated (off on metal) — the oracle for the sim-A/B airtime gate + metal trace.

All fed into the 7-slice implementation plan in §"REVIEW VERDICT + RESOLUTIONS".

## Bottom line

A **bidirectionality plane** the route table never had: nodes learn which links complete a handshake by **reading who their neighbours hear** (reused `hops=1` entries + a 1-bit completeness flag — the only added airtime is the periodic direct-neighbour census, which is sparse-cheap and self-disengages when dense), **prefer** confirmed-bidirectional paths, **propagate** one-way knowledge as a transitive `degraded` bit, **stop** the doomed-RTS storm on isolated nodes via slow re-probe, and **recover** for free when a link flips back — all keep-don't-delete and backward-compatible. This is the algorithm-level answer the saturation investigation concluded was needed; the delivery ceiling on genuinely-isolated nodes remains RF/deployment (rx-boost, siting), but the *airtime* hemorrhage and the *route-poisoning* end here.
