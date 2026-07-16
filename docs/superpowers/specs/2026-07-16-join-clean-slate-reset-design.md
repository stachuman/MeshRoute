# `join` clean-slate reset — design spec

*2026-07-16. For the coder to implement; I QA-gate after (present-state grounded against the working tree at HEAD `7053970` + the uncommitted 2c tree). A **fix** (not a refactor): behavior changes on the join/create/leave verbs only.*

> **IMPLEMENTED 2026-07-16** with QA revises R1–R4 folded in (the sections below reflect the landed code, not the original draft). Summary of what shipped vs. the first draft:
> - **R1** — Change 3's gate is `if (_node_id == 0) return;`, **not** `!_joined`. An operator-pinned host (`cfg set node_id` → `b.joined = 0`) has `_joined == false` **forever** and must keep hosting; `_node_id == 0` spans exactly the mid-DAD window (`reset_join_for_reprovision` → `set_identity(0)`; adopt restores the id right before `_joined = true`). Bonus: kills the absurd `responder_node_id = 0` OFFER.
> - **R2** — Change 1 also fires a `mobile_reg` deregistration push (`registered:false` shape, S2) when `_my_mobile_reg.active`, *before* the struct reset, so a `leave`/re-`join` doesn't leave the companion chip's registration state stale forever.
> - **R3** — Change 1 also resets `_layers[i]._team_liveness_n` (the 2c team-plane liveness mirror) inside the existing `#if MR_FEAT_TEAM` block — same append-only-never-reset disease as the mobile registry.
> - **R4** — the per-layer **channel plane** cluster (`_channel_buffer_n` · `_per_origin_channel` · `_channel_pull_pending` · `_channel_pull_recent_n` · `_flood`) was **MOVED** from `clear_learned_state` into `clear_routing_state`'s per-layer loop (leak confirmed: a beacon digest advertises buffered ids + a pull re-broadcasts them, both stamped with the *current* leaf → old-network channel content floods a shared-key new network). `_peer_keys_n` deliberately stays in `clear_learned_state` only (peer keys are global-hash-keyed, network-independent — a live reprovision must not drop verified contacts).
> - **prep-restart note** — `clear_learned_state()` calls `clear_routing_state()`, so prep-restart now ALSO drops the hosted-mobile registry + own reg + team liveness + fires the dereg push. Harmless and accepted (RAM-only state, the node is halting; a surviving host re-populates via the mobile's ~10-min re-CLAIM upsert). Not "fixed" — this is the intended lifetime.
> - **`create` path** — checked and covered: `name=` is MANDATORY by grammar (`handle_create`, firmware_config.cpp:482) so there is no stale-name leak on `create` (that is *why* Change 2 is join-only); membership is freshly zeroed (:498); Changes 1/3 (+R4) reach `create` via the shared `provision_apply_live`.

## 0. The bug

A node hosting mobiles on one network is re-`join`ed onto another. The join re-DADs a fresh id (trace: `159→58`) and wipes routes/id-bindings — **but keeps hosting the OLD network's mobiles**: it answers a fresh mobile DISCOVER with `tx J mobile OFFER responder=58 resp_hash=61CD83EA` (the *old* mobile's key hash) while still mid-join, and `cfg` still reads `hosting=1 mobile(s)` + the stale `leaf_name="layer 23 b"`. Two planes coexist → inconsistency.

**Root cause:** `_mobile_reg_n` (the per-leaf hosted-mobile registry count) is only ever `++`'d — the sole write is `node_join.cpp:235` (`slot = _active->_mobile_reg_n++`). **No code path anywhere resets it to 0** — not `join`, not `leave`, not even the heavier `clear_learned_state()` (prep-restart). The reprovision wipe `clear_routing_state()` (node.cpp:321) predates the mobile-host feature and never learned about the registry. Two smaller gaps ride along: the stale `leaf_name`, and the fact the node keeps servicing DISCOVERs mid-DAD.

## 1. Current state (grounded)

The `join` verb → `handle_join` (src/firmware_config.cpp:429) → `provision_apply_live(b, do_dad=true)` (src/firmware_config.cpp:407) → `clear_routing_state()` (node.cpp:321).

