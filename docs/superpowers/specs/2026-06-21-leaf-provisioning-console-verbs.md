<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Leaf-provisioning console verbs — `join` / `create` / `leave`

**Status:** READY FOR CODER (decisions locked 2026-06-21). Simplifies **normal-node** leaf provisioning into 3 verbs over the existing `cfg set` + `leaf create` mechanisms. **Live-apply (no reboot).** Gateways out of scope (§5). Update `docs/LEAF_PROVISIONING.md` to these verbs after. **REVISED 2026-06-21:** also folds in the **companion-JSON surface** (§7b — 3 small `console_json` additions) so the iOS app's leaf-membership UI lights up (see `ios-companion/INBOX_SYNC_CONTRACT.md`).

## 0. Locked decisions
1. `join`/`create` carry the full radio floor: **freq, bw, ctrl_sf, level_id**. CR is **not** an arg — stays default **5 (4/5, low)**; LoRa CRs interoperate (RX reads CR from the header) so it needn't match across nodes. **`level_id` is user-facing; `leaf_id = level_id & 0x0F` is derived internally.**
2. **No reboot** — apply live (the live radio re-tune exists, `fw_main.cpp:248`).
3. `leave` **wipes to default, keeps only `freq`**.
4. `create` is **one-shot**, arg order consistent with `join` + the leaf's data config.
5. **Normal nodes only.** Gateways join differently (configure layers → bring up both networks; likely a dedicated `join_as_gateway`) — parked (§5).

## 1. The verbs

### `join <freq_MHz> <bw_kHz> <ctrl_sf> <level_id>`
e.g. `join 868.0 250 8 2`
- Set the radio floor **live**: `freq_mhz`, `radio_bw_hz` (kHz→Hz as `cfg set bw` does), `routing_sf = ctrl_sf`, `leaf_id = level_id & 0x0F` (store the full `level_id` for display — §3). CR stays 5.
- `node_id → 0` (drop any old id). Persist to NV.
- Re-tune the radio live (`reconfig`) + (re-)trigger DAD (`CmdKind::join`) live — **no reboot**.
- The node then auto-DADs an id (17..254) and **auto-pulls the leaf config** when it hears the managed beacon (existing R6.2 flow). Nothing else to do.
- Reject bad args (freq/bw/SF ranges; `level_id` 1..255).

### `create <freq_MHz> <bw_kHz> <ctrl_sf> <level_id> <sf_list> <duty%> "<leaf name>"`
e.g. `create 868.0 250 8 2 7,9 10 "north field"`
- Everything `join` does, **plus** the leaf's distributed config:
  - `allowed_sf_bitmap = parse_sf_list(sf_list)`  (data SFs, e.g. `7,9`)
  - `duty_cycle = duty% / 100`  (the arg is a **percent**; stored as the 0..1 fraction)
  - `leaf_name = <quoted text>`  (the **last** arg is `"…"`-quoted to allow spaces)
- **Mint a managed lineage** (random, never 0) + `config_epoch = 1` (the `leaf create` body) → become the leaf mother.
- `node_id → 0` (DADs an id like any node; pin with `cfg set node_id` if wanted).

### `leave`
- **Wipe to default, keep only `freq`:**
  - `lineage_id = 0`, `config_epoch = 0`, `_max_seen_epoch = 0`
  - `node_id = 0`, `joined = 0`
  - `allowed_sf_bitmap`, `duty_cycle`, `leaf_name` → default/empty
  - `routing_sf`, `radio_bw_hz`, `leaf_id`, `level_id` → default (unset)
  - `freq_mhz` → **KEEP**
- Persist. Apply **live** (the node stops being a member → unprovisioned, idle awaiting a `join`). No reboot.
- This is the piece that makes **managed → different-managed re-join** clean: `leave` then `join <B…>`.

## 2. Live-apply (no reboot) — the three sub-paths
- **Radio** (freq/bw/ctrl_sf): reuse the existing live re-tune (`fw_main.cpp:248`).
- **Membership/id**: set `node_id = 0` live + invoke DAD (`CmdKind::join`) live → re-DAD without reboot.
- **Config** (sf_list/duty/leaf_name): live via `mutable_config()` (the MAC re-reads per use). **Recompute `_duty_cycle_budget_ms` here** so the duty enforcement budget applies live too — this also closes the known duty-live gap (recommend; small).

## 3. `level_id` ↔ `leaf_id`
- User enters `level_id` (1..255). Firmware derives `leaf_id = level_id & 0x0F` (the 4-bit wire filter) and uses **leaf_id on the wire**. Store the **full `level_id`** in NV (reuse `b.layer0_id` or add a field) and show it in `status` next to the derived nibble.
- ⚠ Two `level_id`s with the same low nibble collide on the wire (only 16 distinct leaves) — the existing leaf-nibble constraint. `status` showing `level_id (→ leaf nibble N)` lets the operator catch a clash.

## 4. Keep the low-level path
`cfg set …` and `leaf create` / `leaf name` stay (fine-tuning, scripts, gateways). The verbs are the simplified front-door. `leaf create` is superseded by `create` — **keep it as a back-compat alias** (recommend), or drop (decision).

