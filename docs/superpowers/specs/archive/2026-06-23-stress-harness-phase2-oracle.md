<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Stress harness — Phase 2: the oracle loop — coder instruction

**Status:** coder instruction. Implements Phase 2 of `2026-06-23-stress-test-harness-design.md` on top of the green Phase 0+1 (discovery, `NodeManager`, parsers, provision). Python `tools/` only. User commits; I gate by review + offline units + a bench-run.

**Scope:** build the **full oracle machinery** (tagged send → live capture → `pull_inbox` → reconcile-by-tag → tiered verdict → report) and prove it end-to-end on a **trivial deterministic workload** (`oracle`: round-robin one DM + one channel per node). Phase 3 reuses this machinery and only adds richer workload generators (random, channel-storm). **Do not build those here.**

## Verified console contracts (the oracle keys on these)

| signal | exact line | parse |
|---|---|---|
| send-ack | `> queued ctr=<N> depth=<D> [dh=0x… ] [lp=0x… ]` (any send kind, fw:1424) — key on `queued ctr=` / `err ctr=` | the sent msg's `ctr` |
| DM delivered (live) | `RECV from=<origin>: <body>` (fw:1562) | origin + body (the tag) |
| channel delivered (live) | `CH <channel_id> from=<origin>: <body>` (fw:1567) | channel, origin, body |
| DM acked (live, **async**) | `ACKED ctr=<N>` / `FAILED ctr=<N>` (fw:1571/1573) | the acked-DM verdict |
| durable receive-ledger | `pull_inbox 0 0` → `inbox_dm`/`inbox_channel`/`inbox_end` NDJSON | origin, `ctr`/`channel_msg_id`, `sender_hash`, `body`, `seq` |

The `<body>` is the payload **verbatim** (`Serial.write(body,len)`), so the reconciler matches by the tag embedded in it.

## Concurrency model — THE crux (live during the active window)

`ACKED`/`FAILED` (and `RECV`/`CH`) arrive **asynchronously**, not in a send's response burst. And `NodeManager.request()` refuses to run while a client is in live mode. So the active phase runs in **live mode for every node**, with `request()` used only for the (quiet) final `pull_inbox`:

1. **`manager.live_all(cb)`** — the reader streams every line to `cb(node, host_ts, line)`. The cb feeds two per-node sinks: an **ack-buffer** (lines matching `queued ctr=` / `ACKED ctr=` / `FAILED ctr=`) and a **delivery-log** (`RECV`/`CH` with host_ts).
2. **Add `NodeManager.send(node, line)`** — write-only under the node's writer-lock (`node.client.send_line(line)`), for use *in live mode* (no collect). One command in flight per node still holds (the lock).
3. **Issue a send:** under the src's lock, `manager.send(src, "send …"/"send_channel …")`, then wait (short timeout) for the **next `queued ctr=` from src** in its ack-buffer → that's this send's `ctr`. Record the send-ledger row.
4. After all sends + the settle window: **`manager.stop_live()`**, then `pull_inbox` each node via `request(node, "pull_inbox 0 0", "inbox_end", …)` (marker = `inbox_end`).

## Modules (new)

```
tools/lab/inbox.py       # pull_inbox(manager,node) -> {dm:[…], chan:[…]}; json.loads each NDJSON line until inbox_end
tools/lab/tag.py         # encode/parse the payload tag  T<run>S<src>#<n>   (run = host-side run id; nodes have no clock)
tools/lab/workload.py    # build_actions(scenario, nodes) -> [SendAction{src,kind,dst|chan,tag,ack,enc}]; Phase 2 = `oracle` only
tools/lab/oracle.py      # run(): live_all -> issue actions + capture acks/deliveries -> settle -> pull_inbox -> reconcile
tools/lab/reconcile.py   # match send-ledger <-> receive-ledgers BY TAG; per-msg verdict + tiered aggregates
tools/lab/report.py      # summary.txt (per-type delivery, latency p50/p95, FAIL list) + the run-dir artifacts
```
CLI: add `meshroute_lab.py run <scenario>` (loads a flat scenario, dispatches on `workload`). Phase 2 supports `workload: oracle`.

## Scenario file (flat `key: value`, the netspec loader — per the user's "simple input files")

```
# scenario-oracle.txt
workload: oracle
netspec: netspec.example.yaml   # optional: provision+verify first; omit to run against the standing net
dm_per_node: 1                  # each node sends this many DMs (to random OTHER nodes)
chan_per_node: 1                # …and this many channel messages
channel: 0
ack: true                       # -a on the DMs (drives the acked-DM criterion)
enc: false
settle_s: 30                    # window after the last send to catch async ACKs + flood/repair
```
Reuse `load_netspec`'s flat parser (extend the `_NUM` coercion map). If `netspec:` is present, run `provision` first (Phase 0) and key off its topology; else `discover()` the standing net.

## Reconcile — by tag, tiered criteria

