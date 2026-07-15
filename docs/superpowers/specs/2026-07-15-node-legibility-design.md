# node.h legibility (by-concern reorder) — design spec

*2026-07-15. Scoping/design pass, for review BEFORE any code moves. Triage step 4 (Node/node.h state legibility). Grounded in the `node-legibility-map` workflow (5 dimension readers + completeness critic + synthesis over node.h + the 13 `node_*.cpp` TUs + the test seam).*

**Goal:** make `lib/core/node.h`'s ~1600-line private-state region legible — a reader finds "the MAC state" / "the channel state" in one clearly-headed section — **behavior-preserving**, by reordering the private *data members* within their existing scope into concern sections that match the 13 TU owners, plus two grounded hygiene fixes. No functions move, no types are extracted, no planes are restructured.

---

## 0. Decision & non-goals

**CHOSEN = Option 1 (by-concern reorder).** The triage's literal phrasing — *"LayerRuntime into private headers, grouped by plane"* — names the **two riskiest axes**, per the map:
- *"private headers"* taken literally = extracting the struct definitions (esp. `LayerRuntime`), which drags in the `DualLayerTestAccess` **friend seam** (a private nested type the test names). Real blast radius, unrelated to legibility.
- *"grouped by plane"* taken literally = the `PlaneRuntime` per-plane sub-struct redesign, which is **RAM-costed and behavior-changing** — that's the plane-**leak fix**, not cleanup.
- A **by-concern reorder** captures ~80% of the legibility for ~zero behavior risk. This is the first slice.

**NON-GOALS — deferred to separate, separately-gated slices (NOT this one):**
- **Slice 2 (own review):** struct-definition extraction to private header(s). The `LayerRuntime`-vs-friend-seam handling deserves its own decision (keep nested with an in-class `#include` of its body, vs promote to `meshroute::LayerRuntime` + update the friend decl and every `Node::LayerRuntime` ref).
- **Separate initiative (not "legibility"):** the by-plane `PlaneRuntime` redesign = the actual `local-id`-into-static-array **leak fix** (the one gated in the plane-separation work). RAM-budgeted (doubles the 256-wide arrays), behavior-changing, **s18 will NOT be byte-identical** → sim-validated for real plane separation, gated differently.

---

## 1. ★ The pivot: s18 is now the behavioral teeth (not free)

`node.h` **IS `lib/core`** → the simulator compiles it → **s18 byte-identity (`md5 = 3ac88d40e00d2605ff66659f696d52bf`) is the proof the reorder changed nothing.** Every prior `fw_main` slice had s18 inert-by-construction (the sim compiles `lib/core`, not `src/`); here it is **load-bearing**. Any reorder that alters behavior *will* move the md5. This is the single most important mental shift for this slice.

---

## 2. ★ The new risk class (reorder-specific) + two extra gate checks

A member reorder introduces two hazards the `fw_main` function-moves never had:

### 2a. Declaration-order init semantics (`-Wreorder`)
C++ initializes members in **declaration order**, regardless of the ctor init-list order.
- Node's init-list (`node.cpp:28`) = `: _hal(hal), _node_id(node_id), _key_hash32(key_hash32)` — only **3 members, all param-derived, NO cross-member deps**. Keep these three in declaration order matching the list (or reorder the list to match — harmless here, no cross-dep).
- Every other member uses a default in-class initializer (`= {}` / `= 0`) — POD, no cross-init deps (audited; the `_*_count`/`_*_n` scalars init to 0 independently of their arrays).
- **GATE:** the build MUST be `-Wreorder`-clean. Treat any `-Wreorder` as a **signal to heed** (a real init-order change), never `-Wno-reorder` it away. PLUS a per-section manual audit: confirm no member whose default/ctor init *reads another member* was moved across that member. (Current audit: none exist — re-verify per moved section.)

### 2b. `sizeof(Node)` / padding drift — TWO DISTINCT JOBS
Reordering shifts padding → layout can change. These are **two different checks, don't conflate them:**
- **Layout-invariance tripwire (deterministic):** add `static_assert(sizeof(Node) == <native-baseline>, "node.h reorder must not change Node layout");` under `#ifdef MESHROUTE_NATIVE`. This asserts the **native layout is unchanged** by a reorder — it is NOT the RAM guard (native `sizeof` ≠ nRF52 `sizeof`: different pointer/enum/alignment). **Keep it permanent** as a layout-invariance regression guard. ⚠ We are still in development — members legitimately change over time; when they do, the baseline is *updated deliberately* (the assert is a tripwire that forces a conscious "yes, layout changed on purpose", not a frozen contract). Label it "layout must not change," never "RAM budget."
- **RAM guard (the actual nRF52 constraint):** diff `firmware.map` `.bss`/`.data` on a `gateway` build before/after. This is the real fixed-memory check. Ideally grouping same-alignment members **holds or shrinks** RAM; **verify, don't assume.**
- Capture the native `sizeof(Node)` baseline FIRST (temp `static_assert(sizeof(Node)==1)` → read the actual size from the compile error → set it).

---

## 3. The target concern sections (match the 13 TU owners)

