# MeshRoute port plan (re-baselined 2026-05-28)

> **This supersedes `PORT_NOTES.md`.** PORT_NOTES was written when the
> scaffold was first laid down; three things have changed since and
> invalidate parts of it (see "What changed" below). This document is
> the current authoritative plan. The §8 open decisions are **signed off**
> and the plan is **in execution** — see the **Status** section below for
> where the port actually stands; the iteration tables in §4 are annotated
> with what's done.

---

Implementation notes (status 2026-06-06):
1. DATA frame — byte 0 of the inner is a **payload-flags** byte. ✅ DONE (`CROSS_LAYER`/`H_ANSWER`/`AUTHORITATIVE`/`CRYPTED` + `DST_HASH`; `frame_codec`).
2. Node names — carried in the **identity record**, not the wire hot path. ✅ DONE (`name` in `/mrid`, `cfg set name`; app-level).
3. Prevent joining a leaf without a proper join — **RESOLVED; the old "BCN crypted with a leaf key" idea is REJECTED.** Threat model is honest-node / anti-**misconfiguration**, not anti-adversary (the unique sync word `0x4D` already keeps foreign protocols off the air). The gate is the **beacon config-fingerprint** `{lineage_id, epoch, config_hash}`: a node with wrong/absent config computes a mismatched fingerprint and **self-isolates** — no signing, no group/leaf key. There is deliberately **no cryptographic membership secret** — "join" = sync to the correct config; a correctly-configured node *is* a member. Per-message BCN signing was rejected (≈ +164 ms/beacon at SF8, no adversary). On-air crypto is **DM-only** (X25519 ECDH, opt-in). Refs: `docs/specs/2026-06-05-identity-leaf-membership-join-design.md` §0/§3/§6.
4. Join carries a **1-byte wire-version** (wire-compat, NOT node version) — still planned, lands with R6 leaf-config join.

Discovery
1. req_sync_min_routes=8 caused constant Q frames — note retained (tune in/with R6).

---

## Status — where the port stands (2026-06-06)

**The stable same-layer core + identity + the node_id (short-address) half of join are DONE and FLASHABLE.** XIAO nRF52840 + Heltec v3 builds are green; on metal the node beacons and `send <id> <text>` delivers a DM. Tracks:

