<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# OTA remote diagnostics — command/response over a DM (`rcmd`)

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Firmware change. Adds two DATA app-types (backward-compatible — the whole fleet reflashes pre-deployment, so **no `wire_version` bump**).

## Why

A node whose **USB-CDC dies is unreachable over serial but still alive on the mesh** (bench-confirmed: ACM3 dark on serial, radio LED still blinking, `ran 16m`). The only way in is **over the air**: send it a diagnostic command via a neighbour and get the answer back as a DM. Same channel lets you **reboot** it (which may itself clear the wedged USB) or `prep-restart` it without physical access. We need this regardless — it's the remote-admin path for the fleet.

**Decisions (user, 2026-06-24):** **read-only diagnostics + the two recovery writes (`reboot`, `prep-restart`)**; NOT arbitrary console passthrough (v1). Honest-bench trust model — broad write-auth is a flagged follow-on.

## Verified current state

- `DataType` (frame_codec.h:410): **6 is FREE** (the removed CONFIG_ANSWER slot); 7 free. Types 1-5 are H_ANSWER/E2E_ACK/etc.
- `enqueue_data(dst, body, len, flags, label, app_dm, type)` (node_mac.cpp:301, used by `send_e2e_ack`) is the **typed-DM origination template** — rides normal routing/ACK, multi-hop.
- The receive type-dispatch (node_mac_rx.cpp:551-561): H_ANSWER/E2E_ACK are CONSUMED (not delivered). New types slot in the same place.
- Compact response bodies already exist: console_json `write_status`/`write_duty`/etc.; the `faults`/`version` formatters from the fault-log work.

## The change

### Frame (frame_codec.h)
`DATA_TYPE_REMOTE_CMD = 6` · `DATA_TYPE_REMOTE_RESP = 7`. Body = the command keyword (cmd) / the response text (resp); plain inner, cleartext (diagnostic, honest net — E2E-seal is a later option).

### lib/core (Node) — generic transport only
- `send_remote_cmd(uint8_t dst, const uint8_t* body, uint8_t len)` and `send_remote_response(uint8_t dst, const uint8_t* body, uint8_t len)` → `enqueue_data(dst, body, len, /*flags*/0, "rcmd_tx"/"rcmd_resp_tx", /*app_dm*/false, DATA_TYPE_REMOTE_CMD/RESP)`.
- Receive dispatch (node_mac_rx.cpp:551): `pa.type == REMOTE_CMD || REMOTE_RESP` → stage into a **single-slot inbound** `{active, is_response, from=pa.origin, body[≤inbox_max_body], len}` and RETURN (do NOT inbox/deliver-as-message; do NOT consume-silently like E2E_ACK). One in flight (a 2nd while pending → drop + a `MR_EMIT` count; rcmd is human-paced). A dedicated slot (not the small push ring) so a full ~200 B response body fits and execution happens in the **main loop**, never the RX path.
- `bool take_remote_inbound(RemoteInbound& out)` — fw_main drains it each loop.

### src/fw_main.cpp — origin + target + the whitelist
- **Origin command** `rcmd <dst> <query>`: `g_node.send_remote_cmd(dst, query, len)`. Add to the dispatch table + `dump_help` + the JSON console (so the **harness** can issue it).
- **Loop**: `if (g_node.take_remote_inbound(ri))` →
  - `ri.is_response` → print `[rcmd <from>] <body>` (a parseable single line for the harness).
  - else (a command for us) → `remote_exec(ri.from, ri.body, ri.len)`.
- **`remote_exec(from, query, len)`** — the **whitelist** (anything else → respond `err: <query> not allowed`):
  - **Reads (compact, one DM ≤ inbox_max_body):** `status` (up/rx/tx/txq/txto/routes/duty/reset-cause/ran), `faults` (the `[faults]` summary + the newest record), `version` (fw/build/git + last reset), `uptime`, `cfg` (id/leaf/level_id/sf_list/freq), `duty`. Reuse console_json / the fault-log formatters; truncate to one DM (note `…` if clipped).
  - **Recovery writes:** `reboot` and `prep-restart` — **respond FIRST** (`ok reboot`/`ok prep-restart`), then **defer the action ~3 s** (a timer) so the response DM actually airs before the node reboots/halts. No other writes (no `cfg set`, no `factory_reset`) in v1.

### tools/meshroute_lab.py (harness)
A thin `rcmd <node_id_or_neighbour> <query>` that sends over a chosen **live** node and prints/parses the `[rcmd <from>] …` reply — over-the-air `status`/`faults` when a node's serial is dead.

## Security / trust

Honest-bench v1: any node may `rcmd` any node, incl. `reboot`/`prep-restart`. Fine for the trusted lab. **Flagged follow-on for deployment:** authenticate the command (a signed/HMAC'd cmd or an allow-list of admin origins) before honoring writes — do NOT ship OTA writes to an untrusted network without it.

## Not a wire-version bump

Adds two `DataType` values; the frame layout is unchanged (the conditional TYPE byte already exists). The whole fleet reflashes together pre-deployment, so no node sees an unknown type. (A hypothetical un-updated node receiving type 6/7 would just fail the dispatch and drop it — no corruption.) `wire_version` untouched.

## Tests / gate

- **Native unit:** `send_remote_cmd`/`send_remote_response` enqueue a DATA with type 6/7 to the right dst + body (frame round-trip); the RX dispatch stages REMOTE_CMD/RESP into the inbound slot (not the inbox, not delivered-as-message); `take_remote_inbound` drains it; a 2nd-while-pending drops. `remote_exec` whitelist: a read produces a bounded response; a non-whitelisted query → `err`; `reboot`/`prep-restart` respond-before-acting (assert the response is enqueued before the deferred action fires).
- **Build:** `pio run -e gateway -e xiao_sx1262 -e heltec_v3 -e xiao_esp32s3`.
- **No sim regression:** the new type is inert unless `rcmd` is issued; s18/baseline untouched (verify s18 unchanged — no rcmd in the scenarios).
- **Metal (user — the payoff):** from node A, `rcmd <B> status` → `[rcmd B] …` prints (incl. **multi-hop**, B 2 hops away); `rcmd <B> faults` reads B's ring over the air; `rcmd <B> reboot` → `ok reboot` then B reboots; then the **USB-dead repro**: pull B's USB data (or trigger the CDC wedge), `rcmd <B> status` via a neighbour still answers, and `rcmd <B> reboot` recovers it — **diagnose + recover a node with no serial.**

## Sequencing

Independent of the fault-log v2 + channel work, but **pairs with** the USB-CDC investigation: `rcmd <wedged> status` confirms a "dead" node is mesh-alive (USB-only failure) vs truly gone, and `rcmd reboot` is the remote recovery. Depends on `prep-restart` existing (the other 2026-06-24 spec) for that write.
