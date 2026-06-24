<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# `prep-restart` — clear learned state, keep the join, then HALT (for a coordinated clean fleet restart)

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Firmware change; **no wire change**, no sim impact (device-console only).

## Why

Between bench runs the nodes accumulate cruft — stale routes, a filling inbox — and a node can re-learn a stale topology the instant it's reset, because its still-running neighbours keep beaconing the old routes. The user needs a **middle-tier reset**: drop the learned state (routes + inbox + the rest) but **keep the provisioning** (node_id / leaf / level_id / sf_list / lineage / identity — no re-join needed), and **don't reboot** — go **dormant**. Run it on every node → the network falls silent (no stale beacons to cross-poison) → power-cycle the whole fleet → everyone converges from true zero. A plain power-cycle won't do: it clears RAM routes but **leaves the QSPI inbox**, and a one-at-a-time reset re-poisons from the still-live neighbours.

**Decisions (user, 2026-06-24):** name = **`prep-restart`**; keep `/mrcfg`+`/mrid`; clear routes + inbox + learned state; **fully dormant** while halted (no beacon / no forward / no RX-learning / no TX) but **console-responsive**; un-halt only via power-cycle (or `reboot`).

## Verified current state

- `Node::clear_routing_state()` (node.h:438) — a routes clear already exists.
- `g_inbox_dm.wipe()` / `g_inbox_ch.wipe()` (device_inbox_store.h:77 → `qspi_seg_erase` per segment) — the QSPI inbox-records erase (`factory_reset` already calls it, fw_main.cpp:605).
- `factory_reset` (fw_main.cpp:601) is the *heavy* tier — it also `mrnv::factory_erase()`s `/mrcfg`+`/mrid` (→ unprovisioned) + reboots. `prep-restart` must do **neither** of those.
- The loop's operating block (RX poll/`on_recv`, `take_preamble`, due-timers, `service_tx`, `sample_noise`, inbox-flush, push-drain, sleep) all sits **after** `fault_wdt_feed`/`fault_scratch_alive` (fw_main.cpp:1614-1615) — easy to gate behind a halt flag while still feeding the WDT + servicing the console.
- Command dispatch = the flat `strncmp` table (fw_main.cpp:1011+).

## The change

1. **`Node::clear_learned_state()`** (new): drop every VOLATILE/learned table, KEEP `_cfg` + identity + crypto. Routes (`clear_routing_state()`), the channel buffer (+ seen_by + pull ring + per-origin anti-spam ledgers), peer liveness, pending TX/RX, flood-state, the digest/pull pending — back to a fresh-but-provisioned state. (Implement as explicit clears, OR a careful re-`on_init(_cfg)` if the coder verifies it's re-entrant and doesn't strand an in-flight TX/timer — explicit clears are the safer default.) Must NOT touch `_cfg` (node_id/leaf/level_id/sf_list/lineage/config_epoch) or the crypto identity.
2. **`g_halted`** (new `static bool` in fw_main, **RAM** so a power-off clears it): while true the loop **skips the entire operating block** — but ALWAYS still does `fault_wdt_feed()` (so the deliberate halt is NOT mistaken for a hang → no WDT reset), `fault_scratch_alive()`, and `service_console()` (so you can still talk to it). Wrap the operating block: `if (!g_halted) { …RX/timers/tx/beacon/sleep… }`.
3. **`prep-restart` command** (`handle_prep_restart`): `g_node.clear_learned_state()` → `g_inbox_dm.wipe(); g_inbox_ch.wipe()` → `g_halted = true` → print a loud confirmation:
   `> prep-restart — routes + inbox cleared, network membership KEPT, node HALTED. Power-cycle the fleet to restart clean.`
   No extra confirm word (it's non-destructive to the join; re-running is harmless). Wire into the dispatch table (`len==12 && !strncmp(line,"prep-restart",12)`) + `dump_help` + the JSON console (so the companion/harness can issue it).
4. **`status` while halted:** add a ` halted=1` field (or reuse `state`) so a connected operator/harness sees the node is intentionally dormant, not wedged.

## Operational flow (what it enables)

`prep-restart` on every node (one by one is fine — each goes silent immediately, so order doesn't re-poison) → all dormant + inbox-wiped + membership intact → **power-cycle the whole fleet** → each boots with empty routes/inbox (RAM gone, inbox wiped, `g_halted` gone) and operates → clean convergence, no stale-route carryover, no re-provisioning.

## Tests / gate

- **Native unit** (`test_node_*`): `clear_learned_state()` empties routes/channel-buffer/liveness/pending (assert `rt_count()==0`, channel buffer empty, etc.) while `config()` (node_id/leaf/level_id/sf_list/lineage) and the crypto identity are **unchanged**. A second normal operation after the clear still works (re-learns routes) — proves it's a clean reset, not a break.
- **Build:** `pio run -e gateway -e xiao_sx1262 -e heltec_v3 -e xiao_esp32s3` (the `g_halted` gate + the inbox wipe are platform-neutral; the inbox wipe is already `#if`-guarded by the store).
- **No sim impact:** device-console only; `sim_main` never builds fw_main. BASELINE untouched.
- **Metal (user):** `prep-restart` → `status` shows `halted=1`, routes/inbox empty, but `cfg`/`whoami` still show the provisioned id/leaf/sf_list; the node stops beaconing (a neighbour stops hearing it) and answers the console; a **power-cycle** brings it up provisioned + operating with empty routes. Run it fleet-wide → coordinated power-cycle → clean convergence.

## Note

`reboot` also clears `g_halted` (boots fresh) — fine for a single node, but the intended flow is `prep-restart` all → power-cycle all (so no node re-learns stale routes from a still-live neighbour). Independent of the channel-trace + pacing work.
