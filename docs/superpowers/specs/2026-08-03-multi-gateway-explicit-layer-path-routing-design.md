<!-- Author: OpenAI Codex -->
# Multi-gateway explicit layer-path routing — B60 fix design

*2026-08-03. Status: ROUTING DESIGN READY FOR IMPLEMENTATION REVIEW; B60-O1 ACK POLICY OPEN. Grounded against HEAD `ea1e324` and the
2026-08-03 metal trace `7 → 5 → 6`. This is a new design record; it does not amend the frozen
historical gateway or mobile specifications.*

## 0. Outcome

Make `send_layer <hash> <l1,l2,…>` walk every gateway named by the explicit layer path, instead of
trying to resolve the final recipient hash on the first intermediate layer.

The completed mechanism must support a path of at most **16 total layer entries, including the
source layer**. The console therefore accepts at most **15 user-supplied hops**; the originator or a
registered mobile's home prepends the source entry.

For the metal case:

```
source mobile / home on 7
    → gateway 7/5
    → intermediate leaf 5
    → gateway 5/6
    → final leaf 6
    → destination home
    → destination mobile 0x7B18ADA2
```

An H query for `0x7B18ADA2` is forbidden on leaf 5. Hash resolution is a final-layer operation.

This design is B60, not B59. It changes only explicit cross-layer handoff selection. It must not
relax route loop guards, previous-hop rejection, cascade custody, link-bidirectionality policy or
ordinary intra-layer route selection.

## 1. Confirmed defect

The originating side is correct:

1. A registered mobile wraps `send_layer 0x7B18ADA2 5,6 ...` as
   `DATA_TYPE_MOBILE_SEND` to its home.
2. The home validates the user hops, prepends its active layer and builds `[7,5,6]`, `cur=1`.
3. It selects the gateway serving the first requested leaf, sends the cross-layer DATA to the
   gateway bridging layers 7/5 (local node IDs 7/8), and preserves the final destination hash.

The gateway side is incomplete:

1. `bridge_cross_layer` reads `layer_ids[cur]` and correctly identifies leaf 5 as the layer to
   enter.
2. It unconditionally tries `id_on_leaf_by_hash(dst_hash)` and `mobile_home_on_leaf(dst_hash)`
   on leaf 5.
3. It advances `cur` to the layer-6 entry, but stores no distinction between:
   - “find the final recipient on this leaf”; and
   - “find the gateway for the next explicit path entry.”
4. `drain_xl_handoffs_for_leaf` interprets every unresolved handoff as the first case and emits an
   H query on leaf 5.

That is exactly the observed repeated `H leaf=5 hash=7B18ADA2`. The cursor is present and advanced;
the destination decision ignores it.

## 2. Capacity terminology and the 16-layer ruling

Three quantities must not be conflated:

- **Layer ID:** the path carries a full `uint8_t` layer value. Zero is unset; configured values are
  otherwise 1…255.
- **Wire leaf discriminator:** gateway matching and byte-0 filtering use `layer_id & 0x0F`.
- **Path capacity:** this design permits 16 total entries, origin included.

The implementation shall define explicit constants rather than continue using “hops” for both total
entries and user hops:

```cpp
xl_path_max_layers    = 16;  // complete preserved path, origin included
xl_path_max_user_hops = 15;  // send_layer argument / mobile wrapper
```

Delete the old `gw_env_max_hops` name. Do not retain it as an alias: removing the symbol makes the
compiler expose every four-entry assumption, including validation guards where silently changing
the old name's value from 4 to 16 would hide a missed migration. Codec, command and routing code
must use the new authoritative constants.

### 2.1 Local transition invariant — repeated nibbles are valid

A path entry remains a full byte on the wire. Gateway discovery, selection and bridge-leaf matching
interpret its low nibble in the **current layer's local routing context**:

```
leaf_nibble(layer_id) = layer_id & 0x0F
```

Therefore non-adjacent entries may repeat a nibble. For example, the full-byte chain
`[1,2,3,16,17,18]` is encoded as hex `[01,02,03,10,11,12]`; its locally interpreted nibble chain is
`[1,2,3,0,1,2]`. The later nibble 1 and 2 transitions are valid because they are selected from a
different active layer. No path-wide nibble-uniqueness check is permitted.

Only an **adjacent transition** must have different nibbles. A compliant dual-layer gateway cannot
bridge two leaves with the same nibble, and `validate_gateway_layers` already rejects that local
configuration. Extract or reuse one canonical `leaf_nibble` / `distinct_leaf_nibbles` predicate so
command-path validation and `validate_gateway_layers` cannot drift. Cross-reference the existing
gateway validator; do not create a second path-wide uniqueness policy.

