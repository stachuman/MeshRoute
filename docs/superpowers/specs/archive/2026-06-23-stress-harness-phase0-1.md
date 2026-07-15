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

## Amendment 1 (2026-06-23) — `leave`-first reset (REQUIRED, bench-proven)

**Why:** the first bench-run proved that re-provisioning *on top of a standing managed leaf* split-brains — `create`/`join` to a new `level_id` were accepted, 2/4 nodes briefly converged, then the still-alive old leaf pulled everything back (final `status`: all reverted to the old `level_id`, the mother left half-set). The design doc's "`leave` reset preamble" is therefore **not optional** — provision must wipe every node to a clean slate *before* building the new leaf, so the old lineage/leaf dies everywhere at once.

**Change `lab/provision.py` — `provision(manager, netspec, timeout=90, reset=True, reset_settle_s=3.0, …)`:** when `reset`, BEFORE the `create`/`join` steps:
1. **`broadcast("leave")` to ALL responsive nodes** (concurrent). ⚠ **Do NOT reuse `_accepted(lines, "leave")`** — the success line is `> left network (kept freq=…) — idle`, which does *not* start with `> leave`. Use a leave-specific check: accept = a line containing `> left network`; reject = a line containing `> leave err`. A reject (e.g. `nv_save_failed`) → `ProvisionError`.
2. **Settle** `reset_settle_s` (~3 s) — let the live-apply land and the old leaf's in-flight beacons stop.
3. **Verify idle** — `broadcast("whoami")`, assert every node parses `node_id == 0` (unprovisioned/idle). A node still showing a non-zero id → `ProvisionError("not idle after leave: …")`.
Then run the existing `create` → `join` → converge flow unchanged. `leave` on an already-fresh node is harmless (returns `> left network`), so reset-on is safe universally.

**CLI (`meshroute_lab.py provision`):** add `--no-reset` (skip the `leave` preamble — for *additive* provisioning onto an already-clean network). Default = reset ON.

