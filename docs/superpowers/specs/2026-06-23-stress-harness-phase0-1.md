<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Stress harness — Phase 0 (provision) + Phase 1 (plumbing) — coder instruction

**Status:** coder instruction. Implements Phases 0–1 of `2026-06-23-stress-test-harness-design.md`. Python `tools/` only — **no firmware change**. The user commits; I gate by code-review + a bench-run on the Pi (no native/board/suite gate).

Phase 0 and Phase 1 share the foundation (discovery + the concurrent manager), so build them together. Deliverables:
- `meshroute_lab.py status` — one-shot table of every connected node.
- `meshroute_lab.py provision <netspec>` — `create`/`join` the nodes into one leaf, **verify convergence**, emit the topology map.

Runs **on the Pi (192.168.1.152)**, SSH-launched. Python 3 + `pyserial`.

## Reuse — do NOT rewrite serial

`tools/meshroute_client.py` already has it: `MeshRouteClient(port, baud=115200)` with `open()`/`close()`/context-manager, a reader thread (`_read_loop` → `_rxq` queue of `(ts, line)`), an `_on_line` callback hook (set ⇒ live streaming; unset ⇒ queue for `request()`), `dsrdtr=False`. Also `parse_line`, `_kv`, the port helpers, and the `selftest()` pattern. Reuse all of it; the lab layer is multi-node orchestration on top.

## Module layout

