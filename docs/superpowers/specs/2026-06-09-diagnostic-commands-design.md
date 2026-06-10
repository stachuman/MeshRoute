# Diagnostic & Hash-Locate Command Suite — Design

**Status:** DESIGN (approved shape 2026-06-09; not yet implemented)
**Author context:** extends the app↔firmware command seam (`command.h`) + the device console (`fw_main.cpp service_debug`).

## 1. Goal

Round out the firmware's operator/diagnostic command surface. Today the messaging
primitives are `send`, `send_ack`, `send_channel` (typed actions) plus a handful of
read-only console commands (`routes`, `status`, `cfg`). The hash-locate (H) plane is
fully implemented in core but **unreachable from the console** — you cannot send by
hash, resolve a hash to a node id, or even read your own `key_hash32`. This batch adds
the hash/id family, makes send-by-hash reachable, and adds the missing per-plane state
dumps and two active probes.

## 2. Background — the current surface (verified)

Two distinct layers exist, and every new command lands in one of them:

- **Layer 1 — typed action commands.** `CmdKind` → `Node::on_command` → synchronous
  `CmdResult`, plus an async `Push` drained by the host (`next_push`). Members today:
  `send`, `send_channel`, `join` (+ `send_layer` stub → `err_unsupported`). Defined in
  `lib/core/command.h`; dispatched at `lib/core/node.cpp:195-235`.
  - **Key fact:** `on_command` for `CmdKind::send` already routes `dst_hash != 0` to
    `send_by_hash()` (`node.cpp:204-207`). The whole park→flood-H→drain path is wired;
    the console parser just never sets `dst_hash` (`console_parse.cpp:66-69`).
- **Layer 2 — console diagnostics.** Text-only device shell in `src/fw_main.cpp`
  `service_debug` (`routes`, `status`, `cfg [set …]`, `sleep`, `debug`, `regen`,
  `reboot`). Pure device-local; never touches the typed seam. The sim does **not** use
  this layer — it introspects via the C++ `Node` accessors directly (it links
  `meshroute_core`).

Async push channel (`command.h:54-59`): `PushKind { msg_recv, channel_recv, send_acked,
send_failed }`. No host-facing notification exists for an H resolution today —
`on_hash_bind_response` (`node_hashlocate.cpp:323-337`) silently updates `id_bind` and
drains parked sends.

## 3. Design principle — three homes by behaviour

- **Pure reads** (`lookup`, `hashof`, `whoami`, the table dumps) → **Layer 2 console**.
  The logic is a `node.h` accessor (unit-testable in native doctest); the console is
  thin formatting. Not added to the typed seam — the sim reads state via accessors.
- **Transmitting actions** (`resolve`, `sendhash`) → **Layer 1 typed**, so they are
  sim/BLE-reachable and testable through `on_command`, with async results via `Push`.
- **Device probes** (`findroute`, `beacon`) → **Layer 2 console** actions calling a
  small public trigger on `Node`.

This keeps the typed seam minimal (it only grows for things that genuinely transmit and
report asynchronously) and keeps cheap reads as thin device glue.

## 4. The command suite

### ① Hash/id reads — Layer 2 console, glue over existing getters

| Command | Output (one line unless noted) | Backed by |
|---|---|---|
| `lookup <hash>` | `0x<hash> -> id=<N> (authoritative\|claimed)` or `miss` | `id_bind_find_by_hash()` (exists) |
| `hashof <id>` | `id=<N> -> 0x<hash>` or `unknown` | `key_hash_of_id()` (exists; AUTHORITATIVE-only) |
| `whoami` | `id=<N> hash=0x<hash> name="<name>" leaf=<L> gw=<0/1> gwonly=<0/1> mobile=<0/1>` | `node_id()`/`key_hash32()`/`config()` (exists) — fills the **can't-read-own-hash** gap (`cfg` omits `key_hash32`) |
| `idbind` | table dump, one row per binding: `0x<hash> -> id=<N>  src=<self/bcn/hquery/hrelay>  conf=<claimed/auth>  age=<S>s` | **new** `id_bind_at(i)` (mirror `rt_at`) + `id_bind_count()` (exists) |

