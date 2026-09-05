<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 1 — Quality-Agent pre-check ledger (2026-09-05): the feature-boundary scaffold

Authority: design §19 item 1 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:1655-1660`),
§19.1 gate row `| 1 | lib/core/mr_features.h plus legacy compile owners | 36/36 unchanged; both endpoint-disabled
builds and ruled pair | none |` (:1763), §11 capability definitions (:1262-1270), design ruling 15 = R-RA-8 (:1933),
R-RA-17 (rulings ledger :202: exclusivity scoped to `MR_PROFILE_*`; the host compiles BOTH). Verified at
`HEAD b942c37` ("0c"), tree clean. Every `file:line` moves — re-verify (V2). Hypotheses, not authority.

## 0. What Slice 1 is, in one line

Add the two endpoint capabilities `MR_FEAT_RADMIN_CLIENT` / `MR_FEAT_RADMIN_ACCEPT` to `lib/core/mr_features.h`,
derived from the existing profiles so every BOARD build is exactly one of `{1,0}` (mobile) or `{0,1}` (static,
gateway) and the HOST (native + lus) is `{1,1}`; a board build with both or neither fails to compile; keep
`MR_FEAT_REMOTE_MGMT` alive for the legacy issuer/acceptor until Slice 9; add NO consumer and NO v2 wire behaviour.
A header-only, RAM-inert, corpus-inert scaffold whose whole value is the compile-time matrix and its controls.

## 1. Source facts

| fact | anchor |
| --- | --- |
| the header: profiles → `MR_FEAT_*`: `MR_PROFILE_GATEWAY` → TEAM 0, MOBILE 0 (:11-16, keeps REMOTE_MGMT 1 "a gateway is exactly the infra you can't reach physically"); `MR_PROFILE_MOBILE` → REMOTE_MGMT 0 (:23-25); defaults all 1 except OLED 0 (:27-45); dependency `#error` idiom (:47-50: TEAM requires MOBILE) | `lib/core/mr_features.h` |
| ⚠ V1 drift: the header's own pointer "See docs/superpowers/specs/2026-07-12-firmware-feature-split.md" names a file that is NOT in the repo (`ls docs/superpowers/specs` has no feature-split spec) — correct the pointer (or state the spec is absent) while touching the file | `mr_features.h:7` |
| the profile → env matrix: gateway envs `gateway`, `gateway_heltec`, `gateway_heltec_v4`, `gateway_esp32s3` (`-DMR_PROFILE_GATEWAY -DMR_N_LAYERS=2 -DMR_GATEWAY_BUILD=1`, :450-465); mobile envs `xiao_mobile`, `heltec_mobile`, `heltec_v4_mobile`, `xiao_esp32s3_mobile` (`-DMR_PROFILE_MOBILE`, :518-540); ⚠ NO-PROFILE board envs = STATIC products: `xiao_sx1262`, `heltec_v3` (OLED), `heltec_v4` (OLED), `xiao_esp32s3`, `production` (extends xiao_sx1262, `-DMR_CONSOLE=0`) — today they get REMOTE_MGMT 1 by default = "managed" | `platformio.ini` |
| the host: `native` = `-DMESHROUTE_NATIVE=1 -DMR_N_LAYERS=2`, NO `ARDUINO`, no profile → all defaults; the lus sim compiles `lib/core` with `MR_N_LAYERS` 1 (normal) / 2 (gw variant), no profile, no `ARDUINO` | `.pio/build/native/idedata.json`; `/home/staszek/lora-universal-simulator/CMakeLists.txt:108-114` |
| the existing board/host discriminator already used by the device headers: `defined(ARDUINO)` (`device_ble.h:24`, `device_rng.h:23`) | `src/device_ble.h`, `src/device_rng.h` |
| legacy `MR_FEAT_REMOTE_MGMT` owners (must compile UNCHANGED; Slice 9 deletes): `lib/core/node.h:104` (admin accessors), `:2900-2904` (`_admin_pubkey[32]`, `_admin_counter_floor`, `_admin_provisioned` — Node LAYOUT depends on it), `:2905` `_remote_inbound` UNCONDITIONAL; `src/firmware_remote.{h:33,cpp:22/162/192}`, `src/firmware_config.{h:223,cpp:2412}`, `src/fw_context.h:116`, `src/fw_main.cpp:89/92/96/432/1673`, `src/firmware_commands.cpp:1204` (password/unlock/lock arms), `src/firmware_help.h:80/90/120` (their index names) | grep `MR_FEAT_REMOTE_MGMT` |
| instruments that hold a PROFILE TABLE keyed on the legacy macro: `tools/gen_command_inventory.py:203-209` (six profiles: MR_N_LAYERS / MR_FEAT_MOBILE / MR_FEAT_REMOTE_MGMT / MR_FEAT_OLED), mirrored by `tools/probe_console_sink/run.sh` PROFILES and `ownership.py`, `tools/test_probe_console_sink.py:264`, `tools/probe_inbox_verbs/transcript_main.cpp:110` | those files — Slice 1 adds NO column (no command depends on the new pair until Slice 6's `remote` verb) |
| `sizeof(Node)` is pinned per ABI (native `static_assert` 222072 at `node.h:3958`; gateway/ARM 148680; heltec_mobile/Xtensa 117912 — ⚠ my first spelling swapped the two labels; corrected per B305, the probe prints columns native / heltec_mobile / gateway) and the layout already varies with `MR_FEAT_REMOTE_MGMT` — Slice 1 adds NO Node state | `lib/core/node.h`, `tools/probe_board_abi.py` |
| no `MR_FEAT_RADMIN*` / `RADMIN_` token exists in `lib/`, `src/`, `test/`, `tools/` today (the 0e fixture header `test/radmin_0e_candidate_types.h` names roles in comments only) | grep |
| the nine native TUs that read `MR_FEAT_*` today (`test_node_query`, `test_node_team_seen`, `test_dual_layer`, `test_node_join`, `test_firmware_ui_status`, `test_firmware_ui_model`, `test_node_channel`, `test_node_r3`, `test_node_hashlocate`) — none reads the legacy remote macro; none needs to move | grep `MR_FEAT_` test/ |
| the census's env set is DERIVED (`derive_oled_envs`, `tools/warning_census.sh:104`) — six OLED envs; a header-only change must leave all six at their pins | that runner |

## 2. The derivation — and the ONE decision it needs (§4)

Today's single switch and the ruled pair coincide on every real build: `REMOTE_MGMT 1` ⇔ "managed" ⇔ `ACCEPT`;
`REMOTE_MGMT 0` (mobile) ⇔ "managing" ⇔ `CLIENT`. So the scaffold is:

```
MR_PROFILE_MOBILE                         → CLIENT 1, ACCEPT 0
MR_PROFILE_GATEWAY                        → CLIENT 0, ACCEPT 1
board build with NO profile (static)      → CLIENT 0, ACCEPT 1   ⚠ needs the host/board discriminator (§4.1)
host (native, lus: no ARDUINO, no profile)→ CLIENT 1, ACCEPT 1   (R-RA-17's QA reading, owner-accepted)
```
plus the checks: `#error` when a BOARD build has both or neither (R-RA-17), and the consistency pin
`ACCEPT == MR_FEAT_REMOTE_MGMT` on every board build (the legacy switch and the new pair must agree until Slice 9
deletes the switch — a divergence would mean a build that is "managed" by one flag and not by the other).
Hosts are the ONE place where `CLIENT 1` coexists with `REMOTE_MGMT 1`, by design.

## 3. Gate and fence

- Fence: `lib/core/mr_features.h` (the pair, its derivation, the `#error`s, the consistency assert, the drifted
  pointer comment); ⛔ NO consumer anywhere (no `#if MR_FEAT_RADMIN_*` in code yet), no `platformio.ini` change if
  §4.1(a) is chosen, no `src/`, no Node state, no test moves. Plus ONE executable instrument for the matrix (the
  header compiles on the host under each configuration: `tools/probe_features/` — a tiny TU included with
  `-DMR_PROFILE_MOBILE`, `-DMR_PROFILE_GATEWAY`, `-DARDUINO=100` (no profile = static board), and bare (host),
  printing the six `MR_FEAT_*` values + the pair, asserting the matrix above; controls: both-set / neither on a
  board → the `#error` MUST fire (⚠ define the classification up front: for THESE controls a compile failure IS the
  expected RED, the opposite of every other probe's rule); a reversed derivation → RED; the consistency assert
  dropped → RED) + its auto-discovered `tools/test_probe_features.py`. The brief may instead extend an existing
  instrument that already compiles `mr_features.h` per profile (`probe_console_sink` compiles six profiles) — name it.
- Native: unchanged (post-0c pin 2610 / 110269 / 0) unless a native case is added (`test/` may gain ONE
  TU asserting the host matrix `{1,1}` + `REMOTE_MGMT 1` — optional; the probe is the matrix owner).
- Corpus: 36/36 by construction, BUT the lus REBUILDS (a `lib/core` header changed) — report actions + md5; every
  stream byte-identical; s18 keystone from BASELINE.md. A mover = STOP.
- ABI: `sizeof(Node)` unchanged on all three (no Node state). Boards: RAM ±0, flash ±0 EXPECTED on both
  (unused macros) — the inert proof; any movement is a STOP. Census 6/6 unchanged. Inventory unchanged (no `src/`
  change) — `--check` PASS without regeneration. Standing probes at their pins.
- Mutation selectors: `lib/core/mr_features.h` has NO battery target and no dependency battery ⇒ both selectors
  EMPTY; the probe's controls are the owning gate (state it).
- Bench: NONE (compile-time only; the gate row says `none`).

## 4. Owner decision — RULED 2026-09-05 (R-RA-26): 4.1 = (a) `defined(ARDUINO)`, no `platformio.ini` change; 4.2 = pure scaffold

- **4.1 the no-profile board envs.** `production`, `xiao_sx1262`, `heltec_v3`, `heltec_v4`, `xiao_esp32s3` set no
  `MR_PROFILE_*` and are static products. (a) discriminate host vs board with `defined(ARDUINO)` (the idiom the
  device headers already use): board + no `MR_PROFILE_MOBILE` → ACCEPT; host → both — no `platformio.ini` change,
  and it mirrors exactly how `MR_FEAT_REMOTE_MGMT` is derived today; (b) add `-DMR_PROFILE_STATIC` to the five
  envs — explicit, but widens the fence to `platformio.ini` and the ten-env build matrix. **QA recommends (a).**
- **4.2** confirm Slice 1 adds NO consumer (pure scaffold): the design's "behaviour controls" are the compiled
  matrix + `#error` controls, not runtime behaviour. (Slice 1b is the first consumer.)

Base commit at dispatch = the owner's commit after this ledger + the Author's brief (name it; STOP if different).
