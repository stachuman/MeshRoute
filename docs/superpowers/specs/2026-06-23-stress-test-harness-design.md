<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# MeshRoute hardware stress-test harness — design

**Status:** design (brainstormed + decisions locked 2026-06-23). Coder instruction to follow this doc; the user commits; I quality-gate. Builds on `tools/meshroute_client.py` (reuse its serial core).

## Goal

Replace the manual "send a message, walk over and check the other nodes" loop with a repeatable, automated stress harness across the bench: a **Raspberry Pi (192.168.1.152)** with a USB hub holding **4 nodes now, 10 soon**. The harness generates load, **reconciles what every node actually received via its own inbox**, monitors **duty consumption**, and reports tiered pass/fail.

**Locked decisions (2026-06-23):**
1. **Success = tiered per-type** (acked-DMs ~100%, un-acked DMs/channels measured + thresholded, duty monitored-not-gated).
2. **Workloads first:** **random-realistic** + **channel-storm** (the other patterns come later).
3. **Runs on the Pi, SSH-launched** (direct `/dev/ttyACM*`).

## The oracle is already on every node

No manual checking — each node records what it received and serves it as NDJSON. Verified console contracts the harness parses:

| signal | format (fw_main.cpp) | use |
|---|---|---|
| send-ack | `queued ctr=<N> depth=<D>` / `err ctr=…` (:1424) | the sent msg's `ctr` |
| DM result | `ACKED ctr=<N>` / `FAILED ctr=<N>` (:1571/1573) | acked-DM confirmation |
| live DM push | `RECV from=<origin>: <body>` (:1562) | latency, progress |
| live channel push | `CH <ch> from=<origin>: <body>` | latency, progress |
| inbox dump | `pull_inbox <dm_since> <chan_since>` → `inbox_dm`/`inbox_channel`/`inbox_end` NDJSON (:981) | **authoritative receive-ledger** |
| duty | `duty: <pct>%` [`— SILENT, ~<N> s to availability`] / `duty: disabled` (:279) | duty time-series |
| identity | `whoami` / `hashof <id>` → `id ↔ 0x<key_hash32>` | discovery, id↔hash map |

Message identity is stable: **DM = `(sender_hash, ctr)`**, **channel = `channel_msg_id` (32-bit, now reboot-unique thanks to the ctr-epoch fix)**. The inbox carries `origin`, `ctr`/`channel_msg_id`, `body` (payload verbatim), `rx_ms`, `seq`. Baud **115200**, `dsrdtr=False` (already in the client).

## Architecture — `tools/meshroute_lab.py` (on the Pi)

Reuses `MeshRouteClient` (serial open/read/write) + `parse_line` from `meshroute_client.py`; adds the multi-node + scenario + reconcile layers.

1. **Discovery.** Enumerate `/dev/ttyACM*`, open each at 115200, send `whoami` → registry `{port → node_id, key_hash32, leaf_id}`. **Identify by `whoami`, never by ACM number** (hub enumeration is unstable across replug/reboot). Auto-scales 4→10; a port that doesn't answer `whoami` is logged + skipped (dead-node detection).
2. **Concurrent node manager.** One reader + one writer per node (threads or asyncio). The reader captures *every* serial line, host-clock-stamped, to a per-node log **and** parses live events (`RECV`/`CH`/`ACKED`/`FAILED`/`queued ctr=`). The writer serializes console commands to that node (one in flight at a time; the node console is line-oriented).
3. **Workload engine.** A scenario (YAML or CLI flags) → a timed sequence of sends. **Every message gets a unique tagged payload** `T<run>S<src>#<n>` (≤ the channel/DM payload cap). On each send the engine records a **send-ledger** row `{tag, src, type, dst|chan, flags(-a/-e), host_send_ts, ctr}` (ctr from the send-ack).
4. **Settle + collect.** After the send phase, wait the **settle window** (below), then `pull_inbox 0 0` every node → parse → **receive-ledger** per node.
5. **Reconcile.** Match send-ledger ↔ receive-ledgers **by payload tag** (id fields cross-check). Per message: delivered to each expected recipient? duplicates? latency (host_send_ts → first live push). Apply the tiered criteria.
6. **Duty + health monitor.** A background task polls `duty` + `status` (+ optionally `routes`) every K s on every node → duty% / SILENT / route-count time-series.
7. **Report.** A run directory with machine-readable artifacts + a human summary.

