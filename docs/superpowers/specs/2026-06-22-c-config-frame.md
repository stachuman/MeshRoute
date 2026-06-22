<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# `C` config frame — control-plane leaf-config delivery (replaces CONFIG_ANSWER)

**Status:** DESIGN DECIDED (author 2026-06-22), READY FOR CODER. I quality-gate; author commits.

## 1. Why (confirmed bootstrap bug)
A joiner has `allowed_sf_bitmap == 0` (no sf_list yet). The old `CONFIG_ANSWER` is a **routed DATA** frame (`enqueue_data(… DATA_TYPE_CONFIG_ANSWER …)`), but the RTS receive path bails at `node_mac_rx.cpp:48: if (_cfg.allowed_sf_bitmap == 0) return;` and `select_data_sf` returns 0 on an empty bitmap → **the joiner can't complete the data handshake → never receives its sf_list.** Chicken-and-egg. (The sim masked it: the parity translator pre-fills the joiner's sf_list.) Fix: deliver the config on the **control plane** (`routing_sf`, which the joiner always knows).

## 2. The `C` frame (cmd nibble `0xB`)
A control-plane frame, **1-hop direct** to the puller on `routing_sf` — the answer to the existing `CONFIG_PULL` (`q_opcode::config_pull=2`, unchanged). Header like the other control frames: `[byte0: 0xB<<4 | leaf_id][byte1: src][byte2: dst]` then the body.

**Body (LE):**
```
sf_list   : u8    (bitmap, bit i = SF(5+i); SF5..SF12 — only 8 SFs are valid)
duty_bp   : u16   (duty in 0.01% units / basis points: 0..10000 = 0%..100%; 0.1% = 10)
config_epoch : u16 (LWW config version)
name_len  : u8    (0..10)
name      : name_len bytes (<= 10; truncated if the leaf name is longer)
```
≤ 16 B body. Add `mr_cmd_name(0xB) -> "C"` in `frame_trace.h`.

**`config_epoch` IS in the body** (author added 2026-06-22) — so the adopt is **race-free under LWW**: the joiner tags the config with the answerer's exact epoch, not the maybe-stale epoch of whatever beacon it happened to hear. **`lineage_id` is NOT** in the body — it's the stable leaf identity, taken from the beacon (the joiner heard one on its leaf_id to trigger the pull). `config_hash` (= hash of `sf_bitmap‖duty‖name`) is epoch-independent, so §5 still governs the no-re-pull invariant.

## 3. Remove CONFIG_ANSWER
- Delete `DATA_TYPE_CONFIG_ANSWER` (TYPE 6 — free the type), `Node::send_config_answer`, `Node::adopt_config_answer`, and the data-RX TYPE-6 dispatch.
- `handle_q`'s `config_pull` case → call **`send_c_config(q.src)`** (was `send_config_answer`).
- Repurpose the `leaf_config.cpp` codec: `pack_config_answer`/`parse_config_answer` → **`pack_c_config`/`parse_c_config`** with the §2 body (drop lineage/epoch from the wire form).
- New **`Node::handle_c(bytes, len, meta)`** + dispatch on cmd `0xB`.

## 4. Adopt (`handle_c`)
Mirror the old `adopt_config_answer` minus the wire lineage/epoch:
- `parse_c_config` → set `_cfg.allowed_sf_bitmap` (unpack the u8 SF5..12 bitmap → internal form), `_cfg.duty_cycle = duty_bp / 10000.0` (+ `recompute_duty_budget()`), `_cfg.leaf_name`/`leaf_name_len` (≤10), **`_cfg.config_epoch` from the C frame** (the LWW version), and `_cfg.lineage_id` from the last-heard beacon for this leaf.
- Mark synced (`config_epoch>0`) → the participation gate lifts; persist to NV; emit the `config_adopted` push (the companion surface stays the same).
- This is exactly the path that was unreachable before: a control-plane frame an empty-sf_list joiner CAN receive.

## 5. ★ CRITICAL — the `config_hash` inputs MUST equal the `C`-frame wire encodings (else re-pull loop)
`leaf_config_hash()` must hash the **exact canonical wire forms** the `C` frame carries — `sf_list[u8] ‖ duty_bp[u16] ‖ name_len ‖ name[≤10]`. If the mother hashes a different representation than a joiner adopts, their `config_hash` diverge → the joiner thinks it's perpetually out of sync → **re-pulls forever.** Three places this bites:
- **Name:** cap at 10 EVERYWHERE — `protocol::leaf_name_max: 16 → 10` (this realizes "cut if longer"); `create`/`leaf name` truncate to 10; the hash **and** the `C` frame both use ≤10. (NV `Blob.leaf_name[16]` array can STAY `[16]` — no NV bump — just ≤10 ever used/hashed.)
- **sf_list:** the hash **and** the `C` frame both use the **u8** SF5..12 bitmap (not the old u16); the joiner unpacks it to the identical internal `allowed_sf_bitmap`.
- **duty:** the hash **and** the `C` frame both use **`duty_bp` (u16, 0.01% units)**, AND `create`/`cfg set duty` **quantize the entered % to the 0.01% step** (`duty_bp = round(pct*100)`), so the mother's duty is *exactly* representable on the wire. Otherwise a finer-than-0.01% duty rounds differently on the joiner → hash mismatch. `_cfg.duty_cycle` (double) = `duty_bp / 10000.0`.

## 6. wire_version — do NOT bump (pre-deployment)
This IS a wire-breaking change (removes CONFIG_ANSWER, adds cmd `0xB`, changes `config_hash`). **But MeshRoute is NOT deployed** — there is no fielded v1 network to stay compatible with, so every node is reflashed together. **Keep `protocol::wire_version` at 1 — do NOT bump.** The bump rule (bump on a wire-breaking change) only bites ONCE DEPLOYED, when a new build must coexist with fielded nodes; defer the first bump to then. Until deployment, wire changes are free (reflash all).

## 7. Tests + gate
- **Native:** `pack_c_config`/`parse_c_config` round-trip (incl. `config_epoch`, the u8 sf_list, a **0.1% duty (`duty_bp=10`)** round-tripping, + name truncation at 10); a duty entered finer than 0.01% quantizes identically on both sides; a managed joiner with `allowed_sf_bitmap==0` receiving a `C` frame → adopts sf_list/duty/name, becomes synced, participation gate lifts; **post-adopt `config_hash` == the source's** (proves §5 — no re-pull); a >10-char name truncates identically on both sides.
- The old CONFIG_ANSWER/TYPE-6 tests are removed; `handle_q` config_pull → emits a `C` (not a DATA).
- Full native suite green; the s22 join scenario no-regress (and a follow-up: the parity translator can stop pre-filling the joiner's sf_list now that the on-air path works — note it, separate task). Both boards build; `wire_version` stays 1 (NOT bumped — §6).

## 8. Build order
1. `leaf_name_max` → 10 (§5) + `pack/parse_c_config` codec (§2) + `frame_trace` "C".
2. `send_c_config` (control-plane, 1-hop to the puller) + `handle_q` config_pull → `send_c_config`.
3. `handle_c` + cmd-`0xB` dispatch + the adopt (§4).
4. Delete CONFIG_ANSWER (TYPE 6 / send_config_answer / adopt_config_answer / data-RX dispatch).
5. Native tests + suite + both boards (`wire_version` stays 1 — §6); hand back green-shaped + uncommitted → I gate.
