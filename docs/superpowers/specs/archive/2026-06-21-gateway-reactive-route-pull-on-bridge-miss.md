<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Gateway reactive route-pull on a cross-layer bridge miss

**Status:** ✅ **READY FOR CODER** (2026-06-21). POC validated + in the working tree (`TEST`-marked, user-confirmed correct) — coder finalizes it per the §9 build order + adds the §6 tests; the quality-gate runs §6 before the author commits. This spec also **preserves the reasoning** (§1 root cause, §2 tried-and-rejected, §4 the defer exception) so it isn't lost.
**Relationship:** complements `2026-06-20-gateway-window-broadcast-sync.md` (the *align*, committed `3dbaa14`). Align carries the 2-layer dense case (s16); THIS fix carries the 3-layer route-starved case (s15). Keep both.

---

## 0. The gate

- **s15 (3-layer) cross-layer**, multi-seed MEAN: rises from ~45% to **~90%**, worst-case seed (1522) off the floor (0 → 13-17/21). `leaks==0`.
- **No regression:** s16 ≥ 71% (via align), s09/s10 = 100%, s18/s19/s17 untouched.
- **Airtime ≈ flat:** the pull is reactive (a REQ_SYNC only on an actual miss, rate-limited) — NOT a change to the beacon cadence. Confirm beacon airtime unchanged; REQ_SYNC count is small and bounded.
- Native suite green + new units (below); 4 boards build.
- Gate cross-layer on a **multi-seed mean**, never the single configured seed (it's seed-sensitive 0–57% pre-fix).

---

## 1. Root cause (the WHY — verified, don't re-derive)

A gateway **time-multiplexes** its layers (on layer L only during L's window). Steady-state beacons are **dirty-only** (`node_beacon.cpp:204` `dirty_only = !in_discovery() && kind!="sync"`, Lua dv:7606) — they carry only *changed* routes, never the stable table. The Phase-2 stable-rotation is gated `!dirty_only`, so it runs only in discovery or for a "sync" beacon.

Chain to the bug:
1. A stable route to a far-layer dst is advertised **once** (while dirty — in discovery or a triggered beacon).
2. The gateway is **on its other layer at that instant** → misses it.
3. Once clean, the route is **never re-advertised** (steady-state = dirty-only).
4. The only full-table recovery — a "sync" beacon via REQ_SYNC — is **discovery-gated** (the REQ_SYNC loop fires only while discovering + route-starved), and the gateway has exited discovery.
5. ⇒ the gateway is **permanently missing** that far-layer route → on a bridged cross-layer DM it hits `send_no_route` and drops it. s15 cross-layer collapses (seed-dependent 0–57%).

**Evidence:** in s15, `stable_n=0` in 2838/2844 beacons, 94% of beacons carry zero route entries, only 1 "sync" beacon all sim; the dropped dsts (16/13/26) are known *only* by same-layer regulars and never reach the gateways.

**Ruled OUT — do not re-chase:** byte-budget / beacon truncation. A diagnostic `beacon_tx` byte-breakdown metric showed `max_entries≈31` with the bitmap absent — plenty of room. The cause is the dirty-only *re-advertisement gap* × gateway time-multiplexing, not packing.

---

## 2. What was tried, and why pull+defer won (the empirical record)

Same s15 multi-seed harness (vary the scenario seed), measured per approach:

| approach | s15 mean | s16 | s09/s10 | airtime | why kept/rejected |
|---|---|---|---|---|---|
| baseline (align only) | 45% | 71% | 100% | — | the bug |
| **push:** full BCN every beacon for gw-neighbours | 84% | **35% ↓** | **50% ↓** | high | regresses airtime-constrained scenarios — too much air |
| **push:** throttled full (once/90 s) | 80% | 61% | 100% | medium | no regression, but a **120× hack** vs the ~3 h normal full-BCN cadence — wasteful, proactive, blind |
| **pull:** REQ_SYNC on miss, **drop** the DM | 58% | 71% | 100% | minimal | clean, but **first-packet loss** (DM dropped while the route is fetched) caps it |
| **pull: REQ_SYNC on miss + DEFER** | **90%** | 71% | 100% | **minimal** | ✅ **WINNER** — no first-packet loss, minimal air, reuses existing machinery |

Key reasoning, captured:
- **Pull beats push.** The gateway is the one entity that *knows* it's missing a route (the `send_no_route`). A proactive push makes every gw-neighbour broadcast full tables blindly — wasteful (a normal node sends a full BCN only ~every 3 h). A reactive pull fires only on a real miss.
- **Defer beats drop.** Dropping the bridged leg loses the first DM to each new dst (route learned only afterward). Deferring it (park + retry) delivers it once the pulled route lands — that's the 58% → 90% jump.
- **align is NOT redundant.** Verified: pull+defer gives s15 ~88–90% with *or* without align, but **s16 = 71% with align / 50% without**. Align (timing, gets beacons heard in-window) carries the 2-layer dense case; pull+defer carries the 3-layer route-starved case. They are complementary → keep both.

---

## 3. The solution

In `issue_send` (`node_mac.cpp`), the no-route branch — currently a forwarder drops, an originator defers:
```cpp
if (item.is_forward) {
    MR_EMIT("send_no_route", EF_I("dst", item.dst));
    if (item.is_gw_relay) {                       // a GATEWAY bridging a transit DM with no far-layer route:
        send_req_sync_q("gw_relay_no_route", /*force=*/true);  // (1) reactively PULL the full table
        defer_send(item);                                      // (2) park + retry — do NOT drop transit
    }
} else {
    defer_send(item);                             // originator (unchanged)
}
```
And `send_req_sync_q` gains a `force` flag that bypasses the boot-flag and route-rich guards (keeps the rate-limit):
```cpp
void Node::send_req_sync_q(const char* reason, bool force = false) {
    if (!force && !_cfg.req_sync_on_boot) return;
    ... if (_last_req_sync_tx_ms && now - _last_req_sync_tx_ms < req_sync_retry_ms) return;   // rate-limit (kept)
    if (!force && _active->_rt_count >= _cfg.req_sync_min_routes) return;   // route-rich guard — bypassed by force
    ...
}
```
The pulled REQ_SYNC makes far-layer neighbours reply with full "sync" beacons (existing `schedule_sync_response` → `emit_beacon("sync")`); the gateway, in-window, learns the route; `try_drain_deferred` re-sends the parked DM.

**Why `force`:** the existing REQ_SYNC is for *route-starved* boot ("give me everything, I have almost nothing"). Here the gateway is route-*rich* but missing **one specific** far-layer dst — so the `rt_count >= req_sync_min_routes` guard must be bypassed. `force` says "pull because I'm missing THIS route," not "because I'm starved."

---

## 4. The defer-for-gateway-relay exception (design call to make explicit)

A normal forwarder **drops** a no-route transit DM (dv:7041-7048 — "it can't hold someone else's transit"; memory/fairness). This fix makes a **gateway-relay** leg the deliberate **exception**: it defers (parks + retries) instead. Justification: a gateway bridging cross-layer is effectively the **originator on the far layer** (it's injecting the DM there), so holding it briefly until the pulled route lands is its job — and it's bounded by the existing `send_defer_ttl` giveup. This must be stated in the code comment so a future reader doesn't "fix" it back to a drop.

---

## 5. Open design points to settle in implementation

- **Rate-limit:** `req_sync_retry_ms = 30 s`, and `_last_req_sync_tx_ms` is **Node-global** (shared across a gateway's two layers) — so one layer's pull blocks the other for 30 s. Decide: keep shared (simpler, 30 s is fine since one REQ_SYNC pulls the *whole* table) vs per-layer. Recommend: keep shared first; revisit only if a gate shows a layer starved.
- **force interaction with discovery REQ_SYNC:** both write `_last_req_sync_tx_ms`. Confirm the reactive pull and the boot loop don't starve each other (they share the rate-limit, which is acceptable).
- **REQ_SYNC reply vs window timing:** replies are jittered (`schedule_sync_response`); if they land after the gateway's window closes, recovery slips a window. The defer (retry) absorbs this; confirm `send_defer_ttl` is long enough to span a window or two.
- **Seed 1522 residual:** even at the 90% mean, the hardest seed lands ~13-17/21. Acceptable (off the floor); note it, don't chase to 100%.

---

## 6. Verification / gate

1. **Native units (new):** (a) a gateway-relay TxItem with no far-layer route → `send_req_sync_q` is called (forced) AND the item is deferred, not dropped; (b) `force=true` bypasses the route-rich guard (rt_count ≥ min still sends); (c) the 30 s rate-limit still holds (no Q-storm on repeated misses); (d) a *non*-gateway forwarder still drops (unchanged). Full suite green.
2. **Sim — the payoff:** s15 multi-seed mean → ~90%, `send_no_route` at bridges drops sharply, `leaks==0`.
3. **No regression:** s16 ≥ 71% (align intact), s09/s10 = 100%, s18/s19/s17 unchanged; beacon airtime ≈ flat; REQ_SYNC count small/bounded.
4. **Boards:** 4 builds.
5. **Re-baseline** s15-xl as a multi-seed mean±range in `simulation/BASELINE.md` (cross-layer is gated on the distribution, never one seed).

---

## 7. POC reference (validated, in the tree, `TEST`-marked)

The change above is already proven in the working tree (3 files): `node.h` (`send_req_sync_q` force param), `node_query.cpp` (force bypass), `node_mac.cpp` (gw-relay defer+Q). Committed align (`3dbaa14`) stays. The coder cleans the `TEST` markers into final form + adds §6 tests; the quality-gate then runs §6 before the author commits. (The earlier byte-breakdown diagnostic metric was reverted — it only served to rule out the byte-budget theory, §1.)

## 9. Build order (coder)

1. **Finalize the POC** (already in the tree, `TEST`-marked → clean to final): `node.h` `send_req_sync_q(const char* reason, bool force=false)` decl; `node_query.cpp` the two `!force &&` guard bypasses; `node_mac.cpp` `issue_send` no-route branch `if (item.is_gw_relay) { send_req_sync_q("gw_relay_no_route", true); defer_send(item); }`. Replace the `TEST` comments with the §3/§4 rationale — make the **defer-for-gateway-relay exception (§4) loud** (a future reader must not "fix" it back to a drop).
2. **Keep the committed align** (`3dbaa14`) — complementary (§2), do NOT revert.
3. **Rate-limit (§5):** ship shared `_last_req_sync_tx_ms` first; go per-layer only if the gate shows a starved layer.
4. **Native units (§6.1):** force-pull on gw-relay miss; defer-not-drop; `force` bypasses route-rich; 30 s rate-limit holds (no Q-storm); a non-gateway forwarder still drops. Full suite green.
5. **Hand back green-shaped + uncommitted** → the quality-gate runs §6 (multi-seed s15 → ~90%, no regression on s16/s09/s10/s18/s19/s17, airtime ≈ flat, REQ_SYNC bounded) + re-baselines s15-xl as a multi-seed mean, before the author commits.

## 8. Out of scope
- The sender-side push approaches (full/throttled beacons) — measured, rejected (§2); recorded only so they aren't re-tried.
- The align mechanism itself — separate, committed, KEEP (§2/§3 relationship).
- R6 membership/join — unrelated (s15 is unmanaged; R6 gates inert there).
