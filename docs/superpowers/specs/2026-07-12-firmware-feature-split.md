# Firmware feature split — `mr_features.h` (compile-time features → profiles)

**Date:** 2026-07-12
**Status:** Design — approved shape (feature-flags→profiles; pure-static-bridge gateway; static-home = option A / stays in the full build). Awaiting spec review, then a writing-plans implementation plan.
**Author context:** MeshRoute C++20 LoRa firmware. Fixed-size, no-heap by design.

## 1. Problem / driver

`g_node` (the `Node`) is **188 KB** — 80% of the nRF52's 235 KB RAM — because it allocates **every** feature's fixed-size state unconditionally, and the **dual-layer gateway** (`MR_N_LAYERS=2`) doubles every per-layer table. The gateway build hit **97.6% static RAM (5.5 KB free)** and **can't boot**: this session's **8 KB dedicated mesh task** (the stack-overflow fix, nRF52-only) is heap-allocated at startup and there's no headroom. Single-layer boards (`xiao_sx1262` ≈ 94 KB) have room — which is why the mobiles run but the gateway doesn't.

The worst offender: `RtEntry` = 88 B, so `_rt[254]` = 22 KB and **`_rt_team[254]` = 22 KB, each ×2 layers**. `_rt_team` is a full second routing table a **gateway never uses** (it isn't a team member). Compiling the team (and mobile) features out of the gateway frees **~48 KB** (→ ~77%).

More role/board-specific code is imminent (e.g. the heltec's OLED, which only makes sense on a mobile), so this is the architecture for splitting functionality, not a one-off RAM patch.

## 2. Decisions (agreed)

- **Compile-time feature flags composed into per-env profiles** — fixed-size/no-heap means the arrays must not *exist* in the binary to free RAM, so the split is compile-time, not a runtime config.
- **Gateway = pure static relay + cross-layer bridge** — no team, no mobile-member, no mobile-host.
- **Mobile-host (a "home") stays in the full/production build (option A)** — hosting remains a runtime `host_mobiles` toggle on general static nodes; a dedicated slim home profile is deferred until a relay's RAM demands it.
- **`native` + the `lus` sim build = ALL features on** — the doctest suite and every mobile/team sim keep full coverage.

## 3. Mechanism

### 3.1 `lib/core/mr_features.h` — single source of truth
Each PlatformIO env sets **one** `MR_PROFILE_*` in `build_flags` (or, for a one-off dev build, overrides individual `MR_FEAT_*`). `mr_features.h`:
1. expands the profile into the `MR_FEAT_*` set;
2. resolves dependencies (`MR_FEAT_TEAM` implies `MR_FEAT_MOBILE`);
3. `#error`s on an illegal combo;
4. defaults every `MR_FEAT_*` to `1` when no profile is set (so a bare/native build is full).

```c
// mr_features.h (shape)
#if defined(MR_PROFILE_GATEWAY)
#  define MR_FEAT_GATEWAY      1
#  define MR_FEAT_MOBILE       0
#  define MR_FEAT_MOBILE_HOST  0
#  define MR_FEAT_TEAM         0
#  define MR_FEAT_OLED         0
#elif defined(MR_PROFILE_MOBILE)
#  define MR_FEAT_MOBILE       1
#  define MR_FEAT_TEAM         1
   ... (GATEWAY/HOST 0; OLED per board)
#endif
// defaults: any unset MR_FEAT_* -> 1 (full build)
#ifndef MR_FEAT_TEAM
#  define MR_FEAT_TEAM 1
#endif
   ...
// dependency + sanity
#if MR_FEAT_TEAM && !MR_FEAT_MOBILE
#  error "MR_FEAT_TEAM requires MR_FEAT_MOBILE (a team member is is_mobile)"
#endif
```

### 3.2 Stub-based guarding (the readability keystone)
The goal: **call sites stay unchanged.** Each feature hides its state + logic behind a small public API. When the feature is OFF: the **state is not declared** and the API **stubs to an inert value**. Route-selection / `enqueue_data` / `handle_rts` call the stubs → inert.

```cpp
// node.h — the API is always declared; the body is conditional
#if MR_FEAT_TEAM
bool is_team_peer(uint8_t id) const;          // real: reads _team_peer
#else
bool is_team_peer(uint8_t) const { return false; }   // stub: no team plane
#endif
```
```cpp
// node.h LayerRuntime — the STATE only exists when the feature is on
#if MR_FEAT_TEAM
RtEntry  _rt_team[protocol::cap_routes] = {};
uint8_t  _rt_team_count = 0;
uint8_t  _team_peer[32] = {};
TeamKey  _team_keys[16] = {};
uint8_t  _team_keys_n = 0;
#endif
```
**Rule:** all feature-state access goes through the feature API. The migration effort is finding the few call sites that touch feature state *directly* (e.g. `_active->_rt_team` in `node_beacon.cpp`, `_team_keys` in `node_routing.cpp`) and routing them through the API (or guarding that one line). Once done, the big call sites (`node_mac.cpp` route-selection, `enqueue_data`, `handle_rts`) compile unchanged against the stubs.

## 4. Feature catalog (state + API per feature)

### `MR_FEAT_TEAM` (requires `MR_FEAT_MOBILE`) — **slice 1, fully specified**
- **State (LayerRuntime):** `_rt_team[cap_routes]`, `_rt_team_count`, `_team_peer[32]`, `_team_keys[16]`, `_team_keys_n`.
- **State (Node):** `_team_local_id`, `_team_dad_pending`, `kTeamDadGuardTimerId=77` handling.
- **API (stub when off):** `is_team_peer→false`, `team_key_of_id→false`, `team_key_set→no-op`, `team_dad_fire→no-op`, `team_dad_guard_fire→no-op`, `team_dad_choose_candidate_id→-1`, `team_local_id→0`, `set_team_local_id→no-op`, `rt_team_count→0`. `for_me_dst` reduces to `dst == _node_id` (the `_team_local_id` term is `0`). `learn_direct_neighbor(...,team_plane=true)` → never called (route-learn dispatch skips the team plane when `is_team_peer` is always false).
- **Direct-access call sites to route through the API / guard:** `node_beacon.cpp` (`same_team_beacon` block: `_team_peer`, `team_key_set`, team-TLV parse/emit, team-DAD tiebreak), `node_routing.cpp` (`is_team_peer`, `team_key_*`, `_rt_team` in `rt_find`/`age_out_stale_routes` dispatch), `node_mac.cpp` (`team_next` in RTS/DATA build), `node_mac_rx.cpp` (cross-leaf `team_rts_for_us`, team reverse-learn, `for_team_rts`), `node_mobile.cpp` (`team_dad_*`), `node_hashlocate.cpp` (team-scoped H `H_FLAG_TEAM`), `node_channel.cpp` (team channel M-frame), `frame_codec` team-TLV (type 5) + `H_FLAG_TEAM` (compile the codec always — it's tiny; only the node-side use is gated).
- **Config:** `_cfg.team_id` — keep the field (NV/config-schema stable), but a `MR_FEAT_TEAM=0` build ignores it (team paths compiled out). `cfg set team_id` / `team` console verb → `#if MR_FEAT_TEAM` in fw_main.
- **RAM freed (gateway, ×2 layers):** ~45 KB (`_rt_team`) + ~1 KB (`_team_keys`/`_team_peer`).

### `MR_FEAT_MOBILE` (roaming member) — slice 2
- **State (Node):** `_my_mobile_reg` (`MyMobileReg`), `_mobile_offers[cap_mobile_offers]`, `_mobile_scan_idx`, `_mobile_backoff_ms`, `_learned_layers[]` + `_n`.
- **API/logic (stub/guard when off):** the registration FSM (`mobile_discover_fire`, `mobile_claim_guard_fire`, `mobile_reset_registration`, `mobile_layer_query_fire`, `mobile_register_scan`); the mobile accessors (`mobile_registered`/`mobile_home_id`/`mobile_local_id`/…→0/false); **`stamp_origin`'s mobile branch** (`mob = false` → `origin = _node_id`, as today for a static); the **send-by-hash delegate branch**; the **reverse-ack ECHO** (`send_e2e_ack`'s `key_hash_of_id != sender_hash` echo); the mobile discover/claim timers.
- **Kept general (NOT gated):** `_mobile_peer`/`is_mobile_peer` (avoid-transit-through-a-mobile is route-quality every node needs, ~32 B); the `is_mobile` beacon *bit* parse (a gateway must still recognize a mobile neighbor).

### `MR_FEAT_MOBILE_HOST` (a home) — slice 3; **stays ON in the full/production build (option A)**
- **State (LayerRuntime):** `_mobile_reg[cap_host_mobiles]`, `_mobile_reg_n`, `_mobile_home_cache[cap_mobile_home_cache]`. **(Node):** `_deleg_acks[kDelegAckCap]`.
- **API/logic:** `store_mobile`, the **host last-mile fork** (`do_post_ack`), the **H-proxy** (`handle_h` → `MOBILE_H_ANSWER` / WANT_PUBKEY-on-behalf), the **reverse-ack LAST-MILE** + `deleg_ack_put/translate`, `mobile_reg_count`.
- **Nuance to resolve in-slice:** `_mobile_home_cache` is *sender-side reach-a-mobile* (resolve a mobile's home), used by any node originating to a mobile. Provisionally under `MOBILE_HOST`; if a `MOBILE` member needs it to reach *another* mobile, gate it on `MR_FEAT_MOBILE || MR_FEAT_MOBILE_HOST`.

### `MR_FEAT_GATEWAY` — cross-layer bridge
- **State:** `_gw_schedules`, `_bridged_layers`, `_xl_handoffs` + the dual-layer `_layers[2]` (driven by `MR_N_LAYERS`). **`MR_PROFILE_GATEWAY` sets `MR_N_LAYERS=2`; the mobile/relay profiles keep `MR_N_LAYERS=1`.** (The gateway keeps its full `_rt`/`_id_bind` ×2 — those are legitimately needed for two leaves; only team/mobile state is cut.)

### `MR_FEAT_OLED` (+ future board UI) — slice 4 (empty scaffold)
- A board-UI hook (`mr_ui.h` / a board TU) guarded by `MR_FEAT_OLED`, wired to `Push`/`cfg` events. Slice 4 lands the empty seam so the next custom-code PR just fills it.

## 5. Profiles

| env | `MR_PROFILE_*` | features | `MR_N_LAYERS` |
|---|---|---|---|
| `gateway` (+ `gateway_heltec`/`_esp32s3`) | `GATEWAY` | GATEWAY | 2 |
| `xiao_sx1262` | `MOBILE` | MOBILE + TEAM | 1 |
| `heltec_v3` | `MOBILE` (+ `OLED`) | MOBILE + TEAM + OLED | 1 |
| `production` | *(none → full)* | ALL | 1 |
| `native` | *(none → full)* | ALL | 2 (host-test both) |

*Note:* a **mobile** (roaming endpoint) is a `MOBILE` member and does **not** host — `MOBILE_HOST` is OFF on the mobile profiles. `MOBILE_HOST` is ON only in the full/production build (option A: any general static node can host at runtime).

## 6. Migration — incremental, always gated

Features are already `is_mobile`/`team_id` runtime-gated, so **compiling them out is inert on the static plane**. The gate per slice:
- **native 692/692** (all features on — the doctest suite unchanged),
- **s18 `3ac88d40…`** (static plane byte-identical),
- **⚠ the MOBILE SIMS pass on the all-on build** — `s07`/`s21` + the new `s22` mobile/team scenarios. **s18 alone is NOT enough**: it's static-only, so it cannot catch a mobile/team feature broken or mis-stubbed by the split; the mobile sims prove the features still *work*.
- **every env compiles** (the slim builds must not break — this is where a missing stub surfaces),
- **each board's RAM** reported (the gateway must drop under budget).

**Slices:**
1. **`mr_features.h` + the `TEAM` API/stub boundary + `MR_PROFILE_GATEWAY` (TEAM/MOBILE off).** → **unblocks the gateway now** (~48 KB freed, boots) and proves the mechanism end-to-end. This is the highest-value slice.
2. `MR_FEAT_MOBILE` member split → `MR_PROFILE_MOBILE` (the flashed mobile boards go slim).
3. `MR_FEAT_MOBILE_HOST` split (stays ON in full; the seam exists for a future home profile).
4. `MR_FEAT_OLED` empty scaffold.

## 7. Risks / open items

- **Missing-stub compile breaks** are the main risk — a slim build fails to link because a call site referenced compiled-out state directly. Mitigation: the "route all feature-state access through the API" rule + `every env compiles` in the gate. Expect iteration on slice 1 to find the direct-access sites.
- **`frame_codec` stays feature-agnostic** (compile all pack/parse always — it's tiny and the sim/tests need it); only *node-side* use is gated. This keeps the wire definition single-sourced.
- **NV/config schema stays stable** — `team_id`/`is_mobile`/`host_mobiles` fields remain in the Blob (no version bump); a slim build simply ignores the ones its features don't use. A node re-flashed between profiles keeps a loadable NV.
- **Deferred:** a dedicated slim `MOBILE_HOST` home profile (option B) — revisit only if a relay's RAM becomes a constraint.

## 8. Success criteria

The gateway boots and runs (RAM ~77%); every env builds; native 692/692; s18 byte-identical; the mobile sims pass on the all-on build; and adding the OLED (or any next board feature) is a localized change under its `MR_FEAT_*`.