## Network provisioning (Phase 0 — run once per flash / topology change)

A freshly-flashed **or v15-ctr-epoch-reset** node boots IDLE (`level_id=0`, unprovisioned — silent on the air). Provisioning is **LIVE serial (no reboot)**: one node `create`s the managed leaf (mother), the rest `join`. The harness automates **and verifies** this so every stress run starts from a known, converged topology — and because the reflash cadence means you re-provision constantly, this replaces a 10-node manual `create` + 9×`join` chore with one command.

```
meshroute_lab.py provision <netspec.yaml>
```

**`netspec`** declares the *logical* network: `freq_mhz`, `bw_khz`, `ctrl_sf`, `level_id`, `sf_list`, `duty_pct`, `leaf_name`, and which discovered node is the **mother** (`node_id`, or `"first"`). Single managed leaf for now.

**Steps (reuse the Phase-1 concurrent manager):**
1. Discover (`whoami`).
2. `create <freq> <bw> <ctrl_sf> <level_id> <sf_list> <duty%> "<name>"` → the **mother**.
3. `join <freq> <bw> <ctrl_sf> <level_id>` → every other node (concurrent).
4. **Verify convergence** — poll `status`/`whoami` (timeout) until *every* node shows the mother's **lineage_id + config_epoch**, a **unique DAD node_id** (no 17..254 collision), and the pulled **`sf_list`/duty**. A node that doesn't converge in the window → **fail loud** (which node + its last `status`).
5. Emit the **topology map** `provision.json` (port → node_id → key_hash32 → leaf/lineage/epoch) — the registry every later phase keys off.

**Why verified, not fire-and-forget:** a `send` before sync returns `send_failed{reason:joining}`, so the stress phase needs convergence anyway — verifying here turns a flaky precondition into a hard gate, and incidentally exercises **DAD / join / config-pull** (a duplicate id or a missed `sf_list` surfaces here, not as mystery losses mid-stress).

**Scope:** *now* — single leaf, live `create`/`join`, convergence-verified. *Manual (harness never touches)* — physical layout: `tx_power`, antenna, USB-port↔node placement (the source of the asymmetric links). *Later* — gateway/multi-layer provisioning (`gateway l0=…:l1=…` + window schedules) and a `leave`→re-`join` "reset to clean" preamble. A scenario may embed its `netspec` for a fully self-contained run (provision → verify → stress → reconcile); otherwise provision once, run many.

## Workloads (build these two first)

**A. Random-realistic** — the steady-load baseline.
- Poisson-timed sends at a configured aggregate rate (msgs/s) for a duration. Each send: random `src` (uniform over the node set), type drawn from a weight (e.g. 70% DM / 30% channel), DM `dst` = a random *other* node, `-a`/`-e` per config probabilities, channel = a configured channel id.
- Tunables: rate, duration, type weights, ack/enc probabilities, node subset.

**B. Channel-storm** — flood + repair + duty stress (this is where the asymmetric-leaf + duty pressure live).
- M origins each fire a burst of channel messages (burst size, inter-message gap, inter-burst gap) on one or more channels, optionally overlapping in time.
- The harness expects every other node on the leaf to receive every channel message **eventually** (flood + repair); it reports the **flood-immediate vs repair-caught split** and which nodes needed repair (this directly surfaces the known asymmetric-RX-leaf behaviour on metal).

> **Settle window + repair speed.** Channel repair is digest-driven, and a *managed flood* already calls `schedule_triggered_beacon()` on originate + first-receipt, so the digest is prompt (seconds), not the 15-min `beacon_ms`. Still, size the channel settle window generously (default ~60–120 s) and let the scenario optionally set a **shorter `beacon_ms` via `cfg set` for test runs** to converge repair faster. Record both the immediate (settle-start) and eventual (settle-end) reach.

## Reconciliation + tiered success criteria

Match by the payload tag; the stable id fields (`ctr`, `channel_msg_id`, `sender_hash`) are the cross-check.

