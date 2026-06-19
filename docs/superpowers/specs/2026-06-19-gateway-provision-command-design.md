// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>

# `gateway` — one-command gateway provisioning (design)

**Status:** REVIEWED 2026-06-19 → decisions locked, READY TO IMPLEMENT (coder implements after reading §3/§6). The quality review verified the factual basis (NV blob v10, the `cfg set` keys, `live=false`, the schedule-on-beacon path) and the author resolved the open points. Folded-in decisions:
- **Node-id reservation (documented convention, NOT enforced here):** `0` = fresh/unjoined · **`1..16` = gateways** · `17..0xFE` = normal nodes · `0xFF` = reserved. **The reservation goes live in the to-be-finished Join/DAD process — NOT in this command and NOT at `on_init`** (decided 2026-06-19). The command validates `node ∈ 1..254`; the two gateway leaves MAY share a node_id.
- **Validation must not drift from `on_init`** — extract a shared predicate (§3.1); the command must reject exactly what `on_init` rejects, so a `gateway OK` never dies at boot.
- **Window split is automatic** (SF-weighted), **validated, and displayed** to the operator after provisioning (§2/§3.2).
- **§3a split:** keep only the reboot-path schedule beacon here; the *live* gateway-reconfig path is carved into its own follow-up spec (§5).
- **`is_gateway` is build-time + derived**, not a set field (§2).

---

## 1. Problem (verified, current state)

