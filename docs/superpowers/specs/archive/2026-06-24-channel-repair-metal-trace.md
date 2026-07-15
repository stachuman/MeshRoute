<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel-repair metal trace — make the digest/retire/pull chain visible under `debug on`

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. **DEBUG-ONLY, no behaviour change** (every line gated by `_hal.trace_on()`); no wire change, no sim impact.

## Why

The holder-aware-retire fix (2026-06-23) did **not** rescue the metal channel orphans (bench run `3b9abc`: origins 106/247/170 → 0/7). We can't tell *why* — the channel repair's decisions (advertise / **retire** / pull-schedule / serve) are all `MR_TELEMETRY`, **compiled out on metal**. That's the same observability gap that hid the flood-coverage bug. This adds a `_hal.log` trace at the four repair decision points, gated by the existing `_hal.trace_on()` (`debug on`), so the next run *shows* whether an orphan keeps advertising into the post-burst quiet period or retires early — and whether neighbours hear its digest and pull.

## Verified current state

- The metal-visible trace mechanism already exists: `_hal.set_debug_hooks` (fw_main.cpp:1203) routes `_hal.trace_on()`→`g_mr_trace_on` (`debug on`) and `_hal.log()`→`Serial.println`. The disabled `flood_log_coverage` (node_channel.cpp:41, `#if 0`) is the exact pattern: `if (_hal.trace_on()) { char buf[…]; snprintf(…); _hal.log(buf); }`.
- The four repair decision points (all in `node_channel.cpp`): `commit_channel_digest_advertised` (the advertise/retire — added 2026-06-23), `process_channel_digest` (the per-id have/missing → pull-schedule decision, :353), `channel_pull_fire` (the pull TX), `handle_channel_pull` / the serve path (the holder answering a pull).

## The change — four `_hal.trace_on()`-gated `_hal.log` lines

Compact, one line each (≤~80 chars; bounded stack buffer + snprintf, like flood_log_coverage). No logic change — read-only of existing locals.

1. **Advertise / retire** — in `commit_channel_digest_advertised`, per committed id, AFTER the `++bcn_ad_count` + retire decision:
   `chan <id8hex> ad=<bcn_ad_count> seen=<seen_cnt>/<live_nbrs> -> ADVERTISED` — or on retire — `-> RETIRE(<seen|horizon>)`. (Compute `seen_cnt`/`live_nbrs` from the same `channel_entry_fully_seen` neighbour scan; a tiny helper that returns the counts is fine.) ★ THE key line — shows whether an orphan (seen=0/N) keeps advertising or retired early.
2. **Pull decision** — in `process_channel_digest`, per advertised id from `src`:
   `chan digest<-<src> <id8hex> HAVE` (we hold it → marked seen), or `MISSING -> pull@<jitter>ms` / `MISSING -> skip(<recent|cap|ringfull>)`. Shows whether a neighbour that hears the orphan's digest decides to pull.
3. **Pull TX** — in `channel_pull_fire` (when it actually transmits the pull): `chan pull <id8hex> -> <holder>`.
4. **Serve** — where a holder answers a pull (`handle_channel_pull` / `enqueue_channel_m` for a pull): `chan serve <id8hex> -> <puller>`.

Together they trace the full chain: **advertise → (neighbour) hear+decide → pull → serve**. On an orphan you'll see exactly where it breaks — e.g. `RETIRE(horizon)` before any `digest<-` pull (retired too early), or repeated `ADVERTISED` with no neighbour `pull@` (digest never heard), or `pull` with no `serve` (pull/response lost).

## Not a behaviour/wire/sim change

Every line is `if (_hal.trace_on())`-gated → zero output (and the branch is trivial) when `debug off`, which is the default and the BASELINE/sim condition. No frame, timing, or state change. `_hal.trace_on()`/`_hal.log()` are no-ops in the sim/native HAL.

## Tests / gate

- **Native:** the suite stays green (the trace is print-only behind a default-false gate — no logic touched). A light unit is optional (assert the formatter composes the expected string for a synthetic entry); not required.
- **Build:** `pio run -e gateway -e xiao_sx1262` (the bench boards) — confirm the snprintf/format compiles; the ESP32 envs are unaffected (the `_hal` trace hooks exist on all).
- **Metal (user):** flash, `debug on`, run `scenario-realistic.txt`, and read an **orphan** origin's console: the four-line chain tells us the failure mode. (Heavy output under `debug on` — expected; `debug off` afterward.)

## Note

Pairs with the realistic-pacing run (`scenario-realistic.txt`, Poisson) — together they answer the two open questions: does desynchronizing the sends remove the orphaning (test artifact), and if some remains, *where* in the advertise→pull→serve chain it dies (firmware). Decide the real fix (re-flood "C" / longer-or-time-based horizon / accept-as-load-artifact) from what the trace shows — not blind.