Layer value 0 remains invalid. A nonzero full value whose low nibble is 0 is valid. The 16-entry
ceiling is a bounded resource/product limit, not a proof derived from globally unique nibbles.

The same adjacent-transition predicate must be used by:

- console parsing / command admission;
- static explicit-path origination;
- mobile wrapper creation;
- home unwrap and prepend;
- `pack_unicast_inner`;
- received path validation before a gateway accepts custody; and
- reversed E2E-ack construction.

No caller may canonicalize full bytes to nibbles, remove non-adjacent repeats, reorder hops or
truncate a path.

## 3. Locked routing invariants

1. **The explicit path is authoritative.** A gateway never replaces a remaining explicit hop with
   hash-locate output.
2. **Intermediate layer:** select a gateway for the next path entry. Do not consult the final
   destination binding and do not emit an H query for it.
3. **Final layer:** resolve the final hash to a static node or hosted-mobile home. Only here may the
   existing bounded H fallback run.
4. **One cursor, preserved path:** `layer_ids[]` is immutable. A gateway may change only `cur`.
5. **Stable flight identity:** `origin`, `ctr`, `ctr_lo`, `dst_hash`, `source_hash`, TYPE,
   flags, body and sealed-relay bytes survive every gateway unchanged.
6. **Ordinary routing is reused after gateway choice.** The selected gateway's node ID becomes the
   MAC destination on the current leaf; `rt_find`, deferred send and RREQ provide the intra-layer
   route to it.
7. **No routing fallback to the previous layer.** Existing previous-hop and loop guards stay intact.
8. **Bounded custody:** a missing next-gateway advertisement holds the gateway handoff only for the
   existing handoff TTL. It never creates an unbounded queue.
9. **Final-hash poison resistance:** even an authoritative binding for `dst_hash` on an
   intermediate leaf is ignored. Otherwise stale or leaked state could divert an explicit path.
10. **Reverse parity:** an E2E ACK walks the exact reversed path through the same algorithm.

## 4. Handoff algorithm

### 4.1 Bridge-time classification

Add an explicit handoff target kind:

```cpp
enum class XlHandoffTarget : uint8_t {
    final_recipient,
    next_gateway
};
```

Place it immediately after `dst_node_id`. On the current target ABI, `XlHandoff` has a free padding
byte at offset 3 before the 4-aligned `dst_key_hash32` at offset 4, so this field has **zero struct
size cost**. Pin that assumption with `offsetof` and `sizeof` static assertions and report the
per-board values. Do not overload `dst_node_id == 0` with two meanings again.

When a gateway receives a valid cross-layer DATA:

```text
enter_index = ui.cur
enter_layer = ui.layer_ids[enter_index]
more_layers = enter_index + 1 < ui.n_layers
```

The gateway first verifies that `enter_layer` belongs to its other configured leaf.

If `more_layers`:

- set target kind to `next_gateway`;
- advance the preserved cursor to `enter_index + 1`;
- do not read any final-hash or mobile-home table;
- buffer the handoff for the entered leaf.

Otherwise:

- set target kind to `final_recipient`;
- keep `cur == n_layers - 1`;
- run the existing final hash / mobile-home lookup on the entered leaf;
- buffer unresolved final delivery for the existing H fallback.

The cursor patch remains at the codec-defined path offset. It must not be recomputed independently
from flag combinations; use the parsed-inner offset/helper or add a codec helper that patches only
the cursor after revalidation.

### 4.2 Drain on an intermediate leaf

When the entered leaf becomes active and the target kind is `next_gateway`:

1. Re-parse the preserved inner and validate it.
2. Read `next_layer = layer_ids[cur]`.
3. Assert that its leaf nibble differs from the active leaf; the shared adjacent-transition
   predicate guarantees this for an admitted path.
4. Call `select_gateway_for_leaf(next_layer & 0x0F)` while this intermediate leaf is active.
5. If a gateway is known, set `dst_node_id` to that gateway and build the existing gateway-relay
   `TxItem`.
6. If the gateway is known but no route exists, enqueue normally; the existing deferred/RREQ path
   owns route discovery.
7. If no gateway is known, retain the handoff until `gateway_handoff_defer_ttl_ms`, retrying on
   later visits as gateway advertisements arrive. Emit local telemetry, but no H query.
8. At TTL, drop loudly with a reason that distinguishes `next_gateway_missing` from
   `final_hash_unresolved`.