`<hash>` is parsed/printed as up-to-8 hex digits (`parse_hex32_tok` exists,
`console_parse.cpp:77`).

### ② Network resolve — Layer 1 typed + one new push

| Command | Flow |
|---|---|
| `resolve <hash> [hard]` | `CmdKind::resolve{ dst_hash, hard }` → `request_resolve()` → if an AUTHORITATIVE binding is already cached, push the answer immediately; else park a *resolve-request* and fire the H flood (`emit_hash_query(hard)`). On bind, emit **`PushKind::hash_resolved`**. `hard` skips caches to reach the owner (verify-on-use). |

Host prints on the push:
- success → `RESOLVED 0x<hash> -> id=<N> (auth\|cached)`
- timeout → `UNRESOLVED 0x<hash> (timeout)`

Mechanism detail in §7.

### ③ Send-by-hash — Layer 1 typed, pure glue (core path already wired)

| Command | Flow |
|---|---|
| `sendhash <hash> <text>` | console parses hex → `CmdKind::send` with `dst_hash` set, `flags=0` → existing `on_command` → `send_by_hash` |
| `sendhash_ack <hash> <text>` | same, `flags=0x08` (E2E ack-req) |

Async result rides the **existing** `send_acked` / `send_failed` pushes. This makes the
H plane reachable from the console for the first time. No core change beyond the parser.

### ④ State dumps — Layer 2 console, need index accessors (mirror `rt_at`/`rt_count`)

| Command | Plane | New accessor |
|---|---|---|
| `neighbors` | per-neighbour liveness tier + blind-window status | `neighbor_at(i)` over the neighbour-tier table; cross-ref `is_blind()` |
| `channels` | channel buffer: `id / channel / origin / dirty / age / seen_by_count` | `channel_entry_at(i)` (+ `channel_buffer_count()` exists) |
| `flood` | active flood slots: `id / src / hop_left / awaiting_data / coverage_count` | `flood_at(i)` over `_flood[cap_flood_pending=3]` |
| `queue` | tx-queue depth + deferred-send count + parked-send count, each with oldest-age | `deferred_count()`, `parked_sends_count()` (tx depth via `status` already) |

### ⑤ Probes — Layer 2 console actions

| Command | Action | New trigger |
|---|---|---|
| `findroute <dst>` | originate an F RREQ for `dst` (route-discovery probe) | `trigger_route_discovery(dst)` calling the F origination path |
| `beacon` | force an immediate beacon TX | `trigger_beacon()` |

`ping` is **deliberately dropped** — `send_ack <id>` already provides an app-level
round-trip signal (`ACKED`/`FAILED` push); a dedicated link-ping would need a new echo
responder for marginal value (see §13).

## 5. New core types (`lib/core/command.h`)

```cpp
enum class CmdKind : uint8_t { send, send_layer, send_channel, join, resolve };  // + resolve

struct ResolveCmd { uint32_t dst_hash; bool hard; };   // new union arm

// in Command::u union:  ... ResolveCmd resolve;

enum class PushKind : uint8_t {
    msg_recv, channel_recv, send_acked, send_failed,
    hash_resolved,        // a resolve completed; field mapping below
};
```

`hash_resolved` field mapping on the existing `Push` struct (no struct change):
- `origin` = resolved `node_id` (**0 = unresolved / timeout**)
- `dst`    = `authoritative ? 1 : 0`
- `ctr`    = low 16 bits of the hash (for the host to correlate); the console handler
  re-prints the hash it asked for, so full-hash carriage is not required. *(If full hash
  is wanted, carry it in the first 4 body bytes — decide at implementation; default:
  ctr-low-16 + the console remembers the asked hash.)*

`CmdCode` gains no new value — `resolve` returns `queued`, or `err_unprovisioned` if
`node_id == 0` (the H flood needs a valid origin). It rides the **routing/control SF**
(`emit_hash_query` → `_cfg.routing_sf`), so `err_no_data_sf` does **not** apply — a node
with an empty `sf_list` can still resolve, it just can't `sendhash` the follow-up DM.