A gateway is a dedicated **build** (`meshroute_core_gw`, `-DMR_N_LAYERS=2`); provisioning it = giving that build a valid 2-layer config (`n_layers=2` + both layers' fields). Today that takes **~12 separate `cfg set` commands + a reboot**, spread across **three naming conventions** for the same concepts:

| concept | layer 0 (home) | layer 1 (guest) |
|---|---|---|
| node id | `cfg set node_id` *(top-level legacy)* | `cfg set l1_node_id` |
| control SF | `cfg set routing_sf` / `control_sf` *(top-level)* | `cfg set l1_routing_sf` |
| data SFs | `cfg set sf_list` *(top-level)* | `cfg set l1_sf_list` |
| leaf id | `cfg set layer0_id` | `cfg set l1_layer_id` |
| window | `cfg set l0_window_ms` / `l0_window_offset_ms` | `cfg set l1_window_ms` / `l1_window_offset_ms` |

…plus `n_layers 2`, `window_period_ms`, `l1_beacon_ms`. Layer 0 mixes **top-level + `layer0_id` + `l0_*`**; layer 1 is uniformly **`l1_*`** — asymmetric (`node_id` vs `l1_node_id`, etc.), easy to fumble. Every gateway field is `live=false` (applied at `on_init`, verified `fw_main.cpp:331-446`) → a reboot is required regardless. All fields persist to the `/mrcfg` blob (`device_nv.h`, **v10**, verified). **`is_gateway` is NOT a `cfg set` key** — it is derived at `on_init` (`_cfg.is_gateway = (n_layers==2)`, `node.cpp:100`), so it is never set by hand.

## 2. Design — the `gateway` command

One command provisions a complete 2-layer gateway, symmetric per leaf:

```
gateway l0=<leaf>:<node>:<ctrl_sf>:<data_sfs>  l1=<leaf>:<node>:<ctrl_sf>:<data_sfs>  [opt=val ...]

e.g.  gateway l0=4:10:8:7,9  l1=5:11:9:7,9
      gateway l0=4:10:8:7,9  l1=5:11:9:7,10  period=15000        # windows auto-derived + shown
      gateway l0=4:10:8:7,9  l1=5:11:9:7,10  period=15000 win0=5000:0 win1=5000:7500   # advanced override
```

**Required tokens** (order-independent):
- `l0=<leaf>:<node>:<ctrl_sf>:<data_sfs>` — home leaf.
  - `leaf` = the full 8-bit `layer_id`, **1..255** (NOT 0 — `on_init` refuses `layer_id==0`, `node.cpp:68`).
  - `node` = **1..254** (a valid short id; the `1..16` gateway-reservation is documented but **not enforced here** — see note). MAY equal `l1.node`.
  - `ctrl_sf` = 5..12; `data_sfs` = comma list of SFs 5..12 (e.g. `7,9`), non-empty.
- `l1=…` — guest leaf, same 4-field shape. **The two leaf NIBBLES `(leaf & 0x0F)` MUST differ** (the byte-0 wire filter; same-nibble layers alias — `on_init` refuses, `node.cpp:75`). NB this is *not* "full leaf differs" — `l0=4 l1=20` share nibble 4 and are rejected.

**Optional `key=val`:**
- `period=<ms>` — shared window cycle (`window_period_ms`, must be > 0). Default: keep current / a sane default if unset.
- `win0=<ms>:<off>` / `win1=<ms>:<off>` — **advanced override** of the per-leaf window + phase. **Default omitted ⇒ auto-derive** the SF-weighted anti-phase split (the normal path — see below). If given, they are range/overlap/fit-validated like `on_init`.
- `beacon=<ms>` — layer-1 beacon cadence (`l1_beacon_period_ms`). (Layer-0 cadence stays the top-level `beacon_ms`; add `beacon0=`/`beacon1=` later if symmetry is wanted — minor, out of scope.)
- `gateway_only=0|1` — §7 pure-bridge flag. Default 0.

**Window split — automatic + shown.** Normally the operator gives only SFs + `period`; the command computes the SF-weighted anti-phase split (same formula as `on_init`, `node.cpp:108-122`: `window_i = period · per_byte_air(sf_i) / Σ`, `offset₁ = window₀`), validates it, and **prints the derived schedule in the OK summary** so the operator sees what will run. `win0/win1` is only for manual override.

**`is_gateway` / `n_layers`:** the command implies `n_layers=2`. `is_gateway` is **build-time + derived** (a node running the gateway build is a gateway; `on_init` derives the flag) — the command does **not** treat it as a set field. **The command is only valid on a gateway build** (`MR_N_LAYERS>=2`); on a normal build it must error `not_gateway_build` and persist nothing (else `on_init` refuses the `n_layers==2` blob at boot, `node.cpp:62`, leaving the node down).

### Node-id reservation (new, 2026-06-19)
| range | meaning |
|---|---|
| `0` | fresh / unjoined (no sends) |
| **`1..16`** | **gateways** (convention; enforced at Join/DAD, NOT by this command — leaves may share an id) |
| `17..0xFE` | normal nodes |
| `0xFF` | reserved |
**This command does NOT enforce the reservation** — it validates `node ∈ 1..254` (both leaves). The reservation becomes live in the **to-be-finished Join/DAD process** (the DAD picker chooses gateway ids from `1..16`, normal from `17..254`) — see §5. Do NOT add the ranges as `on_init` hard-refusals (§5 caution).

### Field mapping (command → `/mrcfg` blob, v10 — no schema change)
| token | blob field(s) |
|---|---|
| `l0` leaf / node / ctrl_sf / data_sfs | `layer0_id` / `node_id` / `routing_sf` / `allowed_sf_bitmap` |
| `l1` leaf / node / ctrl_sf / data_sfs | `l1_layer_id` / `l1_node_id` / `l1_routing_sf` / `l1_allowed_sf_bitmap` |
| (always) | `n_layers=2` (is_gateway is derived — NOT written as a meaningful value) |
| `period` / `win0` / `win1` / `beacon` / `gateway_only` | `window_period_ms` · `l0_window_ms`/`l0_window_offset_ms` · `l1_window_ms`/`l1_window_offset_ms` · `l1_beacon_period_ms` · `gateway_only` |

(All fields exist in the v10 blob — `device_nv.h:42-58` — **no NV schema change**.)

## 3. Behavior — atomic, fail-loud, persist + prompt reboot

1. **Build guard:** on a non-gateway build (`MR_N_LAYERS<2`) → `> gateway err not_gateway_build`, persist nothing.
2. **Parse + validate EVERYTHING first** into a staging struct. On ANY error (bad field, missing `l0`/`l1`, out-of-range, bad SF list, unknown opt key) → a specific `> gateway err <reason>`, **apply nothing** (no half-configured gateway).
3. **Persist:** `mrnv::load(b)` the CURRENT blob first, mutate ONLY the gateway fields above, then `mrnv::save(b)` once — so unrelated v7–v10 fields (BLE policy, `loc_in_dm`, `e2e_dm`) and the identity record are preserved. On save failure → `> gateway err nv_save_failed`, apply nothing.
4. **Prompt reboot:** print `> gateway OK …` + the one-line summary **including the derived window split**. Gateway init is `on_init`, so it takes effect on the next boot. No auto-reboot; no live-apply.

`cfg` / `cfg set` keep working for individual inspection/tweaks; `gateway` is the provisioning shortcut, not a replacement.

### 3.1 Validation MUST share `on_init`'s predicate (no drift)
The command's validation must reject **exactly** what `on_init` rejects — otherwise a `gateway OK` line persists a config that `on_init` refuses at boot, and the node silently isn't a gateway (defeating fail-loud). **Extract `on_init`'s §3.2 gate (`node.cpp:61-133`) into a pure helper** that BOTH `on_init` and `parse_gateway_cmd` call:

```
err validate_gateway_layers(const LayerConfig& L0, const LayerConfig& L1);  // pure, no Serial/NV
// checks: layer_id 1..255 (≠0); routing_sf 5..12; allowed_sf_bitmap ≠ 0;
//         (L0.layer_id & 0x0F) != (L1.layer_id & 0x0F);  window_period equal & > 0;
//         window derive (SF-weighted) → window_ms ≠ 0, offset+ms ≤ period, no overlap,
//         window ≤ gateway_schedule_window_max_ms (25.5 s wire cap).
```

The command additionally checks `node ∈ 1..254` (a valid short id) — but **NOT** the `1..16` gateway-reservation (that's deferred to the Join/DAD process, §5; enforcing it here OR in the shared predicate would regress the sim suite — §5 caution). `parse_gateway_cmd` returns the staged `LayerConfig`s + the derived windows so the OK summary can print them.

### 3a. Gateway-parameter changes → refreshed FULL schedule beacon (REBOOT PATH ONLY)

A gateway's neighbours time their RTS to its advertised **window schedule**; a stale schedule breaks cross-layer delivery. For the `gateway` command (reboot-only) this is **covered naturally**: on boot the gateway emits full (discovery) beacons, and `emit_beacon` already appends the schedule block for `n_layers==2` (verified `node_beacon.cpp:254-278`). **Confirm on metal** the first boot beacon is full-with-schedule.

> The *live-tweak* path (change a schedule field on a running gateway → fire a fresh full beacon + re-arm the window scheduler at runtime) is **OUT OF SCOPE here** — it contradicts this command's reboot-only design and is a separate, larger piece. Carved into its own follow-up spec (§5).

### 3b. Help text (in scope)
`dump_help()` (`fw_main.cpp:567`; invoked at `:638`) doesn't document the gateway path. **Add one `gateway l0=… l1=… [opts]` usage line + a one-line example**, and make the dual-layer story discoverable (the `l1_*` / `layer0_id` / `l0_*` keys are effectively undocumented). The `gateway` command is the documented way in; raw `cfg set` keys are the advanced path.

## 4. Examples + error surface
```
> gateway l0=4:10:8:7,9 l1=5:11:9:7,9
  gateway OK — L0 leaf4 id10 sf8 data{7,9} · L1 leaf5 id11 sf9 data{7,9} · n_layers=2
  windows (derived, period 15000): L0 7034ms@0 · L1 7966ms@7034 (SF-weighted) · reboot to apply
> gateway l0=4:10:8:7,9 l1=20:11:9:7,9
  gateway err leaf_nibble_clash (l0 & l1 leaf low-nibble must differ: 4 vs 20&0x0F=4)
> gateway l0=0:10:8:7,9 l1=5:11:9:7,9
  gateway err bad_leaf (leaf 1..255)
> gateway l0=4:0:8:7,9 l1=5:11:9:7,9
  gateway err bad_node (node 1..254)        # node 10, 110, 21… all valid — the 1..16 reservation is NOT enforced here
> gateway l0=4:10:13:7,9 l1=5:11:9:7,9
  gateway err bad_ctrl_sf (ctrl_sf 5..12)
> gateway l0=4:10:8:7,9
  gateway err missing l1
```
(Window values illustrative.) NB **no `same_node` error** — the two leaves MAY share a node_id (both in 1..16).

## 5. Out of scope (explicit)
- **No NV schema change** (reuses v10 fields).
- **Not** renaming/removing the existing `cfg set` keys (they coexist; a later cleanup could unify them).
- **Not** auto-reboot, **not** live-apply (per the chosen flow).
- **The live gateway-reconfig path** (former §3a live-tweak): make schedule fields live-applicable + re-arm the window scheduler at runtime (`activate_layer`/`window_switch_fire`) + fire a full `sync` beacon on change. Its own spec/slice — bigger than a console command.
- **The node-id reservation enforcement (gateways `1..16` / normal `17..254`)** — goes live in the **to-be-finished Join/DAD process**: the DAD picker `join_choose_candidate_id` (`node_join.cpp:84`, today `1..254`) chooses gateway ids from `1..16` and normal ids from `17..254`. **Neither this `gateway` command nor `on_init` enforces it** (user 2026-06-19) — the command stays at `1..254`; the convention is realized at join/claim time.
  - **Caution (verified 2026-06-19):** do NOT enforce the 1–16 / 17–254 split as `on_init` hard-refusals. Existing gateway scenarios use ids WELL outside 1..16 — **s09: 10/110/119/120 · s15: 31–37 · s16: 21/22** — and normal nodes use `1`/`11`/`12`; a hard `on_init` range check would reject every one and regress the baseline suite. The reservation is a **Join/DAD-time** convention, never a wire/`on_init` invariant.
- A symmetric single-layer `node`/`provision` shortcut — natural follow-up, out of scope here.

## 6. Test plan
- **Unit (native):** factor parse+validate into the pure `parse_gateway_cmd(const char* args, GatewayProvision& out, err&)` + the shared `validate_gateway_layers` (no Serial/NV) and TDD:
  - a valid line populates all fields **and the derived window split** (SF-weighted; assert the computed `window_ms`/`offset` match `on_init`'s formula for a known SF pair);
  - each error returns the right code and mutates nothing: missing `l0`/`l1`, `bad_leaf` (0 / >255), `leaf_nibble_clash` (e.g. 4 vs 20), `bad_node` (0 / 255 — node range is **1..254**; assert 10/110/21 are ACCEPTED, i.e. the 1..16 reservation is NOT enforced), `bad_ctrl_sf`, bad SF list, `period=0`, explicit-window overlap / `offset+ms>period` / `>25.5 s`, unknown opt;
  - **parity:** `validate_gateway_layers` accepts a config iff `on_init` accepts it (a couple of round-trip cases — the anti-drift guard);
  - **no `same_node` test** (sharing a node_id is allowed).
- **Metal smoke (the goal):** flash a heltec (gateway build), run `gateway l0=… l1=…`, `reboot`; confirm `cfg` shows the dual-layer config, the node boots as a gateway (`meshroute_core_gw` / 2-layer schedule), and its first beacon is a **FULL page carrying the schedule block** (§3a). `help` lists the `gateway` usage (§3b). On a **normal build**, `gateway …` returns `not_gateway_build` and persists nothing. Pairs with the device OTA already in place.