Recommended telemetry:

```text
xl_transit_gateway_selected {ctr, current_leaf, next_layer, gw, cur, n}
xl_transit_gateway_wait     {ctr, current_leaf, next_layer, cur, n}
xl_handoff_giveup           {ctr, reason, current_leaf, next_layer|dst_hash}
```

Telemetry must be throttled so a waiting handoff does not log every loop iteration.

### 4.3 Drain on the final leaf

For `final_recipient`, retain the existing behavior:

1. Resolve `dst_hash` from the active leaf's authoritative binding.
2. If absent, resolve a hosted mobile to its home.
3. If still absent, emit the existing throttled H query on this final leaf only.
4. When resolved, re-inject toward the node/home.
5. A host's existing last-mile branch forwards the preserved inner to the mobile.

No final hash resolution occurs at earlier handoffs.

## 5. Sixteen-entry path plumbing

The current four-entry assumption exists independently in several places. All must move in one
slice:

| Surface | Required change |
|---|---|
| `protocol_constants.h` | delete `gw_env_max_hops`; add authoritative complete-path cap = 16 and user-hop cap = 15 |
| `SendLayerCmd` | store 15 user hops and a count |
| console parser | accept 1…15 hops; reject 16, zero, malformed and adjacent same-nibble transitions |
| `data_unicast_inner` | `layer_ids[16]` |
| codec parse/pack | accept 1…16 complete entries; reject 17 and adjacent same-nibble transitions; allow non-adjacent repeats |
| `originate_layer_path` | stack path array 16; prepend one source entry |
| `delegate_send_layer` | wrapper accepts at most 15 user hops |
| home unwrap | accepts at most 15 wrapper hops, prepends source, revalidates all 16 |
| `send_xl_ack` | reverse buffer 16; validate before sending |
| tests/helpers | remove literal-four assumptions and add boundary cases |

The wire layout remains:

```
[n_layers:u8][cur:u8][layer_ids:n_layers × u8]
```

There is no new byte, DATA type or flag. Paths of four or fewer remain byte-identical. A 16-entry
path consumes 12 more inner bytes at runtime than the former maximum. **No global payload or DM
body-size constant changes:** `max_payload_bytes_hard_cap` and `dm_max_body_bytes` remain unchanged.
The single size-first `pack_unicast_inner` check is authoritative for each path/body combination:
overflow returns `err_too_large`; nothing truncates.

### 5.1 Stack and retained-RAM accounting

Widening `data_unicast_inner.layer_ids` from 4 to 16 adds 12 bytes to a stack carrier on the RX
path. The local origination and reverse-ACK arrays grow by the same amount. This project has a
documented loop-task stack-overflow failure shape, so a compile-only size report is insufficient.

The implementation gate must:

- record `sizeof`/`offsetof` for every widened or modified carrier;
- inspect compiler stack-usage output for parse, RX/post-ACK, origination, mobile unwrap and reverse
  ACK call paths;
- build every supported board environment, including the smallest loop-task stack;
- measure runtime stack high-water on at least one gateway and one mobile/static board; and
- report `sizeof(Node)`, RAM and flash deltas per board.

`XlHandoff` already costs approximately 290 bytes × 16 slots (about 4.6 KB) on a gateway build; the
new target kind must consume its verified padding and must not grow that ring.

### 5.2 Mobile error mapping

Today `delegate_send_layer` returns only a counter, so every zero is reported as
`err_no_gateway`, including a path/body overflow. The larger path makes that lie easier to reach.

Change the delegate seam to return a typed `CmdCode` plus an output counter, or perform an
equivalent shared preflight. Required distinctions:

- no registered home → `err_no_gateway`;
- invalid path → `err_unsupported`;
- encoded inner too large → `err_too_large`;
- local TX queue full → the existing queue-full code;
- accepted wrapper → `queued` with its real counter.

The home-side asynchronous unwrap retains its one-shot delegated-failure signal, but telemetry must
carry the exact local reason.

## 6. Send-handle and companion contract

`CmdResult::layer_path` is a `uint32_t`; it cannot truthfully echo more than four full layer
bytes. Shifting five or more entries through it silently retains only a suffix, which is forbidden.

Preserve the existing field for compatibility and append a bounded full-path carrier to
`CmdResult`:

```cpp
uint8_t layer_hop_count = 0;
uint8_t layer_hops[xl_path_max_user_hops] = {};
```

Append the fields after existing members so positional aggregate initializers do not shift. Add a
small named result builder rather than duplicating copies at every refusal branch.