**RULE:** each data member moves **only within its current storage tier**. The Node-global ↔ `LayerRuntime` tier boundary is load-bearing (crossing it breaks per-leaf non-aliasing / single-attachment identity per §4). No nesting, no struct extraction, no plane sub-structs. Methods, struct definitions, and the public/private method islands stay exactly where they are.

**Tier A — Node-global (`class Node` body):**
1. **Identity & crypto** — `_node_id`, `_key_hash32`, `_name`/`_name_len`, `_x_secret`, `_ed_pub`, `_crypto_ready`.
2. **Config** — `_cfg`.
3. **Remote-mgmt** (`#if MR_FEAT_REMOTE_MGMT`) — `_admin_pubkey`, `_admin_counter_floor`, `_admin_provisioned`, `_remote_inbound`.
4. **Inbox** — `_inbox`.
5. **Gateway / cross-layer scheduler** — `_layers`/`_active`/`_n_layers`, `_gw_schedules`, `_bridged_layers`, `_xl_handoffs`, **`_window_epoch_ms`** (← the §5 relocate target).
6. **Duty / airtime / beacon witnesses** — the duty EWMA (`_dm_payload_mean`), R4.3 channel-busy witnesses, `_next_open_ms`/`_last_beacon_ms`, beacon/timer scalars.
7. **Team plane** (`#if MR_FEAT_TEAM`) — `_team_local_id`, `_team_dad_pending`.
8. **Mobile-MEMBER identity** (`#if MR_FEAT_MOBILE`) — `_my_mobile_reg`, `_mobile_offers`, `_learned_layers`, `_mobile_scan_idx`, `_mobile_backoff_ms`.

**Tier B — per-leaf `LayerRuntime`:**
1. **Routing (static plane)** — `_rt`/`_rt_count`.
2. **Routing (team plane)** (`#if MR_FEAT_TEAM`) — `_rt_team`/`_rt_team_count`, `_team_peer`, `_team_keys`/`_team_keys_n`.
3. **MAC / flight** — `_tx_queue`/`_tx_queue_n`, `_pending_tx`, `_pending_rx`, `_post_ack`, `_deferred`/`_deferred_n`, `_drain_armed`.
4. **Route discovery (F)** — `_rreq_seen`/n, `_rreq_last`/n.
5. **DAD / id-bind** — `_id_bind`/`_id_bind_n`, join/claim state.
6. **Crypto / peers** — `_peer_keys`/n, `_hash_query_seen`/n, `_mobile_home_cache`/n.
7. **Liveness / freshness** — `_peer_liveness`/n; **and the §6 no-discriminator cluster** (`_dest_seen_ms`, `_link_bidi`, `_link_bidi_confirmed_ms`, `_link_reprobe_last_ms`, `_mobile_peer`, `_blind_until`, `_neighbor_budget_tier`, `_per_sender_originator`).
8. **Channel / flood** — `ChannelEntry` buffer, `FloodState`, origin ledger, pull/re-offer rings.
9. **Host-side mobile registry** — `_mobile_reg` (per-leaf), `_mobile_peer` if filed here rather than §7.

**Straddler filing (RESOLVED — "both, by member"):**
- **Cross-plane shared substrate = its own headed section** (this is the §6 cluster). Home for the no-plane-discriminator aliasing arrays (`_link_bidi`/`_link_bidi_confirmed_ms`/`_dest_seen_ms`/`_link_reprobe_last_ms`/`_blind_until`/`_neighbor_budget_tier`/`_per_sender_originator`) **plus `_mobile_peer`** — it's where the leak-comment lives, so §3.B7's "liveness" and this substrate section merge into one clearly-headed block.
- **Benign straddlers → dominant TU owner + a one-line straddle note in the section.** `_id_bind` files under DAD/id-bind (its dominant owner) with a note that it also feeds crypto (`key_hash_for_id`) + E2E DST_HASH. `_mobile_home_cache` files under crypto/peers (sender-side hash→home) with a note it ships in the static build (not `#if MR_FEAT_MOBILE`).

---

## 4. Invariants the reorder must preserve (the traps)

- **Friend seam untouched.** `friend struct DualLayerTestAccess;` (`node.h:1589`, inside `#ifdef MESHROUTE_NATIVE` @1585) + the timer-id constants (`kBeaconTimerId=1`@803 … @847) + `kTxQueueCap`@ the `static constexpr` block stay **Node `static constexpr` members** named as `Node::kX`. A reorder changes no names/types/scope → the seam stays intact **by construction**. (Do NOT move a timer-id to a free constant / detail namespace — the qualified `Node::kX` reference only compiles via the friend grant.)
- **Public façade + white-box accessors stay public.** `test_*`/`set_*_for_test`/`config()`/`active_bw_hz`/`active_cr`/`channel_cap_origin()` are the *non-friend* contract used by `test_node_channel.cpp` and `test_node_r3.cpp`. Collapsing any into private breaks two test files that do NOT friend Node.
- **Counter-adjacency.** Keep each explicit count paired adjacent to its array — especially `_channel_pull_recent_n` ↔ `_channel_pull_recent[]` (`node_channel.cpp` pull-recent scan desyncs otherwise), and every `_*_count`/`_*_n` pair.
- **`_layers[]` contiguous + `_cfg.layers[i]` parallel** — the four `_active - &_layers[0]` sites (`active_layer_id`/`active_bw_hz`/`active_cr` @541/546/550) pin `_layers` vs `cfg.layers`; don't disturb their parallel indexing.
- **All 13 `node_*.cpp` TUs compile** — names unchanged → recompile-clean; def-before-use preserved (no struct def moves in this slice).
- **`clear_learned_state`/reprovision paths** iterate `_layers[i]` for all `i < _n_layers` (incl. the `#if MR_FEAT_TEAM` scrub) — a reorder must not separate a member from the loop that clears it.