## 5. Gateways — OUT OF SCOPE (future)
Gateways are multi-layer (`l0_*`/`l1_*`, 1..16 ids) and bring up **both** networks. Likely shape: configure the two layers, then a dedicated **`join_as_gateway`** (or `gateway join`) that joins both. Park the design — these 3 verbs are normal-node only.

## 6. Mobile nodes — DEFERRED (noted)
A roaming node ≈ automated `leave` + `join` (hears a different managed leaf → leaves, joins). The `leave`/`join` semantics locked here are the primitives; the roam policy (keep id across hops? re-pull config each hop? hysteresis so it doesn't flap between two leaves?) is the later discussion the user flagged.

## 7. Micro-decisions — ALL CONFIRMED YES (author 2026-06-21)
- **(a)** ✅ Store the full `level_id` for display (§3).
- **(b)** ✅ Recompute `_duty_cycle_budget_ms` on live apply (§2) — closes the duty-live gap as part of this work.
- **(c)** ✅ Keep `leaf create` as an alias of `create` (§4).

## 7b. Companion JSON surface — wire the iOS membership UI (folded from `ios-companion/INBOX_SYNC_CONTRACT.md`, 2026-06-21)

The R6 membership concepts are done but **not yet exposed to the BLE companion** (`lib/console/console_json.cpp`). Three small additions — same area as the verbs, so do them together:

1. **`config_adopted` push writer** — `PushKind::config_adopted` exists but has **no `console_json` writer** (Serial-only today). Add it to `pushkind_name` (`return "config_adopted";`) + a writer that reads `g_node.config()`:
   ```json
   {"ev":"config_adopted","lineage":<u16>,"epoch":<u16>,"leaf":"<name>","level":<id>}
   ```
2. **`joining` send-fail reason** — `SendFailReason::joining` is in `command.h` but **missing from `console_json`'s `sendfailreason_name`** (so the participation-gate fail currently serializes wrong). Add `case SendFailReason::joining: return "joining";`.
3. **`ready` snapshot leaf fields** — add to the `ready` writer:
   ```json
   "lineage":<u16>,"epoch":<u16>,"leaf":"<name>","level":<id>,"synced":<bool>
   ```
   `synced = (lineage==0 || epoch>0)`. `level` = the stored full `level_id` (§3); until that field exists, emit the wire leaf nibble (`leaf_id`).

After implementing, flip the ⚙️ marks in `INBOX_SYNC_CONTRACT.md` to "done".

## 8. Native tests + gate
- **Units:** `join` parses + sets floor live + `leaf_id == level_id & 0x0F` + `node_id == 0`; `create` additionally sets sf_list/duty(%→fraction)/name + mints a lineage (epoch 1); `leave` zeroes membership + config but **keeps freq**; quoted-name parse (with spaces); bad-arg rejection.
- **No-reboot:** assert the radio reconfig + DAD fire on the verb (no reboot path).
- **Companion JSON (§7b):** `config_adopted` serializes `lineage/epoch/leaf/level`; `send_failed{joining}` → `"reason":"joining"`; `ready` carries the leaf fields + correct `synced`.
- Full native suite green; 1 normal board builds.
- **Docs:** update `docs/LEAF_PROVISIONING.md` (verbs) AND flip the ⚙️ marks in `ios-companion/INBOX_SYNC_CONTRACT.md` to done.

## 9. Build order (coder)
1. **Console dispatch:** add `join` / `create` / `leave` (top-level, next to `leaf`/`cfg`). Positional parse; `create`'s last arg is `"…"`-quoted (allow spaces); reject bad args with a `> … err` line.
2. **`join`:** set NV `freq_mhz` / `radio_bw_hz` (kHz→Hz, as `cfg set bw`) / `routing_sf=ctrl_sf`; `leaf_id = level_id & 0x0F`; store full `level_id` (reuse `b.layer0_id`); `node_id=0`; persist → existing **live radio re-tune** (`fw_main.cpp:248`) + invoke DAD (`CmdKind::join`). No reboot.
3. **`create`:** `join`'s actions + `allowed_sf_bitmap = parse_sf_list`, `duty_cycle = duty%/100`, `leaf_name`; then the `leaf create` body (mint lineage, `config_epoch=1`).
4. **`leave`:** NV wipe-to-default **keep `freq_mhz`** (zero lineage/epoch/`_max_seen_epoch`/node_id/joined; default sf_list/duty/leaf_name/routing_sf/bw/leaf_id/level_id); persist; live-clear membership (`node_id=0`, stop beaconing the leaf).
5. **Duty budget (decision b):** recompute `_duty_cycle_budget_ms` in the live-apply path (also covers adopt + `cfg set duty`).
6. **`status`** shows `level_id (→ leaf nibble N)`; keep `leaf create` as a `create` alias (decision c).
7. **Companion JSON (§7b):** add the `config_adopted` writer + the `joining` reason + the `ready` leaf fields to `lib/console/console_json.cpp`; flip the ⚙️ marks in `ios-companion/INBOX_SYNC_CONTRACT.md` to done.
8. **Native tests (§8)** + update `docs/LEAF_PROVISIONING.md`.
9. Hand back **green-shaped + uncommitted** → the quality-gate runs §8 (native + 1 board + the no-reboot assertion + the companion-JSON units) before the author commits.
