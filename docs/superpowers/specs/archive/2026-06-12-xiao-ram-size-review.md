# XIAO nRF52840 RAM/size review (2026-06-12)

**Status:** ANALYSIS ONLY — no code changed. Measured from the `xiao_sx1262` (leaf) `firmware.elf`
built 2026-06-12 20:55 (a mid-gateway-refactor snapshot; numbers will drift as slices land).
**Verdict: flash is fine; RAM is the squeeze — and the big consumers are the §11.1 bounded-state
caps, which are exactly the intended tuning surface (C++-only; the Lua kept them unbounded, so none
of this is a wire change).**

## Measured (leaf build)

| Region | Size | Notes |
|---|---|---|
| Flash `.text`+`.data` | ~301 KB of ~808 KB app region | **37% — not a constraint** |
| RAM total | 256 KB | SoftDevice S140 reserves ~32.4 KB (app base `0x20007E90`) → ~223.6 KB usable |
| `.data` + `.bss` (static) | **204.4 KB (91%)** | |
| `.heap` (the remainder) | **31.1 KB** | also the Bluefruit + LittleFS + malloc arena |

Top static symbols (llvm-nm, bytes):

| Symbol | Size | What |
|---|---|---|
| `g_node` | **148,768** | the Node — 73% of all static RAM |
| `try_drain_deferred()::drained` | 9,984 | function-static = 32 × 312 B — a full TxItem copy-out array, alive forever |
| `g_inbox_dm` + `g_inbox_ch` | 17,440 | interim RAM inbox (32 slots × ~272 B × 2) — dies when QSPI lands |
| `cache_buffer` | 4,096 | LittleFS/InternalFS cache |
| `g_hal` | 3,864 | |
| `try_drain_deferred()::nq` | 2,496 | 8 × 312 B (tx-queue-depth scratch, also permanent) |
| TinyUSB class buffers | ~3,000 | MSC/HID/MIDI/Video/Vendor/**Host** endpoints — unused on this device (CDC only) |
| `Bluefruit` | 1,940 | |
| `s_inbox_jb` + `loop()::jb` | 3,400 | two 1,700 B JSON line-scratch buffers |

## Inside `g_node` (struct arithmetic × caps; confirm on a link map)

| Member | Estimate | Derivation |
|---|---|---|
| `_channel_buffer` | **~27.6 KB** | 128 × ~216 B (200 B payload + meta) |
| `_rt` | **~20.3 KB** | 254 × ~80 B (RtEntry = 3 candidates × 24 B) |
| `_deferred` | ~10.2 KB | 32 × ~320 B (each holds a full 241-B inner) |
| `_per_sender_originator` | **10–30 KB? [VERIFY]** | OrigRing = 64 events × ~16 B ≈ 1 KB per slot — slot count unknown; possibly the #2 member |
| tx queue + pending + post_ack | ~3.5 KB | 8 × 312 + in-flight singletons (241-B inners each) |
| `_id_bind` | ~4 KB | 256 × ~16 B |
| dedup/seen tables | ~10–15 KB | seen_origins 256 · q 128+128 · rreq 64+128 · hash_query 64 · peer state |

## Levers, ranked (leaf build; savings est.)

The knobs marked ⚙ already exist or follow the existing `MR_CAP_*` pattern; the gateway env shows
the precedent (it sets `MR_CAP_CHANNEL_BUFFER=8`, `MR_CAP_DEFERRED_SENDS=16`). Caps are audit-class —
each change needs sign-off + a suite rerun, but none is a wire change.

1. ⚙ **`cap_routes 254 → 64`** (new `MR_CAP_ROUTES` knob; gateway env keeps 254 — "no routing
   sacrifice" is their explicit call): **−15.2 KB**. 254 = the theoretical max leaf; a real >60-node
   leaf can rebuild with the big value.
2. ⚙ **`MR_CAP_CHANNEL_BUFFER 128 → 64` on leafs**: **−13.8 KB**. The FIFO's misses are repaired by
   the digest+pull backstop; 128 was the Lua's unbounded-host default.
3. **`try_drain_deferred` statics (12.5 KB permanent for a transient drain)**: restructure to
   in-place compaction (or share one scratch with other transients): **−~12 KB**. Engineering item,
   not a knob.
4. ⚙ **`MR_CAP_DEFERRED_SENDS 32 → 16` on leafs too**: **−5.1 KB** (and halves `drained` if #3 isn't done).
5. **`_per_sender_originator` slot count [VERIFY on link map]**: if ≥16 slots, capping to 8 is
   **−5…15 KB**. Measure first.
6. **TinyUSB: compile out MSC/HID/MIDI/Video/Vendor/Host (keep CDC)**: **−~3 KB** RAM (+ some flash).
   `CFG_TUD_*` defines via build flags.
7. **LittleFS `cache_buffer` 4096 → 512**: **−3.5 KB**. NV writes are rare; slower IO is fine.
8. ⚙ **`cap_id_bind 256→128`, `cap_seen_origins 256→128`, `cap_q_* 128→64`**: **−~5 KB** combined
   (id_bind/seen are sized for the max leaf, same argument as #1).
9. **Share one JSON scratch** (`s_inbox_jb` + `loop::jb`, same-thread use — verify): **−1.7 KB**.
10. **RAM inbox (17.4 KB)** disappears when the QSPI store lands (its 2×4 KB read-scratch nets
    **−~9 KB**). Don't grow the interim store meanwhile.

**Knob-only total (1+2+4+6+7+8+9): ~−47 KB** → static 204 → ~157 KB, heap 31 → ~78 KB.
**With #3 + #5: up to ~−75 KB.**

## Heap watch

31 KB serves Bluefruit (BANDWIDTH_MAX queues allocate at `begin()`), LittleFS, and any String use.
Print `dbgMemInfo()`/free-heap at boot on the bench; keep ≥16 KB free after BLE init. The knob wins
above triple the margin.

## Process notes (for everyone working on this volume)

- The share **tears files during PC sync**: `node.h` read at ~21:00 showed foreign content + padding
  (size of the new file, content of another) and healed itself at 21:18. Re-stat/re-read before
  trusting a surprising file state, and never "fix" a corrupted-looking file while the PC is active —
  the good copy is on the build PC.
- Build artifacts (`firmware.elf`) vanish mid-analysis during rebuilds — copy them out before analyzing.
- **Suggestion (config, not made):** add `-Wl,-Map=firmware.map` (via `build_flags` link flag) to the
  xiao env like heltec has — makes the next review exact instead of arithmetic.
- Re-measure per gateway slice on the PC: `arm-none-eabi-nm --size-sort --print-size firmware.elf | tail -30`.