---

## 5. The two critic fixes (fold-in-safe hygiene)

1. **Relocate `_window_epoch_ms`** (`node.h:1296`) — Node-global gateway/cross-layer scheduler state (grid anchor; set `node.cpp:271`, read `node.cpp:521-525`) currently **wedged** between the duty EWMA (`_dm_payload_mean`@1295) and the R4.3 channel-busy witnesses (@1298+). Move it into §3.A5 (gateway/cross-layer) next to `_gw_schedules`/`_bridged_layers`/`_xl_handoffs` + the `_layers`/`_active`/`_n_layers` block. Same-scope move, layout-neutral. *(A naïve "gather gateway state by inventory group" would orphan it — call it out.)*
2. **Normalize `_link_reprobe_last_ms` comment** (`node.h:1528`) — reads "FULL 0..255 range" while the sibling `[256]` arrays `_dest_seen_ms`@1513 / `_link_bidi`@1518 read "0..254". **PRE-CHECK done:** node_id `0xFF` (255) is the reserved id (never a real index), so "0..254" is correct — reword to match. Pure comment edit (code is truth).

---

## 6. The plane-leak legibility comment (surface tech-debt, no behavior change)

Group the node_id-indexed arrays with **no plane discriminator** (§3.B7 cluster) under one section whose header states plainly: these carry no plane discriminator, so a team/mobile `local-id` write aliases the same array a static `node_id` uses (`node_mac_rx.cpp:1267,1352`, `node_routing.cpp:298-301`) — **known cross-plane leak sites, fix tracked on the `PlaneRuntime` slice.** This makes the seam legible exactly where the next reader will meet it, **without touching behavior** (the fix is a later, separately-gated track). Do NOT let the cosmetic pass imply these are plane-clean.

---

## 7. Staging — 3–4 cohesive increments (RESOLVED)

Not per-section (17 = QG overkill), not per-tier (2 = a break bisects too wide). **3–4 cohesive increments**, each independently QG-able:
1. **Tier-A-simple** — Identity/crypto → Config → Remote-mgmt → Inbox.
2. **Tier-A-complex + fix#1** — Gateway/cross-layer (relocate `_window_epoch_ms`) → Duty/beacon witnesses → Team / Mobile-MEMBER.
3. **Tier-B-routing/mac** — LayerRuntime: Routing (static) → Routing (team) → MAC/flight → Route-discovery.
4. **Tier-B-crypto/liveness + fix#2 + §6** — DAD/id-bind → Crypto/peers → the cross-plane-shared-substrate section (fix#2 comment + the §6 leak comment) → Channel/flood → Host mobile registry.

The **hard gate runs after each increment regardless of size** — **s18 md5 exact (`3ac88d40`) + `-Wreorder`-clean + the `sizeof(Node)` assert holds**. s18's md5 is a cryptographic proof of behavior-invariance, so smaller steps buy **bisectability, not confidence**. Full board matrix + `firmware.map` RAM diff run at least at the end (per-increment if cheap).

---

## 8. Gate summary (per step + final)

| Check | Bar | Note |
|---|---|---|
| native | 736 / 25359, 0 failed | shared-header compile |
| **s18** | **md5 == `3ac88d40e00d2605ff66659f696d52bf`** | ★ THE behavioral teeth — must match |
| `-Wreorder` | clean | heed, never silence; + per-section init-order audit |
| `sizeof(Node)` | unchanged | native `static_assert` baseline (add first) |
| boards | 10/10 SUCCESS | + `firmware.map` `.bss`/`.data` not bloated (final) |

---

## 9. Open questions — RESOLVED (user, 2026-07-15)
1. **`static_assert(sizeof(Node))`** — keep **permanent**, framed as a *layout-invariance tripwire on the native layout* (NOT the RAM guard — that's the `firmware.map` diff); baseline updated deliberately when members legitimately change in dev. → folded into §2b.
2. **Straddler filing** — **both, by member**: dedicated cross-plane-shared-substrate section for the no-discriminator cluster + `_mobile_peer` (where the leak-comment lives); dominant-owner + straddle note for the benign ones (`_id_bind`, `_mobile_home_cache`). → folded into §3/§6.
3. **Granularity** — **3–4 cohesive increments** (not per-section, not per-tier); hard gate after each. → folded into §7.