Serialization rule:

- for 1…4 user hops, keep the existing `lp` value and existing output byte-identical;
- for 5…15 hops, set legacy `lp=0`—never a truncated suffix—and add
  `"layers":[l1,l2,…]`;
- `dh` and `ctr` retain their existing meanings;
- a new companion prefers `layers` when present and otherwise unpacks `lp`;
- an old companion still correlates by `ctr` and `dh`, but cannot reconstruct a long manual path.

The USB acknowledgement similarly keeps `lp=0x...` for short paths and prints a comma-list for a
long one.

The companion contract update is additive and QA-owned; record it as owed rather than silently
editing the app contract in the firmware slice.

## 7. E2E-ack resource policy — B60-O1 OPEN

The present cross-layer deadline is fixed:

```
2 × gateway_send_giveup_ms = 300 s
```

A healthy long-path round trip can exceed it through window phasing. Linear scaling would use
`2 × (n_layers - 1) × gateway_send_giveup_ms`, reaching 4,500 seconds (75 minutes) at 16 entries.
The node has only eight pending-ACK slots, and `err_ack_ring_full` refuses further acknowledged
sends when they are occupied. Calling 75-minute slot retention “backpressure” does not settle the
resulting user-visible loss of `-a` capacity.

The owner must select one policy before long-path ACK work lands:

1. **Keep 300 seconds for every path.** No new resource pressure, but long paths can false-timeout.
2. **Scale to the full path.** Best witness window, but eight long sends can monopolize `-a` for
   75 minutes.
3. **Bound acknowledged path depth (recommended).** Route up to 16 entries, but synchronously refuse
   `-a` above an owner-selected complete-path limit. Keeping the present four-entry ACK limit
   preserves the provisioned deadline/ring behavior.
4. **Redesign ACK resources.** Add a long-path quota, separate storage or another explicitly
   measured policy before permitting long deadlines.

No implementation slice may silently choose among these. Until B60-O1 is ruled, S1/S2 leave ACK
admission and deadlines unchanged. Regardless of the ruling, same-layer ACK remains 60 seconds and
a reverse ACK frame never arms another deadline.

## 8. Wire and rollout

- DATA layout: unchanged.
- New DATA types or flags: none.
- NV layout / `device_nv::kVersion`: unchanged.
- `protocol::wire_version`: no bump, following the standing owner ruling that MeshRoute is
  test-hardware-only and upgrades are coordinated.
- Mixed firmware: unsupported. An old gateway rejects `n_layers > 4` and already mishandles
  multi-gateway transit even for three entries. Every gateway on an explicit multi-layer path must
  run the new firmware.

This incompatibility must be stated in the release/bench notes; do not claim graceful fallback.

## 9. Test plan

### 9.1 Codec and command boundaries

- round-trip complete paths of 1, 2, 4 and 16 entries;
- reject 0 and 17 entries;
- reject `cur >= n_layers`;
- reject layer value 0;
- reject an adjacent same-nibble transition such as `[1,17]`;
- accept non-adjacent repeated nibbles, including `[1,2,3,16,17,18]`;
- console accepts 15 user hops and rejects 16;
- home accepts a 15-hop mobile wrapper and produces a 16-entry complete path;
- the maximum fitting body for each tested path length succeeds; one byte over returns
  `err_too_large`, without changing the global DM body constants;
- mobile overflow reports `err_too_large`, not `err_no_gateway`;
- long `CmdResult` emits full `layers[]` and `lp=0`; short handles remain byte-identical.

### 9.2 Two-gateway B60 reproducer

Build `A → B → C` with gateways A/B and B/C:

1. Originate `[A,B,C]` for a hash known only on C.
2. At gateway A/B, assert:
   - cursor becomes the C entry;
   - no final-hash lookup result is consumed on B;
   - zero H frames are emitted on B;
   - gateway B/C is selected as the relay destination.
3. Poison B with an authoritative final-hash binding to an unrelated node and prove the selected
   destination remains gateway B/C.
4. At gateway B/C, assert the final hash resolves on C and the DATA reaches the recipient.
5. Remove the C binding and prove the only H query is on C.

The poison arm is load-bearing: without it, a branch that merely happens not to find the hash could
look correct.

### 9.3 Failure controls

