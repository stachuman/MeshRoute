<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0c — Quality-Agent pre-check ledger (2026-09-04): the transport-neutral output path

Authority: design §19 Slice 0c ("make the existing dispatcher/caller output path transport-neutral without adding
remote context or policy"), §12 (the seam Slice 6 later extends). Verified at `HEAD 0ee0f9b`. Depends on 0b
(the last global-sink writer) — sequence 0a → 0b → 0c, all in `src/firmware_commands.cpp` / `src/fw_main.cpp`.

## 0. What 0c is, in one line

Today the two transports reach the same handlers in OPPOSITE orders and with different envelopes; 0c makes ONE
execution seam that both call, so Slice 6 can attach `CommandContext`/`DispatchResult` to one place. Behaviour on
both transports must stay byte-identical (C1: refactor only). ⚠ The tree already warns that unifying the two
orderings "is a behaviour change on two working transports and needs its own slice and gate" — 0c IS that slice.

## 1. Source facts — the two orderings

| transport | order today | anchor |
| --- | --- | --- |
| USB `service_console` | `dispatch(line, pos, mrcon)` FIRST; if false → `parse_command` → `peerkey`/`peername` JSON acks written to `mrcon` → else `g_node.on_command(cmd)` → text `<cmdcode> ctr=N depth=N [dh=0x…]` | `src/fw_main.cpp:1083-1112` |
| BLE `ble_dispatch_line` | `parse_command` FIRST; `ok` → `on_command` → `write_ack`/`write_reqpubkey_sent` JSON; `help` → `write_err(console_only)`; `unknown_verb` → `LineSink ls(ble_sink); dispatch(line,len,ls)`; else `write_err(parse, unknown_cmd\|bad_args)` | `src/fw_main.cpp:556-596` |
| the warning that this is a slice of its own | `src/firmware_commands.h:105-113` ("OPPOSITE orderings … unifying them is a behaviour change … needs its own slice and gate (C1)") |
| `dispatch()` returns only "a handler matched" (Boolean) — Slice 6 replaces it with `DispatchResult`; 0c does NOT | `src/firmware_commands.h:78`, design §12 |
| the sinks: `GuardedConsole` (2048-B stage, USB), `LineSink` 1700 B (BLE, line-flushed), `BufferSink` 512 B (remote/rcmd whole-response) | `src/console_sink.h:61-93`, `src/dispatch_sink.h:15-63` |
| the `send`/`send_channel`/`send_layer` verbs are handled by the CALLERS around `dispatch()` (the parser returns them as `Command`s; `dispatch` returns false for them) — the caller-only arms 0e's inventory lists under `service_console` / `parse_command` (29 rows) | `firmware_commands.h:107-112`, inventory surface 3 |
| the JSON envelope (`write_ack`, `write_err`, `write_reqpubkey_sent`, `cmdcode_name`) is the ONE mapper shared by both transports; USB prints `cmdcode_name(r.code)` as text | `lib/console/console_json.cpp`, `fw_main.cpp:1110-1112` |

## 2. What "transport-neutral" can mean, and what 0c must NOT do

- One seam: e.g. `execute_line(line, len, Print& out, LineFormat fmt)` (names are the coder's) that performs the
  parse-vs-dispatch decision ONCE, in one order, and renders the result through the supplied sink in that
  transport's established envelope (USB text vs BLE JSON). The ORDER must be chosen so both transports keep their
  observable behaviour: the two orders differ only for lines that BOTH `parse_command` accepts AND `dispatch()`
  matches — the brief must have the coder ENUMERATE that intersection from 0e's inventory (verbs present in both
  `parse_command` and `dispatch` surfaces) and prove it empty, or pin each collision's current winner per transport.
- Not in 0c: `CommandContext`, authority, `DispatchResult`, any remote path, any new verb, the `send` semantics.
- The `help` BLE refusal (`console_only`) and the `regen` sink (0b) stay as they are after 0a/0b.

## 3. Gate and fence

- Fence: `src/fw_main.cpp` (the two callers), `src/firmware_commands.{h,cpp}` (the seam), `src/dispatch_sink.h` /
  `src/console_sink.h` only if the seam needs a sink interface change (state it), the probes (`probe_console_sink`,
  `probe_inbox_verbs`, `probe_custody_usb` — all three drive the real dispatcher; they are the wiring gate and must
  stay at their pins or gain arms), the inventory (`--write`), evidence. ⛔ No `lib/`.
- Byte-identity proof: for a corpus of console lines (every inventory verb + the three send verbs + an unknown verb
  + an empty line + a too-long line) capture USB and BLE output BEFORE and AFTER through the probes' fakes; assert
  identical bytes per transport. This is the C1 proof; the probes' negative controls are the mutation coverage
  (`src/` has no battery target).
- Corpus 36/36 by construction; boards flash ±, RAM +0 (no new buffers — a STOP if the seam adds one); census;
  sweep; checkers; `git diff --check`.
- Bench: **Part 60** (USB + BLE): a fixed script of ten lines on each transport reproduces the pre-slice transcript
  (the coder supplies the transcript from the probe capture; the bench compares).

## 4. Owner decision

None expected. If the intersection in §2 is NOT empty, the winner per transport is an owner ruling (which
transport's current behaviour is the one to keep).