| Requirement | Today | |
|---|---|---|
| **level** (layer) | `lc.leaf_id`/`layers[0].layer_id` set from blob (firmware_config.cpp:411) | ✅ reset |
| **routes** | `clear_routing_state`: `_rt_count=0` + `_rt_team_count=0` + `_team_peer` scrub (node.cpp:323-326) | ✅ reset |
| **registered nodes** (id-binds) | `clear_routing_state`: `_id_bind_n=0` (node.cpp:328) | ✅ reset |
| **leaf name/lineage/epoch** | `handle_join` zeros `lineage_id`/`config_epoch` (firmware_config.cpp:450) but **NOT `leaf_name_len`** → the loaded blob's old name is re-applied (firmware_config.cpp:420) | ❌ **stale** |
| **hosted mobiles** (`_mobile_reg[]`) | never cleared anywhere (append-only since node_join.cpp:235) | ❌ **THE BUG** |
| **own registration** (`_my_mobile_reg`) | untouched by the join path | ❌ |
| **suspend hosting during DAD** | the DISCOVER→OFFER handler (node_join.cpp:305-311) gates only on `!j.is_mobile` / `_cfg.is_mobile` / `!_cfg.host_mobiles` — **no `_joined` check** | ❌ |

**Guards (build-critical), confirmed:**
- `_mobile_reg[]` / `_mobile_reg_n` (LayerRuntime, node.h:1285-1286) — **UNGUARDED**, compiled into every build (the "#if MR_FEAT_MOBILE host side" comment at node.h:1274 is aspirational; the guard was never applied — `MR_FEAT_MOBILE_HOST` is always 1, slice-3 boundary deferred).
- `_my_mobile_reg` (Node-global, node.h:1082) — inside `#if MR_FEAT_MOBILE` (node.h:1072-1092).

## 2. The fix — three changes

### Change 1 (PRIMARY) — wipe the hosted-mobile registry + own registration on reprovision
The mobile registry is old-network state of the exact class `clear_routing_state()` already wipes. Add to that function (node.cpp:321), inside the existing per-layer loop for the host registry (unguarded) and once after the loop for the member registration (`#if MR_FEAT_MOBILE`):

```cpp
void Node::clear_routing_state() {
    for (uint8_t i = 0; i < _n_layers; ++i) {
        _layers[i]._rt_count   = 0;
#if MR_FEAT_TEAM
        // … existing _rt_team_count / _team_peer clears …
        _layers[i]._team_liveness_n = 0;   // R3: the 2c team-plane liveness mirror is old-network state too (a stale
                                          // dead/silent tier would misrank the fresh team's _rt_team). Count-reset only.
#endif
        // … existing id_bind / deferred / drain clears …
        _layers[i]._mobile_reg_n = 0;   // §clean-join: the hosted-mobile registry is old-network state — the mobiles
                                        // registered to us on the PREVIOUS network are void. UNGUARDED: _mobile_reg is
                                        // compiled into every build (node.h:1285). Count-reset only.
        // R4: the CHANNEL plane MOVED here from clear_learned_state (which calls us) — else buffered channel messages
        // flood the new network (digest advertises + pull re-broadcasts them, stamped with the CURRENT leaf).
        _layers[i]._channel_buffer_n = 0; _layers[i]._per_origin_channel.clear();
        for (auto& p : _layers[i]._channel_pull_pending) p = ChannelPullPending{};
        _layers[i]._channel_pull_recent_n = 0;
        for (auto& f : _layers[i]._flood) f = FloodState{};
    }
    // … existing _gw_schedules / _bridged_layers clears …
#if MR_FEAT_MOBILE
    // R2: push the deregistration FIRST (active-guarded → registered:false, S2), THEN reset — else after `leave` the
    // companion chip is stale forever. Plain struct reset — NOT mobile_reset_registration() (whose push / re-DISCOVER
    // side-effects don't belong on a verb reprovision; the join's own re-DAD drives rediscovery). _my_mobile_reg (node.h:1082).
    if (_my_mobile_reg.active) { Push pu{}; pu.kind = PushKind::mobile_reg; enqueue_push(pu); }
    _my_mobile_reg = MyMobileReg{};
#endif
}
```

**Why `clear_routing_state`, not `clear_learned_state`:** `clear_routing_state` is the reprovision-only wipe (join/create/leave) — it is deliberately **NOT** called by the same-network heal (`forced_rejoin`), where a host must KEEP its mobiles (node.cpp:318 comment + node.h:212). Placing the registry clear here gives it exactly the right lifetime: void on a network change, preserved on an id-only heal.

