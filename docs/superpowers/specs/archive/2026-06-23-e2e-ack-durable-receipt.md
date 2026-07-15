<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# E2E-ack durable receipt — record it + surface it (not telemetry-only)

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Firmware change; **no wire change**.

## Why

A `-a` DM's end-to-end ack is the only true "the dest got it" signal, but today it is **invisible on metal**: when the origin receives the `DATA_TYPE_E2E_ACK` (node_mac_rx.cpp:561) it does nothing but `_hal.emit("e2e_ack_rx")` — `MR_TELEMETRY`, compiled out under `NO_TELEMETRY`. So neither the stress harness nor the iOS companion can confirm delivery; the harness has been forced to read the hop-ack (`ACKED`, which is `deliv✓` only by the inbox, not the ack — we saw `239→170 ack✓ deliv✗`). The fix: **record the ack as a durable receipt the companion matches by id, and emit a live push** for the connected/harness case.

**Decisions (user, 2026-06-23):** the receipt lives in the **DM store** (an E2E ack *is* a DATA frame) under the **DM seq-cursor** — no third seq-space — distinguished by a new **`type` byte** (option (a); a one-time inbox-store-version wipe is accepted). QSPI is live (`inbox_dm_store_bytes = 512 KB`, durable, epoch/seq survives a wipe), so receipts persist across reboot and capacity is ample.

## Verified current state

- **Receipt data is in hand at receipt** (node_mac_rx.cpp:561-570): `acked` (the original ctr, extracted — branches same-layer vs cross-layer) + `pa.origin` (the dest that confirmed). Just discarded.
- **InboxEntry** (inbox.h:26): `seq · kind · origin · channel_id · msg_id · sender_hash · layer_id · enc · body_len · body`; `InboxKind { dm=0, channel=1 }`; 26-B header. `record_dm(...)` / `record_channel(...)`; a record-format change **bumps the device-store version → old records rejected** (per the §8b `+enc` precedent).
- **Live pushes** (fw_main.cpp:1561-1573): `RECV` / `CH` / `ACKED ctr=` / `FAILED ctr=` from the `PushKind` enum.
- **`pull_inbox` NDJSON**: `inbox_dm {seq,origin,layer_id,ctr,sender_hash,rx_ms,enc,body}` then channel block then `inbox_end`. Schema: `ios-companion/INBOX_SYNC_CONTRACT.md`.

## The change

