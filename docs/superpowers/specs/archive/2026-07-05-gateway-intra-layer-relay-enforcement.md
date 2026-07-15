<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Gateway intra-layer-relay enforcement (two-sided) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-05). Implement as written. The user commits + flashes; I quality-gate. **No wire change, no NV bump** (the config flag is live-only like `nav_enabled`). ⚠ **Order:** land AFTER the membership carve-outs (`2026-07-05-gateway-leaf-config-membership-exemption.md`) + the `l1_beacon_period_ms=UINT32_MAX` fix, so the lus baseline is clean.

## Why
The design (`2026-06-12-gateway-dual-layer-design.md` §3 line 12, §6 lines 120/124) says a gateway **does NOT relay other nodes' same-leaf traffic** — its job is *cross-layer* bridging, and its time-multiplexed window makes it a poor intra-leaf relay. The spec left `intra_layer_relay=false` as **"[Reserved, not implemented.]"** — and it is: **verified** there is no gateway gate in the DM-forward path (node_mac_rx.cpp:717 forward branch) nor in next-hop selection (node_cascade.cpp:48). So a gateway currently relays intra-leaf DMs, and nodes route through it (the metal `[route] dest=214 next=3(gw)`). This spec implements the deferred rule, **two-sided** (both halves must land together): the gateway **drops** intra-leaf forwards, and senders **never route through** a gateway (+ rediscover).