```
tools/meshroute_lab.py        # thin CLI: status | provision
tools/lab/__init__.py
tools/lab/registry.py         # discovery: ports -> [NodeInfo]
tools/lab/manager.py          # NodeManager: request-to-one / broadcast / live-callbacks over N clients
tools/lab/provision.py        # create/join + verify convergence + topology map
tools/lab/parsers.py          # line parsers ([whoami]/[cfg]/[status]/[duty]) -> dicts  (unit-tested offline)
```
(`workload.py`/`reconcile.py`/`duty.py`/`report.py` are later phases — leave hooks, don't build.)

## Console contracts (VERIFIED — parse exactly these)

| cmd | response | parse |
|---|---|---|
| `whoami` | `[whoami] id=<N> hash=0x<HEX> [name="…"] leaf=<L> gw=<0\|1> gwonly=<0\|1> mobile=<0\|1>` (+ `[whoami.layerK]` lines if gateway) | id, hash, leaf, gw, mobile |
| `cfg` | `[cfg] node_id=<N>` then `  radio : … sf_list=<a,b> bw=… …` then `  leaf  : leaf_id=<L> level_id=<LV> (→nibble n) gateway=… …` (multi-line) | node_id, sf_list, leaf_id, level_id, freq, duty |
| `status` | `[status] uptime_ms=… rx=… tx=… txq=… routes=<R> pending=<0\|1> …` (one line) | routes, rx/tx, uptime |
| `duty` | `[duty] <pct>%` [` — SILENT, ~<N> s to availability`] · or `[duty] disabled (no duty limit)` | pct, silent, avail_s |
| `create …` | `> create …` (accept, echoes params) · `> create err …` (reject) | accept/reject + reason |
| `join …` | `> join freq=… bw=… …` (accept) · `> join err …` · `JOIN REFUSED …` (wire-version / leaf-full) | accept/reject + reason |

Identify a node by **`whoami` id/hash, never by ACM number** (hub enumeration is unstable across replug/reboot). `node_id=0` is valid = unprovisioned (will be provisioned).

`request()` line collection: collect until **~250 ms quiet** or `timeout`, EXCEPT where a command has a known terminator (none here need it; `pull_inbox`'s `inbox_end` matters in a later phase). A `cfg` is multi-line — collect the whole quiet burst.

## Phase 1 — discovery + manager + `status`

**`lab/registry.py` — `discover(ports=None) -> [NodeInfo]`:**
- Ports: `glob('/dev/ttyACM*')` (or the `--ports` override). Open each (`MeshRouteClient`).
- `request("whoami", timeout=2.0)` → parse `[whoami]`. Build `NodeInfo{port, client, node_id, hash, leaf, gw, mobile, name, responsive}`.
- A port that errors on open or doesn't answer `whoami` in time → `responsive=False`, logged, kept in the list (so `status` shows it as DEAD).

**`lab/manager.py` — `NodeManager`** wrapping the registry's clients:
- `request(node, cmd, timeout=2.0)` — write `cmd\n` under that node's **writer-lock** (one command in flight per node — the console is line-oriented), collect the response burst, return the lines.
- `broadcast(cmd, timeout)` — `request` to every responsive node **concurrently** (thread pool), return `{node_id: lines}`.
- `on_live(node, cb)` / `live_all(cb)` — set `_on_line` so the reader streams lines to `cb(node, ts, line)` (for RECV/CH/ACKED in later phases); used when NOT mid-`request`.
- Clean open/close of all clients (context manager).

**`meshroute_lab.py status`:** discover → `broadcast` `cfg`, `status`, `duty` → print a table, one row per node:
```
port            id   hash       leaf level_id sf_list  routes duty   uptime_s  state
/dev/ttyACM0    254  0x1a2b3c4d 1    1        7,9      4      42%    1366      ok
/dev/ttyACM3    -    -          -    -        -        -      -      -         DEAD(no whoami)
```
`--json` emits the same as a JSON array. This is the smoke test for 10-port concurrency + every parser.

## Phase 0 — provision

**`netspec`** (YAML file or CLI flags) — the *logical* single-leaf network:
```yaml
freq_mhz: 869.0
bw_khz: 125
ctrl_sf: 9
level_id: 1
sf_list: "7,9"
duty_pct: 10
leaf_name: "bench"
mother: first        # a node_id, or "first" (first discovered)
```

**`lab/provision.py` — `provision(manager, netspec, timeout=60) -> TopologyMap`:**
1. Discover; pick the **mother** (`netspec.mother` id, else first responsive node).
2. **Mother:** `request(mother, 'create <freq> <bw_khz> <ctrl_sf> <level_id> <sf_list> <duty_pct> "<leaf_name>"')`. Reject (`> create err …`) → **fail loud**.
3. **Joiners (all other responsive nodes), concurrent:** `request(node, 'join <freq> <bw_khz> <ctrl_sf> <level_id>')`. A `> join err …` or `JOIN REFUSED …` → **fail loud** with the reason (wire-version mismatch / leaf-full).
4. **Verify convergence** — poll every node (interval ~3 s, until `timeout`) via `cfg` (+ `whoami`). A node is **CONVERGED** when all hold:
   - `node_id != 0` (DAD assigned an id),
   - `level_id == netspec.level_id`,
   - `sf_list == netspec.sf_list` (the **joiner pulled the managed config** — the mother has it from `create`; this is the lineage-adoption proxy).
   Also assert **node_ids are UNIQUE** across the fleet (DAD-collision guard). All converged → success; any node not converged by `timeout`, or a duplicate id → **fail loud** (print the offending node + its last `cfg`/`status`).
5. Emit **`provision.json`** (and a printed table): `[{port, node_id, hash, leaf_id, level_id, sf_list, is_mother}]` — the topology registry later phases load instead of re-discovering.

**CLI:** `meshroute_lab.py provision <netspec.yaml> [--mother <id>] [--timeout 60] [--ports …]`.

## Firmware gap (note for the user — do NOT build here)

Convergence is checked via `id + level_id + sf_list` because **`lineage_id`/`config_epoch` are not console-exposed** (verified: `status`/`cfg`/`whoami` don't print them, contrary to `LEAF_PROVISIONING.md`). `sf_list`-pulled is a sound proxy. A *strict* lineage check (did the joiner adopt **this mother's** lineage vs another managed leaf sharing the `level_id` nibble?) would want `lineage=<id> epoch=<n>` added to `whoami` — a ~2-line firmware add. **Optional**, flagged for the user; the harness works without it.

## Gate (how I'll verify)

- **Code review** vs this spec (module boundaries, the contracts, fail-loud paths).
- **Offline unit** (`tools/lab/parsers.py` + a pytest or the `selftest()` pattern): feed captured sample `[whoami]`/`[cfg]`/`[status]`/`[duty]` lines → assert the parsed dicts. No device.
- **Bench-run (user, on the Pi):** `status` lists the 4 nodes with correct id/leaf/sf_list/duty (+ a DEAD row if a port is unplugged); `provision <netspec>` converges them to one leaf and writes a sane `provision.json`; a deliberately-wrong netspec (bad `ctrl_sf`, or a `level_id` no node can reach) **fails loud**, not hangs.
- Leave GREEN + uncommitted.

## Operational

- Per-node **writer-lock**: never two commands in flight to one node; wait for the response burst (or timeout) before the next.
- Concurrency: 10 nodes → 10 reader threads + a small worker pool for `broadcast`. Fine for pyserial.
- **Never** `debug on` by default (verbose trace).
- `--ports` to target a subset; auto-discover otherwise. A timeout on every `request` (dead-node guard — a hung node must not stall the fleet).
