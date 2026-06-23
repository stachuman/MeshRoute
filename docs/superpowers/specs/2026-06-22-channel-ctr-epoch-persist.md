<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Channel send-ctr persistence — stop reboot id-reuse (the "ctr-epoch" fix)

**Status:** coder instruction. The user does ALL commits — land GREEN + uncommitted, report ready, I gate. Bench-confirmed root cause (2026-06-22 metal).

## Why

On the 4-node bench, a channel flood that *should* work is **dedup-dropped as a duplicate**: `flood FECC2901 already-buffered` / `flood FECC2902 already-buffered`. Root cause, confirmed by the new flood trace:

`channel_msg_id = origin<<24 | (key_hash32 & 0xffff)<<8 | ctr` (node_channel.cpp:28). The ctr comes from `next_ctr(_node_id)` (do_send_channel, node_channel.cpp:258) — the **self-keyed** entry of `std::map<uint8_t,uint16_t> _peer_send_counter` (node.h:1089). That map is **RAM-only**, so on reboot it resets to empty → the ctr restarts at 0 → a rebooted origin **re-mints ids it already used** (`FECC29xx` with a small ctr), and any node still holding the old copy drops the "new" message as a duplicate. (Seen live: 254 even pulled its *own* prior message back from 222 after a reboot.)

**The ctr width is NOT the problem — the reset is.** `cap_channel_buffer = 32` (protocol_constants.h:217), and the 8-bit on-wire ctr wraps at 256 ≫ 32 (per-origin retention in a 4-node net is ~8). So with the counter merely *continued* across reboot (not reset), an id is always evicted long before the ctr cycles back to it. ⇒ **no id-format change, no wire change** — just persist + restore + continue.

## What to persist

**Only the self-keyed channel ctr** — `_peer_send_counter[_node_id]` (one `uint16`). The other per-dst entries are DM `ctr_lo` sequences for short-lived, acked flights; a reboot drops all pending flights anyway, so their reset is benign. (If you later want DM-ctr continuity too, persist the whole map — out of scope here; the channel ctr is the demonstrated bug.)

## Design — mirror the existing `claim_epoch` persistence

`claim_epoch` already does exactly this shape: a field in the `device_nv` Blob, restored on boot via a Node setter (`restore_join_state`, node.h:485), with a `fw_main` change-detect writer (`g_persist_epoch`, fw_main.cpp:115). Follow it:

1. **NV field.** Add `uint16_t channel_ctr;` to the `device_nv` Blob (next to `claim_epoch`, device_nv.h:23) and **bump `kVersion` 14 → 15** with the standard zero-default migration (an old v14 record → `channel_ctr = 0`). ⚠ This is the **NV-layout** version (local flash), **NOT `wire_version`** — nothing on the air changes.
2. **Node accessors** (node.h, host-testable):
   - `uint16_t channel_ctr() const { return self-keyed _peer_send_counter value, 0 if absent; }`
   - `void restore_channel_ctr(uint16_t v) { _peer_send_counter[_node_id] = v; }` (boot reload).
3. **Boot restore** (`fw_main`, the same place `restore_join_state` is called from NV): after loading the Blob, `g_node.restore_channel_ctr(nv.channel_ctr);` so the first post-boot `next_ctr(_node_id)` continues from the persisted value.
4. **Persist on change** (`fw_main`, mirroring `g_persist_id/epoch`): keep a `static uint16_t g_persist_channel_ctr;`; after a `send_channel` (and anywhere `channel_ctr()` can advance), if `g_node.channel_ctr() != g_persist_channel_ctr`, write the Blob and update the shadow. Channel sends are infrequent user/app actions, so a write-per-send is fine (same cadence class as a DAD-lease write). *(If you expect a chatty channel, a reserve-block — persist `base+N`, hand out from RAM, repersist on crossing — avoids per-send writes at the cost of burning ctr values; NOT needed for the bench and skippable unless traffic warrants. Flag it, don't silently choose it.)*

## Not a Lua divergence

The Lua model never reboots (sim nodes are long-lived), so it has no persisted ctr — and the `lus` engine is unaffected (no reboot path). The on-wire id format is **identical**; this only changes how a *metal* node seeds its ctr after a power cycle. So `-e meshroute` sim behavior and the BASELINE suite are untouched (verify: suite unchanged).

## Tests

- **Native unit** (fake NV / a `Node` reused as "rebooted"): send a channel message → `channel_ctr()` advances to K; capture the minted id. Simulate reboot: a fresh `Node`, `restore_channel_ctr(K)`, send again → assert the new id's ctr is **K+1, not 0**, and `!=` the pre-reboot id. Negative control: **without** `restore_channel_ctr`, the fresh node re-mints ctr 0 (== the bug). 
- **Native unit:** `restore_channel_ctr` round-trips through the NV Blob struct (pack/unpack v15; a v14 blob migrates to `channel_ctr = 0`).
- Full native suite green; `pio run -e gateway -e xiao_sx1262 -e heltec_v3` build SUCCESS; BASELINE suite unchanged (the fix is metal-only).

## Gate / bench-confirm

- native + 3 boards + suite-unchanged.
- **Metal:** reboot 254, `send_channel 0 "x"`, confirm the id is **not** `already-buffered` at 222 and the flood proceeds (the trace from `2026-06-22-flood-decision-debug-trace.md` shows it). The id's ctr should be the *continued* value, not 1.
- Leave GREEN + uncommitted.