**★ Cross-layer safety (the key invariant — verified):** routing *to* a gateway (`enqueue_cross_layer` sets `item.dst = gw_node`; the cross-layer egress) MUST keep working. Both halves preserve it: the gateway-side drop only fires on a *forward* (MAC `dst != _node_id`) — a cross-layer transit DM is addressed *to* the gateway (`dst == _node_id` → the deliver/**bridge** branch, never reaches the forward branch); the sender-side gate exempts `next_hop == pt.dst`. So the gateway's core job is untouched.

## Edit 1 — the config flag (`lib/core/node.h`, `src/fw_main.cpp`)
**`node.h`** — add to `NodeConfig`, next to `nav_enabled` (~:180):
```cpp
bool     intra_layer_relay = false;   // §gateway: relay OTHER nodes' same-leaf DMs? default OFF — a gateway is a
                                      // cross-layer bridge, not an intra-leaf relay (design 2026-06-12 §6). Live-only
                                      // like nav_enabled (an opt-in; reverts to OFF on reboot — the default IS the fix).
```
**`fw_main.cpp`** — `cfg set` arm next to `nav` (~:543), LIVE-only (`persist = false`), matching `nav`:
```cpp
else if (!strcmp(key, "intra_layer_relay")) { lc.intra_layer_relay = (atoi(val) != 0 || !strcmp(val, "on")); persist = false; }
```
Also: show it in `dump_cfg` (the `proto :` line, next to `nav=`): `mrcon.print(F(" intra_relay=")); mrcon.print(c.intra_layer_relay ? 1 : 0);` and add `intra_layer_relay` to the `cfg keys:` help line.

## Edit 2 — gateway-side DROP (`lib/core/node_mac_rx.cpp`, the `do_post_ack` forward branch ~:717)
At the **top** of the `} else {` forward branch (before the hash-bind snoop at :718), add:
```cpp
} else {
    // §intra-layer-relay (2026-07-05): a GATEWAY does NOT relay other nodes' same-leaf traffic by default (design §6).
    // A cross-layer transit DM is addressed TO the gateway (dst==_node_id -> the deliver/BRIDGE branch above), so ANY
    // forward (dst!=_node_id) reaching HERE on a gateway is an intra-leaf relay -> DROP (unless the operator opted in).
    // The cross-layer bridge (is_gw_relay re-inject via drain_xl_handoffs) is a SEPARATE originated-TX path, NOT this
    // received-DATA forward, so it is unaffected. Belt-and-suspenders to the sender-side next_hop_selectable gate.
    if (_cfg.is_gateway && !_cfg.intra_layer_relay) {
        MR_EMIT("gateway_intra_relay_drop", EF_I("dst", pa.dst), EF_I("origin", pa.origin));
        become_free();
        return;
    }
    // ... existing: hash-bind snoop + the forward TxItem build + enqueue (UNCHANGED) ...
}
```
(Rationale for drop-before-snoop: a gateway that won't relay shouldn't be on the transit path at all — the sender gate keeps it off — so the snoop is moot; dropping first is the honest "we ignore transit." If passive binding-learning is later wanted, move the drop to after the snoop lines only.)

## Edit 3 — sender-side EXCLUSION (`lib/core/node_cascade.cpp`, `next_hop_selectable` ~:48-60)
Add, after the existing gates (after the `route_uses_mobile_as_transit` line, before `return true;`):
```cpp
    // §intra-layer-relay (2026-07-05): NEVER route THROUGH a gateway — it won't relay intra-leaf traffic (Edit 2 +
    // design §6). Recognize a gateway via is_gateway_dest() — it checks _gw_schedules + _bridged_layers, populated
    // from the gateway's self_gateway beacon + schedule/TLV, so it is the LEARNED gateway role (independent of the
    // RtCandidate.is_gateway flag that learn_route_via zeroes on RREQ/RREP routes = the metal gw=0). ALLOW
    // next_hop==pt.dst: routing TO the gateway (a cross-layer egress — enqueue_cross_layer sets dst=gw_node — or a DM
    // to the gateway itself) is legitimate. Reject only TRANSIT (a gateway next-hop that isn't the destination).
    if (is_gateway_dest(c.next_hop) && c.next_hop != pt.dst) return false;
```
**⚠ Recognition is `is_gateway_dest`, NOT the reserved id range 1..16 — the id range was REJECTED.** It was the first instinct (the reservation is enforced at DAD/provisioning), but it is WRONG for the test/sim world: **s18 (the byte-identical keystone) + s09/s16 + ~41 native tests use node ids 1..16 for NORMAL nodes** (tests/sim set ids directly, bypassing the DAD reservation). An id-range gate excludes those low-id normal relays → **s18 delivery tanks + 41 native tests fail** (the symptom that surfaced this). `is_gateway_dest` returns `false` for a normal node (no schedule/TLV entry) → **s18 stays byte-identical, the 41 tests pass with their ORIGINAL ids (do NOT remap them), no sim churn.** The rare unlearned-gateway case (a gateway known only via an RREQ route, whose beacon we haven't heard → `is_gateway_dest` false) is backstopped by the gateway-side DROP (Edit 2) + the sender's rediscover (Edit 4): the sender routes through, the gateway drops it, the sender rediscovers. So Edit 2 covers Edit 3's blind spot — no delivery black hole. `is_gateway_dest` is `const` (node_routing.cpp:140), callable from the `const` `next_hop_selectable`.

## Edit 4 — rediscover: NOTHING TO ADD (leverage the existing path)
When Edit 3 rejects every candidate, `pick_next_cascade_hop` returns **0**, and the **existing** `node_mac.cpp:525-545` handles it: an **originator** → `defer_send(item)` → `emit_route_request` (RREQ, ttl=1 then escalating to `dv_hop_cap`) = **the rediscover you asked for**; a **forwarder** → drops (it can't hold others' transit — unchanged; the originator rediscovers). So the "no other route → rediscover" behavior is automatic. **Verify** this in the test (below), don't add code.

## Tests
**Native (`test/test_dual_layer.cpp` + `test/test_node_r3.cpp`):**
- **Edit 2 (gateway drop):** a gateway (`n_layers==2`, `intra_layer_relay=false`) drives `do_post_ack` with a `PostAck{is_forward=true, dst=<a third-party same-leaf id>}` → assert **no forward TxItem enqueued** + `gateway_intra_relay_drop` emitted; flip `intra_layer_relay=true` → assert it **does** forward. A **cross-layer** transit (`dst==gateway`, CROSS_LAYER) still **bridges** (not dropped) — proves the branch discrimination.
- **Edit 3 (sender exclusion):** `next_hop_selectable` — with a **known gateway** next-hop (`is_gateway_dest` true via a seeded `_gw_schedules`/`_bridged_layers` entry): **rejects** when `next_hop != pt.dst`, **accepts** when `next_hop == pt.dst` (the cross-layer egress). With a **normal** next-hop (no schedule/TLV — INCLUDING a low id 1..16): **accepts** — the regression guard proving there is no id-range misfire (this is what keeps s18 + the 41 tests green with their original ids).
- **Edit 4 (rediscover):** an ORIGINATOR whose only route to a dest is via a gateway → `pick_next_cascade_hop` returns 0 → assert `defer_send` + `emit_route_request` fires (the DM isn't silently dropped).

**★ lus suite (the delivery-neutrality GATE — run the FULL cross-layer suite, per the user):**
- **s09 / s10 / s15 / s16** (cross-layer): X→G→Y cross-layer delivery must **NOT regress** (that's routing *to* G — allowed). A gateway must show **0 intra-leaf forwards** (grep `gateway_intra_relay_drop` ≥ 0, and no gateway relaying a third-party same-leaf DM). Multi-gw transit (s15) still works.
- **s18** (single-layer): must stay **delivery-neutral / byte-identical** — the gate is id-range (1..16) so it only bites when a route's next-hop is a reserved gateway id, absent in a normal single-layer scenario; confirm no same-leaf delivery drop.
- ⚠ **The watch-item:** a topology where a same-leaf node is reachable *only* via a gateway now fails (rediscover finds nothing → `send_failed`). In a healthy mesh same-leaf nodes relay for each other, so this shouldn't happen — the suite CONFIRMS it. If a scenario regresses, surface it (don't assume neutral).
- Boards + **gateway env** build.

## Sites
`lib/core/node.h` (NodeConfig flag) · `src/fw_main.cpp` (cfg set + dump + help) · `lib/core/node_mac_rx.cpp` (Edit 2, do_post_ack forward branch ~:717) · `lib/core/node_cascade.cpp` (Edit 3, next_hop_selectable ~:48) · `test/test_dual_layer.cpp` + `test/test_node_r3.cpp` (tests). **No wire, no NV.**