- missing next-gateway advertisement: bounded wait, zero H, `next_gateway_missing` giveup;
- known next gateway but no route: ordinary RREQ/deferred path, later drain succeeds;
- intermediate queue full: handoff retained for retry;
- final binding absent: final-layer H retry and bounded giveup unchanged;
- malformed received path: drop loud before ACK/custody where ordering permits;
- adjacent same-nibble transition: refused before any frame is queued;
- non-adjacent repeated nibble: accepted and evaluated in each active layer's local context.

### 9.4 End-to-end variants

- static → static, two gateways, plaintext;
- registered mobile → hosted mobile, two gateways, INTRO (`TYPE 14 → TYPE 15`);
- sealed relay (`TYPE 17`) across two gateways;
- `-a` forward delivery and reversed ACK through both gateways;
- reverse direction from the destination mobile;
- 16-entry native chain: cursor visits every entry exactly once, no intermediate H, final delivery;
- apply the owner-selected B60-O1 policy at 16 entries: either the reversed ACK returns within its
  approved resource policy, or `-a` is synchronously refused before airtime.

### 9.5 Regression gates

- existing one-gateway cross-layer native tests remain green and byte-identical. This expectation is
  structurally grounded: S1 changes host capacity/carriers, and S2's `cur == n_layers - 1` case
  retains the existing final-recipient branch;
- existing same-layer and team-plane tests remain unchanged;
- `pio test -e native`;
- all firmware build environments used by the current project gate;
- pin that `XlHandoffTarget` occupies offset-3 padding and does not grow `XlHandoff`;
- inspect stack-usage output and record runtime stack high-water on gateway and mobile/static metal;
- update the permanent native `sizeof(Node)` assertion only if another measured change requires
  it, with exact arithmetic;
- record per-board RAM, flash and stack deltas;
- run the full simulation corpus. Expect existing scenario streams to remain byte-identical because
  their simulator grammar supplies only one destination layer; attribute any movement rather than
  re-anchor wholesale.

B16 remains separate: this slice does not need to reconcile all simulator-vs-metal command grammar
differences. The 16-entry end-to-end chain may use a native typed-command harness. If the simulator
parser is extended to accept lists for the new scenario, record only that narrow dependency and do
not claim B16 closed.

## 10. Implementation slices

### S1 — capacity and truthful contracts

- delete `gw_env_max_hops`; introduce the 16/15 constants and shared adjacent-transition predicate;
- widen command, codec, reverse-path and wrapper arrays;
- make long command results truthful;
- add codec/parser/result boundary tests;
- add carrier-size, compiler stack-usage and per-board memory gates;
- record B60-O1 without changing ACK admission or deadlines.

Gate: no routing behavior change yet; every existing one-hop path remains byte-identical.

### S2 — intermediate gateway selection

- classify each handoff as `next_gateway` or `final_recipient`;
- skip final hash resolution on intermediate leaves;
- select the next gateway from the active intermediate leaf;
- add no-gateway wait/giveup telemetry;
- land the two-gateway reproducer and poison controls.

Gate: B60 trace shape changes from leaf-5 H floods to a DATA leg addressed to the 5/6 gateway.

### S3 — mobile, sealed and reverse-path closure

- exercise registered-mobile delegation and final hosted-mobile last mile;
- preserve INTRO and SEALED_RELAY types;
- drive reversed E2E ACK through the same two gateways;
- implement the owner-selected B60-O1 ACK policy and verify counter translation.

### S4 — maximum-depth and firmware gate

- drive the 16-entry native chain and apply the owner-selected B60-O1 behavior to a requested reversed
  ACK;
- run native, firmware builds, RAM/flash measurements and full corpus;
- update bench/release notes with the coordinated-upgrade requirement;
- mark B60 fixed only after the metal `7 → 5 → 6` case delivers.

## 11. Acceptance criteria

B60 closes only when all of the following are true:

1. `send_layer 0x7B18ADA2 5,6 ...` sends no H query on leaf 5.
2. The leaf-5 relay leg is addressed to the gateway serving leaf 6.
3. Hash/mobile-home resolution occurs only on leaf 6.
4. The original destination mobile receives plaintext and sealed variants.
5. A requested E2E ACK returns over `[6,5,7]` and clears the original send handle.
6. A complete 16-entry path is accepted; a seventeenth entry is refused before airtime.
7. Adjacent same-nibble transitions are refused, while non-adjacent repeated nibbles traverse in
   their local layer contexts.
8. Long paths are reported without truncating the send handle.
9. Missing intermediate gateways fail boundedly without H-flooding the wrong layer.
10. B59 routing/custody behavior and ordinary routing gates are untouched.
11. The owner-selected B60-O1 ACK policy is implemented without silent deadline/ring tradeoffs.

