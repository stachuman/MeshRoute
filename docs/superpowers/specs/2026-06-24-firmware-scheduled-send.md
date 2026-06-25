<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Firmware scheduled-send — move the test workload onto the node (kill the USB-overuse death)

**Status:** coder instruction (the firmware half). The user commits + flashes + benches; I gate. The harness half (a low-USB oracle mode) is mine — see the last section. nRF52840 + ESP32 (platform-neutral console + send path). No wire change — the radio frames are ordinary DMs/channel posts.

## Why

The oracle hammers USB-CDC: a continuous live serial stream + N×M `send` lines + constant reattaches, for the whole run. That **overuse is what kills nodes mid-test** (the USB-CDC wedge — radio-alive, serial-dead). User's fix: **arm the node once, let it run the whole schedule autonomously over the radio, read the durable inbox at the end.** USB drops from *continuous* to two brief touches (arm, read). The node already has everything needed — a non-blocking `loop()` and **async TX** (`send` only enqueues; `service_tx` transmits across later iterations) — so the schedule fires *between* all the normal loop work and **the console stays live: you can connect mid-run and `status`/`faults`/`pull_inbox` while it sends.**

**Decisions (user, 2026-06-24):** explicit ms-list schedule; embed the send-ms for approximate latency.

## The commands

- **`testsend <dst> <run> [-a] [-e] -t ms1,ms2,…`** — schedule DMs to `<dst>` (id 1..254 or 8-hex hash — reuse the existing `send` dst parse). `-a`=ack(e2e), `-e`=encrypt (reuse the existing flag parse).
- **`testch <ch> <run> -t ms1,ms2,…`** — schedule channel broadcasts on `<ch>`.
- **`testclear`** — wipe the schedule + reset the seq counter + run token.
- **`teststatus`** — `run=<run> armed=N fired=K deferred=D dropped=X next=+<ms> state=running|done|idle`.

`<run>` is a short alnum token (the harness run id, ≤ ~12 chars) — it's the message label AND goes in the tag. `-t` offsets are ms from arm time (when the command is parsed). `testsend`/`testch` **APPEND** to the schedule (seq keeps counting), so a big run is built from a few lines (the console RX buffer is ~1 KB → a few dozen offsets per line). Each command's offsets are relative to its own arm instant — fine, since the harness spreads them over minutes.

## The body the node builds — `T<run>S<self>#<seq>@<sendms>`

- `T<run>S<self>#<seq>` is the **exact harness tag**: `<run>` from the command, `<self>` = this node's `node_id` (decimal), `<seq>` = a per-arming-session counter (0,1,2,…; reset by `testclear`). Byte-identical to the host's `make_tag(run, node_id, seq)` → the existing tag reconcile matches it with **no firmware-vs-host divergence**.
- `@<sendms>` = this node's `millis()` **at the instant of transmit** (stamp it when the scheduler enqueues, not when armed) → the send timestamp for latency. Decimal.

## The scheduler (non-blocking)

A fixed RAM array (NOT persisted — transient test state, and persisting would add the very NV writes the [[InternalFS]] fix is reducing):
```c
struct SchedSend { uint32_t at_ms; uint16_t target; uint8_t seq; uint8_t flags; bool fired; };
//   at_ms = millis()+offset (absolute) · target = dst id / channel id · flags = ack|enc|is_channel
static SchedSend g_sched[kSchedMax];   // kSchedMax ~256 (≈4 KB; tune); reject + report when full
static uint16_t  g_sched_n, g_sched_seq;
static char      g_sched_run[16];
```
**Loop tick** (add to `loop()`, after the console/radio service): scan for the earliest unfired entry whose `at_ms ≤ millis()` and **enqueue it via the existing `send`/`send_channel` path** with the built body. Then mark fired. Gotchas, all the coder's discretion within these rules:
- **Respect the existing TX-queue + duty/airtime gating.** If the enqueue fails because the TX queue is full or duty is over-budget, **leave the entry unfired and retry next loop** (it slips later — correct: the test should respect duty like real traffic). Count these as `deferred`.
- **Bound the slip:** if an entry is overdue by more than a slack window (e.g. > a few seconds) and still can't enqueue, **drop it + count `dropped`** (don't retry forever, don't let a backlog snowball). `teststatus` surfaces `deferred`/`dropped` so a run that outran the radio is visible, never silent.
- **Cheap scan:** O(n) over ≤256 entries per loop is fine; optionally cache `next_due_ms` to skip the scan until something's due.
- A reboot mid-run loses the schedule (RAM) — acceptable (the partial inbox still tells the story; a crashed node is its own finding).

## What it deliberately does NOT do
No NV persistence (transient). No live push of RECV/ACK during the run (that's the USB stream we're eliminating — the durable inbox is the truth). No own rate-limiting beyond the TX-queue/duty gate (the host already Poisson-spreads the offsets).

## The harness half (MINE — follow-on, not the coder's)
A new low-USB oracle mode in `tools/lab/`:
1. **Arm** — per node, compute the schedule (the realistic Poisson offsets, host-side), issue `testsend`/`testch` chunked to the RX buffer, record the send-ledger (expected tags + dst/flags + scheduled ms).
2. **Wait** `duration + margin` — **no live capture** (USB idle → nodes survive).
3. **Align + pull** — read each node's `uptime_ms` once (clock-offset for latency) + the durable inbox via the hard-retry `_robust_pull` (+ the measured/UNMEASURED handling already built).
4. **Reconcile** — `parse_tag` gains: strip the `@<sendms>` suffix → the canonical tag for matching; a `parse_sendms` helper. Latency = `rx_ms` − `sendms`, each lifted to host time via its node's offset; **flagged approximate** (per-node `millis()` drift ≈ ±tens of ms over a 5-min run — fine for second-scale mesh latency).

## Tests / gate (firmware)
- **Native:** the schedule tick is pure logic — unit-test it with a fake clock: entries fire at/after `at_ms`, in order; `testclear` resets; a full schedule rejects; the built body equals `make_tag(run,self,seq)+"@"+sendms`; a failed-enqueue path defers then drops after slack (count correct). Keep the console parse native-testable (no Arduino in the logic).
- **Build:** all 4 boards.
- **Sim:** none — device console + send-path only; lib/core untouched (verify s18 unchanged if any shared TU is edited).
- **Metal (the proof):** `testsend <dst> <run> -a -t 0,2000,5000` → connect mid-run and watch `teststatus` count up + the console stay responsive + the dst inbox get the 3 tagged DMs; confirm **USB can be unplugged during the run and the sends still happen** (the whole point). Then a realistic-size run with the harness mode → far fewer (ideally zero) USB-CDC deaths vs the live-stream oracle.

## Sequencing
High value — this is the durable fix for the test-harness USB death that's been blocking clean channel measurements. Independent of the fault-log-v3 LR work; both are in the hardware thread. Firmware first (coder), then I build the harness mode.
