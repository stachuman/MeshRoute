# Firmware command interface — clean design proposal

**Date:** 2026-05-30  **Status:** IMPLEMENTED. `lib/core` has NO command string-parser —
the typed `Command` seam (`CmdResult Node::on_command(const Command&)`) + the async push
ring (`Node::next_push(Push&)`) are the only command path; the sim `FirmwareNode` parses
the scenario string into a `Command` + drains the push ring to telemetry; a device backend
parses binary frames into the same `Command`. Validated end-to-end: t86 emits
`push send_acked` at the sender + `push msg_recv payload="hello-bob"` at the receiver; t86/
t87/dm_diff + 57 doctest cases green; the 4-step migration kept the delivery gates green at
each step. You commit. **O1 = async push-ring** (the
MeshCore-faithful model — `on_command` returns a token; delivery/ACKs/inbound arrive as
async `Push` frames drained from a bounded ring); **O2 = borrowed body**; Option A
(tagged-union `Command`); O3/O4/O5/O6 take rec defaults (directory home + cmd numbering
decided when `dst_hash`/the device backend land). The §2 "synchronous CmdResult" recommendation
is superseded by the async push-ring below.

### Async push-ring (O1, pinned)

`on_command` still returns a typed `CmdResult{code, ctr, queue_depth}` *token* (so the caller
gets the message id + queued/err immediately), but **outcomes + inbound messages flow back as
async `Push` frames** the transport drains — exactly MeshCore's `PUSH_CODE_*` + `CMD_SYNC_NEXT`
model. New in `command.h`:

```cpp
enum class PushKind : uint8_t { msg_recv, send_acked, send_failed };
struct Push {
    PushKind kind;
    uint8_t  origin, dst; uint16_t ctr;          // correlate to the send's CmdResult.ctr
    uint8_t  body[protocol::max_payload_bytes_hard_cap]; uint8_t body_len;   // msg_recv text
};
bool Node::next_push(Push& out);                 // drains the next ring entry (CMD_SYNC_NEXT style)
```

The Node owns a bounded `_push_ring[cap_push_ring=16]` (drop-oldest on overflow, like MeshCore's
offline queue). It enqueues: `msg_recv` when a message is delivered to us (the app reads it),
`send_acked` when our send's link ACK returns, `send_failed` on giveup. The **sim** `FirmwareNode`
drains the ring each tick and re-emits the pushes as telemetry (for observability + tests); a
**device** backend serializes them to `RESP_CODE_*`/`PUSH_CODE_*` frames over serial/BLE. The
`_hal.emit` telemetry (`delivered`/`ack_rx`) stays — it is the observability channel that
`dm_delivery` reads; the push ring is the *app* channel. (E2E end-to-end delivery confirmation
is the proper `send_acked` source for multi-hop and lands with E2E ACK; R3's `send_acked` is the
hop ACK = delivery for 1-hop, first-hop-accepted for multi-hop — documented.)
A design pause (requested) to replace the string-parsing `on_command` — ported
straight from the Lua, where there were no memory constraints and the command
transport was an afterthought — with a clean, typed, no-heap, transport-agnostic
seam appropriate for a memory-constrained device. Grounded in the Lua command
surface, the **MeshCore device command protocols** (the hardware-proven model we
already reuse the HAL from), and the current C++ path.

---

## 0. The problem

`Node::on_command(const char* cmd, char* out_reply, size_t reply_cap)` string-parses
`"send <id> <text>"` *inside* `lib/core`, and `FirmwareNode`/`SimController` heap-allocate
`std::string`s + a `char[256]` reply per command. It is the **only** core entrypoint that
violates `hal.h`'s typed-POD-in / backend-serializes-out rule that `on_recv(bytes,len,RxMeta)`,
`on_timer(id)`, `on_radio_busy(BusyInfo)` all already follow. On a device this means
string-parsing the message hot path + heap churn — exactly what real radios avoid.

## 1. What real devices do (the MeshCore-proven model)

MeshCore runs **two** command surfaces and never string-parses the hot path:
- **Config → a text CLI** (`CommonCLI::handleCommand`) — a flat `memcmp` chain into a
  caller-owned ASCII reply. Config only (radio/name/power), transport-neutral.
