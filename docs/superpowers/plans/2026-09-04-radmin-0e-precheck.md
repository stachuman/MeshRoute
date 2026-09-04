<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0e — Quality-Agent pre-check ledger (2026-09-04): characterization and generated authorities

Authority: design §19 Slice 0e (R-RA-1, R-RA-2, R-RA-3, the R-RA-20 budget inputs), §12, §13, §15, §19.1 row 0e.
Source facts verified at `HEAD c512580`. Hypotheses, not authority: the coder derives every figure. 0e lands NO
production code and NO capacity: it produces INSTRUMENTS and MEASUREMENTS that later slices and two owner rulings consume.

## 0. The four deliverables, and who consumes each

| deliverable | ruling | consumer |
| --- | --- | --- |
| A. the generated command/sub-command inventory, pinned | R-RA-1 | the owner's authority-classification ruling; Slice 6 cannot start without it (design §19 Slice 6) |
| B. candidate value types for every bounded state record, ABI-measured on host/ARM/Xtensa, with RAM headroom and the timer decision | R-RA-2 | the owner's capacity/partition/rate-limit ruling (R-RA-10); Slices 3/4/5/7b/8a |
| C. `remote_body_cap(carrier)` derived from the real packers, at-cap / cap+1 / outer-`CRYPTED` probes | R-RA-3 | Slice 2 (codec) and every carrier slice |
| D. `remote_scheduled_reply_path_budget_ms(cfg)` computed term by term from the named production authorities, with the first default/floor numbers | R-RA-20 | Slice 7a (owns the production function), the owner's acceptance of the concrete default |

## 1. A — the inventory generator (R-RA-1)

| fact | anchor |
| --- | --- |
| the verb map is an if-chain of `len == N && !strncmp(line, "verb", N)` and `(len == N \|\| (len > N && line[N] == ' ')) && !strncmp(...)` forms | `src/firmware_commands.cpp:1187` `dispatch(...)`; 46 `strncmp(line, "` sites; ~27 top-level verbs |
| sub-verb dispatchers with their own string tables | `handle_ui` (`firmware_commands.cpp:189`), `handle_cfg_set` (`firmware_config.cpp:250`), `handle_team` (`:2000`), `handle_mobile` (`:2283`), `admin`/`rcmd` (`firmware_remote.cpp`), `testsend/testch` (`firmware_commands.cpp:1116+`) |
| caller-only arms OUTSIDE `dispatch()` | serial: `send`/`send_channel` handled around `dispatch()` (`src/fw_main.cpp:592`, `:1083`, `:1392`; `firmware_commands.h:107-112` documents the `false` return); BLE: `ble_dispatch_line` in `src/fw_main.cpp` (its `unknown_verb` fallback through a `LineSink` at `:549`, per `firmware_ui_preset_verbs.h:25`) with `version`/`routes`/`peers`/`status` arms (`fw_main.cpp:63-68`) |
| the parser for the three send verbs | `lib/console/console_parse.cpp:258-262` (`send`, `send_channel`, `send_layer` + flags) |
| the idiom to copy | `tools/check_data_type_literals.py` (source-scanning checker with a PASS/FAIL contract and a pinned semantic set) |