## 6. New `Node` accessors / triggers (`lib/core/node.h`)

Reads (const, mirror `rt_at`/`rt_count`):
- `uint16_t id_bind_count()` *(exists)* + `bool id_bind_at(i, &hash, &node, &src, &conf, &age_ms)`
- `bool channel_entry_at(i, &id, &channel, &origin, &dirty, &age_ms, &seen_count)`
- `uint8_t neighbor_count()` + `bool neighbor_at(i, &node, &tier, &blind)`
- `uint8_t flood_at(i, &id, &src, &hop_left, &awaiting, &coverage_count)`
- `uint8_t deferred_count()`, `uint8_t parked_sends_count()`

Actions/triggers:
- `void request_resolve(uint32_t hash, bool hard)` — the resolve entrypoint (§7)
- `void trigger_route_discovery(uint8_t dst)` — originate an F RREQ
- `void trigger_beacon()` — force a beacon now

Accessors return flattened fields (not internal structs) to avoid widening the public
header's type surface — consistent with the no-heap/POD discipline.

## 7. The resolve mechanism (the one genuinely new piece)

A *resolve-request* reuses the parked-sends machinery (`_parked_sends[cap_parked_sends=8]`,
`node.h`). `ParkedSend` already carries an `is_redirect` flag for L2c redirects; add an
`is_resolve` flag (notify-only, no body).

```
request_resolve(hash, hard):
    if hash == 0 or hash == own:                  -> push hash_resolved(own_id, auth=1); return
    id, conf = id_bind_find_by_hash(hash)
    if id >= 0 and conf == authoritative and !hard:
        push hash_resolved(id, auth=1)            -> immediate (no flood)
        return
    park a resolve-request (is_resolve=true, key_hash32=hash)
    emit_hash_query(hash, hard)                    -> the H flood
```

Resolution path — extend the existing drain:
- `on_hash_bind_response` (and the beacon/snoop binding path) already calls
  `drain_parked_sends(hash, node_id)`. Extend the drain: for a parked entry with
  `is_resolve`, **emit `PushKind::hash_resolved(node_id, authoritative)`** instead of
  flying a DM, and remove the entry.

Timeout — extend `age_out_parked_sends` (`node_hashlocate.cpp:513`, TTL
`send_defer_ttl_ms = 30000`): when a `is_resolve` entry ages out, push
`hash_resolved(node_id=0)` (the "unresolved/timeout" sentinel) so the operator gets a
definite answer rather than silence.

This adds **no new table** — one bool on `ParkedSend`, two branches in existing drain /
age-out, and the new push.

## 8. `sendhash` (glue detail)

`console_parse.cpp parse_command` gains the verbs `sendhash` / `sendhash_ack`:
- parse arg as hex (`parse_hex32_tok`), set `out.kind = CmdKind::send`,
  `out.u.send.dst_hash = <hash>`, `out.u.send.dst_id = 0`,
  `out.u.send.flags = sendhash_ack ? 0x08 : 0x00`, body = remainder.
- `on_command` already does the rest (`node.cpp:204-207`). No core change.

`resolve` parsing: verb `resolve`, arg hex hash, optional trailing `hard` token →
`CmdKind::resolve{ dst_hash, hard }`.

## 9. Console wiring (`src/fw_main.cpp`)

- `service_debug` gains the Layer-2 commands: `lookup`, `hashof`, `whoami`, `idbind`,
  `neighbors`, `channels`, `flood`, `queue`, `findroute`, `beacon`. Each calls the
  matching accessor/trigger and prints (formats in §10). Add them to `help`.
- The push-drain loop (`fw_main.cpp:553-570`) gains a `case PushKind::hash_resolved` →
  print `RESOLVED …` / `UNRESOLVED …`.
- `resolve` and `sendhash`/`sendhash_ack` are parsed by `parse_command`
  (`console_parse.cpp`) and dispatched through `on_command` like the existing sends.

## 10. Output formats (concrete)

