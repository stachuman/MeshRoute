<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel repair — holder-aware digest retirement + air-honest accounting

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Firmware change; **no wire change** (the CHANNEL_DIGEST TLV, the M-frame, and `channel_msg_id` are all untouched — only *when a holder stops advertising* changes).

## Why

A channel message can end up **held only by its origin** (zero other holders) and then be **permanently orphaned** — bench-proven on the 8-node net (run `3ad5d2`): origins **247 and 129 reached 0/7 even at the +300 s eventual repull**, and 129 has 0 channel records across 4 runs while its DMs deliver fine. The channel was on air (`»tx M ... id=F7908903 sf=9`, fresh monotonic id) but no node caught the one-shot flood, so there was **nothing for the repair-pull to seed from**.

Root cause (traced read-only): the repair digest **retires an advertisement on a blind count, ignoring whether anyone received it.** `build_channel_digest_ext` (node_channel.cpp:312-322) does `if (++bcn_ad_count >= channel_dirty_max_advertisements[=3]) e.dirty = false` — after 3 advertisements the entry stops being advertised **regardless of `seen_by`**, and there is no re-arm. An originated entry starts `seen_by`-empty (`do_send_channel:264`), so it can retire held-by-nobody → no advertisement → no pull → orphan. The tell is an internal inconsistency: **eviction already respects holders** (`channel_buffer_pick_eviction:108-121` won't safe-evict until `seen_by` covers all live 1-hop neighbours) but **retirement does not**.

Two compounding accounting bugs make it worse under contention: `build_channel_digest_ext` runs at beacon **build** (node_beacon.cpp:288), incrementing `bcn_ad_count` **before** the LBT TX (`:352`, `tx_flood`), and the `tx_flood` result (`sent`) is `[[maybe_unused]]`. So an advertisement that **LBT-suppresses or pack-drops still burns the budget** without ever going on air.

This is the design's own sanctioned next step: `2026-06-08-channel-flood-redesign.md` §10 deferred *"Originator re-flood on low coverage — rely on the repair backstop. **Revisit if holes prove common.**"* Holes are now proven common. We do **not** add the heavier re-flood here (that escalation = a future phase); we make the **existing** repair layer holder-aware and air-honest.

**Decisions (user, 2026-06-23):** scope = **A (holder-aware retirement) + B (count only adverts that aired)**. The originator re-flood (design §10 / "C") is explicitly **out of scope** — revisit only if A+B don't clear the bench.

## Verified current state

- **Retirement (the bug):** `build_channel_digest_ext` (node_channel.cpp:307-326) walks the buffer newest-first, advertises up to `channel_dirty_max_per_bcn=3` dirty ids, and per advertised entry `++bcn_ad_count; if (bcn_ad_count >= _cfg.channel_dirty_max_advertisements) e.dirty=false`. No `seen_by` check; no re-arm (`dirty=true` only at `do_send_channel:264` originate / `:193` first-receive).
- **The predicate to reuse:** `channel_buffer_pick_eviction` (node_channel.cpp:110-123) builds the live 1-hop set — `for rt[i]: if rt[i].n>0 && rt[i].candidates[0].hops==1 → nbrs[nn++]=rt[i].dest` — then for an entry tests `for j: seen_test(e.seen_by, nbrs[j])`. `seen_test`/`seen_set` at :35/:51; `channel_mark_seen_by` at :62 (stamped from a neighbour's digest cross-ref at :361 and a served pull at :486).
- **The accounting (the air-honesty bug):** `emit_beacon` builds the digest at node_beacon.cpp:288 (before TX), transmits via `const bool sent = tx_flood(...)` at :352 (LBT + duty pre-check; result ignored), and stamps `_last_beacon_tx_ms = _hal.now()` unconditionally at :353. The route-entry page already has the right pattern — a *post-build* dirty-clear of only the entries that landed (:355+).
- **Constants** (protocol_constants.h): `channel_dirty_max_advertisements = 3` (:225), `channel_dirty_max_per_bcn = 3` (:221), `cap_channel_buffer = 32` (:217).

## The change

### A. Holder-aware retirement (replace the blind K=3)

1. **Add the predicate** — `bool Node::channel_entry_fully_seen(const ChannelEntry& e) const`: build the live 1-hop neighbour set as eviction does (rt `hops==1`); return `true` iff **`nn == 0`** (no live neighbour to serve → nothing to advertise → retire) **or** every `nbr ∈ e.seen_by`. ⚠ **Do NOT refactor `channel_buffer_pick_eviction` onto this** — eviction's `nn==0` path is *fallback-evict-oldest with `*safe=false`*, the OPPOSITE of retirement's `nn==0=retire`, so sharing the whole function would flip eviction's telemetry mode (`fallback`→`safe`) and is a silent regression. Accept the ~5-line `nbrs`-scan duplication; leave eviction's tested behaviour untouched.
2. **Retire on coverage, not count.** An entry retires (`dirty=false`) when **`channel_entry_fully_seen(e)`** — its live 1-hop neighbours all hold it. `bcn_ad_count >= channel_dirty_max_advertisements` becomes a **horizon SAFETY backstop only** (bounds the asymmetric case: a neighbour we *hear* but that never hears *us* is in our hops==1 set yet never pulls → never covered → would otherwise advertise forever). **Raise `channel_dirty_max_advertisements` to a generous horizon** (propose `16`; final value tuned at the gate against s15/s17 airtime) so the backstop is a safety net, not the primary trigger. Net rule: `retire = channel_entry_fully_seen(e) || bcn_ad_count >= channel_dirty_max_advertisements`.
   - Result vs today: a message neighbours already hold retires **sooner** (after 1-2 beacons, when they pull) — *less* air in the common case; an orphan keeps advertising until pulled, until its neighbours go stale-and-drop (liveness plane removes them from hops==1 → coverage becomes vacuously true → retire), or until the horizon — *more* air only on the genuinely-unheld tail. Buffer eviction (`cap_channel_buffer`) remains the ultimate bound (a dirty orphan is "unsafe" so it survives until buffer pressure force-evicts the oldest).

### B. Count only advertisements that actually aired

3. **Split `build_channel_digest_ext` into SELECT + COMMIT.**
   - SELECT (at the current call site, node_beacon.cpp:288): build the TLV and **return the picked ids** (it already assembles `ids[]`) — but do **NOT** mutate `bcn_ad_count`/`dirty`. Pass the picked ids back to the caller (e.g. fill a caller-provided `uint32_t picked[channel_dirty_max_per_bcn]; uint8_t& npicked`).
   - COMMIT — add `void Node::commit_channel_digest_advertised(const uint32_t* ids, uint8_t n)`: for each id, **re-find by id** (`channel_buffer_find` — robust to any reorder) and apply the per-advertisement side effects: `++bcn_ad_count` and the (A) retirement check. (Re-find, not index, since indices could shift; n ≤ 3 so the cost is nil.)
4. **Gate the commit on air.** In `emit_beacon`, keep `const bool sent = tx_flood(...)` (already captured) and call `commit_channel_digest_advertised(picked, npicked)` **only when `sent`**. An LBT-suppressed / pack-dropped beacon (the `:343` overflow return is already before TX) therefore never burns an advertisement. Mirror the existing route-entry post-build clear (:355).
   - **OUT OF SCOPE (flag, do not fix here):** `_last_beacon_tx_ms` is also stamped unconditionally at :353 even when `!sent` — a broader beacon-timing concern (it feeds the min-interval throttle for *all* beacons, with Lua-parity implications). Note it in the PR; it gets its own analysis.

### Telemetry

Keep the `channel_dirty_cleared` emit on retirement, and add the retire **reason** (`"seen"` vs `"horizon"`) so the gate/bench can see which path fired. The `bcn_ad_count` in that event now reflects aired-only advertisements.

## Not a wire change · sim IS affected (gate on the breakdown, not bytes)

No frame, no `channel_msg_id`, no TLV layout changes — only the holder-side retire timing. But unlike the e2e-ack receipt, this **changes the digest advertisement stream**, so it is **not** byte-neutral in the sim: the s15/s17 channel plane will move. This is a **deliberate divergence from the faithful Lua K-retire** (dv:1034/1453 — the Lua retires on the same blind count; it doesn't reboot or contend the same way, so it doesn't manifest the orphan). Gate on **no channel-delivery regression**, not the byte stream.

⚠ **Confirm the sim's `tx_flood` `sent` semantics for (B):** if the sim engine's `tx_flood` never returns `false` (no LBT/channel-busy failure in the sim model), then (B) is sim-neutral (build == aired). If the sim CAN LBT-fail, (B) shifts the sim digest stream too — fold it into the same gate. Verify before gating.

## Tests / gate

- **Native unit** (`test_node_channel.cpp`):
  - **(A) holder-aware:** an entry whose `seen_by` does NOT cover a live 1-hop neighbour stays `dirty` past 3 advertisements (today it would retire — the regression the bench hit); after `channel_mark_seen_by` covers all live neighbours, the next commit retires it. An entry with `nn==0` retires. The horizon backstop retires a never-covered (asymmetric) entry after `channel_dirty_max_advertisements`.
  - **(B) air-honest:** drive `tx_flood` to return `false` via the `TestHal` (LBT/channel-busy) → `bcn_ad_count` does NOT increment and the entry does NOT retire; with `sent==true` it does. The SELECT step alone mutates nothing.
  - **eviction unchanged:** the refactor of `channel_buffer_pick_eviction` onto `channel_entry_fully_seen` keeps its existing tests green (safe vs fallback).
- **Sim / BASELINE:** rebuild `lus`; run the channel scenarios — **s15 channels ≥ baseline (218/224, leaks 0)** and **s17 channels leaks 0** + scale, and **flood/digest airtime not materially up** (the orphan tail is bounded; the common case retires *sooner*). **STOP and ask if channels regress or airtime balloons** (same discipline as the parked bidirectional-flood fix). s18/s19 (single-layer, no/low channel) unaffected — spot-check unchanged.
- **Boards:** `pio run -e gateway -e xiao_sx1262 -e heltec_v3` (touches node_channel.cpp + node_beacon.cpp + node.h + protocol_constants.h).
- **Metal (user bench):** re-run `run scenario-oracle.txt`. Channel **eventual** should clear the orphan origins (247/129-type now keep advertising until a neighbour pulls) — expect eventual > the 38/56 baseline, ideally the leaf. Confirm duty/airtime acceptable. The harness's `e2e-acked` DM column is unaffected (this is the channel plane).

## Sequencing / notes

- Independent of the e2e-ack receipt work (different plane) — order doesn't matter.
- Holds the design §10 **originator re-flood (C)** in reserve: if a chronic origin (129) still stalls after A+B — i.e. its neighbours never hear *any* of its beacons — that's the case only an active re-push fixes, and it gets its own spec.