**Timeout:** bump the default `--timeout` 60 → **90** s (a clean slate converges faster, but give headroom). Note in `--help` that convergence speed tracks **beacon cadence** — for slow nets a provisioning-time short `beacon_ms` (the scenario/operator can `cfg set beacon_ms …`) speeds DAD + config-pull. (Don't auto-set it here; just document the knob.)

**Gate (user re-bench):** with a netspec matching the network (e.g. `level_id: 2`, `sf_list: 8,10`), `provision` now `leave`s all 4 → converges all 4 → writes `provision.json`; `--no-reset` skips the leave; a deliberately-bad netspec still fails loud. The parser selftest stays green (no parser change). Plus a tiny offline unit for the new leave-ack check (`> left network` ⇒ ok, `> leave err …` ⇒ reject).

## Amendment 2 (2026-06-23) — marker-aware response collection (REQUIRED, bench-proven)

**Why:** the 2nd bench-run (on a network churned by the earlier split-brain provisions) returned `last cfg: None` for **every** node with the converged-count oscillating 0→1→0→2→0. Root cause is in `collect_burst` (`lab/manager.py`): it stops at the **first ~250 ms quiet gap after the first line received**, but during provisioning churn (DAD + triggered beacons) unsolicited console lines arrive *before* the command's response — so the burst returns beacon noise with no `[cfg]` line and `parse_cfg` → None. The first run only worked because the net was idle. Convergence verification runs **during** the churn, so collection MUST be robust to interleaved unsolicited output.

**Change `lab/manager.py` — make collection marker-aware:**
```
collect_burst(client, cmd, expect, timeout, tail_quiet=0.2):
    client._flush(); client.send_line(cmd)
    # 1) collect until a line CONTAINS `expect` (the real response arrived) OR `timeout`
    # 2) then collect until `tail_quiet` s of silence (the rest of a multi-line reply like [cfg])
    return all_collected_lines
```
Thread `expect` through `NodeManager.request(node, cmd, expect, timeout)` and `broadcast(cmd, expect, …)`. Call-site markers: `whoami`→`[whoami]`, `cfg`→`[cfg]`, `status`→`[status]`, `duty`→`[duty]`; the verbs (`create`/`join`/`leave`) → `"> "` (every result line starts `> `; accept/reject is still parsed from the collected lines). If `expect` never arrives within `timeout`, return whatever was collected (the caller's parser yields None → a transient read-miss, retried next poll — **never** itself a convergence failure).

**Convergence loop:** a None cfg in one poll round = "unread this round," retried next `poll_s`; only the *final-timeout* state decides pass/fail (already structured this way — the marker-aware read just makes None rare, which kills the oscillation).

**Gate:** offline parser selftest stays green; add a unit feeding a burst where an unsolicited `… BCN …`/trace line **precedes** `[cfg]` → assert the marker-aware collector still returns the `[cfg]` block (the old first-quiet-gap logic drops it). Implement Amendments 1 **and** 2 together before the next bench-run.

> **Operational note for the user — recover the churned network first.** The repeated split-brain provisions left the nodes in an unstable state (the `None` reads). Before re-testing, reset them to clean idle: `leave` each node (or `factory_reset confirm` for a hard wipe) so they're all unprovisioned, then run the (amended) `provision` from a known slate.

## Amendment 3 (2026-06-23) — short provisioning beacon (config-pull speed, bench-proven)

**Why:** the 3rd bench-run (Amendments 1+2 working) reached 3/4; the one stuck node had `level_id=4` (joined) but `sf_list=-` (config NOT pulled), with `beacon_ms=900000` (**15 min**). Config-pull relies on hearing the mother's managed beacon — the triggered-beacon makes the *first* one prompt, but a node that misses it (weak link / collision) waits up to a **full beacon period (15 min)** for the next, far past the 90 s timeout. The same cadence drives the converged-count oscillation. `cfg set beacon_ms …` is a **LIVE** MAC knob (takes effect immediately, no reboot).

**★ Amplified by the ~LINEAR topology (A–B–C–D, user 2026-06-23):** config-pull **cascades hop-by-hop** — D can only pull from C *after* C pulled from B *after* B from A (the mother). The far node converges LAST, bounded by **beacon_period × chain-depth**: at 15 min × 3 hops that's up to ~45 min, so the 3/4 was simply the near three cascading while the far node still waited. Two consequences: (1) the short beacon isn't just helpful, it's **necessary** (a slow beacon makes a deep chain un-provisionable in any reasonable window); (2) the **convergence timeout must scale with depth** — make the default `timeout = max(90, n_nodes × beacon_s × 1.5)` (a 10-node chain at a 30 s beacon needs ~4 min, not 90 s). Concurrent `join` stays correct — the cascade **self-orders** (each node pulls the instant its upstream is ready), so no staged/nearest-first joining is needed; it just needs the time + the fast beacon.

**Change:** drive a SHORT beacon on every node during provisioning so DAD + config-pull retry in seconds, not minutes. Add **`beacon_ms`** to the netspec (default **30000** = 30 s). In `provision()`, after each node is provisioned (mother right after `create`, each joiner right after `join` — `leave` resets beacon to 900000, so set it *after* leave), `broadcast('cfg set beacon_ms <beacon_ms>', '> ')`. Re-assert once if a config-pull is observed to reset it. Result: a weak-linked node gets a fresh managed beacon every ~30 s and converges within the timeout, and the oscillation disappears (nodes converge fast and stay put).

**Caveats — document, do NOT auto-decide:**
- The short beacon must FIT the duty budget: a very short beacon under a tight `duty_pct` can exhaust duty (node goes SILENT → can't beacon → can't pull). 30 s is a safe middle; don't go lower than the duty allows.
- ⚠ The current `netspec.example.yaml` sets **`duty_pct: 1`** — *very tight* for a stress test (1 % airtime throttles everything, incl. provisioning beacons). Surface it to the user; **10 %** (the deployment default) gives the harness room. The harness shouldn't override it — just flag a `duty_pct < ~5` in `provision` as a warning line.

**Gate:** with `beacon_ms: 30000` (or a manual `cfg set beacon_ms 30000` on all nodes first), `provision` converges **all 4** — including the weak-linked node — within the timeout, with no oscillation; `provision.json` written. The offline selftest stays green (no parser change).

> **Immediate test for the user (no code needed):** on each node run `cfg set beacon_ms 30000`, then re-run `provision` — the stuck node should pull its `sf_list` within a minute. (Or even just wait: ACM2 will pull on its next 15-min periodic beacon.) Also consider raising `duty_pct` to 10 in the netspec.
