# Firmware Feature Split — Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans (or subagent-driven-development) to implement task-by-task. Steps use checkbox (`- [ ]`) syntax. **⚠ This user does ALL git commits themselves — NEVER commit or offer to. Each task ends with "verify green + leave uncommitted"; report readiness, do not `git commit`.**

**Goal:** Split MeshRoute firmware by role/board via compile-time feature flags so each flashed target compiles only what it uses — immediately unblocking the dual-layer gateway (97.6% RAM → can't boot) by compiling the team plane out of it (~45 KB freed).

**Architecture:** A central `lib/core/mr_features.h` derives `MR_FEAT_*` from a per-env `MR_PROFILE_*`. Feature state is `#if MR_FEAT_X`-declared; the feature's public API **stubs to an inert value** when off, so call sites are unchanged. `native` + the `lus` sim = all features on (full test coverage).

**Tech Stack:** C++20, PlatformIO (envs = build profiles), doctest (native), the `lus` simulator (s18 static tripwire + mobile sims).

**Spec:** `docs/superpowers/specs/2026-07-12-firmware-feature-split.md`. **Memory:** `meshroute-firmware-feature-split.md`.

---

## Invariant gate (run after EVERY task; nothing may regress)

```bash
# 1. native (all features on — the full doctest suite)
cd /home/staszek/MeshRoute && pio test -e native 2>&1 | grep -E "error:|FAILED" ; .pio/build/native/program 2>&1 | tail -3   # expect 692/692 (or higher), 0 failed
# 2. s18 static tripwire (byte-identical)
cmake --build ~/lora-universal-simulator/build --target lus -j4 >/dev/null && \
  ~/lora-universal-simulator/build/orchestrator/lus --engine meshroute simulation/s18_meshroute.json 2>/dev/null | md5sum   # expect 3ac88d40e00d2605ff66659f696d52bf
# 3. mobile sims WORK on the all-on build (s18 alone can't catch a broken mobile/team feature)
for s in s07_seattle_mobile_meshroute s21_mobile_dm_milestone_meshroute; do \
  af=$(~/lora-universal-simulator/build/orchestrator/lus --engine meshroute simulation/$s.json 2>/dev/null | grep -c '"assertion_fail"'); echo "$s af=$af"; done   # expect af=0  (+ s22 when it exists)
```
**Fail on:** any native failure, any s18 md5 drift, any mobile-sim `af>0`. These prove the split is inert on the FULL build.

---

## Task 1: Create `mr_features.h` (the feature/profile layer)

**Files:** Create `lib/core/mr_features.h`.

- [ ] **Step 1 — write the header.** It must (a) expand a profile, (b) default every unset flag to `1` (bare/native = full), (c) `#error` on the `TEAM`-without-`MOBILE` dependency.

```cpp
// MeshRoute — lib/core/mr_features.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Compile-time feature split. An env sets ONE MR_PROFILE_* in build_flags; this header derives the MR_FEAT_* set,
// resolves dependencies, and #errors on an illegal combo. No profile set => every MR_FEAT_* defaults to 1 (a full/dev
// build). Feature STATE is #if MR_FEAT_X-declared; each feature's API stubs to an inert value when off (call sites
// unchanged). See docs/superpowers/specs/2026-07-12-firmware-feature-split.md.
#pragma once

// ---- profiles (each defines the slim feature set; slices flip more OFF as their boundaries land) ----
#if defined(MR_PROFILE_GATEWAY)          // pure static relay + cross-layer bridge
#  define MR_FEAT_TEAM 0                  // slice 1: team plane compiled out (frees ~45 KB of _rt_team ×2)
   // (MR_FEAT_MOBILE / MR_FEAT_MOBILE_HOST flip to 0 in slices 2/3, once their boundaries exist)
#endif

// ---- defaults: any unset feature is ON (a bare/native/production build is full) ----
#ifndef MR_FEAT_TEAM
#  define MR_FEAT_TEAM 1
#endif
#ifndef MR_FEAT_MOBILE
#  define MR_FEAT_MOBILE 1
#endif
#ifndef MR_FEAT_MOBILE_HOST
#  define MR_FEAT_MOBILE_HOST 1
#endif
#ifndef MR_FEAT_GATEWAY
#  define MR_FEAT_GATEWAY 1
#endif
#ifndef MR_FEAT_OLED
#  define MR_FEAT_OLED 0                  // board UI: OFF by default (opt-in per board); scaffold lands in slice 4
#endif

// ---- dependency + sanity checks ----
#if MR_FEAT_TEAM && !MR_FEAT_MOBILE
#  error "MR_FEAT_TEAM requires MR_FEAT_MOBILE (a team member is is_mobile; the team plane reuses the mobile link-layer)"
#endif
```

- [ ] **Step 2 — include it where the features are consumed.** Add `#include "mr_features.h"` at the top of `lib/core/node.h` (after the existing includes). Nothing uses the flags yet → no behavior change.

- [ ] **Step 3 — run the invariant gate.** Expect native 692/692, s18 `3ac88d40…`, mobile sims af=0 (a no-op header + one include; the full build defaults everything to 1).

- [ ] **Step 4 — leave green + report** (do NOT commit).

---

## Task 2: Gate the TEAM state + declare the TEAM API stubs (full build unchanged)

**Files:** Modify `lib/core/node.h`.

- [ ] **Step 1 — wrap the TEAM state in `#if MR_FEAT_TEAM`.** In `LayerRuntime`: `_rt_team[cap_routes]`, `_rt_team_count`, `_team_peer[32]`, the `TeamKey` struct + `_team_keys[16]` + `_team_keys_n`. In `Node`: `_team_local_id`, `_team_dad_pending`.

```cpp
// node.h — LayerRuntime (guard the block)
#if MR_FEAT_TEAM
        RtEntry  _rt_team[protocol::cap_routes] = {};
        uint8_t  _rt_team_count = 0;
        uint8_t  _team_peer[32] = {};
        struct TeamKey { uint8_t id = 0; uint32_t key_hash32 = 0; uint64_t last_seen_ms = 0; };
        TeamKey  _team_keys[16] = {};
        uint8_t  _team_keys_n = 0;
#endif
```
```cpp
// node.h — Node members
#if MR_FEAT_TEAM
    uint8_t  _team_local_id = 0;
    bool     _team_dad_pending = false;
#endif
```

- [ ] **Step 2 — make the TEAM API stub-to-inert when off.** For each header-inline function, provide a `#else` stub; for the out-of-line ones (`is_team_peer`, `team_key_set`, `team_key_of_id`, `team_dad_fire`, `team_dad_guard_fire`, `team_dad_choose_candidate_id`), declare the real one under `#if MR_FEAT_TEAM` and an inline stub under `#else`.

```cpp
// node.h
#if MR_FEAT_TEAM
    bool     is_team_peer(uint8_t id) const;
    void     team_key_set(uint8_t id, uint32_t key_hash32);
    bool     team_key_of_id(uint8_t id, uint32_t& out) const;
    uint8_t  team_local_id() const { return _team_local_id; }
    void     set_team_local_id(uint8_t id) { _team_local_id = id; _team_dad_pending = false; }
    uint8_t  rt_team_count() const { return _active->_rt_team_count; }
    const RtEntry& rt_team_at(uint8_t i) const { return _active->_rt_team[i]; }
#else
    bool     is_team_peer(uint8_t) const { return false; }
    void     team_key_set(uint8_t, uint32_t) {}
    bool     team_key_of_id(uint8_t, uint32_t&) const { return false; }
    uint8_t  team_local_id() const { return 0; }
    void     set_team_local_id(uint8_t) {}
    uint8_t  rt_team_count() const { return 0; }
    // rt_team_at: not stubbed — it's test/diag only; guard its (few) call sites in tests under #if MR_FEAT_TEAM
#endif
    // for_me_dst: the _team_local_id term is 0 when TEAM is off (field absent) — rewrite to compile both ways:
    bool     for_me_dst(uint8_t dst) const {
#if MR_FEAT_TEAM
        return dst == _node_id || (_cfg.team_id != 0 && _team_local_id != 0 && dst == _team_local_id);
#else
        return dst == _node_id;
#endif
    }
```
And the private team-DAD declarations (`team_dad_fire`/`team_dad_guard_fire`/`team_dad_choose_candidate_id`) get `#if MR_FEAT_TEAM` real / `#else` inline no-op (`team_dad_fire(){}`, `team_dad_guard_fire(){}`, `team_dad_choose_candidate_id(){return -1;}`).

- [ ] **Step 3 — run the invariant gate.** `MR_FEAT_TEAM` defaults to `1` on native → the `#if` branch is the original code → **native 692/692, s18 byte-identical, mobile sims af=0 unchanged.** (If s18 drifts, a guard changed a static-plane path — revert and re-scope.)

- [ ] **Step 4 — leave green + report.**

---

## Task 3: Route the direct-access call sites through the API so a `TEAM=0` build compiles

**Files:** Modify the `.cpp` sites that touch TEAM state directly (found by grep, not from memory).

- [ ] **Step 1 — enumerate every direct TEAM-state access outside the API:**
```bash
grep -rn "_rt_team\|_team_peer\|_team_keys\|_team_local_id\|_team_dad_pending\|team_dad_\|team_key_" lib/core/*.cpp | grep -v "is_team_peer\|team_key_of_id\|team_local_id()\|rt_team_count\|rt_team_at"
```
Known sites (verify against the grep): `node_beacon.cpp` (the `same_team_beacon` block — `_team_peer`, `team_key_set`, team-TLV, team-DAD tiebreak), `node_routing.cpp` (`is_team_peer`/`team_key_*` **bodies** — already under the API; guard the whole functions), `node_mac.cpp` (`team_next` uses `is_team_peer` → already API), `node_mac_rx.cpp` (cross-leaf `team_rts_for_us`, team reverse-learn `_team_peer`+`_rt_team`, `for_team_rts`), `node_mobile.cpp` (`team_dad_*` bodies), `node_hashlocate.cpp` (`H_FLAG_TEAM` / `h.team_scoped` — `_cfg.team_id`-gated, may compile as-is), `node.cpp` (on_init team normalize; `mobile_discover_fire` team-DAD kick), `node_cascade.cpp`/`node_query.cpp` (`is_team_peer` guards → API, compile as-is).

- [ ] **Step 2 — for each site, either (a) it already calls the API (compiles via the stub — leave it), or (b) it touches state directly — wrap that block in `#if MR_FEAT_TEAM`.** The function BODIES of `is_team_peer`/`team_key_set`/`team_key_of_id` in `node_routing.cpp`, and `team_dad_fire`/`team_dad_guard_fire`/`team_dad_choose_candidate_id` in `node_mobile.cpp`, go entirely under `#if MR_FEAT_TEAM` (their stubs are the header inline `#else`). The `same_team_beacon` block in `node_beacon.cpp` and the reverse-learn/cross-leaf/`for_team_rts` blocks in `node_mac_rx.cpp` get `#if MR_FEAT_TEAM` around the state-touching lines.

- [ ] **Step 3 — prove a `TEAM=0` build compiles** (before wiring the gateway env), via a throwaway flag on native:
```bash
pio run -e native 2>&1 | grep -E "error:" ; \
PLATFORMIO_BUILD_FLAGS="-DMR_FEAT_TEAM=0 -DMR_FEAT_MOBILE=0" pio run -e native 2>&1 | grep -E "error:|SUCCESS" | tail -5
# fix every 'error:' by guarding the offending direct-access line under #if MR_FEAT_TEAM; repeat until it links.
# (MOBILE=0 too here because TEAM requires MOBILE — the dep #error fires otherwise.)
```
> Note: `MR_FEAT_TEAM=0` forces `MR_FEAT_MOBILE=0` (the dep), so this throwaway build also surfaces MOBILE direct-access sites early — guard the TEAM ones now; MOBILE ones are slice 2 (a `// TODO(slice2)` guard is fine if isolated, else stub minimally to link).

- [ ] **Step 4 — run the invariant gate** (the ALL-ON native/s18/sims must still be green — the `#if` guards are transparent when `TEAM=1`).

- [ ] **Step 5 — leave green + report.**

---

## Task 4: Wire `MR_PROFILE_GATEWAY` (TEAM off) into the gateway env + verify RAM

**Files:** Modify `platformio.ini` (the `gateway`, `gateway_heltec`, `gateway_esp32s3` envs).

- [ ] **Step 1 — add the profile flag** to each gateway env's `build_flags`:
```ini
[env:gateway]
  ...
build_flags =
  ${env:xiao_sx1262.build_flags}   ; or whatever it currently inherits
  -DMR_N_LAYERS=2
  -DMR_PROFILE_GATEWAY             ; slice 1: TEAM compiled out (frees ~45 KB of _rt_team ×2)
```

- [ ] **Step 2 — build the gateway + read RAM:**
```bash
pio run -e gateway 2>&1 | grep -E "RAM:|error:|SUCCESS"
# expect: SUCCESS, RAM ~78-81% (was 97.6%). Repeat for gateway_heltec, gateway_esp32s3.
```
Expected: the gateway compiles and RAM drops well under 90% (the ~45 KB `_rt_team`×2 is gone).

- [ ] **Step 3 — every env still compiles** (the full/mobile builds are unaffected — TEAM defaults on there):
```bash
for e in native heltec_v3 xiao_esp32s3 xiao_sx1262 gateway gateway_heltec gateway_esp32s3 production; do echo "== $e =="; pio run -e $e 2>&1 | grep -E "SUCCESS|FAILED|error:" | tail -1; done
```

- [ ] **Step 4 — full invariant gate** (native 692/692, s18 `3ac88d40…`, mobile sims af=0).

- [ ] **Step 5 — leave green + report the RAM numbers.** Slice 1 done: **the gateway boots (RAM headroom for the 8 KB mesh task).**

---

## Slices 2–4 (outline — each detailed into bite-sized tasks when reached, following Task 2–3's pattern)

Each slice = **add the `MR_FEAT_*` flag + guard its state + stub its API + route direct-access sites + flip it OFF in the relevant profile**, gated by the same invariant (native all-on green, s18 byte-identical, mobile sims af=0, every env compiles, RAM reported).

- [x] **Slice 2 — `MR_FEAT_MOBILE` (roaming member).** DONE 2026-07-12 (gateway→78.3%; native 692, s18 byte-identical, s21/s22/s23 0-fail, all envs green). State: `_my_mobile_reg`, `_mobile_offers`, `_mobile_scan_idx`, `_mobile_backoff_ms`, `_learned_layers`. Stub: the registration FSM (`mobile_discover_fire`/`mobile_claim_guard_fire`/`mobile_reset_registration`/`mobile_layer_query_fire`), the accessors (`mobile_registered`/`mobile_home_id`/…→0/false), `stamp_origin`'s mobile branch (→ `origin=_node_id`), the send-by-hash delegate branch, the reverse-ack ECHO. **Keep general (NOT gated):** `_mobile_peer`/`is_mobile_peer` (avoid-transit routing) + the `is_mobile` beacon-bit parse. `MR_FEAT_MOBILE 0` added to `MR_PROFILE_GATEWAY`. (`MR_PROFILE_MOBILE` NOT added — it would equal the all-on default; a no-op until a mobile profile actually slims, e.g. slice 4 OLED.)

- [ ] ⏸ **Slice 3 — `MR_FEAT_MOBILE_HOST` (a home). DELIBERATELY DEFERRED (2026-07-12, user decision) — NOT unfinished work.** Reason: MOBILE_HOST-off only removes ~1.3 KB (the host tables) from the gateway, which already boots at 78.3% (~50 KB free) — no functional need; option A already keeps MOBILE_HOST in the full/production build (hosting = runtime `host_mobiles` toggle), so the gateway is the only env that'd flip it; and it's the highest-risk slice (host plane woven into the DM hot path — last-mile / H-proxy / reverse-ack / delegated re-origination — plus the `_mobile_home_cache` gating ambiguity and the known plane-separation local-id-leak class). Not worth the risk for 1.3 KB. *If a future relay's RAM ever demands it:* State: `_mobile_reg[cap_host_mobiles]`, `_mobile_reg_n`, `_mobile_home_cache`, `_deleg_acks`. Stub: `store_mobile`, the host last-mile fork (`do_post_ack`), the H-proxy (`MOBILE_H_ANSWER` / WANT_PUBKEY-on-behalf), the reverse-ack LAST-MILE + `deleg_ack_put/translate`, `mobile_reg_count`. **Resolve first:** `_mobile_home_cache` gating (home-only vs `MOBILE || MOBILE_HOST` — a member reaching *another* mobile). Would add `MR_FEAT_MOBILE_HOST 0` to `MR_PROFILE_GATEWAY`.

- [x] **Slice 4 — `MR_FEAT_OLED` scaffold.** DONE 2026-07-12. Created `lib/hal/mr_ui.h` (3 hooks: `mr_ui_init`/`mr_ui_tick`/`mr_ui_on_push`, inline no-ops when OLED=0) + `src/board_ui.cpp` (empty skeleton under `#if MR_FEAT_OLED`, driver-free so it links). Wired into fw_main (setup / mesh_service_once / push-drain). Added `-DMR_FEAT_OLED=1` to `heltec_v3` + `+<board_ui.cpp>` to the 3 base `build_src_filter`s. heltec_v3/gateway_heltec (OLED=1) LINK the seam; all other envs use the no-ops. All 8 envs green.

---

## Self-review notes (checked against the spec)

- **Placeholders:** none — Task 1–4 have complete code; slices 2–4 are intentionally outlined (they depend on slice-1-discovered direct-access sites and get their own bite-sized code at implementation time, per the incremental gate).
- **Type/name consistency:** the stub signatures match the real declarations exactly (`is_team_peer(uint8_t) const`, etc.); `for_me_dst` is rewritten to compile both ways.
- **Commit convention:** every task ends "leave green + report" — the user commits.
- **The one thing that WILL need iteration:** Task 3 (the direct-access site sweep) — the grep finds them, the `TEAM=0` throwaway build proves the set is complete. Budget the most time there.