- **Messages → a binary companion-frame protocol** (`MyMesh::handleCmdFrame`) — every
  frame is `[cmd_code:u8][typed LE payload]`, dispatched once with **no string parsing**.
  `CMD_SEND_TXT_MSG=2` = `[txt_type][attempt][timestamp:u32][dest_pubkey_prefix:6B][text]`.
  The destination is a **public-key prefix** resolved on-device against a bounded contacts
  array (`lookupContactByPubKey`) — there is **no name→id map on the device**. The transport
  is a tiny 3-method `BaseSerialInterface` (write/recv/isConnected) — structurally identical
  to `meshroute::Hal`. Replies/pushes are four flat `u8` enums (`CMD_*`/`RESP_CODE_*`/
  `PUSH_CODE_*`/`ERR_CODE_*`).

**The lesson:** our `on_command` string-parsing the SEND is precisely the anti-pattern
MeshCore rejects. The message path must be a **typed cmd-code + bounded payload, addressed
by id/hash not name, parsed by the per-backend transport — never by `lib/core`.**

## 2. The design (recommended: Option A)

**One typed entrypoint, mirroring both MeshCore's frames and `hal.h`'s typed PODs:**

`CmdResult Node::on_command(const Command& c)` — a new `lib/core/command.h` (header-only,
`<cstdint>/<cstddef>` only, no `std::string`/heap):

```cpp
enum class CmdKind : uint8_t { send, send_layer, send_channel, join };

struct SendCmd      { uint8_t dst_id; uint32_t dst_hash; uint8_t flags; };  // flags: E2E=0x08 | PRIORITY=0x02 (Lua wire bits)
struct SendLayerCmd { uint8_t hops[protocol::gw_env_max_hops]; uint8_t hop_count; uint32_t dst_hash; };
struct SendChannelCmd { uint8_t channel_id; };
struct JoinCmd      { enum Op : uint8_t { discover, claim, deny } op; uint8_t node_id; uint32_t claimant_hash; };

struct Command {
    CmdKind kind;
    union { SendCmd send; SendLayerCmd layer; SendChannelCmd channel; JoinCmd join; } u;
    const uint8_t* body;       // BORROWED for the call only (mirrors hal.h on_recv)
    uint8_t        body_len;
};

enum class CmdCode : uint8_t { ok, queued, err_unknown_dst, err_too_large,
                               err_no_gateway, err_priority_capped, err_no_binding };
struct CmdResult { CmdCode code; uint16_t ctr; uint8_t queue_depth; };   // the Lua status string, typed
```