### Change 2 — clear the stale leaf name on join
In `handle_join` (src/firmware_config.cpp), alongside the existing fresh-blob zeroing (`b.node_id = 0; b.joined = 0; b.lineage_id = 0; b.config_epoch = 0;` at firmware_config.cpp:450), add:

```cpp
b.leaf_name_len = 0;   // §clean-join: don't carry the OLD leaf's name into the new network — present as freshly-joined,
                       // config-not-yet-pulled. A managed leaf repopulates name/lineage/epoch via the config pull; an
                       // unmanaged one shows blank until `cfg set leaf_name`. (Bytes need not be zeroed — len-gated.)
```

**Join-specific by construction:** this lives in `handle_join`, NOT in the shared `provision_apply_live` — so `create` (which mints a leaf and legitimately sets its own name via `handle_create`) is untouched. `leave` also unaffected.

### Change 3 — suspend hosting until (re-)joined
In the DISCOVER→OFFER handler (node_join.cpp:305), after the existing `if (_cfg.is_mobile || !_cfg.host_mobiles) return;` (node_join.cpp:307), add:

```cpp
if (_node_id == 0) return;   // §clean-join (R1): no host OFFER while unprovisioned/mid-DAD (reset_join_for_reprovision
                             // set_identity(0)'d us; adopt restores the id right before _joined). NOT `!_joined`: an
                             // operator-pinned host (`cfg set node_id` -> b.joined=0) has _joined==false FOREVER and must
                             // keep hosting. Bonus: kills the absurd responder_node_id=0 OFFER.
```

Change 1 evicts the *old* mobile; this stops a *new* DISCOVER arriving mid-DAD from getting an OFFER before the node has finished joining. **R1 (why not `!_joined`):** `cfg set node_id` pins a host with `b.joined = 0` deliberately (firmware_config.cpp:99) → boot restores `_joined = false` **permanently** on a pinned host/gateway; the draft's `!_joined` gate would refuse it hosting for life. `reset_join_for_reprovision` calls `set_identity(0)` (node_join.cpp:371) and the adopt restores the id at :184 immediately before `_joined = true` — so `_node_id == 0` spans **exactly** the mid-DAD window.

## 3. Edge cases / what NOT to touch
- **The heal** (`forced_rejoin`, same network, id-only change) MUST keep hosted mobiles — guaranteed because it does not call `clear_routing_state`. Do not add the registry clear anywhere the heal reaches.
- **`create`** keeps its leaf name — guaranteed because Change 2 is in `handle_join` only.
- **`leave`** already goes idle (do_dad=false); Change 1 additionally drops its hosted mobiles + own reg, which is correct (a departed node hosts no one).
- **s18 / static plane:** all three changes touch only the join/create/leave verb paths + a `!_joined` early-return in a mobile-only handler. `clear_routing_state` is never called during s18 (no mid-run reprovision); the OFFER handler is mobile-only and s18 nodes are `_joined` after init anyway. → s18 stays byte-identical **by construction**.

## 4. Gate (when the code lands — I run it)
- **native** + a NEW unit test: provision a host, register a mobile (`mobile_reg_count()==1`), issue a `join`-equivalent reprovision (`clear_routing_state`), assert **`mobile_reg_count()==0`** + routes/id-binds cleared + (`#if MR_FEAT_MOBILE`) `mobile_registered()==false`. A second assert: a mid-DAD (`_joined==false`) DISCOVER produces **no** `mobile_offer_tx`.
- **s18 md5 `3ac88d40e00d2605ff66659f696d52bf` EXACT** (inert by construction — the tripwire still runs).
- **s22–s26** 0 assertion failures (mobile/team scenarios unaffected — none re-join mid-run, but they must stay green).
- **10 boards** sequential, all SUCCESS (Change 1's unguarded `_mobile_reg_n=0` must compile on gateway/static; the `#if MR_FEAT_MOBILE` member-reg clear must compile with mobile OFF — the gateway envs are the check).
- **per-board RAM:** expected **Δ0** (no new state; count/flag resets only).

## 5. Decisions (resolved with the user)
1. **Scope** — full clean (all three changes). ✔
2. **Registry clear location** — `clear_routing_state` (reprovision-only; preserves mobiles across the heal). ✔
3. **Leaf name** — zero `leaf_name_len` on join (fresh-join posture); managed pull / user re-sets it. ✔
4. **Suspend** — gate the host OFFER on `_joined` (no hosting mid-DAD). ✔