1. **`InboxEntry` + record format — add `type`.** Add `uint8_t type;` (the `DATA_TYPE`: `0` = normal app DM, `DATA_TYPE_E2E_ACK` = a receipt; room for `H_ANSWER` etc. later). Insert it into the serialized header (27-B now) in `inbox.h` + `inbox.cpp` (the (de)serialize and the private `record(...)`). **Bump the device-store record version** so a v(old) QSPI record is rejected — the epoch model makes the companion re-pull cleanly. ⚠ This is a **one-time wipe of the persisted inbox** on the upgrade flash (accepted).
2. **`record_ack` method** (inbox.h/.cpp): `uint32_t record_ack(uint8_t from_origin, uint16_t acked_ctr, uint8_t layer_id, uint64_t now_ms, uint32_t acker_hash = 0)` → a **DM-store** entry: `kind=dm`, `type=DATA_TYPE_E2E_ACK`, `origin=from_origin` (the dest that confirmed), `msg_id=acked_ctr`, `sender_hash=acker_hash`, `body_len=0`, `enc=0`. Normal `record_dm` passes `type=0`.
3. **Wire it at the receipt** (node_mac_rx.cpp:561, replacing the telemetry-only block — **keep** the `e2e_ack_rx` `MR_TELEMETRY` for the sim analyzer, it's free on metal):
   - `inbox().record_ack(pa.origin, acked, /*layer_id=*/<the receiving layer>, _hal.now(), /*acker_hash=*/<source_hash if cross-layer, else 0>);`
   - `Push pu{}; pu.kind = PushKind::send_e2e_acked; pu.dst = pa.origin; pu.ctr = acked; enqueue_push(pu);`
   - Handles **both** same-layer and cross-layer acks (the `acked` extraction already branches). For cross-layer, the dest's `source_hash` is available — store it in `acker_hash` (the cross-layer 8-bit `origin` aliases across leaves; the hash is the stable match). Same-layer: `(origin, ctr)` suffices, `acker_hash=0`.
4. **Live push `PushKind::send_e2e_acked`** (node.h enum) + the fw_main.cpp push-loop case: `Serial.print(F("E2E-ACKED ctr=")); Serial.print(pu.ctr); Serial.print(F(" from=")); Serial.println(pu.dst);` → console line **`E2E-ACKED ctr=<X> from=<D>`** (distinct from the hop-ack `ACKED ctr=<X>`).
5. **`pull_inbox` NDJSON — `type` on `inbox_dm`** (console_json `write_inbox_dm`): add `"type"` — `0`/omitted for a normal DM, `"e2e_ack"` for a receipt. A receipt streams as `{"ev":"inbox_dm","type":"e2e_ack","seq":N,"origin":D,"ctr":<acked_ctr>,"sender_hash":H,"rx_ms":T,"body":""}`. **No new block, no new `pull_inbox` arg, no third cursor** — receipts ride the DM cursor.
6. **`INBOX_SYNC_CONTRACT.md` delta:** document the `type` field; the receipt semantics — for `type:"e2e_ack"`, `origin` = the node that CONFIRMED delivery (the original `-a` DM's dest), `ctr` = the acked ctr, so the companion matches **`(origin, ctr)`** (or `(sender_hash, ctr)` when present, for cross-layer) to its **outbox** → marks that sent message delivered; and the live `E2E-ACKED ctr=X from=D` push (non-durable fast path).

## Companion (NOTE — not built here)

The iOS companion must learn the new `type`: `inbox_dm[type=e2e_ack]` → a receipt → match to the outbox and mark delivered (don't render as a received message); `inbox_dm[type=0]` → a received message, as today. Flag for the companion update (the contract change rides the same `pull_inbox`, so an un-updated companion would mis-show receipts as empty-body DMs — coordinate the rollout).

## Not a wire change · no sim impact

The E2E-ack **frame** is unchanged (the dest still sends `DATA_TYPE_E2E_ACK` with `acked_ctr`); only the origin's *handling* changes. `wire_version` untouched. In the sim the inbox has no store installed, so `record_ack` is inert and the `e2e_ack_rx` telemetry is preserved → the BASELINE suite + `dm_delivery_breakdown` are unaffected (verify s18 unchanged).

## Tests / gate

- **Native unit** (fake HAL + a `RamInboxStore`): originate a `-a` DM, feed an inbound `DATA_TYPE_E2E_ACK` → assert (a) a DM-store record exists with `type=E2E_ACK`, `origin=acker`, `msg_id=acked_ctr`, `body_len=0`; (b) a `send_e2e_acked` push enqueued with `ctr=acked_ctr`, `dst=acker`; (c) the record (de)serializes round-trip with the new `type` byte; (d) a normal DM still records `type=0`. Cross-layer variant: `acker_hash` stored.
- **JSON unit** (`test_console_json`): a receipt → `write_inbox_dm` emits `"type":"e2e_ack"` + `origin` + `ctr`; a normal DM omits/zeros `type`.
- **Store-version**: a v(old) record is rejected (existing version-mismatch path); the epoch bumps so the companion re-pulls.
- Full native suite green · `pio run -e gateway -e xiao_sx1262 -e heltec_v3` · **BASELINE suite unchanged** (record_ack inert in sim).
- **Metal (user bench):** the harness sees `E2E-ACKED ctr=X from=D` live, and the receipt appears in `pull_inbox` as `type:"e2e_ack"`. ⚠ The persisted QSPI inbox is wiped once on this flash (accepted) — the companion re-syncs via the new epoch.
