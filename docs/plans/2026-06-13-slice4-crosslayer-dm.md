# Slice 4 — Cross-layer DM (send / bridge / ack) — decomposition

**Date:** 2026-06-13 · **Status:** signed off; implementing. Decomposed via a workflow (6 parallel subsystem
readers → synthesis → adversarial critic), cross-checked against the LOCKED design spec
`docs/superpowers/specs/2026-06-12-gateway-dual-layer-design.md` (§5 is the authority), then user-signed-off.

Slice 4 ACTIVATES the reserved `DATA_FLAG_CROSS_LAYER` (0x40) so a gateway routes DMs between its two layers.
Slices 0–3e are DONE (config/validation, LayerRuntime, activate_layer + window scheduler, per-leaf beacon,
schedule advertise/learn + `gateway_schedule_defer_ms`). Each sub-slice below: native-tested + 3-boards-green
+ quality-gated, smallest reviewable step.

## Sub-slices (ordered; ★ = keystone)

- **4a′ (S) — Push/inbox `layer_id` field. DONE 2026-06-13.** Spec §2/Q13 prereq: the `Push` POD + inbox
  record + the live-push / `inbox_dm` / `inbox_channel` JSON carry the FULL 8-bit receiving `layer_id` (origin
  aliases across a gateway's two leaves). `active_layer_id()` = `_cfg.layers[_active-&_layers[0]].layer_id`
  (single-layer = `leaf_id`). Record format 24→25 B; device meta `kVersion` 1→2. NO migration (dev hardware;
  the QSPI records backend isn't live anyway → inbox disabled on device). native 325/325; s18 md5-identical
  (single-layer no-op; the sim's `drainPushes` doesn't surface `layer_id` — deferred to Slice 5); 3 boards
  green (RAM unchanged). The sim FirmwareNode push telemetry `layer_id` is a Slice-5 add (would break the s18
  baseline now + is redundant for single-layer).
- **4a (S) — wire `gateway_schedule_defer_ms` into the send (the deferred 3e.2b).** In `become_free`/`issue_send`
  when the picked NEXT-HOP is a learned gateway, defer the RTS to its window (`next_attempt_ms = max(existing,
  now+defer)` — COMPOSE, never clobber). Key on next-hop, not dst. Same-layer timing only; no cross-layer wire.
- **4b (M) — layer-path inner codec (§5).** Extend `data_unicast_inner` (`n_layers/cur/layer_ids[4]`); parse the
  CROSS_LAYER branch BETWEEN dst_hash and origin — FAIL LOUD (nullopt) on n_layers 0/>4, cur≥n_layers, short.
  Shared `pack_unicast_inner` proven byte-identical for the same-layer path. Inner order
  `[dst_hash][layer-path][origin][source_hash][body]`.
- **4c.1 (L) ★ KEYSTONE — bridge fork in the DELIVER branch.** Spec §5 step 1: a cross-layer DM is addressed
  TO the gateway → `is_forward==false` → DELIVER branch (beside `l2c_handle_misdelivery`), gated
  `dst_key_hash32 != _key_hash32`, ordered AFTER the deliver-to-me / E2E-ack-for-me tests. Resolve dst on the
  TARGET leaf's `_id_bind` (explicitly `_layers[k]`, never `_active->`), ADVANCE cur (preserve path), buffer a
  compact full-body `XlHandoff` (Node-global, beside `_gw_schedules`, cap 16, refuse-when-full LOUD), drain on
  `activate_layer(target)`. **SEED the far leaf's `_seen_origins` for (origin,new_dst,ctr) at inject** (loop
  suppression — critic gap). A malformed CROSS_LAYER inner is REFUSED, never raw-fallback-delivered.
- **4c.2 (M) — gw_relay anti-spam exemption + `RTS_FLAG_RELAY` threading.** Across the 4 rebuild sites
  (PostAck→TxItem→PendingTx→cascade_requeue + become_free/issue_send/tx_rts_retry). Peeled out: the
  field-threading HIGH-bug surface.
- **4d (L) — `send_layer` origination (§5.1).** Resolve dst-layer (same-layer short-circuit), select a PEER
  `is_gateway` route, pack `[A,B] cur=1` + SOURCE_HASH (required for the ack), 228-B fit-check, defer to G's
  window (4a). **Park + reactive `ROUTE_QUERY`** when a gateway is KNOWN but routeless (user 2026-06-13, the
  Lua Pass-2; explicit recovery, NOT a silent fallback; still `err_no_gateway` on TTL); genuinely-no-gateway →
  `err_no_gateway` (spec §5.2). Small dedicated cross-layer park buffer (compact, cap 8).
- **4e (M) — reversed-path cross-layer E2E ack at Y.** Path reversed / cur reset; dst = the inbound
  SOURCE_HASH (FAIL LOUD if absent — never degrade to acking pa.origin on the local leaf); bridged by 4c.1.
  Net-new (the Lua has no cross-layer ack — intended §0.10 divergence).
- **4f (M) — unknown far-leaf binding.** H-flood on the target leaf + defer the handoff + a NEW named
  `gateway_handoff_defer_ttl_ms` → LOUD drop + `send_failed` ALWAYS (tightens spec §5.2's "(optional)").
  Reflood throttle ~one visit period (Lua ~15s-not-5s).
- **4g (M) — integration + parity.** First 2-node `X@A→G→Y@B` end-to-end + s09/s10 sim parity (non-zero
  cross-layer DM delivery + the channel leak-gate stays CLOSED + a gossip-NOT-forwarded white-box). Also: the
  sim FirmwareNode push `layer_id` telemetry (deferred from 4a′).

## Decisions / divergences (cross-checked vs §5)

- **Bridge = DELIVER branch** (confirms §5 step 1 "addressed to it"); the workflow's first draft (forward
  branch) was wrong — the critic + spec agree.
- **Cursor over a PRESERVED path, advance-not-pop** (§0.10) — the destination reverses the whole path for a
  STANDARD E2E ack. Do NOT port the Lua pop-and-rewrap.
- **Symmetric, layer-key-everything (full 8-bit id), DATA_FLAG_CROSS_LAYER (no gw_env), validate-not-fill** —
  the ~30 Lua `…or home/…or 0` fallbacks are deleted.
- **Gateway-route recovery = park + reactive ROUTE_QUERY** (user; extends §5.2 for the routeless sub-case).
- **Handoff buffer = cap 16 compact full-body (≤228 B)** (user); `cap_gateway_deferred_handoffs=32` stays the
  TTL/policy reference, NOT the array size. A 64-B body cap was REJECTED (would refuse valid DMs).
- **Giveup is LOUD ALWAYS** (tightens §5.2's "(optional) send_failed").

## RAM (gateway build 64.3% today)

Two new SMALL static buffers: the 4c.1 `XlHandoff` (cap 16, full ≤228-B body, ~3.7 KB) Node-global beside
`_gw_schedules`; the 4d cross-layer park (compact, cap 8). The layer-path costs BODY headroom (inside the
241-B inner), not buffer size; only ~7 B threads onto TxItem/PostAck/PendingTx (×kTxQueueCap + scratch —
MEASURE sizeof(TxItem) after 4c.1). Hard gate < 100% (ideally < 90%).