Key decisions:
- **Collapse the 4 Lua `send_*` verbs into ONE `CmdKind::send` + 2 flag bits** (E2E `0x08`,
  PRIORITY `0x02`) — kills the fragile most-specific-first regex ladder. (`send_e2e`/`priority`
  are flags, not separate commands — exactly MeshCore's `txt_type` model.)
- **Parsing lives in the backend, never in `lib/core`** (the HAL split applied to commands):
  the SIM `FirmwareNode` parses the scenario string → `Command` (the `name→id` resolve stays
  sim-side, where it belongs); a future DEVICE backend decodes a MeshCore-style binary frame →
  the **same** `Command`. No string ever enters core.
- **Addressing by id/hash, never name.** `SendCmd` carries both `dst_id` (wired now) and
  `dst_hash` (key_hash32, wired when cross-layer / the known-nodes directory lands). The Lua's
  `send_layer` *already* addresses by `key_hash32` via `id_bind`.
- **Borrowed body** (`const uint8_t*`, `uint8_t len`) — `do_send` already owns the single
  235-B copy in `TxItem.inner`; an embedded array would be a redundant stack copy.
- **Typed `CmdResult`, no `out_reply` buffer** — the `char[256]` + the 2 heap `std::string`s
  per command vanish on-device; reply formatting becomes a pure backend concern (sim → string
  for `EventLog::cmdReply`; device → a `RESP_CODE_*` frame). Telemetry stays via `_hal.emit`.
- **Memory:** `Command` is a ~24-B stack POD, zero heap — the direct analogue of
  `TxParams`/`RxMeta`/`BusyInfo`. The fixed `_tx_queue[8]` + single-flight pending state are
  untouched. `do_send` stays byte-identical → **the R3 delivery path is unchanged.**

## 3. Options considered

| | Approach | Verdict |
|---|---|---|
| **A** *(rec)* | One tagged-union `Command` POD → `CmdResult on_command(const Command&)` | Mirrors MeshCore frames AND `hal.h` PODs; one dispatch; `do_send` unchanged |
| B | Separate typed methods (`send(...)`, `send_layer(...)`, …) | No union, but the backend must itself switch on cmd-code + fan out to N methods — pushes dispatch into every transport; loses the single-frame model |
| C | Keep a string seam, just move parse to backend + ban heap | Smallest diff, but **keeps the anti-pattern** MeshCore explicitly rejects (string-parsing the message path) |

## 4. Migration (keeps t86/t87/dm_diff green at every step)

1. **Add `lib/core/command.h`** — the types only. Compiles, nothing calls it.
2. **Add the typed entrypoint alongside the string one.** `on_command(const Command&)`
   switches on kind; `send` calls the **identical** `do_send(dst, body, len)`. Re-implement the
   old `on_command(const char*, …)` as a thin shim that parses `send <id> <text>` → `Command` →
   the typed path. Every caller still compiles. **Gate:** t86/t87/dm_diff green (`do_send`
   unchanged → the asserted funnel is byte-identical).
3. **Move the parse into the sim backend.** `FirmwareNode` builds a `Command` (the
   `SimController` `name→id` rewrite becomes "fill `Command.u.send.dst_id` from the sim
   directory") and formats `CmdResult` → string *only* for `EventLog::cmdReply`. **Gate:** green
   (same resolved id, same `do_send`).
4. **Delete the string entrypoint from core.** `lib/core` no longer contains any command
   char-parser — the stated goal. **Gate:** final t86/t87/dm_diff + the `dm_diff.py` lua-vs-
   meshroute run → zero behavioural drift.
5. **(Later, non-gating)** wire `SendCmd.flags` (E2E/PRIORITY), then add the `send_layer`/
   `send_channel`/`join` arms one iteration each (new union arm + switch case, **no signature
   change**). The device binary-frame decoder is a separate H-iteration constructing the same
   `Command`.

## 5. Open questions (recommendations; the genuine forks first)

1. **Result delivery model (the big one):** synchronous `CmdResult` return *(rec — matches
   today's inline reply, simplest)* vs MeshCore's fully-decoupled model where `send` returns
   only a token and delivery/ACKs arrive as async `PUSH_CODE_*` frames from a bounded ring. The
   async model is more device-faithful but a much bigger seam than R3 needs. **rec: sync now**,
   adopt the async push model at the device-backend iteration if needed.
2. **Body ownership:** borrowed pointer *(rec — `do_send` owns the only copy)* vs an embedded
   `uint8_t body[235]` for a future store-and-forward command transport. **rec: borrowed**;
   revisit only if a queued-command transport appears.
3. **Known-nodes directory home:** a new core-owned bounded `{id, hash_id, pubkey}` table
   resolving `hash→id` (like MeshCore's contacts) vs reusing the existing `id_bind` table that
   `send_layer` already queries. Ties into [[project_meshroute_future_protocol_ideas]] (the
   known-nodes directory). **rec: decide when `dst_hash` is first wired** (R5/R7), not now —
   but pin the direction.
4. **`dst_hash` in `SendCmd` from day one** (no future signature churn) vs omit until
   cross-layer — **rec: keep it** (tiny cost, commits the addressing contract).
5. **Device cmd-code numbering:** adopt MeshCore's `CMD_*` numbers (`SEND_TXT_MSG=2`, …) vs a
   MeshRoute-native set — behaviour-neutral, pin before the device serial/BLE decoder is
   written (+ mirror MeshCore's version-handshake frame to pin protocol version up front).
   **rec: MeshRoute-native compact set** (the §10 wire already diverges by design).
6. **Reply-string compatibility:** confirm no existing gate asserts the exact
   `"queued (depth=…, ctr=…)"` string; if one does, the sim `CmdResult→string` formatter
   reproduces it. (Check during migration step 3.)

## 6. Files

New: `MeshRoute/lib/core/command.h` (the types). Changed: `node.{h,cpp}` (the typed entrypoint
+ shim → then delete the string parser), `FirmwareNode.cpp` (parse the scenario string → `Command`,
format `CmdResult`), `SimController.cpp` (the `name→id` resolve fills `dst_id`; the rewrite hack
goes away). Gates: `test/t86`/`t87` + `tools/dm_diff.py` unchanged + green. Uncommitted — you commit.