Shape: a `tools/gen_command_inventory.py` that parses the three shapes above (top-level if-chain, sub-verb tables,
caller-only arms), emits a deterministic table (verb · sub-verb · owning function · transport(s) · file:line) to a
tracked file (e.g. `docs/superpowers/evidence/…-command-inventory.md` or `tools/command_inventory.json`), and a
`tools/test_gen_command_inventory.py` that (a) regenerates and diffs against the tracked file, (b) fails on an
added/removed verb (a mutation adding a fake `if (len == 3 && !strncmp(line, "zzz", 3))` must redden), (c) counts.
⚠ V1 traps: `help`/`?` share one arm; `rcmd`'s arm is `MR_FEAT_REMOTE_MGMT`-gated (`:1071`); `ui` is `MR_FEAT_OLED`-
gated; the generator must record the gate macro per row (the ABI probe's PINNED-table idiom, `probe_board_abi.py:144`).
The classification column (open/operator/owner/physical) is EMPTY in 0e — it is the owner's ruling.

## 2. B — candidate value types and their measurement (R-RA-2)

| fact | anchor |
| --- | --- |
| the measurement instrument pins `(type, gate-macro)` pairs and compiles a probe TU per target emitting size/alignment symbols; a pinned type that emits nothing is a coverage failure | `tools/probe_board_abi.py:144-160` (PINNED), `:362` `idedata`, `:388` `compile_command`, `:467-479` (the two coverage refusals), `BOARD_TARGETS = (heltec_mobile, gateway)` `:104` |
| the candidates do not exist; they are DESIGN fields | design §15 :1244-1300 (controller: pending request, session/discovery cache, response assembly, retained BLE result, ACK debt; target: seen record, transcript pool, ingress, deferred action) |
| RAM headroom today | `gateway` 195724 B = 83.1 % (pio size line, S5 gate); `heltec_mobile` 205684 B (the coder reads its pio size line) |
| timer wheel | `lib/hal/timer_wheel.h:25` `kCap = 91`; `protocol_constants.h:403` "last free id consumed"; precedent for a SHARED SCAN: `node.h:1528` `kE2eAckDeadlineTimerId = 90` "single one-shot, re-armed" scan |
| the eight-row ring the mobile's `-a` requests share | `node.h:3185` `kDelegAckCap = 8`; `protocol_constants.h:790` 300 s |

Shape: a header of candidate value types compiled ONLY by the probe and the native tests (⛔ not under `lib/core`
— that would be production; put it under `tools/` or `test/` and give `probe_board_abi.py` an extra-pins/include
option, itself covered by the existing coverage refusals). Measure `sizeof`/`alignof` on host/ARM/Xtensa, the
aggregate cost per candidate capacity (N rows × size), the resulting RAM headroom on `gateway`, and the timer
options: (i) raise `kCap` (price: the wheel's storage per id — measure), (ii) one shared scan timer for every
remote-admin expiry (price: scan cost per tick; the `:90` precedent). Report both; the owner rules. ⚠ Nothing lands
in `Node`.

## 3. C — the carrier caps (R-RA-3)

| carrier | inner layout / overhead | anchor |
| --- | --- | --- |
| all | `data_inner_cap()`/`data_frame_len()` (air-fit) vs the 241-B storage bound; plaintext typed = 242 ⇒ 241 governs; `CRYPTED` typed = 238 | `lib/core/frame_codec.h:738-748`, `:730-737` (B20 banner) |
| unicast inner order | `[dst_key_hash32 4 iff DST_HASH][layer-path iff CROSS_LAYER: n:1 cur:1 ids n×1][origin 1][source_hash 4 iff SOURCE_HASH][body]` | `frame_codec.h:1330-1334` |
| mobile → home wrapper (same layer) | `[dst_hash 4][origin 1][source_hash 4][enclosed_type 1][RPC body]` ⇒ 231 body, 206 authenticated command | `node_hashlocate.cpp:1777-1786`; design §8.11 |
| home → target static leg | origin + SOURCE_HASH(4) (+ DST_HASH 4 if by hash) | `node_mac.cpp:200-207` |
| cross-layer variant | + `2 + n_layers` bytes of path | `frame_codec.h:1331`, `gw_env_max_hops` bound (`:33`) |
| target → mobile response (hash-addressed, last mile `addr_len=1`) | DST_HASH 4 + origin 1 + SOURCE_HASH 4; the last mile adds no inner bytes | `node_hashlocate.cpp:1796-1803` (`enqueue_data(..., addr_len=1, ...)`) |

Shape: `remote_body_cap(RemoteCarrier)` as a TEST-side derivation in 0e (the production function is Slice 2's), a
table of every carrier's cap computed by PACKING through the real packers at cap and refusing at cap+1, plus the
outer-`CRYPTED` negative control (cap drops to 238 − overhead). The numbers become Slice 2's KAT inputs.

## 4. D — the activation budget inputs (R-RA-20)

| term | production authority | anchor |
| --- | --- | --- |
| RTS/CTS/DATA/ACK airtime at cfg PHY | `airtime_ms(sf, bw, cr, preamble, len)` | `lib/core/airtime.h:37` |
| `cts_to_data_gap_ms` | 5 | `protocol_constants.h:133` |
| `rts_max_retries × rts_busy_retry_ms` | 2 × 30 | `:135`, `:134` |
| the MAC CTS-wait window | `start_rts_timeout()`: `delay = (base << shift) + 2*slop + 1`, `shift = min(attempt, 2)`, `slop = rx_window_slop_ms(routing_sf)` (0 on host/sim; ≈53 ms per turnaround on metal) | `lib/core/node_mac.cpp:2337-2386` |
| the MAC ACK-wait window | `start_ack_timeout()`: `base = airtime(DATA, 18 + inner_len) + airtime_routing_ms(3) + slop(sf) + slop(routing_sf)`, armed at `base + 2` | `node_mac.cpp:2389-2410` |
| one requeue, FIRST price | `cascade_requeue_base_ms` 5000 (⛔ not the 30 s cap `:274`, not `send_defer_ttl_ms` `:294`) | `:273` |
| ceiling | `e2e_ack_deadline_xl_ms − 1` | `:780` |
| the maximum `TERMINAL{scheduled}` length | 26-B authenticated response envelope + result code + delay detail — from Slice 2's layout; 0e uses the design's §8.7/§8.9 sizes and says so | design §8.7, §8.9 |

Shape: a host computation (a `tools/` script or a native case) that evaluates the formula for the configured PHY
(the ruled default SF/BW/CR of the static plane) with `slop = 0` (host) and with the bench-measured slop (metal
note), publishing budget / floor / default (2×) / ceiling; a control proving the default moves when any input moves.
⚠ These are the FIRST numbers; the owner accepts the concrete default after seeing them (design §13).

## 5. Gate and fence

- Fence: `tools/` (the generator, its test, the ABI-probe extension, the budget script), `test/` (fixtures,
  candidate-type header, cap derivation cases), the evidence file, the tracked inventory output. ⛔ No `lib/`, `src/`,
  `platformio.ini`, BASELINE, docs. No capacity, no cfg, no timer lands.
- Native (new cases; PIN re-sync); corpus 36/36 by construction (no production change) — run once with the relink
  control expected at ZERO actions and stated honestly; ABI probe in full + the candidate measurements on all three
  ABIs; ruled board pair byte-identical to the committed state; tools sweep (+ the new `test_*.py`); checkers;
  `git diff --check`.
- Bench: none.
- Held for the Author: the inventory file's landing location, the design's §15/§13 numbers (from "to be measured" to
  the measured table), and the two owner rulings' input sheets (classification; capacity/partition).

## 6. Parallelism with 0d

Disjoint fences (0d: `lib/core/node_hashlocate.cpp` + `node_mac.cpp` comment + `test_dual_layer.cpp`; 0e: `tools/`,
`test/` new files). Dispatchable in parallel in isolated worktrees; the second to land rebases its evidence on the
first's HEAD (native PIN moves twice — each evidence states its own before/after).
