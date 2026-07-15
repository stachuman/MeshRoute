// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# Normal-node id reservation (17..254) — the complementary half of the gateway 1..16 reservation

**Status:** design for implementation (next step). Short, focused. This **implements G1** of the canonical DAD spec `docs/specs/2026-06-05-node-id-auto-assignment-design.md` (`:5`) — which already mandates *"`join_choose_candidate_id` MUST pick from `[17,254]` only"* and "gateway per-leaf DAD = DEFERRED." That requirement is **specced but not yet in the picker** (it still scans `1..254`, `node_join.cpp:105`). Pairs with the gateway-side `docs/superpowers/specs/2026-06-19-gateway-provision-command-design.md` (`node ∈ 1..16`).

## 1. Goal

Enforce the node-id reservation (G1 of the canonical DAD spec, `2026-06-05-node-id-auto-assignment-design.md:5`) on the **normal-node** side, so normal nodes never take a gateway id:

| range | meaning |
|---|---|
| `0` | fresh / unjoined (`protocol::unjoined_node_id`) |
| `1..16` | gateways only (the gateway command enforces this) |
| **`17..0xFE`** | **normal nodes (this spec)** |
| `0xFF` | reserved (broadcast sentinel; `node.cpp:28` panics on it as an id) |

This is a **provisioning/DAD-time** convention, NOT a wire/`on_init` invariant (see §3 — a hard `on_init` check would regress the baseline sim suite).

## 2. Changes (3 sites + 1 constant)

1. **Constant** (`protocol_constants.h`, near `cap_routes`): add
   ```
   inline constexpr uint8_t gateway_node_id_max = 16;   // 1..16 reserved for gateways
   inline constexpr uint8_t normal_node_id_min  = 17;   // normal nodes: 17..254
   ```
   The gateway command's `node ∈ 1..16` check should reference `gateway_node_id_max` too (DRY across the two specs).

2. **DAD picker** `join_choose_candidate_id` (`node_join.cpp`) — the §3 candidate selection of the DAD spec, the site G1 names:
   - free-list scan (`:105`): `for (int id = 1; …)` → start at `protocol::normal_node_id_min` (17).
   - prev-id preference (`:86`): `prev >= 1` → `prev >= protocol::normal_node_id_min` (so a legacy/NV prev id in 1..16 is NOT re-preferred — it re-picks a normal id).
   - The distinctness/heal logic, denied-list, and `id_taken` are unchanged (the pool just shrinks to 17..254 = 238 slots; `free_list[254]` stays fine).
   - *Forward note (not this task):* when gateway DAD lands, a gateway (`n_layers==2`) picks `1..gateway_node_id_max`; until then the picker is normal-only, so the literal `normal_node_id_min` lower bound is correct.

3. **`cfg set node_id`** (`fw_main.cpp:331-334`), build-conditional (MR_N_LAYERS is per-build):
   ```
   #if MR_N_LAYERS >= 2   // gateway build: layer-0 (and l1_node_id) are gateway ids
       reject v != 0 && (v < 1 || v > gateway_node_id_max)   → "gateway node_id 1..16"
   #else                  // normal build
       reject v < 0 || v > 254 || (v >= 1 && v <= gateway_node_id_max)
           → "node_id 0 or 17..254 (1..16 reserved for gateways)"
   #endif
   ```
   (`0` = unprovision, allowed on both.) For symmetry on the gateway build, apply the same `1..16` bound to `cfg set l1_node_id` (`fw_main.cpp:418`).

## 3. Explicitly EXEMPT (do NOT touch — verified)

- **Sim scenario provisioning.** `FirmwareNode` seeds `node_id` from the scenario via `_protocol_id` (`FirmwareNode.cpp:52`) — it **bypasses the picker and `cfg set`**, so static scenario ids (gateways at `110`, normal nodes at `1`/`11`/`12`) keep working with NO change. This is *why* the reservation lives at the picker/console, not deeper.
- **`on_init`.** No hard range refusal. Verified: a hard check would refuse the existing scenarios above and regress `simulation/BASELINE.md`. The reservation is a provisioning convention; the wire/runtime treats all of `1..254` as valid ids.
- **Migration:** an existing NV blob with a normal node *cfg-pinned* to `1..16` is not auto-migrated (pinned ids don't DAD). Acceptable — the operator re-provisions; the new `cfg set` guard prevents new such pins.

## 4. Gates

- **`pio test -e native`:** add (a) the picker never returns `1..16` (empty `id_bind` → assert pick ∈ 17..254; seed-vary to cover the random path); (b) a prev-id in `1..16` is NOT preferred (re-picks ≥17); (c) `cfg set node_id` on a normal build rejects `1..16`, accepts `0` and `17..254`.
- **DAD gates `t91/t92/t93`** (the join suite — the only place the picker is exercised end-to-end): re-run. The distinctness/heal *properties* are unchanged, so they should still pass; **if any pins a specific picked id value, update the expectation to the 17..254 range.**
- **`simulation/BASELINE.md`** via `dm_delivery_breakdown.py`: no regression, `leaks==0` (expected — static scenario ids bypass the picker, §3).
- **3 boards** compile (normal `xiao_sx1262`/`heltec_v3` + the gateway build), confirming the `#if MR_N_LAYERS` split.

## 5. Out of scope
- The gateway-side enforcement (the `gateway` command + gateway-build `cfg set` ranges) — its own spec.
- Gateway DAD (the picker's `1..16` branch) — deferred; gateways are static-provisioned today.
- Renaming/migrating legacy ids.