Tag every payload `T<run>S<src>#<n>`; the id fields (`ctr`, `channel_msg_id`, `sender_hash`) are the cross-check. For each send-ledger row:
- **DM:** expected receiver = `dst`. **Delivered** = the tag appears in `dst`'s inbox (`inbox_dm.body`). **Acked** (if `ack`) = the sender's ack-buffer saw `ACKED ctr=<ctr>` (a `FAILED` or none = not-acked). Latency = `host_send_ts` → first `RECV` with the tag.
- **Channel:** expected receivers = **all other discovered nodes** (the leaf). Report **flood-immediate** (tag seen via a live `CH` push before settle-end) vs **eventual** (tag in the inbox after settle). Per-node delivered/missed.

**Tiered verdict (the locked criteria):**
- **acked DMs → must be 100 % ACKED** (a `FAILED`/never-acked `-a` DM = FAIL, listed).
- **un-acked DMs →** delivered-to-dst-inbox rate (reported; threshold from scenario, default surface-all).
- **channels → eventual = 100 %** across the leaf (a settle-end miss = FAIL, listed); flood-immediate-% reported as the quality metric.
- **no node hangs** (the manager flags a src that never returns a `queued ctr=`).

## Run directory `runs/<run-id>/`

`send_ledger.jsonl` (tag, src, kind, dst/chan, flags, ctr, host_send_ts) · `events_<node>.log` (host-stamped live capture) · `inbox_<node>.ndjson` (raw pull) · `reconcile.json` (per-msg verdicts + aggregates) · `summary.txt` (per-type delivery + latency p50/p95, duty-at-miss if available, the FAIL list).

> **Run isolation:** the per-run `T<run>…` tag already prevents cross-matching prior messages; additionally record each node's pre-run `inbox_end` seqs and reconcile only `seq` past them (belt-and-suspenders against an id/tag collision across runs).

## Gate

- **Offline units** (no device): `inbox.py` (parse a captured NDJSON burst incl. an unsolicited line + `inbox_end`); `tag.py` (encode→parse round-trip); `reconcile.py` (a synthetic send-ledger + receive-ledgers → assert the verdicts: a delivered+acked DM, a lost DM, a channel reaching 3/4 → one FAIL). Parser/provision selftests stay green.
- **Bench-run (user):** `run scenario-oracle.txt` on the provisioned net → each node sends 1 DM + 1 channel → `summary.txt` shows the round-robin DMs delivered+acked and the channels reaching every node (or a precise FAIL list), `reconcile.json` sane.
- Leave GREEN + uncommitted.

## Operational

- **Live vs request discipline:** never `request()` a node while live; `stop_live()` before the `pull_inbox` phase (the manager already raises if violated — keep that guard).
- **Pacing (Phase 2):** sequential round-robin (issue one node's sends, brief gap) is fine — deterministic + easy to verify. The Poisson/burst pacing is Phase 3's `workload.py` concern.
- **`debug` stays off** (verbose). The oracle relies on `RECV`/`CH`/`ACKED` + `queued ctr=` only — all on by default.

## Amendment 1 (2026-06-23) — `--verbose` + late re-pull (true eventual)

From the first oracle bench-run: the network was slow enough (duty 1 % + 15-min beacon) that the 30 s settle under-counted the real reach — channels arrived ~95 s after the send, past the pull. Two changes make a slow run legible.

**A. `-v` / `--verbose` on `run`** — default stays the summary + FAIL list; `-v` adds a live event stream + a full per-message reconcile (it's "a little more," not a firehose — no raw serial). Host-relative timestamps:
```
[send]  123 → DM 141    ctr=1   T3a6936S123#0
[recv]  141 ← 123       +14.4s  T3a6936S123#0
[chan]  171 ← 123       +9.2s   T3a6936S123#1
[ack ]  123  ctr=1      +28.1s          [fail]  128 ctr=1
[settle] 30→0s          [pull]  141: 10 dm / 8 chan
```
…then in the reconcile, **every** message (not just fails):
```
DM  123→141 ctr=1   deliv✓  ack✗   lat=14.4s        T3a6936S123#0
CH  123     reach 2/5   ✓171 ✓170   ✗141 ✗76 ✗128   T3a6936S123#1
```
All `[…]` lines + the per-message dump are gated behind `-v`; the summary block is byte-unchanged at default.

**B. Late re-pull for "true eventual"** — the inbox is durable, so a **second `pull_inbox` at `eventual_s`** (scenario field, default 300; `0` disables) after the settle pull catches everything that arrived late. Report BOTH:
```
CHANNEL: immediate 5/30 (16.7%)  ·  eventual@300s 26/30 (86.7%)
```
- The channel criterion gates on the **eventual** column (immediate is the flood-quality metric — it quantifies the asymmetric-leaf flood gap; the repair fills the rest). A node still missing at `eventual_s` = a real loss → FAIL list.
- Set `eventual_s ≈ the beacon period` (repair is beacon-driven): ~300 s for a fast bench, longer for a 15-min-beacon net. Implementation = one extra `pull_inbox` round + a reconcile re-run against the union of both pulls.

**Scenario additions:** `eventual_s: 300` (`verbose` is the `-v` CLI flag, not a scenario key).

**Gate:** the offline reconcile unit gains an immediate-vs-eventual case (tag absent in the settle pull, present in the eventual pull → counted eventual-only, not a loss). `-v` is print-only (no logic change) — confirm it streams and that default output is unchanged.