- **Acked DMs (`-a`):** the **`ACKED ctr=N`** from the sender is the authoritative confirmation. Criterion: **100% acked** — a `FAILED` or a never-acked `-a` DM is a **FAIL** (listed). (The receiver's inbox is the corroboration, not the gate — the ACK is the contract.)
- **Un-acked DMs:** success = the tagged body appears in the **dst's** inbox by settle-end. Report delivery rate vs a configurable threshold (default surface-all, no hard gate) + the loss-list.
- **Channels:** expected receivers = all other nodes on the leaf. Report **(1) flood-immediate reach** (received by settle-start, i.e. before repair) and **(2) eventual reach** (by settle-end). Criterion: **eventual = 100%** (every in-range leaf node gets every channel message; a miss at settle-end is a real loss → FAIL + listed). Flood-immediate-% is the *quality* metric (how well the flood did before repair had to save it).
- **Duty:** **monitored, not gated.** Report peak duty% per node, total time in SILENT, and **correlate any losses with a node being SILENT at send/receive time** (a loss explained by duty-exhaustion is flagged distinctly from a routing loss).
- **Liveness:** no node stops responding (the manager flags a port that goes silent to `status`/`whoami` mid-run).

All thresholds live in the scenario file so a run can be made stricter/looser without code changes.

## Reporting (run directory `runs/<ts>-<scenario>/`)

- `send_ledger.jsonl` — every sent message (tag, src, type, dst/chan, flags, ctr, host_send_ts).
- `inbox_<node>.ndjson` — raw `pull_inbox` per node.
- `events_<node>.log` — full host-stamped serial capture per node.
- `duty.csv` — node, ts, pct, silent, avail_ms.
- `reconcile.json` — per-message verdicts + the aggregates.
- `summary.txt` — human table: per-type delivery + latency p50/p95, duty peaks, and the **FAIL list** (undelivered: tag, src, expected-dst, type, last-known duty of the missing node).

## Reuse vs new code

- **Reuse:** `MeshRouteClient` (serial), `parse_line`, `_kv` (kv parsing), the port helpers.
- **New modules:** `lab/registry.py` (discovery), `lab/manager.py` (concurrent reader/writer + live-event parse), `lab/provision.py` (create/join + verify convergence + topology map), `lab/inbox.py` (NDJSON inbox parser), `lab/workload.py` (A + B generators + send-ledger), `lab/reconcile.py` (tag-match + criteria), `lab/duty.py` (poller), `lab/report.py`. Keep `meshroute_lab.py` a thin CLI over them.

## Build phases (each runnable + useful alone)

0. **Provision** — `meshroute_lab.py provision <netspec>`: `create`/`join` the discovered nodes into one leaf + verify convergence (lineage/epoch/unique-id/`sf_list`) + emit `provision.json`. Run once per flash or topology change; every later phase keys off its registry. (Can be built alongside Phase 1 — both are just the manager + discovery.)
1. **Plumbing** — discovery + concurrent manager + `meshroute_lab.py status` (one-shot table of every node's whoami/status/duty). Proves 10-port concurrency + the contracts.
2. **Oracle loop** — tagged-send + send-ledger + live-push capture + `pull_inbox` parse + reconcile, on a trivial sequential workload (round-robin one DM + one channel). Proves the end-to-end send→reconcile.
3. **Workloads** — A (random-realistic) + B (channel-storm) on top of the oracle loop.
4. **Duty + report** — the concurrent duty/health poller + the full `summary.txt`/`reconcile.json`.

## Operational notes

- Launch on the Pi: `python3 tools/meshroute_lab.py run scenarios/<x>.yaml` (or `status`). Results land in `runs/<ts>/` — scp back or I read them.
- **Per-node command serialization:** the console is line-oriented; the writer must wait for the send-ack (`queued ctr=` / `err`) before the next command to that node, with a timeout (dead-node guard).
- **Clocks:** node `rx_ms` is per-node uptime (not cross-comparable). Use **host-observed** timestamps (send time, first-live-push time) for all latency; `rx_ms` is only for per-node ordering/dedup.
- **No interference:** the harness must not enable `debug on` by default (the flood trace is verbose); make it an opt-in scenario flag for deep dives.
- **Reset between runs:** optional `factory_reset confirm` or `leave`/re-`join` per node for a clean-slate run; default is to leave nodes provisioned and just clear inbox cursors by recording the pre-run `inbox_end` seqs as the baseline (reconcile only messages with `seq` past the baseline).

## Out of scope (later)

- The other workload patterns (simultaneous-burst, all-to-one sink) — drop-in additions to `workload.py`.
- A Pi-side daemon + remote-control API (nicer repeat-run UX) — deferred; SSH-launch is enough now.
- Cross-node wall-clock sync (chrony on the nodes is N/A — they have no RTC); host-observed latency is the model.