| Track | State |
|---|---|
| Codec **C0–C6** — all 10 §10 cmd-nibble frames | ✅ done |
| HAL **H0–H3** — timer wheel, `Sx1262Radio`, `device_hal` (RX polled; PreambleDetected IRQ tried + reverted on metal) | ✅ done — **flashable** |
| Sim-integration **S0–S3** — `FirmwareNode` in-loop beside `ScriptedNode` + the lua-vs-meshroute differential harness | ✅ done |
| Behaviour **R1–R5** — beacon emit, DV routing (K=3, prune), MAC RTS/CTS/DATA/ACK/NACK, throttle/triggered/cascade/LBT, Q REQ_SYNC + channel gossip | ✅ done |
| **Hash-locate plane (H, A0–D)** — `id_bind`, soft/hard resolve, hash-bind response, cache-on-pass, send-by-hash + park/verify-on-use | ✅ done |
| **Identity (Slice A/A2)** — Ed25519+X25519 from one seed (vendored monocypher), `key_hash32 = ed_pub[:4]`, `/mrid` NV + HW-RNG + `cfg set name` / `regen`; the sim derives the same value (single source feeds both engines) | ✅ done |
| **node_id auto-assignment (DAD)** — the SHORT-ADDRESS half of join: picker excludes `_id_bind`∪`_rt`∪defer-queue + **claim-after-listen** (L1); claim→guard→adopt; **key-only** tiebreak (§6); shared-neighbour mediated heal (L2a); delivery-driven **verify-on-delivery + H-redirect + gated heal** (L2c, `DST_HASH`); `node_id`+`joined` in NV (reboot-reclaim) | ✅ done |
| **R6 — leaf-config join** (the CONFIG half): beacon fingerprint `{lineage_id, epoch, config_hash}` + peering/adopt filter + `CONFIG_PULL` (learn `data_sf_list`/`leaf_name`/`duty_cycle`) + `J wire_version` | ⏳ **← NEXT** (design: identity spec §3–§5; wire: frames.md P1/P4; plan §9 below) |
| **Dynamic config write path** — operator-gated `epoch` bump (LWW by `key_hash32`); leaf-death/recreate backstop | ❌ after R6 (R6.3) |
| **E2E DM crypto** — X25519 ECDH → AEAD on the DATA body (`CRYPTED` b3); needs `id_bind` full-pubkey resolution | ⏳ wire reserved (`DST_HASH`/`CRYPTED`); AEAD + redirect-by-hash deferred to this slice |
| **R7 Gateway / cross-layer** (multi-leaf) | ❌ deferred follow-on (consumer of the H plane's cross-layer trigger) |
| **R8 Mobile + asymmetric** | ❌ not started |
| **App layer** — inbox persistence, known-nodes directory, channel subscriptions | ❌ net-new (per-leaf crypto **removed** — rejected; on-air crypto is DM-only) |
| **D0** first on-metal frame (BCN + DM) | ✅ done |
| **D1** two-board over-the-air round-trip | ⏳ bench-pending (on-metal, yours) |

**Verification baseline (the regression bar):** native doctests **220/220**
(identity + node_id DAD added their cases); the **6 MAC differential gates**
(lua-vs-meshroute, via `test/run_tests.sh`) PASS; **channel 6/6 + discovery 2/2**
both engines; identity/join sim gates **t90** (seed→key_hash32 parity), **t91**
(DAD distinct-pick), **t92** (forced collision → heal → distinct), **t93** (s18
dense → meshroute join, staged with start_at/dies_at) all PASS; hash-locate gate
**t89** PASS; both device builds green; telemetry stripped on-device via
`MR_TELEMETRY` (`-DMESHROUTE_NO_TELEMETRY`). The 6 pre-existing **Lua-engine**
fails (join/gateway/long-chain) are unchanged — no C++ regressions.

**Immediate next: R6 leaf-config join.** The `node_id` (short-address) half of
join is DONE (DAD, above). What remains is the **CONFIG** half — a joiner
learning `data_sf_list`/`leaf_name`/`duty_cycle` from the leaf, plus the
misconfiguration gate. **Crypto is decided** (no BCN signing / no per-leaf key —
fingerprint + honest-node; on-air crypto is DM-only X25519). See §9 (rewritten).

---

## 0. What we are doing

Port the *behaviour* of `scenarios/dv_dual_sf.lua` (the LoRa-mesh
firmware model in the sibling `lora-universal-simulator` repo) into real
C++ firmware here, targeting the **SX1262** radio on a **Seeed XIAO
nRF52840** (primary) and **Heltec WiFi LoRa 32 V3 / ESP32-S3** (backup).
Slow, delicate, one reviewable step at a time.

**Source of truth, frozen.** We port from `lora-universal-simulator`
HEAD `6d66a4f "Version 0.1 - before migration"` — a deliberately tagged
migration baseline, working tree clean. **We do not modify
`dv_dual_sf.lua`**; it is the frozen spec. If the Lua evolves later we
re-baseline against a new tag, explicitly.

---

## 1. What changed since PORT_NOTES (why this re-baseline exists)

1. **The C++ wire format is NOT the current Lua wire format.**
   `ROADMAP §10` (refreshed 2026-05-28) is explicit: the cmd-nibble
   wire-format v2 (`cmd 4hi | flags 4lo` in byte 0) is *deferred to the
   C++ port phase*, to be shipped **frame-by-frame as each frame is
   reimplemented in C++**. The Lua stays on the literal-tag-byte format;
   the C++ side lands the cmd-nibble layout. So Lua and C++ wire bytes
   **diverge by design.** → PORT_NOTES' "round-trips byte-for-byte
   against Lua" success criteria are wrong and are replaced (see §4).

2. **The HAL is a MeshCore lift, not a from-scratch build.** A complete
   MeshCore clone (v1.10, MIT) lives at `/home/staszek/MeshCore`. Its
   radio layer is cleanly decoupled from MeshCore's routing and is
   directly reusable for "SX1262 transmit/receive/CAD/SNR + correct pins
   on XIAO/Heltec." We reuse it and build only the two pieces it lacks.

3. **`PROTOCOL.md` (and the Lua top-of-file wire table) are stale in
   places** — e.g. RTS `sf_bitmap`→`sf_index` (`c20585b`), CTS/ACK/NACK
   byte widths, route-entry width, E2E-ACK body width. **The pack/parse
   code in `dv_dual_sf.lua` is authoritative**, not the prose docs.

---

## 2. Verification strategy (revised)

Two complementary levels, because the wire diverges from Lua:

**(a) Per-frame codec tests — pinned to the §10 spec, not to Lua hex.**
Each `pack_*`/`parse_*` gets: a **round-trip** test (`parse(pack(x)) ==
x`), a **golden-hex** test pinned to the §10 byte layout (hand-derived
from the §10.3 tables, reviewed), and a **two-node interop** test
(node A's `pack` decodes correctly under node B's `parse`). The *field
semantics* (which fields exist, body section order, endianness, bit
packing intent) come from the Lua `pack_*`/`parse_*`; the *byte
positions* come from §10.

**(b) Behavioural differential test — semantic NDJSON, same seed.**
A scenario JSON run with the Lua model as node logic, then with the C++
firmware itself as node logic (run *inside this same simulator* — see
§2.1), should produce the **same NDJSON event sequence** for the same
seed. The events are semantic (route decisions, deliveries, RTS
attempts), not raw bytes, so this survives the wire divergence —
**with one caveat:**
§10 changes some frame *sizes* (RTS 8→7, H 8→6/7, F 6→5/6) and
size→airtime→timing→event ordering. §10.4 notes RTS 8→7 is
**airtime-neutral at SF8** (same `pay_sym=23` symbol bucket), so parity
likely holds; **H/F size deltas must be checked against the SF8 symbol
buckets** before we rely on bit-identical NDJSON (this is the wire-decision
spike, the immediate next step after sign-off).

**Determinism is the linchpin of (b)** (Principle 7, ROADMAP §11.3):
- `self:rand(lo,hi)` → `std::mt19937` seeded by `simulation.seed`,
  `std::uniform_int_distribution(lo, hi-1)` for `[lo,hi)`, **draws issued
  in identical call-site order.**
- `pairs()` over ~17 order-observable sites → `std::map` or
  sort-before-iterate (never `std::unordered_map`).
- Q4 int16 fixed-point dB (no FPU) must reproduce routing scores
  bit-for-bit (rounding + saturation at `Q4_MAX=32767`, EWMA `alpha=5`).

### 2.1 How the C++ side runs: the firmware in-sim (decided 2026-05-29)

We do **not** reimplement the simulator's PHY in C++. We run the **real
MeshRoute firmware in-loop inside the existing `lora-universal-simulator`**,
against its existing `SimRadio` + `CollisionModel` — the same trusted
physics the Lua model runs against (and which, per
`core/radio/SimRadio.h` "Originally derived from meshcore_real_sim" /
`CollisionModel.h` "lifted from Orchestrator.cpp isDestroyedBy", already
descends from the elder `~/meshcore_real_sim` project). This is the
pattern `meshcore_real_sim` proved — running unmodified firmware as the
node logic against a virtual radio — but **simpler for us**, because we
own MeshRoute:

- `meshcore_real_sim` had to run *unmodified* MeshCore (Arduino firmware
  full of device globals), so it needed a per-node global-pointer swap
  (`NodeContext::activate()`) and a whole Arduino `shims/` layer
  (SimRadio/SimClock/SimRNG/FS/Arduino.h/crypto). **We need none of
  that.**
- Instead, `lib/core` is **platform-neutral C++20 depending only on an
  abstract `meshroute::Hal` interface** (the 18 `self:*` + 5 callbacks of
  §3). The protocol core therefore has **two HAL backends**:
  - **Sim backend:** a `FirmwareNode` added to
    `lora-universal-simulator/orchestrator/runtime` *beside*
    `ScriptedNode` (via a small `INode` refactor), implementing the HAL
    by delegating to the sim's existing `SimRadio` / `VirtualClock` /
    `TimerWheel` / `LbtModel` / airtime-log / shared `mt19937`. The
    firmware runs in-loop against the real PHY.
  - **Device backend:** the same HAL over the MeshCore-lifted
    RadioLib/SX1262 wrapper + millis/timer + flash, on the XIAO (§3).

So level-(b)'s "C++ side" is the firmware itself, in the trusted sim —
**not a second simulator to build**. The work is the `INode` refactor +
the `FirmwareNode` adapter + a per-node `engine` config field, not a PHY
reimplementation. The differential harness is "run scenario X with
`engine:"lua"`, then `engine:"meshroute"`, diff the NDJSON" — the
`meshcore_real_sim/mixed_firmware_validation` sweep repurposed.

---

## 3. The HAL — reuse from MeshCore (`/home/staszek/MeshCore`, MIT)

The firmware↔host contract we must satisfy is **18 `self:*` methods + 5
callbacks** (full table in the sim's `LuaHost.cpp`/`ScriptedNode.cpp`).
This is expressed in C++ as an **abstract `meshroute::Hal` interface that
`lib/core` depends on**, with **two implementations**: the **sim
backend** (`FirmwareNode`, §2.1 — delegates to the simulator's services)
and the **device backend** (this section — MeshCore's radio stack on the
XIAO). MeshCore's stack covers most of the device backend.

**Reusable as-is (clean, routing-decoupled 3-tier stack):**
- `src/Dispatcher.h` — `mesh::Radio` interface, `mesh::MillisecondClock`
  (→ our `now()`), the duty-cycle token-bucket + LBT/CAD engine.
- `src/helpers/radiolib/RadioLibWrappers.{h,cpp}` — RadioLib glue
  (transmit/startReceive/recvRaw/getSNR/getRSSI/isChannelActive).
- `src/helpers/radiolib/CustomSX1262.h`, `CustomSX1262Wrapper.h`,
  `SX126xReset.h` — the SX1262 driver (one-call `std_init`).
- `src/helpers/ArduinoHelpers.h` — `ArduinoMillis`, `StdRNG`.
- `src/MeshCore.h` — `MainBoard`/`RTCClock`/`RNG` base classes.
- Board pin maps: `variants/xiao_nrf52/*` (NSS=D4, DIO1=D1, RST=D2,
  BUSY=D3, RXEN=D5, DIO2-as-RF-switch, TCXO 1.8 V, TX 22 dBm) +
  `src/helpers/NRF52Board.{h,cpp}`; backup `variants/heltec_v3/*` (NSS=8,
  DIO1=14, BUSY=13, SCK=9/MISO=11/MOSI=10) + `src/helpers/ESP32Board.*`.

**Free bonus:** MeshCore's `Dispatcher` already implements the
duty-cycle ledger (→ `airtime_used_ms`/`oldest_tx_end_ms`) and RSSI-based
LBT/CAD (→ `channel_busy_until`).

**Two gaps we must build ourselves:**
- **A true SX1262 PreambleDetected DIO IRQ.** MeshCore only *polls*
  `getIrqFlags() & (HEADER_VALID|PREAMBLE_DETECTED)` and wires DIO1 to
  RxDone/TxDone. Our `on_preamble_detected` (beacon-throttle witness)
  wants the real IRQ → add `setDio1Action` + `setDioIrqParams` for
  PREAMBLE_DETECTED.
- **Callback `after()`/`cancel()` timers.** MeshCore is a polled
  absolute-timestamp queue. We build a small timer wheel over
  `getMillis()` (the Lua firmware uses `after()` as its *only* scheduling
  primitive, 61 sites; all periodic loops are self-rescheduling closures).

**Non-issue:** MeshCore's radio exposes no `src` on receive (it's
byte-oriented). That's correct — the sim's free `meta.src` was an
artifact; our frames carry `src` in-header and the firmware parses it.

**Attribution:** MeshCore is **MIT** (`/home/staszek/MeshCore/license.txt`,
© 2025 Scott Powell / rippleradios.com), no per-file headers. We vendor
the reused files under a clearly-marked dir, add a one-line origin header
to each (`// Adapted from MeshCore (c) 2025 Scott Powell, MIT`), and copy
MeshCore's `license.txt` into our tree + cite it in `NOTICE.md`. (Our
BSD-3 project incorporating MIT files is fine.)

---

## 4. Revised iteration plan

Codecs are pure functions (no HAL), so the **codec track** and the
**HAL track** can proceed independently and be reviewed separately. The
**runtime/integration track** depends on both.

### Codec track — §10 cmd-nibble layout, simplest frame first
| It | Scope | Success criterion |
|---|---|---|
| C0 | Codec foundation | byte-buffer/`span` abstraction + cmd-nibble byte-0 helpers (`cmd<<4|flags`), LE/BE primitives (`u16/u32_le`, channel-id BE) |
| C1 | CTS + ACK (3 B) | round-trip + §10-hex-pinned + 2-node interop; proves the pattern on trivial fixed frames |
| C2 | RTS + NACK + Q | same; RTS exercises `sf_index`, M-broadcast extension, `leaf_id` |
| C3 | H + F floods | same; TTL/forward fields |
| C4 | J family (4 opcodes) | per-opcode fixed lengths enforced |
| C5 | **Beacon** | the hard one: schedule block + route entries + 32 B seen-bitmap + ext TLVs + differential (dirty-first) emission |
| C6 | DATA | 14+n+4 header + 3 inner layouts (normal / E2E-ACK / M) + GW-envelope & hash-bind sub-formats |

**Status (2026-06-05): C0–C6 all ✅ done.** The DATA inner reframed to the
universal `[payload-flags][origin][body]` prefix (the always-zero `src_addr_len`
slot); H grew to 8 B (the HARD-query flag); the hash-bind sub-format landed with
the H plane. `J` codecs are done; the **J CLAIM/DENY runtime landed with node_id DAD (R6a)** — the leaf-config J handling (`CONFIG_PULL`, `wire_version`) is R6b.

(Order is reversed vs PORT_NOTES, which started at BCN — we de-risk the
new codec+test harness on a 3-byte frame before the most complex one.)

### HAL track — MeshCore lift + the two gaps (reviewed jointly)
| It | Scope | Success criterion |
|---|---|---|
| H0 | Vendor + attribute MeshCore HAL files; `IClock` (arduino-millis + virtual-clock) | both compile under `xiao_sx1262` + `native` |
| H1 | `after()`/`cancel()` timer wheel over `getMillis()` | unit-tested ordering + cancel |
| H2 | PreambleDetected DIO IRQ | bench-verified on XIAO (later, on-device) |
| H3 | Radio facade matching our HAL (`tx(opts)`, `set_rx_sf`, `channel_busy_until`, duty ledger) | drives a raw frame on the native stub |

**Status (2026-06-05): H0–H3 all ✅ done — the device backend is flashable.**
`fw_main` runs the real node loop (not the heartbeat skeleton); `Sx1262Radio` +
`device_hal` + the `after()`/`cancel()` timer wheel are on metal. RX reverted
to polling (the PreambleDetected IRQ didn't fire reliably on the XIAO — see the
device bring-up notes). A device NV (flash KV) store is still TODO — needed when
R6 join lands lease/claim-epoch persistence (§Open questions).

### Sim-integration track — run the firmware in-loop in `lora-universal-simulator` (§2.1)
| It | Scope | Success criterion |
|---|---|---|
| S0 | `INode` refactor in the target `SimController` | `ScriptedNode` reimplemented as one `INode`; `_nodes` is `vector<unique_ptr<INode>>`; per-step dispatch polymorphic; Lua suite still green |
| S1 | `engine` per-node config field + `FirmwareNode` skeleton | `engine:"meshroute"` node constructs, runs `loop()`, draws timers/rng/airtime from the host (reuse ScriptedNode machinery) |
| S2 | `meshroute::Hal` interface + sim backend wiring | a `lib/core` node TXes/RXes a raw frame through the sim's `SimRadio`; static-linked (no dlopen yet) |
| S3 | Differential harness | `engine:"lua"` vs `engine:"meshroute"` on a scenario → NDJSON diff tool; repurpose `mixed_firmware_validation` sweep |

**Status (2026-06-05): S0–S3 all ✅ done.** `FirmwareNode` runs in-loop beside
`ScriptedNode`; the differential gates (`tools/dm_diff.py` / `dm_diff_band.py`)
diff lua-vs-meshroute delivery per scenario. The seam mirrors SimController's
injected `_sim_*` config keys (`duty_cycle` etc.) — `bw`/`cr`/`warmup` is a known
latent differential gap to watch.

### Behaviour track (depends on codecs + the sim-integration track)
| It | Scope | Success criterion |
|---|---|---|
| R1 | Single-node beacon emit | matches Lua `beacon_tx` cadence/events |
| R2 | Routing table + DV merge (K=3, 3-cycle prune) | t10/t12 equivalents |
| R3 | MAC RTS-CTS-DATA-ACK | t01 equivalent |
| R4 | Throttle + triggered beacons / F1 blind window / cascade requeue | t29/t42/t14/t20/t26 equivalents |
| R5 | Q frames / REQ_SYNC / channel gossip | t30/t39/t65-69 equivalents |
| **R5.5** | **Hash-locate (H) plane** — id_bind table, soft/hard resolve, hash-bind response, cache-on-pass, send-by-hash + verify-on-use *(not in the original plan; added 2026-06-04)* | t89 + the 6 MAC gates green |
| R6a | **node_id DAD** (short-address half): claim-after-listen, claim/guard/adopt, key-only heal (§6), L2a mediator + L2c redirect, NV reboot-reclaim | t91/t92/t93 ✅ **done** |
| R6b | **leaf-config join** (config half): beacon fingerprint + peering filter + `CONFIG_PULL` + `J wire_version` | ⏳ **NEXT** — §9 |
| R7 | **(follow-on) Gateway + cross-layer** | s09/s10 — see scope note §5 |
| R8 | Mobile + asymmetric | s07/s08 |
| D0 | First on-device: one BCN over the air on XIAO | — |
| D1 | Two-board RTS-CTS-DATA-ACK between two XIAOs | — |

**Status (2026-06-06): R1–R5 + R5.5 ✅; Identity (Slice A/A2) ✅; node_id DAD
(R6a — `on_recv` J CLAIM/DENY, `on_command(join)` → claim-after-listen, L2a/L2c,
NV reboot-reclaim) ✅; D0 ✅ (BCN + DM on metal).** **R6b (leaf-config join:
beacon fingerprint + `CONFIG_PULL` + `wire_version`) is the NEXT step** — see §9.
**R7 gateway/cross-layer + R8 mobile/asymmetric = not started.** D1 (two-board
over-the-air) is bench-pending on real hardware (yours).

---

## 5. Scope: stable core first, gateway/cross-layer as a follow-on

Maturity is uneven (per `DELIVERY_ANALYSIS.md`, 2026-05-28):
- **Mature & test-guarded** (low/moderate port risk): same-layer DV
  routing (~92-97%), channels (~94-98%, 0 leaks), addressing/join,
  mobility, anti-spam, e2e-ACK, plus the MCU-portability hardening
  (bounded-state caps, Q4 fixed-point, RNG contract).
- **Structurally unfinished & actively tuned** (~60-78%, a long list of
  reverted experiments): cross-layer/gateway delivery.

Recommendation: target the **stable same-layer core** for the first
working firmware (sim-integration S0-S3 + behaviour R1-R6 + device
D0-D1). Treat **gateway/cross-layer (R7) as a follow-on** — porting it
now inherits a moving target.

**Status (2026-06-05):** achieved except **R6 join** (codec-only, the next
step) and **D1** (two-board bench, on-metal). S0–S3 + R1–R5 + the H plane +
D0 are done. R7 gateway/cross-layer remains the follow-on as planned.

---

## 6. Corrected wire reference (code is authoritative)

Known stale spots in the Lua top-of-file table / `PROTOCOL.md` vs the
actual `pack_*`/`parse_*` code at `6d66a4f` — the port follows the code:
- **RTS** byte 6 = 2-bit `sf_index` (top 2 bits) + 6 rsv, *not* an 8-bit
  `sf_bitmap` (changed `c20585b`).
- **CTS** = 3 B (`'C'` | ctr_lo|sf|already_received | to), not 2 B.
- **ACK** byte 1 = ctr_lo(4) | budget_hint(2) | snr_bucket(2), not a
  4-bit snr bucket.
- **NACK** = 4 B incl. the `to` byte, not 3 B.
- **Beacon route entry** = 4 B (dest|next|score/gw byte|hops full byte),
  not 3 B (PROTOCOL.md §3.1 contradicts itself; `beacon_max_entries`
  math still divides by 3 — verify against `pack_beacon`).
- **E2E-ACK inner body** = 3 B (acked_ctr_lo|acked_ctr_hi|actual_hops),
  not 2 B (header comment is stale; builder writes 3).
- **Magic prefixes** `GW_ENV_MAGIC`/`HASH_BIND_MAGIC` use `\31` =
  **0x1F**, not the characters `'3''1'`.

A full per-frame §10 hex worksheet is produced in the wire-decision spike
(next step after sign-off).

---

## 7. Conventions (carried from PORT_NOTES, still valid)

C++20; `snake_case` one-to-one with the Lua; `-fno-exceptions`/`-fno-rtti`;
no heap in hot paths (fixed-size containers sized by the PROTOCOL caps);
`std::span`/`std::optional`; `std::map`/sort-before-iterate where order is
observable; protocol code in `namespace meshroute::`. RF plan locked in
`platformio.ini` `[common]`: 869.4625 MHz / SF8 / BW125 / CR4/5 / 10%
duty / EU868 g3, preamble 16 sym.

---

## 8. Open decisions — need sign-off before any C++

| # | Decision | Recommendation |
|---|---|---|
| D1 | Adopt §10 cmd-nibble wire in C++ from frame #1 (vs port the current tag-byte layout, migrate to §10 later) | **§10 now.** The port is the intended flag-day; doing it later is a second painful migration; airtime-neutral at SF8. |
| D2 | Codec order: simplest-first (CTS/ACK→…→BCN→DATA) vs Lua-build-order (BCN first) | **Simplest-first** — de-risk the new codec+test harness on a 3-byte frame. |
| D3 | ~~Native differential harness fidelity~~ **RESOLVED 2026-05-29** | **Run the firmware in-loop inside `lora-universal-simulator` as a `FirmwareNode`, reusing the existing trusted PHY** (§2.1) — no PHY reimplementation. The cost moves to the `INode` refactor + `FirmwareNode` adapter (small, well-scoped) instead of a second simulator. |
| D7 | Sim loading model: static-link `lib/core` into the orchestrator (one version) vs `dlopen` `fw_*.so` plugin (multi-version, ports from meshcore_real_sim) | **Static-link first**; adopt the plugin ABI later only if multiple MeshRoute versions must coexist. |
| D8 | `INode` refactor of the target `SimController` — touches `lora-universal-simulator` (not just MeshRoute) | **Yes**, but it's a structure-preserving extraction; the Lua node path must stay bit-identical (full t-suite + s-scenario regression after). |
| D4 | Scope first firmware to the stable same-layer core; gateway/XL follow-on | **Yes.** |
| D5 | MeshCore reuse mechanism: vendored copy of the ~11 files (we'll modify them for the 2 gaps) vs git submodule vs adapter-over-submodule | **Vendored copy**, attributed — we need to edit them (IRQ, facade), and a frozen vendored snapshot is reproducible. |
| D6 | License interaction: MeshRoute BSD-3 incorporating MeshCore MIT files | **Vendored files keep an MIT origin header + `NOTICE.md` entry + bundled `license.txt`.** No conflict. |

---

## 9. Immediate next step — R6 leaf-config join

*(Superseded: the original wire-spike (done) and the DISCOVER/OFFER + lease-age
sketch. The `node_id` short-address allocation is DONE via **DAD** — beacon-listen
+ claim/guard/adopt + key-only heal + L2c redirect — NOT DISCOVER/OFFER, and NOT
lease-age (proven non-convergent under wire staleness; dropped for key-only).
DISCOVER/OFFER are deferred.)*

**Goal.** A fresh node boots knowing only **freq / BW / control-SF** (enough to
hear the control channel). R6 lets it learn the leaf's runtime config and only
then participate, so a misconfigured node self-isolates instead of breaking the
leaf. It composes with the done node_id DAD: on join a node does BOTH — sync the
config (this slice) AND DAD-allocate its `node_id` (done); claim-after-listen
already provides the listen window the config-pull needs.

**Pieces** (design: identity spec §3–§5; wire: frames.md P1/P4):
- **Beacon fingerprint** — every BCN carries `{lineage_id(4) · epoch(2) ·
  config_hash(4)}` as a fixed header **before** the route entries (survives the
  151-B page truncation). `config_hash = trunc(BLAKE2b(canonical(data_sf_list
  [ORDER-significant] ‖ leaf_name ‖ duty_cycle)), 4)`.
- **Peering / adopt filter** (§3.3) — same `leaf_id` AND same `(lineage, epoch,
  config_hash)` ⇒ peer. Different `lineage` ⇒ not my leaf (ignore). Same lineage,
  higher `epoch` ⇒ I'm stale ⇒ pull + adopt. **This filter is the misconfig gate
  — no signing.**
- **`CONFIG_PULL`** — a joiner/stale node pulls the full config for `{lineage,
  epoch}` via `Q CONFIG_PULL`, answered as a routed DATA with the `CONFIG`
  payload-flag (b5). Any member at that epoch may serve it (durability §4.2).
- **`J wire_version`** (1 byte) — wire-compat gate at join (distinct from node
  version); reject a wire-incompatible peer.
- **Leaf creation** — an operator `create leaf` mints a random immutable
  `lineage_id` + sets the config (the founding node).

**Slices:**
- **R6.1** — beacon-fingerprint emit/ingest + peering/adopt filter (the misconfig
  gate; no config transfer yet — uses local/`cfg`-set config). Sim gate: two
  nodes with divergent `data_sf_list` must NOT peer; matching ones do.
- **R6.2** — `CONFIG_PULL` request/response (learn `data_sf_list`/`leaf_name`/
  `duty_cycle` from the leaf) + adopt-on-higher-epoch + `J wire_version`. Sim
  gate: a fresh node joins, pulls config, becomes a member.
- **R6.3** — dynamic config write path: operator-gated `epoch` bump, LWW by
  `key_hash32`; leaf-death/recreate backstop (no authority/handoff).

**Crypto — DECIDED (closed):** no BCN signing, no per-leaf/group key. The
fingerprint is the gate (honest-node, anti-misconfiguration). On-air crypto is
**DM-only** (X25519 ECDH → AEAD, `CRYPTED` b3) and ships as its own E2E slice
(needs `id_bind` full-pubkey resolution). Refs: identity spec §0/§6, top-note #3.

**After R6:** E2E DM AEAD, then R7 gateway/cross-layer (the multi-leaf multiplier
+ the H plane's deferred consumer), then the app layer (inbox / known-nodes /
subscriptions).