```
> whoami
id=12 hash=0x8a3f1c02 name="kate" leaf=2 gw=0 gwonly=0 mobile=0

> lookup 0x8a3f1c02
0x8a3f1c02 -> id=12 (authoritative)

> idbind
idbind (4/256):
  0x8a3f1c02 -> id=12  src=self    conf=auth     age=0s
  0x1177aa01 -> id=3   src=bcn     conf=auth     age=84s
  0x55012abc -> id=9   src=hquery  conf=claimed  age=12s

> resolve 0x55012abc hard
queued
RESOLVED 0x55012abc -> id=9 (auth)

> neighbors
neighbors (3):
  id=3   tier=healthy   blind=no
  id=7   tier=strained  blind=no
  id=9   tier=silent    blind=yes(4s)

> sendhash 0x55012abc hello
queued ctr=41 depth=1
ACKED ctr=41
```

Plain text, matching the existing `routes`/`status` style. No JSON (see §13).

## 11. Implementation order (each phase independently shippable + green)

1. **Hash reads** — `lookup`, `hashof`, `whoami`. Pure glue over existing getters.
   Native tests assert the accessor outputs; console formatting is thin.
2. **`sendhash` / `sendhash_ack`** — console parser verbs only. Unlocks the H plane
   end-to-end. Test: `parse_command` sets `dst_hash`; `on_command` queues via
   `send_by_hash`.
3. **`resolve` + `hash_resolved` push** — `CmdKind::resolve`, `PushKind::hash_resolved`,
   `request_resolve`, the `is_resolve` parked-request + drain/age-out branches.
4. **Dumps** — `idbind`, `neighbors`, `channels`, `flood`, `queue` + their accessors.
5. **Probes** — `findroute`, `beacon` + triggers.

## 12. Test plan

- **Native doctest** (the bulk): each new accessor (`id_bind_at`, `channel_entry_at`,
  `neighbor_at`, `flood_at`, counts) returns correct rows after a seeded state;
  `on_command(CmdKind::resolve)` with a cached AUTHORITATIVE binding pushes
  `hash_resolved` immediately; with an unknown hash, parks + emits H, and a subsequent
  `on_hash_bind_response` drains to a `hash_resolved` push; age-out pushes the
  `node_id=0` timeout; `parse_command("sendhash 0x… text")` sets `dst_hash`; `on_command`
  routes it to the hash path.
- **Sim** (one scenario run): an operator-issued `resolve` floods H and the answer
  returns end-to-end (watch `emit_hash_query` → `hash_bind` → `hash_resolved`).
- **Both boards** build green (xiao_sx1262, heltec_v3) — the console additions compile
  under `-DMR_NO_POWERSAVE` and full builds.

## 13. Deliberate decisions / non-goals

- **`ping` dropped.** `send_ack <id>` already round-trips with `ACKED`/`FAILED`; a
  link-layer ping would need a new echo request/responder frame for little extra signal.
  Revisit only if a sub-ACK-latency link probe is needed.
- **Reads are console-only, not typed.** The sim introspects via C++ accessors directly;
  duplicating every dump as a typed query+push would bloat the seam for no test/BLE gain.
  Only transmitting actions (`resolve`, `sendhash`) are typed.
- **Plain-text output, not JSON.** Matches `routes`/`status`. `console_json.cpp` encodes
  the typed result/push names, not diagnostic dumps; a structured diag mode is out of
  scope.
- **`resolve` reuses parked-sends, not a new table.** One bool on `ParkedSend`; bounded
  by the existing `cap_parked_sends=8` and `send_defer_ttl_ms=30000` TTL.

## 14. Open risks / to-confirm at implementation

- **`hash_resolved` field packing:** whether to carry the full 32-bit hash in the push
  body or rely on the console remembering the asked hash (ctr-low-16 correlation).
  Default: console remembers; revisit if a backend needs the full hash echoed.
- **`neighbors` source table:** confirm the neighbour-tier table is the right iteration
  source vs deriving neighbours from `rt[]` hops==1 entries; pick whichever already holds
  the tier + blind state without a second cross-walk.
- **`trigger_route_discovery` / `trigger_beacon`** must respect duty-cycle/LBT like the
  organic paths (no diagnostic bypass of the airtime budget).
